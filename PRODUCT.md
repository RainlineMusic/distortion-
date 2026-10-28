# Distortion

Native JUCE analyzer for a mastering engineer. One instance is the final master insert; its external mono/stereo sidechain carries the unprocessed source. Main output is bit-identical, with zero added latency. No activation, controls, preferences or plugin-owned bypass.

The sole working view is Operate: title `Distortion` and a percentage. Unknown/unavailable measurements use an em dash. Pure sine reference selects harmonic THD_F; broadband material selects a held-out linear-model residual estimate. Music residual is not advertised as true THD. Details live in README and DSP documentation, with accessible state descriptions on the readout.

Identity follows the supplied Clip Pocket Solid Dark and embedded Inter. Windows VBlank pacing fix, font licensing, UTF-8 configuration, float/double support, bounded UI updates and process/UI separation are retained where relevant. Clipper processing, bypass, licensing and settings code are not copied into this analyzer.

Release targets: Windows x64 VST3/CLAP, macOS Universal VST3/AU/CLAP, AAX Developer. Local Linux verification is not evidence of release-platform validation. Source upload remains with the user; only the build workflow is written directly to their GitHub repository.
