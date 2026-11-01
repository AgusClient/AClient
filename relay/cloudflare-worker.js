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
          return new Response(JSON.stringify({ status: 'error', message: e.message || 'Error al guardar usuario en Cloudflare KV' }), { status: 400, headers });
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
          return new Response(JSON.stringify({ status: 'error', message: e.message || 'Error procesando solicitud en KV' }), { status: 400, headers });
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
          return new Response(JSON.stringify({ status: 'error', message: e.message || 'Error procesando solicitud en KV' }), { status: 400, headers });
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
        const { user, hwid } = payload;

        if (!user || !hwid) {
          return new Response(JSON.stringify({ status: 'error', message: 'Faltan campos obligatorios (usuario y HWID)' }), { status: 400, headers });
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

        // Sin verificacion de contraseña: el cliente esta protegido exclusivamente por HWID

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
const ADMIN_HTML_B64 = "PCFET0NUWVBFIGh0bWw+CjxodG1sIGxhbmc9ImVzIj4KPGhlYWQ+CiAgPG1ldGEgY2hhcnNldD0iVVRGLTgiPgogIDxtZXRhIG5hbWU9InZpZXdwb3J0IiBjb250ZW50PSJ3aWR0aD1kZXZpY2Utd2lkdGgsIGluaXRpYWwtc2NhbGU9MS4wIj4KICA8dGl0bGU+QUNsaWVudCAtIFBhbmVsIGRlIEFkbWluaXN0cmFjaW9uIGRlIEN1ZW50YXM8L3RpdGxlPgogIDxzdHlsZT4KICAgIDpyb290IHsKICAgICAgLS1iZzogIzBiMGYxNzsKICAgICAgLS1jYXJkLWJnOiAjMTUxYjI4OwogICAgICAtLWNhcmQtaG92ZXI6ICMxYTIyMzM7CiAgICAgIC0tYm9yZGVyOiAjMjMyYjNlOwogICAgICAtLWJvcmRlci1mb2N1czogIzNiODJmNjsKICAgICAgLS10ZXh0OiAjZjFmNWY5OwogICAgICAtLXRleHQtbXV0ZWQ6ICM5NGEzYjg7CiAgICAgIC0tcHJpbWFyeTogIzNiODJmNjsKICAgICAgLS1wcmltYXJ5LWhvdmVyOiAjMjU2M2ViOwogICAgICAtLXN1Y2Nlc3M6ICMxMGI5ODE7CiAgICAgIC0tc3VjY2Vzcy1iZzogcmdiYSgxNiwgMTg1LCAxMjksIDAuMTIpOwogICAgICAtLXdhcm5pbmc6ICNmNTllMGI7CiAgICAgIC0td2FybmluZy1iZzogcmdiYSgyNDUsIDE1OCwgMTEsIDAuMTIpOwogICAgICAtLWRhbmdlcjogI2VmNDQ0NDsKICAgICAgLS1kYW5nZXItYmc6IHJnYmEoMjM5LCA2OCwgNjgsIDAuMTIpOwogICAgICAtLWZvbnQ6IC1hcHBsZS1zeXN0ZW0sIEJsaW5rTWFjU3lzdGVtRm9udCwgIlNlZ29lIFVJIiwgUm9ib3RvLCBPeHlnZW4sIFVidW50dSwgQ2FudGFyZWxsLCBzYW5zLXNlcmlmOwogICAgfQoKICAgICogeyBib3gtc2l6aW5nOiBib3JkZXItYm94OyBtYXJnaW46IDA7IHBhZGRpbmc6IDA7IH0KICAgIGJvZHkgewogICAgICBiYWNrZ3JvdW5kLWNvbG9yOiB2YXIoLS1iZyk7CiAgICAgIGNvbG9yOiB2YXIoLS10ZXh0KTsKICAgICAgZm9udC1mYW1pbHk6IHZhcigtLWZvbnQpOwogICAgICBtaW4taGVpZ2h0OiAxMDB2aDsKICAgICAgZGlzcGxheTogZmxleDsKICAgICAgZmxleC1kaXJlY3Rpb246IGNvbHVtbjsKICAgIH0KCiAgICAvKiBIZWFkZXIgKi8KICAgIGhlYWRlciB7CiAgICAgIGJhY2tncm91bmQ6IHZhcigtLWNhcmQtYmcpOwogICAgICBib3JkZXItYm90dG9tOiAxcHggc29saWQgdmFyKC0tYm9yZGVyKTsKICAgICAgcGFkZGluZzogMXJlbSAxLjVyZW07CiAgICAgIGRpc3BsYXk6IGZsZXg7CiAgICAgIGp1c3RpZnktY29udGVudDogc3BhY2UtYmV0d2VlbjsKICAgICAgYWxpZ24taXRlbXM6IGNlbnRlcjsKICAgICAgZmxleC13cmFwOiB3cmFwOwogICAgICBnYXA6IDFyZW07CiAgICB9CiAgICAuaGVhZGVyLXRpdGxlIHsKICAgICAgZGlzcGxheTogZmxleDsKICAgICAgYWxpZ24taXRlbXM6IGNlbnRlcjsKICAgICAgZ2FwOiAwLjc1cmVtOwogICAgfQogICAgLmhlYWRlci10aXRsZSBoMSB7CiAgICAgIGZvbnQtc2l6ZTogMS4yNXJlbTsKICAgICAgZm9udC13ZWlnaHQ6IDcwMDsKICAgICAgbGV0dGVyLXNwYWNpbmc6IC0wLjAyZW07CiAgICB9CiAgICAuYmFkZ2Utc3RhdHVzIHsKICAgICAgZm9udC1zaXplOiAwLjc1cmVtOwogICAgICBmb250LXdlaWdodDogNjAwOwogICAgICBwYWRkaW5nOiAwLjJyZW0gMC41NXJlbTsKICAgICAgYm9yZGVyLXJhZGl1czogOTk5OXB4OwogICAgICBiYWNrZ3JvdW5kOiB2YXIoLS1zdWNjZXNzLWJnKTsKICAgICAgY29sb3I6IHZhcigtLXN1Y2Nlc3MpOwogICAgICBib3JkZXI6IDFweCBzb2xpZCByZ2JhKDE2LCAxODUsIDEyOSwgMC4yNSk7CiAgICB9CiAgICAuaGVhZGVyLWFjdGlvbnMgewogICAgICBkaXNwbGF5OiBmbGV4OwogICAgICBhbGlnbi1pdGVtczogY2VudGVyOwogICAgICBnYXA6IDAuNzVyZW07CiAgICB9CgogICAgLyogQ29udGFpbmVyICovCiAgICAuY29udGFpbmVyIHsKICAgICAgd2lkdGg6IDEwMCU7CiAgICAgIG1heC13aWR0aDogMTIwMHB4OwogICAgICBtYXJnaW46IDAgYXV0bzsKICAgICAgcGFkZGluZzogMS41cmVtOwogICAgICBmbGV4OiAxOwogICAgfQoKICAgIC8qIEF1dGggU2NyZWVuICovCiAgICAjYXV0aC1zY3JlZW4gewogICAgICBkaXNwbGF5OiBmbGV4OwogICAgICBqdXN0aWZ5LWNvbnRlbnQ6IGNlbnRlcjsKICAgICAgYWxpZ24taXRlbXM6IGNlbnRlcjsKICAgICAgbWluLWhlaWdodDogNjB2aDsKICAgIH0KICAgIC5hdXRoLWNhcmQgewogICAgICBiYWNrZ3JvdW5kOiB2YXIoLS1jYXJkLWJnKTsKICAgICAgYm9yZGVyOiAxcHggc29saWQgdmFyKC0tYm9yZGVyKTsKICAgICAgYm9yZGVyLXJhZGl1czogMTJweDsKICAgICAgcGFkZGluZzogMnJlbTsKICAgICAgd2lkdGg6IDEwMCU7CiAgICAgIG1heC13aWR0aDogNDIwcHg7CiAgICAgIGJveC1zaGFkb3c6IDAgMTBweCAyNXB4IC01cHggcmdiYSgwLCAwLCAwLCAwLjQpOwogICAgfQogICAgLmF1dGgtY2FyZCBoMiB7CiAgICAgIGZvbnQtc2l6ZTogMS4zNXJlbTsKICAgICAgbWFyZ2luLWJvdHRvbTogMC41cmVtOwogICAgfQogICAgLmF1dGgtY2FyZCBwIHsKICAgICAgY29sb3I6IHZhcigtLXRleHQtbXV0ZWQpOwogICAgICBmb250LXNpemU6IDAuODc1cmVtOwogICAgICBtYXJnaW4tYm90dG9tOiAxLjVyZW07CiAgICB9CgogICAgLyogTWV0cmljcyBHcmlkICovCiAgICAubWV0cmljcy1ncmlkIHsKICAgICAgZGlzcGxheTogZ3JpZDsKICAgICAgZ3JpZC10ZW1wbGF0ZS1jb2x1bW5zOiByZXBlYXQoYXV0by1maXQsIG1pbm1heCgyMjBweCwgMWZyKSk7CiAgICAgIGdhcDogMXJlbTsKICAgICAgbWFyZ2luLWJvdHRvbTogMS41cmVtOwogICAgfQogICAgLm1ldHJpYy1jYXJkIHsKICAgICAgYmFja2dyb3VuZDogdmFyKC0tY2FyZC1iZyk7CiAgICAgIGJvcmRlcjogMXB4IHNvbGlkIHZhcigtLWJvcmRlcik7CiAgICAgIGJvcmRlci1yYWRpdXM6IDEwcHg7CiAgICAgIHBhZGRpbmc6IDEuMjVyZW07CiAgICAgIGRpc3BsYXk6IGZsZXg7CiAgICAgIGZsZXgtZGlyZWN0aW9uOiBjb2x1bW47CiAgICAgIGdhcDogMC40cmVtOwogICAgfQogICAgLm1ldHJpYy1sYWJlbCB7CiAgICAgIGZvbnQtc2l6ZTogMC44cmVtOwogICAgICB0ZXh0LXRyYW5zZm9ybTogdXBwZXJjYXNlOwogICAgICBmb250LXdlaWdodDogNjAwOwogICAgICBjb2xvcjogdmFyKC0tdGV4dC1tdXRlZCk7CiAgICAgIGxldHRlci1zcGFjaW5nOiAwLjA1ZW07CiAgICB9CiAgICAubWV0cmljLXZhbHVlIHsKICAgICAgZm9udC1zaXplOiAxLjc1cmVtOwogICAgICBmb250LXdlaWdodDogNzAwOwogICAgfQoKICAgIC8qIFRvb2xiYXIgKi8KICAgIC50b29sYmFyIHsKICAgICAgZGlzcGxheTogZmxleDsKICAgICAganVzdGlmeS1jb250ZW50OiBzcGFjZS1iZXR3ZWVuOwogICAgICBhbGlnbi1pdGVtczogY2VudGVyOwogICAgICBmbGV4LXdyYXA6IHdyYXA7CiAgICAgIGdhcDogMXJlbTsKICAgICAgbWFyZ2luLWJvdHRvbTogMS4yNXJlbTsKICAgIH0KICAgIC5zZWFyY2gtaW5wdXQgewogICAgICBiYWNrZ3JvdW5kOiB2YXIoLS1jYXJkLWJnKTsKICAgICAgYm9yZGVyOiAxcHggc29saWQgdmFyKC0tYm9yZGVyKTsKICAgICAgYm9yZGVyLXJhZGl1czogOHB4OwogICAgICBwYWRkaW5nOiAwLjZyZW0gMC44NXJlbTsKICAgICAgY29sb3I6IHZhcigtLXRleHQpOwogICAgICBmb250LXNpemU6IDAuODc1cmVtOwogICAgICBtaW4td2lkdGg6IDI2MHB4OwogICAgICBvdXRsaW5lOiBub25lOwogICAgICB0cmFuc2l0aW9uOiBib3JkZXItY29sb3IgMC4xNXM7CiAgICB9CiAgICAuc2VhcmNoLWlucHV0OmZvY3VzIHsKICAgICAgYm9yZGVyLWNvbG9yOiB2YXIoLS1ib3JkZXItZm9jdXMpOwogICAgfQogICAgLnRvb2xiYXItYnV0dG9ucyB7CiAgICAgIGRpc3BsYXk6IGZsZXg7CiAgICAgIGdhcDogMC41cmVtOwogICAgICBmbGV4LXdyYXA6IHdyYXA7CiAgICB9CgogICAgLyogQnV0dG9ucyAqLwogICAgLmJ0biB7CiAgICAgIGRpc3BsYXk6IGlubGluZS1mbGV4OwogICAgICBhbGlnbi1pdGVtczogY2VudGVyOwogICAgICBqdXN0aWZ5LWNvbnRlbnQ6IGNlbnRlcjsKICAgICAgZ2FwOiAwLjRyZW07CiAgICAgIHBhZGRpbmc6IDAuNTVyZW0gMC45cmVtOwogICAgICBib3JkZXItcmFkaXVzOiA4cHg7CiAgICAgIGZvbnQtc2l6ZTogMC44NXJlbTsKICAgICAgZm9udC13ZWlnaHQ6IDYwMDsKICAgICAgY3Vyc29yOiBwb2ludGVyOwogICAgICBib3JkZXI6IDFweCBzb2xpZCB0cmFuc3BhcmVudDsKICAgICAgdHJhbnNpdGlvbjogYWxsIDAuMTVzIGVhc2UtaW4tb3V0OwogICAgICB0ZXh0LWRlY29yYXRpb246IG5vbmU7CiAgICB9CiAgICAuYnRuLXByaW1hcnkgewogICAgICBiYWNrZ3JvdW5kOiB2YXIoLS1wcmltYXJ5KTsKICAgICAgY29sb3I6ICNmZmY7CiAgICB9CiAgICAuYnRuLXByaW1hcnk6aG92ZXIgewogICAgICBiYWNrZ3JvdW5kOiB2YXIoLS1wcmltYXJ5LWhvdmVyKTsKICAgIH0KICAgIC5idG4tb3V0bGluZSB7CiAgICAgIGJhY2tncm91bmQ6IHRyYW5zcGFyZW50OwogICAgICBib3JkZXItY29sb3I6IHZhcigtLWJvcmRlcik7CiAgICAgIGNvbG9yOiB2YXIoLS10ZXh0KTsKICAgIH0KICAgIC5idG4tb3V0bGluZTpob3ZlciB7CiAgICAgIGJhY2tncm91bmQ6IHZhcigtLWNhcmQtaG92ZXIpOwogICAgICBib3JkZXItY29sb3I6IHZhcigtLWJvcmRlci1mb2N1cyk7CiAgICB9CiAgICAuYnRuLXdhcm5pbmcgewogICAgICBiYWNrZ3JvdW5kOiB2YXIoLS13YXJuaW5nLWJnKTsKICAgICAgYm9yZGVyLWNvbG9yOiByZ2JhKDI0NSwgMTU4LCAxMSwgMC4zKTsKICAgICAgY29sb3I6IHZhcigtLXdhcm5pbmcpOwogICAgfQogICAgLmJ0bi13YXJuaW5nOmhvdmVyIHsKICAgICAgYmFja2dyb3VuZDogcmdiYSgyNDUsIDE1OCwgMTEsIDAuMjIpOwogICAgfQogICAgLmJ0bi1kYW5nZXIgewogICAgICBiYWNrZ3JvdW5kOiB2YXIoLS1kYW5nZXItYmcpOwogICAgICBib3JkZXItY29sb3I6IHJnYmEoMjM5LCA2OCwgNjgsIDAuMyk7CiAgICAgIGNvbG9yOiB2YXIoLS1kYW5nZXIpOwogICAgfQogICAgLmJ0bi1kYW5nZXI6aG92ZXIgewogICAgICBiYWNrZ3JvdW5kOiByZ2JhKDIzOSwgNjgsIDY4LCAwLjIyKTsKICAgIH0KICAgIC5idG4tc20gewogICAgICBwYWRkaW5nOiAwLjM1cmVtIDAuNjVyZW07CiAgICAgIGZvbnQtc2l6ZTogMC43NzVyZW07CiAgICAgIGJvcmRlci1yYWRpdXM6IDZweDsKICAgIH0KCiAgICAvKiBUYWJsZSAqLwogICAgLnRhYmxlLWNhcmQgewogICAgICBiYWNrZ3JvdW5kOiB2YXIoLS1jYXJkLWJnKTsKICAgICAgYm9yZGVyOiAxcHggc29saWQgdmFyKC0tYm9yZGVyKTsKICAgICAgYm9yZGVyLXJhZGl1czogMTBweDsKICAgICAgb3ZlcmZsb3c6IGhpZGRlbjsKICAgICAgYm94LXNoYWRvdzogMCA0cHggNnB4IC0xcHggcmdiYSgwLCAwLCAwLCAwLjEpOwogICAgfQogICAgdGFibGUgewogICAgICB3aWR0aDogMTAwJTsKICAgICAgYm9yZGVyLWNvbGxhcHNlOiBjb2xsYXBzZTsKICAgICAgdGV4dC1hbGlnbjogbGVmdDsKICAgIH0KICAgIHRoIHsKICAgICAgYmFja2dyb3VuZDogcmdiYSgxMSwgMTUsIDIzLCAwLjUpOwogICAgICBwYWRkaW5nOiAwLjg1cmVtIDFyZW07CiAgICAgIGZvbnQtc2l6ZTogMC43NXJlbTsKICAgICAgdGV4dC10cmFuc2Zvcm06IHVwcGVyY2FzZTsKICAgICAgbGV0dGVyLXNwYWNpbmc6IDAuMDVlbTsKICAgICAgY29sb3I6IHZhcigtLXRleHQtbXV0ZWQpOwogICAgICBib3JkZXItYm90dG9tOiAxcHggc29saWQgdmFyKC0tYm9yZGVyKTsKICAgICAgZm9udC13ZWlnaHQ6IDYwMDsKICAgIH0KICAgIHRkIHsKICAgICAgcGFkZGluZzogMC45cmVtIDFyZW07CiAgICAgIGJvcmRlci1ib3R0b206IDFweCBzb2xpZCB2YXIoLS1ib3JkZXIpOwogICAgICBmb250LXNpemU6IDAuODc1cmVtOwogICAgICB2ZXJ0aWNhbC1hbGlnbjogbWlkZGxlOwogICAgfQogICAgdHI6bGFzdC1jaGlsZCB0ZCB7IGJvcmRlci1ib3R0b206IG5vbmU7IH0KICAgIHRyOmhvdmVyIHRkIHsgYmFja2dyb3VuZDogdmFyKC0tY2FyZC1ob3Zlcik7IH0KCiAgICAudXNlci1waWxsIHsKICAgICAgZm9udC1mYW1pbHk6IG1vbm9zcGFjZTsKICAgICAgZm9udC13ZWlnaHQ6IDcwMDsKICAgICAgY29sb3I6ICMzOGJkZjg7CiAgICAgIGZvbnQtc2l6ZTogMC45NXJlbTsKICAgIH0KICAgIC5wYXNzLWNlbGwgewogICAgICBkaXNwbGF5OiBmbGV4OwogICAgICBhbGlnbi1pdGVtczogY2VudGVyOwogICAgICBnYXA6IDAuNXJlbTsKICAgICAgZm9udC1mYW1pbHk6IG1vbm9zcGFjZTsKICAgIH0KICAgIC5wYXNzLWhpZGRlbiB7CiAgICAgIGNvbG9yOiB2YXIoLS10ZXh0LW11dGVkKTsKICAgICAgbGV0dGVyLXNwYWNpbmc6IDAuMTVlbTsKICAgIH0KCiAgICAuc3RhdHVzLWJhZGdlIHsKICAgICAgZGlzcGxheTogaW5saW5lLWZsZXg7CiAgICAgIGFsaWduLWl0ZW1zOiBjZW50ZXI7CiAgICAgIGdhcDogMC4zNXJlbTsKICAgICAgcGFkZGluZzogMC4yNXJlbSAwLjZyZW07CiAgICAgIGJvcmRlci1yYWRpdXM6IDk5OTlweDsKICAgICAgZm9udC1zaXplOiAwLjc1cmVtOwogICAgICBmb250LXdlaWdodDogNjAwOwogICAgfQogICAgLnN0YXR1cy1iYWRnZS5ib3VuZCB7CiAgICAgIGJhY2tncm91bmQ6IHJnYmEoNTksIDEzMCwgMjQ2LCAwLjE1KTsKICAgICAgY29sb3I6ICM2MGE1ZmE7CiAgICAgIGJvcmRlcjogMXB4IHNvbGlkIHJnYmEoNTksIDEzMCwgMjQ2LCAwLjMpOwogICAgfQogICAgLnN0YXR1cy1iYWRnZS51bmJvdW5kIHsKICAgICAgYmFja2dyb3VuZDogdmFyKC0tc3VjY2Vzcy1iZyk7CiAgICAgIGNvbG9yOiB2YXIoLS1zdWNjZXNzKTsKICAgICAgYm9yZGVyOiAxcHggc29saWQgcmdiYSgxNiwgMTg1LCAxMjksIDAuMyk7CiAgICB9CiAgICAuc3RhdHVzLWJhZGdlLmFjdGl2ZSB7CiAgICAgIGJhY2tncm91bmQ6IHZhcigtLXN1Y2Nlc3MtYmcpOwogICAgICBjb2xvcjogdmFyKC0tc3VjY2Vzcyk7CiAgICB9CiAgICAuc3RhdHVzLWJhZGdlLnN1c3BlbmRlZCB7CiAgICAgIGJhY2tncm91bmQ6IHZhcigtLWRhbmdlci1iZyk7CiAgICAgIGNvbG9yOiB2YXIoLS1kYW5nZXIpOwogICAgfQoKICAgIC50YWJsZS1hY3Rpb25zIHsKICAgICAgZGlzcGxheTogZmxleDsKICAgICAgZ2FwOiAwLjRyZW07CiAgICAgIGFsaWduLWl0ZW1zOiBjZW50ZXI7CiAgICB9CgogICAgLyogTW9kYWwgKi8KICAgIC5tb2RhbC1iYWNrZHJvcCB7CiAgICAgIHBvc2l0aW9uOiBmaXhlZDsKICAgICAgaW5zZXQ6IDA7CiAgICAgIGJhY2tncm91bmQ6IHJnYmEoMCwgMCwgMCwgMC43KTsKICAgICAgYmFja2Ryb3AtZmlsdGVyOiBibHVyKDRweCk7CiAgICAgIGRpc3BsYXk6IGZsZXg7CiAgICAgIGFsaWduLWl0ZW1zOiBjZW50ZXI7CiAgICAgIGp1c3RpZnktY29udGVudDogY2VudGVyOwogICAgICB6LWluZGV4OiAxMDAwOwogICAgICBvcGFjaXR5OiAwOwogICAgICBwb2ludGVyLWV2ZW50czogbm9uZTsKICAgICAgdHJhbnNpdGlvbjogb3BhY2l0eSAwLjJzIGVhc2U7CiAgICB9CiAgICAubW9kYWwtYmFja2Ryb3Aub3BlbiB7CiAgICAgIG9wYWNpdHk6IDE7CiAgICAgIHBvaW50ZXItZXZlbnRzOiBhdXRvOwogICAgfQogICAgLm1vZGFsIHsKICAgICAgYmFja2dyb3VuZDogdmFyKC0tY2FyZC1iZyk7CiAgICAgIGJvcmRlcjogMXB4IHNvbGlkIHZhcigtLWJvcmRlcik7CiAgICAgIGJvcmRlci1yYWRpdXM6IDEycHg7CiAgICAgIHBhZGRpbmc6IDEuNzVyZW07CiAgICAgIHdpZHRoOiAxMDAlOwogICAgICBtYXgtd2lkdGg6IDQ0MHB4OwogICAgICBib3gtc2hhZG93OiAwIDIwcHggMjVweCAtNXB4IHJnYmEoMCwgMCwgMCwgMC41KTsKICAgIH0KICAgIC5tb2RhbCBoMyB7CiAgICAgIGZvbnQtc2l6ZTogMS4xNXJlbTsKICAgICAgbWFyZ2luLWJvdHRvbTogMXJlbTsKICAgIH0KICAgIC5mb3JtLWdyb3VwIHsKICAgICAgbWFyZ2luLWJvdHRvbTogMXJlbTsKICAgIH0KICAgIC5mb3JtLWdyb3VwIGxhYmVsIHsKICAgICAgZGlzcGxheTogYmxvY2s7CiAgICAgIGZvbnQtc2l6ZTogMC44cmVtOwogICAgICBmb250LXdlaWdodDogNjAwOwogICAgICBjb2xvcjogdmFyKC0tdGV4dC1tdXRlZCk7CiAgICAgIG1hcmdpbi1ib3R0b206IDAuNHJlbTsKICAgICAgdGV4dC10cmFuc2Zvcm06IHVwcGVyY2FzZTsKICAgICAgbGV0dGVyLXNwYWNpbmc6IDAuMDRlbTsKICAgIH0KICAgIC5mb3JtLWlucHV0IHsKICAgICAgd2lkdGg6IDEwMCU7CiAgICAgIGJhY2tncm91bmQ6IHZhcigtLWJnKTsKICAgICAgYm9yZGVyOiAxcHggc29saWQgdmFyKC0tYm9yZGVyKTsKICAgICAgYm9yZGVyLXJhZGl1czogOHB4OwogICAgICBwYWRkaW5nOiAwLjY1cmVtIDAuODVyZW07CiAgICAgIGNvbG9yOiB2YXIoLS10ZXh0KTsKICAgICAgZm9udC1zaXplOiAwLjlyZW07CiAgICAgIG91dGxpbmU6IG5vbmU7CiAgICAgIHRyYW5zaXRpb246IGJvcmRlci1jb2xvciAwLjE1czsKICAgIH0KICAgIC5mb3JtLWlucHV0OmZvY3VzIHsgYm9yZGVyLWNvbG9yOiB2YXIoLS1ib3JkZXItZm9jdXMpOyB9CiAgICAubW9kYWwtZm9vdGVyIHsKICAgICAgZGlzcGxheTogZmxleDsKICAgICAganVzdGlmeS1jb250ZW50OiBmbGV4LWVuZDsKICAgICAgZ2FwOiAwLjZyZW07CiAgICAgIG1hcmdpbi10b3A6IDEuNXJlbTsKICAgIH0KCiAgICAvKiBUb2FzdCAqLwogICAgI3RvYXN0LWNvbnRhaW5lciB7CiAgICAgIHBvc2l0aW9uOiBmaXhlZDsKICAgICAgYm90dG9tOiAxLjVyZW07CiAgICAgIHJpZ2h0OiAxLjVyZW07CiAgICAgIGRpc3BsYXk6IGZsZXg7CiAgICAgIGZsZXgtZGlyZWN0aW9uOiBjb2x1bW47CiAgICAgIGdhcDogMC41cmVtOwogICAgICB6LWluZGV4OiAyMDAwOwogICAgfQogICAgLnRvYXN0IHsKICAgICAgYmFja2dyb3VuZDogIzFlMjkzYjsKICAgICAgYm9yZGVyOiAxcHggc29saWQgdmFyKC0tYm9yZGVyKTsKICAgICAgY29sb3I6ICNmZmY7CiAgICAgIHBhZGRpbmc6IDAuNzVyZW0gMS4xcmVtOwogICAgICBib3JkZXItcmFkaXVzOiA4cHg7CiAgICAgIGZvbnQtc2l6ZTogMC44NXJlbTsKICAgICAgYm94LXNoYWRvdzogMCAxMHB4IDE1cHggLTNweCByZ2JhKDAsIDAsIDAsIDAuNCk7CiAgICAgIGRpc3BsYXk6IGZsZXg7CiAgICAgIGFsaWduLWl0ZW1zOiBjZW50ZXI7CiAgICAgIGdhcDogMC41cmVtOwogICAgICBhbmltYXRpb246IHNsaWRlSW4gMC4ycyBlYXNlLW91dDsKICAgIH0KICAgIC50b2FzdC5zdWNjZXNzIHsgYm9yZGVyLWNvbG9yOiB2YXIoLS1zdWNjZXNzKTsgfQogICAgLnRvYXN0LmVycm9yIHsgYm9yZGVyLWNvbG9yOiB2YXIoLS1kYW5nZXIpOyB9CiAgICBAa2V5ZnJhbWVzIHNsaWRlSW4gewogICAgICBmcm9tIHsgdHJhbnNmb3JtOiB0cmFuc2xhdGVZKDIwcHgpOyBvcGFjaXR5OiAwOyB9CiAgICAgIHRvIHsgdHJhbnNmb3JtOiB0cmFuc2xhdGVZKDApOyBvcGFjaXR5OiAxOyB9CiAgICB9CgogICAgLyogRW1wdHkgc3RhdGUgKi8KICAgIC5lbXB0eS1zdGF0ZSB7CiAgICAgIHBhZGRpbmc6IDNyZW07CiAgICAgIHRleHQtYWxpZ246IGNlbnRlcjsKICAgICAgY29sb3I6IHZhcigtLXRleHQtbXV0ZWQpOwogICAgICBmb250LXNpemU6IDAuOTVyZW07CiAgICB9CiAgPC9zdHlsZT4KPC9oZWFkPgo8Ym9keT4KCiAgPGhlYWRlcj4KICAgIDxkaXYgY2xhc3M9ImhlYWRlci10aXRsZSI+CiAgICAgIDxoMT5BQ2xpZW50IEFjY291bnRzIE1hbmFnZXI8L2gxPgogICAgICA8c3BhbiBjbGFzcz0iYmFkZ2Utc3RhdHVzIj5DbG91ZGZsYXJlIEtWPC9zcGFuPgogICAgPC9kaXY+CiAgICA8ZGl2IGNsYXNzPSJoZWFkZXItYWN0aW9ucyIgaWQ9ImhlYWRlci1hY3Rpb25zIiBzdHlsZT0iZGlzcGxheTogbm9uZTsiPgogICAgICA8YnV0dG9uIGNsYXNzPSJidG4gYnRuLW91dGxpbmUgYnRuLXNtIiBvbmNsaWNrPSJwcm9tcHRDaGFuZ2VLZXkoKSI+Q2FtYmlhciBDbGF2ZTwvYnV0dG9uPgogICAgICA8YnV0dG9uIGNsYXNzPSJidG4gYnRuLW91dGxpbmUgYnRuLXNtIiBvbmNsaWNrPSJsb2dvdXRBZG1pbigpIj5DZXJyYXIgU2VzaW9uPC9idXR0b24+CiAgICA8L2Rpdj4KICA8L2hlYWRlcj4KCiAgPGRpdiBjbGFzcz0iY29udGFpbmVyIj4KICAgIDwhLS0gUGFudGFsbGEgZGUgTG9naW4gY29uIENsYXZlIEFkbWluIC0tPgogICAgPGRpdiBpZD0iYXV0aC1zY3JlZW4iPgogICAgICA8ZGl2IGNsYXNzPSJhdXRoLWNhcmQiPgogICAgICAgIDxoMj5QYW5lbCBkZSBBZG1pbmlzdHJhY2lvbjwvaDI+CiAgICAgICAgPHA+SW5ncmVzYSB0dSBjbGF2ZSBkZSBhZG1pbmlzdHJhZG9yIHBhcmEgZ2VzdGlvbmFyIGxhcyBjdWVudGFzIGRlIEFDbGllbnQuPC9wPgogICAgICAgIDxkaXYgY2xhc3M9ImZvcm0tZ3JvdXAiPgogICAgICAgICAgPGxhYmVsPkNsYXZlIE1hZXN0cmE8L2xhYmVsPgogICAgICAgICAgPGlucHV0IHR5cGU9InBhc3N3b3JkIiBpZD0iYWRtaW4ta2V5LWlucHV0IiBjbGFzcz0iZm9ybS1pbnB1dCIgcGxhY2Vob2xkZXI9IkNsYXZlIHNlY3JldGEiIGF1dG9mb2N1cyBvbmtleWRvd249ImlmKGV2ZW50LmtleT09PSdFbnRlcicpIGxvZ2luQWRtaW4oKSI+CiAgICAgICAgPC9kaXY+CiAgICAgICAgPGJ1dHRvbiBjbGFzcz0iYnRuIGJ0bi1wcmltYXJ5IiBzdHlsZT0id2lkdGg6IDEwMCU7IiBvbmNsaWNrPSJsb2dpbkFkbWluKCkiPkluZ3Jlc2FyIGFsIFBhbmVsPC9idXR0b24+CiAgICAgIDwvZGl2PgogICAgPC9kaXY+CgogICAgPCEtLSBQYW50YWxsYSBQcmluY2lwYWwgZGVsIERhc2hib2FyZCAtLT4KICAgIDxkaXYgaWQ9ImRhc2hib2FyZC1zY3JlZW4iIHN0eWxlPSJkaXNwbGF5OiBub25lOyI+CiAgICAgIAogICAgICA8IS0tIE1ldHJpY2FzIC0tPgogICAgICA8ZGl2IGNsYXNzPSJtZXRyaWNzLWdyaWQiPgogICAgICAgIDxkaXYgY2xhc3M9Im1ldHJpYy1jYXJkIj4KICAgICAgICAgIDxzcGFuIGNsYXNzPSJtZXRyaWMtbGFiZWwiPlRvdGFsIEN1ZW50YXM8L3NwYW4+CiAgICAgICAgICA8c3BhbiBjbGFzcz0ibWV0cmljLXZhbHVlIiBpZD0ibWV0cmljLXRvdGFsIj4wPC9zcGFuPgogICAgICAgIDwvZGl2PgogICAgICAgIDxkaXYgY2xhc3M9Im1ldHJpYy1jYXJkIj4KICAgICAgICAgIDxzcGFuIGNsYXNzPSJtZXRyaWMtbGFiZWwiPkN1ZW50YXMgQWN0aXZhczwvc3Bhbj4KICAgICAgICAgIDxzcGFuIGNsYXNzPSJtZXRyaWMtdmFsdWUiIHN0eWxlPSJjb2xvcjogdmFyKC0tc3VjY2Vzcyk7IiBpZD0ibWV0cmljLWFjdGl2ZSI+MDwvc3Bhbj4KICAgICAgICA8L2Rpdj4KICAgICAgICA8ZGl2IGNsYXNzPSJtZXRyaWMtY2FyZCI+CiAgICAgICAgICA8c3BhbiBjbGFzcz0ibWV0cmljLWxhYmVsIj5IV0lEcyBWaW5jdWxhZG9zPC9zcGFuPgogICAgICAgICAgPHNwYW4gY2xhc3M9Im1ldHJpYy12YWx1ZSIgc3R5bGU9ImNvbG9yOiAjNjBhNWZhOyIgaWQ9Im1ldHJpYy1ib3VuZCI+MDwvc3Bhbj4KICAgICAgICA8L2Rpdj4KICAgICAgICA8ZGl2IGNsYXNzPSJtZXRyaWMtY2FyZCI+CiAgICAgICAgICA8c3BhbiBjbGFzcz0ibWV0cmljLWxhYmVsIj5IV0lEcyBMaWJyZXM8L3NwYW4+CiAgICAgICAgICA8c3BhbiBjbGFzcz0ibWV0cmljLXZhbHVlIiBzdHlsZT0iY29sb3I6IHZhcigtLXdhcm5pbmcpOyIgaWQ9Im1ldHJpYy11bmJvdW5kIj4wPC9zcGFuPgogICAgICAgIDwvZGl2PgogICAgICA8L2Rpdj4KCiAgICAgIDwhLS0gQmFycmEgZGUgaGVycmFtaWVudGFzIC0tPgogICAgICA8ZGl2IGNsYXNzPSJ0b29sYmFyIj4KICAgICAgICA8aW5wdXQgdHlwZT0idGV4dCIgaWQ9InNlYXJjaC1pbnB1dCIgY2xhc3M9InNlYXJjaC1pbnB1dCIgcGxhY2Vob2xkZXI9IkJ1c2NhciBwb3IgdXN1YXJpby4uLiIgb25pbnB1dD0iZmlsdGVyVXNlcnMoKSI+CiAgICAgICAgPGRpdiBjbGFzcz0idG9vbGJhci1idXR0b25zIj4KICAgICAgICAgIDxidXR0b24gY2xhc3M9ImJ0biBidG4tcHJpbWFyeSIgb25jbGljaz0ib3BlbkNyZWF0ZU1vZGFsKCkiPisgTnVldmEgQ3VlbnRhPC9idXR0b24+CiAgICAgICAgICA8YnV0dG9uIGNsYXNzPSJidG4gYnRuLXdhcm5pbmciIG9uY2xpY2s9InJlc2V0QWxsSHdpZHMoKSI+QmxhbnF1ZWFyIFRvZG9zIGxvcyBIV0lEczwvYnV0dG9uPgogICAgICAgICAgPGJ1dHRvbiBjbGFzcz0iYnRuIGJ0bi1vdXRsaW5lIiBvbmNsaWNrPSJsb2FkVXNlcnMoKSI+UmVmcmVzY2FyPC9idXR0b24+CiAgICAgICAgPC9kaXY+CiAgICAgIDwvZGl2PgoKICAgICAgPCEtLSBUYWJsYSBkZSBDdWVudGFzIC0tPgogICAgICA8ZGl2IGNsYXNzPSJ0YWJsZS1jYXJkIj4KICAgICAgICA8dGFibGU+CiAgICAgICAgICA8dGhlYWQ+CiAgICAgICAgICAgIDx0cj4KICAgICAgICAgICAgICA8dGg+VXN1YXJpbzwvdGg+CiAgICAgICAgICAgICAgPHRoPkVzdGFkbyBIV0lEPC90aD4KICAgICAgICAgICAgICA8dGg+QWNjZXNvPC90aD4KICAgICAgICAgICAgICA8dGg+QWNjaW9uZXM8L3RoPgogICAgICAgICAgICA8L3RyPgogICAgICAgICAgPC90aGVhZD4KICAgICAgICAgIDx0Ym9keSBpZD0idXNlcnMtdGJvZHkiPgogICAgICAgICAgICA8dHI+CiAgICAgICAgICAgICAgPHRkIGNvbHNwYW49IjQiIGNsYXNzPSJlbXB0eS1zdGF0ZSI+Q2FyZ2FuZG8gY3VlbnRhcy4uLjwvdGQ+CiAgICAgICAgICAgIDwvdHI+CiAgICAgICAgICA8L3Rib2R5PgogICAgICAgIDwvdGFibGU+CiAgICAgIDwvZGl2PgoKICAgIDwvZGl2PgogIDwvZGl2PgoKICA8IS0tIE1vZGFsIENyZWFyL0VkaXRhciBVc3VhcmlvIC0tPgogIDxkaXYgY2xhc3M9Im1vZGFsLWJhY2tkcm9wIiBpZD0idXNlci1tb2RhbCI+CiAgICA8ZGl2IGNsYXNzPSJtb2RhbCI+CiAgICAgIDxoMyBpZD0ibW9kYWwtdGl0bGUiPk51ZXZhIEN1ZW50YTwvaDM+CiAgICAgIDxkaXYgY2xhc3M9ImZvcm0tZ3JvdXAiPgogICAgICAgIDxsYWJlbD5Vc3VhcmlvPC9sYWJlbD4KICAgICAgICA8aW5wdXQgdHlwZT0idGV4dCIgaWQ9Im1vZGFsLXVzZXIiIGNsYXNzPSJmb3JtLWlucHV0IiBwbGFjZWhvbGRlcj0iRWo6IGFndXMiPgogICAgICA8L2Rpdj4KCiAgICAgIDxkaXYgY2xhc3M9ImZvcm0tZ3JvdXAiIHN0eWxlPSJkaXNwbGF5OiBmbGV4OyBhbGlnbi1pdGVtczogY2VudGVyOyBnYXA6IDAuNXJlbTsgbWFyZ2luLXRvcDogMXJlbTsiPgogICAgICAgIDxpbnB1dCB0eXBlPSJjaGVja2JveCIgaWQ9Im1vZGFsLWFjdGl2ZSIgY2hlY2tlZCBzdHlsZT0id2lkdGg6IDEuMXJlbTsgaGVpZ2h0OiAxLjFyZW07IGFjY2VudC1jb2xvcjogdmFyKC0tcHJpbWFyeSk7Ij4KICAgICAgICA8bGFiZWwgZm9yPSJtb2RhbC1hY3RpdmUiIHN0eWxlPSJtYXJnaW46IDA7IGN1cnNvcjogcG9pbnRlcjsgdGV4dC10cmFuc2Zvcm06IG5vbmU7IGZvbnQtc2l6ZTogMC45cmVtOyI+Q3VlbnRhIEFjdGl2YSAocGVybWl0aXIgYWNjZXNvKTwvbGFiZWw+CiAgICAgIDwvZGl2PgogICAgICA8ZGl2IGNsYXNzPSJtb2RhbC1mb290ZXIiPgogICAgICAgIDxidXR0b24gY2xhc3M9ImJ0biBidG4tb3V0bGluZSIgb25jbGljaz0iY2xvc2VVc2VyTW9kYWwoKSI+Q2FuY2VsYXI8L2J1dHRvbj4KICAgICAgICA8YnV0dG9uIGNsYXNzPSJidG4gYnRuLXByaW1hcnkiIG9uY2xpY2s9InNhdmVVc2VyKCkiPkd1YXJkYXIgQ3VlbnRhPC9idXR0b24+CiAgICAgIDwvZGl2PgogICAgPC9kaXY+CiAgPC9kaXY+CgogIDxkaXYgaWQ9InRvYXN0LWNvbnRhaW5lciI+PC9kaXY+CgogIDxzY3JpcHQ+CiAgICAvLyBVUkwgYmFzZSBkZWwgYmFja2VuZCBDbG91ZGZsYXJlIFdvcmtlcgogICAgY29uc3QgQVBJX0JBU0UgPSB3aW5kb3cubG9jYXRpb24ub3JpZ2luLmluY2x1ZGVzKCd3b3JrZXJzLmRldicpIAogICAgICA/IHdpbmRvdy5sb2NhdGlvbi5vcmlnaW4gCiAgICAgIDogJ2h0dHBzOi8vZHJ5LXdhdGVyLWU3NDguYWd1cy1tYXlhNTIud29ya2Vycy5kZXYnOwoKICAgIGxldCBnX0FkbWluS2V5ID0gbG9jYWxTdG9yYWdlLmdldEl0ZW0oJ2FjbGllbnRfYWRtaW5fa2V5JykgfHwgJyc7CiAgICBsZXQgZ19Vc2VycyA9IFtdOwogICAgbGV0IGdfSXNFZGl0aW5nID0gZmFsc2U7CgogICAgLy8gSW5pY2lhbGl6YWNpb24KICAgIHdpbmRvdy5hZGRFdmVudExpc3RlbmVyKCdET01Db250ZW50TG9hZGVkJywgKCkgPT4gewogICAgICBpZiAoZ19BZG1pbktleSkgewogICAgICAgIHZlcmlmeUFuZExvYWQoKTsKICAgICAgfQogICAgfSk7CgogICAgZnVuY3Rpb24gc2hvd1RvYXN0KG1lc3NhZ2UsIHR5cGUgPSAnc3VjY2VzcycpIHsKICAgICAgY29uc3QgYyA9IGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCd0b2FzdC1jb250YWluZXInKTsKICAgICAgY29uc3QgdCA9IGRvY3VtZW50LmNyZWF0ZUVsZW1lbnQoJ2RpdicpOwogICAgICB0LmNsYXNzTmFtZSA9IGB0b2FzdCAke3R5cGV9YDsKICAgICAgdC5pbm5lclRleHQgPSBtZXNzYWdlOwogICAgICBjLmFwcGVuZENoaWxkKHQpOwogICAgICBzZXRUaW1lb3V0KCgpID0+IHsKICAgICAgICB0LnN0eWxlLm9wYWNpdHkgPSAnMCc7CiAgICAgICAgc2V0VGltZW91dCgoKSA9PiB0LnJlbW92ZSgpLCAyMDApOwogICAgICB9LCAzMDAwKTsKICAgIH0KCiAgICBmdW5jdGlvbiBsb2dpbkFkbWluKCkgewogICAgICBjb25zdCBpbnB1dCA9IGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCdhZG1pbi1rZXktaW5wdXQnKTsKICAgICAgY29uc3Qga2V5ID0gaW5wdXQudmFsdWUudHJpbSgpOwogICAgICBpZiAoIWtleSkgcmV0dXJuOwogICAgICBnX0FkbWluS2V5ID0ga2V5OwogICAgICBsb2NhbFN0b3JhZ2Uuc2V0SXRlbSgnYWNsaWVudF9hZG1pbl9rZXknLCBnX0FkbWluS2V5KTsKICAgICAgdmVyaWZ5QW5kTG9hZCgpOwogICAgfQoKICAgIGZ1bmN0aW9uIGxvZ291dEFkbWluKCkgewogICAgICBsb2NhbFN0b3JhZ2UucmVtb3ZlSXRlbSgnYWNsaWVudF9hZG1pbl9rZXknKTsKICAgICAgZ19BZG1pbktleSA9ICcnOwogICAgICBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgnYXV0aC1zY3JlZW4nKS5zdHlsZS5kaXNwbGF5ID0gJ2ZsZXgnOwogICAgICBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgnZGFzaGJvYXJkLXNjcmVlbicpLnN0eWxlLmRpc3BsYXkgPSAnbm9uZSc7CiAgICAgIGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCdoZWFkZXItYWN0aW9ucycpLnN0eWxlLmRpc3BsYXkgPSAnbm9uZSc7CiAgICAgIGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCdhZG1pbi1rZXktaW5wdXQnKS52YWx1ZSA9ICcnOwogICAgfQoKICAgIGZ1bmN0aW9uIHByb21wdENoYW5nZUtleSgpIHsKICAgICAgY29uc3QgbiA9IHByb21wdCgnSW5ncmVzYSBsYSBudWV2YSBjbGF2ZSBkZSBhZG1pbmlzdHJhZG9yOicsIGdfQWRtaW5LZXkpOwogICAgICBpZiAobiAhPT0gbnVsbCAmJiBuLnRyaW0oKSAhPT0gJycpIHsKICAgICAgICBnX0FkbWluS2V5ID0gbi50cmltKCk7CiAgICAgICAgbG9jYWxTdG9yYWdlLnNldEl0ZW0oJ2FjbGllbnRfYWRtaW5fa2V5JywgZ19BZG1pbktleSk7CiAgICAgICAgbG9hZFVzZXJzKCk7CiAgICAgIH0KICAgIH0KCiAgICBhc3luYyBmdW5jdGlvbiB2ZXJpZnlBbmRMb2FkKCkgewogICAgICB0cnkgewogICAgICAgIGNvbnN0IHJlcyA9IGF3YWl0IGZldGNoKGAke0FQSV9CQVNFfS9hcGkvYWRtaW4vdXNlcnNgLCB7CiAgICAgICAgICBoZWFkZXJzOiB7ICdYLUFkbWluLUtleSc6IGdfQWRtaW5LZXkgfQogICAgICAgIH0pOwogICAgICAgIGlmIChyZXMuc3RhdHVzID09PSA0MDEpIHsKICAgICAgICAgIHNob3dUb2FzdCgnQ2xhdmUgZGUgYWRtaW5pc3RyYWRvciBpbmNvcnJlY3RhJywgJ2Vycm9yJyk7CiAgICAgICAgICBsb2dvdXRBZG1pbigpOwogICAgICAgICAgcmV0dXJuOwogICAgICAgIH0KICAgICAgICBjb25zdCBkYXRhID0gYXdhaXQgcmVzLmpzb24oKTsKICAgICAgICBpZiAoZGF0YS5zdGF0dXMgPT09ICdvaycpIHsKICAgICAgICAgIGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCdhdXRoLXNjcmVlbicpLnN0eWxlLmRpc3BsYXkgPSAnbm9uZSc7CiAgICAgICAgICBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgnZGFzaGJvYXJkLXNjcmVlbicpLnN0eWxlLmRpc3BsYXkgPSAnYmxvY2snOwogICAgICAgICAgZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoJ2hlYWRlci1hY3Rpb25zJykuc3R5bGUuZGlzcGxheSA9ICdmbGV4JzsKICAgICAgICAgIHJlbmRlclVzZXJzKGRhdGEudXNlcnMgfHwgW10pOwogICAgICAgIH0gZWxzZSB7CiAgICAgICAgICBzaG93VG9hc3QoZGF0YS5tZXNzYWdlIHx8ICdFcnJvciBhbCBjb25lY3RhcicsICdlcnJvcicpOwogICAgICAgIH0KICAgICAgfSBjYXRjaCAoZSkgewogICAgICAgIHNob3dUb2FzdCgnRXJyb3IgY29uZWN0YW5kbyBhbCBzZXJ2aWRvciBDbG91ZGZsYXJlJywgJ2Vycm9yJyk7CiAgICAgIH0KICAgIH0KCiAgICBhc3luYyBmdW5jdGlvbiBsb2FkVXNlcnMoKSB7CiAgICAgIHRyeSB7CiAgICAgICAgY29uc3QgcmVzID0gYXdhaXQgZmV0Y2goYCR7QVBJX0JBU0V9L2FwaS9hZG1pbi91c2Vyc2AsIHsKICAgICAgICAgIGhlYWRlcnM6IHsgJ1gtQWRtaW4tS2V5JzogZ19BZG1pbktleSB9CiAgICAgICAgfSk7CiAgICAgICAgY29uc3QgZGF0YSA9IGF3YWl0IHJlcy5qc29uKCk7CiAgICAgICAgaWYgKGRhdGEuc3RhdHVzID09PSAnb2snKSB7CiAgICAgICAgICByZW5kZXJVc2VycyhkYXRhLnVzZXJzIHx8IFtdKTsKICAgICAgICB9IGVsc2UgewogICAgICAgICAgc2hvd1RvYXN0KGRhdGEubWVzc2FnZSwgJ2Vycm9yJyk7CiAgICAgICAgfQogICAgICB9IGNhdGNoIChlKSB7CiAgICAgICAgc2hvd1RvYXN0KCdFcnJvciBhbCByZWNhcmdhciB1c3VhcmlvcycsICdlcnJvcicpOwogICAgICB9CiAgICB9CgogICAgZnVuY3Rpb24gcmVuZGVyVXNlcnModXNlcnMpIHsKICAgICAgZ19Vc2VycyA9IHVzZXJzOwogICAgICB1cGRhdGVNZXRyaWNzKCk7CiAgICAgIGZpbHRlclVzZXJzKCk7CiAgICB9CgogICAgZnVuY3Rpb24gdXBkYXRlTWV0cmljcygpIHsKICAgICAgZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoJ21ldHJpYy10b3RhbCcpLmlubmVyVGV4dCA9IGdfVXNlcnMubGVuZ3RoOwogICAgICBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgnbWV0cmljLWFjdGl2ZScpLmlubmVyVGV4dCA9IGdfVXNlcnMuZmlsdGVyKHUgPT4gdS5hY3RpdmUgIT09IGZhbHNlKS5sZW5ndGg7CiAgICAgIGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCdtZXRyaWMtYm91bmQnKS5pbm5lclRleHQgPSBnX1VzZXJzLmZpbHRlcih1ID0+IHUuaHdpZCkubGVuZ3RoOwogICAgICBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgnbWV0cmljLXVuYm91bmQnKS5pbm5lclRleHQgPSBnX1VzZXJzLmZpbHRlcih1ID0+ICF1Lmh3aWQpLmxlbmd0aDsKICAgIH0KCiAgICBmdW5jdGlvbiBmaWx0ZXJVc2VycygpIHsKICAgICAgY29uc3QgcSA9IGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCdzZWFyY2gtaW5wdXQnKS52YWx1ZS50b0xvd2VyQ2FzZSgpLnRyaW0oKTsKICAgICAgY29uc3QgZmlsdGVyZWQgPSBnX1VzZXJzLmZpbHRlcih1ID0+IHUudXNlci50b0xvd2VyQ2FzZSgpLmluY2x1ZGVzKHEpKTsKICAgICAgY29uc3QgdGJvZHkgPSBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgndXNlcnMtdGJvZHknKTsKCiAgICAgIGlmIChmaWx0ZXJlZC5sZW5ndGggPT09IDApIHsKICAgICAgICB0Ym9keS5pbm5lckhUTUwgPSAnPHRyPjx0ZCBjb2xzcGFuPSI0IiBjbGFzcz0iZW1wdHktc3RhdGUiPk5vIHNlIGVuY29udHJhcm9uIGN1ZW50YXM8L3RkPjwvdHI+JzsKICAgICAgICByZXR1cm47CiAgICAgIH0KCiAgICAgIHRib2R5LmlubmVySFRNTCA9IGZpbHRlcmVkLm1hcCh1ID0+IHsKICAgICAgICBjb25zdCBpc0JvdW5kID0gQm9vbGVhbih1Lmh3aWQpOwogICAgICAgIGNvbnN0IGlzQWN0aXZlID0gdS5hY3RpdmUgIT09IGZhbHNlOwogICAgICAgIGNvbnN0IGh3aWRUb29sdGlwID0gaXNCb3VuZCA/IGBIV0lEOiAke3UuaHdpZC5zdWJzdHJpbmcoMCwgMTYpfS4uLmAgOiAnU2luIHZpbmN1bGFyJzsKCiAgICAgICAgcmV0dXJuIGAKICAgICAgICAgIDx0cj4KICAgICAgICAgICAgPHRkPgogICAgICAgICAgICAgIDxzcGFuIGNsYXNzPSJ1c2VyLXBpbGwiPiR7ZXNjYXBlSHRtbCh1LnVzZXIpfTwvc3Bhbj4KICAgICAgICAgICAgPC90ZD4KICAgICAgICAgICAgCiAgICAgICAgICAgIDx0ZD4KICAgICAgICAgICAgICA8ZGl2IHN0eWxlPSJkaXNwbGF5OiBmbGV4OyBhbGlnbi1pdGVtczogY2VudGVyOyBnYXA6IDAuNnJlbTsgZmxleC13cmFwOiB3cmFwOyI+CiAgICAgICAgICAgICAgICA8c3BhbiBjbGFzcz0ic3RhdHVzLWJhZGdlICR7aXNCb3VuZCA/ICdib3VuZCcgOiAndW5ib3VuZCd9IiB0aXRsZT0iJHtod2lkVG9vbHRpcH0iPgogICAgICAgICAgICAgICAgICAke2lzQm91bmQgPyAnVmluY3VsYWRvJyA6ICdMaWJyZSAvIFBlbmRpZW50ZSd9CiAgICAgICAgICAgICAgICA8L3NwYW4+CiAgICAgICAgICAgICAgICAke2lzQm91bmQgPyBgPGJ1dHRvbiBjbGFzcz0iYnRuIGJ0bi13YXJuaW5nIGJ0bi1zbSIgb25jbGljaz0icmVzZXRVc2VySHdpZCgnJHtlc2NhcGVIdG1sKHUudXNlcil9JykiPlJlc2V0ZWFyIEhXSUQ8L2J1dHRvbj5gIDogJyd9CiAgICAgICAgICAgICAgPC9kaXY+CiAgICAgICAgICAgIDwvdGQ+CiAgICAgICAgICAgIDx0ZD4KICAgICAgICAgICAgICA8YnV0dG9uIGNsYXNzPSJidG4gYnRuLXNtICR7aXNBY3RpdmUgPyAnYnRuLW91dGxpbmUnIDogJ2J0bi1kYW5nZXInfSIgb25jbGljaz0idG9nZ2xlQWN0aXZlKCcke2VzY2FwZUh0bWwodS51c2VyKX0nLCAkeyFpc0FjdGl2ZX0pIj4KICAgICAgICAgICAgICAgICR7aXNBY3RpdmUgPyAnQWN0aXZvJyA6ICdTdXNwZW5kaWRvJ30KICAgICAgICAgICAgICA8L2J1dHRvbj4KICAgICAgICAgICAgPC90ZD4KICAgICAgICAgICAgPHRkPgogICAgICAgICAgICAgIDxkaXYgY2xhc3M9InRhYmxlLWFjdGlvbnMiPgogICAgICAgICAgICAgICAgPGJ1dHRvbiBjbGFzcz0iYnRuIGJ0bi1vdXRsaW5lIGJ0bi1zbSIgb25jbGljaz0ib3BlbkVkaXRNb2RhbCgnJHtlc2NhcGVIdG1sKHUudXNlcil9JywgJHtpc0FjdGl2ZX0pIj5FZGl0YXI8L2J1dHRvbj4KICAgICAgICAgICAgICAgIDxidXR0b24gY2xhc3M9ImJ0biBidG4tZGFuZ2VyIGJ0bi1zbSIgb25jbGljaz0iZGVsZXRlVXNlcignJHtlc2NhcGVIdG1sKHUudXNlcil9JykiPkVsaW1pbmFyPC9idXR0b24+CiAgICAgICAgICAgICAgPC9kaXY+CiAgICAgICAgICAgIDwvdGQ+CiAgICAgICAgICA8L3RyPgogICAgICAgIGA7CiAgICAgIH0pLmpvaW4oJycpOwogICAgfQoKICAgIGZ1bmN0aW9uIHRvZ2dsZVBhc3ModXNlcm5hbWUsIHBsYWluUGFzcykgewogICAgICBjb25zdCBlbCA9IGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKGBwYXNzLSR7dXNlcm5hbWV9YCk7CiAgICAgIGlmIChlbC5pbm5lclRleHQgPT09ICfigKLigKLigKLigKLigKLigKLigKLigKInKSB7CiAgICAgICAgZWwuaW5uZXJUZXh0ID0gcGxhaW5QYXNzOwogICAgICAgIGVsLmNsYXNzTGlzdC5yZW1vdmUoJ3Bhc3MtaGlkZGVuJyk7CiAgICAgIH0gZWxzZSB7CiAgICAgICAgZWwuaW5uZXJUZXh0ID0gJ+KAouKAouKAouKAouKAouKAouKAouKAoic7CiAgICAgICAgZWwuY2xhc3NMaXN0LmFkZCgncGFzcy1oaWRkZW4nKTsKICAgICAgfQogICAgfQoKICAgIGZ1bmN0aW9uIGNvcHlUZXh0KHR4dCkgewogICAgICBuYXZpZ2F0b3IuY2xpcGJvYXJkLndyaXRlVGV4dCh0eHQpOwogICAgICBzaG93VG9hc3QoJ0NvcGlhZG8gYWwgcG9ydGFwYXBlbGVzJyk7CiAgICB9CgogICAgLy8gTW9kYWwgQ3JlYXIgLyBFZGl0YXIKICAgIGZ1bmN0aW9uIG9wZW5DcmVhdGVNb2RhbCgpIHsKICAgICAgZ19Jc0VkaXRpbmcgPSBmYWxzZTsKICAgICAgZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoJ21vZGFsLXRpdGxlJykuaW5uZXJUZXh0ID0gJ051ZXZhIEN1ZW50YSc7CiAgICAgIGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCdtb2RhbC11c2VyJykudmFsdWUgPSAnJzsKICAgICAgZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoJ21vZGFsLXVzZXInKS5kaXNhYmxlZCA9IGZhbHNlOwogICAgICAKICAgICAgZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoJ21vZGFsLWFjdGl2ZScpLmNoZWNrZWQgPSB0cnVlOwogICAgICBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgndXNlci1tb2RhbCcpLmNsYXNzTGlzdC5hZGQoJ29wZW4nKTsKICAgICAgc2V0VGltZW91dCgoKSA9PiBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgnbW9kYWwtdXNlcicpLmZvY3VzKCksIDUwKTsKICAgIH0KCiAgICBmdW5jdGlvbiBvcGVuRWRpdE1vZGFsKHVzZXIsIGFjdGl2ZSkgewogICAgICBnX0lzRWRpdGluZyA9IHRydWU7CiAgICAgIGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCdtb2RhbC10aXRsZScpLmlubmVyVGV4dCA9IGBFZGl0YXIgQ3VlbnRhOiAke3VzZXJ9YDsKICAgICAgZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoJ21vZGFsLXVzZXInKS52YWx1ZSA9IHVzZXI7CiAgICAgIGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCdtb2RhbC11c2VyJykuZGlzYWJsZWQgPSB0cnVlOwogICAgICBkb2N1bWVudC5nZXRFbGVtZW50QnlJZCgnbW9kYWwtYWN0aXZlJykuY2hlY2tlZCA9IGFjdGl2ZTsKICAgICAgZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoJ3VzZXItbW9kYWwnKS5jbGFzc0xpc3QuYWRkKCdvcGVuJyk7CiAgICB9CgogICAgZnVuY3Rpb24gY2xvc2VVc2VyTW9kYWwoKSB7CiAgICAgIGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCd1c2VyLW1vZGFsJykuY2xhc3NMaXN0LnJlbW92ZSgnb3BlbicpOwogICAgfQoKICAgIGFzeW5jIGZ1bmN0aW9uIHNhdmVVc2VyKCkgewogICAgICBjb25zdCB1c2VyID0gZG9jdW1lbnQuZ2V0RWxlbWVudEJ5SWQoJ21vZGFsLXVzZXInKS52YWx1ZS50cmltKCk7CiAgICAgIGNvbnN0IGFjdGl2ZSA9IGRvY3VtZW50LmdldEVsZW1lbnRCeUlkKCdtb2RhbC1hY3RpdmUnKS5jaGVja2VkOwoKICAgICAgaWYgKCF1c2VyKSB7CiAgICAgICAgc2hvd1RvYXN0KCdFbCBub21icmUgZGUgdXN1YXJpbyBlcyBvYmxpZ2F0b3JpbycsICdlcnJvcicpOwogICAgICAgIHJldHVybjsKICAgICAgfQoKICAgICAgdHJ5IHsKICAgICAgICBjb25zdCByZXMgPSBhd2FpdCBmZXRjaChgJHtBUElfQkFTRX0vYXBpL2FkbWluL3VzZXJgLCB7CiAgICAgICAgICBtZXRob2Q6ICdQT1NUJywKICAgICAgICAgIGhlYWRlcnM6IHsKICAgICAgICAgICAgJ0NvbnRlbnQtVHlwZSc6ICdhcHBsaWNhdGlvbi9qc29uJywKICAgICAgICAgICAgJ1gtQWRtaW4tS2V5JzogZ19BZG1pbktleQogICAgICAgICAgfSwKICAgICAgICAgIGJvZHk6IEpTT04uc3RyaW5naWZ5KHsgdXNlciwgYWN0aXZlIH0pCiAgICAgICAgfSk7CiAgICAgICAgY29uc3QgZGF0YSA9IGF3YWl0IHJlcy5qc29uKCk7CiAgICAgICAgaWYgKGRhdGEuc3RhdHVzID09PSAnb2snKSB7CiAgICAgICAgICBzaG93VG9hc3QoZGF0YS5tZXNzYWdlKTsKICAgICAgICAgIGNsb3NlVXNlck1vZGFsKCk7CiAgICAgICAgICBsb2FkVXNlcnMoKTsKICAgICAgICB9IGVsc2UgewogICAgICAgICAgc2hvd1RvYXN0KGRhdGEubWVzc2FnZSwgJ2Vycm9yJyk7CiAgICAgICAgfQogICAgICB9IGNhdGNoIChlKSB7CiAgICAgICAgc2hvd1RvYXN0KCdFcnJvciBhbCBndWFyZGFyIHVzdWFyaW8nLCAnZXJyb3InKTsKICAgICAgfQogICAgfQoKICAgIGFzeW5jIGZ1bmN0aW9uIHJlc2V0VXNlckh3aWQodXNlcikgewogICAgICBpZiAoIWNvbmZpcm0oYERlc2VhcyByZXNldGVhciBlbCBIV0lEIGRlICIke3VzZXJ9Ij9cblxuQWwgcmVzZXRlYXJsbywgcG9kcmEgaW5pY2lhciBzZXNpb24gZGVzZGUgdW5hIG51ZXZhIGNvbXB1dGFkb3JhIHkgc2UgdmluY3VsYXJhIGEgbGEgbWlzbWEuYCkpIHsKICAgICAgICByZXR1cm47CiAgICAgIH0KCiAgICAgIHRyeSB7CiAgICAgICAgY29uc3QgcmVzID0gYXdhaXQgZmV0Y2goYCR7QVBJX0JBU0V9L2FwaS9hZG1pbi9yZXNldC1od2lkYCwgewogICAgICAgICAgbWV0aG9kOiAnUE9TVCcsCiAgICAgICAgICBoZWFkZXJzOiB7CiAgICAgICAgICAgICdDb250ZW50LVR5cGUnOiAnYXBwbGljYXRpb24vanNvbicsCiAgICAgICAgICAgICdYLUFkbWluLUtleSc6IGdfQWRtaW5LZXkKICAgICAgICAgIH0sCiAgICAgICAgICBib2R5OiBKU09OLnN0cmluZ2lmeSh7IHVzZXIgfSkKICAgICAgICB9KTsKICAgICAgICBjb25zdCBkYXRhID0gYXdhaXQgcmVzLmpzb24oKTsKICAgICAgICBpZiAoZGF0YS5zdGF0dXMgPT09ICdvaycpIHsKICAgICAgICAgIHNob3dUb2FzdChgSFdJRCBkZSAiJHt1c2VyfSIgcmVzZXRlYWRvIGNvbiBleGl0b2ApOwogICAgICAgICAgbG9hZFVzZXJzKCk7CiAgICAgICAgfSBlbHNlIHsKICAgICAgICAgIHNob3dUb2FzdChkYXRhLm1lc3NhZ2UsICdlcnJvcicpOwogICAgICAgIH0KICAgICAgfSBjYXRjaCAoZSkgewogICAgICAgIHNob3dUb2FzdCgnRXJyb3IgYWwgcmVzZXRlYXIgSFdJRCcsICdlcnJvcicpOwogICAgICB9CiAgICB9CgogICAgYXN5bmMgZnVuY3Rpb24gcmVzZXRBbGxId2lkcygpIHsKICAgICAgaWYgKCFjb25maXJtKCdBVEVOQ0lPTjogRGVzZWFzIGJsYW5xdWVhciBlbCBIV0lEIGRlIFRPREFTIGxhcyBjdWVudGFzIHJlZ2lzdHJhZGFzP1xuXG5Ub2RhcyBsYXMgY3VlbnRhcyBwYXNhcmFuIGEgZXN0YWRvICJMaWJyZSIgeSBzZSByZXZpbmN1bGFyYW4gYXV0b21hdGljYW1lbnRlIGEgbGEgUEMgZGVzZGUgbGEgcXVlIGluaWNpZW4gc2VzaW9uIGxhIHByb3hpbWEgdmV6LicpKSB7CiAgICAgICAgcmV0dXJuOwogICAgICB9CgogICAgICB0cnkgewogICAgICAgIGNvbnN0IHJlcyA9IGF3YWl0IGZldGNoKGAke0FQSV9CQVNFfS9hcGkvYWRtaW4vcmVzZXQtaHdpZGAsIHsKICAgICAgICAgIG1ldGhvZDogJ1BPU1QnLAogICAgICAgICAgaGVhZGVyczogewogICAgICAgICAgICAnQ29udGVudC1UeXBlJzogJ2FwcGxpY2F0aW9uL2pzb24nLAogICAgICAgICAgICAnWC1BZG1pbi1LZXknOiBnX0FkbWluS2V5CiAgICAgICAgICB9LAogICAgICAgICAgYm9keTogSlNPTi5zdHJpbmdpZnkoeyBhbGw6IHRydWUgfSkKICAgICAgICB9KTsKICAgICAgICBjb25zdCBkYXRhID0gYXdhaXQgcmVzLmpzb24oKTsKICAgICAgICBpZiAoZGF0YS5zdGF0dXMgPT09ICdvaycpIHsKICAgICAgICAgIHNob3dUb2FzdChkYXRhLm1lc3NhZ2UpOwogICAgICAgICAgbG9hZFVzZXJzKCk7CiAgICAgICAgfSBlbHNlIHsKICAgICAgICAgIHNob3dUb2FzdChkYXRhLm1lc3NhZ2UsICdlcnJvcicpOwogICAgICAgIH0KICAgICAgfSBjYXRjaCAoZSkgewogICAgICAgIHNob3dUb2FzdCgnRXJyb3IgYWwgYmxhbnF1ZWFyIEhXSURzJywgJ2Vycm9yJyk7CiAgICAgIH0KICAgIH0KCiAgICBhc3luYyBmdW5jdGlvbiB0b2dnbGVBY3RpdmUodXNlciwgbmV3QWN0aXZlU3RhdGUpIHsKICAgICAgdHJ5IHsKICAgICAgICBjb25zdCByZXMgPSBhd2FpdCBmZXRjaChgJHtBUElfQkFTRX0vYXBpL2FkbWluL3RvZ2dsZS1hY3RpdmVgLCB7CiAgICAgICAgICBtZXRob2Q6ICdQT1NUJywKICAgICAgICAgIGhlYWRlcnM6IHsKICAgICAgICAgICAgJ0NvbnRlbnQtVHlwZSc6ICdhcHBsaWNhdGlvbi9qc29uJywKICAgICAgICAgICAgJ1gtQWRtaW4tS2V5JzogZ19BZG1pbktleQogICAgICAgICAgfSwKICAgICAgICAgIGJvZHk6IEpTT04uc3RyaW5naWZ5KHsgdXNlciwgYWN0aXZlOiBuZXdBY3RpdmVTdGF0ZSB9KQogICAgICAgIH0pOwogICAgICAgIGNvbnN0IGRhdGEgPSBhd2FpdCByZXMuanNvbigpOwogICAgICAgIGlmIChkYXRhLnN0YXR1cyA9PT0gJ29rJykgewogICAgICAgICAgc2hvd1RvYXN0KGRhdGEubWVzc2FnZSk7CiAgICAgICAgICBsb2FkVXNlcnMoKTsKICAgICAgICB9IGVsc2UgewogICAgICAgICAgc2hvd1RvYXN0KGRhdGEubWVzc2FnZSwgJ2Vycm9yJyk7CiAgICAgICAgfQogICAgICB9IGNhdGNoIChlKSB7CiAgICAgICAgc2hvd1RvYXN0KCdFcnJvciBhbCBjYW1iaWFyIGVzdGFkbyBkZSBjdWVudGEnLCAnZXJyb3InKTsKICAgICAgfQogICAgfQoKICAgIGFzeW5jIGZ1bmN0aW9uIGRlbGV0ZVVzZXIodXNlcikgewogICAgICBpZiAoIWNvbmZpcm0oYFNlZ3VybyBxdWUgZGVzZWFzIGVsaW1pbmFyIGRlZmluaXRpdmFtZW50ZSBsYSBjdWVudGEgIiR7dXNlcn0iP2ApKSB7CiAgICAgICAgcmV0dXJuOwogICAgICB9CgogICAgICB0cnkgewogICAgICAgIGNvbnN0IHJlcyA9IGF3YWl0IGZldGNoKGAke0FQSV9CQVNFfS9hcGkvYWRtaW4vZGVsZXRlLXVzZXJgLCB7CiAgICAgICAgICBtZXRob2Q6ICdQT1NUJywKICAgICAgICAgIGhlYWRlcnM6IHsKICAgICAgICAgICAgJ0NvbnRlbnQtVHlwZSc6ICdhcHBsaWNhdGlvbi9qc29uJywKICAgICAgICAgICAgJ1gtQWRtaW4tS2V5JzogZ19BZG1pbktleQogICAgICAgICAgfSwKICAgICAgICAgIGJvZHk6IEpTT04uc3RyaW5naWZ5KHsgdXNlciB9KQogICAgICAgIH0pOwogICAgICAgIGNvbnN0IGRhdGEgPSBhd2FpdCByZXMuanNvbigpOwogICAgICAgIGlmIChkYXRhLnN0YXR1cyA9PT0gJ29rJykgewogICAgICAgICAgc2hvd1RvYXN0KGBDdWVudGEgIiR7dXNlcn0iIGVsaW1pbmFkYWApOwogICAgICAgICAgbG9hZFVzZXJzKCk7CiAgICAgICAgfSBlbHNlIHsKICAgICAgICAgIHNob3dUb2FzdChkYXRhLm1lc3NhZ2UsICdlcnJvcicpOwogICAgICAgIH0KICAgICAgfSBjYXRjaCAoZSkgewogICAgICAgIHNob3dUb2FzdCgnRXJyb3IgYWwgZWxpbWluYXIgdXN1YXJpbycsICdlcnJvcicpOwogICAgICB9CiAgICB9CgogICAgZnVuY3Rpb24gZXNjYXBlSHRtbChzdHIpIHsKICAgICAgaWYgKCFzdHIpIHJldHVybiAnJzsKICAgICAgcmV0dXJuIFN0cmluZyhzdHIpCiAgICAgICAgLnJlcGxhY2UoLyYvZywgJyZhbXA7JykKICAgICAgICAucmVwbGFjZSgvPC9nLCAnJmx0OycpCiAgICAgICAgLnJlcGxhY2UoLz4vZywgJyZndDsnKQogICAgICAgIC5yZXBsYWNlKC8iL2csICcmcXVvdDsnKQogICAgICAgIC5yZXBsYWNlKC8nL2csICcmIzM5OycpOwogICAgfQogIDwvc2NyaXB0Pgo8L2JvZHk+CjwvaHRtbD4=";

function getAdminHtml() {
  const binary = atob(ADMIN_HTML_B64);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) {
    bytes[i] = binary.charCodeAt(i);
  }
  return new TextDecoder().decode(bytes);
}
