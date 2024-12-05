
// so.c
// sistema operacional
// simulador de computador
// so24b

// INCLUDES {{{1
#include "so.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "console.h"
#include "dispositivos.h"
#include "instrucao.h"
#include "irq.h"
#include "programa.h"

// CONSTANTES E TIPOS {{{1
// intervalo entre interrupções do relógio
#define INTERVALO_INTERRUPCAO 50 // em instruções executadas
#define MAX_PROCESSOS 5
#define FATOR_MULTIPLICADOR_LIMITE_PROCESSOS 2
#define NUM_TERMINAIS 4
#define FILA_PROCESSOS_INICIAL 5
#define FATOR_CRESCIMENTO_FILA 2
#define QUANTUM_INICIAL 15
#define ESCALONADOR_ATUAL PRIORIDADE

typedef enum
{
    PRONTO,
    BLOQUEADO,
    MORTO,
} estado_processo_t;

typedef enum motivo_bloqueio_t
{
    ESPERANDO_ESCRITA,
    ESPERANDO_LEITURA,
    ESPERANDO_PROCESSO,
    SEM_BLOQUEIO,
} motivo_bloqueio_t;

typedef struct processo_t
{
    int pid;
    int pc;
    int reg_A;
    int reg_X;

    int terminal;
    estado_processo_t estado_atual;
    motivo_bloqueio_t motivo_bloq;

    float prioridade_exec;
} processo_t;

typedef struct
{
    processo_t **elementos;
    int capacidade;
    int inicio; // indice do primeiro elemento
    int fim;    // indice do ultimo elemento
    int quantidade;
} fila_processos_t;

struct so_t
{
    cpu_t *cpu;
    mem_t *mem;
    es_t *es;
    console_t *console;
    bool erro_interno;

    processo_t **tabela_processos;
    processo_t *processo_corrente;
    fila_processos_t *fila_prontos;

    int limite_processos;
    int n_processos;
    int proximo_pid;
    int quantum;
};

typedef enum escalonador_t
{
    ROUND_ROBIN,
    SIMPLES,
    PRIORIDADE,
} escalonador_t;

static int so_trata_interrupcao(void *argC, int reg_A);
static int so_carrega_programa(so_t *self, char *nome_do_executavel);
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender);

// FILA PRONTOS {{{1

static fila_processos_t *fila_processos_cria()
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

static bool fila_processos_insere(fila_processos_t *fila, processo_t *processo)
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

static processo_t *fila_processos_remove(fila_processos_t *fila)
{
    if (fila->quantidade == 0)
        return NULL;

    processo_t *processo = fila->elementos[fila->inicio];
    fila->inicio = (fila->inicio + 1) % fila->capacidade;
    fila->quantidade--;

    return processo;
}

static bool fila_processos_vazia(fila_processos_t *fila) { return fila->quantidade == 0; }

static void fila_processos_destroi(fila_processos_t *fila)
{
    free(fila->elementos);
    free(fila);
}

static processo_t *fila_processos_primeiro(fila_processos_t *fila)
{
    if (fila->quantidade == 0)
        return NULL;
    return fila->elementos[fila->inicio];
}

static bool fila_processos_deleta_processo(fila_processos_t *fila, processo_t *processo_a_deletar)
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
static void fila_processos_ordena_prioridade(fila_processos_t *fila)
{
    if (fila == NULL || fila->quantidade <= 1)
        return; // Fila vazia ou com um único elemento, já está ordenada.

    int total = fila->quantidade;
    for (int i = 0; i < total - 1; i++) {
        for (int j = 0; j < total - i - 1; j++) {
            int indice_atual = (fila->inicio + j) % fila->capacidade;
            int indice_proximo = (fila->inicio + j + 1) % fila->capacidade;

            if (fila->elementos[indice_atual]->prioridade_exec > fila->elementos[indice_proximo]->prioridade_exec) {
                // Trocar os processos de posição
                processo_t *temp = fila->elementos[indice_atual];
                fila->elementos[indice_atual] = fila->elementos[indice_proximo];
                fila->elementos[indice_proximo] = temp;
            }
        }
    }
}

// PROCESSOS {{{1

static processo_t *cria_processo(int pid, int pc);
static processo_t *so_adiciona_novo_processo(so_t *self, char *nome_do_executavel);
static processo_t *busca_processo_pid(so_t *self, int pid);
static processo_t *busca_primeiro_processo_em_estado(so_t *self, estado_processo_t estado);
static void mata_processo(so_t *self, processo_t *processo);
static void atualiza_prioridade_processo(so_t *self, processo_t *processo);

static processo_t *cria_processo(int pid, int pc)
{
    processo_t *p = malloc(sizeof(processo_t));

    p->pid = pid;
    p->estado_atual = PRONTO;
    p->motivo_bloq = SEM_BLOQUEIO;
    p->pc = pc;
    p->reg_A = 0;
    p->reg_X = 0;
    p->terminal = (pid % NUM_TERMINAIS) * 4;
    p->prioridade_exec = 0.5;

    return p;
}

static void destroi_tabela_processos(so_t *self)
{
    for (int i = 0; i < self->limite_processos; i++) {
        if (self->tabela_processos[i] != NULL) {
            free(self->tabela_processos[i]);
        }
    }
    free(self->tabela_processos);
}

static char *estado_para_string(estado_processo_t estado)
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

static char *motivo_para_string(motivo_bloqueio_t motivo)
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

static void debug_tabela_processos(so_t *self)
{
    console_printf("==== Tabela de Processos ====\n");
    for (int i = 0; i < self->limite_processos; i++) {
        processo_t *proc = self->tabela_processos[i];
        if (proc != NULL) {
            console_printf("PID: %d | Estado: %s | PC: %d | Reg A: %d | Reg X: "
                           "%d | Terminal: %d | Motivo Bloqueio: %s\n",
                           proc->pid, estado_para_string(proc->estado_atual), proc->pc, proc->reg_A, proc->reg_X,
                           proc->terminal, motivo_para_string(proc->motivo_bloq));
        } else {
            console_printf("Posição %d: Vazia\n", i);
        }
    }
    console_printf("=============================\n");
}

static void debug_fila_processos(fila_processos_t *fila)
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
                           i, proc->pid, estado_para_string(proc->estado_atual), proc->pc, proc->reg_A, proc->reg_X,
                           proc->terminal, motivo_para_string(proc->motivo_bloq), proc->prioridade_exec);
        } else {
            console_printf("Posição: %d | Vazia\n", i);
        }
    }
    console_printf("============================\n");
}

static void desbloqueia_processo(so_t *self, processo_t *processo)
{
    if (processo == NULL)
        return;

    processo->motivo_bloq = SEM_BLOQUEIO;
    processo->estado_atual = PRONTO;
}

static void bloqueia_processo(so_t *self, processo_t *processo, motivo_bloqueio_t motivo)
{
    if (processo == NULL)
        return;

    processo->motivo_bloq = motivo;
    processo->estado_atual = BLOQUEADO;

    fila_processos_remove(self->fila_prontos);
    atualiza_prioridade_processo(self, processo);
    // debug_fila_processos(self->fila_prontos);
}

static void mata_processo(so_t *self, processo_t *processo)
{
    if (processo == NULL)
        return;

    console_printf("SO: matando processo %d", processo->pid);
    processo->estado_atual = MORTO;

    fila_processos_deleta_processo(self->fila_prontos, processo);
}

static processo_t *busca_primeiro_processo_em_estado(so_t *self, estado_processo_t estado)
{
    for (int i = 0; i < self->n_processos; i++) {
        processo_t *processo = self->tabela_processos[i];
        if (processo != NULL && processo->estado_atual == estado) {
            return processo;
        }
    }
    return NULL;
}

static processo_t *busca_processo_pid(so_t *self, int pid)
{
    for (int i = 0; i < self->n_processos; i++) {
        processo_t *processo = self->tabela_processos[i];
        if (processo != NULL && processo->pid == pid) {
            return processo;
        }
    }
    return NULL;
}

static int tempo_exec_processo_corrente(so_t *self) { return QUANTUM_INICIAL - self->quantum; }

static void atualiza_prioridade_processo(so_t *self, processo_t *processo)
{
    if (processo == NULL)
        return;

    processo->prioridade_exec += (float)(tempo_exec_processo_corrente(self)) / ((float)QUANTUM_INICIAL / 2);
}

static void so_verifica_e_redimensiona_tabela(so_t *self)
{
    if (self->n_processos < self->limite_processos)
        return; // Não precisa redimensionar

    int novo_limite = self->limite_processos * FATOR_MULTIPLICADOR_LIMITE_PROCESSOS;

    processo_t **nova_tabela = realloc(self->tabela_processos, novo_limite * sizeof(processo_t *));
    if (nova_tabela == NULL) {
        console_printf("Erro ao alocar memória para a tabela de processos\n");
        exit(-1);
    }

    for (int i = self->limite_processos; i < novo_limite; i++) {
        nova_tabela[i] = NULL;
    }

    console_printf("SO: redimensionando tabela de processos de %d para %d\n", self->limite_processos, novo_limite);
    self->tabela_processos = nova_tabela;
    self->limite_processos = novo_limite;
}

// Procura possição livre na tabela de processos para o novo processo criado
static void adiciona_processo_tabela(so_t *self, processo_t *processo)
{
    for (int i = 0; i < self->limite_processos; i++) {
        if (self->tabela_processos[i] == NULL) {
            console_printf("SO: adicionando processo %d na posição %d\n", processo->pid, i);
            self->tabela_processos[i] = processo;
            return;
        }
    }
    console_printf("SO: Erro ao adicionar processo na tabela\n");
    mem_escreve(self->mem, IRQ_END_A, -1);
}

// Instancia e adiciona na tabela um novo processo
static processo_t *so_adiciona_novo_processo(so_t *self, char *nome_do_executavel)
{
    // Verifica e redimensiona a tabela de processos, se necessário
    so_verifica_e_redimensiona_tabela(self);

    // Carrega o programa
    int pc = so_carrega_programa(self, nome_do_executavel);
    if (pc < 0) {
        console_printf("SO: Erro ao carregar o programa '%s'\n", nome_do_executavel);
        return NULL;
    }

    // Cria o processo
    int pid = self->proximo_pid++;
    processo_t *processo = cria_processo(pid, pc);
    if (processo == NULL) {
        console_printf("SO: Erro ao criar o processo para '%s'\n", nome_do_executavel);
        return NULL;
    }

    // Adiciona novo processo na tabela
    adiciona_processo_tabela(self, processo);
    fila_processos_insere(self->fila_prontos, processo);

    self->processo_corrente = processo;
    self->n_processos++;

    debug_tabela_processos(self);
    // debug_fila_processos(self->fila_prontos);
    return processo;
}

static int calcula_terminal_processo(int dispositivo, int terminal_base) { return dispositivo + terminal_base; }

// CRIAÇÃO E DESTRUIÇÃO {{{1

static processo_t **tabela_cria(so_t *self)
{
    processo_t **tabela = malloc(self->limite_processos * sizeof(processo_t *));
    if (tabela == NULL) {
        console_printf("Erro ao alocar memória para a tabela de processos\n");
        exit(-1);
    }
    for (int i = 0; i < self->limite_processos; i++) {
        tabela[i] = NULL;
    }
    return tabela;
}

so_t *so_cria(cpu_t *cpu, mem_t *mem, es_t *es, console_t *console)
{
    so_t *self = malloc(sizeof(*self));
    if (self == NULL)
        return NULL;

    self->cpu = cpu;
    self->mem = mem;
    self->es = es;
    self->console = console;
    self->erro_interno = false;

    self->processo_corrente = NULL;
    self->limite_processos = MAX_PROCESSOS;
    self->tabela_processos = tabela_cria(self);
    self->fila_prontos = fila_processos_cria();

    self->n_processos = 0;
    self->proximo_pid = 1;
    self->quantum = QUANTUM_INICIAL;

    cpu_define_chamaC(self->cpu, so_trata_interrupcao, self);

    int ender = so_carrega_programa(self, "trata_int.maq");
    if (ender != IRQ_END_TRATADOR) {
        console_printf("SO: problema na carga do programa de tratamento de interrupção");
        self->erro_interno = true;
    }

    // programa o relógio para gerar uma interrupção após INTERVALO_INTERRUPCAO
    if (es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO) != ERR_OK) {
        console_printf("SO: problema na programação do timer");
        self->erro_interno = true;
    }

    console_printf("SO: Primeira instrução executada");
    return self;
}

void so_destroi(so_t *self)
{
    // if (self->fila_prontos != NULL) fila_destroi(self->fila_prontos);
    if (self->tabela_processos != NULL)
        destroi_tabela_processos(self);

    if (self->fila_prontos != NULL) {
        fila_processos_destroi(self->fila_prontos);
    }

    cpu_define_chamaC(self->cpu, NULL, NULL);
    free(self);
}

// TRATAMENTO DE INTERRUPÇÃO {{{1

// funções auxiliares para o tratamento de interrupção
static void so_salva_estado_da_cpu(so_t *self);
static void so_trata_irq(so_t *self, int irq);
static void so_trata_pendencias(so_t *self);
static void so_escolhe_e_executa_escalonador(so_t *self, escalonador_t);
static void so_escalona_simples(so_t *self);
static void so_escalona_round_robin(so_t *self);
static void so_escalona_prioridade(so_t *self);
static int so_despacha(so_t *self);

static int so_trata_interrupcao(void *argC, int reg_A)
{
    irq_t irq = reg_A;
    so_t *self = argC;
    // esse print polui bastante, recomendo tirar quando estiver com mais confiança
    // console_printf("SO: recebi IRQ %d (%s)", irq, irq_nome(irq));
    // salva o estado da cpu no descritor do processo que foi interrompido
    so_salva_estado_da_cpu(self);
    // faz o atendimento da interrupção
    so_trata_irq(self, irq);
    // faz o processamento independente da interrupção
    so_trata_pendencias(self);
    // escolhe o próximo processo a executar
    so_escolhe_e_executa_escalonador(self, ESCALONADOR_ATUAL);
    // recupera o estado do processo escolhido
    return so_despacha(self);
}

static void so_salva_estado_da_cpu(so_t *self)
{
    processo_t *processo_corrente = self->processo_corrente;
    if (processo_corrente == NULL)
        return;

    // Obtem os registradores salvos da interrupção anterior e atualiza o estado do processo
    mem_le(self->mem, IRQ_END_PC, &processo_corrente->pc);
    mem_le(self->mem, IRQ_END_A, &processo_corrente->reg_A);
    mem_le(self->mem, IRQ_END_X, &processo_corrente->reg_X);
}

static void trata_pendencia_leitura(so_t *self, processo_t *processo)
{
    int estado;
    int terminal_teclado_ok = calcula_terminal_processo(D_TERM_A_TECLADO_OK, processo->terminal);
    if (es_le(self->es, terminal_teclado_ok, &estado) != ERR_OK) {
        console_printf("SO: problema no acesso ao estado do teclado");
        self->erro_interno = true;
        return;
    }

    // Se o teclado ainda estiver ocupado retorna
    if (estado == 0) {
        return;
    }
    console_printf("SO: terminal %d desbloqueado para leitura", processo->terminal);

    desbloqueia_processo(self, processo);
    fila_processos_insere(self->fila_prontos, processo);
}

static void trata_pendencia_escrita(so_t *self, processo_t *processo)
{
    int estado;
    int terminal_tela_ok = calcula_terminal_processo(D_TERM_A_TELA_OK, processo->terminal);
    if (es_le(self->es, terminal_tela_ok, &estado) != ERR_OK) {
        console_printf("SO: problema no acesso ao estado da tela");
        self->erro_interno = true;
        return;
    }

    // Se o dispositivo não estiver disponivel retorna
    if (estado == 0) {
        return;
    }
    console_printf("SO: terminal %d desbloqueado para escrita", processo->terminal);

    int dado = processo->reg_X;
    int terminal_tela = calcula_terminal_processo(D_TERM_A_TELA, processo->terminal);
    if (es_escreve(self->es, terminal_tela, dado) != ERR_OK) {
        console_printf("SO: problema no acesso à tela");
        self->erro_interno = true;
        return;
    }

    processo->reg_A = 0;
    desbloqueia_processo(self, processo);

    fila_processos_insere(self->fila_prontos, processo);
}

static void trata_pendencia_espera_morte(so_t *self, processo_t *processo)
{
    pid_t pid_esperado = processo->reg_X;
    processo_t *processo_esperado = busca_processo_pid(self, pid_esperado);

    if (processo_esperado->estado_atual == MORTO) {
        console_printf("SO: processo esperado %d morreu", pid_esperado);
        desbloqueia_processo(self, processo);

        fila_processos_insere(self->fila_prontos, processo);
    }
}

static void so_trata_pendencias(so_t *self)
{
    for (int i = 0; i < self->n_processos; i++) {
        processo_t *processo = self->tabela_processos[i];

        if (processo->estado_atual == BLOQUEADO) {
            motivo_bloqueio_t motivo = processo->motivo_bloq;

            switch (motivo) {
            case ESPERANDO_ESCRITA:
                // Verifica se o terminal está disponível para escrita
                trata_pendencia_escrita(self, processo);
                break;
            case ESPERANDO_LEITURA:
                // Verifica se o terminal está disponível para leitura
                trata_pendencia_leitura(self, processo);
                break;
            case ESPERANDO_PROCESSO:
                // Verifica se o processo esperado já morreu
                trata_pendencia_espera_morte(self, processo);
                break;
            case SEM_BLOQUEIO:
                break;
            default:
                console_printf("SO: motivo de bloqueio desconhecido");
                self->erro_interno = true;
            }
        }
    }
}

static void so_escolhe_e_executa_escalonador(so_t *self, escalonador_t escalonador)
{
    // A cada interrupção, atualiza a prioridade do processo corrente antes de escalonar
    atualiza_prioridade_processo(self, self->processo_corrente);

    switch (escalonador) {
    case SIMPLES:
        so_escalona_simples(self);
        break;
    case ROUND_ROBIN:
        so_escalona_round_robin(self);
        break;
    case PRIORIDADE:
        so_escalona_prioridade(self);
        break;
    default:
        console_printf("SO: escalonador desconhecido");
        self->erro_interno = true;
    }
}

static void so_escalona_simples(so_t *self)
{
    // Verifica se o processo corrente pode continuar executando
    if (self->processo_corrente != NULL && self->processo_corrente->estado_atual == PRONTO) {
        // Continua com o processo corrente;
        return;
    }

    // Busca o proximo processo pronto e define como processo corrente
    processo_t *proximo = busca_primeiro_processo_em_estado(self, PRONTO);
    if (proximo != NULL) {
        self->processo_corrente = proximo;
        debug_tabela_processos(self);
        return;
    }

    // Se não houver processos prontos, verifica se há processos bloqueados
    if (busca_primeiro_processo_em_estado(self, BLOQUEADO) != NULL) {
        self->processo_corrente = NULL;
        debug_tabela_processos(self);
        return;
    }

    console_printf("SO: nao existem processos prontos\n");
    self->erro_interno = true;
}

static void so_escalona_round_robin(so_t *self)
{
    // Verifica se o processo corrente pode continuar executando e se ainda possui quantum
    if (self->processo_corrente != NULL && self->processo_corrente->estado_atual == PRONTO && self->quantum > 0) {
        // Continua com o processo corrente;
        return;
    }

    // Se o processo atual ainda não terminou e não possui mais quantum
    if (self->processo_corrente != NULL && self->processo_corrente->estado_atual == PRONTO && self->quantum == 0) {
        // Remove o processo da fila antes de reinserir para evitar duplicatas
        fila_processos_deleta_processo(self->fila_prontos, self->processo_corrente);
        fila_processos_insere(self->fila_prontos, self->processo_corrente);
        // debug_fila_processos(self->fila_prontos);
    }

    // Busca o proximo processo pronto e define como processo corrente
    if (!fila_processos_vazia(self->fila_prontos)) {
        // Obtem o primeiro processo pronto da fila de prontos e define como processo corrente
        self->processo_corrente = fila_processos_primeiro(self->fila_prontos);
        self->quantum = QUANTUM_INICIAL;
        // debug_fila_processos(self->fila_prontos);
        return;
    }

    // Nenhum processo para executar encontrado
    self->processo_corrente = NULL;
}

static void so_escalona_prioridade(so_t *self)
{
    // Verifica se o processo corrente pode continuar executando e se ainda possui quantum
    if (self->processo_corrente != NULL && self->processo_corrente->estado_atual == PRONTO && self->quantum > 0) {
        // Continua com o processo corrente;
        return;
    }

    // Se o processo atual ainda não terminou e não possui mais quantum
    if (self->processo_corrente != NULL && self->processo_corrente->estado_atual == PRONTO && self->quantum == 0) {
        // Remove o processo da fila antes de reinserir para evitar duplicatas
        fila_processos_deleta_processo(self->fila_prontos, self->processo_corrente);
        fila_processos_insere(self->fila_prontos, self->processo_corrente);
        // debug_fila_processos(self->fila_prontos);
    }

    // Busca o proximo processo pronto e define como processo corrente
    if (!fila_processos_vazia(self->fila_prontos)) {
        fila_processos_ordena_prioridade(self->fila_prontos);
        debug_fila_processos(self->fila_prontos);
        // Obtem o primeiro processo pronto da fila de prontos e define como processo corrente
        self->processo_corrente = fila_processos_primeiro(self->fila_prontos);
        self->quantum = QUANTUM_INICIAL;
        return;
    }

    // Nenhum processo para executar encontrado
    self->processo_corrente = NULL;
}

static int so_despacha(so_t *self)
{
    if (self->erro_interno)
        return 1;

    processo_t *processo_corrente = self->processo_corrente;
    if (processo_corrente == NULL)
        return 1;

    mem_escreve(self->mem, IRQ_END_PC, processo_corrente->pc);
    mem_escreve(self->mem, IRQ_END_A, processo_corrente->reg_A);
    mem_escreve(self->mem, IRQ_END_X, processo_corrente->reg_X);

    return 0;
}

// TRATAMENTO DE UMA IRQ {{{1

// funções auxiliares para tratar cada tipo de interrupção
static void so_trata_irq_reset(so_t *self);
static void so_trata_irq_err_cpu(so_t *self);
static void so_trata_irq_relogio(so_t *self);
static void so_trata_irq_desconhecida(so_t *self, int irq);

static void so_trata_irq_chamada_sistema(so_t *self);
static void so_trata_irq(so_t *self, int irq)
{
    // verifica o tipo de interrupção que está acontecendo, e atende de acordo
    switch (irq) {
    case IRQ_RESET:
        so_trata_irq_reset(self);
        break;
    case IRQ_SISTEMA:
        so_trata_irq_chamada_sistema(self);
        break;
    case IRQ_ERR_CPU:
        so_trata_irq_err_cpu(self);
        break;
    case IRQ_RELOGIO:
        so_trata_irq_relogio(self);
        break;
    default:
        so_trata_irq_desconhecida(self, irq);
    }
}

// interrupção gerada uma única vez, quando a CPU inicializa
static void so_trata_irq_reset(so_t *self)
{
    // coloca o programa init na memória
    processo_t *init_processo = so_adiciona_novo_processo(self, "init.maq");

    if (init_processo->pc != 100) {
        console_printf("SO: problema na carga do programa inicial");
        self->erro_interno = true;
        return;
    }

    // altera o PC para o endereço de carga
    mem_escreve(self->mem, IRQ_END_PC, init_processo->pc);
    mem_escreve(self->mem, IRQ_END_modo, usuario);
}

// Interrupção gerada quando a CPU identifica um erro
static void so_trata_irq_err_cpu(so_t *self)
{
    int err_int;
    // t1: com suporte a processos, deveria pegar o valor do registrador erro
    //   no descritor do processo corrente, e reagir de acordo com esse erro
    //   (em geral, matando o processo)
    mem_le(self->mem, IRQ_END_erro, &err_int);
    err_t err = err_int;
    console_printf("SO: IRQ não tratada -- erro na CPU: %s", err_nome(err));
    self->erro_interno = true;
}

// Interrupção gerada quando o timer expira
static void so_trata_irq_relogio(so_t *self)
{
    // Rearma o interruptor do relógio e reinicializa o timer para a próxima interrupção
    err_t e1, e2;
    e1 = es_escreve(self->es, D_RELOGIO_INTERRUPCAO,
                    0); // desliga o sinalizador de interrupção
    e2 = es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO);
    if (e1 != ERR_OK || e2 != ERR_OK) {
        console_printf("SO: problema da reinicialização do timer");
        self->erro_interno = true;
    }

    if (self->quantum > 0) {
        self->quantum--;
        console_printf("SO: decrementando quantum para %d", self->quantum);
    }
}

// foi gerada uma interrupção para a qual o SO não está preparado
static void so_trata_irq_desconhecida(so_t *self, int irq)
{
    console_printf("SO: não sei tratar IRQ %d (%s)", irq, irq_nome(irq));
    self->erro_interno = true;
}

// CHAMADAS DE SISTEMA {{{1

// funções auxiliares para cada chamada de sistema
static void so_chamada_le(so_t *self);
static void so_chamada_escr(so_t *self);
static void so_chamada_cria_proc(so_t *self);
static void so_chamada_mata_proc(so_t *self);
static void so_chamada_espera_proc(so_t *self);

static void so_trata_irq_chamada_sistema(so_t *self)
{
    int id_chamada;
    if (mem_le(self->mem, IRQ_END_A, &id_chamada) != ERR_OK) {
        console_printf("SO: erro no acesso ao id da chamada de sistema");
        self->erro_interno = true;
        return;
    }
    console_printf("SO: chamada de sistema %d", id_chamada);
    switch (id_chamada) {
    case SO_LE:
        so_chamada_le(self);
        break;
    case SO_ESCR:
        so_chamada_escr(self);
        break;
    case SO_CRIA_PROC:
        so_chamada_cria_proc(self);
        break;
    case SO_MATA_PROC:
        so_chamada_mata_proc(self);
        break;
    case SO_ESPERA_PROC:
        so_chamada_espera_proc(self);
        break;
    default:
        console_printf("SO: chamada de sistema desconhecida (%d)", id_chamada);
        mata_processo(self, self->processo_corrente);
        self->erro_interno = true;
    }
}

// implementação da chamada se sistema SO_LE faz a leitura de um dado da entrada corrente do processo,
// coloca o dado no reg A
static void so_chamada_le(so_t *self)
{
    int terminal = self->processo_corrente->terminal;
    processo_t *processo = self->processo_corrente;

    int estado;
    int terminal_teclado_ok = calcula_terminal_processo(D_TERM_A_TECLADO_OK, terminal);
    if (es_le(self->es, terminal_teclado_ok, &estado) != ERR_OK) {
        console_printf("SO: problema no acesso ao estado do teclado");
        self->erro_interno = true;
        return;
    }

    // Dispositivo ocupado, bloqueia o processo
    if (estado == 0) {
        console_printf("SO: dispositivo de entrada ocupado");
        bloqueia_processo(self, processo, ESPERANDO_LEITURA);
        return;
    }

    int dado;
    int terminal_teclado = calcula_terminal_processo(D_TERM_A_TECLADO, terminal);
    // Obtem o dado do teminal e registra no reg A do processo
    if (es_le(self->es, terminal_teclado, &dado) != ERR_OK) {
        console_printf("SO: problema no acesso ao teclado");
        self->erro_interno = true;
        return;
    }
    mem_escreve(self->mem, IRQ_END_A, dado);
}

// implementação da chamada se sistema SO_ESCR
// escreve o valor do reg X na saída corrente do processo
static void so_chamada_escr(so_t *self)
{
    int terminal = self->processo_corrente->terminal;

    int estado;
    int terminal_tela_ok = calcula_terminal_processo(D_TERM_A_TELA_OK, terminal);
    if (es_le(self->es, terminal_tela_ok, &estado) != ERR_OK) {
        console_printf("SO: problema no acesso ao estado da tela");
        self->erro_interno = true;
        return;
    }

    // Se o dispositivo de saida estiver ocupado bloqueia o processo
    if (estado == 0) {
        console_printf("SO: dispositivo de saída ocupado");
        bloqueia_processo(self, self->processo_corrente, ESPERANDO_ESCRITA);
        return;
    }

    int dado;
    int terminal_tela = calcula_terminal_processo(D_TERM_A_TELA, terminal);
    mem_le(self->mem, IRQ_END_X, &dado);
    if (es_escreve(self->es, terminal_tela, dado) != ERR_OK) {
        console_printf("SO: problema no acesso à tela");
        self->erro_interno = true;
        return;
    }

    mem_escreve(self->mem, IRQ_END_A, 0);
}

// implementação da chamada se sistema SO_CRIA_PROC cria um processo
static void so_chamada_cria_proc(so_t *self)
{
    processo_t *processo_corrente = self->processo_corrente;
    if (processo_corrente == NULL)
        return;

    // Le o X do descritor do processo criador para obter o nome do arquivo do processo
    int ender_nome_arq;
    if (mem_le(self->mem, IRQ_END_X, &ender_nome_arq) != ERR_OK) {
        console_printf("SO: Erro ao obter o endereço do nome do programa\n");
        mem_escreve(self->mem, IRQ_END_A, -1);
        return;
    }

    char nome[100];
    // Copia o valor do registrador X do processo para a memória
    if (!copia_str_da_mem(100, nome, self->mem, ender_nome_arq)) {
        console_printf("SO: Erro ao copiar o nome do arquivo do processo\n");
        mem_escreve(self->mem, IRQ_END_A, -1);
        return;
    }

    processo_t *novo_processo = so_adiciona_novo_processo(self, nome);
    if (novo_processo == NULL) {
        console_printf("SO: Erro ao criar o processo\n");
        mem_escreve(self->mem, IRQ_END_A, -1);
        return;
    }

    console_printf("SO: criando processo %d com nome %s", novo_processo->pid, nome);
    processo_corrente->reg_A = novo_processo->pid;
}

// implementação da chamada se sistema SO_MATA_PROC mata o processo com pid X (ou o processo corrente se X é 0)
static void so_chamada_mata_proc(so_t *self)
{
    // PID do processo a ser morto está no registrador X do precesso corrente
    processo_t *processo_corrente = self->processo_corrente;
    if (processo_corrente == NULL)
        return;

    int pid_alvo = processo_corrente->reg_X;
    processo_t *processo_alvo = busca_processo_pid(self, pid_alvo);

    // Processo que deve ser morto é o processo corrente
    if (pid_alvo == 0) {
        processo_alvo = processo_corrente;
        mata_processo(self, processo_corrente);
        processo_corrente->reg_A = 0;
        self->processo_corrente = NULL;
        return;
    }

    // Procuro o processo alvo na tabela e se exitir defino como morto
    if (processo_alvo != NULL) {
        mata_processo(self, processo_alvo);
        self->processo_corrente->reg_A = 0;
        return;
    }

    console_printf("SO: processo %d não encontrado", pid_alvo);
    mem_escreve(self->mem, IRQ_END_A, -1);
}

// implementação da chamada se sistema SO_ESPERA_PROC espera o fim do processo com pid X
static void so_chamada_espera_proc(so_t *self)
{
    processo_t *processo_corrente = self->processo_corrente;
    if (processo_corrente == NULL)
        return;
    int pid_alvo = processo_corrente->reg_X;

    // Verifica se o processo corrente está esperando por si mesmo
    if (pid_alvo == processo_corrente->pid) {
        console_printf("SO: processo %d não pode esperar por si mesmo", pid_alvo);
        mem_escreve(self->mem, IRQ_END_A, -1);
        return;
    }

    // Se o processo alvo não estiver morto, bloqueia o processo corrente
    processo_t *processo_alvo = busca_processo_pid(self, pid_alvo);
    if (processo_alvo->estado_atual != MORTO) {
        bloqueia_processo(self, processo_corrente, ESPERANDO_PROCESSO);
        return;
    }

    // Se o processo alvo já estiver morto
    desbloqueia_processo(self, processo_corrente);
    mem_escreve(self->mem, IRQ_END_A, 0);
}

// CARGA DE PROGRAMA {{{1

// carrega o programa na memória
// retorna o endereço de carga ou -1
static int so_carrega_programa(so_t *self, char *nome_do_executavel)
{
    // programa para executar na nossa CPU
    programa_t *prog = prog_cria(nome_do_executavel);
    if (prog == NULL) {
        console_printf("Erro na leitura do programa '%s'\n", nome_do_executavel);
        return -1;
    }

    int end_ini = prog_end_carga(prog);
    int end_fim = end_ini + prog_tamanho(prog);

    for (int end = end_ini; end < end_fim; end++) {
        if (mem_escreve(self->mem, end, prog_dado(prog, end)) != ERR_OK) {
            console_printf("Erro na carga da memória, endereco %d\n", end);
            return -1;
        }
    }

    prog_destroi(prog);
    console_printf("SO: carga de '%s' em %d-%d", nome_do_executavel, end_ini, end_fim);
    return end_ini;
}

// ACESSO À MEMÓRIA DOS PROCESSOS {{{1

// copia uma string da memória do simulador para o vetor str.
// retorna false se erro (string maior que vetor, valor não char na memória,
//   erro de acesso à memória)
// T1: deveria verificar se a memória pertence ao processo
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender)
{
    for (int indice_str = 0; indice_str < tam; indice_str++) {
        int caractere;
        if (mem_le(mem, ender + indice_str, &caractere) != ERR_OK) {
            return false;
        }
        if (caractere < 0 || caractere > 255) {
            return false;
        }
        str[indice_str] = caractere;
        if (caractere == 0) {
            return true;
        }
    }
    // estourou o tamanho de str
    return false;
}

// vim: foldmethod=marker
