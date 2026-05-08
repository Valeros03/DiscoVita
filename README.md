

# 🎮 VitaCord Bridge: Native Discord Audio for PS Vita

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-PS%20Vita-black.svg)
![Node.js](https://img.shields.io/badge/node-%3E%3D18.0.0-brightgreen)

**VitaCord Bridge** is an advanced, low-latency audio routing system that allows the PlayStation Vita to natively participate in Discord Voice Channels. By leveraging reverse-engineered `SceVoice` topologies and a custom Node.js bridge, this project achieves real-time, two-way audio communication bypassing modern Discord End-to-End Encryption (DAVE) hurdles.

> **⚠️ Disclaimer:** This project is a clean-room reverse engineering effort created strictly for educational purposes and software interoperability. It does not contain any proprietary or confidential code/headers from Sony Interactive Entertainment. The PS Vita kernel plugin source code is kept private to ensure strict copyright compliance.

---

## 🏗️ Architecture & Data Flow

To bridge a legacy console with modern WebRTC-based voice servers, the architecture is split into two components:
1. **The PS Vita Kernel Plugin:** Intercepts audio hardware and handles UDP/TCP socket communication.
2. **The Node.js Node:** Acts as a real-time transcoder, handling the Discord Handshake and E2E Encryption.

```text

The architecture is split into three main components: 
1. PS Vita (Legacy Hardware)
2. Node.js Bridge (Local Network)
3. Discord Servers (External Network)

-----------------------------------------------------------------------
[ UPSTREAM ] - From PS Vita Microphone to Discord
-----------------------------------------------------------------------

 [ PS Vita ]  Microphone
      |
      | (Raw PCM 48kHz)
      v
 [ PS Vita ]  UDP Sender (Port 5555)
      |
      | [Local Network - UDP]
      v
 [ Node.js ]  UDP Receiver
      |
      | (Buffer to Stereo)
      v
 [ Node.js ]  AudioPlayer Stream
      |
      | [DAVE E2EE Protocol]
      v
 [ Discord ]  Discord Network (Voice Servers)


-----------------------------------------------------------------------
[ DOWNSTREAM ] - From Discord to PS Vita Speakers
-----------------------------------------------------------------------

 [ Discord ]  Discord Network (Voice Servers)
      |
      | [DAVE E2EE Protocol]
      v
 [ Node.js ]  Voice Receiver
      |
      | (Opus Decode)
      v
 [ Node.js ]  Downsample & Mono Mix
      |
      | (Precise 640-byte Packets)
      v
 [ Node.js ]  UDP Sender
      |
      | [Local Network - UDP]
      v
 [ PS Vita ]  UDP Receiver (Port 5556)
      |
      | (Raw PCM 16kHz)
      v
 [ PS Vita ]  Speakers / Party App

=======================================================================
```

## 🧗 Technical Challenges & Solutions
Building this bridge required solving several low-level networking, cryptography, and digital signal processing (DSP) challenges.

1. The DAVE Protocol (End-to-End Encryption)
Discord recently rolled out the DAVE protocol (Messaging Layer Security) for Voice Channels. Standard Python libraries (discord.py, py-cord) instantly drop the connection when attempting to receive audio sinks because they lack native MLS key-rotation support.

The Solution: Migrated the bridge infrastructure to Node.js. Utilizing @discordjs/voice combined with native C++ bindings (@snazzah/davey and sodium-native), the bridge successfully negotiates the DAVE handshake, decrypting incoming Opus packets on the fly.

2. Clock Synchronization & The "Pitch" Problem
Early prototypes resulted in audio that sounded either heavily slowed down (low pitch) or played back like a machine gun.

The Solution: Extensive debugging revealed a mismatch in hardware sampling rates. The PS Vita microphone records natively at 48kHz, but the Vita's output speakers (via the Party App topology) expect 16kHz.

Upstream (Mic): We intercept the 48kHz Mono stream, duplicate the bytes in memory to create a pseudo-Stereo stream, and pipe it as StreamType.Raw directly to Discord.

Downstream (Speakers): We decode the incoming 48kHz Stereo Opus from Discord, mathematically mix it to Mono, downsample it to 16kHz, and split it into precise chunks.

3. UDP Packet Fragmentation & Buffer Underruns
Node.js processes streams incredibly fast. Pumping the downsampled audio directly to the PS Vita caused buffer overflows, resulting in stuttering and long silences.

The Solution: Implemented a strict Byte-length Chunking mechanism. The Node.js server buffers the incoming Discord audio and releases it in perfect 640-byte packets (exactly 20ms of audio at 16kHz). This emulates a hardware clock, delivering the audio perfectly in sync with the PS Vita's internal ring buffer.

4. Graceful Degradation & Socket Leaks
Force-closing the PS Vita app left "ghost" bots in Discord channels due to lingering sockets.

The Solution: Hooked the PS Vita's module_stop function to fire a raw, ultra-fast TCP POST request with a 1-second timeout before the kernel kills the app. This triggers a graceful player.stop() and stream destruction on the Node.js side, preventing memory leaks and ERR_STREAM_PREMATURE_CLOSE panics.

## 🚀 How to Run the Bridge
### 1. PS Vita Side (Plugin Installation)
  1) Download VitaCordEngine.suprx from the Releases section.
  2) Transfer the file to your console in ur0:tai/.
  3) Open your ur0:tai/config.txt and add the following lines under the Party App Title ID to enable the engine:

```text
*NPXS10001
ur0:tai/VitaCordEngine.suprx
```

  4) Reboot your console or reload the configuration via VitaShell/Henkaku Settings.

### 2. Networking Reference
To ensure the bridge works, your devices must be configured with static IPs as follows:

| Device | Role | IP Address |
| :--- | :--- | :--- |
| PC / Server | Bridge Host | 192.168.1.24 |
| PS Vita | Client | 192.168.1.7 |

Required Ports:
- 5555 (UDP): Audio Upstream (PS Vita → Server)
- 5556 (UDP): Audio Downstream (Server → PS Vita)
- 7777 (TCP): API Control (PS Vita → Server)

### 3. Discord Bot Setup
You need a Discord Bot to act as your "proxy" in the voice channel.
  1) Go to the Discord Developer Portal.
  2) Click New Application and give it a name (e.g., "VitaCord").
  3) Navigate to the Bot tab:
     - Reset and copy your Token (you'll need this for server.js).
     - [CRITICAL]: Under "Privileged Gateway Intents", enable Voice State Intent.
  4) Navigate to OAuth2 → URL Generator:
     - Scopes: bot, guilds.
     - Bot Permissions: Connect, Speak, Use Voice Activity.
  5) Copy the generated URL into your browser to invite the bot to your server.
  6) 
### 4. Server Setup (Node.js)
  1) Navigate to the Server folder on your PC.
  2) Install all required dependencies at once with this command:

```bash
npm install discord.js @discordjs/voice @discordjs/opus prism-media @snazzah/davey sodium-native express
```
  3) Open server.js and update the token block:

```bash
const TOKEN = 'YOUR_DISCORD_BOT_TOKEN_HERE';
```

  4) Start the bridge:

```bash
 node server.js
```

### 💡 Additional Pro-Tips

- Firewall Configuration: If you are on Linux (Fedora/Ubuntu), ensure your firewall isn't blocking the ports. You might need to run:
  
```bash
firewall-cmd --add-port=5555/udp --add-port=5556/udp --add-port=7777/tcp --permanent
sudo firewall-cmd --reload
```
Static IPs: assign a static IP to your PS Vita and your PC/server through your router settings. If the IPs changes via DHCP, server and PC won't know where to send the audio!


## 🎮 Operating the Bridge (The "Background" Rule)
Because the kernel plugin is injected into the Party App (System Title ID: NPXS10001), this system application acts as the "host" for the bridge. To use VitaCord while playing a game, you must follow this specific workflow:

[Recommended Workflow]:
1) Launch the Party App: Open the Party app from the LiveArea. You do not need to create or join a party room; simply having the application launched is enough to "wake up" the SceVoice engine and the plugin's background threads.

2) Launch the VitaCord App: Open your VitaCord homebrew app to manage the connection.

3) Join a Channel: Select your desired Discord server and voice channel, then trigger the Join command.

4) Transition to your Game: Once the bot has joined and audio is flowing, you can press the PS Button to return to the LiveArea and launch any game.

    - Note: The OS will ask you to close the VitaCord app to launch the game. This is safe to do.

5) Keep Party Open: CRITICAL! Do NOT close or swipe away the Party app "page" in the LiveArea. As long as the Party app remains launched in the background, the bridge will stay active.

[Why is this necessary?]
- System Process Persistence: Unlike standard homebrew, the Party app is a system-level application that the PS Vita allows to remain resident in memory while a game is running. By injecting the engine into the Party app, we ensure the bridge isn't killed when you start your game.

- Audio Ducking: The native SceVoice engine managed by the Party app handles the "Audio Ducking" automatically, lowering your game's volume when your friends speak on Discord.

- Auto-Cleanup: If you wish to stop the bridge and disconnect the bot, simply close the Party app. The plugin's module_stop will trigger the /api/leave command automatically.

## 👨‍💻 About the Author
This project was built as a deep dive into embedded systems, reverse engineering, and real-time networking. If you are a recruiter or an engineer interested in the low-level C code running on the Vita kernel, feel free to reach out for a technical discussion!




    
