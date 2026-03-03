#include <string.h>
#include <sys/socket.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/un.h>
#include <unistd.h>
#include "audio_common.h"
#include "logging.h"
#include "utils.h"
#include "network.h"
#include "output.h"
#include "output_server.h"

#define TAG "NET_OUTPUT"

void *audio_output_server_thread(void *arg) {
    printf("[INFO] [AO] Entering audio_output_server_thread\n");

    update_socket_paths_from_config();

    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        handle_audio_error(TAG, "socket");
        return NULL;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(&addr.sun_path[1], AUDIO_OUTPUT_SOCKET_PATH, sizeof(addr.sun_path) - 2);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

    printf("[INFO] [AO] Attempting to bind socket\n");
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(sa_family_t) + strlen(AUDIO_OUTPUT_SOCKET_PATH) + 1) == -1) {
        handle_audio_error(TAG, "bind failed");
        close(sockfd);
        return NULL;
    }
    else {
        printf("[INFO] [AO] Bind to output socket succeeded\n");
    }

    printf("[INFO] [AO] Attempting to listen on socket\n");
    if (listen(sockfd, 5) == -1) {
        handle_audio_error(TAG, "listen");
        close(sockfd);
        return NULL;
    }
    else {
        printf("[INFO] [AO] Listening on output socket\n");
    }

    while (1) {
        int should_stop = 0;
        pthread_mutex_lock(&g_stop_thread_mutex);
        should_stop = g_stop_thread;
        pthread_mutex_unlock(&g_stop_thread_mutex);

        if (should_stop) {
            break;
        }

printf("[INFO] [AO] Waiting for output client connection\n");
        // --- DEADLOCK FIX: Use select() to allow periodic g_stop_thread checks ---
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        tv.tv_sec = 1; // 1-second timeout
        tv.tv_usec = 0;

        int ret = select(sockfd + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            handle_audio_error(TAG, "select");
            break;
        } else if (ret == 0) {
            // Timeout occurred. Loop around to evaluate g_stop_thread.
            continue;
        }
		
		int client_sock = accept(sockfd, NULL, NULL);
        if (client_sock == -1) {
            handle_audio_error(TAG, "accept");
            continue;
        }

		// --- NETWORK DEADLOCK FIX: Set a 1-second receive timeout ---
        struct timeval tv_rcv;
        tv_rcv.tv_sec = 1;
        tv_rcv.tv_usec = 0;
        setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_rcv, sizeof(tv_rcv));

        pthread_mutex_lock(&audio_buffer_lock);
        while (active_client_sock != -1) {
            pthread_cond_wait(&audio_data_cond, &audio_buffer_lock);
        }
        active_client_sock = client_sock;

        // --- INGENIC DIRTY HACK REMOVED ---
        /*
        // Enabling the channel, after its already enabled, clears all buffers for some reason... otherwise
        // old audio will play on each subsequent client connect... unknown why.
        enable_output_channel(); //There has to be a better way than to do this.
        */

        // --- SIGMASTAR CLEAN FLUSH ADDED ---
        // Mathematically clear the DMA ring buffer before the new client sends data
        clear_audio_output_buffer();

        printf("[INFO] [AO] Client connected\n");
        memset(audio_buffer, 0, g_ao_max_frame_size);
        audio_buffer_size = 0;
        pthread_mutex_unlock(&audio_buffer_lock);
		
		unsigned char buf[g_ao_max_frame_size];
        ssize_t read_size;

        printf("[INFO] [AO] Receiving audio data from client\n");
		
		// --- MSG_WAITALL TIMEOUT HANDLING ---
        while (1) {
            read_size = recv(client_sock, buf, sizeof(buf), MSG_WAITALL);
            
            if (read_size < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Timeout hit. Check if we need to shut down.
                    int stop = 0;
                    pthread_mutex_lock(&g_stop_thread_mutex);
                    stop = g_stop_thread;
                    pthread_mutex_unlock(&g_stop_thread_mutex);
                    if (stop) break;
                    
                    continue; // No shutdown requested, keep waiting for data
                } else {
                    handle_audio_error(TAG, "recv error");
                    break;
                }
            } else if (read_size == 0) {
                // Client gracefully disconnected
                break;
            } else if (read_size != sizeof(buf)) {
                printf("[WARN] [AO] Partial frame received (%zd bytes). Discarding to protect DMA.\n", read_size);
                break;
            }

            pthread_mutex_lock(&audio_buffer_lock);
            memcpy(audio_buffer, buf, read_size);
            audio_buffer_size = read_size;
            pthread_cond_signal(&audio_data_cond);
            pthread_mutex_unlock(&audio_buffer_lock);
        }

        // Clear audio buffer after reading ends
        memset(audio_buffer, 0, g_ao_max_frame_size);
        audio_buffer_size = 0;

        pthread_mutex_lock(&audio_buffer_lock);
        active_client_sock = -1;
        pthread_cond_broadcast(&audio_data_cond);
        pthread_mutex_unlock(&audio_buffer_lock);

        close(client_sock);
        printf("[INFO] [AO] Client Disconnected\n");

        // Causes small files to cut off too soon, disable for now.
	//clear_audio_output_buffer();
    }

    close(sockfd);
    return NULL;
}
