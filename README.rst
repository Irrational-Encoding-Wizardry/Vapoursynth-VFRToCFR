Description
===========

Converts Variable Frame Rate (VFR) video to a Constant Frame Rate (CFR) video through the use of Matroska Version 2 Timecodes.

Ported from the Avisynth plugin written by Nicholai Main.


Usage
=====
::

    vfrtocfr.VFRToCFR(clip clip, string timecodes, int fpsnum, int fpsden[, bint drop = True])

Parameters:
    *clip*
        Input clip.

    *timecodes*
        Name of the Matroska V2 Timecodes file.

    *fpsnum*
        Target output framerate numerator. Must be greater than 0.
        
    *fpsden*
        Target output framerate denominator. Must be greater than 0.
        
    *drop*
        If set to True, allows frame drops to acquire target framerate.
        Otherwise, throws an error if frames are dropped.

Building from sources
=====================
You need `The Meson Build System <http://mesonbuild.com>`_ installed.
::

    $ cd /path/to/src/root && mkdir build && cd build && meson --buildtype release .. && ninja  
    # ninja install
