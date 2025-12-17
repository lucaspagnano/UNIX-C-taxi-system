CC = gcc
CFLAGS = -Wall -pthread -g

all: controlador cliente veiculo

controlador: controlador.c common.h
	$(CC) $(CFLAGS) -o controlador controlador.c

cliente: cliente.c common.h
	$(CC) $(CFLAGS) -o cliente cliente.c

veiculo: veiculo.c common.h
	$(CC) $(CFLAGS) -o veiculo veiculo.c

clean:
	rm -f controlador cliente veiculo *.o
