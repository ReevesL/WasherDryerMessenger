//*****************************************************************
// Washer and Dryer Messenger
// Send notifications when washing and/or drying are complete
// License: GPL
// By Reeves Little
// http://little.org
//*****************************************************************
/*
// Messenger V2
//
// Built up from Thomas Taylor's Dryer Messenger
// Used Thomas Taylor's Dryer Messenger code, ripped out WiFly
//   code since I don't have one.
//
// Change history
// 8/10 - Corrected target addresses
// 5/27 - added some fresh response strings
// 4/28 - completed V1
// 11/27/2013 - Started V2 (addition of washer)
// 12/07/2013 - Correction of current transformer pin logic
// 12/31/2013 - Added logic to measurement to account for different washer/dryer offsets
// 01/03/2014 - Pulling washer & dryer strings from a web page
// 01/13/2014 - Added config to shut off string pull for perf and debugging
// 02/03/2014 - Increased washer timeout to 1min30seconds for my washer, too many false alarms
// 07/13/2014 - Started V4, second try at pulling strings from web
// 10/02/2014 - Cleaning up code, clarifying comments
//
//
// Credits:
//
//  Original Dryer Messenger code for Make by Thomas Taylor
//  http://makezine.com/projects/make-34/the-dryer-messenger/
//  http://makezine.com/author/thomas-taylor/
//
//  Code for the current transformer measurement and current calculations 
//  were done by Trystan Lea at http://openenergymonitor.org
//
//  Twitter library was originally done by Neocat. More information on that
//  library can be found on the Arduino Playground: http://www.arduino.cc/playground/Code/TwitterLibrary
//  and also the twitter library site: http://arduino-tweet.appspot.com/
//
*/

#include <Arduino.h>
#include <Streaming.h>
#include <PString.h>
#include <SoftwareSerial.h>
#include "MemoryFree.h"
#include <TextFinder.h>


// Tweet
#if defined(ARDUINO) && ARDUINO > 18   // Arduino 0019 or later
#include <SPI.h>
#endif
#include <Ethernet.h>
//#include <EthernetDNS.h>  Only needed in Arduino 0022 or earlier
#include <Twitter.h>

// Debugging flags
#define _DEBUG  1   // try to push data to serial monitor (save memory by setting to 0)
#define NOTWEET 0   // don't start networking or send tweet 
#define WASHER  0   // washing machine connected, 0==no
#define DRYER   1   // dryer connected, 0==no
#define WEBMSG  0   // pull tweet strings from the web, 0==no

// Dryer settings
// Using analog 0 for the Current Transformer
#define CTPIN_D              0
#define WAITONTIME         15000        //In milliseconds, time before setting appliace as "running" (used for Washer & Dryer)
#define WAITOFFTIME        30000        //In milliseconds, time to wait to make sure dryer is really off (avoid false positives)
#define CURRENT_THRESHOLD    20.0    //In AMPS
double CURRENT_OFFSET  =   0.0;  // Dryer offset (made a double so it can auto set)
#define NUMADCSAMPLES      4000     //Number of samples to get before calculating RMS

// Washer settings
// Using analog 5 for the Current Transformer for Washer
#define CTPIN_W            5
#define CURRENT_THRESHOLD_W      10.0    // In AMPS
double CURRENT_OFFSET_W   =     0.0;  // Washer offset in Amps
#define WAITOFFTIME_W        99000    // In MS 

//STATUS LEDS
#define DRYLED   5     //Dryer status
#define WASHLED    7   //Washer status
#define REDLED     6   //General status/error

// Buffer for reading messages?
char strBuf[165];      
PString str(strBuf, sizeof(strBuf));
#define TEMP_MSG_BUFFER_SIZE 100
char tempMsgBuffer[TEMP_MSG_BUFFER_SIZE];

// Networking settings
byte mac[] = { 
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
//byte ip[] = { 10, 0, 0, 177 }; // not used, DHCP enabled, uncoment and manually set if needed

// Twitter settings
// Uses OAuth tokens provided by service to auth
// Go to http://arduino-tweet.appspot.com/ to set up your account
Twitter twitter("YOUR_DRYER_TOKEN_HERE"); // OAuth token for dryer
Twitter washTwit("YOUR_WASHER_TOKEN_HERE"); // OAuth token for washer 

// Random strings db settings
char stringServer[] = "YOUR_STRING_SERVER";    // Server for strings

//If using 3.3V Arduino use this
#define ADC_OFFSET 1.65                    //Resistor divider is used to bias current transformer 3.3V/2 = 1.65V
#define ADCVOLTSPERDIVISION 0.003222656    //3.3V/1024 units

//If using 5V Arduino use this
//#define ADC_OFFSET 2.5                   //Resistor divider is used to bias current transformer 5V/2 = 2.5V
//#define ADCVOLTSPERDIVISION 0.0047363    //5V / 1024 units

//These are the various states during monitoring. WAITON and WAITOFF both have delays
//that can be set with the WAITONTIME and WAITOFFTIME parameters
enum STATE{
  WAITON,    //Washer/Dryer is off, waiting for it to turn on
  RUNNING,   //Exceeded current threshold, waiting for washer/dryer to turn off
  WAITOFF    //Washer/dryer is off, sends notifications, and returns to WAITON
};

//This keeps up with the current state of the FSM, default startup setting is the first state of waiting for the dryer to turn on.
STATE dryerState = WAITON;
STATE washerState = WAITON;

EthernetClient client;
TextFinder  finder( client );  

boolean printCurrent = true;

void setup() {

  Serial.begin(9600);

  delay(1000);

  // Configure LEDs
  pinMode(DRYLED, OUTPUT);
  pinMode(REDLED, OUTPUT);
  pinMode(WASHLED, OUTPUT);

  // Turn on the red LED during setup
  digitalWrite(REDLED, HIGH);

  if (_DEBUG){
    Serial.println("Washer Dryer Tweeter");
    Serial.println("==========================");

  }


  if (!NOTWEET) { // check to see if debugging/no tweeting
    if (_DEBUG){
      Serial.println("Initializing network");

    }
    while (Ethernet.begin(mac) != 1) {

      if (_DEBUG){
        Serial.println("Error getting IP address via DHCP, trying again...");
        delay(15000);
      }
    }
  }
  else {
    if (_DEBUG){
      Serial.println("----- No Tweets: Networking off -----");
    }
  }
  if (_DEBUG && !NOTWEET){
    if (!NOTWEET) { // check to see if debugging/no tweeting

      Serial.println("Network initialized");
      Serial.println();
    }

    Serial.println(F("Starting Dryer Messenger." ) );
  }
  //Initialize random seed using unused analog 1
  randomSeed(analogRead(1));


  // Instead of having the offset hard coded, I've made it zero itself
  // on boot. This removed variance in the offset that seemed to come
  // from different power sources for the Arduino.
  //
  // Pull current on startup to zero the measurements
  double theMeasurement = 0;
  // Pull washer
  for (int i = 0; i < 3; i++){
    theMeasurement +=takeCurrentMeasurement(CTPIN_W);
  }
  // 
  CURRENT_OFFSET_W = 0 - (theMeasurement/3);

  // display the calculation
  if (_DEBUG){
    Serial << F("Washer offset: ") << CURRENT_OFFSET_W  << endl; 
    delay(5000);
  }


  theMeasurement = 0; // zero the measurement variable
  // Pull dryer
  for (int i = 0; i < 3; i++){
    theMeasurement +=takeCurrentMeasurement(CTPIN_D);
  }
  // 
  CURRENT_OFFSET = 0 - (theMeasurement/3);

  // display the calculation
  if (_DEBUG){
    Serial << F("Dryer offset: ") << CURRENT_OFFSET  << endl; 
    delay(5000);
  }
  // Setup successful, turn out red LED, turn on the wash and dry LEDs
  digitalWrite(REDLED, LOW); 
  digitalWrite(WASHLED, HIGH);
  digitalWrite(DRYLED, HIGH);

}

void loop() {
  if (WASHER)
  {
    monitorWasher();
  }

  if (DRYER)
  {
    monitorDryer();
  } 


} //loop

void monitorWasher()
{
  // TODO: what's the loop count for?
  static unsigned long wLoopCount = 0;
  static unsigned long wStartTime;
  static unsigned long wRunStartTime;
  static boolean wWaitCheck;
  double wCurrent;

  wLoopCount++;
  switch (washerState)
  {
  case WAITON:
    wCurrent = takeCurrentMeasurement(CTPIN_W);
    if(printCurrent)
    {
      Serial << F("Washer Current: ") << wCurrent  << F(" Washer Threshold: ") << CURRENT_THRESHOLD_W << endl; 
    }
    if(wCurrent > CURRENT_THRESHOLD_W)
    {
      if(!wWaitCheck)
      {
        wStartTime = millis();
        wWaitCheck = true;
        Serial << F("Started watching washer in WAITON. wStartTime: ") << wStartTime << endl;
      }
      else
      {
        if( (millis() - wStartTime  ) >= WAITONTIME)
        {
          Serial << F("Changing washer state to RUNNING") << endl;
          washerState = RUNNING;
          wWaitCheck = false;
          wRunStartTime = millis();
        }
      }
    }
    else
    {
      if(wWaitCheck)
      {
        Serial << F("False alarm, not enough time elapsed to advance washer to running") << endl;
      }
      wWaitCheck = false;
    }
    break;

  case RUNNING:
    wCurrent = takeCurrentMeasurement(CTPIN_W);
    digitalWrite(WASHLED, !digitalRead(WASHLED));
    if(printCurrent)
    {
      Serial << F("Washer Current: ") << wCurrent  << F(" Threshold: ") << CURRENT_THRESHOLD_W << endl; 
    }
    if(wCurrent < CURRENT_THRESHOLD_W)
    {
      Serial << F("Changing washer state to WAITOFF") << endl;
      washerState = WAITOFF;        
    }
    break;

  case WAITOFF:
    if(!wWaitCheck)
    {
      wStartTime = millis();
      wWaitCheck = true;
      if(printCurrent)
      {
        Serial << F("Washer Current: ") << wCurrent  << F(" Threshold: ") << CURRENT_THRESHOLD_W << endl; 
        Serial << F("Entered washwer WAITOFF. wStartTime: ") << wStartTime  << endl;
      }
      // setting the RED LED to solid on while waiting to send off signal.
      // TODO: Check for conflict with dryer use of red led
      digitalWrite(REDLED, HIGH);
    }

    wCurrent = takeCurrentMeasurement(CTPIN_W);

    if(wCurrent > CURRENT_THRESHOLD_W)
    {      
      if(printCurrent)
      {
        Serial << F("Washer Current: ") << wCurrent  << F(" Threshold: ") << CURRENT_THRESHOLD_W << endl; 
        Serial << F("False Alarm, washer not off long enough, back to RUNNING we go!") << endl;
      }
      wWaitCheck = false;
      // TODO: need to make it so the false alarm light goes off when done,
      //       but keep false alarm on one from turning off light for other.
      digitalWrite(REDLED, LOW);

      washerState = RUNNING;
    }  
    else
    {
      if( (millis() - wStartTime) >= WAITOFFTIME_W)
      {
        Serial << F("Washer cycle complete") << endl;
        str.begin();
        str.print(GetAMessage('w')); // call GetAMessage with 'w' for washer message

        // Cycle time
        // I don't really care about this info, commenting out
        // Uncomment if you want to know how long the cycle ran
        //        str.print( F(" Wash Time: " ));
        //        double totalWashRun = (double) (millis() - wRunStartTime) / 1000 / 3600;
        //        str.print(totalWashRun);
        //        if(totalWashRun < 0)
        //          str.print("hr");
        //        else
        //          str.print("hrs");

        // @mention my account so I get a push notification
        // TODO: Should move this into a common settings area
        //        to make it easier to configure.
        str.print(" @YOUR_TWITTER_HANDLE_HERE");

        Serial << F("Posting to twitter") << endl;
        Serial << F("Posting the following: ") << endl;
        Serial << str << endl;

        //Tweet it (unless in notweet debugging mode)
        if (!NOTWEET) { // check to see if debugging/no tweeting

          if (washTwit.post(str)) {
            int status = washTwit.wait();
            if (status == 200) {
              Serial.println("OK.");
              Serial << F("Post Successful!") << endl;

            } 
            else {
              Serial.print("failed : code ");
              Serial.println(status);
            }
          } 
          else {
            Serial.println("connection failed.");
          }
        }
        else {
          Serial << F("----- Not posted: Tweets off (debug). -----") << endl;
        }

        Serial << F("Resetting washer state back to WAITON") << endl;
        washerState = WAITON;
        wWaitCheck = false;
        // TODO: setting RED to low will turn off even if 
        //       washer is in wait off. Need logic to
        //       account for the potential conflict.
        digitalWrite(REDLED, LOW);
        digitalWrite(WASHLED, HIGH);
      }
    }
  }

} // monitorWasher

void monitorDryer()
{
  static unsigned long loopCount = 0;
  static unsigned long startTime;
  static unsigned long runStartTime;
  static boolean waitCheck;
  double current;

  loopCount++;
  switch (dryerState)
  {
  case WAITON:
    current = takeCurrentMeasurement(CTPIN_D);
    if(printCurrent)
    {
      Serial << F("Dryer Current: ") << current  << F(" Dryer Threshold: ") << CURRENT_THRESHOLD << endl; 
    }
    if(current > CURRENT_THRESHOLD)
    {
      if(!waitCheck)
      {
        startTime = millis();
        waitCheck = true;
        Serial << F("Started watching dryer in WAITON. startTime: ") << startTime << endl;
      }
      else
      {
        if( (millis() - startTime  ) >= WAITONTIME)
        {
          Serial << F("Changing dryer state to RUNNING") << endl;
          dryerState = RUNNING;
          waitCheck = false;
          runStartTime = millis();
        }
      }
    }
    else
    {
      if(waitCheck)
      {
        Serial << F("False alarm, not enough time elapsed to advance to running") << endl;
      }
      waitCheck = false;
    }
    break;

  case RUNNING:
    current = takeCurrentMeasurement(CTPIN_D);
    digitalWrite(DRYLED, !digitalRead(DRYLED));
    if(printCurrent)
    {
      Serial << F("Dryer Current: ") << current  << F(" Threshold: ") << CURRENT_THRESHOLD << endl; 
    }
    if(current < CURRENT_THRESHOLD)
    {
      Serial << F("Changing dryer state to WAITOFF") << endl;
      dryerState = WAITOFF;        
    }
    break;

  case WAITOFF:
    if(!waitCheck)
    {
      startTime = millis();
      waitCheck = true;
      Serial << F("Entered WAITOFF. startTime: ") << startTime  << endl;

      // setting the RED LED to solid on while waiting to send off signal.
      digitalWrite(REDLED, HIGH);
    }

    current = takeCurrentMeasurement(CTPIN_D);

    if(current > CURRENT_THRESHOLD)
    {
      Serial << F("False Alarm, dryer not off long enough, back to RUNNING we go!") << endl;
      waitCheck = false;
      dryerState = RUNNING;
    }  
    else
    {
      if( (millis() - startTime) >= WAITOFFTIME)
      {
        Serial << F("Dryer cycle complete") << endl;
        str.begin();
        str.print(GetAMessage('d'));

        // How long was the cycle?
        // I don't really care, so commeting out
        // Uncomment if you want this info in the tweet
        //        str.print( F(" Run Time: " ));
        //        double totalRun = (double) (millis() - runStartTime) / 1000 / 3600;
        //        str.print(totalRun);
        //        if(totalRun < 0)
        //          str.print("hr");
        //        else
        //          str.print("hrs");

        // @mention my account so I get a push notification
        // TODO: Should move this into a common settings area
        //        to make it easier to configure.
        str.print(" @YOUR_TWITTER_HANDLE_HERE");

        Serial << F("Posting to twitter") << endl;
        Serial << F("Posting the following: ") << endl;
        Serial << str << endl;

        //Tweet it (unless in notweet debugging mode)
        if (!NOTWEET) { // check to see if debugging/no tweeting

          if (twitter.post(str)) {
            int status = twitter.wait();
            if (status == 200) {
              Serial.println("OK.");
              Serial << F("Post Successful!") << endl;

            } 
            else {
              Serial.print("failed : code ");
              Serial.println(status);
            }
          } 
          else {
            Serial.println("connection failed.");
          }
        }
        else {
          Serial << F("----- Not posted: Tweets off (debug). -----") << endl;

        }



        Serial << F("Resetting state back to WAITON") << endl;
        dryerState = WAITON;
        waitCheck = false;
        digitalWrite(DRYLED, HIGH);
        digitalWrite(REDLED, LOW);
      }
    }
  }

}// end monitorDryer

double takeCurrentMeasurement(int channel)
{
  static double VDOffset = 1.65;

  //Equation of the line calibration values
  //double factorA = 15.2; //factorA = CT reduction factor / rsens
  //double factorA = 25.7488;
  double factorA = 33.0113;
  //double factorA = 29.03225;
  double Ioffset =  -0.04;
  double theCurrentOffset = 0.0;

  if (channel == CTPIN_D) { // check if washer or dryer
    theCurrentOffset = CURRENT_OFFSET; // use dryer offset
  }
  else {
    theCurrentOffset = CURRENT_OFFSET_W; // use washer offset
  }

  //Used for calculating real, apparent power, Irms and Vrms.
  double sumI=0.0;

  int sum1i=0;
  double sumVadc=0.0;

  double Vadc,Vsens,Isens,Imains,sqI,Irms;

  for(int i = 0; i < NUMADCSAMPLES; i++)
  {
    int val = 0;
    val = analogRead(channel);
    Vadc = val * ADCVOLTSPERDIVISION;
    Vsens = Vadc - VDOffset;
    //Current transformer scale to find Imains
    Imains = Vsens;
    //Calculates Voltage divider offset.
    sum1i++; 
    sumVadc = sumVadc + Vadc;
    if (sum1i>=1000) {
      VDOffset = sumVadc/sum1i; 
      sum1i = 0; 
      sumVadc=0.0;
    }

    //Root-mean-square method current
    //1) square current values
    sqI = Imains*Imains;
    //2) sum 
    sumI=sumI+sqI;
  }
  Irms = factorA*sqrt(sumI/NUMADCSAMPLES)+theCurrentOffset;
  sumI = 0.0;
  return Irms;  
}


// pulls a message from a web page
char* GetAMessage(char theType)
{
  if (!WEBMSG){ // Pull message from web is off

    if (theType == 'w'){ // washer is done
      return "Wash is done.";
    }
    else { // dryer is done
      return "Dryer is done.";
    }

  }

  if (WEBMSG){ // Pull message from web

    if (theType == 'w'){ // washer is done
      //return "Wash is done.";
      String myTempString = strFromMySQL("washer");
      char myTempChar [myTempString.length()];
       myTempString.toCharArray(myTempChar, myTempString.length());
       return myTempChar;

    }
    else { // dryer is done
      //return "Dryer is done.";
      String myTempString = strFromMySQL("dryer");
      char myTempChar [myTempString.length()];
       myTempString.toCharArray(myTempChar, myTempString.length());
       return myTempChar;
    }
  }
}// end getAMessage

// strFromMySQL
// Function to pull a random string from from mySQL for quote
// Requires setting up mySQL db and hosting the required pages
// See http://www.little.org/blog/?p=2229
String strFromMySQL(String theDevice)
{
  String mySqlString = "";
  if (client.connect(stringServer,80)) {
    //TODO: should move string path to general settings
    // Set the path to the location of your washer/dryer db strings
    client.println("GET /YOUR_PATH_HERE/"+ theDevice+".php HTTP/1.1");  // dryer quote page
    client.print("Host: ");
    client.print(stringServer);
    client.print("\n");
    client.println("Connection: close");
    client.println();

  } 
  else {
    Serial.println(" connection failed");
  } 
  while (client.connected()) {
    if (client.available()){
      if (finder.find("<") ){
        char c;
        for (int i=0; i<200; i++){ 
          c = client.read();
          // did we find the terminating string?
          if (c == '>'){
            break;
          }        
          mySqlString += c;
        } 
      }
    }
  }

  client.stop();
  client.flush();  
  return mySqlString;
} // end strFromMySQL() //

