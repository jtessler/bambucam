#include "bambucam.h"
#include "rtp_server.h"
#include <stdio.h>
#include <stdlib.h>

size_t fill_image_from_bambucam(void* callback_ctx,
                                uint8_t* buffer, size_t buffer_size_max) {
  bambucam_ctx_t bambucam_ctx = *((bambucam_ctx_t*) callback_ctx);
  size_t image_size = bambucam_get_frame(bambucam_ctx, buffer, buffer_size_max);
  if (image_size < 0) {
    fprintf(stderr, "Error getting frame\n");
  }
  return image_size;
}

int main(int argc, char** argv) {
  if (argc != 5) {
    fprintf(stderr, "Usage: %s <ip> <device> <passcode> <rtp-port>\n", argv[0]);
    return -1;
  }

  char* ip = argv[1];
  char* device = argv[2];
  char* passcode = argv[3];
  int rtp_port = atoi(argv[4]);

  bambucam_ctx_t bambucam_ctx = NULL;
  rtp_server_ctx_t rtp_server_ctx = NULL;
  rtp_server_callbacks_t rtp_server_callbacks = {
    .callback_ctx = &bambucam_ctx,
    .fill_image_buffer = fill_image_from_bambucam,
  };
  int res;

  res = bambucam_alloc_ctx(&bambucam_ctx);
  if (res < 0) {
    fprintf(stderr, "Error allocating bambucam\n");
    goto close_and_exit;
  }

  res = rtp_server_alloc_ctx(&rtp_server_ctx);
  if (res < 0) {
    fprintf(stderr, "Error allocating RTP server\n");
    goto close_and_exit;
  }

  res = bambucam_connect(bambucam_ctx, ip, device, passcode);
  if (res < 0) {
    fprintf(stderr, "Error connecting via bambucam\n");
    goto close_and_exit;
  }

  int fps = bambucam_get_framerate(bambucam_ctx);
  int width = bambucam_get_frame_width(bambucam_ctx);
  int height = bambucam_get_frame_height(bambucam_ctx);
  size_t frame_buffer_size = bambucam_get_max_frame_buffer_size(bambucam_ctx);

  res = rtp_server_start(rtp_server_ctx, rtp_port, &rtp_server_callbacks,
                         width, height, fps, frame_buffer_size);
  if (res < 0) {
    fprintf(stderr, "Error running RTP server\n");
    goto close_and_exit;
  }

close_and_exit:
  if (rtp_server_ctx) rtp_server_free_ctx(rtp_server_ctx);
  if (bambucam_ctx) bambucam_free_ctx(bambucam_ctx);
  return res;
}
