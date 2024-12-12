#ifndef GERE_BLOCOS_H
#define GERE_BLOCOS_H

#include <stdbool.h>

typedef struct
{
    bool em_uso;
    int processo_pid;
    int pagina;
} bloco_t;

// rastreia memoria fisica principal
typedef struct gere_blocos_t
{
    bloco_t *blocos;
    int total_blocos;
} gere_blocos_t;

// recebe o numero de paginas fisicas rastreadas
gere_blocos_t *gere_blocos_cria(int tam);

bool gere_blocos_tem_disponivel(gere_blocos_t *self);

int gere_blocos_buscar_proximo(gere_blocos_t *self);

void gere_blocos_atualiza_bloco(gere_blocos_t *gerenciador, int indice, int pid, int pagina);

#endif // GERE_BLOCOS_H
