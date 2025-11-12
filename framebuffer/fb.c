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

uint32_t framebuffer[STATIC_HEIGHT][STATIC_WIDTH]; // 2D array for the pixel data
int fb_fd;
struct fb_var_screeninfo vinfo;
unsigned char *fb_ptr;

///////////////////////////////////////////////////////STRUCTS//AND//ENUMS
enum Render {
  SCALE = 1,
  CENTRED = 2,
  NO_SCALING = 3
};

typedef struct {
  bool help;
  int seed;
  bool seed_set;
  enum Render render;
} Args;

//////////////////////////////////////////////////////FUNCTIONS
Args parseArgs(int argc, char **argv) {
  Args args;
  args.help = false;
  args.seed = 0;
  args.seed_set = false;
  args.render = SCALE;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0) {
      args.help = true;
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

int initializeFramebuffer(int *fb_fd, struct fb_var_screeninfo *vinfo, unsigned char **fb_ptr) {
  *fb_fd = open("/dev/fb0", O_RDWR);
  if (*fb_fd == -1) {
    perror("Error opening framebuffer device");
    return -1;
  }

  if (ioctl(*fb_fd, FBIOGET_VSCREENINFO, vinfo)) {
    perror("Error reading variable information");
    close(*fb_fd);
    return -1;
  }

  int screen_size = vinfo->yres_virtual * vinfo->xres_virtual * vinfo->bits_per_pixel / 8;
  *fb_ptr = (unsigned char *)mmap(0, screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, *fb_fd, 0);
  if (*fb_ptr == MAP_FAILED) {
    perror("Error mapping framebuffer device to memory");
    close(*fb_fd);
    return -1;
  }

  return 0; // Success
}
// renderers
void renderNoScaling(uint32_t (*fb)[STATIC_WIDTH], unsigned char *fb_ptr, struct fb_var_screeninfo vinfo) {
  // Calculate the number of bytes per line in the framebuffer
  int line_length = vinfo.xres_virtual * vinfo.bits_per_pixel / 8;

  // Loop through each pixel in the static framebuffer
  for (int y = 0; y < STATIC_HEIGHT; y++) {
    for (int x = 0; x < STATIC_WIDTH; x++) {
      // Get the color from the static framebuffer
      uint32_t color = fb[y][x];

      // Calculate the position directly for the graphical framebuffer
      long location = (x * (vinfo.bits_per_pixel / 8)) + (y * line_length);

      // Check bounds to ensure we don't write outside the framebuffer
      if (x < vinfo.xres_virtual && y < vinfo.yres_virtual) {
	*((uint32_t *)(fb_ptr + location)) = color; // Set the pixel color
      }
    }
  }
}

void render(uint32_t (*fb)[STATIC_WIDTH], unsigned char *fb_ptr, struct fb_var_screeninfo vinfo) {
  int line_length = vinfo.xres_virtual * vinfo.bits_per_pixel / 8;
  float scale_x = (float)vinfo.xres_virtual / STATIC_WIDTH;
  float scale_y = (float)vinfo.yres_virtual / STATIC_HEIGHT;
  float scale = scale_x < scale_y ? scale_x : scale_y;

  for (int y = 0; y < vinfo.yres_virtual; y++) {
    for (int x = 0; x < vinfo.xres_virtual; x++) {
      int sx = (int)(x / scale);
      int sy = (int)(y / scale);

      if (sx < STATIC_WIDTH && sy < STATIC_HEIGHT) {
	uint32_t color = fb[sy][sx];
	long location = (x + vinfo.xoffset) * (vinfo.bits_per_pixel / 8) + (y * line_length);
	*((uint32_t *)(fb_ptr + location)) = color;
      } else {
	long location = (x + vinfo.xoffset) * (vinfo.bits_per_pixel / 8) + (y * line_length);
	*((uint32_t *)(fb_ptr + location)) = 0x00000000; // Black pixel
      }
    }
  }
}

// centred render
void renderCenter(uint32_t (*fb)[STATIC_WIDTH], unsigned char *fb_ptr, struct fb_var_screeninfo vinfo) {
  int line_length = vinfo.xres_virtual * vinfo.bits_per_pixel / 8;
  float scale_x = (float)vinfo.xres_virtual / STATIC_WIDTH;
  float scale_y = (float)vinfo.yres_virtual / STATIC_HEIGHT;
  float scale = (scale_x < scale_y) ? scale_x : scale_y;

  // Calculate offsets to center the content
  int offset_x = (vinfo.xres_virtual - (STATIC_WIDTH * scale)) / 2;
  int offset_y = (vinfo.yres_virtual - (STATIC_HEIGHT * scale)) / 2;

  for (int y = 0; y < vinfo.yres_virtual; y++) {
    for (int x = 0; x < vinfo.xres_virtual; x++) {
      int sx = (int)((x - offset_x) / scale);
      int sy = (int)((y - offset_y) / scale);

      if (sx >= 0 && sx < STATIC_WIDTH && sy >= 0 && sy < STATIC_HEIGHT) {
	uint32_t color = fb[sy][sx];
	long location = (x + vinfo.xoffset) * (vinfo.bits_per_pixel / 8) + (y * line_length);
	*((uint32_t *)(fb_ptr + location)) = color;
      } else {
	long location = (x + vinfo.xoffset) * (vinfo.bits_per_pixel / 8) + (y * line_length);
	*((uint32_t *)(fb_ptr + location)) = 0x00000000; // Black pixel
      }
    }
  }
}

void createRainbow(uint32_t (*fb)[STATIC_WIDTH]) {
  for (int y = 0; y < STATIC_HEIGHT; y++) {
    for (int x = 0; x < STATIC_WIDTH; x++) {
      unsigned char red = (x * 255) / STATIC_WIDTH;
      unsigned char green = (y * 255) / STATIC_HEIGHT;
      unsigned char blue = 255 - ((x * 255) / STATIC_WIDTH);
      fb[y][x] = (red << 16) | (green << 8) | blue;
    }
  }
}

void clearframebuffer(uint32_t (*fb)[STATIC_WIDTH]) {
  for (int y = 0; y < STATIC_HEIGHT; y++) {
    for (int x = 0; x < STATIC_WIDTH; x++) {
      fb[y][x] = 0x0;
    }
  }
}

void randomframebuffer(uint32_t (*fb)[STATIC_WIDTH]) {
  for (int y = 0; y < STATIC_HEIGHT; y++) {
    for (int x = 0; x < STATIC_WIDTH; x++) {
      // fb[y][x] = rand();
      fb[y][x] = ((uint8_t)random() << 16) | ((uint8_t)random() << 8) | (uint8_t)random();
    }
  }
}

void handleSignal(int signum) {
  munmap(fb_ptr, vinfo.yres_virtual * vinfo.xres_virtual * vinfo.bits_per_pixel / 8);
  close(fb_fd);
  endwin();
  exit(signum);
}

int main(int argc, char **argv) {
  signal(SIGINT, handleSignal);	 // Ctrl+C
  signal(SIGTERM, handleSignal); // teminate signal

  Args args = parseArgs(argc, argv);
  args.seed_set == true ? srandom(args.seed) : srandom(time(0));

  // ncurses
  initscr();		 // Start ncurses mode
  cbreak();		 // Disable line buffering
  noecho();		 // Don't echo input
  keypad(stdscr, TRUE);	 // Enable special keys
  nodelay(stdscr, TRUE); // Make getch() non-blocking

  // Initialize framebuffer
  if (initializeFramebuffer(&fb_fd, &vinfo, &fb_ptr) != 0) {
    exit(1);
  }
  //////////////////////////////////////////////////////////////////////////////////////////////////////////
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
      // Create the rainbow pattern in the 2D array
      createRainbow(framebuffer);
      break;
    case 'r':
    case 'R':
      randomframebuffer(framebuffer);
      break;
    case 'c':
    case 'C':
      clearframebuffer(framebuffer);
      break;
    case 'q':
      goto cleanup;
    }
    // Render the framebuffer array to the framebuffer device
    if (args.render == SCALE) {
      render(framebuffer, fb_ptr, vinfo);
    } else if (args.render == CENTRED) {
      renderCenter(framebuffer, fb_ptr, vinfo);
    } else if (args.render == NO_SCALING) {
      renderNoScaling(framebuffer, fb_ptr, vinfo);
    }
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Clean up
cleanup:

  printf("\033[?25h");
  munmap(fb_ptr, vinfo.yres_virtual * vinfo.xres_virtual * vinfo.bits_per_pixel / 8);
  close(fb_fd);
  endwin();
  system("clear");
  // printf("\033[H\033[J");

  return 0;
}
