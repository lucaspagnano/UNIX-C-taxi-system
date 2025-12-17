#include "common.h"

char client_fifo_name[50];
int server_fifo_fd = -1;
int client_fifo_fd = -1;
int continua = 1;

// Tratamento de Sinais
void encerra(int s) { // s - numero do sinal \ SIGINT = 2
    if (server_fifo_fd != -1) close(server_fifo_fd);
    if (client_fifo_fd != -1) close(client_fifo_fd);
    unlink(client_fifo_name); // remove o FIFO do disco
    exit(0);
}

// Thread de Escuta
void *escutaServidor(void *arg) {
    RespostaServidor resp;
    int n;

    while (continua) {
        n = read(client_fifo_fd, &resp, sizeof(RespostaServidor)); // threaad fica parada aqui a espera que o controlador ou veiculo escreva algo no pipe privado
        
        // Se recebeu dados válidos
        if (n == sizeof(RespostaServidor)) {
            printf("%s\n", resp.mensagem);
            fflush(stdout); // forcar printf a aparecer no ecra 
            if (strstr(resp.mensagem, "SHUTDOWN") != NULL) { // procura keyword SHUTDOWN e encerra tudo 
                printf("Ordem de encerramento recebida.\n");
                if (server_fifo_fd != -1) close(server_fifo_fd);
                if (client_fifo_fd != -1) close(client_fifo_fd);
                unlink(client_fifo_name);
                kill(getpid(), SIGKILL); 
            }
        }
        // Se o servidor fechar o pipe abruptamente (EOF)
        else if (n == 0) {
             printf("Erro: Ligação perdida com o servidor.\n");
             unlink(client_fifo_name);
             kill(getpid(), SIGKILL);
        }
    }
    return NULL;
}

void mostraAjuda() {
    printf("\n");
    printf("MENU DO UTILIZADOR\n");
    printf("agendar <t> <loc> <km> -> Agendar nova viagem\n");
    printf("consultar -> Ver estado das minhas viagens\n");
    printf("cancelar <id> -> Cancelar uma viagem específica\n");
    printf("terminar -> Sair da aplicação\n\n");
}

int main(int argc, char *argv[]) {
    pthread_t t_escuta;
    PedidoCliente pedido;
    char linha[100];

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <username>\n", argv[0]);
        exit(1);
    }

    signal(SIGINT, encerra); // se apertar ctrl+c quero que chame encerra 

    // 1. Criar FIFO do Cliente
    sprintf(client_fifo_name, CLIENT_FIFO_FMT, getpid()); // obtem o id
    if (mkfifo(client_fifo_name, 0777) == -1) { // cria o FIFO
        perror("mkfifo cliente"); exit(1);
    }

    // 2. Abrir FIFOs
    server_fifo_fd = open(SERVER_FIFO, O_WRONLY); // canal para falar
    if (server_fifo_fd == -1) {
        printf("Controlador offline.\n"); unlink(client_fifo_name); exit(1);
    }
    client_fifo_fd = open(client_fifo_name, O_RDWR); // RDWR evita EOF - canal para ouvir privado

    // 3. Enviar Login
    memset(&pedido, 0, sizeof(pedido)); // preenche com 0 inicialmente
    //preencher dados
    pedido.tipo = MSG_LOGIN;
    pedido.pid_cliente = getpid();
    strncpy(pedido.dados, argv[1], sizeof(pedido.dados)-1);
    write(server_fifo_fd, &pedido, sizeof(pedido)); // cliente apresenta-se ao servidor 

    // 4. ESPERA BLOQUEANTE PELA RESPOSTA DO LOGIN
    RespostaServidor resp;
    read(client_fifo_fd, &resp, sizeof(resp)); // Fica aqui parado à espera
    
    if (resp.sucesso == 0) {
        printf("Erro Login: %s\n", resp.mensagem);
        encerra(0); // Sai logo se recusado
    }
    printf("Login: %s\n", resp.mensagem);

    // 5. Se passou, lança thread e mostra menu
    pthread_create(&t_escuta, NULL, escutaServidor, NULL); //  lanca thread para ficar a ouvir em segundo plano

    // APRESENTAÇÃO INICIAL
    printf("\nBem-vindo ao serviço de Táxis!\n");
    mostraAjuda(); 
    
    while (continua) {
        if (fgets(linha, sizeof(linha), stdin) == NULL) break;
        linha[strcspn(linha, "\n")] = 0;

        if (strcmp(linha, "terminar") == 0) {
            // Enviar Logout
            pedido.tipo = MSG_LOGOUT;
            pedido.pid_cliente = getpid();
            write(server_fifo_fd, &pedido, sizeof(pedido));
            continua = 0;
        } 
        else if (strncmp(linha, "agendar", 7) == 0) {
            pedido.tipo = MSG_PEDIDO_VIAGEM;
            pedido.pid_cliente = getpid();
            strncpy(pedido.dados, linha + 8, sizeof(pedido.dados)-1); // +8 salta a palavra agendar e envia resto em dados
            write(server_fifo_fd, &pedido, sizeof(pedido)); // enviar estrutura ao servidor 
        }
        else if (strncmp(linha, "cancelar", 8) == 0) {
            int id;
            if (sscanf(linha + 9, "%d", &id) == 1) {
                pedido.tipo = MSG_CANCELAR_PEDIDO;
                pedido.pid_cliente = getpid();
                sprintf(pedido.dados, "%d", id); // Envia o ID como texto
                write(server_fifo_fd, &pedido, sizeof(pedido));
            } else {
                printf("Erro: 'cancelar <ID>'\n ");
            }
        }
        else if (strcmp(linha, "consultar") == 0) {
            pedido.tipo = MSG_PEDIDO_CONSULTA;
            pedido.pid_cliente = getpid();
            strcpy(pedido.dados, "");
            
            write(server_fifo_fd, &pedido, sizeof(pedido));
        }
        else {
            printf("Comando desconhecido.\n ");
        }
    }

    encerra(0);
    return 0;
}
