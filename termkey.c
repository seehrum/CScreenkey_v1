// termkey.c - Professional keyboard and mouse event monitor
// Displays keyboard and mouse events centered on screen with color support
// Author: Programming Expert | Optimized Professional Version

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/extensions/record.h>

#define MAX_MSG_LEN 256
#define COLOR_LEN 20

// Global state structure
typedef struct {
    Display *display, *record_display;
    int use_color, mouse_pressed, color_toggle;
    char bg_color[COLOR_LEN], fg_color[COLOR_LEN], text_color[COLOR_LEN];
    struct { int shift_l, shift_r, ctrl_l, ctrl_r, alt_l, alt_r, meta_l, meta_r, altgr, super_l, super_r; } mods;
} AppState;

static AppState app = {0};

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
    {XK_Shift_L, "SHIFT_L"}, {XK_Shift_R, "SHIFT_R"}, {XK_Control_L, "CONTROL_L"}, {XK_Control_R, "CONTROL_R"},
    {XK_Alt_L, "ALT_L"}, {XK_Alt_R, "ALT_R"}, {XK_Meta_L, "META_L"}, {XK_Meta_R, "META_R"},
    {XK_ISO_Level3_Shift, "ALTGR"}, {XK_Super_L, "SUPER_L"}, {XK_Super_R, "SUPER_R"},
    {XK_apostrophe, "APOSTROPHE (')"}, {XK_slash, "SLASH (/)"}, {XK_backslash, "BACKSLASH (\\)"},
    {XK_Left, "ARROW LEFT"}, {XK_Right, "ARROW RIGHT"}, {XK_Up, "ARROW UP"}, {XK_Down, "ARROW DOWN"},
    {XK_KP_Divide, "KP_DIVIDE (/)"}, {XK_KP_Multiply, "KP_MULTIPLY (*)"}, {XK_KP_Subtract, "KP_SUBTRACT (-)"},
    {XK_KP_Add, "KP_ADD (+)"}, {XK_bracketleft, "BRACKETLEFT ([)"}, {XK_bracketright, "BRACKETRIGHT (])"},
    {XK_comma, "COMMA (,)"}, {XK_period, "PERIOD (.)"}, {XK_dead_acute, "DEAD_ACUTE (´)"},
    {XK_dead_tilde, "DEAD_TILDE (~)"}, {XK_dead_cedilla, "DEAD_CEDILLA (Ç)"}, {XK_minus, "MINUS (-)"},
    {XK_equal, "EQUAL (=)"}, {XK_semicolon, "SEMICOLON (;)"}, {XK_Page_Up, "PAGE UP"},
    {XK_Page_Down, "PAGE DOWN"}, {XK_Home, "HOME"}, {XK_End, "END"}, {0, NULL}
};

// Function prototypes
static void cleanup_and_exit(int sig);
static const char* get_color_code(const char *name, int is_bg);
static void get_terminal_size(int *rows, int *cols);
static void print_centered(const char *msg);
static const char* mouse_button_name(int btn);
static const char* keysym_to_name(KeySym sym);
static void update_modifiers(KeySym sym, int pressed);
static void event_callback(XPointer priv, XRecordInterceptData *data);
static void print_usage(const char *prog);
static int parse_args(int argc, char *argv[]);

// Signal handler for clean exit
static void cleanup_and_exit(int sig) {
    const char reset[] = "\033c\033[0m\033[?25h\033[2J\033[H";
    if (write(STDOUT_FILENO, reset, sizeof(reset) - 1) == -1) {
        write(STDOUT_FILENO, "\033[0m\033[?25h", 10);
    }
    _exit(0);
}

// Get ANSI color code for given color name
static const char* get_color_code(const char *name, int is_bg) {
    if (!name || strcmp(name, "") == 0) return NULL;
    for (int i = 0; colors[i].name; i++) {
        if (strcmp(name, colors[i].name) == 0) {
            return is_bg ? colors[i].bg : colors[i].fg;
        }
    }
    return NULL;
}

// Get current terminal dimensions
static void get_terminal_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24; *cols = 80; // Fallback defaults
    }
}

// Print message centered on screen with optional colors
static void print_centered(const char *msg) {
    int rows, cols, len = strlen(msg);
    get_terminal_size(&rows, &cols);
    
    printf("\033[H\033[J\033[%d;%dH", rows / 2, (cols - len) / 2 + 1);
    
    if (app.use_color) {
        const char *bg = app.color_toggle ? get_color_code(app.fg_color, 1) : get_color_code(app.bg_color, 1);
        const char *fg = app.color_toggle ? get_color_code(app.bg_color, 0) : get_color_code(app.fg_color, 0);
        const char *txt = get_color_code(app.text_color, 0);
        
        if (bg) printf("%s", bg);
        
        for (int i = 0; i < len; i++) {
            if (isgraph((unsigned char)msg[i]) && txt) {
                printf("%s%c", txt, msg[i]);
            } else {
                printf("%s%c", fg ? fg : "\033[39m", msg[i]);
            }
        }
        
        app.color_toggle = !app.color_toggle;
        printf("\033[0m");
    } else {
        printf("%s", msg);
    }
    
    printf("\n");
    fflush(stdout);
}

// Convert mouse button number to descriptive name
static const char* mouse_button_name(int btn) {
    switch (btn) {
        case Button1: return "LEFT CLICK";
        case Button2: return "MIDDLE CLICK";
        case Button3: return "RIGHT CLICK";
        case Button4: return "WHEEL UP";
        case Button5: return "WHEEL DOWN";
        default: return "UNKNOWN BUTTON";
    }
}

// Convert KeySym to human-readable name
static const char* keysym_to_name(KeySym sym) {
    for (int i = 0; special_keys[i].name; i++) {
        if (special_keys[i].sym == sym) return special_keys[i].name;
    }
    return XKeysymToString(sym);
}

// Update modifier key states
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

// X11 event callback - processes keyboard and mouse events
static void event_callback(XPointer priv, XRecordInterceptData *data) {
    if (data->category != XRecordFromServer || !data->data) {
        XRecordFreeData(data);
        return;
    }
    
    xEvent *event = (xEvent *)data->data;
    int event_type = event->u.u.type & 0x7F;
    char message[MAX_MSG_LEN * 2] = "";
    
    if (event_type == ButtonPress) {
        app.mouse_pressed = event->u.u.detail;
        print_centered(mouse_button_name(app.mouse_pressed));
    } else if (event_type == ButtonRelease) {
        app.mouse_pressed = 0;
    } else if (event_type == KeyPress) {
        KeySym sym = XkbKeycodeToKeysym(app.display, event->u.u.detail, 0, 0);
        update_modifiers(sym, 1);
        
        const char *key_name = keysym_to_name(sym);
        if (!key_name) {
            XRecordFreeData(data);
            return;
        }
        
        char upper_key[MAX_MSG_LEN], mod_msg[MAX_MSG_LEN] = "";
        strncpy(upper_key, key_name, sizeof(upper_key) - 1);
        upper_key[sizeof(upper_key) - 1] = '\0';
        for (int i = 0; upper_key[i]; i++) upper_key[i] = toupper((unsigned char)upper_key[i]);
        
        int is_modifier = (sym == XK_Shift_L || sym == XK_Shift_R || sym == XK_Control_L || 
                          sym == XK_Control_R || sym == XK_Alt_L || sym == XK_Alt_R ||
                          sym == XK_Meta_L || sym == XK_Meta_R || sym == XK_ISO_Level3_Shift ||
                          sym == XK_Super_L || sym == XK_Super_R);
        
        // Build modifier combination string
        if (app.mods.ctrl_l && sym != XK_Control_L) strncat(mod_msg, "CONTROL_L + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
        if (app.mods.ctrl_r && sym != XK_Control_R) strncat(mod_msg, "CONTROL_R + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
        if (app.mods.alt_l && sym != XK_Alt_L) strncat(mod_msg, "ALT_L + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
        if (app.mods.alt_r && sym != XK_Alt_R) strncat(mod_msg, "ALT_R + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
        if (app.mods.shift_l && sym != XK_Shift_L) strncat(mod_msg, "SHIFT_L + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
        if (app.mods.shift_r && sym != XK_Shift_R) strncat(mod_msg, "SHIFT_R + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
        if (app.mods.meta_l && sym != XK_Meta_L) strncat(mod_msg, "META_L + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
        if (app.mods.meta_r && sym != XK_Meta_R) strncat(mod_msg, "META_R + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
        if (app.mods.altgr && sym != XK_ISO_Level3_Shift) strncat(mod_msg, "ALTGR + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
        if (app.mods.super_l && sym != XK_Super_L) strncat(mod_msg, "SUPER_L + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
        if (app.mods.super_r && sym != XK_Super_R) strncat(mod_msg, "SUPER_R + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
        
        if (!(is_modifier && mod_msg[0] == '\0')) {
            strncat(mod_msg, upper_key, sizeof(mod_msg) - strlen(mod_msg) - 1);
        } else {
            strncpy(mod_msg, upper_key, sizeof(mod_msg) - 1);
            mod_msg[sizeof(mod_msg) - 1] = '\0';
        }
        
        if (app.mouse_pressed) {
            snprintf(message, sizeof(message), "%s + %s", mouse_button_name(app.mouse_pressed), mod_msg);
        } else {
            strncpy(message, mod_msg, sizeof(message) - 1);
            message[sizeof(message) - 1] = '\0';
        }
        
        print_centered(message);
    } else if (event_type == KeyRelease) {
        KeySym sym = XkbKeycodeToKeysym(app.display, event->u.u.detail, 0, 0);
        update_modifiers(sym, 0);
    }
    
    XRecordFreeData(data);
}

// Display usage information
static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("Monitor and display keyboard/mouse events in terminal center\n\n");
    printf("Options:\n");
    printf("  -c, --color           Enable color mode with following options:\n");
    printf("      --bg=COLOR        Set background color\n");
    printf("      --fg=COLOR        Set foreground color\n");
    printf("      --text=COLOR      Set text color for printable characters\n");
    printf("  -h, --help            Show this help message\n\n");
    printf("Available colors: black, red, green, yellow, blue, magenta, cyan, white, default\n\n");
    printf("Examples:\n");
    printf("  %s -c --text=green                    # Green text only\n", prog);
    printf("  %s -c --bg=black --text=cyan          # Black background, cyan text\n", prog);
    printf("  %s -c --bg=red --fg=white --text=blue # Full color customization\n", prog);
    exit(EXIT_SUCCESS);
}

// Parse command line arguments
static int parse_args(int argc, char *argv[]) {
    strcpy(app.bg_color, "default");
    strcpy(app.fg_color, "default");
    strcpy(app.text_color, "");
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--color") == 0) {
            app.use_color = 1;
            if (++i >= argc) { print_usage(argv[0]); return 0; }
            
            while (i < argc) {
                if (strncmp(argv[i], "--bg=", 5) == 0) {
                    strncpy(app.bg_color, argv[i] + 5, COLOR_LEN - 1);
                } else if (strncmp(argv[i], "--fg=", 5) == 0) {
                    strncpy(app.fg_color, argv[i] + 5, COLOR_LEN - 1);
                } else if (strncmp(argv[i], "--text=", 7) == 0) {
                    strncpy(app.text_color, argv[i] + 7, COLOR_LEN - 1);
                } else { i--; break; }
                i++;
            }
            
            // Validate colors
            if ((strcmp(app.bg_color, "default") != 0 && !get_color_code(app.bg_color, 1)) ||
                (strcmp(app.fg_color, "default") != 0 && !get_color_code(app.fg_color, 0)) ||
                (strlen(app.text_color) > 0 && strcmp(app.text_color, "default") != 0 && !get_color_code(app.text_color, 0))) {
                fprintf(stderr, "Error: Invalid color name provided\n");
                return 0;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
        } else {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
        }
    }
    return 1;
}

// Main function - initialize X11 and start event monitoring
int main(int argc, char *argv[]) {
    // Setup signal handlers for clean exit
    signal(SIGINT, cleanup_and_exit);
    signal(SIGTERM, cleanup_and_exit);
    signal(SIGQUIT, cleanup_and_exit);
    signal(SIGHUP, cleanup_and_exit);
    
    if (!parse_args(argc, argv)) return EXIT_FAILURE;
    
    // Hide cursor and initialize X11
    printf("\033[?25l");
    fflush(stdout);
    
    app.display = XOpenDisplay(NULL);
    app.record_display = XOpenDisplay(NULL);
    if (!app.display || !app.record_display) {
        fprintf(stderr, "Error: Cannot open X display\n");
        printf("\033[?25h"); // Restore cursor
        return EXIT_FAILURE;
    }
    
    // Setup X11 event recording
    XRecordRange *range = XRecordAllocRange();
    if (!range) {
        fprintf(stderr, "Error: Cannot allocate X record range\n");
        XCloseDisplay(app.display);
        XCloseDisplay(app.record_display);
        printf("\033[?25h");
        return EXIT_FAILURE;
    }
    
    range->device_events.first = KeyPress;
    range->device_events.last = ButtonRelease;
    XRecordClientSpec clients = XRecordAllClients;
    
    XRecordContext context = XRecordCreateContext(app.record_display, 0, &clients, 1, &range, 1);
    if (!context) {
        fprintf(stderr, "Error: Cannot create X record context\n");
        XFree(range);
        XCloseDisplay(app.display);
        XCloseDisplay(app.record_display);
        printf("\033[?25h");
        return EXIT_FAILURE;
    }
    
    if (!XRecordEnableContextAsync(app.record_display, context, event_callback, NULL)) {
        fprintf(stderr, "Error: Cannot enable X record context\n");
        XRecordFreeContext(app.record_display, context);
        XFree(range);
        XCloseDisplay(app.display);
        XCloseDisplay(app.record_display);
        printf("\033[?25h");
        return EXIT_FAILURE;
    }
    
    print_centered("Termkey - Professional Edition");
    
    // Main event loop
    while (1) {
        XRecordProcessReplies(app.record_display);
        usleep(10000); // Prevent high CPU usage
    }
    
    return 0; // Unreachable due to signal handlers
}
