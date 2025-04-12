#include "bambu.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <jpeglib.h>

#define WIDTH 640
#define HEIGHT 480
#define FPS 1
#define COLOR_COUNT 3

// Generates a JPEG image of a specified size and color to a buffer.
int generate_jpeg(int width, int height,
                  uint8_t red, uint8_t green, uint8_t blue,
                  uint8_t **outbuffer, size_t *outsize) {
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;
  JSAMPROW row_pointer[1];
  int row_stride;
  unsigned char *buffer = NULL;
  unsigned long size = 0;

  // Initialize the JPEG compression object.
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);

  // Set the image parameters.
  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = COLOR_COUNT; // Number of color components (RGB)
  cinfo.in_color_space = JCS_RGB; // Color space

  jpeg_set_defaults(&cinfo);
  // Set the 0-100. Higher = better quality, larger file.
  jpeg_set_quality(&cinfo, 100, TRUE);

  // Set up the in-memory destination.  This replaces the file output.
  jpeg_mem_dest(&cinfo, &buffer, &size);

  // Start the compression.
  jpeg_start_compress(&cinfo, TRUE);

  // Allocate memory for a single row of image data.
  row_stride = width * 3; // width * number of components
  unsigned char *row = (unsigned char *)malloc(row_stride);
  if (!row) {
    fprintf(stderr, "Error allocating image row: %s\n", strerror(errno));
    jpeg_destroy_compress(&cinfo);
    return -errno;
  }
  row_pointer[0] = row;

  // Main loop: process one row at a time.
  while (cinfo.next_scanline < cinfo.image_height) {
    // Fill the row with the specified color.
    for (int x = 0; x < width; x++) {
      row[x * 3 + 0] = red;   // Red
      row[x * 3 + 1] = green; // Green
      row[x * 3 + 2] = blue;  // Blue
    }
    // Write the row to the JPEG file.
    jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }

  // Finish the compression and clean up.
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  free(row);

  // Return the pointer to the buffer and its size.
  *outbuffer = buffer;
  *outsize = size;
  return 0;
}

// The internal representations of the opaque pointers.
typedef struct {
  uint8_t* jpeg[COLOR_COUNT];
  size_t jpeg_size[COLOR_COUNT];
  size_t frame_i;
} ctx_internal_t;

int bambu_alloc_ctx(bambu_ctx_t* ctx) {
  *ctx = malloc(sizeof(ctx_internal_t));
  if (*ctx == NULL) {
    fprintf(stderr, "Error allocating context: %s\n", strerror(errno));
    return -errno;
  }
  memset(*ctx, 0, sizeof(ctx_internal_t));

  ctx_internal_t* ctx_internal = (ctx_internal_t*) *ctx;
  size_t buffer_size;
  generate_jpeg(WIDTH, HEIGHT, 255, 0, 0,
                &ctx_internal->jpeg[0], &ctx_internal->jpeg_size[0]);
  generate_jpeg(WIDTH, HEIGHT, 0, 255, 0,
                &ctx_internal->jpeg[1], &ctx_internal->jpeg_size[1]);
  generate_jpeg(WIDTH, HEIGHT, 0, 0, 255,
                &ctx_internal->jpeg[2], &ctx_internal->jpeg_size[2]);
  return 0;
}

int bambu_free_ctx(bambu_ctx_t ctx) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;
  for (int i = 0; i < COLOR_COUNT; ++i) {
    if (ctx_internal->jpeg[i]) free(ctx_internal->jpeg[i]);
  }
  free(ctx_internal);
  return 0;
}

int bambu_connect(bambu_ctx_t ctx,
                     char* ip, char* device, char* passcode) {
  return 0;
}

int bambu_disconnect(bambu_ctx_t ctx) {
  return 0;
}

size_t bambu_get_max_frame_buffer_size(bambu_ctx_t ctx) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;
  size_t max_buffer_size = 0;
  for (int i = 0; i < COLOR_COUNT; ++i) {
    if (ctx_internal->jpeg_size[i] > max_buffer_size) {
      max_buffer_size = ctx_internal->jpeg_size[i];
    }
  }
  return max_buffer_size;
}

int bambu_get_framerate(bambu_ctx_t ctx) {
  return FPS;
}

int bambu_get_frame_width(bambu_ctx_t ctx) {
  return WIDTH;
}

int bambu_get_frame_height(bambu_ctx_t ctx) {
  return HEIGHT;
}

int bambu_get_frame(bambu_ctx_t ctx, uint8_t** buffer, size_t* size) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;

  int color_index = ctx_internal->frame_i++ % COLOR_COUNT;
  *buffer = ctx_internal->jpeg[color_index];
  *size = ctx_internal->jpeg_size[color_index];
  return 0;
}
