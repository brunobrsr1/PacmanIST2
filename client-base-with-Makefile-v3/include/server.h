#ifndef SERVER_H
#define SERVER_H

#include "board.h"
#include <semaphore.h>

#define MAX_PENDING_CONNECTIONS 10

// Pedido de conexão (formato exato do protocolo)
typedef struct {
    char req_pipe_path[40];
    char notif_pipe_path[40];
    int client_id;
} connection_request_t;

// Buffer produtor-consumidor (COM SEMÁFOROS - exigência do enunciado)
typedef struct {
    connection_request_t *buffer;
    int size;
    int in;
    int out;
    pthread_mutex_t mutex;
    sem_t empty;
    sem_t full;
} request_buffer_t;

// Sessão do cliente (APENAS informações de conexão e pontuação)
typedef struct {
    int client_id;
    int req_fd;
    int notif_fd;
    int active;
    int points;           // Pontuação atual do cliente (para top5)
    pthread_t game_thread;
} client_session_t;

// Funções do buffer (exigidas)
void buffer_init(request_buffer_t *buf, int size);
void buffer_destroy(request_buffer_t *buf);
void buffer_put(request_buffer_t *buf, connection_request_t req);
connection_request_t buffer_get(request_buffer_t *buf);

#endif

