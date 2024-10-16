#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>      // For xEvent
#include <X11/XKBlib.h>      // For XkbKeycodeToKeysym
#include <X11/extensions/record.h>
#include <X11/keysym.h>
#include <X11/X.h>

#define MAX_MESSAGE_LENGTH 256

// Global variables
static Display *display = NULL;             // Display for keyboard events
static Display *record_display = NULL;      // Display for recording events
static XRecordContext context;
static XRecordRange *range = NULL;
static int mouse_button_pressed = 0;        // Indicates if a mouse button is pressed
static int use_color = 0;                   // Flag to activate/deactivate color function
static char bg_color_name[20] = "";         // Background color name
static char fg_color_name[20] = "";         // Foreground color name

// Variables for terminal resizing
static volatile sig_atomic_t terminal_resized = 0; // Flag to indicate terminal resize
static char last_message[MAX_MESSAGE_LENGTH * 2 + 10] = ""; // Store the last displayed message

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
static const KeyMap specialKeyMap[] = {
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

#define SPECIAL_KEY_MAP_SIZE (sizeof(specialKeyMap) / sizeof(KeyMap))

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
void cleanup_and_exit(int signum);
void handle_resize(int signum);

int main(int argc, char *argv[]) {
    // Register signal handlers for clean exit and terminal resize
    signal(SIGINT, cleanup_and_exit);
    signal(SIGTERM, cleanup_and_exit);
    signal(SIGWINCH, handle_resize); // Handle terminal resize

    // Disable the cursor when starting the program
    disable_cursor();

    XRecordClientSpec clients;

    // Initialize color names with defaults
    strcpy(bg_color_name, "default");
    strcpy(fg_color_name, "default");

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0) {
            use_color = 1;
            int colors_provided = argc - i - 1; // Number of arguments after -c

            if (colors_provided == 0) {
                // No colors provided, display help
                print_usage(argv[0]);
            } else if (colors_provided >= 2) {
                // User provided both background and foreground colors
                strncpy(bg_color_name, argv[i + 1], sizeof(bg_color_name) - 1);
                bg_color_name[sizeof(bg_color_name) - 1] = '\0';
                strncpy(fg_color_name, argv[i + 2], sizeof(fg_color_name) - 1);
                fg_color_name[sizeof(fg_color_name) - 1] = '\0';
                i += 2; // Skip the next two arguments
            } else if (colors_provided == 1) {
                // User provided only background color
                strncpy(bg_color_name, argv[i + 1], sizeof(bg_color_name) - 1);
                bg_color_name[sizeof(bg_color_name) - 1] = '\0';
                strcpy(fg_color_name, "default");
                i += 1; // Skip the next argument
            }

            // Validate colors
            if ((!color_name_to_code(bg_color_name, 1) && strcmp(bg_color_name, "default") != 0) ||
                (!color_name_to_code(fg_color_name, 0) && strcmp(fg_color_name, "default") != 0)) {
                fprintf(stderr, "Invalid color name(s) provided.\n");
                enable_cursor();
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
        enable_cursor();
        exit(EXIT_FAILURE);
    }

    record_display = XOpenDisplay(NULL);
    if (record_display == NULL) {
        fprintf(stderr, "Error opening display for recording.\n");
        enable_cursor();
        XCloseDisplay(display);
        exit(EXIT_FAILURE);
    }

    // Define the range of events we want to capture (mouse and keyboard)
    range = XRecordAllocRange();
    if (range == NULL) {
        fprintf(stderr, "Error allocating event range.\n");
        enable_cursor();
        XCloseDisplay(display);
        XCloseDisplay(record_display);
        exit(EXIT_FAILURE);
    }
    range->device_events.first = KeyPress;
    range->device_events.last  = ButtonRelease; // Includes mouse events

    clients = XRecordAllClients;

    // Create the recording context to capture events
    context = XRecordCreateContext(record_display, 0, &clients, 1, &range, 1);
    if (!context) {
        fprintf(stderr, "Error creating recording context.\n");
        enable_cursor();
        XFree(range);
        XCloseDisplay(display);
        XCloseDisplay(record_display);
        exit(EXIT_FAILURE);
    }

    // Enable the recording context asynchronously
    if (!XRecordEnableContextAsync(record_display, context, event_callback, NULL)) {
        fprintf(stderr, "Error enabling recording context.\n");
        enable_cursor();
        XRecordFreeContext(record_display, context);
        XFree(range);
        XCloseDisplay(display);
        XCloseDisplay(record_display);
        exit(EXIT_FAILURE);
    }

    // Main event loop
    while (1) {
        XRecordProcessReplies(record_display);

        // Check if terminal was resized
        if (terminal_resized) {
            terminal_resized = 0; // Reset the flag
            print_centered(last_message); // Reprint the last message
        }

        usleep(10000); // Small pause to avoid high CPU load
    }

    // Cleanup (should not reach here)
    cleanup_and_exit(0);

    return EXIT_SUCCESS;
}

// Function to handle termination signals
void cleanup_and_exit(int signum) {
    enable_cursor();
    if (context != 0) {
        XRecordDisableContext(record_display, context);
        XRecordFreeContext(record_display, context);
    }
    if (range != NULL) {
        XFree(range);
    }
    if (display != NULL) {
        XCloseDisplay(display);
    }
    if (record_display != NULL) {
        XCloseDisplay(record_display);
    }
    exit(EXIT_SUCCESS);
}

// Function to handle terminal resize signals
void handle_resize(int signum) {
    terminal_resized = 1; // Set the flag to indicate terminal was resized
}

// Function to print usage instructions
void print_usage(const char *prog_name) {
    printf("Usage: %s [-c bg_color fg_color]\n", prog_name);
    printf("Available colors: black, red, green, yellow, blue, magenta, cyan, white, default\n");
    printf("Examples:\n");
    printf("  %s -c red blue       # Background red, foreground blue\n", prog_name);
    printf("  %s -c red default    # Background red, foreground default\n", prog_name);
    printf("  %s -c default green  # Background default, foreground green\n", prog_name);
    printf("  %s -c                # Display this help message\n", prog_name);
    exit(EXIT_SUCCESS);
}

// Function to disable the cursor
void disable_cursor(void) {
    printf("\033[?25l");  // Hides the cursor
}

// Function to enable the cursor
void enable_cursor(void) {
    printf("\033[?25h");  // Shows the cursor
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
    if (strcmp(color_name, "black") == 0) {
        return is_background ? "\033[40m" : "\033[30m";
    } else if (strcmp(color_name, "red") == 0) {
        return is_background ? "\033[41m" : "\033[31m";
    } else if (strcmp(color_name, "green") == 0) {
        return is_background ? "\033[42m" : "\033[32m";
    } else if (strcmp(color_name, "yellow") == 0) {
        return is_background ? "\033[43m" : "\033[33m";
    } else if (strcmp(color_name, "blue") == 0) {
        return is_background ? "\033[44m" : "\033[34m";
    } else if (strcmp(color_name, "magenta") == 0) {
        return is_background ? "\033[45m" : "\033[35m";
    } else if (strcmp(color_name, "cyan") == 0) {
        return is_background ? "\033[46m" : "\033[36m";
    } else if (strcmp(color_name, "white") == 0) {
        return is_background ? "\033[47m" : "\033[37m";
    } else if (strcmp(color_name, "default") == 0) {
        return is_background ? "\033[49m" : "\033[39m";
    } else {
        return NULL; // Invalid color
    }
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
        const char *bg_color_code;
        const char *fg_color_code;

        if (color_toggle) {
            // Swap background and foreground colors to create blinking effect
            bg_color_code = color_name_to_code(fg_color_name, 1);
            fg_color_code = color_name_to_code(bg_color_name, 0);
        } else {
            bg_color_code = color_name_to_code(bg_color_name, 1);
            fg_color_code = color_name_to_code(fg_color_name, 0);
        }

        // Apply background color if valid
        if (bg_color_code) {
            printf("%s", bg_color_code);
        }

        // Apply foreground color if valid
        if (fg_color_code) {
            printf("%s", fg_color_code);
        }

        // Toggle color for next time
        color_toggle = !color_toggle;
    }

    printf("%s", message);  // Prints the text

    // Reset attributes
    printf("\033[0m\n");

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
        if (specialKeyMap[i].keysym == keysym) {
            return specialKeyMap[i].name;
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
                strncpy(uppercase_key, key_string, sizeof(uppercase_key));
                uppercase_key[sizeof(uppercase_key) - 1] = '\0'; // Ensure null termination

                for (int i = 0; uppercase_key[i]; i++) {
                    uppercase_key[i] = toupper((unsigned char)uppercase_key[i]);
                }

                // Check if the key is a modifier key
                int is_modifier_key = (keysym == XK_Shift_L || keysym == XK_Shift_R ||
                                       keysym == XK_Control_L || keysym == XK_Control_R ||
                                       keysym == XK_Alt_L || keysym == XK_Alt_R ||
                                       keysym == XK_Meta_L || keysym == XK_Meta_R ||
                                       keysym == XK_ISO_Level3_Shift);

                char modifiers_message[MAX_MESSAGE_LENGTH];
                build_modifiers_message(modifiers_message, sizeof(modifiers_message), keysym);

                // Add the key pressed
                if (!(is_modifier_key && modifiers_message[0] == '\0')) {
                    // If the key is not a lone modifier, add it to the message
                    if (modifiers_message[0] != '\0') {
                        strncat(modifiers_message, uppercase_key, sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    } else {
                        strncpy(modifiers_message, uppercase_key, sizeof(modifiers_message));
                        modifiers_message[sizeof(modifiers_message) - 1] = '\0';
                    }
                } else {
                    // If it's a lone modifier, just use its name
                    strncpy(modifiers_message, uppercase_key, sizeof(modifiers_message));
                    modifiers_message[sizeof(modifiers_message) - 1] = '\0';
                }

                // Handle mouse button pressed along with key
                if (mouse_button_pressed != 0) {
                    const char *button_name = mouse_button_to_name(mouse_button_pressed);
                    snprintf(message, sizeof(message), "%s + %s", button_name, modifiers_message);
                } else {
                    strncpy(message, modifiers_message, sizeof(message));
                    message[sizeof(message) - 1] = '\0'; // Ensure null termination
                }

                strncpy(last_message, message, sizeof(last_message)); // Store the message
                print_centered(message);
            }
        }
    }

    // Free the intercepted event data
    XRecordFreeData(data);
}
