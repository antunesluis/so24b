#include "fila.h"
#include "console.h"

// Cria uma nova fila
fila_t *fila_cria(void (*destroi_dado)(void *dado))
{
    fila_t *fila = malloc(sizeof(fila_t));
    if (fila == NULL) {
        console_printf("FILA: Erro ao alocar memória para a fila\n");
        return NULL;
    }

    fila->inicio = NULL;
    fila->fim = NULL;
    fila->tamanho = 0;
    fila->destroi_dado = destroi_dado;

    return fila;
}

// Destroi a fila
void fila_destroi(fila_t *fila)
{
    if (fila == NULL) {
        console_printf("FILA: Erro ao destruir fila\n");
        return;
    }

    while (!fila_vazia(fila)) {
        void *dado = fila_remove(fila);
        if (fila->destroi_dado != NULL) {
            fila->destroi_dado(dado);
        }
    }

    free(fila);
}

// Verifica se a fila está vazia
bool fila_vazia(fila_t *fila)
{
    if (fila == NULL)
        return true;
    return fila->tamanho == 0;
}

// Retorna o tamanho da fila
int fila_tamanho(fila_t *fila)
{
    if (fila == NULL)
        return 0;
    return fila->tamanho;
}

// Insere um elemento no final da fila
bool fila_insere(fila_t *fila, void *dado)
{
    if (fila == NULL) {
        console_printf("FILA: Erro ao inserir elemento na fila\n");
        return false;
    }

    no_fila_t *novo_no = malloc(sizeof(no_fila_t));
    if (novo_no == NULL) {
        console_printf("FILA: Erro ao alocar memória para novo nó\n");
        return false;
    }

    novo_no->dado = dado;
    novo_no->proximo = NULL;

    if (fila_vazia(fila)) {
        fila->inicio = novo_no;
        fila->fim = novo_no;
    } else {
        fila->fim->proximo = novo_no;
        fila->fim = novo_no;
    }

    fila->tamanho++;
    return true;
}

// Remove e retorna o primeiro elemento da fila
void *fila_remove(fila_t *fila)
{
    if (fila == NULL || fila_vazia(fila)) {
        console_printf("FILA: Erro ao remover elemento da fila\n");
        return NULL;
    }

    no_fila_t *no_removido = fila->inicio;
    void *dado = no_removido->dado;

    fila->inicio = fila->inicio->proximo;
    fila->tamanho--;

    // Se a fila ficou vazia, atualiza o fim
    if (fila->inicio == NULL) {
        fila->fim = NULL;
    }

    free(no_removido);
    return dado;
}

// Retorna o primeiro elemento sem remover
void *fila_primeiro(fila_t *fila)
{
    if (fila == NULL || fila_vazia(fila)) {
        console_printf("FILA: Erro ao acessar primeiro elemento da fila\n");
        return NULL;
    }
    return fila->inicio->dado;
}

void fila_imprime(fila_t *fila, void (*imprime_dado)(void *))
{
    if (fila == NULL || imprime_dado == NULL) {
        console_printf("FILA: Erro ao imprimir fila\n");
        return;
    }

    if (fila_vazia(fila)) {
        console_printf("FILA: A fila está vazia\n");
        return;
    }

    console_printf("FILA: Imprimindo elementos da fila:\n");
    no_fila_t *atual = fila->inicio;
    while (atual != NULL) {
        imprime_dado(atual->dado); // Chama a função para imprimir o dado
        atual = atual->proximo;
    }
}

void *fila_remove_posicao(fila_t *fila, int posicao)
{
    if (fila == NULL || fila_vazia(fila)) {
        console_printf("FILA: Erro ao remover elemento da fila\n");
        return NULL;
    }

    // Verifica se a posição é válida
    if (posicao < 0 || posicao >= fila->tamanho) {
        console_printf("FILA: Posição inválida\n");
        return NULL;
    }

    no_fila_t *atual = fila->inicio;
    no_fila_t *anterior = NULL;

    // Navega até a posição desejada
    for (int i = 0; i < posicao; i++) {
        anterior = atual;
        atual = atual->proximo;
    }

    void *dado = atual->dado;

    // Remove o nó da lista
    if (anterior == NULL) {
        // Removendo o primeiro elemento
        fila->inicio = atual->proximo;
    } else {
        anterior->proximo = atual->proximo;
    }

    // Atualiza o ponteiro de fim se necessário
    if (atual == fila->fim) {
        fila->fim = anterior;
    }

    fila->tamanho--;
    free(atual);

    return dado;
}

// Retorna o dado de uma posição específica na fila
void *fila_elemento_posicao(fila_t *fila, int posicao)
{
    if (fila == NULL || fila_vazia(fila)) {
        console_printf("FILA: Erro ao acessar elemento da fila\n");
        return NULL;
    }

    // Verifica se a posição é válida
    if (posicao < 0 || posicao >= fila->tamanho) {
        console_printf("FILA: Posição inválida\n");
        return NULL;
    }

    no_fila_t *atual = fila->inicio;

    // Navega até a posição desejada
    for (int i = 0; i < posicao; i++) {
        atual = atual->proximo;
    }

    return atual->dado;
}
