# Contributing

Contributions are welcome. Each one is accepted under the license of the files it changes (see `LICENSE`).
Sign off every commit (`git commit -s`). Signing off certifies the Developer Certificate of Origin 1.1
(https://developercertificate.org): you wrote the change, or otherwise have the right to submit it under these
licenses. New files need an SPDX header, or an entry in `REUSE.toml`; `reuse lint` at the repo root must end
"compliant with version 3.3 of the REUSE Specification".

Publish source with binaries: every `.lbk` in `.lbk_files/` has its matching source (`life_logger/` and
`dashboard/dashboard.html`) in the same commit.

`life_logger/clock_glyphs.h` and `docs/demo/clock_glyphs.js` are made by `tools/clock_glyphs.py`, and
`docs/index.html` by `docs/demo/build_demo.py`: change the script and run it, rather than editing what it made.
