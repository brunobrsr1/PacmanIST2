#include "api.h"
#include "protocol.h"
#include "display.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <unistd.h> // usleep

Board board = {0};  // Inicializar estrutura com zeros (data será NULL, width/height 0)
bool stop_execution = false;
int tempo = 500;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void* receiver_thread(void* arg) {
    (void)arg;

    while (true) {
          Board new_board = receive_board_update();

          pthread_mutex_lock(&mutex);

        // Sem atualização (pipe não tinha dados agora) -> continuar a aguardar
        if (!new_board.data && new_board.game_over == 0) {
            pthread_mutex_unlock(&mutex);
            sleep_ms(10); // evitar ciclo apertado (~10 ms)
            continue;
        }

        // Fim de jogo (pipe fechado ou game_over explícito)
        if (new_board.game_over == 1) {
            stop_execution = true;
            pthread_mutex_unlock(&mutex);
            if (board.data && board.data != new_board.data) {
                free(board.data);
                board.data = NULL;
            }
            break;
        }

        // Atualizar variáveis globais
        if (board.data && board.data != new_board.data) {
            free(board.data);
        }
        board = new_board;
        tempo = board.tempo;

        // Desenhar tabuleiro
        draw_board_client(board);
        refresh_screen();

        pthread_mutex_unlock(&mutex);
    }

    debug("Receiver thread ending...\n");
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 3 && argc != 4) {
        fprintf(stderr,
            "Usage: %s <client_id> <register_pipe> [commands_file]\n",
            argv[0]);
        return 1;
    }

    const char* client_id = argv[1];
    const char* register_pipe = argv[2];
    const char* commands_file = (argc == 4) ? argv[3] : NULL;

    FILE* cmd_fp = NULL;
    if (commands_file) {
        cmd_fp = fopen(commands_file, "r");
        if (!cmd_fp) {
            perror("Failed to open commands file");
            return 1;
        }
    }

    char req_pipe_path[MAX_PIPE_PATH_LENGTH];
    char notif_pipe_path[MAX_PIPE_PATH_LENGTH];

    snprintf(req_pipe_path, MAX_PIPE_PATH_LENGTH,
             "/tmp/%s_request", client_id);

    snprintf(notif_pipe_path, MAX_PIPE_PATH_LENGTH,
             "/tmp/%s_notification", client_id);

    open_debug_file("client-debug.log");

    debug("Connecting to server...\n");

    if (pacman_connect(req_pipe_path, notif_pipe_path, register_pipe) != 0) {
        debug("Failed to connect to server\n");
        if (cmd_fp) fclose(cmd_fp);
        return 1;
    }

    debug("Connected successfully\n");

    // Criar thread para receber atualizações
    pthread_t receiver_thread_id;
    pthread_create(&receiver_thread_id, NULL, receiver_thread, NULL);

    // Inicializar terminal ncurses
    terminal_init();
    set_timeout(500);
    
    // Desenhar apenas se já temos dados (será desenhado pela receiver_thread)
    // draw_board_client(board);  // Removido - board ainda está vazio
    refresh_screen();

    char command;
    int ch;

    while (1) {
        // Verificar se deve parar
        pthread_mutex_lock(&mutex);
        if (stop_execution) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        pthread_mutex_unlock(&mutex);

        if (cmd_fp) {
            // Input from file
            ch = fgetc(cmd_fp);

            if (ch == EOF) {
                // Reiniciar ficheiro
                rewind(cmd_fp);
                continue;
            }

            command = (char)ch;

            if (command == '\n' || command == '\r' || command == '\0')
                continue;

            command = toupper(command);

            // Esperar pelo tempo do jogo
            pthread_mutex_lock(&mutex);
            int wait_for = tempo;
            pthread_mutex_unlock(&mutex);

            sleep_ms(wait_for);

        }
        else {
            // Interactive input
            command = get_input();
            command = toupper(command);
        }

        if (command == '\0')
            continue;

        if (command == 'Q') {
            debug("Client pressed 'Q', quitting game\n");
            break;
        }

        debug("Sending command: %c\n", command);

        pacman_play(command);
    }

    debug("Disconnecting...\n");
    pacman_disconnect();

    // Aguardar thread de receção
    pthread_join(receiver_thread_id, NULL);

    if (cmd_fp)
        fclose(cmd_fp);

    pthread_mutex_destroy(&mutex);

    terminal_cleanup();

    close_debug_file();

    debug("Client exiting\n");
    return 0;
}

