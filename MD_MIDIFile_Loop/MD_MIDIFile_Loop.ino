// Play a file from the SD card in looping mode, from the SD card.
// Example program to demonstrate the use of the MIDFile library
//
// Hardware required:
//	SD card interface - change SD_SELECT for SPI comms

#include <avr/pgmspace.h>

#include <SPI.h>
#include <SdFat.h> 
#include <MD_MIDIFile.h>

#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

#include "FSMtypes.h"    // FSM enumerated types

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

// SD chip select pin for SPI comms.
// Arduino Ethernet shield, pin 4.
// Default SD chip select is the SPI SS pin (10).
// Other hardware will be different as documented for that hardware.
#define  SD_SELECT  4

// LCD display defines ---------
#define  LCD_ROWS  2
#define  LCD_COLS  16


// LCD messages
const char string_0[] PROGMEM = "----- Mo's -----"; 
const char string_1[] PROGMEM = "-Floppy Jukebox-";
const char string_2[] PROGMEM = "PL create fail!";   
const char string_3[] PROGMEM = "Select music:";
const char string_4[] PROGMEM = "PL file not open";
const char string_5[] PROGMEM = "playing ...";
const char string_6[] PROGMEM = "SD init fail!";
const char string_7[] PROGMEM = "No files!";
const char* const string_table[] PROGMEM = {string_0, string_1, string_2, string_3, string_4, string_5, string_6, string_7};
char message[16];    // make sure this is large enough for the largest string it must hold

// input buttons
int NBUTTONS = 4;
const PROGMEM int buttons[4] = {6,7,8,9}; // pins for buttons
const char ButtonMsg[] PROGMEM  = {'L','S','P','R'};

// Playlist handling -----------
#define  FNAME_SIZE    13        // 8.3 + '\0' character file names
#define PLAYLIST_FILE "PL.txt"   // file of file names
#define MIDI_EXT    ".mid"      // MIDI file extension
uint16_t  plCount = 0;


SdFat	SD;
MD_MIDIFile SMF;

LiquidCrystal_I2C LCD(0x3F, LCD_COLS, LCD_ROWS);   


void midiCallback(midi_event *pev)
// Called by the MIDIFile library when a file event needs to be processed
// thru the midi communications interface.
// This callback is set up in the setup() function.
{
  
   int microPeriods[128] = {
        30578, 28861, 27242, 25713, 24270, 22909, 21622, 20409, 19263, 18182, 17161, 16198, //C1 - B1
        15289, 14436, 13621, 12856, 12135, 11454, 10811, 10205, 9632, 9091, 8581, 8099, //C2 - B2
        7645, 7218, 6811, 6428, 6068, 5727, 5406, 5103, 4816, 4546, 4291, 4050, //C3 - B3
        3823, 3609, 3406, 3214, 3034, 2864, 2703, 2552, 2408, 2273, 2146, 2025 //C4 - B4
    };
    
    
    // Maximum number of cents to bend +/-.
    int BEND_CENTS = 200;
    
    
    // Resolution of the Arduino code in microSeconds.
    int ARDUINO_RESOLUTION = 40;
    
    
    // Current period of each MIDI channel (zero is off) as set 
    // by the NOTE ON message; for pitch-bending.
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

                //double pitchBend = (pev->data[2] << 7) + (pev->data[1]);
                double pitchBend = (pev->data[2])*128. + (pev->data[1]);
                
                // Calculate the new period based on the desired maximum bend and the current pitchBend value
                int period = (int) (currentPeriod[pev->channel] / pow(2.0, (BEND_CENTS/1200.0)*((pitchBend - 8192.0) / 8192.0)));
                //System.out.println("Bent by " + Math.pow(2.0, (bendCents/1200.0)*((pitchBend - 8192.0) / 8192.0)));
                sendEvent(pin, period);
            }
        }
        
}

/*
void midiCallback(midi_event *pev)
// Called by the MIDIFile library when a file event needs to be processed
// thru the midi communications interface.
// This callback is set up in the setup() function.
{
#if USE_MIDI
  if ((pev->data[0] >= 0x80) && (pev->data[0] <= 0xe0))
  {
    Serial.write(pev->data[0] | pev->channel);
    Serial.write(&pev->data[1], pev->size-1);
  }
  else
    Serial.write(pev->data, pev->size);
#endif
  DEBUG("\nM T", pev->track);
  DEBUG(":  Ch ", pev->channel+1);
  DEBUGS(" Data");
  for (uint8_t i=0; i<pev->size; i++)
  {
    DEBUGX(" ", pev->data[i]);
  }
}
*/

void sendEvent(byte pin, int periodData) {
  byte startMarker = 0x3C;
  byte endMarker = 0x3E;
  #if USE_MIDI
    Serial.write(startMarker);
    Serial.write(pin); 
    Serial.write((byte)periodData);
    Serial.write(endMarker);
  #endif
  
  //DEBUG("\n## Writing to serial: byte 1: ", pin);
  //DEBUG(", byte 2: ", (byte)periodData);
  
}

char getButton() {
  for(int i=0;i<NBUTTONS;++i) {
    if (digitalRead(pgm_read_word_near(buttons+i)) == HIGH) {
      char button = pgm_read_word_near(ButtonMsg+i);
      DEBUG("\n#### Pressed button ", button);
      delay(300);
      return button;
    }
  }
}

//---------------------------


void midiSilence(void)
// Turn everything off on every channel.
// Some midi files are badly behaved and leave notes hanging, so between songs turn
// off all the notes and sound
{
  midi_event  ev;

  // All sound off
  // When All Sound Off is received all oscillators will turn off, and their volume
  // envelopes are set to zero as soon as possible.
  ev.size = 0;
  ev.data[ev.size++] = 0xb0;
  ev.data[ev.size++] = 120;
  ev.data[ev.size++] = 0;

  for (ev.channel = 0; ev.channel < 16; ev.channel++)
    midiCallback(&ev);
}


// LCD Message Helper functions -----------------
void LCDMessage(uint8_t r, uint8_t c, const char *msg, bool clrEol = false)
// Display a message on the LCD screen with optional spaces padding the end
{
  LCD.setCursor(c, r);
  LCD.print(msg);
  if (clrEol)
  {
    c += strlen(msg);
    while (c++ < LCD_COLS)
      LCD.write(' ');
  }
}

void LCDErrMessage(const char *msg, bool fStop)
{
  LCDMessage(1, 0, msg, true);
  DEBUGS(F("\nLCDErr: "));
  DEBUGS(msg);
  while (fStop) ;   // stop here if told to
  delay(2000);    // if not stop, pause to show message
}


// Create list of files for menu --------------

uint16_t createPlaylistFile(void)
// create a play list file on the SD card with the names of the files.
// This will then be used in the menu.
{
  SdFile    plFile;   // play list file
  SdFile    mFile;    // MIDI file
  uint16_t  count = 0;// count of files
  char      fname[FNAME_SIZE];

  // open/create the play list file
  //LCDErrMessage("Creating playlist", false); //*
  if (!plFile.open(PLAYLIST_FILE, O_RDWR | O_CREAT | O_AT_END)) {
    strcpy_P(message, (char*)pgm_read_word(&(string_table[2]))); // Necessary casts and dereferencing, just copy.
    LCDErrMessage(message, true);
  }

  //LCDErrMessage("playlist created", false); //*
  SD.vwd()->rewind();
  while (mFile.openNext(SD.vwd(), O_READ))
  {
    mFile.getName(fname,FNAME_SIZE);

    DEBUGS(F("\nFile "));
    DEBUGS(count);
    DEBUGS(F(" "));
    DEBUGS(fname);

    if (mFile.isFile())
    {
      if (strcmp(MIDI_EXT, &fname[strlen(fname)-strlen(MIDI_EXT)]) == 0)
      // only include files with MIDI extension
      {
        plFile.write(fname, FNAME_SIZE);
        count++;
      }
    }
    mFile.close();
  }
  DEBUGS(F("\nList completed"));

  // close the play list file
  plFile.close();

  return(count);
}

// FINITE STATE MACHINES -----------------------------

seq_state lcdFSM(seq_state curSS)
// Handle selecting a file name from the list (user input)
{
  static lcd_state s = LSBegin;
  static uint8_t  plIndex = 0;
  static char fname[FNAME_SIZE];
  static SdFile plFile;   // play list file

  // LCD state machine
  switch (s)
  {
  case LSBegin:
    strcpy_P(message, (char*)pgm_read_word(&(string_table[3]))); // Necessary casts and dereferencing, just copy.
    LCDMessage(0, 0, message, true);
    if (!plFile.isOpen())
    {
      if (!plFile.open(PLAYLIST_FILE, O_READ)) {
        //char message2[16];
        strcpy_P(message, (char*)pgm_read_word(&(string_table[4]))); // Necessary casts and dereferencing, just copy.
        LCDErrMessage(message, true);
      }
    }
    s = LSShowFile;
    break;

  case LSShowFile:
    plFile.seekSet(FNAME_SIZE*plIndex);
    plFile.read(fname, FNAME_SIZE);

    LCDMessage(1, 0, fname, true);
    LCD.setCursor(LCD_COLS-2, 1);
    LCD.print(plIndex == 0 ? ' ' : '<');
    LCD.print(plIndex == plCount-1 ? ' ' : '>');
    s = LSSelect;
    break;

  case LSSelect:
    switch (getButton())
    // Keys are mapped as follows:
    // 'L':  use the previous file name (move back one file name)
    // 'S':  move to the first file name
    // 'P':  move on to the next state in the state machine
    // 'R':  use the next file name (move forward one file name)

    {
      
      case 'L': // Left
        if (plIndex != 0) 
          plIndex--;
        s = LSShowFile;
        break;

        case 'S': // Stop
        plIndex = 0;
        s = LSShowFile;
        break;

        case 'P': // Play
        s = LSGotFile;
        break;

      case 'R': // Right
        if (plIndex != plCount-1) 
          plIndex++;
        s = LSShowFile;
        break;

        
    }
    break;

  case LSGotFile:
    // copy the file name and switch mode to playing MIDI
    SMF.setFilename(fname);
    curSS = MIDISeq;
    // fall through to default state

  default:
    s = LSBegin;
    break;
  }  

  return(curSS);
}

seq_state midiFSM(seq_state curSS)
// Handle playing the selected MIDI file
{
  static midi_state s = MSBegin;
  
  switch (s)
  {
  case MSBegin:
    // Set up the LCD 
    LCDMessage(0, 0, SMF.getFilename(), true);
    strcpy_P(message, (char*)pgm_read_word(&(string_table[5]))); // Necessary casts and dereferencing, just copy.
    LCDMessage(1, 0, message, true);   // string of user defined characters
    sendEvent((byte)100,100); // reset pins
    s = MSLoad;
    break;

  case MSLoad:
    // Load the current file in preparation for playing
    {
      int  err;

      // Attempt to load the file
      if ((err = SMF.load()) == -1)
      {
        // Set tempo by defining number of ticks per quater note
        SMF.setTicksPerQuarterNote(24);

        s = MSProcess;
      }
      else
      {
        char  aErr[16];

        sprintf(aErr, "SMF error %03d", err);
        LCDErrMessage(aErr, false);
        s = MSClose;
      }
    }
    break;

  case MSProcess:
    // Play the MIDI file
    if (!SMF.isEOF())
    {
      SMF.getNextEvent();      
    }    
    else {
      s = MSClose;
    }

    // check the keys
    switch (getButton())
    {
      case 'L': SMF.restart(); sendEvent((byte)100,100); break;  // Rewind
      case 'S': s = MSClose;      break;  // Stop
      case 'P':                   break;  // Nothing assigned to this key
      case 'R':                   break;  // Nothing assigned to this key
    }

    break;

  case MSClose:
    // close the file and switch mode to user input
    SMF.close();
    midiSilence();
    sendEvent((byte)100,100); // reset pins
    curSS = LCDSeq;
    // fall through to default state

  default:
    s = MSBegin;
    break;
  }

  return(curSS);
}

//---------------------------


void setup(void)
{

  int  err;

  // initialise MIDI output stream
  Serial.begin(SERIAL_RATE);

  // initialise LCD display
  //LCD.begin(LCD_COLS, LCD_ROWS);
  LCD.init();
  LCD.backlight();
  LCD.clear();
  LCD.noCursor();
  strcpy_P(message, (char*)pgm_read_word(&(string_table[0]))); // Necessary casts and dereferencing, just copy.
  LCDMessage(0, 0, message, false);
  strcpy_P(message, (char*)pgm_read_word(&(string_table[1]))); // Necessary casts and dereferencing, just copy.
  LCDMessage(1, 0, message, false);

  // setup input pins for buttons
  for(int i=0;i<NBUTTONS;++i) {
    pinMode(buttons[i], INPUT); 
  } 

  // initialise SDFat
  if (!SD.begin(SD_SELECT, SPI_FULL_SPEED)) {
    strcpy_P(message, (char*)pgm_read_word(&(string_table[6]))); // Necessary casts and dereferencing, just copy.
    LCDErrMessage(message, true);
  }
  
  plCount = createPlaylistFile();
  if (plCount == 0) {
    strcpy_P(message, (char*)pgm_read_word(&(string_table[7]))); // Necessary casts and dereferencing, just copy.
    LCDErrMessage(message, true);
  }
  
  // initialise MIDIFile
  
  SMF.begin(&SD);
  SMF.setMidiHandler(midiCallback);  

  delay(2000);   // allow the welcome to be read on the LCD
  LCD.clear();
}

void loop(void)
// only need to look after 2 things - the user interface (LCD_FSM) 
// and the MIDI playing (MIDI_FSM). While playing we have a different 
// mode from choosing the file, so the FSM will run alternately, depending 
// on which state we are currently in.
{

  static seq_state  s = LCDSeq;

  switch (s)
  {
    case LCDSeq:  s = lcdFSM(s);  break;
    case MIDISeq: s = midiFSM(s); break;
    default: s = LCDSeq;
  }
  
}
