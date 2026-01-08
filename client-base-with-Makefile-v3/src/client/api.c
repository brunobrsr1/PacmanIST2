#include "api.h"
#include "protocol.h"
#include "debug.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>

// Estrutura da sessão (como no código base)
struct Session {
    int id;
    int req_pipe;
    int notif_pipe;
    char req_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
    char notif_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
};

static struct Session session = {.id = -1, .req_pipe = -1, .notif_pipe = -1};

// Implementação EXATA conforme protocolo
int pacman_connect(char const *req_pipe_path, char const *notif_pipe_path, char const *server_pipe_path) {
    // Criar pipes (EXIGÊNCIA: cliente cria os pipes)
    if (mkfifo(req_pipe_path, 0666) == -1 && errno != EEXIST) {
        debug("Erro ao criar pipe de pedidos: %s\n", req_pipe_path);
        return 1;
    }
    
    if (mkfifo(notif_pipe_path, 0666) == -1 && errno != EEXIST) {
        debug("Erro ao criar pipe de notificações: %s\n", notif_pipe_path);
        unlink(req_pipe_path);
        return 1;
    }
    
    // Abrir pipe do servidor (EXIGÊNCIA: já deve existir)
    int server_fd = open(server_pipe_path, O_WRONLY);
    if (server_fd == -1) {
        debug("Erro ao abrir pipe do servidor: %s\n", server_pipe_path);
        unlink(req_pipe_path);
        unlink(notif_pipe_path);
        return 1;
    }
    
    // Enviar pedido de conexão (EXIGÊNCIA: formato OP_CODE=1 + 2 pipes)
    char op_code = OP_CODE_CONNECT; // 1
    
    if (write(server_fd, &op_code, 1) != 1 ||
        write(server_fd, req_pipe_path, MAX_PIPE_PATH_LENGTH) != MAX_PIPE_PATH_LENGTH ||
        write(server_fd, notif_pipe_path, MAX_PIPE_PATH_LENGTH) != MAX_PIPE_PATH_LENGTH) {
        debug("Erro ao enviar pedido de conexão\n");
        close(server_fd);
        unlink(req_pipe_path);
        unlink(notif_pipe_path);
        return 1;
    }
    
    close(server_fd);
    
    // Aguardar resposta (EXIGÊNCIA: pelo pipe de notificações)
    int notif_fd = open(notif_pipe_path, O_RDONLY);
    if (notif_fd == -1) {
        debug("Erro ao abrir pipe de notificações para leitura\n");
        unlink(req_pipe_path);
        unlink(notif_pipe_path);
        return 1;
    }
    
    char response[2];
    if (read(notif_fd, response, 2) != 2) {
        debug("Erro ao ler resposta do servidor\n");
        close(notif_fd);
        unlink(req_pipe_path);
        unlink(notif_pipe_path);
        return 1;
    }
    
    close(notif_fd);
    
    // Verificar resposta (EXIGÊNCIA: OP_CODE=1, result=0)
    if (response[0] != OP_CODE_CONNECT || response[1] != 0) {
        debug("Conexão rejeitada pelo servidor\n");
        unlink(req_pipe_path);
        unlink(notif_pipe_path);
        return 1;
    }
    
    // Abrir pipes para comunicação futura
    session.req_pipe = open(req_pipe_path, O_WRONLY);
    if (session.req_pipe == -1) {
        debug("Erro ao abrir pipe de pedidos para escrita\n");
        unlink(req_pipe_path);
        unlink(notif_pipe_path);
        return 1;
    }
    
    session.notif_pipe = open(notif_pipe_path, O_RDONLY | O_NONBLOCK);
    if (session.notif_pipe == -1) {
        debug("Erro ao abrir pipe de notificações para leitura\n");
        close(session.req_pipe);
        unlink(req_pipe_path);
        unlink(notif_pipe_path);
        return 1;
    }
    
    // Guardar paths (EXIGÊNCIA: para desconexão)
    strncpy(session.req_pipe_path, req_pipe_path, MAX_PIPE_PATH_LENGTH);
    strncpy(session.notif_pipe_path, notif_pipe_path, MAX_PIPE_PATH_LENGTH);
    
    debug("Conexão estabelecida com sucesso\n");
    return 0;
}

// Implementação EXATA conforme protocolo
void pacman_play(char command) {
    if (session.req_pipe == -1) {
        debug("Tentativa de jogar sem conexão ativa\n");
        return;
    }
    
    // Enviar comando (EXIGÊNCIA: OP_CODE=3 + comando)
    char op_code = OP_CODE_PLAY; // 3
    char msg[2] = {op_code, command};
    
    if (write(session.req_pipe, msg, 2) != 2) {
        debug("Erro ao enviar comando\n");
    }
    
    debug("Comando enviado: %c\n", command);
}

// Implementação EXATA conforme protocolo
int pacman_disconnect() {
    if (session.req_pipe == -1) {
        debug("Tentativa de desconectar sem conexão ativa\n");
        return 1;
    }
    
    // Enviar pedido de desconexão (EXIGÊNCIA: OP_CODE=2)
    char op_code = OP_CODE_DISCONNECT; // 2
    
    if (write(session.req_pipe, &op_code, 1) != 1) {
        debug("Erro ao enviar pedido de desconexão\n");
    }
    
    // Fechar pipes (EXIGÊNCIA: sem esperar por respostas)
    close(session.req_pipe);
    close(session.notif_pipe);
    
    // Remover pipes (EXIGÊNCIA: apagar named pipes do cliente)
    unlink(session.req_pipe_path);
    unlink(session.notif_pipe_path);
    
    // Resetar sessão
    session.req_pipe = -1;
    session.notif_pipe = -1;
    session.req_pipe_path[0] = '\0';
    session.notif_pipe_path[0] = '\0';
    
    debug("Desconectado com sucesso\n");
    return 0;
}

// Implementação EXATA conforme protocolo
Board receive_board_update(void) {
    Board board = {0};
    
    if (session.notif_pipe == -1) {
        return board;
    }
    
    // Tentar ler atualização (pipe está em modo não-bloqueante)
    char op_code;
    ssize_t bytes = read(session.notif_pipe, &op_code, 1);

    if (bytes < 0) {
        // Sem dados disponíveis neste momento (EAGAIN/EWOULDBLOCK): apenas não há update
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return board; // board.data == NULL sinaliza "sem atualização"
        }

        // Erro real
        return board;
    }

    if (bytes == 0) {
        // EOF: servidor fechou pipe -> sinalizar fim de jogo
        board.game_over = 1;
        return board;
    }
    
    if (op_code != OP_CODE_BOARD) { // 4
        debug("Código de operação inválido: %d\n", op_code);
        return board;
    }
    
    // Ler dados do tabuleiro (EXIGÊNCIA: formato específico)
    if (read(session.notif_pipe, &board.width, sizeof(int)) != sizeof(int) ||
        read(session.notif_pipe, &board.height, sizeof(int)) != sizeof(int) ||
        read(session.notif_pipe, &board.tempo, sizeof(int)) != sizeof(int) ||
        read(session.notif_pipe, &board.victory, sizeof(int)) != sizeof(int) ||
        read(session.notif_pipe, &board.game_over, sizeof(int)) != sizeof(int) ||
        read(session.notif_pipe, &board.accumulated_points, sizeof(int)) != sizeof(int)) {
        debug("Erro ao ler dados do tabuleiro\n");
        return board;
    }
      
    // Alocar e ler dados do tabuleiro
    int board_size = board.width * board.height;
    board.data = malloc(board_size + 1);
    
    if (!board.data) {
        debug("Erro de alocação de memória\n");
        return board;
    }
    
    if (read(session.notif_pipe, board.data, board_size) != board_size) {
        debug("Erro ao ler dados do tabuleiro\n");
        free(board.data);
        board.data = NULL;
        return board;
    }
    
    board.data[board_size] = '\0';
    
    return board;
}