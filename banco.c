/**
 * Universidad Simón Bolívar
 * CI3825 - Sistemas de Operación I
 * Trimestre: Enero - Marzo 2026
 *
 * @file banco.c
 * @brief Programa para simular un Sistema de Colas Bancario concurrente (basado en M/M/c)
 * Este proograma permite simular el comportamiento de un banco y la atención al cliente
 * en cajeros utilizando hilos POSIX. Se implementa un modelo productor-consumidor adaptado
 * con tiempo lógico, donde el hilo principal produce las llegadas (productor) y múltiples
 * hilos secundarios actúan como cajeros (consumidores) compartiendo una cola circular
 * sincronizada.
 *
 * @author Victor Hernández (20-10349)
 * @author Ángel Pacheco (20-10479)
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>

/**
 * @struct Cliente
 * @brief Representa a un cliente individual en el sistema bancario.
 */
typedef struct
{
    int id;
    double A;
} Cliente;

/**
 * @struct ColaBancaria
 * @brief Control de la cola circular y variables de sincronización.
 */
typedef struct
{
    Cliente *buffer; // Arreglo dinámico que sirve de buffer para la cola circular.
    int capacidad;   // Capacidad (tamaño) del buffer.
    int frente;      // Índice al primer elemento de la cola.
    int final;       // Índice a la última posición donde ser insertará el siguiente elemento.
    int cuenta;      // Número actual de elementos en la cola.

    pthread_mutex_t mutex;        // Mutex para proteger el acceso concurrente a la cola.
    pthread_cond_t cond_no_vacia; // Variable de condición para detener cajeros si la cola ya está vacía.
    int banco_cerrado;            // Varible boolean (0 = abierto y 1 = cerrado).
} ColaBancaria;

/**
 * @struct Estadisticas
 * @brief Base para almacenar métricas estadíticas globales obtenidas en la simulación.
 */
typedef struct
{
    double suma_Wq;         // Sumatoria de los tiempos de espera en cola.
    double suma_W;          // Sumatoria de los tiempos totales en el sistema.
    double Wq_max;          // Tiempo de espera en cola máximo.
    double T_total;         // Tiempo en que terminó la atención del último cliente.
    int clientes_atendidos; // Cantidad de clientes que ya fueron atendidos.

    pthread_mutex_t mutex_stats; // Mutex para proteger la actualización de las métricas estadísticas.
    pthread_mutex_t mutex_print; // Mutex para evitar que la salida por stdout se intercale.
} Estadisticas;

/**
 * @struct DatosCajero
 * @brief Argumentos para pasar a los hilos de cajero en su creación.
 */
typedef struct
{
    int id_cajero;       // Identificador del cajero.
    ColaBancaria *cola;  // Puntero al monitor de cola compartida.
    double mu;           // Tasa de servicio exponencial.
    Estadisticas *stats; // Puntero a la estructura de métricas estadísticas globales.
} DatosCajero;

// Inicialización de variables globales (Parámetro de la configuración .txt)
int CAJEROS, TCIERRE, MAX_CLIENTES;
double LAMBDA, MU;

/**
 * @brief Genera una variable aleatoria de distribución exponencial.
 *
 * Utiliza el método de la transformada inversa.
 *
 * @param tasa La tasa (lambda o mu) de la distribución.
 * @return Un valor double que representa el tiempo generado.
 */
double generarExponencial(double tasa)
{
    double u = (double)rand() / (double)RAND_MAX;
    if (u == 0.0)
        u = 1e-9;
    if (u >= 1.0)
        u = 0.999999999;

    return -log(u) / tasa;
}

/**
 * @brief Lee un archivo .txt y extrae la configuración del sistema bancario.
 *
 * @param filename Ruta del archivo de texto con los parámetros.
 * @return 0 en caso de éxito, 1 en caso de error.
 */
int leerConfiguracion(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL)
    {
        fprintf(stderr, "Error: El archivo .txt no existe o no puede abrirse.\n");
        return -1;
    }

    char buffer[100], basura[2];
    (void)basura; // Evita mensajes de alerta de variables sin uso.

    // Variables de verificación de existencia de parámetros.
    int f_caj = 0, f_tci = 0, f_lam = 0, f_mu = 0, f_max = 0;

    while (fgets(buffer, sizeof(buffer), file) != NULL)
    {
        // Ignora lineas vacías y comentarios
        if (buffer[0] == '\n' || buffer[0] == '\r' || buffer[0] == '#' ||
            (buffer[0] == '/' && buffer[1] == '/'))
            continue;

        // Identifica cada variable y extrae sus valores
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
        else if (strchr(buffer, '=') != NULL) // Verifica parámetros desconocidos.
        {
            char param_desconocido[50];
            if (sscanf(buffer, "%49[^=]", param_desconocido) == 1)
                fprintf(stderr, "Warning: Parámetro desconocido '%s' encontrado y omitido.\n\n", param_desconocido);
        }
    }
    fclose(file);

    // Comprueba la existencia de todos los parámetros (1 si los encontró, 0 en caso contrario)
    if (!(f_caj && f_tci && f_lam && f_mu && f_max))
    {
        fprintf(stderr, "Error: Faltan parametros o violan restricciones.\n");
        return -1;
    }
    return 0;
}

/**
 * @brief Inicializa la memoria y las variables de sincronización de la cola.
 *
 * @param q Puntero de la cola (ColaBancaria) a ejecutar.
 * @param capacidad Tamaño máximo del buffer circular.
 */
void inicializarCola(ColaBancaria *q, int capacidad)
{
    q->buffer = malloc(sizeof(Cliente) * capacidad);

    // Verifica que el buffer se haya inicializado correctamente
    if (q->buffer == NULL)
    {
        fprintf(stderr, "Error: No se pudo asignar memoria para el buffer de la cola.\n");
        exit(EXIT_FAILURE);
    }

    // Inicializa las variables de cada cola
    q->capacidad = capacidad;
    q->frente = 0;
    q->final = 0;
    q->cuenta = 0;
    q->banco_cerrado = 0;

    if (pthread_mutex_init(&q->mutex, NULL) != 0)
    {
        fprintf(stderr, "Error al inicialiar mutex de cola.\n");
        free(q->buffer);
        exit(EXIT_FAILURE);
    }
    if (pthread_cond_init(&q->cond_no_vacia, NULL) != 0)
    {
        fprintf(stderr, "Error al inicialiar variable de condicion.\n");
        pthread_mutex_destroy(&q->mutex);
        free(q->buffer);
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Inserta un cliente en la cola compartida (Sección Crítica).
 *
 * @param q Puntero a cola (ColaBancaria).
 * @param c Estructura Cliente a encolar.
 */
void insertarCliente(ColaBancaria *q, Cliente c)
{
    pthread_mutex_lock(&q->mutex);

    if (q->cuenta < q->capacidad)
    {
        q->buffer[q->final] = c;
        q->final = (q->final + 1) % q->capacidad;
        q->cuenta++;

        // Despertar al cajero dormido (si lo hay)
        pthread_cond_signal(&q->cond_no_vacia);
    }

    pthread_mutex_unlock(&q->mutex);
}

/**
 * @brief Extrae un cliente de la cola (Sección Crítica bloqueante).
 * Si la cola está vacía pero banco sigue abierto, se bloquea el hilo a espera
 * de un pthread_cond_signal o pthread_cond_broadcast.
 *
 * @param q Puntero a cola (ColaBancaria).
 * @param c Puntero donde se almacenará el cliente extraído.
 * @return 0 en caso de extrar al cliente con éxito, -1 si el banco cerró y la cola se vació.
 */
int extraerCliente(ColaBancaria *q, Cliente *c)
{
    pthread_mutex_lock(&q->mutex);

    // Espera pasivamente mientras no haya clientes y el banco siga abierto
    while (q->cuenta == 0 && q->banco_cerrado == 0)
    {
        pthread_cond_wait(&q->cond_no_vacia, &q->mutex);
    }

    // Condición de terminación de hilo: Cola vacía y Banco cerrado
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

/**
 * @brief Rutina de ejecución de hilos consumidores (Cajeros).
 * Extrae clientes de la cola en bucle, simula la antención y actualiza
 * los datos estadísticos y registro de la consola de manera sincronizada.
 *
 * @param arg Puntero a la estructura DatosCajero.
 * @return NULL.
 */
void *cajero_thread_func(void *arg)
{
    DatosCajero *datos = (DatosCajero *)arg;
    Cliente c;
    double F_ant = 0.0;

    while (extraerCliente(datos->cola, &c) == 0)
    {
        // Cálculo del tiempo lógico
        double S = generarExponencial(datos->mu);
        double B = fmax(c.A, F_ant);
        double F = B + S;
        F_ant = F;

        double Wq = B - c.A;
        double W = F - c.A;

        // Sección Crítica (Impresión por stdout)
        pthread_mutex_lock(&datos->stats->mutex_print);
        printf("[t=%.2f] Cliente %d inicia atencion en Cajero %d\n", B, c.id, datos->id_cajero);
        printf("[t=%.2f] Cliente %d finaliza atencion en Cajero %d\n", F, c.id, datos->id_cajero);
        pthread_mutex_unlock(&datos->stats->mutex_print);

        // Sección Crítca (Actualización de métricas estadísticas)
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

/**
 * @brief Simulación de llegada de clientes al banco según la distribución de Poisson.
 * Función gestionada por el hilo principal antes de la ejecución de consumidores.
 *
 * @param cola Puntero a la cola (ColaBancaria) donde se insertarán clientes.
 * @return 1 si el límite de MAX_CLIENTES truncó la simulación, 0 en caso contrario.
 */
int simularLlegadas(ColaBancaria *cola)
{
    double acumulado = 0.0;
    int N = 0;

    while (N < MAX_CLIENTES)
    {
        acumulado += generarExponencial(LAMBDA);

        // No se aceptan más clientes luego de la hora de cierre
        if (acumulado > TCIERRE)
            break;

        Cliente c = {N + 1, acumulado};
        insertarCliente(cola, c);
        N++;

        printf("[t=%.2f] Cliente %d llega al banco\n", c.A, c.id);
    }

    return (N == MAX_CLIENTES && acumulado <= TCIERRE) ? 1 : 0;
}

/**
 * @brief Gestionador de ciclos de vida de los hilos consumidores (Cajeros).
 * Crea hilos de cajeros, avisa de la terminación de llegadas y los reúne.
 *
 * @param cola Puntero a cola (ColaBancaria).
 * @param stats Puntero a las métricas estadísticas globales.
 */
void procesarCajeros(ColaBancaria *cola, Estadisticas *stats)
{
    pthread_t cajeros[CAJEROS];
    DatosCajero d_caj[CAJEROS];

    // Loop principal para la creación de los hilos de cajeros
    for (int i = 0; i < CAJEROS; i++)
    {
        d_caj[i].id_cajero = i + 1;
        d_caj[i].cola = cola;
        d_caj[i].mu = MU;
        d_caj[i].stats = stats;
        int creacion = pthread_create(&cajeros[i], NULL, cajero_thread_func, &d_caj[i]);

        if (creacion != 0)
        {
            fprintf(stderr, "Error creando hilo de cajero %d.\n", i);
            exit(EXIT_FAILURE);
        }
    }

    // Envío de señal de cierre de banco
    pthread_mutex_lock(&cola->mutex);
    cola->banco_cerrado = 1;
    pthread_cond_broadcast(&cola->cond_no_vacia); // Despierta todos los hilos en espera.
    pthread_mutex_unlock(&cola->mutex);

    // Espera por la terminación de la jornada (Join)
    for (int i = 0; i < CAJEROS; i++)
    {
        int reunion = pthread_join(cajeros[i], NULL);
        if (reunion != 0)
        {
            fprintf(stderr, "Error esperando hilo cajero %d.\n", i);
            exit(EXIT_FAILURE);
        }
    }
}

/**
 * @brief Imprime el reporte final de la ejecución con la métricas estadísticas teóricas
 * y valores reales medidos durante la ejecución
 *
 * @param stats Puntero a los resultados simulados empíricamante.
 * @param truncado Flag devuelto por simularLlegadas().
 */
void imprimirReporteFinal(Estadisticas *stats, int truncado)
{
    double Wq_sim = (stats->clientes_atendidos > 0) ? stats->suma_Wq / stats->clientes_atendidos : 0;
    double W_sim = (stats->clientes_atendidos > 0) ? stats->suma_W / stats->clientes_atendidos : 0;

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
    printf("  Clientes atendidos:                %d\n", stats->clientes_atendidos);
    printf("  Truncado por MAX_CLIENTES:         %s\n", truncado ? "SI" : "NO");
    printf("  Tiempo promedio de espera (Wq):    %.2f\n", Wq_sim);
    printf("  Tiempo promedio en sistema (W):    %.2f\n", W_sim);
    printf("  Tiempo maximo de espera:           %.2f\n", stats->Wq_max);
    printf("  Tiempo total hasta ultimo cliente: %.2f\n", stats->T_total);

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
            // Uso de l-gamma para prevenir overflow de factoriales grandes
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
}

/**
 * @brief Destruye los mecanismos de sincronización y libera la memoria usada
 * durante la ejecución.
 *
 * @param cola Puntero a cola (ColaBancaria).
 * @param stats Puntero a métricas estadísticas a limpiar.
 */
void limpiarRecursos(ColaBancaria *cola, Estadisticas *stats)
{
    if (cola->buffer)
        free(cola->buffer);
    pthread_mutex_destroy(&cola->mutex);
    pthread_cond_destroy(&cola->cond_no_vacia);
    pthread_mutex_destroy(&stats->mutex_stats);
    pthread_mutex_destroy(&stats->mutex_print);
}

/**
 * @brief Hilo de ejecución principal. Gestiona el ciclo de vida del simulador.
 */
int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Uso: %s <archivo.txt>\n", argv[0]);
        return 1;
    }
    else if (leerConfiguracion(argv[1]) != 0)
        return 1;

    srand(time(NULL));

    // Inicialización
    ColaBancaria cola;
    inicializarCola(&cola, MAX_CLIENTES);

    Estadisticas stats = {0}; // Garantiza inicialización en ceros
    pthread_mutex_init(&stats.mutex_stats, NULL);
    pthread_mutex_init(&stats.mutex_print, NULL);

    // Ejecución lógica
    int truncado = simularLlegadas(&cola);
    procesarCajeros(&cola, &stats);

    // Finalización
    imprimirReporteFinal(&stats, truncado); // Impresión de reporte
    limpiarRecursos(&cola, &stats);         // Limpieza de recursos empleados

    return 0;
}
