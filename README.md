# Silo de Grãos (IoT · G0813 · Trabalho N2)

Kauã Fardin e Héric Rian — Faculdade Antonio Meneghetti, Sistemas de Informação.

**O que faz:** um ESP32 a bateria mede temperatura e umidade dentro do silo, decide sozinho quando ligar o ventilador (aeração) e dorme entre as leituras. Publica o estado por MQTT com TLS, grava o histórico num Worker da Cloudflare (D1) e recebe comandos assinados do painel.

```
DHT22 ─► ESP32 (decide + deep sleep) ──MQTT/TLS 8883──► Broker ◄──WSS 8084── Painel (navegador)
                  │                                                              │
                  └────────HTTPS (X-API-Key)──► Worker ──► D1 (histórico) / KV (limites) ◄──┘
```

## Estrutura
| Pasta | Conteúdo |
|---|---|
| `esp32/` | Projeto Wokwi: `sketch.ino`, `diagram.json`, `libraries.txt`, `certs.h`, `secrets.example.h` |
| `worker/` | Worker (`src/index.js`), `schema.sql`, `wrangler.toml`, painel em `public/index.html` |
| `docs/` | Caderno de projeto preenchido |

## Como montar (Wokwi)
1. Crie um projeto ESP32 no Wokwi e cole os arquivos de `esp32/` (ou importe pelo `diagram.json`).
2. Copie `secrets.example.h` para `secrets.h` e preencha (Wi-Fi `Wokwi-GUEST`, broker, chaves).
3. Em `certs.h`, cole os certificados raiz do broker e do Worker (instruções no próprio arquivo).
4. Ligações: DHT22 SDA → GPIO 15 · LED (ventilador) → GPIO 26 com resistor de 330 Ω.
5. `MODO_DEMO 1` usa tempos curtos (acorda a cada 30 s). Para campo, use `0` (15 min).

## Como subir a nuvem
```bash
cd worker
npx wrangler d1 create silo-graos          # copie o database_id para o wrangler.toml
npx wrangler kv namespace create KV        # copie o id para o wrangler.toml
npx wrangler d1 execute silo-graos --remote --file=schema.sql
npx wrangler secret put DEVICE_KEY         # a mesma do secrets.h do ESP32
npx wrangler secret put PAINEL_KEY         # a que o operador digita no painel
npx wrangler deploy
```
No broker (EMQX Serverless, deployment a6c06cd1.ala.us-east-1.emqxsl.com; Access Control > Authentication e Authorization), crie dois usuários com permissões separadas:
`esp32_silo` publica em `telemetria|estado|ack|alerta` e assina `comando`; `painel` assina os quatro primeiros e publica só em `comando`.

## Como testar
- **Normal:** DHT22 em 24 °C / 60 % → LED apagado. Suba a umidade para 75 % → após o próximo despertar o LED acende e fica aceso (histerese: só apaga abaixo de 65 %).
- **Sensor:** remova o fio do DHT22 → LED apaga (estado seguro) e, após 3 ciclos, chega o alerta `falha_sensor`.
- **Sem internet:** use SSID errado → o ventilador continua decidindo; ao voltar a rede, as leituras guardadas são enviadas.
- **Comando falso:** publique em `comando` com assinatura errada → o ESP32 descarta e nada muda (Serial mostra `assinatura invalida`).
- **Painel:** abra a URL do Worker, entre com as credenciais e envie “Ligar ventilador”: fica *enviado* até o ESP32 acordar e confirmar.

## Segurança
Sem senhas no repositório (`secrets.h` e segredos do Worker ficam fora). TLS verificado com certificado raiz (sem `setInsecure()`), rota de gravação com chave, comandos com HMAC-SHA256 + id crescente, usuários MQTT com permissão mínima.
