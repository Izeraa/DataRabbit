/*
DataRabbit — biblioteca C header-only para bancos de dados em texto (.txt).
Simples, poderosa, sem senha. Une grid tradicional + arraydynamic num arquivo só.

FORMATO DO ARQUIVO (imutável)
Uma linha de texto por linha da tabela. Cada célula é gravada como
[♜ valor ♜]. Dentro do valor são escapados: \\ [ ] \n \r \t e \xHH.
Linha em branco = linha vazia (preservada para manter índices).
Arquivos são gravados como "datarabbit+<nome>.txt".

MODELO DE DADOS (todos os vetores terminam em NULL)
rabbit_array *      = string (célula, UTF-8)
rabbit_array **     = linha  (vetor de células)
rabbit_array ***    = tabela (vetor de linhas)
rabbit_array ****   = handle (ponteiro para a tabela)

FILEDIR (onde ficam os arquivos)
FILEDIR = "/dados/meu_app/";       // PASTA (barra final); atribuição direta (const char *)
rabbit_filedir_set("/dados/app/"); // ou copiando a string
#define FILEDIR "/dados/app/"      // ou em tempo de compilação, antes do include
ARQUIVO EXATO: rabbit_open_file("Screen/0/FrontEnd/Style.txt") abre esse arquivo como
está (sem FILEDIR/prefixo/extensão); rabbit_save_here(db) salva nele mesmo.

FILEDIR pode apontar DIRETO PARA UM ARQUIVO:
FILEDIR = "/dados/clientes.db";    // rabbit_open("qualquer") abre exatamente esse arquivo
É arquivo quando não termina em '/' e não é um diretório existente. Nesse modo o
nome passado a rabbit_open/rabbit_save é ignorado (sem prefixo "datarabbit+" nem
".txt"), só um banco pode ficar aberto (o 2º rabbit_open falha com EBUSY), e o
espelho JIT fica na mesma pasta do arquivo. ATENÇÃO: um caminho sem '/' final
que ainda não existe é tratado como ARQUIVO. Para pasta, termine SEMPRE com '/'
("/dados/app/"), ou aponte para um diretório que já exista.
Vazio ("") = diretório atual. O diretório é criado se não existir. Nomes com
caminho relativo ("clientes/2026") ficam dentro de FILEDIR; nomes absolutos
("/tmp/x") ignoram FILEDIR; ".." no nome é recusado. Não mude FILEDIR com
bancos abertos. rabbit_make_filename(nome) devolve o caminho final.

BANCO JSON (rabbit_json_*) — mesmo formato de arquivo, semântica de objetos
Cada propriedade é uma linha [♜ chave ♜][♜ tipo ♜][♜ valor ♜]; o tipo é
s (texto), n (número), b (true/false), z (null) ou j (objeto/array aninhado,
JSON compacto). Cada objeto termina com a linha separadora [♜ 1 ♜].
rabbit_array ****db = rabbit_json_open("pessoas");
rabbit_json_add(db, "{\"nome\":\"Ana\",\"idade\":30}");
rabbit_json_set_string(db, 0, "cidade", "Natal");
const char *nome = rabbit_json_get(db, 0, "nome");
rabbit_json_export(db, "pessoas.json", 1);   // arquivo .json de verdade
rabbit_json_import(db, "outros.json");       // objeto ou array de objetos
rabbit_save(db, "pessoas"); rabbit_close(db);

ESPELHO JIT
Cada alteração em memória é espelhada em "<FILEDIR>/.datarabbit-jit-<pid>.tmp" (0600),
removido quando o último banco fecha. O banco definitivo só muda em rabbit_save.
rabbit_set/push/insert/delete/sort retornam 0 se a memória foi alterada, mesmo
que o espelho falhe (rabbit_jit_dirty fica 1 e o próximo flush tenta de novo).

USO BÁSICO (grid tradicional)
rabbit_array ****db = rabbit_open("clientes");
rabbit_set(db, 0, 0, "Ana");
rabbit_push(db, (const char*[]){"Bia","30"}, 2);
rabbit_insert_row(db, 0, (const char*[]){"Topo","1"}, 2);
rabbit_delete_row(db, 2);
rabbit_sort(db, 0, 1);
rabbit_view(db);
rabbit_save(db, "clientes");
rabbit_close(db);

USO BÁSICO (arraydynamic)
arraydynamic cells = arraydynamic_create_string_elements(sizeof(char *));
arraydynamic_push(&cells, &(const char *){"Ana"});
rabbit_array_dynamic_save_file("clientes", &cells, 1, 1);
arraydynamic_destroy(&cells);
*/
#ifndef DATARABBIT_H
#define DATARABBIT_H

#if !defined(_XOPEN_SOURCE) && !defined(_POSIX_C_SOURCE)
#define _XOPEN_SOURCE 700
#endif

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef char rabbit_array;

#ifndef RABBIT_JIT_AUTOFLUSH
#define RABBIT_JIT_AUTOFLUSH 1
#endif

/* Maior índice (linha ou coluna) aceito por rabbit_set. Evita que um índice
   absurdo (ex.: 1000000000) tente alocar bilhões de linhas vazias. */
#ifndef RABBIT_MAX_INDEX
#define RABBIT_MAX_INDEX 4194304U
#endif

/* ============================================================================
UTF-8
============================================================================ */
static inline int rabbit_utf8_validate(const char *text) {
    size_t length, offset = 0U;
    if (text == NULL) return 0;
    length = strlen(text);
    while (offset < length) {
        const unsigned char *s = (const unsigned char *)text + offset;
        uint32_t cp;
        size_t need, i;
        if (s[0] <= 0x7FU) { cp = s[0]; need = 1U; }
        else if (s[0] >= 0xC2U && s[0] <= 0xDFU) { cp = s[0] & 0x1FU; need = 2U; }
        else if (s[0] >= 0xE0U && s[0] <= 0xEFU) { cp = s[0] & 0x0FU; need = 3U; }
        else if (s[0] >= 0xF0U && s[0] <= 0xF4U) { cp = s[0] & 0x07U; need = 4U; }
        else return 0;
        if (need > length - offset) return 0;
        for (i = 1U; i < need; ++i) {
            if ((s[i] & 0xC0U) != 0x80U) return 0;
            cp = (cp << 6U) | (uint32_t)(s[i] & 0x3FU);
        }
        if ((need == 3U && cp < 0x800U) || (need == 4U && cp < 0x10000U) ||
            (cp >= 0xD800U && cp <= 0xDFFFU) || cp > 0x10FFFFU) return 0;
        offset += need;
    }
    return 1;
}

static inline size_t rabbit_utf8_next(const char *src, char dst[5]) {
    unsigned char c;
    size_t need, i;
    if (src == NULL || src[0] == '\0') return 0U;
    c = (unsigned char)src[0];
    if (c <= 0x7FU) need = 1U;
    else if (c >= 0xC2U && c <= 0xDFU) need = 2U;
    else if (c >= 0xE0U && c <= 0xEFU) need = 3U;
    else if (c >= 0xF0U && c <= 0xF4U) need = 4U;
    else return 0U;
    for (i = 0U; i < need; ++i) {
        if (src[i] == '\0') return 0U;
        dst[i] = src[i];
    }
    dst[need] = '\0';
    return rabbit_utf8_validate(dst) ? need : 0U;
}

static inline size_t rabbit_utf8_length(const char *text) {
    size_t len = 0U, offset = 0U;
    if (text == NULL) return 0U;
    while (text[offset] != '\0') {
        char cell[5];
        size_t n = rabbit_utf8_next(text + offset, cell);
        if (n == 0U) return SIZE_MAX;
        offset += n;
        ++len;
    }
    return len;
}

/* ============================================================================
Helpers gerais
============================================================================ */
static inline rabbit_array *rabbit_strdup(const rabbit_array *value) {
    size_t length;
    rabbit_array *copy;
    if (value == NULL) value = "";
    length = strlen(value);
    copy = (rabbit_array *)malloc(length + 1U);
    if (copy == NULL) return NULL;
    memcpy(copy, value, length + 1U);
    return copy;
}

/* ---- FILEDIR: diretório onde ficam os bancos ----------------------------- */
#ifdef FILEDIR
static const char *rabbit_filedir = FILEDIR;
#undef FILEDIR
#else
static const char *rabbit_filedir = "";
#endif
#define FILEDIR rabbit_filedir   /* permite: FILEDIR = "/meu/dir"; */

static char rabbit_filedir_store[4096];
static inline int rabbit_filedir_set(const char *dir) {
    size_t n;
    if (dir == NULL) dir = "";
    n = strlen(dir);
    if (n >= sizeof(rabbit_filedir_store)) return -1;
    memmove(rabbit_filedir_store, dir, n + 1U);
    rabbit_filedir = rabbit_filedir_store;
    return 0;
}

/* Cria os diretórios que faltam no caminho de um arquivo (best effort). */
static inline void rabbit_mkdir_parents(const char *path) {
    rabbit_array *tmp = rabbit_strdup(path);
    size_t i;
    if (tmp == NULL) return;
    for (i = 1U; tmp[i] != '\0'; ++i) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            (void)mkdir(tmp, 0755);
            tmp[i] = '/';
        }
    }
    free(tmp);
}

/* 1 se FILEDIR aponta para um arquivo: não termina em '/' e não é diretório existente. */
static inline int rabbit_filedir_is_file(void) {
    struct stat st;
    size_t n;
    if (rabbit_filedir == NULL || rabbit_filedir[0] == '\0') return 0;
    n = strlen(rabbit_filedir);
    if (rabbit_filedir[n - 1U] == '/') return 0;
    if (stat(rabbit_filedir, &st) == 0 && S_ISDIR(st.st_mode)) return 0;
    return 1;
}

/* nome -> [FILEDIR/][dir/]datarabbit+<nome>.txt  (NULL se o nome tiver "..")
   Se FILEDIR for um arquivo, devolve o próprio FILEDIR e ignora o nome. */
static inline rabbit_array *rabbit_make_filename(const rabbit_array *name) {
    static const char prefix[] = "datarabbit+";
    static const char extension[] = ".txt";
    const size_t plen = sizeof(prefix) - 1U;
    const size_t elen = sizeof(extension) - 1U;
    const rabbit_array *base, *slash, *leaf, *seg;
    const char *fd;
    size_t llen, dlen, fdlen, total, pos;
    int has_extension, need_slash;
    rabbit_array *filename;
    if (rabbit_filedir_is_file()) return rabbit_strdup(rabbit_filedir);
    base = (name == NULL || name[0] == '\0') ? "database" : name;
    for (seg = base; *seg != '\0';) {               /* recusa componente ".." */
        const char *end = strchr(seg, '/');
        size_t seglen = end ? (size_t)(end - seg) : strlen(seg);
        if (seglen == 2U && seg[0] == '.' && seg[1] == '.') { errno = EINVAL; return NULL; }
        seg += seglen;
        if (*seg == '/') ++seg;
    }
    slash = strrchr(base, '/');
    dlen = slash ? (size_t)(slash - base) + 1U : 0U;
    leaf = slash ? slash + 1 : base;
    if (strncmp(leaf, prefix, plen) == 0) leaf += plen;
    if (leaf[0] == '\0') leaf = "database";
    llen = strlen(leaf);
    has_extension = llen >= elen && strcmp(leaf + llen - elen, extension) == 0;
    fd = (base[0] == '/' || rabbit_filedir == NULL) ? "" : rabbit_filedir;
    fdlen = strlen(fd);
    need_slash = fdlen > 0U && fd[fdlen - 1U] != '/';
    if (fdlen > SIZE_MAX / 4U || dlen > SIZE_MAX / 4U || llen > SIZE_MAX / 4U) return NULL;
    total = fdlen + (need_slash ? 1U : 0U) + dlen + plen + llen + (has_extension ? 0U : elen) + 1U;
    filename = (rabbit_array *)malloc(total);
    if (filename == NULL) return NULL;
    pos = 0U;
    memcpy(filename + pos, fd, fdlen); pos += fdlen;
    if (need_slash) filename[pos++] = '/';
    memcpy(filename + pos, base, dlen); pos += dlen;
    memcpy(filename + pos, prefix, plen); pos += plen;
    memcpy(filename + pos, leaf, llen); pos += llen;
    if (!has_extension) { memcpy(filename + pos, extension, elen); pos += elen; }
    filename[pos] = '\0';
    return filename;
}

/* ============================================================================
arraydynamic (vetor dinâmico genérico)
============================================================================ */
#ifndef EAGLEHEART_H
#ifndef DATARABBIT_ARRAYDYNAMIC_DEFINED
#define DATARABBIT_ARRAYDYNAMIC_DEFINED

typedef struct arraydynamic {
    void  *data;
    size_t length;
    size_t capacity;
    size_t element_size;
    int    string_elements;
    int    nested_elements;
} arraydynamic;

typedef arraydynamic ArrayDynamic;

static inline void arraydynamic_init(arraydynamic *items, size_t element_size) {
    if (items == NULL) return;
    items->data = NULL;
    items->length = 0U;
    items->capacity = 0U;
    items->element_size = element_size == 0U ? sizeof(char) : element_size;
    items->string_elements = 0;
    items->nested_elements = 0;
}

static inline arraydynamic arraydynamic_create(size_t element_size) {
    arraydynamic items;
    arraydynamic_init(&items, element_size);
    return items;
}

static inline arraydynamic arraydynamic_create_string_elements(size_t element_size) {
    arraydynamic items = arraydynamic_create(element_size);
    if (element_size == sizeof(char *)) items.string_elements = 1;
    return items;
}

static inline arraydynamic arraydynamic_create_nested(void) {
    arraydynamic items = arraydynamic_create(sizeof(arraydynamic));
    items.nested_elements = 1;
    return items;
}

static inline void arraydynamic_destroy(arraydynamic *items) {
    size_t i;
    if (items == NULL) return;
    if (items->string_elements && items->data != NULL) {
        for (i = 0U; i < items->length; ++i) free(((char **)items->data)[i]);
    }
    if (items->nested_elements && items->data != NULL) {
        for (i = 0U; i < items->length; ++i) arraydynamic_destroy(&((arraydynamic *)items->data)[i]);
    }
    free(items->data);
    items->data = NULL;
    items->length = 0U;
    items->capacity = 0U;
    items->element_size = 0U;
    items->string_elements = 0;
    items->nested_elements = 0;
}

/* Limpa os elementos (libera strings se for o caso) mas mantém a capacidade. */
static inline void arraydynamic_clear(arraydynamic *items) {
    size_t i;
    if (items == NULL || items->data == NULL) return;
    if (items->string_elements) {
        for (i = 0U; i < items->length; ++i) free(((char **)items->data)[i]);
    }
    if (items->nested_elements) {
        for (i = 0U; i < items->length; ++i) arraydynamic_destroy(&((arraydynamic *)items->data)[i]);
    }
    items->length = 0U;
}

static inline int arraydynamic_reserve(arraydynamic *items, size_t minimum_capacity) {
    void *new_data;
    size_t new_capacity, max_capacity;
    if (items == NULL || items->element_size == 0U) return -1;
    if (minimum_capacity <= items->capacity) return 0;
    max_capacity = SIZE_MAX / items->element_size;
    if (minimum_capacity > max_capacity) return -1;
    new_capacity = items->capacity == 0U ? 4U : items->capacity;
    while (new_capacity < minimum_capacity) {
        if (new_capacity > max_capacity / 2U) { new_capacity = minimum_capacity; break; }
        new_capacity *= 2U;
    }
    if (new_capacity > max_capacity) new_capacity = minimum_capacity;
    new_data = realloc(items->data, new_capacity * items->element_size);
    if (new_data == NULL) return -1;
    items->data = new_data;
    items->capacity = new_capacity;
    return 0;
}

static inline int arraydynamic_push(arraydynamic *items, const void *value) {
    void *slot;
    char *string_copy = NULL;
    if (items == NULL || items->element_size == 0U || items->length == SIZE_MAX ||
        value == NULL || items->nested_elements) return -1;
    if (items->string_elements) {
        const char *text = *(const char * const *)value;
        string_copy = text == NULL ? NULL : rabbit_strdup(text);
        if (text != NULL && string_copy == NULL) return -1;
    }
    if (arraydynamic_reserve(items, items->length + 1U) != 0) { free(string_copy); return -1; }
    slot = (char *)items->data + (items->length * items->element_size);
    if (items->string_elements) memcpy(slot, &string_copy, sizeof(string_copy));
    else memcpy(slot, value, items->element_size);
    items->length += 1U;
    return 0;
}

static inline void *arraydynamic_at(const arraydynamic *items, size_t index) {
    if (items == NULL || index >= items->length) return NULL;
    return (char *)items->data + (index * items->element_size);
}

#endif /* DATARABBIT_ARRAYDYNAMIC_DEFINED */
#endif /* EAGLEHEART_H */

/* ============================================================================
Grid (rabbit_array ***)
============================================================================ */
static inline size_t rabbit_grid_rows(rabbit_array ***g) {
    size_t r = 0U;
    if (g == NULL) return 0U;
    while (g[r] != NULL) ++r;
    return r;
}

static inline size_t rabbit_grid_cols(rabbit_array ***g, size_t row) {
    size_t c = 0U;
    if (g == NULL || g[row] == NULL) return 0U;
    while (g[row][c] != NULL) ++c;
    return c;
}

static inline const rabbit_array *rabbit_row_get(rabbit_array **row, size_t c) {
    size_t k;
    if (row == NULL) return NULL;
    for (k = 0U; k < c; ++k) {
        if (row[k] == NULL) return NULL;
    }
    return row[c];
}

static inline void rabbit_free_grid(rabbit_array ***g) {
    size_t r, c;
    if (g == NULL) return;
    for (r = 0U; g[r] != NULL; ++r) {
        for (c = 0U; g[r][c] != NULL; ++c) free(g[r][c]);
        free(g[r]);
    }
    free(g);
}

static inline rabbit_array ***rabbit_alloc_grid(void) {
    return (rabbit_array ***)calloc(1U, sizeof(rabbit_array **));
}

static inline void rabbit_free_row(rabbit_array **row) {
    size_t c;
    if (row == NULL) return;
    for (c = 0U; row[c] != NULL; ++c) free(row[c]);
    free(row);
}

static inline int rabbit_grid_push_row(rabbit_array ****h, rabbit_array **row) {
    rabbit_array ***grown;
    size_t n;
    if (h == NULL || *h == NULL || row == NULL) return -1;
    n = rabbit_grid_rows(*h);
    if (n > (SIZE_MAX / sizeof(rabbit_array **)) - 2U) return -1;
    grown = (rabbit_array ***)realloc(*h, sizeof(rabbit_array **) * (n + 2U));
    if (grown == NULL) return -1;
    grown[n] = row;
    grown[n + 1U] = NULL;
    *h = grown;
    return 0;
}

static inline int rabbit_grid_set_value(rabbit_array ****h, size_t r, size_t c, const rabbit_array *value) {
    rabbit_array ***g;
    rabbit_array **row;
    rabbit_array *copy;
    size_t rows, cols, i;
    if (h == NULL || *h == NULL || value == NULL) return -1;
    if (!rabbit_utf8_validate(value)) return -1;
    if (r > (SIZE_MAX / sizeof(rabbit_array **)) - 2U || c > (SIZE_MAX / sizeof(rabbit_array *)) - 2U ||
        r >= RABBIT_MAX_INDEX || c >= RABBIT_MAX_INDEX) return -1;
    copy = rabbit_strdup(value);
    if (copy == NULL) return -1;
    g = *h;
    rows = rabbit_grid_rows(g);
    if (r >= rows) {
        rabbit_array ***grown = (rabbit_array ***)realloc(g, sizeof(rabbit_array **) * (r + 2U));
        if (grown == NULL) { free(copy); return -1; }
        g = grown;
        *h = g;
        for (i = rows; i <= r; ++i) {
            rabbit_array **empty = (rabbit_array **)calloc(1U, sizeof(rabbit_array *));
            if (empty == NULL) { g[i] = NULL; free(copy); return -1; }
            g[i] = empty;
            g[i + 1U] = NULL;
        }
    }
    row = g[r];
    cols = rabbit_grid_cols(g, r);
    if (c >= cols) {
        rabbit_array **grown = (rabbit_array **)realloc(row, sizeof(rabbit_array *) * (c + 2U));
        if (grown == NULL) { free(copy); return -1; }
        row = grown;
        g[r] = row;
        for (i = cols; i < c; ++i) {
            row[i] = rabbit_strdup("");
            if (row[i] == NULL) { row[i] = NULL; free(copy); return -1; }
            row[i + 1U] = NULL;
        }
        row[c] = copy;
        row[c + 1U] = NULL;
        return 0;
    }
    free(row[c]);
    row[c] = copy;
    return 0;
}

static inline const rabbit_array *rabbit_grid_get_value(rabbit_array ***g, size_t r, size_t c) {
    size_t i;
    if (g == NULL) return NULL;
    for (i = 0U; i <= r; ++i) {
        if (g[i] == NULL) return NULL;
    }
    return rabbit_row_get(g[r], c);
}

static inline int rabbit_grid_append_row(rabbit_array ****h, const rabbit_array *const *values, size_t count) {
    rabbit_array **row;
    size_t i;
    if (h == NULL || *h == NULL || (values == NULL && count != 0U)) return -1;
    if (count > (SIZE_MAX / sizeof(rabbit_array *)) - 1U) return -1;
    row = (rabbit_array **)calloc(count + 1U, sizeof(rabbit_array *));
    if (row == NULL) return -1;
    for (i = 0U; i < count; ++i) {
        const rabbit_array *v = values[i] == NULL ? "" : values[i];
        if (!rabbit_utf8_validate(v)) { rabbit_free_row(row); return -1; }
        row[i] = rabbit_strdup(v);
        if (row[i] == NULL) { rabbit_free_row(row); return -1; }
    }
    if (rabbit_grid_push_row(h, row) != 0) { rabbit_free_row(row); return -1; }
    return 0;
}

static inline ssize_t rabbit_grid_find_row(rabbit_array ***g, size_t col, const rabbit_array *value) {
    size_t r;
    if (g == NULL || value == NULL) return -1;
    for (r = 0U; g[r] != NULL; ++r) {
        const rabbit_array *cell = rabbit_row_get(g[r], col);
        if (cell != NULL && strcmp(cell, value) == 0) return (ssize_t)r;
    }
    return -1;
}

/* ============================================================================
Conversões grid <-> arraydynamic
============================================================================ */
static inline rabbit_array ***datarabbit_from_arraydynamic(const arraydynamic *cells,
                                                           size_t rows, size_t cols) {
    rabbit_array ***grid;
    size_t r, c, idx;
    if (cells == NULL || !cells->string_elements) return NULL;
    if (cols == 0U) return NULL;
    if (rows > SIZE_MAX / cols) return NULL;
    if (cells->length != rows * cols) return NULL;
    if (cells->element_size != sizeof(char *)) return NULL;

    grid = (rabbit_array ***)calloc(rows + 1U, sizeof(rabbit_array **));
    if (grid == NULL) return NULL;

    for (r = 0U; r < rows; ++r) {
        rabbit_array **row = (rabbit_array **)calloc(cols + 1U, sizeof(rabbit_array *));
        if (row == NULL) {
            for (size_t k = 0U; k < r; ++k) rabbit_free_row(grid[k]);
            free(grid);
            return NULL;
        }
        for (c = 0U; c < cols; ++c) {
            idx = r * cols + c;
            const char *src = ((const char **)cells->data)[idx];
            const char *v = (src == NULL) ? "" : src;
            if (!rabbit_utf8_validate(v)) {
                rabbit_free_row(row);
                for (size_t k = 0U; k < r; ++k) rabbit_free_row(grid[k]);
                free(grid);
                return NULL;
            }
            row[c] = rabbit_strdup(v);
            if (row[c] == NULL) {
                rabbit_free_row(row);
                for (size_t k = 0U; k < r; ++k) rabbit_free_row(grid[k]);
                free(grid);
                return NULL;
            }
        }
        row[cols] = NULL;
        grid[r] = row;
    }
    grid[rows] = NULL;
    return grid;
}

static inline int datarabbit_to_arraydynamic(rabbit_array ***g, arraydynamic *out,
                                             size_t *out_rows, size_t *out_cols) {
    size_t rows, cols, r, c, max_cols;
    if (g == NULL || out == NULL) return -1;

    rows = rabbit_grid_rows(g);
    max_cols = 0U;
    for (r = 0U; r < rows; ++r) {
        size_t cc = rabbit_grid_cols(g, r);
        if (cc > max_cols) max_cols = cc;
    }
    cols = max_cols;

    /* Não reaproveita um array de outro tipo: mudar element_size com dados
       já alocados faria o push escrever além do buffer. */
    if (out->data != NULL &&
        (!out->string_elements || out->nested_elements || out->element_size != sizeof(char *)))
        return -1;
    if (out->data == NULL) {
        out->string_elements = 1;
        out->nested_elements = 0;
        out->element_size = sizeof(char *);
        out->length = 0U;
        out->capacity = 0U;
    } else {
        arraydynamic_clear(out);
    }

    if (cols == 0U) {
        if (out_rows) *out_rows = rows;
        if (out_cols) *out_cols = 0U;
        return 0;
    }

    if (arraydynamic_reserve(out, rows * cols) != 0) return -1;

    for (r = 0U; r < rows; ++r) {
        for (c = 0U; c < cols; ++c) {
            const rabbit_array *cell = rabbit_row_get(g[r], c);
            const char *v = (cell == NULL) ? "" : cell;
            if (arraydynamic_push(out, &v) != 0) {
                arraydynamic_clear(out);
                return -1;
            }
        }
    }

    if (out_rows) *out_rows = rows;
    if (out_cols) *out_cols = cols;
    return 0;
}

static inline int datarabbit_save_arraydynamic(const char *name, const arraydynamic *cells,
                                               size_t rows, size_t cols);
static inline int datarabbit_load_arraydynamic(const char *name, arraydynamic *out,
                                               size_t *out_rows, size_t *out_cols);
static inline void datarabbit_view_arraydynamic(const arraydynamic *cells,
                                                size_t rows, size_t cols);

/* ============================================================================
Arquivo .txt <-> grid
============================================================================ */
#define RABBIT_MARK     "\xE2\x99\x9C"
#define RABBIT_MARK_LEN 3U
#define RABBIT_OPEN     "[" RABBIT_MARK " "
#define RABBIT_CLOSE    " " RABBIT_MARK "]"

static inline int rabbit_hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

static inline int rabbit_write_escaped(FILE *file, const rabbit_array *value) {
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char *cursor;
    if (file == NULL) return -1;
    if (value == NULL) value = "";
    cursor = (const unsigned char *)value;
    if (fputs(RABBIT_OPEN, file) == EOF) return -1;
    while (*cursor != 0U) {
        unsigned char byte = *cursor++;
        const char *esc = NULL;
        switch (byte) {
            case '\\': esc = "\\\\"; break;
            case '[':  esc = "\\[";  break;
            case ']':  esc = "\\]";  break;
            case '\n': esc = "\\n";  break;
            case '\r': esc = "\\r";  break;
            case '\t': esc = "\\t";  break;
            default: break;
        }
        if (esc != NULL) {
            if (fputs(esc, file) == EOF) return -1;
        } else if (byte < 0x20U || byte == 0x7FU) {
            if (fputs("\\x", file) == EOF ||
                fputc(hex[byte >> 4U], file) == EOF ||
                fputc(hex[byte & 0x0FU], file) == EOF) return -1;
        } else if (fputc((int)byte, file) == EOF) {
            return -1;
        }
    }
    return fputs(RABBIT_CLOSE, file) == EOF ? -1 : 0;
}

/* fsync do diretório que contém path (best effort): faz o rename sobreviver a crash. */
static inline void rabbit_sync_dir(const rabbit_array *path) {
    rabbit_array *dir = rabbit_strdup(path);
    const char *slash;
    int fd;
    if (dir == NULL) return;
    slash = strrchr(dir, '/');
    if (slash == NULL) {
        free(dir);
        dir = rabbit_strdup(".");
        if (dir == NULL) return;
    } else if (slash == dir) {
        dir[1] = '\0';
    } else {
        dir[slash - dir] = '\0';
    }
    fd = open(dir, O_RDONLY);
    if (fd >= 0) { (void)fsync(fd); close(fd); }
    free(dir);
}

/* private != 0: arquivo novo nasce 0600 (espelho JIT). Senão: respeita o umask.
   Se o destino já existe, mantém as permissões dele. */
static inline int rabbit_save_grid_impl(rabbit_array ***g, const rabbit_array *path, int private_file) {
    static const char suffix[] = ".tmp.XXXXXX";
    FILE *file;
    int fd, result = 0;
    size_t r, c, path_length, size;
    rabbit_array *temporary_path;
    struct stat st;
    mode_t mode;
    if (path == NULL) return -1;
    path_length = strlen(path);
    if (path_length > SIZE_MAX - sizeof(suffix)) return -1;
    size = path_length + sizeof(suffix);
    temporary_path = (rabbit_array *)malloc(size);
    if (temporary_path == NULL) return -1;
    snprintf(temporary_path, size, "%s%s", path, suffix);
    fd = mkstemp(temporary_path);
    if (fd < 0 && errno == ENOENT) { rabbit_mkdir_parents(path); fd = mkstemp(temporary_path); }
    if (fd < 0) { free(temporary_path); return -1; }
    if (stat(path, &st) == 0) {
        mode = st.st_mode & 07777;
    } else if (private_file) {
        mode = 0600;
    } else {
        mode_t um = umask(0);
        umask(um);
        mode = (mode_t)(0666 & ~um);
    }
    (void)fchmod(fd, mode);
    file = fdopen(fd, "w");
    if (file == NULL) { close(fd); unlink(temporary_path); free(temporary_path); return -1; }
    if (g != NULL) {
        for (r = 0U; g[r] != NULL && result == 0; ++r) {
            for (c = 0U; g[r][c] != NULL; ++c) {
                if (rabbit_write_escaped(file, g[r][c]) != 0) { result = -1; break; }
            }
            if (result == 0 && fputc('\n', file) == EOF) result = -1;
        }
    }
    if (result == 0 && fflush(file) != 0) result = -1;
    if (result == 0 && fsync(fileno(file)) != 0) result = -1;
    if (fclose(file) != 0) result = -1;
    if (result == 0 && rename(temporary_path, path) != 0) result = -1;
    if (result == 0) rabbit_sync_dir(path);
    if (result != 0) unlink(temporary_path);
    free(temporary_path);
    return result;
}

static inline int rabbit_save_grid_to_file(rabbit_array ***g, const rabbit_array *path) {
    return rabbit_save_grid_impl(g, path, 0);
}

static inline int rabbit_parse_cell(const char *line, size_t len, size_t *pos, rabbit_array **out) {
    size_t p = *pos, n = 0U;
    int closed = 0;
    rabbit_array *value;
    if (p >= len || line[p] != '[') return -1;
    ++p;
    value = (rabbit_array *)malloc(len - p + 1U);
    if (value == NULL) return -1;
    while (p < len && line[p] != '\n' && line[p] != '\r') {
        unsigned char ch = (unsigned char)line[p++];
        if (ch == ']') { closed = 1; break; }
        if (ch == '\\') {
            if (p >= len) goto bad;
            ch = (unsigned char)line[p++];
            switch (ch) {
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case '\\': case '[': case ']': break;
                case 'x': {
                    int hi, lo;
                    if (p + 1U >= len) goto bad;
                    hi = rabbit_hex_value(line[p]);
                    lo = rabbit_hex_value(line[p + 1U]);
                    if (hi < 0 || lo < 0) goto bad;
                    ch = (unsigned char)((hi << 4) | lo);
                    p += 2U;
                    if (ch == 0U) goto bad;
                    break;
                }
                default: goto bad;
            }
        }
        value[n++] = (rabbit_array)ch;
    }
    if (!closed) goto bad;
    {
        const size_t open_len = RABBIT_MARK_LEN + 1U;
        if (n < 2U * open_len ||
            memcmp(value, RABBIT_MARK " ", open_len) != 0 ||
            memcmp(value + n - open_len, " " RABBIT_MARK, open_len) != 0) goto bad;
        memmove(value, value + open_len, n - 2U * open_len);
        value[n - 2U * open_len] = '\0';
    }
    if (!rabbit_utf8_validate(value)) goto bad;
    *out = value;
    *pos = p;
    return 0;
bad:
    free(value);
    return -1;
}

static inline rabbit_array ***rabbit_read_file(const rabbit_array *path) {
    FILE *file;
    rabbit_array *line = NULL;
    size_t line_capacity = 0U;
    ssize_t nread;
    rabbit_array ***g;
    size_t rows = 0U, grid_capacity = 8U;
    if (path == NULL) return NULL;
    file = fopen(path, "r");
    if (file == NULL) return NULL;
    g = (rabbit_array ***)calloc(grid_capacity, sizeof(rabbit_array **));
    if (g == NULL) { fclose(file); return NULL; }
    while ((nread = getline(&line, &line_capacity, file)) != -1) {
        size_t pos = 0U, len = (size_t)nread, cols = 0U;
        rabbit_array **row = (rabbit_array **)calloc(1U, sizeof(rabbit_array *));
        if (row == NULL) goto fail;
        while (pos < len && line[pos] != '\n' && line[pos] != '\r') {
            rabbit_array *cell = NULL;
            rabbit_array **grown;
            if (rabbit_parse_cell(line, len, &pos, &cell) != 0) goto fail_row;
            grown = (rabbit_array **)realloc(row, sizeof(rabbit_array *) * (cols + 2U));
            if (grown == NULL) { free(cell); goto fail_row; }
            row = grown;
            row[cols++] = cell;
            row[cols] = NULL;
        }
        if (rows + 2U > grid_capacity) {
            size_t new_capacity = grid_capacity * 2U;
            rabbit_array ***grown = (rabbit_array ***)realloc(g, new_capacity * sizeof(rabbit_array **));
            if (grown == NULL) goto fail_row;
            g = grown;
            grid_capacity = new_capacity;
        }
        g[rows++] = row;
        g[rows] = NULL;
        continue;
fail_row:
        rabbit_free_row(row);
fail:
        free(line);
        fclose(file);
        rabbit_free_grid(g);
        return NULL;
    }
    free(line);
    if (ferror(file)) { fclose(file); rabbit_free_grid(g); return NULL; }
    if (fclose(file) != 0) { rabbit_free_grid(g); return NULL; }
    return g;
}

/* ============================================================================
Estado global
============================================================================ */
#define RABBIT_MAX_OPEN 3
/* Espelho de trabalho (JIT) do banco em memória. O nome começa com ponto e NÃO
   começa com "datarabbit+", então rabbit_make_filename() jamais gera esse nome:
   nenhum banco do usuário pode colidir com ele. Inclui o PID, então dois
   processos no mesmo diretório não se atrapalham. */
static inline const char *rabbit_jit_path(void) {
    static char path[4352];
    const char *d = rabbit_filedir ? rabbit_filedir : "";
    size_t dl = strlen(d);
    int n;
    if (rabbit_filedir_is_file()) {                 /* espelho na pasta do arquivo */
        const char *sl = strrchr(d, '/');
        dl = sl ? (size_t)(sl - d) + 1U : 0U;
    }
    n = snprintf(path, sizeof(path), "%.*s%s.datarabbit-jit-%ld.tmp",
                 (int)dl, d, (dl > 0U && d[dl - 1U] != '/') ? "/" : "", (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(path))
        snprintf(path, sizeof(path), ".datarabbit-jit-%ld.tmp", (long)getpid());
    return path;
}

typedef struct rabbit_slot {
    rabbit_array ***grid;
    rabbit_array *filename;
} rabbit_slot;

static rabbit_slot rabbit_slots[RABBIT_MAX_OPEN];
static rabbit_array ****rabbit_handle = NULL;
static size_t rabbit_size_rows = 0U;
static size_t rabbit_size_cols = 0U;
static int rabbit_jit_dirty = 0;

static inline void rabbit_refresh_size(rabbit_array ****h) {
    rabbit_array ***g;
    size_t r, c;
    rabbit_size_rows = 0U;
    rabbit_size_cols = 0U;
    if (h == NULL || *h == NULL) return;
    g = *h;
    for (r = 0U; g[r] != NULL; ++r) {
        c = 0U;
        while (g[r][c] != NULL) ++c;
        if (c > rabbit_size_cols) rabbit_size_cols = c;
        ++rabbit_size_rows;
    }
}

/* ============================================================================
JIT
============================================================================ */
static inline int rabbit_jit_workspace_open(void) {
    int fd = open(rabbit_jit_path(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return -1;
    return close(fd) == 0 ? 0 : -1;
}

static inline void rabbit_jit_temp_close(void) {
    remove(rabbit_jit_path());
    rabbit_jit_dirty = 0;
}

static inline int rabbit_jit_workspace_flush(rabbit_array ****h) {
    if (h == NULL || *h == NULL) return -1;
    if (rabbit_save_grid_impl(*h, rabbit_jit_path(), 1) != 0) return -1;
    rabbit_jit_dirty = 0;
    return 0;
}

static inline void rabbit_jit_mark_dirty(void) {
    rabbit_jit_dirty = 1;
}

static inline int rabbit_jit_ready(rabbit_array ****h) {
    if (rabbit_jit_dirty) return rabbit_jit_workspace_flush(h);
    return rabbit_jit_workspace_open();
}

/* Fecha uma mutação já aplicada em memória. A memória é a fonte da verdade:
   se espelhar no arquivo JIT falhar, a operação continua valendo (retorna 0)
   e rabbit_jit_dirty permanece 1 para o próximo flush tentar de novo.
   O arquivo definitivo só muda em rabbit_save. */
static inline int rabbit_commit(rabbit_array ****h) {
    rabbit_jit_mark_dirty();
#if RABBIT_JIT_AUTOFLUSH
    (void)rabbit_jit_workspace_flush(h);
#endif
    rabbit_refresh_size(h);
    return 0;
}

/* ============================================================================
Abrir / fechar bancos
============================================================================ */
static inline rabbit_array ***rabbit_load_path(const rabbit_array *path) {
    FILE *probe;
    if (path == NULL) return NULL;
    probe = fopen(path, "r");
    if (probe == NULL) {
        if (errno != ENOENT) return NULL;
        rabbit_mkdir_parents(path);
        probe = fopen(path, "ab");
        if (probe == NULL) return NULL;
    }
    if (fclose(probe) != 0) return NULL;
    return rabbit_read_file(path);
}

static inline rabbit_array ***rabbit_load_named(const rabbit_array *name) {
    rabbit_array *path;
    rabbit_array ***g;
    if (name == NULL || name[0] == '\0') return NULL;
    path = rabbit_make_filename(name);
    if (path == NULL) return NULL;
    g = rabbit_load_path(path);
    free(path);
    return g;
}

/* Abre o caminho já resolvido. Assume a posse de path (libera em qualquer falha). */
static inline rabbit_array ****rabbit_open_resolved(rabbit_array *path) {
    rabbit_slot *slot = NULL;
    rabbit_array ***g;
    size_t i;
    if (path == NULL) return NULL;
    for (i = 0U; i < RABBIT_MAX_OPEN; ++i) {
        if (rabbit_slots[i].grid != NULL) {
            if (strcmp(rabbit_slots[i].filename, path) == 0) { free(path); errno = EBUSY; return NULL; }
        } else if (slot == NULL) {
            slot = &rabbit_slots[i];
        }
    }
    if (slot == NULL) { free(path); errno = EMFILE; return NULL; }
    g = rabbit_load_path(path);
    if (g == NULL) { free(path); return NULL; }
    slot->grid = g;
    slot->filename = path;
    if (rabbit_jit_workspace_flush(&slot->grid) != 0) {
        rabbit_free_grid(slot->grid);
        free(slot->filename);
        slot->grid = NULL;
        slot->filename = NULL;
        return NULL;
    }
    rabbit_refresh_size(&slot->grid);
    return &slot->grid;
}

static inline rabbit_array ****rabbit_open(const rabbit_array *name) {
    if (name == NULL || name[0] == '\0') return NULL;
    return rabbit_open_resolved(rabbit_make_filename(name));
}

/* Abre EXATAMENTE este arquivo (sem FILEDIR, sem "datarabbit+", sem ".txt").
   Cria o arquivo e as pastas que faltarem. Ex.: Screen/0/FrontEnd/Style.txt */
static inline rabbit_array ****rabbit_open_file(const char *path) {
    if (path == NULL || path[0] == '\0') return NULL;
    return rabbit_open_resolved(rabbit_strdup(path));
}

static inline void rabbit_close(rabbit_array ****handle) {
    size_t i;
    int any_open = 0;
    if (handle == NULL) return;
    if (rabbit_handle == handle) rabbit_handle = NULL;
    for (i = 0U; i < RABBIT_MAX_OPEN; ++i) {
        if (handle == &rabbit_slots[i].grid) {
            rabbit_free_grid(rabbit_slots[i].grid);
            free(rabbit_slots[i].filename);
            rabbit_slots[i].grid = NULL;
            rabbit_slots[i].filename = NULL;
        }
        if (rabbit_slots[i].grid != NULL) any_open = 1;
    }
    if (!any_open) {
        rabbit_size_rows = 0U;
        rabbit_size_cols = 0U;
        rabbit_jit_temp_close();
    }
}

/* ============================================================================
Operações básicas de banco (grid tradicional)
============================================================================ */
static inline rabbit_array ***rabbit_grid(rabbit_array ****h) {
    if (h == NULL) return NULL;
    return *h;
}

static inline const rabbit_array *rabbit_get(rabbit_array ****h, size_t r, size_t c) {
    if (h == NULL || *h == NULL) return NULL;
    return rabbit_grid_get_value(*h, r, c);
}

static inline int rabbit_set(rabbit_array ****h, size_t r, size_t c, const rabbit_array *v) {
    if (h == NULL || *h == NULL) return -1;
    if (rabbit_grid_set_value(h, r, c, v) != 0) return -1;
    return rabbit_commit(h);
}

static inline int rabbit_push(rabbit_array ****h, const rabbit_array *const *values, size_t count) {
    if (h == NULL || *h == NULL) return -1;
    if (rabbit_grid_append_row(h, values, count) != 0) return -1;
    return rabbit_commit(h);
}

static inline ssize_t rabbit_find(rabbit_array ****h, size_t col, const rabbit_array *v) {
    if (h == NULL || *h == NULL) return -1;
    return rabbit_grid_find_row(*h, col, v);
}

static inline void rabbit_view(rabbit_array ****h) {
    size_t r, c;
    if (h == NULL || *h == NULL) { printf("(empty)\n"); return; }
    for (r = 0U; (*h)[r] != NULL; ++r) {
        for (c = 0U; (*h)[r][c] != NULL; ++c) printf("[ %s ]", (*h)[r][c]);
        putchar('\n');
    }
}

static inline int rabbit_save(rabbit_array ****h, const rabbit_array *name) {
    rabbit_array *path;
    int result;
    if (h == NULL || *h == NULL || name == NULL) return -1;
    path = rabbit_make_filename(name);
    if (path == NULL) return -1;
    result = rabbit_save_grid_to_file(*h, path);
    free(path);
    return result;
}

/* Salva o banco no PRÓPRIO arquivo com que foi aberto (rabbit_open / rabbit_open_file). */
static inline int rabbit_save_here(rabbit_array ****handle) {
    size_t i;
    if (handle == NULL || *handle == NULL) return -1;
    for (i = 0U; i < RABBIT_MAX_OPEN; ++i) {
        if (handle == &rabbit_slots[i].grid && rabbit_slots[i].filename != NULL)
            return rabbit_save_grid_to_file(*handle, rabbit_slots[i].filename);
    }
    return -1;
}

static inline int rabbit_rename(rabbit_array ****h, const rabbit_array *new_name) {
    rabbit_array *new_path;
    size_t i;
    rabbit_slot *slot = NULL;
    if (h == NULL || *h == NULL || new_name == NULL) return -1;
    for (i = 0U; i < RABBIT_MAX_OPEN; ++i) {
        if (h == &rabbit_slots[i].grid) slot = &rabbit_slots[i];
    }
    if (slot == NULL || slot->filename == NULL) return -1;
    new_path = rabbit_make_filename(new_name);
    if (new_path == NULL) return -1;
    if (strcmp(slot->filename, new_path) == 0) { free(new_path); return 0; }
    if (access(new_path, F_OK) == 0) { free(new_path); errno = EEXIST; return -1; }
    if (rabbit_save_grid_to_file(*h, slot->filename) != 0 ||
        rename(slot->filename, new_path) != 0) {
        free(new_path);
        return -1;
    }
    free(slot->filename);
    slot->filename = new_path;
    return 0;
}

/* ============================================================================
🚀 OPERAÇÕES AVANÇADAS (novas!)
============================================================================ */

/* Deleta a linha r (desloca as de baixo pra cima). */
static inline int rabbit_delete_row(rabbit_array ****h, size_t r) {
    rabbit_array ***g;
    size_t rows, i;
    if (h == NULL || *h == NULL) return -1;
    g = *h;
    rows = rabbit_grid_rows(g);
    if (r >= rows) return -1;
    rabbit_free_row(g[r]);
    for (i = r; i < rows; ++i) g[i] = g[i + 1U];
    /* realloc pra encolher (opcional, mas limpo) */
    {
        rabbit_array ***shrunk = (rabbit_array ***)realloc(g, sizeof(rabbit_array **) * (rows));
        if (shrunk != NULL) {
            *h = shrunk;
            shrunk[rows - 1U] = NULL;
        }
    }
    return rabbit_commit(h);
}

/* Deleta a coluna c de todas as linhas. */
static inline int rabbit_delete_col(rabbit_array ****h, size_t c) {
    rabbit_array ***g;
    size_t r;
    if (h == NULL || *h == NULL) return -1;
    g = *h;
    for (r = 0U; g[r] != NULL; ++r) {
        size_t cols = rabbit_grid_cols(g, r);
        if (c < cols) {
            size_t k;
            free(g[r][c]);
            for (k = c; k + 1U < cols; ++k) g[r][k] = g[r][k + 1U];
            g[r][cols - 1U] = NULL;
        }
    }
    return rabbit_commit(h);
}

/* Insere uma linha na posição at (desloca as de baixo). */
static inline int rabbit_insert_row(rabbit_array ****h, size_t at,
                                    const rabbit_array *const *values, size_t count) {
    rabbit_array ***g;
    rabbit_array **row;
    size_t rows, i;
    if (h == NULL || *h == NULL) return -1;
    if (values == NULL && count != 0U) return -1;
    g = *h;
    rows = rabbit_grid_rows(g);
    if (at > rows) return -1;

    /* cria a linha nova */
    row = (rabbit_array **)calloc(count + 1U, sizeof(rabbit_array *));
    if (row == NULL) return -1;
    for (i = 0U; i < count; ++i) {
        const rabbit_array *v = values[i] == NULL ? "" : values[i];
        if (!rabbit_utf8_validate(v)) { rabbit_free_row(row); return -1; }
        row[i] = rabbit_strdup(v);
        if (row[i] == NULL) { rabbit_free_row(row); return -1; }
    }

    /* cresce o array de linhas */
    {
        rabbit_array ***grown = (rabbit_array ***)realloc(g, sizeof(rabbit_array **) * (rows + 2U));
        if (grown == NULL) { rabbit_free_row(row); return -1; }
        g = grown;
        *h = g;
    }

    /* desloca as linhas pra baixo */
    for (i = rows; i > at; --i) g[i] = g[i - 1U];
    g[at] = row;
    g[rows + 1U] = NULL;

    return rabbit_commit(h);
}

/* Conta quantas linhas têm "value" na coluna "col". */
static inline size_t rabbit_count(rabbit_array ****h, size_t col, const rabbit_array *value) {
    rabbit_array ***g;
    size_t r, n = 0U;
    if (h == NULL || *h == NULL || value == NULL) return 0U;
    g = *h;
    for (r = 0U; g[r] != NULL; ++r) {
        const rabbit_array *cell = rabbit_row_get(g[r], col);
        if (cell != NULL && strcmp(cell, value) == 0) ++n;
    }
    return n;
}

/* Retorna TODAS as linhas que batem (arraydynamic de size_t). */
static inline int rabbit_find_all(rabbit_array ****h, size_t col, const rabbit_array *value,
                                  arraydynamic *out) {
    rabbit_array ***g;
    size_t r;
    if (h == NULL || *h == NULL || value == NULL || out == NULL) return -1;
    if (out->string_elements || out->nested_elements || out->element_size != sizeof(size_t)) return -1;
    g = *h;
    for (r = 0U; g[r] != NULL; ++r) {
        const rabbit_array *cell = rabbit_row_get(g[r], col);
        if (cell != NULL && strcmp(cell, value) == 0) {
            if (arraydynamic_push(out, &r) != 0) return -1;
        }
    }
    return 0;
}

/* Callback de filtro: recebe a linha e um contexto, retorna 1 pra manter. */
typedef int (*rabbit_filter_fn)(rabbit_array **row, void *ctx);

/* Filtra linhas com callback customizado.
   out recebe os ÍNDICES das linhas que passaram (size_t). */
static inline int rabbit_filter(rabbit_array ****h, rabbit_filter_fn fn, void *ctx,
                                arraydynamic *out_rows) {
    rabbit_array ***g;
    size_t r;
    if (h == NULL || *h == NULL || fn == NULL || out_rows == NULL) return -1;
    if (out_rows->string_elements || out_rows->nested_elements || out_rows->element_size != sizeof(size_t)) return -1;
    g = *h;
    for (r = 0U; g[r] != NULL; ++r) {
        if (fn(g[r], ctx)) {
            if (arraydynamic_push(out_rows, &r) != 0) return -1;
        }
    }
    return 0;
}

/* Ordena as linhas pela coluna "col" (asc=1 crescente, asc=0 decrescente). */
static size_t rabbit_sort_col_g = 0U;
static int rabbit_sort_asc_g = 1;

static inline int rabbit_sort_cmp(const void *a, const void *b) {
    rabbit_array **ra = *(rabbit_array **const *)a;
    rabbit_array **rb = *(rabbit_array **const *)b;
    const char *va = rabbit_row_get(ra, rabbit_sort_col_g);
    const char *vb = rabbit_row_get(rb, rabbit_sort_col_g);
    if (va == NULL) va = "";
    if (vb == NULL) vb = "";
    int r = strcmp(va, vb);
    r = (r > 0) - (r < 0);
    return rabbit_sort_asc_g ? r : -r;
}

static inline int rabbit_sort(rabbit_array ****h, size_t col, int ascending) {
    rabbit_array ***g;
    size_t rows;
    if (h == NULL || *h == NULL) return -1;
    g = *h;
    rows = rabbit_grid_rows(g);
    if (rows < 2U) return 0;
    rabbit_sort_col_g = col;
    rabbit_sort_asc_g = ascending ? 1 : 0;
    qsort(g, rows, sizeof(rabbit_array **), rabbit_sort_cmp);
    return rabbit_commit(h);
}

/* Exporta o banco pra CSV (arquivo separado, não mexe no .txt do DataRabbit). */
static inline int rabbit_export_csv(rabbit_array ****h, const char *filename, char sep) {
    rabbit_array ***g;
    FILE *f;
    size_t r, c;
    if (h == NULL || *h == NULL || filename == NULL) return -1;
    if (sep == '\0') sep = ',';
    g = *h;
    f = fopen(filename, "w");
    if (f == NULL) return -1;
    for (r = 0U; g[r] != NULL; ++r) {
        for (c = 0U; g[r][c] != NULL; ++c) {
            const char *v = g[r][c];
            int need_quote = (strchr(v, sep) != NULL || strchr(v, '"') != NULL ||
                              strchr(v, '\n') != NULL || strchr(v, '\r') != NULL);
            if (c > 0U) fputc(sep, f);
            if (need_quote) fputc('"', f);
            for (const char *p = v; *p; ++p) {
                if (*p == '"') fputs("\"\"", f);
                else fputc(*p, f);
            }
            if (need_quote) fputc('"', f);
        }
        fputc('\n', f);
    }
    {
        int bad = ferror(f);
        return (fclose(f) == 0 && !bad) ? 0 : -1;
    }
}

/* ============================================================================
Operações arraydynamic (com nome "dynamic" no meio)
============================================================================ */

/* Salva arraydynamic em banco ABERTO (com rollback se falhar). */
static inline int rabbit_array_dynamic_save(rabbit_array ****handle, const arraydynamic *cells,
                                            size_t row_count, size_t col_count) {
    rabbit_array ***old_grid;
    rabbit_array ***new_grid;
    if (handle == NULL || *handle == NULL || cells == NULL || col_count == 0U) return -1;
    if (!cells->string_elements || cells->element_size != sizeof(char *)) return -1;
    if (row_count > SIZE_MAX / col_count) return -1;
    if (cells->length != row_count * col_count) return -1;

    old_grid = *handle;
    new_grid = datarabbit_from_arraydynamic(cells, row_count, col_count);
    if (new_grid == NULL) return -1;

    *handle = new_grid;
    rabbit_refresh_size(handle);
    rabbit_jit_mark_dirty();

#if RABBIT_JIT_AUTOFLUSH
    if (rabbit_jit_workspace_flush(handle) != 0) {
        /* ROLLBACK: restaura o grid antigo */
        rabbit_free_grid(new_grid);
        *handle = old_grid;
        rabbit_refresh_size(handle);
        return -1;
    }
#endif
    rabbit_free_grid(old_grid);
    return 0;
}

/* Carrega banco ABERTO para arraydynamic. */
static inline int rabbit_array_dynamic_load(rabbit_array ****handle, arraydynamic *out,
                                            size_t *out_rows, size_t *out_cols) {
    if (handle == NULL || *handle == NULL || out == NULL) return -1;
    return datarabbit_to_arraydynamic(*handle, out, out_rows, out_cols);
}

/* Salva arraydynamic direto em arquivo (sem abrir banco). */
static inline int rabbit_array_dynamic_save_file(const char *name, const arraydynamic *cells,
                                                 size_t row_count, size_t col_count) {
    rabbit_array *path;
    rabbit_array ***grid;
    int result;
    if (name == NULL || cells == NULL) return -1;
    path = rabbit_make_filename(name);
    if (path == NULL) return -1;
    grid = datarabbit_from_arraydynamic(cells, row_count, col_count);
    if (grid == NULL) { free(path); return -1; }
    result = rabbit_save_grid_to_file(grid, path);
    rabbit_free_grid(grid);
    free(path);
    return result;
}

/* Carrega arquivo em arraydynamic (sem abrir banco). */
static inline int rabbit_array_dynamic_load_file(const char *name, arraydynamic *out,
                                                 size_t *out_rows, size_t *out_cols) {
    rabbit_array *path;
    rabbit_array ***grid;
    int result;
    if (name == NULL || out == NULL) return -1;
    path = rabbit_make_filename(name);
    if (path == NULL) return -1;
    grid = rabbit_read_file(path);
    free(path);
    if (grid == NULL) return -1;
    result = datarabbit_to_arraydynamic(grid, out, out_rows, out_cols);
    rabbit_free_grid(grid);
    return result;
}

/* Imprime arraydynamic como tabela formatada. */
static inline void rabbit_array_dynamic_view(const arraydynamic *cells,
                                             size_t row_count, size_t col_count) {
    size_t r, c, idx;
    if (cells == NULL || !cells->string_elements || col_count == 0U) {
        printf("(empty)\n");
        return;
    }
    if (row_count == 0U || cells->length < row_count * col_count) {
        row_count = cells->length / col_count;
    }
    for (r = 0U; r < row_count; ++r) {
        for (c = 0U; c < col_count; ++c) {
            idx = r * col_count + c;
            const char *v = ((const char **)cells->data)[idx];
            printf("[ %s ]", v == NULL ? "" : v);
        }
        putchar('\n');
    }
}

/* Implementações das funções forward-declared */
static inline int datarabbit_save_arraydynamic(const char *name, const arraydynamic *cells,
                                               size_t rows, size_t cols) {
    return rabbit_array_dynamic_save_file(name, cells, rows, cols);
}

static inline int datarabbit_load_arraydynamic(const char *name, arraydynamic *out,
                                               size_t *out_rows, size_t *out_cols) {
    return rabbit_array_dynamic_load_file(name, out, out_rows, out_cols);
}

static inline void datarabbit_view_arraydynamic(const arraydynamic *cells,
                                                size_t rows, size_t cols) {
    rabbit_array_dynamic_view(cells, rows, cols);
}

/* ============================================================================
JSON — banco de objetos sobre o formato DataRabbit
Linha de propriedade: [♜ chave ♜][♜ tipo ♜][♜ valor ♜]   tipo: s n b z j
Linha separadora (fim de cada objeto): [♜ 1 ♜]
============================================================================ */
#ifndef RABBIT_JSON_MAX_DEPTH
#define RABBIT_JSON_MAX_DEPTH 128
#endif
#define RABBIT_JSON_SEP "1"

/* ---- construtor de strings ------------------------------------------------ */
typedef struct rabbit_sb { char *data; size_t len; size_t cap; int err; } rabbit_sb;

static inline void rabbit_sb_put(rabbit_sb *b, const char *s, size_t n) {
    if (b->err) return;
    if (n > SIZE_MAX - b->len - 1U) { b->err = 1; return; }
    if (b->len + n + 1U > b->cap) {
        size_t cap = b->cap ? b->cap : 64U;
        char *d;
        while (cap < b->len + n + 1U) {
            if (cap > SIZE_MAX / 2U) { b->err = 1; return; }
            cap *= 2U;
        }
        d = (char *)realloc(b->data, cap);
        if (d == NULL) { b->err = 1; return; }
        b->data = d;
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}
static inline void rabbit_sb_str(rabbit_sb *b, const char *s) { rabbit_sb_put(b, s, strlen(s)); }
static inline void rabbit_sb_putc(rabbit_sb *b, char c) { rabbit_sb_put(b, &c, 1U); }

static inline void rabbit_sb_jstring(rabbit_sb *b, const char *s) {
    static const char hex[] = "0123456789ABCDEF";
    rabbit_sb_putc(b, '"');
    for (; *s != '\0'; ++s) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"':  rabbit_sb_str(b, "\\\""); break;
            case '\\': rabbit_sb_str(b, "\\\\"); break;
            case '\n': rabbit_sb_str(b, "\\n");  break;
            case '\r': rabbit_sb_str(b, "\\r");  break;
            case '\t': rabbit_sb_str(b, "\\t");  break;
            case '\b': rabbit_sb_str(b, "\\b");  break;
            case '\f': rabbit_sb_str(b, "\\f");  break;
            default:
                if (c < 0x20U) {
                    char u[7] = { '\\', 'u', '0', '0', hex[c >> 4U], hex[c & 0x0FU], '\0' };
                    rabbit_sb_put(b, u, 6U);
                } else {
                    rabbit_sb_putc(b, (char)c);
                }
        }
    }
    rabbit_sb_putc(b, '"');
}

/* ---- validação (retornam ponteiro após o trecho, ou NULL) ----------------- */
static inline const char *rabbit_js_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    return p;
}

static inline const char *rabbit_js_string(const char *p) {
    int i;
    if (*p != '"') return NULL;
    ++p;
    while (*p != '\0') {
        unsigned char c = (unsigned char)*p;
        if (c == '"') return p + 1;
        if (c < 0x20U) return NULL;
        if (c == '\\') {
            ++p;
            switch (*p) {
                case '"': case '\\': case '/': case 'b': case 'f': case 'n': case 'r': case 't':
                    ++p; break;
                case 'u':
                    for (i = 1; i <= 4; ++i) if (rabbit_hex_value(p[i]) < 0) return NULL;
                    p += 5; break;
                default: return NULL;
            }
        } else {
            ++p;
        }
    }
    return NULL;
}

static inline int rabbit_js_digit(char c) { return c >= '0' && c <= '9'; }

static inline const char *rabbit_js_number(const char *p) {
    if (*p == '-') ++p;
    if (*p == '0') ++p;
    else if (*p >= '1' && *p <= '9') { while (rabbit_js_digit(*p)) ++p; }
    else return NULL;
    if (*p == '.') {
        ++p;
        if (!rabbit_js_digit(*p)) return NULL;
        while (rabbit_js_digit(*p)) ++p;
    }
    if (*p == 'e' || *p == 'E') {
        ++p;
        if (*p == '+' || *p == '-') ++p;
        if (!rabbit_js_digit(*p)) return NULL;
        while (rabbit_js_digit(*p)) ++p;
    }
    return p;
}

static inline const char *rabbit_js_value(const char *p, int depth) {
    p = rabbit_js_ws(p);
    if (depth > RABBIT_JSON_MAX_DEPTH) return NULL;
    switch (*p) {
        case '"': return rabbit_js_string(p);
        case '{':
            p = rabbit_js_ws(p + 1);
            if (*p == '}') return p + 1;
            for (;;) {
                p = rabbit_js_string(rabbit_js_ws(p));
                if (p == NULL) return NULL;
                p = rabbit_js_ws(p);
                if (*p != ':') return NULL;
                p = rabbit_js_value(p + 1, depth + 1);
                if (p == NULL) return NULL;
                p = rabbit_js_ws(p);
                if (*p == ',') { ++p; continue; }
                if (*p == '}') return p + 1;
                return NULL;
            }
        case '[':
            p = rabbit_js_ws(p + 1);
            if (*p == ']') return p + 1;
            for (;;) {
                p = rabbit_js_value(p, depth + 1);
                if (p == NULL) return NULL;
                p = rabbit_js_ws(p);
                if (*p == ',') { ++p; continue; }
                if (*p == ']') return p + 1;
                return NULL;
            }
        case 't': return strncmp(p, "true", 4U) == 0 ? p + 4 : NULL;
        case 'f': return strncmp(p, "false", 5U) == 0 ? p + 5 : NULL;
        case 'n': return strncmp(p, "null", 4U) == 0 ? p + 4 : NULL;
        default:  return rabbit_js_number(p);
    }
}

/* Decodifica uma string JSON já validada: [p, end) inclui as aspas. */
static inline uint32_t rabbit_js_hex4(const char *p) {
    return ((uint32_t)rabbit_hex_value(p[0]) << 12U) | ((uint32_t)rabbit_hex_value(p[1]) << 8U) |
           ((uint32_t)rabbit_hex_value(p[2]) << 4U) | (uint32_t)rabbit_hex_value(p[3]);
}

static inline char *rabbit_js_decode(const char *p, const char *end) {
    char *out = (char *)malloc((size_t)(end - p) + 1U);
    size_t n = 0U;
    if (out == NULL) return NULL;
    ++p;
    while (p < end - 1) {
        unsigned char c = (unsigned char)*p++;
        if (c != '\\') { out[n++] = (char)c; continue; }
        c = (unsigned char)*p++;
        switch (c) {
            case 'b': out[n++] = '\b'; break;
            case 'f': out[n++] = '\f'; break;
            case 'n': out[n++] = '\n'; break;
            case 'r': out[n++] = '\r'; break;
            case 't': out[n++] = '\t'; break;
            case 'u': {
                uint32_t cp = rabbit_js_hex4(p);
                p += 4;
                if (cp >= 0xD800U && cp <= 0xDBFFU) {
                    uint32_t lo;
                    if (p[0] != '\\' || p[1] != 'u') goto bad;
                    lo = rabbit_js_hex4(p + 2);
                    if (lo < 0xDC00U || lo > 0xDFFFU) goto bad;
                    cp = 0x10000U + ((cp - 0xD800U) << 10U) + (lo - 0xDC00U);
                    p += 6;
                } else if (cp >= 0xDC00U && cp <= 0xDFFFU) {
                    goto bad;
                }
                if (cp == 0U) goto bad;
                if (cp < 0x80U) {
                    out[n++] = (char)cp;
                } else if (cp < 0x800U) {
                    out[n++] = (char)(0xC0U | (cp >> 6U));
                    out[n++] = (char)(0x80U | (cp & 0x3FU));
                } else if (cp < 0x10000U) {
                    out[n++] = (char)(0xE0U | (cp >> 12U));
                    out[n++] = (char)(0x80U | ((cp >> 6U) & 0x3FU));
                    out[n++] = (char)(0x80U | (cp & 0x3FU));
                } else {
                    out[n++] = (char)(0xF0U | (cp >> 18U));
                    out[n++] = (char)(0x80U | ((cp >> 12U) & 0x3FU));
                    out[n++] = (char)(0x80U | ((cp >> 6U) & 0x3FU));
                    out[n++] = (char)(0x80U | (cp & 0x3FU));
                }
                break;
            }
            default: out[n++] = (char)c; break;   /* " \ / */
        }
    }
    out[n] = '\0';
    if (!rabbit_utf8_validate(out)) goto bad;
    return out;
bad:
    free(out);
    return NULL;
}

static inline void rabbit_js_compact(rabbit_sb *b, const char *p, const char *end) {
    int in_string = 0;
    for (; p < end; ++p) {
        char c = *p;
        if (in_string) {
            rabbit_sb_putc(b, c);
            if (c == '\\') { ++p; rabbit_sb_putc(b, *p); }
            else if (c == '"') in_string = 0;
        } else if (c == '"') {
            in_string = 1;
            rabbit_sb_putc(b, c);
        } else if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            rabbit_sb_putc(b, c);
        }
    }
}

/* Lê um valor JSON: devolve o ponteiro após ele; *type e *value (malloc). */
static inline const char *rabbit_jg_value(const char *p, char *type, char **value) {
    const char *q;
    p = rabbit_js_ws(p);
    q = rabbit_js_value(p, 0);
    if (q == NULL) return NULL;
    switch (*p) {
        case '"':
            *type = 's';
            *value = rabbit_js_decode(p, q);
            break;
        case '{': case '[': {
            rabbit_sb sb = { NULL, 0U, 0U, 0 };
            *type = 'j';
            rabbit_js_compact(&sb, p, q);
            if (sb.err) { free(sb.data); return NULL; }
            *value = sb.data;
            break;
        }
        case 't': case 'f': {
            size_t n = (size_t)(q - p);
            *type = 'b';
            *value = (char *)malloc(n + 1U);
            if (*value != NULL) { memcpy(*value, p, n); (*value)[n] = '\0'; }
            break;
        }
        case 'n':
            *type = 'z';
            *value = rabbit_strdup("null");
            break;
        default: {
            size_t n = (size_t)(q - p);
            *type = 'n';
            *value = (char *)malloc(n + 1U);
            if (*value != NULL) { memcpy(*value, p, n); (*value)[n] = '\0'; }
            break;
        }
    }
    return *value == NULL ? NULL : q;
}

/* ---- operações cruas na grade (sem flush) --------------------------------- */
static inline int rabbit_jg_is_sep(rabbit_array **row) {
    return row != NULL && row[0] != NULL && row[1] == NULL && strcmp(row[0], RABBIT_JSON_SEP) == 0;
}

static inline int rabbit_jg_is_prop(rabbit_array **row) {
    return row != NULL && row[0] != NULL && row[1] != NULL && row[2] != NULL && row[3] == NULL;
}

static inline void rabbit_jg_truncate(rabbit_array ****h, size_t n) {
    rabbit_array ***g = *h;
    size_t r;
    if (g == NULL) return;
    for (r = n; g[r] != NULL; ++r) rabbit_free_row(g[r]);
    g[n] = NULL;
}

static inline int rabbit_jg_insert(rabbit_array ****h, size_t at, const char *const *cells, size_t count) {
    rabbit_array ***g = *h;
    rabbit_array ***grown;
    rabbit_array **row;
    size_t rows = rabbit_grid_rows(g), i;
    if (at > rows) return -1;
    row = (rabbit_array **)calloc(count + 1U, sizeof(rabbit_array *));
    if (row == NULL) return -1;
    for (i = 0U; i < count; ++i) {
        row[i] = rabbit_strdup(cells[i]);
        if (row[i] == NULL) { rabbit_free_row(row); return -1; }
    }
    grown = (rabbit_array ***)realloc(g, sizeof(rabbit_array **) * (rows + 2U));
    if (grown == NULL) { rabbit_free_row(row); return -1; }
    g = grown;
    *h = g;
    for (i = rows; i > at; --i) g[i] = g[i - 1U];
    g[at] = row;
    g[rows + 1U] = NULL;
    return 0;
}

static inline void rabbit_jg_delete_row(rabbit_array ****h, size_t r) {
    rabbit_array ***g = *h;
    size_t rows = rabbit_grid_rows(g), i;
    if (r >= rows) return;
    rabbit_free_row(g[r]);
    for (i = r; i < rows; ++i) g[i] = g[i + 1U];
}

/* Acha o objeto k: start = 1ª linha, end = linha do separador. */
static inline int rabbit_jg_locate(rabbit_array ***g, size_t k, size_t *start, size_t *end) {
    size_t r, obj = 0U, first = 0U;
    if (g == NULL) return -1;
    for (r = 0U; g[r] != NULL; ++r) {
        if (rabbit_jg_is_sep(g[r])) {
            if (obj == k) { *start = first; *end = r; return 0; }
            ++obj;
            first = r + 1U;
        }
    }
    return -1;
}

static inline ssize_t rabbit_jg_find_prop(rabbit_array ***g, size_t start, size_t end, const char *key) {
    size_t r;
    for (r = start; r < end; ++r) {
        if (rabbit_jg_is_prop(g[r]) && strcmp(g[r][0], key) == 0) return (ssize_t)r;
    }
    return -1;
}

/* Lê um objeto de p e acrescenta ao fim. Em erro, desfaz tudo. */
static inline const char *rabbit_jg_add_object(rabbit_array ****h, const char *p) {
    size_t base = rabbit_grid_rows(*h);
    p = rabbit_js_ws(p);
    if (*p != '{') return NULL;
    p = rabbit_js_ws(p + 1);
    if (*p == '}') {
        ++p;
    } else {
        for (;;) {
            const char *kq, *q;
            char *key, *val = NULL;
            char type = 0, tbuf[2];
            const char *cells[3];
            size_t i;
            int dup = 0, rc;
            p = rabbit_js_ws(p);
            kq = rabbit_js_string(p);
            if (kq == NULL) goto fail;
            key = rabbit_js_decode(p, kq);
            if (key == NULL) goto fail;
            p = rabbit_js_ws(kq);
            if (*p != ':') { free(key); goto fail; }
            q = rabbit_jg_value(p + 1, &type, &val);
            if (q == NULL) { free(key); goto fail; }
            for (i = base; (*h)[i] != NULL; ++i) {
                if (strcmp((*h)[i][0], key) == 0) { dup = 1; break; }
            }
            tbuf[0] = type; tbuf[1] = '\0';
            cells[0] = key; cells[1] = tbuf; cells[2] = val;
            rc = dup ? -1 : rabbit_grid_append_row(h, cells, 3U);
            free(key);
            free(val);
            if (rc != 0) goto fail;
            p = rabbit_js_ws(q);
            if (*p == ',') { ++p; continue; }
            if (*p == '}') { ++p; break; }
            goto fail;
        }
    }
    {
        const char *sep[1] = { RABBIT_JSON_SEP };
        if (rabbit_grid_append_row(h, sep, 1U) != 0) goto fail;
    }
    return p;
fail:
    rabbit_jg_truncate(h, base);
    return NULL;
}

/* ---- API pública ----------------------------------------------------------- */
static inline size_t rabbit_json_count(rabbit_array ****h) {
    rabbit_array ***g;
    size_t r, n = 0U;
    if (h == NULL || *h == NULL) return 0U;
    g = *h;
    for (r = 0U; g[r] != NULL; ++r) if (rabbit_jg_is_sep(g[r])) ++n;
    return n;
}

/* Confere se a tabela inteira segue o layout JSON do DataRabbit. */
static inline int rabbit_json_validate(rabbit_array ****h) {
    rabbit_array ***g;
    size_t r;
    int pending = 0;
    if (h == NULL || *h == NULL) return 0;
    g = *h;
    for (r = 0U; g[r] != NULL; ++r) {
        rabbit_array **row = g[r];
        const char *v;
        const char *q;
        if (rabbit_jg_is_sep(row)) { pending = 0; continue; }
        if (!rabbit_jg_is_prop(row) || row[1][0] == '\0' || row[1][1] != '\0') return 0;
        v = row[2];
        switch (row[1][0]) {
            case 's': break;
            case 'n': q = rabbit_js_number(v); if (q == NULL || *q != '\0') return 0; break;
            case 'b': if (strcmp(v, "true") != 0 && strcmp(v, "false") != 0) return 0; break;
            case 'z': if (strcmp(v, "null") != 0) return 0; break;
            case 'j':
                if (*v != '{' && *v != '[') return 0;
                q = rabbit_js_value(v, 0);
                if (q == NULL || *q != '\0') return 0;
                break;
            default: return 0;
        }
        pending = 1;
    }
    return pending == 0;
}

/* Abre (ou cria) um banco e confere se ele é um banco JSON. */
static inline rabbit_array ****rabbit_json_open(const rabbit_array *name) {
    rabbit_array ****h = rabbit_open(name);
    if (h == NULL) return NULL;
    if (!rabbit_json_validate(h)) { rabbit_close(h); errno = EINVAL; return NULL; }
    return h;
}

/* Importa texto JSON: um objeto {...} ou um array de objetos [{...},{...}].
   Tudo ou nada: se algo for inválido, o banco fica como estava. */
static inline int rabbit_json_import_string(rabbit_array ****h, const char *text) {
    size_t base;
    const char *p;
    if (h == NULL || *h == NULL || text == NULL) return -1;
    if (!rabbit_utf8_validate(text)) return -1;
    base = rabbit_grid_rows(*h);
    p = rabbit_js_ws(text);
    if (*p == '[') {
        p = rabbit_js_ws(p + 1);
        if (*p == ']') {
            ++p;
        } else {
            for (;;) {
                p = rabbit_jg_add_object(h, p);
                if (p == NULL) goto fail;
                p = rabbit_js_ws(p);
                if (*p == ',') { ++p; continue; }
                if (*p == ']') { ++p; break; }
                goto fail;
            }
        }
    } else {
        p = rabbit_jg_add_object(h, p);
        if (p == NULL) goto fail;
    }
    p = rabbit_js_ws(p);
    if (*p != '\0') goto fail;
    return rabbit_commit(h);
fail:
    rabbit_jg_truncate(h, base);
    return -1;
}

/* Acrescenta UM objeto JSON ("{...}") como novo registro. */
static inline int rabbit_json_add(rabbit_array ****h, const char *json_object) {
    if (json_object == NULL || *rabbit_js_ws(json_object) != '{') return -1;
    return rabbit_json_import_string(h, json_object);
}

static inline int rabbit_json_import(rabbit_array ****h, const char *filename) {
    FILE *f;
    long size;
    char *buf;
    size_t n;
    const char *text;
    int rc;
    if (h == NULL || *h == NULL || filename == NULL) return -1;
    f = fopen(filename, "rb");
    if (f == NULL) return -1;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    buf = (char *)malloc((size_t)size + 1U);
    if (buf == NULL) { fclose(f); return -1; }
    n = fread(buf, 1U, (size_t)size, f);
    if (fclose(f) != 0 || n != (size_t)size) { free(buf); return -1; }
    buf[n] = '\0';
    if (strlen(buf) != n) { free(buf); return -1; }          /* NUL embutido */
    text = (n >= 3U && memcmp(buf, "\xEF\xBB\xBF", 3U) == 0) ? buf + 3 : buf;   /* BOM */
    rc = rabbit_json_import_string(h, text);
    free(buf);
    return rc;
}

static inline const rabbit_array *rabbit_json_get(rabbit_array ****h, size_t obj, const char *key) {
    size_t s, e;
    ssize_t r;
    if (h == NULL || *h == NULL || key == NULL) return NULL;
    if (rabbit_jg_locate(*h, obj, &s, &e) != 0) return NULL;
    r = rabbit_jg_find_prop(*h, s, e, key);
    return r < 0 ? NULL : (*h)[r][2];
}

/* 's' texto, 'n' número, 'b' booleano, 'z' null, 'j' objeto/array; 0 = não existe. */
static inline char rabbit_json_type(rabbit_array ****h, size_t obj, const char *key) {
    size_t s, e;
    ssize_t r;
    if (h == NULL || *h == NULL || key == NULL) return 0;
    if (rabbit_jg_locate(*h, obj, &s, &e) != 0) return 0;
    r = rabbit_jg_find_prop(*h, s, e, key);
    return r < 0 ? (char)0 : (*h)[r][1][0];
}

static inline int rabbit_jg_put(rabbit_array ****h, size_t obj, const char *key, char type, const char *value) {
    rabbit_array ***g;
    size_t s, e;
    ssize_t r;
    char t[2];
    if (h == NULL || *h == NULL || key == NULL || value == NULL) return -1;
    if (!rabbit_utf8_validate(key) || !rabbit_utf8_validate(value)) return -1;
    g = *h;
    if (rabbit_jg_locate(g, obj, &s, &e) != 0) return -1;
    t[0] = type; t[1] = '\0';
    r = rabbit_jg_find_prop(g, s, e, key);
    if (r >= 0) {
        char *nt = rabbit_strdup(t);
        char *nv = rabbit_strdup(value);
        if (nt == NULL || nv == NULL) { free(nt); free(nv); return -1; }
        free(g[r][1]);
        free(g[r][2]);
        g[r][1] = nt;
        g[r][2] = nv;
    } else {
        const char *cells[3];
        cells[0] = key; cells[1] = t; cells[2] = value;
        if (rabbit_jg_insert(h, e, cells, 3U) != 0) return -1;
    }
    return rabbit_commit(h);
}

static inline int rabbit_json_set_string(rabbit_array ****h, size_t obj, const char *key, const char *text) {
    return rabbit_jg_put(h, obj, key, 's', text);
}
static inline int rabbit_json_set_number(rabbit_array ****h, size_t obj, const char *key, const char *number_text) {
    const char *q;
    if (number_text == NULL) return -1;
    q = rabbit_js_number(number_text);
    if (q == NULL || *q != '\0') return -1;
    return rabbit_jg_put(h, obj, key, 'n', number_text);
}
static inline int rabbit_json_set_int(rabbit_array ****h, size_t obj, const char *key, long long value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", value);
    return rabbit_jg_put(h, obj, key, 'n', buf);
}
static inline int rabbit_json_set_double(rabbit_array ****h, size_t obj, const char *key, double value) {
    char buf[40];
    char *c;
    if (value != value || value - value != 0.0) return -1;     /* NaN / infinito */
    snprintf(buf, sizeof(buf), "%.17g", value);
    for (c = buf; *c != '\0'; ++c) if (*c == ',') *c = '.';    /* locale */
    return rabbit_jg_put(h, obj, key, 'n', buf);
}
static inline int rabbit_json_set_bool(rabbit_array ****h, size_t obj, const char *key, int value) {
    return rabbit_jg_put(h, obj, key, 'b', value ? "true" : "false");
}
static inline int rabbit_json_set_null(rabbit_array ****h, size_t obj, const char *key) {
    return rabbit_jg_put(h, obj, key, 'z', "null");
}
/* Qualquer valor JSON: "texto", 12, true, null, {...}, [...]. */
static inline int rabbit_json_set_json(rabbit_array ****h, size_t obj, const char *key, const char *json_value) {
    char type = 0, *val = NULL;
    const char *q;
    int rc;
    if (json_value == NULL || !rabbit_utf8_validate(json_value)) return -1;
    q = rabbit_jg_value(json_value, &type, &val);
    if (q == NULL) return -1;
    q = rabbit_js_ws(q);
    if (*q != '\0') { free(val); return -1; }
    rc = rabbit_jg_put(h, obj, key, type, val);
    free(val);
    return rc;
}

static inline int rabbit_json_remove(rabbit_array ****h, size_t obj, const char *key) {
    size_t s, e;
    ssize_t r;
    if (h == NULL || *h == NULL || key == NULL) return -1;
    if (rabbit_jg_locate(*h, obj, &s, &e) != 0) return -1;
    r = rabbit_jg_find_prop(*h, s, e, key);
    if (r < 0) return -1;
    rabbit_jg_delete_row(h, (size_t)r);
    return rabbit_commit(h);
}

static inline int rabbit_json_delete(rabbit_array ****h, size_t obj) {
    size_t s, e, i;
    if (h == NULL || *h == NULL) return -1;
    if (rabbit_jg_locate(*h, obj, &s, &e) != 0) return -1;
    for (i = s; i <= e; ++i) rabbit_jg_delete_row(h, s);
    return rabbit_commit(h);
}

/* Índice do primeiro objeto cuja propriedade key tem o texto value; -1 se não achar. */
static inline ssize_t rabbit_json_find(rabbit_array ****h, const char *key, const char *value) {
    rabbit_array ***g;
    size_t r, obj = 0U;
    if (h == NULL || *h == NULL || key == NULL || value == NULL) return -1;
    g = *h;
    for (r = 0U; g[r] != NULL; ++r) {
        if (rabbit_jg_is_sep(g[r])) { ++obj; continue; }
        if (rabbit_jg_is_prop(g[r]) && strcmp(g[r][0], key) == 0 && strcmp(g[r][2], value) == 0)
            return (ssize_t)obj;
    }
    return -1;
}

/* Gera o texto JSON (array de objetos). Libere com free(). pretty=1: indentado. */
static inline char *rabbit_json_to_string(rabbit_array ****h, int pretty) {
    rabbit_sb b = { NULL, 0U, 0U, 0 };
    rabbit_array ***g;
    size_t r, nobj = 0U, nprop = 0U;
    int open = 0;
    if (h == NULL || *h == NULL) return NULL;
    g = *h;
    rabbit_sb_putc(&b, '[');
    for (r = 0U; g[r] != NULL; ++r) {
        rabbit_array **row = g[r];
        int sep = rabbit_jg_is_sep(row);
        if (!sep && !rabbit_jg_is_prop(row)) continue;
        if (!open) {
            if (nobj > 0U) rabbit_sb_putc(&b, ',');
            if (pretty) rabbit_sb_str(&b, "\n  ");
            rabbit_sb_putc(&b, '{');
            open = 1;
            nprop = 0U;
        }
        if (sep) {
            if (pretty && nprop > 0U) rabbit_sb_str(&b, "\n  ");
            rabbit_sb_putc(&b, '}');
            open = 0;
            ++nobj;
            continue;
        }
        if (nprop > 0U) rabbit_sb_putc(&b, ',');
        if (pretty) rabbit_sb_str(&b, "\n    ");
        rabbit_sb_jstring(&b, row[0]);
        rabbit_sb_putc(&b, ':');
        if (pretty) rabbit_sb_putc(&b, ' ');
        if (row[1][0] == 's') rabbit_sb_jstring(&b, row[2]);
        else rabbit_sb_str(&b, row[2]);
        ++nprop;
    }
    if (open) {
        if (pretty && nprop > 0U) rabbit_sb_str(&b, "\n  ");
        rabbit_sb_putc(&b, '}');
        ++nobj;
    }
    if (pretty && nobj > 0U) rabbit_sb_putc(&b, '\n');
    rabbit_sb_putc(&b, ']');
    if (pretty) rabbit_sb_putc(&b, '\n');
    if (b.err) { free(b.data); return NULL; }
    return b.data;
}

/* Exporta para um arquivo .json de verdade (caminho usado como está). */
static inline int rabbit_json_export(rabbit_array ****h, const char *filename, int pretty) {
    char *text;
    FILE *f;
    size_t n;
    int ok;
    if (filename == NULL) return -1;
    text = rabbit_json_to_string(h, pretty);
    if (text == NULL) return -1;
    f = fopen(filename, "wb");
    if (f == NULL) { free(text); return -1; }
    n = strlen(text);
    ok = fwrite(text, 1U, n, f) == n && fflush(f) == 0 && !ferror(f);
    if (fclose(f) != 0) ok = 0;
    free(text);
    return ok ? 0 : -1;
}

static inline void rabbit_json_view(rabbit_array ****h) {
    char *text = rabbit_json_to_string(h, 1);
    if (text == NULL) { printf("(empty)\n"); return; }
    fputs(text, stdout);
    free(text);
}

/* ============================================================================
Aliases de compatibilidade
============================================================================ */
#define datarabbit_open_named        rabbit_open
#define datarabbit_close_named       rabbit_close
#define datarabbit_get               rabbit_get
#define datarabbit_set               rabbit_set
#define datarabbit_push              rabbit_push
#define datarabbit_find              rabbit_find
#define datarabbit_view_db           rabbit_view
#define datarabbit_save              rabbit_save
#define datarabbit_rename            rabbit_rename
#define datarabbit_grid              rabbit_grid
#define datarabbit_grid_rows         rabbit_grid_rows
#define datarabbit_grid_cols         rabbit_grid_cols
#define datarabbit_grid_get_value    rabbit_grid_get_value
#define datarabbit_grid_set_value    rabbit_grid_set_value
#define datarabbit_grid_append_row   rabbit_grid_append_row
#define datarabbit_grid_find_row     rabbit_grid_find_row
#define datarabbit_free_grid         rabbit_free_grid
#define datarabbit_alloc_grid        rabbit_alloc_grid
#define datarabbit_read_file         rabbit_read_file
#define datarabbit_save_grid_to_file rabbit_save_grid_to_file
#define datarabbit_load_path         rabbit_load_path
#define datarabbit_load_named        rabbit_load_named
#define datarabbit_make_filename     rabbit_make_filename
#define datarabbit_strdup            rabbit_strdup
#define datarabbit_utf8_validate     rabbit_utf8_validate
#define datarabbit_utf8_next         rabbit_utf8_next
#define datarabbit_utf8_length       rabbit_utf8_length
#define datarabbit_jit_ready         rabbit_jit_ready
#define datarabbit_refresh_size      rabbit_refresh_size

/* Novos aliases */
#define datarabbit_delete_row        rabbit_delete_row
#define datarabbit_delete_col        rabbit_delete_col
#define datarabbit_insert_row        rabbit_insert_row
#define datarabbit_count             rabbit_count
#define datarabbit_find_all          rabbit_find_all
#define datarabbit_filter            rabbit_filter
#define datarabbit_sort              rabbit_sort
#define datarabbit_export_csv        rabbit_export_csv

/* Aliases antigos das funções dinâmicas */
#define datarabbit_array_save        rabbit_array_dynamic_save
#define datarabbit_array_load        rabbit_array_dynamic_load
#define datarabbit_array_save_file   rabbit_array_dynamic_save_file
#define datarabbit_array_load_file   rabbit_array_dynamic_load_file
#define datarabbit_array_view        rabbit_array_dynamic_view
#define rabbit_array_save            rabbit_array_dynamic_save
#define rabbit_array_load            rabbit_array_dynamic_load
#define rabbit_array_save_file       rabbit_array_dynamic_save_file
#define rabbit_array_load_file       rabbit_array_dynamic_load_file
#define rabbit_array_view            rabbit_array_dynamic_view

#ifdef __cplusplus
}
#endif

#endif /* DATARABBIT_H */
