//PROJETO SO - File System
//Cláudia Torres e Maria João Rosa


//INCLUDES
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <semaphore.h>


//READ FROM CONFIG FILE - - - - - - - - - - - - - - - - - - -


void read_file(FILE *f, int* QUEUE_SZ, int* N_WORKERS, int* MAX_KEYS, int* MAX_SENSORS, int* MAX_ALERTS) {
    
    int i = 0;
    int numbers[5];
    char buffer[256];
	
	//Ler o ficheiro
    while (fgets(buffer, sizeof(buffer), f)) {
        char* endptr;
        int value = strtol(buffer, &endptr, 10);
        
        //Verificações:
        if (*endptr != '\n' && *endptr != '\0') {
        	//Ver se só temos números
            printf("Error: invalid item on line %d: %s.\n", i+1, buffer);
            exit(0);
        } else if ((value < 1 && i != 4) || (value < 0 && i == 4)){
        	//Ver se as variaveis estivão no range certo
            printf("Error: invalid value on line %d: %d.\n", i+1, value);
            exit(0);
        } else {
        	//Se estiver tudo correto
            numbers[i] = value;
        }

        i++;
    }
	
	//Dar valores ás variáveis
    *QUEUE_SZ = numbers[0];
    *N_WORKERS = numbers[1];
    *MAX_KEYS = numbers[2];
    *MAX_SENSORS = numbers[3];
    *MAX_ALERTS = numbers[4];

}


//WRITE TO/CLEAN LOG FILE - - - - - - - - - - - - - - - - - - - - -


void escreverLOG(FILE *LOG, const char* mensagem, sem_t* sem_LOG) {
    
    //Pôr semáforo do log em espera
    sem_wait(sem_LOG);
    
    //Obter a data e hora atual
    time_t now = time(NULL);
    struct tm *informacaoTime = localtime(&now);
    char formatoTime[100];
    strftime(formatoTime, sizeof(formatoTime), "%H:%M:%S", informacaoTime);
    
    //Escrever no ficheiro log
    fprintf(LOG, "%s %s\n", formatoTime, mensagem);
    
    //Tirar semáforo do log de espera
    sem_post(sem_LOG);

}
