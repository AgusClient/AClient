// AClient Gameskins Presence Relay & HWID Auth Server
// Native Node.js (Zero external dependencies)

const http = require('http');
const url = require('url');

const PORT = process.env.PORT || 3000;
const TTL_MS = 5 * 60 * 1000; // 5 minutos de inactividad

// Map: serverAddress -> Map: playerName -> { asset_game, last_seen }
const servers = new Map();

// Map: userName -> { password, hwid, active }
const users = new Map();

// Usuario de prueba por defecto
users.set('agus', {
  password: '123',
  hwid: null,
  active: true,
});

// Limpieza de entradas inactivas cada 60 segundos
setInterval(() => {
  const now = Date.now();
  for (const [server, players] of servers.entries()) {
    for (const [player, data] of players.entries()) {
      if (now - data.last_seen > TTL_MS) {
        players.delete(player);
      }
    }
    if (players.size === 0) {
      servers.delete(server);
    }
  }
}, 60000);

const server = http.createServer((req, res) => {
  // Encabezados CORS y tipo de respuesta
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type, User-Agent');

  if (req.method === 'OPTIONS') {
    res.writeHead(204);
    res.end();
    return;
  }

  const parsedUrl = url.parse(req.url, true);

  // ==========================================
  // AUTENTICACION HWID & LOGIN
  // ==========================================
  if (parsedUrl.pathname === '/api/auth/login' || parsedUrl.pathname === '/auth/login') {
    if (req.method !== 'POST') {
      res.writeHead(405, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ status: 'error', message: 'Metodo no permitido' }));
      return;
    }

    let body = '';
    req.on('data', chunk => {
      body += chunk;
      if (body.length > 1e6) req.destroy();
    });

    req.on('end', () => {
      try {
        const payload = JSON.parse(body);
        const { user, password, hwid } = payload;

        if (!user || !password || !hwid) {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ status: 'error', message: 'Faltan campos obligatorios' }));
          return;
        }

        const normalizedUser = user.trim().toLowerCase();
        const userData = users.get(normalizedUser);

        if (!userData) {
          res.writeHead(401, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ status: 'error', message: 'Usuario no registrado' }));
          return;
        }

        if (userData.active === false) {
          res.writeHead(403, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ status: 'error', message: 'Acceso revocado por el administrador' }));
          return;
        }

        if (userData.password !== password) {
          res.writeHead(401, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ status: 'error', message: 'Contraseña incorrecta' }));
          return;
        }

        // Si es la primera vez que inicia sesion, vinculamos su HWID
        if (!userData.hwid) {
          userData.hwid = hwid;
          userData.activated_at = Date.now();
        } else if (userData.hwid !== hwid) {
          res.writeHead(403, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({
            status: 'error',
            message: 'Esta cuenta ya esta vinculada a otra computadora'
          }));
          return;
        }

        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({
          status: 'ok',
          message: 'Autenticado correctamente',
          user: normalizedUser,
          hwid: userData.hwid
        }));

      } catch (err) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ status: 'error', message: 'Error procesando solicitud' }));
      }
    });
    return;
  }

  // ==========================================
  // PRESENCIA DE GAMESKINS (GET)
  // ==========================================
  if (req.method === 'GET') {
    const serverAddr = parsedUrl.query.server;
    const playerName = parsedUrl.query.player;

    if (playerName) {
      for (const [sAddr, playerMap] of servers.entries()) {
        if (playerMap.has(playerName)) {
          const data = playerMap.get(playerName);
          res.writeHead(200, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ player: playerName, asset_game: data.asset_game, server: sAddr }));
          return;
        }
      }
      res.writeHead(404, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ error: 'Player not found' }));
      return;
    }

    if (!serverAddr) {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ error: 'Missing server parameter' }));
      return;
    }

    const playerMap = servers.get(serverAddr);
    const result = [];
    if (playerMap) {
      const now = Date.now();
      for (const [player, data] of playerMap.entries()) {
        if (now - data.last_seen <= TTL_MS) {
          result.push({
            player: player,
            asset_game: data.asset_game
          });
        }
      }
    }

    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(result));
    return;
  }

  // ==========================================
  // PRESENCIA DE GAMESKINS (POST)
  // ==========================================
  if (req.method === 'POST') {
    let body = '';
    req.on('data', chunk => {
      body += chunk;
      if (body.length > 1e6) req.destroy();
    });

    req.on('end', () => {
      try {
        const data = JSON.parse(body);
        const { server: serverAddr, player, asset_game } = data;

        if (!serverAddr || !player || !asset_game) {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ error: 'Missing required fields' }));
          return;
        }

        if (!servers.has(serverAddr)) {
          servers.set(serverAddr, new Map());
        }

        servers.get(serverAddr).set(player, {
          asset_game: asset_game,
          last_seen: Date.now()
        });

        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ status: 'ok' }));
      } catch (err) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Invalid JSON payload' }));
      }
    });
    return;
  }

  res.writeHead(404, { 'Content-Type': 'application/json' });
  res.end(JSON.stringify({ error: 'Endpoint not found' }));
});

server.listen(PORT, () => {
  console.log(`[AClient Relay & Auth] Servidor activo en puerto ${PORT}`);
});
