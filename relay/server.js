// AClient Gameskins Presence Relay Server
// Native Node.js (Zero external dependencies)

const http = require('http');
const url = require('url');

const PORT = process.env.PORT || 3000;
const TTL_MS = 5 * 60 * 1000; // 5 minutos de inactividad

// Map: serverAddress -> Map: playerName -> { asset_game, last_seen }
const servers = new Map();

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

  // GET: Consultar skins de jugadores en un servidor (?server=ip:port)
  if (req.method === 'GET') {
    const serverAddr = parsedUrl.query.server;
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

  // POST: Anunciar presencia y skin actual
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
  console.log(`[AClient Relay] Servidor de presencia activo en puerto ${PORT}`);
});
