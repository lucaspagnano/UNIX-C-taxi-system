#include "common.h"

int g_client_fifo_fd = -1;
int g_veiculo_id = -1;

void trataCancelamento(int s) {
    // Avisar Cliente
    if (g_client_fifo_fd != -1) {
        RespostaServidor resp;
        memset(&resp, 0, sizeof(resp));
        resp.tipo = MSG_VEICULO_CHEGOU;
        snprintf(resp.mensagem, sizeof(resp.mensagem), "Viagem CANCELADA!");
        write(g_client_fifo_fd, &resp, sizeof(resp));
        close(g_client_fifo_fd);
    }
    // Avisar Controlador (via stdout)
    printf("%d CANCELADO\n", g_veiculo_id);
    fflush(stdout);
    exit(0);
}

int main(int argc, char *argv[]) {
    // args: <ID> <Distancia> <PID_Cliente>
    if (argc != 4) return 1;

    g_veiculo_id = atoi(argv[1]);
    int distancia = atoi(argv[2]);
    pid_t pid_cliente = atoi(argv[3]);
    
    signal(SIGUSR1, trataCancelamento);

    // Conectar ao Cliente
    char client_fifo[50];
    sprintf(client_fifo, CLIENT_FIFO_FMT, pid_cliente);
    g_client_fifo_fd = open(client_fifo, O_WRONLY);

    // Início
    printf("%d INICIO\n", g_veiculo_id); fflush(stdout); // P/ Controlador

    if (g_client_fifo_fd != -1) {
        RespostaServidor resp;
        memset(&resp, 0, sizeof(resp));
        resp.tipo = MSG_VEICULO_CHEGOU;
        snprintf(resp.mensagem, sizeof(resp.mensagem), "Taxi %d chegou! A iniciar %dKm.", g_veiculo_id, distancia);
        write(g_client_fifo_fd, &resp, sizeof(resp));
    }

    // Simulação (1 seg por 10% da viagem)
    for (int i = 1; i <= 10; i++) {
        usleep(500000); // 0.5 segundos por tick (pode ajustar)
        printf("%d PERCENTAGEM %d\n", g_veiculo_id, i * 10);
        fflush(stdout);
    }

    // Fim
    if (g_client_fifo_fd != -1) {
        RespostaServidor resp;
        memset(&resp, 0, sizeof(resp));
        resp.tipo = MSG_VEICULO_CHEGOU;
        snprintf(resp.mensagem, sizeof(resp.mensagem), "Taxi chegou ao destino!");
        write(g_client_fifo_fd, &resp, sizeof(resp));
        close(g_client_fifo_fd);
    }

    printf("%d CONCLUIDO\n", g_veiculo_id); fflush(stdout);
    return 0;
}
