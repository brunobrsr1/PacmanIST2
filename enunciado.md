# Projeto de Sistemas Operativos 2025-26 - Enunciado da 2ª Parte
**Cursos:** LEIC-A / LEIC-T / LETI

## 1. Objetivos
A segunda parte do projeto consiste em 2 exercícios que visam:
1. Desenvolver um servidor de jogos para o PacmanIST que suporte jogos paralelos iniciados por múltiplos processos cliente ligando ao PacmanIST através de named pipes.
2. Desenvolver um signal handler que permita gerar um log para ficheiro que descreve o estado de todos os tabuleiros de jogo atualmente existentes no servidor.

## 2. Código Base
O código base contém uma implementação do PacmanIST (solução da 1ª parte) e uma API de cliente vazia. A funcionalidade de gravação do estado (comando "G") deve ser desativada nesta parte.

## 3. Exercício 1: Interação com processos clientes por named pipes
O PacmanIST passa a ser um servidor autónomo lançado como:
`./PacmanIST levels_dir max_games nome_do_FIFO_de_registo`

* **levels_dir:** Diretoria com os níveis de jogo.
* **max_games:** Número máximo de instâncias de jogos paralelos.
* **nome_do_FIFO_de_registo:** Nome do named pipe que o servidor cria para receber pedidos de início de sessão.

O cliente é lançado como:
`./client id_do_cliente nome_do_FIFO_de_registo [ficheiro_pacman]`

### 3.1. Funcionamento das Sessões
- O cliente liga-se ao pipe do servidor e envia um pedido de sessão contendo os nomes de dois named pipes criados pelo cliente: um para **pedidos** e outro para **notificações**.
- O servidor aceita no máximo `max_games` sessões. Se atingir o limite, deve bloquear à espera que uma sessão termine.
- O cliente envia comandos pelo FIFO de pedidos e recebe atualizações periódicas do tabuleiro pelo FIFO de notificações (ritmo definido pelo parâmetro "TEMPO").
- A sessão termina quando o servidor deteta que o cliente está indisponível ou o jogo acaba.

### 3.2. API Cliente do PacmanIST (em C)
- `int pacman_connect(char const *req_pipe_path, char const *notif_pipe_path, char const *server_pipe_path)`
    - Estabelece a sessão. Retorna 0 em sucesso, 1 em erro.
- `int pacman_disconnect()`
    - Termina a sessão, fecha e apaga os pipes do cliente. Retorna 0 em sucesso, 1 em erro.
- `int receive_board_updates(char* tabuleiro)`
    - Atualiza o tabuleiro local com base nas mensagens periódicas do servidor.
- `int pacman_play(char command)`
    - Envia um movimento ao servidor através do FIFO de pedidos.

### 3.3. Arquitetura do Cliente
O cliente deve usar duas tarefas (threads):
1. **Tarefa Principal:** Estabelece a ligação e recebe atualizações do servidor (ncurses).
2. **Tarefa de Input:** Lê comandos do stdin/ficheiro e envia-os ao servidor.

### 3.4. Protocolo de Comunicação (Mensagens)
As mensagens seguem o formato: `OP_CODE (char) | Dados...`

- **Connect (OP_CODE=1):** `1 | char[40] req_pipe | char[40] notif_pipe`. Resposta: `1 | result (char)`.
- **Disconnect (OP_CODE=2):** `2`. (Sem resposta).
- **Play (OP_CODE=3):** `3 | command (char)`. (Sem resposta).
- **Update (OP_CODE=4):** `4 | width (int) | height (int) | tempo (int) | victory (int) | game_over (int) | points (int) | board_data (char[w*h])`.

### 3.5. Arquitetura do Servidor (Múltiplas Sessões)
- **Tarefa Anfitriã:** Escuta no FIFO de registo e coloca pedidos num buffer produtor-consumidor (sincronizado com semáforos e mutexes).
- **Pool de Tarefas Gestoras:** `max_games` tarefas que retiram pedidos do buffer e gerem a interação com cada cliente.
- **Tarefas de Monstros:** Independentes das tarefas de gestão de sessão.

## 4. Exercício 2: Terminação das ligações usando sinais
Redefinir o tratamento do sinal **SIGUSR1** no servidor:
- Apenas a **tarefa anfitriã** deve escutar o sinal.
- As outras tarefas devem inibir o sinal usando `pthread_sigmask(SIG_BLOCK, ...)`.
- Ao receber `SIGUSR1`, o servidor deve gerar um ficheiro com a lista dos 5 clientes ligados com maior pontuação.

## 5. Submissão e Avaliação
- **Prazo:** 9 de Janeiro às 23h59 via Fénix.
- **Formato:** Ficheiro ZIP com código fonte e Makefile.
- **Nota:** O projeto deve compilar e correr corretamente no cluster Sigma.