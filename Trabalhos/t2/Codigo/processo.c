#include "processo.h"
#include "console.h"
#include <stdlib.h>

#define NUM_TERMINAIS 4
#define QUANTUM_INICIAL 10

typedef struct
{
    int tempo_retorno;
    int preempcoes;
    int entradas_estado[N_ESTADO];
    int tempo_total_estado[N_ESTADO];
    float tempo_medio_resposta;
} metricas_processo_t;

struct processo
{
    int pid;
    int pc;
    int reg_A;
    int reg_X;

    int terminal;
    estado_processo_t estado_atual;
    motivo_bloqueio_t motivo_bloq;
    float prioridade_exec;

    metricas_processo_t *metricas;
};

static metricas_processo_t *cria_metricas_processo()
{
    metricas_processo_t *metricas = malloc(sizeof(metricas_processo_t));
    if (metricas == NULL) {
        console_printf("Erro ao alocar memória para as métricas do processo\n");
        exit(EXIT_FAILURE);
    }

    metricas->tempo_retorno = 0;
    metricas->preempcoes = 0;
    metricas->tempo_medio_resposta = 0;

    for (int i = 0; i < N_ESTADO; i++) {
        metricas->entradas_estado[i] = 0;
        metricas->tempo_total_estado[i] = 0;
    }

    // Processo começa em PRONTO
    metricas->entradas_estado[PRONTO] = 1;
    return metricas;
}

void incrementa_preempcoes_processo(processo_t *processo) { processo->metricas->preempcoes++; }

processo_t *processo_cria(int pid, int pc)
{
    processo_t *p = malloc(sizeof(processo_t));
    if (p == NULL)
        return NULL;

    p->pid = pid;
    p->estado_atual = PRONTO;
    p->motivo_bloq = SEM_BLOQUEIO;
    p->pc = pc;
    p->reg_A = 0;
    p->reg_X = 0;
    p->terminal = (pid % NUM_TERMINAIS) * 4;
    p->prioridade_exec = 0.5;

    p->metricas = cria_metricas_processo();

    return p;
}

void processo_destroi(processo_t *processo)
{
    if (processo != NULL) {
        free(processo->metricas);
        free(processo);
    }
}

// Getters
int processo_get_pid(processo_t *processo) { return processo->pid; }
int processo_get_pc(processo_t *processo) { return processo->pc; }
int processo_get_reg_A(processo_t *processo) { return processo->reg_A; }
int processo_get_reg_X(processo_t *processo) { return processo->reg_X; }
int processo_get_terminal(processo_t *processo) { return processo->terminal; }
estado_processo_t processo_get_estado(processo_t *processo) { return processo->estado_atual; }
motivo_bloqueio_t processo_get_motivo_bloqueio(processo_t *processo) { return processo->motivo_bloq; }
float processo_get_prioridade(processo_t *processo) { return processo->prioridade_exec; }
int processo_get_preempcoes(processo_t *processo) { return processo->metricas->preempcoes; }
int processo_get_tempo_em_estado(processo_t *processo, estado_processo_t estado)
{
    return processo->metricas->tempo_total_estado[estado];
}

// Setters
void processo_set_pc(processo_t *processo, int pc) { processo->pc = pc; }
void processo_set_reg_A(processo_t *processo, int reg_A) { processo->reg_A = reg_A; }
void processo_set_reg_X(processo_t *processo, int reg_X) { processo->reg_X = reg_X; }
void processo_set_estado(processo_t *processo, estado_processo_t estado) { processo->estado_atual = estado; }
void processo_set_motivo_bloqueio(processo_t *processo, motivo_bloqueio_t motivo) { processo->motivo_bloq = motivo; }
void processo_set_prioridade(processo_t *processo, float prioridade) { processo->prioridade_exec = prioridade; }

// Métodos de estado
void processo_bloqueia(processo_t *processo, motivo_bloqueio_t motivo)
{
    processo->motivo_bloq = motivo;
    processo->estado_atual = BLOQUEADO;
    processo->metricas->entradas_estado[BLOQUEADO]++;
}

void processo_desbloqueia(processo_t *processo)
{
    processo->motivo_bloq = SEM_BLOQUEIO;
    processo->estado_atual = PRONTO;
    processo->metricas->entradas_estado[PRONTO]++;
}

void processo_mata(processo_t *processo)
{
    processo->estado_atual = MORTO;
    processo->metricas->entradas_estado[MORTO]++;
}

// Métodos de métricas
void processo_atualiza_metricas(processo_t *processo, int tempo_percorrido)
{
    if (processo == NULL)
        return;

    if (processo->estado_atual != MORTO) {
        processo->metricas->tempo_retorno += tempo_percorrido;
    }

    processo->metricas->tempo_total_estado[processo->estado_atual] += tempo_percorrido;
    processo->metricas->tempo_medio_resposta =
        (processo->metricas->tempo_total_estado[PRONTO] / processo->metricas->entradas_estado[PRONTO]);
}

int tempo_exec_processo_corrente(int quantum) { return QUANTUM_INICIAL - quantum; }

void processo_atualiza_prioridade(processo_t *processo, int quantum)
{
    if (processo == NULL)
        return;

    processo->prioridade_exec =
        (processo->prioridade_exec + (float)tempo_exec_processo_corrente(quantum) / (float)QUANTUM_INICIAL) / 2;
}

bool processo_verifica_todos_mortos(processo_t **tabela_processos, int n_processos)
{
    for (int i = 0; i < n_processos; i++) {
        processo_t *processo = tabela_processos[i];
        if (processo != NULL && processo->estado_atual != MORTO) {
            return false;
        }
    }
    return true;
}

processo_t *processo_busca_primeiro_em_estado(processo_t **tabela_processos, int n_processos, estado_processo_t estado)
{
    for (int i = 0; i < n_processos; i++) {
        processo_t *processo = tabela_processos[i];
        if (processo != NULL && processo->estado_atual == estado) {
            return processo;
        }
    }
    return NULL;
}

processo_t *processo_busca_por_pid(processo_t **tabela_processos, int n_processos, int pid)
{
    for (int i = 0; i < n_processos; i++) {
        processo_t *processo = tabela_processos[i];
        if (processo != NULL && processo->pid == pid) {
            return processo;
        }
    }
    return NULL;
}

int processo_calcula_terminal(int dispositivo, int terminal_base) { return dispositivo + terminal_base; }

void processo_imprime_metricas(processo_t *processo, FILE *arq)
{
    fprintf(arq, "===============================\n");
    fprintf(arq, "Processo PID %d:\n", processo->pid);
    fprintf(arq, "  Tempo de retorno: %d\n", processo->metricas->tempo_retorno);
    fprintf(arq, "  Preempções: %d\n", processo->metricas->preempcoes);
    fprintf(arq, "  Tempo em PRONTO: %d\n", processo->metricas->tempo_total_estado[PRONTO]);
    fprintf(arq, "  Tempo em BLOQUEADO: %d\n", processo->metricas->tempo_total_estado[BLOQUEADO]);
    fprintf(arq, "  Tempo médio de resposta: %.2f\n", processo->metricas->tempo_medio_resposta);
    for (int j = 0; j < N_ESTADO; j++) {
        fprintf(arq, "  Entradas no estado %s: %d\n", processo_estado_para_string(j),
                processo->metricas->entradas_estado[j]);
    }
}

// Métodos de utilitário
char *processo_estado_para_string(estado_processo_t estado)
{
    switch (estado) {
    case PRONTO:
        return "PRONTO";
    case BLOQUEADO:
        return "BLOQUEADO";
    case MORTO:
        return "MORTO";
    default:
        return "DESCONHECIDO";
    }
}

char *processo_motivo_para_string(motivo_bloqueio_t motivo)
{
    switch (motivo) {
    case ESPERANDO_ESCRITA:
        return "ESPERANDO_ESCRITA";
    case ESPERANDO_LEITURA:
        return "ESPERANDO_LEITURA";
    case ESPERANDO_PROCESSO:
        return "ESPERANDO_PROCESSO";
    case SEM_BLOQUEIO:
        return "SEM_BLOQUEIO";
    default:
        return "DESCONHECIDO";
    }
}

void debug_tabela_processos(processo_t **tabela_processos, int limite_processos)
{
    console_printf("==== Tabela de Processos ====\n");
    for (int i = 0; i < limite_processos; i++) {
        processo_t *proc = tabela_processos[i];
        if (proc != NULL) {
            console_printf("PID: %d | Estado: %s | PC: %d | Reg A: %d | Reg X: "
                           "%d | Terminal: %d | Motivo Bloqueio: %s\n",
                           proc->pid, processo_estado_para_string(proc->estado_atual), proc->pc, proc->reg_A,
                           proc->reg_X, proc->terminal, processo_motivo_para_string(proc->motivo_bloq));
        } else {
            console_printf("Posição %d: Vazia\n", i);
        }
    }
    console_printf("=============================\n");
}
