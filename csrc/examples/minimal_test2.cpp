#include <cstdio>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

int main() {
    // Mimic term_init exactly
    struct termios orig;
    tcgetattr(STDIN_FILENO, &orig);

    struct termios raw = orig;
    raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(tcflag_t)(OPOST);
    raw.c_cflag |= (tcflag_t)(CS8);
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    setvbuf(stdout, nullptr, _IONBF, 0);

    // Alt screen
    const char init[] = "\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H";
    write(STDOUT_FILENO, init, sizeof(init) - 1);

    // Get terminal size
    struct winsize ws;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    int rows = ws.ws_row;

    // Draw some cells with explicit cursor positioning
    for (int r = 1; r <= 3; r++) {
        for (int c = 1; c <= 10; c++) {
            char buf[64];
            int n = snprintf(buf, sizeof(buf),
                "\x1b[%d;%dH\x1b[38;2;255;0;0m\x1b[48;2;0;0;50m\xe2\x96\x80",
                r, c);
            write(STDOUT_FILENO, buf, n);
        }
    }

    // Status at last row
    char status[128];
    int n = snprintf(status, sizeof(status), "\x1b[%d;1H\x1b[0m\x1b[7mPress q to exit\x1b[0m", rows);
    write(STDOUT_FILENO, status, n);

    // Read keys
    while (true) {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1 && c == 'q') break;
        usleep(16000);
    }

    // Restore
    const char restore[] = "\x1b[?25h\x1b[?1049l\x1b[0m";
    write(STDOUT_FILENO, restore, sizeof(restore) - 1);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
    return 0;
}
