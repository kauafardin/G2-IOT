# Roteiro de apresentação (4 minutos) — SiloVigia

Dupla: Kauã Fardin e Héric Rian

## 1. Problema e quem usa (30 s)
"Para o produtor rural, que sofre com perda de grão por umidade em silo sem tomada, o SiloVigia sente temperatura e umidade (DHT22), decide a aeração com histerese e age com o ventilador (relé), mesmo com bateria de vários meses e internet instável."

## 2. Arquitetura: do sensor ao painel (45 s)
Mostrar `docs/arquitetura.png`.
DHT22 → ESP32 (decide, aciona o ventilador, dorme) → MQTT/TLS → broker EMQX ⇄ painel (WSS). ESP32 → HTTPS → Worker → D1 (histórico) e KV (limites).

## 3. Escolhas e por quê (60 s)
- **Onde decide:** no ESP32. A decisão não pode depender de internet. Os limites ficam no KV porque o agrônomo pode ajustar de fora.
- **Regra:** liga se umidade > 70 % ou T > 30 °C; desliga só se umidade < 65 % e T < 28 °C e depois de 30 min ligado (histerese + tempo mínimo).
- **Estado seguro:** sem leitura válida, o ventilador desliga e sai alerta.
- **Protocolo:** MQTT (comando e estado), HTTPS (histórico/limites). WebSocket no painel.
- **KV e D1:** KV para o limite e o último valor; D1 para responder perguntas sobre o passado.
- **Segurança:** TLS verificado, usuários MQTT com ACL por tópico, comando assinado (HMAC) com id crescente, rota de gravação com chave, segredos fora do GitHub.
- **Extra:** energia. Deep sleep de 15 min, ~6 s acordado: ~137 dias com 2 baterias 18650 (contra ~25 h sempre acordado).

## 4. Demonstração ao vivo (75 s)
1. Wokwi rodando: Serial com `[WIFI]`, `[NTP]`, `[MQTT] conectado`.
2. Subir a umidade para 75 % → LED (ventilador) liga no próximo ciclo; descer para 64 % → desliga só depois do tempo mínimo.
3. Painel: enviar "Ligar ventilador" → aparece "enviado" e só vira "executado" com o ack do ESP32.
4. Mostrar o D1 / resumo por hora (se o HTTPS estiver funcionando) ou o print guardado.

## 5. O que falta para virar produto (30 s)
1.000 unidades: tópicos por silo (`sis1a/<grupo>/<silo>/...`), credencial por dispositivo, provisionamento, caixa estanque, painel solar, alimentação do ventilador separada, custo por unidade.

## Perguntas da turma
- **"E se a internet cair?"** O ESP32 continua decidindo sozinho com os últimos limites e guarda até 48 leituras na memória RTC; quando a rede volta, envia em lote (o D1 usa o timestamp como chave, então não duplica).
- **"E se alguém publicar no seu tópico?"** O broker só deixa o usuário `painel` publicar em `/comando`, e o ESP32 ainda confere a assinatura HMAC e o id (sem assinatura válida ou com id repetido, descarta).
- **"E com 1.000 unidades?"** Tópico por silo, credencial e ACL por dispositivo (placeholders `${username}`), D1 com coluna de dispositivo, e o ciclo de 15 min reduz a carga no broker.

## Limitações a admitir (se perguntarem)
- Os testes da Etapa 10 foram feitos só em parte (veja o caderno).
- O HTTPS do ESP32 ao Worker falhou no simulador (SSL EOF), enquanto o Worker aceita handshakes TLS 1.2 de teste fora do Wokwi.
