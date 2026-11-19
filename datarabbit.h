/*
DataRabbit — biblioteca C header-only para bancos de dados em texto (.txt).
FORMATO DO ARQUIVO
Uma linha de texto por linha da tabela. Cada célula é gravada como
[♜ valor ♜]. Dentro do valor são escapados: \  [  ]  \n  \r  \t  e
\xHH (demais caracteres de controle). Linha em branco = linha vazia
(é preservada, para que os índices de linha não mudem ao recarregar).
Arquivos são sempre gravados como "datarabbit+<nome>.txt".
MODELO DE DADOS (todos os vetores terminam em NULL)
rabbit_array *      = string (célula, UTF-8)
rabbit_array **     = linha  (vetor de células)
rabbit_array ***    = tabela (vetor de linhas)
rabbit_array ****   = handle (ponteiro para a tabela; permite realloc das linhas)
USO BÁSICO
rabbit_array ****clientes = rabbit_open("clientes");  // abre "datarabbit+clientes.txt"
rabbit_set(clientes, 0, 0, "Ana");
const char *row[] = { "Bia", "30" };
rabbit_push(clientes, row, 2);
rabbit_view(clientes);
rabbit_save(clientes, "clientes");
rabbit_close(clientes);
OBSERVAÇÕES
Retorno: funções int devolvem 0 em sucesso e -1 em erro.
O estado (bancos abertos, rabbit, arquivo JIT) é `static`: cada unidade
de tradução que incluir este header terá a sua cópia. Use um único .c.
Não é thread-safe.
Defina RABBIT_JIT_AUTOFLUSH 0 antes do include para NÃO gravar o
arquivo temporário a cada set/push (use rabbit_jit_ready(h) quando
precisar dele atualizado).
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

#ifdef __cplusplus
extern "C" {
#endif

typedef char rabbit_array;

#ifndef RABBIT_JIT_AUTOFLUSH
#define RABBIT_JIT_AUTOFLUSH 1
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

static inline rabbit_array *rabbit_make_filename(const rabbit_array *name) {
    static const char prefix[] = "datarabbit+";
    static const char extension[] = ".txt";
    const size_t plen = sizeof(prefix) - 1U;
    const size_t elen = sizeof(extension) - 1U;
    const rabbit_array *base;
    size_t blen, total;
    int has_extension;
    rabbit_array *filename;
    
    base = (name == NULL || name[0] == '\0') ? "database" : name;
    if (strncmp(base, prefix, plen) == 0) base += plen;
    if (base[0] == '\0') base = "database";
    blen = strlen(base);
    if (blen > SIZE_MAX - plen - elen - 1U) return NULL;
    has_extension = blen >= elen && strcmp(base + blen - elen, extension) == 0;
    total = plen + blen + (has_extension ? 0U : elen) + 1U;
    filename = (rabbit_array *)malloc(total);
    if (filename == NULL) return NULL;
    snprintf(filename, total, "%s%s%s", prefix, base, has_extension ? "" : extension);
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

#endif
#endif

/* ============================================================================
Grid (rabbit_array ***)
============================================================================ */

static inline size_t rabbit_grid_rows(rabbit_array ***g) {
    size_t r = 0U;
    if (g == NULL) return 0U;
    while (g[r] != NULL) ++r;
    return r;
}

static inline size_t rabbit_grid_cols(rabbit_array **g, size_t row) {
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
    if (r > (SIZE_MAX / sizeof(rabbit_array **)) - 2U || c > (SIZE_MAX / sizeof(rabbit_array *)) - 2U) return -1;
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

static inline int rabbit_save_grid_to_file(rabbit_array ***g, const rabbit_array *path) {
    static const char suffix[] = ".tmp.XXXXXX";
    FILE *file;
    int fd, result = 0;
    size_t r, c, path_length, size;
    rabbit_array *temporary_path;
    if (path == NULL) return -1;
    path_length = strlen(path);
    if (path_length > SIZE_MAX - sizeof(suffix)) return -1;
    size = path_length + sizeof(suffix);
    temporary_path = (rabbit_array *)malloc(size);
    if (temporary_path == NULL) return -1;
    snprintf(temporary_path, size, "%s%s", path, suffix);
    fd = mkstemp(temporary_path);
    if (fd < 0) { free(temporary_path); return -1; }
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
    if (result != 0) unlink(temporary_path);
    free(temporary_path);
    return result;
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
#define RABBIT_JIT_TEMP_FILE "datarabbit+JitTempR.txt"

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
    FILE *f = fopen(RABBIT_JIT_TEMP_FILE, "ab");
    if (f == NULL) return -1;
    return fclose(f) == 0 ? 0 : -1;
}

static inline void rabbit_jit_temp_close(void) {
    remove(RABBIT_JIT_TEMP_FILE);
    rabbit_jit_dirty = 0;
}

static inline int rabbit_jit_workspace_flush(rabbit_array ****h) {
    if (h == NULL || *h == NULL) return -1;
    if (rabbit_save_grid_to_file(*h, RABBIT_JIT_TEMP_FILE) != 0) return -1;
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

/* ============================================================================
Abrir / fechar bancos
============================================================================ */

static inline rabbit_array ***rabbit_load_path(const rabbit_array *path) {
    FILE *probe;
    if (path == NULL) return NULL;
    probe = fopen(path, "r");
    if (probe == NULL) {
        if (errno != ENOENT) return NULL;
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

static inline rabbit_array ****rabbit_open(const rabbit_array *name) {
    rabbit_slot *slot = NULL;
    rabbit_array *path;
    rabbit_array ***g;
    size_t i;
    if (name == NULL || name[0] == '\0') return NULL;
    path = rabbit_make_filename(name);
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
Operações de banco
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
    rabbit_jit_mark_dirty();
#if RABBIT_JIT_AUTOFLUSH
    if (rabbit_jit_workspace_flush(h) != 0) return -1;
#endif
    rabbit_refresh_size(h);
    return 0;
}

static inline int rabbit_push(rabbit_array ****h, const rabbit_array *const *values, size_t count) {
    if (h == NULL || *h == NULL) return -1;
    if (rabbit_grid_append_row(h, values, count) != 0) return -1;
    rabbit_jit_mark_dirty();
#if RABBIT_JIT_AUTOFLUSH
    if (rabbit_jit_workspace_flush(h) != 0) return -1;
#endif
    rabbit_refresh_size(h);
    return 0;
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

#ifdef __cplusplus
}
#endif

#endif /* DATARABBIT_H */
