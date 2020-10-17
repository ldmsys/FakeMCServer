#!/usr/bin/env node
require('minecraft-protocol').createServer({
    'online-mode': false,
    encryption: true,
    host: '0.0.0.0',
    port: 25565,
    motd: "Test MC Server",
    version: "1.12.2",
    maxPlayers: 99999,
    playersCount: 99999
}).on('login', client => {
    client.write('position', {
        x: 0,
        y: 1.62,
        z: 0,
        yaw: 0,
        pitch: 0,
        flags: 0x00    
    });
    client.write("chat", { message: JSON.stringify({translate: "chat.type.announcement", with: [ 'Test', "It works!"]}), position: 0});
});
console.log("[Test] Server is running on 0.0.0.0:25565");