Bambu Cam
=========

A simple wrapper around [Bambu Studio]'s `libBambuSource.so` prebuilt to access
the camera frames from a 3D printer in LAN mode and, encode it, and serve an
RTP stream at a given port.

Usage:
```
$ bambucam <device-ip> <device-id> <passcode> <rtp-port>
```

You can view the RTP stream in VLC:

```
$ vlc rtp://localhost/<rtp-port>
```

## Build instructions

Prepare the necessary `ffmpeg` dependencies:

```
$ sudo apt install libavutil-dev libavcodec-dev libavformat-dev
```

Assumes you have [Bambu Studio] installed and ran at least once to download the
expected plugins.

```
$ make -j
```

Use `PLUGIN_PATH` to specify a different path if the plugins are installed in a
directory other than the default `~/.config/BambuStudio/plugins`.

```
$ PLUGIN_PATH=/path/to/bambu/plugins make -j
```

## RTP Stream Details

The RTP stream uses the Pro-MPEG Code of Practice #3 Release 2 FEC protocol,
which is "a 2D parity-check forward error correction mechanism for MPEG-2
Transport Streams sent over RTP." Read more about the protocol in the [FFmpeg]
and [Wireshark] documention.

VLC supports this protocol and explains why we can use the `rtp://` path
without serving any SDP nor RTSP information.


[Bambu Studio]:https://bambulab.com/en/download/studio
[FFmpeg]:https://ffmpeg.org/ffmpeg-protocols.html#prompeg
[Wireshark]:https://wiki.wireshark.org/2dParityFEC
