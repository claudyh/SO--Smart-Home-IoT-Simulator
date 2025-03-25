//PROJETO SO - System Manager
//Cláudia Torres e Maria João Rosa

//COMMAND LINE: ./home_iot Config.txt


//INCLUDES////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <errno.h>

//Shared memory
#include <sys/shm.h>

//Processes
#include <sys/wait.h>

//Semaphores
#include <semaphore.h>

//Threads:
#include <pthread.h>
#include <signal.h>

//Pipes:
#include <fcntl.h>
#include <sys/stat.h>

//Message Queue:
#include <sys/types.h>
#include <sys/msg.h>

//Header files:
#include "files.h"

//DEFINE
#define BUFFER_SIZE 2048


//STRUCTS/////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    char id[32];
    char key[32];
    int value;
} Sensor;

typedef struct {
    char key[32];
    int last_value;
    int min_value;
    int max_value;
    double avg_value;
    int key_usage;
} Data;

typedef struct {
    char id[32];
    char key[32];
    int min_value;
    int max_value;
} Alert;

//Message Queue
typedef struct {
    long mtype;
    char mtext[BUFFER_SIZE];
} MessageQueue;

//Internal Queue
typedef struct Message {
    int type;
    char text[BUFFER_SIZE];
    struct Message *next;
} Message;

//Worker process
typedef struct {
    int id;
    int state;
    int pipefd[2]; // unnamed pipe file descriptors
} Worker;


//VARIABLES//////////////////////////////////////////////////////////////////////////////////////////////

int QUEUE_SZ=0, N_WORKERS=0, MAX_KEYS=0, MAX_SENSORS=0, MAX_ALERTS=0;
char buffer[BUFFER_SIZE];
int msg_id;

FILE* f;
FILE *LOG;

//Mutex and Cond Variable
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t cond_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_iqueue;

//Internal Queue:
Message *head = NULL;

//Semáforos:
sem_t *sem_LOG;
sem_t *sem_worker;
sem_t *sem_awatcher;
sem_t *worker_shm;
sem_t *sem_sensors;
sem_t *sem_data;
sem_t *sem_alerts;
sem_t *sem_iqueue;
sem_t *sem_last_sensor;

sem_t *my_semaphore;

//Arrays e counters:
Worker *workers;
    
Sensor *sensors; // array de sensores
Data *sensor_data; // array dos dados dos sensores
Alert *alert_rules; // array de regras das alertas
    
int *n_sensors; // número de sensores
int *n_sensor_data; // número de dados de sensores
int *n_alert_rules; // número de regras de alertas

Data *last_sensor;

//Memória partilhada:
int shmid; //id
void *shm; //apontador

//Threads:
pthread_t sensorReader_id;
pthread_t consoleReader_id;
pthread_t dispatcher_id;


//FUNCTIONS/////////////////////////////////////////////////////////////////////////////////////////////

//Internal Queue
void addMessage(Message **head, int type, char *text) {
	//printf("entramos no add message\n");
	pthread_mutex_lock(&queue_mutex);

    Message *newMessage = (Message*) malloc(sizeof(Message));
    newMessage->type = type;
    strcpy(newMessage->text, text);
    newMessage->next = NULL;    
    
    if (*head == NULL) {
        // Lista vazia
        *head = newMessage;
    } else {
        Message *current = *head;
        Message *lastOfType1 = NULL;

        // Encontrar o último nó do tipo 1 e o último nó da lista
        while (current != NULL) {
            if (current->type == 1) {
                lastOfType1 = current;
            }
            if (current->next == NULL) {
                break;
            }
            current = current->next;
        }

        // Adicionar o novo nó no final da sequência de mensagens do tipo 1
        if (type == 1) {
            if (lastOfType1 == NULL) {
                // Se não houver mensagens do tipo 1, adiciona no início da lista
                newMessage->next = *head;
                *head = newMessage;
            } else {
                newMessage->next = lastOfType1->next;
                lastOfType1->next = newMessage;
            }
        } else {
            // Adicionar o novo nó no final da lista
            current->next = newMessage;
        }
    }
    
    sem_post(sem_iqueue);
    pthread_mutex_unlock(&queue_mutex);
}

void removeMessage(Message **head) {
    if (*head != NULL) {
        Message *temp = *head;
        *head = (*head)->next;
        free(temp);
    }
}

int isFull(Message *head) {
    int count = 0;
    Message *current = head;

    // Count the number of nodes in the queue
    while (current != NULL) {
        count++;
        current = current->next;
    }

    // Check if the count exceeds the maximum queue size
    if (count >= QUEUE_SZ) {
        return 1; // Queue is full
    } else {
        return 0; // Queue is not full
    }
}


//SIGINT//////////////////////////////////////////////////////////////////////////////////////////////////

void sigint() {
	printf("\n");
	escreverLOG(LOG, "SIGNAL SIGINT RECEIVED\n", sem_LOG);
	
	shmdt(shm);
	shmctl(shmid, IPC_RMID, NULL);
	
	//Eliminar message queue
	msgctl(msg_id, IPC_RMID, NULL);
    
    //Unlink pipes
    if (unlink("/tmp/CONSOLE_PIPE") == 0) {
        printf("Console pipe deleted\n");
    }
    
    if (unlink("/tmp/SENSOR_PIPE") == 0) {
        printf("Sensor pipe deleted\n");
    }
	
	pthread_cond_destroy(&cond_iqueue);
    
    //Closing semaphores
    sem_close(sem_LOG);
	sem_close(sem_worker);
	sem_close(worker_shm);
	sem_close(sem_awatcher);
	sem_close(sem_sensors);
	sem_close(sem_data);
	sem_close(sem_alerts);
	sem_close(sem_iqueue);
	sem_close(sem_last_sensor);
	sem_close(my_semaphore);

	sem_unlink("sem_LOG");
	sem_unlink("sem_worker");
	sem_unlink("worker_shm");
	sem_unlink("sem_awatcher");
	sem_unlink("sem_sensors");
	sem_unlink("sem_data");
	sem_unlink("sem_alerts");
	sem_unlink("sem_worker");
	sem_unlink("sem_last_sensor");
	sem_unlink("my_semaphore");
    
    //CLOSE FILES ---------------------------------------------------------------------------------------
    fclose(f);
    fclose(LOG);
	
    exit(0);
}


//UPDATE DATA//////////////////////////////////////////////////////////////////////////////////////////////

void update_data(FILE *LOG, char *buffer) {

    //Splittar o buffer por #
    char *sensor_id = strtok(buffer, "#");
    char *key = strtok(NULL, "#");
    int value = atoi(strtok(NULL, "#"));
    
    sem_wait(sem_data);
    sem_wait(sem_sensors);
    sem_wait(sem_last_sensor);

    //Procurar o id no array sensor_data
    int i;
    for (i = 0; i < *n_sensor_data; i++) {
    	
    	//Se existir
        if (strcmp(sensor_data[i].key, key) == 0) {
        	
        	strcpy(last_sensor->key, key);
        	
        	//Updatar parametros dos dados deste sensor
   			sensor_data[i].last_value = value;
   			last_sensor->last_value=value;
   			
    		if(value < sensor_data[i].min_value){
    			sensor_data[i].min_value = value;
    			last_sensor->min_value=value;
    		}
    		if(value > sensor_data[i].max_value){
    			sensor_data[i].max_value = value;
    			last_sensor->max_value=value;
    		}
    		sensor_data[i].avg_value = ((sensor_data[i].avg_value*sensor_data[i].key_usage) + (value))/(sensor_data[i].key_usage + 1);
    		last_sensor->avg_value = ((sensor_data[i].avg_value*sensor_data[i].key_usage) + (value))/(sensor_data[i].key_usage + 1);
    		
    		sensor_data[i].key_usage++;
    		last_sensor->key_usage=sensor_data[i].key_usage;
    		
    		
    		printf("Dados do sensor updated.\n");
    		
            break;
        }
    }
    
    //Se não existir
    if (i == *n_sensor_data) {
    	
    	//Se n_data ainda não tiver chegado ao max ???????????????????????????
    	if(*n_sensor_data < MAX_KEYS){
    		//Se n_sensors ainda não tiver chegado ao max
    		if(*n_sensors < MAX_SENSORS){
    		
        		//Criar um novo sensor
        		strcpy(sensors[*n_sensors].id, sensor_id);
				strcpy(sensors[*n_sensors].key, key);
				sensors[*n_sensors].value = value;
				
				printf("Novo sensor criado.\n");
				
				//Inserir os dados do novo sensor
        		strcpy(sensor_data[*n_sensor_data].key, key);
				sensor_data[*n_sensor_data].last_value = value;
				sensor_data[*n_sensor_data].min_value = value;
				sensor_data[*n_sensor_data].max_value = value;
				sensor_data[*n_sensor_data].avg_value = value;
				sensor_data[*n_sensor_data].key_usage = 1;
				
				strcpy(last_sensor->key, key);
				last_sensor->last_value = value;
				last_sensor->min_value = value;
				last_sensor->max_value = value;
				last_sensor->avg_value = value;
				last_sensor->key_usage = 1;
				
    			printf("Dados do novo sensor criados.\n");
    		
        		//Incrementar tamanho arrays
				(*n_sensor_data)++;
				(*n_sensors)++;

        	}
        	//Se não houver espaço para sensores
        	else{
        		escreverLOG(LOG, "Atenção: limite de sensores atingido.\n", sem_LOG);
        		printf("Atenção: limite de sensores atingido.\n");
        	}
        }
        //Se o sensor não tiver mais espaço para dados
        else{
        	escreverLOG(LOG, "Atenção: limite de chaves atingido.\n", sem_LOG);
        	printf("Atenção: limite de chaves atingido.\n");
        	}
    }
    
    sem_post(sem_data);
    sem_post(sem_sensors);
    sem_post(sem_last_sensor);
    
    sem_post(sem_awatcher); //Sempre que adiciona um sensor, checamos se ele vai dar um alerta

}


//SENSOR READER////////////////////////////////////////////////////////////////////////////////////////////

void *sensorReader(){

	//Abrir o named pipe para leitura (CONSOLE_PIPE)
    int fd2 = open("/tmp/SENSOR_PIPE", O_RDONLY);
    
    //Verificação:
    if (fd2 < 0) {
        perror("Erro ao abrir o named pipe para leitura");
        sigint();
    }
    
    //Ler do named pipe e escrever para internal queue
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        
        if (read(fd2, buffer, sizeof(buffer)) <= 0) {
            
            fd2 = open("/tmp/SENSOR_PIPE", O_RDONLY);
    
    		//Verificação:
    		if (fd2 < 0) {
        		perror("Erro ao abrir o named pipe para leitura");
        		sigint();
    		}		
        } else{
        	//Escrever na internal queue
        	addMessage(&head, 2, buffer);
        }
        
    }
    
    // fechar o named pipe
    close(fd2);
}


//CONSOLE READER////////////////////////////////////////////////////////////////////////////////////////////

void *consoleReader(){
    
    //Abrir o named pipe para leitura (CONSOLE_PIPE)
    int fd = open("/tmp/CONSOLE_PIPE", O_RDONLY);
    
    //Verificação:
    if (fd < 0) {
        perror("Erro ao abrir o named pipe para leitura");
        sigint();
    }
    
    //Ler do named pipe e escrever para stdout
    while (1) {
    
        memset(buffer, 0, strlen(buffer));
        
        if (read(fd, buffer, BUFFER_SIZE-1) <= 0) {
            
            fd = open("/tmp/CONSOLE_PIPE", O_RDONLY);
    
    		//Verificação:
    		if (fd < 0) {
        		perror("Erro ao abrir o named pipe para leitura");
        		sigint();
    		}  
        } else{
        
        	pthread_mutex_lock(&cond_mutex);
        
        	//Se a internal queue estiver cheia
        	while(isFull(head) == 1){
        		pthread_cond_wait(&cond_iqueue, &cond_mutex);
        	}
        
        	pthread_mutex_unlock(&cond_mutex);
        
        	//Escrever na internal queue
        	addMessage(&head, 1, buffer);
        }    
    }
    
    // fechar o named pipe
    close(fd);
}


//DISPATCHER//////////////////////////////////////////////////////////////////////////////////////////////

void *dispatcher(void *arg) {
    Worker *workers = (Worker *)arg;
	//printf("id: %d e state: %d\n",0,workers[0].state);
    char *msg = (char*) malloc(sizeof(char) * BUFFER_SIZE);

    while (1) {
    
        // Verificar se há alguma mensagem na fila interna
        sem_wait(sem_iqueue);
        
        pthread_mutex_lock(&queue_mutex);
              
        msg = head->text;
        removeMessage(&head);
        
        //Internal queue já não está full
        pthread_mutex_lock(&cond_mutex);
        
        pthread_cond_signal(&cond_iqueue);
        
        pthread_mutex_unlock(&cond_mutex);
        
        pthread_mutex_unlock(&queue_mutex);
        
        if (strchr(msg, '\n')) *strchr(msg, '\n') = '\0';
        //printf("\nDISPATCHER LEU %s DA INTERNAL QUEUE\n", msg);
        	
        // Encontrar o próximo Worker livre
        sem_wait(sem_worker);
        sem_wait(worker_shm);
        for (int i = 0; i < N_WORKERS; i++) {
        	//printf("id: %d e state: %d\n",i,workers[i].state);
            if (workers[i].state == 0) {
            	// Worker livre encontrado, enviar a mensagem
                //printf("\nA marcar o worker como ocupado...\n");
                workers[i].state = 1; // Marcar o Worker como ocupado
                //printf("worker id %d e estado %d\n", i,workers[i].state);
                write(workers[i].pipefd[1], msg, BUFFER_SIZE-1);
                //printf("Escrever %s po unnamed pipe",msg);
                break;
            
            }
         }
             
         sem_post(worker_shm);
    }
	
	free(msg);
    pthread_exit(NULL);
}


//WORKER//////////////////////////////////////////////////////////////////////////////////////////////////

void* worker(void* arg, Worker* workers, FILE *LOG) {
    int id = *((int*)arg);
    char *msg = (char*) malloc(sizeof(char) * BUFFER_SIZE);
    char firstWord[BUFFER_SIZE];
    
    int n;
    MessageQueue final_msg; //Mensagem a enviar para a MsgQueue
    final_msg.mtype = 1; //tipo - pomos o id da user console

    while (1) {
        // Ler a mensagem do unnamed pipe
        n = read(workers[id].pipefd[0], msg, BUFFER_SIZE-1);
        
        if (n <= 0) {
            // Ocorreu um erro na leitura do pipe
            printf("Erro ao ler do unnamed pipe.\n");
            sigint();
        }
        
        strcpy(firstWord, msg);
        
        // Processar a mensagem
        printf("\nWorker %d processando a mensagem: %s\n", id, msg);
               
        //Se a mensagem vier de sensores:
        if (strncmp(msg, "SENS", 4) == 0) {
            update_data(LOG, msg);
        }
        //Se a mensagem vier da consola:
        else if (strcmp(msg, "stats") == 0) {
        	char s_data[BUFFER_SIZE];
            
            sem_wait(sem_data);
            
            strcpy(final_msg.mtext, "Key Last Min Max Avg Count\n");

            for (int i = 0; i < *n_sensor_data; i++) {
            	sprintf(s_data, "%s %d %d %d %.2lf %d\n", sensor_data[i].key, sensor_data[i].last_value, sensor_data[i].min_value, sensor_data[i].max_value, sensor_data[i].avg_value, sensor_data[i].key_usage);
            	
            	strcat(final_msg.mtext, s_data);
            
            }
            
            sem_post(sem_data);
			
			//Enviar para a message queue
        	if (msgsnd(msg_id, &final_msg, sizeof(MessageQueue)-sizeof(long), 0) == -1) {
            	perror("Erro no msgsnd");
            	sigint();
        	}
        }
        else if (strcmp(msg, "reset") == 0) {
            
            sem_wait(sem_data);
            sem_wait(sem_sensors);
            
            for (int i = 0; i < *n_sensor_data; i++) {
        		strcpy(sensor_data[i].key, "");
        		sensor_data[i].last_value = 0;
        		sensor_data[i].min_value = 0;
        		sensor_data[i].max_value = 0;
        		sensor_data[i].avg_value = 0.0;
        		sensor_data[i].key_usage = 0;
        	}
            
            for (int i = 0; i < *n_sensors; i++) {
        		// Set each element to its default or empty state
        		strcpy(sensors[i].key, "");
        		sensors[i].value = 0;
        	}
        
            *n_sensor_data = 0;
            *n_sensors = 0;
            
            sem_post(sem_data);
            sem_post(sem_sensors);
            
            strcpy(final_msg.mtext, "OK");
            
            //Enviar para a message queue
        	if (msgsnd(msg_id, &final_msg, sizeof(MessageQueue)-sizeof(long), 0) == -1) {
            	perror("Erro no msgsnd");
            	sigint();
        	}
            
        }
        else if (strcmp(msg, "sensors") == 0) {
        
        	sem_wait(sem_sensors);
        	
            strcpy(final_msg.mtext, "ID");

            for (int i = 0; i < *n_sensors; i++) {
                strcat(final_msg.mtext, "\n");
                strcat(final_msg.mtext, sensors[i].key);
                
            }
            
            sem_post(sem_sensors);
            
            //Enviar para a message queue
        	if (msgsnd(msg_id, &final_msg, sizeof(MessageQueue)-sizeof(long), 0) == -1) {
            	perror("Erro no msgsnd");
            	sigint();
        	}
        }
        else if (strcmp(strtok(firstWord, " "), "add_alert") == 0){
        	
        	char command[32];
        	char id[32];
			char word[32];
			int min;
			int max;
			
			sscanf(msg, "%s %s %s %d %d", command, id, word, &min, &max);
            
            sem_wait(sem_alerts);

            int j=0;
            
            while(j < *n_alert_rules){
                if (strcmp(alert_rules[j].id, id) == 0) {
                    break;
                }
            }    
			
			//Se o while acima der break (id já existe) dá erro
            if(j != *n_alert_rules){
                strcpy(final_msg.mtext, "Error: alert ID already exists.");
            }
            else{
            	//Se ainda houver espaço para alertas
                if (*n_alert_rules < MAX_ALERTS) {
                    Alert new_alert;
                    strcpy(new_alert.id, id);
                    strcpy(new_alert.key, word);
                    new_alert.min_value = min;
                    new_alert.max_value = max;

                    alert_rules[*n_alert_rules] = new_alert;
                    (*n_alert_rules)++;

                    printf("Alert added.\n");
                    
                    strcpy(final_msg.mtext, "OK");
                }
                //Se já tivermos atingido o MAX_ALERTS
                else{
                	strcpy(final_msg.mtext, "Error: alert limit reached.");
                    escreverLOG(LOG, "Atenção: limite de alertas atingido.\n", sem_LOG);
                }
            }
            
            sem_post(sem_alerts);
            
            //Enviar para a message queue
        	if (msgsnd(msg_id, &final_msg, sizeof(MessageQueue)-sizeof(long), 0) == -1) {
            	perror("Erro no msgsnd");
            	sigint();
        	}
        }
        else if (strcmp(strtok(firstWord, " "), "remove_alert") == 0){
            char command[32];
            char id[32];

            sscanf(msg, "%s %s", command, id);
            
            sem_wait(sem_alerts);
            
            if(*n_alert_rules == 0){
            	strcpy(final_msg.mtext, "Error: alert list is empty.");
            }
			
			//Percorrer lista de alertas
            for (int i = 0; i < *n_alert_rules; i++) {
            	//Se for encontrada
                if (strcmp(alert_rules[i].id, id) == 0) {
                	//eliminar ao por as seguintes para trás
                    for (int j = i; j < *n_alert_rules - 1; j++) {
                        alert_rules[j] = alert_rules[j+1];
                    }
                    //remover do n de alert rules
                    n_alert_rules--;
                    printf("Alert deleted.\n");
                    strcpy(final_msg.mtext, "OK");
                    break;
                //Se não for encontrada
                } else{
                	strcpy(final_msg.mtext, "Error: alert doest exist.");
                }
            }

            sem_post(sem_alerts);
            
            //Enviar para a message queue
        	if (msgsnd(msg_id, &final_msg, sizeof(MessageQueue)-sizeof(long), 0) == -1) {
            	perror("Erro no msgsnd");
            	sigint();
        	}
        }
        else if (strcmp(msg, "list_alerts") == 0){
        	char alerts[BUFFER_SIZE];
            
            sem_wait(sem_alerts);
            
            strcpy(final_msg.mtext, "ID Key Min Max\n");

            for (int i = 0; i < *n_alert_rules; i++) {
            	sprintf(alerts, "%s %s %d %d\n", alert_rules[i].id, alert_rules[i].key, alert_rules[i].min_value, alert_rules[i].max_value);
            	strcat(final_msg.mtext, alerts);
            }
            
            sem_post(sem_alerts);
            
            //Enviar para a message queue
        	if (msgsnd(msg_id, &final_msg, sizeof(MessageQueue)-sizeof(long), 0) == -1) {
            	perror("Erro no msgsnd");
            	sigint();
        	}
        }
        else{
        	printf("Command not valid.\n");
        	escreverLOG(LOG, "WRONG COMMAND\n", sem_LOG);
        }

        // Marcar o Worker como livre
        printf("A marcar o worker como livre...\n");
        
        sem_wait(worker_shm);
        workers[id].state = 0;
        //printf("worker id %d e estado %d\n", id,workers[id].state);
        sem_post(worker_shm);
        
        sem_post(sem_worker);
    }
    
	close(workers[id].pipefd[0]);
	close(workers[id].pipefd[1]);
	free(msg);
    pthread_exit(NULL);
}


//ALERTS WATCHER///////////////////////////////////////////////////////////////////////////////////////////

void alerts_watcher(){
	MessageQueue f_msg; //Mensagem a enviar para a MsgQueue
	
	printf("Alerts Watcher Active...\n");
	
	while(1){
		sem_wait(sem_awatcher);
		
		sem_wait(sem_alerts);
		sem_wait(sem_last_sensor);
	
		//Percorrer lista de alertas
		for (int i = 0; i < *n_alert_rules; i++) {
			//Se a key for igual
			if (strcmp(alert_rules[i].key, last_sensor->key) == 0) {
				//Verificar se está no range certo
				if((last_sensor->last_value < alert_rules[i].min_value) || (last_sensor->last_value > alert_rules[i].max_value)){
					printf("\n\nALERTTTTTTTTTTTTTTTTTTTTTTTTTTTTT\n\n");
					escreverLOG(LOG, "ALERT!!!\n", sem_LOG);
					
					strcpy(f_msg.mtext, "\n\nALERT: data of ");
					strcat(f_msg.mtext, alert_rules[i].key);
					strcat(f_msg.mtext, " sensor out of range!\n\n");
					/**
					//Enviar para a message queue
        			if (msgsnd(msg_id, &f_msg, sizeof(MessageQueue)-sizeof(long), 0) == -1) {
            			perror("Erro no msgsnd");
            			sigint();
        			}
   
        			sem_post(my_semaphore);
        			*/
        			
				}
			}
		}
	
		sem_post(sem_alerts);
		sem_post(sem_last_sensor);
	}	
}


//MAIN///////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
	
	//SIGNAL HANDLER (CTRL-C) ---------------------------------------------------------------------------
  
	// Initialize the signal handler
  	struct sigaction ctrlc;
  	ctrlc.sa_handler = sigint;
  	sigfillset(&ctrlc.sa_mask);
  	ctrlc.sa_flags = 0;
  	sigaction(SIGINT, &ctrlc, NULL);

	sigset_t mask;
  	sigfillset(&mask);
  	sigdelset(&mask, SIGINT);
  	sigprocmask(SIG_SETMASK, &mask, NULL);

	//LOG FILE ------------------------------------------------------------------------------------------
    
    if(access("log.txt", F_OK) != -1){
    	
    	//Se o ficheiro log existir
    	LOG = fopen("log.txt", "a");
    	
    	//Verificação:
    	if (LOG == NULL) {
        	printf("Error opening LOG file.\n");
        	exit(0);
    	}
    	
    } else{

		//Se o ficheiro log não existir
    	LOG = fopen("log.txt", "w");
    	
    	//Verificação:
    	if (LOG == NULL) {
        	printf("Error creating LOG file.");
        	exit(0);
    	}
    }

	//COMMAND LINE PROTECTION ---------------------------------------------------------------------------
    if (argc != 2) {
        printf("Command not valid.\n");
        escreverLOG(LOG, "WRONG COMMAND\n", sem_LOG);
        exit(0);
    }


	//CONFIG FILE ---------------------------------------------------------------------------------------
	f = fopen(argv[1], "r");
	
	//Verificação:
	if (f == NULL) {
        printf("Error: file %s not valid.\n", argv[1]);
        
        exit(0);
    }
    
    //Ler ficheiro:
    read_file(f, &QUEUE_SZ, &N_WORKERS, &MAX_KEYS, &MAX_SENSORS, &MAX_ALERTS);
	
	
	//NAMED PIPES ---------------------------------------------------------------------------------------
    mkfifo("/tmp/CONSOLE_PIPE", 0666);
    mkfifo("/tmp/SENSOR_PIPE", 0666);
    
    //Verificações:
    if (access("/tmp/CONSOLE_PIPE", F_OK) != 0) {
        printf("Error: creating console pipe.\n");
        sigint();
    }
    
    if (access("/tmp/SENSOR_PIPE", F_OK) != 0) {
        printf("Error: creating sensor pipe.\n");
        sigint();
    }
    
    
    //MESSAGE QUEUE -------------------------------------------------------------------------------------
    msg_id = msgget(ftok(".", 'a'), IPC_CREAT | 0700);
    //"." -> represents the current directory / 'a' -> unique identifier
    //ftok(,) -> combines the given path and identifier: generates a unique key
    
    //Verificação:
    if (msg_id < 0) {
        perror("Error: creating message queue.\n");
        sigint();
    }
    
    
    //INTERNAL QUEUE ------------------------------------------------------------------------------------
    
    //Condition Variable
    int result = pthread_cond_init(&cond_iqueue, NULL);
	
	//Verificação:
	if (result != 0) {
		printf("Error: creating cond variable.\n");
    	sigint();
	}
    
    
    //NAMED SEMAPHORES ----------------------------------------------------------------------------------
    sem_unlink("sem_LOG");
	sem_unlink("sem_worker");
	sem_unlink("sem_awatcher");
	sem_unlink("worker_shm");
	sem_unlink("sem_sensors");
	sem_unlink("sem_data");
	sem_unlink("sem_alerts");
	sem_unlink("sem_iqueue");
	sem_unlink("sem_last_sensor");
    
    sem_LOG = sem_open("sem_LOG", O_CREAT | O_EXCL , 0666, 1);
    sem_awatcher = sem_open("sem_awatcher", O_CREAT | O_EXCL , 0666, 0);
    sem_worker = sem_open("sem_worker", O_CREAT | O_EXCL , 0666, N_WORKERS);
    worker_shm = sem_open("worker_shm", O_CREAT | O_EXCL , 0666, N_WORKERS);
    sem_sensors = sem_open("sem_sensors", O_CREAT | O_EXCL , 0666, 1);
    sem_data = sem_open("sem_data", O_CREAT | O_EXCL , 0666, 1);
    sem_alerts = sem_open("sem_alerts", O_CREAT | O_EXCL , 0666, 1);
    sem_iqueue = sem_open("sem_iqueue", O_CREAT | O_EXCL , 0666, 0);
    sem_last_sensor = sem_open("sem_last_sensor", O_CREAT | O_EXCL , 0666, 1);
    
    
    //Verificações:
	if (sem_LOG == SEM_FAILED) {
		fprintf(stderr, "Error: sem_open() failed for sem_LOG. errno:%d\n", errno);
		sigint();
	}
	if (sem_awatcher == SEM_FAILED) {
		fprintf(stderr, "Error: sem_open() failed for sem_awatcher. errno:%d\n", errno);
		sigint();
	}	
	if (sem_worker == SEM_FAILED) {
    	fprintf(stderr, "Error: sem_open() failed for sem_worker. errno:%d\n", errno);
    	sigint();
	}
	if (worker_shm == SEM_FAILED) {
    	fprintf(stderr, "Error: sem_open() failed for worker_shm. errno:%d\n", errno); 
    	sigint();
	}
	if (sem_sensors == SEM_FAILED) {
    	fprintf(stderr, "Error: sem_open() failed for sem_sensors. errno:%d\n", errno); 
    	sigint();
	}
	if (sem_data == SEM_FAILED) {
    	fprintf(stderr, "Error: sem_open() failed for sem_data. errno:%d\n", errno); 
    	sigint();
	}
	if (sem_alerts == SEM_FAILED) {
    	fprintf(stderr, "Error: sem_open() failed for sem_alerts. errno:%d\n", errno); 
    	sigint();
	}
	if (sem_iqueue == SEM_FAILED) {
    	fprintf(stderr, "Error: sem_open() failed for sem_iqueue. errno:%d\n", errno); 
    	sigint();
	}
    if (sem_last_sensor == SEM_FAILED) {
    	fprintf(stderr, "Error: sem_open() failed for sem_last_sensor. errno:%d\n", errno); 
    	sigint();
	}
	
	//SEM
    my_semaphore = sem_open("my_semaphore", O_CREAT, 0700, 0);
    sem_unlink("my_semaphore");
    
    if (my_semaphore == SEM_FAILED) {
    	printf("NOOOOOOOOOOOOOO\n");
    	//exit(0);
	}
    
    //SHARED MEMORY -------------------------------------------------------------------------------------
    int SIZE_SHARED_MEMORY= (sizeof(Alert)*MAX_ALERTS)+(sizeof(Sensor)*MAX_SENSORS)+(sizeof(Data)*MAX_SENSORS)+(sizeof(Worker)*N_WORKERS);
    
    //Criar:
    shmid= shmget(IPC_PRIVATE, SIZE_SHARED_MEMORY, IPC_CREAT | 0777);
    //Anexar:
    shm= (int*) shmat(shmid, NULL, 0);
    
    //Verificação:
    if (shmid == -1) {
    	perror("Error: creating shared memory.");
    	sigint();
    }
	if (shm == (void*)-1) {
    	perror("Error: attaching shared memory.");
    	sigint();
    }
    
    //Localizar ponteiros na memória:
    sensors = (Sensor*)((char*)shm + sizeof(Worker) * N_WORKERS);
    sensor_data = (Data*)((char*)sensors + sizeof(Sensor) * MAX_SENSORS);
    alert_rules = (Alert*)((char*)sensor_data + sizeof(Data) * MAX_SENSORS);
    workers = (Worker*)((char*)alert_rules + sizeof(Alert) * MAX_ALERTS);
    last_sensor=(Data*)((char*)workers + sizeof(Worker) * N_WORKERS);
    n_sensors = (int*)((char*)last_sensor + sizeof(Data));
    n_sensor_data = n_sensors + 1;
    n_alert_rules = n_sensor_data + 1;
    
    //Inicializar na memória partilhada:
    *n_sensors = 0;
    *n_sensor_data = 0;
    *n_alert_rules = 0;
    
    
    //START PROCESSES -----------------------------------------------------------------------------------
    escreverLOG(LOG, "HOME_IOT SIMULATOR STARTING\n", sem_LOG);
   
    
    //THREADS -------------------------------------------------------------------------------------------
    int s_reader= pthread_create(&sensorReader_id, NULL, sensorReader, NULL);
    int c_reader= pthread_create(&consoleReader_id, NULL, consoleReader, NULL);
    int disp= pthread_create(&dispatcher_id, NULL, dispatcher, (void *)workers);
    
    //Verificações:
    if (s_reader != 0) {
    	printf("Error: creating sensorReader thread.\n");
    	sigint();
    }
    if (c_reader != 0) {
    	printf("Error: creating consoleReader thread.\n");
    	sigint();
    }
    if (disp != 0) {
    	printf("Error: creating dispatcher thread.\n");
    	sigint();
    }

    escreverLOG(LOG, "THREAD SENSOR_READER CREATED\n", sem_LOG);
    escreverLOG(LOG, "THREAD CONSOLE_READER CREATED\n", sem_LOG);
    escreverLOG(LOG, "THREAD DISPATCHER_READER CREATED\n", sem_LOG);
    

    //ALERTS WATCHER ------------------------------------------------------------------------------------
    
    pid_t alerts_watcher_pid = fork();
    escreverLOG(LOG, "PROCESS ALERTS_WATCHER CREATED\n", sem_LOG);
    	
    if (alerts_watcher_pid < 0) {
    	printf("Fork failed.");
    	sigint();
    }
    //Processo filho (alerts_watcher)
    else if (alerts_watcher_pid == 0) {
    	alerts_watcher();
    	printf("Acabou o alerts watcher");
    	exit(0);
    }
    
	//WORKERS -------------------------------------------------------------------------------------------
	char strLog[BUFFER_SIZE];
	
	for (int i = 0; i < N_WORKERS; i++) {
        pipe(workers[i].pipefd);
        pid_t pid = fork();
        
        snprintf(strLog, BUFFER_SIZE, "WORKER %d READY\n", i);
        escreverLOG(LOG, strLog, sem_LOG);
        
        //Verificação:
        if (pid == -1) {
    		perror("Error: in fork (workers).\n");
    		sigint();
    		
        // Processo filho (Worker):
        } else if (pid == 0) {     
            // Fecha unnamed pipe de escrita
            close(workers[i].pipefd[1]);
            int id = i;
            //Chamar a função worker
            worker(&id, workers, LOG);
            exit(0);
        
        // Processo pai:
        } else {
        	
        	// Fecha unnamed pipe de leitura
            close(workers[i].pipefd[0]);
            workers[i].id = i;
            //Iniciar Workers como livres
            workers[i].state = 0;
            
        }
    }
    
    
    //WAIT FOR WORKERS ----------------------------------------------------------------------------------
    	
	for (int i = 0; i < N_WORKERS; i++) {
		wait(NULL);
	}
	
	escreverLOG(LOG, "HOME_IOT SIMULATOR WAITING FOR LAST TASKS TO FINISH\n", sem_LOG);
	
	//WAIT FOR THREADS TO FINNISH -----------------------------------------------------------------------
    pthread_join(sensorReader_id, NULL);
    pthread_join(consoleReader_id, NULL);
    pthread_join(dispatcher_id, NULL);
	
	escreverLOG(LOG, "HOME_IOT SIMULATOR CLOSING\n\n", sem_LOG);
	
	//CLEAN RESOURCES -----------------------------------------------------------------------------------
	sigint();
    
    return 0;
}
