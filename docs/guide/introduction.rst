Introduction
============

Compiling
---------

Rename ``local.cmake.example`` to ``local.cmake`` and ensure that that ``CMAKE_PREFIX_PATH`` points to the location of
your ``llvm-mos`` installation and ``cc65`` installation. If you are a Windows user, ensure that you use `docker`_ for Linux builds for local WSL
tests or a Mac device for Mac tests.

Famistudio
----------

in the ``CMakeLists.txt`` file, ensure that you have the necessary defines for what features you wish `famistudio`_ to
use. Without this `famistudio`_ may produce incorrect outputs on your NES target.

Audio
----------
Every song that *should* exist in the NES target should be symmetrical for a WAV file for SDL3 targets. This can be
decalred with:

.. code-block:: c

        // SDL3

        TRACKS(
            {.fp ="tracks/pc_audio.wav", .loop_start = 0}
        );

.. code-block:: c

        // NES

        extern const uint8_t _music_data_mega_man_2[];

        TRACKS(_music_data_mega_man_2);


Character ROM
-------------
The ``PPU`` is the one thing that is emulated in ``platform-nes`` and weakly, to build Character ROM you must do:

.. code-block:: c

    CHARACTER_ROM(crate, "../demo/chr/all");

    CHARACTER_ROM_ALIGN(0x2000);

This will load 2BPP NES pattern data into ``rodata`` for both NES and SDL3 targets and then aligning the size to be
correct for emulation.

Boilerplate
-----------

To create your program, you must use the ``RESET`` and ``NMI`` macros like so:

.. code-block:: c

    RESET {
        // main code here
    }


    NMI {
        // post render code here
    }


Once you have this up and running, ensure you can build for both PC and NES and refer to the docs for other areas.
I hope you enjoy using ``platform-nes``.


.. _docker: https://www.docker.com
.. _famistudio: https://github.com/BleuBleu/FamiStudio