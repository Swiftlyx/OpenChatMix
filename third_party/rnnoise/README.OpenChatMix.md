# RNNoise in OpenChatMix

RNNoise 0.1.1 (https://github.com/xiph/rnnoise, tag `v0.1.1`), the release that still ships its
trained weights in `src/rnn_data.c`. Only the files the library needs are kept; the license is in
`COPYING` (BSD 3-clause).

Changes for MSVC, marked with `OpenChatMix` comments:

- `common.h`: `RNN_STACK_ARRAY`, which takes variable length arrays from the stack with `_alloca`
  under MSVC; used in `celt_lpc.c` and `pitch.c`.
- `denoise.c`: defines `M_PI` when `math.h` does not.
