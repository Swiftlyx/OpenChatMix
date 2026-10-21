# Contributing to OpenChatMix

Thanks for helping! OpenChatMix is a small one-person project, so the most useful contributions are
often reports from hardware I don't have.

## Reporting a bug

Open a [bug report](https://github.com/Swiftlyx/OpenChatMix/issues/new?template=bug-report.yml) with
your OpenChatMix version (shown at the bottom of the *Settings* tab), your headset and what happened.
A log helps a lot:

1. Quit OpenChatMix from the tray menu (*Quit*). It saves its settings when it exits, so a change made
   while it runs would be undone.
2. Set `Log=1` in `%APPDATA%\OpenChatMix\OpenChatMix.ini`.
3. Start OpenChatMix and reproduce the problem.
4. Attach `OpenChatMix.log` from the same folder. If OpenChatMix stopped responding, attach the
   `OpenChatMix-hang-<time>.dmp` file there too.

The log can contain device and app names and file paths, which may include your Windows user name;
look it over before you post it. More tips are in [Troubleshooting](README.md#troubleshooting).

## Reporting a headset

Reports for untested models help most. Open a
[headset report](https://github.com/Swiftlyx/OpenChatMix/issues/new?template=headset-report.yml) and
attach a log made as above: turn the ChatMix dial a few times, press the headset's buttons and open the
panel. With a SteelSeries headset OpenChatMix does not support yet, the log lists its HID collections
and the reports it sends; OpenChatMix only reads from it and sends nothing.

A USB capture of SteelSeries GG changing a setting (Wireshark with USBPcap) is the best way to get a new
setting or equalizer supported.

## Suggesting a feature

Open an issue and describe what you want to do, not only how. OpenChatMix aims to stay light (a few
megabytes of RAM, no background work it doesn't need), so larger features are usually optional.

## Pull requests

For anything bigger than a small fix, please open an issue first, so we can agree on the approach before
you spend time on it. Then:

- Keep one topic per pull request.
- Say how you tested it, and on which headset.
- Add a screenshot for panel changes, with the panel in English.
- Contributions are released under the [MIT License](LICENSE), like the rest of the project.

### Building

You need Windows 10 or 11 and Visual Studio 2022 (or the Build Tools) with the *Desktop development with
C++* workload:

```bat
build.bat                      :: builds build\OpenChatMix.exe
tools\preview\build.bat        :: builds build\preview\OpenChatMixPreview.exe (after build.bat)
```

`OpenChatMixPreview.exe` shows the real panel with a simulated headset, so you can check every page
without the hardware. More details are in [Building from source](README.md#building-from-source).

### Code

- C++20 and the Win32 API. Please don't add new dependencies without asking first.
- The build uses `/W4` and should finish without warnings.
- Follow the code around your change: naming, structure and comment density.
- Comments are in English and only describe the code: what it does, why a non-obvious choice was made,
  constraints and edge cases.
- Panel changes follow the design system in [docs/design-system.md](docs/design-system.md).
- Texts shown in the app go into `src/i18n.h` and `src/i18n.cpp`, in English and Ukrainian; if you
  don't speak Ukrainian, leave the English text there and say so in the pull request. Texts start with a
  capital letter.

### Headset protocols

- Commands that change a headset's settings should come from a USB capture or a description confirmed on
  the device. Mark anything not yet tried on the hardware as experimental.
- Other projects' code under licenses that don't fit MIT, such as the GPL, can't be copied. Protocol
  facts (report layouts, command bytes) are fine; please name the source in the pull request.
- Files from SteelSeries software, such as GG's device definitions or Sonar's models, can't be included
  or decrypted.
