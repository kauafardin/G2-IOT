// Copie este arquivo para "secrets.h" e preencha. NUNCA suba o secrets.h para o GitHub.
#pragma once

#define WIFI_SSID        "Wokwi-GUEST"   // no Wokwi; na placa real, use o Wi-Fi da fazenda
#define WIFI_PASS        ""

// Broker MQTT com TLS (EMQX Serverless, porta 8883)
#define MQTT_HOST        "a6c06cd1.ala.us-east-1.emqxsl.com"
#define MQTT_PORT        8883
#define MQTT_USER        "esp32_silo"        // usuário com permissão só nos tópicos do grupo
#define MQTT_PASS        "troque-esta-senha"

// Segredo compartilhado com o painel para assinar comandos (HMAC-SHA256)
#define CMD_SECRET       "troque-este-segredo-de-comando"
