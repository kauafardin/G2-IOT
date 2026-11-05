import os
from PIL import Image, ImageDraw, ImageFont

T = ('C:/Users/user/.claude/projects/C--Users-user-OneDrive--rea-de-Trabalho-Silo-Graos/'
     '0e2de6e0-7832-4180-ad8a-ee7829d6aadb/tool-results/mcp-claude-in-chrome-blob-')
os.makedirs('docs/prints', exist_ok=True)


def abre(nome):
    return Image.open(T + nome).convert('RGB')


# 01: circuito real do Wokwi + texto real do Serial (transcrito do Serial Monitor)
circ = abre('1793889156393-k41tkt.jpg').crop((785, 60, 1568, 530))
serial = ['T=24.0 C  U=60.0 %  ok=1  falhas=0',
          '[WIFI] conectado',
          '[NTP] hora ok',
          '[MQTT] conectado',
          '[MQTT] 1 leituras enviadas (restam 0)',
          'Dormindo 30 s (ventilador desligado)']
f = ImageFont.truetype('C:/Windows/Fonts/consola.ttf', 20)
fb = ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf', 18)
h = 52 + 30 * len(serial)
img = Image.new('RGB', (circ.width, circ.height + h), 'white')
img.paste(circ, (0, 0))
d = ImageDraw.Draw(img)
d.rectangle([0, circ.height, circ.width, circ.height + h], fill='#f4f4f2')
d.text((14, circ.height + 10), 'Serial Monitor do Wokwi (transcrição de um ciclo)', font=fb, fill='#222')
for i, ln in enumerate(serial):
    d.text((14, circ.height + 44 + i * 30), ln, font=f, fill='#111')
img.save('docs/prints/01_wokwi_serial_mqtt.png')

# 02, 03: EMQX
abre('1793889182602-8yfy57.jpg').crop((240, 40, 1229, 584)).save('docs/prints/02_emqx_broker_rodando.png')
abre('1793889182602-w8j54p.jpg').crop((240, 40, 1229, 380)).save('docs/prints/03_emqx_usuarios.png')

# 04: permissões atualizadas (painel com 3 e esp32_silo com 7), duas capturas empilhadas
a = abre('1793897038223-6bujpm.jpg').crop((345, 60, 1500, 480))        # painel + início do esp32_silo
b = abre('1793897031101-veg8hd.jpg').crop((345, 335, 1500, 674))       # linhas do esp32_silo
perm = Image.new('RGB', (a.width, a.height + b.height), 'white')
perm.paste(a, (0, 0))
perm.paste(b, (0, a.height))
perm.save('docs/prints/04_emqx_permissoes.png')

# 05, 06: painel e Cloudflare
abre('1793889251856-i0kgct.jpg').crop((440, 40, 790, 525)).save('docs/prints/05_painel_login.png')
abre('1793889251856-jnu1nb.jpg').crop((208, 0, 1229, 584)).save('docs/prints/06_cloudflare_worker.png')

# 07: regra do broker (conector conectado + regra habilitada)
abre('1793896703784-b1lj6e.jpg').crop((215, 35, 1075, 390)).save('docs/prints/07_emqx_regra_webhook.png')

# 08: leituras no D1 (resultado do SELECT)
abre('1793896989082-9snxfh.jpg').crop((210, 40, 1075, 330)).save('docs/prints/08_d1_leituras.png')
# 09: painel com comando executado e ventilador LIGADO
abre('1793897874394-raij7t.jpg').crop((290, 15, 1235, 490)).save('docs/prints/09_painel_comando_executado.png')

# 10: Wokwi com LED aceso e estado restaurado da flash
abre('1793897862088-4pwj7t.jpg').crop((552, 40, 1098, 462)).save('docs/prints/10_wokwi_led_ligado.png')
# 11: painel com grafico (2 horas), tabela do D1 e alerta de falha do sensor
abre('1793899216432-xvxxul.jpg').crop((290, 150, 1235, 730)).save('docs/prints/11_painel_grafico_alerta.png')


def figura(titulo, linhas, saida, largura=900):
    f = ImageFont.truetype('C:/Windows/Fonts/consola.ttf', 19)
    fb = ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf', 18)
    h = 56 + 28 * len(linhas)
    im = Image.new('RGB', (largura, h), '#f4f4f2')
    d = ImageDraw.Draw(im)
    d.text((14, 12), titulo, font=fb, fill='#222')
    for i, ln in enumerate(linhas):
        d.text((14, 46 + i * 28), ln, font=f, fill='#111')
    im.save(saida)


# 12: teste dos limites (transcricao do Serial do Wokwi)
figura('Teste dos limites no Wokwi (transcrição do Serial; ciclos de 30 s)',
       ['Umidade 60 %  (modo auto)  -> Dormindo 30 s (ventilador desligado)',
        'Umidade 75 %               -> Dormindo 30 s (ventilador LIGADO)',
        'Umidade 66 %               -> Dormindo 30 s (ventilador LIGADO)   [histerese]',
        'Umidade 64 %               -> ligado no 1º ciclo (tempo mínimo) e depois',
        '                              Dormindo 30 s (ventilador desligado)',
        'Regra: liga se U>70 %; desliga só se U<65 % e T<28 °C, após o tempo mínimo.'],
       'docs/prints/12_teste_limites.png')

# 13: sensor com falha e alerta (transcricao do Serial do Wokwi)
figura('Sensor removido no Wokwi (transcrição do Serial)',
       ['T=nan C  U=nan %  ok=0  falhas=1   -> Dormindo 30 s (ventilador desligado)',
        'T=nan C  U=nan %  ok=0  falhas=2   -> Dormindo 30 s (ventilador desligado)',
        'T=nan C  U=nan %  ok=0  falhas=3   -> Dormindo 30 s (ventilador desligado)',
        '...',
        '[ALERTA] {"tipo":"falha_sensor","msg":"Sensor sem leitura: ventilador',
        '          desligado por seguranca"}',
        '[WIFI] sem rede: opera sozinho, historico fica na memoria'],
       'docs/prints/13_sensor_falha_alerta.png')
# 14: comando falso (transcricao do Serial do Wokwi)
figura('Comandos falsos no Wokwi (transcrição do Serial)',
       ['[CMD] assinatura invalida: descartado                 (assinatura errada)',
        '[CMD] ventilador_auto -> executado                    (comando válido do painel)',
        '[CMD] id repetido/antigo: descartado                  (assinatura válida, id antigo)'],
       'docs/prints/14_comando_falso.png', 1000)
print('ok')
