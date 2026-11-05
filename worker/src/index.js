// Worker do Silo de Grãos: histórico (D1), limites (KV) e painel (arquivos estáticos).
// Segredos (wrangler secret put): DEVICE_KEY (ESP32) e PAINEL_KEY (painel/operador).

const DEFAULT_CONFIG = { umid_liga: 70, umid_desliga: 65, t_liga: 30, t_desliga: 28 };
const RETENCAO_DIAS = 90;

const json = (obj, status = 200) =>
  new Response(JSON.stringify(obj), {
    status,
    headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" },
  });

// Comparação em tempo constante (compara os hashes SHA-256 das duas chaves).
async function chaveConfere(recebida, esperada) {
  if (!recebida || !esperada) return false;
  const enc = new TextEncoder();
  const [a, b] = await Promise.all([
    crypto.subtle.digest("SHA-256", enc.encode(recebida)),
    crypto.subtle.digest("SHA-256", enc.encode(esperada)),
  ]);
  return crypto.subtle.timingSafeEqual(a, b);
}

const ehDispositivo = (req, env) => chaveConfere(req.headers.get("X-API-Key"), env.DEVICE_KEY);
const ehPainel = (req, env) => chaveConfere(req.headers.get("X-API-Key"), env.PAINEL_KEY);

function numero(v, min, max) {
  return typeof v === "number" && Number.isFinite(v) && v >= min && v <= max;
}

function configValida(c) {
  return (
    c &&
    numero(c.umid_liga, 1, 100) &&
    numero(c.umid_desliga, 1, 100) &&
    numero(c.t_liga, 1, 60) &&
    numero(c.t_desliga, 1, 60) &&
    c.umid_desliga < c.umid_liga && // histerese obrigatória
    c.t_desliga < c.t_liga
  );
}

async function gravarLeituras(req, env) {
  if (!(await ehDispositivo(req, env))) return json({ erro: "nao_autorizado" }, 401);
  let corpo;
  try { corpo = await req.json(); } catch { return json({ erro: "json_invalido" }, 400); }
  if (!Array.isArray(corpo) || corpo.length === 0 || corpo.length > 100) {
    return json({ erro: "esperado_array_de_1_a_100" }, 400);
  }
  const agora = Math.floor(Date.now() / 1000);
  const validas = corpo.filter(
    (l) =>
      Number.isInteger(l.ts) && l.ts > 1700000000 && l.ts < agora + 3600 &&
      numero(l.temp_c, -40, 80) && numero(l.umid_pct, 0, 100) &&
      (l.ventilador === 0 || l.ventilador === 1)
  );
  if (validas.length === 0) return json({ erro: "nenhuma_leitura_valida" }, 400);

  // INSERT OR IGNORE: se o ESP32 reenviar o mesmo lote, não duplica (ts é a chave).
  const stmt = env.DB.prepare(
    "INSERT OR IGNORE INTO leituras (ts, temp_c, umid_pct, ventilador) VALUES (?, ?, ?, ?)"
  );
  await env.DB.batch(validas.map((l) => stmt.bind(l.ts, l.temp_c, l.umid_pct, l.ventilador)));

  const ultima = validas.reduce((a, b) => (b.ts > a.ts ? b : a));
  await env.KV.put("ultimo", JSON.stringify(ultima));
  return json({ ok: true, gravadas: validas.length, descartadas: corpo.length - validas.length });
}

async function resumo(req, env, url) {
  if (!(await ehPainel(req, env))) return json({ erro: "nao_autorizado" }, 401);
  const horas = Math.min(Math.max(parseInt(url.searchParams.get("horas") || "24", 10) || 24, 1), 168);
  const desde = Math.floor(Date.now() / 1000) - horas * 3600;
  // Pergunta: "como ficaram umidade e temperatura hora a hora, e quanto tempo o ventilador trabalhou?"
  const { results } = await env.DB.prepare(
    `SELECT (ts / 3600) * 3600            AS hora,
            ROUND(MAX(umid_pct), 1)       AS umid_max,
            ROUND(AVG(umid_pct), 1)       AS umid_media,
            ROUND(MAX(temp_c), 1)         AS temp_max,
            ROUND(AVG(temp_c), 1)         AS temp_media,
            ROUND(100.0 * SUM(ventilador) / COUNT(*), 0) AS pct_ventilador_ligado,
            COUNT(*)                      AS leituras
       FROM leituras
      WHERE ts >= ?
      GROUP BY hora
      ORDER BY hora`
  ).bind(desde).all();
  return json({ horas, linhas: results });
}

async function config(req, env) {
  const atual = (await env.KV.get("config", "json")) || DEFAULT_CONFIG;
  if (req.method === "GET") {
    if (!(await ehDispositivo(req, env)) && !(await ehPainel(req, env))) return json({ erro: "nao_autorizado" }, 401);
    return json(atual);
  }
  if (!(await ehPainel(req, env))) return json({ erro: "nao_autorizado" }, 401); // só o operador altera
  let nova;
  try { nova = await req.json(); } catch { return json({ erro: "json_invalido" }, 400); }
  const c = {
    umid_liga: nova.umid_liga, umid_desliga: nova.umid_desliga,
    t_liga: nova.t_liga, t_desliga: nova.t_desliga,
  };
  if (!configValida(c)) return json({ erro: "limites_invalidos (desliga precisa ser menor que liga)" }, 400);
  await env.KV.put("config", JSON.stringify(c));
  return json(c);
}

export default {
  async fetch(req, env) {
    const url = new URL(req.url);
    const { pathname: p, } = url;

    if (p === "/api/leituras" && req.method === "POST") return gravarLeituras(req, env);
    if (p === "/api/resumo" && req.method === "GET") return resumo(req, env, url);
    if (p === "/api/config" && (req.method === "GET" || req.method === "PUT")) return config(req, env);
    if (p === "/api/ultimo" && req.method === "GET") {
      if (!(await ehPainel(req, env))) return json({ erro: "nao_autorizado" }, 401);
      return json((await env.KV.get("ultimo", "json")) || null);
    }
    if (p.startsWith("/api/")) return json({ erro: "rota_inexistente" }, 404);
    return env.ASSETS.fetch(req); // painel
  },

  // Roda 1x por dia: LGPD/boa prática — guarda só o necessário, pelo tempo necessário.
  async scheduled(_evt, env) {
    const limite = Math.floor(Date.now() / 1000) - RETENCAO_DIAS * 86400;
    await env.DB.prepare("DELETE FROM leituras WHERE ts < ?").bind(limite).run();
  },
};
