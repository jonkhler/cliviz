// Pipe raw RGB frames from stdin to the terminal.
//
// Usage:
//   ffmpeg -i video.mp4 -f rawvideo -pix_fmt rgb24 -s WxH - | ./build/pipe
//
// Where WxH matches your terminal's pixel resolution (cols × rows*2).
// To get your terminal size: tput cols; echo x; echo $(($(tput lines) * 2))

#include <cstdio>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "outbuf.h"
#include "pixbuf.h"
#include "term.h"

using namespace cliviz;

int main() {
    // When piping, stdin is the pipe and stdout is the terminal.
    // We need to set up raw mode on the terminal (via /dev/tty)
    // while reading video data from stdin.
    int tty_fd = open("/dev/tty", O_RDWR);
    if (tty_fd < 0) {
        std::fprintf(stderr, "Cannot open /dev/tty\n");
        return 1;
    }

    // Save and set raw mode on the TTY
    struct termios orig;
    tcgetattr(tty_fd, &orig);
    struct termios raw = orig;
    raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON);
    tcsetattr(tty_fd, TCSAFLUSH, &raw);

    // Get terminal size from the TTY
    struct winsize ws{};
    ioctl(tty_fd, TIOCGWINSZ, &ws);
    uint16_t cols = ws.ws_col;
    uint16_t rows = ws.ws_row;

    if (cols == 0 || rows == 0) {
        std::fprintf(stderr, "Cannot determine terminal size\n");
        tcsetattr(tty_fd, TCSAFLUSH, &orig);
        close(tty_fd);
        return 1;
    }

    // Enter alternate screen + hide cursor on stdout (which IS the terminal)
    const char init[] = "\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H";
    ::write(STDOUT_FILENO, init, sizeof(init) - 1);

    auto pb = PixelBuffer::create(cols, rows);
    OutputBuffer outbuf;
    outbuf.color_mode = detect_color_mode();

    uint32_t frame_bytes = pb->width * pb->height * 3;

    while (true) {
        // Read one frame of raw RGB from stdin (the pipe)
        uint32_t read_total = 0;
        while (read_total < frame_bytes) {
            auto n = ::read(STDIN_FILENO, pb->pixels + read_total,
                            frame_bytes - read_total);
            if (n <= 0) goto done;
            read_total += static_cast<uint32_t>(n);
        }

        pb->encode_all();
        outbuf.clear();
        pb->fb->flush(outbuf);
        outbuf.flush();
    }

done:
    // Restore terminal
    const char restore[] = "\x1b[?25h\x1b[?1049l\x1b[0m";
    ::write(STDOUT_FILENO, restore, sizeof(restore) - 1);
    tcsetattr(tty_fd, TCSAFLUSH, &orig);
    close(tty_fd);
    return 0;
}
