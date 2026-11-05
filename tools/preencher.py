import copy
import docx
from docx.shared import Pt
from docx.text.paragraph import Paragraph

d = docx.Document('tools/original.docx')
T = d.tables


def put(cell, text, size=Pt(9)):
    lines = text.split('\n')
    first = cell.paragraphs[0]
    for extra in cell.paragraphs[1:]:
        extra._p.getparent().remove(extra._p)
    for r in list(first.runs):
        r._r.getparent().remove(r._r)
    template = copy.deepcopy(first._p)
    prev = first
    for i, ln in enumerate(lines):
        if i == 0:
            p = first
        else:
            new = copy.deepcopy(template)
            prev._p.addnext(new)
            p = Paragraph(new, cell)
        run = p.add_run(ln)
        run.font.size = size
        prev = p


def row_vals(t, ri, vals, start=0):
    for j, v in enumerate(vals):
        put(t.rows[ri].cells[start + j], v)


# ---- capa
put(T[0].rows[0].cells[1], 'Kauã Fardin e Héric Rian')
put(T[0].rows[1].cells[1], 'Opção A — Silo de grãos')
put(T[0].rows[2].cells[1], 'https://github.com/SEU-USUARIO/silo-graos   (trocar pelo link real do repositório)')

# ---- etapa 0
put(T[4].rows[0].cells[1], 'Opção A — Silo de grãos.')
put(T[4].rows[1].cells[1],
    'Grão úmido ou quente no silo cria fungo e praga e estraga a safra; a aeração resolve, mas o silo fica longe da tomada, então tudo roda a bateria e com internet instável. Ganham o produtor (menos perda de safra e menos idas ao silo) e a cooperativa (histórico para provar a qualidade do grão armazenado).\n'
    'Frase da ideia: "Para o produtor rural, que sofre com perda de grão por umidade em silo sem tomada, o SiloVigia sente temperatura e umidade (DHT22), decide a aeração com histerese e age com o ventilador (relé), mesmo com bateria de vários meses e internet instável."')

# ---- etapa 1
rf = [
    ('A cada despertar (15 min), ler o DHT22 e decidir na borda: LIGAR o ventilador se umidade > 70 % OU temperatura > 30 °C; DESLIGAR só se umidade < 65 % E temperatura < 28 °C.',
     'Wokwi: mover os sliders do DHT22 e ver o LED; o Serial mostra T, U e estado do ventilador.'),
    ('Manter o ventilador ligado por no mínimo 30 min antes de poder desligar, mesmo que a leitura melhore (evita ligar/desligar o relé).',
     'Teste com umidade oscilando entre 64 % e 71 %: o LED não pisca; no MODO_DEMO o mínimo é 60 s.'),
    ('Publicar leitura e estado por MQTT/TLS e gravar o histórico no D1 em até 10 s após acordar; sem internet, guardar até 48 leituras e enviá-las quando a rede voltar. Alertar se umidade ≥ 78 % (cooldown de 6 h por tipo).',
     'Consulta /api/resumo no painel; derrubar o Wi-Fi e ver as leituras chegarem depois; alerta no tópico /alerta.'),
    ('Executar comando remoto assinado (ligar / desligar / automático) no primeiro despertar após o envio e confirmar com ack; descartar comando sem assinatura válida ou repetido; o modo manual expira em 1 h.',
     'Painel mostra "enviado" e depois "executado" só com o ack; comando falso não muda nada (log no Serial).'),
]
for i, (a, b) in enumerate(rf, 1):
    row_vals(T[5], i, [a, b], 1)

rnf = [
    ('Autonomia de pelo menos 4 meses com 2 baterias 18650 (5.000 mAh), usando deep sleep, ciclo de 15 min e no máximo ~6 s acordado por ciclo.',
     'O silo não tem tomada; se a bateria acabar o produtor volta a ir ao silo fisicamente.'),
    ('Continuar decidindo e agindo sozinho, 100 % na borda, sem internet.',
     'Wi-Fi/4G em área rural cai com frequência; a aeração não pode depender da nuvem.'),
    ('Nenhum comando sem assinatura é executado e nenhum segredo vai para o GitHub.',
     'O ventilador é um equipamento físico: um comando falso gasta energia ou umedece o grão com ar úmido.'),
]
for i, (a, b) in enumerate(rnf, 1):
    row_vals(T[6], i, [a, b], 1)

# ---- etapa 2
comp = [
    ('DHT22', 'Temperatura (°C) e umidade relativa (%) do ar dentro do silo', 'GPIO 15',
     '-40 a 80 °C e 0 a 100 % UR. Liga: U > 70 % ou T > 30 °C. Desliga: U < 65 % e T < 28 °C.',
     'Tenta 3 leituras; se continuar NaN, ESTADO SEGURO: ventilador DESLIGADO (ar úmido sem medir pode piorar o grão e gasta bateria) e, após 3 ciclos seguidos, envia o alerta falha_sensor.'),
    ('LED verde + resistor 330 Ω (representa o RELÉ do ventilador de aeração)', 'Liga/desliga o ventilador', 'GPIO 26',
     'HIGH = ventilador ligado; LOW = desligado. O nível é mantido durante o deep sleep (gpio_hold).',
     'Após reinício o ESP32 relê o sensor e decide de novo; a RTC lembra o estado e o painel só dá "executado" com o ack.'),
    ('Wi-Fi + MQTT/HTTPS + timer de deep sleep', 'Comunica e acorda o ESP32 a cada ciclo', '—',
     'Wi-Fi com timeout de 10 s; ciclo de 900 s (15 min); janela de comandos de 2 s.',
     'Sem rede: continua decidindo sozinho, guarda as leituras na memória RTC e tenta de novo no próximo ciclo.'),
]
for i, row in enumerate(comp, 1):
    row_vals(T[7], i, row)

# ---- etapa 3
put(T[9].rows[0].cells[1],
    'a cada despertar:\n'
    '  le DHT22 (ate 3 tentativas)\n'
    '  se leitura invalida: ventilador = DESLIGADO (estado seguro); falhas++\n'
    '  senao:\n'
    '    se modo manual ativo e nao expirou: ventilador = manual\n'
    '    senao se ventilador desligado e (U > 70 ou T > 30): ventilador = LIGADO\n'
    '    senao se ventilador ligado e ficou >= 30 min e U < 65 e T < 28: ventilador = DESLIGADO\n'
    '  escreve GPIO 26 e trava o nivel (gpio_hold)\n'
    '  envia, recebe comandos por 2 s e dorme 15 min')
put(T[9].rows[1].cells[1],
    'No ESP32 (borda): a decisão não pode esperar nem depender da internet, e o dado já nasce aqui. Os limites (70/65 %, 30/28 °C) ficam no KV do Worker porque o agrônomo pode ajustá-los de fora; o ESP32 lê esse valor a cada despertar e guarda o último na memória RTC. Cruzar dados e mostrar histórico fica na nuvem.')
put(T[9].rows[2].cells[1],
    'Cai: o ESP32 continua ligando/desligando o ventilador com os últimos limites que recebeu e guarda até 48 leituras (12 h) na RTC; a mais antiga é descartada se encher. Volta: no primeiro ciclo com Wi-Fi ele reenvia tudo em lote (o D1 usa ts como chave, então reenvio não duplica), busca os limites novos e recebe os comandos que ficaram na fila do broker.')

# ---- etapa 4
put(T[11].rows[0].cells[1],
    'MQTT com TLS (porta 8883) para estado, telemetria, comando, ack e alerta: o ESP32 usa sessão persistente e QoS 1, então um comando enviado enquanto ele dorme fica na fila e chega quando acorda. HTTPS (Worker) para gravar o histórico no D1 e ler os limites no KV, porque a nuvem não consegue "chamar" o ESP32. O painel usa MQTT sobre WebSocket seguro (8084) para o tempo real.')
put(T[11].rows[1].cells[1],
    'sis1a/kauaheric/silo/telemetria — ESP32 publica (retido); painel assina\n'
    'sis1a/kauaheric/silo/estado — ESP32 publica (retido); painel assina\n'
    'sis1a/kauaheric/silo/comando — painel publica; ESP32 assina (QoS 1)\n'
    'sis1a/kauaheric/silo/ack — ESP32 publica; painel assina\n'
    'sis1a/kauaheric/silo/alerta — ESP32 publica; painel assina')
put(T[11].rows[2].cells[1], '{"temp_c":24.3,"umid_pct":71.5,"ventilador":true,"ts":1767225600}')
put(T[11].rows[3].cells[1],
    'MQTT: porta 8883. HTTPS: 443. Painel: wss 8084. O ESP32 chama setCACert() com o certificado raiz (o "CA Certificate" do EMQX para o broker; a raiz do Worker para o HTTPS) e só conecta se a cadeia do servidor terminar nele. Não usamos setInsecure().')

# ---- etapa 5
put(T[13].rows[0].cells[1],
    'Os dois. O KV guarda um valor por chave: os limites do ventilador (config) e a última leitura (ultimo). O D1 guarda cada leitura como linha, porque queremos responder perguntas sobre o passado (média e máxima de umidade por hora, quanto tempo o ventilador trabalhou).')
put(T[13].rows[1].cells[1],
    'D1, tabela leituras(ts INTEGER PRIMARY KEY, temp_c REAL, umid_pct REAL, ventilador INTEGER 0/1).\n'
    'KV: chave "config" = {umid_liga, umid_desliga, t_liga, t_desliga}; chave "ultimo" = última leitura.')
put(T[13].rows[2].cells[1], 'A cada despertar, ou seja, a cada 15 min (96 linhas por dia). Se a internet cair, as leituras acumulam na RTC e são gravadas em lote.')
put(T[13].rows[3].cells[1],
    'SELECT (ts/3600)*3600 AS hora, MAX(umid_pct), AVG(umid_pct), MAX(temp_c), ROUND(100.0*SUM(ventilador)/COUNT(*)) AS pct_ventilador FROM leituras WHERE ts >= ? GROUP BY hora ORDER BY hora;\n'
    'Responde: "Como a umidade e a temperatura variaram hora a hora nas últimas 24 h e quanto tempo o ventilador ficou ligado?"')
put(T[13].rows[4].cells[1],
    '90 dias (limpeza diária por cron do Worker), cobrindo uma safra armazenada. Não há dado pessoal: só temperatura, umidade e estado do ventilador, sem identificar pessoas; por isso o risco de LGPD é baixo.')

# ---- etapa 6
put(T[14].rows[0].cells[1],
    'Cartões com umidade, temperatura, estado do ventilador (e modo) e situação do dispositivo ("Dormindo, acorda em ~X s" ou "Sem sinal"); gráfico de umidade por hora das últimas 24 h e tabela de resumo (vindos do D1); lista de alertas; formulário dos limites (KV).')
put(T[14].rows[1].cells[1],
    'Ligar / desligar / voltar ao automático, publicados em sis1a/kauaheric/silo/comando como JSON {id, cmd, sig}, com sig = HMAC-SHA256(segredo, id|cmd). Os limites são alterados por PUT /api/config no Worker (exige a chave do painel).')
put(T[14].rows[2].cells[1],
    'Só mostra "executado" quando chega no tópico /ack a confirmação com o mesmo id; antes disso mostra "enviado, aguardando o silo acordar". Espera um ciclo + 20 s (15 min + 20 s); passado isso exibe "SEM CONFIRMAÇÃO — o silo pode estar offline". O painel também mostra "dormindo até..." ou "sem sinal".')
put(T[14].rows[3].cells[1],
    'Dispara no ESP32 quando a umidade chega a 78 % ou mais, ou quando o sensor fica 3 ciclos sem leitura válida. Chega pelo tópico /alerta (banner e lista no painel). Cooldown de 6 h por tipo de alerta, contado pelo relógio e guardado na RTC, então sobrevive ao deep sleep.')
put(T[14].rows[4].cells[1],
    'WebSocket (MQTT sobre WSS) para estado, ack e alertas, com mensagens retidas: como o ESP32 dorme, o painel que abre depois já recebe o último estado. Polling a cada 5 min só para o resumo do D1, que muda devagar.')

# ---- etapa 7
sec = [
    ('Sim, poderia existir.', 'O broker só aceita TLS na porta 8883; a 1883 não é usada. Camada: rede/transporte.'),
    ('Sim, poderia existir.', 'setCACert() com certificado raiz no ESP32 para o broker e para o Worker; setInsecure() não aparece no código. Camada: dispositivo.'),
    ('Sim, a rota de gravação.', 'POST /api/leituras exige X-API-Key (DEVICE_KEY, comparação em tempo constante) e valida faixas e formato; PUT /api/config exige a chave do painel. Camada: nuvem.'),
    ('Sim, o tópico de comando.', 'Usuários MQTT com permissão mínima (o painel só publica em /comando; o ESP32 só assina nele) e comando com HMAC-SHA256 + id crescente guardado na flash, que bloqueia comando falso e repetido. Camadas: broker e dispositivo.'),
    ('Sim, poderia existir.', 'secrets.h está no .gitignore (só o secrets.example.h vai ao GitHub); segredos do Worker via wrangler secret; o painel pede as credenciais na tela e as mantém só na aba; o Serial nunca imprime senhas ou chaves. Camada: repositório.'),
]
for i, (a, b) in enumerate(sec, 1):
    row_vals(T[15], i, [a, b], 1)

# ---- etapa 8
put(T[17].rows[0].cells[1],
    'Energia e autonomia (deep sleep + cálculo). Combina com o problema: o silo não tem tomada e a pergunta difícil do desafio é "quanto tempo o ESP32 pode ficar acordado para a bateria durar?".')
put(T[17].rows[1].cells[1],
    'Premissas (estimativas a confirmar com multímetro): acordado ~6 s por ciclo a ~160 mA (Wi-Fi + TLS + HTTPS + janela de comandos de 2 s); deep sleep ~0,15 mA (kit com regulador; ~0,01 mA no chip puro); bateria 2×18650 = 5.000 mAh com 80 % úteis = 4.000 mAh. O ventilador tem alimentação própria (painel solar ou rede), fora desta conta.\n'
    'Ciclo de 15 min (900 s): corrente média = (160×6 + 0,15×894) ÷ 900 = 1.094 ÷ 900 ≈ 1,22 mA. Autonomia = 4.000 ÷ 1,22 ≈ 3.290 h ≈ 137 dias (~4,5 meses).\n'
    'Ciclo de 30 min: (160×6 + 0,15×1.794) ÷ 1.800 ≈ 0,68 mA → ≈ 5.860 h ≈ 244 dias.\n'
    'Sempre acordado: 160 mA → 4.000 ÷ 160 = 25 h (pouco mais de 1 dia).\n'
    'Conclusão: ficar acordado só ~6 s a cada 15 min multiplica a autonomia por ~130 vezes e atende o RNF1 (≥ 4 meses). Cada segundo a mais acordado custa cerca de 0,18 mA de média (≈ 15 % da autonomia), por isso a janela de comandos é de apenas 2 s.')

# ---- etapa 9
put(T[18].rows[1].cells[1], 'ESP32 DevKit + sensor DHT22 (GPIO 15) + relé do ventilador (LED no Wokwi, GPIO 26). Decide sozinho, usa deep sleep e guarda estado e leituras na memória RTC.')
put(T[18].rows[2].cells[1], 'Wi-Fi do local; MQTT com TLS (8883) até o broker EMQX Serverless; HTTPS (443) até o Worker; painel por WebSocket seguro (8084).')
put(T[18].rows[3].cells[1], 'Cloudflare Worker (rotas /api/leituras, /api/resumo, /api/config), D1 (histórico), KV (limites e última leitura) e cron diário de limpeza (90 dias).')
put(T[18].rows[4].cells[1], 'Painel web servido pelo próprio Worker: estado em tempo real, gráfico e resumo, comandos assinados com confirmação (ack), alertas e ajuste de limites.')
put(T[19].rows[0].cells[1],
    'Fluxo: DHT22 → ESP32 (decide, aciona o ventilador, dorme) → MQTT/TLS → broker ⇄ painel (WSS); ESP32 → HTTPS → Worker → D1/KV ⇄ painel.\n'
    'Diagrama de arquitetura (acima) e esquemático no Wokwi (DHT22 no GPIO 15; LED + 330 Ω no GPIO 26) (abaixo).')
from docx.shared import Inches
_cel = T[19].rows[0].cells[1]
for _img, _w in (('docs/arquitetura.png', 5.4), ('docs/wokwi_esquematico.png', 4.2)):
    _p = _cel.add_paragraph()
    _p.add_run().add_picture(_img, width=Inches(_w))

# ---- etapa 10 (procedimento; resultados ficam para depois de rodar)
RES = '☐ Ainda não executado. Preencher após rodar no Wokwi.'
resultados = [
    'Observado no Serial do Wokwi: DHT22 lendo 24,0 °C e 60 % (ok=1); Wi-Fi conectado; NTP sincronizado; MQTT com TLS conectado ao EMQX (certificado do broker conferido). Fora do Wokwi: o Worker recusa a gravação sem chave (HTTP 401) e o painel abre (HTTP 200). PENDENTE: o HTTPS do ESP32 ao Worker falha no simulador (SSL - EOF), então ainda não há leituras no D1 vindas do ESP32.',
    RES,
    'Parcial. Com o HTTPS falhando, o ESP32 não travou: guardou a leitura na memória ("1 leituras ficam na memoria"), publicou por MQTT e foi dormir. Senha MQTT errada foi recusada pelo broker (rc=4) até corrigirmos. Pendente: remover o sensor e trocar o SSID.',
    'Parcial. Observado: o ESP32 reiniciou sozinho no primeiro boot (assert, antes de o NTP sincronizar), voltou a operar e fez ciclos de deep sleep de 30 s sem intervenção. Pendente: parar e iniciar a simulação e conferir o id do último comando.',
    'Parcial. A credencial errada no broker foi recusada (rc=4) e o Worker recusa chamadas sem chave (401). Pendente: publicar comando com assinatura errada e reenviar comando antigo.',
    'Parcial. Foram observados vários ciclos de 30 s (~2 min) sem travar. Pendente: 10 min completos e conferência no D1.',
]
tests = [
    'Rodar o DHT22 e o LED separadamente, depois o MQTT e o HTTPS (curl na rota); conferir cada um no Serial.',
    'Subir a umidade para 71 % (liga), baixá-la para 66 % (continua ligado) e para 64 % (desliga após o tempo mínimo); repetir com a temperatura em 31 e 27 °C.',
    'Remover o fio SDA do DHT22: o ventilador deve desligar e, após 3 ciclos, chegar o alerta falha_sensor. Trocar o SSID por um errado: o ventilador continua decidindo e as leituras são enviadas ao voltar.',
    'Reiniciar o ESP32 (Stop/Start): o ventilador deve voltar ao estado coerente com a leitura; o id do último comando persiste (anti-repetição).',
    'Publicar em /comando com assinatura errada e reenviar um comando antigo: nada deve mudar e o Serial deve registrar o descarte.',
    'Deixar 10 min ligado no MODO_DEMO (≈ 20 ciclos): conferir se as leituras chegam ao D1 sem repetição e sem travar.',
]
for i, tx in enumerate(tests, 1):
    row_vals(T[20], i, [tx, resultados[i - 1]], 1)

d.save('docs/Trabalho_N2_Caderno_de_Projeto_Preenchido.docx')
print('ok')
