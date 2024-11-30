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
#define INTERVALO_INTERRUPCAO 50  // em instruções executadas
#define MAX_PROCESSOS 10
#define FATOR_MULTIPLICADOR_LIMITE_PROCESSOS 2
#define NUM_TERMINAIS 4

typedef enum { PRONTO, EXECUTANDO, BLOQUEADO, MORTO } estado_t;

typedef struct processo_t {
    int pid;    // Identificador único do processo
    int pc;     // Contador de programa
    int reg_A;  // Registrador A
    int reg_X;  // Registrador X

    estado_t estado_atual;  // Estado atual do processo
    int terminal;
    /* motivo_bloqueio_t motivo;  // Motivo do bloqueio (se bloqueado) */
} processo_t;

struct so_t {
    cpu_t *cpu;
    mem_t *mem;
    es_t *es;
    console_t *console;
    bool erro_interno;

    struct processo_t **tabela_processos;  // Lista de todos os processos
    struct processo_t *processo_corrente;  // Processo atualmente executando

    int limite_processos;  // Quantidade máxima de processos
    int n_processos;       // Numero atual de processos
    int proximo_pid;       // PID do próximo processo a ser criado
};

// t1: tabela de processos, processo corrente, pendências, etc
// função de tratamento de interrupção (entrada no SO)
static int so_trata_interrupcao(void *argC, int reg_A);

// funções auxiliares:
// carrega o programa contido no arquivo na memória do processador; retorna end. inicial
static int so_carrega_programa(so_t *self, char *nome_do_executavel);
// copia para str da memória do processador, até copiar um 0 (retorna true) ou tam bytes
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender);

// PROCESSOS {{{1

static processo_t *cria_processo(int pid, int pc) {
    processo_t *p = malloc(sizeof(processo_t));

    p->pid = pid;
    p->estado_atual = PRONTO;
    p->pc = pc;
    p->reg_A = 0;
    p->reg_X = 0;
    p->terminal = (pid % NUM_TERMINAIS) * 4;

    return p;
}

static void muda_estado_processo(processo_t *processo, estado_t novo_estado) {
    console_printf("SO: processo %d passa de %d para %d\n", processo->pid, processo->estado_atual, novo_estado);
    processo->estado_atual = novo_estado;
}

static void mata_processo(so_t *self, processo_t *processo) {
    console_printf("SO: matando processo %d", processo->pid);
    muda_estado_processo(processo, MORTO);
}

static processo_t *busca_primeiro_processo_em_estado(so_t *self, estado_t estado) {
    for (int i = 0; i < self->n_processos; i++) {
        processo_t *processo = self->tabela_processos[i];
        if (processo != NULL && processo->estado_atual == estado) {
            return processo;
        }
    }
    return NULL;
}

static processo_t *busca_processo(so_t *self, int pid) {
    for (int i = 0; i < self->n_processos; i++) {
        processo_t *processo = self->tabela_processos[i];
        if (processo != NULL && processo->pid == pid) {
            return processo;
        }
    }
    return NULL;
}

static void so_verifica_e_redimensiona_tabela(so_t *self) {
    if (self->n_processos < self->limite_processos) return;  // Não precisa redimensionar

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
static void adiciona_processo_tabela(so_t *self, processo_t *processo) {
    for (int i = 0; i < self->limite_processos; i++) {
        if (self->tabela_processos[i] == NULL) {
            self->tabela_processos[i] = processo;
            return;
        }
    }
    console_printf("SO: Erro ao adicionar processo na tabela\n");
    mem_escreve(self->mem, IRQ_END_A, -1);
}

// Instancia e adiciona na tabela um novo processo
static processo_t *so_adiciona_novo_processo(so_t *self, char *nome_do_executavel) {
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
    self->processo_corrente = processo;
    self->n_processos++;
    adiciona_processo_tabela(self, processo);

    return processo;
}

// CRIAÇÃO {{{1

so_t *so_cria(cpu_t *cpu, mem_t *mem, es_t *es, console_t *console) {
    so_t *self = malloc(sizeof(*self));
    if (self == NULL) return NULL;

    self->cpu = cpu;
    self->mem = mem;
    self->es = es;
    self->console = console;
    self->erro_interno = false;

    self->processo_corrente = NULL;
    self->limite_processos = MAX_PROCESSOS;
    self->tabela_processos = calloc(self->limite_processos, sizeof(processo_t *));
    self->n_processos = 0;
    self->proximo_pid = 1;

    // quando a CPU executar uma instrução CHAMAC, deve chamar a função
    //   so_trata_interrupcao, com primeiro argumento um ptr para o SO
    cpu_define_chamaC(self->cpu, so_trata_interrupcao, self);

    // coloca o tratador de interrupção na memória
    // quando a CPU aceita uma interrupção, passa para modo supervisor,
    //   salva seu estado à partir do endereço 0, e desvia para o endereço
    //   IRQ_END_TRATADOR
    // colocamos no endereço IRQ_END_TRATADOR o programa de tratamento
    //   de interrupção (escrito em asm). esse programa deve conter a
    //   instrução CHAMAC, que vai chamar so_trata_interrupcao (como
    //   foi definido acima)
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

void so_destroi(so_t *self) {
    cpu_define_chamaC(self->cpu, NULL, NULL);
    free(self);
}

// TRATAMENTO DE INTERRUPÇÃO {{{1

// funções auxiliares para o tratamento de interrupção
static void so_salva_estado_da_cpu(so_t *self);
static void so_trata_irq(so_t *self, int irq);
static void so_trata_pendencias(so_t *self);
static void so_escalona(so_t *self);
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
static int so_trata_interrupcao(void *argC, int reg_A) {
    irq_t irq = reg_A;
    so_t *self = argC;
    // esse print polui bastante, recomendo tirar quando estiver com mais confiança
    console_printf("SO: recebi IRQ %d (%s)", irq, irq_nome(irq));
    // salva o estado da cpu no descritor do processo que foi interrompido
    so_salva_estado_da_cpu(self);
    // faz o atendimento da interrupção
    so_trata_irq(self, irq);
    // faz o processamento independente da interrupção
    so_trata_pendencias(self);
    // escolhe o próximo processo a executar
    so_escalona(self);
    // recupera o estado do processo escolhido
    return so_despacha(self);
}

static void so_salva_estado_da_cpu(so_t *self) {
    // t1: salva os registradores que compõem o estado da cpu no descritor do
    //   processo corrente. os valores dos registradores foram colocados pela
    //   CPU na memória, nos endereços IRQ_END_*
    // se não houver processo corrente, não faz nada

    processo_t *processo_corrente = self->processo_corrente;
    if (processo_corrente == NULL) return;

    // Obtem os registradores salvos da interrupção anterior e atualiza o estado do processo
    mem_le(self->mem, IRQ_END_PC, &processo_corrente->pc);
    mem_le(self->mem, IRQ_END_A, &processo_corrente->reg_A);
    mem_le(self->mem, IRQ_END_X, &processo_corrente->reg_X);
}
static void so_trata_pendencias(so_t *self) {
    // t1: realiza ações que não são diretamente ligadas com a interrupção que
    //   está sendo atendida:
    // - E/S pendente
    // - desbloqueio de processos
    // - contabilidades
}

static void so_escalona(so_t *self) {
    // escolhe o próximo processo a executar, que passa a ser o processo
    //   corrente; pode continuar sendo o mesmo de antes ou não
    // t1: na primeira versão, escolhe um processo caso o processo corrente não possa continuar
    //   executando. depois, implementar escalonador melhor

    // Verifica se o processo corrente pode continuar executando
    if (self->processo_corrente != NULL && self->processo_corrente->estado_atual == PRONTO) {
        // Continua com o processo corrente;
        console_printf("SO: processo corrente pronto escolhido pelo escalonador\n");
        return;
    }

    // Busca o proximo processo pronto e define como processo corrente executando
    processo_t *proximo = busca_primeiro_processo_em_estado(self, PRONTO);
    if (proximo != NULL) {
        self->processo_corrente = proximo;
        // muda_estado_processo(proximo, EXECUTANDO);
        return;
    }

    console_printf("SO: nao existem processos prontos\n");
    // self->processo_corrente = NULL;
    self->erro_interno = true;
}

static int so_despacha(so_t *self) {
    // t1: se houver processo corrente, coloca o estado desse processo onde ele
    //   será recuperado pela CPU (em IRQ_END_*) e retorna 0, senão retorna 1
    // o valor retornado será o valor de retorno de CHAMAC
    if (self->erro_interno) return 1;

    processo_t *processo_corrente = self->processo_corrente;
    if (processo_corrente == NULL) return 1;

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
static void so_trata_irq(so_t *self, int irq) {
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
static void so_trata_irq_reset(so_t *self) {
    // t1: deveria criar um processo para o init, e inicializar o estado do
    //   processador para esse processo com os registradores zerados, exceto
    //   o PC e o modo.
    // como não tem suporte a processos, está carregando os valores dos
    //   registradores diretamente para a memória, de onde a CPU vai carregar
    //   para os seus registradores quando executar a instrução RETI

    // coloca o programa init na memória
    processo_t *init_processo = so_adiciona_novo_processo(self, "init.maq");

    if (init_processo->pc != 100) {
        console_printf("SO: problema na carga do programa inicial");
        self->erro_interno = true;
        return;
    }

    // altera o PC para o endereço de carga
    mem_escreve(self->mem, IRQ_END_PC, init_processo->pc);
    // passa o processador para modo usuário
    mem_escreve(self->mem, IRQ_END_modo, usuario);
}

// interrupção gerada quando a CPU identifica um erro
static void so_trata_irq_err_cpu(so_t *self) {
    // Ocorreu um erro interno na CPU
    // O erro está codificado em IRQ_END_erro
    // Em geral, causa a morte do processo que causou o erro
    // Ainda não temos processos, causa a parada da CPU
    int err_int;
    // t1: com suporte a processos, deveria pegar o valor do registrador erro
    //   no descritor do processo corrente, e reagir de acordo com esse erro
    //   (em geral, matando o processo)
    mem_le(self->mem, IRQ_END_erro, &err_int);
    err_t err = err_int;
    console_printf("SO: IRQ não tratada -- erro na CPU: %s", err_nome(err));
    self->erro_interno = true;
}

// interrupção gerada quando o timer expira
static void so_trata_irq_relogio(so_t *self) {
    // rearma o interruptor do relógio e reinicializa o timer para a próxima interrupção
    err_t e1, e2;
    e1 = es_escreve(self->es, D_RELOGIO_INTERRUPCAO, 0);  // desliga o sinalizador de interrupção
    e2 = es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO);
    if (e1 != ERR_OK || e2 != ERR_OK) {
        console_printf("SO: problema da reinicialização do timer");
        self->erro_interno = true;
    }
    // t1: deveria tratar a interrupção
    //   por exemplo, decrementa o quantum do processo corrente, quando se tem
    //   um escalonador com quantum
    // console_printf("SO: interrupção do relógio (não tratada)");
}

// foi gerada uma interrupção para a qual o SO não está preparado
static void so_trata_irq_desconhecida(so_t *self, int irq) {
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

static void so_trata_irq_chamada_sistema(so_t *self) {
    // a identificação da chamada está no registrador A
    // t1: com processos, o reg A tá no descritor do processo corrente
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
            // t1: deveria matar o processo
            self->erro_interno = true;
    }
}

static int calcula_terminal_processo(int dispositivo, int terminal_base) {
    // 16 dispositivos terminal, 4 terminais, cada terminal possui 4 dispositivos
    return dispositivo + terminal_base;
}

// implementação da chamada se sistema SO_LE
// faz a leitura de um dado da entrada corrente do processo, coloca o dado no reg A
static void so_chamada_le(so_t *self) {
    // implementação com espera ocupada
    //   T1: deveria realizar a leitura somente se a entrada estiver disponível,
    //     senão, deveria bloquear o processo.
    //   no caso de bloqueio do processo, a leitura (e desbloqueio) deverá
    //     ser feita mais tarde, em tratamentos pendentes em outra interrupção,
    //     ou diretamente em uma interrupção específica do dispositivo, se for
    //     o caso
    // implementação lendo direto do terminal A
    //   T1: deveria usar dispositivo de entrada corrente do processo

    int terminal = self->processo_corrente->terminal;
    int terminal_teclado_ok = calcula_terminal_processo(D_TERM_A_TECLADO_OK, terminal);
    int terminal_teclado = calcula_terminal_processo(D_TERM_A_TECLADO, terminal);

    for (;;) {
        int estado;

        if (es_le(self->es, terminal_teclado_ok, &estado) != ERR_OK) {
            console_printf("SO: problema no acesso ao estado do teclado");
            return;
        }
        self->erro_interno = true;
        // como não está saindo do SO, a unidade de controle não está executando seu laço.
        // esta gambiarra faz pelo menos a console ser atualizada
        if (estado != 0) break;
        // T1: com a implementação de bloqueio de processo, esta gambiarra não
        //   deve mais existir.
        console_tictac(self->console);
    }

    int dado;
    if (es_le(self->es, terminal_teclado, &dado) != ERR_OK) {
        console_printf("SO: problema no acesso ao teclado");
        self->erro_interno = true;
        return;
    }

    // escreve no reg A do processador
    // (na verdade, na posição onde o processador vai pegar o A quando retornar da int)
    // T1: se houvesse processo, deveria escrever no reg A do processo
    // T1: o acesso só deve ser feito nesse momento se for possível; se não, o processo
    //   é bloqueado, e o acesso só deve ser feito mais tarde (e o processo desbloqueado)
    mem_escreve(self->mem, IRQ_END_A, dado);
}

// implementação da chamada se sistema SO_ESCR
// escreve o valor do reg X na saída corrente do processo
static void so_chamada_escr(so_t *self) {
    // implementação com espera ocupada
    //   T1: deveria bloquear o processo se dispositivo ocupado
    // implementação escrevendo direto do terminal A
    //   T1: deveria usar o dispositivo de saída corrente do processo

    int terminal = self->processo_corrente->terminal;
    int terminal_tela_ok = calcula_terminal_processo(D_TERM_A_TELA_OK, terminal);
    int terminal_tela = calcula_terminal_processo(D_TERM_A_TELA, terminal);

    for (;;) {
        int estado;
        if (es_le(self->es, terminal_tela_ok, &estado) != ERR_OK) {
            console_printf("SO: problema no acesso ao estado da tela");
            self->erro_interno = true;
            return;
        }
        if (estado != 0) break;
        // como não está saindo do SO, a unidade de controle não está executando seu laço.
        // esta gambiarra faz pelo menos a console ser atualizada
        // T1: não deve mais existir quando houver suporte a processos, porque o SO não poderá
        //   executar por muito tempo, permitindo a execução do laço da unidade de controle
        console_tictac(self->console);
    }
    int dado;
    // está lendo o valor de X e escrevendo o de A direto onde o processador colocou/vai pegar
    // T1: deveria usar os registradores do processo que está realizando a E/S
    // T1: caso o processo tenha sido bloqueado, esse acesso deve ser realizado em outra execução
    //   do SO, quando ele verificar que esse acesso já pode ser feito.
    mem_le(self->mem, IRQ_END_X, &dado);
    if (es_escreve(self->es, terminal_tela, dado) != ERR_OK) {
        console_printf("SO: problema no acesso à tela");
        self->erro_interno = true;
        return;
    }
    mem_escreve(self->mem, IRQ_END_A, 0);
}

// implementação da chamada se sistema SO_CRIA_PROC
// cria um processo
static void so_chamada_cria_proc(so_t *self) {
    // ainda sem suporte a processos, carrega programa e passa a executar ele
    // quem chamou o sistema não vai mais ser executado, coitado!
    // T1: deveria criar um novo processo

    int ender_nome_arq;
    // Le o X do descritor do processo criador
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

    // deveria escrever -1 (se erro) ou o PID do processo criado (se OK) no reg A
    //   do processo que pediu a criação
    console_printf("SO: criando processo %d", novo_processo->pid);
    self->processo_corrente->reg_A = novo_processo->pid;
}

// implementação da chamada se sistema SO_MATA_PROC
// T1: deveria matar um processo
// mata o processo com pid X (ou o processo corrente se X é 0)
static void so_chamada_mata_proc(so_t *self) {
    // ainda sem suporte a processos, retorna erro -1

    // PID do processo a ser morto está no registrador X do precesso corrente
    processo_t *processo_corrente = self->processo_corrente;
    if (processo_corrente == NULL) return;
    int pid_alvo = processo_corrente->reg_X;

    // Processo que deve ser morto é o processo corrente
    if (pid_alvo == 0) {
        mata_processo(self, processo_corrente);
        processo_corrente->reg_A = 0;
        self->processo_corrente = NULL;
        return;
    }

    // Procuro o processo alvo na tabela e se exitir defino como morto
    processo_t *processo_alvo = busca_processo(self, pid_alvo);
    if (processo_alvo != NULL) {
        mata_processo(self, processo_alvo);
        self->processo_corrente->reg_A = 0;
        return;
    }

    console_printf("SO: processo %d não encontrado", pid_alvo);
    mem_escreve(self->mem, IRQ_END_A, -1);
}

// implementação da chamada se sistema SO_ESPERA_PROC
// espera o fim do processo com pid X
static void so_chamada_espera_proc(so_t *self) {
    // T1: deveria bloquear o processo se for o caso (e desbloquear na morte do esperado)
    // ainda sem suporte a processos, retorna erro -1
    console_printf("SO: SO_ESPERA_PROC não implementada");
    mem_escreve(self->mem, IRQ_END_A, -1);
}

// CARGA DE PROGRAMA {{{1

// carrega o programa na memória
// retorna o endereço de carga ou -1
static int so_carrega_programa(so_t *self, char *nome_do_executavel) {
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
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender) {
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
