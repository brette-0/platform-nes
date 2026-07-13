reset
=====

Reset and startup handling lives in ``interrupts.hpp`` (there is no
separate ``reset`` header); this page pulls out just the reset-related
symbols. See :doc:`interrupts` for the rest of the file.

.. doxygendefine:: RESET
   :project: api

.. doxygendefine:: NMI
   :project: api

.. doxygenfunction:: reset
   :project: api
