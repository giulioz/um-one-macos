# um-one-macos

Roland stopped maintaining the macOS drivers for the original [UM-ONE](https://www.roland.com/us/products/um-one/) and the last release is for macOS Sierra (2016!). Are having trouble making yours work on newer macOS versions? This driver can help you.

## Building

### Dependencies

- [PortMIDI](https://github.com/PortMidi/portmidi)
- [libusb](https://github.com/libusb/libusb)

### Building

```
gcc driver.cpp \
  deps/libusb/libusb/.libs/libusb-1.0.a deps/portmidi/build/libportmidi_static.a \
  -I deps/libusb/libusb \
  -I deps/portmidi/pm_common \
  -I deps/portmidi/porttime \
  -framework Foundation \
  -framework IOKit \
  -framework Security \
  -framework CoreMIDI \
  -framework CoreAudio \
  -lstdc++ \
  -o driver
```

### Running

Just run `./driver` and it should automatically detect your MIDI interface and show up as MIDI device in your DAW.
