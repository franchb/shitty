/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */
/* The C embedding facade over the shitty VT core: one opaque terminal,
 * fed bytes, read back as a grid. Everything the terminal wants from
 * its surroundings arrives through shitty_vt_callbacks; everything it
 * emits toward the child sits in the reply buffer until the embedder
 * drains it into its own pty. */

#pragma once

#include <stddef.h>
#include <stdint.h>

/* shitty_vt_cell.attributes bits */
#define SHITTY_VT_ATTR_BOLD (1u << 0)
#define SHITTY_VT_ATTR_FAINT (1u << 1)
#define SHITTY_VT_ATTR_ITALIC (1u << 2)
#define SHITTY_VT_ATTR_BLINK (1u << 3)
#define SHITTY_VT_ATTR_INVERSE (1u << 4)
#define SHITTY_VT_ATTR_CONCEAL (1u << 5)
#define SHITTY_VT_ATTR_STRIKE (1u << 6)
#define SHITTY_VT_ATTR_OVERLINE (1u << 7)

/* shitty_vt_modes() bits */
#define SHITTY_VT_MODE_ALT_SCREEN (1u << 0)
#define SHITTY_VT_MODE_BRACKETED_PASTE (1u << 1)
#define SHITTY_VT_MODE_APP_CURSOR_KEYS (1u << 2)
#define SHITTY_VT_MODE_APP_KEYPAD (1u << 3)
#define SHITTY_VT_MODE_FOCUS_EVENTS (1u << 4)
#define SHITTY_VT_MODE_AUTO_WRAP (1u << 5)
#define SHITTY_VT_MODE_ORIGIN (1u << 6)
#define SHITTY_VT_MODE_INSERT (1u << 7)
#define SHITTY_VT_MODE_CURSOR_VISIBLE (1u << 8)
#define SHITTY_VT_MODE_SCREEN_REVERSE (1u << 9)
#define SHITTY_VT_MODE_SYNCHRONIZED_OUTPUT (1u << 10)
#define SHITTY_VT_MODE_MOUSE_CLICK (1u << 11)
#define SHITTY_VT_MODE_MOUSE_DRAG (1u << 12)
#define SHITTY_VT_MODE_MOUSE_MOTION (1u << 13)
#define SHITTY_VT_MODE_MOUSE_SGR (1u << 14)
/* DECSET 1007: while the alternate screen is up, wheel input should be
 * sent as arrow keys rather than scrolling a history it does not keep. */
#define SHITTY_VT_MODE_ALTERNATE_SCROLL (1u << 15)

/* Low byte of a shitty_vt_cell color source: where the color the
 * application asked for came from, before the palette resolved it. */
#define SHITTY_VT_COLOR_DEFAULT_FOREGROUND 0
#define SHITTY_VT_COLOR_DEFAULT_BACKGROUND 1
#define SHITTY_VT_COLOR_INDEXED 2
#define SHITTY_VT_COLOR_DIRECT 3
#define SHITTY_VT_COLOR_KIND(source) ((source) & 0xff)
/* Palette entry, valid when the kind is SHITTY_VT_COLOR_INDEXED. */
#define SHITTY_VT_COLOR_INDEX(source) (((source) >> 8) & 0xff)

#ifdef __cplusplus
extern "C" {
#endif
    typedef struct shitty_vt shitty_vt;

    /* One readable cell. Colors are resolved through the palette into
     * 0x00BBGGRR - the little-endian view of struct { uint8_t r, g, b; }.
     * The grapheme is the cell's decoded codepoints; an empty cell has
     * grapheme_len 0. The pointer is valid only for the duration of the
     * shitty_vt_each_cell callback.
     *
     * The *_source fields say where each resolved color came from: the
     * kind in the low byte, and for SHITTY_VT_COLOR_INDEXED the palette
     * entry in the high byte. Read them with SHITTY_VT_COLOR_KIND and
     * SHITTY_VT_COLOR_INDEX.
     *
     * An embedder that paints with this terminal's palette can ignore
     * them and use the resolved values. One that owns a palette of its
     * own - drawing into another terminal, or theming its panes - cannot:
     * resolved RGB pins every cell to the colors this terminal was
     * configured with, so an application asking for the default color, or
     * for ANSI red, arrives painted rather than named and the embedder's
     * own theme never applies. The source is what the application asked
     * for; the resolved value is what this terminal would have drawn.
     *
     * A special color the terminal's own configuration sets - the bold,
     * blink, underline, italic or inverse color - replaces the resolved
     * value without replacing the request. Those cells report
     * SHITTY_VT_COLOR_DIRECT, because what reaches the embedder is then a
     * color in its own right rather than that palette entry. */
    typedef struct shitty_vt_cell {
        const uint32_t* grapheme;
        size_t grapheme_len;
        uint32_t foreground;
        uint32_t background;
        uint32_t underline_color;
        uint16_t attributes;
        /* 0 none, 1 straight, 2 double, 3 curly, 4 dotted, 5 dashed */
        uint8_t underline_style;
        /* 1 or 2; the continuation of a wide cell is not reported */
        uint8_t width;
        uint16_t foreground_source;
        uint16_t background_source;
        uint16_t underline_source;
    } shitty_vt_cell;

    /* row is a row of the current view, not of the live screen: while the
     * view sits in the scrollback the cursor can be at or past rows, which
     * means it is simply not on screen and nothing should be drawn for it. */
    typedef struct shitty_vt_cursor {
        uint16_t column;
        uint16_t row;
        /* 0 hidden, 1 filled block, 2 hollow block, 3 underline, 4 bar */
        uint8_t style;
        uint8_t visible;
    } shitty_vt_cursor;

    typedef void (*shitty_vt_cell_fn)(void* user, uint16_t row, uint16_t column, const shitty_vt_cell* cell);

    /* Everything the terminal may want from its embedder. Every callback
     * may be null; user is passed back verbatim. The terminal keeps the
     * pointer, not a copy - the struct must outlive it. Nothing fires
     * during shitty_vt_new: the first callback an embedder sees is the
     * application's own doing. */
    typedef struct shitty_vt_callbacks {
        void* user;
        /* The application published a new title. */
        void (*title_changed)(void* user, const uint8_t* title, size_t len);
        /* BEL, or an attention request - present it however fits. */
        void (*bell)(void* user);
        /* The presentation moved; re-read the grid when convenient. */
        void (*damaged)(void* user);
        /* A hyperlink wants opening on the embedder's desktop. */
        void (*open_uri)(void* user, const uint8_t* uri, size_t len);
        /* OSC 52: the application replaced a selection. 0 is the primary
         * selection, 1 the clipboard. */
        void (*clipboard_set)(void* user, int clipboard, const uint8_t* bytes, size_t len);
        /* XTWINOPS asked for a grid this large; the embedder decides and
         * answers with shitty_vt_resize if it agrees. */
        void (*resize_request)(void* user, uint16_t columns, uint16_t rows);
    } shitty_vt_callbacks;

    /* callbacks may be null when the embedder wants none. */
    shitty_vt* shitty_vt_new(uint16_t columns, uint16_t rows, uint16_t save_lines, const shitty_vt_callbacks* callbacks);
    void shitty_vt_free(shitty_vt*);
    void shitty_vt_feed(shitty_vt*, const uint8_t* bytes, size_t len);
    void shitty_vt_resize(shitty_vt*, uint16_t columns, uint16_t rows);

    /* Terminal-generated replies (DA, DSR, ...) the embedder must forward
     * to its pty. Drains up to cap bytes into out and returns how many. */
    size_t shitty_vt_take_replies(shitty_vt*, uint8_t* out, size_t cap);

    /* Walks the visible grid row-major; wide-cell continuations are
     * skipped. Reads whatever the view currently shows, so it follows
     * shitty_vt_scroll into the scrollback. */
    void shitty_vt_each_cell(shitty_vt*, shitty_vt_cell_fn, void* user);

    /* Moves the view through the scrollback: positive rows scroll up into
     * history, negative back toward the live bottom. Clamped to the retained
     * history, and inert on the alternate screen, which keeps none. Returns
     * the resulting offset. */
    uint32_t shitty_vt_scroll(shitty_vt*, int32_t rows);

    /* Places the view so that offset rows of history sit above it; 0 is
     * the live bottom. Clamped like shitty_vt_scroll. Returns the resulting
     * offset. */
    uint32_t shitty_vt_scroll_to(shitty_vt*, uint32_t offset);

    /* Rows of history above the live bottom the view currently shows;
     * 0 while it is live. */
    uint32_t shitty_vt_scroll_offset(const shitty_vt*);

    /* Rows of scrollback retained, which is the largest offset
     * shitty_vt_scroll_to will accept. */
    uint32_t shitty_vt_history_rows(const shitty_vt*);

    /* Rows addressable through shitty_vt_row_cells: the retained history
     * followed by the visible grid. Index 0 is the oldest row still kept and
     * the last index is the bottom of the live screen. */
    uint32_t shitty_vt_total_rows(const shitty_vt*);

    /* What the terminal is spending on its grid and history. Cells only:
     * grapheme clusters, hyperlinks and sixel patches live in a separate
     * store this does not count, so treat it as the floor of the real cost
     * rather than the whole of it. */
    typedef struct shitty_vt_memory {
        /* Row slots actually backed by cells. The ring behind them is
         * rounded up to a power of two, so this can exceed capacity_rows:
         * it is what the screen costs, not what it is allowed to hold. */
        uint32_t allocated_rows;
        /* Rows the terminal will keep: rows + save_lines. */
        uint32_t capacity_rows;
        uint32_t columns;
        /* Bytes in one cell, so an embedder can do its own arithmetic. */
        uint32_t cell_size;
        /* allocated_rows * columns * cell_size. */
        uint64_t cell_bytes;
    } shitty_vt_memory;

    void shitty_vt_memory_usage(const shitty_vt*, shitty_vt_memory* out);

    /* Changes how many rows of scrollback the terminal keeps. Lowering it
     * drops the oldest rows that no longer fit, and does so at once rather
     * than as the history is overwritten. The visible grid is untouched. */
    void shitty_vt_set_save_lines(shitty_vt*, uint16_t save_lines);

    /* Walks one row by absolute index, oldest first, leaving the view
     * where it is - the row argument handed to the callback is the index
     * asked for. An index past the last row visits nothing. Use this to read
     * the scrollback without scrolling; use shitty_vt_each_cell to read what
     * the user is looking at. */
    void shitty_vt_row_cells(shitty_vt*, uint32_t index, shitty_vt_cell_fn, void* user);

    /* Where a row's text stops because it wrapped onto the next one: the
     * columns that belong to the row, or 0 if it ends on its own. Indexed
     * like shitty_vt_row_cells, and an index past the last row reports 0.
     *
     * An embedder that keeps its own scrollback needs this to tell one
     * logical line from two. Without it a line the terminal wrapped comes
     * back split at whatever width it happened to be written at, which is
     * wrong for anything that reads the text again - a search, a copy, or
     * handing a command's output to something else.
     *
     * It is a length rather than a flag because the terminal wraps
     * wherever it ran out of room, which is not always the last column: a
     * double-width character that does not fit leaves the one before it
     * empty. Rejoining takes exactly this many columns and none of the
     * blanks after them. Asking only whether the row continues is a
     * comparison against 0. */
    uint16_t shitty_vt_row_wrap_length(const shitty_vt*, uint32_t index);
    shitty_vt_cursor shitty_vt_cursor_state(const shitty_vt*);
    uint32_t shitty_vt_modes(const shitty_vt*);

    /* Input.
     *
     * The terminal encodes its own input: cursor and keypad modes,
     * modifyOtherKeys, the kitty keyboard protocol with its flag stack,
     * every mouse encoding, and bracketed paste are all applied to the
     * events below exactly as the application negotiated them, and the
     * encoded bytes land in the reply buffer for shitty_vt_take_replies
     * to drain. The embedder reports events; it never encodes.
     *
     * Deliver keyboard events the way a windowing layer would: the key
     * event first, the text it produced (if any) second, and
     * shitty_vt_input_flush to end the batch. The flush matters - a key
     * may be held back waiting to learn whether text follows (the kitty
     * protocol's associated text) and is released either by that text or
     * by the flush. A key that produces no text needs no text event; a
     * text event may also arrive alone (input methods do). */

    /* Key codes, mirroring the input layer one to one. The values are
     * pinned ABI. SHITTY_VT_KEY_PRINTABLE is every character key: the
     * identity travels in the codepoints of the event, not the code. */
#define SHITTY_VT_KEY_UNKNOWN 0
#define SHITTY_VT_KEY_PRINTABLE 1
#define SHITTY_VT_KEY_SPACE 2
#define SHITTY_VT_KEY_ESCAPE 3
#define SHITTY_VT_KEY_ENTER 4
#define SHITTY_VT_KEY_BACKSPACE 5
#define SHITTY_VT_KEY_TAB 6
#define SHITTY_VT_KEY_INSERT 7
#define SHITTY_VT_KEY_DELETE 8
#define SHITTY_VT_KEY_HOME 9
#define SHITTY_VT_KEY_END 10
#define SHITTY_VT_KEY_UP 11
#define SHITTY_VT_KEY_DOWN 12
#define SHITTY_VT_KEY_LEFT 13
#define SHITTY_VT_KEY_RIGHT 14
#define SHITTY_VT_KEY_PAGE_UP 15
#define SHITTY_VT_KEY_PAGE_DOWN 16
#define SHITTY_VT_KEY_CLEAR 17
#define SHITTY_VT_KEY_F1 18
#define SHITTY_VT_KEY_F2 19
#define SHITTY_VT_KEY_F3 20
#define SHITTY_VT_KEY_F4 21
#define SHITTY_VT_KEY_F5 22
#define SHITTY_VT_KEY_F6 23
#define SHITTY_VT_KEY_F7 24
#define SHITTY_VT_KEY_F8 25
#define SHITTY_VT_KEY_F9 26
#define SHITTY_VT_KEY_F10 27
#define SHITTY_VT_KEY_F11 28
#define SHITTY_VT_KEY_F12 29
#define SHITTY_VT_KEY_F13 30
#define SHITTY_VT_KEY_F14 31
#define SHITTY_VT_KEY_F15 32
#define SHITTY_VT_KEY_F16 33
#define SHITTY_VT_KEY_F17 34
#define SHITTY_VT_KEY_F18 35
#define SHITTY_VT_KEY_F19 36
#define SHITTY_VT_KEY_F20 37
#define SHITTY_VT_KEY_F21 38
#define SHITTY_VT_KEY_F22 39
#define SHITTY_VT_KEY_F23 40
#define SHITTY_VT_KEY_F24 41
#define SHITTY_VT_KEY_F25 42
#define SHITTY_VT_KEY_F26 43
#define SHITTY_VT_KEY_F27 44
#define SHITTY_VT_KEY_F28 45
#define SHITTY_VT_KEY_F29 46
#define SHITTY_VT_KEY_F30 47
#define SHITTY_VT_KEY_F31 48
#define SHITTY_VT_KEY_F32 49
#define SHITTY_VT_KEY_F33 50
#define SHITTY_VT_KEY_F34 51
#define SHITTY_VT_KEY_F35 52
#define SHITTY_VT_KEY_KEYPAD_0 53
#define SHITTY_VT_KEY_KEYPAD_1 54
#define SHITTY_VT_KEY_KEYPAD_2 55
#define SHITTY_VT_KEY_KEYPAD_3 56
#define SHITTY_VT_KEY_KEYPAD_4 57
#define SHITTY_VT_KEY_KEYPAD_5 58
#define SHITTY_VT_KEY_KEYPAD_6 59
#define SHITTY_VT_KEY_KEYPAD_7 60
#define SHITTY_VT_KEY_KEYPAD_8 61
#define SHITTY_VT_KEY_KEYPAD_9 62
#define SHITTY_VT_KEY_KEYPAD_DECIMAL 63
#define SHITTY_VT_KEY_KEYPAD_DIVIDE 64
#define SHITTY_VT_KEY_KEYPAD_MULTIPLY 65
#define SHITTY_VT_KEY_KEYPAD_SUBTRACT 66
#define SHITTY_VT_KEY_KEYPAD_ADD 67
#define SHITTY_VT_KEY_KEYPAD_ENTER 68
#define SHITTY_VT_KEY_KEYPAD_EQUAL 69
#define SHITTY_VT_KEY_KEYPAD_SEPARATOR 70
#define SHITTY_VT_KEY_KEYPAD_F1 71
#define SHITTY_VT_KEY_KEYPAD_F2 72
#define SHITTY_VT_KEY_KEYPAD_F3 73
#define SHITTY_VT_KEY_KEYPAD_F4 74
#define SHITTY_VT_KEY_KEYPAD_INSERT 75
#define SHITTY_VT_KEY_KEYPAD_DELETE 76
#define SHITTY_VT_KEY_KEYPAD_UP 77
#define SHITTY_VT_KEY_KEYPAD_DOWN 78
#define SHITTY_VT_KEY_KEYPAD_LEFT 79
#define SHITTY_VT_KEY_KEYPAD_RIGHT 80
#define SHITTY_VT_KEY_KEYPAD_HOME 81
#define SHITTY_VT_KEY_KEYPAD_END 82
#define SHITTY_VT_KEY_KEYPAD_PAGE_UP 83
#define SHITTY_VT_KEY_KEYPAD_PAGE_DOWN 84
#define SHITTY_VT_KEY_KEYPAD_BEGIN 85
#define SHITTY_VT_KEY_KEYPAD_SPACE 86
#define SHITTY_VT_KEY_KEYPAD_TAB 87
#define SHITTY_VT_KEY_CAPS_LOCK 88
#define SHITTY_VT_KEY_SCROLL_LOCK 89
#define SHITTY_VT_KEY_NUM_LOCK 90
#define SHITTY_VT_KEY_PRINT_SCREEN 91
#define SHITTY_VT_KEY_PAUSE 92
#define SHITTY_VT_KEY_MENU 93
#define SHITTY_VT_KEY_LEFT_SHIFT 94
#define SHITTY_VT_KEY_LEFT_CONTROL 95
#define SHITTY_VT_KEY_LEFT_ALT 96
#define SHITTY_VT_KEY_LEFT_SUPER 97
#define SHITTY_VT_KEY_RIGHT_SHIFT 98
#define SHITTY_VT_KEY_RIGHT_CONTROL 99
#define SHITTY_VT_KEY_RIGHT_ALT 100
#define SHITTY_VT_KEY_RIGHT_SUPER 101
#define SHITTY_VT_KEY_MEDIA_PLAY 102
#define SHITTY_VT_KEY_MEDIA_PAUSE 103
#define SHITTY_VT_KEY_MEDIA_PLAY_PAUSE 104
#define SHITTY_VT_KEY_MEDIA_REVERSE 105
#define SHITTY_VT_KEY_MEDIA_STOP 106
#define SHITTY_VT_KEY_MEDIA_FAST_FORWARD 107
#define SHITTY_VT_KEY_MEDIA_REWIND 108
#define SHITTY_VT_KEY_MEDIA_TRACK_NEXT 109
#define SHITTY_VT_KEY_MEDIA_TRACK_PREVIOUS 110
#define SHITTY_VT_KEY_MEDIA_RECORD 111
#define SHITTY_VT_KEY_VOLUME_DOWN 112
#define SHITTY_VT_KEY_VOLUME_UP 113
#define SHITTY_VT_KEY_VOLUME_MUTE 114
#define SHITTY_VT_KEY_COUNT 115

    /* shitty_vt_key_event.modifiers bits. The lock states matter: NumLock
     * selects between keypad identities, and the kitty protocol reports
     * both locks when asked to. */
#define SHITTY_VT_MOD_SHIFT (1u << 0)
#define SHITTY_VT_MOD_CONTROL (1u << 1)
#define SHITTY_VT_MOD_ALT (1u << 2)
#define SHITTY_VT_MOD_SUPER (1u << 3)
#define SHITTY_VT_MOD_CAPS_LOCK (1u << 4)
#define SHITTY_VT_MOD_NUM_LOCK (1u << 5)
#define SHITTY_VT_MOD_ALT_GRAPH (1u << 6)

    /* shitty_vt_key_event.action values. Repeat and release exist for the
     * kitty protocol; a legacy application sees presses and repeats the
     * same way and releases not at all. */
#define SHITTY_VT_KEY_PRESS 0
#define SHITTY_VT_KEY_REPEAT 1
#define SHITTY_VT_KEY_RELEASE 2

    typedef struct shitty_vt_key_event {
        /* A SHITTY_VT_KEY_* code. */
        uint16_t key;
        /* SHITTY_VT_KEY_PRESS, _REPEAT or _RELEASE. */
        uint8_t action;
        /* SHITTY_VT_MOD_* bits. */
        uint16_t modifiers;
        /* The key's unicode identity in the active layout unshifted, in
         * the base (ASCII) layout, and with Shift in the active layout.
         * They feed chord encoding and the kitty protocol's alternate
         * keys; zero means unknown, and a plain embedder that sets only
         * layout_codepoint (or none, for a named key) is well-formed. */
        uint32_t layout_codepoint;
        uint32_t base_codepoint;
        uint32_t shifted_codepoint;
    } shitty_vt_key_event;

    /* A physical key event. Returns 1 when the terminal consumed it,
     * 0 for an event it does not handle (or a malformed one). */
    int shitty_vt_key(shitty_vt*, const shitty_vt_key_event* event);

    /* The text a key produced (or an input method committed), one
     * codepoint per call. Returns 1 when consumed. */
    int shitty_vt_text(shitty_vt*, uint32_t codepoint, uint16_t modifiers);

    /* Ends the event batch: a key still waiting for its text is released
     * as text-less. Call after every batch of key/text events. */
    void shitty_vt_input_flush(shitty_vt*);

    /* Mouse buttons. */
#define SHITTY_VT_MOUSE_LEFT 0
#define SHITTY_VT_MOUSE_RIGHT 1
#define SHITTY_VT_MOUSE_MIDDLE 2
#define SHITTY_VT_MOUSE_AUX1 3
#define SHITTY_VT_MOUSE_AUX2 4
#define SHITTY_VT_MOUSE_AUX3 5
#define SHITTY_VT_MOUSE_AUX4 6
#define SHITTY_VT_MOUSE_AUX5 7

    /* Pointer events in cell coordinates, 0-based from the top left; the
     * protocols' 1-based numbering is the terminal's business. During a
     * drag the coordinates may leave the grid. Unshifted pointer events
     * also drive selection when no tracking mode captures them: a
     * finished selection reaches the embedder through clipboard_set.
     * time is seconds on any monotonic clock and separates the clicks of
     * a double or triple click. */
    int shitty_vt_mouse_button(shitty_vt*, int button, int pressed, int32_t column, int32_t row, uint16_t modifiers, double time);
    int shitty_vt_mouse_motion(shitty_vt*, int32_t column, int32_t row, uint16_t modifiers);

    /* Wheel or trackpad scroll; dx/dy are in wheel lines, positive up
     * and right, and fractions accumulate across calls. Reported to the
     * application when a mouse mode captures the wheel (including
     * alternate-screen wheel-to-arrows), otherwise scrolls the view. */
    int shitty_vt_mouse_scroll(shitty_vt*, double dx, double dy, int32_t column, int32_t row, uint16_t modifiers);

    /* Paste as the terminal's own paste path does it: the payload is
     * sanitized and wrapped in bracketed-paste markers when the
     * application turned the mode on. Capped at 16 MiB. */
    void shitty_vt_paste(shitty_vt*, const uint8_t* bytes, size_t len);

    /* Keyboard focus, reported to applications that asked for focus
     * events. A fresh terminal is focused. */
    void shitty_vt_focus(shitty_vt*, int focused);

    /* The text an input method is composing, before it commits. The
     * preview is drawn over the cursor row and belongs to no one else:
     * it never enters the grid, the scrollback or the replies, and the
     * committed text arrives later as ordinary shitty_vt_text events.
     * Empty text clears it.
     *
     * cursor_begin and cursor_end are byte offsets into text, or -1 when
     * the input method hides its cursor. That range is shown in reverse
     * video, the rest of the preview underlined.
     *
     * The preview clusters like printed text: a combining mark, a joiner
     * or a variation selector shares the cell it extends, and the cell's
     * grapheme carries the whole cluster. What the preview shows is what
     * the grid will hold once the composition commits.
     *
     * While a preview is up shitty_vt_cursor_state reports a hidden
     * cursor positioned at the preview's cursor cell, which is where an
     * input method wants its candidate window. */
    void shitty_vt_preedit(shitty_vt*, const uint8_t* text, size_t len, int32_t cursor_begin, int32_t cursor_end);

    /* Visits the preview's cells left to right, at the row and columns
     * they cover; visits nothing when no preview is active. They are an
     * overlay, so shitty_vt_each_cell does not report them and what
     * they cover is still underneath - draw them last. A preview too
     * wide for the row is clipped to it, keeping the freshest input. */
    void shitty_vt_preedit_cells(shitty_vt*, shitty_vt_cell_fn, void* user);
#ifdef __cplusplus
}
#endif
