#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

typedef struct {
    int id;
    double arrival_time;
    double service_time;
} Cliente;

typedef struct {
    Cliente *buffer;
    int capacidad;
    int frente;
    int final;
    int cuenta;

    pthread_mutex_t mutex;
    pthread_cond_t cond_no_llena;
    pthread_cond_t cond_no_vacia;
    bool terminer;
} ColaBancaria;

double generar_Exponencial(double tasa) {
    // Generar U ~ Uniforme(0,1)
    double u = (double)rand() / (double)RAND_MAX;

    // Evitar log(0) que daria error
    if (u == 0) u = 1e-9;
    
    // Formula: T = -ln(1-u) / tasa
    return -log(1.0 - u) / tasa; 
}

void inicializarCola(ColaBancaria *cola, int capacidad) {
    q->buffer = malloc(sizeof(Cliente) * capacidad);
    q->capacidad = capacidad;
    q->frente = 0;
    q->final = 0;
    q->cuenta = 0;
    q->terminar = false;

    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond_no_llena, NULL);
    pthread_cond_init(&q->cond_no_vacia, NULL);
}

void insertarCliente(ColaBancaria *q, Cliente c) {
    pthread_mutex_lock(&q->mutex);

    while (q->cuenta == q->capacidad) {
        pthread_cond_wait(&q->cond_no_llena, &q->mutex);
    }

    q->buffer[q->final] = c;
    q->final = (q->final + 1) % q->capacidad;
    q->cuenta++;

    pthread_cond_signal(&q->cond_no_vacia);
    pthread_mutex_unlock(&q->mutex);
}

int extraerCliente(ColaBancaria *q, Cliente *c) {
    pthread_mutex_lock(&q->mutex);

    while (q->cuenta == 0 && !q->terminar) {
        pthread_cond_wait(&q->cond_no_vacia, &q->mutex);
    }

    if (q->cuenta == 0 && q->terminar) {
        pthread_mutex_unlock(&q->mutex);
        return -1; 
    }

    *c = q->buffer[q->frente];
    q->frente = (q->frente + 1) % q->capacidad;
    q->cuenta--;

    pthread_cond_signal(&q->cond_no_llena);
    pthread_mutex_unlock(&q->mutex);
    
    return 0;
}
