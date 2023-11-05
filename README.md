Bambu Cam
=========

A simple wrapper around [Bambu Studio]'s `libBambuSource.so` prebuilt to access
the video stream from a 3D printer in LAN mode.

Usage:
```
$ bambucam <device-ip> <device-id> <passcode> <path/to/output.jpg>
```

## Build instructions

Assuming you have [Bambu Studio] installed and downloaded all plugins, copy the
`libBambuSource.so` prebuilt to this working directory, then build:

```
$ cp ~/.config/BambuStudioInternal/plugins/libBambuSource.so ./
$ make -j
```

[Bambu Studio]:https://bambulab.com/en/download/studio
