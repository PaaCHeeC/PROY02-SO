#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

    char buffer[100];
    char basura[2];

    int CAJEROS, TCIERRE, MAX_CLIENTES;
    int f_cajeros = 0, f_tcierre = 0, f_maxclientes = 0, f_lambda = 0, f_mu = 0;
    double LAMBDA, MU;

    file = fopen("Datos.txt", "r");

    if (file == NULL)
    {
        fprintf(stderr, "Error: El archivo .txt no existe o no puede abrirse.");
        return 1;
    }

    while (fgets(buffer, sizeof(buffer), file) != NULL)
    {
        if (buffer[0] == '\n' || buffer[0] == '\r')
            continue;

        if (buffer[0] == '#' || (buffer[0] == '/' && buffer[1] == '/'))
            continue;

        if (strncmp(buffer, "CAJEROS=", 8) == 0)
        {
            if (sscanf(buffer + 8, "%d%1s", &CAJEROS, basura) != 1)
            {
                fprintf(stderr, "Error: El valor de CAJEROS no es del tipo correcto (se espera un entero).\n");
                return 1;
            }
            if (CAJEROS < 1)
            {
                fprintf(stderr, "Error: El valor de CAJEROS debe ser mayor o igual que 1.\n");
                return 1;
            }

            f_cajeros = 1;
            continue;
        }

        if (strncmp(buffer, "TCIERRE=", 8) == 0)
        {
            if (sscanf(buffer + 8, "%d%1s", &TCIERRE, basura) != 1)
            {
                fprintf(stderr, "Error: El valor de TCIERRE no es del tipo correcto (se espera un entero).\n");
                return 1;
            }
            if (TCIERRE <= 0)
            {
                fprintf(stderr, "Error: El valor de TCIERRE debe ser mayor que 0.\n");
                return 1;
            }

            f_tcierre = 1;
            continue;
        }

        if (strncmp(buffer, "LAMBDA=", 7) == 0)
        {
            if (strchr(buffer + 7, '.') == NULL)
            {
                fprintf(stderr, "Error: El valor de LAMBDA no es del tipo correcto (se espera un decimal con '.').\n");
                return 1;
            }

            if (sscanf(buffer + 7, "%lf%1s", &LAMBDA, basura) != 1)
            {
                fprintf(stderr, "Error: El valor de LAMBDA no es del tipo correcto (se espera un decimal).\n");
                return 1;
            }
            if (LAMBDA <= 0.0)
            {
                fprintf(stderr, "Error: El valor de LAMBDA debe ser mayor que 0.\n");
                return 1;
            }

            f_lambda = 1;
            continue;
        }

        if (strncmp(buffer, "MU=", 3) == 0)
        {
            if (strchr(buffer + 3, '.') == NULL)
            {
                fprintf(stderr, "Error: El valor de MU no es del tipo correcto (se espera un decimal con '.').\n");
                return 1;
            }

            if (sscanf(buffer + 3, "%lf%1s", &MU, basura) != 1)
            {
                fprintf(stderr, "Error: El valor de MU no es del tipo correcto (se espera un decimal).\n");
                return 1;
            }
            if (MU <= 0.0)
            {
                fprintf(stderr, "Error: El valor de MU debe ser mayor que 0.\n");
                return 1;
            }

            f_mu = 1;
            continue;
        }

        if (strncmp(buffer, "MAX_CLIENTES=", 13) == 0)
        {
            if (sscanf(buffer + 13, "%d%1s", &MAX_CLIENTES, basura) != 1)
            {
                fprintf(stderr, "Error: El valor de MAX_CLIENTES no es del tipo correcto (se espera un entero).\n");
                return 1;
            }
            if (MAX_CLIENTES < 1)
            {
                fprintf(stderr, "Error: El valor de MAX_CLIENTES debe ser mayor que 1.\n");
                return 1;
            }

            f_maxclientes = 1;
            continue;
        }

        if (strchr(buffer, '=') != NULL)
        {
            char clave[50];
            sscanf(buffer, "%49[^=]", clave);
            fprintf(stderr, "Error: Parametro desconocido encontrado y omitido %s\n", clave);
        }
    }

    fclose(file);

    if (!f_cajeros || !f_tcierre || !f_lambda || !f_mu || !f_maxclientes)
    {
        fprintf(stderr, "Error: Faltan parametros obligatorios en el archivo de configuracion.\n");
        return 1;
    }

    printf("%d\n", CAJEROS);
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