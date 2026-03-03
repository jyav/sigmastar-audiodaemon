#include <errno.h>          
#include <stdio.h>          
#include <stdlib.h>         
#include <unistd.h>         
#include <pthread.h>        
#include <string.h>

// --- INGENIC HEADERS REMOVED ---
/*
#include "imp/imp_audio.h"  
#include "imp/imp_log.h"    
*/

// --- SIGMASTAR HEADERS ADDED ---
#include "mi_sys.h"
#include "mi_ai.h"
#include "mi_ao.h"

#include "audio_common.h"   
#include "cJSON.h"          
#include "config.h"         
#include "input.h"
#include "output.h"         // Required for DEFAULT_AO_DEV_ID for AEC Binding
#include "logging.h"        
#include "utils.h"          

#define TRUE 1
#define TAG "AI"

/**
 * Initializes the audio input device with the specified attributes.
 */
int initialize_audio_input_device(int aiDevID, int aiChnID) {
    AudioInputAttributes attrs = get_audio_input_attributes();

    // --- SIGMASTAR INITIALIZATION ---
    MI_AUDIO_Attr_t stAttr;

    // 1. HARDCODE STRICT SILICON CONSTRAINTS USING MACROS
    stAttr.eBitwidth = E_MI_AUDIO_BIT_WIDTH_16;
    stAttr.eSamplerate = DEFAULT_AI_SAMPLE_RATE;
    stAttr.eSoundmode = E_MI_AUDIO_SOUND_MODE_MONO;
    stAttr.eWorkmode = E_MI_AUDIO_MODE_I2S_MASTER;
    stAttr.u32PtNumPerFrm = DEFAULT_AI_PT_NUM_PER_FRM;
    stAttr.u32ChnCnt = DEFAULT_AI_CHN_CNT;

    // Initialize base hardware
    if (MI_AI_SetPubAttr(aiDevID, &stAttr) != 0 || 
        MI_AI_Enable(aiDevID) != 0 || 
        MI_AI_EnableChn(aiDevID, aiChnID) != 0) {
        handle_audio_error("AI: Failed to initialize SigmaStar audio attributes");
        exit(EXIT_FAILURE);
    }

    // 2. HARDWARE GAIN / VOLUME
    int vol = attrs.SetVolItem ? attrs.SetVolItem->valueint : DEFAULT_AI_CHN_VOL;
    if (vol < -60 || vol > 30) {
        printf("[ERROR] [%s] SetVol value out of range: %d. Clamping to %d.\n", TAG, vol, DEFAULT_AI_CHN_VOL);
        vol = DEFAULT_AI_CHN_VOL;
    }
    
    // Note: In the SigmaStar SDK, Microphone volume is primarily handled through 
    // the VQE module when hardware DSP is active.
    MI_AI_SetVqeVolume(aiDevID, aiChnID, vol);

    // 3. HARDWARE DSP (VQE: AEC, ANR, AGC)
    MI_AI_VqeConfig_t stAiVqe;
    memset(&stAiVqe, 0, sizeof(stAiVqe));

    // Hardcoded to TRUE because audio_common.c fails to parse the JSON for these
    stAiVqe.bAecOpen = TRUE; // Acoustic Echo Cancellation
    stAiVqe.bAnrOpen = TRUE; // Acoustic Noise Reduction
    stAiVqe.bAgcOpen = TRUE; // Auto Gain Control
    
    stAiVqe.u32ChnNum = 1;
    stAiVqe.s32WorkSampleRate = DEFAULT_AI_SAMPLE_RATE;
    stAiVqe.s32FrameSample = 128; // Strict SigmaStar VQE requirement

    // Configure AGC (Auto Gain Control) mapped to standard telecom curves
    stAiVqe.stAgcCfg.eMode = E_MI_AUDIO_ALGORITHM_MODE_DEFAULT; // FIXED
    
    // Configure ANR (Acoustic Noise Reduction)
    stAiVqe.stAnrCfg.eMode = E_MI_AUDIO_ALGORITHM_MODE_DEFAULT; // FIXED
    
    // 4. THE AEC BINDING (CRITICAL)
    // We bind the microphone's echo canceller directly to the Speaker Output (AO)
    // using the defaults established in output.h
    if (MI_AI_SetVqeAttr(aiDevID, aiChnID, DEFAULT_AO_DEV_ID, DEFAULT_AO_CHN_ID, &stAiVqe) == 0) {
        MI_AI_EnableVqe(aiDevID, aiChnID);
    } else {
        printf("[WARN] [%s] Failed to initialize AI VQE DSP.\n", TAG);
    }

    // Debugging prints
    printf("[INFO] AI HW Samplerate Locked: 16000 Hz\n");
    printf("[INFO] AI HW Volume: %d dB\n", vol);

    return 0;
}

/**
 * The main thread function for recording audio input.
 */
void *ai_record_thread(void *arg) {
    printf("[INFO] [AI] Entering ai_record_thread\n");

    int aiDevID, aiChnID;
    get_audio_input_device_attributes(&aiDevID, &aiChnID);

    printf("[INFO] Sending audio data to input client\n");

// --- CPU SPIN FIX: Honor the stop thread flag ---
    while (!g_stop_thread) {
        MI_AUDIO_Frame_t stAiChFrame;
        MI_AUDIO_AecFrame_t stAecFrame;
        
        memset(&stAiChFrame, 0, sizeof(MI_AUDIO_Frame_t));
        memset(&stAecFrame, 0, sizeof(MI_AUDIO_AecFrame_t));
        
        if (MI_AI_GetFrame(aiDevID, aiChnID, &stAiChFrame, &stAecFrame, -1) == 0) {
            
            // --- SIGMASTAR FIX: USE ISOLATED MUTEX ---
            pthread_mutex_lock(&client_list_lock);

            ClientNode *current = client_list_head;
            while (current) {
                // --- HARDWARE STALL FIX: Use non-blocking MSG_DONTWAIT ---
                ssize_t wr_sock = send(current->sockfd, stAiChFrame.apVirAddr[0], stAiChFrame.u32Len, MSG_DONTWAIT);
                
                // --- AUDIO CORRUPTION FIX: Catch partial sends to prevent byte-shifting ---
                if (wr_sock < 0 || wr_sock != stAiChFrame.u32Len) {
                    
                    if (wr_sock < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        // Buffer is full. Drop this frame.
                        current = current->next;
                        continue;
                    }
                    
                    if (wr_sock >= 0 && wr_sock != stAiChFrame.u32Len) {
                        printf("[WARN] Partial PCM send (%zd bytes). Stream misaligned. Kicking client.\n", wr_sock);
                    } else if (errno == EPIPE || errno == ECONNRESET) {
                        printf("[INFO] Client disconnected\n");
                    } else {
                        handle_audio_error("AI: send to sockfd");
                    }
                    
                    // The socket must be closed at the OS level before we free the memory node
                    close(current->sockfd); 
                    
                    if (current == client_list_head) {
                        client_list_head = current->next;
                        free(current);
                        current = client_list_head;
                    } else {
                        ClientNode *temp = client_list_head;
                        while (temp->next != current) {
                            temp = temp->next;
                        }
                        temp->next = current->next;
                        free(current);
                        current = temp->next;
                    }
                    continue;
                }
                current = current->next;
            }
            
            pthread_mutex_unlock(&client_list_lock);
            MI_AI_ReleaseFrame(aiDevID, aiChnID, &stAiChFrame, &stAecFrame);
        } else {
            // If the hardware drops or is being disabled, sleep 10ms to prevent a 100% CPU lockup
            usleep(10000);
        }
    }
    return NULL;
}

int disable_audio_input() {
    int aiDevID, aiChnID;
    get_audio_input_device_attributes(&aiDevID, &aiChnID);

    if (MI_AI_DisableVqe(aiDevID, aiChnID) != 0) {
        printf("[ERROR] [%s] SigmaStar audio VQE disable error\n", TAG);
        return -1;
    }
    
    if (MI_AI_DisableChn(aiDevID, aiChnID) != 0) {
        printf("[ERROR] [%s] SigmaStar audio channel disable error\n", TAG);
        return -1;
    }

    if (MI_AI_Disable(aiDevID) != 0) {
        printf("[ERROR] [%s] SigmaStar audio device disable error\n", TAG);
        return -1;
    }

    MI_AI_ClrPubAttr(aiDevID); // FIXED: Prevent kernel lock on daemon restart
    
    return 0;
}
