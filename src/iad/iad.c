/*
 * INGENIC AUDIO DAEMON (SigmaStar Port)
 *
 * This daemon manages audio input and output for SigmaStar devices.
 */

#if !defined(__UCLIBC__) && !defined(__GLIBC__)
#include <bits/signal.h>             
#endif

#include <signal.h>                  
#include <stdio.h>                   
#include <stdlib.h>
#include <pthread.h>                 

#include "mi_sys.h"
#include "iad.h"
#include "network/input_server.h"    
#include "network/output_server.h"   
#include "network/control_server.h"  
#include "audio/output.h"            
#include "utils/cmdline.h"           
#include "utils/config.h"            
#include "utils/utils.h"             
#include "utils/logging.h"           
#include "version.h"                 

#define TAG "IAD"

int main(int argc, char *argv[]) {
    printf("INGENIC AUDIO DAEMON Version: %s\n", VERSION);

    // 1. Parse command-line arguments
    CmdOptions options;
    if (parse_cmdline(argc, argv, &options)) {
        return 1;
    }

    if (options.daemonize) {
        daemonize();
    }

    if (is_already_running()) {
        exit(1);
    }

    setup_signal_handling();
    signal(SIGPIPE, SIG_IGN);

    // 2. LOAD CONFIGURATION FIRST
    // We must load JSON before touching any hardware, as the hardware 
    // initialization routines depend on the parsed attributes.
    char *config_file_path = options.config_file_path;
    int disable_ai = options.disable_ai;
    int disable_ao = options.disable_ao;

    if (config_load_from_file(config_file_path) == 0) {
        if (!validate_json(get_audio_config())) {
            handle_audio_error("Invalid configuration format. Continuing with default settings.", config_file_path);
        }
    } else {
        handle_audio_error("Failed to load configuration. Continuing with default settings. File", config_file_path);
    }

    // Determine final AI/AO states based on overrides
    if (!disable_ai) {
        disable_ai = !config_get_ai_enabled();
    }
    if (!disable_ao) {
        disable_ao = !config_get_ao_enabled();
    }

    // 3. MASTER SIGMASTAR INITIALIZATION
    printf("[INFO] Starting audio daemon\n");
    if (MI_SYS_Init() != 0) {
        printf("[FATAL] Failed to initialize SigmaStar MI_SYS memory pool.\n");
        exit(EXIT_FAILURE);
    }

    // 4. STRICT SEQUENTIAL HARDWARE BOOT
    int aoDev, aoChn, aiDev, aiChn;

    if (!disable_ao) {
        // Bring up the Speaker (Creates the AEC reference channel)
        get_audio_output_device_attributes(&aoDev, &aoChn);
        initialize_audio_output_device(aoDev, aoChn);
    }

    if (!disable_ai) {
        // Bring up the Microphone
        get_audio_input_device_attributes(&aiDev, &aiChn);
        initialize_audio_input_device(aiDev, aiChn);
    }

    // 5. SPAWN NETWORK & PLAYBACK THREADS
    pthread_t control_server_thread, input_server_thread, output_server_thread, play_thread_id;
    int exit_code = 0;

    // Tracking flags to prevent joining uninitialized threads
    int control_up = 0, input_up = 0, output_up = 0, play_up = 0;

    // --- BOOT DEADLOCK FIX: Signal threads to abort if a sibling fails to spawn ---
    #define ABORT_STARTUP() do { \
        pthread_mutex_lock(&g_stop_thread_mutex); \
        g_stop_thread = 1; \
        pthread_mutex_unlock(&g_stop_thread_mutex); \
        exit_code = 1; \
        goto join_threads; \
    } while(0)

    if (create_thread(&control_server_thread, audio_control_server_thread, NULL) == 0) {
        control_up = 1;
    } else {
        ABORT_STARTUP();
    }

    if (!disable_ai) {
        if (create_thread(&input_server_thread, audio_input_server_thread, NULL) == 0) {
            input_up = 1;
        } else {
            ABORT_STARTUP();
        }
    }

    if (!disable_ao) {
        if (create_thread(&output_server_thread, audio_output_server_thread, NULL) == 0) {
            output_up = 1;
        } else {
            ABORT_STARTUP();
        }
        if (create_thread(&play_thread_id, ao_play_thread, NULL) == 0) {
            play_up = 1;
        } else {
            ABORT_STARTUP();
        }
    }

join_threads:
    // 6. WAIT FOR COMPLETION
    if (control_up) pthread_join(control_server_thread, NULL);
    if (input_up) pthread_join(input_server_thread, NULL);
    if (output_up) pthread_join(output_server_thread, NULL);
    if (play_up) pthread_join(play_thread_id, NULL);
    
cleanup:
    // Execute global teardown sequence
    perform_cleanup();
    printf("[INFO] Audio daemon exited safely.\n");
    return exit_code;
}
