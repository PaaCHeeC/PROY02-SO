#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>

typedef struct
{
    int id;
    double A;
} Cliente;

typedef struct
{
    Cliente *buffer;
    int capacidad;
    int frente;
    int final;
    int cuenta;

    pthread_mutex_t mutex;
    pthread_cond_t cond_no_vacia;
    int banco_cerrado;
} ColaBancaria;

typedef struct
{
    double suma_Wq;
    double suma_W;
    double Wq_max;
    double T_total;
    int clientes_atendidos;
    pthread_mutex_t mutex_stats;
    pthread_mutex_t mutex_print;
} Estadisticas;

typedef struct
{
    int id_cajero;
    ColaBancaria *cola;
    double mu;
    Estadisticas *stats;
} DatosCajero;

int CAJEROS, TCIERRE, MAX_CLIENTES;
double LAMBDA, MU;

double generarExponencial(double tasa)
{
    double u = (double)rand() / (double)RAND_MAX;
    if (u == 0.0)
        u = 1e-9;
    if (u >= 1.0)
        u = 0.999999999;
    return -log(u) / tasa;
}

int leerConfiguracion(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL)
    {
        fprintf(stderr, "Error: El archivo .txt no existe o no puede abrirse.\n");
        return 1;
    }

    char buffer[100], basura[2];
    (void)basura;

    int f_caj = 0, f_tci = 0, f_lam = 0, f_mu = 0, f_max = 0;

    while (fgets(buffer, sizeof(buffer), file) != NULL)
    {
        if (buffer[0] == '\n' || buffer[0] == '\r' || buffer[0] == '#' ||
            (buffer[0] == '/' && buffer[1] == '/'))
            continue;

        if (strncmp(buffer, "CAJEROS=", 8) == 0)
        {
            if (sscanf(buffer + 8, "%d%1s", &CAJEROS, basura) == 1 && CAJEROS >= 1)
                f_caj = 1;
        }
        else if (strncmp(buffer, "TCIERRE=", 8) == 0)
        {
            if (sscanf(buffer + 8, "%d%1s", &TCIERRE, basura) == 1 && TCIERRE > 0)
                f_tci = 1;
        }
        else if (strncmp(buffer, "LAMBDA=", 7) == 0)
        {
            if (sscanf(buffer + 7, "%lf%1s", &LAMBDA, basura) == 1 && LAMBDA > 0)
                f_lam = 1;
        }
        else if (strncmp(buffer, "MU=", 3) == 0)
        {
            if (sscanf(buffer + 3, "%lf%1s", &MU, basura) == 1 && MU > 0)
                f_mu = 1;
        }
        else if (strncmp(buffer, "MAX_CLIENTES=", 13) == 0)
        {
            if (sscanf(buffer + 13, "%d%1s", &MAX_CLIENTES, basura) == 1 && MAX_CLIENTES >= 1)
                f_max = 1;
        }
        else if (strchr(buffer, '=') != NULL)
        {
            char param_desconocido[50];
            if (sscanf(buffer, "%49[^=]", param_desconocido) == 1)
                fprintf(stderr, "Warning: Parámetro desconocido '%s' encontrado y omitido.\n\n", param_desconocido);
        }
    }
    fclose(file);

    if (!(f_caj && f_tci && f_lam && f_mu && f_max))
    {
        fprintf(stderr, "Error: Faltan parametros o violan restricciones.\n");
        return 1;
    }
    return 0;
}

void inicializarCola(ColaBancaria *q, int capacidad)
{
    q->buffer = malloc(sizeof(Cliente) * capacidad);
    q->capacidad = capacidad;
    q->frente = 0;
    q->final = 0;
    q->cuenta = 0;
    q->banco_cerrado = 0;

    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond_no_vacia, NULL);
}

void insertarCliente(ColaBancaria *q, Cliente c)
{
    pthread_mutex_lock(&q->mutex);
    q->buffer[q->final] = c;
    q->final = (q->final + 1) % q->capacidad;
    q->cuenta++;
    pthread_mutex_unlock(&q->mutex);
}

int extraerCliente(ColaBancaria *q, Cliente *c)
{
    pthread_mutex_lock(&q->mutex);

    while (q->cuenta == 0 && q->banco_cerrado == 0)
    {
        pthread_cond_wait(&q->cond_no_vacia, &q->mutex);
    }

    if (q->cuenta == 0 && q->banco_cerrado == 1)
    {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }

    *c = q->buffer[q->frente];
    q->frente = (q->frente + 1) % q->capacidad;
    q->cuenta--;
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

void *cajero_thread_func(void *arg)
{
    DatosCajero *datos = (DatosCajero *)arg;
    Cliente c;
    double F_anterior_del_cajero = 0.0;

    while (extraerCliente(datos->cola, &c) == 0)
    {
        double S = generarExponencial(datos->mu);
        double B = fmax(c.A, F_anterior_del_cajero);
        double F = B + S;
        F_anterior_del_cajero = F;

        double Wq = B - c.A;
        double W = F - c.A;

        pthread_mutex_lock(&datos->stats->mutex_print);
        printf("[t=%.2f] Cliente %d inicia atencion en Cajero %d\n", B, c.id, datos->id_cajero);
        printf("[t=%.2f] Cliente %d finaliza atencion en Cajero %d\n", F, c.id, datos->id_cajero);
        pthread_mutex_unlock(&datos->stats->mutex_print);

        pthread_mutex_lock(&datos->stats->mutex_stats);
        datos->stats->suma_Wq += Wq;
        datos->stats->suma_W += W;
        if (Wq > datos->stats->Wq_max)
            datos->stats->Wq_max = Wq;
        if (F > datos->stats->T_total)
            datos->stats->T_total = F;
        datos->stats->clientes_atendidos++;
        pthread_mutex_unlock(&datos->stats->mutex_stats);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Uso: %s <archivo.txt>\n", argv[0]);
        return 1;
    }
    if (leerConfiguracion(argv[1]) != 0)
        return 1;

    srand(time(NULL));

    ColaBancaria cola;
    inicializarCola(&cola, MAX_CLIENTES);

    Estadisticas stats;
    stats.suma_Wq = 0.0;
    stats.suma_W = 0.0;
    stats.Wq_max = 0.0;
    stats.T_total = 0.0;
    stats.clientes_atendidos = 0;
    pthread_mutex_init(&stats.mutex_stats, NULL);
    pthread_mutex_init(&stats.mutex_print, NULL);

    double acumulado = 0.0;
    int N = 0, truncado = 0;

    while (N < MAX_CLIENTES)
    {
        acumulado += generarExponencial(LAMBDA);
        if (acumulado > TCIERRE)
            break;

        Cliente c = {N + 1, acumulado};
        insertarCliente(&cola, c);
        N++;

        printf("[t=%.2f] Cliente %d llega al banco\n", c.A, c.id);
    }

    if (N == MAX_CLIENTES && acumulado <= TCIERRE)
    {
        truncado = 1;
    }

    pthread_t cajeros[CAJEROS];
    DatosCajero d_caj[CAJEROS];
    for (int i = 0; i < CAJEROS; i++)
    {
        d_caj[i].id_cajero = i + 1;
        d_caj[i].cola = &cola;
        d_caj[i].mu = MU;
        d_caj[i].stats = &stats;
        pthread_create(&cajeros[i], NULL, cajero_thread_func, &d_caj[i]);
    }

    pthread_mutex_lock(&cola.mutex);
    cola.banco_cerrado = 1;
    pthread_cond_broadcast(&cola.cond_no_vacia);
    pthread_mutex_unlock(&cola.mutex);

    for (int i = 0; i < CAJEROS; i++)
    {
        pthread_join(cajeros[i], NULL);
    }

    double Wq_sim = (stats.clientes_atendidos > 0) ? stats.suma_Wq / stats.clientes_atendidos : 0;
    double W_sim = (stats.clientes_atendidos > 0) ? stats.suma_W / stats.clientes_atendidos : 0;

    printf("\n==================================================\n");
    printf("                   RESUMEN FINAL");
    printf("\n==================================================\n");
    printf("Parametros:\n");
    printf("  CAJEROS:          %d\n", CAJEROS);
    printf("  TCIERRE:          %d\n", TCIERRE);
    printf("  LAMBDA:           %.2f\n", LAMBDA);
    printf("  MU:               %.2f\n", MU);
    printf("  MAX_CLIENTES:     %d\n", MAX_CLIENTES);

    printf("Resultados Simulados:\n");
    printf("  Clientes atendidos:                %d\n", stats.clientes_atendidos);
    printf("  Truncado por MAX_CLIENTES:         %s\n", truncado ? "SI" : "NO");
    printf("  Tiempo promedio de espera (Wq):    %.2f\n", Wq_sim);
    printf("  Tiempo promedio en sistema (W):    %.2f\n", W_sim);
    printf("  Tiempo maximo de espera:           %.2f\n", stats.Wq_max);
    printf("  Tiempo total hasta ultimo cliente: %.2f\n", stats.T_total);

    printf("Resultados Teoricos (M/M/c):\n");

    int c = CAJEROS;
    double a = LAMBDA / MU;
    double rho = a / c;

    printf("  Utilizacion (rho):                 %.4f\n", rho);

    if (rho >= 1.0)
    {
        printf("Estado del sistema:\n  rho = %.4f >= 1 -> Sistema inestable\n", rho);
    }
    else
    {
        double sumatoria = 1.0;
        double termino = 1.0;

        for (int k = 1; k < CAJEROS; k++)
        {
            termino = termino * (a / k);
            sumatoria += termino;
        }

        double termino_c;
        if (CAJEROS <= 170)
        {
            termino_c = termino * (a / (c * (1.0 - rho)));
        }
        else
        {
            double ln_termino_c = c * log(a) - lgamma(c + 1.0);
            termino_c = exp(ln_termino_c) * (1.0 / (1.0 - rho));
        }

        double factor_c = termino_c / (sumatoria + termino_c);

        double Wq_teo = factor_c / ((c * MU) - LAMBDA);
        double W_teo = Wq_teo + (1.0 / MU);

        double error_Wq = (Wq_teo > 0) ? fabs(Wq_sim - Wq_teo) / Wq_teo * 100 : 0;
        double error_W = (W_teo > 0) ? fabs(W_sim - W_teo) / W_teo * 100 : 0;

        printf("  Tiempo promedio de espera teorico: %.2f\n", Wq_teo);
        printf("  Tiempo promedio en sistema teorico: %.2f\n", W_teo);
        printf("  Error relativo Wq: %.1f %%\n", error_Wq);
        printf("  Error relativo W:  %.1f %%\n", error_W);

        printf("Estado del sistema:\n  rho = %.4f < 1 -> Sistema estable\n", rho);
        printf("==================================================\n");
    }

    free(cola.buffer);

    pthread_mutex_destroy(&cola.mutex);
    pthread_cond_destroy(&cola.cond_no_vacia);
    pthread_mutex_destroy(&stats.mutex_stats);
    pthread_mutex_destroy(&stats.mutex_print);
    return 0;
}
