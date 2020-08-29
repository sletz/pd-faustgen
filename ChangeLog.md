
### v2.0.0 (faustgen2)

This is an extensive update by Albert Gräf (JGU-IKM, aggraef at gmail.com) with many new features:

- Renamed external to faustgen2~ to avoid clashes with CICM faustgen~ (needed since faustgen2~ is *not* a drop-in replacement for faustgen~)
- Reworked layout of inlets and outlets (now one control inlet/outlet pair on the left, remaining iolets are the dsp's inputs and outputs)
- MIDI, polyphony, Scala tunings, OSC, and Pd GUI support (that's an abundance of new functionality, bringing faustgen2~ up to par with both Grame's faustgen~ and JGU's pd-faust)
- polyphony uses a new simplified scheme which places all voices into a single dsp and marks up the indexed freq/gain/gate parameters with special meta data
- special monophonic voice allocation mode featuring old-school monophonic/legato play and portamento (see monosynth.pd for an example)
- New creation arguments and related messages for inspection, MIDI, OSC, and Pd GUI support
- Various receivers for wireless communication (you can now send messages to all faustgen2~ objects, just a specific class, or just a specific object)
- Pd loader extension, lets you type just the name of the dsp and be done with it, once faustgen2~ has been loaded
- Name mangling of Faust labels to improve handling of control names in Pd, also removed the ugly 0x00 ui name components Faust generates for anonymous groups and controls
- Use Pd's built-in menu_openfile interface to open the text editor on the dsp source file
- Improvements in the build system (make install, option to build against an installed Faust version)
- Added a bunch of new examples, including various synths and effects, Scala tuning files, and a sample TouchOSC layout for the synth-osc.pd example
- Many smaller fixes and improvements

### v0.1.2
- Update Faust (2.72.2)
- Update LLVM (>= 9.0.0)

### v0.1.1
- Fix the method to open the FAUST file on Windows when the path contains a space

### v0.1.0
- Add support to preserve parameters values during recompilation
- Fix autocompilation when DSP instance not initialized
- Fix support for the double-float-precision option
- Add support to look for the DSP file in all Pd's search paths
- Add support to open the FAUST file in the default text editor when object is clicked
- Add a default locked FAUST file used when no argument is provided

### v0.0.5
- Fix when no argument is provided
- Update deken script
- Fix sources distribution

### v0.0.4
- Move repository to CICM
- Improve/Secure code
- Use repository local LLVM pre-built

### v0.0.3
- Change name from faust~ to faustgen~
- Remove abstraction faust.watcher
- Add support for autocompile method
- Add support for passive parameter
- Add support for list of active parameters
- Add support for ui glue long names
- Add support for dynamic compile options
- Improve print method

### v0.0.2
- Fix Linux dependencies
- Add abstraction faust.watcher

### v0.0.1
- Integration of FAUST lib inside the faust~ external (Linux/MacOS/Windows)
