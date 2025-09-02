/**
 * @file terminal_paint.c
 * @brief Terminal Paint Application
 * @date 2025
 * 
 * Simple terminal-based painting application using ncurses library.
 * Implements character-based drawing with color support and file persistence.
 * 
 * @section build Build Instructions
 * Linux/Unix: gcc -O2 -Wall -Wextra terminal_paint.c -o terminal_paint -lncurses
 * Windows (MSYS2): gcc terminal_paint.c -o terminal_paint -I/ucrt64/include/ncursesw -L/ucrt64/lib -lncursesw
 * 
 * @section implementation Implementation Details
 * - Canvas: Dynamic 2D cell array storing character and color data
 * - Input: ncurses getch() with switch-case key mapping
 * - Rendering: Selective screen updates using ncurses drawing functions
 * - File format: Plain text with dimension header and comma-separated values
 * - Load behavior: Overlays loaded canvas onto existing canvas (preserves non-overlapping areas)
 * 
 * @section controls Control Mapping
 * Movement: Arrow keys
 * Paint: Space (single), Enter (toggle continuous)
 * Tools: B (brush cycle), C (color cycle), E (eraser), X (clear)
 * Colors: 0-7 (direct index selection)
 * File: S (save), L (load)
 * Exit: Q
 */

#include <ncursesw/curses.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

/*==============================================================================
 * CONSTANTS AND CONFIGURATION
 *============================================================================*/

/**
 * @def MAX_CANVAS_WIDTH
 * @brief Maximum canvas width to prevent excessive memory allocation
 */
#define MAX_CANVAS_WIDTH  1000

/**
 * @def MAX_CANVAS_HEIGHT
 * @brief Maximum canvas height to prevent excessive memory allocation
 */
#define MAX_CANVAS_HEIGHT 1000

/**
 * @def DEFAULT_SAVE_FILE
 * @brief Default filename for save/load operations
 */
#define DEFAULT_SAVE_FILE "paint_save.txt"

/**
 * @def BRUSH_COUNT
 * @brief Number of available brush characters
 */
#define BRUSH_COUNT (sizeof(brush_chars))

/**
 * @def COLOR_COUNT
 * @brief Number of available colors in the palette
 */
#define COLOR_COUNT 8

/**
 * @def STATUS_LINES_TOP
 * @brief Number of status lines reserved at top of screen
 */
#define STATUS_LINES_TOP    2

/**
 * @def STATUS_LINES_BOTTOM
 * @brief Number of status lines reserved at bottom of screen
 */
#define STATUS_LINES_BOTTOM 1

/*==============================================================================
 * TYPE DEFINITIONS
 *============================================================================*/

/**
 * @struct Cell
 * @brief Represents a single canvas cell with character and color information
 * 
 * Each cell stores both the ASCII character to display and its color index.
 * Color indices map to ncurses color pairs for efficient rendering.
 */
typedef struct {
    unsigned char ch;    /**< ASCII character (' ' for empty cells) */
    short color;         /**< Color index (0-7, maps to COLOR_* constants) */
} Cell;

/**
 * @struct AppState
 * @brief Global application state container
 * 
 * Centralizes all application state for better organization and
 * easier debugging/maintenance.
 */
typedef struct {
    Cell *canvas;           /**< Dynamic canvas array */
    int canvas_width;       /**< Canvas width in characters */
    int canvas_height;      /**< Canvas height in characters */
    int cursor_x;           /**< Current cursor X position */
    int cursor_y;           /**< Current cursor Y position */
    bool pen_down;          /**< Pen mode: paint while moving */
    int brush_index;        /**< Current brush character index */
    short current_color;    /**< Current color index (0-7) */
    bool running;           /**< Main loop control flag */
} AppState;

/*==============================================================================
 * GLOBAL DATA
 *============================================================================*/

/**
 * @var g_app
 * @brief Global application state instance
 * @details Centralized state container for the entire application
 */
static AppState g_app = {0};

/**
 * @var brush_chars
 * @brief Available brush characters ordered by visual density
 * @details Modifiable array that can be altered for special modes like eraser
 */
static char brush_chars[] = { '#', '*', '@', '%', '+', 'o', 'x', '.', '~', '&' };

/**
 * @var original_brush_chars
 * @brief Backup copy of original brush characters
 * @details Used to restore brushes after eraser mode or other modifications
 */
static const char original_brush_chars[] = { '#', '*', '@', '%', '+', 'o', 'x', '.', '~', '&' };

/**
 * @var base_colors
 * @brief Color mapping to ncurses COLOR_* constants
 * @details Maps color indices 0-7 to their corresponding ncurses color values
 */
static const short base_colors[COLOR_COUNT] = {
    COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW,
    COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE
};

/**
 * @var color_names
 * @brief Human-readable color names for status display
 * @details String representations of colors for user interface display
 */
static const char *color_names[COLOR_COUNT] = {
    "BLACK", "RED", "GREEN", "YELLOW", "BLUE", "MAGENTA", "CYAN", "WHITE"
};

/*==============================================================================
 * FORWARD DECLARATIONS
 *============================================================================*/

static int sneak_peek_at_file(FILE *f);
static bool start_stuff(void);
static void clean_stuff(void);
static bool setup_palette(void);
static void canvas_fit(void);
static void input_stuff(int key);
static void refresh_view(void);
static void show_status_info(void);
static void paint_entire_canvas(void);
static void render_stuff(int x, int y);
static void show_or_hide_cursor(bool show);
static Cell* find_spot(int x, int y);
static void paint_stuff(void);
static void start_with_blank_canvas(void);
static void move_brush(int dx, int dy);
static void save_masterpiece(const char *filename);
static void load_masterpiece(const char *filename);
static bool check_if_coordinates_make_sense(int x, int y);

/*==============================================================================
 * UTILITY FUNCTIONS
 *============================================================================*/

/**
 * @brief Peek at the next character in a file stream without consuming it
 * @param f File stream to peek into
 * @return Next character or EOF if at end of file
 */
static int sneak_peek_at_file(FILE *f) {
    int c = fgetc(f);
    if (c != EOF) {
        ungetc(c, f);
    }
    return c;
}

/**
 * @brief Convert canvas coordinates to screen coordinates
 * @param y Canvas Y coordinate
 * @return Screen Y coordinate
 */
static inline int canvas_to_screen_y(int y) {
    return STATUS_LINES_TOP + y;
}

/**
 * @brief Convert canvas coordinates to screen coordinates
 * @param x Canvas X coordinate  
 * @return Screen X coordinate
 */
static inline int canvas_to_screen_x(int x) {
    return x;
}

/**
 * @brief Validate canvas coordinates
 * @param x X coordinate to validate
 * @param y Y coordinate to validate
 * @return true if coordinates are within canvas bounds
 */
static bool check_if_coordinates_make_sense(int x, int y) {
    return (x >= 0 && x < g_app.canvas_width && 
            y >= 0 && y < g_app.canvas_height);
}

/*==============================================================================
 * CANVAS OPERATIONS
 *============================================================================*/

/**
 * @brief Get a pointer to the cell at the specified coordinates
 * @param x X coordinate
 * @param y Y coordinate
 * @return Pointer to Cell or NULL if coordinates are invalid
 */
static Cell* find_spot(int x, int y) {
    if (!check_if_coordinates_make_sense(x, y)) {
        return NULL;
    }
    return &g_app.canvas[y * g_app.canvas_width + x];
}

/**
 * @brief Paint at the current cursor position
 */
static void paint_stuff(void) {
    Cell *cell = find_spot(g_app.cursor_x, g_app.cursor_y);
    if (!cell) return;
    
    cell->ch = brush_chars[g_app.brush_index];
    cell->color = g_app.current_color;
    render_stuff(g_app.cursor_x, g_app.cursor_y);
}



/**
 * @brief Fill the canvas with spaces
 */
static void start_with_blank_canvas(void) {
    const size_t total_cells = (size_t)g_app.canvas_width * (size_t)g_app.canvas_height;
    
    for (size_t i = 0; i < total_cells; ++i) {
        g_app.canvas[i].ch = ' ';
        g_app.canvas[i].color = g_app.current_color;
    }
    
    paint_entire_canvas();
}

/**
 * @brief Move cursor with boundary checking and optional auto-paint
 * @param dx X movement delta (-1, 0, or 1)
 * @param dy Y movement delta (-1, 0, or 1)
 */
static void move_brush(int dx, int dy) {
    int new_x = g_app.cursor_x + dx;
    int new_y = g_app.cursor_y + dy;
    
    // Clamp to canvas boundaries
    if (new_x < 0) new_x = 0;
    if (new_x >= g_app.canvas_width) new_x = g_app.canvas_width - 1;
    if (new_y < 0) new_y = 0;
    if (new_y >= g_app.canvas_height) new_y = g_app.canvas_height - 1;
    
    // Only update if position actually changed
    if (new_x != g_app.cursor_x || new_y != g_app.cursor_y) {
        g_app.cursor_x = new_x;
        g_app.cursor_y = new_y;
        
        // Auto-paint if pen is down
        if (g_app.pen_down) {
            paint_stuff();
        }
    }
}

/*==============================================================================
 * RENDERING SYSTEM
 *============================================================================*/

/**
 * @brief Render a single canvas cell to the screen
 * @param x Canvas X coordinate
 * @param y Canvas Y coordinate
 */
static void render_stuff(int x, int y) {
    Cell *cell = find_spot(x, y);
    if (!cell) return;
    
    int screen_y = canvas_to_screen_y(y);
    int screen_x = canvas_to_screen_x(x);
    
    // Set color attributes
    attrset(COLOR_PAIR(cell->color + 1));
    mvaddch(screen_y, screen_x, cell->ch ? cell->ch : ' ');
    attrset(A_NORMAL);
}

/**
 * @brief Render the entire canvas to the screen
 */
static void paint_entire_canvas(void) {
    for (int y = 0; y < g_app.canvas_height; ++y) {
        for (int x = 0; x < g_app.canvas_width; ++x) {
            render_stuff(x, y);
        }
    }
}

/**
 * @brief Render or hide the cursor highlight
 * @param show true to show cursor, false to hide
 */
static void show_or_hide_cursor(bool show) {
    Cell *cell = find_spot(g_app.cursor_x, g_app.cursor_y);
    if (!cell) return;
    
    int screen_y = canvas_to_screen_y(g_app.cursor_y);
    int screen_x = canvas_to_screen_x(g_app.cursor_x);
    
    if (show) {
        // Highlight cursor with reverse video
        attrset(COLOR_PAIR(cell->color + 1) | A_REVERSE);
        mvaddch(screen_y, screen_x, cell->ch ? cell->ch : ' ');
        attrset(A_NORMAL);
    } else {
        // Render normally
        render_stuff(g_app.cursor_x, g_app.cursor_y);
    }
}

/**
 * @brief Render the status bars and help information
 */
static void show_status_info(void) {
    // Top status line
    move(0, 0);
    clrtoeol();
    attrset(A_BOLD);
    printw("Terminal Paint :D  |  Brush: '%c'  |  Color: %s  |  Pen: %s  |  Canvas: %dx%d",
           brush_chars[g_app.brush_index],
           color_names[g_app.current_color],
           g_app.pen_down ? "DOWN" : "UP",
           g_app.canvas_width,
           g_app.canvas_height);
    attrset(A_NORMAL);

    // Second status line with controls
    move(1, 0);
    clrtoeol();
    printw("Position: (%d,%d)  |  Movement: Arrow keys  |  "
           "Paint: Space  |  Pen: Enter  |  Tools: B/C/E/X  |  "
           "Colors: 0-7  |  File: S/L  |  Quit: Q",
           g_app.cursor_x, g_app.cursor_y);

    // Bottom help line
    move(LINES - 1, 0);
    clrtoeol();
    printw("Tips: Enter toggles pen mode for continuous painting. "
           "Files save to '%s'. Use 0-7 for quick color selection.",
           DEFAULT_SAVE_FILE);
}

/**
 * @brief Render the complete frame (status + canvas + cursor)
 */
static void refresh_view(void) {
    show_status_info();
    show_or_hide_cursor(true);
    refresh();
}

/*==============================================================================
 * FILE I/O OPERATIONS
 *============================================================================*/

/**
 * @brief Save the current canvas to a file in custom text format
 * @param filename Target filename (NULL uses DEFAULT_SAVE_FILE)
 * 
 * @details
 * File format specification:
 * - Line 1: "width height" (canvas dimensions)
 * - Subsequent lines: "color,ascii color,ascii ..." (space-separated pairs)
 * - Each row on separate line
 * - color: 0-7 (color index)
 * - ascii: 0-255 (ASCII character code, 32 = space)
 * 
 * @note File creation errors are silently ignored for simplicity
 */
static void save_masterpiece(const char *filename) {
    if (!filename) filename = DEFAULT_SAVE_FILE;
    
    FILE *f = fopen(filename, "w");
    if (!f) {
        // Could add error reporting here
        return;
    }
    
    // Write header
    fprintf(f, "%d %d\n", g_app.canvas_width, g_app.canvas_height);
    
    // Write canvas data
    for (int y = 0; y < g_app.canvas_height; ++y) {
        for (int x = 0; x < g_app.canvas_width; ++x) {
            Cell *cell = find_spot(x, y);
            if (!cell) continue;
            
            fprintf(f, "%d,%d", (int)cell->color, (int)cell->ch);
            if (x < g_app.canvas_width - 1) {
                fputc(' ', f);
            }
        }
        fputc('\n', f);
    }
    
    fclose(f);
}

/**
 * @brief Load a canvas from a file and overlay onto current canvas
 * @param filename Source filename (NULL uses DEFAULT_SAVE_FILE)
 * 
 * @details
 * Loading behavior:
 * - Reads canvas data from file in custom text format
 * - Overlays loaded data onto current canvas (preserving canvas size)
 * - If loaded canvas is smaller: only overlapping region is affected
 * - If loaded canvas is larger: clipped to current canvas boundaries
 * - Invalid files or read errors are silently ignored
 * - Canvas is automatically re-rendered after successful load
 * 
 * @note Memory allocation failures result in graceful abort
 */
static void load_masterpiece(const char *filename) {
    if (!filename) filename = DEFAULT_SAVE_FILE;
    
    FILE *f = fopen(filename, "r");
    if (!f) return;
    
    int file_width = 0, file_height = 0;
    if (fscanf(f, "%d %d", &file_width, &file_height) != 2 ||
        file_width <= 0 || file_height <= 0 ||
        file_width > MAX_CANVAS_WIDTH || file_height > MAX_CANVAS_HEIGHT) {
        fclose(f);
        return;
    }
    
    // Allocate temporary storage
    size_t temp_size = (size_t)file_width * (size_t)file_height;
    Cell *temp_canvas = malloc(temp_size * sizeof(Cell));
    if (!temp_canvas) {
        fclose(f);
        return;
    }
    
    // Consume rest of header line
    int ch;
    do { 
        ch = fgetc(f); 
    } while (ch != '\n' && ch != EOF);
    
    // Read canvas data
    bool read_success = true;
    for (int y = 0; y < file_height && read_success; ++y) {
        for (int x = 0; x < file_width && read_success; ++x) {
            int color_val = 0, ascii_val = 32;
            
            if (fscanf(f, "%d,%d", &color_val, &ascii_val) != 2) {
                read_success = false;
                break;
            }
            
            // Validate and store data
            Cell *cell = &temp_canvas[y * file_width + x];
            cell->color = (short)((color_val >= 0 && color_val < COLOR_COUNT) ? color_val : 7);
            cell->ch = (unsigned char)((ascii_val >= 0 && ascii_val <= 255) ? ascii_val : ' ');
            
            // Skip whitespace
            sneak_peek_at_file(f);
        }
        
        // Skip to end of line
        do { 
            ch = fgetc(f); 
        } while (ch != '\n' && ch != EOF);
    }
    
    fclose(f);
    
    if (!read_success) {
        free(temp_canvas);
        return;
    }
    
    // Copy overlapping region to main canvas
    int copy_width = (file_width < g_app.canvas_width) ? file_width : g_app.canvas_width;
    int copy_height = (file_height < g_app.canvas_height) ? file_height : g_app.canvas_height;
    
    for (int y = 0; y < copy_height; ++y) {
        for (int x = 0; x < copy_width; ++x) {
            Cell *src = &temp_canvas[y * file_width + x];
            Cell *dst = find_spot(x, y);
            if (dst) {
                *dst = *src;
            }
        }
    }
    
    free(temp_canvas);
    paint_entire_canvas();
}

/*==============================================================================
 * INPUT HANDLING
 *============================================================================*/

/**
 * @brief Process a single key input and update application state
 * @param key Key code from getch()
 * @details 
 * Central input handler that processes all user input including:
 * - Movement commands (arrow keys only)
 * - Painting operations (space, enter for pen mode)
 * - Tool selection (brush, color, eraser)
 * - File operations (save, load)
 * - Application control (quit)
 * 
 * @note Cursor highlighting is automatically managed during state changes
 */
static void input_stuff(int key) {
    // Turn off cursor before state changes
    show_or_hide_cursor(false);
    
    switch (key) {
        // === MOVEMENT CONTROLS ===
        case KEY_UP:
            move_brush(0, -1);
            break;
            
        case KEY_DOWN:
            move_brush(0, 1);
            break;
            
        case KEY_LEFT:
            move_brush(-1, 0);
            break;
            
        case KEY_RIGHT:
            move_brush(1, 0);
            break;
        
        // === PAINTING CONTROLS ===
        case ' ':  // Single paint operation
            paint_stuff();
            break;
            
        case '\n': case '\r':  // Toggle pen mode for continuous painting
            g_app.pen_down = !g_app.pen_down;
            break;
        
        // === TOOL CONTROLS ===
        case 'b': case 'B':  // Cycle through available brushes
            // Restore original brushes when cycling (exits eraser mode)
            memcpy(brush_chars, original_brush_chars, sizeof(brush_chars));
            g_app.brush_index = (g_app.brush_index + 1) % BRUSH_COUNT;
            break;
            
        case 'e': case 'E':  // Enter eraser mode 
            // Restore original brushes first (in case we were in eraser mode)
            memcpy(brush_chars, original_brush_chars, sizeof(brush_chars));
            // Set current brush to space for eraser
            g_app.brush_index = 0;
            brush_chars[0] = ' ';
            break;
            
        case 'c':  // Cycle through color palette (lowercase only)
            g_app.current_color = (short)((g_app.current_color + 1) % COLOR_COUNT);
            break;
            
        case 'x': case 'X':  // Clear entire canvas
            start_with_blank_canvas();
            break;
        
        // === DIRECT COLOR SELECTION ===
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7':
            g_app.current_color = (short)(key - '0');
            break;
        
        // === FILE OPERATIONS ===
        case 's': case 'S':  // Save canvas to file
            save_masterpiece(NULL);
            break;
            
        case 'l': case 'L':  // Load canvas from file
            load_masterpiece(NULL);
            break;
        
        // === APPLICATION CONTROL ===
        case 'q': case 'Q': case 27:  // Quit application (q, Q, or Escape)
            g_app.running = false;
            break;
            
        default:
            // Ignore unrecognized key inputs
            break;
    }
}

/*==============================================================================
 * INITIALIZATION AND CLEANUP
 *============================================================================*/

/**
 * @brief Initialize ncurses color system
 * @return true if colors are available and initialized successfully
 */
static bool setup_palette(void) {
    if (!has_colors()) {
        return false;
    }
    
    if (start_color() == ERR) {
        return false;
    }
    
    // Initialize color pairs (foreground on black background)
    // Pairs 1-8 map to color indices 0-7
    for (short i = 0; i < COLOR_COUNT; ++i) {
        if (init_pair(i + 1, base_colors[i], COLOR_BLACK) == ERR) {
            return false;
        }
    }
    
    return true;
}

/**
 * @brief Adjusts canvas size to fit current terminal window
 */
static void canvas_fit(void) {
    // Calculate available canvas space
    int available_width = COLS;
    int available_height = LINES - (STATUS_LINES_TOP + STATUS_LINES_BOTTOM);
    
    // Clamp to reasonable limits
    g_app.canvas_width = (available_width < MAX_CANVAS_WIDTH) ? 
                        available_width : MAX_CANVAS_WIDTH;
    g_app.canvas_height = (available_height < MAX_CANVAS_HEIGHT) ? 
                         available_height : MAX_CANVAS_HEIGHT;
    
    // Allocate canvas memory
    size_t canvas_size = (size_t)g_app.canvas_width * (size_t)g_app.canvas_height;
    g_app.canvas = malloc(canvas_size * sizeof(Cell));
    
    if (g_app.canvas) {
        // Initialize all cells to empty/white
        for (size_t i = 0; i < canvas_size; ++i) {
            g_app.canvas[i].ch = ' ';
            g_app.canvas[i].color = 7;  // Default to white
        }
    }
    
    // Initialize cursor to center of canvas
    g_app.cursor_x = g_app.canvas_width / 2;
    g_app.cursor_y = g_app.canvas_height / 2;
}

/**
 * @brief Initialize the complete application
 * @return true if initialization successful, false on error
 */
static bool start_stuff(void) {
    // Initialize ncurses
    if (!initscr()) {
        fprintf(stderr, "Error: Failed to initialize ncurses\n");
        return false;
    }
    
    // Configure ncurses settings
    noecho();              // Don't echo typed characters
    cbreak();              // Disable line buffering
    keypad(stdscr, TRUE);  // Enable function keys
    curs_set(0);           // Hide hardware cursor
    timeout(-1);           // Blocking input
    
    // Initialize colors
    setup_palette();  // Non-critical if it fails
    
    // Check minimum terminal size
    if (COLS < 20 || LINES < 10) {
        endwin();
        fprintf(stderr, "Error: Terminal too small (minimum 20x10)\n");
        return false;
    }
    
    // Setup canvas
    canvas_fit();
    if (!g_app.canvas) {
        endwin();
        fprintf(stderr, "Error: Failed to allocate canvas memory\n");
        return false;
    }
    
    // Initialize application state
    g_app.pen_down = false;
    g_app.brush_index = 0;
    g_app.current_color = 7;  // Default to white
    g_app.running = true;
    
    return true;
}

/**
 * @brief Clean up application resources
 */
static void clean_stuff(void) {
    if (g_app.canvas) {
        free(g_app.canvas);
        g_app.canvas = NULL;
    }
    
    endwin();  // Restore terminal
}

/*==============================================================================
 * MAIN APPLICATION LOOP
 *============================================================================*/

/**
 * @brief Main application entry point
 * @return 0 on successful completion, 1 on initialization failure
 * 
 * @details
 * Application lifecycle:
 * 1. Initialize ncurses and application state
 * 2. Validate terminal capabilities and size
 * 3. Allocate and initialize canvas
 * 4. Enter main event loop processing user input
 * 5. Clean up resources and restore terminal
 * 
 * @note All resources are properly cleaned up regardless of exit path
 */
int main(void) {
    // Initialize application subsystems
    if (!start_stuff()) {
        return 1;
    }
    
    // Perform initial screen render
    paint_entire_canvas();
    refresh_view();
    
    // Main event processing loop
    while (g_app.running) {
        int key = getch();
        input_stuff(key);
        refresh_view();
    }
    
    // Clean up and restore terminal state
    clean_stuff();
    return 0;
}