Bambu Cam
=========

A simple wrapper around [Bambu Studio]'s `libBambuSource.so` prebuilt to access
the video stream from a 3D printer in LAN mode.

Usage:
```
$ bambucam <device-ip> <device-id> <passcode> <path/to/output.mkv>
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

[Bambu Studio]:https://bambulab.com/en/download/studio
