DataRabbit
==========

DataRabbit é uma pequena biblioteca em C para salvar dados em arquivos TXT em formato de tabela.

Formato de armazenamento:
  [id][name][score]
  [1][Alice][99]
  [2][Bob][87]

Cada linha é uma linha da tabela.
Cada valor dentro de [] é uma coluna.

Criação do arquivo
------------------
Função:
  dr_database_create_file("users");

Cria:
  datarabbit+users.txt

Exemplo:
  dr_database_create_file("users");

Abrir banco
-----------
Função:
  dr_database_open(&db, "users");

Exemplo:
  dr_database db;
  dr_database_open(&db, "users");

Salvar
------
Função:
  dr_database_save(&db);

Exemplo:
  dr_database_save(&db);

Carregar Ler tabela
--------
Função:
  dr_database_load(&db);

Exemplo:
  dr_database_load(&db);

Insert
------
Função:
  dr_database_insert_row(&db, values, count);

Exemplo:
  const char *user[] = { "1", "Alice", "99" };
  dr_database_insert_row(&db, user, 3);

Update
------
Função:
  dr_database_update(&db, row_index, col_index, "novo valor");

Exemplo:
  dr_database_update(&db, 1, 1, "Alice Silva");

Delete
------
Função:
  dr_database_delete(&db, row_index, col_index);

Exemplo:
  dr_database_delete(&db, 1, 1);

Select
------
Função:
  dr_database_select_value(&db, row_index, col_index, &value);

Exemplo:
  const char *value = NULL;
  dr_database_select_value(&db, 1, 1, &value);
  printf("%s\n", value);

Where
-----
Função:
  dr_database_where(&db, col_index, "Bob", rows, max_rows, &count);

Exemplo:
  size_t rows[10];
  size_t count = 0;
  dr_database_where(&db, 1, "Bob", rows, 10, &count);

Exemplo completo
----------------
#include "datarabbit.h"

int main(void) {
    dr_database db;
    const char *header[] = { "id", "name", "score" };
    const char *user1[] = { "1", "Alice", "99" };
    const char *user2[] = { "2", "Bob", "87" };
    const char *value = NULL;
    size_t rows[10];
    size_t count = 0;

    dr_database_create_file("users");
    dr_database_open(&db, "users");

    dr_database_set_row(&db, 0, header, 3);
    dr_database_insert_row(&db, user1, 3);
    dr_database_insert_row(&db, user2, 3);

    dr_database_select_value(&db, 1, 1, &value);
    printf("Valor: %s\n", value);

    dr_database_where(&db, 1, "Bob", rows, 10, &count);
    printf("Linhas encontradas: %zu\n", count);

    dr_database_update(&db, 1, 1, "Bobby");
    dr_database_delete(&db, 1, 1);
    dr_database_save(&db);
    dr_database_free(&db);
    return 0;
}

Arquivo gerado:
  datarabbit+users.txt

Observação:
DataRabbit é uma base leve para dados simples em arquivos TXT.
Não é um banco SQL completo, mas funciona bem para pequenos projetos, protótipos e armazenamento local simples.
