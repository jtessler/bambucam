// Adapted from BambuStudio's BambuTunnel.h with all dynamic loading support
// removed for the Bambu Source library. This project links against it at build
// time.
//
// https://github.com/bambulab/BambuStudio/blob/v01.07.07.89/src/slic3r/GUI/Printer/BambuTunnel.h

typedef char tchar;

typedef void* Bambu_Tunnel;

typedef void (*Logger)(void * context, int level, tchar const* msg);

typedef enum __Bambu_StreamType
{
    VIDE,
    AUDI
} Bambu_StreamType;

typedef enum __Bambu_VideoSubType
{
    AVC1,
    MJPG,
} Bambu_VideoSubType;

typedef enum __Bambu_AudioSubType
{
    MP4A
} Bambu_AudioSubType;

typedef enum __Bambu_FormatType
{
    video_avc_packet,
    video_avc_byte_stream,
    video_jpeg,
    audio_raw,
    audio_adts
} Bambu_FormatType;

typedef struct __Bambu_StreamInfo
{
    Bambu_StreamType type;
    int sub_type;
    union {
        struct
        {
            int width;
            int height;
            int frame_rate;
        } video;
        struct
        {
            int sample_rate;
            int channel_count;
            int sample_size;
        } audio;
    } format;
    int format_type;
    int format_size;
    int max_frame_size;
    unsigned char const * format_buffer;
} Bambu_StreamInfo;

typedef enum __Bambu_SampleFlag
{
    f_sync = 1
} Bambu_SampleFlag;

typedef struct __Bambu_Sample
{
    int itrack;
    int size;
    int flags;
    unsigned char const * buffer;
    unsigned long long decode_time;
} Bambu_Sample;

typedef enum __Bambu_Error
{
    Bambu_success,
    Bambu_stream_end,
    Bambu_would_block,
    Bambu_buffer_limit
} Bambu_Error;

int Bambu_Create(Bambu_Tunnel* tunnel, char const* path);

void Bambu_SetLogger(Bambu_Tunnel tunnel, Logger logger, void * context);

int Bambu_Open(Bambu_Tunnel tunnel);

int Bambu_StartStream(Bambu_Tunnel tunnel, int video);

int Bambu_GetStreamCount(Bambu_Tunnel tunnel);

int Bambu_GetStreamInfo(Bambu_Tunnel tunnel, int index, Bambu_StreamInfo* info);

unsigned long Bambu_GetDuration(Bambu_Tunnel tunnel);

int Bambu_Seek(Bambu_Tunnel tunnel, unsigned long time);

int Bambu_ReadSample(Bambu_Tunnel tunnel, Bambu_Sample* sample);

int Bambu_SendMessage(Bambu_Tunnel tunnel, int ctrl, char const* data, int len);

int Bambu_RecvMessage(Bambu_Tunnel tunnel, int* ctrl, char* data, int* len);

void Bambu_Close(Bambu_Tunnel tunnel);

void Bambu_Destroy(Bambu_Tunnel tunnel);

int Bambu_Init();

void Bambu_Deinit();

char const* Bambu_GetLastErrorMsg();

void Bambu_FreeLogMsg(tchar const* msg);
