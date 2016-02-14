/*
The MIT License (MIT)

Copyright (c) 2016 vaucan

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/


#include "ThingSpeak.h"
#include "math.h"

#define PUB_DELAY 2e3

// pin config
int LED = D7;                               // LED on D7 on Core/Photon
int CHARGE = D2;                            // Set (digital) pin for charge/discharge
int MEAS = A0;                              // Set (analog) pin for ADC

// cap measurement vars
float V_s, V_d;                             // measured voltages on measurement pin [V]
const unsigned int M = 8;                   // margin for charging, keep large if actual cap value may differ from nominal
float delayCharge;                          // time for charging cap [s]
unsigned int delayCharge_us;                // time for charging cap [us]
const float Rch = 98e3;                     // charge resistance [Ohm]
const float Cnom = 380e-12;                 // nominal capacitance at typical conditions [F]
float RCch_nom = Rch*Cnom;                  // nominal charge time constant [s] (corresponds to time for charge/discharge to 63% of final value)
uint32_t t_start_ti, dt_ti;                 // start time and discharge time [ticks]
float dt;                                   // discharge time [s]
const float V_discharge = 1.5;              // voltage to discharge to [V] 
int THRESH_DISCHARGE = V_discharge*4096/3.3;// threshold for discharge in ADC levels
float C_est;                                // estimated capacitance [F]
int read, read_s, read_d;                   // temp variables for ADC readings
const int N = 300;                          // number of samples in average
int n = 1;                                  // counter for averaging
float C_tmp = 0;                            // temp variable for averaging
float C_mean = 0.0;                         // average cap over N measurements [F]
float hum = 0.0;                            // estimated humidity [%RH]
const float S = 0.6;                        // sensitivity [pF/%RH]
const float C_ref_pF = 389.0;               // reference capacitance [pF] (requires calibration)
const int RH_ref = 75;                      // reference humidity [%RH] corresponding to reference capacitance (requires calibration)
// 2016-02-13: calibration w/ NaCl (75% RH), ~389 pF @ 20.5 deg C

// ThingSpeak vars/objects
unsigned long myChannelNumber = 87649;
const char * myWriteAPIKey = "FZKJ116H0YZ8XI8U";
TCPClient client; 
float frac = -1.2;
int fails = 0, tot = 0, valSet, valsSent;

const unsigned long LOOPDELAY = 5*60*1000;

void setup() {
     // Scale the RGB LED brightness to 50% of max (256)
    RGB.control(true);      // take control of the RGB LED
    RGB.brightness(16);
    RGB.control(false);     // resume normal operation
    
    pinMode(LED, OUTPUT);   // init led
    digitalWrite(LED,LOW);  // off
    
    pinMode(MEAS, INPUT);   // measuring pin to input
    pinMode(CHARGE,INPUT);  // no charge/discharge 
    
    ThingSpeak.begin(client,"api.thingspeak.com", 80);
    
    Serial.begin(9600);
    
    Serial.println("init finished");
    Serial.printlnf("System version: %s", System.version().c_str());
	Serial.print("This is compiled and flashed with Particle CLI and uses symlink to library.\n");
}

void loop() {
    
    
    //Serial.printlnf("Nominal charge time constant: %f s",RCch_nom);
    delayCharge = (float) M*RCch_nom;           // calc time for charging
    //Serial.printlnf("Charge time: %f s",delayCharge);
    delayCharge_us = delayCharge*1e6;           // to us
    //Serial.printlnf("time for charging: %u us", delayCharge_us);
    
    // discharge cap properly
    pinMode(CHARGE, OUTPUT);
    digitalWrite(CHARGE, LOW);
    delayMicroseconds(delayCharge_us);
  
    //Serial.printlnf("V_meas: %d",analogRead(MEAS));
   
    // charge cap
    digitalWrite(CHARGE,HIGH);                         
    delayMicroseconds(delayCharge_us);          // wait sufficient time
    read_s = analogRead(MEAS);                  // measure voltage where discharge starts
  
    // discharge cap
    digitalWrite(LED,HIGH);                     // generate trigger signal
    noInterrupts();                             // turn off interrupts
    t_start_ti = System.ticks();                // mark start of time measurement in CPU ticks
    digitalWrite(CHARGE, LOW);                  // start discharging
    while (analogRead(MEAS) > THRESH_DISCHARGE); // wait until voltage below threshold
    digitalWrite(LED,LOW);                      // trigger low
    dt_ti = System.ticks() - t_start_ti;        // mark end of time measurement and calculate discharge duration in ticks
    pinMode(CHARGE,INPUT);                      // set to hi Z, no charge nor discharge
    //Serial.printlnf("ADC level after discharge: %d",analogRead(MEAS));
    read_d = analogRead(MEAS);                  // measure voltage where discharge stops
    interrupts();                               // turn on interrupts
    
    //Serial.printf("%u,",dt_ti);
    
    //Serial.printlnf("Charged voltage ADC level: %d", read_s);
    V_s = (float) read_s/4096*3.3;                                  // calculate start voltage            
    //Serial.printlnf("Charged voltage: %f V", V_s);
    //Serial.printlnf("THRESH_DISCHARGE: %d",THRESH_DISCHARGE);
    V_d = (float) read_d/4096*3.3;                                  // calculate stop voltage
    //Serial.printlnf("Discharged actual voltage: %f V",V_d);
    dt = (float) dt_ti/System.ticksPerMicrosecond()/1e6;            // convert to s when using ticks
    //Serial.printf("%1.12e,",dt);
    C_est = (double)dt/Rch/log((double)V_s/V_d);                    // calculate capacitance estimate 
    //Serial.printlnf("Capacitance, estimated: %f pF",C_est*1e12);
    //Serial.printf("%f\n",C_est*1e12);
    C_tmp += C_est;                                                 // add up estimate for this iteration
    if (n >= N) {                                                   // N iterations done
        C_tmp = C_tmp/N;                                            // divide to get the average
        C_mean = C_tmp;
        Serial.printlnf("MEAN: %f",C_mean*1e12);
        hum = (float) (C_mean*1e12-C_ref_pF)/S+RH_ref;              // calculate humidity
        Serial.printlnf("humidity: %1.0f",hum);
        C_tmp = 0;                                                  // reinit for next estimate
        n = 1;                                                      // reinit for next estimate
    } else
        n++;
    
   // Then you write the fields that you've set all at once.
    /* Generally, these are HTTP status codes.  Negative values indicate an error generated by the library.
    Possible response codes:
        OK_SUCCESS              200     // OK / Success
        ERR_BADAPIKEY           400     // Incorrect API key (or invalid ThingSpeak server address)
        ERR_BADURL              404     // Incorrect API key (or invalid ThingSpeak server address)
        ERR_OUT_OF_RANGE        -101    // Value is out of range or string is too long (> 255 bytes)
        ERR_INVALID_FIELD_NUM   -201    // Invalid field number specified
        ERR_SETFIELD_NOT_CALLED -210    // setField() was not called before writeFields()
        ERR_CONNECT_FAILED      -301    // Failed to connect to ThingSpeak
        ERR_UNEXPECTED_FAIL     -302    // Unexpected failure during write to ThingSpeak
        ERR_BAD_RESPONSE        -303    // Unable to parse response
        ERR_TIMEOUT             -304    // Timeout waiting for server to respond
        ERR_NOT_INSERTED        -401    // Point was not inserted (most probable cause is the rate limit of once every 15 seconds)
    */

    // turn HTTP status code of 200 if successful.  See getLastReadStatus() for other possible return values.
    // int writeField(unsigned long channelNumber, unsigned int field, float value, const char * writeAPIKey)
    if (n==1) {
        waitUntil(WiFi.ready); // wait until WiFi is ready
        Serial.println("wifi ready");
        
        //waitUntil(Particle.connected);
        //Serial.println("cloud ready");
        
        C_tmp = C_mean*1e12;
        valsSent = ThingSpeak.writeField(myChannelNumber, 1, C_tmp, myWriteAPIKey);	//thingspeak connection
        C_tmp = 0;
        Serial.printlnf("Attempt POST to ThingSpeak. Return code: %d",valsSent);
        
        if (valsSent == OK_SUCCESS) {
            Serial.println("Value successfully sent to thingspeak");
        } else {
            Serial.println("Sending to thingspeak FAILED");
            //valsSent = ThingSpeak.getLastReadStatus();
            Serial.printlnf("Error code: %d",valsSent);
            
            fails++;
        }
        tot++;
        frac = (float) fails/tot;
        Particle.publish("fraction-fails", String(frac), 60, PRIVATE);
        delay(LOOPDELAY);
    }
    //delay(1e3);
    
}
