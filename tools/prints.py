import os
from PIL import Image

T = ('C:/Users/user/.claude/projects/C--Users-user-OneDrive--rea-de-Trabalho-Silo-Graos/'
     '0e2de6e0-7832-4180-ad8a-ee7829d6aadb/tool-results/mcp-claude-in-chrome-blob-')
os.makedirs('docs/prints', exist_ok=True)

# (arquivo de origem, caixa de recorte ou None, nome de saída)
itens = [
    ('1793889156393-k41tkt.jpg', (785, 60, 1568, 665), '01_wokwi_serial_mqtt.png'),
    ('1793889182602-8yfy57.jpg', (240, 40, 1229, 584), '02_emqx_broker_rodando.png'),
    ('1793889182602-w8j54p.jpg', (240, 40, 1229, 380), '03_emqx_usuarios.png'),
    ('1793889222692-67g8m5.jpg', (330, 40, 1536, 700), '04_emqx_permissoes.png'),
    ('1793889251856-i0kgct.jpg', (440, 40, 790, 525), '05_painel_login.png'),
    ('1793889251856-jnu1nb.jpg', (208, 0, 1229, 584), '06_cloudflare_worker.png'),
]
for src, box, out in itens:
    im = Image.open(T + src).convert('RGB')
    if box:
        im = im.crop(box)
    im.save('docs/prints/' + out)
    print(out, im.size)
