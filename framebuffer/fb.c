#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <ncurses.h> //ncurses
#include <signal.h>  // For signal handling
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define STATIC_WIDTH 100
#define STATIC_HEIGHT 100

/////////////////////////////////////////////////////GLOBAL
const char *fbdev = "/dev/fb0";
uint32_t frame[STATIC_HEIGHT]
                    [STATIC_WIDTH]; // 2D array for the pixel data

///////////////////////////////////////////////////////STRUCTS//AND//ENUMS
enum Render
{
  SCALE = 1,
  CENTRED = 2,
  NO_SCALING = 3
};

typedef struct
{
  bool help;
  int seed;
  bool seed_set;
  enum Render render;
} Args;

typedef struct
{
  int fd;
  int w;
  int h;
  int fb_data_size;
  uint8_t *fb_data;
  char *fbdev;
  int fb_bytes;
  int stride;
  bool linear;
} FrameBuffer;

//////////////////////////////////////////////////////FUNCTIONS
Args
parseArgs (int argc, char **argv)
{
  Args args;
  args.help = false;
  args.seed = 0;
  args.seed_set = false;
  args.render = SCALE;

  for (int i = 1; i < argc; i++)
    {
      if (strcmp (argv[i], "-h") == 0)
        {
          args.help = true;
        }
      else if (strcmp (argv[i], "-s") == 0)
        {
          args.seed_set = true;
          if (++i < argc)
            {
              args.seed = atoi (argv[i]);
            }
          else
            {
              fprintf (stderr, "no seed after -s (seed) flag\n");
              exit (1);
            }
        }
      else if (strcmp (argv[i], "-c") == 0)
        {
          args.render = CENTRED;
        }
      else if (strcmp (argv[i], "-n") == 0)
        {
          args.render = NO_SCALING;
        }
    }

  if (args.help)
    {
      printf ("help placeholder\n\n");
      exit (1);
    }

  return args;
}
////////////////////////////////////////////////////////////////////framebuffer_create
FrameBuffer *
framebuffer_create (const char *fbdev)
{
  FrameBuffer *self = malloc (sizeof (FrameBuffer));
  self->fbdev = strdup (fbdev);
  self->fd = -1;
  self->fb_data = NULL;
  self->fb_data_size = 0;
  return self;
}

/////////////////////////////////////////////////////////////////////framebuffer_init
bool
framebuffer_init (FrameBuffer *self, char **error)
{
  bool ret = FALSE;
  self->fd = open (self->fbdev, O_RDWR);
  if (self->fd >= 0)
    {
      struct fb_var_screeninfo vinfo;
      struct fb_fix_screeninfo finfo;

      ioctl (self->fd, FBIOGET_FSCREENINFO, &finfo);
      ioctl (self->fd, FBIOGET_VSCREENINFO, &vinfo);

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

      self->fb_data = mmap (0, self->fb_data_size, PROT_READ | PROT_WRITE,
                            MAP_SHARED, self->fd, (off_t)0);

      ret = true;
    }
  else
    {
      if (error)
        asprintf (error, "Can't open framebuffer: %s", strerror (errno));
    }
  return ret;
}

///////////////////////////////////////////////////////////////////////framebuffer_deinit
void
framebuffer_deinit (FrameBuffer *self)
{
  if (self)
    {
      if (self->fb_data)
        {
          munmap (self->fb_data, self->fb_data_size);
          self->fb_data = NULL;
        }
      if (self->fd != -1)
        {
          close (self->fd);
          self->fd = -1;
        }
    }
}

////////////////////////////////////////////////////////////////////////////framebuffer_destroy
void
framebuffer_destroy (FrameBuffer *self)
{
  framebuffer_deinit (self);
  if (self)
    {
      if (self->fbdev)
        free (self->fbdev);
      free (self);
    }
}

/////////////////////////////////////////////////////////////////////////////////renderers
void
renderNoScaling (FrameBuffer *self, uint32_t (*fb)[STATIC_WIDTH])
{

  for (int y = 0; y < STATIC_HEIGHT; y++)
    {
      for (int x = 0; x < STATIC_WIDTH; x++)
        {
          uint32_t color = fb[y][x];

          long location = (x * self->fb_bytes + (y * self->stride));

          *((uint32_t *)(self->fb_data + location))
              = color; // Set the pixel color
        }
    }
}

void
render (FrameBuffer *self, uint32_t (*fb)[STATIC_WIDTH])
{
  float scale_x = (float)self->w / STATIC_WIDTH;
  float scale_y = (float)self->h / STATIC_HEIGHT;
  float scale = (scale_x < scale_y) ? scale_x : scale_y;

  for (int y = 0; y < self->h; y++)
    {
      for (int x = 0; x < self->w; x++)
        {
          int sx = (int)(x / scale);
          int sy = (int)(y / scale);

          long location = (x * self->fb_bytes + (y * self->stride));
          uint32_t color;

          if (sx < STATIC_WIDTH && sy < STATIC_HEIGHT)
            {
              color = fb[sy][sx];
            }
          else
            {
              color = 0x00000000; // Black pixel
            }

          *((uint32_t *)(self->fb_data + location)) = color;
        }
    }
}

void
renderCenter (FrameBuffer *self, uint32_t (*fb)[STATIC_WIDTH])
{
  float scale_x = (float)self->w / STATIC_WIDTH;
  float scale_y = (float)self->h / STATIC_HEIGHT;
  float scale = (scale_x < scale_y) ? scale_x : scale_y;

  // Calculate offsets to center the content
  int offset_x = (self->w - (STATIC_WIDTH * scale)) / 2;
  int offset_y = (self->h - (STATIC_HEIGHT * scale)) / 2;

  for (int y = 0; y < self->h; y++)
    {
      for (int x = 0; x < self->w; x++)
        {
          int sx = (int)(x - offset_x / scale);
          int sy = (int)(y - offset_y / scale);

          long location = (x * self->fb_bytes + (y * self->stride));
          uint32_t color;

          if (sx < STATIC_WIDTH && sy < STATIC_HEIGHT)
            {
              color = fb[sy][sx];
            }
          else
            {
              color = 0x00000000; // Black pixel
            }

          //	    framebuffer_set_pixel(self, sx, sy, r, g, b);
          *((uint32_t *)(self->fb_data + location)) = color;
        }
    }
}

void
createRainbow (uint32_t (*fb)[STATIC_WIDTH])
{
  for (int y = 0; y < STATIC_HEIGHT; y++)
    {
      for (int x = 0; x < STATIC_WIDTH; x++)
        {
          uint8_t red = (x * 255) / STATIC_WIDTH;
          uint8_t green = (y * 255) / STATIC_HEIGHT;
          uint8_t blue = 255 - ((x * 255) / STATIC_WIDTH);
          fb[y][x] = (red << 16) | (green << 8) | blue;
        }
    }
}

void
randomframebuffer (uint32_t (*fb)[STATIC_WIDTH])
{
  for (int y = 0; y < STATIC_HEIGHT; y++)
    {
      for (int x = 0; x < STATIC_WIDTH; x++)
        {
          uint8_t red = (uint8_t)rand ();
          uint8_t green = (uint8_t)rand ();
          uint8_t blue = (uint8_t)rand ();
          fb[y][x] = (red << 16) | (green << 8) | blue;
          // fb[y][x] = rand();
        }
    }
}

void
createCheckerboard (uint32_t (*fb)[STATIC_WIDTH])
{
  for (int y = 0; y < STATIC_HEIGHT; y++)
    {
      for (int x = 0; x < STATIC_WIDTH; x++)
        {
          fb[y][x] = ((x % 2 == 0) == (y % 2 == 0)) ? 0xffffffff : 0x0;
        }
    }
}

void
fillWithColorFramebuffer (uint32_t (*fb)[STATIC_WIDTH], uint32_t color)
{
  for (int y = 0; y < STATIC_HEIGHT; y++)
    {
      for (int x = 0; x < STATIC_WIDTH; x++)
        {
          fb[y][x] = color;
        }
    }
}

void
cleanup (FrameBuffer *fb)
{
  printf ("\033[?25h");
  framebuffer_deinit (fb);
  framebuffer_destroy (fb);
  endwin ();
  system ("clear");
  // printf("\033[H\033[J");
}

// pGlobalFb same ptr as fb in main just for cleanups
FrameBuffer *pGlobalFb;

void
handleSignal (int signum)
{
  cleanup (pGlobalFb);
  exit (signum);
}

int
main (int argc, char **argv)
{
  signal (SIGINT, handleSignal);  // Ctrl+C
  signal (SIGTERM, handleSignal); // teminate signal

  Args args = parseArgs (argc, argv);
  args.seed_set == true ? srand (args.seed) : srand (time (0));

  // ncurses
  initscr ();             // Start ncurses mode
  cbreak ();              // Disable line buffering
  noecho ();              // Don't echo input
  keypad (stdscr, TRUE);  // Enable special keys
  nodelay (stdscr, TRUE); // Make getch() non-blocking

  FrameBuffer *fb = framebuffer_create (fbdev);
  pGlobalFb = fb;
  char *error = NULL;
  framebuffer_init (fb, &error);
  if (error != NULL)
    goto cleanup;

  //////////////////////////////////////////////////////////////////////////////////////////////////////////
  printf ("\033[?25l"); // hide cursor
  move (0, 0);
  refresh ();
  // main loop with input handling
  int ch;
  while (1)
    {
      ch = getchar ();
      switch (ch)
        {
        case 'w':
        case 'W':
          createRainbow (frame);
          break;
        case 'e':
        case 'E':
          createCheckerboard (frame);
          break;
        case 'r':
        case 'R':
          randomframebuffer (frame);
          break;
        case 'c':
          fillWithColorFramebuffer (frame, 0x0);
          break;
        case 'C':
          fillWithColorFramebuffer (frame, 0xffffffff);
          break;
        case 'q':
          goto cleanup;
        }
      // Render the framebuffer array to the framebuffer device
      if (args.render == SCALE)
        {
          render (fb, frame);
        }
      else if (args.render == CENTRED)
        {
          renderCenter (fb, frame);
        }
      else if (args.render == NO_SCALING)
        {
          renderNoScaling (fb, frame);
        }
    }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Clean up
cleanup:
  cleanup (fb);

  return 0;
}
