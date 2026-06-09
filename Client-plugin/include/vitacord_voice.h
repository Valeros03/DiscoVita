#ifndef VITACORD_VOICE_STUBS_H
#define VITACORD_VOICE_STUBS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- MACRO E COSTANTI ---
#define VC_OK 0

// Tipi di porta
#define VC_PORT_MIC          0
#define VC_PORT_IN_PCM       1
#define VC_PORT_OUT_PCM      3
#define VC_PORT_SPEAKER      5

// Formati Audio
#define VC_PCM_S16LE         0
#define VC_SAMPLE_RATE_16K   16000

// --- STRUTTURE DATI ---

typedef struct {
    int32_t dataType;
    int32_t sampleRate;
} VitaCordPCMFormat;

typedef struct {
    int32_t portType;
    uint16_t threshold;
    uint16_t bMute;
    float volume;
    union {
        struct { int32_t bitrate; } voice;
        struct {
            uint32_t bufSize;
            VitaCordPCMFormat format;
        } pcmaudio;
        struct { uint32_t playerId; } device;
    };
} __attribute__((aligned(32))) VitaCordPortParam;

typedef struct {
    int32_t appType;
    void *onEvent;      
    void *pUserData;
    uint8_t padding[20]; 
} __attribute__((aligned(32))) VitaCordInitParam;

typedef struct {
    int32_t container;
    uint8_t padding[28]; 
} __attribute__((aligned(32))) VitaCordStartParam;

typedef struct {
    int32_t portType;
    int32_t state;
    uint32_t *pEdge;
    uint32_t numByte;
    uint32_t frameSize;
    uint16_t numEdge;
    uint16_t reserved;
} __attribute__((aligned(32))) VitaCordPortInfo;


// --- PROTOTIPI DELLE FUNZIONI ---
// Il resolving dei NID avverrà tramite file .yml esterno

extern int vc_voice_init(VitaCordInitParam *param, int version);
extern int vc_voice_start(VitaCordStartParam *param);
extern int vc_voice_stop(void);

extern int vc_voice_create_port(uint32_t *portId, VitaCordPortParam *param);
extern int vc_voice_delete_port(uint32_t portId);

extern int vc_voice_connect_ports(uint32_t iport, uint32_t oport);
extern int vc_voice_disconnect_ports(uint32_t iport, uint32_t oport);
extern int vc_voice_check_topology(void);

extern int vc_voice_resume_port(uint32_t portId);
extern int vc_voice_set_mute(uint32_t portId, int mute);
extern int vc_voice_set_volume(uint32_t portId, float volume);

extern int vc_voice_write_port(uint32_t portId, const void *buf, uint32_t *size, int flag);
extern int vc_voice_read_port(uint32_t portId, void *buf, uint32_t *size);

extern int vc_voice_get_port_info(uint32_t portId, VitaCordPortInfo *info);

#ifdef __cplusplus
}
#endif

#endif // VITACORD_VOICE_STUBS_H
