#include "gere_blocos.h"
#include "console.h"
#include "mmu.h"
#include <stdlib.h>

gere_blocos_t *gere_blocos_cria(int tam)
{
    gere_blocos_t *gerenciador = malloc(sizeof(gere_blocos_t));
    if (!gerenciador) {
        console_printf("Erro ao alocar memória para o gerenciador de blocos.");
        return NULL;
    }

    gerenciador->blocos = malloc(sizeof(bloco_t) * tam);
    if (!gerenciador->blocos) {
        console_printf("Erro ao alocar memória para os blocos.");
        free(gerenciador);
        return NULL;
    }

    gerenciador->total_blocos = tam;
    for (int i = 0; i < tam; i++) {
        if (i < 1) {
            gerenciador->blocos[i].em_uso = true;
            continue;
        }
        gerenciador->blocos[i].em_uso = false;
        gerenciador->blocos[i].processo_pid = 0;
    }
    return gerenciador;
}

bool gere_blocos_tem_disponivel(gere_blocos_t *gerenciador)
{
    for (int i = 0; i < gerenciador->total_blocos; i++) {
        if (!gerenciador->blocos[i].em_uso) {
            return true;
        }
    }
    return false;
}

int gere_blocos_buscar_proximo(gere_blocos_t *gerenciador)
{
    for (int i = 0; i < gerenciador->total_blocos; i++) {
        if (!gerenciador->blocos[i].em_uso) {
            gerenciador->blocos[i].em_uso = true;
            return i;
        }
    }
    return -1;
}

void gere_blocos_cadastra_bloco(gere_blocos_t *gerenciador, int end_ini, int end_fim, int pid)
{
    for (int address = 0; address < end_fim; address += TAM_PAGINA) {
        gerenciador->blocos[address / TAM_PAGINA].em_uso = true;
        gerenciador->blocos[address / TAM_PAGINA].processo_pid = pid;
    }
}

void gere_blocos_atualiza_bloco(gere_blocos_t *gerenciador, int indice, int pid, int pagina)
{
    gerenciador->blocos[indice].em_uso = true;
    gerenciador->blocos[indice].processo_pid = pid;
    gerenciador->blocos[indice].pagina = pagina;
}
