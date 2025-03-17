all: comserver comcli

comserver: comserver.c
	$(CC) $(CFLAGS) comserver.c -o comserver

comcli: comcli.c
	$(CC) $(CFLAGS) comcli.c -o comcli

clean:
	rm -f comserver comcli 
