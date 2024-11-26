## Funcionalidades a Implementar no Trabalho 1

1. suporte a processos com multiprogramação,
2. um escalonador (ou três),
3. chamadas de sistema,
4. bloqueio de processos por entrada e saída e para esperar a morte de outro, para melhorar o uso da CPU,
5. preempção de processos, para melhorar a distribuição da CPU.

## Hardware simulado

O computador simulado é constituído de três partes:

1. a memória, que contém instruções e dados,
2. o subsistema de entrada e saída, que permite comunicação externa,
3. a unidade central de processamento, que executa as instruções, manipula e movimenta os dados entre as demais unidades.

### Memória

- A memória é organizada como um vetor de int, endereçados a partir de 0.

- A memória é utilizada para conter os programas e os dados acessáveis pela CPU.
- As primeiras 100 posições de memória são reservadas para uso interno da CPU, e não devem ser usadas pelos programas.

### Entrada e Saída

O acesso aos dispositivos de E/S pela CPU é feito por meio de um controlador de E/S, que mapeia a identificação dos dispositivos reconhecidos pela CPU no código que implementa esses dispositivos.

Para ser acessável pela CPU, um dispositivo deve antes ser registrado no controlador de E/S. Esse registro é realizado na inicialização do simulador.

Estão implementados dois dispositivos de E/S:

- terminal, para entrada pelo teclado e saída no vídeo,
- relógio, que conta o número de instruções executadas e o tempo de execução da CPU.

### Console

Os terminais são controlados pela console, que é quem realmente tem o controle do teclado e do vídeo físicos. A console controla também a interface com o usuário da simulação (o operador).

Ela mostra na tela a entrada e saída dos vários terminais (está configurada para 4), os prints para debug do programa (feitos com a função console_printf), além da interface com o operador. Essa interface permite realizar entrada e saída nos terminais e também controlar a simulação.

As funções que a console usa para acesso ao teclado e à tela estão em `tela_curses.c`, implementadas usando a biblioteca "curses". Caso tenha problemas para compilar com curses, pode reimplementar as funções de tela.h em outro arquivo, usando outra biblioteca.

### CPU

A CPU é dividida em unidade de execução e unidade de controle.

A `unidade de execução` contém os registradores e sabe executar cada instrução reconhecida pela CPU.

A `unidade de controle` contém o laço principal de execução, que ordena a execução de uma instrução por vez, a execução das funções que permitem o funcionamento das demais unidades simuladas e o controle da simulação em geral, atendendo os comandos do operador realizados na console.

### Registradores da CPU

A CPU tem três registradores principais:

- PC, o contador de programa, tem o endereço onde está a próxima instrução a ser executada;
- A, acumulador, é usado nas instruções aritméticas, e meio que pra todo o resto;
- X, registrador auxiliar, usado para acessos indexados à memória.

Além desses, tem um registrador de erro, para quando a CPU detecta algum problema, e um registrador complementar, para quando o registrador de erro não é suficiente para codificar o problema. Todos os registradores contêm um valor int, e são inicializados em 0, exceto o PC, que é inicializado em 100.

### Instruções da CPU

Uma instrução pode ocupar uma ou duas posições de memória.

1. A primeira é o **código da instrução** (campo código na tabela, o valor em `mem[PC]`), presente em todas as instruções;
2. A segunda é o **argumento da instrução** (o valor em `mem[PC+1]`, chamado `A1` na tabela), presente somente em algumas instruções.

O campo args da tabela contém 0 para instruções sem argumento (ocupam uma posição de memória e não tem `A1`) e `1` para as que contêm um argumento (essas instruções ocupam duas posições de memória).

Ao final da execução bem sucedida de uma instrução, caso não seja uma instrução de desvio que causou a alteração do PC, o PC é incrementado para apontar para a instrução seguinte (levando em consideração o número de argumentos da instrução).

| código | nome   | args | operação                  | descrição                  |
| -----: | :----- | :--: | :------------------------ | :------------------------- |
|    --- | ------ | ---  | ------------------------- | **controle**               |
|      0 | NOP    |  0   | -                         | não faz nada               |
|      1 | PARA   |  0   | erro = ERR_CPU_PARADA     | para a CPU                 |
|    --- | ------ | ---  | ------------------------- | **acesso à memória**       |
|      2 | CARGI  |  1   | A = A1                    | carrega imediato           |
|      3 | CARGM  |  1   | A = mem[A1]               | carrega da memória         |
|      4 | CARGX  |  1   | A = mem[A1+X]             | carrega indexado           |
|      5 | ARMM   |  1   | mem[A1] = A               | armazena na memória        |
|      6 | ARMX   |  1   | mem[A1+X] = A             | armazena indexado          |
|    --- | ------ | ---  | ------------------------- | **acesso a registradores** |
|      7 | TRAX   |  0   | X ⇄ A                     | troca A com X              |
|      8 | CPXA   |  0   | A = X                     | copia X para A             |
|    --- | ------ | ---  | ------------------------- | **aritmética**             |
|      9 | INCX   |  0   | X++                       | incrementa X               |
|     10 | SOMA   |  1   | A += mem[A1]              | soma                       |
|     11 | SUB    |  1   | A -= mem[A1]              | subtração                  |
|     12 | MULT   |  1   | A \*= mem[A1]             | multiplicação              |
|     13 | DIV    |  1   | A /= mem[A1]              | quociente da divisão       |
|     14 | RESTO  |  1   | A %= mem[A1]              | resto da divisão           |
|     15 | NEG    |  0   | A = -A                    | negação                    |
|    --- | ------ | ---  | ------------------------- | **desvios**                |
|     16 | DESV   |  1   | PC = A1                   | desvio                     |
|     17 | DESVZ  |  1   | se A for 0, PC = A1       | desvio se zero             |
|     18 | DESVNZ |  1   | se A não for 0, PC = A1   | desvio se não zero         |
|     19 | DESVN  |  1   | se A < 0, PC = A1         | desvio se negativo         |
|     20 | DESVP  |  1   | se A > 0, PC = A1         | desvio se positivo         |
|    --- | ------ | ---  | ------------------------- | **chamada de subrotina**   |
|     21 | CHAMA  |  1   | mem[A1] = PC+2; PC = A1+1 | chamada de subrotina       |
|     22 | RET    |  1   | PC = mem[A1]              | retorno de subrotina       |
|    --- | ------ | ---  | ------------------------- | **entrada e saída**        |
|     23 | LE     |  1   | A = es[A1]                | leitura do dispositivo A1  |
|     24 | ESCR   |  1   | es[A1] = A                | escrita no dispositivo A1  |

A CPU só executa uma instrução se o registrador de erro indicar que a CPU não está em erro (valor `ERR_OK`). A execução de uma instrução pode colocar a CPU em erro, por tentativa de execução de instrução ilegal, acesso a posição inválida de memória, acesso a dispositivo de E/S inexistente, etc.

Os códigos de erro estão em `err.h`. Caso isso aconteça, o valor do PC não é alterado e o código do erro é colocado no registrador de erro. Para alguns erros, um valor adicional é colocado no registrador de complemento de erro (por exemplo, em caso de erro de acesso à memória, é colocado no complemento o endereço que causou erro de acesso).

O processador não tem uma pilha de execução, que a maior parte dos processadores reais usa para implementar chamadas de sub-rotinas.

A chamada de sub-rotinas é implementada de uma forma mais simples e limitada: **deve-se reservar uma posição de memória antes da primeira instrução de uma sub-rotina**. A instrução CHAMA tem como argumento o endereço dessa posição, e coloca aí o endereço para onde a sub-rotina deve retornar, antes de desviar para o endereço seguinte. A instrução RET tem esse mesmo endereço como argumento, e desvia para esse endereço que está nesse local.

## Execução do projeto

A compilação é realizada pelo programa `make`, que por sua vez é controlado pelo arquivo `Makefile`. Basta copiar os arquivos do github para um diretório e executar

```shell
make
```

Se tudo der certo, um executável 'main' será gerado, além de 'montador' e um .maq para cada .asm)

Durante a execução, é apresentada a console do simulador. Ela é definida com um tamanho fixo de **24 linhas de 80 colunas**. Para que a apresentação seja bem sucedida, é necessário que o emulador de terminal tenha pelo menos 24 linhas de 80 colunas. A tela do simulador é dividida em **4 partes**:

- **No topo**, duas linhas para cada terminal, uma com a saída do terminal (o que for escrito nele pelo programa) e outra com a entrada (o que for digitado pelo operador e ainda não lido pelo programa).
- **No meio**, uma linha com o estado do simulador (a primeira palavra, que deve ser `PARADO`, já que a simulação inicia nesse estado) e o estado da CPU, com:
  - o valor dos registradores PC, A e X,
  - a próxima instrução a ser executada (a que está no endereço do PC),
  - o erro da CPU, se for o caso.
- **Embaixo**, linha de entrada de comandos pelo operador
- no **espaço entre a linha de estado e a de entrada** aparecerão mensagens da console, se houverem,

O controle da execução é realizado pelo operador com a entrada textual de comandos na console. Cada comando é digitado em uma linha, terminada por `enter`. A linha pode ser editada antes do `enter` com `backspace`. Existem 3 comandos para controle dos terminais e 4 para controle da execução:

- **E**: Faz a entrada de texto em um terminal (exemplo: `eaxis` coloca uma linha com o valor `xis`  na entrada do terminal `a`, o primeiro)
- **Z**: Limpa a tela de um terminal (exemplo: `zb` limpa a saída (uma linha na tela) do segundo terminal, `b`)
- **D**: Altera o tempo de espera em cada acesso ao teclado, mudando a velocidade da simulação (o valor default corresponde a D5)
- **P**: Para a execução, o controlador não vai mais mandar a CPU executar instruções em seu laço (a execução inicia nesse modo)
- **1**: O controlador vai executar uma instrução e depois parar
- **C**: O controlador vai continuar a execução das instruções uma após a outra
- **F**: Fim da simulação.

## Chamadas de Sistema

As chamadas de sistema são instruções executadas pelos processos para interagir com o SO e executar operações específicas:

- SO_LE: lê um número inteiro da entrada do processo.
- SO_ESCR: escreve um número inteiro na saída do processo.
- SO_CRIA_PROC: cria um novo processo.
- SO_MATA_PROC: termina um processo, podendo ser o próprio processo que executou a chamada.
- SO_ESPERA_PROC: espera a terminação de outro processo antes de prosseguir.

## Modos de Operação da CPU

A CPU possui dois modos de operação:

- Modo Usuário: utilizado para a execução dos programas; restrito, sem permissão para executar instruções sensíveis.
- Modo Supervisor: utilizado pelo SO para executar instruções privilegiadas. Somente o SO pode operar nesse modo.

### Controle de Troca de Modo

A troca para o modo supervisor é controlada e ocorre através de uma transição segura, onde:

- A CPU armazena os valores dos registradores críticos (ex.: PC) antes de entrar no modo supervisor.
- A execução no modo supervisor é limitada a um endereço de execução pré-determinado.
- Somente o SO possui acesso a esse endereço, garantindo que os programas não possam executar operações privilegiadas.

Quando o SO está em modo supervisor, a CPU armazena o valor de todos os registradores para garantir a retomada correta do processo de usuário após o retorno.

### Troca de Modo de Execução e Interrupções

O modo de execução pode ser alterado nas seguintes situações:

- CHAMAS (chamada ao sistema): instrução usada pelos processos para solicitar serviços ao SO.
- Erros de CPU: instruções inválidas ou acessos ilegais à memória resultam em uma interrupção.
- Dispositivos de E/S: podem solicitar atenção da CPU, gerando uma interrupção.

Em qualquer desses casos, ocorre o seguinte fluxo:

- A CPU troca para o modo supervisor e salva o estado dos registradores.
- O PC é redirecionado para um endereço pré-determinado onde o código do SO deve ser executado.
- Para evitar interrupções durante a execução de código crítico, a CPU não aceita novas interrupções enquanto está em modo supervisor.

No simulador, uma instrução especial CHAMAC permite ao SO executar uma função em C, simulando a execução do código do SO em um único comando de processador.

### Timer e Inicialização do SO

Para que o SO mantenha o controle da execução:

- Timer do relógio: configurável para gerar interrupções periódicas, que acionam o SO.
- Interrupção de reset: ao iniciar o sistema, a CPU gera uma interrupção de reset, garantindo que o SO seja a primeira execução ao iniciar a máquina.

Essas inicializações e a configuração do timer são realizadas durante a inicialização do SO, assegurando que o SO tenha controle total da máquina e possa gerenciar o tempo da CPU adequadamente.
