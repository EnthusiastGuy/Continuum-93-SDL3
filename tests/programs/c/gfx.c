// steps: 3000000
// continuum.h: video pages, palette, rectangles, lines, ellipses, plots, built-in text, sprites and the clock.
#include <continuum.h>

unsigned char sprite[8 * 8];
int out[8];

int main(void)
{
    c93_set_video_pages(2);
    c93_set_color(0, 1, 255, 0, 0);
    c93_set_color(0, 2, 0, 255, 0);
    c93_clear(0, 3);
    c93_clear(1, 0);
    c93_fill_rect(0, 10, 10, 60, 40, 1);
    c93_rect(0, 5, 5, 80, 60, 2);
    c93_fill_round_rect(0, 100, 20, 50, 30, 4, 6);
    c93_round_rect(0, 160, 20, 50, 30, 5, 6);
    c93_line(0, 0, 0, 479, 269, 6);
    c93_ellipse(0, 300, 150, 40, 20, 7);
    c93_fill_ellipse(1, 200, 200, 25, 15, 8);
    for (int i = 0; i < 50; i++) c93_plot(1, i * 3, 100 + i, (i & 7) + 1);
    for (int i = 0; i < 64; i++) sprite[i] = (unsigned char)((i ^ (i >> 3)) & 3);
    c93_draw_sprite(sprite, 1, 400, 10, 8, 8);
    c93_draw_text(0, 20, 220, 2, 9, "HELLO 93");
    out[0] = c93_text_width("HELLO 93", 2);
    unsigned char *page = c93_video_page(0);
    out[1] = page[12 * C93_SCREEN_WIDTH + 12];
    out[2] = page[5 * C93_SCREEN_WIDTH + 40];
    unsigned char *pal = c93_palette(0);
    out[3] = pal[3] + pal[7] * 256;
    c93_copy(sprite, page, 64);
    c93_fill(page, 9, 16);
    out[4] = sprite[63];
    return out[0];
}
