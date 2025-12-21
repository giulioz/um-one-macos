# um-one-macos

Roland stopped maintaining the macOS drivers for the original [UM-ONE](https://www.roland.com/us/products/um-one/) and the last release is for macOS Sierra (2016!). Are having trouble making yours work on newer macOS versions? This driver can help you.

## Usage

[Download](https://github.com/giulioz/um-one-macos/releases/latest/download/driver) the latest release and double click on it. It should automatically detect your MIDI interface and show up as MIDI device in your DAW.

## Building

### Dependencies

- [PortMIDI](https://github.com/PortMidi/portmidi)
- [libusb](https://github.com/libusb/libusb)

You can easily install the dependencies with [Homebrew](https://brew.sh/):
```
brew install libusb portmidi
```

### Building

```
git clone https://github.com/giulioz/um-one-macos.git
cd um-one-macos
cmake .
make
```

### Running

Just run `./driver` and it should automatically detect your MIDI interface and show up as MIDI device in your DAW.
