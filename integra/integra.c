#include <stdio.h>
#include <stdlib.h>
#include <pthread.h> 
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <mpi.h> 

// * Compilação: mpicc -o integra integra.c  -lpthread -lrt
// * Execução:   mpiexec -n 3 ./integra

#define SIZE 10

typedef struct Clock { 
   int p[3];
} Clock;

typedef struct mensagem { 
    int destino;
    int origem;
} Mensagem;

//----------------------Variáveis---------------------------

// Processo
Clock clockGlobal = {{0,0,0}};

int filaEntradaCont = 0;
pthread_cond_t condFullEntrada;
pthread_cond_t condEmptyEntrada;
pthread_mutex_t mutexEntrada;
Clock filaEntrada[SIZE];

int filaSaidaCont = 0;
pthread_cond_t condFullSaida;
pthread_cond_t condEmptySaida;
pthread_mutex_t mutexSaida;
Mensagem filaSaida[SIZE];


// declaração de funções

void printClock(Clock *clock, int processo) {
   printf("Process: %d, Clock: (%d, %d, %d)\n", processo, clock->p[0], clock->p[1], clock->p[2]);
}

void Event(int pid, Clock *clock){
   clock->p[pid]++;   
}


void Send(int origem, int destino, Clock *clock){
    clock->p[origem]++; //atualiza o clock
    printClock(clock, origem); //print do clock atualizado
    // printf("Foi enviado! \n");
    
    int *clockValue; //valores para enviar no MPI_Send
    clockValue = calloc(3, sizeof(int));
   
    for (int i = 0; i < 3; i++) { //coloca o clock atual nos valores a enviar
        clockValue[i] = clock->p[i];
    }
   
    //printf("Enviando o clock {%d, %d, %d} do processo %d para o processo %d\n", clock->p[0], clock->p[1], clock->p[2], origem, destino);

    MPI_Send(clockValue, 3, MPI_INT, destino, origem, MPI_COMM_WORLD);
   
    free(clockValue);
}


Clock* Receive(){
   int *clockValue; //valores pra receber o clock
   clockValue = calloc (3, sizeof(int));
   Clock *clock = (Clock*)malloc(sizeof(Clock));
   
   MPI_Recv(clockValue, 3,  MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
   
   // printf("recebido! \n");
  
   for (int i = 0; i < 3; i++) {//coloca os valores recebidos em um clock
        clock->p[i] = clockValue[i];
   }

   free(clockValue);
   return clock;
}



void insereFilaSaida(pthread_mutex_t* mutex, pthread_cond_t* condEmpty, pthread_cond_t* condFull, int* filaCont, Mensagem* fila, Clock* clockGlobal, int origem, int destino) {
        pthread_mutex_lock(mutex);//faz o lock da fila de saída
        
        while(*filaCont == SIZE) { //enquanto estiver cheia espere
            pthread_cond_wait(condFull, mutex);
        }
        
        //cria a mensagem
        Mensagem *mensagem = (Mensagem*)malloc(sizeof(Mensagem));
        mensagem->origem = origem;
        mensagem->destino = destino;
        
        //insere na fila
        fila[*filaCont] = *mensagem;
        (*filaCont)++;
        
        pthread_mutex_unlock(mutex); //faz o unlock da fila de saída
        pthread_cond_signal(condEmpty); //fila não está mais vazia
}

void retiraFilaSaida(pthread_mutex_t* mutex, pthread_cond_t* condEmpty, pthread_cond_t* condFull, int* filaCont, Mensagem* fila, Clock* clockGlobal) {
    pthread_mutex_lock(mutex); //faz o lock na fila de entrada
    
    while(*filaCont == 0) { //enquanto estiver vazia espere
        pthread_cond_wait(condEmpty, mutex);
    }
    
    //tira do começo da fila
    Mensagem mensagem = fila[0];
    for (int i = 0; i < *filaCont -1; i++) {
        fila[i] = fila[i+1];
    }
    (*filaCont)--;
    
    Send(mensagem.origem, mensagem.destino, clockGlobal);
    
    pthread_mutex_unlock(mutex); //faz o unlock na fila de entrada
    pthread_cond_signal(condFull); //fila não está mais cheia
}

void insereFilaEntrada(pthread_mutex_t* mutex, pthread_cond_t* condEmpty, pthread_cond_t* condFull, int* filaCont, Clock* fila, Clock* clockGlobal) {
        Clock *clock = Receive(); //recebe algum clock
        
        pthread_mutex_lock(mutex); //faz o lock da fila de entrada
        
        while(*filaCont == SIZE) { //enquanto estiver cheia espere
            pthread_cond_wait(condFull, mutex);
        }
        
        //insere clock no começo da fila
        fila[*filaCont] = *clock;
        (*filaCont)++;
        
        pthread_mutex_unlock(mutex); //faz o unlock da fila de entrada
        pthread_cond_signal(condEmpty); //fila não está mais vazia
}

void retiraFilaEntrada(pthread_mutex_t* mutex, pthread_cond_t* condEmpty, pthread_cond_t* condFull, int* filaCont, Clock* fila, Clock* clockGlobal, int processo) {
    pthread_mutex_lock(mutex); //faz o lock na fila de entrada
    
    while(*filaCont == 0) { //enquanto estiver vazia espere
        pthread_cond_wait(condEmpty, mutex);
    }
    
    //tira do começo da fila
    Clock clock = fila[0];
    for (int i = 0; i < *filaCont -1; i++) {
        fila[i] = fila[i+1];
    }
    (*filaCont)--;
    
    clockGlobal->p[processo]++; //atualiza o clock
    
    for (int i = 0; i < 3; i++) { //atualiza o clock da thread relogio
        if(clock.p[i] > clockGlobal->p[i]) {
            clockGlobal->p[i] = clock.p[i];
        }
    }
    
    printClock(clockGlobal, processo); //printa o clock atualizado
    
    pthread_mutex_unlock(mutex); //faz o unlock na fila de entrada
    pthread_cond_signal(condFull); //fila não está mais cheia
}

void* threadRelogio(void* arg) {
    long p = (long) arg;
    if (p == 0) {
        Event(0, &clockGlobal);
        printClock(&clockGlobal, 0);
        
        insereFilaSaida(&mutexSaida, &condEmptySaida, &condFullSaida, &filaSaidaCont, filaSaida, &clockGlobal, 0, 1); //envia do processo 0 ao processo 1
        //printClock(&clock0, 0);
        
        retiraFilaEntrada(&mutexEntrada, &condEmptyEntrada, &condFullEntrada, &filaEntradaCont, filaEntrada, &clockGlobal, 0); //recebe
        //printClock(&clock0, 0);
        
        insereFilaSaida(&mutexSaida, &condEmptySaida, &condFullSaida, &filaSaidaCont, filaSaida, &clockGlobal, 0, 2); //envia do processo 0 ao processo 2
       // printClock(&clock0, 0);
        
        retiraFilaEntrada(&mutexEntrada, &condEmptyEntrada, &condFullEntrada, &filaEntradaCont, filaEntrada, &clockGlobal, 0); //recebe
        //printClock(&clock0, 0);
        
        insereFilaSaida(&mutexSaida, &condEmptySaida, &condFullSaida, &filaSaidaCont, filaSaida, &clockGlobal, 0, 1); //envia do processo 0 ao processo 1
        //printClock(&clock0, 0);

        Event(0, &clockGlobal);
        printClock(&clockGlobal, 0);
        
    }
    
    if (p == 1) {
        insereFilaSaida(&mutexSaida, &condEmptySaida, &condFullSaida, &filaSaidaCont, filaSaida, &clockGlobal, 1, 0); //envia do processo 1 ao processo 0
        //printClock(&clock1, 1);

        retiraFilaEntrada(&mutexEntrada, &condEmptyEntrada, &condFullEntrada, &filaEntradaCont, filaEntrada, &clockGlobal, 1); //recebe
        //printClock(&clock1, 1);

        retiraFilaEntrada(&mutexEntrada, &condEmptyEntrada, &condFullEntrada, &filaEntradaCont, filaEntrada, &clockGlobal, 1); //recebe
        //printClock(&clock1, 1);
    }

    if (p == 2) {
        Event(2, &clockGlobal);
        printClock(&clockGlobal, 2);

        insereFilaSaida(&mutexSaida, &condEmptySaida, &condFullSaida, &filaSaidaCont, filaSaida, &clockGlobal, 2, 0); //envia do processo 2 ao processo 0
        //printClock(&clock2, 2);

        retiraFilaEntrada(&mutexEntrada, &condEmptyEntrada, &condFullEntrada, &filaEntradaCont, filaEntrada, &clockGlobal, 2); //recebe
        //printClock(&clock2, 2);
    }
    return NULL;
}

void* threadSaida(void* arg) {
    long p = (long) arg;
    while(1) {
        retiraFilaSaida(&mutexSaida, &condEmptySaida, &condFullSaida, &filaSaidaCont, filaSaida, &clockGlobal);
    }
    return NULL;
}

void* threadEntrada(void* arg) {
    long p = (long) arg;
    while(1) {
        insereFilaEntrada(&mutexEntrada, &condEmptyEntrada, &condFullEntrada, &filaEntradaCont, filaEntrada, &clockGlobal);
    }
    return NULL;
}


void processo(long p) {
    Clock clock = {{0,0,0}};
    
    pthread_t tSaida; 
    pthread_t tEntrada;
    pthread_t tRelogio;
    
    //inicialização
    pthread_cond_init(&condFullEntrada, NULL);
    pthread_cond_init(&condEmptyEntrada, NULL);
    pthread_cond_init(&condFullSaida, NULL);
    pthread_cond_init(&condEmptySaida, NULL);
    pthread_mutex_init(&mutexEntrada, NULL);
    pthread_mutex_init(&mutexSaida, NULL);
    
    

    //cria threads
    if (pthread_create(&tRelogio, NULL, &threadRelogio, (void*) p) != 0) { //cria thread Relogio
        perror("Failed to create the thread");
    }     
    if (pthread_create(&tEntrada, NULL, &threadEntrada, (void*) p) != 0) { //cria thread de entrada
        perror("Failed to create the thread");
    }  
    if (pthread_create(&tSaida, NULL, &threadSaida, (void*) p) != 0) { //cria thread de saida
        perror("Failed to create the thread");
    }  
    
    //join das threads 
    if (pthread_join(tRelogio, NULL) != 0) { //join thread Relogio
        perror("Failed to join the thread");
    }  
    if (pthread_join(tEntrada, NULL) != 0) { //join threads entrada
        perror("Failed to join the thread");
    }  
    if (pthread_join(tSaida, NULL) != 0) { //join threads saida
        perror("Failed to join the thread");
    } 
    
    //destroi as condições e mutex
    pthread_cond_destroy(&condFullEntrada);
    pthread_cond_destroy(&condEmptyEntrada);
    pthread_cond_destroy(&condFullSaida);
    pthread_cond_destroy(&condEmptySaida);
    pthread_mutex_destroy(&mutexEntrada);
    pthread_mutex_destroy(&mutexSaida);

}



int main(void) {
   int my_rank;               

   MPI_Init(NULL, NULL); 
   MPI_Comm_rank(MPI_COMM_WORLD, &my_rank); 

   if (my_rank == 0) { 
      processo(0);
   } else if (my_rank == 1) {  
      processo(1);
   } else if (my_rank == 2) {  
      processo(2);
   }

   /* Finaliza MPI */
   MPI_Finalize(); 

   return 0;
}  /* main */
