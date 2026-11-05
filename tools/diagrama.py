from PIL import Image, ImageDraw, ImageFont

F = 'C:/Windows/Fonts/arial.ttf'
FB = 'C:/Windows/Fonts/arialbd.ttf'
f = ImageFont.truetype(F, 20)
fb = ImageFont.truetype(FB, 22)
fs = ImageFont.truetype(F, 17)

W, H = 1500, 760
im = Image.new('RGB', (W, H), 'white')
d = ImageDraw.Draw(im)

COR = {'disp': '#dff0e3', 'rede': '#fff1cf', 'nuvem': '#dbe9fb', 'ui': '#f1e0f5'}
BORDA = {'disp': '#2e7d32', 'rede': '#b26a00', 'nuvem': '#1f5fbf', 'ui': '#7b3f98'}


def box(x, y, w, h, camada, titulo, linhas):
    d.rounded_rectangle([x, y, x + w, y + h], 14, fill=COR[camada], outline=BORDA[camada], width=3)
    d.text((x + 14, y + 10), titulo, font=fb, fill='black')
    for i, ln in enumerate(linhas):
        d.text((x + 14, y + 44 + i * 25), ln, font=f, fill='#222')


def seta(p1, p2, texto=None, tpos=None, dupla=False):
    d.line([p1, p2], fill='#444', width=3)
    for a, b in ([(p1, p2), (p2, p1)] if dupla else [(p1, p2)]):
        import math
        ang = math.atan2(b[1] - a[1], b[0] - a[0])
        for s in (-0.45, 0.45):
            d.line([b, (b[0] - 16 * math.cos(ang + s), b[1] - 16 * math.sin(ang + s))], fill='#444', width=3)
    if texto:
        x, y = tpos or ((p1[0] + p2[0]) // 2, (p1[1] + p2[1]) // 2 - 28)
        d.text((x, y), texto, font=fs, fill='#333', anchor='mm' if tpos is None else 'la')


d.text((W // 2, 24), 'SiloVigia: do sensor ao painel', font=ImageFont.truetype(FB, 30), fill='black', anchor='mm')

# dispositivo
box(40, 150, 330, 150, 'disp', 'DISPOSITIVO', ['DHT22 (GPIO 15)', 'ESP32: decide e age', 'Relé/LED ventilador (GPIO 26)', 'Deep sleep + memória RTC'])
# rede
box(520, 70, 340, 120, 'rede', 'REDE', ['Broker EMQX Serverless', 'MQTT/TLS 8883 · WSS 8084', 'usuários + ACL + regra webhook'])
# nuvem
box(520, 330, 340, 140, 'nuvem', 'NUVEM (Cloudflare)', ['Worker: /api/leituras,', '/api/resumo, /api/config', 'chave X-API-Key'])
box(1000, 300, 200, 80, 'nuvem', 'D1', ['histórico'])
box(1000, 410, 200, 80, 'nuvem', 'KV', ['limites, último'])
# interface
box(1000, 70, 440, 120, 'ui', 'INTERFACE', ['Painel web (navegador)', 'estado · gráfico · alertas', 'comando assinado (HMAC) + ack'])

seta((370, 190), (520, 130), 'MQTT/TLS 8883 · telemetria,\nestado, leitura, ack, alerta, config', (40, 95))
seta((520, 160), (370, 235), None)
seta((860, 130), (1000, 130), 'WSS 8084', dupla=True)
seta((690, 190), (690, 330), 'regra do broker:\nwebhook HTTPS (leituras)', (705, 235))
seta((860, 400), (1000, 340), None)
seta((860, 410), (1000, 450), None)
seta((1220, 190), (860, 350), 'HTTPS: resumo e limites', (1050, 235))

d.text((40, 560), 'Comando: painel → broker (/comando, QoS 1) → fila até o ESP32 acordar → executa → ack → painel mostra "executado".',
       font=f, fill='#222')
d.text((40, 595), 'O ESP32 só fala MQTT: o broker encaminha as leituras ao Worker (D1). Sem internet ele segue decidindo e guarda até 48 leituras na RTC.',
       font=f, fill='#222')
for i, (k, t) in enumerate([('disp', 'Dispositivo'), ('rede', 'Rede'), ('nuvem', 'Nuvem'), ('ui', 'Interface')]):
    x = 40 + i * 240
    d.rounded_rectangle([x, 660, x + 28, 688], 6, fill=COR[k], outline=BORDA[k], width=3)
    d.text((x + 38, 662), t, font=f, fill='black')

im.save('docs/arquitetura.png')

# recorte do esquemático real do Wokwi (somente a área do circuito, sem código nem chaves)
src = ('C:/Users/user/.claude/projects/C--Users-user-OneDrive--rea-de-Trabalho-Silo-Graos/'
       '0e2de6e0-7832-4180-ad8a-ee7829d6aadb/tool-results/mcp-claude-in-chrome-blob-1793885957106-m135g2.jpg')
w = Image.open(src)
w.crop((790, 100, 1560, 530)).save('docs/wokwi_esquematico.png')
print('ok')
