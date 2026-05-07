

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
Prerequisites
A hacked PS Vita with the VitaCord.suprx plugin installed (Download from Releases).

A PC/Server running on the same local network as the console.

Node.js (v18 or higher) and FFmpeg installed.

Setup Instructions
Clone this repository and navigate to the Server folder.

Run npm install to grab the required audio and crypto dependencies.

Open server.js and input your Discord Bot Token and your PS Vita's local IP address.

Start the bridge with:

```bash
node server.js
```
Open the target application on your PS Vita. The console will automatically establish the UDP/TCP handshake and bridge your audio!

## 👨‍💻 About the Author
This project was built as a deep dive into embedded systems, reverse engineering, and real-time networking. If you are a recruiter or an engineer interested in the low-level C code running on the Vita kernel, feel free to reach out for a technical discussion!




    