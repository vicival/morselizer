<h1 align = "center">🌟Morselizer🌟</h1>

## **for Arduino Uno (and not only..) | Category - Ham Radio | License - [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html)**

This MORSELIZER is an asynchronous morse code generator sketch for Arduino Uno 
that uses precisely-timed hardware interrupts.

Unlike most morse code generators, this one runs in a non-blocking way (for the rest of the applications/code that you might add) in the 
background, much like the BIOS keyboard subroutine in PCs, leaving ~98% of the 
processor time at your disposal; so you can expand the sketch with your own 
heavy code, like add some LCD & keyboard, iambic keyer, morse decoder, toaster..

- Morse Code generator
- ASCII to Morse Mode (when connected to Arduino_IDE's Serial Monitor terminal) 
- CW
- Telegraphy
- Trainer Mode (groups of five random letters)
- Variable Transmit Speed
- Variable Dots/Line Ratio
- Variable inter-symbols Pause
- 1 µs Accuracy of the duration of all dots, lines (aka symbols) and pauses
- Assures a non-blocking flow of execution of the rest of the code (meaning the extra code you might add, to run together..)
- The characters you type in the terminal are queued into an Unlocked Circular Buffer
- it uses the 16 bits Timer1 in CTC mode 12 (a mode with no other examples on the net)
- easily portable to any microcontroller capable of timer-triggered interrupts
- Arduino Uno
- Easy to understand C++ code
- basically, an Interrupt Routine does it all so it's assuring a good isolation between it and whatever code you might want to add
