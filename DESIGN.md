---
name: Distortion
description: Native spectral and residual analyzer in Clip Pocket Solid Dark.
colors:
  chassis: "#151d25"
  chassis-light: "#1c2732"
  glass: "#14191f"
  label: "#adb9c7"
  before: "#8eafa7"
  after: "#a9cbee"
  residual: "#e5b887"
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
    width: "800px"
    height: "648px"
  spectrum-panel:
    backgroundColor: "{colors.glass}"
    rounded: "{rounded.recess}"
    width: "764px"
    height: "461px"
  measurement-panel:
    backgroundColor: "{colors.glass}"
    rounded: "{rounded.recess}"
    width: "764px"
    height: "102px"
---

# Design System: Distortion

## Overview

**Creative North Star: "Clip Pocket Solid Dark"**

A native instrument in the supplied dark blue-gray material. Embedded Inter, recessed panels and restrained trace colors preserve the Clip Pocket identity while giving spectral comparison the largest area.

**Key Characteristics:**

- Before/after spectra above the added residual spectrum.
- A separate Short / Integrated measurement panel with Reset; no Momentary meter.
- A fixed native window and a quiet dark radial chassis.

## Colors

The frontmatter owns the palette. Green identifies Before, pale blue identifies After and both percentage readouts, and amber identifies added residual. Muted ink labels, low-opacity ink grid lines and dark recesses support the data without competing with it. Reset uses a darker blue-gray button surface.

## Typography

Embedded Inter Medium is used for the title and section labels; Inter Regular is used for values and details. Sizes are JUCE logical font heights passed to `FontOptions::withHeight`, not measured glyph bounds or CSS em sizes. The title uses the title token; values use the value token. State messages use 13–14 logical pixels.

## Layout

The editor is fixed at 800 × 648 logical pixels and is not resizable. Host/OS scaling remains native; there are no responsive breakpoints.

The spectrum recess is `(18, 66, 764, 461)` and the measurement recess is `(18, 535, 764, 102)`. Before/after plot bounds are `(72, 115, 686, 196)`; residual plot bounds are `(72, 381, 686, 107)`. Both plots share a logarithmic 20 Hz–20 kHz axis and a displayed −96 to 0 dBFS range. These are raw signal spectra per 1/6 octave, not a system transfer response.

Short sits at the left of the bottom panel and Integrated at the center, with a vertical divider between them. Reset sits at the right. The title and `Estimated residual` descriptor occupy the header.

## Elevation & Depth

A radial gradient shades the chassis from its upper left. Flat dark recesses and fine, low-opacity dividers establish depth; there are no raised cards or decorative shadows. The chassis is cached as an RGB image on resize and drawn with JUCE Graphics, without a custom OpenGL context.

## Shapes

The window is rectangular. Recesses have nearly square corners using the recess token. Traces use curved 1.65-pixel strokes. Grid lines are subtle and subordinate to traces.

## Components

### Spectral comparison

Before is green and After is blue, with labeled line samples in the legend. Residual is amber in the lower plot. The display uses 192 logarithmically spaced band centers. Without spectrum data, show the routing prompt in the upper plot; while a model is unavailable, show `Learning the linear response...` in the lower plot.

### Residual measurements

Short is a rolling three-second ratio of sample energies, updated every 100 ms after model learning. Integrated is the ratio of summed residual and output energies since reset; warm-up and unavailable data do not contribute. Measured seconds appear below Integrated. These are residual estimates including noise, changing dynamics and model error, not certified loudness or harmonic THD readouts.

Valid percentages below `0.005` display `0.00%`; below `100` use two decimals; below `10000` use one decimal; larger values display `>9999%`. Unavailable values use an em dash. Accessible titles distinguish Short and Integrated; descriptions explain their definitions.

### Reset

The native JUCE text button is labeled `Reset` and has an accessible title of `Reset integrated measurement`. It clears Integrated energy and elapsed measured time while preserving Short. Pause makes Short unavailable; pause and capture-epoch changes preserve accumulated Integrated energy.

### Refresh

The editor polls at 10 Hz. Analysis and spectrum generation run off the audio thread. No idle animation is added.

## Do's and Don'ts

### Do:

- Do retain Clip Pocket Solid Dark and embedded Inter.
- Do keep Before green, After blue and residual amber.
- Do keep Short and Integrated in their separate panel.
- Do preserve native host/OS scaling and meaningful accessible descriptions.

### Don't:

- Don't add a Momentary meter.
- Don't label residual as true THD, loudness or transfer response.
- Don't add decorative animation or a custom OpenGL context.
