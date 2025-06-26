#include "bambu.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <jpeglib.h>

// Define constants for the fake video stream.
#define WIDTH 640       // Frame width in pixels.
#define HEIGHT 480      // Frame height in pixels.
#define FPS 1           // Frames per second.
#define COLOR_COUNT 3   // Number of distinct color frames to cycle through (R, G, B).

/*
 * Generates a JPEG image of a specified size and solid color.
 *
 * This function uses the libjpeg library to create a JPEG image in memory.
 * The image is filled with a single color specified by the `red`, `green`, and
 * `blue` parameters. The `width` and `height` parameters determine the image
 * dimensions.
 *
 * Upon successful generation, `outbuffer` will point to the newly allocated
 * buffer containing the JPEG data, and `outsize` will contain the size of this
 * data in bytes. The caller is responsible for freeing the `outbuffer`.
 *
 * Returns 0 on success, or a negative errno value on failure.
 */
int generate_jpeg(int width, int height,
                  uint8_t red, uint8_t green, uint8_t blue,
                  uint8_t **outbuffer, size_t *outsize) {
  // libjpeg structures for compression and error handling.
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;

  // A pointer to a single row of image data.
  JSAMPROW row_pointer[1];
  // The number of bytes in a single row of the image.
  int row_stride;
  // The buffer that will hold the generated JPEG data.
  unsigned char *buffer = NULL;
  // The size of the generated JPEG data.
  unsigned long size = 0;

  // Step 1: Initialize the JPEG compression object.
  // Set up the standard error handler.
  cinfo.err = jpeg_std_error(&jerr);
  // Create the compression object.
  jpeg_create_compress(&cinfo);

  // Step 2: Set the image parameters.
  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = COLOR_COUNT; // Number of color components (R, G, B)
  cinfo.in_color_space = JCS_RGB;       // The input color space.

  // Set default compression parameters.
  jpeg_set_defaults(&cinfo);
  // Set the JPEG quality. 100 is highest quality, resulting in a larger file.
  jpeg_set_quality(&cinfo, 100, TRUE);

  // Step 3: Set up the in-memory destination for the JPEG data.
  // This tells libjpeg to compress to a buffer in memory instead of a file.
  jpeg_mem_dest(&cinfo, &buffer, &size);

  // Step 4: Start the compression process.
  // The TRUE argument indicates that a full header should be written.
  jpeg_start_compress(&cinfo, TRUE);

  // Step 5: Generate and write the image data row by row.
  // Calculate the size of a single row.
  row_stride = width * COLOR_COUNT; // width * number of components (RGB)
  // Allocate memory for one row of image data.
  unsigned char *row = (unsigned char *)malloc(row_stride);
  if (!row) {
    fprintf(stderr, "Error allocating image row: %s\n", strerror(errno));
    jpeg_destroy_compress(&cinfo);
    return -errno;
  }
  row_pointer[0] = row;

  // Main loop: process one row at a time from top to bottom.
  while (cinfo.next_scanline < cinfo.image_height) {
    // Fill the row with the specified solid color.
    for (int x = 0; x < width; x++) {
      row[x * COLOR_COUNT + 0] = red;   // Red component
      row[x * COLOR_COUNT + 1] = green; // Green component
      row[x * COLOR_COUNT + 2] = blue;  // Blue component
    }
    // Write the row to the JPEG compression stream.
    jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }

  // Step 6: Finish the compression and clean up resources.
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  free(row);

  // Step 7: Return the pointer to the generated JPEG buffer and its size.
  *outbuffer = buffer;
  *outsize = size;
  return 0;
}

/*
 * Internal representation of the bambu_ctx_t opaque pointer.
 *
 * This structure holds the state for the fake camera context, including the
 * pre-generated JPEG frames that it will cycle through.
 */
typedef struct {
  // Array of pointers to the raw JPEG data for each color frame.
  uint8_t* jpeg[COLOR_COUNT];
  // Array of sizes for each corresponding JPEG data buffer.
  size_t jpeg_size[COLOR_COUNT];
  // A counter to keep track of the current frame index, used to cycle colors.
  size_t frame_i;
} ctx_internal_t;

/*
 * Allocates and initializes a new fake camera context.
 *
 * This function allocates memory for the internal context structure and then
 * pre-generates three solid-color JPEG frames (red, green, and blue). These
 * frames are stored in the context and will be served by bambu_get_frame().
 *
 * The `ctx` parameter is a pointer to a bambu_ctx_t which will be updated to
 * point to the newly created context.
 *
 * Returns 0 on success, or a negative errno value on failure.
 */
int bambu_alloc_ctx(bambu_ctx_t* ctx) {
  // Allocate memory for the internal context structure.
  *ctx = malloc(sizeof(ctx_internal_t));
  if (*ctx == NULL) {
    fprintf(stderr, "Error allocating context: %s\n", strerror(errno));
    return -errno;
  }
  // Zero out the newly allocated memory.
  memset(*ctx, 0, sizeof(ctx_internal_t));

  // Pre-generate the JPEG frames for the fake video stream.
  ctx_internal_t* ctx_internal = (ctx_internal_t*) *ctx;
  // Generate a red frame.
  generate_jpeg(WIDTH, HEIGHT, 255, 0, 0,
                &ctx_internal->jpeg[0], &ctx_internal->jpeg_size[0]);
  // Generate a green frame.
  generate_jpeg(WIDTH, HEIGHT, 0, 255, 0,
                &ctx_internal->jpeg[1], &ctx_internal->jpeg_size[1]);
  // Generate a blue frame.
  generate_jpeg(WIDTH, HEIGHT, 0, 0, 255,
                &ctx_internal->jpeg[2], &ctx_internal->jpeg_size[2]);
  return 0;
}

/*
 * Frees all resources associated with a fake camera context.
 *
 * This function deallocates the memory used by the pre-generated JPEG frames
 * and the context structure itself. The `ctx` to be freed is passed as an
 * argument.
 *
 * Returns 0 on success.
 */
int bambu_free_ctx(bambu_ctx_t ctx) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;
  // Free each of the pre-generated JPEG image buffers.
  for (int i = 0; i < COLOR_COUNT; ++i) {
    if (ctx_internal->jpeg[i]) {
      free(ctx_internal->jpeg[i]);
    }
  }
  // Free the context structure itself.
  free(ctx_internal);
  return 0;
}

/*
 * Placeholder for connecting to the camera.
 *
 * This function is part of the Bambu camera API but is a no-op in this fake
 * implementation. It immediately returns success without doing anything.
 * The parameters `ctx`, `ip`, `device`, and `passcode` are ignored.
 *
 * Always returns 0 (success).
 */
int bambu_connect(bambu_ctx_t ctx,
                     char* ip, char* device, char* passcode) {
  // This is a fake implementation, so we don't need to connect to anything.
  return 0;
}

/*
 * Placeholder for disconnecting from the camera.
 *
 * This function is part of the Bambu camera API but is a no-op in this fake
 * implementation. It immediately returns success without doing anything.
 * The `ctx` parameter is ignored.
 *
 * Always returns 0 (success).
 */
int bambu_disconnect(bambu_ctx_t ctx) {
  // This is a fake implementation, so there's nothing to disconnect from.
  return 0;
}

/*
 * Gets the maximum possible frame buffer size.
 *
 * This function iterates through the pre-generated frames within the given `ctx`
 * and returns the size of the largest one. The caller can use this to allocate
 * a sufficiently large buffer for receiving any frame from bambu_get_frame().
 *
 * Returns the maximum frame size in bytes.
 */
size_t bambu_get_max_frame_buffer_size(bambu_ctx_t ctx) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;
  size_t max_buffer_size = 0;
  // Find the largest size among the pre-generated JPEG frames.
  for (int i = 0; i < COLOR_COUNT; ++i) {
    if (ctx_internal->jpeg_size[i] > max_buffer_size) {
      max_buffer_size = ctx_internal->jpeg_size[i];
    }
  }
  return max_buffer_size;
}

/*
 * Gets the frame rate of the fake video stream.
 * The `ctx` parameter is ignored.
 * Returns the configured frames per second (FPS).
 */
int bambu_get_framerate(bambu_ctx_t ctx) {
  return FPS;
}

/*
 * Gets the frame width of the fake video stream.
 * The `ctx` parameter is ignored.
 * Returns the configured frame width in pixels.
 */
int bambu_get_frame_width(bambu_ctx_t ctx) {
  return WIDTH;
}

/*
 * Gets the frame height of the fake video stream.
 * The `ctx` parameter is ignored.
 * Returns the configured frame height in pixels.
 */
int bambu_get_frame_height(bambu_ctx_t ctx) {
  return HEIGHT;
}

/*
 * Retrieves the next frame from the fake video stream.
 *
 * This function cycles through the pre-generated solid-color JPEG frames
 * (red, green, blue) stored in `ctx`. It returns a pointer to the current
 * frame's data via the `buffer` output parameter and its size via the `size`
 * output parameter.
 *
 * Always returns 0 (success).
 */
int bambu_get_frame(bambu_ctx_t ctx, uint8_t** buffer, size_t* size) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;

  // Determine which color frame to return based on the frame counter.
  // The modulo operator ensures that we cycle through the available colors.
  int color_index = ctx_internal->frame_i++ % COLOR_COUNT;
  // Set the output pointers to the data of the selected frame.
  *buffer = ctx_internal->jpeg[color_index];
  *size = ctx_internal->jpeg_size[color_index];
  return 0;
}
