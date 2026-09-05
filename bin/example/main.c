/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */
/* The C embedding example: feed a recorded byte stream into a
 * shitty_vt, then print what an embedder can read back - the grid as
 * UTF-8 text, the cursor, the mode bits, the terminal's replies and
 * the scrollback position.
 *
 * Usage: example [columns rows save_lines] [stream-file]
 * With no file the stream is read from stdin.
 *
 * An optional input script (the tenth argument) runs after the stream,
 * one command per line, driving the input entry points; whatever they
 * encode shows up on the replies line. Numeric fields use the pinned
 * SHITTY_VT_* values:
 *   key KEY ACTION MODS LAYOUT BASE SHIFTED
 *   text CODEPOINT MODS
 *   flush
 *   button BUTTON PRESSED COL ROW MODS TIME
 *   motion COL ROW MODS
 *   wheel DX DY COL ROW MODS
 *   paste HEXBYTES
 *   feed HEXBYTES
 *   focus 0|1
 *   preedit HEXBYTES BEGIN END
 *   resize COLUMNS ROWS */

#include "lib/embed/shitty_vt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Enough for one grapheme cluster rendered as UTF-8. */
#define CELL_TEXT_CAP 64

struct grid {
    char* text; /* rows * columns slots of CELL_TEXT_CAP bytes */
    uint16_t columns;
    uint16_t rows;
};

static size_t utf8_encode(uint32_t codepoint, char* out) {
    if (codepoint < 0x80) {
        out[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800) {
        out[0] = (char)(0xc0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3f));
        return 2;
    }
    if (codepoint < 0x10000) {
        out[0] = (char)(0xe0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        out[2] = (char)(0x80 | (codepoint & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (codepoint >> 18));
    out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
    out[3] = (char)(0x80 | (codepoint & 0x3f));
    return 4;
}

static void collect_cell(void* user, uint16_t row, uint16_t column, const shitty_vt_cell* cell) {
    struct grid* grid = user;
    char* slot;
    /* The stream may have resized the terminal past our snapshot. */
    if (row >= grid->rows || column >= grid->columns) {
        return;
    }
    slot = grid->text + ((size_t)row * grid->columns + column) * CELL_TEXT_CAP;
    size_t used = 0;
    size_t index;
    for (index = 0; index < cell->grapheme_len && used + 4 < CELL_TEXT_CAP; ++index) {
        used += utf8_encode(cell->grapheme[index], slot + used);
    }
    if (used == 0) {
        slot[used++] = ' ';
    }
    slot[used] = '\0';
}

/* The preview cells with the span they cover: the callback reports the
 * columns they are drawn at, which is not where the row starts. */
struct preview_span {
    struct grid* grid;
    uint16_t row;
    uint16_t column;
    uint16_t count;
};

static void collect_preview(void* user, uint16_t row, uint16_t column, const shitty_vt_cell* cell) {
    struct preview_span* const span = user;
    if (column >= span->grid->columns) {
        return;
    }
    span->row = row;
    if (column < span->column) {
        span->column = column;
    }
    ++span->count;
    collect_cell(span->grid, 0, column, cell);
}

/* One color's provenance, as the header packs it. */
static const char* color_source_name(uint16_t source, char* buffer, size_t size) {
    switch (SHITTY_VT_COLOR_KIND(source)) {
        case SHITTY_VT_COLOR_DEFAULT_FOREGROUND:
            return "default_fg";
        case SHITTY_VT_COLOR_DEFAULT_BACKGROUND:
            return "default_bg";
        case SHITTY_VT_COLOR_INDEXED:
            snprintf(buffer, size, "indexed:%u", (unsigned)SHITTY_VT_COLOR_INDEX(source));
            return buffer;
        default:
            return "direct";
    }
}

/* Every drawn cell of the top row as column=fg/bg/underline. */
static void collect_colors(void* user, uint16_t row, uint16_t column, const shitty_vt_cell* cell) {
    char foreground[16];
    char background[16];
    char underline[16];
    (void)user;
    if (row != 0 || cell->grapheme_len == 0) {
        return;
    }
    printf(" %u=%s/%s/%s", column,
           color_source_name(cell->foreground_source, foreground, sizeof(foreground)),
           color_source_name(cell->background_source, background, sizeof(background)),
           color_source_name(cell->underline_source, underline, sizeof(underline)));
}

static void on_title(void* user, const uint8_t* title, size_t len) {
    (void)user;
    printf("title: %.*s\n", (int)len, (const char*)title);
}

static void on_bell(void* user) {
    (void)user;
    printf("bell\n");
}

static void on_clipboard(void* user, int clipboard, const uint8_t* bytes, size_t len) {
    (void)user;
    printf("clipboard %d: %.*s\n", clipboard, (int)len, (const char*)bytes);
}

static void on_open_uri(void* user, const uint8_t* uri, size_t len) {
    (void)user;
    printf("open-uri: %.*s\n", (int)len, (const char*)uri);
}

/* Collects one row's text, ignoring the row number the callback repeats. */
static void collect_row(void* user, uint16_t row, uint16_t column, const shitty_vt_cell* cell) {
    struct grid* const target = (struct grid*)user;
    (void)row;
    collect_cell(user, 0, column, cell);
    (void)target;
}

static size_t parse_hex(const char* text, uint8_t* out, size_t cap) {
    size_t used = 0;
    while (used < cap && text[0] != '\0' && text[1] != '\0') {
        unsigned value;
        if (sscanf(text, "%2x", &value) != 1) {
            break;
        }
        out[used++] = (uint8_t)value;
        text += 2;
    }
    return used;
}

static void run_input_line(shitty_vt* vt, const char* line) {
    unsigned a = 0;
    unsigned b = 0;
    unsigned c = 0;
    unsigned d = 0;
    unsigned e = 0;
    unsigned f = 0;
    double x = 0;
    double y = 0;
    int begin = 0;
    int end = 0;
    char payload[4096];
    uint8_t bytes[2048];
    if (sscanf(line, "key %u %u %u %u %u %u", &a, &b, &c, &d, &e, &f) == 6) {
        shitty_vt_key_event event = {0};
        event.key = (uint16_t)a;
        event.action = (uint8_t)b;
        event.modifiers = (uint16_t)c;
        event.layout_codepoint = d;
        event.base_codepoint = e;
        event.shifted_codepoint = f;
        shitty_vt_key(vt, &event);
    } else if (sscanf(line, "text %u %u", &a, &b) == 2) {
        shitty_vt_text(vt, a, (uint16_t)b);
    } else if (strncmp(line, "flush", 5) == 0) {
        shitty_vt_input_flush(vt);
    } else if (sscanf(line, "button %u %u %u %u %u %lf", &a, &b, &c, &d, &e, &x) == 6) {
        shitty_vt_mouse_button(vt, (int)a, (int)b, (int32_t)c, (int32_t)d, (uint16_t)e, x);
    } else if (sscanf(line, "motion %u %u %u", &a, &b, &c) == 3) {
        shitty_vt_mouse_motion(vt, (int32_t)a, (int32_t)b, (uint16_t)c);
    } else if (sscanf(line, "wheel %lf %lf %u %u %u", &x, &y, &a, &b, &c) == 5) {
        shitty_vt_mouse_scroll(vt, x, y, (int32_t)a, (int32_t)b, (uint16_t)c);
    } else if (sscanf(line, "paste %4095s", payload) == 1) {
        shitty_vt_paste(vt, bytes, parse_hex(payload, bytes, sizeof(bytes)));
    } else if (sscanf(line, "feed %4095s", payload) == 1) {
        shitty_vt_feed(vt, bytes, parse_hex(payload, bytes, sizeof(bytes)));
    } else if (sscanf(line, "focus %u", &a) == 1) {
        shitty_vt_focus(vt, (int)a);
    } else if (sscanf(line, "preedit %4095s %d %d", payload, &begin, &end) == 3) {
        /* "-" is the empty preview that clears a composition. */
        const size_t used = strcmp(payload, "-") == 0 ? 0 : parse_hex(payload, bytes, sizeof(bytes));
        shitty_vt_preedit(vt, bytes, used, begin, end);
    } else if (sscanf(line, "resize %u %u", &a, &b) == 2) {
        shitty_vt_resize(vt, (uint16_t)a, (uint16_t)b);
    } else if (sscanf(line, "wrap %u", &a) == 1) {
        /* Asks for one row by index, which the wrap line cannot do: it
         * only walks the rows that exist. */
        printf("wrap %u: %u\n", a, shitty_vt_row_wrap_length(vt, a));
    }
}

static int run_input_script(shitty_vt* vt, const char* path) {
    char line[8192];
    FILE* script = fopen(path, "r");
    if (script == NULL) {
        fprintf(stderr, "example: can not open %s\n", path);
        return 0;
    }
    while (fgets(line, sizeof(line), script) != NULL) {
        run_input_line(vt, line);
    }
    fclose(script);
    return 1;
}

int main(int argc, char** argv) {
    uint16_t columns = 80;
    uint16_t rows = 24;
    uint16_t save_lines = 0;
    const char* path = NULL;
    int scroll = 0;
    long scroll_to = -1;
    int dump_rows = 0;
    long new_save_lines = -1;
    const char* input_script = NULL;
    if (argc >= 4) {
        columns = (uint16_t)atoi(argv[1]);
        rows = (uint16_t)atoi(argv[2]);
        save_lines = (uint16_t)atoi(argv[3]);
        path = argc >= 5 ? argv[4] : NULL;
        /* Rows to scroll up into the scrollback before reading the grid. */
        scroll = argc >= 6 ? atoi(argv[5]) : 0;
        /* An absolute offset to settle on afterwards; negative leaves the
         * view where the relative scroll put it. */
        scroll_to = argc >= 7 ? atol(argv[6]) : -1;
        /* Print every addressable row, history first, after the grid. */
        dump_rows = argc >= 8 ? atoi(argv[7]) : 0;
        /* A history cap to apply after feeding; negative keeps the one
         * the terminal was built with. */
        new_save_lines = argc >= 9 ? atol(argv[8]) : -1;
        /* Input commands to run after the stream; see the usage note. */
        input_script = argc >= 10 ? argv[9] : NULL;
    } else if (argc == 2) {
        path = argv[1];
    }

    shitty_vt_callbacks callbacks = {0};
    callbacks.title_changed = on_title;
    callbacks.bell = on_bell;
    callbacks.clipboard_set = on_clipboard;
    callbacks.open_uri = on_open_uri;

    shitty_vt* vt = shitty_vt_new(columns, rows, save_lines, &callbacks);
    if (vt == NULL) {
        fprintf(stderr, "example: shitty_vt_new failed\n");
        return 1;
    }

    FILE* input = path != NULL ? fopen(path, "rb") : stdin;
    if (input == NULL) {
        fprintf(stderr, "example: can not open %s\n", path);
        shitty_vt_free(vt);
        return 1;
    }
    for (;;) {
        uint8_t chunk[16 * 1024];
        const size_t count = fread(chunk, 1, sizeof(chunk), input);
        if (count == 0) {
            break;
        }
        shitty_vt_feed(vt, chunk, count);
    }
    if (input != stdin) {
        fclose(input);
    }

    if (input_script != NULL && !run_input_script(vt, input_script)) {
        shitty_vt_free(vt);
        return 1;
    }

    if (new_save_lines >= 0) {
        shitty_vt_set_save_lines(vt, (uint16_t)new_save_lines);
    }

    shitty_vt_scroll(vt, scroll);
    if (scroll_to >= 0) {
        shitty_vt_scroll_to(vt, (uint32_t)scroll_to);
    }

    struct grid grid;
    grid.columns = columns;
    grid.rows = rows;
    grid.text = calloc((size_t)columns * rows, CELL_TEXT_CAP);
    if (grid.text == NULL) {
        shitty_vt_free(vt);
        return 1;
    }
    /* Continuations of wide cells are not reported; leave them blank. */
    {
        size_t slot;
        for (slot = 0; slot < (size_t)columns * rows; ++slot) {
            grid.text[slot * CELL_TEXT_CAP] = ' ';
            grid.text[slot * CELL_TEXT_CAP + 1] = '\0';
        }
    }
    shitty_vt_each_cell(vt, collect_cell, &grid);

    {
        uint16_t row;
        uint16_t column;
        for (row = 0; row < rows; ++row) {
            for (column = 0; column < columns; ++column) {
                fputs(grid.text + ((size_t)row * columns + column) * CELL_TEXT_CAP, stdout);
            }
            fputc('\n', stdout);
        }
    }
    free(grid.text);

    {
        const shitty_vt_cursor cursor = shitty_vt_cursor_state(vt);
        printf("cursor: %u %u style=%u visible=%u\n", cursor.column, cursor.row, cursor.style, cursor.visible);
    }
    printf("modes: 0x%x\n", shitty_vt_modes(vt));

    fputs("colors:", stdout);
    shitty_vt_each_cell(vt, collect_colors, NULL);
    fputc('\n', stdout);

    {
        uint8_t replies[4096];
        const size_t count = shitty_vt_take_replies(vt, replies, sizeof(replies));
        size_t index;
        fputs("replies:", stdout);
        for (index = 0; index < count; ++index) {
            printf(" %02x", replies[index]);
        }
        fputc('\n', stdout);
    }

    printf("scrollback: offset=%u history=%u total=%u\n", shitty_vt_scroll_offset(vt), shitty_vt_history_rows(vt), shitty_vt_total_rows(vt));

    {
        /* Where every addressable row stops because it wrapped, history
         * first, so a reader can rejoin the lines the terminal split. */
        const uint32_t total = shitty_vt_total_rows(vt);
        uint32_t index;
        fputs("wrap:", stdout);
        for (index = 0; index < total; ++index) {
            printf(" %u=%u", index, shitty_vt_row_wrap_length(vt, index));
        }
        fputc('\n', stdout);
    }

    {
        shitty_vt_memory memory;
        shitty_vt_memory_usage(vt, &memory);
        printf("memory: allocated_rows=%u capacity_rows=%u columns=%u cell_size=%u cell_bytes=%llu\n", memory.allocated_rows, memory.capacity_rows, memory.columns, memory.cell_size, (unsigned long long)memory.cell_bytes);
    }

    {
        struct grid preview;
        uint16_t column;
        preview.columns = columns;
        preview.rows = 1;
        preview.text = calloc(columns, CELL_TEXT_CAP);
        if (preview.text == NULL) {
            shitty_vt_free(vt);
            return 1;
        }
        for (column = 0; column < columns; ++column) {
            preview.text[column * CELL_TEXT_CAP] = '\0';
        }
        struct preview_span span;
        span.grid = &preview;
        span.row = 0;
        span.column = columns;
        span.count = 0;
        shitty_vt_preedit_cells(vt, collect_preview, &span);
        if (span.count == 0) {
            fputs("preedit: none\n", stdout);
        } else {
            printf("preedit: row=%u column=%u cells=%u text=", span.row, span.column, span.count);
            for (column = span.column; column < columns; ++column) {
                fputs(preview.text + column * CELL_TEXT_CAP, stdout);
            }
            fputc('\n', stdout);
        }
        free(preview.text);
    }

    if (dump_rows) {
        const uint32_t total = shitty_vt_total_rows(vt);
        uint32_t index;
        struct grid one;
        one.columns = columns;
        one.rows = 1;
        one.text = calloc(columns, CELL_TEXT_CAP);
        if (one.text == NULL) {
            shitty_vt_free(vt);
            return 1;
        }
        for (index = 0; index < total; ++index) {
            uint16_t column;
            for (column = 0; column < columns; ++column) {
                one.text[column * CELL_TEXT_CAP] = ' ';
                one.text[column * CELL_TEXT_CAP + 1] = '\0';
            }
            shitty_vt_row_cells(vt, index, collect_row, &one);
            printf("row %u:", index);
            for (column = 0; column < columns; ++column) {
                fputs(one.text + column * CELL_TEXT_CAP, stdout);
            }
            fputc('\n', stdout);
        }
        free(one.text);
    }

    shitty_vt_free(vt);
    return 0;
}
