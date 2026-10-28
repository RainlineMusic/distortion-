---
name: Distortion
description: A compact native analyzer in the supplied Clip Pocket Solid Dark material.
colors:
  chassis: "#151d25"
  chassis-light: "#1c2732"
  glass: "#14191f"
  label: "#adb9c7"
  readout: "#a9cbee"
  edge: "#d9e3ee"
typography:
  title:
    fontFamily: Inter
    fontSize: "23px"
    fontWeight: 500
  value:
    fontFamily: Inter
    fontSize: "62px"
    fontWeight: 400
rounded:
  recess: "1.5px"
spacing:
  panel-inset: "18px"
components:
  analyzer-surface:
    backgroundColor: "{colors.chassis}"
    width: "414px"
    height: "218px"
  measurement-panel:
    backgroundColor: "{colors.glass}"
    rounded: "{rounded.recess}"
    width: "378px"
    height: "182px"
  title:
    textColor: "{colors.label}"
    typography: "{typography.title}"
    width: "342px"
    height: "32px"
  readout:
    textColor: "{colors.readout}"
    typography: "{typography.value}"
    width: "350px"
    height: "81px"
---

# Design System: Distortion

## Overview

**Creative North Star: "Clip Pocket Solid Dark"**

A single recessed measurement display inside the supplied Clip Pocket Solid Dark material. The visual identity is restrained and compact: muted Inter title, large output-blue percentage, and a shallow dark recess.

The implemented Operate surface contains only “Distortion” and a percentage or em dash. Measurement definitions and unavailable-state guidance live in the readout’s accessibility description, without additional visible copy.

**Key Characteristics:**

- Fixed native window with one centered measurement.
- Embedded Inter Medium title and Inter Regular value.
- Dark radial chassis shading, a shallow inset shade, and a fine lower edge.

## Colors

The supplied dark blue-gray material uses one pale blue readout accent. The frontmatter is the source of color values.

### Primary

- **Output Blue** (`readout`): the percentage and unavailable em dash.

### Neutral

- **Chassis Dark** (`chassis`) and **Chassis Light** (`chassis-light`): the outer radial gradient, light at the upper left and dark toward the opposite corner.
- **Recess Glass** (`glass`): the dark measurement panel.
- **Muted Label** (`label`): the title.
- **Fine Edge** (`edge`): the panel’s lower hairline at 13% opacity.

## Typography

Both roles use embedded Inter typefaces from the supplied project: Inter Medium for the title and Inter Regular for the value. The frontmatter sizes denote JUCE logical font heights passed to `FontOptions::withHeight`; they are not a claim about CSS em sizing or measured glyph bounds.

- **Title:** centered in its label, with no label border.
- **Value:** centered in its label, with no label border. JUCE may compress it horizontally down to a 0.6 scale for long values.

## Layout

The native window is fixed at 414 × 218 logical pixels and is not resizable. Host/OS pixel scaling remains native; there are no responsive breakpoints.

The panel is inset by the spacing token on every side. Title bounds are `(36, 43, 342, 32)` and value bounds are `(32, 87, 350, 81)`, expressed as logical `(x, y, width, height)`. Both labels are horizontally centered. These coordinates describe this sole working surface, not a general grid for hypothetical screens.

## Elevation & Depth

A JUCE radial gradient shades the chassis. The panel has a black-to-transparent inset shade over its top 14 logical pixels, starting at 60% black, and a one-pixel lower hairline using the edge token at 13% opacity. There are no raised surfaces or drop shadows.

The chassis and recess are drawn into a cached RGB image on resize. The editor draws that image through JUCE Graphics, with no custom OpenGL context or idle animation.

## Shapes

The outer window is rectangular. The inset panel has nearly square corners using the recess radius token. The only fine stroke is the lower panel edge; neither label has a border.

## Components

### Passive measurement display

The visible content is the title `Distortion` and one value. There are no interactive controls or hover, pressed, selected, or focus variants. The editor does not request keyboard focus.

- **Valid:** show a percentage. Values below `0.005` show `0.00%`; values below `100` use two decimal places; other values below `10000` use one decimal place; values at or above `10000` show `>9999%`.
- **Unavailable:** show an em dash in the same value slot and color. This covers measuring, no reference, silence, insufficient material, unrelated reference, capture overflow, and invalid input. The visible layout and palette do not change between these states.
- **Accessible state:** the readout title is `Distortion measurement`. Its description distinguishes harmonic THD from estimated residual and supplies state-specific guidance for unavailable results. Those descriptions are not visible labels.
- **Refresh:** the editor polls at 5 Hz and changes the value label only when its formatted text changes. No UI animation accompanies an update.

`Docs/Previews` contains four 2× captures from the editor rendering path: `thd-5-percent.png` (5.00%), `residual-example.png` (the explicitly synthetic 2.50% residual display), `no-reference.png`, and `measuring.png` (both em dash). These are visual references, not release-platform or DSP-validation evidence.

## Do's and Don'ts

### Do:

- Do retain the supplied Clip Pocket Solid Dark palette and embedded Inter.
- Do keep the visible surface to the title and percentage or em dash.
- Do expose measurement definitions and unavailable-state guidance through the readout’s accessibility description.
- Do preserve the fixed logical window and native host/OS pixel scaling.

### Don't:

- Don’t add controls, navigation, meters, badges, footers, or signal diagrams to this surface.
- Don’t add hover, pressed, or focus treatments to the passive readout.
- Don’t add idle animation or a custom OpenGL context.
