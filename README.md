## CScreenkey_v1

### Description:
This program captures keyboard and mouse events in a Linux environment using the X11 library and the XTest extension. It detects and displays keys or mouse buttons pressed, including special keys (e.g., arrow keys, page up/down) and mouse clicks. The captured events are displayed in the terminal, with the key or button name being centered on the screen.

### Features:
- Captures keyboard key presses and releases.
- Detects and displays special keys (e.g., arrow keys, page up, page down).
- Captures mouse button presses and releases.
- Displays the key/button pressed centered in the terminal.
- Supports modifier keys (Control, Shift, Alt) and shows key combinations.

### Prerequisites:
- **Linux environment** with X11 display system.
- **X11 development libraries** (`libX11`, `libXtst`).
  
  You can install the required libraries using the following command (on Debian/Ubuntu-based systems):
  ```bash
  sudo apt-get install libx11-dev libxtst-dev
  ```

### Compilation:
To compile the program, use the following `gcc` command:

```bash
gcc CScreenkey_v1.c -o screenkey -lX11 -lXtst
```

### Usage:
1. **Run the compiled executable:**
   ```bash
   ./screenkey
   ```
2. The program will start capturing and displaying key presses and mouse clicks in the terminal.
3. Press **Ctrl+C** to stop the program.

### Special Keys Mapped:
- Arrow keys: Left, Right, Up, Down.
- Keypad: Add, Subtract, Multiply, Divide.
- Brackets: Left ([), Right (]).
- Other symbols: Apostrophe, Slash, Backslash, Comma, Period, Semicolon, etc.
- Modifier keys: Control, Shift, Alt.
- Mouse buttons: Left, Middle, Right, Mouse Wheel Up, Mouse Wheel Down.

### Files:
- `CScreenkey_v1.c`: The main C source code file.

### Limitations:
- This program is designed to work only in Linux environments with X11 support.
- It does not support other operating systems or windowing systems like Wayland or Windows.

### License:
MIT License
