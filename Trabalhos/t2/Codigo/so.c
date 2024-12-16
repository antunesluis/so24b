
// so.c
// sistema operacional
// simulador de computador
// so24b

// INCLUDES {{{1
#include "so.h"
#include "dispositivos.h"
#include "err.h"
#include "fila_processos.h"
#include "gere_blocos.h"
#include "instrucao.h"
#include "irq.h"
#include "memoria.h"
#include "mmu.h"
#include "processo.h"
#include "programa.h"
#include "so.h"
#include "tabpag.h"

#include <assert.h>
#include <stdbool.h>

#include <stdlib.h>

// CONSTANTES E TIPOS {{{1

#define INTERVALO_INTERRUPCAO 50
#define QUANTUM_INICIAL 10
#define ESCALONADOR_ATUAL SIMPLES
#define ALGORITMO_SUBSTITUICAO_ATUAL SIMPLES

#define FILA_PROCESSOS_INICIAL 5
#define MAX_PROCESSOS 4
#define NUM_TERMINAIS 4
#define FATOR_MULTIPLICADOR_LIMITE_PROCESSOS 2
#define FATOR_CRESCIMENTO_FILA 2

#define TAMANHO_MEMORIA_SECUNDARIA = 10000
#define TOTAL_QUADROS_MEMORIA = 100

#define TEMPO_MUDANCA_PAGINA_CPU 2

// Não tem processos nem memória virtual, mas é preciso usar a paginação,
//   pelo menos para implementar relocação, já que os programas estão sendo
//   todos montados para serem executados no endereço 0 e o endereço 0
//   físico é usado pelo hardware nas interrupções.
// Os programas estão sendo carregados no início de um quadro, e usam quantos
//   quadros forem necessárias. Para isso a variável quadro_livre contém
//   o número do primeiro quadro da memória principal que ainda não foi usado.
//   Na carga do processo, a tabela de páginas (deveria ter uma por processo,
//   mas não tem processo) é alterada para que o endereço virtual 0 resulte
//   no quadro onde o programa foi carregado. Com isso, o programa carregado
//   é acessível, mas o acesso ao anterior é perdido.

// t2: a interface de algumas funções que manipulam memória teve que ser alterada,
//   para incluir o processo ao qual elas se referem. Para isso, precisa de um
//   tipo para o processo. Neste código, não tem processos implementados, e não
//   tem um tipo para isso. Chutei o tipo int. Foi necessário também um valor para
//   representar a inexistência de um processo, coloquei -1. Altere para o seu
//   tipo, ou substitua os usos de processo_t e NENHUM_PROCESSO para o seu tipo.
#define NENHUM_PROCESSO NULL

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
    mmu_t *mmu;
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
    int t_relogio_atual;

    mem_t *memoria_secundaria;
    int prox_endereco_mem_sec; // Próximo endereço disponível na memória secundária

    gere_blocos_t *gere_blocos;
    int n_paginas_fisica;
    int quadro_livre_inicial;
    int quadro_livre;

    metricas_so_t *metricas;
};

typedef enum escalonador_t
{
    ROUND_ROBIN,
    SIMPLES,
    PRIORIDADE,
} escalonador_t;

typedef enum algoritmo_substituicao_t
{
    FIFO,
    SEGUNDA_CHANCE,
} algoritmo_substituicao_t;

// função de tratamento de interrupção (entrada no SO)
static int so_trata_interrupcao(void *argC, int reg_A);

// funções auxiliares
// no t2, foi adicionado o 'processo' aos argumentos dessas funções
// carrega o programa na memória virtual de um processo; retorna end. inicial
static int so_carrega_programa(so_t *self, processo_t *processo, char *nome_do_executavel);
// copia para str da memória do processo, até copiar um 0 (retorna true) ou tam bytes
static bool so_copia_str_do_processo(so_t *self, int tam, char str[tam], int end_virt, processo_t *processo);

// CRIAÇÃO {{{1

static metricas_so_t *cria_metricas_so();
static void gera_relatorio_final(so_t *self);
static void finaliza_metricas(so_t *self);

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

static void configura_cpu(so_t *self)
{
    //   so_trata_interrupcao, com primeiro argumento um ptr para o SO
    cpu_define_chamaC(self->cpu, so_trata_interrupcao, self);

    // coloca o tratador de interrupção na memória
    int ender = so_carrega_programa(self, NENHUM_PROCESSO, "trata_int.maq");
    if (ender != IRQ_END_TRATADOR) {
        console_printf("SO: problema na carga do programa de tratamento de interrupção");
        self->erro_interno = true;
    }

    // programa o relógio para gerar uma interrupção após INTERVALO_INTERRUPCAO
    if (es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO) != ERR_OK) {
        console_printf("SO: problema na programação do timer");
        self->erro_interno = true;
    }
}

so_t *so_cria(cpu_t *cpu, mem_t *mem, mmu_t *mmu, es_t *es, console_t *console)
{
    so_t *self = malloc(sizeof(*self));
    assert(self != NULL);

    self->cpu = cpu;
    self->mem = mem;
    self->memoria_secundaria = mem_cria(10000);
    self->mmu = mmu;
    self->es = es;
    self->console = console;
    self->erro_interno = false;

    self->proximo_pid = 1;
    self->n_processos = 0;
    self->t_relogio_atual = -1;

    self->processo_corrente = NULL;
    self->quantum = QUANTUM_INICIAL;
    self->limite_processos = MAX_PROCESSOS;

    self->prox_endereco_mem_sec = 0;
    self->n_paginas_fisica = mem_tam(self->mem) / TAM_PAGINA;
    self->quadro_livre_inicial = 99 / TAM_PAGINA + 1;
    self->quadro_livre = 0;

    self->tabela_processos = tabela_cria(self);
    self->fila_prontos = fila_processos_cria();
    self->metricas = cria_metricas_so();
    self->gere_blocos = gere_blocos_cria(self->n_paginas_fisica);
    configura_cpu(self);

    return self;
}

static void so_encerra_atividade(so_t *self)
{
    console_printf("SO: encerrando atividades");
    finaliza_metricas(self);
    gera_relatorio_final(self);
    so_destroi(self);
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
    self->t_relogio_atual = self->metricas->tempo_total_execucao;

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

static void so_processa_desbloqueio_proc(so_t *self, processo_t *processo, bool insere_fim_fila)
{
    console_printf("SO: processo %d desbloqueado\n", processo_get_pid(processo));
    processo_desbloqueia(processo);
    debug_tabela_processos(self->tabela_processos, self->limite_processos);

    if (insere_fim_fila) {
        fila_processos_insere(self->fila_prontos, processo);
    }
}

static void so_processa_bloqueio_proc(so_t *self, processo_t *processo, motivo_bloqueio_t motivo)
{
    if (processo == NULL)
        return;

    console_printf("SO: processo %d bloqueado por motivo %s\n", processo_get_pid(processo),
                   processo_motivo_para_string(motivo));

    processo_bloqueia(processo, motivo);
    debug_tabela_processos(self->tabela_processos, self->limite_processos);

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
            console_printf("SO: adicionando processo %d na posição %d da tabela de processos\n",
                           processo_get_pid(processo), i);
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
    so_verifica_e_redimensiona_tabela(self);

    // Cria o processo e incrementa o PID
    int pid = self->proximo_pid++;
    processo_t *processo = processo_cria(pid, 0);
    if (processo == NULL) {
        console_printf("SO: Erro ao criar o processo para '%s'\n", nome_do_executavel);
        return NULL;
    }

    // Carrega o programa
    int pc = so_carrega_programa(self, processo, nome_do_executavel);
    if (pc < 0) {
        console_printf("SO: Erro ao carregar o programa '%s'\n", nome_do_executavel);
        processo_destroi(processo);
        return NULL;
    }
    // Define o PC
    processo_set_pc(processo, pc);

    // Adiciona novo processo na tabela
    so_adiciona_processo_tabela(self, processo);
    fila_processos_insere(self->fila_prontos, processo);

    self->processo_corrente = processo;
    self->n_processos++;

    /* debug_tabela_processos(self->tabela_processos, self->limite_processos); */
    /* debug_fila_processos(self->fila_prontos); */
    return processo;
}

// MEMORIA {{{1

int tempo_atual_sistema(so_t *self)
{
    int tempo_atual;
    if (es_le(self->es, D_RELOGIO_INSTRUCOES, &tempo_atual) != ERR_OK) {
        console_printf("SO: problema na leitura do relógio");
        self->erro_interno = true;
        return -1;
    }
    return tempo_atual;
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

// função a ser chamada pela CPU quando executa a instrução CHAMAC, no tratador de
//   interrupção em assembly
// essa é a única forma de entrada no SO depois da inicialização
// na inicialização do SO, a CPU foi programada para chamar esta função para executar
//   a instrução CHAMAC
// a instrução CHAMAC só deve ser executada pelo tratador de interrupção
//
// o primeiro argumento é um ponteiro para o SO, o segundo é a identificação
//   da interrupção
// o valor retornado por esta função é colocado no registrador A, e pode ser
//   testado pelo código que está após o CHAMAC. No tratador de interrupção em
//   assembly esse valor é usado para decidir se a CPU deve retornar da interrupção
//   (e executar o código de usuário) ou executar PARA e ficar suspensa até receber
//   outra interrupção

static int so_trata_interrupcao(void *argC, int reg_A)
{
    so_t *self = argC;
    irq_t irq = reg_A;

    // Atualizo todas as métricas do simulador e dos processos atuais.
    so_atualiza_metricas_globais(self, irq);
    // esse print polui bastante, recomendo tirar quando estiver com mais confiança
    console_printf("SO: recebi IRQ %d (%s)", irq, irq_nome(irq));
    // salva o estado da cpu no descritor do processo que foi interrompido
    so_salva_estado_da_cpu(self);
    // faz o atendimento da interrupção
    so_trata_irq(self, irq);
    // faz o processamento independente da interrupção
    so_trata_pendencias(self);
    // escolhe o próximo processo a executar
    so_escolhe_e_executa_escalonador(self, ESCALONADOR_ATUAL);

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
    int pc, reg_A, reg_X, complemento, err;
    mem_le(self->mem, IRQ_END_PC, &pc);
    mem_le(self->mem, IRQ_END_A, &reg_A);
    mem_le(self->mem, IRQ_END_X, &reg_X);
    mem_le(self->mem, IRQ_END_complemento, &complemento);
    mem_le(self->mem, IRQ_END_erro, &err);

    processo_set_pc(processo_corrente, pc);
    processo_set_reg_A(processo_corrente, reg_A);
    processo_set_reg_X(processo_corrente, reg_X);
    processo_set_complemento(processo_corrente, complemento);
    processo_set_erro(processo_corrente, err);
}

static int escolhe_pagina_substituir(so_t *self)
{
    switch (ALGORITMO_SUBSTITUICAO_ATUAL) {
    case FIFO:
        console_printf("SO: escolhendo página para substituir com FIFO");
        break;
    case SEGUNDA_CHANCE:
        console_printf("SO: escolhendo página para substituir com Segunda Chance");
        break;
    default:
        console_printf("SO: algoritmo de substituição de página não reconhecido");
        break;
    }
    return -1;
}

static bool transf_pag_mem_sec_para_mem_princ(so_t *self, int end_mem_sec, int quadro_livre, int end_virt_ini,
                                              int end_virt_fim)
{
    for (int end_virt = end_virt_ini; end_virt <= end_virt_fim; end_virt++) {
        int dado;
        // Leio da memória secundária o valor destino
        if (mem_le(self->memoria_secundaria, end_mem_sec, &dado) != ERR_OK) {
            console_printf("SO: problema ao ler da memória secundária");
            return false;
        }
        // Calculo o endereço físico da página
        int end_fisico_pag = quadro_livre * TAM_PAGINA + end_virt - end_virt_ini;
        if (mem_escreve(self->mem, end_fisico_pag, dado) != ERR_OK) {
            // Escrevo na memória principal o valor lido da memória secundaria
            console_printf("SO: problema ao escrever na memória principal");
            return false;
        }
        end_mem_sec++;
    }
    return true;
}

static void so_trata_page_fault_bloco_livre(so_t *self, int end_causador)
{
    int free_page = gere_blocos_buscar_proximo(self->gere_blocos);

    int end_disk_ini = processo_get_end_mem_sec(self->processo_corrente) + end_causador - end_causador % TAM_PAGINA;
    int end_disk = end_disk_ini;

    int end_virt_ini = end_causador;
    int end_virt_fim = end_virt_ini + TAM_PAGINA - 1;

    if (transf_pag_mem_sec_para_mem_princ(self, end_disk, free_page, end_virt_ini, end_virt_fim)) {
        gere_blocos_atualiza_bloco(self->gere_blocos, free_page, processo_get_pid(self->processo_corrente),
                                   end_causador / TAM_PAGINA);

        tabpag_t *tabela = processo_get_tabpag(self->processo_corrente);
        tabpag_define_quadro(tabela, end_causador / TAM_PAGINA, free_page);
        return;
    }

    console_printf("SO: problema ao transferir página da memória secundária para a memória principal");
    self->erro_interno = true;
}

static void trata_falha_pagina_substituicao(so_t *self, int end_ausente)
{
    console_printf("SO: SUBSTUTUICAO de pagina necessaria");

    int pagina_removida = escolhe_pagina_substituir(self);

    if (pagina_removida == -1) {
        console_printf("SO: PROBLEMA AO ESCOLHER PAGINA");
        return;
    }
}

static void so_trata_falha_pagina(so_t *self)
{
    console_printf("SO: tratando página ausente");
    int end_ausente = processo_get_complemento(self->processo_corrente);

    // Verifica se existe bloco disponivel na memoria principal para importar da memoria secundária
    if (gere_blocos_tem_disponivel(self->gere_blocos)) {
        console_printf("SO: BLOCO DISPONIVEL na memória principal");
        // Transfere a página da memória secundária para o bloco disponivel na memória principal
        so_trata_page_fault_bloco_livre(self, end_ausente);
    } else {
        console_printf("SO: SUBSTITUINDO página na memória principal");
        // Substitui uma página da memória principal por uma da memória sec
        trata_falha_pagina_substituicao(self, end_ausente);
    }

    int tempo_sistema = tempo_atual_sistema(self);
    so_processa_bloqueio_proc(self, self->processo_corrente, ESPERANDO_PAGINA);
    processo_set_tempo_desbloqueio(self->processo_corrente, tempo_sistema + TEMPO_MUDANCA_PAGINA_CPU);
}

static void trata_pendencia_leitura(so_t *self, processo_t *processo)
{
    int estado;
    int terminal_proc = processo_get_terminal(processo);
    int terminal_teclado_ok = processo_calcula_terminal(D_TERM_A_TECLADO_OK, terminal_proc);
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
    int terminal_leitura = processo_calcula_terminal(D_TERM_A_TECLADO, terminal_proc);
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
    int terminal_proc = processo_get_terminal(processo);
    int terminal_tela_ok = processo_calcula_terminal(D_TERM_A_TELA_OK, terminal_proc);
    if (es_le(self->es, terminal_tela_ok, &estado) != ERR_OK) {
        console_printf("SO: problema no acesso ao estado da tela");
        self->erro_interno = true;
        return;
    }

    // Se o dispositivo não estiver disponivel retorna
    if (estado == 0) {
        return;
    }
    console_printf("SO: terminal %d desbloqueado para escrita", terminal_proc);

    int dado = processo_get_reg_X(processo);
    int terminal_tela = processo_calcula_terminal(D_TERM_A_TELA, terminal_proc);
    if (es_escreve(self->es, terminal_tela, dado) != ERR_OK) {
        console_printf("SO: problema no acesso à tela");
        self->erro_interno = true;
        return;
    }

    processo_set_reg_A(processo, 0);
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

static void trata_pendencia_pagina(so_t *self, processo_t *processo)
{
    int tempo_desbloqueio = processo_get_tempo_desbloqueio(processo);
    int tempo_sistema = tempo_atual_sistema(self);

    console_printf("SO: tempo proc: %d, tempo sistema: %d", tempo_desbloqueio, tempo_sistema);

    if (tempo_sistema >= tempo_desbloqueio) {
        so_processa_desbloqueio_proc(self, processo, true);
        processo_set_reg_A(processo, 0);
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
            case ESPERANDO_PAGINA:
                // Verifica se a memória secundária está disponível
                trata_pendencia_pagina(self, processo);
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
        /* debug_tabela_processos(self->tabela_processos, self->limite_processos); */
        return;
    }

    // Se não houver processos prontos, verifica se há processos bloqueados
    if (processo_busca_primeiro_em_estado(self->tabela_processos, self->n_processos, BLOQUEADO) != NULL) {
        self->processo_corrente = NULL;
        /* debug_tabela_processos(self->tabela_processos, self->limite_processos); */
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
        /* debug_fila_processos(self->fila_prontos); */
    }

    // Busca o proximo processo pronto e define como processo corrente
    if (!fila_processos_vazia(self->fila_prontos)) {
        // Obtem o primeiro processo pronto da fila de prontos e define como processo corrente
        self->processo_corrente = fila_processos_primeiro(self->fila_prontos);
        self->quantum = QUANTUM_INICIAL;
        /* debug_fila_processos(self->fila_prontos); */
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
        console_printf("SO: processo %d continua executando", processo_get_pid(processo_corrente));
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

        console_printf("SO: preempção no processo %d", processo_get_pid(processo_corrente));
        /* debug_fila_processos(self->fila_prontos); */
    }

    // Busca o proximo processo pronto e define como processo corrente
    if (!fila_processos_vazia(self->fila_prontos)) {
        fila_processos_ordena_prioridade(self->fila_prontos);

        console_printf("SO: escalonando por prioridade");
        /* debug_fila_processos(self->fila_prontos); */

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
    // t1: se houver processo corrente, coloca o estado desse processo onde ele
    //   será recuperado pela CPU (em IRQ_END_*) e retorna 0, senão retorna 1
    // o valor retornado será o valor de retorno de CHAMAC
    processo_t *processo_corrente = self->processo_corrente;
    if (self->erro_interno || processo_corrente == NULL)
        return 1;

    // Obtem o estado do processo corrente
    int a, x, pc, complemento;
    a = processo_get_reg_A(processo_corrente);
    x = processo_get_reg_X(processo_corrente);
    pc = processo_get_pc(processo_corrente);
    complemento = processo_get_complemento(processo_corrente);
    tabpag_t *tabpag = processo_get_tabpag(processo_corrente);

    // Atualiza os registradores do processador
    mem_escreve(self->mem, IRQ_END_A, a);
    mem_escreve(self->mem, IRQ_END_X, x);
    mem_escreve(self->mem, IRQ_END_PC, pc);
    mem_escreve(self->mem, IRQ_END_complemento, complemento);
    mem_escreve(self->mem, IRQ_END_erro, ERR_OK);

    // Atualiza a tabela de páginas do processo corrente
    mmu_define_tabpag(self->mmu, tabpag);

    return 0;
}

// TRATAMENTO DE UMA IRQ {{{1

// funções auxiliares para tratar cada tipo de interrupção
static void so_trata_irq_reset(so_t *self);
static void so_trata_irq_chamada_sistema(so_t *self);
static void so_trata_irq_err_cpu(so_t *self);
static void so_trata_irq_relogio(so_t *self);
static void so_trata_irq_desconhecida(so_t *self, int irq);

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
    // t2: deveria criar um processo, e programar a tabela de páginas dele
    processo_t *init_processo = so_adiciona_novo_processo(self, "init.maq");
    if (init_processo == NULL) {
        console_printf("SO: problema na criação do processo inicial");
        self->erro_interno = true;
        return;
    }

    console_printf("SO: processo inicial criado");

    // altera o PC para o endereço de carga (deve ter sido o endereço virtual 0)
    mem_escreve(self->mem, IRQ_END_PC, processo_get_pc(self->processo_corrente));
    // passa o processador para modo usuário
    mem_escreve(self->mem, IRQ_END_modo, usuario);
}

// interrupção gerada quando a CPU identifica um erro
static void so_trata_irq_err_cpu(so_t *self)
{

    int err_int = processo_get_erro(self->processo_corrente);
    if (err_int == ERR_PAG_AUSENTE) {
        console_printf("SO: PÁGINA AUSENTE");
        so_trata_falha_pagina(self);
        return;
    }

    // Endereço traduzido pela mmu não foi reconhecido pela memoria
    if (err_int == ERR_INSTR_INV) {
        console_printf("SO: endereço traduzido não reconhecido pela memoria");
        self->erro_interno = true;
    }

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
    if (!so_copia_str_do_processo(self, 100, nome, ender_nome_arq, processo_corrente)) {
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
        so_processa_morte_proc(self, processo_alvo);
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

// funções auxiliares
static int so_carrega_programa_na_memoria_fisica(so_t *self, programa_t *programa);
static int so_carrega_programa_na_memoria_virtual(so_t *self, programa_t *programa, processo_t *processo);

// carrega o programa na memória de um processo ou na memória física se NENHUM_PROCESSO
// retorna o endereço de carga ou -1
static int so_carrega_programa(so_t *self, processo_t *processo, char *nome_do_executavel)
{
    console_printf("SO: carga de '%s'", nome_do_executavel);

    programa_t *programa = prog_cria(nome_do_executavel);
    if (programa == NULL) {
        console_printf("Erro na leitura do programa '%s'\n", nome_do_executavel);
        return -1;
    }

    int end_carga;
    if (processo == NENHUM_PROCESSO) {
        end_carga = so_carrega_programa_na_memoria_fisica(self, programa);
    } else {
        end_carga = so_carrega_programa_na_memoria_virtual(self, programa, processo);
        processo_set_end_mem_sec(processo, end_carga);
        end_carga = 0;
    }

    console_printf("SO: programa '%s' carregado em %d", nome_do_executavel, end_carga);
    prog_destroi(programa);
    return end_carga;
}

static int so_carrega_programa_na_memoria_fisica(so_t *self, programa_t *programa)
{
    int end_ini = prog_end_carga(programa);
    int end_fim = end_ini + prog_tamanho(programa);

    for (int end = end_ini; end < end_fim; end++) {
        if (mem_escreve(self->mem, end, prog_dado(programa, end)) != ERR_OK) {
            console_printf("Erro na carga da memória, endereco %d\n", end);
            return -1;
        }
    }

    gere_blocos_cadastra_bloco(self->gere_blocos, end_ini, end_fim, 0);

    console_printf("carregado na memória física, %d-%d", end_ini, end_fim);
    return end_ini;
}

static int so_carrega_programa_na_memoria_virtual(so_t *self, programa_t *programa, processo_t *processo)
{
    // meu: carregará programa na memória secundária

    // t2: isto tá furado...
    // está simplesmente lendo para o próximo quadro que nunca foi ocupado,
    //   nem testa se tem memória disponível
    // com memória virtual, a forma mais simples de implementar a carga de um
    //   programa é carregá-lo para a memória secundária, e mapear todas as páginas
    //   da tabela de páginas do processo como inválidas. Assim, as páginas serão
    //   colocadas na memória principal por demanda. Para simplificar ainda mais, a
    //   memória secundária pode ser alocada da forma como a principal está sendo
    //   alocada aqui (sem reuso)

    // carrega o programa na memória secundária
    int end_disk_ini = self->prox_endereco_mem_sec;
    int end_disk = end_disk_ini;

    int end_virt_ini = 0;
    int end_virt_fim = end_virt_ini + prog_tamanho(programa) - 1;

    self->prox_endereco_mem_sec = end_disk_ini + end_virt_fim + 1;

    for (int end_virt = end_virt_ini; end_virt <= end_virt_fim; end_virt++) {
        if (mem_escreve(self->memoria_secundaria, end_disk, prog_dado(programa, end_virt)) != ERR_OK) {
            console_printf("Erro na carga da memória, end virt %d fís %d\n", end_virt, end_disk);
            return -1;
        }
        end_disk++;
    }
    console_printf("SO: carregado na memória secundária virt:%d a %d, sec: %d a %d", end_virt_ini, end_virt_fim,
                   end_disk_ini, end_disk - 1);

    return end_disk_ini;
}

// ACESSO À MEMÓRIA DOS PROCESSOS {{{1

// copia uma string da memória do processo para o vetor str.
// retorna false se erro (string maior que vetor, valor não char na memória,
//   erro de acesso à memória)
// O endereço é um endereço virtual de um processo.
// T2: Com memória virtual, cada valor do espaço de endereçamento do processo
//   pode estar em memória principal ou secundária (e tem que achar onde)
static bool so_copia_str_do_processo(so_t *self, int tam, char str[tam], int end_virt, processo_t *processo)
{
    if (processo == NENHUM_PROCESSO)
        return false;
    for (int indice_str = 0; indice_str < tam; indice_str++) {
        int caractere;
        // não tem memória virtual implementada, posso usar a mmu para traduzir
        //   os endereços e acessar a memória
        if (mmu_le(self->mmu, end_virt + indice_str, &caractere, usuario) != ERR_OK) {
            return false;
            // se não está na memória principal, busca na memória secundária (disco)
            mem_le(self->memoria_secundaria, processo_get_end_mem_sec(self->processo_corrente) + end_virt + indice_str,
                   &caractere);
        }
        if (caractere < 0 || caractere > 255) {
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

