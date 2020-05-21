# morselizer

This MORSELIZER is an asynchronous morse code generator sketch for Arduino Uno that uses precisely timed hardware interrupts.

Unlike most morse code generators, this one runs in a non-blocking way in the background, much like the BIOS keyboard subroutine 
in PCs, leaving ~98% of the processor time at your disposal; so you can expand the sketch with your own heavy code, 
like add some LCD & keyboard, iambic keyer, morse decoder, mp3 player etc
