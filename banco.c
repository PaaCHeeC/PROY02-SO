#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

typedef struct
{
    int id;
    double arrival_time;
    double service_time;
} Cliente;

typedef struct
{
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

/**
double generar_Exponencial(double tasa)
{
    // Generar U ~ Uniforme(0,1)
    double u = (double)rand() / (double)RAND_MAX;

    // Evitar log(0) que daria error
    if (u == 0)
        u = 1e-9;

    // Formula: T = -ln(1-u) / tasa
    return -log(1.0 - u) / tasa;
}
*/

void inicializarCola(ColaBancaria *q, int capacidad)
{
    q->buffer = malloc(sizeof(Cliente) * capacidad);
    q->capacidad = capacidad;
    q->frente = 0;
    q->final = 0;
    q->cuenta = 0;
    q->terminer = false;

    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond_no_llena, NULL);
    pthread_cond_init(&q->cond_no_vacia, NULL);
}

void insertarCliente(ColaBancaria *q, Cliente c)
{
    pthread_mutex_lock(&q->mutex);

    while (q->cuenta == q->capacidad)
    {
        pthread_cond_wait(&q->cond_no_llena, &q->mutex);
    }

    q->buffer[q->final] = c;
    q->final = (q->final + 1) % q->capacidad;
    q->cuenta++;

    pthread_cond_signal(&q->cond_no_vacia);
    pthread_mutex_unlock(&q->mutex);
}

int extraerCliente(ColaBancaria *q, Cliente *c)
{
    pthread_mutex_lock(&q->mutex);

    while (q->cuenta == 0 && !q->terminer)
    {
        pthread_cond_wait(&q->cond_no_vacia, &q->mutex);
    }

    if (q->cuenta == 0 && q->terminer)
    {
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

int Lecture()
{
    FILE *file;
    // char linea[100];
    char buffer[100];
    // char cadena[50] = "";
    int CAJERO = 0;
    int TCIERRE = 0;
    double LAMBDA = 0.0;
    double MU = 0.0;
    int MAX_CLIENTES = 0;

    file = fopen("Datos.txt", "r");

    if (file == NULL)
    {
        perror("Error al abrir archivo.");
        return 1;
    }

    while (fgets(buffer, sizeof(buffer), file) != NULL)
    {
        if (buffer[0] == '\n' || buffer[0] == '\r')
            continue;

        if (buffer[0] == '#' || (buffer[0] == '/' && buffer[1] == '/'))
            continue;

        if (sscanf(buffer, "CAJERO=%d", &CAJERO) == 1)
            continue;

        if (sscanf(buffer, "TCIERRE=%d", &TCIERRE) == 1)
            continue;

        if (sscanf(buffer, "LAMBDA=%lf", &LAMBDA) == 1)
            continue;

        if (sscanf(buffer, "MU=%lf", &MU) == 1)
            continue;

        if (sscanf(buffer, "MAX_CLIENTES=%d", &MAX_CLIENTES) == 1)
            continue;
    }

    fclose(file);

    printf("%d\n", CAJERO);
    printf("%d\n", TCIERRE);
    printf("%lf\n", LAMBDA);
    printf("%lf\n", MU);
    printf("%d\n", MAX_CLIENTES);

    return 0;
}

int main()
{
    Lecture();
    return 0;
}