# 🚖 Simulação de Gestão de Frota de Táxis Autónomos

Este projeto consiste na simulação de uma plataforma de gestão de táxis autónomos, desenvolvida em **Linguagem C** para ambiente **Linux/UNIX**. [cite_start]O trabalho foi realizado no âmbito da unidade curricular de **Sistemas Operativos** (2025/2026) do [ISEC - Instituto Superior de Engenharia de Coimbra](https://www.isec.pt).

## 📋 Sobre o Projeto

O objetivo principal é a aplicação prática de mecanismos de **Sistemas Operativos**, focando-se na concorrência, comunicação entre processos (IPC) e sincronização. O sistema segue uma arquitetura Cliente-Servidor distribuída, composta por três módulos distintos:

1.  **Controlador (Servidor):** Gere a frota, os utilizadores e o relógio de simulação.
2.  **Cliente:** Interface para interação do utilizador (agendamento e consulta de viagens).
3.  **Veículo:** Processo simulado que executa as viagens e envia telemetria em tempo real.

## ⚙️ Especificações Técnicas

**Multithreading:** Uso de `pthreads` no Controlador para gestão simultânea de comandos, clientes, relógio e veículos.
**Sincronização:** Proteção de dados partilhados (listas de viagens e utilizadores) utilizando **Mutexes** para garantir exclusão mútua.
**IPC (Comunicação entre Processos):**
    **Named Pipes (FIFOs):** Comunicação bidirecional entre Clientes e Servidor.
    **Pipes Anónimos & Redirecionamento:** Comunicação entre Veículos e Controlador (captura de `stdout`).
    **Sinais:** Uso de `SIGUSR1` para cancelamento de viagens e `SIGINT`/`SIGKILL` para encerramento controlado.
**Gestão de Processos:** Criação dinâmica de processos Veículo via `fork` e `exec`.

## 🚀 Compilação e Execução

### Pré-requisitos
* GCC Compiler
* Ambiente Linux

### Compilação
```bash
make all
```
## 🚀 Como Executar

A ordem de execução é estrita: o **Controlador** deve estar em funcionamento antes de iniciar qualquer **Cliente**.

### 1. Iniciar o Controlador (Servidor)
O sistema assume o valor padrão (10 veículos).

```bash
# Opção A: Execução padrão
./controlador
```
### 2. Iniciar Clientes
Em novos terminais (um para cada utilizador), execute o cliente fornecendo um username único como argumento.

```bash
./cliente <username>

#Exemplo: ./cliente Lucas
```

## 🎮 Comandos Disponíveis

### 🖥️ No Controlador (Administrador)
Estes comandos devem ser introduzidos no terminal onde o `./controlador` está a correr.

| Comando | Descrição |
| :--- | :--- |
| `utiliz` | Lista todos os utilizadores atualmente ligados à plataforma. |
| `listar` | Mostra todas as viagens registadas (Agendadas, Em Curso e Concluídas). |
| `frota` | Apresenta a percentagem de conclusão das viagens que estão a decorrer. |
| `km` | Exibe o total acumulado de quilómetros percorridos por toda a frota. |
| `hora` | Mostra o tempo atual da simulação (em segundos desde o início). |
| `cancelar <id>` | Cancela a viagem com o ID especificado. Use `0` para cancelar todas. |
| `terminar` | Encerra o sistema, desconecta todos os clientes e recolhe os veículos. |

### 📱 No Cliente (Utilizador)
Estes comandos são introduzidos no terminal onde o `./cliente` está a ser executado.

| Comando | Descrição |
| :--- | :--- |
| `agendar <t> <loc> <km>` | Agenda uma viagem para daqui a `t` segundos, de `loc` com `km` de distância. |
| `consultar` | Lista o estado e detalhes das viagens agendadas por este utilizador. |
| `cancelar <id>` | Cancela uma viagem específica previamente agendada. |
| `terminar` | Faz logout do utilizador e fecha a aplicação cliente. |
