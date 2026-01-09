#include "server.h"
#include "board.h"
#include "parser.h"
#include "display.h"
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <sys/select.h>

// ==================== VARIÁVEIS GLOBAIS ====================
static request_buffer_t connection_buffer;
static client_session_t *sessions = NULL;
static int max_sessions = 0;
static pthread_mutex_t sessions_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t sigusr1_received = 0;
static char* levels_dir = NULL;
static char register_pipe_name[100];

// ==================== BUFFER PRODUTOR-CONSUMIDOR ====================

void buffer_init(request_buffer_t* buf, int size) {
    buf->buffer = malloc(size * sizeof(connection_request_t));
    buf->size = size;
    buf->in = 0;
    buf->out = 0;
    pthread_mutex_init(&buf->mutex, NULL);
    sem_init(&buf->empty, 0, size);
    sem_init(&buf->full, 0, 0);
}

void buffer_destroy(request_buffer_t* buf) {
    free(buf->buffer);
    pthread_mutex_destroy(&buf->mutex);
    sem_destroy(&buf->empty);
    sem_destroy(&buf->full);
}

void buffer_put(request_buffer_t* buf, connection_request_t req) {
    sem_wait(&buf->empty);
    pthread_mutex_lock(&buf->mutex);
    buf->buffer[buf->in] = req;
    buf->in = (buf->in + 1) % buf->size;
    pthread_mutex_unlock(&buf->mutex);
    sem_post(&buf->full);
}

connection_request_t buffer_get(request_buffer_t* buf) {
    sem_wait(&buf->full);
    pthread_mutex_lock(&buf->mutex);
    connection_request_t req = buf->buffer[buf->out];
    buf->out = (buf->out + 1) % buf->size;
    pthread_mutex_unlock(&buf->mutex);
    sem_post(&buf->empty);
    return req;
}

// ==================== UTILITÁRIOS ====================

static int extract_client_id(const char* pipe_path) {
    const char* prefix = "/tmp/";
    const char* start = strstr(pipe_path, prefix);
    if (!start) return -1;
    
    start += strlen(prefix);
    char id_str[20];
    int i = 0;
    while (start[i] >= '0' && start[i] <= '9' && i < 19) {
        id_str[i] = start[i];
        i++;
    }
    id_str[i] = '\0';
    
    return atoi(id_str);
}

static void send_board_update(int notif_fd, board_t* board, int points, int game_over, int victory) {
    char op_code = OP_CODE_BOARD;
    int width = board->width;
    int height = board->height;
    int tempo = board->tempo;
    
    write(notif_fd, &op_code, 1);
    write(notif_fd, &width, sizeof(int));
    write(notif_fd, &height, sizeof(int));
    write(notif_fd, &tempo, sizeof(int));
    write(notif_fd, &victory, sizeof(int));
    write(notif_fd, &game_over, sizeof(int));
    write(notif_fd, &points, sizeof(int));
    
    char* board_data = get_board_displayed(board);
    write(notif_fd, board_data, width * height);
    free(board_data);
}

// ==================== SIGNAL HANDLER (EXERCÍCIO 2) ====================

static void sigusr1_handler(int sig) {
    (void)sig;
    sigusr1_received = 1;
}

// Estrutura para armazenar clientes ordenados por pontuação
typedef struct {
    int client_id;
    int points;
} client_score_t;

static void generate_top5_log() {
    FILE* log = fopen("top5_clients.log", "w");
    if (!log) return;
    
    fprintf(log, "=== TOP 5 CLIENTES POR PONTUAÇÃO ===\n");
    
    // Coletar pontuações de todas as sessões ativas
    client_score_t scores[100];
    int count = 0;
    
    pthread_mutex_lock(&sessions_mutex);
    if (sessions != NULL) {
        for (int i = 0; i < max_sessions; i++) {
            if (sessions[i].active) {
                scores[count].client_id = sessions[i].client_id;
                scores[count].points = sessions[i].points;
                count++;
                if (count >= 100) break;
            }
        }
    }
    pthread_mutex_unlock(&sessions_mutex);
    
    // Ordenar por pontuação (decrescente)
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (scores[j].points > scores[i].points) {
                client_score_t temp = scores[i];
                scores[i] = scores[j];
                scores[j] = temp;
            }
        }
    }
    
    // Escrever top 5
    int top = count < 5 ? count : 5;
    for (int i = 0; i < top; i++) {
        fprintf(log, "%d. Cliente ID: %d - Pontuação: %d\n", 
                i + 1, scores[i].client_id, scores[i].points);
    }
    
    if (count == 0) {
        fprintf(log, "Nenhum cliente ativo.\n");
    }
    
    fclose(log);
}

// Estruturas para o jog

typedef struct {
    int shutdown;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} game_control_t;

typedef struct {
    board_t* board;
    int req_fd;
    int notif_fd;
    game_control_t* control;
    int* session_idx;
    int ghost_index;
    int had_dots;
} game_thread_data_t;

// Thread do pacman

static void* pacman_server_thread(void* arg) {
    game_thread_data_t* data = (game_thread_data_t*)arg;
    board_t* board = data->board;
    int req_fd = data->req_fd;
    game_control_t* control = data->control;
    
    pacman_t* pacman = &board->pacmans[0];
    
    while (1) {
        pthread_mutex_lock(&control->mutex);
        if (control->shutdown) {
            pthread_mutex_unlock(&control->mutex);
            break;
        }
        pthread_mutex_unlock(&control->mutex);
        
        if (!pacman->alive) break;
        
        sleep_ms(board->tempo * (1 + pacman->passo));
        
        char op_code;
        fd_set readfds;
        struct timeval tv = {0, 10000};
        
        FD_ZERO(&readfds);
        FD_SET(req_fd, &readfds);
        
        int ready = select(req_fd + 1, &readfds, NULL, NULL, &tv);
        if (ready > 0) {
            ssize_t bytes = read(req_fd, &op_code, 1);
            if (bytes > 0) {
                if (op_code == OP_CODE_DISCONNECT) {
                    break;
                }
                else if (op_code == OP_CODE_PLAY) {
                    char command;
                    read(req_fd, &command, 1);
                    
                    // Ignorar comando 'G' conforme enunciado
                    if (command == 'G') {
                        continue;
                    }
                    
                    command_t cmd;
                    cmd.command = command;
                    cmd.turns = 1;
                    cmd.turns_left = 1;
                    
                    pthread_rwlock_rdlock(&board->state_lock);
                    int result = move_pacman(board, 0, &cmd);
                    
                    if (result == REACHED_PORTAL) {
                        pthread_rwlock_unlock(&board->state_lock);
                        pthread_mutex_lock(&control->mutex);
                        control->shutdown = 2;
                        pthread_mutex_unlock(&control->mutex);
                        break;
                    }
                    
                    if (result == DEAD_PACMAN) {
                        pthread_rwlock_unlock(&board->state_lock);
                        break;
                    }
                    
                    pthread_rwlock_unlock(&board->state_lock);
                }
            }
            else if (bytes == 0) {
                break;
            }
        }
    }
    
    return NULL;
}

// Thread dos fantasmas

static void* ghost_server_thread(void* arg) {
    game_thread_data_t* data = (game_thread_data_t*)arg;
    board_t* board = data->board;
    int ghost_index = data->ghost_index;
    game_control_t* control = data->control;
    
    if (ghost_index < 0 || ghost_index >= board->n_ghosts) {
        return NULL;
    }
    
    ghost_t* ghost = &board->ghosts[ghost_index];
    
    while (1) {
        pthread_mutex_lock(&control->mutex);
        if (control->shutdown) {
            pthread_mutex_unlock(&control->mutex);
            break;
        }
        pthread_mutex_unlock(&control->mutex);
        
        sleep_ms(board->tempo * (1 + ghost->passo));

        pthread_rwlock_rdlock(&board->state_lock);

        // Proteger contra n_moves == 0 (evita divisão por zero)
        if (ghost->n_moves <= 0) {
            pthread_rwlock_unlock(&board->state_lock);
            continue;
        }

        command_t cmd;
        cmd.command = ghost->moves[ghost->current_move % ghost->n_moves].command;
        cmd.turns = 1;
        cmd.turns_left = 1;

        move_ghost(board, ghost_index, &cmd);
        pthread_rwlock_unlock(&board->state_lock);
    }
    
    return NULL;
}

// Thread de notificação

static void* notification_thread(void* arg) {
    game_thread_data_t* data = (game_thread_data_t*)arg;
    board_t* board = data->board;
    int notif_fd = data->notif_fd;
    game_control_t* control = data->control;
    int* session_idx = data->session_idx;
    
        // Enviar board inicial IMEDIATAMENTE
        pthread_rwlock_rdlock(&board->state_lock);
        send_board_update(notif_fd, board, board->pacmans[0].points, 0, 0);
        pthread_rwlock_unlock(&board->state_lock);
    
    while (1) {
        pthread_mutex_lock(&control->mutex);
        if (control->shutdown) {
            pthread_mutex_unlock(&control->mutex);
            break;
        }
        pthread_mutex_unlock(&control->mutex);
        
        sleep_ms(board->tempo);
        
        pthread_rwlock_rdlock(&board->state_lock);
        
        int points = board->pacmans[0].points;
        int game_over = !board->pacmans[0].alive;
        int victory = 0;
        
        if (session_idx && *session_idx >= 0) {
            pthread_mutex_lock(&sessions_mutex);
            sessions[*session_idx].points = points;
            pthread_mutex_unlock(&sessions_mutex);
        }
        
        int dots_remaining = 0;
        for (int i = 0; i < board->width * board->height; i++) {
            if (board->board[i].has_dot) {
                dots_remaining = 1;
                break;
            }
        }

        for (int i = 0; i < board->width * board->height; i++) {
            if (board->board[i].has_portal && board->board[i].content == 'P') {
                victory = 1;
                break;
            }
        }

        // Só considerar vitória por "sem pontos" se este nível chegou a ter dots
        if (data->had_dots && !dots_remaining) {
            victory = 1;
        }

        send_board_update(notif_fd, board, points, game_over, victory);
        
        if (game_over || victory) {
            pthread_mutex_lock(&control->mutex);
            if (victory) control->shutdown = 2;
            else control->shutdown = 1;
            pthread_mutex_unlock(&control->mutex);
        }
        
        pthread_rwlock_unlock(&board->state_lock);
    }
    
    return NULL;
}

// Thread principal do JOGO

typedef struct {
    int client_id;
    int req_fd;
    int notif_fd;
    char* levels_dir;
} client_game_args_t;

static void* client_game_thread(void* arg) {
    client_game_args_t* args = (client_game_args_t*)arg;
    int client_id = args->client_id;
    int req_fd = args->req_fd;
    int notif_fd = args->notif_fd;
    char* levels_dir = args->levels_dir;
    free(args);
    
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    
    int session_idx = -1;
    pthread_mutex_lock(&sessions_mutex);
    for (int i = 0; i < max_sessions; i++) {
        if (sessions[i].active && sessions[i].client_id == client_id) {
            session_idx = i;
            break;
        }
    }
    pthread_mutex_unlock(&sessions_mutex);
    
    if (session_idx == -1) {
        close(req_fd);
        close(notif_fd);
        return NULL;
    }
    
    DIR* level_dir = opendir(levels_dir);
    if (!level_dir) {
        close(req_fd);
        close(notif_fd);
        return NULL;
    }
    
    char* level_files[100];
    int num_levels = 0;
    struct dirent* entry;
    
    while ((entry = readdir(level_dir)) != NULL && num_levels < 100) {
        if (entry->d_name[0] == '.') continue;
        
        char* dot = strrchr(entry->d_name, '.');
        if (dot && strcmp(dot, ".lvl") == 0) {
            level_files[num_levels] = strdup(entry->d_name);
            num_levels++;
        }
    }
    closedir(level_dir);
    
    if (num_levels == 0) {
        close(req_fd);
        close(notif_fd);
        return NULL;
    }
    
    int current_level = 0;
    while (current_level < num_levels) {
        board_t game_board;
        memset(&game_board, 0, sizeof(board_t));
        
        int accumulated_points = 0;
        if (current_level > 0) {
            pthread_mutex_lock(&sessions_mutex);
            accumulated_points = sessions[session_idx].points;
            pthread_mutex_unlock(&sessions_mutex);
        }
        
        if (load_level(&game_board, level_files[current_level], levels_dir, accumulated_points) < 0) {
            fprintf(stderr, "ERRO: load_level falhou para %s\n", level_files[current_level]);
            free(level_files[current_level]);
            current_level++;
            continue;
        }
        
        int dots_count = 0;
        for (int i = 0; i < game_board.width * game_board.height; i++) {
            if (game_board.board[i].has_dot) dots_count++;
        }

        fprintf(stderr,
            "DEBUG: Nível carregado: %s, width=%d, height=%d, tempo=%d, n_pacmans=%d\n",
            level_files[current_level], game_board.width, game_board.height,
            game_board.tempo, game_board.n_pacmans);
        
        free(level_files[current_level]);
        
        game_control_t control;
        control.shutdown = 0;
        pthread_mutex_init(&control.mutex, NULL);
        pthread_cond_init(&control.cond, NULL);
        
        game_thread_data_t shared_data;
        shared_data.board = &game_board;
        shared_data.req_fd = req_fd;
        shared_data.notif_fd = notif_fd;
        shared_data.control = &control;
        shared_data.session_idx = &session_idx;
        shared_data.ghost_index = -1;
        shared_data.had_dots = (dots_count > 0);
        
        pthread_t pacman_tid, notif_tid;
        pthread_t* ghost_tids = malloc(game_board.n_ghosts * sizeof(pthread_t));
        game_thread_data_t* ghost_datas = malloc(game_board.n_ghosts * sizeof(game_thread_data_t));
        
        pthread_create(&pacman_tid, NULL, pacman_server_thread, &shared_data);
        
        for (int i = 0; i < game_board.n_ghosts; i++) {
            memcpy(&ghost_datas[i], &shared_data, sizeof(game_thread_data_t));
            ghost_datas[i].ghost_index = i;
            pthread_create(&ghost_tids[i], NULL, ghost_server_thread, &ghost_datas[i]);
        }
        
        pthread_create(&notif_tid, NULL, notification_thread, &shared_data);
        
        pthread_join(pacman_tid, NULL);
        
        pthread_mutex_lock(&control.mutex);
        if (control.shutdown == 0) control.shutdown = 1;
        pthread_mutex_unlock(&control.mutex);
        
        pthread_join(notif_tid, NULL);
        for (int i = 0; i < game_board.n_ghosts; i++) {
            pthread_join(ghost_tids[i], NULL);
        }
        
        free(ghost_tids);
        free(ghost_datas);
        
        pthread_mutex_lock(&control.mutex);
        int next_level = (control.shutdown == 2);
        pthread_mutex_unlock(&control.mutex);
        
        pthread_mutex_destroy(&control.mutex);
        pthread_cond_destroy(&control.cond);
        unload_level(&game_board);
        
        if (!next_level) {
            break;
        }
        
        current_level++;
    }
    
    close(req_fd);
    close(notif_fd);
    
    pthread_mutex_lock(&sessions_mutex);
    sessions[session_idx].active = 0;
    pthread_mutex_unlock(&sessions_mutex);
    
    return NULL;
}

// Thread anfitriã

void* host_thread(void* arg) {
    (void)arg;
    
    fprintf(stderr, "HOST THREAD: Iniciada\n");
    
    // Configurar SIGUSR1 APENAS nesta thread
    struct sigaction sa;
    sa.sa_handler = sigusr1_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);
    
    fprintf(stderr, "HOST THREAD: SIGUSR1 configurado\n");
    
    // Criar pipe de registo
    unlink(register_pipe_name);
    if (mkfifo(register_pipe_name, 0666) == -1) {
        perror("Erro ao criar pipe de registo");
        return NULL;
    }
    
    fprintf(stderr, "HOST THREAD: FIFO criado: %s\n", register_pipe_name);
    
    // Abrir em modo leitura/escrita para não bloquear
    int reg_fd = open(register_pipe_name, O_RDWR);
    if (reg_fd == -1) {
        perror("Erro ao abrir pipe de registo");
        unlink(register_pipe_name);
        return NULL;
    }
    
    fprintf(stderr, "HOST THREAD: FIFO aberto, aguardando clientes...\n");
    
    while (1) {
        // Verificar sigusr1
        if (sigusr1_received) {
            sigusr1_received = 0;
            generate_top5_log();
        }
        
        // Ler pedido de conexão
        char op_code;
        ssize_t bytes = read(reg_fd, &op_code, 1);
        
        if (bytes <= 0) {
            if (bytes == 0) continue;
            break;
        }
        
        if (op_code != OP_CODE_CONNECT) continue;
        
        char req_pipe[40], notif_pipe[40];
        
        if (read(reg_fd, req_pipe, 40) != 40) continue;
        if (read(reg_fd, notif_pipe, 40) != 40) continue;
        
        // Criar pedido de conexão
        connection_request_t req;
        strncpy(req.req_pipe_path, req_pipe, 40);
        strncpy(req.notif_pipe_path, notif_pipe, 40);
        req.client_id = extract_client_id(req_pipe);
        
        // Inserir no buffer (bloqueia se cheio - max_games)
        buffer_put(&connection_buffer, req);
    }
    
    close(reg_fd);
    unlink(register_pipe_name);
    return NULL;
}

// thread worker (Gestora)

void* worker_thread(void* arg) {
    (void)arg;
    
    // Bloquear SIGUSR1 (apenas thread anfitriã recebe)
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    
    while (1) {
        // Retirar pedido do buffer (bloqueia se vazio)
        connection_request_t req = buffer_get(&connection_buffer);

        // Esperar até existir um slot de sessão livre (bloqueia novos pedidos quando max_games está cheio)
        int session_idx = -1;
        while (1) {
            pthread_mutex_lock(&sessions_mutex);
            for (int i = 0; i < max_sessions; i++) {
                if (!sessions[i].active) {
                    session_idx = i;
                    sessions[i].active = 1;      // reservar slot
                    sessions[i].client_id = req.client_id;
                    sessions[i].points = 0;
                    break;
                }
            }
            pthread_mutex_unlock(&sessions_mutex);

            if (session_idx != -1) {
                break; // já temos sessão reservada para este cliente
            }

            // Nenhum slot livre ainda: aguardar um pouco antes de voltar a tentar
            sleep_ms(50);
        }

        // Abrir pipes do cliente apenas depois de garantir uma sessão
        int req_fd = open(req.req_pipe_path, O_RDONLY | O_NONBLOCK);
        int notif_fd = open(req.notif_pipe_path, O_WRONLY);

        if (req_fd == -1 || notif_fd == -1) {
            if (req_fd != -1) close(req_fd);
            if (notif_fd != -1) close(notif_fd);

            // Libertar slot de sessão porque o cliente já não está disponível
            pthread_mutex_lock(&sessions_mutex);
            sessions[session_idx].active = 0;
            sessions[session_idx].client_id = 0;
            sessions[session_idx].points = 0;
            pthread_mutex_unlock(&sessions_mutex);
            continue;
        }

        // Guardar descritores de ficheiro na sessão
        pthread_mutex_lock(&sessions_mutex);
        sessions[session_idx].req_fd = req_fd;
        sessions[session_idx].notif_fd = notif_fd;
        pthread_mutex_unlock(&sessions_mutex);

        // Enviar confirmação (OP_CODE=1, result=0)
        char response[2] = {OP_CODE_CONNECT, 0};
        write(notif_fd, response, 2);
        
        // Criar thread do jogo para este cliente
        // Arquitetura: worker cria thread de jogo para não ficar bloqueada
        client_game_args_t* game_args = malloc(sizeof(client_game_args_t));
        game_args->client_id = req.client_id;
        game_args->req_fd = req_fd;
        game_args->notif_fd = notif_fd;
        game_args->levels_dir = strdup(levels_dir);
        
        pthread_create(&sessions[session_idx].game_thread, NULL,
                      client_game_thread, game_args);
    }
    
    return NULL;
}

// Main do servidor

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s levels_dir max_games nome_do_FIFO_de_registo\n", argv[0]);
        return 1;
    }
    
    levels_dir = argv[1];
    int max_games = atoi(argv[2]);
    strncpy(register_pipe_name, argv[3], sizeof(register_pipe_name) - 1);
    
    if (max_games <= 0) {
        fprintf(stderr, "max_games deve ser maior que 0\n");
        return 1;
    }
    
    srand((unsigned int)time(NULL));
    
    // Inicializar buffer
    buffer_init(&connection_buffer, MAX_PENDING_CONNECTIONS);
    
    // Inicializar sessões
    max_sessions = max_games;
    sessions = calloc(max_sessions, sizeof(client_session_t));
    
    // Criar thread anfitriã
    pthread_t host_tid;
    pthread_create(&host_tid, NULL, host_thread, NULL);
    
    // Criar threads worker (max_games threads)
    pthread_t* worker_tids = malloc(max_games * sizeof(pthread_t));
    for (int i = 0; i < max_games; i++) {
        pthread_create(&worker_tids[i], NULL, worker_thread, NULL);
    }
    
    // Aguardar (nunca retorna em condições normais)
    pthread_join(host_tid, NULL);
    
    for (int i = 0; i < max_games; i++) {
        pthread_join(worker_tids[i], NULL);
    }
    
    // Limpeza (nunca alcançado)
    free(worker_tids);
    free(sessions);
    buffer_destroy(&connection_buffer);
    
    return 0;
}

