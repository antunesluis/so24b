
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
#include "fila_processos.h"
#include "instrucao.h"
#include "irq.h"
#include "processo.h"
#include "programa.h"

// CONSTANTES E TIPOS {{{
// intervalo entre interrupções do relógio
#define INTERVALO_INTERRUPCAO 50 // em instruções executadas
#define QUANTUM_INICIAL 10
#define ESCALONADOR_ATUAL PRIORIDADE

#define FILA_PROCESSOS_INICIAL 5
#define MAX_PROCESSOS 5
#define NUM_TERMINAIS 4
#define FATOR_MULTIPLICADOR_LIMITE_PROCESSOS 2
#define FATOR_CRESCIMENTO_FILA 2

typedef struct
{
    int processos_criados;
    int preempcoes;
    int interrupcoes[N_IRQ]; // Reset, Sistema, CPU Error, Timer
    int tempo_total_execucao;
    int tempo_sistema_ocioso;
} metricas_so_t;

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

    metricas_so_t *metricas;
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

// METRICAS {{{1

static metricas_so_t *cria_metricas_so()
{
    metricas_so_t *metricas = malloc(sizeof(metricas_so_t));
    if (metricas == NULL) {
        console_printf("Erro ao alocar memória para as métricas globais\n");
        exit(-1);
    }

    metricas->processos_criados = 0;
    metricas->preempcoes = 0;
    metricas->tempo_total_execucao = 0;
    metricas->tempo_sistema_ocioso = 0;

    for (int i = 0; i < N_IRQ; i++) {
        metricas->interrupcoes[i] = 0;
    }
    return metricas;
}

static void so_atualiza_metricas(so_t *self, int tempo_percorrido)
{
    self->metricas->tempo_total_execucao += tempo_percorrido;

    if (self->processo_corrente == NULL) {
        self->metricas->tempo_sistema_ocioso += tempo_percorrido;
    }

    for (int i = 0; i < self->n_processos; i++) {
        processo_t *processo = self->tabela_processos[i];
        if (processo != NULL) {
            processo_atualiza_metricas(processo, tempo_percorrido);
        }
    }
}

static void so_atualiza_metricas_globais(so_t *self, int irq)
{
    if (irq < 0 || irq >= N_IRQ) {
        console_printf("SO: IRQ inválida ao atualizar métricas globais");
        return;
    }

    self->metricas->interrupcoes[irq]++;

    int tempo_anterior_execucao = self->metricas->tempo_total_execucao;

    // Atualiza o tempo total de execução com base no relógio
    if (es_le(self->es, D_RELOGIO_INSTRUCOES, &self->metricas->tempo_total_execucao) != ERR_OK) {
        console_printf("SO: problema na leitura do relógio");
        self->erro_interno = true;
        return;
    }

    if (tempo_anterior_execucao == 0) {
        return;
    }

    int tempo_percorrido = self->metricas->tempo_total_execucao - tempo_anterior_execucao;
    so_atualiza_metricas(self, tempo_percorrido);
}

static void finaliza_metricas(so_t *self)
{
    self->metricas->processos_criados = self->n_processos;

    for (int i = 0; i < self->n_processos; i++) {
        processo_t *processo = self->tabela_processos[i];
        if (processo != NULL) {
            self->metricas->preempcoes += processo_get_preempcoes(processo);
            self->metricas->tempo_sistema_ocioso += processo_get_tempo_em_estado(processo, MORTO);
        }
    }
}

static void gera_relatorio_final(so_t *self)
{
    FILE *arq = fopen("../metricas_simulador.txt", "w");
    if (arq == NULL) {
        console_printf("SO: problema na abertura do arquivo de métricas");
        return;
    }

    fprintf(arq, "Número de processos criados: %d\n", self->metricas->processos_criados);

    fprintf(arq, "Instruçẽos para innterrupcao de clock: %d\n", INTERVALO_INTERRUPCAO);
    fprintf(arq, "Quantum: %d\n", QUANTUM_INICIAL);

    fprintf(arq, "==== Relatório de Execução ====\n");
    fprintf(arq, "Número de preempções: %d\n", self->metricas->preempcoes);
    fprintf(arq, "Tempo total de execução: %d\n", self->metricas->tempo_total_execucao);
    fprintf(arq, "Tempo ocioso do sistema: %d\n", self->metricas->tempo_sistema_ocioso);

    for (int i = 0; i < N_IRQ; i++) {
        fprintf(arq, "Interrupções de %s: %d\n", irq_nome(i), self->metricas->interrupcoes[i]);
    }

    for (int i = 0; i < self->n_processos; i++) {
        processo_t *proc = self->tabela_processos[i];
        if (proc != NULL) {
            processo_imprime_metricas(proc, arq);
        }
    }

    fclose(arq);
}

// PROCESSOS {{{1

static void destroi_tabela_processos(so_t *self)
{
    for (int i = 0; i < self->limite_processos; i++) {
        if (self->tabela_processos[i] != NULL) {
            free(self->tabela_processos[i]);
        }
    }
    free(self->tabela_processos);
}

static void so_processa_desbloqueio_proc(so_t *self, processo_t *processo, bool insere_fim_fila)
{
    processo_desbloqueia(processo);
    if (insere_fim_fila) {
        fila_processos_insere(self->fila_prontos, processo);
    }
}

static void so_processa_bloqueio_proc(so_t *self, processo_t *processo, motivo_bloqueio_t motivo)
{
    if (processo == NULL)
        return;

    processo_bloqueia(processo, motivo);
    fila_processos_remove(self->fila_prontos);
    processo_atualiza_prioridade(processo, self->quantum);
}

static void so_processa_morte_proc(so_t *self, processo_t *processo)
{
    if (processo == NULL)
        return;

    processo_mata(processo);
    fila_processos_deleta_processo(self->fila_prontos, processo);
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
static void so_adiciona_processo_tabela(so_t *self, processo_t *processo)
{
    for (int i = 0; i < self->limite_processos; i++) {
        if (self->tabela_processos[i] == NULL) {
            console_printf("SO: adicionando processo %d na posição %d\n", processo_get_pid(processo), i);
            self->tabela_processos[i] = processo;
            return;
        }
    }
    console_printf("SO: Erro ao adicionar processo na tabela\n");
    processo_set_reg_A(self->processo_corrente, -1);
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
    processo_t *processo = processo_cria(pid, pc);
    if (processo == NULL) {
        console_printf("SO: Erro ao criar o processo para '%s'\n", nome_do_executavel);
        return NULL;
    }

    // Adiciona novo processo na tabela
    so_adiciona_processo_tabela(self, processo);
    fila_processos_insere(self->fila_prontos, processo);

    self->processo_corrente = processo;
    self->n_processos++;

    debug_tabela_processos(self->tabela_processos, self->limite_processos);
    debug_fila_processos(self->fila_prontos);
    return processo;
}

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
    self->metricas = cria_metricas_so();

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
static void so_encerra_atividade(so_t *self);
static int so_despacha(so_t *self);

static void so_encerra_atividade(so_t *self)
{
    console_printf("SO: encerrando atividades");
    finaliza_metricas(self);
    gera_relatorio_final(self);
    so_destroi(self);
}

static int so_trata_interrupcao(void *argC, int reg_A)
{
    irq_t irq = reg_A;
    so_t *self = argC;

    // Atualizo todas as métricas do simulador e dos processos atuais.
    so_atualiza_metricas_globais(self, irq);

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

    if (processo_verifica_todos_mortos(self->tabela_processos, self->n_processos)) {
        so_encerra_atividade(self);
    }

    if (self->erro_interno) {
        console_printf("SO: erro interno detectado, encerrando atividades");
        exit(-1);
    }

    return so_despacha(self);
}

static void so_salva_estado_da_cpu(so_t *self)
{
    processo_t *processo_corrente = self->processo_corrente;
    if (processo_corrente == NULL)
        return;

    // Obtem os registradores salvos da interrupção anterior e atualiza o estado do processo
    int pc, reg_A, reg_X;
    mem_le(self->mem, IRQ_END_PC, &pc);
    mem_le(self->mem, IRQ_END_A, &reg_A);
    mem_le(self->mem, IRQ_END_X, &reg_X);

    processo_set_pc(processo_corrente, pc);
    processo_set_reg_A(processo_corrente, reg_A);
    processo_set_reg_X(processo_corrente, reg_X);
}

static void trata_pendencia_leitura(so_t *self, processo_t *processo)
{
    int estado;
    int terminal_teclado_ok = processo_calcula_terminal(D_TERM_A_TECLADO_OK, processo_get_terminal(processo));
    if (es_le(self->es, terminal_teclado_ok, &estado) != ERR_OK) {
        console_printf("SO: problema no acesso ao estado do teclado");
        self->erro_interno = true;
        return;
    }

    // Se o teclado ainda estiver ocupado retorna
    if (estado == 0) {
        return;
    }
    console_printf("SO: terminal %d desbloqueado para leitura", processo_get_terminal(processo));

    int dado;
    int terminal_leitura = processo_calcula_terminal(D_TERM_A_TECLADO, processo_get_terminal(processo));
    if (es_le(self->es, terminal_leitura, &dado) != ERR_OK) {
        console_printf("SO: problema no acesso ao teclado");
        self->erro_interno = true;
        return;
    }

    processo_set_reg_A(processo, dado);
    so_processa_desbloqueio_proc(self, processo, true);
}

static void trata_pendencia_escrita(so_t *self, processo_t *processo)
{
    int estado;
    int terminal = processo_get_terminal(processo);

    int terminal_tela_ok = processo_calcula_terminal(D_TERM_A_TELA_OK, terminal);
    if (es_le(self->es, terminal_tela_ok, &estado) != ERR_OK) {
        console_printf("SO: problema no acesso ao estado da tela");
        self->erro_interno = true;
        return;
    }

    // Se o dispositivo não estiver disponivel retorna
    if (estado == 0) {
        return;
    }
    console_printf("SO: terminal %d desbloqueado para escrita", terminal);

    int dado = processo_get_reg_X(processo);
    int terminal_tela = processo_calcula_terminal(D_TERM_A_TELA, terminal);
    if (es_escreve(self->es, terminal_tela, dado) != ERR_OK) {
        console_printf("SO: problema no acesso à tela");
        self->erro_interno = true;
        return;
    }

    processo_set_reg_X(processo, 0);
    so_processa_desbloqueio_proc(self, processo, true);
}

static void trata_pendencia_espera_morte(so_t *self, processo_t *processo)
{
    pid_t pid_esperado = processo_get_reg_X(processo);
    processo_t *proc_esperado = processo_busca_por_pid(self->tabela_processos, self->n_processos, pid_esperado);

    if (processo_get_estado(proc_esperado) == MORTO) {
        console_printf("SO: processo esperado %d morreu", pid_esperado);
        so_processa_desbloqueio_proc(self, processo, true);
    }
}

static void so_trata_pendencias(so_t *self)
{
    for (int i = 0; i < self->n_processos; i++) {
        processo_t *processo = self->tabela_processos[i];

        if (processo_get_estado(processo) == BLOQUEADO) {
            motivo_bloqueio_t motivo = processo_get_motivo_bloqueio(processo);

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
    if (self->processo_corrente != NULL && processo_get_estado(self->processo_corrente) == PRONTO) {
        // Continua com o processo corrente;
        return;
    }

    // Busca o proximo processo pronto e define como processo corrente
    processo_t *proximo = processo_busca_primeiro_em_estado(self->tabela_processos, self->n_processos, PRONTO);
    if (proximo != NULL) {
        self->processo_corrente = proximo;
        debug_tabela_processos(self->tabela_processos, self->limite_processos);
        return;
    }

    // Se não houver processos prontos, verifica se há processos bloqueados
    if (processo_busca_primeiro_em_estado(self->tabela_processos, self->n_processos, BLOQUEADO) != NULL) {
        self->processo_corrente = NULL;
        debug_tabela_processos(self->tabela_processos, self->limite_processos);
        return;
    }

    console_printf("SO: nao existem processos prontos\n");
    self->erro_interno = true;
}

static void so_escalona_round_robin(so_t *self)
{
    processo_t *processo_corrente = self->processo_corrente;
    // Verifica se o processo corrente pode continuar executando e se ainda possui quantum
    if (processo_corrente != NULL && processo_get_estado(processo_corrente) == PRONTO && self->quantum > 0) {
        // Continua com o processo corrente;
        return;
    }

    // Se o processo atual ainda não terminou e não possui mais quantum
    if (processo_corrente != NULL && processo_get_estado(processo_corrente) == PRONTO && self->quantum == 0) {
        // Remove o processo da fila antes de reinserir para evitar duplicatas
        fila_processos_deleta_processo(self->fila_prontos, processo_corrente);
        fila_processos_insere(self->fila_prontos, processo_corrente);

        // Buscas na fila resultam em preempcoes
        incrementa_preempcoes_processo(processo_corrente);
        debug_fila_processos(self->fila_prontos);
    }

    // Busca o proximo processo pronto e define como processo corrente
    if (!fila_processos_vazia(self->fila_prontos)) {
        // Obtem o primeiro processo pronto da fila de prontos e define como processo corrente
        self->processo_corrente = fila_processos_primeiro(self->fila_prontos);
        self->quantum = QUANTUM_INICIAL;
        debug_fila_processos(self->fila_prontos);
        return;
    }

    // Nenhum processo para executar encontrado
    self->processo_corrente = NULL;
}

static void so_escalona_prioridade(so_t *self)
{
    processo_t *processo_corrente = self->processo_corrente;
    // Verifica se o processo corrente pode continuar executando e se ainda possui quantum
    if (processo_corrente != NULL && processo_get_estado(processo_corrente) == PRONTO && self->quantum > 0) {
        // Continua com o processo corrente;
        return;
    }

    // Se o processo atual ainda não terminou e não possui mais quantum
    if (processo_corrente != NULL && processo_get_estado(processo_corrente) == PRONTO && self->quantum == 0) {
        // Atualiza a prioridade do processo corrente quando seu quantum termina
        processo_atualiza_prioridade(self->processo_corrente, self->quantum);

        // Remove o processo da fila antes de reinserir para evitar duplicatas
        fila_processos_deleta_processo(self->fila_prontos, processo_corrente);
        fila_processos_insere(self->fila_prontos, processo_corrente);

        // Buscas na fila resultam em preempcoes
        incrementa_preempcoes_processo(processo_corrente);
        debug_fila_processos(self->fila_prontos);
    }

    // Busca o proximo processo pronto e define como processo corrente
    if (!fila_processos_vazia(self->fila_prontos)) {
        fila_processos_ordena_prioridade(self->fila_prontos);
        debug_fila_processos(self->fila_prontos);
        //  Obtem o primeiro processo pronto da fila de prontos e define como processo corrente
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

    mem_escreve(self->mem, IRQ_END_PC, processo_get_pc(processo_corrente));
    mem_escreve(self->mem, IRQ_END_A, processo_get_reg_A(processo_corrente));
    mem_escreve(self->mem, IRQ_END_X, processo_get_reg_X(processo_corrente));

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

    if (processo_get_pc(init_processo) != 100) {
        console_printf("SO: problema na carga do programa inicial");
        self->erro_interno = true;
        return;
    }

    // altera o PC para o endereço de carga
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
        so_processa_morte_proc(self, self->processo_corrente);
        self->erro_interno = true;
    }
}

// implementação da chamada se sistema SO_LE faz a leitura de um dado da entrada corrente do processo,
// coloca o dado no reg A
static void so_chamada_le(so_t *self)
{
    processo_t *processo = self->processo_corrente;
    int terminal = processo_get_terminal(processo);

    int estado;
    int terminal_teclado_ok = processo_calcula_terminal(D_TERM_A_TECLADO_OK, terminal);
    if (es_le(self->es, terminal_teclado_ok, &estado) != ERR_OK) {
        console_printf("SO: problema no acesso ao estado do teclado");
        self->erro_interno = true;
        return;
    }

    // Dispositivo ocupado, bloqueia o processo
    if (estado == 0) {
        console_printf("SO: dispositivo de entrada ocupado");
        so_processa_bloqueio_proc(self, processo, ESPERANDO_LEITURA);
        return;
    }

    int dado;
    int terminal_teclado = processo_calcula_terminal(D_TERM_A_TECLADO, terminal);
    // Obtem o dado do teminal e registra no reg A do processo
    if (es_le(self->es, terminal_teclado, &dado) != ERR_OK) {
        console_printf("SO: problema no acesso ao teclado");
        self->erro_interno = true;
        return;
    }

    processo_set_reg_A(processo, dado);
}

// implementação da chamada se sistema SO_ESCR
// escreve o valor do reg X na saída corrente do processo
static void so_chamada_escr(so_t *self)
{
    int terminal = processo_get_terminal(self->processo_corrente);
    processo_t *processo = self->processo_corrente;

    int estado;
    int terminal_tela_ok = processo_calcula_terminal(D_TERM_A_TELA_OK, terminal);
    if (es_le(self->es, terminal_tela_ok, &estado) != ERR_OK) {
        console_printf("SO: problema no acesso ao estado da tela");
        self->erro_interno = true;
        return;
    }

    // Se o dispositivo de saida estiver ocupado bloqueia o processo
    if (estado == 0) {
        console_printf("SO: dispositivo de saída ocupado");
        so_processa_bloqueio_proc(self, processo, ESPERANDO_ESCRITA);
        return;
    }

    int dado;
    int terminal_tela = processo_calcula_terminal(D_TERM_A_TELA, terminal);
    mem_le(self->mem, IRQ_END_X, &dado);
    if (es_escreve(self->es, terminal_tela, dado) != ERR_OK) {
        console_printf("SO: problema no acesso à tela");
        self->erro_interno = true;
        return;
    }

    processo_set_reg_A(processo, 0);
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
        processo_set_reg_A(processo_corrente, -1);
        return;
    }

    char nome[100];
    // Copia o valor do registrador X do processo para a memória
    if (!copia_str_da_mem(100, nome, self->mem, ender_nome_arq)) {
        console_printf("SO: Erro ao copiar o nome do arquivo do processo\n");
        processo_set_reg_A(processo_corrente, -1);
        return;
    }

    processo_t *novo_processo = so_adiciona_novo_processo(self, nome);
    if (novo_processo == NULL) {
        console_printf("SO: Erro ao criar o processo\n");
        processo_set_reg_A(processo_corrente, -1);
        return;
    }

    console_printf("SO: criando processo %d com nome %s", processo_get_pid(novo_processo), nome);
    processo_set_reg_A(processo_corrente, processo_get_pid(novo_processo));
}

// implementação da chamada se sistema SO_MATA_PROC mata o processo com pid X (ou o processo corrente se X é 0)
static void so_chamada_mata_proc(so_t *self)
{
    // PID do processo a ser morto está no registrador X do precesso corrente
    processo_t *processo_corrente = self->processo_corrente;
    if (processo_corrente == NULL)
        return;

    int pid_alvo = processo_get_reg_X(processo_corrente);
    processo_t *processo_alvo = processo_busca_por_pid(self->tabela_processos, self->n_processos, pid_alvo);

    // Processo que deve ser morto é o processo corrente
    if (pid_alvo == 0) {
        so_processa_morte_proc(self, processo_corrente);
        processo_set_reg_A(processo_corrente, 0);
        self->processo_corrente = NULL;
        return;
    }

    // Procuro o processo alvo na tabela e se exitir defino como morto
    if (processo_alvo != NULL) {
        so_processa_morte_proc(self, processo_corrente);
        processo_set_reg_A(processo_corrente, 0);
        return;
    }

    console_printf("SO: processo %d não encontrado", pid_alvo);
    processo_set_reg_A(processo_corrente, -1);
}

// implementação da chamada se sistema SO_ESPERA_PROC espera o fim do processo com pid X
static void so_chamada_espera_proc(so_t *self)
{
    processo_t *processo_corrente = self->processo_corrente;
    if (processo_corrente == NULL)
        return;
    int pid_alvo = processo_get_reg_X(processo_corrente);

    // Verifica se o processo corrente está esperando por si mesmo
    if (pid_alvo == processo_get_pid(processo_corrente)) {
        console_printf("SO: processo %d não pode esperar por si mesmo", pid_alvo);
        processo_set_reg_A(processo_corrente, -1);
        return;
    }

    // Se o processo alvo não estiver morto, bloqueia o processo corrente
    processo_t *processo_alvo = processo_busca_por_pid(self->tabela_processos, self->n_processos, pid_alvo);
    if (processo_get_estado(processo_alvo) != MORTO) {
        so_processa_bloqueio_proc(self, processo_corrente, ESPERANDO_PROCESSO);
        return;
    }

    // Se o processo alvo já estiver morto
    so_processa_desbloqueio_proc(self, processo_corrente, false);
    processo_set_reg_A(processo_corrente, 0);
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
