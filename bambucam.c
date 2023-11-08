#include "bambu.h"
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#define URL_MAX_SIZE 2048
#define URL_FORMAT "bambu:///local/%s.?port=6000&" \
                   "user=bblp&passwd=%s&device=%s&" \
                   "version=00.00.00.00"

static void bambu_log(void *ctx, int lvl, const char *msg) {
  printf("Bambu<%d>: %s\n", lvl, msg);
  Bambu_FreeLogMsg(msg);
}

int main(int argc, char** argv) {
  if (argc != 5) {
    printf("Usage: %s <ip> <device> <passcode> <output.jpg>\n", argv[0]);
    return -1;
  }

  char* ip = argv[1];
  char* device = argv[2];
  char* passcode = argv[3];
  char* out_filename = argv[4];
  char url[URL_MAX_SIZE];
  snprintf(url, URL_MAX_SIZE, URL_FORMAT, ip, passcode, device);

  FILE* out_file = fopen(out_filename, "w");
  if (!out_file) {
    printf("Error opening output file: %s\n", out_filename);
    return -1;
  }

  Bambu_Tunnel tnl = NULL;
  int res = Bambu_Create(&tnl, url);
  if (res != Bambu_success) {
    printf("Error creating Bambu Tunnel: %d\n", res);
    fclose(out_file);
    return -1;
  }

  Bambu_SetLogger(tnl, bambu_log, NULL);
  res = Bambu_Open(tnl);
  if (res != Bambu_success) {
    printf("Error opening Bambu Tunnel: %d\n", res);
    Bambu_Destroy(tnl);
    fclose(out_file);
    return -1;
  }

  while ((res = Bambu_StartStream(tnl, 1 /* video */)) == Bambu_would_block) {
    usleep(100000);
  }
  if (res != Bambu_success) {
    printf("Error starting stream: %d\n", res);
    goto close_and_exit;
  }

  res = Bambu_GetStreamCount(tnl);
  if (res != 1) {
    printf("Expected one video stream, got %d\n", res);
    res = -1;
    goto close_and_exit;
  }

  Bambu_StreamInfo info;
  Bambu_GetStreamInfo(tnl, 1, &info);
  if (info.type != VIDE) {
    printf("Expected stream type VIDE, got %d\n", info.type);
    res = -1;
    goto close_and_exit;
  }

  printf("Stream: type=%d, sub_type=%d width=%d height=%d, frame_rate=%d\n",
         info.type, info.sub_type, info.format.video.width,
         info.format.video.height, info.format.video.frame_rate);

  Bambu_Sample sample;
  while ((res = Bambu_ReadSample(tnl, &sample)) == Bambu_would_block) {
    usleep(33333); /* 30Hz */
  }
  if (res == Bambu_stream_end) {
    printf("End of stream, exiting");
    res = 0;
    goto close_and_exit;
  } else if (res != Bambu_success) {
    printf("End of stream, exiting");
    goto close_and_exit;
  }

  printf("Sample: size=%d flags=%d decode_time=%llu\n",
         sample.size, sample.flags, sample.decode_time);
  fwrite(sample.buffer, sizeof(*sample.buffer), sample.size, out_file);

close_and_exit:
  Bambu_Close(tnl);
  Bambu_Destroy(tnl);
  fclose(out_file);
  return res;
}
