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
    char cliente_fifo[50]; // caminho do ficheiro
    RespostaServidor resp; // estrutura que vamos enviar
    sprintf(cliente_fifo, CLIENT_FIFO_FMT, pid_cliente); // usamos formato definido em common.h e subs %d por PID
    memset(&resp, 0, sizeof(resp)); // preenche inicialmente tudo a 0
    resp.tipo = tipo; 
    resp.sucesso = sucesso; 
    strncpy(resp.mensagem, msg, sizeof(resp.mensagem) - 1); //preencher estrutura (usa strncpy para limitar buffer)
    
    int fd = open(cliente_fifo, O_WRONLY); // abre named pipe do cliente
    if (fd != -1) {
        write(fd, &resp, sizeof(resp)); // envia a estrutura pelo named pipe 
        close(fd); // fecha named pipe para servidor nao crashar
    }
}

void processaLogin(ControladorData *dados, PedidoCliente *p) {
    pthread_mutex_lock(&dados->mutex); // trancar acessso aos dados 
    for(int i=0; i<dados->num_users; i++) {
        if(strcmp(dados->utilizadores[i].username, p->dados) == 0) { // se user existir
            pthread_mutex_unlock(&dados->mutex); // fax unlock
            enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, "User ja existe", 0); // enviar mensagem ao cliente
            return;
        }
    }
    if(dados->num_users < MAX_USERS) { // se houver espaco
        Utilizador *u = &dados->utilizadores[dados->num_users++]; // estrutura auxiliar para prox pos e incrementa num_users logo a seguir 
        strcpy(u->username, p->dados); // copia nome
        u->pid = p->pid_cliente; // copia pid
        u->ocupado = 0; 
        printf("Login: %s entrou.\n", u->username);
        enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, "Login Aceite", 1); // confirma ao cliente que entrou
    } else {
        enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, "Servidor Cheio", 0); 
    }
    pthread_mutex_unlock(&dados->mutex); // destranca mutex para que outras threads trabalhem
}

void processaLogout(ControladorData *dados, pid_t pid) {
    pthread_mutex_lock(&dados->mutex); // tranca mutex 
    for(int i=0; i<dados->num_users; i++) {
        if(dados->utilizadores[i].pid == pid) { // percorre lista ate encontrar cliente 
            printf("Logout: %s saiu.\n", dados->utilizadores[i].username); // log para admin ver qm saiu
            dados->utilizadores[i] = dados->utilizadores[dados->num_users-1]; // copiamos o ultimo para a posicao que sai 
            dados->num_users--; // decrementa num clientes
            break;
        }
    }
    pthread_mutex_unlock(&dados->mutex); // detranca mutex
}

void processaAgendar(ControladorData *dados, PedidoCliente *p) {
    //cliente envia tudo numa linha
    //aqui declaramos variaveis para guardar info 
    int segundos, dist;
    char local[50];
    
    if (sscanf(p->dados, "%d %s %d", &segundos, local, &dist) != 3) { // parte a string e verifica se conseguiu ler as 3 infos
        enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, "Erro: agendar <segs> <local> <dist>", 0); // devolve erro ao cliente 
        return;
    }

    pthread_mutex_lock(&dados->mutex); // tranca mutex 
    if (dados->num_viagens < MAX_VIAGENS) { //verifica se ta no limite de viagens 
        Viagem *v = &dados->viagens[dados->num_viagens++]; // ponteiro pra prox posicao e incrementa num_viagens logo a seguir 
        v->id = dados->num_viagens; // atribui ID sequencial da viagem 
        v->pid_cliente = p->pid_cliente; // para saber quem avisar quando taxi chegar
        v->hora_inicio = dados->tempo_sistema + segundos; // calculo do instante de inicio 
        v->distancia = dist; // copia distancia
        strcpy(v->origem, local); // copia local
        v->status = 0; // status 0 - agendado (1 - em curso \ 2 - concluida)
        v->percentagem_atual = 0; // percentagem comeca a 0 
        
        char msg[100];
        sprintf(msg, "Viagem agendada para t=%d", v->hora_inicio);
        enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, msg, 1); // envia resposta ao cliente 
        printf("Viagem %d agendada para t=%d\n", v->id, v->hora_inicio); // avisa admin
    } else {
        enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, "Lista cheia", 0);
    }
    pthread_mutex_unlock(&dados->mutex); // destranca mutex
}

void processaConsulta(ControladorData *dados, PedidoCliente *p) {
    pthread_mutex_lock(&dados->mutex); // tranca mutex 
    char buffer_total[2048] = "\n--- As suas Viagens ---\n";
    char linha[200];
    int count = 0;

    for (int i = 0; i < dados->num_viagens; i++) {
        if (dados->viagens[i].pid_cliente == p->pid_cliente) { // verificamos em todas as viagens quais sao do cliente 
            char *st = (dados->viagens[i].status==0) ? "Agendada" : 
                       (dados->viagens[i].status==1) ? "Em Curso" : 
                       (dados->viagens[i].status==2) ? "Concluida" : "Cancelada"; // copia estado da viagem 
            sprintf(linha, "ID: %d | Orig: %s | Hora: %d | Estado: %s\n", 
                    dados->viagens[i].id, dados->viagens[i].origem, dados->viagens[i].hora_inicio, st); // imprime para a string linha
            strcat(buffer_total, linha); // concatena para buffer
            count++; // incrementa contador num de viagens do cliente 
        }
    }
    if(count==0) strcat(buffer_total, "Nenhuma viagem.\n");
    strcat(buffer_total, "-----------------------");
    enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, buffer_total, 1); //  envia a resposta ao cliente 
    pthread_mutex_unlock(&dados->mutex); //  destranca mutex 
}

int coreCancelarViagem(ControladorData *dados, int id, pid_t pid_requerente, int is_admin) {
    int index = -1;
    for (int i = 0; i < dados->num_viagens; i++) {
        if (dados->viagens[i].id == id && (is_admin || dados->viagens[i].pid_cliente == pid_requerente)) { // procuramos a viagem na lista
            index = i;
            break;
        }
    }

    if (index == -1) return 0;

    if (dados->viagens[index].status == 0) { // ainda nao houve fork - carro nao saiu
        dados->viagens[index].status = -1; // mudamos estado da viagem para cancelada, assim relogio chega na hora e ignora
        printf("Viagem %d cancelada.\n", id); 
        return 1;
    } 
    else if (dados->viagens[index].status == 1) { // carro ja existe 
        kill(dados->viagens[index].pid_veiculo, SIGUSR1); //  enviamos sinal ao processo do veiculo 
        dados->viagens[index].status = -1; // mudar estado da viagem
        printf("Viagem %d interrompida (SIGUSR1 enviado).\n", id);
        return 1;
    }
    return 0;
}

void processaCancelamentoCliente(ControladorData *dados, PedidoCliente *p) {
    int id; // guardar num da viagem 
    if (sscanf(p->dados, "%d", &id) != 1) { // tenta extrair o inteiro
        enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, "Use: cancelar <ID>", 0); //   envia resposta ao cliente 
        return;
    }
    pthread_mutex_lock(&dados->mutex); // tranca mutex
    if (coreCancelarViagem(dados, id, p->pid_cliente, 0)) { // invoca funcao coreCancelar indicando que nao eh admin
        enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, "Viagem cancelada com sucesso.", 1); // avisamos cliente que correu bem (core devolveu 1)
    } else {
        enviaResposta(p->pid_cliente, MSG_RESPOSTA_SERVIDOR, "Viagem não encontrada ou concluída.", 0); // core devolveu 0
    }
    pthread_mutex_unlock(&dados->mutex); // destrancamos mutex 
}

// Thread Leitura Telemetria
void *trataVeiculo(void *arg) { // fica colada no pipe anonimo a ler tudo que o veiculo escreve no ecra e vai transformando em dados na estrutura viagens
    ArgsTelemetria *args = (ArgsTelemetria *)arg; // convertemos void para estrutura ArgsTelemetria
    char buf[256]; 
    int perc;
    int n;
    char ch;
    int pos = 0;

    while ((n = read(args->fd, &ch, 1)) > 0) { 
        if (ch == '\n') {
            buf[pos] = '\0';
            
            if (sscanf(buf, "%*d PERCENTAGEM %d", &perc) == 1) { // ignora primeiro numero e captura a percentagem
                pthread_mutex_lock(&args->dados->mutex); // trancamos mutex
                args->dados->viagens[args->index_viagem].percentagem_atual = perc; // atualizamos percentagem
                pthread_mutex_unlock(&args->dados->mutex); // destrancamos mutex
            }

            if (strstr(buf, "CONCLUIDO") != NULL) { // detecta a palavra chave concluido 
                pthread_mutex_lock(&args->dados->mutex); // trancamos mutex
                // atualizar dados globais
                args->dados->viagens[args->index_viagem].status = 2;
                args->dados->veiculos_ativos--;
                args->dados->total_kms_frota += args->dados->viagens[args->index_viagem].distancia;
                
                printf("Viagem %d concluída. KMs acumulados: %lld\n", 
                       args->dados->viagens[args->index_viagem].id, args->dados->total_kms_frota);
                pthread_mutex_unlock(&args->dados->mutex);
            }
            else if (strstr(buf, "CANCELADO") != NULL || strstr(buf, "ERRO") != NULL) { 
                pthread_mutex_lock(&args->dados->mutex); // trancamos mutex
                args->dados->viagens[args->index_viagem].status = -1; // atualiza estado 
                args->dados->veiculos_ativos--; // decrementa contagem
                pthread_mutex_unlock(&args->dados->mutex); // destrancamos mutex
            }
            
            pos = 0; // reset buffer para ler proxima linha
        } else {
            if (pos < sizeof(buf) - 1) {
                buf[pos++] = ch; // acumula caracteres em buf 
            }
        }
    }
    
    close(args->fd); // fecha ponta da leitura de pipe 
    free(args); // libera memoria 
    return NULL;
}

void lancaVeiculo(ControladorData *dados, int idx) {
    int pfd[2]; // pfd[0] - extremidade de leitura \ pai ; pfd[1] - extremidade de escrita \ filho
    pipe(pfd); // cria um canal unidirecional
    pid_t pid = fork(); // a partir daqui se pid==0 estamos no codigo do filho e 1 no codigo do pai

    if (pid == 0) { // codigo do filho 
        close(pfd[0]); // filho fecha leitura
        dup2(pfd[1], STDOUT_FILENO); // tudo que for escrito no ecra vai para a entrada do pipe pfd[1]
        close(pfd[1]); // fecha o original
        char sid[10], sdist[10], spid[10]; 
        //recolhe dados
        sprintf(sid, "%d", dados->viagens[idx].id);
        sprintf(sdist, "%d", dados->viagens[idx].distancia);
        sprintf(spid, "%d", dados->viagens[idx].pid_cliente);
        execl("./veiculo", "veiculo", sid, sdist, spid, NULL); // executa filho
        exit(1);
    } else { 
        close(pfd[1]); // pai nao escreve no pipe 
        dados->viagens[idx].status = 1; // altera estado - em curso
        dados->viagens[idx].pid_veiculo = pid; // guarda pid para KILL SIGUSR1
        dados->viagens[idx].percentagem_atual = 0;
        dados->veiculos_ativos++; 
        
        printf("Taxi enviado (Viagem %d). Frota: %d/%d\n", 
               dados->viagens[idx].id, dados->veiculos_ativos, dados->max_veiculos);

        //criacao da thread dinamica 
        pthread_t t;
        ArgsTelemetria *args = malloc(sizeof(ArgsTelemetria)); // alocamos memoria para os args 
        args->fd = pfd[0]; args->index_viagem = idx; args->dados = dados;
        pthread_create(&t, NULL, trataVeiculo, args);
        pthread_detach(t); // utilizamos detach para o taxi correr de fundo e depois morrer automaticamente 
    }
}

// Thread Relógio
void *trataRelogio(void *arg) {
    ControladorData *dados = (ControladorData*)arg; // transforma o void em ControladorData
    while(dados->continua) { // quando o admin escreve termina, continua=0, thread termina suavemente 
        sleep(1);
        pthread_mutex_lock(&dados->mutex); //trancamos mutex
        dados->tempo_sistema++; //  incrementamos instante 
        for(int i=0; i<dados->num_viagens; i++) {
            if(dados->viagens[i].status == 0 && dados->viagens[i].hora_inicio <= dados->tempo_sistema) { // verificacao na lista de viagens 
                if (dados->veiculos_ativos < dados->max_veiculos) { // verificacao maximo de veiculos 
                    lancaVeiculo(dados, i); // chamamos lancaVeiculo \ fork exec
                } else {
                    dados->viagens[i].status = -1; // cancela viagem 
                    pthread_mutex_unlock(&dados->mutex); // destrancamos mutex
                    enviaResposta(dados->viagens[i].pid_cliente, MSG_RESPOSTA_SERVIDOR, "Cancelada: Frota cheia", 0); // enviamos ao cliente 
                    pthread_mutex_lock(&dados->mutex); // trancamos novamente logo a seguir para continuar no for 
                }
            }
        }
        pthread_mutex_unlock(&dados->mutex); // destrancamos mutex
    }
    return NULL;
}

// Thread Clientes
void *trataClientes(void *arg) {
    ControladorData *d = (ControladorData*)arg; // transforma void em ControladorData 
    PedidoCliente p; // para guardar dados que chegam do tubo 

    printf("Thread Clientes à escuta de pedidos\n"); // avisamos admin que thread cliente esta funcionando

    while(d->continua) {
        if(read(d->servidor_fifo_fd, &p, sizeof(p)) == sizeof(p)) { // a thread para aqui ate que alguem escreva no pipe 
            if (d->continua == 0) {
                break;
            }
            switch(p.tipo) { // encaminha pedido para as funcoes 
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
    ControladorData dados; // memoria global partilhada do programa - passamos & para restantes threads
    pthread_t t_cli, t_rel;
    char cmd[100];

    dados.continua = 1; dados.num_users = 0; dados.num_viagens = 0; 
    dados.tempo_sistema = 0; dados.total_kms_frota = 0;
    pthread_mutex_init(&dados.mutex, NULL); // inicializamos mutex antes de criar qualquer thread 

    char *env = getenv("NVEICULOS"); // lemos variavel do ambiente 
    dados.max_veiculos = (env) ? atoi(env) : 10; // se nao existir forçamos 10 
    dados.veiculos_ativos = 0;

    unlink(SERVER_FIFO); mkfifo(SERVER_FIFO, 0777);
    dados.servidor_fifo_fd = open(SERVER_FIFO, O_RDWR); // abrimos pipe RDWR para nao terminar sem clientes

    // aqui criamos as proxs threads e o processo deixa de ser single-thread e passa a ter 3 fluxos de execucao em paralelo
    pthread_create(&t_cli, NULL, trataClientes, &dados);
    pthread_create(&t_rel, NULL, trataRelogio, &dados);

    printf("Sistema Iniciado com Sucesso.\n");
    mostraAjuda();

    while(dados.continua) {
        //printf("Inicio > ");
        if(!fgets(cmd, sizeof(cmd), stdin)) break;
        cmd[strcspn(cmd, "\n")] = 0; // tirar \n do fgets

        // Comandos ADMIN
        if(strcmp(cmd, "terminar") == 0) {
            pthread_mutex_lock(&dados.mutex); // tranca mutex para iniciar encerramento
            printf("\nA cancelar todos os serviços\n");

            // A. Cancelar Viagens (Agendadas e Ativas)
            int existem_ativos = 0;
            for(int i=0; i < dados.num_viagens; i++) {
                if(dados.viagens[i].status == 1) { // taxis que estao na estrada
                    kill(dados.viagens[i].pid_veiculo, SIGUSR1); // Manda parar veículo
                    existem_ativos++;
                }
                else if(dados.viagens[i].status == 0) { // taxis agendados
                    dados.viagens[i].status = -1; // Cancela agendamento
                    // Avisar cliente que a viagem foi cancelada
                    enviaResposta(dados.viagens[i].pid_cliente, MSG_RESPOSTA_SERVIDOR, 
                                  "Viagem CANCELADA pelo servidor.", 0);
                }
            }
            pthread_mutex_unlock(&dados.mutex); // destrancar mutex 

            // B. Esperar que os veículos parem (Limpeza de processos)
            if (existem_ativos > 0) {
                printf("A aguardar paragem de %d veículos\n", existem_ativos);
                int tentativas = 0;
                while(1) {
                    pthread_mutex_lock(&dados.mutex);
                    int restantes = dados.veiculos_ativos; // le constantemente veiculos_ativos (decrementado em trataVeiculos)
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
                // Envia a palavra-chave "SHUTDOWN" - ativa KILL
                enviaResposta(dados.utilizadores[i].pid, MSG_RESPOSTA_SERVIDOR, 
                              "SHUTDOWN: O servidor encerrou.", 0);
            }
            pthread_mutex_unlock(&dados.mutex);

            // D. Terminar Controlador
            printf("A encerrar controlador\n"),
            dados.continua = 0; // terminar o while(continua)
            PedidoCliente d = {0}; 
            write(dados.servidor_fifo_fd, &d, sizeof(d)); // escreve lixo pra thread trataClientes acordar e ver continua=0
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
