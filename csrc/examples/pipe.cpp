// Pipe raw RGB frames from stdin to the terminal.
//
// Usage:
//   ffmpeg -i video.mp4 -f rawvideo -pix_fmt rgb24 -s WxH - | ./build/pipe
//
// Where WxH matches your terminal's pixel resolution (cols × rows*2).
// To get your terminal size: tput cols; echo x; echo $(($(tput lines) * 2))

#include <cstdio>
#include <unistd.h>

#include "outbuf.h"
#include "pixbuf.h"
#include "term.h"

using namespace cliviz;

int main() {
    if (!term_init()) {
        std::fprintf(stderr, "Failed to init terminal\n");
        return 1;
    }

    TermSize ts = term_get_size();
    auto pb = PixelBuffer::create(ts.cols, ts.rows);
    OutputBuffer outbuf;
    outbuf.color_mode = detect_color_mode();

    uint32_t frame_bytes = pb->width * pb->height * 3;

    while (true) {
        // Read one frame of raw RGB from stdin
        uint32_t read_total = 0;
        while (read_total < frame_bytes) {
            auto n = ::read(STDIN_FILENO, pb->pixels + read_total,
                            frame_bytes - read_total);
            if (n <= 0) goto done; // EOF or error
            read_total += static_cast<uint32_t>(n);
        }

        pb->encode_all();
        outbuf.clear();
        pb->fb->flush(outbuf);
        outbuf.flush();
    }

done:
    term_shutdown();
    return 0;
}
