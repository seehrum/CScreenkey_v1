//  command to compile: gcc CScreenkey_v1.c -o screenkey -lX11 -lXtst

#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/record.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <ctype.h>  // Includes for toupper()

// Define the key map structure
typedef struct {
    KeySym keysym;
    const char* name;
} KeyMap;

// Special key map
KeyMap specialKeyMap[] = {
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

// Function to find special key by keysym
const char* find_special_key_name(KeySym keysym) {
    int map_size = sizeof(specialKeyMap) / sizeof(specialKeyMap[0]);
    for (int i = 0; i < map_size; i++) {
        if (specialKeyMap[i].keysym == keysym) {
            return specialKeyMap[i].name;
        }
    }
    return NULL;  // Return NULL if no match found
}

// Function to clear the screen
void clear_screen() {
    printf("\033[H\033[J");
}

// Function to get the terminal size
void get_terminal_size(int *rows, int *cols) {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    *rows = w.ws_row;
    *cols = w.ws_col;
}

// Function to print centered text
void print_centered(const char *text) {
    int rows, cols;
    get_terminal_size(&rows, &cols);  // Get terminal size
    int len = strlen(text);
    int padding_left = (cols - len) / 2;
    int padding_top = rows / 2;

    clear_screen();  // Clear screen

    // Add vertical padding
    for (int i = 0; i < padding_top; i++) {
        printf("\n");
    }

    // Add horizontal padding and print the text centered
    for (int i = 0; i < padding_left; i++) {
        printf(" ");
    }
    printf("%s\n", text);
}

// Function to convert string to uppercase
void to_uppercase(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = toupper(str[i]);
    }
}

// Function to handle mouse events
void print_mouse_event(const char *button_name) {
    char upper_button_name[256];
    strcpy(upper_button_name, button_name);
    to_uppercase(upper_button_name);
    print_centered(upper_button_name);  // Display the button pressed in the center
}

// Global variables for modifier key states
char ctrl_pressed[10] = "";
char shift_pressed[10] = "";
char alt_pressed[10] = "";
int mouse_button_pressed = 0;  // Indicates if a mouse button is pressed
char last_mouse_button[50] = "";  // Stores the name of the last mouse button pressed

// Function to handle keyboard and mouse events
void key_event(XPointer priv, XRecordInterceptData *hook) {
    if (hook->category == XRecordFromServer && hook->data != NULL) {
        unsigned char event_type = hook->data[0];  // Event type
        unsigned char keycode_or_button = hook->data[1];  // Key or button code

        // Check if the event is KeyPress or KeyRelease
        if (event_type == KeyPress || event_type == KeyRelease) {
            Display *dpy = XOpenDisplay(NULL);
            if (dpy == NULL) {
                fprintf(stderr, "Unable to open X display\n");
                return;
            }

            // Get KeySym from the keycode
            KeySym keysym = XKeycodeToKeysym(dpy, keycode_or_button, 0);
            if (keysym != NoSymbol) {
                const char *key_name = find_special_key_name(keysym);
                if (key_name == NULL) {
                    key_name = XKeysymToString(keysym);  // Fallback to keysym string
                }

                if (key_name != NULL) {
                    char display_string[256] = "";
                    strcpy(display_string, key_name);
                    to_uppercase(display_string);

                    // Handle KeyPress event
                    if (event_type == KeyPress) {
                        // If a mouse button is pressed, display mouse button + key
                        if (mouse_button_pressed) {
                            char display_combination[256] = "";
                            strcat(display_combination, last_mouse_button);
                            strcat(display_combination, " + ");
                            strcat(display_combination, display_string);
                            to_uppercase(display_combination);
                            print_centered(display_combination);

                            // Reset mouse button state after displaying
                            mouse_button_pressed = 0;
                            last_mouse_button[0] = '\0';
                        } else {
                            // Update modifier key states
                            if (strcmp(display_string, "CONTROL_L") == 0 || strcmp(display_string, "CONTROL_R") == 0) {
                                strcpy(ctrl_pressed, display_string);
                                print_centered(display_string);  // Display the modifier key alone
                            } else if (strcmp(display_string, "SHIFT_L") == 0 || strcmp(display_string, "SHIFT_R") == 0) {
                                strcpy(shift_pressed, display_string);
                                print_centered(display_string);  // Display the modifier key alone
                            } else if (strcmp(display_string, "ALT_L") == 0 || strcmp(display_string, "ALT_R") == 0) {
                                strcpy(alt_pressed, display_string);
                                print_centered(display_string);  // Display the modifier key alone
                            } else {
                                // Display key with modifiers, if any
                                char display_combination[256] = "";

                                if (strlen(ctrl_pressed) > 0) {
                                    strcat(display_combination, ctrl_pressed);
                                    strcat(display_combination, " + ");
                                }
                                if (strlen(shift_pressed) > 0) {
                                    strcat(display_combination, shift_pressed);
                                    strcat(display_combination, " + ");
                                }
                                if (strlen(alt_pressed) > 0) {
                                    strcat(display_combination, alt_pressed);
                                    strcat(display_combination, " + ");
                                }

                                strcat(display_combination, display_string);
                                to_uppercase(display_combination);

                                print_centered(display_combination);
                            }
                        }
                    }
                    // Handle KeyRelease event to reset the state
                    else if (event_type == KeyRelease) {
                        if (strcmp(display_string, "CONTROL_L") == 0 || strcmp(display_string, "CONTROL_R") == 0) {
                            ctrl_pressed[0] = '\0';  // Clear the Control key state
                        } else if (strcmp(display_string, "SHIFT_L") == 0 || strcmp(display_string, "SHIFT_R") == 0) {
                            shift_pressed[0] = '\0';  // Clear the Shift key state
                        } else if (strcmp(display_string, "ALT_L") == 0 || strcmp(display_string, "ALT_R") == 0) {
                            alt_pressed[0] = '\0';  // Clear the Alt key state
                        }
                    }
                }
            }

            XCloseDisplay(dpy);
        }
        // Check if the event is ButtonPress or ButtonRelease (mouse)
        else if (event_type == ButtonPress || event_type == ButtonRelease) {
            const char *button_name = NULL;

            switch (keycode_or_button) {
                case 1:
                    button_name = "LEFT BUTTON";
                    break;
                case 2:
                    button_name = "MIDDLE BUTTON";
                    break;
                case 3:
                    button_name = "RIGHT BUTTON";
                    break;
                case 4:
                    button_name = "MOUSE WHEEL UP";
                    break;
                case 5:
                    button_name = "MOUSE WHEEL DOWN";
                    break;
            }

            if (button_name != NULL) {
                if (event_type == ButtonPress) {
                    mouse_button_pressed = 1;  // Set mouse pressed state
                    strcpy(last_mouse_button, button_name);  // Store the mouse button name
                } else if (event_type == ButtonRelease) {
                    mouse_button_pressed = 0;  // Reset mouse pressed state
                    last_mouse_button[0] = '\0';  // Clear the mouse button name
                }

                print_mouse_event(button_name);  // Display the button pressed
            }
        }
    }

    XRecordFreeData(hook);
}

int main() {
    Display *display;
    XRecordContext context;
    XRecordRange *range;
    XRecordClientSpec clients;

    display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Unable to open X display\n");
        exit(1);
    }

    range = XRecordAllocRange();
    if (!range) {
        fprintf(stderr, "Unable to allocate XRecord range\n");
        exit(1);
    }

    // Capture only KeyPress and ButtonPress events
    range->device_events.first = KeyPress;
    range->device_events.last = ButtonPress;

    clients = XRecordAllClients;

    // Create recording context
    context = XRecordCreateContext(display, 0, &clients, 1, &range, 1);
    if (!context) {
        fprintf(stderr, "Unable to create XRecord context\n");
        exit(1);
    }

    // Enable the recording context to capture events
    if (!XRecordEnableContext(display, context, key_event, NULL)) {
        fprintf(stderr, "Unable to enable XRecord context\n");
        exit(1);
    }

    // Free resources
    XRecordFreeContext(display, context);
    XCloseDisplay(display);

    return 0;
}
