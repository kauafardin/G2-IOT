# SiloVigia — Silo de Grãos (IoT · G0813 · Trabalho N2)

Kauã Fardin e Héric Rian — Faculdade Antonio Meneghetti, Sistemas de Informação.

**O que faz:** um ESP32 a bateria mede temperatura e umidade dentro do silo, decide sozinho quando ligar o ventilador (aeração) e dorme entre as leituras. Tudo sai por MQTT com TLS; uma regra do broker grava o histórico no D1 (Cloudflare) e o painel mostra o estado e envia comandos assinados.

```
DHT22 ─► ESP32 (decide + deep sleep) ──MQTT/TLS 8883──► Broker EMQX ◄──WSS 8084── Painel (navegador)
                                                           │ regra: webhook HTTPS (leituras)
                                                           ▼
                                              Worker ──► D1 (histórico) / KV (limites) ◄── Painel (HTTPS)
```

## Estrutura
| Pasta | Conteúdo |
|---|---|
| `esp32/` | Projeto Wokwi: `sketch.ino`, `diagram.json`, `libraries.txt`, `certs.h`, `secrets.example.h` |
| `worker/` | Worker (`src/index.js`), `schema.sql`, `wrangler.toml`, painel em `public/index.html` |
| `docs/` | Caderno de projeto preenchido, diagrama e prints |
| `tools/` | Scripts que geram o caderno e as imagens |

## Tópicos MQTT (`sis1a/kauaheric/silo/...`)
| Tópico | Quem publica | Quem assina |
|---|---|---|
| `telemetria`, `estado` (retidos) | ESP32 | painel |
| `leitura` (histórico) | ESP32 | regra do broker (webhook) |
| `comando` (QoS 1, HMAC) | painel | ESP32 |
| `ack`, `alerta` | ESP32 | painel |
| `config` (retido) | painel | ESP32 |

## Como montar (Wokwi)
1. Crie um projeto ESP32 no Wokwi e cole os arquivos de `esp32/`.
2. Copie `secrets.example.h` para `secrets.h` e preencha (Wi-Fi `Wokwi-GUEST`, usuário e senha MQTT, `CMD_SECRET`).
3. Ligações: DHT22 SDA → GPIO 15 · LED (ventilador) → GPIO 26 com resistor de 330 Ω.
4. `MODO_DEMO 1` usa tempos curtos (acorda a cada 30 s). Para campo, use `0` (15 min).

## Como subir a nuvem
```bash
cd worker
npx wrangler d1 create silo-graos          # copie o database_id para o wrangler.toml
npx wrangler kv namespace create KV        # copie o id para o wrangler.toml
npx wrangler d1 execute silo-graos --remote --file=schema.sql
npx wrangler secret put DEVICE_KEY         # a chave que o broker envia no webhook
npx wrangler secret put PAINEL_KEY         # a que o operador digita no painel
npx wrangler deploy
```

## Broker (EMQX Serverless)
1. **Authentication:** usuários `esp32_silo` e `painel`.
2. **Authorization → Username:**
   - `esp32_silo`: publica `telemetria`, `estado`, `ack`, `alerta`, `leitura`; assina `comando` e `config`.
   - `painel`: assina `sis1a/kauaheric/silo/#`; publica `comando` e `config`.
   - *All Users*: regra **Deny `#`** (publicar e assinar) como bloqueio final.
3. **Data Integration → HTTP Server** (encaminha o histórico ao Worker):
   - URL: `https://<seu-worker>.workers.dev/api/leituras`, método `POST`.
   - Headers: `content-type: application/json` e `x-api-key: <DEVICE_KEY>`.
   - Regra (SQL): `SELECT payload.ts AS ts, payload.temp_c AS temp_c, payload.umid_pct AS umid_pct, payload.ventilador AS ventilador FROM "sis1a/kauaheric/silo/leitura"`
   - Ação (corpo): `[{"ts": ${ts}, "temp_c": ${temp_c}, "umid_pct": ${umid_pct}, "ventilador": ${ventilador}}]`

## Como testar
- **Normal:** DHT22 em 24 °C / 60 % → LED apagado. Suba a umidade para 75 % → após o próximo despertar o LED acende e fica aceso (só apaga abaixo de 65 % e depois do tempo mínimo).
- **Histórico:** depois de alguns ciclos, o painel (ou `/api/resumo`) mostra as linhas no D1.
- **Sensor:** remova o fio do DHT22 → LED apaga (estado seguro) e, após 3 ciclos, chega o alerta `falha_sensor`.
- **Sem internet:** SSID errado → o ventilador continua decidindo; ao voltar a rede, as leituras guardadas são publicadas.
- **Comando falso:** publique em `comando` com assinatura errada → o ESP32 descarta (Serial: `assinatura invalida`).
- **Painel:** abra a URL do Worker, entre com as credenciais e envie "Ligar ventilador": fica *enviado* até o ESP32 acordar e confirmar.

## Segurança
Sem senhas no repositório (`secrets.h` e os segredos do Worker ficam fora). TLS verificado com certificado raiz (sem `setInsecure()`), usuários MQTT com permissão mínima e bloqueio geral, comandos com HMAC-SHA256 + id crescente, rota de gravação com chave (a chave só existe no broker e no Worker, nunca no ESP32).
