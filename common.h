#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

// --- Constantes e Limites ---
#define MAX_USERS 30
#define SERVER_FIFO "/tmp/taxi_srv_fifo"
#define CLIENT_FIFO_FMT "/tmp/taxi_cli_%d_fifo" // %d será o PID do cliente

// --- Tipos de Mensagens (Protocolo) ---
typedef enum {
    MSG_LOGIN,
    MSG_PEDIDO_VIAGEM,
    MSG_CANCELAR_PEDIDO,
    MSG_PEDIDO_CONSULTA,
    MSG_RESPOSTA_SERVIDOR,
    MSG_VEICULO_CHEGOU,
    MSG_LOGOUT 
} TipoMsg;

// --- Estrutura de Comunicação (Cliente -> Servidor) ---
typedef struct {
    TipoMsg tipo;
    pid_t pid_cliente;
    char dados[100]; // Username ou "hora local distancia"
    int id_pedido;   
} PedidoCliente;

// --- Estrutura de Resposta (Servidor -> Cliente) ---
typedef struct {
    TipoMsg tipo;
    char mensagem[2048]; 
    int sucesso;        // 1 = Sim, 0 = Não
} RespostaServidor;

// --- Estrutura Interna de Utilizador (Controlador) ---
typedef struct {
    char username[50];
    pid_t pid;
    int ocupado; 
} Utilizador;

#endif
