#ifndef PROCESSO_H
#define PROCESSO_H

#include "tabpag.h"
#include <stdbool.h>
#include <stdio.h>

// Tipos enumerados
typedef enum
{
    PRONTO,
    BLOQUEADO,
    MORTO,
    N_ESTADO
} estado_processo_t;

typedef enum
{
    ESPERANDO_ESCRITA,
    ESPERANDO_LEITURA,
    ESPERANDO_PROCESSO,
    ESPERANDO_PAGINA,
    SEM_BLOQUEIO,
    N_BLOQUEIO
} motivo_bloqueio_t;

// Estrutura do processo
typedef struct processo processo_t;

// Funções de criação e destruição
processo_t *processo_cria(int pid, int pc);
void processo_destroi(processo_t *processo);

// Getters
int processo_get_pid(processo_t *processo);
int processo_get_pc(processo_t *processo);
int processo_get_reg_A(processo_t *processo);
int processo_get_reg_X(processo_t *processo);
int processo_get_terminal(processo_t *processo);
estado_processo_t processo_get_estado(processo_t *processo);
motivo_bloqueio_t processo_get_motivo_bloqueio(processo_t *processo);
float processo_get_prioridade(processo_t *processo);
int processo_get_preempcoes(processo_t *processo);
int processo_get_tempo_em_estado(processo_t *processo, estado_processo_t estado);
int processo_get_complemento(processo_t *processo);
int processo_get_erro(processo_t *processo);
tabpag_t *processo_get_tabpag(processo_t *processo);
int processo_get_end_mem_sec(processo_t *processo);

// Setters
void processo_set_pc(processo_t *processo, int pc);
void processo_set_reg_A(processo_t *processo, int reg_A);
void processo_set_reg_X(processo_t *processo, int reg_X);
void processo_set_estado(processo_t *processo, estado_processo_t estado);
void processo_set_motivo_bloqueio(processo_t *processo, motivo_bloqueio_t motivo);
void processo_set_prioridade(processo_t *processo, float prioridade);
void processo_set_complemento(processo_t *processo, int complemento);
void processo_set_erro(processo_t *processo, int erro);
void processo_set_end_mem_sec(processo_t *processo, int endereco);

// Métodos de estado
void processo_bloqueia(processo_t *processo, motivo_bloqueio_t motivo);
void processo_desbloqueia(processo_t *processo);
void processo_mata(processo_t *processo);

// Métodos de métricas
void processo_atualiza_metricas(processo_t *processo, int tempo_percorrido);
void processo_imprime_metricas(processo_t *processo, FILE *arq);

// Métodos de utilitário
char *processo_estado_para_string(estado_processo_t estado);
char *processo_motivo_para_string(motivo_bloqueio_t motivo);

// Métodos adicionais
processo_t *processo_busca_por_pid(processo_t **tabela_processos, int n_processos, int pid);
processo_t *processo_busca_primeiro_em_estado(processo_t **tabela_processos, int n_processos, estado_processo_t estado);
bool processo_verifica_todos_mortos(processo_t **tabela_processos, int n_processos);
void processo_atualiza_prioridade(processo_t *processo, int quantum);
int processo_calcula_terminal(int dispositivo, int terminal_base);
void incrementa_preempcoes_processo(processo_t *processo);
void debug_tabela_processos(processo_t **tabela_processos, int limite_processos);

#endif // PROCESSO_H
