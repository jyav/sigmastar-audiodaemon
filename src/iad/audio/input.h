#ifndef INPUT_H
#define INPUT_H

#include <pthread.h>

// --- SIGMASTAR HEADERS ---
#include "mi_ai.h"
#include "mi_sys.h"

// --- FIX REDUNDANT MACROS FROM audio_common.h ---
// We must undefine the sloppy Ingenic defaults inherited from audio_common.h
// to ensure the compiler strictly honors our SigmaStar hardware constraints.
#undef DEFAULT_AI_SAMPLE_RATE
#undef DEFAULT_AI_CHN_VOL
#undef DEFAULT_AI_GAIN
#undef DEFAULT_AI_CHN_CNT
#undef DEFAULT_AI_FRM_NUM
#undef DEFAULT_AI_DEV_ID
#undef DEFAULT_AI_CHN_ID
#undef DEFAULT_AI_USR_FRM_DEPTH

// --- SIGMASTAR AI DEFAULTS ---
#define DEFAULT_AI_SAMPLE_RATE E_MI_AUDIO_SAMPLE_RATE_16000
#define DEFAULT_AI_PT_NUM_PER_FRM 1024
#define DEFAULT_AI_CHN_VOL 10
#define DEFAULT_AI_GAIN 0
#define DEFAULT_AI_CHN_CNT 1
#define DEFAULT_AI_DEV_ID 0
#define DEFAULT_AI_CHN_ID 0

typedef struct {
    int sockfd;
} AiThreadArg;

// --- DEDICATED AI MUTEX ---
extern pthread_mutex_t client_list_lock;

// Functions
int initialize_audio_input_device(int aiDevID, int aiChnID);
void *ai_record_thread(void *output_file_path);
int disable_audio_input(void);

#endif // INPUT_H
