Bambu Cam
=========

A simple wrapper around [Bambu Studio]'s `libBambuSource.so` prebuilt to access
the camera frames from a 3D printer in LAN mode and, encode it, and serve a
video stream at a given port.

Usage:

```
$ bambucam <device-ip> <device-id> <passcode> <port>
```

Where:

- `<device-ip>`: Bambu printer local IP, e.g. `192.168.0.200`
- `<device-id>`: Bambu printer ID (serial number), e.g. `0123456789ABCDE`
- `<passcode>`: Bambu printer LAN mode pass code, e.g. `12345678`
- `<port>`: Port on which to serve the video stream

Bambu Cam supports multiple video stream types depending on the `SERVER` build
flag. The supported video stream types are:

- `HTTP`: Multipart JPEG stream using microhttpd
- `RTP`: RTP video stream using FFmpeg

## Build instructions

Prepare the necessary `ffmpeg` and `libmicrohttpd` dependencies:

```
$ sudo apt install \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libjpeg-dev \
    libmicrohttpd-dev
```

Assumes you have [Bambu Studio] installed and ran at least once to download the
expected plugins.

```
$ make -j
```

Use `PLUGIN_PATH` to specify a different path if the plugins are installed in a
directory other than the default `~/.config/BambuStudio/plugins`.

```
$ make PLUGIN_PATH=/path/to/bambu/plugins -j
```

Use `DEBUG` to add more verbose logging and build debug symbols.

```
$ make DEBUG=1 -j
```

Use `BAMBU_FAKE` to use a fake camera implementation to test without an actual
printer hardware. All device arguments are obviously ignored in this mode.

```
$ make BAMBU_FAKE=1 -j
```

Use `SERVER` to select the video streaming server implementation. More details
below.

```
$ make SERVER=RTP -j
```

## HTTP Stream Details

```
$ make SERVER=HTTP -j
```

The HTTP server uses [`multipart/x-mixed-replace`] to continuous send JPEG files
in a never-ending response.

Build Bambu Cam with `SERVER=HTTP` and you can view the video stream on any web
browser by navigating to `http://localhost:<port>/`.

![Video stream example in a web browser](https://i.imgur.com/hvHuyc6.png])

[`multipart/x-mixed-replace`]:https://wiki.tcl-lang.org/page/multipart%2Fx-mixed-replace

## RTP Stream Details

```
$ make SERVER=RTP -j
```

The RTP stream uses the Pro-MPEG Code of Practice #3 Release 2 FEC protocol,
which is "a 2D parity-check forward error correction mechanism for MPEG-2
Transport Streams sent over RTP." Read more about the protocol in the [FFmpeg]
and [Wireshark] documention.

VLC supports this protocol and explains why we can use the `rtp://` path
without serving any SDP nor RTSP information.

Build Bambu Cam with `SERVER=RTP` and you can view the RTP stream in VLC:

```
$ vlc rtp://localhost/<port>
```

![Video stream example in VLC](https://i.imgur.com/lOo64MV.png)

[Bambu Studio]:https://bambulab.com/en/download/studio
[FFmpeg]:https://ffmpeg.org/ffmpeg-protocols.html#prompeg
[Wireshark]:https://wiki.wireshark.org/2dParityFEC
