#include "fila.h"

// Cria uma nova fila
fila_t *fila_cria()
{
    fila_t *fila = malloc(sizeof(fila_t));
    if (fila == NULL) {
        fprintf(stderr, "Erro: falha ao alocar memória para a fila.\n");
        exit(EXIT_FAILURE);
    }
    fila->inicio = NULL;
    fila->fim = NULL;
    fila->tamanho = 0;
    return fila;
}

// Destroi a fila e libera memória
void fila_destroi(fila_t *fila, void (*destruir_dado)(void *))
{
    no_t *atual = fila->inicio;
    while (atual != NULL) {
        no_t *temp = atual;
        atual = atual->proximo;
        if (destruir_dado != NULL) {
            destruir_dado(temp->dado);
        }
        free(temp);
    }
    free(fila);
}
// Verifica se a fila está vazia
bool fila_vazia(fila_t *fila) { return fila->tamanho == 0; }

// Adiciona um elemento à fila
void fila_enfileira(fila_t *fila, void *dado)
{
    no_t *novo_no = malloc(sizeof(no_t));
    if (novo_no == NULL) {
        fprintf(stderr, "Erro: falha ao alocar memória para o nó da fila.\n");
        exit(EXIT_FAILURE);
    }
    novo_no->dado = dado;
    novo_no->proximo = NULL;

    if (fila->fim != NULL) {
        fila->fim->proximo = novo_no;
    }
    fila->fim = novo_no;

    if (fila->inicio == NULL) {
        fila->inicio = novo_no;
    }

    fila->tamanho++;
}

// Remove um elemento da fila
void *fila_desenfileira(fila_t *fila)
{
    if (fila_vazia(fila)) {
        fprintf(stderr, "Erro: tentativa de desenfileirar de uma fila vazia.\n");
        return NULL;
    }

    no_t *temp = fila->inicio;
    void *dado = temp->dado;

    fila->inicio = temp->proximo;
    if (fila->inicio == NULL) {
        fila->fim = NULL;
    }

    free(temp);
    fila->tamanho--;
    return dado;
}

// Retorna o elemento no início da fila sem removê-lo
void *fila_topo(fila_t *fila)
{
    if (fila_vazia(fila)) {
        return NULL;
    }
    return fila->inicio->dado;
}
