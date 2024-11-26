#ifndef PROCESSO_H
#define PROCESSO_H

typedef enum { PRONTO, EXECUTANDO, BLOQUEADO, TERMINADO } estado_t;

typedef struct processo_t processo_t;

// Criação
processo_t* cria_processo(int pid, int pc);

// // Getters
// int processo_get_pid(const process_t* p);
// estado_t processo_get_estado(const process_t* p);
// int processo_get_pc(const process_t* p);
// int processo_get_reg_A(const process_t* p);
// int processo_get_reg_X(const process_t* p);

// // Setters
// void processo_set_estado(process_t* p, estado_t estado);
// void processo_set_pc(process_t* p, int pc);
// void processo_set_reg_A(process_t* p, int reg_A);
// void processo_set_reg_X(process_t* p, int reg_X);

#endif  // PROCESSO_H
