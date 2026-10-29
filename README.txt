DataRabbit
==========
DataRabbit is a small C library for saving data to TXT files in table format.

Storage format:
[id][name][score]
[1][Alice][99]
[2][Bob][87]

Each row is a row of the table and each value within [] is a column.

==========
O DataRabbit é uma biblioteca em C para armazenar dados em formato de tabela, com suporte a gravação e leitura em arquivo TXT.
DataRabbit é uma pequena biblioteca em C para salvar dados em arquivos TXT em formato de tabela.

Formato de armazenamento:
  [id][name][score]
  [1][Alice][99]
  [2][Bob][87]

Cada Linha é Uma linha da tabela e Cada valor dentro de [] é uma coluna.

==========
Ele usa o prefixo datarabbit_ em todas as funções e estruturas para evitar conflitos com outras bibliotecas.

Estruturas principais
--------------------
- datarabbit_table
- datarabbit_database
- datarabbit_cell

Macro útil
----------
- DATARABBIT_CELL(db, row, col)

Funções principais
------------------
Banco de dados:
- datarabbit_database_init
- datarabbit_database_open
- datarabbit_database_load
- datarabbit_database_save
- datarabbit_database_free
- datarabbit_database_create_file

Leitura:
- datarabbit_database_select_value
- datarabbit_database_select_row
- datarabbit_database_find_row
- datarabbit_database_find_value
- datarabbit_database_where

Inserção e atualização:
- datarabbit_database_insert
- datarabbit_database_insert_row
- datarabbit_database_set_row
- datarabbit_database_update
- datarabbit_database_update_cell
- datarabbit_database_add_column

Exclusão:
- datarabbit_database_delete
- datarabbit_database_delete_row
- datarabbit_database_delete_value

Célula:
- datarabbit_cell_load
- datarabbit_cell_remove
- datarabbit_cell_value

Tabela:
- datarabbit_table_init
- datarabbit_table_clear
- datarabbit_table_free
- datarabbit_table_add_row
- datarabbit_table_add_column
- datarabbit_table_set_row
- datarabbit_table_get_row
- datarabbit_table_set_value
- datarabbit_table_get_value
- datarabbit_table_find_row_by_value
- datarabbit_table_update_value
- datarabbit_table_remove_row
- datarabbit_table_insert_row
- datarabbit_table_save_file
- datarabbit_table_load_file

Auxiliares:
- datarabbit_make_filename
- datarabbit_strdup

Exemplo de uso
--------------
#include <stdio.h>
#include "datarabbit.h"

int main(void) {
    datarabbit_database db;
    const char *values1[] = { "Alice", "25", "SP" };
    const char *values2[] = { "Bob", "30", "RJ" };
    const char *valor;
    const char **linha;
    size_t count;
    int result;

    result = datarabbit_database_init(&db, "clientes");
    if (result != 0) {
        printf("Erro ao inicializar banco\n");
        return 1;
    }

    datarabbit_database_insert(&db, values1, 3);
    datarabbit_database_insert(&db, values2, 3);

    if (datarabbit_database_select_value(&db, 0, 1, &valor) == 0) {
        printf("Idade da linha 0: %s\n", valor);
    }

    if (datarabbit_database_select_row(&db, 1, &linha, &count) == 0) {
        printf("Linha 1: %s | %s | %s\n", linha[0], linha[1], linha[2]);
    }

    datarabbit_database_free(&db);
    return 0;
}

Observações
-----------
- Os arquivos gerados pelo DataRabbit são salvos no formato datarabbit+nome.txt.
- Se o nome informado for null ou vazio, o arquivo padrão usado será "datarabbit+database.txt".
- A biblioteca foi escrita como header-only (arquivo .h), então basta incluir datarabbit.h no projeto.

Compilação
----------
Exemplo:

gcc main.c -std=c11 -Wall -Wextra -o exemplo

