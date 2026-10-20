# MP3 dependencies

LAME 3.100, unchanged upstream encoder source mirrored from its release archive:
https://github.com/ffmpegwasm/lame, commit 2badea1974ae36cb8312afe99cff1e6b3b5decee.
Official project: https://lame.sourceforge.io/. LGPL 2 or later; see lame-3.100/COPYING and LICENSE. Built with upstream configMS.h and the SSE vector routine. Only Generate Data links the encoder.

minimp3 and minimp3_ex, https://github.com/lieff/minimp3,
commit ea99364f61c14656440e8d77e9c233ccf3124633. CC0; see MINIMP3-LICENSE.
Both game and generator use the same decoder and gapless sample-count validation.

Our CMakeLists.txt, mp3_decoder.cpp and cdda_archive files are integration code.
LAME writes a gapless Info/LAME tag after flushing. The archive timing table retains original CD track starts, stored pregaps and sector counts. A raw disc without that table keeps original playback. Generated data sectors remain unchanged. MP3 is lossy; 320 kbps CBR, 44.1 kHz stereo, encoder quality0.

The initial dr_mp3 prototype was superseded after real-disc end-of-track seeking exposed offset failures. It is not linked or shipped. minimp3 passed complete real-disc decoding and random-seek comparisons; see Port/data/032-audio-verification.log.
