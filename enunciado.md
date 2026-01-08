# [cite_start]Projeto de Sistemas Operativos 2025-26 - Enunciado da 2ª Parte [cite: 1]

[cite_start]Este documento detalha as instruções para a segunda parte do projeto, focada no desenvolvimento de um servidor de jogos paralelo para o PacmanIST e na gestão de sinais para monitorização do estado. [cite: 2, 6, 7]

---

## 1. Objetivos Principais

- [cite_start]**Servidor Parallelizado:** Desenvolver um servidor que suporte jogos paralelos iniciados por múltiplos clientes via _named pipes_ (FIFOs). [cite: 6]
- [cite_start]**Monitorização por Sinais:** Implementar um _signal handler_ para gerar logs do estado de todos os tabuleiros ativos no servidor. [cite: 7]
- [cite_start]**Código Base:** O projeto utiliza uma implementação da 1ª parte como base, mas a funcionalidade de gravação de estado (comando "G") deve ser desativada. [cite: 8, 9]

---

## 2. Exercício 1: Interação via Named Pipes

### [cite_start]2.1 Lançamento do Servidor [cite: 10]

O servidor deve ser lançado com o seguinte comando:
[cite_start]`PacmanIST levels_dir max_games nome_do_FIFO_de_registo` [cite: 11, 12]

- [cite_start]**levels_dir:** Diretoria com os níveis de jogo disponíveis. [cite: 15]
- [cite_start]**max_games:** Número máximo de instâncias de jogos paralelos. [cite: 16]
- [cite_start]**nome_do_FIFO_de_registo:** Nome do _named pipe_ que o servidor cria para aceitar novos clientes. [cite: 13, 14]

### [cite_start]2.2 Lançamento do Cliente [cite: 17]

O cliente deve ser lançado com:
[cite_start]`client id_do_cliente nome_do_FIFO_de_registo [ficheiro_pacman]` [cite: 18, 19, 20]

- [cite_start]**id_do_cliente:** Identificador único do cliente (inteiro). [cite: 21]
- [cite_start]**ficheiro_pacman:** (Opcional) Ficheiro com comandos; caso contrário, lê do `stdin`. [cite: 21]

### 2.3 Gestão de Sessões

- [cite_start]O servidor aceita no máximo `max_games` sessões em simultâneo. [cite: 27]
- [cite_start]Se o limite for atingido, o servidor deve bloquear novos pedidos até que uma sessão termine. [cite: 28]
- [cite_start]Uma sessão termina se o servidor detetar que o cliente está indisponível ou se o jogo acabar. [cite: 32]
- [cite_start]O cliente cria dois FIFOs (pedidos e atualizações) e envia os seus nomes ao servidor no pedido inicial. [cite: 23, 26, 39]

---

## 3. API Cliente e Protocolo

### 3.1 Funções da API

- **pacman_connect:** Estabelece a ligação enviando os caminhos dos FIFOs do cliente. [cite_start]Retorna 0 em sucesso e 1 em erro. [cite: 37, 38, 41]
- **pacman_disconnect:** Termina a sessão, fecha e apaga os FIFOs do cliente. [cite_start]O servidor deve libertar todos os recursos associados. [cite: 42, 43, 44]
- [cite_start]**receive_board_updates:** Função para atualizar o tabuleiro local com base nas mensagens periódicas do servidor. [cite: 49, 50]
- **pacman_play:** Envia comandos de movimento do Pacman para o servidor. [cite_start]Não espera por resposta imediata. [cite: 54, 55]

### [cite_start]3.2 Protocolo de Comunicação (Mensagens) [cite: 75]

| Operação               | OP_CODE | Estrutura da Mensagem                                        |
| :--------------------- | :------ | :----------------------------------------------------------- | -------------------------------------------------------- | ---------------------------------------------------- | ----------- | ------------- | --------------- | ------------ | -------------------------------------------------- |
| **Connect (Pedido)**   | 1       | `(char)1                                                     | (char[40]) pipe_pedidos                                  | (char[40]) [cite_start]pipe_notificacoes` [cite: 80] |
| **Connect (Resposta)** | 1       | `(char)1                                                     | (char) [cite_start]result` (0 = sucesso) [cite: 82, 102] |
| **Disconnect**         | 2       | [cite_start]`(char)2` (Sem resposta esperada) [cite: 85, 87] |
| **Play**               | 3       | `(char)3                                                     | (char) [cite_start]comando` [cite: 91]                   |
| **Board Update**       | 4       | `(char)4                                                     | (int) width                                              | (int) height                                         | (int) tempo | (int) victory | (int) game_over | (int) points | (char[w*h]) [cite_start]board_data` [cite: 95, 96] |

[cite_start]_Nota: As strings têm tamanho fixo de 40 caracteres, preenchidas com `\0`. [cite: 101]_

---

## 4. Arquitetura do Servidor

### [cite_start]Etapa 1.1: Sessão Única [cite: 105]

- [cite_start]O servidor atende apenas um cliente remoto de cada vez (`max_games = 1`). [cite: 107, 108]
- [cite_start]Uma tarefa dedicada gere a sessão, lendo o tabuleiro e enviando atualizações ao cliente. [cite: 122]

### [cite_start]Etapa 1.2: Múltiplas Sessões [cite: 126]

- [cite_start]**Tarefa Anfitriã:** Responsável por receber pedidos de conexão no FIFO de registo. [cite: 130]
- [cite_start]**Buffer Produtor-Consumidor:** A tarefa anfitriã insere pedidos num buffer sincronizado com semáforos e mutexes. [cite: 135, 138]
- [cite_start]**Tarefas Gestoras:** Existe uma pool de `max_games` tarefas que extraem pedidos do buffer e servem os clientes. [cite: 131, 132, 136]

---

## [cite_start]5. Exercício 2: Sinais e SIGUSR1 [cite: 159]

- [cite_start]**Redefinição de SIGUSR1:** O servidor deve capturar o sinal `SIGUSR1`. [cite: 160, 161]
- [cite_start]**Gestão de Máscaras:** Apenas a tarefa anfitriã deve escutar o sinal; todas as outras tarefas devem bloqueá-lo usando `pthread_sigmask` com `SIG_BLOCK`. [cite: 162, 163]
- [cite_start]**Ação:** Ao receber o sinal, a tarefa anfitriã gera um ficheiro com a lista dos 5 clientes ligados com maior pontuação. [cite: 164]

---

## [cite_start]6. Submissão e Avaliação [cite: 168]

- [cite_start]**Prazo:** 9 de Janeiro às 23h59 via Fénix. [cite: 169]
- [cite_start]**Ficheiros:** Arquivo `.zip` contendo código fonte e `Makefile`. [cite: 170]
- [cite_start]**Makefile:** Deve incluir o comando `make clean` para limpar binários. [cite: 172]
- [cite_start]**Plataforma de Teste:** O projeto deve compilar e correr no cluster **sigma**. [cite: 173, 174]
- [cite_start]**Ética:** Trabalho original; a partilha de código resulta em reprovação. [cite: 177, 178, 179]
