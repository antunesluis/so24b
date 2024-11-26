#include "processo.h"

#include <stdlib.h>

struct processo_t {
    int pid;    // Identificador único do processo
    int pc;     // Contador de programa
    int reg_A;  // Registrador A
    int reg_X;  // Registrador X

    estado_t estado_atual;  // Estado atual do processo
    /* motivo_bloqueio_t motivo;  // Motivo do bloqueio (se bloqueado) */
};

processo_t* cria_processo(int pid, int pc) {
    processo_t* p = malloc(sizeof(processo_t));
    p->pid = pid;
    p->estado_atual = PRONTO;

    return p;
}
