---
name: Distortion
description: Compact native music residual meter in Clip Pocket Solid Dark.
colors:
  chassis: "#151d25"
  glass: "#14191f"
  label: "#adb9c7"
  value: "#a9cbee"
  button: "#27333e"
typography:
  title:
    fontFamily: Inter
    fontSize: "25px"
    fontWeight: 500
  value:
    fontFamily: Inter
    fontSize: "43px"
    fontWeight: 400
  section:
    fontFamily: Inter
    fontSize: "12px"
    fontWeight: 500
  detail:
    fontFamily: Inter
    fontSize: "11px"
    fontWeight: 400
rounded:
  recess: "2px"
spacing:
  panel-inset: "18px"
components:
  analyzer-surface:
    backgroundColor: "{colors.chassis}"
    width: "560px"
    height: "236px"
  measurement-panel:
    backgroundColor: "{colors.glass}"
    rounded: "{rounded.recess}"
    width: "524px"
    height: "138px"
---

# Design System: Distortion

## Overview

Clip Pocket Solid Dark, reduced to a compact music meter. A flat dark blue-gray chassis, embedded Inter and two pale blue percentage values make Short and Integrated the focus. The header reads “Distortion” and “Music estimate”. No graphs, spectra or Momentary meter appear.

## Typography and color

Embedded Inter Medium is used for the 25-pixel title and 12-pixel meter labels; Inter Regular is used for 43-pixel values and 11-pixel details. Sizes are JUCE logical font heights passed to `FontOptions::withHeight`. Muted ink labels and a fine divider support the pale blue values. Reset uses a darker blue-gray button surface.

## Layout and rendering

The editor is fixed at 560 × 236 logical pixels and is not resizable. Native host/OS scaling applies. The measurement recess is `(18, 70, 524, 138)`, with a divider at x=272 from y=86 to 188. Short occupies the left column and Integrated the right. Value bounds are `(32, 112, 218, 55)` and `(292, 112, 238, 55)`. Reset is below the panel at `(464, 209, 78, 24)`.

A flat chassis and nearly square 2-pixel recess corners preserve the supplied identity. The background is cached as an RGB image on resize and drawn with JUCE Graphics, without a custom OpenGL context or decorative animation.

## Measurement states

Short is a rolling three-second ratio of accepted sample energies, updated every 100 ms after model learning. Integrated uses summed residual and output energies since reset; warm-up and unavailable data do not contribute. Measured seconds appear below Integrated. The readouts estimate music residual after linear-response and 20 ms envelope compensation. Noise, fast dynamics and model error can remain; compensation can also remove some nonlinear energy, especially at low frequencies. They are not true THD or loudness readouts and cannot independently identify every compressor or transient shaper.

Valid percentages below `0.005` display `0.00%`; below `100` use two decimals; below `10000` use one decimal; larger values display `>9999%`. Unavailable values use an em dash. Accessible titles distinguish Short and Integrated; descriptions explain their definitions.

Reset has the accessible title `Reset integrated measurement`. It clears Integrated energy and measured time while preserving Short. Pause makes Short unavailable; pause and capture-epoch changes preserve accumulated Integrated energy. A new capture epoch restarts Short.

## Refresh and performance

The editor polls at 10 Hz and attempts a nonblocking meter snapshot; if the worker holds the mutex, the UI keeps its previous values. Learning and FFT convolution run off the audio thread. The learner waits two seconds after a completed fit before another fit. There is no graph rendering or spectrum calculation.

## Rules

- Retain Clip Pocket Solid Dark, embedded Inter and native scaling.
- Keep Short, Integrated, measured seconds and Reset easy to read.
- Describe the result as a music residual estimate, with meaningful accessible descriptions.
- Do not add graphs, a Momentary meter, decorative animation or a custom OpenGL context.
- Do not label residual as true THD, loudness or an exact isolated measure of nonlinear processing.
