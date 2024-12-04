#ifndef FILA_H
#define FILA_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// Estrutura de nó para a fila
typedef struct no_fila_t
{
    void *dado;
    struct no_fila_t *proximo;
} no_fila_t;

// Estrutura da fila
typedef struct fila_t
{
    no_fila_t *inicio;
    no_fila_t *fim;
    int tamanho;
    void (*destroi_dado)(void *dado); // Função para liberar memória de elementos
} fila_t;

// Protótipos das funções
fila_t *fila_cria(void (*destroi_dado)(void *dado));
void fila_destroi(fila_t *fila);
bool fila_vazia(fila_t *fila);
int fila_tamanho(fila_t *fila);
bool fila_insere(fila_t *fila, void *dado);
void *fila_remove(fila_t *fila);
void *fila_primeiro(fila_t *fila);
void fila_imprime(fila_t *fila, void (*imprime_dado)(void *));

#endif // FILA_H
