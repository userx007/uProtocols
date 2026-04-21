/*
 * ssd1306_i2c.c — Minimal SSD1306 OLED driver via /dev/i2c-N
 *
 * Wiring (I2C):
 *   SSD1306 SDA  →  SDA pin of your board (e.g. GPIO2  on RPi)
 *   SSD1306 SCL  →  SCL pin of your board (e.g. GPIO3  on RPi)
 *   SSD1306 VCC  →  3.3V
 *   SSD1306 GND  →  GND
 *
 * Build:
 *   gcc -O2 -Wall -o ssd1306_i2c ssd1306_i2c.c
 *
 * Run:
 *   ./ssd1306_i2c           # uses /dev/i2c-1, address 0x3C by default
 *   ./ssd1306_i2c 2 0x3D    # bus 2, alternate address
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

/* ── Display geometry ───────────────────────────────────────────────────── */
#define SSD1306_W       128
#define SSD1306_H        64
#define SSD1306_PAGES   (SSD1306_H / 8)   /* 8 rows of 8 px each = 8 pages */

/* ── I2C control bytes ──────────────────────────────────────────────────── */
#define CTRL_CMD        0x00   /* following bytes are commands */
#define CTRL_DATA       0x40   /* following bytes are GDDRAM data */

/* ── SSD1306 command set (subset) ───────────────────────────────────────── */
#define CMD_DISPLAY_OFF         0xAE
#define CMD_DISPLAY_ON          0xAF
#define CMD_SET_CONTRAST        0x81
#define CMD_ENTIRE_DISPLAY_RAM  0xA4   /* output follows RAM */
#define CMD_NORMAL_DISPLAY      0xA6   /* 0 in RAM = dark pixel */
#define CMD_SET_OSC_FREQ        0xD5
#define CMD_SET_MUX             0xA8
#define CMD_SET_DISPLAY_OFFSET  0xD3
#define CMD_SET_START_LINE      0x40   /* 0x40–0x7F */
#define CMD_CHARGE_PUMP         0x8D
#define CMD_MEM_ADDR_MODE       0x20
#define CMD_SEG_REMAP           0xA1   /* mirror horizontally */
#define CMD_COM_SCAN_DEC        0xC8   /* mirror vertically */
#define CMD_SET_COM_PINS        0xDA
#define CMD_SET_PRECHARGE       0xD9
#define CMD_SET_VCOM_DESEL      0xDB
#define CMD_SET_COL_ADDR        0x21
#define CMD_SET_PAGE_ADDR       0x22

/* ── Framebuffer ────────────────────────────────────────────────────────── */
static uint8_t fb[SSD1306_PAGES][SSD1306_W];  /* 1 bit per pixel, 8 px/byte */

/* ── Low-level I2C helpers ──────────────────────────────────────────────── */

/*
 * Write a buffer prefixed with a control byte over I2C.
 * The SSD1306 expects:  [ control_byte | payload... ]
 */
static int i2c_write(int fd, uint8_t ctrl, const uint8_t *data, size_t len)
{
    /* +1 for the control byte prepended to every transfer */
    uint8_t *buf = malloc(1 + len);
    if (!buf) return -ENOMEM;

    buf[0] = ctrl;
    memcpy(buf + 1, data, len);

    ssize_t ret = write(fd, buf, 1 + len);
    free(buf);

    if (ret < 0) {
        perror("i2c write");
        return -errno;
    }
    return 0;
}

/* Send a single command byte */
static int cmd(int fd, uint8_t c)
{
    return i2c_write(fd, CTRL_CMD, &c, 1);
}

/* Send two command bytes (command + argument) */
static int cmd2(int fd, uint8_t c, uint8_t arg)
{
    uint8_t buf[2] = { c, arg };
    return i2c_write(fd, CTRL_CMD, buf, sizeof(buf));
}

/* ── Initialisation sequence ────────────────────────────────────────────── */
static int ssd1306_init(int fd)
{
    const uint8_t init_seq[] = {
        CMD_DISPLAY_OFF,
        CMD_SET_OSC_FREQ,   0x80,
        CMD_SET_MUX,        0x3F,          /* 1/64 duty */
        CMD_SET_DISPLAY_OFFSET, 0x00,
        CMD_SET_START_LINE | 0x00,
        CMD_CHARGE_PUMP,    0x14,          /* enable internal charge pump */
        CMD_MEM_ADDR_MODE,  0x00,          /* horizontal addressing */
        CMD_SEG_REMAP,                     /* col 127 → SEG0 */
        CMD_COM_SCAN_DEC,
        CMD_SET_COM_PINS,   0x12,
        CMD_SET_CONTRAST,   0xCF,
        CMD_SET_PRECHARGE,  0xF1,
        CMD_SET_VCOM_DESEL, 0x40,
        CMD_ENTIRE_DISPLAY_RAM,
        CMD_NORMAL_DISPLAY,
        CMD_DISPLAY_ON,
    };
    return i2c_write(fd, CTRL_CMD, init_seq, sizeof(init_seq));
}

/* ── Framebuffer → display ──────────────────────────────────────────────── */

/* Flush the entire framebuffer to the SSD1306 */
static int ssd1306_flush(int fd)
{
    /* Set column address range: 0 – 127 */
    uint8_t col_addr[] = { CMD_SET_COL_ADDR,  0, SSD1306_W - 1 };
    i2c_write(fd, CTRL_CMD, col_addr, sizeof(col_addr));

    /* Set page address range: 0 – 7 */
    uint8_t page_addr[] = { CMD_SET_PAGE_ADDR, 0, SSD1306_PAGES - 1 };
    i2c_write(fd, CTRL_CMD, page_addr, sizeof(page_addr));

    /*
     * Send all pages in one shot.
     * Each page is SSD1306_W bytes; total = PAGES × W = 1024 bytes.
     * The kernel i2c-dev limit is 8 KB, so this fits fine.
     */
    return i2c_write(fd, CTRL_DATA, (uint8_t *)fb, sizeof(fb));
}

/* ── Drawing primitives ─────────────────────────────────────────────────── */

static void fb_clear(void) { memset(fb, 0, sizeof(fb)); }
static void fb_fill (void) { memset(fb, 0xFF, sizeof(fb)); }

/*
 * Set/clear a single pixel at (x, y).
 * The SSD1306 stores pixels vertically: each byte in a page covers 8 rows.
 *   page  = y / 8
 *   bit   = y % 8   (bit 0 = top of page)
 */
static void fb_pixel(int x, int y, int on)
{
    if (x < 0 || x >= SSD1306_W || y < 0 || y >= SSD1306_H) return;
    if (on) fb[y / 8][x] |=  (1 << (y % 8));
    else    fb[y / 8][x] &= ~(1 << (y % 8));
}

/* Bresenham line */
static void fb_line(int x0, int y0, int x1, int y1, int on)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2, e2;
    for (;;) {
        fb_pixel(x0, y0, on);
        if (x0 == x1 && y0 == y1) break;
        e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 <  dy) { err += dx; y0 += sy; }
    }
}

/* Hollow rectangle */
static void fb_rect(int x, int y, int w, int h, int on)
{
    fb_line(x,       y,       x+w-1, y,       on);  /* top    */
    fb_line(x,       y+h-1,   x+w-1, y+h-1,   on);  /* bottom */
    fb_line(x,       y,       x,     y+h-1,   on);  /* left   */
    fb_line(x+w-1,   y,       x+w-1, y+h-1,   on);  /* right  */
}

/* Midpoint circle */
static void fb_circle(int cx, int cy, int r, int on)
{
    int x = 0, y = r, d = 1 - r;
    while (x <= y) {
        fb_pixel(cx+x, cy+y, on); fb_pixel(cx-x, cy+y, on);
        fb_pixel(cx+x, cy-y, on); fb_pixel(cx-x, cy-y, on);
        fb_pixel(cx+y, cy+x, on); fb_pixel(cx-y, cy+x, on);
        fb_pixel(cx+y, cy-x, on); fb_pixel(cx-y, cy-x, on);
        if (d < 0) d += 2*x + 3; else { d += 2*(x-y) + 5; y--; }
        x++;
    }
}

/* ── Demo scene ─────────────────────────────────────────────────────────── */
static void demo_scene(void)
{
    fb_clear();
    fb_rect  (0,  0, 128, 64, 1);          /* outer border     */
    fb_rect  (4,  4, 120, 56, 1);          /* inner border     */
    fb_circle(64, 32, 20,    1);           /* centre circle    */
    fb_line  (10, 32, 118, 32, 1);         /* horizontal cross */
    fb_line  (64,  8,  64, 56, 1);         /* vertical cross   */
}

/* ── main ───────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    const char *dev  = (argc > 1) ? argv[1] : "/dev/i2c-1";
    int         addr = (argc > 2) ? (int)strtol(argv[2], NULL, 0) : 0x3C;

    printf("Opening %s, SSD1306 @ 0x%02X\n", dev, addr);

    int fd = open(dev, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    /* Tell i2c-dev which slave to address for plain read()/write() calls */
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        perror("ioctl I2C_SLAVE");
        close(fd);
        return 1;
    }

    if (ssd1306_init(fd) < 0) {
        fprintf(stderr, "Initialisation failed\n");
        close(fd);
        return 1;
    }

    /* ── scene 1: fill (all pixels on) ── */
    printf("Fill...\n");
    fb_fill();
    ssd1306_flush(fd);
    sleep(1);

    /* ── scene 2: clear ── */
    printf("Clear...\n");
    fb_clear();
    ssd1306_flush(fd);
    sleep(1);

    /* ── scene 3: demo geometry ── */
    printf("Demo scene...\n");
    demo_scene();
    ssd1306_flush(fd);
    sleep(5);

    /* ── teardown: display off ── */
    cmd(fd, CMD_DISPLAY_OFF);
    close(fd);
    printf("Done.\n");
    return 0;
}