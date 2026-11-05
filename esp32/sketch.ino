// Silo de grãos — ESP32 (borda, a bateria)
// Sente: DHT22 (temperatura e umidade do ar dentro do silo).
// Decide: liga/desliga o ventilador (aeração) com histerese, SEM depender da internet.
// Age: GPIO 26 (LED no Wokwi = relé do ventilador na vida real).
// Fala: só MQTT com TLS (telemetria, comandos, limites e histórico). O broker EMQX encaminha
// cada leitura ao Worker (D1), então o ESP32 não guarda nenhuma chave de nuvem.
// Para economizar bateria o ESP32 passa quase todo o tempo em deep sleep.
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Preferences.h>
#include <mbedtls/md.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <time.h>

#include "secrets.h"   // copie de secrets.example.h (fica fora do GitHub)
#include "certs.h"

// ---------- pinos ----------
#define PIN_DHT 15
#define PIN_FAN 26

// ---------- tempos ----------
#define MODO_DEMO 1    // 1 = tempos curtos p/ apresentação; 0 = valores reais de campo

#if MODO_DEMO
const uint32_t CICLO_S            = 30;      // dorme 30 s    (real: 900 s = 15 min)
const uint32_t JANELA_COMANDO_MS  = 2000;    // fica acordado esperando comandos
const uint32_t MIN_LIGADO_S       = 60;      // ventilador não desliga antes disso (real: 1800)
const uint32_t COOLDOWN_ALERTA_S  = 120;     // (real: 21600 = 6 h)
const uint32_t MANUAL_MAX_S       = 300;     // comando manual expira (real: 3600)
#else
const uint32_t CICLO_S            = 900;
const uint32_t JANELA_COMANDO_MS  = 2000;
const uint32_t MIN_LIGADO_S       = 1800;
const uint32_t COOLDOWN_ALERTA_S  = 21600;
const uint32_t MANUAL_MAX_S       = 3600;
#endif
const int   FALHAS_PARA_ALERTA = 3;          // ciclos seguidos sem leitura válida
const float UMID_CRITICA       = 78.0;       // alerta: umidade perigosa para o grão

// ---------- tópicos: sis1a/<grupo>/<local>/... ----------
#define BASE "sis1a/kauaheric/silo"
#define T_TELEMETRIA BASE "/telemetria"
#define T_ESTADO     BASE "/estado"
#define T_COMANDO    BASE "/comando"
#define T_ACK        BASE "/ack"
#define T_ALERTA     BASE "/alerta"
#define T_LEITURA    BASE "/leitura"   // histórico: o broker encaminha ao Worker/D1
#define T_CONFIG     BASE "/config"    // limites do ventilador (retido, publicado pelo painel)

// ---------- memória que sobrevive ao deep sleep (RTC) ----------
struct Leitura { uint32_t ts; float t; float u; uint8_t vent; };
const int BUF_MAX = 48;                       // 12 h de leituras a cada 15 min
RTC_DATA_ATTR Leitura buf[BUF_MAX];
RTC_DATA_ATTR int      bufN = 0;
RTC_DATA_ATTR bool     ventLigado = false;
RTC_DATA_ATTR uint32_t ventDesde = 0;         // epoch em que ligou
RTC_DATA_ATTR uint8_t  modo = 0;              // 0 auto | 1 forçado ligado | 2 forçado desligado
RTC_DATA_ATTR uint32_t modoAte = 0;           // epoch em que o modo manual expira
RTC_DATA_ATTR int      falhasSeguidas = 0;
RTC_DATA_ATTR uint32_t ultimoAlerta[2] = {0, 0};   // 0=umidade, 1=sensor
// limites (podem ser alterados pelo Worker/KV; valores iniciais são o plano B offline)
RTC_DATA_ATTR float umidLiga = 70, umidDesliga = 65, tLiga = 30, tDesliga = 28;
RTC_DATA_ATTR uint32_t rtcMagic = 0;          // 0 = RTC vazia (energia caiu ou reinício frio): restaura da flash
const uint32_t RTC_MAGIC = 0xA5C0DE01;

DHT dht(PIN_DHT, DHT22);
WiFiClientSecure netMqtt;
PubSubClient mqtt(netMqtt);
Preferences prefs;
uint64_t ultimoIdComando = 0;
float tempC = NAN, umidPct = NAN;
bool leituraOk = false;

// ---------- estado na flash (NVS): sobrevive a reinício frio e queda de energia ----------
void salvaEstado() {   // o NVS só grava o que mudou
  prefs.putBool("vent", ventLigado);
  prefs.putUInt("ventDesde", ventDesde);
  prefs.putUChar("modo", modo);
  prefs.putUInt("modoAte", modoAte);
  prefs.putFloat("ul", umidLiga); prefs.putFloat("ud", umidDesliga);
  prefs.putFloat("tl", tLiga);    prefs.putFloat("td", tDesliga);
}

void restauraEstado() {
  ventLigado = prefs.getBool("vent", false);
  ventDesde = prefs.getUInt("ventDesde", 0);
  modo = prefs.getUChar("modo", 0);
  modoAte = prefs.getUInt("modoAte", 0);
  umidLiga = prefs.getFloat("ul", umidLiga);       umidDesliga = prefs.getFloat("ud", umidDesliga);
  tLiga = prefs.getFloat("tl", tLiga);             tDesliga = prefs.getFloat("td", tDesliga);
  Serial.printf("[NVS] estado restaurado: ventilador=%d modo=%d\n", ventLigado, modo);
}

// ---------- utilidades ----------
uint32_t agoraEpoch() { return (uint32_t)time(nullptr); }
bool horaOk() { return time(nullptr) > 1700000000; }

void hmacHex(const char* msg, char* out65) {
  uint8_t h[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, (const uint8_t*)CMD_SECRET, strlen(CMD_SECRET));
  mbedtls_md_hmac_update(&ctx, (const uint8_t*)msg, strlen(msg));
  mbedtls_md_hmac_finish(&ctx, h);
  mbedtls_md_free(&ctx);
  for (int i = 0; i < 32; i++) sprintf(out65 + i * 2, "%02x", h[i]);
  out65[64] = 0;
}

bool iguaisConstante(const char* a, const char* b) {
  size_t la = strlen(a), lb = strlen(b);
  if (la != lb) return false;
  uint8_t d = 0;
  for (size_t i = 0; i < la; i++) d |= a[i] ^ b[i];
  return d == 0;
}

// ---------- atuador (mantém o nível do pino durante o deep sleep) ----------
void escreveVentilador(bool ligar) {
  gpio_hold_dis((gpio_num_t)PIN_FAN);
  pinMode(PIN_FAN, OUTPUT);
  digitalWrite(PIN_FAN, ligar ? HIGH : LOW);
  gpio_hold_en((gpio_num_t)PIN_FAN);
}

// ---------- regra de decisão ----------
// Auto: liga se umidade > umidLiga OU temp > tLiga.
//       desliga só se umidade < umidDesliga E temp < tDesliga (histerese)
//       E já ficou ligado o tempo mínimo (não "bater" o relé).
// Sem leitura válida: ventilador DESLIGADO (estado seguro) + alerta.
void decide() {
  uint32_t agora = agoraEpoch();
  if (modo != 0 && horaOk() && agora >= modoAte) modo = 0;   // manual expira

  bool novo = ventLigado;
  if (modo == 1) novo = true;
  else if (modo == 2) novo = false;
  else if (!leituraOk) novo = false;
  else {
    if (!ventLigado) {
      if (umidPct > umidLiga || tempC > tLiga) novo = true;
    } else {
      bool passouMinimo = !horaOk() || (agora - ventDesde >= MIN_LIGADO_S);
      if (passouMinimo && umidPct < umidDesliga && tempC < tDesliga) novo = false;
    }
  }
  if (novo != ventLigado) {
    ventLigado = novo;
    ventDesde = ventLigado ? agora : 0;
  }
  escreveVentilador(ventLigado);
}

// ---------- alertas com cooldown (por tempo de relógio, sobrevive ao sleep) ----------
void enviaAlerta(int tipo, const char* nome, const char* msg) {
  uint32_t agora = agoraEpoch();
  if (ultimoAlerta[tipo] && agora - ultimoAlerta[tipo] < COOLDOWN_ALERTA_S) return;
  if (!mqtt.connected()) return;                 // não "gasta" o cooldown se não conseguiu enviar
  ultimoAlerta[tipo] = agora;
  JsonDocument d;
  d["tipo"] = nome;
  d["msg"] = msg;
  if (leituraOk) { d["temp_c"] = round(tempC * 10) / 10.0; d["umid_pct"] = round(umidPct * 10) / 10.0; }
  d["ts"] = agora;
  char out[256];
  serializeJson(d, out);
  mqtt.publish(T_ALERTA, out);
  Serial.printf("[ALERTA] %s\n", out);
}

// ---------- MQTT ----------
void publicaEstado() {
  if (!mqtt.connected()) return;
  JsonDocument d;
  d["ventilador"] = ventLigado;
  d["modo"] = modo == 0 ? "auto" : (modo == 1 ? "forcado_ligado" : "forcado_desligado");
  d["falha_sensor"] = falhasSeguidas >= FALHAS_PARA_ALERTA;
  d["ts"] = agoraEpoch();
  d["proximo_ts"] = agoraEpoch() + CICLO_S;      // o painel usa isto para dizer "dormindo até..."
  d["ciclo_s"] = CICLO_S;
  d["umid_liga"] = umidLiga; d["umid_desliga"] = umidDesliga;
  d["t_liga"] = tLiga;       d["t_desliga"] = tDesliga;
  char out[320];
  serializeJson(d, out);
  mqtt.publish(T_ESTADO, out, true);             // retido: painel novo já vê o último estado
}

void publicaTelemetria() {
  if (!mqtt.connected()) return;
  JsonDocument d;
  if (leituraOk) { d["temp_c"] = round(tempC * 10) / 10.0; d["umid_pct"] = round(umidPct * 10) / 10.0; }
  else { d["temp_c"] = nullptr; d["umid_pct"] = nullptr; }
  d["ventilador"] = ventLigado;
  d["ts"] = agoraEpoch();
  char out[200];
  serializeJson(d, out);
  mqtt.publish(T_TELEMETRIA, out, true);
}

void enviaAck(const char* idStr, const char* cmd, bool ok, const char* motivo) {
  JsonDocument d;
  d["id"] = idStr;
  d["cmd"] = cmd;
  d["ok"] = ok;
  d["motivo"] = motivo;
  char out[200];
  serializeJson(d, out);
  mqtt.publish(T_ACK, out);
}

bool configMudou = false;

// Limites vindos do painel por MQTT (tópico retido). Valida antes de aceitar.
void aplicaConfig(byte* payload, unsigned int len) {
  JsonDocument d;
  if (deserializeJson(d, payload, len)) { Serial.println("[CFG] JSON invalido"); return; }
  float ul = d["umid_liga"] | umidLiga, ud = d["umid_desliga"] | umidDesliga;
  float tl = d["t_liga"] | tLiga,       td = d["t_desliga"] | tDesliga;
  if (ul > 0 && ul <= 100 && ud > 0 && ud < ul && tl > 0 && tl < 60 && td > 0 && td < tl) {
    umidLiga = ul; umidDesliga = ud; tLiga = tl; tDesliga = td;
    configMudou = true;
    Serial.printf("[CFG] liga>%.0f%%/%.0fC desliga<%.0f%%/%.0fC\n", umidLiga, tLiga, umidDesliga, tDesliga);
  } else {
    Serial.println("[CFG] limites invalidos: ignorados");
  }
}

void aoReceber(char* topic, byte* payload, unsigned int len) {
  if (!strcmp(topic, T_CONFIG)) { aplicaConfig(payload, len); return; }
  JsonDocument d;
  if (deserializeJson(d, payload, len)) { Serial.println("[CMD] JSON invalido"); return; }
  const char* idStr = d["id"] | "";
  const char* cmd = d["cmd"] | "";
  const char* sig = d["sig"] | "";
  if (!*idStr || !*cmd || !*sig) { Serial.println("[CMD] campos faltando"); return; }

  // 1) assinatura HMAC: só quem conhece CMD_SECRET consegue gerar
  char msg[96], esperado[65];
  snprintf(msg, sizeof(msg), "%s|%s", idStr, cmd);
  hmacHex(msg, esperado);
  if (!iguaisConstante(esperado, sig)) { Serial.println("[CMD] assinatura invalida: descartado"); return; }

  // 2) anti-repetição: id precisa ser maior que o último aceito (guardado na flash)
  uint64_t id = strtoull(idStr, nullptr, 10);
  if (id <= ultimoIdComando) { Serial.println("[CMD] id repetido/antigo: descartado"); return; }
  ultimoIdComando = id;
  prefs.putULong64("ultimoId", id);

  bool ok = true; const char* motivo = "executado";
  if (!strcmp(cmd, "ventilador_ligar")) { modo = 1; modoAte = agoraEpoch() + MANUAL_MAX_S; }
  else if (!strcmp(cmd, "ventilador_desligar")) { modo = 2; modoAte = agoraEpoch() + MANUAL_MAX_S; }
  else if (!strcmp(cmd, "ventilador_auto")) { modo = 0; modoAte = 0; }
  else { ok = false; motivo = "comando_desconhecido"; }

  if (ok) decide();                               // age de verdade ANTES de confirmar
  Serial.printf("[CMD] %s -> %s\n", cmd, motivo);
  enviaAck(idStr, cmd, ok, motivo);               // o painel só mostra "executado" depois disto
  publicaEstado();
}

bool conectaMqtt() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(aoReceber);
  mqtt.setBufferSize(512);
  netMqtt.setCACert(ROOT_CA_MQTT);                // confere o broker (sem setInsecure)
  // clientId FIXO + sessão persistente: comandos enviados enquanto o ESP32 dorme ficam
  // guardados no broker (QoS 1) e chegam quando ele acorda.
  String cid = "esp32-silo-kauaheric";
  for (int i = 0; i < 3; i++) {
    if (mqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS, nullptr, 0, false, nullptr, false)) {
      mqtt.subscribe(T_COMANDO, 1);
      mqtt.subscribe(T_CONFIG, 1);
      return true;
    }
    Serial.printf("[MQTT] falhou rc=%d\n", mqtt.state());
    delay(1000);
  }
  return false;
}

// ---------- histórico ----------
void guardaNoBuffer() {
  if (!leituraOk || !horaOk()) return;            // leitura inválida não vira histórico
  if (bufN == BUF_MAX) { memmove(&buf[0], &buf[1], sizeof(Leitura) * (BUF_MAX - 1)); bufN--; }
  buf[bufN++] = { agoraEpoch(), tempC, umidPct, (uint8_t)(ventLigado ? 1 : 0) };
}

// Publica cada leitura guardada em T_LEITURA. A regra do broker (EMQX) encaminha ao Worker, que grava no D1.
void enviaHistorico() {
  if (bufN == 0 || !mqtt.connected()) return;
  int enviadas = 0;
  for (int i = 0; i < bufN; i++) {
    JsonDocument d;
    d["ts"] = buf[i].ts;
    d["temp_c"] = round(buf[i].t * 10) / 10.0;
    d["umid_pct"] = round(buf[i].u * 10) / 10.0;
    d["ventilador"] = buf[i].vent;
    char out[120];
    serializeJson(d, out);
    if (!mqtt.publish(T_LEITURA, out)) break;
    enviadas++;
    mqtt.loop();
    delay(20);
  }
  if (enviadas > 0) {
    memmove(&buf[0], &buf[enviadas], sizeof(Leitura) * (bufN - enviadas));
    bufN -= enviadas;
  }
  Serial.printf("[MQTT] %d leituras enviadas (restam %d)\n", enviadas, bufN);
}

bool conectaWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) delay(100);
  return WiFi.status() == WL_CONNECTED;
}

void dormir() {
  salvaEstado();
  mqtt.disconnect();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  gpio_deep_sleep_hold_en();                      // ventilador mantém o estado dormindo
  Serial.printf("Dormindo %u s (ventilador %s)\n", CICLO_S, ventLigado ? "LIGADO" : "desligado");
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)CICLO_S * 1000000ULL);
  esp_deep_sleep_start();
}

// ---------- um ciclo por despertar ----------
void setup() {
  Serial.begin(115200);
  gpio_deep_sleep_hold_dis();
  prefs.begin("silo", false);
  ultimoIdComando = prefs.getULong64("ultimoId", 0);
  if (rtcMagic != RTC_MAGIC) { restauraEstado(); rtcMagic = RTC_MAGIC; }   // RTC vazia: volta da flash
  dht.begin();

  // 1) sentir (DHT22 pode falhar na 1ª leitura: tenta 3 vezes)
  leituraOk = false;
  for (int i = 0; i < 3 && !leituraOk; i++) {
    delay(i == 0 ? 1000 : 2000);
    tempC = dht.readTemperature();
    umidPct = dht.readHumidity();
    leituraOk = !isnan(tempC) && !isnan(umidPct);
  }
  falhasSeguidas = leituraOk ? 0 : falhasSeguidas + 1;
  Serial.printf("T=%.1f C  U=%.1f %%  ok=%d  falhas=%d\n", tempC, umidPct, leituraOk, falhasSeguidas);

  // 2) decidir e agir NA BORDA (antes de qualquer rede)
  decide();

  // 3) comunicar (se a rede falhar, o ciclo continua: guarda e dorme)
  if (conectaWifi()) {
    Serial.println("[WIFI] conectado");
    if (!horaOk()) {
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      for (int i = 0; i < 150 && !horaOk(); i++) delay(100);   // até 15 s (TLS precisa da hora certa)
      if (horaOk()) {
        // decide() rodou antes do relógio existir: reajusta os tempos gravados com data de 1970
        if (ventLigado && ventDesde < 1700000000) ventDesde = agoraEpoch();
        if (modo != 0 && modoAte < 1700000000) modoAte = agoraEpoch() + MANUAL_MAX_S;
      }
    }
    Serial.printf("[NTP] hora %s\n", horaOk() ? "ok" : "NAO sincronizada");
    guardaNoBuffer();   // só depois do relógio: a leitura precisa de timestamp
    if (conectaMqtt()) {
      Serial.println("[MQTT] conectado");
      publicaTelemetria();
      publicaEstado();
      if (leituraOk && umidPct >= UMID_CRITICA) enviaAlerta(0, "umidade_alta", "Umidade critica no silo");
      if (falhasSeguidas >= FALHAS_PARA_ALERTA) enviaAlerta(1, "falha_sensor", "Sensor sem leitura: ventilador desligado por seguranca");
      // janela curta (2 s) acordado para receber comandos que ficaram na fila do broker
      uint32_t t0 = millis();
      while (millis() - t0 < JANELA_COMANDO_MS) { mqtt.loop(); delay(10); }
      if (configMudou) { decide(); publicaEstado(); }   // limites novos valem já neste ciclo
      enviaHistorico();
      mqtt.loop();
      delay(100);   // deixa o último pacote sair antes de dormir
    }
  } else {
    guardaNoBuffer();
    Serial.println("[WIFI] sem rede: opera sozinho, historico fica na memoria");
  }
  dormir();
}

void loop() {}
