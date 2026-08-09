Introduction
============

Dependencies
------------

``platform-nes`` requires a few libraries, some of which are fetched at compile time for static compilation.

1. ``intsh`` - integer type shorthands under the ``br0`` namespace for easy concise type specification and usage.
2. ``tuple`` - a lightweight clone of ``std::tuple`` under ``br0::tuple`` to gain compile time advantages for ``llvm-mos-sdk``.
3. ``SDL3`` - used for WASM, Linux, Windows and MacOS builds handling input, audio and video.
4. ``devkitpro`` - used for nintendo ARM/PPC targets
5. ``libusb1.0`` - used for USB connectivity on linux


Compiling
---------

**1. Compiling For NES**

Ensure that you have the ``llvm-mos-sdk`` installed and are using clang with c++23 and the compilation variable is set in
your ``local.cmake`` file. The rest of the dependencies should be included via the ``CMakeLists``.

**2. Compiling for Windows/Mac/Linux**

If you are compiling for Linux, ``SDL3`` is linked against dynamically, Otherwise ``SDL3`` is linked against statically.
``libusb1.0`` is also linked against dynamically as its expected to be on your system.

**3. Compiling for Web Assembly**

I use docker and CI for this, you can copy the CI if you want but of course you'll need to set up your host and I won't
give you the cloudflare token for the official demo.

**4. Compiling for OGC/Wii/3ds/WiiU/Switch**

You need to set up ``devkitpro`` on your machine or in your CI. ``local.cmake`` does not ask for the paths here as
compiler variables and the compilation workflow will expect the correct variables to exist OS-level. On some IDEs this
isn't trivial but you can likely find a way.

Famistudio
----------

In the ``famistudio_config.s`` file ensure you have the correct configuration for what features you wish
`famistudio`_ to use. Without this `famistudio`_ may produce incorrect outputs on your NES target.

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

``platform-nes`` promotes symbolic referencable assets, it does this through compile time technology.

.. code-block:: c
    CHARACTER_ROM_BEGIN(chrSprite0)
    #embed "../../chr/sprites/sprite0.chr"
    CHARACTER_ROM_END(chrSprite0, CHR_ORIGIN); // include one asset

    CHARACTER_ROM_BEGIN(chrMushletStanding)
    #embed "../../chr/sprites/enemies/mushlet/standing.chr"
    CHARACTER_ROM_END_PAD_TO(chrMushletStanding, chrWand, CHR_TILES_PER_TABLE); // to pad

    CHARACTER_ROM_BEGIN(chrCoin)
    #embed "../../chr/tiles/dynamic/coin.chr"
    CHARACTER_ROM_END_FINAL(chrCoin, chrHUDWhitespace, 0x2000); // to finish

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

If you wish to create a trampoline (to have multiple interrupt flavour handlers):

.. code-block:: c

    interrupt nmi_handler() {
        // my interrupt code
    }

    NAKED_NMI {
        JUMP(nmi_handler) // or other function pointer, to create indirect jump
    }

.. warning::
    You absolutely must use the ``NAKED_NMI`` for this approach, as otherwise you **will** experience a stack overflow.
    This is because the ``NMI`` macro will generate llvm-mos interrupt boilerplate that the inline assembly macro
    ``JUMP`` will make the exit of the boiler plate (which handles stack) unreachable.

.. warning::
    As of writing this, llvm-mos has a 'c-stack' for both standard codeflow and interrupts. Entering an interrupt can
    be quite costly and will use memory in a fixed region and may write into reserved memory if an overlap is present.
    This can be mitigated by moving the ``c_writable`` segment elsewhere in the linker script.

Once you have this up and running, ensure you can build for both PC and NES and refer to the docs for other areas.
I hope you enjoy using ``platform-nes``.

Program ROM Bankswitching (VRC1)
--------------------------------


Character ROM Bankswitching (VRC1)
----------------------------------

.. _docker: https://www.docker.com
.. _famistudio: https://github.com/BleuBleu/FamiStudio