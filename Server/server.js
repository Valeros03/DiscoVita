const { Client, GatewayIntentBits } = require('discord.js');
const { 
    joinVoiceChannel, 
    createAudioPlayer, 
    createAudioResource, 
    VoiceConnectionStatus, 
    EndBehaviorType,
    StreamType
} = require('@discordjs/voice');
const prism = require('prism-media');
const dgram = require('node:dgram');
const express = require('express');
const { PassThrough } = require('node:stream');

// =========================================================
// CONFIGURAZIONE
// =========================================================
const TOKEN = '*******'; // YOUR TOKEN
const API_PORT = 7777;
const UDP_PORT_MIC = 5555; 
const VITA_IP = '192.168.1.7';
const VITA_PORT = 5556;    

// =========================================================
// INIZIALIZZAZIONE SERVER E BOT
// =========================================================
const client = new Client({
    intents: [GatewayIntentBits.Guilds, GatewayIntentBits.GuildVoiceStates]
});

const app = express();
app.use(express.json());

const udpReceiver = dgram.createSocket('udp4'); 
const udpSender = dgram.createSocket('udp4');   

let connection = null;
let player = createAudioPlayer();
let micBuffer = null;

// Gestione Errori Player per evitare crash
player.on('error', error => {
    console.error('⚠️ Errore AudioPlayer Discord (ignorato):', error.message);
});

const { AudioPlayerStatus } = require('@discordjs/voice');

// Se il player si svuota (Idle), fa log invece di crashare silenziosamente
player.on(AudioPlayerStatus.Idle, () => {
    // È normale che accada se non parli o se c'è lag di rete
});

// =========================================================
// RICEZIONE UDP DALLA VITA (Microfono -> Discord)
// =========================================================
let lastSample = 0;
let audioQueue = Buffer.alloc(0); // Coda per accumulare i pacchetti
let isStreamingActive = false;    // Flag per sapere se il player è attivo

// Dimensione esatta di un frame Discord: 20ms a 48kHz Stereo 16-bit
// 48000 campioni * 2 canali * 2 byte = 192000 byte/sec -> diviso 50 (per 20ms) = 3840 byte
const FRAME_SIZE = 3840; 

udpReceiver.on('message', (msg) => {
    // Non chiudiamo né riavviamo più il flusso qui! Raccogliamo solo i dati.
    
    const outBuffer = Buffer.alloc(msg.length * 6);
    let outOffset = 0;
    
    for (let i = 0; i < msg.length; i += 2) {
        const currentSample = msg.readInt16LE(i);
        
        // Interpolazione (16kHz -> 48kHz)
        const s0 = lastSample;
        const s3 = currentSample;
        const s1 = Math.floor(s0 + (s3 - s0) * (1/3));
        const s2 = Math.floor(s0 + (s3 - s0) * (2/3));
        
        // Campione 1
        outBuffer.writeInt16LE(s1, outOffset); outBuffer.writeInt16LE(s1, outOffset + 2); outOffset += 4;
        // Campione 2
        outBuffer.writeInt16LE(s2, outOffset); outBuffer.writeInt16LE(s2, outOffset + 2); outOffset += 4;
        // Campione 3
        outBuffer.writeInt16LE(s3, outOffset); outBuffer.writeInt16LE(s3, outOffset + 2); outOffset += 4;

        lastSample = currentSample;
    }

    // Aggiungiamo i nuovi dati alla coda
    audioQueue = Buffer.concat([audioQueue, outBuffer]);
    
    // SISTEMA ANTI-LATENZA ⚡
    // Se la rete lagga e si accumulano troppi dati (più di 100ms di ritardo), 
    // tagliamo la coda scartando i dati vecchi per tornare in tempo reale puro.
    if (audioQueue.length > FRAME_SIZE * 5) {
        audioQueue = audioQueue.subarray(audioQueue.length - (FRAME_SIZE * 2));
    }
});

// =========================================================
// IL "CUORE PULSANTE" (Mantiene Discord sempre caldo)
// =========================================================
setInterval(() => {
    if (!micBuffer || micBuffer.destroyed || !isStreamingActive) return;

    if (audioQueue.length >= FRAME_SIZE) {
        // Abbiamo dati dalla Vita! Estraiamo 20ms di audio e li inviamo.
        const frame = audioQueue.subarray(0, FRAME_SIZE);
        audioQueue = audioQueue.subarray(FRAME_SIZE); // Rimuoviamo dalla coda
        
        try { micBuffer.write(frame); } catch (e) {}
    } else {
        // La Vita è in silenzio (Noise Gate attivo) o c'è lag di rete.
        // INIETTIAMO SILENZIO per evitare che Discord chiuda la connessione!
        try {
            micBuffer.write(Buffer.alloc(FRAME_SIZE)); // Buffer pieno di 0
            audioQueue = Buffer.alloc(0); // Svuotiamo i rimasugli per non sporcare l'audio
        } catch (e) {}
    }
}, 20); // Gira ogni 20 millisecondi esatti

function startStreamingToDiscord() {
    if (micBuffer) micBuffer.destroy();
    
    micBuffer = new PassThrough();
    micBuffer.on('error', () => {}); // Ignoriamo gli errori silenziosamente ora

    const resource = createAudioResource(micBuffer, {
        inputType: StreamType.Raw
    });

    player.play(resource);
    isStreamingActive = true;
    console.log("🎙️ Flusso audio microfono avviato in modalità 'Always-On'.");
}

// =========================================================
// DIREZIONE: DISCORD -> PS VITA (Casse) [PERFETTAMENTE FUNZIONANTE]
// =========================================================
function setupDiscordToVitaReceiver(conn) {
    const receiver = conn.receiver;

    receiver.speaking.on('start', (userId) => {
        const opusStream = receiver.subscribe(userId, {
            end: { behavior: EndBehaviorType.AfterSilence, duration: 100 },
        });

        const decoder = new prism.opus.Decoder({ rate: 48000, channels: 2, frameSize: 960 });
        
        opusStream.pipe(decoder);

        decoder.on('data', (pcmData) => {
            const outBuffer = Buffer.alloc(pcmData.length / 6); 
            let outOffset = 0;

            for (let i = 0; i < pcmData.length; i += 12) {
                const left = pcmData.readInt16LE(i);
                const right = pcmData.readInt16LE(i + 2);
                const mono = Math.floor((left + right) / 2);
                outBuffer.writeInt16LE(mono, outOffset);
                outOffset += 2;
            }

            udpSender.send(outBuffer, VITA_PORT, VITA_IP);
        });

        opusStream.on('end', () => {
            decoder.destroy();
        });
    });
}

// =========================================================
// API HTTP E GESTIONE CONNESSIONI
// =========================================================
app.post('/api/join', async (req, res) => {
    const { guild_id, channel_id } = req.body;
    console.log(`🔌 Connessione a: Guild ${guild_id}, Channel ${channel_id}`);

    try {
        const guild = await client.guilds.fetch(guild_id);
        const channel = await guild.channels.fetch(channel_id);

        connection = joinVoiceChannel({
            channelId: channel.id,
            guildId: guild.id,
            adapterCreator: guild.voiceAdapterCreator,
            selfDeaf: false,
            selfMute: false,
        });

        connection.on(VoiceConnectionStatus.Ready, () => {
            console.log('✅ Connesso. Inizio routing audio...');
            connection.subscribe(player);
            startStreamingToDiscord();
            setupDiscordToVitaReceiver(connection);
        });

        res.json({ status: "connesso", udp_port: UDP_PORT_MIC });
    } catch (e) {
        console.error("❌ Errore:", e);
        res.status(500).json({ error: e.message });
    }
});

app.post('/api/leave', (req, res) => {
    if (connection) {
        console.log('🔌 Chiusura graziosa della connessione...');
        
        // 1. Fermiamo il player PRIMA di distruggere lo stream
        player.stop(true);
        
        // 2. Chiudiamo il buffer in modo morbido (end invece di destroy)
        if (micBuffer) {
            micBuffer.end(); 
            // Aspettiamo un istante per far svuotare i buffer interni prima di distruggere
            setTimeout(() => {
                if (micBuffer) micBuffer.destroy();
            }, 100);
        }

        // 3. Disconnettiamo il bot
        connection.destroy();
        connection = null;
        console.log('✅ Disconnesso con successo.');
    }
    res.json({ status: "disconnesso" });
});

// =========================================================
// AVVIO
// =========================================================
client.on('ready', () => {
    console.log(`🤖 Bot online: ${client.user.tag}`);
    app.listen(API_PORT, () => console.log(`🌐 API in ascolto (Porta ${API_PORT})`));
    udpReceiver.bind(UDP_PORT_MIC, () => console.log(`🎧 UDP Mic in ascolto (Porta ${UDP_PORT_MIC})`));
});

client.login(TOKEN);