#include <TimerOne.h>

boolean firstRun = true; // Used for one-run-only stuffs;

//First pin being used for floppies, and the last pin.  Used for looping over all pins.
const byte FIRST_PIN = 2;
const byte PIN_MAX = 7;
#define RESOLUTION 40 //Microsecond resolution for notes

#define VERBOSE 0

#define SERIAL_RATE 57600

/*NOTE: Many of the arrays below contain unused indexes.  This is 
 to prevent the Arduino from having to convert a pin input to an alternate
 array index and save as many cycles as possible.  In other words information 
 for pin 2 will be stored in index 2, and information for pin 4 will be 
 stored in index 4.*/


/*An array of maximum track positions for each step-control pin.  Even pins
 are used for control, so only even numbers need a value here.  3.5" Floppies have
 80 tracks, 5.25" have 50.  These should be doubled, because each tick is now
 half a position (use 158 and 98).
 */
byte MAX_POSITION[] = {
  0,0,158,0,158,0,158,0};

//Array to track the current position of each floppy head.  (Only even indexes (i.e. 2,4,6...) are used)
byte currentPosition[] = {
  0,0,0,0,0,0,0,0};

/*Array to keep track of state of each pin.  Even indexes track the control-pins for toggle purposes.  Odd indexes
 track direction-pins.  LOW = forward, HIGH=reverse
 */
int currentState[] = {
  0,0,LOW,LOW,LOW,LOW,LOW,LOW
};

//Current period assigned to each pin.  0 = off.  Each period is of the length specified by the RESOLUTION
//variable above.  i.e. A period of 10 is (RESOLUTION x 10) microseconds long.
unsigned int currentPeriod[] = {
  0,0,0,0,0,0,0,0
};

//Current tick
unsigned int currentTick[] = {
  0,0,0,0,0,0,0,0 
};

int pin, period;

const byte numBytes = 32;
byte receivedBytes[numBytes];
byte numReceived = 0;

boolean newData = false;

// normalize RGB channels to equal brightness
long bright[3] = { 107, 67, 256};

// LED pins for all drives
#define R1 A0
#define G1 A1
#define B1 A2
#define R2 A3
#define G2 A4
#define B2 A5
#define R3 8
#define G3 9
#define B3 10

//Setup pins (Even-odd pairs for step control and direction
void setup(){
  pinMode(13, OUTPUT);// Pin 13 has an LED connected on most Arduino boards
  pinMode(2, OUTPUT); // Step control 1
  pinMode(3, OUTPUT); // Direction 1
  pinMode(4, OUTPUT); // Step control 2
  pinMode(5, OUTPUT); // Direction 2
  pinMode(6, OUTPUT); // Step control 3
  pinMode(7, OUTPUT); // Direction 3

  // RGB LED pins
  pinMode(R1, OUTPUT); // LED1 R
  pinMode(G1, OUTPUT); // LED1 G (LED broken)
  pinMode(B1, OUTPUT); // LED1 B
  pinMode(R2, OUTPUT); // LED2 R
  pinMode(G2, OUTPUT); // LED2 G
  pinMode(B2, OUTPUT); // LED2 B
  pinMode(R3, OUTPUT); // LED3 R
  pinMode(G3, OUTPUT); // LED3 G
  pinMode(B3, OUTPUT); // LED3 B (LED broken)

  //With all pins setup, let's do a first run reset
  resetAll();
  resetRGBLED();
  delay(1000);
	
  Timer1.initialize(RESOLUTION); // Set up a timer at the defined resolution
  Timer1.attachInterrupt(tick); // Attach the tick function

  Serial.begin(SERIAL_RATE);
}

void loop(){

  /*
    Serial.print(F("\n#### Loop Time: ")); 
    Serial.println(micros()-lastlooptime);
    lastlooptime = micros();
   */
  
recvBytesWithStartEndMarkers();

if (newData == true) {
       pin = receivedBytes[0];
       period = receivedBytes[1];
       if(pin == 100 && period == 100) {
        if(VERBOSE) {
          Serial.println("Resetting pins ...");
        }
        resetAll();
        resetRGBLED();
       }
       else {
       currentPeriod[pin] = period;
       setRGBLED(pin,period);
        if(VERBOSE) {
          Serial.print("Setting pin ");
          Serial.print(pin);
          Serial.print(" with period ");
          Serial.println(period);
        }
     }
     newData = false;
  }
}
      


/*
Called by the timer interrupt at the specified resolution.
 */
void tick()
{
  /* 
   If there is a period set for control pin 2, count the number of
   ticks that pass, and toggle the pin if the current period is reached.
   */
  if (currentPeriod[2]>0){
    currentTick[2]++;
    if (currentTick[2] >= currentPeriod[2]){
      togglePin(2,3);
      currentTick[2]=0;
    }
  }
  if (currentPeriod[4]>0){
    currentTick[4]++;
    if (currentTick[4] >= currentPeriod[4]){
      togglePin(4,5);
      currentTick[4]=0;
    }
  }
  if (currentPeriod[6]>0){
    currentTick[6]++;
    if (currentTick[6] >= currentPeriod[6]){
      togglePin(6,7);
      currentTick[6]=0;
    }
  }

}

void togglePin(byte pin, byte direction_pin) {

  //Switch directions if end has been reached
  if (currentPosition[pin] >= MAX_POSITION[pin]) {
    currentState[direction_pin] = HIGH;
    digitalWrite(direction_pin,HIGH);
  } 
  else if (currentPosition[pin] <= 0) {
    currentState[direction_pin] = LOW;
    digitalWrite(direction_pin,LOW);
  }

  //Update currentPosition
  if (currentState[direction_pin] == HIGH){
    currentPosition[pin]--;
  } 
  else {
    currentPosition[pin]++;
  }

  //Pulse the control pin
  digitalWrite(pin,currentState[pin]);
  currentState[pin] = ~currentState[pin];
}


//
//// UTILITY FUNCTIONS
//

//Not used now, but good for debugging...
void blinkLED(){
  digitalWrite(13, HIGH); // set the LED on
  delay(250);              // wait for a second
  digitalWrite(13, LOW); 
}

//For a given controller pin, runs the read-head all the way back to 0
void reset(byte pin)
{
  digitalWrite(pin+1,HIGH); // Go in reverse
  for (byte s=0;s<MAX_POSITION[pin];s+=2){ //Half max because we're stepping directly (no toggle)
    digitalWrite(pin,HIGH);
    digitalWrite(pin,LOW);
    delay(5);
  }
  currentPosition[pin] = 0; // We're reset.
  digitalWrite(pin+1,LOW);
  currentPosition[pin+1] = 0; // Ready to go forward.
}

//Resets all the pins
void resetAll(){

  // Old one-at-a-time reset
  //for (byte p=FIRST_PIN;p<=PIN_MAX;p+=2){
  //  reset(p);
  //}

  //Stop all notes (don't want to be playing during/after reset)
  for (byte p=FIRST_PIN;p<=PIN_MAX;p+=2){
    currentPeriod[p] = 0; // Stop playing notes
  }

  // New all-at-once reset
  for (byte s=0;s<80;s++){ // For max drive's position
    for (byte p=FIRST_PIN;p<=PIN_MAX;p+=2){
      digitalWrite(p+1,HIGH); // Go in reverse
      digitalWrite(p,HIGH);
      digitalWrite(p,LOW);
    }
    delay(5);
  }

  for (byte p=FIRST_PIN;p<=PIN_MAX;p+=2){
    currentPosition[p] = 0; // We're reset.
    digitalWrite(p+1,LOW);
    currentState[p+1] = 0; // Ready to go forward.
  }

}

void recvBytesWithStartEndMarkers() {
    static boolean recvInProgress = false;
    static byte ndx = 0;
    byte startMarker = 0x3C;
    byte endMarker = 0x3E;
    byte rb;
   

    while (Serial.available() > 0 && newData == false) {
        rb = Serial.read();

        if (recvInProgress == true) {
            if (rb != endMarker) {
                receivedBytes[ndx] = rb;
                ndx++;
                if (ndx >= numBytes) {
                    ndx = numBytes - 1;
                }
            }
            else {
                receivedBytes[ndx] = '\0'; // terminate the string
                recvInProgress = false;
                numReceived = ndx;  // save the number for use when printing
                ndx = 0;
                newData = true;
            }
        }

        else if (rb == startMarker) {
            recvInProgress = true;
        }
    }
}

void resetRGBLED() {
  digitalWrite(R1, LOW);
  //digitalWrite(G1, LOW); // LED broken
  digitalWrite(B1, LOW);
  digitalWrite(R2, LOW);
  digitalWrite(G2, LOW);
  digitalWrite(B2, LOW);
  digitalWrite(R3, LOW);
  digitalWrite(G3, LOW);
  //digitalWrite(B3, LOW); // LED broken
}

void setRGBLED(int pin, int period) {
  int k = 0;
  int value = LOW;
  if((period<=30578/80&&period>=25713/80) || (period<=15289/80&&period>=12856/80) || (period<= 7645/80&&period>=6428/80)|| (period<= 3823/80&&period>=3214/80)) {
    k = 0;
    value = HIGH;
  }
  else if ((period<=24270/80&&period>=20409/80) || (period<=12135/80&&period>=10205/80) || (period<= 6068/80&&period>=5103/80)|| (period<= 3034/80&&period>=2552/80)) {
    k = 1;
    value = HIGH;
  }
  else if ((period<=19263/80&&period>=16198/80) || (period<=9632/80&&period>=8099/80) || (period<= 4816/80&&period>=4050/80)|| (period<= 2408/80&&period>=2025/80)) {
    k = 2;
    value = HIGH;
  }
  else if (period == 0) {
    switch(pin) {
      case 2:
        digitalWrite(R1, LOW);
        //digitalWrite(G1, LOW); // LED broken
        digitalWrite(B1, LOW);
        break;
      case 4:
        digitalWrite(R2, LOW);
        digitalWrite(G2, LOW);
        digitalWrite(B2, LOW);
        break;
     case 6:
        digitalWrite(R3, LOW);
        digitalWrite(G3, LOW);
        //digitalWrite(B3, LOW); // LED broken
        break;
    }
  }

  switch(pin) {
    case 2:
      if(k==1) { // check for broken LED
        digitalWrite(B1, value);
      }
      else {
        digitalWrite(R1+k, value);
      }
      break;
    case 4:
      digitalWrite(R2+k, value);
      break;
    case 6:
      if(k==2) { // check for broken LED
        digitalWrite(G3, value);
      }
      else {
        digitalWrite(R3+k, value);
      }
      break;
      
  }
 
}

