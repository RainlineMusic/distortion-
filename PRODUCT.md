# Distortion 0.1.3

Native JUCE analyzer for a mastering engineer. One instance is the final master insert; its external mono/stereo sidechain carries the unprocessed source. Main output is bit-identical, with zero added latency. No activation, preferences or plugin-owned bypass.

The working view is Operate: before/after spectra at the top, added residual spectrum below, and a separate Short / Integrated panel with Reset. There is no Momentary meter. Short uses three seconds of sample energies and updates every 100 ms after model learning. Integrated is the ratio of summed residual and output energies since reset, excluding warm-up and unavailable data; Reset preserves Short. Pauses make Short unavailable; pauses and capture-epoch changes preserve Integrated. Unknown/unavailable measurements use an em dash.

Live percentages estimate residual RMS relative to output RMS. Residual includes nonlinear processing, noise, changing dynamics and finite-model error; it is not true harmonic THD on music or a live test tone. The CLI retains its harmonic THD_F test-tone method. Spectra show raw dBFS per 1/6 octave, not a system transfer response. These are not LUFS measurements; there is no loudness weighting, loudness gating or certification claim.

Identity follows the supplied Clip Pocket Solid Dark and embedded Inter: fixed 800 × 648 native window, green Before, blue After and amber residual. Windows VBlank pacing fix, font licensing, UTF-8 configuration, float/double support, bounded UI updates and process/UI separation are retained where relevant. Clipper processing, bypass, licensing and settings code are not copied into this analyzer.

Release targets: Windows x64 VST3/CLAP, macOS Universal VST3/AU/CLAP, AAX Developer. Local Linux verification is not evidence of release-platform validation. Source upload remains with the user; only the build workflow is written directly to their GitHub repository.
