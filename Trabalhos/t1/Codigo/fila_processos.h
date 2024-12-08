#ifndef FILA_PROCESSOS_H
#define FILA_PROCESSOS_H

#include "processo.h"
#include <stdbool.h>

// Estrutura da fila de processos
typedef struct fila_processos fila_processos_t;

// Funções de criação e destruição
fila_processos_t *fila_processos_cria();
void fila_processos_destroi(fila_processos_t *fila);

// Operações básicas
bool fila_processos_insere(fila_processos_t *fila, processo_t *processo);
processo_t *fila_processos_remove(fila_processos_t *fila);
processo_t *fila_processos_primeiro(fila_processos_t *fila);

// Funções de verificação
bool fila_processos_vazia(fila_processos_t *fila);
int fila_processos_tamanho(fila_processos_t *fila);

// Funções específicas
bool fila_processos_deleta_processo(fila_processos_t *fila, processo_t *processo_a_deletar);
void fila_processos_ordena_prioridade(fila_processos_t *fila);

// Funções de depuração
void debug_fila_processos(fila_processos_t *fila);

#endif // FILA_PROCESSOS_H

