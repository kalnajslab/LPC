/*
 *  StratoLPC.cpp
 *  Author:  Alex St. Clair
 *  Created: June 2019
 *  Updated: November 2020 (LEK)
 *  
 *  This file implements an Arduino library (C++ class) that inherits
 *  from the StratoCore class. It serves as both a template and test
 *  class for inheriting from the StratoCore.
 * 
 *  Updated in Novemeber 2020 to work with the LOPC main board Rev D
 *  Updated in Arpil 2024 to work with LOPC Main board Rev F / G
 */

#include "StratoLPC.h"

StratoLPC::StratoLPC()
    : StratoCore(&ZEPHYR_SERIAL, INSTRUMENT),
    OPC(13),
    _rs41(Serial7)
{
}

void StratoLPC::InstrumentSetup()
{   
    OPC.SetUp();  //Setup the board
    OPC.ConfigureChannels(); //Setup the LTC2983 Channels

    /*Figure out haw many High Gain and Low Gain Bins we Have */
    NumberHGBins = sizeof(Set_HGBinBoundaries)/sizeof(Set_HGBinBoundaries[0]) - 1;
    NumberLGBins = sizeof(Set_LGBinBoundaries)/sizeof(Set_LGBinBoundaries[0]) - 1;
    
    /*set the data array to zeros so we can co-add to it */
    memset(BinData,0,sizeof(BinData));
    
    OPCSERIAL.addMemoryForRead(&OPC_serial_RX_buffer, sizeof(OPC_serial_RX_buffer));
    Wire.begin();//Activate  Bus I2C for Mass Flow Meter

}

void StratoLPC::InstrumentLoop()
{
    WatchFlags();
}

// The telecommand handler must return ACK/NAK
bool StratoLPC::TCHandler(Telecommand_t telecommand)
{
    String dbg_msg = "";

    switch (telecommand) {
    case SETLASERTEMP:
        Set_LaserTemp = lpcParam.setLaserTemp; // todo: checking
        break;
    case SETFLUSH:
        Set_FlushingTime = lpcParam.lpc_flush; // todo: checking
        log_nominal("TC: Changing Flushing Time");
        ZephyrLogFine("TC: Changing Flushing Time");
        break;
    case SETWARMUPTIME:
        Set_warmUpTime = lpcParam.warmUpTime; // todo: checking
        log_nominal("TC: Changing WarmUpTime");
        ZephyrLogFine("TC: Changing WarmUpTime");
        break;
    case SETCYCLETIME:
        Set_cycleTime = lpcParam.setCycleTime; // todo: checking
        log_nominal("TC: Changing CycleTime");
        ZephyrLogFine("TC: Changing CycleTime");
        break;
    case SETSAMPLE:
        Set_numberSamples = lpcParam.samples; // todo: checking
        log_nominal("TC: Changing Number of Samples per Cycle");
        ZephyrLogFine("TC: Changing Number of Samples per Cycle");
        break;
    case SETSAMPLEAVG:
        Set_samplesToAverage = lpcParam.samplesToAverage; // todo: checking
        log_nominal("TC: Changing Samples to Average");
        ZephyrLogFine("TC: Changing Samples to Average");
        break;
    case SETHGBINS:
        // todo: reader gets 24 values, class expects 16
        log_error("HG bins unimplemented");
        break;
    case SETLGBINS:
        // todo: reader gets 24 values, class expects 16
        log_error("LG bins unimplemented");
        break;
    default:
        ZephyrLogWarn("Unknown TC received");
        break;
    }
    return true;
}

void StratoLPC::ActionHandler(uint8_t action)
{
    // for safety, ensure index doesn't exceed array size
    if (action >= NUM_ACTIONS) {
        log_error("Out of bounds action flag access");
        return;
    }

    // set the flag and reset the stale count
    action_flags[action].flag_value = true;
    action_flags[action].stale_count = 0;
}

bool StratoLPC::CheckAction(uint8_t action)
{
    // for safety, ensure index doesn't exceed array size
    if (action >= NUM_ACTIONS) {
        log_error("Out of bounds action flag access");
        return false;
    }

    // check and clear the flag if it is set, return the value
    if (action_flags[action].flag_value) {
        action_flags[action].flag_value = false;
        action_flags[action].stale_count = 0;
        return true;
    } else {
        return false;
    }
}

void StratoLPC::WatchFlags()
{
    // monitor for and clear stale flags
    for (int i = 0; i < NUM_ACTIONS; i++) {
        if (action_flags[i].flag_value) {
            action_flags[i].stale_count++;
            if (action_flags[i].stale_count >= FLAG_STALE) {
                action_flags[i].flag_value = false;
                action_flags[i].stale_count = 0;
            }
        }
    }
}

void StratoLPC::LPC_Shutdown()
{
    // Make sure everything is off while we wait for a mode
    analogWrite(PUMP1_PWR, 0); //turn off pump1
    analogWrite(PUMP2_PWR, 0); //turn off pump2

    /*disable Serial port to stop backdriving PHA*/
    pinMode(0,INPUT);
    pinMode(1,INPUT);
    digitalWrite(1, LOW);
    
    digitalWrite(MFS_POWER, LOW); //Turn off AFS
    digitalWrite(PHA_POWER, LOW); //Turn off Optical Head
    digitalWrite(HEATER1, LOW); //Turn off Laser Heater
    digitalWrite(HEATER2, LOW); //Turn of unused heater
}

TimeElements StratoLPC::Get_Next_Hour()
{
    /* Returns a Time Elements struct of the next whole hour to sync up measurements
     with the clock */
    
    int32_t current_time;
    TimeElements temp_time;
    
    current_time = now();
    breakTime(current_time, temp_time);
    
    if(temp_time.Hour < 23)
        temp_time.Hour += 1 ;  //advance the hour buy one /* Set this back to 1 */
    else
    {
        temp_time.Hour = 0;  //unless it is the end of the day
        temp_time.Day += 1; //then we add one to the day and zero hours
    }
    
    temp_time.Minute = 0; //zero minute /* Set this back to zero!*/
    temp_time.Second = 0; //zero seconds
    
    //StartSeconds = makeTime(temp_time);
    
    
    return temp_time;
}

/*
int32_t StratoLPC::Next_Start_Time(int32_t LastStartTime)
{
    
    
    Serial.print("Next Start Time: ");
    Serial.println(new_start_time);
}
*/

void StratoLPC::ReadHK(int record)
{
    /*
     * Read the analog data from Teensy channels and convert to real units
     * and read temperatures from LTC2983 part.  Put these values into an array to telemeter.
     */
 
 long I1_Bits = 0;
 long I2_Bits = 0;
 long ID_Bits = 0;
 int i = 0;  
 int StartTime = millis();
 
 while (millis() - StartTime < 10) //Average pump current for 10ms to make sure we get a full pwm cycle
 {
    I1_Bits += analogRead(I_PUMP1);
    I2_Bits += analogRead(I_PUMP2);
    ID_Bits += analogRead(PHA_I);
    i++;
 }
 
  IPump1 = (I1_Bits/i)/4095.0 * 30000; // Pump Current in mA
  IPump2 = (I2_Bits/i)/4095.0 * 30000; // Pump Current in mA
  IDetector = (ID_Bits/i)/1.058; //Detector Current in mA


    HKData[0][record] = (uint16_t) (now() - MeasurementStartTime); //save the elapsed time since we started.
    HKData[1][record] = (uint16_t) IPump1; //Current in mA
    HKData[2][record] = (uint16_t) IPump2; //Current in mA
    IHeater1 = analogRead(HEATER1_I)/1.058;
    HKData[3][record] = (uint16_t) IHeater1;  //Current in mA 
    HKData[4][record] = (uint16_t) IDetector;  //Current in mA
    VDetector = analogRead(PHA_12V_V)*3.3/4095.0*5.993;
    HKData[5][record] = (uint16_t) (VDetector * 1000.0);  //Volts in mV
    VPHA = analogRead(PHA_3V3_V)*3.3/4095.0*2.0;
    HKData[6][record] = (uint16_t) (VPHA * 1000.0); //Volts in mV
    VTeensy = analogRead(TEENSY_3V3)*3.3/4095.0*2.0;
    HKData[7][record] = (uint16_t) (VTeensy * 1000.0); //volte in mV
    VBat = analogRead(BATTERY_V)*3.3/4095.0 *6.772;
    HKData[8][record] = (uint16_t) (VBat * 1000.0);
    Flow = getFlow(); //get the flow in LPM
    HKData[9][record] = (uint16_t)(Flow * 1000); //Flow in ccm
//    VMotors = analogRead(MOTOR_V_MON)*3.0/4095.0*5.993;
//    HKData[10][record] = (uint16_t) (VMotors * 1000.0);
    
    /* Reads temperatures from the LTC part*/
    
    TempPump1 = OPC.MeasureLTC2983(4);
    HKData[11][record] = (uint16_t) (TempPump1 + 273.15) * 100.0; //Kelvin * 100
    TempPump2 = OPC.MeasureLTC2983(6);
    HKData[12][record] = (uint16_t) (TempPump2 + 273.15) * 100.0; //Kelvin * 100
    TempLaser = OPC.MeasureLTC2983(8);
    HKData[13][record] = (uint16_t) (TempLaser + 273.15) * 100.0; //Kelvin * 100
 //   TempDCDC = OPC.MeasureLTC2983(12);
 //   HKData[14][record] = (uint16_t) (TempDCDC + 273.15) * 100.0; //Kelvin * 100
    TempInlet = OPC.MeasureLTC2983(14);
    HKData[15][record] = (uint16_t) (TempInlet + 273.15) * 100.0; //Kelvin * 100
    
}

void StratoLPC::CheckTemps(void)
{
    /*
     * Verify temperatures are within limits, shutdown if we exceed limits
     */
    
    /* Pump 1 */
    if (TempPump1 > T_PUMP_SHUTDOWN) // If over maximum temperature shutdown
    {
        digitalWrite(PUMP1_PWR, LOW);
        //inst_substate = FL_ERROR;
    }
    
    /* Pump 2 */
    if (TempPump2 > T_PUMP_SHUTDOWN) // If over maximum temperature shutdown
    {
        digitalWrite(PUMP2_PWR, LOW);
        //inst_substate = FL_ERROR;
    }
    
    
}

void StratoLPC::AdjustPumps()
{
  int i = 0;
  analogWrite(PUMP1_PWR, 0); //Turn off Pump1
  delayMicroseconds(500); //Hold off for spike to collapse
  
  for(i = 0; i < 32 ; i++)
    backEMF1 += analogRead(PUMP1_BEMF); 

  BEMF1_V = VBat - backEMF1/(4095.0*32.0)*18.0;
  error1 = BEMF1_V - BEMF1_SP;
  BEMF1_pwm = int(BEMF1_pwm - error1*Kp);
  analogWrite(PUMP1_PWR, BEMF1_pwm);
  delay(10);
  
  analogWrite(PUMP2_PWR, 0); //Turn off Pump1
  delayMicroseconds(500); //Hold off for spike to collapse
  
  for(i = 0; i < 32 ; i++)
    backEMF2 += analogRead(PUMP2_BEMF); 

  BEMF2_V = VBat - backEMF2/(4095.0*32.0)*18.0;
  error2 = BEMF2_V - BEMF2_SP;
  BEMF2_pwm = int(BEMF2_pwm - error2*Kp);
  analogWrite(PUMP2_PWR, BEMF2_pwm);

  backEMF1 = 0;
  backEMF2 = 0;
}

float StratoLPC::getFlow()
{
    /*
     * Read the Mass flow meter and return calibrated values (standard liters per minute)
     */
    byte aa,bb; //Define bytes used when requesting data from sensor
    int digital_output;
    float flow;
    
    Wire.requestFrom(sensor,2); //Request data from sensor, 2 bytes at a time
    aa = Wire.read(); //Get first byte
    bb = Wire.read(); //Get second byte
    digital_output=aa<<8;
    digital_output=digital_output+bb; //Combine bytes into an integer
    flow = 20.0*((digital_output/16383.0)-0.1)/0.8*2.66; //Convert digital output into correct air flow
    
    //Return correct value
    return flow;
}

int StratoLPC::parsePHA(int charsToParse) {
    /*This Function parses the char string from the PHA into two integer arrays containing
     * the PHA HG and LG arrays.  We can then down sample to 'bins'.
     */
    
    char * strtokIndx; // this is used by strtok() as an index
    int i;
    
    memset(LGArray, -999, sizeof(LGArray)); //initialize int arays to -999 so we
    memset(HGArray, -999, sizeof(HGArray)); //know if there are missing values.
    
    DEBUG_SERIAL.print("PHA Array as passed to parsePHA: ");
    DEBUG_SERIAL.println(PHAArray);
    
    strtokIndx = strtok(PHAArray,",");      // get the first part - the timestamp
    PHA_TimeStamp = atoi(strtokIndx);
    strtokIndx = strtok(NULL, ",");
    PHA_LaserI = atof(strtokIndx);
    strtokIndx = strtok(NULL, ",");
    PHA_Threshold = atoi(strtokIndx);
    strtokIndx = strtok(NULL, ",");
    PHA_PulseCount = atol(strtokIndx);
    
   
    //Get the next 255 fields as HG array
    for(i = 255; i > 0; i--)
    {
        strtokIndx = strtok(NULL, ","); // this continues where the previous call left off
        if(strtokIndx != NULL)         //if we find a filed
        {
            HGArray[i] = atoi(strtokIndx);     // convert this part to an integer
            //StringBins += HGArray[i];  // Add value to string to write to SD Card
            //StringBins += ',';
            //Serial.print(HGArray[i]);
            //Serial.print(",");
        }
        else
            return -1;
    }
    DEBUG_SERIAL.println();
    //Get the next 255 fields as LG array
    for(i = 255; i > 0; i--)
    {
        strtokIndx = strtok(NULL, ","); // this continues where the previous call left off
        if(strtokIndx != NULL)          //if we find a filed
        {
            LGArray[i] = atoi(strtokIndx);     // convert this part to an integer
            //StringBins += LGArray[i];  // Add value to string to write to SD Card
            //StringBins += ',';
            DEBUG_SERIAL.print(LGArray[i]);
            DEBUG_SERIAL.print(",");
        }
        else
            return -1;
    }
    DEBUG_SERIAL.println();
    return 1;
    
}

void StratoLPC::fillBins(int record, int SamplesToCoAdd)
{
    int m = 0;
    int n = 0;
    
    // DEBUG_SERIAL.print("High Gain Bins: ");
    for(m = 0; m < NumberHGBins; m++)
    {
        HGBins[m] = 0;
        for(n = Set_HGBinBoundaries[m] ; n < Set_HGBinBoundaries[m+1]; n++)
        {
            HGBins[m] += HGArray[n];
        }
        //DEBUG_SERIAL.print(HGBins[m]), DEBUG_SERIAL.print(", ");
    }
    //DEBUG_SERIAL.println();
    //DEBUG_SERIAL.print("Low Gain Bins: ");
    
    for(m = 0; m < NumberLGBins; m++)
    {
        LGBins[m] = 0;
        for(n = Set_LGBinBoundaries[m] ; n < Set_LGBinBoundaries[m+1]; n++)
        {
            LGBins[m] += LGArray[n];
        }
        //DEBUG_SERIAL.print(LGBins[m]), DEBUG_SERIAL.print(", ");
    }
    //DEBUG_SERIAL.println();
    
    for(m = 0; m < 16; m++)
    {
        BinData[m][record/SamplesToCoAdd] += (uint16_t) HGBins[m];
        BinData[m+16][record/SamplesToCoAdd] += (uint16_t) LGBins[m];
    }
}

void StratoLPC::PackageTelemetry(int Records)
{
    int m = 0;
    int n = 0;
    int i = 0;
    String Message = "";
    bool flag1 = true;
    bool flag2 = true;
    
    /* Check the values for the TM message header */
    if ((TempPump1 > 60.0) || (TempPump1 < -30.0))
        flag1 = false;
    if ((TempPump2 > 60.0) || (TempPump2 < -30.0))
        flag1 = false;
    if ((TempLaser > 50.0) || (TempLaser < -30.0))
        flag1 = false;
    
    
    /*Check Voltages are in range */
    if ((VBat > 18.0) || (VBat < 14.0))
        flag2 = false;
   
    
    // First Field
    if (flag1) {
        zephyrTX.setStateFlagValue(1, FINE);
    } else {
        zephyrTX.setStateFlagValue(1, WARN);
    }
    
    Message.concat(TempPump1);
    Message.concat(',');
    Message.concat(TempPump2);
    Message.concat(',');
    Message.concat(TempLaser);
    zephyrTX.setStateDetails(1, Message);
    Message = "";
    
    // Second Field
    if (flag2) {
        zephyrTX.setStateFlagValue(2, FINE);
    } else {
        zephyrTX.setStateFlagValue(2, WARN);
    }
    
    Message.concat(VBat);
    Message.concat(',');
    Message.concat(VTeensy);
    zephyrTX.setStateDetails(2, Message);
    Message = "";


    /* Build the telemetry binary array */
    
    /* Add the initial timestamp */
    zephyrTX.addTm(MeasurementStartTime);
    
    
    for(n = 0; n < NumberHKChannels; n++) //add all the initial HK values
    {
        zephyrTX.addTm(HKData[n][0]);
        HKData[n][m] = 0;
        i++;
    }
    
    for (m = 0; m < Records; m++)
    {
        for( n = 0; n < (NumberLGBins + NumberHGBins); n++)
        {
            zephyrTX.addTm(BinData[n][m]);
            BinData[n][m] = 0;
            i++;
        
        }
        for(n = 0; n < NumberHKChannels; n++)
        {
            zephyrTX.addTm(HKData[n][m]);
            HKData[n][m] = 0;
            i++;
        }
    }

   
    
    //zephyrTX.addTm((uint16_t) 0xFFFF);

    Serial.print("Sending Records: ");
    Serial.println(m);
    Serial.print("Sending Bytes: ");
    Serial.println(i);
    
    
    /* send the TM packet to the OBC */
    zephyrTX.TM();
    
    //memset(BinData,0,sizeof(BinData)); //After we send a TM packet, zero out the arrays.
    //memset(HKData,0,sizeof(BinData)); //After we send a TM packet, zero out the arrays.
}

void StratoLPC::SendRS41Telemetry(uint32_t rs41_start_time, RS41Sample_t* rs41_sample_array, int n_samples)
{
    // Calculate the size of a transmitted data frame
    int sample_bytes = 
    sizeof(rs41_sample_array[0].valid) + 
    sizeof(rs41_sample_array[0].frame) + 
    sizeof(rs41_sample_array[0].tdry) + 
    sizeof(rs41_sample_array[0].humidity) + 
    sizeof(rs41_sample_array[0].pres) + 
    sizeof(rs41_sample_array[0].error); 

    String Message = "";

    // First Field
    Message = "";
    bool flag1 = true;
    for (int i = 0; i < n_samples; i++) {
        flag1 = flag1 & !rs41_sample_array[i].error;
    }
    if (flag1) {
        zephyrTX.setStateFlagValue(1, FINE);
    } else {
        zephyrTX.setStateFlagValue(1, WARN);
    } 
    zephyrTX.setStateDetails(1, Message);

    // Second Field
    Message = "";
    bool flag2 = true;
    for (int i = 0; i < n_samples; i++) {
        flag1 = flag1 & rs41_sample_array[i].valid;
    }
    if (flag2) {
        zephyrTX.setStateFlagValue(2, FINE);
    } else {
        zephyrTX.setStateFlagValue(2, WARN);
    }
    zephyrTX.setStateDetails(2, Message);

    // Build the telemetry binary data array
    
    // Add the initial timestamp
    zephyrTX.addTm(rs41_start_time);

    // And the number of samples
    zephyrTX.addTm(uint16_t(n_samples));
    
    // Add the samples
    for (int i = 0; i < n_samples; i++)
    {
            zephyrTX.addTm(rs41_sample_array[i].valid);
            zephyrTX.addTm(rs41_sample_array[i].frame);
            zephyrTX.addTm(rs41_sample_array[i].tdry);
            zephyrTX.addTm(rs41_sample_array[i].humidity);
            zephyrTX.addTm(rs41_sample_array[i].pres);
            zephyrTX.addTm(rs41_sample_array[i].error);
    }

    //zephyrTX.addTm((uint16_t) 0xFFFF);

    Serial.print("Sending RS41 samples: ");
    Serial.println(n_samples);
    Serial.print("Sending Bytes: ");
    Serial.println(n_samples*(sample_bytes));

    /* send the TM packet to the OBC */
    zephyrTX.TM();
    
    //memset(BinData,0,sizeof(BinData)); //After we send a TM packet, zero out the arrays.
    //memset(HKData,0,sizeof(BinData)); //After we send a TM packet, zero out the arrays.
}

