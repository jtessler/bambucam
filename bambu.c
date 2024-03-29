#include "bambu.h"

#include "bambu_tunnel.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// URL format as copied directly from Bambu Studio source code.
// See: //BambuStudio/src/slic3r/GUI/MediaPlayCtrl.cpp
#define URL_MAX_SIZE 2048
#define URL_INPUT_FORMAT "bambu:///local/%s.?port=6000&" \
                         "user=bblp&passwd=%s&device=%s&" \
                         "version=00.00.00.00"

// Retry times in microseconds based on real-world observation to minimize
// the number of "would block" results.
#define START_STREAM_RETRY_US (100 * 1000)  // 100ms.
#define READ_SAMPLE_RETRY_US (50 * 1000)  // 50ms.

// Observed frame buffer sizes averages around ~110000 bytes. Ensure callers
// allocate plenty of space (~2x) in the absence of finding a better way to
// get the max frame size.
//
// Note: ctx_internal->stream_info.max_frame_size is always zero...
#define MAX_FRAME_SIZE_BYTES (200 * 1024)

// The internal representations of the opaque pointers.
typedef struct {
  Bambu_Tunnel tunnel;
  Bambu_StreamInfo stream_info;
} ctx_internal_t;

int bambu_alloc_ctx(bambu_ctx_t* ctx) {
  *ctx = malloc(sizeof(ctx_internal_t));
  if (*ctx == NULL) {
    fprintf(stderr, "Error allocating context: %s\n", strerror(errno));
    return -errno;
  }

  memset(*ctx, 0, sizeof(ctx_internal_t));
  return 0;
}

int bambu_free_ctx(bambu_ctx_t ctx) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;

  if (ctx_internal->tunnel) Bambu_Destroy(ctx_internal->tunnel);
  free(ctx_internal);
  return 0;
}

static void tunnel_log(void* context, int level, tchar const* msg) {
  fprintf(stderr, "Bambu<%d>: %s\n", level, msg);
  Bambu_FreeLogMsg(msg);
}

int bambu_connect(bambu_ctx_t ctx,
                     char* ip, char* device, char* passcode) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;
  int res;
  char url[URL_MAX_SIZE];

  res = snprintf(url, URL_MAX_SIZE, URL_INPUT_FORMAT, ip, passcode, device);
  if (res < 0) {
    fprintf(stderr, "Error formatting input URL\n");
    return res;
  }

  res = Bambu_Create(&ctx_internal->tunnel, url);
  if (res != Bambu_success) {
    fprintf(stderr, "Error creating Bambu tunnel: %d\n", res);
    return -1;
  }

#ifdef DEBUG
  Bambu_SetLogger(ctx_internal->tunnel, tunnel_log, NULL);
#endif

  res = Bambu_Open(ctx_internal->tunnel);
  if (res != Bambu_success) {
    printf("Error opening Bambu tunnel: %d\n", res);
    return -1;
  }

  // Attempt to start a stream indefinitely. Assumes the Bambu library will
  // eventually return something besides "will block."
  do {
    // The second argument is undocumented. Bambu Studio source code suggests
    // "1" or "true" means "video."
    res = Bambu_StartStream(ctx_internal->tunnel, 1);
    if (res == Bambu_would_block) {
      usleep(START_STREAM_RETRY_US);
    } else if (res != Bambu_success) {
      fprintf(stderr, "Error starting stream: %d\n", res);
      return -1;
    }
  } while (res == Bambu_would_block);

  res = Bambu_GetStreamCount(ctx_internal->tunnel);
  if (res != 1) {
    fprintf(stderr, "Expected one video stream, got %d\n", res);
    return -1;
  }

  res = Bambu_GetStreamInfo(ctx_internal->tunnel,
                            1, // Stream index (assuming 1).
                            &ctx_internal->stream_info);
  if (res != Bambu_success) {
    fprintf(stderr, "Error getting stream info: %d\n", res);
    return -1;
  }

  if (ctx_internal->stream_info.type != VIDE) {
    fprintf(stderr, "Expected stream type VIDE, got %d\n",
            ctx_internal->stream_info.type);
    return -1;
  }

  return 0;
}

int bambu_disconnect(bambu_ctx_t ctx) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;
  Bambu_Close(ctx_internal->tunnel);
  return 0;
}

size_t bambu_get_max_frame_buffer_size(bambu_ctx_t ctx) {
  return MAX_FRAME_SIZE_BYTES;
}

int bambu_get_framerate(bambu_ctx_t ctx) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;
  return ctx_internal->stream_info.format.video.frame_rate;
}

int bambu_get_frame_width(bambu_ctx_t ctx) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;
  return ctx_internal->stream_info.format.video.width;
}

int bambu_get_frame_height(bambu_ctx_t ctx) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;
  return ctx_internal->stream_info.format.video.height;
}

int bambu_get_frame(bambu_ctx_t ctx, uint8_t** buffer, size_t* size) {
  ctx_internal_t* ctx_internal = (ctx_internal_t*) ctx;
  Bambu_Sample sample;
  int res;

  // Attempt to grab a frame indefinitely. Assumes the Bambu library will
  // eventually return something besides "will block."
  do {
    res = Bambu_ReadSample(ctx_internal->tunnel, &sample);
    if (res == Bambu_would_block) {
      usleep(READ_SAMPLE_RETRY_US);
    } else if (res != Bambu_success) {
      fprintf(stderr, "Error reading sample: %d\n", res);
      return -1;
    }
  } while (res == Bambu_would_block);

  // TODO: Can we be sure this buffer exists after sample is gone?
  //
  // Consider adding a bambu_frame_t opaque pointer to a Bambu_Sample to add
  // a copy function here instead of passing the underlying pointer.
  *buffer = (uint8_t*) sample.buffer;
  *size = sample.size;
  return 0;
}
