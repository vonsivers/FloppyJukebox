// Play a file from the SD card in looping mode, from the SD card.
// Example program to demonstrate the use of the MIDFile library
//
// Hardware required:
//	SD card interface - change SD_SELECT for SPI comms

#include <SPI.h>
#include <SdFat.h> 
#include <MD_MIDIFile.h>

#define	USE_MIDI	1
#define SERIAL_RATE 56700

#if USE_MIDI // set up for direct MIDI serial output

#define DEBUGS(s)
#define	DEBUG(s, x)
#define	DEBUGX(s, x)

#else // don't use MIDI to allow printing debug statements

#define DEBUGS(s)     Serial.print(s)
#define	DEBUG(s, x)	  { Serial.print(F(s)); Serial.print(x); }
#define	DEBUGX(s, x)	{ Serial.print(F(s)); Serial.print(x, HEX); }

#endif // USE_MIDI

// compare tick time to lop time
//unsigned long lastlooptime = 0;

// SD chip select pin for SPI comms.
// Arduino Ethernet shield, pin 4.
// Default SD chip select is the SPI SS pin (10).
// Other hardware will be different as documented for that hardware.
#define  SD_SELECT  4

#define	ARRAY_SIZE(a)	(sizeof(a)/sizeof(a[0]))

// The files in the tune list should be located on the SD card 
// or an error will occur opening the file and the next in the 
// list will be opened (skips errors).
char *loopfile = "Ghostbusters.mid";  

SdFat	SD;
MD_MIDIFile SMF;

void midiCallback(midi_event *pev)
// Called by the MIDIFile library when a file event needs to be processed
// thru the midi communications interface.
// This callback is set up in the setup() function.
{

      /**
     * The periods for each MIDI note in an array.  The floppy drives
     * don't really do well outside of the defined range, so skip those notes.
     * Periods are in microseconds because that's what the Arduino uses for its
     * clock-cycles in the micro() function, and because milliseconds aren't
     * precise enough for musical notes.
     * 
     * Notes are named (e.g. C1-B4) based on scientific pitch notation (A4=440Hz) 
     */
   int microPeriods[128] = {
        30578, 28861, 27242, 25713, 24270, 22909, 21622, 20409, 19263, 18182, 17161, 16198, //C1 - B1
        15289, 14436, 13621, 12856, 12135, 11454, 10811, 10205, 9632, 9091, 8581, 8099, //C2 - B2
        7645, 7218, 6811, 6428, 6068, 5727, 5406, 5103, 4816, 4546, 4291, 4050, //C3 - B3
        3823, 3609, 3406, 3214, 3034, 2864, 2703, 2552, 2408, 2273, 2146, 2025 //C4 - B4
    };
    
    /**
     * Maximum number of cents to bend +/-.
     */
    int BEND_CENTS = 200;
    
    /**
     * Resolution of the Arduino code in microSeconds.
     */
    int ARDUINO_RESOLUTION = 40;
    
    /**
     * Current period of each MIDI channel (zero is off) as set 
     * by the NOTE ON message; for pitch-bending.
     */
    int currentPeriod[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; 
    byte pin;
    int period;
  

    DEBUG("\nM T", pev->track);
    DEBUG(":  Ch ", pev->channel+1);
    DEBUGS(" Data");
    for (uint8_t i=0; i<pev->size; i++)
     {
       DEBUGX(" ", pev->data[i]);
     }

  // for midi event able description see http://www.onicos.com/staff/iz/formats/midi-event.html
        if ((pev->data[0] >= 0x80) && (pev->data[0] <= 0x8F)) { // Note OFF
            //Convert the MIDI channel being used to the controller pin on the
            //Arduino by multiplying by 2.
            pin = (byte) (2 * (pev->channel + 1));
            sendEvent(pin,0);
            currentPeriod[pev->channel] = 0;
        } else if ((pev->data[0] >= 0x90) && (pev->data[0] <= 0x9F)) { // Note ON
            //Convert the MIDI channel being used to the controller pin on the
            //Arduino by multiplying by 2.
            pin = (byte) (2 * (pev->channel + 1));

            //Get note number from MIDI message, and look up the period.

            // After looking up the period, devide by (the Arduino resolution * 2).
            // The Arduino's timer will only tick once per X microseconds based on the
            // resolution.  And each tick will only turn the pin on or off.  So a full
            // on-off cycle (one step on the floppy) is two periods.
            if((pev->data[1] >= 0x18) && (pev->data[1] <= 0x47)) {
              period = microPeriods[pev->data[1]-24] / (ARDUINO_RESOLUTION * 2);
            }
             else {
              period = 0;
             }

            //Zero velocity events turn off the pin.
            if (pev->data[2] == 0) {
                sendEvent(pin, 0);
                currentPeriod[pev->channel] = 0;
            } else {
                sendEvent(pin, period);
                currentPeriod[pev->channel] = period;
            }
        } else if ((pev->data[0] >= 0xE0) && (pev->data[0] <= 0xEF)) { //Pitch bends
            //Only proceed if the note is on (otherwise, no pitch bending)
            if (currentPeriod[pev->channel - 1] != 0) {
                //Convert the MIDI channel being used to the controller pin on the
                //Arduino by multipying by 2.
                pin = (byte) (2 * (pev->channel + 1));

                double pitchBend = 2*(pev->data[2] << 7) + 2*(pev->data[1]);

                // Calculate the new period based on the desired maximum bend and the current pitchBend value
                int period = (int) (currentPeriod[pev->channel] / pow(2.0, (BEND_CENTS/1200.0)*((pitchBend - 8192.0) / 8192.0)));
                //System.out.println("Bent by " + Math.pow(2.0, (bendCents/1200.0)*((pitchBend - 8192.0) / 8192.0)));
                sendEvent(pin, period);
            }
        }
}

void sendEvent(byte pin, int periodData) {
  byte startMarker = 0x3C;
  byte endMarker = 0x3E;
  #if USE_MIDI
    Serial.write(startMarker);
    Serial.write(pin); 
    Serial.write((byte)periodData);
    Serial.write(endMarker);
  #endif
  
  DEBUG("\n## Writing to serial: byte 1: ", pin);
  DEBUG(", byte 2: ", (byte)periodData);
  
}

void setup(void)
{
  int  err;

  Serial.begin(SERIAL_RATE);

  DEBUGS("\n[MidiFile Looper]");

  // Initialize SD
  if (!SD.begin(SD_SELECT, SPI_FULL_SPEED))
  {
    DEBUGS("\nSD init fail!");
    while (true) ;
  }

  // Initialize MIDIFile
  SMF.begin(&SD);
  SMF.setMidiHandler(midiCallback);
  SMF.looping(true);

  // use the next file name and play it
  DEBUG("\nFile: ", loopfile);
  SMF.setFilename(loopfile);
  err = SMF.load();
  //SMF.setTempoAdjust(-40);
  //SMF.setTempo(120);
  
  // Set only 16 ticks per quarter note in order to keep up (!)
  SMF.setTicksPerQuarterNote(16); 

  // For comparing tick time to loop time
  /*
  Serial.println("\n##################"); 
  Serial.print("\nTick Time: "); 
  Serial.println(SMF.getTickTime());
  Serial.println("\n##################"); 
  */

  if (err != -1)
  {
    DEBUG("\nSMF load Error ", err);
    while (true);
  }
}

void loop(void)
{
	// play the file
	if (!SMF.isEOF())
	{
    // For comparing tick time to loop time
    /*
    Serial.print("\n#### Loop Time: "); 
    Serial.println(micros()-lastlooptime);
    lastlooptime = micros();
    */
    
		SMF.getNextEvent();
	}
}
