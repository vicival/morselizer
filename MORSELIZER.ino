/*
                  <<   MORSELIZER  v3   >>

An asynchronous Text to Morse Converter and Morse training software 
for Arduino Uno, based on interrupts and Timer1.

Copyright (C) 2018   by   Valentin Lesovici

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
 
#include <Arduino.h>
#define NoC 52

/*   Tune the operating mode, audio, speed and melody of the Morselizer through these params:  */

/* When in 'Training Mode', it generates random letters; else it morselizes what it gets from Serial()*/
boolean	trainingMode = true;

/* You can hear it on Port 8. Don't connect the headphone, piezo or speaker directly to Port_8 .
Put an RC series group ( 1Kohm and 470nF) in series with the headphone (in order to limit the 
current through that I/O to 5mA); or else you'll burn the IC.
I advise AGAINST using THIS internal TONE generator because it's also based on interrupts which
are consuming processor time for nothing important; You better build/use an external tone generator 
with two transistors, commanded by Port 13 (the LED_BUILTIN) and disable this built-in tone. */
boolean toneOn = true;

/* The "width" of the Dot; between 1 and 65535. Transmit speed is INVERSELY proportional with tDot */
unsigned long tDot = 1023;

/* Some HAM opperators prefer the 3.5 ratio to the official 3.0 one; It changes the melody sound */
float dotsPerLine = 3.3;

/* Some HAM opperators even prefer a shorter/longer pause between symbols */
float dotsPerSymbolsSeparator = 1.0;

/*   the End of the params meant for user-tuning   */

unsigned long tLine;
unsigned long tSymbolsSeparator;
unsigned long tCharsSeparator;
unsigned long tWordsSeparator;
volatile boolean isIdling = true;


typedef struct cqs {
	volatile byte         first;
	volatile byte         last;
	volatile unsigned int itemsInside; // max is 0x0100 therefore it needs two bytes
	volatile char         data[256];
} strctq;

strctq   TxQueue; // reserve space for our 256 Bytes Un-locked Circular Buffer

void initRingBuff(strctq *q) { // buffer's initializer
	q->itemsInside =  0;
	q->first       =  0;
	q->last        =  0;  
}

void push(strctq *q, char item) { // this will put one char in the buffer
	unsigned char oldSREG;          // even if it's already "full" (un-locked)
	oldSREG = SREG; // prevent pull() to interrupt while in the middle of
	noInterrupts(); // of this push() or else it'll use obsolete indexes.
	if (q->itemsInside == 0x0100) {// there's no way to expand beyond 256 
		q->first++;                 // if it's full, sacrifice the oldest item
		q->itemsInside-- ;
	}                               
	q->data[q->last] = item;    // and push this new one.
	q->itemsInside++ ;
	q->last++; // the tail index is self-closing in a circle (0xff++ leads to 0)  
	SREG = oldSREG; // Restore interrupts (the global interrupt flag)
}

boolean pull(strctq *q, char *item) { // this will remove the oldest char/item
	if(q->itemsInside == 0) {           // from the buffer (if any..)
		*item = 0;
		return false; // is empty
	}
	*item = q->data[q->first];
	q->first++; // the head index is also self-closing in a circle  
	q->itemsInside-- ;
	return true;
}

// initialize the mMap array of structures with the morse encoding
static const struct {
	const char keyChar;
	const byte mSequence;
} mMap[NoC] = {
	{'E',2},{'T',3},{'I',4},{'A',5},{'N',6},{'M',7},{'S',8},{'U',9},
	{'R',10},{'W',11},{'D',12},{'K',13},{'G',14},{'O',15},{'H',16},
	{'V',17},{'F',18},{'L',20},{'P',22},{'J',23},{'B',24},{'X',25},
	{'C',26},{'Y',27},{'Z',28},{'Q',29},{'5',32},{'4',33},{'3',35},
	{'2',39},{'&',40},{'+',42},{'1',47},{'6',48},{'=',49},{'/',50},
	{'(',54},{'7',56},{'8',60},{'9',62},{'0',63},{'?',76},{'"',82},
	{'.',85},{'@',90},{"'",94},{'-',97},{'!',107},{')',109},{',',115},
	{':',120},{' ',128}
};
/*
the leftmost '1' bit (in the mSequence byte here ^^) marks the start 
boundary of the morse sequence, where a 0 means . (dot) and a 1 
means - (line) . For example,  ? (..--..) is encoded as 001100 but
after the 'start boundary' it becomes B01001100 , which is 76
Because of this "The Leftmost 1 Marks the Boundary" IDEA of JURS, 
the SIZE of this ^^ array is now half of the one in the previous version. 
To apply this idea, I had to rewrite the ISR() and fEncodeAndTransmit(). 
Because now an item/byte from the circular buffer holds the entire morse
sequence of a char, (not just one symbol (dots or line) as it did before)
up to 256 chars can be queued for transmission before it's out of sync.
https://forum.arduino.cc/index.php?topic=371198.0
*/

ISR(TIMER1_CAPT_vect) {
	static unsigned char ival;
	static boolean doInterSymbolsPause = false;
	static boolean doInterCharsPause = false;
	static byte n = 0;
	unsigned int top;
	isIdling = false;

	if (!doInterCharsPause) {
		if (!doInterSymbolsPause) {
			if ( n > 0) {
				digitalWrite(LED_BUILTIN, true); // light it immediately for it's a symbol anyway
				if (toneOn)	{tone(8, 300);}
				n-- ; // advance to this symbol
				if (bitRead(ival, n)) {
					top = tLine;
				}
				else {
					top = tDot;
				}
				if (n == 0) {
					doInterCharsPause = true; // schedule and inter-chars_pase after this last symbol of the char
				}
				else {
					doInterSymbolsPause = true; // this char has at least one more symbol
				}
			}
			else {
				if (pull(&TxQueue, &ival)) {
					if (ival != B10000000) { // is this a space character ?
						digitalWrite(LED_BUILTIN, true); // light it immediately for all** chars begin with a symbol anyway
						if (toneOn)	{tone(8, 300);}
						for (n=7; n>= 0; n--) {
							if (bitRead(ival, n)) {
								break; // exit with the index of the boundary
							}
						}
						n-- ; // first symbol sits AFTER the boundary
						if (bitRead(ival, n)) {
							top = tLine;
						}
						else {
							top = tDot;
						}
						if (n == 0) {
							doInterCharsPause = true; // all 'E' and 'T' end up through here, with this inter-chars_pause
						}
						else {
							doInterSymbolsPause = true; // this char has at least one more symbol
						}
					}
					else {	// indeed, it's a space character (the only special case..)
						digitalWrite(LED_BUILTIN, false);
						if (toneOn) {noTone(8);}
						top = tWordsSeparator;
						doInterCharsPause = false;
						doInterSymbolsPause = false;
						n = 0;
					}
				}
				else {	// start idling
					digitalWrite(LED_BUILTIN, false);
					if (toneOn) {noTone(8);}
					top = 0xfffe; // when the ring buffer is empty, interrupt less often (4.2s)
					doInterCharsPause = false;
					doInterSymbolsPause = false;
					isIdling = true;
				}
			}
		}
		else {	// do an inter-symbols_pause
			digitalWrite(LED_BUILTIN, false);
			if (toneOn) {noTone(8);}
			top = tSymbolsSeparator;
			doInterSymbolsPause = false; // the next one can't be again an inter-symbols_pause
		}
	}
	else {	// do an inter-Characters_pause
		digitalWrite(LED_BUILTIN, false);
		if (toneOn) {noTone(8);}
		top = tCharsSeparator;
		doInterCharsPause = false; // the next one can't be again an inter-Characters_pause
	}
  ICR1 = top; // set the TOP counting value
}

void fEncodeAndTransmit(char *inpBuff) {
	int b;
	byte j, n, k;
	for (b = 0; inpBuff[b]; ++b) {
		for (j = 0; j<NoC; ++j) {
			if (toupper(inpBuff[b]) == mMap[j].keyChar) {
				push(&TxQueue, mMap[j].mSequence);
				if (isIdling) {
					TCNT1 = 0xfffb; // end the idling; fix the counter to have the next interrupt immediately
					isIdling = !isIdling; 
				}
				break;
			}
		}
	}
}


void setup(){
	tDot = constrain(tDot, 128, 49152);
	dotsPerLine = constrain(dotsPerLine, 1.5, 4.5); // anti exageration
	dotsPerSymbolsSeparator = constrain(dotsPerSymbolsSeparator, 0.2, 2.2);
	tLine = dotsPerLine * tDot;
	tSymbolsSeparator = dotsPerSymbolsSeparator * tDot; // only the last symbol (dot or line) of a char 
														// is NOT followed by this pause (but by a tCharsSeparator)
	tCharsSeparator = 3 * tDot;; // all chars are followed by this (except Space)
	tWordsSeparator = 7 * tDot; // aka Space

	randomSeed(analogRead(0));
	pinMode(LED_BUILTIN, OUTPUT);
	digitalWrite( LED_BUILTIN, false );
	initRingBuff(&TxQueue);

	TCCR1A = 0; // initialize timer1 to interrupt - 'CTC mode 12'
	TCCR1B = 0;
	TCNT1  = 0;
	TCCR1B |= (1 << CS12) | (1 << CS10); // prescaler to 1024
	TCCR1B |= (1 << WGM13) | (1 << WGM12); // CTC 12
	ICR1  = 0x3d08; // and set it to trigger its first interrupt in 1s 
	TIMSK1 |= (1 << ICIE1);

	Serial.begin(9600);
	Serial.setTimeout(100); // don't wait for an eventual 'next one' and let it morse what it has
	Serial.print("Text to Morse Converter \n");
}

void loop() {
	String oStr;

	while (Serial.available() > 0) {
		oStr = Serial.readString(); // use Arduino_IDE's Serial Monitor to USB transmit ASCII codes  
		fEncodeAndTransmit(oStr.c_str());
		Serial.print(oStr); // echo back to the Serial Monitor what was received/typed
	}

	if (trainingMode) {
		if (isIdling) {
			random5();
		}
	}
}

void random5() {
	static byte c5 = 1;
	char cStr[7] = {'Y','O','4','M','M',' '};
	for(byte i = 0; i < 5; i++) {
		cStr[i] = random(65, 91);
	}
	fEncodeAndTransmit(cStr);
	Serial.print(cStr);
	if (c5 > 4) {
		Serial.print('\n');
		c5 = 0;
	}
	c5++ ;
}

