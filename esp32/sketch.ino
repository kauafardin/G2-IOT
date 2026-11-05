// Silo de grãos — ESP32 (borda, a bateria)
// Sente: DHT22 (temperatura e umidade do ar dentro do silo).
// Decide: liga/desliga o ventilador (aeração) com histerese, SEM depender da internet.
// Age: GPIO 26 (LED no Wokwi = relé do ventilador na vida real).
// Fala: MQTT com TLS (estado/comandos) + HTTPS para o Worker (histórico e limites).
// Para economizar bateria o ESP32 passa quase todo o tempo em deep sleep.
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
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

DHT dht(PIN_DHT, DHT22);
WiFiClientSecure netMqtt;
PubSubClient mqtt(netMqtt);
Preferences prefs;
uint64_t ultimoIdComando = 0;
float tempC = NAN, umidPct = NAN;
bool leituraOk = false;

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

void aoReceber(char* topic, byte* payload, unsigned int len) {
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
      return true;
    }
    Serial.printf("[MQTT] falhou rc=%d\n", mqtt.state());
    delay(1000);
  }
  return false;
}

// ---------- HTTPS: limites (KV) e histórico (D1) ----------
// SÓ PARA DIAGNÓSTICO: 1 = não confere o certificado do Worker. Deixe SEMPRE em 0 na entrega.
#define DIAG_HTTPS_INSEGURO 0

void configuraTlsHttp(WiFiClientSecure& c) {
#if DIAG_HTTPS_INSEGURO
  Serial.println("[AVISO] HTTPS sem verificar certificado (diagnostico)");
  c.setInsecure();
#else
  c.setCACert(ROOT_CA_HTTP);       // confere o servidor de verdade
#endif
}

void buscaConfig() {
  WiFiClientSecure tls;
  configuraTlsHttp(tls);
  HTTPClient http;
  http.setTimeout(6000);
  if (!http.begin(tls, String(WORKER_URL) + "/api/config")) return;
  http.addHeader("X-API-Key", WORKER_API_KEY);
  int code = http.GET();
  if (code < 0) {   // diagnóstico: motivo exato da falha de TLS/conexão
    char err[100];
    tls.lastError(err, sizeof(err));
    Serial.printf("[HTTP] erro %d: %s\n", code, err);
  }
  if (code == 200) {
    JsonDocument d;
    if (!deserializeJson(d, http.getString())) {
      float ul = d["umid_liga"] | umidLiga, ud = d["umid_desliga"] | umidDesliga;
      float tl = d["t_liga"] | tLiga,       td = d["t_desliga"] | tDesliga;
      // valida: ignora configuração absurda ou sem histerese
      if (ul > 0 && ul <= 100 && ud > 0 && ud < ul && tl > 0 && tl < 60 && td > 0 && td < tl) {
        umidLiga = ul; umidDesliga = ud; tLiga = tl; tDesliga = td;
      }
    }
  }
  http.end();
  Serial.printf("[HTTP] config %d: liga>%.0f%%/%.0fC desliga<%.0f%%/%.0fC\n", code, umidLiga, tLiga, umidDesliga, tDesliga);
}

void guardaNoBuffer() {
  if (!leituraOk || !horaOk()) return;            // leitura inválida não vira histórico
  if (bufN == BUF_MAX) { memmove(&buf[0], &buf[1], sizeof(Leitura) * (BUF_MAX - 1)); bufN--; }
  buf[bufN++] = { agoraEpoch(), tempC, umidPct, (uint8_t)(ventLigado ? 1 : 0) };
}

void enviaHistorico() {
  if (bufN == 0) return;
  JsonDocument d;
  JsonArray arr = d.to<JsonArray>();
  for (int i = 0; i < bufN; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ts"] = buf[i].ts;
    o["temp_c"] = round(buf[i].t * 10) / 10.0;
    o["umid_pct"] = round(buf[i].u * 10) / 10.0;
    o["ventilador"] = buf[i].vent;
  }
  String body;
  serializeJson(d, body);

  WiFiClientSecure tls;
  configuraTlsHttp(tls);
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(tls, String(WORKER_URL) + "/api/leituras")) return;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", WORKER_API_KEY);
  int code = http.POST(body);
  http.end();
  if (code >= 200 && code < 300) { Serial.printf("[HTTP] %d leituras gravadas\n", bufN); bufN = 0; }
  else Serial.printf("[HTTP] falhou (%d): %d leituras ficam na memoria\n", code, bufN);
}

bool conectaWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) delay(100);
  return WiFi.status() == WL_CONNECTED;
}

void dormir() {
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
    if (horaOk()) buscaConfig();   // sem hora certa o TLS recusa o certificado
    if (conectaMqtt()) {
      Serial.println("[MQTT] conectado");
      publicaTelemetria();
      publicaEstado();
      if (leituraOk && umidPct >= UMID_CRITICA) enviaAlerta(0, "umidade_alta", "Umidade critica no silo");
      if (falhasSeguidas >= FALHAS_PARA_ALERTA) enviaAlerta(1, "falha_sensor", "Sensor sem leitura: ventilador desligado por seguranca");
      // janela curta (2 s) acordado para receber comandos que ficaram na fila do broker
      uint32_t t0 = millis();
      while (millis() - t0 < JANELA_COMANDO_MS) { mqtt.loop(); delay(10); }
    }
    enviaHistorico();
  } else {
    guardaNoBuffer();
    Serial.println("[WIFI] sem rede: opera sozinho, historico fica na memoria");
  }
  dormir();
}

void loop() {}
