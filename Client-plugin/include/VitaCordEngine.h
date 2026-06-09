#ifndef VITACORD_ENGINE_H
#define VITACORD_ENGINE_H

#include <psp2/types.h>

// Definiamo i comandi che l'app può inviare al plugin via TCP
typedef enum {
    CMD_STOP_STREAMING = 0,
    CMD_START_STREAMING = 1,
    CMD_SHUTDOWN_PLUGIN = 2 // Utile per chiudere il plugin in modo pulito
} CommandType;

// La struttura del pacchetto TCP che la tua app invierà al plugin
typedef struct {
    CommandType command;
    char target_ip[16];   // L'IP del Raspberry (es. "192.168.1.100")
    int target_port;      // La porta UDP (es. 5000)
} VitaCordCommand;

// Stato globale del motore
typedef struct {
    int is_streaming;
    char current_ip[16];
    int current_port;
    SceUID audio_thread_id;
    SceUID tcp_thread_id;
} EngineState;

#endif
