// termkey_win.c - Professional keyboard and mouse event monitor for Windows
// Displays keyboard and mouse events centered on screen with color support
// Compatible with Windows 7, 8, 10, 11 (32-bit and 64-bit)
// Author: Programming Expert | Windows Professional Version

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define MAX_MSG_LEN 256
#define COLOR_LEN 20

// Global state structure
typedef struct {
    int use_color, mouse_pressed, color_toggle;
    char bg_color[COLOR_LEN], fg_color[COLOR_LEN], text_color[COLOR_LEN];
    HANDLE console_handle;
    CONSOLE_SCREEN_BUFFER_INFO console_info;
    struct { int shift_l, shift_r, ctrl_l, ctrl_r, alt_l, alt_r, win_l, win_r; } mods;
} AppState;

static AppState app = {0};
static HHOOK kb_hook = NULL, mouse_hook = NULL;
static volatile int running = 1;

// Color mapping table
static const struct { const char *name; WORD color; } colors[] = {
    {"black", 0}, {"red", FOREGROUND_RED}, {"green", FOREGROUND_GREEN}, 
    {"yellow", FOREGROUND_RED | FOREGROUND_GREEN}, {"blue", FOREGROUND_BLUE},
    {"magenta", FOREGROUND_RED | FOREGROUND_BLUE}, {"cyan", FOREGROUND_GREEN | FOREGROUND_BLUE},
    {"white", FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE},
    {"default", FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE}, {NULL, 0}
};

// Virtual key to name mapping
static const struct { int vk; const char *name; } special_keys[] = {
    {VK_LSHIFT, "SHIFT_L"}, {VK_RSHIFT, "SHIFT_R"}, {VK_LCONTROL, "CONTROL_L"}, {VK_RCONTROL, "CONTROL_R"},
    {VK_LMENU, "ALT_L"}, {VK_RMENU, "ALT_R"}, {VK_LWIN, "WIN_L"}, {VK_RWIN, "WIN_R"},
    {VK_LEFT, "ARROW LEFT"}, {VK_RIGHT, "ARROW RIGHT"}, {VK_UP, "ARROW UP"}, {VK_DOWN, "ARROW DOWN"},
    {VK_DIVIDE, "KP_DIVIDE (/)"}, {VK_MULTIPLY, "KP_MULTIPLY (*)"}, {VK_SUBTRACT, "KP_SUBTRACT (-)"},
    {VK_ADD, "KP_ADD (+)"}, {VK_OEM_4, "BRACKETLEFT ([)"}, {VK_OEM_6, "BRACKETRIGHT (])"},
    {VK_OEM_COMMA, "COMMA (,)"}, {VK_OEM_PERIOD, "PERIOD (.)"}, {VK_OEM_MINUS, "MINUS (-)"},
    {VK_OEM_PLUS, "EQUAL (=)"}, {VK_OEM_1, "SEMICOLON (;)"}, {VK_OEM_7, "APOSTROPHE (')"},
    {VK_OEM_2, "SLASH (/)"}, {VK_OEM_5, "BACKSLASH (\\)"}, {VK_PRIOR, "PAGE UP"},
    {VK_NEXT, "PAGE DOWN"}, {VK_HOME, "HOME"}, {VK_END, "END"}, {VK_SPACE, "SPACE"},
    {VK_RETURN, "ENTER"}, {VK_BACK, "BACKSPACE"}, {VK_TAB, "TAB"}, {VK_ESCAPE, "ESCAPE"},
    {VK_DELETE, "DELETE"}, {VK_INSERT, "INSERT"}, {VK_CAPITAL, "CAPS LOCK"},
    {VK_NUMLOCK, "NUM LOCK"}, {VK_SCROLL, "SCROLL LOCK"}, {VK_PAUSE, "PAUSE"},
    {VK_PRINT, "PRINT SCREEN"}, {0, NULL}
};

// Function prototypes
static WORD get_color_value(const char *name);
static void get_console_size(int *rows, int *cols);
static void print_centered(const char *msg);
static const char* mouse_button_name(int btn);
static const char* vkey_to_name(int vk);
static void update_modifiers(int vk, int pressed);
static LRESULT CALLBACK keyboard_proc(int code, WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK mouse_proc(int code, WPARAM wparam, LPARAM lparam);
static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type);
static void cleanup_and_exit(void);
static void print_usage(const char *prog);
static int parse_args(int argc, char *argv[]);

// Get Windows console color value for given color name
static WORD get_color_value(const char *name) {
    if (!name || strcmp(name, "") == 0) return 0;
    for (int i = 0; colors[i].name; i++) {
        if (strcmp(name, colors[i].name) == 0) {
            return colors[i].color;
        }
    }
    return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
}

// Get current console dimensions
static void get_console_size(int *rows, int *cols) {
    if (GetConsoleScreenBufferInfo(app.console_handle, &app.console_info)) {
        *rows = app.console_info.srWindow.Bottom - app.console_info.srWindow.Top + 1;
        *cols = app.console_info.srWindow.Right - app.console_info.srWindow.Left + 1;
    } else {
        *rows = 25; *cols = 80; // Fallback defaults
    }
}

// Print message centered on screen with optional colors
static void print_centered(const char *msg) {
    int rows, cols, len = (int)strlen(msg);
    COORD pos;
    DWORD written;
    
    get_console_size(&rows, &cols);
    
    // Clear screen and position cursor
    system("cls");
    pos.X = (SHORT)((cols - len) / 2);
    pos.Y = (SHORT)(rows / 2);
    SetConsoleCursorPosition(app.console_handle, pos);
    
    if (app.use_color) {
        WORD bg_attr = get_color_value(app.bg_color) << 4;
        WORD fg_attr = get_color_value(app.fg_color);
        WORD txt_attr = get_color_value(app.text_color);
        
        if (app.color_toggle) {
            WORD temp = bg_attr;
            bg_attr = fg_attr << 4;
            fg_attr = temp >> 4;
        }
        
        for (int i = 0; i < len; i++) {
            WORD attr = (isgraph((unsigned char)msg[i]) && strlen(app.text_color) > 0) ? 
                       (bg_attr | txt_attr) : (bg_attr | fg_attr);
            
            SetConsoleTextAttribute(app.console_handle, attr);
            WriteConsole(app.console_handle, &msg[i], 1, &written, NULL);
        }
        
        app.color_toggle = !app.color_toggle;
        
        // Reset to default colors
        SetConsoleTextAttribute(app.console_handle, 
            FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    } else {
        WriteConsole(app.console_handle, msg, len, &written, NULL);
    }
    
    printf("\n");
    fflush(stdout);
}

// Convert mouse button to descriptive name
static const char* mouse_button_name(int btn) {
    switch (btn) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: return "LEFT CLICK";
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: return "MIDDLE CLICK";
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: return "RIGHT CLICK";
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: return "X BUTTON";
        default: return "UNKNOWN BUTTON";
    }
}

// Convert virtual key code to human-readable name
static const char* vkey_to_name(int vk) {
    // Check special keys first
    for (int i = 0; special_keys[i].name; i++) {
        if (special_keys[i].vk == vk) return special_keys[i].name;
    }
    
    // Handle function keys
    if (vk >= VK_F1 && vk <= VK_F24) {
        static char f_key[16];
        sprintf(f_key, "F%d", vk - VK_F1 + 1);
        return f_key;
    }
    
    // Handle numeric keypad
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        static char num_key[16];
        sprintf(num_key, "KP_%d", vk - VK_NUMPAD0);
        return num_key;
    }
    
    // Handle regular alphanumeric keys
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        static char reg_key[2] = {0};
        reg_key[0] = (char)vk;
        return reg_key;
    }
    
    return "UNKNOWN";
}

// Update modifier key states
static void update_modifiers(int vk, int pressed) {
    switch (vk) {
        case VK_LSHIFT: app.mods.shift_l = pressed; break;
        case VK_RSHIFT: app.mods.shift_r = pressed; break;
        case VK_LCONTROL: app.mods.ctrl_l = pressed; break;
        case VK_RCONTROL: app.mods.ctrl_r = pressed; break;
        case VK_LMENU: app.mods.alt_l = pressed; break;
        case VK_RMENU: app.mods.alt_r = pressed; break;
        case VK_LWIN: app.mods.win_l = pressed; break;
        case VK_RWIN: app.mods.win_r = pressed; break;
    }
}

// Low-level keyboard hook procedure
static LRESULT CALLBACK keyboard_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code >= 0) {
        KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lparam;
        int vk = kb->vkCode;
        int pressed = (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN);
        
        if (pressed) {
            update_modifiers(vk, 1);
            
            const char *key_name = vkey_to_name(vk);
            char upper_key[MAX_MSG_LEN], mod_msg[MAX_MSG_LEN] = "", message[MAX_MSG_LEN * 2] = "";
            
            strncpy(upper_key, key_name, sizeof(upper_key) - 1);
            upper_key[sizeof(upper_key) - 1] = '\0';
            for (int i = 0; upper_key[i]; i++) {
                upper_key[i] = (char)toupper((unsigned char)upper_key[i]);
            }
            
            int is_modifier = (vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL || 
                              vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU ||
                              vk == VK_LWIN || vk == VK_RWIN);
            
            // Build modifier combination string
            if (app.mods.ctrl_l && vk != VK_LCONTROL) strncat(mod_msg, "CONTROL_L + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
            if (app.mods.ctrl_r && vk != VK_RCONTROL) strncat(mod_msg, "CONTROL_R + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
            if (app.mods.alt_l && vk != VK_LMENU) strncat(mod_msg, "ALT_L + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
            if (app.mods.alt_r && vk != VK_RMENU) strncat(mod_msg, "ALT_R + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
            if (app.mods.shift_l && vk != VK_LSHIFT) strncat(mod_msg, "SHIFT_L + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
            if (app.mods.shift_r && vk != VK_RSHIFT) strncat(mod_msg, "SHIFT_R + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
            if (app.mods.win_l && vk != VK_LWIN) strncat(mod_msg, "WIN_L + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
            if (app.mods.win_r && vk != VK_RWIN) strncat(mod_msg, "WIN_R + ", sizeof(mod_msg) - strlen(mod_msg) - 1);
            
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
        } else {
            update_modifiers(vk, 0);
        }
    }
    
    return CallNextHookEx(kb_hook, code, wparam, lparam);
}

// Low-level mouse hook procedure
static LRESULT CALLBACK mouse_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code >= 0) {
        MSLLHOOKSTRUCT *mouse = (MSLLHOOKSTRUCT *)lparam;
        
        switch (wparam) {
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_XBUTTONDOWN:
                app.mouse_pressed = wparam;
                print_centered(mouse_button_name(wparam));
                break;
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_MBUTTONUP:
            case WM_XBUTTONUP:
                app.mouse_pressed = 0;
                break;
            case WM_MOUSEWHEEL:
                {
                    int delta = GET_WHEEL_DELTA_WPARAM(mouse->mouseData);
                    if (delta > 0) {
                        print_centered("WHEEL UP");
                    } else {
                        print_centered("WHEEL DOWN");
                    }
                }
                break;
        }
    }
    
    return CallNextHookEx(mouse_hook, code, wparam, lparam);
}

// Console control handler for clean exit
static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            cleanup_and_exit();
            return TRUE;
        default:
            return FALSE;
    }
}

// Cleanup resources and exit
static void cleanup_and_exit(void) {
    running = 0;
    if (kb_hook) UnhookWindowsHookEx(kb_hook);
    if (mouse_hook) UnhookWindowsHookEx(mouse_hook);
    
    // Reset console
    system("cls");
    CONSOLE_CURSOR_INFO cursor_info = {100, TRUE};
    SetConsoleCursorInfo(app.console_handle, &cursor_info);
    SetConsoleTextAttribute(app.console_handle, 
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    
    ExitProcess(0);
}

// Display usage information
static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("Monitor and display keyboard/mouse events in console center\n\n");
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
    exit(0);
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
            int valid_bg = 1, valid_fg = 1, valid_text = 1;
            if (strcmp(app.bg_color, "default") != 0) {
                valid_bg = 0;
                for (int j = 0; colors[j].name; j++) {
                    if (strcmp(app.bg_color, colors[j].name) == 0) { valid_bg = 1; break; }
                }
            }
            if (strcmp(app.fg_color, "default") != 0) {
                valid_fg = 0;
                for (int j = 0; colors[j].name; j++) {
                    if (strcmp(app.fg_color, colors[j].name) == 0) { valid_fg = 1; break; }
                }
            }
            if (strlen(app.text_color) > 0 && strcmp(app.text_color, "default") != 0) {
                valid_text = 0;
                for (int j = 0; colors[j].name; j++) {
                    if (strcmp(app.text_color, colors[j].name) == 0) { valid_text = 1; break; }
                }
            }
            
            if (!valid_bg || !valid_fg || !valid_text) {
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

// Main function - initialize Windows hooks and start event monitoring
int main(int argc, char *argv[]) {
    // Initialize console
    app.console_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (app.console_handle == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error: Cannot get console handle\n");
        return 1;
    }
    
    // Setup console control handler
    if (!SetConsoleCtrlHandler(console_ctrl_handler, TRUE)) {
        fprintf(stderr, "Error: Cannot set console control handler\n");
        return 1;
    }
    
    if (!parse_args(argc, argv)) return 1;
    
    // Hide console cursor
    CONSOLE_CURSOR_INFO cursor_info = {100, FALSE};
    SetConsoleCursorInfo(app.console_handle, &cursor_info);
    
    // Install low-level hooks
    kb_hook = SetWindowsHookEx(WH_KEYBOARD_LL, keyboard_proc, GetModuleHandle(NULL), 0);
    mouse_hook = SetWindowsHookEx(WH_MOUSE_LL, mouse_proc, GetModuleHandle(NULL), 0);
    
    if (!kb_hook || !mouse_hook) {
        fprintf(stderr, "Error: Cannot install system hooks. Run as Administrator.\n");
        cleanup_and_exit();
        return 1;
    }
    
    print_centered("Termkey - Windows Professional Edition");
    
    // Main message loop
    MSG msg;
    while (running && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    cleanup_and_exit();
    return 0;
}
