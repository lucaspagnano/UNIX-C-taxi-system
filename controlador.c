#include "common.h"

#define MAX_VIAGENS 10

typedef struct {
    int id;
    pid_t pid_cliente;
    int hora_inicio;
    char origem[50];
    int distancia;
    int status; // 0=Agendada, 1=Em Curso, 2=Concluida, -1=Cancelada
    pid_t pid_veiculo;
    int percentagem_atual;
} Viagem;

typedef struct {
    int servidor_fifo_fd;
    int continua;
    pthread_mutex_t mutex; 
    
    Utilizador utilizadores[MAX_USERS];
    int num_users;

    Viagem viagens[MAX_VIAGENS];
    int num_viagens;
    int tempo_sistema;
    
    int max_veiculos;
    int veiculos_ativos;
    long long total_kms_frota;
} ControladorData;

typedef struct {
    int fd;                 
    int index_viagem;       
    ControladorData *dados; 
} ArgsTelemetria;


void enviaResposta(pid_t pid_cliente, TipoMsg tipo, const char *msg, int sucesso) {
    char cliente_fifo[50];
    RespostaServidor resp;
    sprintf(cliente_fifo, CLIENT_FIFO_FMT, pid_cliente);
    memset(&resp, 0, sizeof(resp));
    resp.tipo = tipo;
    resp.sucesso = sucesso;
    strncpy(resp.mensagem, msg, sizeof(resp.mensagem) - 1);
    
    int fd = open(cliente_fifo, O_WRONLY);
    if (fd != -1) {
        write(fd, &resp, sizeof(resp));
        close(fd);
    }
}

void processaLogin(ControladorData *dados, PedidoCliente *p) {
    pthread_mutex_lock(&dados->mutex);
    for(int i=0; i<dados->num_users; i++) {
        if(strcmp(dados->utilizadores[i].username, p->dados) == 0) {
            pthread_mutex_unlock(&dados->mutex);
            enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, "User ja existe", 0);
            return;
        }
    }
    if(dados->num_users < MAX_USERS) {
        Utilizador *u = &dados->utilizadores[dados->num_users++];
        strcpy(u->username, p->dados);
        u->pid = p->pid_cliente;
        u->ocupado = 0;
        printf("Login: %s entrou.\n", u->username);
        enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, "Login Aceite", 1);
    } else {
        enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, "Servidor Cheio", 0);
    }
    pthread_mutex_unlock(&dados->mutex);
}

void processaLogout(ControladorData *dados, pid_t pid) {
    pthread_mutex_lock(&dados->mutex);
    for(int i=0; i<dados->num_users; i++) {
        if(dados->utilizadores[i].pid == pid) {
            printf("Logout: %s saiu.\n", dados->utilizadores[i].username);
            dados->utilizadores[i] = dados->utilizadores[dados->num_users-1];
            dados->num_users--;
            break;
        }
    }
    pthread_mutex_unlock(&dados->mutex);
}

void processaAgendar(ControladorData *dados, PedidoCliente *p) {
    int segundos, dist;
    char local[50];
    
    if (sscanf(p->dados, "%d %s %d", &segundos, local, &dist) != 3) {
        enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, "Erro: agendar <segs> <local> <dist>", 0);
        return;
    }

    pthread_mutex_lock(&dados->mutex);
    if (dados->num_viagens < MAX_VIAGENS) {
        Viagem *v = &dados->viagens[dados->num_viagens++];
        v->id = dados->num_viagens; 
        v->pid_cliente = p->pid_cliente;
        v->hora_inicio = dados->tempo_sistema + segundos;
        v->distancia = dist;
        strcpy(v->origem, local);
        v->status = 0;
        v->percentagem_atual = 0;
        
        char msg[100];
        sprintf(msg, "Viagem agendada para t=%d", v->hora_inicio);
        enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, msg, 1);
        printf("Viagem %d agendada para t=%d\n", v->id, v->hora_inicio);
    } else {
        enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, "Lista cheia", 0);
    }
    pthread_mutex_unlock(&dados->mutex);
}

void processaConsulta(ControladorData *dados, PedidoCliente *p) {
    pthread_mutex_lock(&dados->mutex);
    char buffer_total[2048] = "\n--- As suas Viagens ---\n";
    char linha[200];
    int count = 0;

    for (int i = 0; i < dados->num_viagens; i++) {
        if (dados->viagens[i].pid_cliente == p->pid_cliente) {
            char *st = (dados->viagens[i].status==0) ? "Agendada" : 
                       (dados->viagens[i].status==1) ? "Em Curso" : 
                       (dados->viagens[i].status==2) ? "Concluida" : "Cancelada";
            sprintf(linha, "ID: %d | Orig: %s | Hora: %d | Estado: %s\n", 
                    dados->viagens[i].id, dados->viagens[i].origem, dados->viagens[i].hora_inicio, st);
            strcat(buffer_total, linha);
            count++;
        }
    }
    if(count==0) strcat(buffer_total, "Nenhuma viagem.\n");
    strcat(buffer_total, "-----------------------");
    enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, buffer_total, 1);
    pthread_mutex_unlock(&dados->mutex);
}

int coreCancelarViagem(ControladorData *dados, int id, pid_t pid_requerente, int is_admin) {
    int index = -1;
    for (int i = 0; i < dados->num_viagens; i++) {
        if (dados->viagens[i].id == id && (is_admin || dados->viagens[i].pid_cliente == pid_requerente)) {
            index = i;
            break;
        }
    }

    if (index == -1) return 0;

    if (dados->viagens[index].status == 0) {
        dados->viagens[index].status = -1;
        printf("Viagem %d cancelada.\n", id);
        return 1;
    } 
    else if (dados->viagens[index].status == 1) {
        kill(dados->viagens[index].pid_veiculo, SIGUSR1);
        dados->viagens[index].status = -1;
        printf("Viagem %d interrompida (SIGUSR1 enviado).\n", id);
        return 1;
    }
    return 0;
}

void processaCancelamentoCliente(ControladorData *dados, PedidoCliente *p) {
    int id;
    if (sscanf(p->dados, "%d", &id) != 1) {
        enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, "Use: cancelar <ID>", 0);
        return;
    }
    pthread_mutex_lock(&dados->mutex);
    if (coreCancelarViagem(dados, id, p->pid_cliente, 0)) {
        enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, "Viagem cancelada com sucesso.", 1);
    } else {
        enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, "Viagem não encontrada ou concluída.", 0);
    }
    pthread_mutex_unlock(&dados->mutex);
}

// Thread Leitura Telemetria
void *trataVeiculo(void *arg) {
    ArgsTelemetria *args = (ArgsTelemetria *)arg;
    char buf[256];
    int perc;
    int n;
    char ch;
    int pos = 0;

    while ((n = read(args->fd, &ch, 1)) > 0) {
        if (ch == '\n') {
            buf[pos] = '\0';
            
            if (sscanf(buf, "%*d PERCENTAGEM %d", &perc) == 1) {
                pthread_mutex_lock(&args->dados->mutex);
                args->dados->viagens[args->index_viagem].percentagem_atual = perc;
                pthread_mutex_unlock(&args->dados->mutex);
            }

            if (strstr(buf, "CONCLUIDO") != NULL) {
                pthread_mutex_lock(&args->dados->mutex);
                args->dados->viagens[args->index_viagem].status = 2;
                args->dados->veiculos_ativos--;
                
                args->dados->total_kms_frota += args->dados->viagens[args->index_viagem].distancia;
                
                printf("Viagem %d concluída. KMs acumulados: %lld\n", 
                       args->dados->viagens[args->index_viagem].id, args->dados->total_kms_frota);
                pthread_mutex_unlock(&args->dados->mutex);
            }
            else if (strstr(buf, "CANCELADO") != NULL || strstr(buf, "ERRO") != NULL) {
                pthread_mutex_lock(&args->dados->mutex);
                args->dados->viagens[args->index_viagem].status = -1;
                args->dados->veiculos_ativos--;
                pthread_mutex_unlock(&args->dados->mutex);
            }
            
            pos = 0;
        } else {
            if (pos < sizeof(buf) - 1) {
                buf[pos++] = ch;
            }
        }
    }
    
    close(args->fd);
    free(args);
    return NULL;
}

void lancaVeiculo(ControladorData *dados, int idx) {
    int pfd[2];
    pipe(pfd);
    pid_t pid = fork();

    if (pid == 0) { 
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        close(pfd[1]);
        char sid[10], sdist[10], spid[10];
        sprintf(sid, "%d", dados->viagens[idx].id);
        sprintf(sdist, "%d", dados->viagens[idx].distancia);
        sprintf(spid, "%d", dados->viagens[idx].pid_cliente);
        execl("./veiculo", "veiculo", sid, sdist, spid, NULL);
        exit(1);
    } else { 
        close(pfd[1]);
        dados->viagens[idx].status = 1;
        dados->viagens[idx].pid_veiculo = pid;
        dados->viagens[idx].percentagem_atual = 0;
        dados->veiculos_ativos++; 
        
        printf("Taxi enviado (Viagem %d). Frota: %d/%d\n", 
               dados->viagens[idx].id, dados->veiculos_ativos, dados->max_veiculos);
        
        pthread_t t;
        ArgsTelemetria *args = malloc(sizeof(ArgsTelemetria));
        args->fd = pfd[0]; args->index_viagem = idx; args->dados = dados;
        pthread_create(&t, NULL, trataVeiculo, args);
        pthread_detach(t);
    }
}

// Thread Relógio
void *trataRelogio(void *arg) {
    ControladorData *dados = (ControladorData*)arg;
    while(dados->continua) {
        sleep(1);
        pthread_mutex_lock(&dados->mutex);
        dados->tempo_sistema++;
        for(int i=0; i<dados->num_viagens; i++) {
            if(dados->viagens[i].status == 0 && dados->viagens[i].hora_inicio <= dados->tempo_sistema) {
                if (dados->veiculos_ativos < dados->max_veiculos) {
                    lancaVeiculo(dados, i);
                } else {
                    dados->viagens[i].status = -1; 
                    pthread_mutex_unlock(&dados->mutex);
                    enviaResposta(dados->viagens[i].pid_cliente, MSG_RESPOSTA_SERVIDOR, "Cancelada: Frota cheia", 0);
                    pthread_mutex_lock(&dados->mutex);
                }
            }
        }
        pthread_mutex_unlock(&dados->mutex);
    }
    return NULL;
}

// Thread Clientes
void *trataClientes(void *arg) {
    ControladorData *d = (ControladorData*)arg;
    PedidoCliente p;

    printf("Thread Clientes à escuta de pedidos\n");

    while(d->continua) {
        if(read(d->servidor_fifo_fd, &p, sizeof(p)) == sizeof(p)) {
            if (d->continua == 0) {
                break;
            }
            switch(p.tipo) {
                case MSG_LOGIN: 
                    processaLogin(d, &p); 
                    break;
                
                case MSG_LOGOUT: 
                    processaLogout(d, p.pid_cliente); 
                    break;
                
                case MSG_PEDIDO_VIAGEM: 
                    processaAgendar(d, &p); 
                    break;
                
                case MSG_PEDIDO_CONSULTA: 
                    processaConsulta(d, &p); 
                    break;
                
                case MSG_CANCELAR_PEDIDO: 
                    processaCancelamentoCliente(d, &p); 
                    break;
                
                default: 
                    printf("[Aviso] Tipo de mensagem desconhecido: %d\n", p.tipo);
                    break;
            }
        }
    }
    return NULL;
}

void mostraAjuda() {
    printf("\n");
    printf("COMANDOS DE ADMINISTRADOR\n");
    printf("utiliz -> Listar utilizadores conectados\n");
    printf("listar -> Listar todas as viagens\n");
    printf("frota -> Ver estado dos veículos ativos\n");
    printf("km -> Total de KMs percorridos pela frota\n");
    printf("hora -> Mostra valor atual do tempo simulado\n");
    printf("cancelar <id> -> Cancelar uma viagem\n");
    printf("terminar -> Encerrar o sistema e fechar clientes\n\n");
}


int main() {
    ControladorData dados;
    pthread_t t_cli, t_rel;
    char cmd[100];

    dados.continua = 1; dados.num_users = 0; dados.num_viagens = 0; 
    dados.tempo_sistema = 0; dados.total_kms_frota = 0;
    pthread_mutex_init(&dados.mutex, NULL);

    char *env = getenv("NVEICULOS");
    dados.max_veiculos = (env) ? atoi(env) : 10;
    dados.veiculos_ativos = 0;

    unlink(SERVER_FIFO); mkfifo(SERVER_FIFO, 0777);
    dados.servidor_fifo_fd = open(SERVER_FIFO, O_RDWR);

    pthread_create(&t_cli, NULL, trataClientes, &dados);
    pthread_create(&t_rel, NULL, trataRelogio, &dados);

    printf("Sistema Iniciado com Sucesso.\n");
    mostraAjuda();

    while(dados.continua) {
        //printf("Inicio > ");
        if(!fgets(cmd, sizeof(cmd), stdin)) break;
        cmd[strcspn(cmd, "\n")] = 0;

        // Comandos ADMIN
        if(strcmp(cmd, "terminar") == 0) {
            pthread_mutex_lock(&dados.mutex);
            printf("\nA cancelar todos os serviços\n");

            // A. Cancelar Viagens (Agendadas e Ativas)
            int existem_ativos = 0;
            for(int i=0; i < dados.num_viagens; i++) {
                if(dados.viagens[i].status == 1) { 
                    kill(dados.viagens[i].pid_veiculo, SIGUSR1); // Manda parar veículo
                    existem_ativos++;
                }
                else if(dados.viagens[i].status == 0) {
                    dados.viagens[i].status = -1; // Cancela agendamento
                    // Avisar cliente que a viagem foi cancelada
                    enviaResposta(dados.viagens[i].pid_cliente, MSG_RESPOSTA_SERVIDOR, 
                                  "Viagem CANCELADA pelo servidor.", 0);
                }
            }
            pthread_mutex_unlock(&dados.mutex);

            // B. Esperar que os veículos parem (Limpeza de processos)
            if (existem_ativos > 0) {
                printf("A aguardar paragem de %d veículos\n", existem_ativos);
                int tentativas = 0;
                while(1) {
                    pthread_mutex_lock(&dados.mutex);
                    int restantes = dados.veiculos_ativos;
                    pthread_mutex_unlock(&dados.mutex);

                    if (restantes <= 0) break;
                    usleep(100000); // 100ms
                    if (++tentativas > 30) break; // Timeout 3s
                }
            }

            // C. Mandar os Clientes saírem (SHUTDOWN)
            printf("A desligar clientes\n");
            pthread_mutex_lock(&dados.mutex);
            for(int i=0; i < dados.num_users; i++) {
                // Envia a palavra-chave "SHUTDOWN"
                enviaResposta(dados.utilizadores[i].pid, MSG_RESPOSTA_SERVIDOR, 
                              "SHUTDOWN: O servidor encerrou.", 0);
            }
            pthread_mutex_unlock(&dados.mutex);

            // D. Terminar Controlador
            printf("A encerrar controlador\n"),
            dados.continua = 0;
            PedidoCliente d = {0}; 
            write(dados.servidor_fifo_fd, &d, sizeof(d));
        }
        else if(strcmp(cmd, "utiliz")==0) {
            pthread_mutex_lock(&dados.mutex);
            printf("--- Utilizadores (%d) ---\n", dados.num_users);
            for(int i=0; i<dados.num_users; i++) printf("> %s\n", dados.utilizadores[i].username);
            pthread_mutex_unlock(&dados.mutex);
        } 
        else if(strcmp(cmd, "listar")==0) {
            pthread_mutex_lock(&dados.mutex);
            printf("--- Viagens Agendadas/Histórico ---\n");
            for(int i=0; i<dados.num_viagens; i++) {
                char *st = (dados.viagens[i].status==0) ? "Agendada" : 
                           (dados.viagens[i].status==1) ? "EmCurso" : 
                           (dados.viagens[i].status==2) ? "Concluida" : "Cancelada";
                printf("ID:%d | T:%d | %s | %dkm\n", dados.viagens[i].id, dados.viagens[i].hora_inicio, st, dados.viagens[i].distancia);
            }
            pthread_mutex_unlock(&dados.mutex);
        }
        else if(strcmp(cmd, "frota")==0) {
            // Requisito: Percentagem do percurso
            pthread_mutex_lock(&dados.mutex);
            printf("--- Estado da Frota (Ativos: %d) ---\n", dados.veiculos_ativos);
            for(int i=0; i<dados.num_viagens; i++) {
                if(dados.viagens[i].status == 1) { // Só viagens em curso
                    printf("Veículo na Viagem %d: %d%% concluído.\n", 
                           dados.viagens[i].id, dados.viagens[i].percentagem_atual);
                }
            }
            pthread_mutex_unlock(&dados.mutex);
        }
        else if(strcmp(cmd, "km")==0) {
            // Requisito: Total de KMs percorridos
            pthread_mutex_lock(&dados.mutex);
            printf("Total KMs Frota: %lld km\n", dados.total_kms_frota);
            pthread_mutex_unlock(&dados.mutex);
        }
        else if(strncmp(cmd, "cancelar", 8)==0) {
            // Requisito: Admin cancela (ID 0 = todos)
            int id;
            if(sscanf(cmd+9, "%d", &id)==1) {
                pthread_mutex_lock(&dados.mutex);
                if(id == 0) {
                    // Cancelar TODOS
                    printf("A cancelar TODAS as viagens\n");
                    for(int i=0; i<dados.num_viagens; i++) {
                        if(dados.viagens[i].status == 0 || dados.viagens[i].status == 1) {
                            coreCancelarViagem(&dados, dados.viagens[i].id, 0, 1); // 1 = is_admin
                        }
                    }
                } else {
                    coreCancelarViagem(&dados, id, 0, 1); // 1 = is_admin, ignora PID
                }
                pthread_mutex_unlock(&dados.mutex);
            } else {
                printf("Erro: cancelar <id> (0 para todos)\n");
            }
        }
        else if(strcmp(cmd, "hora")==0) {
            // Aceder à variável partilhada com segurança
            pthread_mutex_lock(&dados.mutex);
            int t_atual = dados.tempo_sistema;
            pthread_mutex_unlock(&dados.mutex);
            
            printf("Instante atual da simulação: t = %d\n", t_atual);
        }
        else {
            printf("Comando desconhecido.\n ");
        }
    }

    pthread_join(t_cli, NULL);
    pthread_join(t_rel, NULL);
    close(dados.servidor_fifo_fd);
    unlink(SERVER_FIFO);
    pthread_mutex_destroy(&dados.mutex);
    return 0;
}
