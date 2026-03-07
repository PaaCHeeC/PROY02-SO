# Makefile para compilar programa principal y sus dependencias. Para usar escribir 'make' en la terminal desde la carpeta deseada.

CC      = gcc
OBJS    = banco.c
CFLAGS  = -std=c11 -Wall -Wextra  -Werror -pthread 	# Flags de compilación para errores e hilos
LDFLAGS = -lm 										# Flag para importación de librería matemática

# all: Compila el proyecto completo
all: banco run 										# Compila el programa principal y ejecuta inmediatamente

# Regla de compilación principal
banco: $(OBJS)
	$(CC) $(CFLAGS) -o banco banco.c $(LDFLAGS)

run: banco.txt
	./banco banco.txt

# clean: Limpia los archivos generados
clean: 												# Borra archivos objeto, identificadores y ejecutables
	rm -f banco
