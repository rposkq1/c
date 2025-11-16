#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <ncurses.h> //ncurses
#include <signal.h> // For signal handling
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define STATIC_WIDTH 320 // default and maximum width
#define STATIC_HEIGHT 240 // default and maximum height

const char* fbdev = "/dev/fb0";

///////////////////////////////////////////////////////STRUCTS//ENUMS//ONIONS
enum Render {
    SCALE = 1,
    CENTRED = 2,
    NO_SCALING = 3
};

union Color {
    uint32_t color;
    struct
    {
        uint8_t blue;
        uint8_t green;
        uint8_t red;
    };
    //  uint8_t rgb[4]; // index: 0 - blue; 1 - green; 2 - red; 3 - irrevelant
};

typedef struct
{
    uint32_t fb[STATIC_HEIGHT][STATIC_WIDTH]; // pixel data
    uint32_t fb_size;
    uint16_t x, y;
} frame_t;
frame_t frame; // make a global frame

typedef struct
{
    bool help;
    int seed;
    bool seed_set;
    uint16_t x, y;
    bool x_set, y_set;
    enum Render render;
} args_t;

typedef struct
{
    int fd;
    int w;
    int h;
    int fb_data_size;
    uint8_t* fb_data;
    char* fbdev;
    int fb_bytes;
    int stride;
    bool linear;
} frame_buffer_t;

//////////////////////////////////////////////////////FUNCTIONS
args_t parseArgs(int argc, char** argv)
{
    args_t args;
    args.help = false;
    args.seed_set = false;
    args.x_set = false;
    args.y_set = false;
    args.render = SCALE;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            args.help = true;
        } else if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "-X") == 0) {
            args.x_set = true;
            if (++i < argc && atoi(argv[i]) > 0 && atoi(argv[i]) <= STATIC_WIDTH) {
                args.x = (int)strtol(argv[i], NULL, 10);
            } else {
                fprintf(stderr,
                    "no x (width) after -x flag, and x cannot be bigger "
                    "than %d\n",
                    STATIC_WIDTH);
                exit(1);
            }
        } else if (strcmp(argv[i], "-y") == 0 || strcmp(argv[i], "-Y") == 0) {
            args.y_set = true;
            if (++i < argc && atoi(argv[i]) > 0 && atoi(argv[i]) <= STATIC_HEIGHT) {
                args.y = atoi(argv[i]);
            } else {
                fprintf(stderr,
                    "no y (height) after -y flag and y cannot be bigger "
                    "than %d\n",
                    STATIC_HEIGHT);
                exit(1);
            }
        } else if (strcmp(argv[i], "-s") == 0) {
            args.seed_set = true;
            if (++i < argc) {
                args.seed = atoi(argv[i]);
            } else {
                fprintf(stderr, "no seed after -s (seed) flag\n");
                exit(1);
            }
        } else if (strcmp(argv[i], "-c") == 0) {
            args.render = CENTRED;
        } else if (strcmp(argv[i], "-n") == 0) {
            args.render = NO_SCALING;
        }
    }

    if (args.help) {
        printf("help placeholder\n\n");
        exit(1);
    }

    return args;
}

////////////////////////////////////////////////////////////////////framebuffer_create
frame_buffer_t* framebuffer_create(const char* fbdev)
{
    frame_buffer_t* self = malloc(sizeof(frame_buffer_t));
    self->fbdev = strdup(fbdev);
    self->fd = -1;
    self->fb_data = NULL;
    self->fb_data_size = 0;
    return self;
}

/////////////////////////////////////////////////////////////////////framebuffer_init
bool framebuffer_init(frame_buffer_t* self, char** error)
{
    bool ret = FALSE;
    self->fd = open(self->fbdev, O_RDWR);
    if (self->fd >= 0) {
        struct fb_var_screeninfo vinfo;
        struct fb_fix_screeninfo finfo;

        ioctl(self->fd, FBIOGET_FSCREENINFO, &finfo);
        ioctl(self->fd, FBIOGET_VSCREENINFO, &vinfo);

        self->w = vinfo.xres;
        self->h = vinfo.yres;
        int fb_bpp = vinfo.bits_per_pixel;
        int fb_bytes = fb_bpp / 8;
        self->fb_bytes = fb_bytes;
        self->stride = finfo.line_length;
        self->fb_data_size = self->stride * self->h;

        if (self->stride == self->w * self->fb_bytes)
            self->linear = true;
        else
            self->linear = false;

        self->fb_data = mmap(0,
            self->fb_data_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            self->fd,
            (off_t)0);

        ret = true;
    } else {
        if (error)
            asprintf(error, "Can't open framebuffer: %s", strerror(errno));
    }
    return ret;
}

///////////////////////////////////////////////////////////////////////framebuffer_deinit
void framebuffer_deinit(frame_buffer_t* self)
{
    if (self) {
        if (self->fb_data) {
            munmap(self->fb_data, self->fb_data_size);
            self->fb_data = NULL;
        }
        if (self->fd != -1) {
            close(self->fd);
            self->fd = -1;
        }
    }
}

////////////////////////////////////////////////////////////////////////////framebuffer_destroy
void framebuffer_destroy(frame_buffer_t* self)
{
    framebuffer_deinit(self);
    if (self) {
        if (self->fbdev)
            free(self->fbdev);
        free(self);
    }
}

/////////////////////////////////////////////////////////////////////////////////renderers
void render_no_scaling(frame_buffer_t* self, frame_t* fb)
{
    for (int y = 0; y < fb->y; y++) {
        for (int x = 0; x < fb->x; x++) {
            uint32_t color = fb->fb[y][x];

            long location = (x * self->fb_bytes + (y * self->stride));

            *((uint32_t*)(self->fb_data + location)) = color; // Set the pixel color
        }
    }
}

void render(frame_buffer_t* self, frame_t* fb)
{
    float scale_x = (float)self->w / fb->x;
    float scale_y = (float)self->h / fb->y;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;

    for (int y = 0; y < self->h; y++) {
        for (int x = 0; x < self->w; x++) {
            int sx = (int)(x / scale);
            int sy = (int)(y / scale);

            long location = (x * self->fb_bytes + (y * self->stride));
            uint32_t color;

            if (sx < fb->x && sy < fb->y) {
                color = fb->fb[sy][sx];
            } else {
                color = 0x00000000; // Black pixel
            }

            *((uint32_t*)(self->fb_data + location)) = color;
        }
    }
}
/* // not available bc of duplication of 1 column on left
void renderCenter(frame_buffer_t *self, Frame *fb) {
    float scale_x = (float)self->w / fb->x;
    float scale_y = (float)self->h / fb->y;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;

    int offset_x = (self->w - (fb->x * scale)) / 2;
    int offset_y = (self->h - (fb->y * scale)) / 2;

    for (int y = 0; y < self->h; y++) {
        for (int x = 0; x < self->w; x++) {
            int sx = (int)((x - offset_x) / scale);
            int sy = (int)((y - offset_y) / scale);

            long location = (x * self->fb_bytes + (y * self->stride));
            uint32_t color;

            if (sx >= 0 && sx < fb->x && sy >= 0 && sy < fb->y) {
                color = fb->fb[sy][sx];
            } else {
                color = 0x0;
            }

            *((uint32_t *)(self->fb_data + location)) = color;
        }
    }
}
*/
void create_rainbow(frame_t* fb)
{
    union Color c = { 0 };
    for (int y = 0; y < fb->y; y++) {
        for (int x = 0; x < fb->x; x++) {
            c.red = (x * 255) / fb->x;
            c.green = (y * 255) / fb->y;
            c.blue = 255 - ((x * 255) / fb->x);
            fb->fb[y][x] = c.color;
        }
    }
}

void random_framebuffer(frame_t* fb)
{
    union Color c;
    for (int y = 0; y < fb->y; y++) {
        for (int x = 0; x < fb->x; x++) {
            c.red = (uint8_t)rand();
            c.green = (uint8_t)rand();
            c.blue = (uint8_t)rand();
            fb->fb[y][x] = c.color;
        }
    }
}

void random_grayscale_framebuffer(frame_t* fb)
{
    uint8_t col;
    union Color c;
    for (int y = 0; y < fb->y; y++) {
        for (int x = 0; x < fb->x; x++) {
            col = (uint8_t)rand();
            c.red = col;
            c.green = col;
            c.blue = col;
            fb->fb[y][x] = c.color;
        }
    }
}

void create_checkerboard(frame_t* fb)
{
    for (int y = 0; y < fb->y; y++) {
        for (int x = 0; x < fb->x; x++) {
            fb->fb[y][x] = ((x % 2 == 0) == (y % 2 == 0)) ? 0xffffff : 0x0;
        }
    }
}

void fill_with_color_framebuffer(frame_t* fb, union Color c)
{
    for (int y = 0; y < fb->y; y++) {
        for (int x = 0; x < fb->x; x++) {
            fb->fb[y][x] = c.color;
        }
    }
}

void cleanup(frame_buffer_t* fb)
{
    printf("\033[?25h");
    framebuffer_destroy(fb);
    endwin();
    system("clear");
    // printf("\033[H\033[J");
}

// pGlobalFb same ptr as fb in main just for cleanups
frame_buffer_t* pGlobalFb;
void handleSignal(int signum)
{
    cleanup(pGlobalFb);
    exit(signum);
}

int main(int argc, char** argv)
{
    signal(SIGINT, handleSignal); // Ctrl+C
    signal(SIGTERM, handleSignal); // teminate signal

    args_t args = parseArgs(argc, argv);
    args.seed_set == true ? srand(args.seed) : srand(time(0));

    // if y and/or x is set trough flag get smaller framebuffer display screen
    if (args.y_set)
        frame.y = args.y;
    else
        frame.y = STATIC_HEIGHT;

    if (args.x_set)
        frame.x = args.x;
    else
        frame.x = STATIC_WIDTH;

    frame.fb_size = sizeof(frame.fb) * frame.y * frame.x;

    frame_buffer_t* fb = framebuffer_create(fbdev);

    pGlobalFb = fb;
    char* error = NULL;
    framebuffer_init(fb, &error);
    if (error != NULL)
        goto cleanup;

    union Color c = { 0 };

    //////////////////////////////////////////////////////////////////////////////////////////////////////////

    // ncurses
    initscr(); // Start ncurses mode
    cbreak(); // Disable line buffering
    noecho(); // Don't echo input
    keypad(stdscr, TRUE); // Enable special keys
    nodelay(stdscr, TRUE); // Make getch() non-blocking

    printf("\033[?25l"); // hide cursor
    move(0, 0);
    refresh();
    // main loop with input handling
    int ch;
    while (1) {
        ch = getchar();
        switch (ch) {
        case 'w':
        case 'W':
            create_rainbow(&frame);
            break;
        case 'e':
        case 'E':
            create_checkerboard(&frame);
            break;
        case 'r':
            random_framebuffer(&frame);
            break;
        case 'R':
            random_grayscale_framebuffer(&frame);
            break;
        case 'c':
            c.color = 0x0;
            goto fill_with_color;
        case 'C':
            c.color = 0xffffffff;
            goto fill_with_color;
        case '1':
            c.red = 0xff;
            c.green = 0x0;
            c.blue = 0x0;
            goto fill_with_color;
        case '2':
            c.red = 0x0;
            c.green = 0xff;
            c.blue = 0x0;
            goto fill_with_color;
        case '3':
            c.red = 0x0;
            c.green = 0x0;
            c.blue = 0xff;
            goto fill_with_color;
        case 'q':
            goto cleanup;

        fill_with_color:
            fill_with_color_framebuffer(&frame, c);
        }

        // Render the framebuffer array to the framebuffer device
        if (args.render == SCALE || args.render == CENTRED)
            render(fb, &frame);
        //      else if (args.render == CENTRED)
        //        render_center (fb, &frame);
        else if (args.render == NO_SCALING)
            render_no_scaling(fb, &frame);
    }

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clean up
cleanup:
    cleanup(fb);

    return 0;
}
