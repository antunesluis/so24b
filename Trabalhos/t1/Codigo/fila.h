#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// Definição de um nó da lista
typedef struct no_t
{
    void *dado;
    struct no_t *proximo;
} no_t;

// Definição da fila
typedef struct
{
    no_t *inicio;
    no_t *fim;
    int tamanho;
} fila_t;
