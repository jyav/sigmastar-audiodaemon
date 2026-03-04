#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <errno.h>
#include "output_server.h"
#include "output.h"
#include "config.h"
#include "logging.h"

// External condition variable defined in output.c for hardware pacing
extern pthread_cond_t audio_free_cond;

static int active_client_sock = -1;

void *audio_output_server_thread(void *arg) {
    int server_sock, client_sock;
    struct sockaddr_un addr;
    unsigned char buf[DEFAULT_AO_MAX_FRAME_SIZE];

    server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sock < 0) {
        handle_audio_error("AO Server: socket error");
        return NULL;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/audio_output.sock", sizeof(addr.sun_path) - 1);
    unlink(addr.sun_path);

    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        handle_audio_error("AO Server: bind error");
        close(server_sock);
        return NULL;
    }

    if (listen(server_sock, 5) < 0) {
        handle_audio_error("AO Server: listen error");
        close(server_sock);
        return NULL;
    }

while (!g_stop_thread) {
        client_sock = accept(server_sock, NULL, NULL);
        if (client_sock < 0) {
            if (g_stop_thread) break;
            continue;
        }

        active_client_sock = client_sock;
        printf("[INFO] [AO] Client Connected\n");

        while (!g_stop_thread) {
            ssize_t read_size = recv(client_sock, buf, sizeof(buf), 0);
            if (read_size > 0) {
                pthread_mutex_lock(&audio_buffer_lock);

                // --- THE BACKPRESSURE FIX ---
                // Wait here if the hardware hasn't finished the previous frame.
                // This forces Baresip to wait, pacing the stream to 16kHz.
                while (audio_buffer_size > 0 && !g_stop_thread) {
                    pthread_cond_wait(&audio_free_cond, &audio_buffer_lock);
                }

                if (g_stop_thread) {
                    pthread_mutex_unlock(&audio_buffer_lock);
                    break;
                }

                memcpy(audio_buffer, buf, read_size);
                audio_buffer_size = read_size;
                
                pthread_cond_signal(&audio_data_cond);
                pthread_mutex_unlock(&audio_buffer_lock);
            } else {
                break; // Connection closed or error
            }
        }

        // --- DISCONNECT CLEANUP ---
        pthread_mutex_lock(&audio_buffer_lock);
        memset(audio_buffer, 0, g_ao_max_frame_size);
        audio_buffer_size = 0;
        active_client_sock = -1;
        // Wake the playback thread so it can go back to sleep gracefully
        pthread_cond_broadcast(&audio_data_cond);
        pthread_cond_broadcast(&audio_free_cond);
        pthread_mutex_unlock(&audio_buffer_lock);

        close(client_sock);
        printf("[INFO] [AO] Client Disconnected\n");
    }

    close(server_sock);
    unlink("/tmp/audio_output.sock");
    return NULL;
}
printf("[INFO] [AO] Server listening on /tmp/audio_output.sock\n");
