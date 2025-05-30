// termkey.c
//
// A program to display keyboard and mouse events in the terminal.
// It captures key presses and mouse clicks, displaying them centered on the screen.
// Supports color customization and handles various modifier keys.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>          // For xEvent
#include <X11/XKBlib.h>          // For XkbKeycodeToKeysym
#include <X11/keysym.h>
#include <X11/X.h>
#include <X11/extensions/record.h>
#include <signal.h>

#define MAX_MESSAGE_LENGTH 256
#define COLOR_NAME_LENGTH  20

// Global variables for display connections
static Display *display = NULL;        // Display for keyboard events
static Display *record_display = NULL; // Display for recording events

// Mouse button state
static int mouse_button_pressed = 0;   // Indicates if a mouse button is pressed

// Modifier keys state
typedef struct {
    int shift_l;
    int shift_r;
    int ctrl_l;
    int ctrl_r;
    int alt_l;
    int alt_r;
    int meta_l;
    int meta_r;
    int altgr;
    int super_l;
    int super_r;
} ModifierState;

static ModifierState modifiers = {0};

// Color-related variables
static int use_color = 0;                              // Flag to activate/deactivate color function
static char bg_color_name[COLOR_NAME_LENGTH] = "default";   // Background color name
static char fg_color_name[COLOR_NAME_LENGTH] = "default";   // Foreground color name
static char letter_color_name[COLOR_NAME_LENGTH] = "";      // Letter color name

// Special key mapping
typedef struct {
    KeySym keysym;
    const char *name;
} KeyMap;

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
    {XK_apostrophe,       "APOSTROPHE (')"},
    {XK_slash,            "SLASH (/)"},
    {XK_backslash,        "BACKSLASH (\\)"},
    {XK_Left,             "ARROW LEFT"},
    {XK_Right,            "ARROW RIGHT"},
    {XK_Up,               "ARROW UP"},
    {XK_Down,             "ARROW DOWN"},
    {XK_KP_Divide,        "KP_DIVIDE (/)"},
    {XK_KP_Multiply,      "KP_MULTIPLY (*)"},
    {XK_KP_Subtract,      "KP_SUBTRACT (-)"},
    {XK_KP_Add,           "KP_ADD (+)"},
    {XK_bracketleft,      "BRACKETLEFT ([)"},
    {XK_bracketright,     "BRACKETRIGHT (])"},
    {XK_comma,            "COMMA (,)"},
    {XK_period,           "PERIOD (.)"},
    {XK_dead_acute,       "DEAD_ACUTE (´)"},
    {XK_dead_tilde,       "DEAD_TILDE (~)"},
    {XK_dead_cedilla,     "DEAD_CEDILLA (Ç)"},
    {XK_minus,            "MINUS (-)"},
    {XK_equal,            "EQUAL (=)"},
    {XK_semicolon,        "SEMICOLON (;)"},
    {XK_Page_Up,          "PAGE UP"},
    {XK_Page_Down,        "PAGE DOWN"},
    {XK_Home,             "HOME"},
    {XK_End,              "END"}
};

#define SPECIAL_KEY_MAP_SIZE (sizeof(special_key_map) / sizeof(KeyMap))

// Function prototypes
void disable_cursor(void);
void enable_cursor(void);
void get_terminal_size(int *rows, int *cols);
void print_centered(const char *message);
const char *color_name_to_code(const char *color_name, int is_background);
const char *mouse_button_to_name(int button);
const char *keysym_to_string(KeySym keysym);
void print_usage(const char *prog_name);
void event_callback(XPointer priv, XRecordInterceptData *data);
void update_modifier_state(KeySym keysym, int is_key_press);
void cleanup(void);
void signal_handler(int sig);

void signal_handler(int sig) {
    // Make signal handler async-safe by avoiding complex operations
    static const char reset_seq[] = "\033c\033[0m\033[?25h\033[2J\033[H";
    
    // Write reset sequence directly (async-safe)
    if (write(STDOUT_FILENO, reset_seq, sizeof(reset_seq) - 1) == -1) {
        // If write fails, try basic reset
        if (write(STDOUT_FILENO, "\033[0m\033[?25h", 10) == -1) {
            // Ignore write errors in signal handler
        }
    }
    
    // Don't call complex X11 functions in signal handler
    // Just exit - the OS will clean up file descriptors
    _exit(0);
}

// Main function
int main(int argc, char *argv[]) {
    // Register signal handlers FIRST, before any other initialization
    signal(SIGINT, signal_handler);   // Ctrl+C
    signal(SIGTERM, signal_handler);  // Termination request
    signal(SIGQUIT, signal_handler);  // Quit signal (Ctrl+\)
    signal(SIGHUP, signal_handler);   // Hangup signal

    // Parse command-line arguments with improved logic
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--color") == 0) {
            use_color = 1;
            i++; // Move to next argument
            
            // No more arguments after -c, show help
            if (i >= argc) {
                print_usage(argv[0]);
                exit(EXIT_SUCCESS);
            }
            
            // Parse color arguments with named options
            while (i < argc) {
                if (strncmp(argv[i], "--bg=", 5) == 0) {
                    strncpy(bg_color_name, argv[i] + 5, COLOR_NAME_LENGTH - 1);
                    bg_color_name[COLOR_NAME_LENGTH - 1] = '\0';
                } else if (strncmp(argv[i], "--fg=", 5) == 0) {
                    strncpy(fg_color_name, argv[i] + 5, COLOR_NAME_LENGTH - 1);
                    fg_color_name[COLOR_NAME_LENGTH - 1] = '\0';
                } else if (strncmp(argv[i], "--text=", 7) == 0) {
                    strncpy(letter_color_name, argv[i] + 7, COLOR_NAME_LENGTH - 1);
                    letter_color_name[COLOR_NAME_LENGTH - 1] = '\0';
                } else {
                    // Unknown option, break out of color parsing
                    i--;
                    break;
                }
                i++;
            }
            
            // Validate colors
            if ((strlen(bg_color_name) > 0 && strcmp(bg_color_name, "default") != 0 && 
                 color_name_to_code(bg_color_name, 1) == NULL) ||
                (strlen(fg_color_name) > 0 && strcmp(fg_color_name, "default") != 0 && 
                 color_name_to_code(fg_color_name, 0) == NULL) ||
                (strlen(letter_color_name) > 0 && strcmp(letter_color_name, "default") != 0 && 
                 color_name_to_code(letter_color_name, 0) == NULL)) {
                fprintf(stderr, "Invalid color name(s) provided.\n");
                exit(EXIT_FAILURE);
            }
            
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
        } else {
            // Unknown option
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
        }
    }

    // Disable the cursor when starting the program
    disable_cursor();

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
    XRecordRange *range = XRecordAllocRange();
    if (range == NULL) {
        fprintf(stderr, "Error allocating event range.\n");
        cleanup();
        exit(EXIT_FAILURE);
    }
    range->device_events.first = KeyPress;
    range->device_events.last  = ButtonRelease; // Includes mouse events

    XRecordClientSpec clients = XRecordAllClients;

    // Create the recording context to capture events
    XRecordContext context = XRecordCreateContext(record_display, 0, &clients, 1, &range, 1);
    if (!context) {
        fprintf(stderr, "Error creating recording context.\n");
        XFree(range);
        cleanup();
        exit(EXIT_FAILURE);
    }

    // Enable the recording context asynchronously
    if (!XRecordEnableContextAsync(record_display, context, event_callback, NULL)) {
        fprintf(stderr, "Error enabling recording context.\n");
        XRecordFreeContext(record_display, context);
        XFree(range);
        cleanup();
        exit(EXIT_FAILURE);
    }

    // Display "Termkey" at startup
    print_centered("Termkey");

    // Infinite loop to process events
    while (1) {
        XRecordProcessReplies(record_display);
        usleep(10000);  // Small pause to avoid high CPU load
    }

    // Cleanup resources (unreachable code in current state)
    // To make the code more robust, you might want to handle signals to allow graceful exit.
    // For now, we can leave this here for completeness.
    XRecordDisableContext(record_display, context);
    XRecordFreeContext(record_display, context);
    XFree(range);
    cleanup();

    return 0;
}

// Function to get terminal size
void get_terminal_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24; // Default rows
        *cols = 80; // Default columns
    }
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

// Function to map color names to ANSI codes
const char *color_name_to_code(const char *color_name, int is_background) {
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
    printf("\033[%d;%dH", y, x + 1); // +1 for 1-based indexing in terminals

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
const char *mouse_button_to_name(int button) {
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
const char *keysym_to_string(KeySym keysym) {
    // Check if the key is in the special key map
    for (size_t i = 0; i < SPECIAL_KEY_MAP_SIZE; i++) {
        if (special_key_map[i].keysym == keysym) {
            return special_key_map[i].name;
        }
    }
    // If not, convert to default string
    return XKeysymToString(keysym);
}

// Function to print usage instructions
void print_usage(const char *prog_name) {
    printf("Usage: %s -c bg_color fg_color \n", prog_name);
    printf("Available colors: black, red, green, yellow, blue, magenta, cyan, white, default\n");
    printf("Examples:\n");
    printf("  %s -c --text=green            # Just green text\n", prog_name);
    printf("  %s -c --bg=red --text=cyan    # Red background, cyan text\n", prog_name);
    printf("  %s -c --bg=black --text=white # Black background, white foreground\n", prog_name);
    printf("  %s -c                         # Display this help message\n", prog_name);
    exit(EXIT_SUCCESS);
}

// Function to update the modifier keys state
void update_modifier_state(KeySym keysym, int is_key_press) {
    switch (keysym) {
        case XK_Shift_L:          modifiers.shift_l   = is_key_press; break;
        case XK_Shift_R:          modifiers.shift_r   = is_key_press; break;
        case XK_Control_L:        modifiers.ctrl_l    = is_key_press; break;
        case XK_Control_R:        modifiers.ctrl_r    = is_key_press; break;
        case XK_Alt_L:            modifiers.alt_l     = is_key_press; break;
        case XK_Alt_R:            modifiers.alt_r     = is_key_press; break;
        case XK_Meta_L:           modifiers.meta_l    = is_key_press; break;
        case XK_Meta_R:           modifiers.meta_r    = is_key_press; break;
        case XK_ISO_Level3_Shift: modifiers.altgr     = is_key_press; break;
        case XK_Super_L:          modifiers.super_l   = is_key_press; break;
        case XK_Super_R:          modifiers.super_r   = is_key_press; break;
        default: break;
    }
}

// Callback function to process intercepted events
void event_callback(XPointer priv, XRecordInterceptData *data) {
    if (data->category == XRecordFromServer && data->data != NULL) {
        xEvent *event = (xEvent *)data->data;
        char message[MAX_MESSAGE_LENGTH * 2 + 10] = ""; // Adjusted buffer size

        // Get the event type
        int event_type = event->u.u.type & 0x7F; // Ignore the send_event bit

        // Mouse event handling
        if (event_type == ButtonPress) {
            mouse_button_pressed = event->u.u.detail; // The detail field contains the button number
            const char *button_name = mouse_button_to_name(mouse_button_pressed);
            snprintf(message, sizeof(message), "%s", button_name);
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

                    char modifiers_message[MAX_MESSAGE_LENGTH] = "";

                    // Include specific modifiers if they are pressed and are not the current key
                    if (modifiers.ctrl_l && keysym != XK_Control_L)
                        strncat(modifiers_message, "CONTROL_L + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    if (modifiers.ctrl_r && keysym != XK_Control_R)
                        strncat(modifiers_message, "CONTROL_R + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    if (modifiers.alt_l && keysym != XK_Alt_L)
                        strncat(modifiers_message, "ALT_L + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    if (modifiers.alt_r && keysym != XK_Alt_R)
                        strncat(modifiers_message, "ALT_R + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    if (modifiers.shift_l && keysym != XK_Shift_L)
                        strncat(modifiers_message, "SHIFT_L + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    if (modifiers.shift_r && keysym != XK_Shift_R)
                        strncat(modifiers_message, "SHIFT_R + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    if (modifiers.meta_l && keysym != XK_Meta_L)
                        strncat(modifiers_message, "META_L + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    if (modifiers.meta_r && keysym != XK_Meta_R)
                        strncat(modifiers_message, "META_R + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    if (modifiers.altgr && keysym != XK_ISO_Level3_Shift)
                        strncat(modifiers_message, "ALTGR + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    if (modifiers.super_l && keysym != XK_Super_L)
                        strncat(modifiers_message, "SUPER_L + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    if (modifiers.super_r && keysym != XK_Super_R)
                        strncat(modifiers_message, "SUPER_R + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);

                    // Add the key pressed
                    if (!(is_modifier_key && modifiers_message[0] == '\0')) {
                        // If the key is not a lone modifier, add it to the message
                        strncat(modifiers_message, uppercase_key, sizeof(modifiers_message) - strlen(modifiers_message) - 1);
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

                    print_centered(message);
                }
            }
        }
    }

    // Free the intercepted event data
    XRecordFreeData(data);
}

// Cleanup function to restore cursor and close displays
void cleanup(void) {
    enable_cursor();
    if (display) {
        XCloseDisplay(display);
        display = NULL;
    }
    if (record_display) {
        XCloseDisplay(record_display);
        record_display = NULL;
    }
}
