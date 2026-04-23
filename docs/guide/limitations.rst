Limitations
===========

Things platform-nes cannot (or deliberately does not) do. This page
documents the known walls you will hit, so you can plan around them
instead of rediscovering them at runtime.

General
-------

Due to interrupts being an abstraction and not an emulated feature, as a result of this :func:`WaitForPresent` must be
invoked. On the NES its an empty stub, permitting NMI exits to continue main-thread tasks but assumes other targets will
be able to complete an entire workload before waiting for render sync. As a result of this, on modern targets programs
are NMI-abstraction threaded whereas on NES targets its required to support reset threading.

Audio
-----

At this stage, the audio system mirrors `famistudio`_ entirely, requiring other targets to manage their sound effects as
dpcm or non-dpcm respectively despite no audio emulation. Due to none of the audio files containing loop points, its
also required for other targets to specify where the song should loop on completion.

Video
-----

It's required to specify which mirroring technology is being used during compilation, as ``platform-nes`` only supports
NROM at this point in time and generates more emulated PPU VRAM to scale with the axis which isn't mirrored for true
widescreen.

Input
-----

``platform-nes`` only supports 2 SDL3 gamepads to register at any given point, not permitting Mouse or Keyboard input.
Linux builds will also require using ``libusb`` to compile.

.. _famistudio: https://github.com/BleuBleu/FamiStudio