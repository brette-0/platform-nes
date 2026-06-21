Introduction
============

Dependencies
------------

``platform-nes`` requires a few libraries, some of which are fetched at compile time for static compilation.

1. ``intsh`` - integer type shorthands under the ``br0`` namespace for easy concise type specification and usage.
2. ``tuple`` - a lightweight clone of ``std::tuple`` under ``br0::tuple`` to gain compile time advantages for ``llvm-mos-sdk``.
3. ``SDL3`` - used for WASM, Linux, Windows and MacOS builds handling input, audio and video.
4. ``devkitpro`` - used for nintendo ARM/PPC targets


Compiling
---------

**1. Compiling For NES**

Ensure that you have the ``llvm-mos-sdk`` installed and are using clang with c++23 and the compilation variable is set in
your ``local.cmake`` file. The rest of the dependencies should be included via the ``CMakeLists``.

**2. Compiling for Windows/Mac/Linux**

If you are compiling for Linux, its recommended to not link against ``SDL3`` statically, however, it works on my machine.
Regardless, you should face no issues as all libraries in this workflow are accounted for.

**3. Compiling for Web Assembly**

I use docker and CI for this, you can copy the CI if you want but of course you'll need to set up your host and I won't
give you the cloudflare token for the official demo.

**4. Compiling for OGC/Wii/3ds/WiiU/Switch**

You need to set up ``devkitpro`` on your machine or in your CI. ``local.cmake`` does not ask for the paths here as
compiler variables and the compilation workflow will expect the correct variables to exist OS-level. On some IDEs this
isn't trivial but you can likely find a way.

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

Under Construction.

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