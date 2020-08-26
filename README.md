
<p align="center">
  <h1 align="center">
  <img width="50" alt="FaustLogo" img src="https://user-images.githubusercontent.com/1409918/64951909-41544a00-d87f-11e9-87dd-720e0f8e1570.png"/> faustgen2~ <img width="40" alt="PdLogo" img src="https://user-images.githubusercontent.com/1409918/64951943-5335ed00-d87f-11e9-9b52-b4b6af47d7ba.png"/>
  </h1>
  <p align="center">
    The Faust compiler in a box
	<img alt="Screenshot" img src="faustgen2~.png"/>
  </p>
</p>

## Introduction

The **faustgen2~** object is a [Faust](https://faust.grame.fr/) external for Pd a.k.a. [Pure Data](http://msp.ucsd.edu/software.html), Miller Puckette's interactive multimedia programming environment. Yann Orlarey's Faust is a functional programming language developed by [Grame](https://www.grame.fr/), which is tailored for real-time signal processing and synthesis.

faustgen2~ was written by Albert Gräf (JGU Mainz, IKM, Music Informatics) based on [faustgen~](https://github.com/CICM/pd-faustgen) by  Pierre Guillot (Paris 8, CICM), which in turn was inspired by Grame's [faustgen~](https://github.com/grame-cncm/faust/tree/master-dev/embedded/faustgen) object for Max/MSP. faustgen2~ is a comprehensive update which offers plenty new features, bringing it up to par with both Grame's faustgen~ and the author's [pd-faust](https://agraef.github.io/pure-docs/pd-faust.html) external.

faustgen2~, like faustgen~, uses Faust's [LLVM](http://llvm.org)-based just-in-time (JIT) compiler to load, compile and play Faust programs on the fly. The Faust JIT compiler brings together the convenience of a standalone interpreted language with the efficiency of a compiled language.

Binary packages for Mac and Windows containing the ready-to-use external are available at <https://github.com/agraef/pd-faustgen/releases>. Please use these if you can. If you want or need to compile faustgen2~ yourself, see the instructions below.

## Prerequisites

To compile faustgen2~ you'll need [LLVM](http://llvm.org), [Faust](https://github.com/grame-cncm/faust.git), [Pd](https://github.com/pure-data/pure-data.git), and [CMake](https://cmake.org/). The sources for Faust and Pd are included, so you don't have to install these beforehand, but of course you'll want to install Pd (or one of its flavors, such as [Purr Data](https://agraef.github.io/purr-data/)) to use faustgen2~. If you already have an installation of the Faust compiler (including libfaust), you can use that version instead of the included Faust source, which will make the installation much easier and faster. Make sure that you have Faust 2.27.2 or later (older versions may or may not work with faustgen2~, use at your own risk), and LLVM 9.0.0 or later.

If you're running Linux, recent versions of LLVM and cmake should be readily available in your distribution's package repositories. On the Mac, they are available in MacPorts or Homebrew. On Windows, you can get them via Visual Studio, or install them from the corresponding websites.

Please also check the [original (faustgen~) README](README-CICM.md), which goes into more detail about installing LLVM and how to upload an external to Deken, Pd's package manager. Up-to-date information on how to install faustgen2~ can be found below.

## Getting the Source Code

You can install either from a released source tarball available at <https://github.com/agraef/pd-faustgen>, or from the Git sources. The latter can be obtained as follows:

~~~shell
git clone https://github.com/agraef/pd-faustgen.git
git submodule update --init --recursive
~~~

Note that the second command above will check out various required sources from other locations which are included in faustgen2~ as git submodules. The distributed tarballs are self-contained and already include all the submodule sources.

## Build

faustgen2~ uses cmake as its build system. Having installed all the dependencies and unpacked the sources, you can build faustgen2~ starting from its source directory as follows:

~~~shell
mkdir build && cd build
cmake ..
make
~~~

This should work at least on Linux and Mac. If your system doesn't have a working make program, you can also try `cmake --build .` instead. (Using MSYS or MSYS2 on Windows, you'll have to add the option `-G "MSYS Makefiles"` when running cmake. Right now, this still requires a fair amount of fiddling with the sources, so you might want to make your life a lot easier by using the precompiled binaries. Compilation with MSVC should work out of the box, though.)

The above will compile the included Faust source and use that to build the external. This may take a while. To use an installed Faust library, you can run cmake as follows:

~~~shell
cmake .. -DINSTALLED_FAUST=ON
~~~

This will be *much* faster than using the included Faust source. By default, this will link libfaust statically. You can also link against the shared library if you have it, as follows:

~~~shell
cmake .. -DINSTALLED_FAUST=ON -DSTATIC_FAUST=OFF
~~~

If you have Faust installed in a custom location, so that cmake fails to find the installed Faust library, you can point cmake to the library file (libfaust.a or libfaust.so on Linux, libfaust.a or libfaust.dylib on the Mac, faust.lib or faust.dll on Windows), e.g., like this:

~~~shell
cmake .. -DFAUST_LIBRARY=/some/path/to/libfaust.a
~~~

cmake should then be able to find the other required files (include and dsp library files) on its own. If all else fails, just use the included Faust source, this should always work.

## Install

Once the compilation finishes, you can install the external by running `make install` or `cmake . --install` from the build directory. By default, installation will go into the lib/pd/extra/faustgen2~ directory on Linux, and to just faustgen2~ on Mac and Windows, but this directory can be changed by setting the INSTALL_DIR variable at configuration time (`cmake .. -DINSTALL_DIR=some/path`). In any case, this directory is taken relative to cmake's CMAKE_INSTALL_PREFIX, which has an OS-specific default (e.g., on Linux it is /usr/local), but can be set with the `--prefix` option at installation time when running `cmake . --install`, see below.

### The TL;DR

Follow this cheat sheet and adjust the paths accordingly:

#### Linux

Either just `make install` or `cmake . --install --prefix /usr` (depending on whether you have Pd under /usr/local or /usr) should hopefully do the trick.

#### Mac

Use `cmake . --install --prefix ~/Library/Pd` for personal or `sudo cmake . --install --prefix /Library/Pd` for system-wide installation. That should be the safest option, since your Pd extra directory most likely lives somewhere in the Pd application bundle, which you usually don't want to touch.

#### Windows

Try `cmake . --install --prefix "/Users/Your Name/AppData/Roaming/Pd"` for personal or `cmake . --install --prefix "/Program Files/Pd/extra"` for system-wide installation. The prefix for the latter may vary *a lot* depending on which package you use and how you installed it (e.g., use the "Program Files (x86)" folder if you're running the 32 bit version of Pd). If you installed Pd from a zip package then all bets are off, and you should go look where your extra directory is and adjust the prefix path accordingly.

### Staged Installation

It's also possible (and recommended) to do a "staged install" first. You can do that in a platform-independent way as follows:

~~~shell
cmake . --install --prefix staging
~~~

This will leave the installed files in the staging subdirectory of your build directory. On Linux and other Unix-like systems, you can also run:

~~~shell
make install DESTDIR=staging
~~~

This has the advantage that it keeps the CMAKE_INSTALL_PREFIX intact, and thus the staging directory will contain the entire directory hierarchy, as `make install` would create it.

The staged installation allows you to see beforehand what exactly gets installed and where. You can then still make up your mind, or just grab the faustgen2~ folder and install it manually wherever you want it to go. To do this, you have to copy the faustgen2~ folder from the staging area to a directory where Pd looks for externals. Please consult your local Pd documentation or check the [Pd FAQ](https://puredata.info/docs/faq/how-do-i-install-externals-and-help-files) for details.

### Faust Loader

After finishing the installation, you also want to add faustgen~ to Pd's startup libraries. This isn't necessary to run the faustgen2~ external under its name, but loading the external at startup enables its included *loader extension* which hooks into Pd's abstraction and external loader. This allows you to create Faust dsps by just typing their names (*without* the faustgen2~ prefix), just as if the dsp files themselves were Pd externals (which effectively they are, although they're loaded through the faustgen2~ object rather than Pd's built-in loader).

## Run

You can try the external without installing it first, by running it directly from the staging area (see "Staged Installation" above), or you can give it a go after finishing installation. The faustgen2~ help patch is a good place to start, as it describes the many features in quite some detail (make sure you explore all of the subpatches to get a good overview). If you installed faustgen2~ in a folder which is searched by Pd for externals, the help patch should also be shown in Pd's help browser.

To start using faustgen2~ in your own projects, you will have to finish the installation as described in the preceding section. Start from an empty patch and a Faust dsp file, both in the same directory. You can then just create an object like `faustgen2~ amp` and connect its inlets and outlets as explained in the help patch.

To avoid having to type `faustgen2~` each time you create an object, you can add `faustgen2~` to your startup libraries in Pd in order to enable its loader extension, as described under "Faust Loader" above. faustgen2~ then lets you type just the dsp name (e.g., `amp~` or `amp`) and be done with it. The trailing tilde is optional (and ignored when searching for the dsp file) but customary to denote dsp objects in Pd, so faustgen2~ supports this notation.

faustgen2~ offers many other possibilities, such as MIDI (including monophonic and polyphonic synths, using the author's [SMMF](https://bitbucket.org/agraef/pd-smmf/) format for representing MIDI messages), communication with OSC (Open Sound Control) devices or applications, and automatic generation of Pd GUIs. These are all explained in the help patch. Running Faust dsps in Pd has never been easier!

## Author

Albert Gräf, Johannes Gutenberg University (JGU) Mainz/Germany, IKM, Music-Informatics department <aggraef at gmail.com>

## Credits

Many thanks are due to Pierre Guillot from CICM (Paris 8) for his awesome faustgen~ external which faustgen2~ is based on. Without Pierre's pioneering work the present version simply wouldn't exist. I'd also like to say thanks for his artwork which I shamelessly pilfered for this updated version of the README.
