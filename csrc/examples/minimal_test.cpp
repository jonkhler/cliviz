#include <cstdio>
#include <unistd.h>

// Minimal terminal test — no library dependencies
int main() {
    // Enter alternate screen + hide cursor
    const char init[] = "\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H";
    write(STDOUT_FILENO, init, sizeof(init) - 1);

    // Draw a red block at row 5, col 10
    const char draw[] = "\x1b[5;10H\x1b[38;2;255;0;0m\x1b[48;2;0;0;0m\xe2\x96\x80";
    write(STDOUT_FILENO, draw, sizeof(draw) - 1);

    // Status bar at row 24, col 1
    const char status[] = "\x1b[24;1H\x1b[0m\x1b[7mPress any key to exit\x1b[0m";
    write(STDOUT_FILENO, status, sizeof(status) - 1);

    // Wait for keypress
    char c;
    read(STDIN_FILENO, &c, 1);

    // Restore
    const char restore[] = "\x1b[?25h\x1b[?1049l\x1b[0m";
    write(STDOUT_FILENO, restore, sizeof(restore) - 1);

    return 0;
}
