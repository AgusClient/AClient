// AClient Gameskins Presence Relay & HWID Authentication - Cloudflare Worker
// Compatible con Cloudflare Workers KV (PRESENCE_KV)

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

    if (!env.PRESENCE_KV) {
      return new Response(JSON.stringify({ error: 'KV namespace not bound' }), { status: 500, headers });
    }

    // ==========================================
    // AUTENTICACION HWID & LOGIN
    // ==========================================
    if (url.pathname === '/api/auth/login' || url.pathname === '/auth/login') {
      if (request.method !== 'POST') {
        return new Response(JSON.stringify({ status: 'error', message: 'Metodo no permitido' }), { status: 405, headers });
      }

      try {
        const payload = await request.json();
        const { user, password, hwid } = payload;

        if (!user || !password || !hwid) {
          return new Response(JSON.stringify({ status: 'error', message: 'Faltan campos obligatorios' }), { status: 400, headers });
        }

        const normalizedUser = user.trim().toLowerCase();
        const kvKey = `user:${normalizedUser}`;

        const rawUser = await env.PRESENCE_KV.get(kvKey);
        if (!rawUser) {
          return new Response(JSON.stringify({ status: 'error', message: 'Usuario no registrado' }), { status: 401, headers });
        }

        let userData;
        try {
          userData = JSON.parse(rawUser);
        } catch (e) {
          return new Response(JSON.stringify({ status: 'error', message: 'Error interno de cuenta' }), { status: 500, headers });
        }

        if (userData.active === false) {
          return new Response(JSON.stringify({ status: 'error', message: 'Acceso revocado por el administrador' }), { status: 403, headers });
        }

        if (userData.password !== password) {
          return new Response(JSON.stringify({ status: 'error', message: 'Contrasena incorrecta' }), { status: 401, headers });
        }

        // Si es la primera vez que inicia sesion, vinculamos su HWID
        if (!userData.hwid) {
          userData.hwid = hwid;
          userData.activated_at = Date.now();
          await env.PRESENCE_KV.put(kvKey, JSON.stringify(userData));
        } else if (userData.hwid !== hwid) {
          return new Response(JSON.stringify({
            status: 'error',
            message: 'Esta cuenta ya esta vinculada a otra computadora'
          }), { status: 403, headers });
        }

        return new Response(JSON.stringify({
          status: 'ok',
          message: 'Autenticado correctamente',
          user: normalizedUser,
          hwid: userData.hwid
        }), { headers });

      } catch (err) {
        return new Response(JSON.stringify({ status: 'error', message: 'Error procesando solicitud' }), { status: 400, headers });
      }
    }

    // ==========================================
    // PRESENCIA DE GAMESKINS (Existente)
    // ==========================================
    // GET /?server=ip:port
    if (request.method === 'GET') {
      const server = url.searchParams.get('server');
      if (!server) {
        return new Response(JSON.stringify([]), { headers });
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
