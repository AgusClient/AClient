// AClient Gameskins Presence Relay - Cloudflare Worker
// Compatible con Cloudflare Workers KV para almacenamiento temporal con TTL

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    // CORS preflight
    if (request.method === 'OPTIONS') {
      return new Response(null, {
        status: 204,
        headers: {
          'Access-Control-Allow-Origin': '*',
          'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
          'Access-Control-Allow-Headers': 'Content-Type, User-Agent',
        },
      });
    }

    const headers = {
      'Content-Type': 'application/json',
      'Access-Control-Allow-Origin': '*',
    };

    // GET /?server=ip:port
    if (request.method === 'GET') {
      const server = url.searchParams.get('server');
      if (!server) {
        return new Response(JSON.stringify([]), { headers });
      }

      if (!env.PRESENCE_KV) {
        return new Response(JSON.stringify({ error: 'KV namespace not bound' }), { status: 500, headers });
      }

      const raw = await env.PRESENCE_KV.get(server);
      return new Response(raw || '[]', { headers });
    }

    // POST /
    if (request.method === 'POST') {
      try {
        const payload = await request.json();
        const { server, player, asset_game } = payload;

        if (!server || !player || !asset_game) {
          return new Response(JSON.stringify({ error: 'Missing required fields' }), { status: 400, headers });
        }

        if (!env.PRESENCE_KV) {
          return new Response(JSON.stringify({ error: 'KV namespace not bound' }), { status: 500, headers });
        }

        let players = [];
        const raw = await env.PRESENCE_KV.get(server);
        if (raw) {
          try {
            players = JSON.parse(raw);
          } catch (e) {
            players = [];
          }
        }

        players = players.filter(p => p.player !== player);
        players.push({ player, asset_game, updated_at: Date.now() });

        // TTL de 300 segundos (5 minutos)
        await env.PRESENCE_KV.put(server, JSON.stringify(players), { expirationTtl: 300 });

        return new Response(JSON.stringify({ status: 'ok' }), { headers });
      } catch (err) {
        return new Response(JSON.stringify({ error: 'Invalid JSON payload' }), { status: 400, headers });
      }
    }

    return new Response(JSON.stringify({ error: 'Method not allowed' }), { status: 405, headers });
  },
};
