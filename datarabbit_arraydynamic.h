/*
datarabbit_arraydynamic.h — Adapter entre tabelas DataRabbit e arraydynamic.
Fornece funções para converter entre arraydynamic (vetor dinâmico de strings)
e bancos DataRabbit abertos (array**** handle) ou arquivos diretos.

USO BÁSICO:
  // Com banco aberto:
  array ****clientes = rabbit_open("clientes");
  arraydynamic cells = arraydynamic_create_string_elements(sizeof(char*));
  
  // Salvar arraydynamic no banco aberto:
  rabbit_array_save(clientes, &cells, 3, 2);  // 3 linhas, 2 colunas
  
  // Carregar banco aberto para arraydynamic:
  size_t rows, cols;
  rabbit_array_load(clientes, &cells, &rows, &cols);
  
  // Com arquivo (sem banco aberto):
  rabbit_array_save_file("clientes", &cells, 3, 2);
  rabbit_array_load_file("clientes", &cells, &rows, &cols);
*/

#ifndef DATARABBIT_ARRAYDYNAMIC_H
#define DATARABBIT_ARRAYDYNAMIC_H

#include "DataRabbit.h"

/* ============================================================================
Operações com banco ABERTO (array**** handle)
============================================================================ */

/* Salva arraydynamic de strings UTF-8 como tabela no banco aberto.
   Substitui o conteúdo atual do banco.
   Retorna 0 em sucesso, -1 em erro. */
static inline int rabbit_array_save(array ****handle, const arraydynamic *cells,
                                     size_t row_count, size_t col_count) {
    array ***grid;
    
    if (handle == NULL || *handle == NULL || cells == NULL || col_count == 0U) return -1;
    if (!cells->string_elements || cells->element_size != sizeof(char *)) return -1;
    if (row_count > SIZE_MAX / col_count) return -1;
    if (cells->length != row_count * col_count) return -1;
    
    /* Cria grid temporário a partir do arraydynamic */
    grid = datarabbit_from_arraydynamic(cells, row_count, col_count);
    if (grid == NULL) return -1;
    
    /* Libera o grid atual do banco e substitui pelo novo */
    datarabbit_free_grid(*handle);
    *handle = grid;
    
    /* Atualiza dimensões globais e marca como dirty */
    datarabbit_refresh_size(handle);
    datarabbit_jit_mark_dirty();
    
    /* Se AUTOFLUSH está ativo, grava imediatamente */
#if DATARABBIT_JIT_AUTOFLUSH
    if (datarabbit_jit_workspace_flush(handle) != 0) return -1;
#endif
    
    return 0;
}

/* Carrega banco aberto para arraydynamic.
   Retorna 0 em sucesso, -1 em erro. */
static inline int rabbit_array_load(array ****handle, arraydynamic *out,
                                     size_t *out_rows, size_t *out_cols) {
    if (handle == NULL || *handle == NULL || out == NULL) return -1;
    return datarabbit_to_arraydynamic(*handle, out, out_rows, out_cols);
}

/* ============================================================================
Operações com ARQUIVO (sem banco aberto)
============================================================================ */

/* Salva arraydynamic diretamente em arquivo (sem precisar abrir banco).
   Retorna 0 em sucesso, -1 em erro. */
static inline int rabbit_array_save_file(const char *name, const arraydynamic *cells,
                                          size_t row_count, size_t col_count) {
    return datarabbit_save_arraydynamic(name, cells, row_count, col_count);
}

/* Carrega arquivo em arraydynamic (sem precisar abrir banco).
   Retorna 0 em sucesso, -1 em erro. */
static inline int rabbit_array_load_file(const char *name, arraydynamic *out,
                                          size_t *out_rows, size_t *out_cols) {
    return datarabbit_load_arraydynamic(name, out, out_rows, out_cols);
}

/* ============================================================================
Visualização
============================================================================ */

/* Imprime arraydynamic como tabela formatada no console. */
static inline void rabbit_array_view(const arraydynamic *cells,
                                      size_t row_count, size_t col_count) {
    datarabbit_view_arraydynamic(cells, row_count, col_count);
}

/* ============================================================================
Aliases de compatibilidade (caso algum código antigo ainda use os nomes longos)
============================================================================ */
#define datarabbit_array_save      rabbit_array_save
#define datarabbit_array_load      rabbit_array_load
#define datarabbit_array_save_file rabbit_array_save_file
#define datarabbit_array_load_file rabbit_array_load_file
#define datarabbit_array_view      rabbit_array_view

#endif /* DATARABBIT_ARRAYDYNAMIC_H */
