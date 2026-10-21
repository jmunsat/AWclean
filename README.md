# AWclean

All the [Airwindows](https://www.airwindows.com) plugins in one, with plain-English labels.

AWclean is a build of [Airwindows Consolidated](https://github.com/baconpaul/airwin2rack)
(by Chris Johnson and BaconPaul) that keeps every sound exactly the same and changes only
what you read:

- **Names say what it is.** Each plugin shows as `Name - what it does`, for example
  `Pressure5 - Bus comp + clipper`. Gear names (Neve, SSL, Bricasti and so on) appear only
  where Chris's own notes or the code say so.
- **Knobs have real labels.** Abstract controls like `Pressure` or `Speed` become
  `Threshold`, `Release`, `Ratio feel`. Where the code allows it, the value reads in real
  units (dB, Hz, ms, %), and you can type a value in those units. Controls without a clean
  unit show words for each end, like `slow / fast`.
- **Clearer categories.** 17 categories by job: Compress & Gate, Clip & Limit, Saturate &
  Color, Tape, Console & Summing, EQ & Tone, Filters, Reverb, Delay & Doubling,
  Mod & Pitch, Distort & Amp, Stereo & Space, Lo-fi & Noise, Low End, Air & De-ess,
  Utility & Meter, Dither. Search also matches titles and Chris's original categories.
- **A short explainer** above Chris's documentation: what it is and what each control does.
- **Update button.** When opened, AWclean checks this repo's GitHub releases and shows an
  "Update available" button that opens the download page. Turn it off in the settings cog.

It installs as its own plugin ("AWclean", maker "AWclean"), so the original Airwindows
Consolidated keeps working next to it. Favorites and custom docs are shared with the
original, since both read the same `Airwindows` settings folder.

## Install

Download the installer for your system from
[Releases](https://github.com/jmunsat/AWclean/releases/latest).

The Mac installer is not signed with an Apple Developer ID, so macOS will warn the first
time. Right-click the `.pkg` and choose Open, or allow it under
System Settings > Privacy & Security.

## Editing the labels

Everything AWclean adds lives in [`awclean/overlay.json`](awclean/overlay.json). After
editing it, run

```
python3 awclean/gen_overlay.py
```

to regenerate `src/AWCleanOverlay.h`, and commit both. CI fails if they are out of sync.
Formulas use `x` (the 0 to 1 knob value) and `sr` (sample rate), with `+ - * / ^`,
`log10`, `abs`, `floor` and `sqrt`. `awclean/test/overlay_test.cpp` checks every formula.

## Releasing

Push a tag like `v1.0.0`. CI builds Windows, Mac and Linux installers and publishes a
GitHub release, which is what the update button looks for.

## License

Same as upstream: MIT for the source, GPL3 for the JUCE-based plugin build. The upstream
README is in [README-upstream.md](README-upstream.md).
