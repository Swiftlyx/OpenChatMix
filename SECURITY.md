# Security policy

## Supported versions

Only the [latest release](https://github.com/Swiftlyx/OpenChatMix/releases/latest) gets security fixes.
OpenChatMix updates itself (*Settings → Updates*), so a fix reaches users with the next release.

## Reporting a vulnerability

Please do not open a public issue for a security problem. Report it privately instead:

1. Open the [Security tab](https://github.com/Swiftlyx/OpenChatMix/security) of the repository.
2. Choose **Report a vulnerability** and describe the problem, the version and the steps to reproduce it.

OpenChatMix is a one-person project, so a reply may take a few days. Once the problem is confirmed, the
fix goes into a release, and the advisory credits you unless you prefer otherwise.

## Scope

In scope, for example:

- The updater: downloading a release, checking it against `SHA256SUMS.txt` and replacing the executable.
- The local GameSense receiver for add-ons on `127.0.0.1`, and anything a web page or another user's
  process could do through it.
- Reading the settings file, EQ presets and other files OpenChatMix opens.
- The commands OpenChatMix sends to a headset, if a crafted input makes it send something unintended.
- `DeepFilterNet-OpenChatMix.dll`, built from `tools/deepfilter`.

Not in scope:

- VST plugins and add-ons you choose to load: they run with OpenChatMix's rights by design, so load only
  plugins you trust.
- The binaries not being code-signed, SmartScreen prompts and antivirus false positives.
- Problems in SteelSeries GG, device drivers or virtual audio cables; please report those to their
  vendors.
