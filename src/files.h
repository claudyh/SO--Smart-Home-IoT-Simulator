#ifndef FILES_H
#define FILES_H

void read_file(FILE *f, int* QUEUE_SZ, int* N_WORKERS, int* MAX_KEYS, int* MAX_SENSORS, int* MAX_ALERTS);
void escreverLOG(FILE *LOG, const char* mensagem, sem_t* sem_LOG);

#endif
