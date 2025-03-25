//PROJETO SO - Sensor
//Cláudia Torres e Maria João Rosa

//COMMAND LINE: ./sensor SENS1 3 HOUSETEMP 10 100

//INCLUDES
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h> //[necessário para o sleep()]
#include <signal.h> //[necessário para o SIGTSTP e o SIGINT]
#include <semaphore.h> //[necessário para os semáforos]
#include <fcntl.h>
#include <sys/stat.h>

//COUNTER
int nMessages = 0;
int fd2;

//FUNCTIONS
void sensor(const char* sensorID, int interval, const char* key, int min, int max) {
    
    srand(time(NULL)); //[necessário para rand()]
    
    while (1) {
    	
    	int valor = (rand() % (max - min + 1)) + min;
        char buffer[100];
        snprintf(buffer, 100, "%s#%s#%d\n", sensorID, key, valor);
  
        printf("%s", buffer); //[imprimir dados do sensor]
        write(fd2, buffer, strlen(buffer)); //[escrever para o SENSOR_PIPE]
        
        nMessages++; //[incrementar contador]
        sleep(interval); //[esperar intervalo definido]
    }
}

void sigtstp() {
    printf("\nNumber of messages sent: %d\n", nMessages);
    sem_t mutex_LOG;
    sem_init(&mutex_LOG, 0, 1);
}

void sigint() {
	printf("\n");
	
	//WRITE TO LOG FILE
	sem_t mutex_LOG;
    sem_init(&mutex_LOG, 0, 1);
    
    //CLOSE RESOURCES
    close(fd2); //[fechar o SENSOR_PIPE]
    
    exit(0);
}

//MAIN
int main(int argc, char *argv[]){
	
	//COMMAND LINE PROTECTION
    if (argc != 6) {
        printf("Command not valid\n");
        exit(0);
    }
    
    //OPEN SENSOR_PIPE
    fd2 = open("/tmp/SENSOR_PIPE", O_WRONLY);
    
    //PIPE VERIFICATION
    if (fd2 < 0) {
        perror("Erro ao abrir o named pipe para leitura");
        exit(1);
    }
    
    //ATTRIBUTES
    char *sensorID = argv[1];
    int interval = atoi(argv[2]);
    char *key = argv[3];
    int min = atoi(argv[4]);
    int max = atoi(argv[5]);


    //INPUT PROTECTION
    if (strspn(sensorID, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") != strlen(sensorID)) {
    	printf("SENSOR ID not valid: can only contain letters and numbers\n");
    	exit(0);
    }
    else if (strspn(key, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != strlen(key)) {
    	printf("KEY not valid: can only contain letters, numbers and underscore\n");
    	exit(0);
    }
    else if (strlen(sensorID) < 3 || strlen(sensorID) > 32) {
        printf("SENSOR ID not valid: length out of bounds\n");
        exit(0);
    }
    else if (strlen(key) < 3 || strlen(key) > 32){
        printf("KEY not valid: length out of bounds\n");
        exit(0);
    }
    else if (interval < 0){
        printf("RANGE not valid: time out of bounds\n");
        exit(0);
    }
    
    //CTRL HANDLER
    signal(SIGTSTP, sigtstp); //[FUNÇÃO SIGTSTP - counter (CTRL-Z)]
    
    signal(SIGINT, sigint); //[FUNÇÃO SIGINT - exit (CTRL-C)]
    
    //SENSOR    
    sensor(sensorID, interval, key, min, max); //[FUNÇÃO SENSOR - generates data]

    return 0;
}
