#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "utils.h"
#include "config.h"
#include "output.h"
#include "input.h"
#include "mi_sys.h"

#define PID_FILE "/var/run/iad.pid"

ClientNode *client_list_head = NULL;
pthread_mutex_t audio_buffer_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t audio_data_cond = PTHREAD_COND_INITIALIZER;
unsigned char *audio_buffer = NULL;
ssize_t audio_buffer_size = 0;
int active_client_sock = -1;

volatile int g_stop_thread = 0;
pthread_mutex_t g_stop_thread_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Create a new thread.
 *
 * This function creates a new thread and starts it.
 *
 * @param thread_id Pointer to the thread identifier.
 * @param start_routine Pointer to the function to be executed by the thread.
 * @param arg Arguments to be passed to the start_routine.
 * @return int Returns 0 on success, error code on failure.
 */
int create_thread(pthread_t *thread_id, void *(*start_routine)(void *), void *arg) {
    int ret = pthread_create(thread_id, NULL, start_routine, arg);
    if (ret) {
        fprintf(stderr, "[ERROR] pthread_create for thread failed with error code: %d\n", ret);
    }
    return ret;
}

/**
 * @brief Compute number of samples per frame.
 *
 * This function computes the number of samples per frame based on the sample rate.
 *
 * @param sample_rate The sample rate in Hz.
 * @return int Number of samples per frame.
 */
int compute_numPerFrm(int sample_rate) {
    return sample_rate * FRAME_DURATION;
}

/**
 * @brief Clean up resources.
 *
 * This function cleans up allocated resources and restores the system to its initial state.
 */
void perform_cleanup() {

    disable_audio_input();
    disable_audio_output();

    config_cleanup();

    pthread_mutex_destroy(&audio_buffer_lock);
    pthread_cond_destroy(&audio_data_cond);
    pthread_mutex_destroy(&g_stop_thread_mutex);
}

/**
 * @brief Remove PID file.
 *
 * This function removes the PID file when called, typically during cleanup.
 */
void remove_pid_file() {
    unlink(PID_FILE);
}

/**
 * @brief Signal handler for SIGINT.
 *
 * This function handles the SIGINT signal (typically sent from the
 * command line via CTRL+C). It ensures that the daemon exits gracefully.
 *
 * @param sig Signal number (expected to be SIGINT).
 */
void handle_sigint(int sig) {
    // POSIX strictly forbids complex functions here. 
    // Set the flag and let the threads gracefully exit.
    g_stop_thread = 1;
}

/**
 * @brief Set up signal handling.
 *
 * This function sets up signal handlers for various signals the program might receive.
 */
void setup_signal_handling() {
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN); // Ignore broken pipes from disconnected sockets
}

/**
 * @brief Daemonize the process.
 *
 * This function forks the current process to create a daemon. The parent process exits, and the child process continues.
 * The child process becomes the session leader, and it detaches from the controlling terminal.
 */
void daemonize() {
    if (is_already_running()) {
        exit(1);
    }

    printf("Starting the program in the background as a daemon
