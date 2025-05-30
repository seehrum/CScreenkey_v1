
# TermKey

TermKey is a Unix-based program that captures and processes keyboard and mouse events using the X11 system, displaying these events in the terminal. This tool is highly useful for logging input events in real-time, with optional color-coded display for improved visual feedback.

## Features

- **Real-Time Event Capture**: Captures and logs both keyboard and mouse events in real-time.
- **Color-Coded Display**: Optional background and foreground colors for enhanced visibility of events.
- **Support for Special Keys**: Recognizes and maps special keys such as Shift, Control, Alt, Meta, and more.
- **Customizable Output**: Choose your preferred colors for background and foreground, with a list of supported colors.
- **Minimal System Resource Usage**: Designed to run efficiently with low CPU usage.

## Requirements

To build and run TermKey, you will need the following dependencies installed on your system:

- X11 development libraries (`libx11-dev`, `libxkbfile-dev`)

On Debian-based systems (like Ubuntu), you can install them with:

```bash
sudo apt-get install libx11-dev libxkbfile-dev libxtst-dev x11proto-dev libxext-dev
```

## Building TermKey

1. Clone the repository or download the source code.
2. Compile the code using GCC:

```bash
gcc -o termkey termkey.c -lX11 -lXtst
```

This will create an executable file called `termkey`.

## Usage

Run the program from the terminal:

```bash
./termkey -c bg_color fg_color text_color
```

- `bg_color`: Background color
- `fg_color`: Foreground color

### Example Commands:

1. **Set Background to Red and Text to Blue**:
   ```bash
   ./termkey -c --bg=red --text=blue
   ```

2. **Set Background to Red and Text to Default**:
   ```bash
   ./termkey -c --bg=red --text=default
   ```

3. **Set text color to green**:
   ```bash
   ./termkey -c --text=green
   ```
   
4. **Set Background to Black and Foreground to White**:
   ```bash
   ./termkey -c --bg=black --fg=white
   ```

### Supported Colors

The following colors are supported for both background and foreground:

- `black`
- `red`
- `green`
- `yellow`
- `blue`
- `magenta`
- `cyan`
- `white`
- `default`

## Program Behavior

- **Key Logging**: Captures key presses, including special keys and modifier keys, and displays them in real-time.
- **Mouse Events**: Captures mouse clicks and wheel movements.
- **Centered Output**: Displays the captured event centrally within the terminal window for easy visibility.
- **Cursor Control**: Hides the cursor during program execution to prevent clutter and re-enables it upon exit.

## Signal Handling

TermKey gracefully handles termination signals like `SIGINT` (Ctrl+C) or `SIGTERM`. Upon receiving these signals, the program will:

- Re-enable the terminal cursor.
- Close X11 connections properly.
- Exit the program without leaving the terminal in an inconsistent state.

## License

This project is licensed under the MIT License - see the LICENSE file for details.
