#include "fila_processos.h"
#include "console.h"
#include <stdio.h>
#include <stdlib.h>

#define FILA_PROCESSOS_INICIAL 5
#define FATOR_CRESCIMENTO_FILA 2

struct fila_processos
{
    processo_t **elementos;
    int capacidade;
    int inicio;
    int fim;
    int quantidade;
};

static bool fila_processos_redimensiona(fila_processos_t *fila)
{
    int nova_capacidade = fila->capacidade * FATOR_CRESCIMENTO_FILA;
    processo_t **novo_elementos = realloc(fila->elementos, nova_capacidade * sizeof(processo_t *));

    if (novo_elementos == NULL)
        return false;

    // Reorganiza os elementos para manter a ordem lógica
    if (fila->inicio > fila->fim) {
        for (int i = 0; i < fila->quantidade; i++) {
            novo_elementos[i] = novo_elementos[(fila->inicio + i) % fila->capacidade];
        }
    }

    fila->elementos = novo_elementos;
    fila->inicio = 0;
    fila->fim = fila->quantidade;
    fila->capacidade = nova_capacidade;

    return true;
}

fila_processos_t *fila_processos_cria()
{
    fila_processos_t *fila = malloc(sizeof(fila_processos_t));
    if (fila == NULL)
        return NULL;

    fila->elementos = malloc(FILA_PROCESSOS_INICIAL * sizeof(processo_t *));
    if (fila->elementos == NULL) {
        free(fila);
        return NULL;
    }

    fila->capacidade = FILA_PROCESSOS_INICIAL;
    fila->inicio = 0;
    fila->fim = 0;
    fila->quantidade = 0;

    return fila;
}

void fila_processos_destroi(fila_processos_t *fila)
{
    if (fila != NULL) {
        free(fila->elementos);
        free(fila);
    }
}

bool fila_processos_insere(fila_processos_t *fila, processo_t *processo)
{
    if (fila->quantidade == fila->capacidade) {
        if (!fila_processos_redimensiona(fila))
            return false;
    }

    fila->elementos[fila->fim] = processo;
    fila->fim = (fila->fim + 1) % fila->capacidade;
    fila->quantidade++;

    return true;
}

processo_t *fila_processos_remove(fila_processos_t *fila)
{
    if (fila->quantidade == 0)
        return NULL;

    processo_t *processo = fila->elementos[fila->inicio];
    fila->inicio = (fila->inicio + 1) % fila->capacidade;
    fila->quantidade--;

    return processo;
}

processo_t *fila_processos_primeiro(fila_processos_t *fila)
{
    if (fila->quantidade == 0)
        return NULL;
    return fila->elementos[fila->inicio];
}

bool fila_processos_vazia(fila_processos_t *fila) { return fila->quantidade == 0; }

int fila_processos_tamanho(fila_processos_t *fila) { return fila->quantidade; }

bool fila_processos_deleta_processo(fila_processos_t *fila, processo_t *processo_a_deletar)
{
    if (fila_processos_vazia(fila)) {
        return false;
    }

    // Encontrar o índice do processo a ser deletado
    int indice = -1;
    for (int i = 0; i < fila->quantidade; i++) {
        int indice_real = (fila->inicio + i) % fila->capacidade;
        if (fila->elementos[indice_real] == processo_a_deletar) {
            indice = indice_real;
            break;
        }
    }

    // Se o processo não foi encontrado
    if (indice == -1) {
        return false;
    }

    // Deslocar elementos para cobrir o espaço do processo deletado
    for (int i = indice; i != fila->fim; i = (i + 1) % fila->capacidade) {
        int proximo = (i + 1) % fila->capacidade;
        fila->elementos[i] = fila->elementos[proximo];
    }

    // Ajustar o fim e a quantidade
    fila->fim = (fila->fim - 1 + fila->capacidade) % fila->capacidade;
    fila->quantidade--;

    return true;
}

// Ordem crescente de prioridade (menor valor = maior prioridade)
void fila_processos_ordena_prioridade(fila_processos_t *fila)
{
    if (fila == NULL || fila->quantidade <= 1)
        return; // Fila vazia ou com um único elemento, já está ordenada.

    int total = fila->quantidade;
    for (int i = 0; i < total - 1; i++) {
        for (int j = 0; j < total - i - 1; j++) {
            int indice_atual = (fila->inicio + j) % fila->capacidade;
            int indice_proximo = (fila->inicio + j + 1) % fila->capacidade;

            processo_t *proc_atual = fila->elementos[indice_atual];
            if (processo_get_prioridade(proc_atual) > processo_get_prioridade(proc_atual)) {
                // Trocar os processos de posição
                processo_t *temp = fila->elementos[indice_atual];
                fila->elementos[indice_atual] = fila->elementos[indice_proximo];
                fila->elementos[indice_proximo] = temp;
            }
        }
    }
}

void debug_fila_processos(fila_processos_t *fila)
{
    console_printf("==== Fila de Processos ====\n");
    if (fila == NULL || fila->quantidade == 0) {
        console_printf("Fila vazia ou não inicializada.\n");
        return;
    }
    for (int i = 0; i < fila->quantidade; i++) {
        int indice = (fila->inicio + i) % fila->capacidade;
        processo_t *proc = fila->elementos[indice];
        if (proc != NULL) {
            console_printf("Posição: %d | PID: %d | Estado: %s | PC: %d | Reg A: %d | Reg X: %d | Terminal: %d | "
                           "Motivo Bloqueio: %s | Prioridade: %.2f\n",
                           i, processo_get_pid(proc), processo_estado_para_string(processo_get_estado(proc)),
                           processo_get_pc(proc), processo_get_reg_A(proc), processo_get_reg_X(proc),
                           processo_get_terminal(proc), processo_motivo_para_string(processo_get_motivo_bloqueio(proc)),
                           processo_get_prioridade(proc));

        } else {
            console_printf("Posição: %d | Vazia\n", i);
        }
    }
    console_printf("============================\n");
}
