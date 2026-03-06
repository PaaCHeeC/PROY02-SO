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
    bool terminar;
} ColaBancaria;

typedef struct {
    ColaBancaria *cola;
    double lambda;
    int total_clientes;
} DatosLlegada;

typedef struct {
    int id_cajero;
    ColaBancaria *cola;
    double mu;
    Estadisticas *stats;
} DatosCajero;

typedef struct {
    double tiempo_espera_total;
    double tiempo_servicio_total;
    int clientes_atendidos;
    pthread_mutex_t mutex_stats;
} Estadisticas;

double generar_exponencial(double tasa) {
    double u = (double)rand() / (double)RAND_MAX;
    if (u == 0) u = 1e-9;
    return -log(1.0 - u) / tasa; 
}

void inicializarCola(ColaBancaria *q, int capacidad) {
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

void* llegada_thread_func(void* arg) {
    DatosLlegada *datos = (DatosLlegada*)arg;

    for (int i = 0; i < datos->total_clientes; i++) {
        double espera = generar_exponencial(datos->lambda);

        usleep((useconds_t)(espera * 1000000));
        Cliente nuevo;
        nuevo.id = i + 1;
        nuevo.arrival_time = (double)time(NULL);

        printf("[Llegada] Cliente %d entro a la cola.\n", nuevo.id);
        insertarCliente(datos->cola, nuevo);
    }
    return NULL;
}

void* cajero_thread_func(void* arg) {
    DatosCajero *datos = (DatosCajero*)arg;
    Cliente c;

    while (1) {
        int resultado = extraerCliente(datos->cola, &c);

        if (resultado == -1) {
            printf("[Cajero %d] No hay mas clientes. No retiro\n.", datos->id_cajero);
            break;
        }

        double t_servicio = generar_exponencial(datos->mu);
        printf("[Cajero %d] Atendiendo al cliente %d por %.2f segundos...\n",
               datos->id_cajero, c.id, t_servicio);

        usleep((useconds_t)(t_servicio * 1000000));

        printf("[Cajero %d] Cliente %d atendido con exito.\n", datos->id_cajero, c.id);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 6) return 1;
    srand(time(NULL));

    double lambda = atof(argv[1]);
    double mu = atof(argv[2]);
    int num_cajeros = atoi(argv[3]);
    int capacidad_cola = atoi(argv[4]);
    int total_clientes = atoi(argv[5]);

    ColaBancaria cola;
    inicializarCola(&cola, capacidad_cola);

    Estadisticas stats = {0, 0, 0, PTHREAD_MUTEX_INITIALIZER};

    pthread_t hilo_llegada;
    DatosLlegada d_lleg = {&cola, lambda, total_clientes};
    pthread_create(&hilo_llegada, NULL, llegada_thread_func, &d_lleg);

    pthread_t cajeros[num_cajeros];
    DatosCajero d_caj[num_cajeros];
    for (int i = 0; i < num_cajeros; i++) {
        d_caj[i] = (DatosCajero){i + 1, &cola, mu, &stats}; 
        pthread_create(&cajeros[i], NULL, cajero_thread_func, &d_caj[i]);
    }

    pthread_join(hilo_llegada, NULL);

    pthread_mutex_lock(&cola.mutex);
    cola.terminar = true;
    pthread_cond_broadcast(&cola.cond_no_vacia);
    pthread_mutex_unlock(&cola.mutex);

    for (int i = 0; i < num_cajeros; i++) pthread_join(cajeros[i], NULL);

    printf("Simulación terminada. Clientes atendidos: %d\n", stats.clientes_atendidos);
    printf("Tiempo de espera promedio: %.4f\n", stats.tiempo_espera_total / stats.clientes_atendidos);

    free(cola.buffer);
    return 0;
}    
