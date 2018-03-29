/*
                  <<   MORSELIZER  v.2   >>

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

// Tune the operating mode, speed & melody of the Morselizer through these three params:
boolean	trainingMode = true; // when in 'Training mode', it generates random letters;
                            // else it morselizes whatever it gets from the Serial() input.
						  
unsigned long tDot = 2047; // between 1 and 65535. Tx speed is inversely proportional with tDot

float dotsPerLine = 3.0; // some HAM opperators prefer the 3.5 ratio to the official 3.0 one

float dotsPerSymbolsSeparator = 1.0; // some even prefer a shorter pause between symbols
// ---- end of the list of the params meant for user-tuning ----

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

// initialize the MorseMap array of structures with the morse encoding
static const struct {
  const char keyChar; 
  const byte nrOfSymbols; 
  const byte symbols;
} MorseMap[NoC] = {
	{'A',2,B1     },{'B',4,B1000  },{'C',4,B1010  },{'D',3,B100   },
	{'E',1,B0     },{'F',4,B10    },{'G',3,B110   },{'H',4,B0     },
	{'I',2,B0     },{'J',4,B111   },{'K',3,B101   },{'L',4,B100   },
	{'M',3,B11    },{'N',2,B10    },{'O',3,B111   },{'P',4,B110   },
	{'Q',4,B1101  },{'R',3,B10    },{'S',3,B0     },{'T',1,B1     },
	{'U',3,B1     },{'V',4,B1     },{'W',3,B11    },{'X',4,B1001  },
	{'Y',4,B1011  },{'Z',4,B1100  },{'@',6,B11010 },{'1',5,B1111  },
	{'2',5,B111   },{'3',5,B11    },{'4',5,B1     },{'5',5,B0     },
	{'6',5,B10000 },{'7',5,B11000 },{'8',5,B11100 },{'9',5,B11110 },
	{'0',5,B11111 },{'.',6,B10101 },{',',6,B110011},{'?',6,B1100  },
	{"'",6,B11110 },{'!',6,B101011},{'/',5,B10010 },{'(',5,B10110 },
	{')',6,B101101},{'&',5,B1000  },{':',6,B111000},{'=',5,B10001 },
	{'+',5,B1010  },{'-',6,B100001},{'"',6,B10010 },{' ',0,B1     }
};


ISR(TIMER1_CAPT_vect) {
  static boolean doInterSymbolsPause = false;
  unsigned int top;
  char ival;
  isIdling = false;
  
  if (doInterSymbolsPause) {
      top = tSymbolsSeparator;
      digitalWrite(LED_BUILTIN, false);
      doInterSymbolsPause = false; // so the next interrupt/time can't be (again) an inter-symbols_pause
  }
  else {
      if ( pull(&TxQueue, &ival) ) {
          switch (ival) {
              case '.':
                top = tDot;
                digitalWrite(LED_BUILTIN, true);
                doInterSymbolsPause = true; // a dot or a line is automatically followed by an inter-symbols_pause
                break;
              case '-':
                top = tLine;
                digitalWrite(LED_BUILTIN, true);
                doInterSymbolsPause = true;
                break;
              case 31: // the ASCII 'Unit Separator' represents the pause between the adjacent characters
                top = tCharsSeparator;
                digitalWrite(LED_BUILTIN, false);
                doInterSymbolsPause = false;
                break;
              case ' ': // the space between words translates into a seven-dots pause in total
                top = tWordsSeparator; // the previous (inherent) tCharsSeparator plus this one, makes 7
                digitalWrite(LED_BUILTIN, false);
                doInterSymbolsPause = false;
          }
      }
      else {
          top = 0xfffe; // when the ring buffer is empty, interrupt less often (4.2s ; idling)
          doInterSymbolsPause = false; 
          isIdling = true;
      }
  }
  ICR1 = top; // set the TOP counting value
}

void fEncode(char *inpBuff) {
  int b;
  byte j, n, k;
  for (b = 0; inpBuff[b]; ++b) {
    for (j = 0; j<NoC; ++j) {
      if (toupper(inpBuff[b]) == MorseMap[j].keyChar) {
        n = MorseMap[j].nrOfSymbols;
        for (k=n; k>0; --k) {
          if (MorseMap[j].symbols & (1<<(k-1))){
            push(&TxQueue, '-');
          }
          else {
            push(&TxQueue, '.');
          }
        }
        if (!n){
          push(&TxQueue, ' '); // space is the only exception
        }
        if (isIdling) {
          TCNT1 = 0xfffb; // end the idling; fix the counter to have the next interrupt immediately
          isIdling = !isIdling; 
        }
        break;
      }
    }
    push(&TxQueue, 31); // insert a 'Unit Separator' after each character
  }
}


void setup(){
  tDot = constrain(tDot, 128, 49152);
  dotsPerLine = constrain(dotsPerLine, 2.0, 4.0); // anti exageration
  dotsPerSymbolsSeparator = constrain(dotsPerSymbolsSeparator, 0.5, 1.5);
  tLine = dotsPerLine * tDot;
  tSymbolsSeparator = dotsPerSymbolsSeparator * tDot; // all dots and lines are followed by this pause
  tCharsSeparator = tDot * (dotsPerLine - dotsPerSymbolsSeparator); // all characters are followed by this
  tWordsSeparator = 4 * tDot; // added to the inherent tCharsSeparator, it gives in total a 7 dots wide pause

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
    fEncode(oStr.c_str());
    Serial.print(oStr); // echo back to the Serial Monitor what was received/typed
  }
  
  // This morse approach is a non-blocking one, so you can add/run more code here
  if (trainingMode) {
  	if (isIdling) {
  		random5();
  	}
  }

}

void random5() {
  static byte c5 = 1;
  char cStr[7] = {'Y', 'O', '4', 'M', 'M', ' ', '\0'};
  for(byte i = 0; i < 5; i++) {
    cStr[i] = random(65, 91);
  }
  fEncode(cStr);
  Serial.print(cStr); 
  if (c5 > 4) {
    Serial.print('\n');
    c5 = 0;
  }
  c5++ ;
}

