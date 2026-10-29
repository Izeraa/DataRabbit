#ifndef DATARABBIT_H
#define DATARABBIT_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ***rows;
    size_t row_count;
    size_t col_count;
} datarabbit_table;

typedef struct {
    datarabbit_table table;
    char *name;
} datarabbit_database;

typedef struct {
    datarabbit_database *db;
    size_t row;
    size_t col;
} datarabbit_cell;

#define DATARABBIT_CELL(db, row, col) ((datarabbit_cell){ .db = (db), .row = (row), .col = (col) })

static inline int datarabbit_table_add_column(datarabbit_table *table);
static inline int datarabbit_table_set_row(datarabbit_table *table, size_t row_index, const char *const *values, size_t value_count);
static inline int datarabbit_table_get_row(const datarabbit_table *table, size_t row_index, const char ***out_values, size_t *out_count);
static inline ssize_t datarabbit_table_find_row_by_value(const datarabbit_table *table, size_t col_index, const char *value);
static inline int datarabbit_table_update_value(datarabbit_table *table, size_t row_index, size_t col_index, const char *value);
static inline int datarabbit_table_remove_row(datarabbit_table *table, size_t row_index);
static inline int datarabbit_table_save_file(const datarabbit_table *table, const char *path);
static inline int datarabbit_table_load_file(datarabbit_table *table, const char *path);

static inline char *datarabbit_make_filename(const char *name) {
    const char *base;
    const char *prefix = "datarabbit+";
    const char *extension = ".txt";
    size_t filename_length;
    char *filename;

    if (name == NULL || name[0] == '\0') {
        base = "database";
    } else {
        base = name;
    }

    if (strncmp(base, prefix, strlen(prefix)) == 0) {
        base = base + strlen(prefix);
    }

    if (strstr(base, ".txt") == NULL) {
        filename_length = strlen(prefix) + strlen(base) + strlen(extension) + 1U;
        filename = (char *)malloc(filename_length);
        if (filename == NULL) {
            return NULL;
        }
        snprintf(filename, filename_length, "%s%s%s", prefix, base, extension);
        return filename;
    }

    filename_length = strlen(prefix) + strlen(base) + 1U;
    filename = (char *)malloc(filename_length);
    if (filename == NULL) {
        return NULL;
    }
    snprintf(filename, filename_length, "%s%s", prefix, base);
    return filename;
}

static inline char *datarabbit_strdup(const char *value) {
    size_t length;
    char *copy;

    if (value == NULL) {
        value = "";
    }

    length = strlen(value);
    copy = (char *)malloc(length + 1U);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, value, length + 1U);
    return copy;
}

static inline void datarabbit_table_init(datarabbit_table *table) {
    if (table == NULL) {
        return;
    }

    table->rows = NULL;
    table->row_count = 0U;
    table->col_count = 0U;
}

static inline void datarabbit_table_clear(datarabbit_table *table) {
    size_t row;
    size_t col;

    if (table == NULL || table->rows == NULL) {
        return;
    }

    for (row = 0U; row < table->row_count; ++row) {
        if (table->rows[row] == NULL) {
            continue;
        }

        for (col = 0U; col < table->col_count; ++col) {
            free(table->rows[row][col]);
            table->rows[row][col] = NULL;
        }

        free(table->rows[row]);
        table->rows[row] = NULL;
    }

    free(table->rows);
    table->rows = NULL;
    table->row_count = 0U;
    table->col_count = 0U;
}

static inline void datarabbit_table_free(datarabbit_table *table) {
    datarabbit_table_clear(table);
}

static inline int datarabbit_table_append_row(datarabbit_table *table) {
    char ***new_rows;
    char **new_row = NULL;
    size_t index;

    if (table == NULL) {
        return -1;
    }

    index = table->row_count;
    new_rows = (char ***)realloc(table->rows, sizeof(char **) * (index + 1U));
    if (new_rows == NULL) {
        return -1;
    }

    table->rows = new_rows;
    table->rows[index] = NULL;

    if (table->col_count > 0U) {
        new_row = (char **)calloc(table->col_count, sizeof(char *));
        if (new_row == NULL) {
            return -1;
        }
        table->rows[index] = new_row;
    }

    table->row_count += 1U;
    return 0;
}

static inline int datarabbit_table_expand_columns(datarabbit_table *table, size_t min_columns) {
    size_t row;
    size_t col;
    size_t target_columns;

    if (table == NULL) {
        return -1;
    }

    target_columns = (table->col_count > min_columns) ? table->col_count : min_columns;

    if (table->row_count == 0U && target_columns == 0U) {
        table->col_count = 0U;
        return 0;
    }

    for (row = 0U; row < table->row_count; ++row) {
        char **new_row = (char **)realloc(table->rows[row], sizeof(char *) * target_columns);
        if (new_row == NULL) {
            return -1;
        }

        table->rows[row] = new_row;
        for (col = table->col_count; col < target_columns; ++col) {
            table->rows[row][col] = NULL;
        }
    }

    table->col_count = target_columns;
    return 0;
}

static inline int datarabbit_table_ensure_cell(datarabbit_table *table, size_t row_index, size_t col_index) {
    size_t row;

    if (table == NULL) {
        return -1;
    }

    while (table->row_count <= row_index) {
        if (datarabbit_table_append_row(table) != 0) {
            return -1;
        }
    }

    if (table->col_count <= col_index) {
        if (datarabbit_table_expand_columns(table, col_index + 1U) != 0) {
            return -1;
        }
    }

    if (table->rows[row_index] == NULL) {
        table->rows[row_index] = (char **)calloc(table->col_count, sizeof(char *));
        if (table->rows[row_index] == NULL) {
            return -1;
        }
    }

    for (row = 0U; row < table->row_count; ++row) {
        if (table->rows[row] == NULL && table->col_count > 0U) {
            table->rows[row] = (char **)calloc(table->col_count, sizeof(char *));
            if (table->rows[row] == NULL) {
                return -1;
            }
        }
    }

    return 0;
}

static inline int datarabbit_table_set_value(datarabbit_table *table, size_t row_index, size_t col_index, const char *value) {
    char *copy;

    if (table == NULL) {
        return -1;
    }

    if (datarabbit_table_ensure_cell(table, row_index, col_index) != 0) {
        return -1;
    }

    copy = datarabbit_strdup(value);
    if (copy == NULL) {
        return -1;
    }

    free(table->rows[row_index][col_index]);
    table->rows[row_index][col_index] = copy;
    return 0;
}

static inline const char *datarabbit_table_get_value(const datarabbit_table *table, size_t row_index, size_t col_index) {
    if (table == NULL || table->rows == NULL || row_index >= table->row_count || col_index >= table->col_count) {
        return NULL;
    }

    if (table->rows[row_index] == NULL) {
        return NULL;
    }

    return table->rows[row_index][col_index];
}

static inline int datarabbit_table_add_column(datarabbit_table *table) {
    size_t row;

    if (table == NULL) {
        return -1;
    }

    if (datarabbit_table_expand_columns(table, table->col_count + 1U) != 0) {
        return -1;
    }

    for (row = 0U; row < table->row_count; ++row) {
        if (table->rows[row] == NULL) {
            table->rows[row] = (char **)calloc(table->col_count, sizeof(char *));
            if (table->rows[row] == NULL) {
                return -1;
            }
        }
    }

    return 0;
}

static inline int datarabbit_table_set_row(datarabbit_table *table, size_t row_index, const char *const *values, size_t value_count) {
    size_t i;

    if (table == NULL || values == NULL) {
        return -1;
    }

    if (datarabbit_table_ensure_cell(table, row_index, value_count > 0U ? value_count - 1U : 0U) != 0) {
        return -1;
    }

    if (value_count > table->col_count) {
        if (datarabbit_table_expand_columns(table, value_count) != 0) {
            return -1;
        }
    }

    for (i = 0U; i < value_count; ++i) {
        if (datarabbit_table_set_value(table, row_index, i, values[i]) != 0) {
            return -1;
        }
    }

    return 0;
}

static inline int datarabbit_table_get_row(const datarabbit_table *table, size_t row_index, const char ***out_values, size_t *out_count) {
    if (table == NULL || out_values == NULL || out_count == NULL) {
        return -1;
    }

    if (row_index >= table->row_count || table->rows == NULL || table->rows[row_index] == NULL) {
        return -1;
    }

    *out_values = (const char **)table->rows[row_index];
    *out_count = table->col_count;
    return 0;
}

static inline ssize_t datarabbit_table_find_row_by_value(const datarabbit_table *table, size_t col_index, const char *value) {
    size_t row;

    if (table == NULL || value == NULL || table->rows == NULL) {
        return -1;
    }

    if (col_index >= table->col_count) {
        return -1;
    }

    for (row = 0U; row < table->row_count; ++row) {
        const char *cell = datarabbit_table_get_value(table, row, col_index);
        if (cell != NULL && strcmp(cell, value) == 0) {
            return (ssize_t)row;
        }
    }

    return -1;
}

static inline int datarabbit_table_update_value(datarabbit_table *table, size_t row_index, size_t col_index, const char *value) {
    return datarabbit_table_set_value(table, row_index, col_index, value);
}

static inline int datarabbit_table_add_row(datarabbit_table *table) {
    return datarabbit_table_append_row(table);
}

static inline int datarabbit_database_init(datarabbit_database *db, const char *name) {
    if (db == NULL) {
        return -1;
    }

    datarabbit_table_init(&db->table);
    db->name = datarabbit_make_filename(name);
    if (db->name == NULL) {
        return -1;
    }

    return 0;
}

static inline int datarabbit_database_create_file(const char *name) {
    char *path;
    FILE *file;
    int result;

    if (name == NULL) {
        return -1;
    }

    path = datarabbit_make_filename(name);
    if (path == NULL) {
        return -1;
    }

    file = fopen(path, "ab");
    result = (file == NULL) ? -1 : 0;
    if (file != NULL) {
        fclose(file);
    }

    free(path);
    return result;
}

static inline void datarabbit_database_free(datarabbit_database *db) {
    if (db == NULL) {
        return;
    }

    datarabbit_table_free(&db->table);
    free(db->name);
    db->name = NULL;
}

static inline int datarabbit_database_save(const datarabbit_database *db) {
    if (db == NULL || db->name == NULL) {
        return -1;
    }

    return datarabbit_table_save_file(&db->table, db->name);
}

static inline int datarabbit_database_load(datarabbit_database *db);

static inline int datarabbit_database_select_value(const datarabbit_database *db, size_t row_index, size_t col_index, const char **out_value) {
    if (db == NULL || out_value == NULL) {
        return -1;
    }

    *out_value = datarabbit_table_get_value(&db->table, row_index, col_index);
    return (*out_value == NULL) ? -1 : 0;
}

static inline int datarabbit_database_select_row(const datarabbit_database *db, size_t row_index, const char ***out_values, size_t *out_count) {
    if (db == NULL || out_values == NULL || out_count == NULL) {
        return -1;
    }

    return datarabbit_table_get_row(&db->table, row_index, out_values, out_count);
}

static inline int datarabbit_database_where(const datarabbit_database *db, size_t col_index, const char *value, size_t *rows, size_t max_rows, size_t *out_count) {
    size_t match_count = 0U;
    size_t row;

    if (db == NULL || value == NULL) {
        return -1;
    }

    if (out_count != NULL) {
        *out_count = 0U;
    }

    if (col_index >= db->table.col_count) {
        return -1;
    }

    for (row = 0U; row < db->table.row_count; ++row) {
        const char *cell = datarabbit_table_get_value(&db->table, row, col_index);
        if (cell != NULL && strcmp(cell, value) == 0) {
            if (rows != NULL && match_count < max_rows) {
                rows[match_count] = row;
            }
            ++match_count;
        }
    }

    if (out_count != NULL) {
        *out_count = match_count;
    }

    return (match_count == 0U) ? -1 : 0;
}

static inline int datarabbit_database_open(datarabbit_database *db, const char *name) {
    if (db == NULL) {
        return -1;
    }

    if (datarabbit_database_init(db, name) != 0) {
        return -1;
    }

    if (access(db->name, F_OK) == 0) {
        return datarabbit_database_load(db);
    }

    return 0;
}

static inline int datarabbit_database_load(datarabbit_database *db) {
    if (db == NULL || db->name == NULL) {
        return -1;
    }

    return datarabbit_table_load_file(&db->table, db->name);
}

static inline int datarabbit_database_insert(datarabbit_database *db, const char *const *values, size_t count) {
    int result;

    if (db == NULL || values == NULL) {
        return -1;
    }

    if (datarabbit_table_add_row(&db->table) != 0) {
        return -1;
    }

    result = datarabbit_table_set_row(&db->table, db->table.row_count - 1U, values, count);
    if (result != 0) {
        return result;
    }

    return datarabbit_database_save(db);
}

static inline int datarabbit_database_insert_row(datarabbit_database *db, const char *const *values, size_t count) {
    return datarabbit_database_insert(db, values, count);
}

static inline int datarabbit_database_update_cell(datarabbit_database *db, size_t row_index, size_t col_index, const char *value) {
    int result;

    if (db == NULL) {
        return -1;
    }

    result = datarabbit_table_update_value(&db->table, row_index, col_index, value);
    if (result != 0) {
        return result;
    }

    return datarabbit_database_save(db);
}

static inline int datarabbit_database_update(datarabbit_database *db, size_t row_index, size_t col_index, const char *value) {
    return datarabbit_database_update_cell(db, row_index, col_index, value);
}

static inline int datarabbit_database_delete_row(datarabbit_database *db, size_t row_index) {
    int result;

    if (db == NULL) {
        return -1;
    }

    result = datarabbit_table_remove_row(&db->table, row_index);
    if (result != 0) {
        return result;
    }

    return datarabbit_database_save(db);
}

static inline int datarabbit_database_delete_value(datarabbit_database *db, size_t row_index, size_t col_index) {
    if (db == NULL || row_index >= db->table.row_count || col_index >= db->table.col_count) {
        return -1;
    }

    if (db->table.rows[row_index] == NULL) {
        return -1;
    }

    free(db->table.rows[row_index][col_index]);
    db->table.rows[row_index][col_index] = NULL;
    return datarabbit_database_save(db);
}

static inline int datarabbit_database_delete(datarabbit_database *db, size_t row_index, size_t col_index) {
    if (db == NULL) {
        return -1;
    }

    if (row_index < db->table.row_count && col_index < db->table.col_count) {
        return datarabbit_database_delete_value(db, row_index, col_index);
    }
    return datarabbit_database_delete_row(db, row_index);
}

static inline ssize_t datarabbit_database_find_row(datarabbit_database *db, size_t col_index, const char *value) {
    if (db == NULL || value == NULL) {
        return -1;
    }

    return datarabbit_table_find_row_by_value(&db->table, col_index, value);
}

static inline int datarabbit_database_add_column(datarabbit_database *db) {
    if (db == NULL) {
        return -1;
    }

    return datarabbit_table_add_column(&db->table);
}

static inline int datarabbit_database_set_row(datarabbit_database *db, size_t row_index, const char *const *values, size_t count) {
    int result;

    if (db == NULL || values == NULL) {
        return -1;
    }

    result = datarabbit_table_set_row(&db->table, row_index, values, count);
    if (result != 0) {
        return result;
    }

    return datarabbit_database_save(db);
}

static inline int datarabbit_cell_load(datarabbit_cell *cell) {
    if (cell == NULL || cell->db == NULL) {
        return -1;
    }

    return datarabbit_database_load(cell->db);
}

static inline int datarabbit_cell_remove(datarabbit_cell *cell) {
    if (cell == NULL || cell->db == NULL) {
        return -1;
    }

    if (cell->row >= cell->db->table.row_count || cell->col >= cell->db->table.col_count) {
        return -1;
    }

    if (cell->db->table.rows[cell->row] == NULL) {
        return -1;
    }

    free(cell->db->table.rows[cell->row][cell->col]);
    cell->db->table.rows[cell->row][cell->col] = NULL;
    return datarabbit_database_save(cell->db);
}

static inline int datarabbit_cell_value(datarabbit_cell *cell, const char **out_value) {
    if (cell == NULL || cell->db == NULL || out_value == NULL) {
        return -1;
    }

    if (cell->row >= cell->db->table.row_count || cell->col >= cell->db->table.col_count) {
        *out_value = NULL;
        return -1;
    }

    *out_value = datarabbit_table_get_value(&cell->db->table, cell->row, cell->col);
    return (*out_value == NULL) ? -1 : 0;
}

static inline int datarabbit_database_find_value(datarabbit_database *db, size_t row_index, size_t col_index, const char **out_value) {
    if (db == NULL || out_value == NULL) {
        return -1;
    }

    *out_value = datarabbit_table_get_value(&db->table, row_index, col_index);
    return (*out_value == NULL) ? -1 : 0;
}

static inline int datarabbit_database_load_from_file(datarabbit_database *db, const char *path) {
    if (db == NULL || path == NULL) {
        return -1;
    }

    return datarabbit_table_load_file(&db->table, path);
}

static inline int datarabbit_database_save_to_file(const datarabbit_database *db, const char *path) {
    if (db == NULL || path == NULL) {
        return -1;
    }

    return datarabbit_table_save_file(&db->table, path);
}

static inline int datarabbit_table_insert_row(datarabbit_table *table, size_t row_index) {
    size_t row;
    char ***new_rows;

    if (table == NULL || row_index > table->row_count) {
        return -1;
    }

    new_rows = (char ***)realloc(table->rows, sizeof(char **) * (table->row_count + 1U));
    if (new_rows == NULL) {
        return -1;
    }

    table->rows = new_rows;

    for (row = table->row_count; row > row_index; --row) {
        table->rows[row] = table->rows[row - 1U];
    }

    table->rows[row_index] = NULL;
    if (table->col_count > 0U) {
        table->rows[row_index] = (char **)calloc(table->col_count, sizeof(char *));
        if (table->rows[row_index] == NULL) {
            return -1;
        }
    }

    table->row_count += 1U;
    return 0;
}

static inline int datarabbit_table_remove_row(datarabbit_table *table, size_t row_index) {
    size_t row;
    size_t col;

    if (table == NULL || table->rows == NULL || row_index >= table->row_count) {
        return -1;
    }

    if (table->rows[row_index] != NULL) {
        for (col = 0U; col < table->col_count; ++col) {
            free(table->rows[row_index][col]);
            table->rows[row_index][col] = NULL;
        }
        free(table->rows[row_index]);
    }

    for (row = row_index; row + 1U < table->row_count; ++row) {
        table->rows[row] = table->rows[row + 1U];
    }

    table->rows[table->row_count - 1U] = NULL;
    table->row_count -= 1U;

    if (table->row_count == 0U) {
        free(table->rows);
        table->rows = NULL;
        table->col_count = 0U;
    }

    return 0;
}

static inline int datarabbit_table_save_file(const datarabbit_table *table, const char *path) {
    FILE *file;
    size_t row;
    size_t col;

    if (table == NULL || path == NULL) {
        return -1;
    }

    file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }

    for (row = 0U; row < table->row_count; ++row) {
        if (table->rows == NULL || table->rows[row] == NULL) {
            fprintf(file, "\n");
            continue;
        }

        for (col = 0U; col < table->col_count; ++col) {
            const char *value = table->rows[row][col];
            if (value == NULL) {
                value = "";
            }
            fprintf(file, "[%s]", value);
        }

        fprintf(file, "\n");
    }

    fclose(file);
    return 0;
}

static inline int datarabbit_table_load_file(datarabbit_table *table, const char *path) {
    FILE *file;
    char *line = NULL;
    size_t capacity = 0U;
    ssize_t read;
    size_t row_index = 0U;
    size_t col_index = 0U;

    if (table == NULL || path == NULL) {
        return -1;
    }

    datarabbit_table_clear(table);

    file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }

    while ((read = getline(&line, &capacity, file)) != -1) {
        char *cursor = line;
        (void)read;

        while (*cursor != '\0') {
            if (*cursor == '\r' || *cursor == '\n') {
                *cursor = '\0';
                break;
            }

            if (*cursor == '[') {
                char *value_start = cursor + 1;
                char *value_end = strchr(value_start, ']');
                char *value;
                size_t length;

                if (value_end == NULL) {
                    free(line);
                    fclose(file);
                    return -1;
                }

                length = (size_t)(value_end - value_start);
                value = (char *)malloc(length + 1U);
                if (value == NULL) {
                    free(line);
                    fclose(file);
                    return -1;
                }

                memcpy(value, value_start, length);
                value[length] = '\0';

                if (datarabbit_table_ensure_cell(table, row_index, col_index) != 0) {
                    free(value);
                    free(line);
                    fclose(file);
                    return -1;
                }

                if (datarabbit_table_set_value(table, row_index, col_index, value) != 0) {
                    free(value);
                    free(line);
                    fclose(file);
                    return -1;
                }

                free(value);
                cursor = value_end + 1;
                col_index += 1U;
                continue;
            }

            cursor += 1;
        }

        if (row_index > 0U || col_index > 0U) {
            row_index += 1U;
        }
        col_index = 0U;
    }

    free(line);
    fclose(file);
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif
