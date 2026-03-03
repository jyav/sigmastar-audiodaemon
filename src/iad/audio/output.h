#ifndef OUTPUT_H
#define OUTPUT_H

#include <pthread.h>

// --- INGENIC HEADERS REMOVED ---
/*
#include "imp/imp_audio.h"  // for AUDIO_SAMPLE_RATE_48000
*/

// --- SIGMASTAR HEADERS ADDED ---
#include "mi_ao.h"
#include "mi_sys.h"

// --- INGENIC DEFAULTS REMOVED ---
/*
#define DEFAULT_AO_SAMPLE_RATE AUDIO_SAMPLE_RATE_48000
#define DEFAULT_AO_MAX_FRAME_SIZE 1280
#define DEFAULT_AO_CHN_VOL 90
#define DEFAULT_AO_GAIN 25
#define DEFAULT_AO_CHN_CNT 1
#define DEFAULT_AO_FRM_NUM 20
#define DEFAULT_AO_DEV_ID 0
#define DEFAULT_AO_CHN_ID 0
*/

// --- SIGMASTAR DEFAULTS ADDED ---

// Target 16kHz (Wideband) to align perfectly with the hardware AEC math.
#define DEFAULT_AO_SAMPLE_RATE E_MI_AUDIO_SAMPLE_RATE_16000

// SigmaStar DMA chunk size: 1024 samples * 2 bytes (16-bit depth) = 2048 bytes
#define DEFAULT_AO_MAX_FRAME_SIZE 2048

// SigmaStar MI_AO_SetVolume uses a -60dB to +30dB scale. 
// +10dB is a solid default boost for small camera speakers.
#define DEFAULT_AO_CHN_VOL 10

// Hardware analog gain (if supported by specific board trace). Safe default is 0.
#define DEFAULT_AO_GAIN 0

// Mono (1 channel) is strictly required for the acoustic echo cancellation math.
#define DEFAULT_AO_CHN_CNT 1

// SigmaStar DMA ring buffer depth.
#define DEFAULT_AO_FRM_NUM 2

// Standard MI_AO hardware targets for the primary DAC.
#define DEFAULT_AO_DEV_ID 0
#define DEFAULT_AO_CHN_ID 0

// Functions
void reinitialize_audio_output_device(int aoDevID, int aoChnID);
void *ao_play_thread(void *arg);

extern int g_ao_max_frame_size;
void set_ao_max_frame_size(int frame_size);
void cleanup_audio_output();
int disable_audio_output(void);

// Global variable declaration for the maximum frame size for audio output.
extern int g_ao_max_frame_size;

// Global flag and mutex to control thread termination
extern volatile int g_stop_thread;
extern pthread_mutex_t g_stop_thread_mutex;

#endif // OUTPUT_H
