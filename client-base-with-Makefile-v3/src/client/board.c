#include "board.h"
#include "parser.h"
#include <stdlib.h>
#include <stdio.h> //snprintf
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

// Helper private function to find and kill pacman at specific position
static int find_and_kill_pacman(board_t* board, int new_x, int new_y) {
    for (int p = 0; p < board->n_pacmans; p++) {
        pacman_t* pac = &board->pacmans[p];
        if (pac->pos_x == new_x && pac->pos_y == new_y && pac->alive) {
            pac->alive = 0;
            kill_pacman(board, p);
            return DEAD_PACMAN;
        }
    }
    return VALID_MOVE;
}

// Helper private function for getting board position index
static inline int get_board_index(board_t* board, int x, int y) {
    return y * board->width + x;
}

// Helper private function for checking valid position
static inline int is_valid_position(board_t* board, int x, int y) {
    return (x >= 0 && x < board->width) && (y >= 0 && y < board->height);
}

int move_pacman(board_t* board, int pacman_index, command_t* command) {
    if (pacman_index < 0 || !board->pacmans[pacman_index].alive) {
        return DEAD_PACMAN;
    }

    pacman_t* pac = &board->pacmans[pacman_index];
    int new_x = pac->pos_x;
    int new_y = pac->pos_y;

    // check passo
    if (pac->waiting > 0) {
        pac->waiting -= 1;
        return VALID_MOVE;
    }
    pac->waiting = pac->passo;

    char direction = command->command;

    if (direction == 'R') {
        char directions[] = {'W', 'S', 'A', 'D'};
        direction = directions[rand() % 4];
    }

    // Calculate new position based on direction
    switch (direction) {
        case 'W': // Up
            new_y--;
            break;
        case 'S': // Down
            new_y++;
            break;
        case 'A': // Left
            new_x--;
            break;
        case 'D': // Right
            new_x++;
            break;
        case 'T': // Wait
            if (command->turns_left == 1) {
                pac->current_move += 1;
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        default:
            return INVALID_MOVE;
    }

    pac->current_move += 1;

    // Check boundaries
    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, pac->pos_x, pac->pos_y);

    // locks - acquire in consistent order
    if (old_index < new_index) {
        pthread_mutex_lock(&board->board[old_index].lock);
        pthread_mutex_lock(&board->board[new_index].lock);
    }
    else {
        pthread_mutex_lock(&board->board[new_index].lock);
        pthread_mutex_lock(&board->board[old_index].lock);
    }

    char target_content = board->board[new_index].content;

    // Check for portal
    if (board->board[new_index].has_portal) {
        board->board[old_index].content = ' ';
        board->board[new_index].content = 'P';
        pthread_mutex_unlock(&board->board[old_index].lock);
        pthread_mutex_unlock(&board->board[new_index].lock);
        return REACHED_PORTAL;
    }

    // Check for walls
    if (target_content == 'W') {
        pthread_mutex_unlock(&board->board[old_index].lock);
        pthread_mutex_unlock(&board->board[new_index].lock);
        return INVALID_MOVE;
    }

    // Check for ghosts
    if (target_content == 'M') {
        kill_pacman(board, pacman_index);
        pthread_mutex_unlock(&board->board[old_index].lock);
        pthread_mutex_unlock(&board->board[new_index].lock);
        return DEAD_PACMAN;
    }

    // Collect points
    if (board->board[new_index].has_dot) {
        pac->points++;
        board->board[new_index].has_dot = 0;
    }

    board->board[old_index].content = ' ';
    pac->pos_x = new_x;
    pac->pos_y = new_y;
    board->board[new_index].content = 'P';

    pthread_mutex_unlock(&board->board[old_index].lock);
    pthread_mutex_unlock(&board->board[new_index].lock);

    return VALID_MOVE;
}

int move_ghost_charged(board_t* board, int ghost_index, char direction) {
    ghost_t* ghost = &board->ghosts[ghost_index];
    int x = ghost->pos_x;
    int y = ghost->pos_y;
    int new_x = x;
    int new_y = y;
    int result;

    ghost->charged = 0;

    switch (direction) {
        case 'W':
            if (y == 0) return INVALID_MOVE;

            for (int i = 0; i <= y; i++) {
                pthread_mutex_lock(&board->board[i * board->width + x].lock);
            }

            new_y = 0;
            result = VALID_MOVE;
            for (int i = y - 1; i >= 0; i--) {
                char target_content = board->board[i * board->width + x].content;
                if (target_content == 'W' || target_content == 'M') {
                    new_y = i + 1;
                    result = VALID_MOVE;
                    break;
                }
                else if (target_content == 'P') {
                    new_y = i;
                    result = find_and_kill_pacman(board, new_x, new_y);
                    break;
                }
            }

            for (int i = 0; i <= y; i++) {
                pthread_mutex_unlock(&board->board[i * board->width + x].lock);
            }
            break;
        case 'S':
            if (y == board->height - 1) return INVALID_MOVE;

            for (int i = y; i < board->height; i++) {
                pthread_mutex_lock(&board->board[i * board->width + x].lock);
            }

            new_y = board->height - 1;
            result = VALID_MOVE;
            for (int i = y + 1; i < board->height; i++) {
                char target_content = board->board[i * board->width + x].content;
                if (target_content == 'W' || target_content == 'M') {
                    new_y = i - 1;
                    result = VALID_MOVE;
                    break;
                }
                else if (target_content == 'P') {
                    new_y = i;
                    result = find_and_kill_pacman(board, new_x, new_y);
                    break;
                }
            }

            for (int i = y; i < board->height; i++) {
                pthread_mutex_unlock(&board->board[i * board->width + x].lock);
            }
            break;
        case 'A':
            if (x == 0) return INVALID_MOVE;

            for (int j = 0; j <= x; j++) {
                pthread_mutex_lock(&board->board[y * board->width + j].lock);
            }

            new_x = 0;
            result = VALID_MOVE;
            for (int j = x - 1; j >= 0; j--) {
                char target_content = board->board[y * board->width + j].content;
                if (target_content == 'W' || target_content == 'M') {
                    new_x = j + 1;
                    result = VALID_MOVE;
                    break;
                }
                else if (target_content == 'P') {
                    new_x = j;
                    result = find_and_kill_pacman(board, new_x, new_y);
                    break;
                }
            }

            for (int j = 0; j <= x; j++) {
                pthread_mutex_unlock(&board->board[y * board->width + j].lock);
            }
            break;
        case 'D':
            if (x == board->width - 1) return INVALID_MOVE;

            for (int j = x; j < board->width; j++) {
                pthread_mutex_lock(&board->board[y * board->width + j].lock);
            }

            new_x = board->width - 1;
            result = VALID_MOVE;
            for (int j = x + 1; j < board->width; j++) {
                char target_content = board->board[y * board->width + j].content;
                if (target_content == 'W' || target_content == 'M') {
                    new_x = j - 1;
                    result = VALID_MOVE;
                    break;
                }
                else if (target_content == 'P') {
                    new_x = j;
                    result = find_and_kill_pacman(board, new_x, new_y);
                    break;
                }
            }

            for (int j = x; j < board->width; j++) {
                pthread_mutex_unlock(&board->board[y * board->width + j].lock);
            }
            break;
        default:
            return INVALID_MOVE;
    }

    board->board[y * board->width + x].content = ' ';

    ghost->pos_x = new_x;
    ghost->pos_y = new_y;
    board->board[new_y * board->width + new_x].content = 'M';
    return result;
}

int move_ghost(board_t* board, int ghost_index, command_t* command) {
    ghost_t* ghost = &board->ghosts[ghost_index];
    int new_x = ghost->pos_x;
    int new_y = ghost->pos_y;

    // check passo
    if (ghost->waiting > 0) {
        ghost->waiting -= 1;
        return VALID_MOVE;
    }
    ghost->waiting = ghost->passo;

    char direction = command->command;

    if (direction == 'R') {
        char directions[] = {'W', 'S', 'A', 'D'};
        direction = directions[rand() % 4];
    }

    // Calculate new position based on direction
    switch (direction) {
        case 'W':
            new_y--;
            break;
        case 'S':
            new_y++;
            break;
        case 'A':
            new_x--;
            break;
        case 'D':
            new_x++;
            break;
        case 'C': // Charge
            ghost->current_move += 1;
            ghost->charged = 1;
            return VALID_MOVE;
        case 'T':
            if (command->turns_left == 1) {
                ghost->current_move += 1;
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        default:
            return INVALID_MOVE;
    }

    ghost->current_move++;
    if (ghost->charged)
        return move_ghost_charged(board, ghost_index, direction);

    // Check boundaries
    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    int new_index = new_y * board->width + new_x;
    int old_index = ghost->pos_y * board->width + ghost->pos_x;

    // locks
    if (old_index < new_index) {
        pthread_mutex_lock(&board->board[old_index].lock);
        pthread_mutex_lock(&board->board[new_index].lock);
    }
    else {
        pthread_mutex_lock(&board->board[new_index].lock);
        pthread_mutex_lock(&board->board[old_index].lock);
    }

    char target_content = board->board[new_index].content;

    // Check for walls
    if (target_content == 'W') {
        pthread_mutex_unlock(&board->board[old_index].lock);
        pthread_mutex_unlock(&board->board[new_index].lock);
        return INVALID_MOVE;
    }

    // Check for ghosts
    if (target_content == 'M') {
        pthread_mutex_unlock(&board->board[old_index].lock);
        pthread_mutex_unlock(&board->board[new_index].lock);
        return INVALID_MOVE;
    }

    int result = VALID_MOVE;
    // Check for pacman
    if (target_content == 'P') {
        for (int i = 0; i < board->n_pacmans; i++) {
            pacman_t* pac = &board->pacmans[i];
            if (pac->pos_x == new_x && pac->pos_y == new_y && pac->alive) {
                pac->alive = 0;
                kill_pacman(board, i);
                result = DEAD_PACMAN;
                break;
            }
        }
    }

    board->board[old_index].content = ' ';
    ghost->pos_x = new_x;
    ghost->pos_y = new_y;
    board->board[new_index].content = 'M';

    pthread_mutex_unlock(&board->board[old_index].lock);
    pthread_mutex_unlock(&board->board[new_index].lock);

    return result;
}

void kill_pacman(board_t* board, int pacman_index) {
    pacman_t* pac = &board->pacmans[pacman_index];
    int index = pac->pos_y * board->width + pac->pos_x;

    board->board[index].content = ' ';
    pac->alive = 0;
}

void kill_ghost(board_t* board, int ghost_index) {
    if (ghost_index < 0 || ghost_index >= board->n_ghosts) {
        return; // Invalid ghost index
    }
    
    ghost_t* ghost = &board->ghosts[ghost_index];
    
    // Check if ghost is already eliminated or has invalid position
    if (ghost->pos_x < 0 || ghost->pos_y < 0) {
        return; // Ghost already eliminated
    }
    
    int index = ghost->pos_y * board->width + ghost->pos_x;
    
    // Remove ghost from board
    board->board[index].content = ' ';
    
    // Mark ghost as eliminated by setting position to invalid coordinates
    ghost->pos_x = -1;
    ghost->pos_y = -1;
}

int load_pacman(board_t* board) {
    board->board[1 * board->width + 1].content = 'P';
    board->pacmans[0].pos_x = 1;
    board->pacmans[0].pos_y = 1;
    board->pacmans[0].alive = 1;
    board->pacmans[0].points = 0;
    return 0;
}

int load_ghost(board_t* board) {
    board->board[4 * board->width + 8].content = 'M';
    board->ghosts[0].pos_x = 8;
    board->ghosts[0].pos_y = 4;
    board->board[0 * board->width + 5].content = 'M';
    board->ghosts[1].pos_x = 5;
    board->ghosts[1].pos_y = 0;
    return 0;
}

int load_level(board_t* board, char* filename, char* dirname, int points) {
    if (read_level(board, filename, dirname) < 0) {
        return -1;
    }

    if (read_pacman(board, points) < 0) {
    }

    if (read_ghosts(board) < 0) {
    }

    pthread_rwlock_init(&board->state_lock, NULL);

    for (int i = 0; i < board->height * board->width; i++) {
        pthread_mutex_init(&board->board[i].lock, NULL);
    }

    return 0;
}

void unload_level(board_t* board) {
    pthread_rwlock_destroy(&board->state_lock);
    for (int i = 0; i < board->height * board->width; i++) {
        pthread_mutex_destroy(&board->board[i].lock);
    }
    free(board->board);
    free(board->pacmans);
    free(board->ghosts);
}

