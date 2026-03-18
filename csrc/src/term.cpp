#include "term.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "outbuf.h"

namespace cliviz {
namespace {

bool g_active = false;
volatile sig_atomic_t g_resized = 0;
struct termios g_orig_termios{};

void winch_handler(int /*sig*/) { g_resized = 1; }

void restore_terminal() {
    if (!g_active) return;
    g_active = false;

    // Restore cursor, leave alternate screen, reset attributes
    const char restore_seq[] =
        "\x1b[?25h"    // show cursor
        "\x1b[?1049l"  // leave alternate screen
        "\x1b[0m";     // reset attributes
    ::write(STDOUT_FILENO, restore_seq, sizeof(restore_seq) - 1);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
}

void signal_handler(int /*sig*/) {
    restore_terminal();
    _exit(1);
}

} // namespace

TermSize term_get_size() {
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        return {0, 0};
    }
    return {ws.ws_col, ws.ws_row};
}

bool term_init() {
    if (g_active) return true;
    if (!isatty(STDOUT_FILENO)) return false;

    // Save original terminal state
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) == -1) return false;

    // Enter raw mode
    struct termios raw = g_orig_termios;
    raw.c_iflag &= ~static_cast<tcflag_t>(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~static_cast<tcflag_t>(OPOST);
    raw.c_cflag |= static_cast<tcflag_t>(CS8);
    raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) return false;

    // Disable stdout buffering — we manage our own buffer
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // Register cleanup
    std::atexit(restore_terminal);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGWINCH, winch_handler);

    // Enter alternate screen, hide cursor
    const char init_seq[] =
        "\x1b[?1049h"  // alternate screen
        "\x1b[?25l"    // hide cursor
        "\x1b[2J"      // clear screen
        "\x1b[H";      // cursor to home
    ::write(STDOUT_FILENO, init_seq, sizeof(init_seq) - 1);

    g_active = true;
    return true;
}

void term_shutdown() { restore_terminal(); }

bool term_is_active() { return g_active; }

bool term_was_resized() {
    if (g_resized) {
        g_resized = 0;
        return true;
    }
    return false;
}

} // namespace cliviz
