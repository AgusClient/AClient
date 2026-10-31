# AClient Gameskins Presence Relay

Servidor de presencia para la sincronizacion de gameskins entre jugadores de AClient.

## Como funciona

1. Al conectarse a un servidor de Teeworlds/DDNet, AClient envia un `POST` con:
   ```json
   {
     "server": "ip:puerto",
     "player": "NombreJugador",
     "asset_game": "nombre_skin"
   }
   ```
2. Inmediatamente despues, realiza un `GET ?server=ip:puerto` para obtener la lista de jugadores conectados a ese mismo servidor y los skins que estan utilizando:
   ```json
   [
     {
       "player": "OtroJugador",
       "asset_game": "game_06"
     }
   ]
   ```
3. Las entradas tienen un tiempo de vida (TTL) de 5 minutos, tras el cual se limpian automaticamente de memoria si el jugador se desconecta o deja de emitir presencia.

---

## Opcion 1: Ejecucion Local / VPS (Node.js)

Sin dependencias externas (utiliza unicamente modulos nativos de Node.js):

```bash
# Iniciar en puerto por defecto (3000)
node server.js

# O especificando puerto personalizado
PORT=8080 node server.js
```

Para mantenerlo corriendo en un VPS con `pm2` o `systemd`:
```bash
npm install -g pm2
pm2 start server.js --name "aclient-relay"
```

---

## Opcion 2: Cloudflare Worker

El archivo `cloudflare-worker.js` esta listo para desplegarse como un Worker con Cloudflare KV:

1. Crea un KV Namespace en Cloudflare llamado `PRESENCE_KV`.
2. Vincula la variable de entorno `PRESENCE_KV` en tu Worker.
3. Pega el contenido de `cloudflare-worker.js` y publica el Worker.
