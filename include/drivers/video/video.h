#ifndef DRIVERS_VIDEO_H
#define DRIVERS_VIDEO_H
#include <dos/type.h>
#include <uapi/terminal.h>
/* Firmware timing wire format, 36 bytes; clocks are kHz, refresh Hz. */
struct video_mode {
    uint8_t display, pad0; uint16_t vic;
    uint32_t clock;
    uint16_t width, hs, he, ht, hskew, height, vs, ve, vt, vscan, refresh, pad1;
    uint32_t flags;
};
_Static_assert(sizeof(struct video_mode)==36,"firmware timing layout");
struct video_edid {struct video_mode preferred;unsigned int bpc;bool hdmi,valid,has_mode;};
bool video_mode_valid(const struct video_mode *);
uint32_t video_refresh(const struct video_mode *);
int video_edid_parse(const uint8_t *,size_t,uint32_t,struct video_edid *);
struct video_surface {uint8_t *pixels;size_t bytes;unsigned int width,height,pitch,depth,order;};
struct video_terminal {
    struct video_surface fb;
    unsigned int columns,rows,x,y,saved_x,saved_y,state,params[16],count;
    uint32_t foreground,background,cursor_pixels[6];
    unsigned int utf_left,utf_width;
    bool bold,inverse,private_mode,cursor_visible,cursor_drawn,wrap;
};
int video_terminal_init(struct video_terminal *,const struct video_surface *);
void video_terminal_write(struct video_terminal *,char);
uint8_t video_font_row(unsigned char,unsigned int);
#endif
