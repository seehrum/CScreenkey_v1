#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>          // For xEvent
#include <X11/XKBlib.h>          // For XkbKeycodeToKeysym
#include <X11/extensions/record.h>
#include <X11/keysym.h>
#include <X11/X.h>

#define MAX_MESSAGE_LENGTH 256

// Global variables
static Display *display = NULL;                // Display for keyboard events
static Display *record_display = NULL;         // Display for recording events
static XRecordContext record_context;
static XRecordRange *record_range = NULL;
static int mouse_button_pressed = 0;           // Indicates if a mouse button is pressed
static int use_color = 0;                      // Flag to activate/deactivate color function
static char bg_color_name[20] = "default";     // Background color name
static char fg_color_name[20] = "default";     // Foreground color name
static char letter_color_name[20] = "";        // Letter color name

// Variables for terminal resizing and exit
static volatile sig_atomic_t terminal_resized = 0; // Flag to indicate terminal resize
static volatile sig_atomic_t exit_requested = 0;   // Flag to indicate exit requested
static char last_message[MAX_MESSAGE_LENGTH * 2 + 10] = "Termkey"; // Store the last displayed message

// Enum to represent modifier keys
typedef enum {
    SHIFT_L,
    SHIFT_R,
    CONTROL_L,
    CONTROL_R,
    ALT_L,
    ALT_R,
    META_L,
    META_R,
    ALTGR,
    SUPER_L,
    SUPER_R,
    MODIFIER_COUNT
} ModifierKey;

// Array to maintain the state of modifier keys
static int modifiers_state[MODIFIER_COUNT] = {0};

// Structure to map special keys
typedef struct {
    KeySym keysym;
    const char *name;
} KeyMap;

// Array of special keys and their names
static const KeyMap special_key_map[] = {
    // Modifier keys
    {XK_Shift_L,          "SHIFT_L"},
    {XK_Shift_R,          "SHIFT_R"},
    {XK_Control_L,        "CONTROL_L"},
    {XK_Control_R,        "CONTROL_R"},
    {XK_Alt_L,            "ALT_L"},
    {XK_Alt_R,            "ALT_R"},
    {XK_Meta_L,           "META_L"},
    {XK_Meta_R,           "META_R"},
    {XK_ISO_Level3_Shift, "ALTGR"},
    {XK_Super_L,          "SUPER_L"},
    {XK_Super_R,          "SUPER_R"},
    // Other special keys
    {XK_apostrophe,     "APOSTROPHE (')"},
    {XK_slash,          "SLASH (/)"},
    {XK_backslash,      "BACKSLASH (\\)"},
    {XK_Left,           "ARROW LEFT"},
    {XK_Right,          "ARROW RIGHT"},
    {XK_Up,             "ARROW UP"},
    {XK_Down,           "ARROW DOWN"},
    {XK_KP_Divide,      "KP_DIVIDE (/)"},
    {XK_KP_Multiply,    "KP_MULTIPLY (*)"},
    {XK_KP_Subtract,    "KP_SUBTRACT (-)"},
    {XK_KP_Add,         "KP_ADD (+)"},
    {XK_bracketleft,    "BRACKETLEFT ([)"},
    {XK_bracketright,   "BRACKETRIGHT (])"},
    {XK_comma,          "COMMA (,)"},
    {XK_period,         "PERIOD (.)"},
    {XK_dead_acute,     "DEAD_ACUTE (´)"},
    {XK_dead_tilde,     "DEAD_TILDE (~)"},
    {XK_dead_cedilla,   "DEAD_CEDILLA (Ç)"},
    {XK_minus,          "MINUS (-)"},
    {XK_equal,          "EQUAL (=)"},
    {XK_semicolon,      "SEMICOLON (;)"},
    {XK_Page_Up,        "PAGE UP"},
    {XK_Page_Down,      "PAGE DOWN"},
    {XK_Home,           "HOME"},
    {XK_End,            "END"}
};

#define SPECIAL_KEY_MAP_SIZE (sizeof(special_key_map) / sizeof(KeyMap))

// List of supported colors
static const char *color_names[] = {
    "black", "red", "green", "yellow", "blue", "magenta", "cyan", "white", "default", NULL
};

// Function prototypes
void disable_cursor(void);
void enable_cursor(void);
void get_terminal_size(int *rows, int *cols);
void print_centered(const char *message);
const char* color_name_to_code(const char *color_name, int is_background);
const char* mouse_button_to_name(int button);
const char* keysym_to_string(KeySym keysym);
void update_modifier_state(KeySym keysym, int is_pressed);
void build_modifiers_message(char *modifiers_message, size_t size, KeySym current_keysym);
void event_callback(XPointer priv, XRecordInterceptData *data);
void print_usage(const char *prog_name);
void signal_handler(int signum);
void handle_resize(int signum);
void cleanup(void);

int main(int argc, char *argv[]) {
    // Register signal handlers for clean exit and terminal resize
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    sa.sa_handler = handle_resize;
    sigaction(SIGWINCH, &sa, NULL);

    // Disable the cursor when starting the program
    disable_cursor();

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0) {
            use_color = 1;
            int colors_provided = argc - i - 1; // Number of arguments after -c

            if (colors_provided == 0) {
                // No colors provided, display help
                print_usage(argv[0]);
            } else if (colors_provided >= 3) {
                // User provided background, foreground, and letter colors
                strncpy(bg_color_name, argv[i + 1], sizeof(bg_color_name) - 1);
                strncpy(fg_color_name, argv[i + 2], sizeof(fg_color_name) - 1);
                strncpy(letter_color_name, argv[i + 3], sizeof(letter_color_name) - 1);
                i += 3; // Skip the next three arguments
            } else if (colors_provided == 2) {
                // User provided background and foreground colors
                strncpy(bg_color_name, argv[i + 1], sizeof(bg_color_name) - 1);
                strncpy(fg_color_name, argv[i + 2], sizeof(fg_color_name) - 1);
                letter_color_name[0] = '\0'; // No letter color
                i += 2; // Skip the next two arguments
            } else if (colors_provided == 1) {
                // User provided only background color
                strncpy(bg_color_name, argv[i + 1], sizeof(bg_color_name) - 1);
                strcpy(fg_color_name, "default");
                letter_color_name[0] = '\0'; // No letter color
                i += 1; // Skip the next argument
            } else {
                // No colors provided, display help
                print_usage(argv[0]);
            }

            // Validate colors
            if ((color_name_to_code(bg_color_name, 1) == NULL && strcmp(bg_color_name, "default") != 0) ||
                (color_name_to_code(fg_color_name, 0) == NULL && strcmp(fg_color_name, "default") != 0) ||
                (letter_color_name[0] != '\0' &&
                 color_name_to_code(letter_color_name, 0) == NULL &&
                 strcmp(letter_color_name, "default") != 0)) {
                fprintf(stderr, "Invalid color name(s) provided.\n");
                cleanup();
                exit(EXIT_FAILURE);
            }
        } else {
            // Unknown option
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
        }
    }

    // Open the connection to the X server
    display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Error opening display.\n");
        cleanup();
        exit(EXIT_FAILURE);
    }

    record_display = XOpenDisplay(NULL);
    if (record_display == NULL) {
        fprintf(stderr, "Error opening display for recording.\n");
        cleanup();
        exit(EXIT_FAILURE);
    }

    // Define the range of events we want to capture (mouse and keyboard)
    record_range = XRecordAllocRange();
    if (record_range == NULL) {
        fprintf(stderr, "Error allocating event range.\n");
        cleanup();
        exit(EXIT_FAILURE);
    }
    record_range->device_events.first = KeyPress;
    record_range->device_events.last  = ButtonRelease; // Includes mouse events

    XRecordClientSpec clients = XRecordAllClients;

    // Create the recording context to capture events
    record_context = XRecordCreateContext(record_display, 0, &clients, 1, &record_range, 1);
    if (!record_context) {
        fprintf(stderr, "Error creating recording context.\n");
        cleanup();
        exit(EXIT_FAILURE);
    }

    // Display "Termkey" at startup
    print_centered(last_message);

    // Enable the recording context asynchronously
    if (!XRecordEnableContextAsync(record_display, record_context, event_callback, NULL)) {
        fprintf(stderr, "Error enabling recording context.\n");
        cleanup();
        exit(EXIT_FAILURE);
    }

    // Main event loop
    while (!exit_requested) {
        if (XPending(record_display)) {
            XRecordProcessReplies(record_display);
        }

        // Check if terminal was resized
        if (terminal_resized) {
            terminal_resized = 0; // Reset the flag
            print_centered(last_message); // Reprint the last message
        }

        usleep(10000); // Small pause to avoid high CPU load
    }

    // Cleanup before exit
    cleanup();
    system("reset");

    return EXIT_SUCCESS;
}

// Function to clean up resources
void cleanup(void) {
    enable_cursor();
    if (record_context != 0) {
        XRecordDisableContext(record_display, record_context);
        XRecordFreeContext(record_display, record_context);
    }
    if (record_range != NULL) {
        XFree(record_range);
    }
    if (display != NULL) {
        XCloseDisplay(display);
        display = NULL;
    }
    if (record_display != NULL) {
        XCloseDisplay(record_display);
        record_display = NULL;
    }
}

// Signal handler to set exit flag
void signal_handler(int signum) {
    (void)signum; // Unused parameter
    exit_requested = 1;
}

// Function to handle terminal resize signals
void handle_resize(int signum) {
    (void)signum; // Unused parameter
    terminal_resized = 1; // Set the flag to indicate terminal was resized
}

// Function to print usage instructions
void print_usage(const char *prog_name) {
    printf("Usage: %s [-c bg_color [fg_color [letter_color]]]\n", prog_name);
    printf("Available colors: black, red, green, yellow, blue, magenta, cyan, white, default\n");
    printf("Examples:\n");
    printf("  %s -c red blue             # Background red, foreground blue\n", prog_name);
    printf("  %s -c red default          # Background red, foreground default\n", prog_name);
    printf("  %s -c default green        # Background default, foreground green\n", prog_name);
    printf("  %s -c default default red  # Only letters colored red\n", prog_name);
    printf("  %s -c                      # Display this help message\n", prog_name);
    exit(EXIT_SUCCESS);
}

// Function to disable the cursor
void disable_cursor(void) {
    printf("\033[?25l");  // Hides the cursor
    fflush(stdout);
}

// Function to enable the cursor
void enable_cursor(void) {
    printf("\033[?25h");  // Shows the cursor
    fflush(stdout);
}

// Function to get terminal size
void get_terminal_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
}

// Function to map color names to ANSI codes
const char* color_name_to_code(const char *color_name, int is_background) {
    static const struct {
        const char *name;
        const char *code_fg;
        const char *code_bg;
    } color_table[] = {
        {"black",   "\033[30m", "\033[40m"},
        {"red",     "\033[31m", "\033[41m"},
        {"green",   "\033[32m", "\033[42m"},
        {"yellow",  "\033[33m", "\033[43m"},
        {"blue",    "\033[34m", "\033[44m"},
        {"magenta", "\033[35m", "\033[45m"},
        {"cyan",    "\033[36m", "\033[46m"},
        {"white",   "\033[37m", "\033[47m"},
        {"default", "\033[39m", "\033[49m"},
        {NULL,      NULL,       NULL}
    };

    for (int i = 0; color_table[i].name != NULL; i++) {
        if (strcmp(color_name, color_table[i].name) == 0) {
            return is_background ? color_table[i].code_bg : color_table[i].code_fg;
        }
    }
    return NULL; // Invalid color
}

// Function to print centered text in the terminal
void print_centered(const char *message) {
    int rows, cols;
    static int color_toggle = 0;  // Static variable to keep track of color toggling

    get_terminal_size(&rows, &cols);

    int len = (int)strlen(message);
    int x = (cols - len) / 2;
    int y = rows / 2;

    // Clear the screen
    printf("\033[H\033[J");  // Clears the screen

    // Move the cursor to the position
    printf("\033[%d;%dH", y, x + 1); // +1 to adjust for 1-based indexing in terminals

    // Check if color function is activated
    if (use_color) {
        const char *bg_color_code = NULL;
        const char *fg_color_code = NULL;
        const char *letter_color_code = NULL;

        if (color_toggle) {
            // Swap background and foreground colors to create blinking effect
            bg_color_code = color_name_to_code(fg_color_name, 1);
            fg_color_code = color_name_to_code(bg_color_name, 0);
        } else {
            bg_color_code = color_name_to_code(bg_color_name, 1);
            fg_color_code = color_name_to_code(fg_color_name, 0);
        }

        // Get letter color code if provided
        if (letter_color_name[0] != '\0') {
            letter_color_code = color_name_to_code(letter_color_name, 0);
        }

        // Apply background color if valid
        if (bg_color_code) {
            printf("%s", bg_color_code);
        }

        // Now print each character
        for (int i = 0; i < len; i++) {
            char c = message[i];

            if (isgraph((unsigned char)c) && letter_color_code) {
                // Apply letter color to all printable characters
                printf("%s", letter_color_code);
            } else if (fg_color_code) {
                // Apply foreground color
                printf("%s", fg_color_code);
            } else {
                // Reset to default foreground color
                printf("\033[39m");
            }

            // Print the character
            printf("%c", c);
        }

        // Toggle color for next time
        color_toggle = !color_toggle;

        // Reset attributes
        printf("\033[0m\n");

    } else {
        // No color, simply print the message
        printf("%s\n", message);
    }

    fflush(stdout);  // Ensures the text is displayed immediately
}

// Function to convert mouse button number to name
const char* mouse_button_to_name(int button) {
    switch (button) {
        case Button1: return "LEFT CLICK";
        case Button2: return "MIDDLE CLICK";
        case Button3: return "RIGHT CLICK";
        case Button4: return "WHEEL UP";
        case Button5: return "WHEEL DOWN";
        default:      return "UNKNOWN BUTTON";
    }
}

// Function to convert KeySym to a friendly name
const char* keysym_to_string(KeySym keysym) {
    // Check if the key is in the special key map
    for (size_t i = 0; i < SPECIAL_KEY_MAP_SIZE; i++) {
        if (special_key_map[i].keysym == keysym) {
            return special_key_map[i].name;
        }
    }
    // If not, convert to default string
    return XKeysymToString(keysym);
}

// Function to update modifier key state
void update_modifier_state(KeySym keysym, int is_pressed) {
    switch (keysym) {
        case XK_Shift_L:          modifiers_state[SHIFT_L]   = is_pressed; break;
        case XK_Shift_R:          modifiers_state[SHIFT_R]   = is_pressed; break;
        case XK_Control_L:        modifiers_state[CONTROL_L] = is_pressed; break;
        case XK_Control_R:        modifiers_state[CONTROL_R] = is_pressed; break;
        case XK_Alt_L:            modifiers_state[ALT_L]     = is_pressed; break;
        case XK_Alt_R:            modifiers_state[ALT_R]     = is_pressed; break;
        case XK_Meta_L:           modifiers_state[META_L]    = is_pressed; break;
        case XK_Meta_R:           modifiers_state[META_R]    = is_pressed; break;
        case XK_ISO_Level3_Shift: modifiers_state[ALTGR]     = is_pressed; break;
        case XK_Super_L:          modifiers_state[SUPER_L]   = is_pressed; break;
        case XK_Super_R:          modifiers_state[SUPER_R]   = is_pressed; break;
        default: break;
    }
}

// Function to build the modifiers message
void build_modifiers_message(char *modifiers_message, size_t size, KeySym current_keysym) {
    modifiers_message[0] = '\0'; // Ensure the string is empty

    for (int i = 0; i < MODIFIER_COUNT; i++) {
        if (modifiers_state[i]) {
            KeySym modifier_keysym;
            switch (i) {
                case SHIFT_L:   modifier_keysym = XK_Shift_L;          break;
                case SHIFT_R:   modifier_keysym = XK_Shift_R;          break;
                case CONTROL_L: modifier_keysym = XK_Control_L;        break;
                case CONTROL_R: modifier_keysym = XK_Control_R;        break;
                case ALT_L:     modifier_keysym = XK_Alt_L;            break;
                case ALT_R:     modifier_keysym = XK_Alt_R;            break;
                case META_L:    modifier_keysym = XK_Meta_L;           break;
                case META_R:    modifier_keysym = XK_Meta_R;           break;
                case ALTGR:     modifier_keysym = XK_ISO_Level3_Shift; break;
                case SUPER_L:   modifier_keysym = XK_Super_L;          break;
                case SUPER_R:   modifier_keysym = XK_Super_R;          break;
                default: continue;
            }
            if (modifier_keysym != current_keysym) {
                const char *modifier_name = keysym_to_string(modifier_keysym);
                if (modifier_name) {
                    strncat(modifiers_message, modifier_name, size - strlen(modifiers_message) - 1);
                    strncat(modifiers_message, " + ", size - strlen(modifiers_message) - 1);
                }
            }
        }
    }
}

// Callback function to process intercepted events
void event_callback(XPointer priv, XRecordInterceptData *data) {
    (void)priv; // Unused parameter
    if (data->category != XRecordFromServer || data->data == NULL) {
        XRecordFreeData(data);
        return;
    }

    xEvent *event = (xEvent *)data->data;
    char message[MAX_MESSAGE_LENGTH * 2 + 10] = ""; // Adjusted buffer size

    // Get the event type
    int event_type = event->u.u.type & 0x7F; // Ignore the send_event bit

    // Mouse event handling
    if (event_type == ButtonPress) {
        mouse_button_pressed = event->u.u.detail; // The detail field contains the button number
        const char *button_name = mouse_button_to_name(mouse_button_pressed);
        snprintf(message, sizeof(message), "%s", button_name);
        strncpy(last_message, message, sizeof(last_message)); // Store the message
        print_centered(message);
    } else if (event_type == ButtonRelease) {
        mouse_button_pressed = 0; // Mouse button released
    }
    // Keyboard event handling
    else if (event_type == KeyPress || event_type == KeyRelease) {
        unsigned int keycode = event->u.u.detail;
        KeySym keysym = XkbKeycodeToKeysym(display, keycode, 0, 0);

        // Update the state of modifier keys
        int is_key_press = (event_type == KeyPress);
        update_modifier_state(keysym, is_key_press);

        // Process the key if it's a KeyPress event
        if (is_key_press) {
            const char *key_string = keysym_to_string(keysym);

            if (key_string != NULL) {
                char uppercase_key[MAX_MESSAGE_LENGTH];
                // Copy key_string to uppercase_key and convert to uppercase
                strncpy(uppercase_key, key_string, sizeof(uppercase_key) - 1);
                uppercase_key[sizeof(uppercase_key) - 1] = '\0'; // Ensure null termination

                for (int i = 0; uppercase_key[i]; i++) {
                    uppercase_key[i] = toupper((unsigned char)uppercase_key[i]);
                }

                // Check if the key is a modifier key
                int is_modifier_key = (keysym == XK_Shift_L || keysym == XK_Shift_R ||
                                       keysym == XK_Control_L || keysym == XK_Control_R ||
                                       keysym == XK_Alt_L || keysym == XK_Alt_R ||
                                       keysym == XK_Meta_L || keysym == XK_Meta_R ||
                                       keysym == XK_ISO_Level3_Shift ||
                                       keysym == XK_Super_L || keysym == XK_Super_R);

                char modifiers_message[MAX_MESSAGE_LENGTH];
                build_modifiers_message(modifiers_message, sizeof(modifiers_message), keysym);

                // Add the key pressed
                if (!(is_modifier_key && modifiers_message[0] == '\0')) {
                    // If the key is not a lone modifier, add it to the message
                    if (modifiers_message[0] != '\0') {
                        strncat(modifiers_message, uppercase_key, sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    } else {
                        strncpy(modifiers_message, uppercase_key, sizeof(modifiers_message) - 1);
                    }
                    modifiers_message[sizeof(modifiers_message) - 1] = '\0'; // Ensure null termination
                } else {
                    // If it's a lone modifier, just use its name
                    strncpy(modifiers_message, uppercase_key, sizeof(modifiers_message) - 1);
                    modifiers_message[sizeof(modifiers_message) - 1] = '\0';
                }

                // Handle mouse button pressed along with key
                if (mouse_button_pressed != 0) {
                    const char *button_name = mouse_button_to_name(mouse_button_pressed);
                    snprintf(message, sizeof(message), "%s + %s", button_name, modifiers_message);
                } else {
                    strncpy(message, modifiers_message, sizeof(message) - 1);
                    message[sizeof(message) - 1] = '\0'; // Ensure null termination
                }

                strncpy(last_message, message, sizeof(last_message) - 1); // Store the message
                last_message[sizeof(last_message) - 1] = '\0'; // Ensure null termination
                print_centered(message);
            }
        }
    }

    // Free the intercepted event data
    XRecordFreeData(data);
}
