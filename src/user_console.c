//PROJETO SO - User Console
//Cláudia Torres e Maria João Rosa

//COMMAND LINE: ./user_console 1234

//INCLUDES
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "string.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <sys/types.h>
#include <sys/msg.h>

#include <semaphore.h>

//Threads:
#include <pthread.h>
#include <signal.h>

#define BUFFER_SIZE 2048

//Message Queue
typedef struct {
    long mtype;
    char mtext[BUFFER_SIZE];
} MessageQueue;

//VARIABLES
int fd;
int msg_id;

sem_t *my_semaphore;
pthread_t alert_rcv_id;

//FUNCTION
void sigint() {
	printf("\n");
    exit(0);
}

//THREAD ALERT RCV
void *alertRcv(){ return NULL; }
/**
	MessageQueue msg;
	
	int h=2;
	sem_getvalue(my_semaphore, &h);
	
	while(1){
		printf("SEM %d\n\n", h);
		sem_wait(my_semaphore);
		printf("SEM2 %d\n\n", h);
	
		if (msgrcv(msg_id, &msg, sizeof(MessageQueue)-sizeof(long), 1, 0) == -1) {
            perror("Erro no msgrcv");
            sigint();
        }
        
        fflush(stdout);
		printf("WHYYYYYYYY\n");
        printf("%s\n", msg.mtext);
    	
        
	}
	*/

//MAIN
int main(int argc,char *argv[]) {
	
	//COMMAND LINE PROTECTION
    if (argc != 2) {
        printf("Command not valid\n");
        exit(0);
    }
   
    //OPEN CONSOLE_PIPE
    fd = open("/tmp/CONSOLE_PIPE", O_WRONLY);
    
    //PIPE VERIFICATION
    if (fd < 0) {
        perror("Erro ao abrir o named pipe para leitura");
        exit(1);
    }
    
    //MESSAGE QUEUE -------------------------------------------------------------------------------------
    msg_id = msgget(ftok(".", 'a'), 0);
    
    //Verificação:
    if (msg_id < 0) {
        perror("Error: creating message queue.\n");
        sigint();
    }
    
    //SEM
    my_semaphore = sem_open("my_semaphore", 0);
    
    if (my_semaphore == SEM_FAILED) {
    	printf("NOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO TT\n");
    	//exit(0);
	}
    
    //THREAD
    int alert_rcv= pthread_create(&alert_rcv_id, NULL, alertRcv, NULL);
    
    //Verificações:
    if (alert_rcv != 0) {
    	printf("Error: creating alert_rcv thread.\n");
    	sigint();
    }
    
	//MENU ------------------------------------------------------------------------------------------------
	printf("Welcome user %d!\n", atoi(argv[1]));
	printf("\nMENU\n- - - - -\nAvailable commands:\n- sensors\n- stats\n- add_alert [id] [chave] [min] [max]\n- remove_alert [id]\n- list_alerts\n- reset\n- exit\n");
	
	//USER INPUT
    int exit=0;
    char *msg = (char*) malloc(sizeof(char) * BUFFER_SIZE);
    char *buf = (char*) malloc(sizeof(char) * BUFFER_SIZE);
    
    while(exit != 1){
		
		printf("\n- - - - - - - -\nInsert command:\n");
	
        fgets(msg, BUFFER_SIZE, stdin);
        strcpy(buf, msg);
        fflush(stdin);

        char * input[5];
        char * token = strtok(msg, " ");

        int i = 0;
        while (token != NULL) {
            input[i++] = token;
            token = strtok(NULL, " ");
        }
        
        //INPUT VERIFICATIONS
        
        //exit
        if (strcmp(input[0], "exit\n") == 0) {
        	printf("\nSee you next time! :)\n");
            sigint();

        //stats
        } else if (strcmp(input[0], "stats\n") == 0){
            printf("\nStats:\n");
            //listar os stats (fila mensagens)

        //reset
        } else if (strcmp(input[0], "reset\n") == 0){
            //DAR RESET (limpar o log)
            printf("\nSensors statistics reseting...\n");

        //sensors
        } else if (strcmp(input[0], "sensors\n") == 0){
            printf("\nSensors:\n");
            //listar sensores (fila mensagens)

        //add alert
        } else if (strcmp(input[0], "add_alert") == 0){
            //proteção input
            if (strlen(input[1]) < 3 || strlen(input[1]) > 32){
                printf("ID not valid: length out of bounds\n");
                continue;
            }
            else if (strspn(input[1], "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") != strlen(input[1])) {
                printf("ID not valid: can only contain letters and numbers\n");
                continue;
            }
            else{
                //adicionar novo alert
                printf("\nAdding alert...\n");
            }

        //remove alert
        } else if (strcmp(input[0], "remove_alert") == 0){
            //remover alert
            printf("\nRemoving alert...\n");

        //list alerts
        } else if (strcmp(input[0], "list_alerts\n") == 0){
            printf("\nAlerts:\n");
            //listar alerts

        //proteção input
        } else {
            printf("Command not valid\n");
            continue;
        }
        
  		//Escrever pedido para o system_manager (console pipe)
        write(fd, buf, BUFFER_SIZE-1);
        
        //Ler e escrever resposta (veio message queue)
        MessageQueue msg;  
        
        if (msgrcv(msg_id, &msg, sizeof(MessageQueue)-sizeof(long), 1, 0) == -1) {
            perror("Erro no msgrcv");
            sigint();
        }
        
        fflush(stdout);
        
        printf("%s\n", msg.mtext);
    }
    
    //CTRL HANDLER
    signal(SIGINT, sigint); //[FUNÇÃO SIGINT - exit (CTRL-C)]

	pthread_join(alert_rcv_id, NULL);
    printf("THE END TAN TAN TAN\n");
    return 0;
}
