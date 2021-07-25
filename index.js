#!/usr/bin/env node

const mcproto = require("minecraft-protocol");

let motd = "&b&lTest MC Server&r &k12345";
let kickMessage = "You are not white-listed on this server!";

(async () => {
  motd = motd.replace(/&/g, "§");
  kickMessage = kickMessage.replace(/&/g, "§");

  mcproto.createServer({
    'online-mode': false,
    encryption: true,
    host: '0.0.0.0',
    port: 25565,
    motd: motd,
    maxPlayers: 99999,
    playersCount: 99999
  }).on('login', client => {
    client.end(kickMessage);
  });
  console.log("[Test] Server is running on 0.0.0.0:25565");
})();