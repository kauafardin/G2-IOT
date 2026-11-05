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
print('ok')
