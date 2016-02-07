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

#include "math.h"

int LED = D7;                               // LED on D7 on Core/Photon
int CHARGE = D2;                            // Set (digital) pin for charge/discharge
int MEAS = A0;                              // Set (analog) pin for ADC
float V_s, V_d;                             // measured voltages on measurement pin [V]
const unsigned int M = 8;                   // margin for charging, keep large if actual cap value may differ from nominal
float delayCharge;                          // time for charging cap [s]
unsigned int delayCharge_us;                // time for charging cap [us]
const float Rch = 98e3;                     // charge resistance [Ohm]
const float Cnom = 380e-12;                 // nominal capacitance at typical conditions [F]
float RCch_nom = Rch*Cnom;                  // nominal charge time constant [s] (corresponds to time for charge/discharge to 63% of final value)
unsigned long t_start_us, dt_us;            // start time and discharge time [u]s
float dt;                                   // discharge time [s]
const float V_discharge = 1.5;              // voltage to discharge to [V] 
int THRESH_DISCHARGE = V_discharge*4096/3.3;// threshold for discharge in ADC levels
float C_est;                                // estimated capacitance [F]
int read, read_s, read_d;                   // temp variables for ADC readings
const int N = 500;                          // number of samples in average
int n = 1;                                  // counter for averaging
float C_mean = 0;                           // average cap over N measurements [F]
int hum = 0;                                // estimated humidity [%RH]
const float S = 0.6;                        // sensitivity [pF/%RH]
const int C_ref_pF = 338;                   // reference capacitance [pF]
const int RH_ref = 41;                      // reference humidity [%RH] corresponding to reference capacitance

void setup() {
     // Scale the RGB LED brightness to 50% of max (256)
    RGB.control(true);      // take control of the RGB LED
    RGB.brightness(16);
    RGB.control(false);     // resume normal operation
    
    Serial.begin(9600);
    Serial.println("init finished");
    
    pinMode(LED, OUTPUT);   // init led
    digitalWrite(LED,LOW);  // off
    
    pinMode(MEAS, INPUT);   // measuring pin to input
    pinMode(CHARGE,INPUT);  // no charge/discharge 
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
    t_start_us = micros();                      // mark start of time measurement
    digitalWrite(CHARGE, LOW);                  // start discharging
    while (analogRead(MEAS) > THRESH_DISCHARGE); // wait until voltage below threshold
    digitalWrite(LED,LOW);                      // trigger low
    dt_us = micros() - t_start_us;              // mark end of time measurement and calculate discharge duration
    pinMode(CHARGE,INPUT);                      // set to hi Z, no charge nor discharge
    //Serial.printlnf("ADC level after discharge: %d",analogRead(MEAS));
    //Serial.printlnf("time out criteria was: stop: %u <-> threshold: %u",dt_us,t_start_us + 1e3);
    read_d = analogRead(MEAS);                  // measure voltage where discharge stops
    interrupts();                               // turn on interrupts
    
    
    //Serial.printlnf("Charged voltage ADC level: %d", read_s);
    V_s = (float) read_s/4096*3.3;              // calculate start voltage            
    //Serial.printlnf("Charged voltage: %f V", V_s);
    //Serial.printlnf("THRESH_DISCHARGE: %d",THRESH_DISCHARGE);
    V_d = (float) read_d/4096*3.3;              // calculate stop voltage
    //Serial.printlnf("Discharged actual voltage: %f V",V_d);
    dt = (float) dt_us/1e6;                     // convert to s
    //Serial.printlnf("Time taken for discharge: %u us",dt_us);
    C_est = (double)dt/Rch/log((double)V_s/V_d);// calculate capacitance estimate 
    //Serial.printlnf("Capacitance, estimated: %f pF",C_est*1e12);
    //Serial.printlnf("%f",C_est*1e12);
    C_mean += C_est;                            // add up estimate for this iteration
    if (n >= N) {                               // N iterations done
        C_mean = C_mean/N;                      // divide to get the average
        Serial.printlnf("MEAN: %f",C_mean*1e12);
        hum = (C_mean*1e12-C_ref_pF)/S+RH_ref;     // calculate humidity
        Serial.printlnf("humidity: %d",hum);
        C_mean = 0;                             // reinit for next estimate
        n = 1;                                  // reinit for next estimate
    } else
        n++;
    
    //delay(1e3);
    
}
