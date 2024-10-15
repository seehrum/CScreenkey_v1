#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>    // For xEvent
#include <X11/XKBlib.h>    // For XkbKeycodeToKeysym
#include <X11/keysym.h>
#include <X11/X.h>
#include <X11/extensions/record.h>

#define MAX_MESSAGE_LENGTH 256

static Display *display = NULL;            // Display for keyboard events
static int mouse_button_pressed = 0;       // Indicates if a mouse button is pressed

// Variables to maintain the state of modifier keys
static int shift_l_pressed = 0;
static int shift_r_pressed = 0;
static int ctrl_l_pressed = 0;
static int ctrl_r_pressed = 0;
static int alt_l_pressed = 0;
static int alt_r_pressed = 0;
static int meta_l_pressed = 0;
static int meta_r_pressed = 0;

// Structure to map special keys
typedef struct {
    KeySym keysym;
    const char *name;
} KeyMap;

KeyMap specialKeyMap[] = {
    // Modifier keys
    {XK_Shift_L, "SHIFT_L"},
    {XK_Shift_R, "SHIFT_R"},
    {XK_Control_L, "CONTROL_L"},
    {XK_Control_R, "CONTROL_R"},
    {XK_Alt_L, "ALT_L"},
    {XK_Alt_R, "ALT_R"},
    {XK_Meta_L, "META_L"},
    {XK_Meta_R, "META_R"},
    // Other special keys
    {XK_apostrophe, "APOSTROPHE (')"},
    {XK_slash, "SLASH (/)"},
    {XK_backslash, "BACKSLASH (\\)"},
    {XK_Left, "ARROW LEFT"},
    {XK_Right, "ARROW RIGHT"},
    {XK_Up, "ARROW UP"},
    {XK_Down, "ARROW DOWN"},
    {XK_KP_Divide, "KP_DIVIDE (/)"},
    {XK_KP_Multiply, "KP_MULTIPLY (*)"},
    {XK_KP_Subtract, "KP_SUBTRACT (-)"},
    {XK_KP_Add, "KP_ADD (+)"},
    {XK_bracketleft, "BRACKETLEFT ([)"},
    {XK_bracketright, "BRACKETRIGHT (])"},
    {XK_comma, "COMMA (,)"},
    {XK_period, "PERIOD (.)"},
    {XK_dead_acute, "DEAD_ACUTE (´)"},
    {XK_dead_tilde, "DEAD_TILDE (~)"},
    {XK_dead_cedilla, "DEAD_CEDILLA (Ç)"},
    {XK_minus, "MINUS (-)"},
    {XK_equal, "EQUAL (=)"},
    {XK_semicolon, "SEMICOLON (;)"},
    {XK_Page_Up, "PAGE UP"},
    {XK_Page_Down, "PAGE DOWN"},
    {XK_Home, "HOME"},
    {XK_End, "END"}
};

#define SPECIAL_KEY_MAP_SIZE (sizeof(specialKeyMap)/sizeof(KeyMap))

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

// Function to disable the cursor
void disable_cursor() {
    printf("\033[?25l");  // Hides the cursor
}

// Function to enable the cursor
void enable_cursor() {
    printf("\033[?25h");  // Shows the cursor
}

// Function to print centered text in the terminal
void print_centered(const char *message) {
    int rows, cols;
    get_terminal_size(&rows, &cols);

    int len = strlen(message);
    int x = (cols - len) / 2;
    int y = rows / 2;

    // Clear the screen and move the cursor to the center
    printf("\033[H\033[J"); // Clears the screen
    printf("\033[%d;%dH%s\n", y, x, message); // Moves the cursor and prints the text
    fflush(stdout); // Ensures the text is displayed immediately
}

// Function to convert mouse button number to name
const char* mouse_button_to_name(int button) {
    switch (button) {
        case Button1:
            return "LEFT CLICK";
        case Button2:
            return "MIDDLE CLICK";
        case Button3:
            return "RIGHT CLICK";
        case Button4:
            return "WHEEL UP";
        case Button5:
            return "WHEEL DOWN";
        default:
            return "UNKNOWN BUTTON";
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

            // Update individual modifier key states
            switch (keysym) {
                case XK_Shift_L:
                    shift_l_pressed = is_key_press;
                    break;
                case XK_Shift_R:
                    shift_r_pressed = is_key_press;
                    break;
                case XK_Control_L:
                    ctrl_l_pressed = is_key_press;
                    break;
                case XK_Control_R:
                    ctrl_r_pressed = is_key_press;
                    break;
                case XK_Alt_L:
                    alt_l_pressed = is_key_press;
                    break;
                case XK_Alt_R:
                    alt_r_pressed = is_key_press;
                    break;
                case XK_Meta_L:
                    meta_l_pressed = is_key_press;
                    break;
                case XK_Meta_R:
                    meta_r_pressed = is_key_press;
                    break;
                default:
                    break;
            }

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
                                           keysym == XK_Meta_L || keysym == XK_Meta_R);

                    char modifiers_message[MAX_MESSAGE_LENGTH] = "";

                    // Include specific modifiers if they are pressed and are not the current key
                    if (ctrl_l_pressed && keysym != XK_Control_L)
                        strncat(modifiers_message, "CONTROL_L + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    if (ctrl_r_pressed && keysym != XK_Control_R)
                        strncat(modifiers_message, "CONTROL_R + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    if (alt_l_pressed && keysym != XK_Alt_L)
                        strncat(modifiers_message, "ALT_L + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    if (alt_r_pressed && keysym != XK_Alt_R)
                        strncat(modifiers_message, "ALT_R + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    if (shift_l_pressed && keysym != XK_Shift_L)
                        strncat(modifiers_message, "SHIFT_L + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    if (shift_r_pressed && keysym != XK_Shift_R)
                        strncat(modifiers_message, "SHIFT_R + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    if (meta_l_pressed && keysym != XK_Meta_L)
                        strncat(modifiers_message, "META_L + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);
                    if (meta_r_pressed && keysym != XK_Meta_R)
                        strncat(modifiers_message, "META_R + ", sizeof(modifiers_message) - strlen(modifiers_message) - 1);

                    // Add the key pressed
                    if (!(is_modifier_key && modifiers_message[0] == '\0')) {
                        // If the key is not a lone modifier, add it to the message
                        strncat(modifiers_message, uppercase_key, sizeof(modifiers_message) - strlen(modifiers_message) - 1);
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

                    print_centered(message);
                }
            }
        }
    }

    // Free the intercepted event data
    XRecordFreeData(data);
}

int main() {
    // Disable the cursor when starting the program
    disable_cursor();

    XRecordRange *range;
    XRecordClientSpec clients;
    Display *record_display = NULL;
    XRecordContext context;

    // Open the connection to the X server
    display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Error opening display.\n");
        enable_cursor();
        exit(1);
    }

    record_display = XOpenDisplay(NULL);
    if (record_display == NULL) {
        fprintf(stderr, "Error opening display for recording.\n");
        enable_cursor();
        XCloseDisplay(display);
        exit(1);
    }

    // Define the range of events we want to capture (mouse and keyboard)
    range = XRecordAllocRange();
    if (range == NULL) {
        fprintf(stderr, "Error allocating event range.\n");
        enable_cursor();
        XCloseDisplay(display);
        XCloseDisplay(record_display);
        exit(1);
    }
    range->device_events.first = KeyPress;
    range->device_events.last = ButtonRelease; // Includes mouse events

    clients = XRecordAllClients;

    // Create the recording context to capture events
    context = XRecordCreateContext(record_display, 0, &clients, 1, &range, 1);
    if (!context) {
        fprintf(stderr, "Error creating recording context.\n");
        enable_cursor();
        XFree(range);
        XCloseDisplay(display);
        XCloseDisplay(record_display);
        exit(1);
    }

    // Enable the recording context asynchronously
    if (!XRecordEnableContextAsync(record_display, context, event_callback, NULL)) {
        fprintf(stderr, "Error enabling recording context.\n");
        enable_cursor();
        XRecordFreeContext(record_display, context);
        XFree(range);
        XCloseDisplay(display);
        XCloseDisplay(record_display);
        exit(1);
    }

    // Infinite loop to process events
    while (1) {
        XRecordProcessReplies(record_display);
        usleep(10000);  // Small pause to avoid high CPU load
    }

    // Restore the cursor when exiting the program
    enable_cursor();

    // Free resources
    XRecordFreeContext(record_display, context);
    XFree(range);
    XCloseDisplay(display);
    XCloseDisplay(record_display);

    return 0;
}
