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
          return new Response(JSON.stringify({ status: 'error', message: 'Contraseña incorrecta' }), { status: 401, headers });
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
    // PRESENCIA DE GAMESKINS
    // ==========================================
    // GET /?server=ip:port o /?player=Nombre
    if (request.method === 'GET') {
      const server = url.searchParams.get('server');
      const player = url.searchParams.get('player');

      // Consulta directa del skin de un jugador por nombre
      if (player) {
        const safePlayerKey = encodeURIComponent(player.trim());
        const raw = await env.PRESENCE_KV.get(`player_skin:${safePlayerKey}`);
        if (raw) {
          return new Response(raw, { headers });
        }
        return new Response(JSON.stringify({ error: 'Player not found' }), { status: 404, headers });
      }

      if (!server) {
        return new Response(JSON.stringify([]), { headers });
      }

      const normalizedServer = server.trim();
      const prefix = `presence:${normalizedServer}:`;
      const list = await env.PRESENCE_KV.list({ prefix });
      const players = [];

      for (const item of list.keys) {
        if (item.metadata && item.metadata.player && item.metadata.asset_game) {
          players.push({
            player: item.metadata.player,
            asset_game: item.metadata.asset_game
          });
        } else {
          const raw = await env.PRESENCE_KV.get(item.name);
          if (raw) {
            try {
              const data = JSON.parse(raw);
              if (data && data.player && data.asset_game) {
                players.push({
                  player: data.player,
                  asset_game: data.asset_game
                });
              }
            } catch (e) {}
          }
        }
      }

      // Si no hay claves atomicas, revisar si existe el registro legacy para compatibilidad
      if (players.length === 0) {
        const legacyRaw = await env.PRESENCE_KV.get(normalizedServer);
        if (legacyRaw) {
          try {
            const legacyPlayers = JSON.parse(legacyRaw);
            if (Array.isArray(legacyPlayers)) {
              return new Response(JSON.stringify(legacyPlayers), { headers });
            }
          } catch (e) {}
        }
      }

      return new Response(JSON.stringify(players), { headers });
    }

    // POST /
    if (request.method === 'POST') {
      try {
        const payload = await request.json();
        const { server, player, asset_game } = payload;

        if (!server || !player || !asset_game) {
          return new Response(JSON.stringify({ error: 'Missing required fields' }), { status: 400, headers });
        }

        const normalizedServer = server.trim();
        const safePlayer = player.trim();
        const safeSkin = asset_game.trim();
        const safePlayerKey = encodeURIComponent(safePlayer);

        const individualKey = `presence:${normalizedServer}:${safePlayerKey}`;
        const record = {
          player: safePlayer,
          asset_game: safeSkin,
          updated_at: Date.now()
        };

        // 1. Guardar la clave atomica individual por jugador en KV con TTL de 300 segundos
        // Almacenamos metadata para recuperacion sin lecturas KV adicionales
        await env.PRESENCE_KV.put(individualKey, JSON.stringify(record), {
          expirationTtl: 300,
          metadata: { player: safePlayer, asset_game: safeSkin }
        });

        // 2. Guardar registro por jugador individual (player_skin:<nombre>) con TTL de 600 segundos
        const playerGlobalKey = `player_skin:${safePlayerKey}`;
        await env.PRESENCE_KV.put(playerGlobalKey, JSON.stringify(record), {
          expirationTtl: 600,
          metadata: { player: safePlayer, asset_game: safeSkin }
        });

        // 3. Mantener compatibilidad con clientes legados actualizando la clave monolítica
        let legacyPlayers = [];
        const raw = await env.PRESENCE_KV.get(normalizedServer);
        if (raw) {
          try {
            legacyPlayers = JSON.parse(raw);
            if (!Array.isArray(legacyPlayers)) legacyPlayers = [];
          } catch (e) {
            legacyPlayers = [];
          }
        }
        legacyPlayers = legacyPlayers.filter(p => p.player !== safePlayer);
        legacyPlayers.push({ player: safePlayer, asset_game: safeSkin, updated_at: Date.now() });
        await env.PRESENCE_KV.put(normalizedServer, JSON.stringify(legacyPlayers), { expirationTtl: 300 });

        return new Response(JSON.stringify({ status: 'ok' }), { headers });
      } catch (err) {
        return new Response(JSON.stringify({ error: 'Invalid JSON payload' }), { status: 400, headers });
      }
    }

    return new Response(JSON.stringify({ error: 'Method not allowed' }), { status: 405, headers });
  },
};
