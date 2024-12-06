# Relatório de Execução e Análise de Escalonadores

## Introdução

Este documento apresenta os resultados e análises de três diferentes escalonadores testados no simulador:

- Escalonador Simples
- Escalonador Round-Robin
- Escalonador por Prioridade

Os testes envolveram a execução de 4 processos em diferentes configurações, com métricas coletadas para avaliação de desempenho.

---

## Configurações Gerais

- **Número de Processos Criados**: 4
- **Instruções para Interrupção de Clock**: 50
- **Quantum**: 10

---

## Resultados

### 1. Escalonador Simples

#### Resumo Geral

- **Número de Preempções**: 0
- **Tempo Total de Execução**: 27.661 ms
- **Tempo Ocioso do Sistema**: 20.861 ms
- **Interrupções**:
  - Reset: 1
  - Chamada de Sistema: 462
  - E/S (Relógio): 560
  - E/S (Teclado): 0
  - E/S (Console): 0

#### Métricas por Processo

| PID | Tempo de Retorno | Preempções | Tempo em PRONTO | Tempo em BLOQUEADO | Tempo Médio de Resposta | Entradas PRONTO | Entradas BLOQUEADO | Entradas MORTO |
|-----|------------------|------------|-----------------|--------------------|--------------------------|------------------|--------------------|----------------|
| 1   | 13.824 ms        | 0          | 3.877 ms        | 9.947 ms          | 969,00 ms               | 4                | 3                  | 1              |
| 2   | 6.901 ms         | 0          | 7.638 ms        | 737 ms            | 1.273,00 ms             | 6                | 5                  | 1              |
| 3   | 4.843 ms         | 0          | 1.362 ms        | 3.481 ms          | 64,00 ms                | 21               | 20                 | 1              |
| 4   | 10.748 ms        | 0          | 4.052 ms        | 6.696 ms          | 30,00 ms                | 132              | 131                | 1              |

---

### 2. Escalonador Round-Robin

#### Resumo Geral

- **Número de Preempções**: 24
- **Tempo Total de Execução**: 25.641 ms
- **Tempo Ocioso do Sistema**: 15.210 ms
- **Interrupções**:
  - Reset: 1
  - Chamada de Sistema: 462
  - E/S (Relógio): 526
  - E/S (Teclado): 0
  - E/S (Console): 0

#### Métricas por Processo

| PID | Tempo de Retorno | Preempções | Tempo em PRONTO | Tempo em BLOQUEADO | Tempo Médio de Resposta | Entradas PRONTO | Entradas BLOQUEADO | Entradas MORTO |
|-----|------------------|------------|-----------------|--------------------|--------------------------|------------------|--------------------|----------------|
| 1   | 12.814 ms        | 0          | 987 ms          | 11.827 ms         | 246,00 ms               | 4                | 2                  | 1              |
| 2   | 7.643 ms         | 19         | 4.360 ms        | 3.283 ms          | 622,00 ms               | 7                | 6                  | 1              |
| 3   | 6.789 ms         | 3          | 7.622 ms        | 833 ms            | 544,00 ms               | 14               | 13                 | 1              |
| 4   | 12.805 ms        | 2          | 6.480 ms        | 6.325 ms          | 53,00 ms                | 121              | 120                | 1              |

---

### 3. Escalonador por Prioridade

#### Resumo Geral

- **Número de Preempções**: 24
- **Tempo Total de Execução**: 24.711 ms
- **Tempo Ocioso do Sistema**: 12.025 ms
- **Interrupções**:
  - Reset: 1
  - Chamada de Sistema: 462
  - E/S (Relógio): 504
  - E/S (Teclado): 0
  - E/S (Console): 0

#### Métricas por Processo

| PID | Tempo de Retorno | Preempções | Tempo em PRONTO | Tempo em BLOQUEADO | Tempo Médio de Resposta | Entradas PRONTO | Entradas BLOQUEADO | Entradas MORTO |
|-----|------------------|------------|-----------------|--------------------|--------------------------|------------------|--------------------|----------------|
| 1   | 12.349 ms        | 0          | 782 ms          | 11.567 ms         | 195,00 ms               | 4                | 2                  | 1              |
| 2   | 8.001 ms         | 19         | 7.812 ms        | 189 ms            | 976,00 ms               | 8                | 7                  | 1              |
| 3   | 5.892 ms         | 3          | 5.181 ms        | 711 ms            | 272,00 ms               | 19               | 18                 | 1              |
| 4   | 11.850 ms        | 2          | 6.985 ms        | 4.865 ms          | 61,00 ms                | 114              | 113                | 1              |

---

## Análise e Discussão

- **Escalonador Simples**:
  - 24,6% de utilização de CPU.
  - Menor complexidade, mas não utiliza preempção, resultando em menor alternância entre processos.
  - Maior tempo ocioso do sistema devido ao bloqueio dos processos.

- **Escalonador Round-Robin**:
  - 40,7% de utilização de CPU.
  - Oferece alternância justa entre processos por meio de preempções.
  - Tempo ocioso ainda elevado devido à dependência de I/O.

- **Escalonador por Prioridade**:
  - 51,3% de utilização de CPU.
  - Maior eficiência em termos de tempo total de execução e alternância controlada por prioridades.
  - Redução significativa de ociosidade do sistema comparado ao Round-Robin.
