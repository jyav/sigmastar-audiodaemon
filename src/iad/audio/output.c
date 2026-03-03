#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h> // Required for memset

// --- SIGMASTAR HEADERS ADDED ---
#include "mi_sys.h"
#include "mi_ao.h"

#include "audio_common.h"
#include "config.h"
#include "cJSON.h"
#include "output.h"
#include "logging.h"
#include "utils.h"

#define TRUE 1
#define TAG "AO"

// Global variable to hold the maximum frame size for audio output.
int g_ao_max_frame_size = DEFAULT_AO_MAX_FRAME_SIZE;

/**
 * Set the global maximum frame size for audio output.
 * @param frame_size The desired frame size.
 */
void set_ao_max_frame_size(int frame_size) {
    g_ao_max_frame_size = frame_size;
}

/**
 * Handles errors and reinitializes the audio device.
 * @param aoDevID Device ID.
 * @param aoChnID Channel ID.
 * @param errorMsg Error message to be handled.
 */
void handle_and_reinitialize_output(int aoDevID, int aoChnID, const char *errorMsg) {
    handle_audio_error(errorMsg);
    reinitialize_audio_output_device(aoDevID, aoChnID);
}

/**
 * Initializes the audio device using the attributes from the configuration.
 * @param aoDevID Device ID.
 * @param aoChnID Channel ID.
 */
void initialize_audio_output_device(int aoDevID, int aoChnID) {
    AudioOutputAttributes attrs = get_audio_attributes();

    // --- SIGMASTAR INITIALIZATION ---
    MI_AUDIO_Attr_t stAttr;

    // 1. HARDCODE STRICT SILICON CONSTRAINTS USING MACROS
    stAttr.eBitwidth = E_MI_AUDIO_BIT_WIDTH_16;
    stAttr.eSamplerate = DEFAULT_AO_SAMPLE_RATE;
    stAttr.eSoundmode = E_MI_AUDIO_SOUND_MODE_MONO;
    stAttr.eWorkmode = E_MI_AUDIO_MODE_I2S_MASTER;
    stAttr.u32PtNumPerFrm = DEFAULT_AO_PT_NUM_PER_FRM;
    stAttr.u32ChnCnt = DEFAULT_AO_CHN_CNT;

    // Initialize the base hardware
    if (MI_AO_SetPubAttr(aoDevID, &stAttr) != 0 || 
        MI_AO_Enable(aoDevID) != 0 || 
        MI_AO_EnableChn(aoDevID, aoChnID) != 0) {
        handle_audio_error("AO: Failed to initialize SigmaStar audio attributes");
        exit(EXIT_FAILURE);
    }

    // 2. HARDWARE GAIN/VOLUME
    int vol = attrs.SetVolItem ? attrs.SetVolItem->valueint : DEFAULT_AO_CHN_VOL;
    if (vol < -60 || vol > 30) {
        printf("[ERROR] [%s] SetVol value out of range: %d. Clamping to %d.\n", TAG, vol, DEFAULT_AO_CHN_VOL);
        vol = DEFAULT_AO_CHN_VOL;
    }
    MI_AO_SetMute(aoDevID, FALSE);
    if (MI_AO_SetVolume(aoDevID, vol) != 0) {
        handle_audio_error("AO: Failed to set SigmaStar volume attribute");
    }

    // 3. HARDWARE DSP (VQE: HPF & EQ)
    MI_AO_VqeConfig_t stAoVqe;
    memset(&stAoVqe, 0, sizeof(stAoVqe));
    
    stAoVqe.bHpfOpen = TRUE; 
    stAoVqe.bEqOpen = TRUE;  
    stAoVqe.s32WorkSampleRate = DEFAULT_AO_SAMPLE_RATE; 
    stAoVqe.s32FrameSample = 128; // Strict SigmaStar VQE requirement

    stAoVqe.stHpfCfg.eMode = E_MI_AUDIO_ALGORITHM_MODE_USER;
    stAoVqe.stHpfCfg.eHpfFreq = E_MI_AUDIO_HPF_FREQ_150; 

    if (MI_AO_SetVqeAttr(aoDevID, aoChnID, &stAoVqe) == 0) {
        MI_AO_EnableVqe(aoDevID, aoChnID);
    } else {
        printf("[WARN] [%s] Failed to initialize AO VQE DSP.\n", TAG);
    }

    // 4. OVERRIDE JSON FRAME SIZE FOR SIGMASTAR DMA SAFETY
    set_ao_max_frame_size(DEFAULT_AO_MAX_FRAME_SIZE);

    // Allocate memory for audio_buffer based on the strict hardware frame size
    audio_buffer = (unsigned char*) malloc(g_ao_max_frame_size);
    if (!audio_buffer) {
        handle_audio_error("AO: Failed to allocate memory for audio_buffer");
        exit(EXIT_FAILURE);
    }

    // Debugging prints
    printf("[INFO] AO HW Samplerate Locked: 16000 Hz\n");
    printf("[INFO] AO HW Volume: %d dB\n", vol);
}

/**
 * Cleans up resources used for audio output.
 */
void cleanup_audio_output() {
    if (audio_buffer) {
        free(audio_buffer);
        audio_buffer = NULL;
    }
}

/**
 * Reinitialize the audio device by first disabling it and then initializing.
 * @param aoDevID Device ID.
 * @param aoChnID Channel ID.
 */
void reinitialize_audio_output_device(int aoDevID, int aoChnID) {
    MI_AO_DisableVqe(aoDevID, aoChnID);
    MI_AO_DisableChn(aoDevID, aoChnID);
    MI_AO_Disable(aoDevID);
    initialize_audio_output_device(aoDevID, aoChnID);
}

/**
 * Thread function to continuously play audio.
 * @param arg Thread arguments.
 * @return NULL.
 */
void *ao_play_thread(void *arg) {
    printf("[INFO] [AO] Entering ao_play_thread\n");

    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    int aoDevID, aoChnID;
    get_audio_output_device_attributes(&aoDevID, &aoChnID);

    while (TRUE) {
        pthread_mutex_lock(&audio_buffer_lock);

        while (audio_buffer_size == 0) {
            pthread_mutex_lock(&g_stop_thread_mutex);
            if (g_stop_thread) {
                pthread_mutex_unlock(&audio_buffer_lock);
                pthread_mutex_unlock(&g_stop_thread_mutex);
                return NULL;
            }
            pthread_mutex_unlock(&g_stop_thread_mutex);

            pthread_cond_wait(&audio_data_cond, &audio_buffer_lock);
        }

        // --- SIGMASTAR FRAME SEND ---
        MI_AUDIO_Frame_t stAoSendFrame;
        
        // Zero out the struct to prevent stack garbage from corrupting the DMA registers
        // (This implicitly sets eBitwidth=0/16-bit and eSoundmode=0/Mono, matching init)
        memset(&stAoSendFrame, 0, sizeof(MI_AUDIO_Frame_t)); 
        
        stAoSendFrame.u32Len = audio_buffer_size;
        stAoSendFrame.apVirAddr[0] = audio_buffer;
        stAoSendFrame.apVirAddr[1] = NULL; 

        if (MI_AO_SendFrame(aoDevID, aoChnID, &stAoSendFrame, -1) != 0) {
            pthread_mutex_unlock(&audio_buffer_lock);

            // --- RACE CONDITION FIX: Do not resurrect hardware during shutdown ---
            if (g_stop_thread) {
                break;
            }
            
            handle_and_reinitialize_output(aoDevID, aoChnID, "MI_AO_SendFrame data error");
            continue;
        }

        audio_buffer_size = 0;
        pthread_mutex_unlock(&audio_buffer_lock);
    }

    return NULL;
}

/**
 * Disables the audio channel and audio devices.
 * @return 0 on success, -1 on failure.
 */
int disable_audio_output() {
    int ret;

    int aoDevID, aoChnID;
    get_audio_output_device_attributes(&aoDevID, &aoChnID);

    // --- LOGIC BUG FIXED: Force Mute to prevent shutdown pop ---
    int mute_status = 1; 
    mute_audio_output_device(mute_status);

    ret = MI_AO_DisableVqe(aoDevID, aoChnID);
    if (ret != 0) {
        printf("[ERROR] [%s] SigmaStar audio VQE disable error\n", TAG);
    }

    ret = MI_AO_DisableChn(aoDevID, aoChnID);
    if (ret != 0) {
        printf("[ERROR] [%s] SigmaStar audio channel disable error\n", TAG);
        return -1;
    }

    ret = MI_AO_Disable(aoDevID);
    if (ret != 0) {
        printf("[ERROR] [%s] SigmaStar audio device disable error\n", TAG);
        return -1;
    }

    cleanup_audio_output();

    return 0;
}
