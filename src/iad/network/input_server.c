#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/un.h>
#include <unistd.h>

#include "input.h"
#include "logging.h"
#include "utils.h"
#include "network.h"
#include "input_server.h"
#include "audio_common.h"

#define TAG "NET_INPUT"

extern volatile int g_stop_thread;
extern pthread_mutex_t g_stop_thread_mutex;

// --- INITIALIZE THE DEDICATED LOCK ---
pthread_mutex_t client_list_lock = PTHREAD_MUTEX_INITIALIZER;
ClientNode *client_list_head = NULL;

void handle_audio_input_client(int client_sock) {
    // --- SIGMASTAR FIX: USE ISOLATED MUTEX ---
    pthread_mutex_lock(&client_list_lock);
    
    ClientNode *new_client = (ClientNode *)malloc(sizeof(ClientNode));
    if (!new_client) {
        handle_audio_error(TAG, "malloc");
        close(client_sock);
        pthread_mutex_unlock(&client_list_lock);
        return;
    }
    
    new_client->sockfd = client_sock;
    new_client->next = client_list_head;
    client_list_head = new_client;
    
    pthread_mutex_unlock(&client_list_lock);
    printf("[INFO] [AI] Input client connected\n");
}

void *audio_input_server_thread(void *arg) {
    printf("[INFO] [AI] Entering audio_input_server_thread\n");

    int aiDevID, aiChnID;
    get_audio_input_device_attributes(&aiDevID, &aiChnID);

    update_socket_paths_from_config();

    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        handle_audio_error(TAG, "socket");
        return NULL;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    
    // --- NAMESPACE FIX: Use standard filesystem sockets, NOT abstract sockets ---
    strncpy(addr.sun_path, AUDIO_INPUT_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    printf("[INFO] [AI] Attempting to bind socket\n");
    
    // Remove orphaned socket files from previous crashes
    unlink(addr.sun_path); 

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        handle_audio_error(TAG, "bind failed");
        close(sockfd);
        return NULL;
    } else {
        printf("[INFO] [AI] Bind to input socket succeeded\n");
    }

    printf("[INFO] [AI] Attempting to listen on socket\n");
    if (listen(sockfd, 5) == -1) {
        handle_audio_error(TAG, "listen");
        close(sockfd);
        return NULL;
    } else {
        printf("[INFO] [AI] Listening on input socket\n");
    }

    // --- ZOMBIE THREAD FIX: Spawn hardware ONLY after socket is ready ---
    pthread_t hw_ai_thread;
    if (pthread_create(&hw_ai_thread, NULL, ai_record_thread, NULL) != 0) {
        fprintf(stderr, "[FATAL] Failed to spawn SigmaStar AI hardware thread\n");
        
        // --- DAEMON DEADLOCK FIX: Trigger global shutdown ---
        pthread_mutex_lock(&g_stop_thread_mutex);
        g_stop_thread = 1;
        pthread_mutex_unlock(&g_stop_thread_mutex);
        
        close(sockfd);
        return NULL;
    }
    // pthread_detach(hw_ai_thread);

    while (1) {
        int should_stop = 0;
        pthread_mutex_lock(&g_stop_thread_mutex);
        should_stop = g_stop_thread;
        pthread_mutex_unlock(&g_stop_thread_mutex);

        if (should_stop) break;

        printf("[INFO] [AI] Waiting for input client connection\n");
        // --- DEADLOCK FIX: Use select() to allow periodic g_stop_thread checks ---
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        tv.tv_sec = 1; // 1-second timeout
        tv.tv_usec = 0;

        int ret = select(sockfd + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue; // Ignore harmless OS interrupts
            handle_audio_error(TAG, "select");
            break;
        } else if (ret == 0) {
            // Timeout occurred. Loop around to evaluate g_stop_thread.
            continue;
        }
        
        int client_sock = accept(sockfd, NULL, NULL);
        if (client_sock == -1) {
            handle_audio_error(TAG, "accept");
            usleep(10000); // 10ms backoff to prevent tight CPU spinning
            continue;
        }

        handle_audio_input_client(client_sock);
    }

    // --- KERNEL PANIC FIX: Synchronize hardware shutdown ---
    pthread_join(hw_ai_thread, NULL);

    // --- POSIX HYGIENE FIX: Cleanly sever all active network clients ---
    pthread_mutex_lock(&client_list_lock);
    ClientNode *current = client_list_head;
    while (current != NULL) {
        ClientNode *next = current->next;
        close(current->sockfd); // Force EOF to the connected client
        free(current);          // Free the heap memory
        current = next;
    }
    client_list_head = NULL;
    pthread_mutex_unlock(&client_list_lock);
    
    close(sockfd);
    unlink(AUDIO_INPUT_SOCKET_PATH); // --- HYGIENE FIX: Remove the socket file ---
    return NULL;
}
