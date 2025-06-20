// termkey.c - Professional keyboard and mouse event monitor
// Enhanced with multi-button mouse support and simultaneous click detection
// Author: Programming Expert | Optimized Professional Version

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/extensions/record.h>

#define MAX_MSG_LEN 256
#define COLOR_LEN 20
#define REPEAT_THRESHOLD_MS 100
#define MAX_MOUSE_BUTTONS 16
#define MULTI_CLICK_TIMEOUT_MS 50
#define COMBINED_MSG_LEN 768  // Increased buffer size for combined messages

// Global state structure
typedef struct {
    Display *display, *record_display;
    int use_color, running;
    char bg_color[COLOR_LEN], fg_color[COLOR_LEN];
    struct { 
        int shift_l, shift_r, ctrl_l, ctrl_r, alt_l, alt_r, 
            meta_l, meta_r, altgr, super_l, super_r; 
    } mods;
    struct {
        int buttons[MAX_MOUSE_BUTTONS];
        int active_count;
        struct timeval last_press_time;
        char combined_message[COMBINED_MSG_LEN];
    } mouse_state;
    KeySym last_key;
    int key_count;
    struct timeval last_key_time;
    char last_message[COMBINED_MSG_LEN];
    XRecordContext context;
    XRecordRange *range;
} AppState;

static AppState app = {0};
static volatile sig_atomic_t exit_requested = 0;

// Color mapping table
static const struct { const char *name, *fg, *bg; } colors[] = {
    {"black", "\033[30m", "\033[40m"}, {"red", "\033[31m", "\033[41m"},
    {"green", "\033[32m", "\033[42m"}, {"yellow", "\033[33m", "\033[43m"},
    {"blue", "\033[34m", "\033[44m"}, {"magenta", "\033[35m", "\033[45m"},
    {"cyan", "\033[36m", "\033[46m"}, {"white", "\033[37m", "\033[47m"},
    {"default", "\033[39m", "\033[49m"}, {NULL, NULL, NULL}
};

// Special key mapping
static const struct { KeySym sym; const char *name; } special_keys[] = {
    {XK_Shift_L, "SHIFT_L"}, {XK_Shift_R, "SHIFT_R"}, 
    {XK_Control_L, "CONTROL_L"}, {XK_Control_R, "CONTROL_R"},
    {XK_Alt_L, "ALT_L"}, {XK_Alt_R, "ALT_R"}, 
    {XK_Meta_L, "META_L"}, {XK_Meta_R, "META_R"},
    {XK_ISO_Level3_Shift, "ALTGR"}, {XK_Super_L, "SUPER_L"}, {XK_Super_R, "SUPER_R"},
    {XK_apostrophe, "APOSTROPHE (')"}, {XK_slash, "SLASH (/)"}, 
    {XK_backslash, "BACKSLASH (\\)"}, {XK_Left, "ARROW LEFT"}, 
    {XK_Right, "ARROW RIGHT"}, {XK_Up, "ARROW UP"}, {XK_Down, "ARROW DOWN"},
    {XK_KP_Divide, "KP_DIVIDE (/)"}, {XK_KP_Multiply, "KP_MULTIPLY (*)"}, 
    {XK_KP_Subtract, "KP_SUBTRACT (-)"}, {XK_KP_Add, "KP_ADD (+)"}, 
    {XK_bracketleft, "BRACKETLEFT ([)"}, {XK_bracketright, "BRACKETRIGHT (])"},
    {XK_comma, "COMMA (,)"}, {XK_period, "PERIOD (.)"}, 
    {XK_dead_acute, "DEAD_ACUTE (´)"}, {XK_dead_tilde, "DEAD_TILDE (~)"}, 
    {XK_dead_cedilla, "DEAD_CEDILLA (Ç)"}, {XK_minus, "MINUS (-)"}, 
    {XK_equal, "EQUAL (=)"}, {XK_semicolon, "SEMICOLON (;)"}, 
    {XK_Page_Up, "PAGE UP"}, {XK_Page_Down, "PAGE DOWN"}, 
    {XK_Home, "HOME"}, {XK_End, "END"}, {0, NULL}
};

// Function prototypes
static void cleanup_and_exit(int sig);
static void cleanup_resources(void);
static const char* get_color_code(const char *name, int is_bg);
static void get_terminal_size(int *rows, int *cols);
static void print_centered(const char *msg);
static const char* mouse_button_name(int btn);
static void handle_mouse_button_press(int button);
static void handle_mouse_button_release(int button);
static void update_mouse_display(void);
static int is_mouse_multi_click_timeout(void);
static const char* keysym_to_name(KeySym sym);
static void update_modifiers(KeySym sym, int pressed);
static long get_time_diff_ms(struct timeval *start, struct timeval *end);
static void event_callback(XPointer priv, XRecordInterceptData *data);
static void print_usage(const char *prog);
static int parse_args(int argc, char *argv[]);
static int validate_color(const char *color);
static void safe_string_append(char *dest, size_t dest_size, const char *src);

// Signal handler for clean exit
static void cleanup_and_exit(int sig) {
    exit_requested = 1;
    app.running = 0;
    
    // Use async-signal-safe functions only
    const char reset[] = "\033c\033[0m\033[?25h\033[2J\033[H";
    write(STDOUT_FILENO, reset, sizeof(reset) - 1);
    
    _exit(0);
}

// Cleanup resources properly
static void cleanup_resources(void) {
    if (app.context && app.record_display) {
        XRecordDisableContext(app.record_display, app.context);
        XSync(app.record_display, False);
        XRecordFreeContext(app.record_display, app.context);
        app.context = 0;
    }
    
    if (app.range) {
        XFree(app.range);
        app.range = NULL;
    }
    
    if (app.record_display) {
        XCloseDisplay(app.record_display);
        app.record_display = NULL;
    }
    
    if (app.display) {
        XCloseDisplay(app.display);
        app.display = NULL;
    }
}

// Safe string append function to prevent buffer overflow
static void safe_string_append(char *dest, size_t dest_size, const char *src) {
    if (!dest || !src || dest_size <= 1) return;
    
    size_t dest_len = strnlen(dest, dest_size - 1);
    size_t remaining = dest_size - dest_len - 1;
    
    if (remaining > 0) {
        strncat(dest, src, remaining);
        dest[dest_size - 1] = '\0';
    }
}

// Validate color name
static int validate_color(const char *color) {
    if (!color || strlen(color) == 0 || strcmp(color, "default") == 0) return 1;
    
    for (int i = 0; colors[i].name; i++) {
        if (strcmp(color, colors[i].name) == 0) return 1;
    }
    return 0;
}

// Get ANSI color code
static const char* get_color_code(const char *name, int is_bg) {
    if (!name || strcmp(name, "") == 0 || strcmp(name, "default") == 0) {
        return is_bg ? colors[8].bg : colors[8].fg;
    }
    
    for (int i = 0; colors[i].name; i++) {
        if (strcmp(name, colors[i].name) == 0) {
            return is_bg ? colors[i].bg : colors[i].fg;
        }
    }
    return is_bg ? colors[8].bg : colors[8].fg;
}

// Get terminal dimensions
static void get_terminal_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1 && ws.ws_row > 0 && ws.ws_col > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24; *cols = 80;
    }
}

// Calculate time difference in milliseconds
static long get_time_diff_ms(struct timeval *start, struct timeval *end) {
    long sec_diff = end->tv_sec - start->tv_sec;
    long usec_diff = end->tv_usec - start->tv_usec;
    
    if (sec_diff > 1000) return 1000000;
    return sec_diff * 1000 + usec_diff / 1000;
}

// Print centered message
static void print_centered(const char *msg) {
    if (!msg || exit_requested) return;
    
    int rows, cols, len = strlen(msg);
    get_terminal_size(&rows, &cols);
    
    int row_pos = rows > 0 ? rows / 2 : 1;
    int col_pos = (cols > len) ? (cols - len) / 2 + 1 : 1;
    
    printf("\033[H\033[J\033[%d;%dH", row_pos, col_pos);
    
    if (app.use_color) {
        printf("%s%s%s\033[0m", 
               get_color_code(app.bg_color, 1),
               get_color_code(app.fg_color, 0),
               msg);
    } else {
        printf("%s", msg);
    }
    
    printf("\n");
    fflush(stdout);
}

// Enhanced mouse button names with support for 5+ button mice
static const char* mouse_button_name(int btn) {
    switch (btn) {
        case Button1: return "CLICK LEFT";
        case Button2: return "CLICK MIDDLE";
        case Button3: return "CLICK RIGHT";
        case Button4: return "WHEEL UP";
        case Button5: return "WHEEL DOWN";
        case 6: return "CLICK BUTTON 6";
        case 7: return "CLICK BUTTON 7";
        case 8: return "CLICK BUTTON 8";
        case 9: return "CLICK BUTTON 9";
        case 10: return "CLICK BUTTON 10";
        case 11: return "CLICK BUTTON 11";
        case 12: return "CLICK BUTTON 12";
        case 13: return "CLICK BUTTON 13";
        case 14: return "CLICK BUTTON 14";
        case 15: return "CLICK BUTTON 15";
        default: return "CLICK UNKNOWN";
    }
}

// Check if we're still within multi-click timeout
static int is_mouse_multi_click_timeout(void) {
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    long time_diff = get_time_diff_ms(&app.mouse_state.last_press_time, &current_time);
    return time_diff <= MULTI_CLICK_TIMEOUT_MS;
}

// Handle mouse button press with multi-button support
static void handle_mouse_button_press(int button) {
    if (button < 1 || button >= MAX_MOUSE_BUTTONS) return;
    
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    
    // If this is a new button press outside timeout, reset state
    if (!is_mouse_multi_click_timeout() && app.mouse_state.active_count == 0) {
        memset(app.mouse_state.buttons, 0, sizeof(app.mouse_state.buttons));
        app.mouse_state.active_count = 0;
        app.mouse_state.combined_message[0] = '\0';
    }
    
    // Mark button as pressed if not already
    if (!app.mouse_state.buttons[button]) {
        app.mouse_state.buttons[button] = 1;
        app.mouse_state.active_count++;
        app.mouse_state.last_press_time = current_time;
    }
    
    update_mouse_display();
}

// Handle mouse button release
static void handle_mouse_button_release(int button) {
    if (button < 1 || button >= MAX_MOUSE_BUTTONS) return;
    
    if (app.mouse_state.buttons[button]) {
        app.mouse_state.buttons[button] = 0;
        app.mouse_state.active_count--;
        
        if (app.mouse_state.active_count <= 0) {
            app.mouse_state.active_count = 0;
            memset(app.mouse_state.buttons, 0, sizeof(app.mouse_state.buttons));
        }
    }
}

// Update mouse display with combined button presses
static void update_mouse_display(void) {
    if (app.mouse_state.active_count == 0) return;
    
    char message[COMBINED_MSG_LEN] = "";
    int first = 1;
    
    // Build combined message for simultaneous button presses
    for (int i = 1; i < MAX_MOUSE_BUTTONS; i++) {
        if (app.mouse_state.buttons[i]) {
            const char *btn_name = mouse_button_name(i);
            
            if (!first) {
                safe_string_append(message, sizeof(message), " + ");
            }
            
            safe_string_append(message, sizeof(message), btn_name);
            first = 0;
        }
    }
    
    // Store and display the combined message
    strncpy(app.mouse_state.combined_message, message, sizeof(app.mouse_state.combined_message) - 1);
    app.mouse_state.combined_message[sizeof(app.mouse_state.combined_message) - 1] = '\0';
    
    print_centered(message);
    
    // Reset key state when mouse is active
    app.last_key = 0;
    app.key_count = 0;
}

// Convert KeySym to name
static const char* keysym_to_name(KeySym sym) {
    for (int i = 0; special_keys[i].name; i++) {
        if (special_keys[i].sym == sym) return special_keys[i].name;
    }
    
    const char *name = XKeysymToString(sym);
    return name ? name : "UNKNOWN";
}

// Update modifier states
static void update_modifiers(KeySym sym, int pressed) {
    switch (sym) {
        case XK_Shift_L: app.mods.shift_l = pressed; break;
        case XK_Shift_R: app.mods.shift_r = pressed; break;
        case XK_Control_L: app.mods.ctrl_l = pressed; break;
        case XK_Control_R: app.mods.ctrl_r = pressed; break;
        case XK_Alt_L: app.mods.alt_l = pressed; break;
        case XK_Alt_R: app.mods.alt_r = pressed; break;
        case XK_Meta_L: app.mods.meta_l = pressed; break;
        case XK_Meta_R: app.mods.meta_r = pressed; break;
        case XK_ISO_Level3_Shift: app.mods.altgr = pressed; break;
        case XK_Super_L: app.mods.super_l = pressed; break;
        case XK_Super_R: app.mods.super_r = pressed; break;
    }
}

// X11 event callback with enhanced mouse handling
static void event_callback(XPointer priv, XRecordInterceptData *data) {
    if (!data || data->category != XRecordFromServer || !data->data || exit_requested) {
        if (data) XRecordFreeData(data);
        return;
    }
    
    xEvent *event = (xEvent *)data->data;
    int event_type = event->u.u.type & 0x7F;
    char message[COMBINED_MSG_LEN] = "";
    
    if (event_type == ButtonPress) {
        handle_mouse_button_press(event->u.u.detail);
    } else if (event_type == ButtonRelease) {
        handle_mouse_button_release(event->u.u.detail);
    } else if (event_type == KeyPress) {
        KeySym sym = XkbKeycodeToKeysym(app.display, event->u.u.detail, 0, 0);
        update_modifiers(sym, 1);
        
        const char *key_name = keysym_to_name(sym);
        if (!key_name || strlen(key_name) == 0) {
            XRecordFreeData(data);
            return;
        }
        
        char upper_key[MAX_MSG_LEN], mod_msg[COMBINED_MSG_LEN / 2] = "";
        strncpy(upper_key, key_name, sizeof(upper_key) - 1);
        upper_key[sizeof(upper_key) - 1] = '\0';
        
        // Convert to uppercase
        for (int i = 0; upper_key[i] && i < sizeof(upper_key) - 1; i++) {
            upper_key[i] = toupper((unsigned char)upper_key[i]);
        }
        
        int is_modifier = (sym == XK_Shift_L || sym == XK_Shift_R || sym == XK_Control_L || 
                          sym == XK_Control_R || sym == XK_Alt_L || sym == XK_Alt_R ||
                          sym == XK_Meta_L || sym == XK_Meta_R || sym == XK_ISO_Level3_Shift ||
                          sym == XK_Super_L || sym == XK_Super_R);
        
        // Build modifier string using safe append
        if (app.mods.ctrl_l && sym != XK_Control_L) {
            safe_string_append(mod_msg, sizeof(mod_msg), "CONTROL_L + ");
        }
        if (app.mods.ctrl_r && sym != XK_Control_R) {
            safe_string_append(mod_msg, sizeof(mod_msg), "CONTROL_R + ");
        }
        if (app.mods.alt_l && sym != XK_Alt_L) {
            safe_string_append(mod_msg, sizeof(mod_msg), "ALT_L + ");
        }
        if (app.mods.alt_r && sym != XK_Alt_R) {
            safe_string_append(mod_msg, sizeof(mod_msg), "ALT_R + ");
        }
        if (app.mods.shift_l && sym != XK_Shift_L) {
            safe_string_append(mod_msg, sizeof(mod_msg), "SHIFT_L + ");
        }
        if (app.mods.shift_r && sym != XK_Shift_R) {
            safe_string_append(mod_msg, sizeof(mod_msg), "SHIFT_R + ");
        }
        if (app.mods.meta_l && sym != XK_Meta_L) {
            safe_string_append(mod_msg, sizeof(mod_msg), "META_L + ");
        }
        if (app.mods.meta_r && sym != XK_Meta_R) {
            safe_string_append(mod_msg, sizeof(mod_msg), "META_R + ");
        }
        if (app.mods.altgr && sym != XK_ISO_Level3_Shift) {
            safe_string_append(mod_msg, sizeof(mod_msg), "ALTGR + ");
        }
        if (app.mods.super_l && sym != XK_Super_L) {
            safe_string_append(mod_msg, sizeof(mod_msg), "SUPER_L + ");
        }
        if (app.mods.super_r && sym != XK_Super_R) {
            safe_string_append(mod_msg, sizeof(mod_msg), "SUPER_R + ");
        }
        
        // Add main key
        safe_string_append(mod_msg, sizeof(mod_msg), upper_key);
        
        // Format final message - enhanced with mouse combination support
        if (app.mouse_state.active_count > 0) {
            // Safely combine mouse and keyboard messages
            size_t mouse_len = strlen(app.mouse_state.combined_message);
            size_t mod_len = strlen(mod_msg);
            
            if (mouse_len > 0 && mod_len > 0 && 
                mouse_len + mod_len + 4 < sizeof(message)) {
                strcpy(message, app.mouse_state.combined_message);
                safe_string_append(message, sizeof(message), " + ");
                safe_string_append(message, sizeof(message), mod_msg);
            } else if (mouse_len > 0) {
                strncpy(message, app.mouse_state.combined_message, sizeof(message) - 1);
                message[sizeof(message) - 1] = '\0';
            } else {
                strncpy(message, mod_msg, sizeof(message) - 1);
                message[sizeof(message) - 1] = '\0';
            }
        } else {
            strncpy(message, mod_msg, sizeof(message) - 1);
            message[sizeof(message) - 1] = '\0';
        }
        
        // Key repeat logic
        struct timeval current_time;
        gettimeofday(&current_time, NULL);
        long time_diff = get_time_diff_ms(&app.last_key_time, &current_time);
        
        if (sym == app.last_key && strcmp(message, app.last_message) == 0 && 
            !is_modifier && time_diff > REPEAT_THRESHOLD_MS) {
            app.key_count++;
            char counted_msg[COMBINED_MSG_LEN + 32];
            size_t msg_len = strlen(message);
            
            // Safe formatting for repeat count
            if (msg_len + 32 < sizeof(counted_msg)) {
                snprintf(counted_msg, sizeof(counted_msg), "%s [x%d]", message, app.key_count);
                print_centered(counted_msg);
            } else {
                print_centered(message);
            }
            app.last_key_time = current_time;
        } else if (time_diff > REPEAT_THRESHOLD_MS || sym != app.last_key || 
                   strcmp(message, app.last_message) != 0) {
            app.last_key = sym;
            app.key_count = 1;
            app.last_key_time = current_time;
            strncpy(app.last_message, message, sizeof(app.last_message) - 1);
            app.last_message[sizeof(app.last_message) - 1] = '\0';
            print_centered(message);
        }
        
    } else if (event_type == KeyRelease) {
        KeySym sym = XkbKeycodeToKeysym(app.display, event->u.u.detail, 0, 0);
        update_modifiers(sym, 0);
    }
    
    XRecordFreeData(data);
}

// Usage information
static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("Monitor and display keyboard/mouse events in terminal center\n\n");
    printf("Options:\n");
    printf("  -c, --color           Enable color mode\n");
    printf("      --bg=COLOR        Set background color\n");
    printf("      --fg=COLOR        Set foreground color\n");
    printf("  -h, --help            Show help\n\n");
    printf("Colors: black, red, green, yellow, blue, magenta, cyan, white, default\n\n");
    printf("Features:\n");
    printf("  - Simultaneous mouse button detection\n");
    printf("  - Extended mouse support (up to 15 buttons)\n");
    printf("  - Key combination and modifier display\n");
    printf("  - Color customization\n\n");
    printf("Examples:\n");
    printf("  %s -c --fg=green\n", prog);
    printf("  %s -c --bg=black --fg=cyan\n", prog);
    exit(EXIT_SUCCESS);
}

// Parse arguments
static int parse_args(int argc, char *argv[]) {
    strncpy(app.bg_color, "default", COLOR_LEN - 1);
    strncpy(app.fg_color, "default", COLOR_LEN - 1);
    app.bg_color[COLOR_LEN - 1] = '\0';
    app.fg_color[COLOR_LEN - 1] = '\0';
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--color") == 0) {
            app.use_color = 1;
        } else if (strncmp(argv[i], "--bg=", 5) == 0) {
            app.use_color = 1;
            const char *color = argv[i] + 5;
            if (!validate_color(color)) {
                fprintf(stderr, "Error: Invalid background color '%s'\n", color);
                return 0;
            }
            strncpy(app.bg_color, color, COLOR_LEN - 1);
            app.bg_color[COLOR_LEN - 1] = '\0';
        } else if (strncmp(argv[i], "--fg=", 5) == 0) {
            app.use_color = 1;
            const char *color = argv[i] + 5;
            if (!validate_color(color)) {
                fprintf(stderr, "Error: Invalid foreground color '%s'\n", color);
                return 0;
            }
            strncpy(app.fg_color, color, COLOR_LEN - 1);
            app.fg_color[COLOR_LEN - 1] = '\0';
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
        } else {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            return 0;
        }
    }
    return 1;
}

// Main function
int main(int argc, char *argv[]) {
    // Setup signal handlers
    signal(SIGINT, cleanup_and_exit);
    signal(SIGTERM, cleanup_and_exit);
    signal(SIGQUIT, cleanup_and_exit);
    signal(SIGHUP, cleanup_and_exit);
    
    if (!parse_args(argc, argv)) {
        return EXIT_FAILURE;
    }
    
    app.running = 1;
    
    // Initialize mouse state
    memset(&app.mouse_state, 0, sizeof(app.mouse_state));
    
    // Initialize terminal
    printf("\033[?25l\033[2J\033[H");
    fflush(stdout);
    
    // Initialize X11
    app.display = XOpenDisplay(NULL);
    app.record_display = XOpenDisplay(NULL);
    if (!app.display || !app.record_display) {
        fprintf(stderr, "Error: Cannot open X display\n");
        printf("\033[?25h");
        return EXIT_FAILURE;
    }
    
    // Check XRecord extension
    int major, minor;
    if (!XRecordQueryVersion(app.record_display, &major, &minor)) {
        fprintf(stderr, "Error: XRecord extension not available\n");
        cleanup_resources();
        printf("\033[?25h");
        return EXIT_FAILURE;
    }
    
    // Setup recording
    app.range = XRecordAllocRange();
    if (!app.range) {
        fprintf(stderr, "Error: Cannot allocate X record range\n");
        cleanup_resources();
        printf("\033[?25h");
        return EXIT_FAILURE;
    }
    
    app.range->device_events.first = KeyPress;
    app.range->device_events.last = ButtonRelease;
    XRecordClientSpec clients = XRecordAllClients;
    
    app.context = XRecordCreateContext(app.record_display, 0, &clients, 1, &app.range, 1);
    if (!app.context) {
        fprintf(stderr, "Error: Cannot create X record context\n");
        cleanup_resources();
        printf("\033[?25h");
        return EXIT_FAILURE;
    }
    
    if (!XRecordEnableContextAsync(app.record_display, app.context, event_callback, NULL)) {
        fprintf(stderr, "Error: Cannot enable X record context\n");
        cleanup_resources();
        printf("\033[?25h");
        return EXIT_FAILURE;
    }
    
    print_centered("Termkey - Professional Edition v2.0");
    
    // Main event loop
    while (app.running && !exit_requested) {
        XRecordProcessReplies(app.record_display);
        usleep(10000);
    }
    
    // Clean exit
    printf("\033[?25h\033[0m\033[2J\033[H");
    cleanup_resources();
    return EXIT_SUCCESS;
}
