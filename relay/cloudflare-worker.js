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
          'Access-Control-Allow-Headers': 'Content-Type, User-Agent, X-Admin-Key',
        },
      });
    }

    const headers = {
      'Content-Type': 'application/json',
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Headers': 'Content-Type, User-Agent, X-Admin-Key',
    };

    // ==========================================
    // PANEL WEB DE ADMINISTRACION
    // ==========================================
    if (url.pathname === '/admin' || url.pathname === '/admin.html') {
      return new Response(getAdminHtml(), {
        headers: {
          'Content-Type': 'text/html; charset=utf-8',
          'Access-Control-Allow-Origin': '*',
        },
      });
    }

    if (!env.PRESENCE_KV) {
      return new Response(JSON.stringify({ error: 'KV namespace not bound' }), { status: 500, headers });
    }

    // ==========================================
    // API ADMINISTRACION DE CUENTAS & HWID
    // ==========================================
    if (url.pathname.startsWith('/api/admin/')) {
      const adminKey = request.headers.get('X-Admin-Key') || url.searchParams.get('admin_key');
      const expectedKey = env.ADMIN_KEY || 'agus2026';

      if (!adminKey || adminKey !== expectedKey) {
        return new Response(JSON.stringify({ status: 'error', message: 'No autorizado. Clave de administrador incorrecta.' }), { status: 401, headers });
      }

      // GET /api/admin/users
      if (url.pathname === '/api/admin/users' && request.method === 'GET') {
        const list = await env.PRESENCE_KV.list({ prefix: 'user:' });
        const users = [];

        for (const item of list.keys) {
          const raw = await env.PRESENCE_KV.get(item.name);
          if (raw) {
            try {
              const data = JSON.parse(raw);
              const username = item.name.replace(/^user:/, '');
              users.push({
                user: username,
                password: data.password || '',
                hwid: data.hwid || null,
                active: data.active !== false,
                activated_at: data.activated_at || null,
              });
            } catch (e) {}
          }
        }

        users.sort((a, b) => a.user.localeCompare(b.user));
        return new Response(JSON.stringify({ status: 'ok', users }), { headers });
      }

      // POST /api/admin/user (Crear o actualizar usuario)
      if (url.pathname === '/api/admin/user' && request.method === 'POST') {
        try {
          const body = await request.json();
          const { user, password, active, hwid } = body;
          if (!user || !user.trim()) {
            return new Response(JSON.stringify({ status: 'error', message: 'El nombre de usuario es obligatorio' }), { status: 400, headers });
          }

          const normalizedUser = user.trim().toLowerCase();
          const kvKey = `user:${normalizedUser}`;
          let userData = {
            password: password ? String(password).trim() : '123',
            hwid: hwid || null,
            active: active !== false,
            created_at: Date.now(),
          };

          const rawExisting = await env.PRESENCE_KV.get(kvKey);
          if (rawExisting) {
            try {
              const existing = JSON.parse(rawExisting);
              userData = {
                ...existing,
                password: password !== undefined ? String(password).trim() : existing.password,
                active: active !== undefined ? active : existing.active,
                hwid: hwid !== undefined ? hwid : existing.hwid,
                updated_at: Date.now(),
              };
            } catch (e) {}
          }

          await env.PRESENCE_KV.put(kvKey, JSON.stringify(userData));
          return new Response(JSON.stringify({ status: 'ok', message: `Usuario "${normalizedUser}" guardado con exito`, user: normalizedUser }), { headers });
        } catch (e) {
          return new Response(JSON.stringify({ status: 'error', message: 'Payload invalido' }), { status: 400, headers });
        }
      }

      // POST /api/admin/reset-hwid
      if (url.pathname === '/api/admin/reset-hwid' && request.method === 'POST') {
        try {
          const body = await request.json();
          const { user, all } = body;

          if (all) {
            const list = await env.PRESENCE_KV.list({ prefix: 'user:' });
            let count = 0;
            for (const item of list.keys) {
              const raw = await env.PRESENCE_KV.get(item.name);
              if (raw) {
                try {
                  const data = JSON.parse(raw);
                  data.hwid = null;
                  data.activated_at = null;
                  await env.PRESENCE_KV.put(item.name, JSON.stringify(data));
                  count++;
                } catch (e) {}
              }
            }
            return new Response(JSON.stringify({ status: 'ok', message: `Se blanquearon los HWIDs de ${count} cuentas`, count }), { headers });
          }

          if (!user) {
            return new Response(JSON.stringify({ status: 'error', message: 'Usuario no especificado' }), { status: 400, headers });
          }

          const normalizedUser = user.trim().toLowerCase();
          const kvKey = `user:${normalizedUser}`;
          const raw = await env.PRESENCE_KV.get(kvKey);
          if (!raw) {
            return new Response(JSON.stringify({ status: 'error', message: 'Usuario no encontrado' }), { status: 404, headers });
          }

          const data = JSON.parse(raw);
          data.hwid = null;
          data.activated_at = null;
          await env.PRESENCE_KV.put(kvKey, JSON.stringify(data));
          return new Response(JSON.stringify({ status: 'ok', message: `HWID de "${normalizedUser}" reseteado con exito` }), { headers });
        } catch (e) {
          return new Response(JSON.stringify({ status: 'error', message: 'Error procesando solicitud' }), { status: 400, headers });
        }
      }

      // POST /api/admin/toggle-active
      if (url.pathname === '/api/admin/toggle-active' && request.method === 'POST') {
        try {
          const body = await request.json();
          const { user, active } = body;
          if (!user) {
            return new Response(JSON.stringify({ status: 'error', message: 'Usuario no especificado' }), { status: 400, headers });
          }

          const normalizedUser = user.trim().toLowerCase();
          const kvKey = `user:${normalizedUser}`;
          const raw = await env.PRESENCE_KV.get(kvKey);
          if (!raw) {
            return new Response(JSON.stringify({ status: 'error', message: 'Usuario no encontrado' }), { status: 404, headers });
          }

          const data = JSON.parse(raw);
          data.active = Boolean(active);
          await env.PRESENCE_KV.put(kvKey, JSON.stringify(data));
          return new Response(JSON.stringify({ status: 'ok', message: `Estado de "${normalizedUser}" cambiado a ${data.active ? 'Activo' : 'Suspendido'}` }), { headers });
        } catch (e) {
          return new Response(JSON.stringify({ status: 'error', message: 'Error procesando solicitud' }), { status: 400, headers });
        }
      }

      // POST /api/admin/delete-user
      if (url.pathname === '/api/admin/delete-user' && request.method === 'POST') {
        try {
          const body = await request.json();
          const { user } = body;
          if (!user) {
            return new Response(JSON.stringify({ status: 'error', message: 'Usuario no especificado' }), { status: 400, headers });
          }

          const normalizedUser = user.trim().toLowerCase();
          const kvKey = `user:${normalizedUser}`;
          await env.PRESENCE_KV.delete(kvKey);
          return new Response(JSON.stringify({ status: 'ok', message: `Cuenta "${normalizedUser}" eliminada de la base de datos` }), { headers });
        } catch (e) {
          return new Response(JSON.stringify({ status: 'error', message: 'Error procesando solicitud' }), { status: 400, headers });
        }
      }

      return new Response(JSON.stringify({ status: 'error', message: 'Endpoint no encontrado' }), { status: 404, headers });
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

// ==========================================
// DECODIFICADOR HTML DEL PANEL DE ADMINISTRACION
// ==========================================
const ADMIN_HTML_B64 = "PCFET0NUWVBFIGh0bWw+CjxodG1sIGxhbmc9ImVzIj4KPGhlYWQ+CiAgPG1ldGEgY2hhcnNldD0iVVRGLTgiPgogIDxtZXRhIG5hbWU9InZpZXdwb3J0IiBjb250ZW50PSJ3aWR0aD1kZXZpY2Utd2lkdGgsIGluaXRpYWwtc2NhbGU9MS4wIj4KICA8dGl0bGU+QUNsaWVudCAtIFBhbmVsIGRlIEFkbWluaXN0cmFjaW9uIGRlIEN1ZW50YXM8L3RpdGxlPgogIDxzdHlsZT4KICAgIDpyb290IHsKICAgICAgLS1iZzogIzBiMGYxNzsKICAgICAgLS1jYXJkLWJnOiAjMTUxYjI4OwogICAgICAtLWNhcmQtaG92ZXI6ICMxYTIyMzM7CiAgICAgIC0tYm9yZGVyOiAjMjMyYjNlOwogICAgICAtLWJvcmRlci1mb2N1czogIzNiODJmNjsKICAgICAgLS10ZXh0OiAjZjFmNWY5OwogICAgICAtLXRleHQtbXV0ZWQ6ICM5NGEzYjg7CiAgICAgIC0tcHJpbWFyeTogIzNiODJmNjsKICAgICAgLS1wcmltYXJ5LWhvdmVyOiAjMjU2M2ViOwogICAgICAtLXN1Y2Nlc3M6ICMxMGI5ODE7CiAgICAgIC0tc3VjY2Vzcy1iZzogcmdiYSgxNiwgMTg1LCAxMjksIDAuMTIpOwogICAgICAtLXdhcm5pbmc6ICNmNTllMGI7CiAgICAgIC0td2FybmluZy1iZzogcmdiYSgyNDUsIDE1OCwgMTEsIDAuMTIpOwogICAgICAtLWRhbmdlcjogI2VmNDQ0NDsKICAgICAgLS1kYW5nZXItYmc6IHJnYmEoMjM5LCA2OCwgNjgsIDAuMTIpOwogICAgICAtLWZvbnQ6IC1hcHBsZS1zeXN0ZW0sIEJsaW5rTWFjU3lzdGVtRm9udCwgIlNlZ29lIFVJIiwgUm9ib3RvLCBPeHlnZW4sIFVidW50dSwgQ2FudGFyZWxsLCBzYW5zLXNlcmlmOwogICAgfQoKICAgICogeyBib3gtc2l6aW5nOiBib3JkZXItYm94OyBtYXJnaW46IDA7IHBhZGRpbmc6IDA7IH0KICAgIGJvZHkgewogICAgICBiYWNrZ3JvdW5kLWNvbG9yOiB2YXIoLS1iZyk7CiAgICAgIGNvbG9yOiB2YXIoLS10ZXh0KTsKICAgICAgZm9udC1mYW1pbHk6IHZhcigtLWZvbnQpOwogICAgICBtaW4taGVpZ2h0OiAxMDB2aDsKICAgICAgZGlzcGxheTogZmxleDsKICAgICAgZmxleC1kaXJlY3Rpb246IGNvbHVtbjsKICAgIH0KCiAgICAvKiBIZWFkZXIgKi8KICAgIGhlYWRlciB7CiAgICAgIGJhY2tncm91bmQ6IHZhcigtLWNhcmQtYmcpOwogICAgICBib3JkZXItYm90dG9tOiAxcHggc29saWQgdmFyKC0tYm9yZGVyKTsKICAgICAgcGFkZGluZzogMXJlbSAxLjVyZW07CiAgICAgIGRpc3BsYXk6IGZsZXg7CiAgICAgIGp1c3RpZnktY29udGVudDogc3BhY2UtYmV0d2VlbjsKICAgICAgYWxpZ24taXRlbXM6IGNlbnRlcjsKICAgICAgZmxleC13cmFwOiB3cmFwOwogICAgICBnYXA6IDFyZW07CiAgICB9CiAgICAuaGVhZGVyLXRpdGxlIHsKICAgICAgZGlzcGxheTogZmxleDsKICAgICAgYWxpZ24taXRlbXM6IGNlbnRlcjsKICAgICAgZ2FwOiAwLjc1cmVtOwogICAgfQogICAgLmhlYWRlci10aXRsZSBoMSB7CiAgICAgIGZvbnQtc2l6ZTogMS4yNXJlbTsKICAgICAgZm9udC13ZWlnaHQ6IDcwMDsKICAgICAgbGV0dGVyLXNwYWNpbmc6IC0wLjAyZW07CiAgICB9CiAgICAuYmFkZ2Utc3RhdHVzIHsKICAgICAgZm9udC1zaXplOiAwLjc1cmVtOwogICAgICBmb250LXdlaWdodDogNjAwOwogICAgICBwYWRkaW5nOiAwLjJyZW0gMC41NXJlbTsKICAgICAgYm9yZGVyLXJhZGl1czogOTk5OXB4OwogICAgICBiYWNrZ3JvdW5kOiB2YXIoLS1zdWNjZXNzLWJnKTsKICAgICAgY29sb3I6IHZhcigtLXN1Y2Nlc3MpOwogICAgICBib3JkZXI6IDFweCBzb2xpZCByZ2JhKDE2LCAxODUsIDEyOSwgMC4yNSk7CiAgICB9CiAgICAuaGVhZGVyLWFjdGlvbnMgewogICAgICBkaXNwbGF5OiBmbGV4OwogICAgICBhbGlnbi1pdGVtczogY2VudGVyOwogICAgICBnYXA6IDAuNzVyZW07CiAgICB9CgogICAgLyogQ29udGFpbmVyICovCiAgICAuY29udGFpbmVyIHsKICAgICAgd2lkdGg6IDEwMCU7CiAgICAgIG1heC13aWR0aDogMTIwMHB4OwogICAgICBtYXJnaW46IDAgYXV0bzsKICAgICAgcGFkZGluZzogMS41cmVtOwogICAgICBmbGV4OiAxOwogICAgfQoKICAgIC8qIEF1dGggU2NyZWVuICovCiAgICAjYXV0aC1zY3JlZW4gewogICAgICBkaXNwbGF5OiBmbGV4OwogICAgICBqdXN0aWZ5LWNvbnRlbnQ6IGNlbnRlcjsKICAgICAgYWxpZ24taXRlbXM6IGNlbnRlcjsKICAgICAgbWluLWhlaWdodDogNjB2aDsKICAgIH0KICAgIC5hdXRoLWNhcmQgewogICAgICBiYWNrZ3JvdW5kOiB2YXIoLS1jYXJkLWJnKTsKICAgICAgYm9yZGVyOiAxcHggc29saWQgdmFyKC0tYm9yZGVyKTsKICAgICAgYm9yZGVyLXJhZGl1czogMTJweDsKICAgICAgcGFkZGluZzogMnJlbTsKICAgICAgd2lkdGg6IDEwMCU7CiAgICAgIG1heC13aWR0aDogNDIwcHg7CiAgICAgIGJveC1zaGFkb3c6IDAgMTBweCAyNXB4IC01cHggcmdiYSgwLCAwLCAwLCAwLjQpOwogICAgfQogICAgLmF1dGgtY2FyZCBoMiB7CiAgICAgIGZvbnQtc2l6ZTogMS4zNXJlbTsKICAgICAgbWFyZ2luLWJvdHRvbTogMC41cmVtOwogICAgfQogICAgLmF1dGgtY2FyZCBwIHsKICAgICAgY29sb3I6IHZhcigtLXRleHQtbXV0ZWQpOwogICAgICBmb250LXNpemU6IDAuODc1cmVtOwogICAgICBtYXJnaW4tYm90dG9tOiAxLjVyZW07CiAgICB9CgogICAgLyogTWV0cmljcyBHcmlkICovCiAgICAubWV0cmljcy1ncmlkIHsKICAgICAgZGlzcGxheTogZ3JpZDsKICAgICAgZ3JpZC10ZW1wbGF0ZS1jb2x1bW5zOiByZXBlYXQoYXV0by1maXQsIG1pbm1heCgyMjBweCwgMWZyKSk7CiAgICAgIGdhcDogMXJlbTsKICAgICAgbWFyZ2luLWJvdHRvbTogMS41cmVtOwogICAgfQogICAgLm1ldHJpYy1jYXJkIHsKICAgICAgYmFja2dyb3VuZDogdmFyKC0tY2FyZC1iZyk7CiAgICAgIGJvcmRlcjogMXB4IHNvbGlkIHZhcigtLWJvcmRlcik7CiAgICAgIGJvcmRlci1yYWRpdXM6IDEwcHg7CiAgICAgIHBhZGRpbmc6IDEuMjVyZW07CiAgICAgIGRpc3BsYXk6IGZsZXg7CiAgICAgIGZsZXgtZGlyZWN0aW9uOiBjb2x1bW47CiAgICAgIGdhcDogMC40cmVtOwogICAgfQogICAgLm1ldHJpYy1sYWJlbCB7CiAgICAgIGZvbnQtc2l6ZTogMC44cmVtOwogICAgICB0ZXh0LXRyYW5zZm9ybTogdXBwZXJjYXNlOwogICAgICBmb250LXdlaWdodDogNjAwOwogICAgICBjb2xvcjogdmFyKC0tdGV4dC1tdXRlZCk7CiAgICAgIGxldHRlci1zcGFjaW5nOiAwLjA1ZW07CiAgICB9CiAgICAubWV0cmljLXZhbHVlIHsKICAgICAgZm9udC1zaXplOiAxLjc1cmVtOwogICAgICBmb250LXdlaWdodDogNzAwOwogICAgfQoKICAgIC8qIFRvb2xiYXIgKi8KICAgIC50b29sYmFyIHsKICAgICAgZGlzcGxheTogZmxleDsKICAgICAganVzdGlmeS1jb250ZW50OiBzcGFjZS1iZXR3ZWVuOwogICAgICBhbGlnbi1pdGVtczogY2VudGVyOwogICAgICBmbGV4LXdyYXA6IHdyYXA7CiAgICAgIGdhcDogMXJlbTsKICAgICAgbWFyZ2luLWJvdHRvbTogMS4yNXJlbTsKICAgIH0KICAgIC5zZWFyY2gtaW5wdXQgewogICAgICBiYWNrZ3JvdW5kOiB2YXIoLS1jYXJkLWJnKTsKICAgICAgYm9yZGVyOiAxcHggc29saWQgdmFyKC0tYm9yZGVyKTsKICAgICAgYm9yZGVyLXJhZGl1czogOHB4OwogICAgICBwYWRkaW5nOiAwLjZyZW0gMC44NXJlbTsKICAgICAgY29sb3I6IHZhcigtLXRleHQpOwogICAgICBmb250LXNpemU6IDAuODc1cmVtOwogICAgICBtaW4td2lkdGg6IDI2MHB4OwogICAgICBvdXRsaW5lOiBub25lOwogICAgICB0cmFuc2l0aW9uOiBib3JkZXItY29sb3IgMC4xNXM7CiAgICB9CiAgICAuc2VhcmNoLWlucHV0OmZvY3VzIHsKICAgICAgYm9yZGVyLWNvbG9yOiB2YXIoLS1ib3JkZXItZm9jdXMpOwogICAgfQogICAgLnRvb2xiYXItYnV0dG9ucyB7CiAgICAgIGRpc3BsYXk6IGZsZXg7CiAgICAgIGdhcDogMC41cmVtOwogICAgICBmbGV4LXdyYXA6IHdyYXA7CiAgICB9CgogICAgLyogQnV0dG9ucyAqLwogICAgLmJ0biB7CiAgICAgIGRpc3BsYXk6IGlubGluZS1mbGV4OwogICAgICBhbGlnbi1pdGVtczogY2VudGVyOwogICAgICBqdXN0aWZ5LWNvbnRlbnQ6IGNlbnRlcjsKICAgICAgZ2FwOiAwLjRyZW07CiAgICAgIHBhZGRpbmc6IDAuNTVyZW0gMC45cmVtOwogICAgICBib3JkZXItcmFkaXVzOiA4cHg7CiAgICAgIGZvbnQtc2l6ZTogMC44NXJlbTsKICAgICAgZm9udC13ZWlnaHQ6IDYwMDsKICAgICAgY3Vyc29yOiBwb2ludGVyOwogICAgICBib3JkZXI6IDFweCBzb2xpZCB0cmFuc3BhcmVudDsKICAgICAgdHJhbnNpdGlvbjogYWxsIDAuMTVzIGVhc2UtaW4tb3V0OwogICAgICB0ZXh0LWRlY29yYXRpb246IG5vbmU7CiAgICB9CiAgICAuYnRuLXByaW1hcnkgewogICAgICBiYWNrZ3JvdW5kOiB2YXIoLS1wcmltYXJ5KTsKICAgICAgY29sb3I6ICNmZmY7CiAgICB9CiAgICAuYnRuLXByaW1hcnk6aG92ZXIgewogICAgICBiYWNrZ3JvdW5kOiB2YXIoLS1wcmltYXJ5LWhvdmVyKTsKICAgIH0KICAgIC5idG4tb3V0bGluZSB7CiAgICAgIGJhY2tncm91bmQ6IHRyYW5zcGFyZW50OwogICAgICBib3JkZXItY29sb3I6IHZhcigtLWJvcmRlcik7CiAgICAgIGNvbG9yOiB2YXIoLS10ZXh0KTsKICAgIH0KICAgIC5idG4tb3V0bGluZTpob3ZlciB7CiAgICAgIGJhY2tncm91bmQ6IHZhcigtLWNhcmQtaG92ZXIpOwogICAgICBib3JkZXItY29sb3I6IHZhcigtLWJvcmRlci1mb2N1cyk7CiAgICB9CiAgICAuYnRuLXdhcm5pbmcgewogICAgICBiYWNrZ3JvdW5kOiB2YXIoLS13YXJuaW5nLWJnKTsKICAgICAgYm9yZGVyLWNvbG9yOiByZ2JhKDI0NSwgMTU4LCAxMSwgMC4zKTsKICAgICAgY29sb3I6IHZhcigtLXdhcm5pbmcpOwogICAgfQogICAgLmJ0bi13YXJuaW5nOmhvdmVyIHsKICAgICAgYmFja2dyb3VuZDogcmdiYSgyNDUsIDE1OCwgMTEsIDAuMjIpOwogICAgfQogICAgLmJ0bi1kYW5nZXIgewogICAgICBiYWNrZ3JvdW5kOiB2YXIoLS1kYW5nZXItYmcpOwogICAgICBib3JkZXItY29sb3I6IHJnYmEoMjM5LCA2OCwgNjgsIDAuMyk7CiAgICAgIGNvbG9yOiB2YXIoLS1kYW5nZXIpOwogICAgfQogICAgLmJ0bi1kYW5nZXI6aG92ZXIgewogICAgICBiYWNrZ3JvdW5kOiByZ2JhKDIzOSwgNjgsIDY4LCAwLjIyKTsKICAgIH0KICAgIC5idG4tc20gewogICAgICBwYWRkaW5nOiAwLjM1cmVtIDAuNjVyZW07CiAgICAgIGZvbnQtc2l6ZTogMC43NzVyZW07CiAgICAgIGJvcmRlci1yYWRpdXM6IDZweDsKICAgIH0KCiAgICAvKiBUYWJsZSAqLwogICAgLnRhYmxlLWNhcmQgewogICAgICBiYWNrZ3JvdW5kOiB2YXIoLS1jYXJkLWJnKTsKICAgICAgYm9yZGVyOiAxcHggc29saWQgdmFyKC0tYm9yZGVyKTsKICAgICAgYm9yZGVyLXJhZGl1czogMTBweDsKICAgICAgb3ZlcmZsb3c6IGhpZGRlbjsKICAgICAgYm94LXNoYWRvdzogMCA0cHggNnB4IC0xcHggcmdiYSgwLCAwLCAwLCAwLjEpOwogICAgfQogICAgdGFibGUgewogICAgICB3aWR0aDogMTAwJTsKICAgICAgYm9yZGVyLWNvbGxhcHNlOiBjb2xsYXBzZTsKICAgICAgdGV4dC1hbGlnbjogbGVmdDsKICAgIH0KICAgIHRoIHsKICAgICAgYmFja2dyb3VuZDogcmdiYSgxMSwgMTUsIDIzLCAwLjUpOwogICAgICBwYWRkaW5nOiAwLjg1cmVtIDFyZW07CiAgICAgIGZvbnQtc2l6ZTogMC43NXJlbTsKICAgICAgdGV4dC10cmFuc2Zvcm06IHVwcGVyY2FzZTsKICAgICAgbGV0dGVyLXNwYWNpbmc6IDAuMDVlbTsKICAgICAgY29sb3I6IHZhcigtLXRleHQtbXV0ZWQpOwogICAgICBib3JkZXItYm90dG9tOiAxcHggc29saWQgdmFyKC0tYm9yZGVyKTsKICAgICAgZm9udC13ZWlnaHQ6IDYwMDsKICAgIH0KICAgIHRkIHsKICAgICAgcGFkZGluZzogMC45cmVtIDFyZW07CiAgICAgIGJvcmRlci1ib3R0b206IDFweCBzb2xpZCB2YXIoLS1ib3JkZXIpOwogICAgICBmb250LXNpemU6IDAuODc1cmVtOwogICAgICB2ZXJ0aWNhbC1hbGlnbjogbWlkZGxlOwogICAgfQogICAgdHI6bGFzdC1jaGlsZCB0ZCB7IGJvcmRlci1ib3R0b206IG5vbmU7IH0KICAgIHRyOmhvdmVyIHRkIHsgYmFja2dyb3VuZDogdmFyKC0tY2FyZC1ob3Zlcik7IH0KCiAgICAudXNlci1waWxsIHsKICAgICAgZm9udC1mYW1pbHk6IG1vbm9zcGFjZTsKICAgICAgZm9udC13ZWlnaHQ6IDcwMDsKICAgICAgY29sb3I6ICMzOGJkZjg7CiAgICAgIGZvbnQtc2l6ZTogMC45NXJlbTsKICAgIH0KICAgIC5wYXNzLWNlbGwgewogICAgICBkaXNwbGF5OiBmbGV4OwogICAgICBhbGlnbi1pdGVtczogY2VudGVyOwogICAgICBnYXA6IDAuNXJlbTsKICAgICAgZm9udC1mYW1pbHk6IG1vbm9zcGFjZTsKICAgIH0KICAgIC5wYXNzLWhpZGRlbiB7CiAgICAgIGNvbG9yOiB2YXIoLS10ZXh0LW11dGVkKTsKICAgICAgbGV0dGVyLXNwYWNpbmc6IDAuMTVlbTsKICAgIH0KCiAgICAuc3RhdHVzLWJhZGdlIHsKICAgICAgZGlzcGxheTogaW5saW5lLWZsZXg7CiAgICAgIGFsaWduLWl0ZW1zOiBjZW50ZXI7CiAgICAgIGdhcDogMC4zNXJlbTsKICAgICAgcGFkZGluZzogMC4yNXJlbSAwLjZyZW07CiAgICAgIGJvcmRlci1yYWRpdXM6IDk5OTlweDsKICAgICAgZm9udC1zaXplOiAwLjc1cmVtOwogICAgICBmb250LXdlaWdodDogNjAwOwogICAgfQogICAgLnN0YXR1cy1iYWRnZS5ib3VuZCB7CiAgICAgIGJhY2tncm91bmQ6IHJnYmEoNTksIDEzMCwgMjQ2LCAwLjE1KTsKICAgICAgY29sb3I6ICM2MGE1ZmE7CiAgICAgIGJvcmRlcjogMXB4IHNvbGlkIHJnYmEoNTksIDEzMCwgMjQ2LCAwLjMpOwogICAgfQogICAgLnN0YXR1cy1iYWRnZS51bmJvdW5kIHsKICAgICAgYmFja2dyb3VuZDogdmFyKC0tc3VjY2Vzcy1iZyk7CiAgICAgIGNvbG9yOiB2YXIoLS1zdWNjZXNzKTsKICAgICAgYm9yZGVyOiAxcHggc29saWQgcmdiYSgxNiwgMTg1LCAxMjksIDAuMyk7CiAgICB9CiAgICAuc3RhdHVzLWJhZGdlLmFjdGl2ZSB7CiAgICAgIGJhY2tncm91bmQ6IHZhcigtLXN1Y2Nlc3MtYmcpOwogICAgICBjb2xvcjogdmFyKC0tc3VjY2Vzcyk7CiAgICB9CiAgICAuc3RhdHVzLWJhZGdlLnN1c3BlbmRlZCB7CiAgICAgIGJhY2tncm91bmQ6IHZhcigtLWRhbmdlci1iZyk7CiAgICAgIGNvbG9yOiB2YXIoLS1kYW5nZXIpOwogICAgfQoKICAgIC50YWJsZS1hY3Rpb25zIHsKICAgICAgZGlzcGxheTogZmxleDsKICAgICAgZ2FwOiAwLjRyZW07CiAgICAgIGFsaWduLWl0ZW1zOiBjZW50ZXI7CiAgICB9CgogICAgLyogTW9kYWwgKi8KICAgIC5tb2RhbC1iYWNrZHJvcCB7CiAgICAgIHBvc2l0aW9uOiBmaXhlZDsKICAgICAgaW5zZXQ6IDA7CiAgICAgIGJhY2tncm91bmQ6IHJnYmEoMCwgMCwgMCwgMC43KTsKICAgICAgYmFja2Ryb3AtZmlsdGVyOiBibHVyKDRweCk7CiAgICAgIGRpc3BsYXk6IGZsZXg7CiAgICAgIGFsaWduLWl0ZW1zOiBjZW50ZXI7CiAgICAgIGp1c3RpZnktY29udGVudDogY2VudGVyOwogICAgICB6LWluZGV4OiAxMDAwOwogICAgICBvcGFjaXR5OiAwOwogICAgICBwb2ludGVyLWV2ZW50czogbm9uZTsKICAgICAgdHJhbnNpdGlvbjogb3BhY2l0eSAwLjJzIGVhc2U7CiAgICB9CiAgICAubW9kYWwtYmFja2Ryb3Aub3BlbiB7CiAgICAgIG9wYWNpdHk6IDE7CiAgICAgIHBvaW50ZXItZXZlbnRzOiBhdXRvOwogICAgfQogICAgLm1vZGFsIHsKICAgICAgYmFja2dyb3VuZDogdmFyKC0tY2FyZC1iZyk7CiAgICAgIGJvcmRlcjogMXB4IHNvbGlkIHZhcigtLWJvcmRlcik7CiAgICAgIGJvcmRlci1yYWRpdXM6IDEycHg7CiAgICAgIHBhZGRpbmc6IDEuNzVyZW07CiAgICAgIHdpZHRoOiAxMDAlOwogICAgICBtYXgtd2lkdGg6IDQ0MHB4OwogICAgICBib3gtc2hhZG93OiAwIDIwcHggMjVweCAtNXB4IHJnYmEoMCwgMCwgMCwgMC41KTsKICAgIH0KICAgIC5tb2RhbCBoMyB7CiAgICAgIGZvbnQtc2l6ZTogMS4xNXJlbTsKICAgICAgbWFyZ2luLWJvdHRvbTogMXJlbTsKICAgIH0KICAgIC5mb3JtLWdyb3VwIHsKICAgICAgbWFyZ2luLWJvdHRvbTogMXJlbTsKICAgIH0KICAgIC5mb3JtLWdyb3VwIGxhYmVsIHsKICAgICAgZGlzcGxheTogYmxvY2s7CiAgICAgIGZvbnQtc2l6ZTogMC44cmVtOwogICAgICBmb250LXdlaWdodDogNjAwOwogICAgICBjb2xvcjogdmFyKC0tdGV4dC1tdXRlZCk7CiAgICAgIG1hcmdpbi1ib3R0b206IDAuNHJlbTsKICAgICAgdGV4dC10cmFuc2Zvcm06IHVwcGVyY2FzZTsKICAgICAgbGV0dGVyLXNwYWNpbmc6IDAuMDRlbTsKICAgIH0KICAgIC5mb3JtLWlucHV0IHsKICAgICAgd2lkdGg6IDEwMCU7CiAgICAgIGJhY2tncm91bmQ6IHZhcigtLWJnKTsKICAgICAgYm9yZGVyOiAxcHggc29saWQgdmFyKC0tYm9yZGVyKTsKICAgICAgYm9yZGVyLXJhZGl1czogOHB4OwogICAgICBwYWRkaW5nOiAwLjY1cmVtIDAuODVyZW07CiAgICAgIGNvbG9yOiB2YXIoLS10ZXh0KTsKICAgICAgZm9udC1zaXplOiAwLjlyZW07CiAgICAgIG91dGxpbmU6IG5vbmU7CiAgICAgIHRyYW5zaXRpb246IGJvcmRlci1jb2xvciAwLjE1czsKICAgIH0KICAgIC5mb3JtLWlucHV0OmZvY3VzIHsgYm9yZGVyLWNvbG9yOiB2YXIoLS1ib3JkZXItZm9jdXMpOyB9CiAgICAubW9kYWwtZm9vdGVyIHsKICAgICAgZGlzcGxheTogZmxleDsKICAgICAganVzdGlmeS1jb250ZW50OiBmbGV4LWVuZDsKICAgICAgZ2FwOiAwLjZyZW07CiAgICAgIG1hcmdpbi10b3A6IDEuNXJlbTsKICAgIH0KCiAgICAvKiBUb2FzdCAqLwogICAgI3RvYXN0LWNvbnRhaW5lciB7CiAgICAgIHBvc2l0aW9uOiBmaXhlZDsKICAgICAgYm90dG9tOiAxLjVyZW07CiAgICAgIHJpZ2h0OiAxLjVyZW07CiAgICAgIGRpc3BsYXk6IGZsZXg7CiAgICAgIGZsZXgtZGlyZWN0aW9uOiBjb2x1bW47CiAgICAgIGdhcDogMC41cmVtOwogICAgICB6LWluZGV4OiAyMDAwOwogICAgfQogICAgLnRvYXN0IHsKICAgICAgYmFja2dyb3VuZDogIzFlMjkzYjsKICAgICAgYm9yZGVyOiAxcHggc29saWQgdmFyKC0tYm9yZGVyKTsKICAgICAgY29sb3I6ICNmZmY7CiAgICAgIHBhZGRpbmc6IDAuNzVyZW0gMS4xcmVtOwogICAgICBib3JkZXItcmFkaXVzOiA4cHg7CiAgICAgIGZvbnQtc2l6ZTogMC44NXJlbTsKICAgICAgYm94LXNoYWRvdzogMCAxMHB4IDE1cHggLTNweCByZ2JhKDAsIDAsIDAsIDAuNCk7CiAgICAgIGRpc3BsYXk6IGZsZXg7CiAgICAgIGFsaWduLWl0ZW1zOiBjZW50ZXI7CiAgICAgIGdhcDogMC41cmVtOwogICAgICBhbmltYXRpb246IHNsaWRlSW4gMC4ycyBlYXNlLW91dDsKICAgIH0KICAgIC50b2FzdC5zdWNjZXNzIHsgYm9yZGVyLWNvbG9yOiB2YXIoLS1zdWNjZXNzKTsgfQogICAgLnRvYXN0LmVycm9yIHsgYm9yZGVyLWNvbG9yOiB2YXIoLS1kYW5nZXIpOyB9CiAgICBAa2V5ZnJhbWVzIHNsaWRlSW4gewogICAgICBmcm9tIHsgdHJhbnNmb3JtOiB0cmFuc2xhdGVZKDIwcHgpOyBvcGFjaXR5OiAwOyB9CiAgICAgIHRvIHsgdHJhbnNmb3JtOiB0cmFuc2xhdGVZKDApOyBvcGFjaXR5OiAxOyB9CiAgICB9CgogICAgLyogRW1wdHkgc3RhdGUgKi8KICAgIC5lbXB0eS1zdGF0ZSB7CiAgICAgIHBhZGRpbmc6IDNyZW07CiAgICAgIHRleHQtYWxpZ246IGNlbnRlcjsKICAgICAgY29sb3I6IHZhcigtLXRleHQtbXV0ZWQpOwogICAgICBmb250LXNpemU6IDAuOTVyZW07CiAgICB9CiAgPC9zdHlsZT4KPC9oZWFkPgo8Ym9keT4KCiAgPGhlYWRlcj4KICAgIDxkaXYgY2xhc3M9ImhlYWRlci10aXRsZSI+CiAgICAgIDxoMT5BQ2xpZW50IEFjY291bnRzIE1hbmFnZXI8L2gxPgogICAgICA8c3BhbiBjbGFzcz0iYmFkZ2Utc3RhdHVzIj5DbG91ZGZsYXJlIEtWPC9zcGFuPgogICAgPC9kaXY+CiAgICA8ZGl2IGNsYXNzPSJoZWFkZXItYWN0aW9ucyIgaWQ9ImhlYWRlci1hY3Rpb25zIiBzdHlsZT0iZGlzcGxheTogbm9uZTsiPgogICAgICA8YnV0dG9uIGNsYXNzPSJidG4gYnRuLW91dGxpbmUgYnRuLXNtIiBvbmNsaWNrPSJwcm9tcHRDaGFuZ2VLZXkoKSI+Q2FtYmlhciBDbGF2ZTwvYnV0dG9uPgogICAgICA8YnV0dG9uIGNsYXNzPSJidG4gYnRuLW91dGxpbmUgYnRuLXNtIiBvbmNsaWNrPSJsb2dvdXRBZG1pbigpIj5DZXJyYXIgU2VzaW9uPC9idXR0b24+CiAgICA8L2Rpdj4KICA8L2hlYWRlcj4KCiAgPGRpdiBjbGFzcz0iY29udGFpbmVyIj4KICAgIDwhLS0gUGFudGFsbGEgZGUgTG9naW4gY29uIENsYXZlIEFkbWluIC0tPgogICAgPGRpdiBpZD0iYXV0aC1zY3JlZW4iPgogICAgICA8ZGl2IGNsYXNzPSJhdXRoLWNhcmQiPgogICAgICAgIDxoMj5QYW5lbCBkZSBBZG1pbmlzdHJhY2lvbjwvaDI+CiAgICAgICAgPHA+SW5ncmVzYSB0dSBjbGF2ZSBkZSBhZG1pbmlzdHJhZG9yIHBhcmEgZ2VzdGlvbmFyIGxhcyBjdWVudGFzIGRlIEFDbGllbnQuPC9wPgogICAgICAgIDxkaXYgY2xhc3M9ImZvcm0tZ3JvdXAiPgogICAgICAgICAgPGxhYmVsPkNsYXZlIE1hZXN0cmE8L2xhYmVsPgogICAgICAgICAgPGlucHV0IHR5cGU9InBhc3N3b3JkIiBpZD0iYWRtaW4ta2V5LWlucHV0IiBjbGFzcz0iZm9ybS1pbnB1dCIgcGxhY2Vob2xkZXI9IkNsYXZlIHNlY3JldGEiIGF1dG9mb2N1cyBvbmtleWRvd249ImlmKGV2ZW50LmtleT09PSdFbnRlcicpIGxvZ2luQWRtaW4oKSI+CiAgICAgICAgPC9kaXY+CiAgICAgICAgPGJ1dHRvbiBjbGFzcz0iYnRuIGJ0bi1wcmltYXJ5IiBzdHlsZT0id2lkdGg6IDEwMCU7IiBvbmNsaWNrPSJsb2dpbkFkbWluKCkiPkluZ3Jlc2FyIGFsIFBhbmVsPC9idXR0b24+CiAgICAgIDwvZGl2PgogICAgPC9kaXY+CgogICAgPCEtLSBQYW50YWxsYSBQcmluY2lwYWwgZGVsIERhc2hib2FyZCAtLT4KICAgIDxkaXYgaWQ9ImRhc2hib2FyZC1zY3JlZW4iIHN0eWxlPSJkaXNwbGF5OiBub25lOyI+CiAgICAgIAogICAgICA8IS0tIE1ldHJpY2FzIC0tPgogICAgICA8ZGl2IGNsYXNzPSJtZXRyaWNzLWdyaWQiPgogICAgICAgIDxkaXYgY2xhc3M9Im1ldHJpYy1jYXJkIj4KICAgICAgICAgIDxzcGFuIGNsYXNzPSJtZXRyaWMtbGFiZWwiPlRvdGFsIEN1ZW50YXM8L3NwYW4+CiAgICAgICAgICA8c3BhbiBjbGFzcz0ibWV0cmljLXZhbHVlIiBpZD0ibWV0cmljLXRvdGFsIj4wPC9zcGFuPgogICAgICAgIDwvZGl2PgogICAgICAgIDxkaXYgY2xhc3M9Im1ldHJpYy1jYXJkIj4KICAgICAgICAgIDxzcGFuIGNsYXNzPSJtZXRyaWMtbGFiZWwiPkN1ZW50YXMgQWN0aXZhczwvc3Bhbj4KICAgICAgICAgIDxzcGFuIGNsYXNzPSJtZXRyaWMtdmFsdWUiIHN0eWxlPSJjb2xvcjogdmFyKC0tc3VjY2Vzcyk7IiBpZD0ibWV0cmljLWFjdGl2ZSI+MDwvc3Bhbj4KICAgICAgICA8L2Rpdj4KICAgICAgICA8ZGl2IGNsYXNzPSJtZXRyaWMtY2FyZCI+CiAgICAgICAgICA8c3BhbiBjbGFzcz0ibWV0cmljLWxhYmVsIj5IV0lEcyBWaW5jdWxhZG9zPC9zcGFuPgogICAgICAgICAgPHNwYW4gY2xhc3M9Im1ldHJpYy12YWx1ZSIgc3R5bGU9ImNvbG9yOiAjNjBhNWZhOyIgaWQ9Im1ldHJpYy1ib3VuZCI+MDwvc3Bhbj4KICAgICAgICA8L2Rpdj4KICAgICAgICA8ZGl2IGNsYXNzPSJtZXRyaWMtY2FyZCI+CiAgICAgICAgICA8c3BhbiBjbGFzcz0ibWV0cmljLWxhYmVsIj5IV0lEcyBMaWJyZXM8L3NwYW4+CiAgICAgICAgICA8c3BhbiBjbGFzcz0ibWV0cmljLXZhbHVlIiBzdHlsZT0iY29sb3I6IHZhcigtLXdhcm5pbmcpOyIgaWQ9Im1ldHJpYy11bmJvdW5kIj4wPC9zcGFuPgogICAgICAgIDwvZGl2PgogICAgICA8L2Rpdj4KCiAgICAgIDwhLS0gQmFycmEgZGUgaGVycmFtaWVudGFzIC0tPgogICAgICA8ZGl2IGNsYXNzPSJ0b29sYmFyIj4KICAgICAgICA8aW5wdXQgdHlwZT0idGV4dCIgaWQ9InNlYXJjaC1pbnB1dCIgY2xhc3M9InNlYXJjaC1pbnB1dCIgcGxhY2Vob2xkZXI9IkJ1c2NhciBwb3IgdXN1YXJpby4uLiIgb25pbnB1dD0iZmlsdGVyVXNlcnMoKSI+CiAgICAgICAgPGRpdiBjbGFzcz0idG9vbGJhci1idXR0b25zIj4KICAgICAgICAgIDxidXR0b24gY2xhc3M9ImJ0biBidG4tcHJpbWFyeSIgb25jbGljaz0ib3BlbkNyZWF0ZU1vZGFsKCkiPisgTnVldmEgQ3VlbnRhPC9idXR0b24+CiAgICAgICAgICA8YnV0dG9uIGNsYXNzPSJidG4gYnRuLXdhcm5pbmciIG9uY2xpY2s9InJlc2V0QWxsSHdpZHMoKSI+QmxhbnF1ZWFyIFRvZG9zIGxvcyBIV0lEczwvYnV0dG9uPgogICAgICAgICAgPGJ1dHRvbiBjbGFzcz0iYnRuIGJ0bi1vdXRsaW5lIiBvbmNsaWNrPSJsb2FkVXNlcnMoKSI+UmVmcmVzY2FyPC9idXR0b24+CiAgICAgICAgPC9kaXY+CiAgICAgIDwvZGl2PgoKICAgICAgPCEtLSBUYWJsYSBkZSBDdWVudGFzIC0tPgogICAgICA8ZGl2IGNsYXNzPSJ0YWJsZS1jYXJkIj4KICAgICAgICA8dGFibGU+CiAgICAgICAgICA8dGhlYWQ+CiAgICAgICAgICAgIDx0cj4KICAgICAgICAgICAgICA8dGg+VXN1YXJpbzwvdGg+CiAgICAgICAgICAgICAgPHRoPkNvbnRyYXNlbmE8L3RoPgogICAgICAgICAgICAgIDx0aD5Fc3RhZG8gSFdJRDwvdGg+CiAgICAgICAgICAgICAgPHRoPkFjY2VzbzwvdGg+CiAgICAgICAgICAgICAgPHRoPkFjY2lvbmVzPC90aD4KICAgICAgICAgICAgPC90cj4KICAgICAgICAgIDwvdGhlYWQ+CiAgICAgICAgICA8dGJvZHkgaWQ9InVzZXJzLXRib2R5Ij4KICAgICAgICAgICAgPHRyPgogICAgICAgICAgICAgIDx0ZCBjb2xzcGFuPSI1IiBjbGFzcz0iZW1wdHktc3RhdGUiPkNhcmdhbmRvIGN1ZW50YXMuLi48L3RkPgogICAgICAgICAgICA8L3RyPgogICAgICAgICAgPC90Ym9keT4KICAgICAgICA8L3RhYmxlPgogICAgICA8L2Rpdj4KCiAgICA8L2Rpdj4KICA8L2Rpdj4KCiAgPCEtLSBNb2RhbCBDcmVhci9FZGl0YXIgVXN1YXJpbyAtLT4KICA8ZGl2IGNsYXNzPSJtb2RhbC1iYWNrZHJvcCIgaWQ9InVzZXItbW9kYWwiPgogICAgPGRpdiBjbGFzcz0ibW9kYWwiPgogICAgICA8aDMgaWQ9Im1vZGFsLXRpdGxlIj5OdWV2YSBDdWVudGE8L2gzPgogICAgICA8ZGl2IGNsYXNzPSJmb3JtLWdyb3VwIj4KICAgICAgICA8bGFiZWw+VXN1YXJpbzwvbGFiZWw+CiAgICAgICAgPGlucHV0IHR5cGU9InRleHQiIGlkPSJtb2RhbC11c2VyIiBjbGFzcz0iZm9ybS1pbnB1dCIgcGxhY2Vob2xkZXI9IkVqOiBhZ3VzIj4KICAgICAgPC9kaXY+CiAgICAgIDxkaXYgY2xhc3M9ImZvcm0tZ3JvdXAiPgogICAgICAgIDxsYWJlbD5Db250cmFzZW5hPC9sYWJlbD4KICAgICAgICA8aW5wdXQgdHlwZT0idGV4dCIgaWQ9Im1vZGFsLXBhc3MiIGNsYXNzPSJmb3JtLWlucHV0IiBwbGFjZWhvbGRlcj0iRWo6IDEyMyI+CiAgICAgIDwvZGl2PgogICAgICA8ZGl2IGNsYXNzPSJmb3JtLWdyb3VwIiBzdHlsZT0iZGlzcGxheTogZmxleDsgYWxpZ24taXRlbXM6IGNlbnRlcjsgZ2FwOiAwLjVyZW07IG1hcmdpbi10b3A6IDFyZW07Ij4KICAgICAgICA8aW5wdXQgdHlwZT0iY2hlY2tib3giIGlkPSJtb2RhbC1hY3RpdmUiIGNoZWNrZWQgc3R5bGU9IndpZHRoOiAxLjFyZW07IGhlaWdodDogMS4xcmVtOyBhY2NlbnQtY29sb3I6IHZhcigtLXByaW1hcnkpOyI+CiAgICAgICAgPGxhYmVsIGZvcj0ibW9kYWwtYWN0aXZlIiBzdHlsZT0ibWFyZ2luOiAwOyBjdXJzb3I6IHBvaW50ZXI7IHRleHQtdHJhbnNmb3JtOiBub25lOyBmb250LXNpemU6IDAuOXJlbTsiPkN1ZW50YSBBY3RpdmEgKHBlcm1pdGlyIGFjY2Vzbyk8L2xhYmVsPgogICAgICA8L2Rpdj4KICAgICAgPGRpdiBjbGFzcz0ibW9kYWwtZm9vdGVyIj4KICAgICAgICA8YnV0dG9uIGNsYXNzPSJidG4gYnRuLW91dGxpbmUiIG9uY2xpY2s9ImNsb3NlVXNlck1vZGFsKCkiPkNhbmNlbGFyPC9idXR0b24+CiAgICAgICAgPGJ1dHRvbiBjbGFzcz0iYnRuIGJ0bi1wcmltYXJ5IiBvbmNsaWNrPSJzYXZlVXNlcigpIj5HdWFyZGFyIEN1ZW50YTwvYnV0dG9uPgogICAgICA8L2Rpdj4KICAgIDwvZGl2PgogIDwvZGl2PgoKICA8ZGl2IGlkPSJ0b2FzdC1jb250YWluZXIiPjwvZGl2PgoKICA8c2NyaXB0PgogICAgLy8gVVJMIGJhc2UgZGVsIGJhY2tlbmQgQ2xvdWRmbGFyZSBXb3JrZXIKICAgIGNvbnN0IEFQSV9CQVNFID0gd2luZG93LmxvY2F0aW9uLm9yaWdpbi5pbmNsdWRlcygnd29ya2Vycy5kZXYnKSAKICAgICAgPyB3aW5kb3cubG9jYXRpb24ub3JpZ2luIAogICAgICA6ICdodHRwczovL2RyeS13YXRlci1lNzQ4LmFndXMtbWF5YTUyLndvcmtlcnMuZGV2JzsKCiAgICBsZXQgZ19BZG1pbktleSA9IGxvY2FsU3RvcmFnZS5nZXRJdGVtKCdhY2xpZW50X2FkbWluX2tleScpIHx8ICcnOwogICAgbGV0IGdfVXNlcnMgPSBbXTsKICAgIGxldCBnX0lzRWRpdGluZyA9IGZhbHNlOwoKICAgIC8vIEluaWNpYWxpemFjaW9uCiAgICB3aW5kb3cuYWRkRXZlbnRMaXN0ZW5lcignRE9NQ29udGVudExvYWRlZCcsICgpID0+IHsKICAgICAgaWYgKGdfQWRtaW5LZXkpIHsKICAgICAgICB2ZXJpZnlBbmRMb2FkKCk7CiAgICAgIH0KICAgIH0pOwoKICAgIGZ1bmN0aW9uIHNob3dUb2FzdChtZXNzYWdlLCB0eXBlID0gJ3N1Y2Nlc3MnKSB7CiAgICAgIGNvbnN0IGMgPSBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgndG9hc3QtY29udGFpbmVyJyk7CiAgICAgIGNvbnN0IHQgPSBkb2N1bWVudC5jcmVhdGVFbGVtZW50KCdkaXYnKTsKICAgICAgdC5jbGFzc05hbWUgPSBgdG9hc3QgJHt0eXBlfWA7CiAgICAgIHQuaW5uZXJUZXh0ID0gbWVzc2FnZTsKICAgICAgYy5hcHBlbmRDaGlsZCh0KTsKICAgICAgc2V0VGltZW91dCgoKSA9PiB7CiAgICAgICAgdC5zdHlsZS5vcGFjaXR5ID0gJzAnOwogICAgICAgIHNldFRpbWVvdXQoKCkgPT4gdC5yZW1vdmUoKSwgMjAwKTsKICAgICAgfSwgMzAwMCk7CiAgICB9CgogICAgZnVuY3Rpb24gbG9naW5BZG1pbigpIHsKICAgICAgY29uc3QgaW5wdXQgPSBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgnYWRtaW4ta2V5LWlucHV0Jyk7CiAgICAgIGNvbnN0IGtleSA9IGlucHV0LnZhbHVlLnRyaW0oKTsKICAgICAgaWYgKCFrZXkpIHJldHVybjsKICAgICAgZ19BZG1pbktleSA9IGtleTsKICAgICAgbG9jYWxTdG9yYWdlLnNldEl0ZW0oJ2FjbGllbnRfYWRtaW5fa2V5JywgZ19BZG1pbktleSk7CiAgICAgIHZlcmlmeUFuZExvYWQoKTsKICAgIH0KCiAgICBmdW5jdGlvbiBsb2dvdXRBZG1pbigpIHsKICAgICAgbG9jYWxTdG9yYWdlLnJlbW92ZUl0ZW0oJ2FjbGllbnRfYWRtaW5fa2V5Jyk7CiAgICAgIGdfQWRtaW5LZXkgPSAnJzsKICAgICAgZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoJ2F1dGgtc2NyZWVuJykuc3R5bGUuZGlzcGxheSA9ICdmbGV4JzsKICAgICAgZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoJ2Rhc2hib2FyZC1zY3JlZW4nKS5zdHlsZS5kaXNwbGF5ID0gJ25vbmUnOwogICAgICBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgnaGVhZGVyLWFjdGlvbnMnKS5zdHlsZS5kaXNwbGF5ID0gJ25vbmUnOwogICAgICBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgnYWRtaW4ta2V5LWlucHV0JykudmFsdWUgPSAnJzsKICAgIH0KCiAgICBmdW5jdGlvbiBwcm9tcHRDaGFuZ2VLZXkoKSB7CiAgICAgIGNvbnN0IG4gPSBwcm9tcHQoJ0luZ3Jlc2EgbGEgbnVldmEgY2xhdmUgZGUgYWRtaW5pc3RyYWRvcjonLCBnX0FkbWluS2V5KTsKICAgICAgaWYgKG4gIT09IG51bGwgJiYgbi50cmltKCkgIT09ICcnKSB7CiAgICAgICAgZ19BZG1pbktleSA9IG4udHJpbSgpOwogICAgICAgIGxvY2FsU3RvcmFnZS5zZXRJdGVtKCdhY2xpZW50X2FkbWluX2tleScsIGdfQWRtaW5LZXkpOwogICAgICAgIGxvYWRVc2VycygpOwogICAgICB9CiAgICB9CgogICAgYXN5bmMgZnVuY3Rpb24gdmVyaWZ5QW5kTG9hZCgpIHsKICAgICAgdHJ5IHsKICAgICAgICBjb25zdCByZXMgPSBhd2FpdCBmZXRjaChgJHtBUElfQkFTRX0vYXBpL2FkbWluL3VzZXJzYCwgewogICAgICAgICAgaGVhZGVyczogeyAnWC1BZG1pbi1LZXknOiBnX0FkbWluS2V5IH0KICAgICAgICB9KTsKICAgICAgICBpZiAocmVzLnN0YXR1cyA9PT0gNDAxKSB7CiAgICAgICAgICBzaG93VG9hc3QoJ0NsYXZlIGRlIGFkbWluaXN0cmFkb3IgaW5jb3JyZWN0YScsICdlcnJvcicpOwogICAgICAgICAgbG9nb3V0QWRtaW4oKTsKICAgICAgICAgIHJldHVybjsKICAgICAgICB9CiAgICAgICAgY29uc3QgZGF0YSA9IGF3YWl0IHJlcy5qc29uKCk7CiAgICAgICAgaWYgKGRhdGEuc3RhdHVzID09PSAnb2snKSB7CiAgICAgICAgICBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgnYXV0aC1zY3JlZW4nKS5zdHlsZS5kaXNwbGF5ID0gJ25vbmUnOwogICAgICAgICAgZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoJ2Rhc2hib2FyZC1zY3JlZW4nKS5zdHlsZS5kaXNwbGF5ID0gJ2Jsb2NrJzsKICAgICAgICAgIGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCdoZWFkZXItYWN0aW9ucycpLnN0eWxlLmRpc3BsYXkgPSAnZmxleCc7CiAgICAgICAgICByZW5kZXJVc2VycyhkYXRhLnVzZXJzIHx8IFtdKTsKICAgICAgICB9IGVsc2UgewogICAgICAgICAgc2hvd1RvYXN0KGRhdGEubWVzc2FnZSB8fCAnRXJyb3IgYWwgY29uZWN0YXInLCAnZXJyb3InKTsKICAgICAgICB9CiAgICAgIH0gY2F0Y2ggKGUpIHsKICAgICAgICBzaG93VG9hc3QoJ0Vycm9yIGNvbmVjdGFuZG8gYWwgc2Vydmlkb3IgQ2xvdWRmbGFyZScsICdlcnJvcicpOwogICAgICB9CiAgICB9CgogICAgYXN5bmMgZnVuY3Rpb24gbG9hZFVzZXJzKCkgewogICAgICB0cnkgewogICAgICAgIGNvbnN0IHJlcyA9IGF3YWl0IGZldGNoKGAke0FQSV9CQVNFfS9hcGkvYWRtaW4vdXNlcnNgLCB7CiAgICAgICAgICBoZWFkZXJzOiB7ICdYLUFkbWluLUtleSc6IGdfQWRtaW5LZXkgfQogICAgICAgIH0pOwogICAgICAgIGNvbnN0IGRhdGEgPSBhd2FpdCByZXMuanNvbigpOwogICAgICAgIGlmIChkYXRhLnN0YXR1cyA9PT0gJ29rJykgewogICAgICAgICAgcmVuZGVyVXNlcnMoZGF0YS51c2VycyB8fCBbXSk7CiAgICAgICAgfSBlbHNlIHsKICAgICAgICAgIHNob3dUb2FzdChkYXRhLm1lc3NhZ2UsICdlcnJvcicpOwogICAgICAgIH0KICAgICAgfSBjYXRjaCAoZSkgewogICAgICAgIHNob3dUb2FzdCgnRXJyb3IgYWwgcmVjYXJnYXIgdXN1YXJpb3MnLCAnZXJyb3InKTsKICAgICAgfQogICAgfQoKICAgIGZ1bmN0aW9uIHJlbmRlclVzZXJzKHVzZXJzKSB7CiAgICAgIGdfVXNlcnMgPSB1c2VyczsKICAgICAgdXBkYXRlTWV0cmljcygpOwogICAgICBmaWx0ZXJVc2VycygpOwogICAgfQoKICAgIGZ1bmN0aW9uIHVwZGF0ZU1ldHJpY3MoKSB7CiAgICAgIGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCdtZXRyaWMtdG90YWwnKS5pbm5lclRleHQgPSBnX1VzZXJzLmxlbmd0aDsKICAgICAgZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoJ21ldHJpYy1hY3RpdmUnKS5pbm5lclRleHQgPSBnX1VzZXJzLmZpbHRlcih1ID0+IHUuYWN0aXZlICE9PSBmYWxzZSkubGVuZ3RoOwogICAgICBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgnbWV0cmljLWJvdW5kJykuaW5uZXJUZXh0ID0gZ19Vc2Vycy5maWx0ZXIodSA9PiB1Lmh3aWQpLmxlbmd0aDsKICAgICAgZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoJ21ldHJpYy11bmJvdW5kJykuaW5uZXJUZXh0ID0gZ19Vc2Vycy5maWx0ZXIodSA9PiAhdS5od2lkKS5sZW5ndGg7CiAgICB9CgogICAgZnVuY3Rpb24gZmlsdGVyVXNlcnMoKSB7CiAgICAgIGNvbnN0IHEgPSBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgnc2VhcmNoLWlucHV0JykudmFsdWUudG9Mb3dlckNhc2UoKS50cmltKCk7CiAgICAgIGNvbnN0IGZpbHRlcmVkID0gZ19Vc2Vycy5maWx0ZXIodSA9PiB1LnVzZXIudG9Mb3dlckNhc2UoKS5pbmNsdWRlcyhxKSk7CiAgICAgIGNvbnN0IHRib2R5ID0gZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoJ3VzZXJzLXRib2R5Jyk7CgogICAgICBpZiAoZmlsdGVyZWQubGVuZ3RoID09PSAwKSB7CiAgICAgICAgdGJvZHkuaW5uZXJIVE1MID0gJzx0cj48dGQgY29sc3Bhbj0iNSIgY2xhc3M9ImVtcHR5LXN0YXRlIj5ObyBzZSBlbmNvbnRyYXJvbiBjdWVudGFzPC90ZD48L3RyPic7CiAgICAgICAgcmV0dXJuOwogICAgICB9CgogICAgICB0Ym9keS5pbm5lckhUTUwgPSBmaWx0ZXJlZC5tYXAodSA9PiB7CiAgICAgICAgY29uc3QgaXNCb3VuZCA9IEJvb2xlYW4odS5od2lkKTsKICAgICAgICBjb25zdCBpc0FjdGl2ZSA9IHUuYWN0aXZlICE9PSBmYWxzZTsKICAgICAgICBjb25zdCBod2lkVG9vbHRpcCA9IGlzQm91bmQgPyBgSFdJRDogJHt1Lmh3aWQuc3Vic3RyaW5nKDAsIDE2KX0uLi5gIDogJ1NpbiB2aW5jdWxhcic7CgogICAgICAgIHJldHVybiBgCiAgICAgICAgICA8dHI+CiAgICAgICAgICAgIDx0ZD4KICAgICAgICAgICAgICA8c3BhbiBjbGFzcz0idXNlci1waWxsIj4ke2VzY2FwZUh0bWwodS51c2VyKX08L3NwYW4+CiAgICAgICAgICAgIDwvdGQ+CiAgICAgICAgICAgIDx0ZD4KICAgICAgICAgICAgICA8ZGl2IGNsYXNzPSJwYXNzLWNlbGwiPgogICAgICAgICAgICAgICAgPHNwYW4gaWQ9InBhc3MtJHtlc2NhcGVIdG1sKHUudXNlcil9IiBjbGFzcz0icGFzcy1oaWRkZW4iPuKAouKAouKAouKAouKAouKAouKAouKAojwvc3Bhbj4KICAgICAgICAgICAgICAgIDxidXR0b24gY2xhc3M9ImJ0biBidG4tb3V0bGluZSBidG4tc20iIG9uY2xpY2s9InRvZ2dsZVBhc3MoJyR7ZXNjYXBlSHRtbCh1LnVzZXIpfScsICcke2VzY2FwZUh0bWwodS5wYXNzd29yZCl9JykiPlZlcjwvYnV0dG9uPgogICAgICAgICAgICAgICAgPGJ1dHRvbiBjbGFzcz0iYnRuIGJ0bi1vdXRsaW5lIGJ0bi1zbSIgb25jbGljaz0iY29weVRleHQoJyR7ZXNjYXBlSHRtbCh1LnBhc3N3b3JkKX0nKSI+Q29waWFyPC9idXR0b24+CiAgICAgICAgICAgICAgPC9kaXY+CiAgICAgICAgICAgIDwvdGQ+CiAgICAgICAgICAgIDx0ZD4KICAgICAgICAgICAgICA8ZGl2IHN0eWxlPSJkaXNwbGF5OiBmbGV4OyBhbGlnbi1pdGVtczogY2VudGVyOyBnYXA6IDAuNnJlbTsgZmxleC13cmFwOiB3cmFwOyI+CiAgICAgICAgICAgICAgICA8c3BhbiBjbGFzcz0ic3RhdHVzLWJhZGdlICR7aXNCb3VuZCA/ICdib3VuZCcgOiAndW5ib3VuZCd9IiB0aXRsZT0iJHtod2lkVG9vbHRpcH0iPgogICAgICAgICAgICAgICAgICAke2lzQm91bmQgPyAnVmluY3VsYWRvJyA6ICdMaWJyZSAvIFBlbmRpZW50ZSd9CiAgICAgICAgICAgICAgICA8L3NwYW4+CiAgICAgICAgICAgICAgICAke2lzQm91bmQgPyBgPGJ1dHRvbiBjbGFzcz0iYnRuIGJ0bi13YXJuaW5nIGJ0bi1zbSIgb25jbGljaz0icmVzZXRVc2VySHdpZCgnJHtlc2NhcGVIdG1sKHUudXNlcil9JykiPlJlc2V0ZWFyIEhXSUQ8L2J1dHRvbj5gIDogJyd9CiAgICAgICAgICAgICAgPC9kaXY+CiAgICAgICAgICAgIDwvdGQ+CiAgICAgICAgICAgIDx0ZD4KICAgICAgICAgICAgICA8YnV0dG9uIGNsYXNzPSJidG4gYnRuLXNtICR7aXNBY3RpdmUgPyAnYnRuLW91dGxpbmUnIDogJ2J0bi1kYW5nZXInfSIgb25jbGljaz0idG9nZ2xlQWN0aXZlKCcke2VzY2FwZUh0bWwodS51c2VyKX0nLCAkeyFpc0FjdGl2ZX0pIj4KICAgICAgICAgICAgICAgICR7aXNBY3RpdmUgPyAnQWN0aXZvJyA6ICdTdXNwZW5kaWRvJ30KICAgICAgICAgICAgICA8L2J1dHRvbj4KICAgICAgICAgICAgPC90ZD4KICAgICAgICAgICAgPHRkPgogICAgICAgICAgICAgIDxkaXYgY2xhc3M9InRhYmxlLWFjdGlvbnMiPgogICAgICAgICAgICAgICAgPGJ1dHRvbiBjbGFzcz0iYnRuIGJ0bi1vdXRsaW5lIGJ0bi1zbSIgb25jbGljaz0ib3BlbkVkaXRNb2RhbCgnJHtlc2NhcGVIdG1sKHUudXNlcil9JywgJyR7ZXNjYXBlSHRtbCh1LnBhc3N3b3JkKX0nLCAke2lzQWN0aXZlfSkiPkVkaXRhcjwvYnV0dG9uPgogICAgICAgICAgICAgICAgPGJ1dHRvbiBjbGFzcz0iYnRuIGJ0bi1kYW5nZXIgYnRuLXNtIiBvbmNsaWNrPSJkZWxldGVVc2VyKCcke2VzY2FwZUh0bWwodS51c2VyKX0nKSI+RWxpbWluYXI8L2J1dHRvbj4KICAgICAgICAgICAgICA8L2Rpdj4KICAgICAgICAgICAgPC90ZD4KICAgICAgICAgIDwvdHI+CiAgICAgICAgYDsKICAgICAgfSkuam9pbignJyk7CiAgICB9CgogICAgZnVuY3Rpb24gdG9nZ2xlUGFzcyh1c2VybmFtZSwgcGxhaW5QYXNzKSB7CiAgICAgIGNvbnN0IGVsID0gZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoYHBhc3MtJHt1c2VybmFtZX1gKTsKICAgICAgaWYgKGVsLmlubmVyVGV4dCA9PT0gJ+KAouKAouKAouKAouKAouKAouKAouKAoicpIHsKICAgICAgICBlbC5pbm5lclRleHQgPSBwbGFpblBhc3M7CiAgICAgICAgZWwuY2xhc3NMaXN0LnJlbW92ZSgncGFzcy1oaWRkZW4nKTsKICAgICAgfSBlbHNlIHsKICAgICAgICBlbC5pbm5lclRleHQgPSAn4oCi4oCi4oCi4oCi4oCi4oCi4oCi4oCiJzsKICAgICAgICBlbC5jbGFzc0xpc3QuYWRkKCdwYXNzLWhpZGRlbicpOwogICAgICB9CiAgICB9CgogICAgZnVuY3Rpb24gY29weVRleHQodHh0KSB7CiAgICAgIG5hdmlnYXRvci5jbGlwYm9hcmQud3JpdGVUZXh0KHR4dCk7CiAgICAgIHNob3dUb2FzdCgnQ29waWFkbyBhbCBwb3J0YXBhcGVsZXMnKTsKICAgIH0KCiAgICAvLyBNb2RhbCBDcmVhciAvIEVkaXRhcgogICAgZnVuY3Rpb24gb3BlbkNyZWF0ZU1vZGFsKCkgewogICAgICBnX0lzRWRpdGluZyA9IGZhbHNlOwogICAgICBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgnbW9kYWwtdGl0bGUnKS5pbm5lclRleHQgPSAnTnVldmEgQ3VlbnRhJzsKICAgICAgZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoJ21vZGFsLXVzZXInKS52YWx1ZSA9ICcnOwogICAgICBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgnbW9kYWwtdXNlcicpLmRpc2FibGVkID0gZmFsc2U7CiAgICAgIGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCdtb2RhbC1wYXNzJykudmFsdWUgPSAnMTIzJzsKICAgICAgZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoJ21vZGFsLWFjdGl2ZScpLmNoZWNrZWQgPSB0cnVlOwogICAgICBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgndXNlci1tb2RhbCcpLmNsYXNzTGlzdC5hZGQoJ29wZW4nKTsKICAgICAgc2V0VGltZW91dCgoKSA9PiBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgnbW9kYWwtdXNlcicpLmZvY3VzKCksIDUwKTsKICAgIH0KCiAgICBmdW5jdGlvbiBvcGVuRWRpdE1vZGFsKHVzZXIsIHBhc3MsIGFjdGl2ZSkgewogICAgICBnX0lzRWRpdGluZyA9IHRydWU7CiAgICAgIGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCdtb2RhbC10aXRsZScpLmlubmVyVGV4dCA9IGBFZGl0YXIgQ3VlbnRhOiAke3VzZXJ9YDsKICAgICAgZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoJ21vZGFsLXVzZXInKS52YWx1ZSA9IHVzZXI7CiAgICAgIGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCdtb2RhbC11c2VyJykuZGlzYWJsZWQgPSB0cnVlOwogICAgICBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgnbW9kYWwtcGFzcycpLnZhbHVlID0gcGFzczsKICAgICAgZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoJ21vZGFsLWFjdGl2ZScpLmNoZWNrZWQgPSBhY3RpdmU7CiAgICAgIGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCd1c2VyLW1vZGFsJykuY2xhc3NMaXN0LmFkZCgnb3BlbicpOwogICAgICBzZXRUaW1lb3V0KCgpID0+IGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCdtb2RhbC1wYXNzJykuZm9jdXMoKSwgNTApOwogICAgfQoKICAgIGZ1bmN0aW9uIGNsb3NlVXNlck1vZGFsKCkgewogICAgICBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgndXNlci1tb2RhbCcpLmNsYXNzTGlzdC5yZW1vdmUoJ29wZW4nKTsKICAgIH0KCiAgICBhc3luYyBmdW5jdGlvbiBzYXZlVXNlcigpIHsKICAgICAgY29uc3QgdXNlciA9IGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCdtb2RhbC11c2VyJykudmFsdWUudHJpbSgpOwogICAgICBjb25zdCBwYXNzd29yZCA9IGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCdtb2RhbC1wYXNzJykudmFsdWUudHJpbSgpOwogICAgICBjb25zdCBhY3RpdmUgPSBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgnbW9kYWwtYWN0aXZlJykuY2hlY2tlZDsKCiAgICAgIGlmICghdXNlciB8fCAhcGFzc3dvcmQpIHsKICAgICAgICBzaG93VG9hc3QoJ0NvbXBsZXRhIHVzdWFyaW8geSBjb250cmFzZW5hJywgJ2Vycm9yJyk7CiAgICAgICAgcmV0dXJuOwogICAgICB9CgogICAgICB0cnkgewogICAgICAgIGNvbnN0IHJlcyA9IGF3YWl0IGZldGNoKGAke0FQSV9CQVNFfS9hcGkvYWRtaW4vdXNlcmAsIHsKICAgICAgICAgIG1ldGhvZDogJ1BPU1QnLAogICAgICAgICAgaGVhZGVyczogewogICAgICAgICAgICAnQ29udGVudC1UeXBlJzogJ2FwcGxpY2F0aW9uL2pzb24nLAogICAgICAgICAgICAnWC1BZG1pbi1LZXknOiBnX0FkbWluS2V5CiAgICAgICAgICB9LAogICAgICAgICAgYm9keTogSlNPTi5zdHJpbmdpZnkoeyB1c2VyLCBwYXNzd29yZCwgYWN0aXZlIH0pCiAgICAgICAgfSk7CiAgICAgICAgY29uc3QgZGF0YSA9IGF3YWl0IHJlcy5qc29uKCk7CiAgICAgICAgaWYgKGRhdGEuc3RhdHVzID09PSAnb2snKSB7CiAgICAgICAgICBzaG93VG9hc3QoZGF0YS5tZXNzYWdlKTsKICAgICAgICAgIGNsb3NlVXNlck1vZGFsKCk7CiAgICAgICAgICBsb2FkVXNlcnMoKTsKICAgICAgICB9IGVsc2UgewogICAgICAgICAgc2hvd1RvYXN0KGRhdGEubWVzc2FnZSwgJ2Vycm9yJyk7CiAgICAgICAgfQogICAgICB9IGNhdGNoIChlKSB7CiAgICAgICAgc2hvd1RvYXN0KCdFcnJvciBhbCBndWFyZGFyIHVzdWFyaW8nLCAnZXJyb3InKTsKICAgICAgfQogICAgfQoKICAgIGFzeW5jIGZ1bmN0aW9uIHJlc2V0VXNlckh3aWQodXNlcikgewogICAgICBpZiAoIWNvbmZpcm0oYERlc2VhcyByZXNldGVhciBlbCBIV0lEIGRlICIke3VzZXJ9Ij9cblxuQWwgcmVzZXRlYXJsbywgcG9kcmEgaW5pY2lhciBzZXNpb24gZGVzZGUgdW5hIG51ZXZhIGNvbXB1dGFkb3JhIHkgc2UgdmluY3VsYXJhIGEgbGEgbWlzbWEuYCkpIHsKICAgICAgICByZXR1cm47CiAgICAgIH0KCiAgICAgIHRyeSB7CiAgICAgICAgY29uc3QgcmVzID0gYXdhaXQgZmV0Y2goYCR7QVBJX0JBU0V9L2FwaS9hZG1pbi9yZXNldC1od2lkYCwgewogICAgICAgICAgbWV0aG9kOiAnUE9TVCcsCiAgICAgICAgICBoZWFkZXJzOiB7CiAgICAgICAgICAgICdDb250ZW50LVR5cGUnOiAnYXBwbGljYXRpb24vanNvbicsCiAgICAgICAgICAgICdYLUFkbWluLUtleSc6IGdfQWRtaW5LZXkKICAgICAgICAgIH0sCiAgICAgICAgICBib2R5OiBKU09OLnN0cmluZ2lmeSh7IHVzZXIgfSkKICAgICAgICB9KTsKICAgICAgICBjb25zdCBkYXRhID0gYXdhaXQgcmVzLmpzb24oKTsKICAgICAgICBpZiAoZGF0YS5zdGF0dXMgPT09ICdvaycpIHsKICAgICAgICAgIHNob3dUb2FzdChgSFdJRCBkZSAiJHt1c2VyfSIgcmVzZXRlYWRvIGNvbiBleGl0b2ApOwogICAgICAgICAgbG9hZFVzZXJzKCk7CiAgICAgICAgfSBlbHNlIHsKICAgICAgICAgIHNob3dUb2FzdChkYXRhLm1lc3NhZ2UsICdlcnJvcicpOwogICAgICAgIH0KICAgICAgfSBjYXRjaCAoZSkgewogICAgICAgIHNob3dUb2FzdCgnRXJyb3IgYWwgcmVzZXRlYXIgSFdJRCcsICdlcnJvcicpOwogICAgICB9CiAgICB9CgogICAgYXN5bmMgZnVuY3Rpb24gcmVzZXRBbGxId2lkcygpIHsKICAgICAgaWYgKCFjb25maXJtKCdBVEVOQ0lPTjogRGVzZWFzIGJsYW5xdWVhciBlbCBIV0lEIGRlIFRPREFTIGxhcyBjdWVudGFzIHJlZ2lzdHJhZGFzP1xuXG5Ub2RhcyBsYXMgY3VlbnRhcyBwYXNhcmFuIGEgZXN0YWRvICJMaWJyZSIgeSBzZSByZXZpbmN1bGFyYW4gYXV0b21hdGljYW1lbnRlIGEgbGEgUEMgZGVzZGUgbGEgcXVlIGluaWNpZW4gc2VzaW9uIGxhIHByb3hpbWEgdmV6LicpKSB7CiAgICAgICAgcmV0dXJuOwogICAgICB9CgogICAgICB0cnkgewogICAgICAgIGNvbnN0IHJlcyA9IGF3YWl0IGZldGNoKGAke0FQSV9CQVNFfS9hcGkvYWRtaW4vcmVzZXQtaHdpZGAsIHsKICAgICAgICAgIG1ldGhvZDogJ1BPU1QnLAogICAgICAgICAgaGVhZGVyczogewogICAgICAgICAgICAnQ29udGVudC1UeXBlJzogJ2FwcGxpY2F0aW9uL2pzb24nLAogICAgICAgICAgICAnWC1BZG1pbi1LZXknOiBnX0FkbWluS2V5CiAgICAgICAgICB9LAogICAgICAgICAgYm9keTogSlNPTi5zdHJpbmdpZnkoeyBhbGw6IHRydWUgfSkKICAgICAgICB9KTsKICAgICAgICBjb25zdCBkYXRhID0gYXdhaXQgcmVzLmpzb24oKTsKICAgICAgICBpZiAoZGF0YS5zdGF0dXMgPT09ICdvaycpIHsKICAgICAgICAgIHNob3dUb2FzdChkYXRhLm1lc3NhZ2UpOwogICAgICAgICAgbG9hZFVzZXJzKCk7CiAgICAgICAgfSBlbHNlIHsKICAgICAgICAgIHNob3dUb2FzdChkYXRhLm1lc3NhZ2UsICdlcnJvcicpOwogICAgICAgIH0KICAgICAgfSBjYXRjaCAoZSkgewogICAgICAgIHNob3dUb2FzdCgnRXJyb3IgYWwgYmxhbnF1ZWFyIEhXSURzJywgJ2Vycm9yJyk7CiAgICAgIH0KICAgIH0KCiAgICBhc3luYyBmdW5jdGlvbiB0b2dnbGVBY3RpdmUodXNlciwgbmV3QWN0aXZlU3RhdGUpIHsKICAgICAgdHJ5IHsKICAgICAgICBjb25zdCByZXMgPSBhd2FpdCBmZXRjaChgJHtBUElfQkFTRX0vYXBpL2FkbWluL3RvZ2dsZS1hY3RpdmVgLCB7CiAgICAgICAgICBtZXRob2Q6ICdQT1NUJywKICAgICAgICAgIGhlYWRlcnM6IHsKICAgICAgICAgICAgJ0NvbnRlbnQtVHlwZSc6ICdhcHBsaWNhdGlvbi9qc29uJywKICAgICAgICAgICAgJ1gtQWRtaW4tS2V5JzogZ19BZG1pbktleQogICAgICAgICAgfSwKICAgICAgICAgIGJvZHk6IEpTT04uc3RyaW5naWZ5KHsgdXNlciwgYWN0aXZlOiBuZXdBY3RpdmVTdGF0ZSB9KQogICAgICAgIH0pOwogICAgICAgIGNvbnN0IGRhdGEgPSBhd2FpdCByZXMuanNvbigpOwogICAgICAgIGlmIChkYXRhLnN0YXR1cyA9PT0gJ29rJykgewogICAgICAgICAgc2hvd1RvYXN0KGRhdGEubWVzc2FnZSk7CiAgICAgICAgICBsb2FkVXNlcnMoKTsKICAgICAgICB9IGVsc2UgewogICAgICAgICAgc2hvd1RvYXN0KGRhdGEubWVzc2FnZSwgJ2Vycm9yJyk7CiAgICAgICAgfQogICAgICB9IGNhdGNoIChlKSB7CiAgICAgICAgc2hvd1RvYXN0KCdFcnJvciBhbCBjYW1iaWFyIGVzdGFkbyBkZSBjdWVudGEnLCAnZXJyb3InKTsKICAgICAgfQogICAgfQoKICAgIGFzeW5jIGZ1bmN0aW9uIGRlbGV0ZVVzZXIodXNlcikgewogICAgICBpZiAoIWNvbmZpcm0oYFNlZ3VybyBxdWUgZGVzZWFzIGVsaW1pbmFyIGRlZmluaXRpdmFtZW50ZSBsYSBjdWVudGEgIiR7dXNlcn0iP2ApKSB7CiAgICAgICAgcmV0dXJuOwogICAgICB9CgogICAgICB0cnkgewogICAgICAgIGNvbnN0IHJlcyA9IGF3YWl0IGZldGNoKGAke0FQSV9CQVNFfS9hcGkvYWRtaW4vZGVsZXRlLXVzZXJgLCB7CiAgICAgICAgICBtZXRob2Q6ICdQT1NUJywKICAgICAgICAgIGhlYWRlcnM6IHsKICAgICAgICAgICAgJ0NvbnRlbnQtVHlwZSc6ICdhcHBsaWNhdGlvbi9qc29uJywKICAgICAgICAgICAgJ1gtQWRtaW4tS2V5JzogZ19BZG1pbktleQogICAgICAgICAgfSwKICAgICAgICAgIGJvZHk6IEpTT04uc3RyaW5naWZ5KHsgdXNlciB9KQogICAgICAgIH0pOwogICAgICAgIGNvbnN0IGRhdGEgPSBhd2FpdCByZXMuanNvbigpOwogICAgICAgIGlmIChkYXRhLnN0YXR1cyA9PT0gJ29rJykgewogICAgICAgICAgc2hvd1RvYXN0KGBDdWVudGEgIiR7dXNlcn0iIGVsaW1pbmFkYWApOwogICAgICAgICAgbG9hZFVzZXJzKCk7CiAgICAgICAgfSBlbHNlIHsKICAgICAgICAgIHNob3dUb2FzdChkYXRhLm1lc3NhZ2UsICdlcnJvcicpOwogICAgICAgIH0KICAgICAgfSBjYXRjaCAoZSkgewogICAgICAgIHNob3dUb2FzdCgnRXJyb3IgYWwgZWxpbWluYXIgdXN1YXJpbycsICdlcnJvcicpOwogICAgICB9CiAgICB9CgogICAgZnVuY3Rpb24gZXNjYXBlSHRtbChzdHIpIHsKICAgICAgaWYgKCFzdHIpIHJldHVybiAnJzsKICAgICAgcmV0dXJuIFN0cmluZyhzdHIpCiAgICAgICAgLnJlcGxhY2UoLyYvZywgJyZhbXA7JykKICAgICAgICAucmVwbGFjZSgvPC9nLCAnJmx0OycpCiAgICAgICAgLnJlcGxhY2UoLz4vZywgJyZndDsnKQogICAgICAgIC5yZXBsYWNlKC8iL2csICcmcXVvdDsnKQogICAgICAgIC5yZXBsYWNlKC8nL2csICcmIzM5OycpOwogICAgfQogIDwvc2NyaXB0Pgo8L2JvZHk+CjwvaHRtbD4=";

function getAdminHtml() {
  const binary = atob(ADMIN_HTML_B64);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) {
    bytes[i] = binary.charCodeAt(i);
  }
  return new TextDecoder().decode(bytes);
}
