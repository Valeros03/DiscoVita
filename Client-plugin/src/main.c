#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/fcntl.h>
#include <psp2/audioin.h>
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/audioout.h>
#include <psp2/ctrl.h>

#include "vitacord_voice.h"
#include "VitaCordEngine.h"

// Costanti dei moduli di sistema
#ifndef SCE_SYSMODULE_NET
#define SCE_SYSMODULE_NET 0x0001
#endif
#ifndef SCE_SYSMODULE_RTC
#define SCE_SYSMODULE_RTC 0x0002
#endif
#ifndef SCE_SYSMODULE_AUDIOIN
#define SCE_SYSMODULE_AUDIOIN 0x000D
#endif
#ifndef SCE_SYSMODULE_AUDIOOUT
#define SCE_SYSMODULE_AUDIOOUT 0x000C
#endif
#ifndef SCE_SYSMODULE_AVCODEC
#define SCE_SYSMODULE_AVCODEC 0x002B
#endif
#ifndef SCE_SYSMODULE_VOICE
#define SCE_SYSMODULE_VOICE 0x0019
#endif

#define VC_VERSION 100

// --- MACRO E COSTANTI DI RETE ---
#define VOICE_PLUGIN_PORT   9999
#define API_IP "192.168.1.7" // Sostituisci con l'IP del tuo PC
#define API_PORT 7777

volatile int g_is_streaming = 0;
unsigned int g_target_ip = 0;
int g_target_port = 0;

// --- VARIABILI GLOBALI (Motore Audio) ---
uint32_t portOutDevice = 0;
uint32_t portInPCM = 0;
uint32_t portMic = 0;
uint32_t portNetOut = 0;
SceUID voiceMemBlock = -1;
void *voiceMemBase = NULL;

static VitaCordInitParam g_initParam __attribute__((aligned(32)));
static VitaCordStartParam g_startParam __attribute__((aligned(32)));

// --- FUNZIONI DI SUPPORTO (Clib-free) ---
void *custom_memset(void *s, int c, unsigned int n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

unsigned int custom_strlen(const char *str) {
    const char *s = str;
    while (*s) ++s;
    return (s - str);
}

void write_simple(const char *text) {
    unsigned int len = custom_strlen(text);
    if (len == 0) return;
    SceUID fd = sceIoOpen("ux0:/data/vitacord_test.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, text, len);
        sceIoClose(fd);
    }
}

void log_hex(const char *prefix, unsigned int val) {
    write_simple(prefix);
    char buf[16];
    custom_memset(buf, 0, 16);
    buf[0] = ' '; buf[1] = '0'; buf[2] = 'x';
    char hex_chars[] = "0123456789ABCDEF";
    for (int i = 0; i < 8; i++) {
        buf[3 + i] = hex_chars[(val >> (28 - (i * 4))) & 0xF];
    }
    buf[11] = '\n';
    buf[12] = '\0';
    write_simple(buf);
}

void log_port_state(int state, unsigned int numByte) {
    char buf[128];
    custom_memset(buf, 0, 128);
    char *ptr = buf;
    const char *prefix = "[RX] STATO MOTORE: ";
    while (*prefix) *ptr++ = *prefix++;
    *ptr++ = (char)('0' + state);
    const char *mid = " | Byte nel buffer: ";
    while (*mid) *ptr++ = *mid++;
    if (numByte == 0) {
        *ptr++ = '0';
    } else {
        char temp[16];
        int i = 0;
        unsigned int temp_val = numByte;
        while (temp_val > 0) {
            temp[i++] = (temp_val % 10) + '0';
            temp_val /= 10;
        }
        while (i > 0) {
            i--;
            *ptr++ = temp[i];
        }
    }
    *ptr++ = '\n';
    *ptr = '\0';
    write_simple(buf);
}

// --- DISCONNESSIONE BOT (TCP) ---
void disconnect_discord_bot() {
    int sock = sceNetSocket("DiscordLeaveSock", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
    if (sock < 0) return;

    SceNetSockaddrIn addr;
    custom_memset(&addr, 0, sizeof(addr));
    addr.sin_family = SCE_NET_AF_INET;
    addr.sin_port = sceNetHtons(API_PORT);
    sceNetInetPton(SCE_NET_AF_INET, API_IP, &addr.sin_addr);

    int timeout = 1000000; 
    sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_SNDTIMEO, &timeout, sizeof(timeout));

    if (sceNetConnect(sock, (SceNetSockaddr *)&addr, sizeof(addr)) == 0) {
        char request[256];
        char *ptr = request; 
        const char *part1 = "POST /api/leave HTTP/1.1\r\nHost: ";
        while (*part1) *ptr++ = *part1++;
        const char *ip_ptr = API_IP;
        while (*ip_ptr) *ptr++ = *ip_ptr++;
        *ptr++ = ':';

        char port_str[8];
        int p_idx = 0;
        int temp_port = API_PORT;
        while (temp_port > 0) {
            port_str[p_idx++] = (temp_port % 10) + '0';
            temp_port /= 10;
        }
        while (p_idx > 0) {
            p_idx--;
            *ptr++ = port_str[p_idx];
        }

        const char *part2 = "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        while (*part2) *ptr++ = *part2++;

        unsigned int req_length = (unsigned int)(ptr - request);
        sceNetSend(sock, request, req_length, 0);
    }
    sceNetSocketClose(sock);
}

// --- SETUP MOTORE AUDIO ---
int setup_voice_engine() {
    int ret;
    sceKernelDelayThread(5 * 1000 * 1000); 

    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    sceSysmoduleLoadModule(SCE_SYSMODULE_RTC);
    sceSysmoduleLoadModule(SCE_SYSMODULE_AUDIOIN);
    sceSysmoduleLoadModule(SCE_SYSMODULE_AUDIOOUT);
    sceSysmoduleLoadModule(SCE_SYSMODULE_AVCODEC); 
    sceSysmoduleLoadModule(SCE_SYSMODULE_VOICE);

    SceKernelAllocMemBlockOpt opt;
    custom_memset(&opt, 0, sizeof(opt));
    opt.size = sizeof(opt);
    opt.attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT;
    opt.alignment = 0x1000;

    voiceMemBlock = sceKernelAllocMemBlock("VitaCordContainer", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x80000, &opt);
    if (voiceMemBlock < 0) return voiceMemBlock;
    
    sceKernelGetMemBlockBase(voiceMemBlock, &voiceMemBase);
    
    custom_memset(&g_initParam, 0, sizeof(g_initParam));
    g_initParam.appType = 0x20000000; // Valore AppType Party
    
    ret = vc_voice_init(&g_initParam, VC_VERSION); 
    return 0; 
}

// --- THREAD RX (Network -> Speaker) ---
int audio_rx_thread(SceSize args, void *argp) {
    while (1) {
        if (g_is_streaming) {
            int udp_sock = sceNetSocket("VitaCord_RX", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
            SceNetSockaddrIn rx_addr;
            rx_addr.sin_family = SCE_NET_AF_INET;
            rx_addr.sin_port = sceNetHtons(5556); 
            rx_addr.sin_addr.s_addr = sceNetHtonl(0); 
            sceNetBind(udp_sock, (SceNetSockaddr*)&rx_addr, sizeof(rx_addr));

            short audio_buf[512] __attribute__((aligned(64)));

            while (g_is_streaming) {
                int read_bytes = sceNetRecvfrom(udp_sock, audio_buf, sizeof(audio_buf), 0, NULL, NULL);
                if (read_bytes > 0) {
                    uint32_t size_to_write = read_bytes;
                    vc_voice_write_port(portInPCM, (const void *)audio_buf, &size_to_write, 0);
                } else if (read_bytes < 0) {
                    sceKernelDelayThread(10000); 
                }
            }
            if (udp_sock >= 0) sceNetSocketClose(udp_sock);
        } else {
            sceKernelDelayThread(100 * 1000); 
        }
    }
    return 0;
}

// --- THREAD TX (Mic -> Network) ---
int audio_thread(SceSize args, void *argp) {
    while (1) {
        if (g_is_streaming) {
            int udp_sock = sceNetSocket("VitaCord_UDP", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
            SceNetSockaddrIn udp_addr;
            udp_addr.sin_family = SCE_NET_AF_INET;
            udp_addr.sin_port = sceNetHtons(g_target_port);
            udp_addr.sin_addr.s_addr = sceNetHtonl(g_target_ip);

            if (udp_sock >= 0) {
                uint32_t sizeToRead;
                uint8_t voiceBuffer[1024] __attribute__((aligned(64))); 

                while (g_is_streaming) {
                    sizeToRead = sizeof(voiceBuffer); 
                    int ret = vc_voice_read_port(portNetOut, voiceBuffer, &sizeToRead);
                    
                    if (ret == VC_OK && sizeToRead > 0) {
                        sceNetSendto(udp_sock, voiceBuffer, sizeToRead, 0, (SceNetSockaddr*)&udp_addr, sizeof(udp_addr));
                    } else {
                        sceKernelDelayThread(5 * 1000); 
                    }
                }
            }
            if (udp_sock >= 0) sceNetSocketClose(udp_sock);
        } else {
            sceKernelDelayThread(100 * 1000); 
        }
    }
    return 0;
}

// --- THREAD SERVER TCP ---
unsigned int parse_ip(const char *ip_str) {
    unsigned int bytes[4] = {0};
    int index = 0;
    for (int i = 0; ip_str[i] != '\0' && index < 4; i++) {
        if (ip_str[i] == '.') index++;
        else if (ip_str[i] >= '0' && ip_str[i] <= '9') bytes[index] = (bytes[index] * 10) + (ip_str[i] - '0');
    }
    return (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
}

int tcp_server_thread(SceSize args, void *argp) {
    SceUID memid = sceKernelAllocMemBlock("NetMem", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 256 * 1024, NULL);
    void *net_memory = NULL;
    sceKernelGetMemBlockBase(memid, &net_memory);
    SceNetInitParam net_param = {net_memory, 256 * 1024, 0};
    sceNetInit(&net_param);
    sceNetCtlInit();

    SceUID audio_thid = sceKernelCreateThread("VC_Audio_TX", audio_thread, 0x40, 0x10000, 0, 0, NULL);
    sceKernelStartThread(audio_thid, 0, NULL);
    
    SceUID rx_thid = sceKernelCreateThread("VC_Audio_RX", audio_rx_thread, 0x40, 0x10000, 0, 0, NULL);
    sceKernelStartThread(rx_thid, 0, NULL);

    int server_sock = sceNetSocket("VC_TCP", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
    SceNetSockaddrIn server_addr;
    server_addr.sin_family = SCE_NET_AF_INET;
    server_addr.sin_addr.s_addr = sceNetHtonl(0x7F000001); 
    server_addr.sin_port = sceNetHtons(VOICE_PLUGIN_PORT);
    sceNetBind(server_sock, (SceNetSockaddr*)&server_addr, sizeof(server_addr));
    sceNetListen(server_sock, 5);

    while (1) {
        int client_sock = sceNetAccept(server_sock, NULL, NULL);
        if (client_sock >= 0) {
            VitaCordCommand cmd;
            int read_bytes = sceNetRecv(client_sock, &cmd, sizeof(cmd), 0);
            if (read_bytes == sizeof(cmd)) {
                
                if (cmd.command == CMD_START_STREAMING) {
                    if (!g_is_streaming) {
                        static VitaCordPortParam portParam __attribute__((aligned(32)));

                        // Porta MIC IN
                        custom_memset(&portParam, 0, sizeof(portParam));
                        portParam.portType = VC_PORT_MIC; 
                        portParam.volume = 1.0f;
                        portParam.device.playerId = 0; 
                        int ret = vc_voice_create_port(&portMic, &portParam);

                        // Porta NET OUT
                        custom_memset(&portParam, 0, sizeof(portParam));
                        portParam.portType = VC_PORT_OUT_PCM; 
                        portParam.volume = 1.0f;
                        portParam.pcmaudio.bufSize = 16384; 
                        portParam.pcmaudio.format.dataType = VC_PCM_S16LE; 
                        portParam.pcmaudio.format.sampleRate = VC_SAMPLE_RATE_16K; 
                        ret = vc_voice_create_port(&portNetOut, &portParam);

                        // Porta NET IN
                        custom_memset(&portParam, 0, sizeof(portParam));
                        portParam.portType = VC_PORT_IN_PCM; 
                        portParam.bMute = 0;       
                        portParam.volume = 1.0f;
                        portParam.threshold = 256; 
                        portParam.pcmaudio.bufSize = 16384;
                        portParam.pcmaudio.format.dataType = VC_PCM_S16LE;      
                        portParam.pcmaudio.format.sampleRate = VC_SAMPLE_RATE_16K;
                        ret = vc_voice_create_port(&portInPCM, &portParam);

                        // Porta SPEAKER OUT
                        custom_memset(&portParam, 0, sizeof(portParam));
                        portParam.portType = VC_PORT_SPEAKER;     
                        portParam.device.playerId = 0;      
                        portParam.volume = 1.0f;      
                        ret = vc_voice_create_port(&portOutDevice, &portParam);

                        // Connessioni
                        vc_voice_connect_ports(portMic, portNetOut);
                        vc_voice_connect_ports(portInPCM, portOutDevice);

                        /*
                        int topo_ret = vc_voice_check_topology();
                        if (topo_ret == VC_OK) log_hex("TOPOLOGY OK: ", topo_ret);
                        else log_hex("ERROR TOPOLOGY: ", topo_ret);
                        */
                       
                        custom_memset(&g_startParam, 0, sizeof(g_startParam));
                        g_startParam.container = voiceMemBlock;
                        ret = vc_voice_start(&g_startParam);
                        
                        vc_voice_resume_port(portMic);
                        vc_voice_resume_port(portNetOut);
                        vc_voice_resume_port(portInPCM);
                        vc_voice_resume_port(portOutDevice);

                        vc_voice_set_mute(portMic, 0);
                        vc_voice_set_mute(portNetOut, 0);
                        vc_voice_set_mute(portInPCM, 0);
                        vc_voice_set_mute(portOutDevice, 0);

                        vc_voice_set_volume(portMic, 1.0f);
                        vc_voice_set_volume(portNetOut, 1.0f);
                        vc_voice_set_volume(portInPCM, 1.0f);
                        vc_voice_set_volume(portOutDevice, 1.0f);

                        if (ret >= 0) {
                            g_target_ip = parse_ip(cmd.target_ip);
                            g_target_port = cmd.target_port;
                            g_is_streaming = 1; 
                        }
                    }
                    
                } else if (cmd.command == CMD_STOP_STREAMING) {
                    if (g_is_streaming) {
                        g_is_streaming = 0; 
                        vc_voice_stop();
                        vc_voice_disconnect_ports(portInPCM, portOutDevice);
                        vc_voice_disconnect_ports(portMic, portNetOut);
                        vc_voice_delete_port(portMic);
                        vc_voice_delete_port(portNetOut);
                        vc_voice_delete_port(portInPCM);
                        vc_voice_delete_port(portOutDevice);
                    }
                }
            }
            sceNetSocketClose(client_sock); 
        }
    }
    return 0;
}

// --- ENTRY POINT ---
int plugin_main_thread(SceSize args, void *argp) {
    int voice_status = setup_voice_engine();
    if (voice_status >= 0 || voice_status == (int)0x804E0002) {
        SceUID tcp_thid = sceKernelCreateThread("TCP_Thread", tcp_server_thread, 64, 0x10000, 0, 0, NULL);
        if (tcp_thid >= 0) sceKernelStartThread(tcp_thid, 0, NULL);
    }
    while (1) sceKernelDelayThread(100 * 1000); 
    return 0;
}

__attribute__((target("arm")))
int module_start(SceSize argc, const void *args) {
    SceUID main_thid = sceKernelCreateThread("VC_Main", plugin_main_thread, 64, 0x20000, 0, 0, NULL);
    if (main_thid >= 0) sceKernelStartThread(main_thid, 0, NULL);
    return SCE_KERNEL_START_SUCCESS; 
}

int module_stop(SceSize argc, const void *args) {
    disconnect_discord_bot();
    return SCE_KERNEL_STOP_SUCCESS;
}

int module_exit(SceSize argc, const void *args) {
    return SCE_KERNEL_STOP_SUCCESS;
}
