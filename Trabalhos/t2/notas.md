# Estruturas

## O que deve ser implementado

E precisamos alterar o SO, para:

- Manter uma tabela de páginas por processo, e alterar a configuração da MMU para usar a tabela certa a cada troca de processo;
- Alterar a carga de programas em memória, usando a memória secundária;
- Atender interrupções de falta de página, para identificar e tratar:
  - Acesso ilegal à memória ou
  - Troca de páginas entre a memória principal e a secundária;
- Implementar um algoritmo de substituição de páginas (ou dois);
- Medir o desempenho da memória virtual.

## MMU

- Tradução entre endereços virtuais e físicos usando tabela de páginas;
- A MMU tem acesso à memória principal e a uma tabela de páginas.
- Ela pode realizar operações de leitura e escrita na memória, tanto com endereços físicos quanto com endereços virtuais, que ela traduz para físicos com o uso da tabela de páginas.
- Produz erros caso a tradução não seja possível ou caso o acesso seja negado pela memória.
- Operações:
  - Acesso de leitura ou escrita à memória (recebe um argumento a mais, que permite que o acesso seja à memória virtual ou física).
  - Definição da tabela de páginas a usar (usada pelo SO cada vez que troca o
  - processo em execução).

## Memoria secundaria

- Usa a mesma implementação de memória primária `mem_t` e bloqueia o processo por tempo.
- O tempo de transferência de uma página entre a memória principal e a secundária é sempre o mesmo (configuração do sistema).
- A memória secundária mantém uma variável que diz quando o disco estará livre (ou se já está).
- Quando uma `troca de página é necessária`, se o disco estiver livre, `atualiza-se esse tempo para "agora" mais o tempo de espera`; se não estiver, `soma-se o tempo de espera a essa variável.`
- O valor da variável indicará a data até a qual o processo deve ser bloqueado por causa dessa troca de página.
