//Note: For nodes with 2xORP probes Uncomment lines 22, 58, 121, 122, 167, 183, then comment 168, and 184


#include <stdlib.h>
#include <time.h>       /* time_t, struct tm, difftime, time, mktime */
#include <stdlib.h>
#include <string.h>
#include <SdCardLogHandlerRK.h>

using namespace std;


#define PRES_READ_PERIOD 5  //defined in minutes. Take a pressure reading every 5 minutes
#define SENS_READ_PERIOD 5 //defined in minutes. Take a sensor reading every 60 minutes
#define FLUSH_PERIOD 60 // defined in minutes. Flushed water every 60 minutes
#define READ_DELAY 1    //defined in minutes. How long after new water is flushed through until atlas sensors take readings
#define bfc D7
#define pres_pin A5
#define signal_pin D6 
#define pH_address 99
#define ORP_address 98
#define ORPDup_address 97
#define EC_address 100
#define RTD_address 102
// Connect to InfluxDB
#define HOSTNAME "REDACTED"
#define HOSTNUM REDACTED
TCPClient client;                                   //Starts client to connect to the internet (library to make connection to influxdb)

SYSTEM_THREAD(ENABLED);                             //Allows the firmware to run without being connected to the cloud.

SerialLogHandler logHandler;                        //Particle speciic library to use the serial communication

                                     //Initialize the String object 

//declaring and initializing the SD card access
const int SD_CHIP_SELECT = D5;      //
SdFat sd(&SPI1);                    // for an alternative SPI interface

//const int SD_CHIP_SELECT = D5;      //for D5 pin, the SPI interface bellow needs to be adjusted to 1.
//SdFat sd(&SPI1);                    // for an alternative SPI interface

//declaring and initializing the pressure interrupt function
SdCardPrintHandler printToCard(sd, SD_CHIP_SELECT, SPI_FULL_SPEED);
Timer timer(7500, take_pressure_sd);

//int getID(String command);                          //Not sure we need this
int flushTime = 5;                                  //defined in seconds
double pvolt;
double pressure;
int pres_counter = 0;
int flush_counter = 0;
int wq_counter = 0;
String tempData;
String pHData;
String ECData;
String ORPData;
String ORPDupData;
String moistState;
String timestamp;
char computerdata[40];              //we make an 40 byte character array to hold incoming data from a pc/mac/other0.
byte code = 0;                      //used to hold the I2C response code.
char _data[200];                    //we make a 200-byte character array to hold incoming data from any circuit.
byte in_char = 0;

String econd[4] = {"EC", "TDS", "SAL", "SG"};
char *ec;                       //char pointer used in string parsing. As the EC sensor sends all 4 of these readings as 1 string
char *tds;                      //char pointer used in string parsing.
char *sal;                      //char pointer used in string parsing.
char *sg;                       //char pointer used in string parsing.

unsigned long previousMillis_pres = 0; //used for pressure measurments frequency in intervals
unsigned long previousMillis_wq = 0; //used for wq measurments frequency in intervals
unsigned long previousMillis_flush = 0; //used for wq measurments frequency in intervals
unsigned long previousMillis_smartflush = 0; // used for smart flush and keep at least at 4 hr separation
unsigned long minsmartflushseparator = 4*60*60*1000; //minimum hours in milliseconds between smart flush being activated again


bool smart_flush_on=FALSE;    //indicator to keep track of the fixed flushing each day
int maxsmartflushTime = 15; //defined in minutes


int tinkerDigitalRead(String pin);
int tinkerDigitalWrite(String command);
int tinkerAnalogRead(String pin);
int tinkerAnalogWrite(String command);


void setup() {
    
    pinMode(pres_pin, INPUT);         //set pin as an input for the pressure transducer
    pinMode(bfc, OUTPUT);  //commented out because not using beef cake -- well we back here again fool!  //Sets digital pin as output to the relay switch
    pinMode(signal_pin, INPUT);
    Wire.begin();
    timer.start();                      //starts timer funciton for pressure interrupter function
    
    //Particle.function("Get Device ID",getID);             //do not need
    //Particle.variable("Device",DEV_ID);                   //do not need
    Particle.function("Flush Time",getFlushTime);
    Particle.variable("Flush Time",flushTime);
    
    Particle.function("Take Pressure", take_pressure);
    Particle.variable("Pressure Voltage",pvolt);            //not sure we need this
    Particle.variable("Pressure",pressure);
    
    Particle.function("Take Moisture", take_moisture);    
    Particle.variable("Moisture", moistState);
    
    Particle.function("Take Temperature", take_temp);
    Particle.variable("Temperature", tempData);
    
    Particle.function("Take pH", take_pH);
    Particle.variable("pH", pHData);
    
    Particle.function("Take EC", take_EC);
    Particle.variable("EC", ECData);    
    
    Particle.function("Take ORP", take_ORP);
    Particle.variable("ORP", ORPData);
    
    Particle.function("Take ORPdup", take_ORPdup);
    Particle.variable("ORPdup", ORPDupData);
    
    //flushing function for smart flushing
    Particle.function("Smart Flush",smart_flush);


	//Register all the Tinker functions
	Particle.function("digitalread", tinkerDigitalRead);
	Particle.function("digitalwrite", tinkerDigitalWrite);
	Particle.function("analogread", tinkerAnalogRead);
	Particle.function("analogwrite", tinkerAnalogWrite);
}

void loop() {
    
    unsigned long currentMillis = millis();
    
    
    if ((currentMillis - previousMillis_pres >= PRES_READ_PERIOD*60*1000 || pres_counter == 0) && !smart_flush_on) {
    
        previousMillis_pres = currentMillis;     // save the last time pressure taken
        timestamp = String(Time.now())+"000000000";
        take_pressure("");
        take_moisture("");        // Take moisture reading
        pres_counter = 1;
    }
    
    if ((currentMillis - previousMillis_flush >= FLUSH_PERIOD*60*1000 || flush_counter == 0)&& !smart_flush_on) {
    
        previousMillis_flush = currentMillis;     // save the last time pressure taken
        timestamp = String(Time.now())+"000000000";
        water_exchange();                       // flushTime is defined through the particlefunciton and variable
        flush_counter = 1;
        
        //if(ORPData.toFloat() < 400){
        //    smart_flush("HIGH1");
        //}
    }
    
    if ((currentMillis - previousMillis_wq >= SENS_READ_PERIOD*60*1000 || wq_counter == 0)&& !smart_flush_on) {
    
        previousMillis_wq = currentMillis;     // save the last time wq was taken
        timestamp = String(Time.now())+"000000000";
        take_temp("");
        take_ORP("");
        take_ORPdup("");
        //take_pH("");
        take_EC("");
        wq_counter = 1;
    }
    
    
    if (smart_flush_on && (currentMillis - previousMillis_smartflush >= maxsmartflushTime*60*1000)) {
        digitalWrite(bfc,LOW); //turn on beefcake (and therefore valve)
        smart_flush_on = FALSE; //adjust the indicator
    }
    
    if(smart_flush_on){
        timestamp = String(Time.now())+"000000000";
        take_temp("");
        take_ORP("");
        take_ORPdup("");
        //take_pH("");
        take_EC("");
        take_pressure("");
    }
    
}

//function used with a Python code to activate smart flushing based on cloud-analyzed data. Arg is HIGH or LOW depending if want to turn on or off the valve. n is the type of behavior the flush is addressing (sent from the pythin code)
int smart_flush(String position){
    
    bool arg;
    String n;
    //Evaluates first part of the position string that sets the valve on or off
    if(position.startsWith("HIGH")){
        arg = HIGH;
    }else if(position.startsWith("LOW")){
        arg = LOW;
    }else {return -1;}
    
    //evaluates the last character of the string, which represents the type of flush required (1, 2, or 3)
    if(position.endsWith("1")){
        n = "1";
    }else if(position.endsWith("2")){
        n = "2";
    }else {n="3";
    }
    
    int i=0;
    unsigned long currentflushMillis = millis();
    String tiename = "SmartFlush"+n;
    
    if(arg && ((currentflushMillis - previousMillis_smartflush >= minsmartflushseparator) || previousMillis_smartflush==0)){
        smart_flush_on=TRUE;
        previousMillis_smartflush = currentflushMillis;     // save the last time valve was opened taken
        timestamp = String(Time.now())+"000000000";
        createDataStream(tiename, "1");
        i = 1;
        digitalWrite(bfc,arg); //turn on beefcake (and therefore valve)
    }else{
        smart_flush_on=FALSE;
        i=0;
        digitalWrite(bfc,LOW); //turn on beefcake (and therefore valve)
        }
    
    
    
    return i;
}


int take_ORP(String command){
    computerdata[0] = 'r';
    Wire.beginTransmission(ORP_address);     
    Wire.write(computerdata);               
    Wire.endTransmission(); 
    delay(815);
   
        Wire.requestFrom(ORP_address, 48, 1);    //call the circuit and request 48 bytes (this may be more than we need -- from https://www.arduino.cc/en/Reference/WireRead it might only send 1 byte)
        code = Wire.read();             //the first byte is the response code, we read this separately.
        // char P_code = (char)code;    // Erros with type with these lines byte --> const char & char --> const char
        // Particle.publish(P_code);
        switch (code) {                     //switch case based on what the response code is.
            case 1:                         //decimal 1 means the command was successful.
            //Particle.publish("Success");  //commented out to avoid using too much data.
            Serial.println("Success");
            break;                       //exits the switch case.
            
            case 2:                        //decimal 2. means the command has failed.
            //Particle.publish("Failed");
            Serial.println("Failed");
            break;                       //exits the switch case.
            
            case 254:                      //decimal 254 means the command has not yet been finished calculating.
            //Particle.publish("Pending");
            Serial.println("Pending");
            break;                       //exits the switch case.
            
            case 255:                      //decimal 255 means there is no further data to send.
            //Particle.publish("No Data");
            Serial.println("No Data");
            break;                       //exits the switch case.
        }
        
        for (int j=0; Wire.available(); j++) {   //are there bytes to receive.
            in_char = Wire.read();              //receive a byte. (ASCII number)
            _data[j] = in_char;                 //load this byte into our array.

            if (in_char == 0) {                 //if we see that we have been sent a null command.
                Wire.endTransmission();         //end the I2C data transmission.
                break;                          //exit the while loop.
            }
        }
        
        createDataStream("ORP", String(_data));
        ORPData = String(_data);
        

        memset(_data,0, sizeof(_data));

    return 1;
}


int take_ORPdup(String command){
    computerdata[0] = 'r';
    Wire.beginTransmission(ORPDup_address);     
    Wire.write(computerdata);               
    Wire.endTransmission(); 
    delay(815);
   
        Wire.requestFrom(ORPDup_address, 48, 1);    //call the circuit and request 48 bytes (this may be more than we need -- from https://www.arduino.cc/en/Reference/WireRead it might only send 1 byte)
        code = Wire.read();             //the first byte is the response code, we read this separately.
        // char P_code = (char)code;    // Erros with type with these lines byte --> const char & char --> const char
        // Particle.publish(P_code);
        switch (code) {                     //switch case based on what the response code is.
            case 1:                         //decimal 1 means the command was successful.
            //Particle.publish("Success");  //commented out to avoid using too much data.
            Serial.println("Success");
            break;                       //exits the switch case.
            
            case 2:                        //decimal 2. means the command has failed.
            //Particle.publish("Failed");
            Serial.println("Failed");
            break;                       //exits the switch case.
            
            case 254:                      //decimal 254 means the command has not yet been finished calculating.
            //Particle.publish("Pending");
            Serial.println("Pending");
            break;                       //exits the switch case.
            
            case 255:                      //decimal 255 means there is no further data to send.
            //Particle.publish("No Data");
            Serial.println("No Data");
            break;                       //exits the switch case.
        }
        
        for (int j=0; Wire.available(); j++) {   //are there bytes to receive.
            in_char = Wire.read();              //receive a byte. (ASCII number)
            _data[j] = in_char;                 //load this byte into our array.

            if (in_char == 0) {                 //if we see that we have been sent a null command.
                Wire.endTransmission();         //end the I2C data transmission.
                break;                          //exit the while loop.
            }
        }
        
        createDataStream("ORPdup", String(_data));
        ORPDupData = String(_data);
        

        memset(_data,0, sizeof(_data));

    return 1;
}


int take_EC(String command){
    
    computerdata[0] = 't'; //t takes first spot in array
    computerdata[1] = ','; //, takes second spot

    //sets remaining characters in computer data (computerdata+2) to be temperature reading for temperature compensation
    //Since temp is a String, we must convert to c_str for function to work correctly
    strcpy(computerdata+2, tempData.c_str());
    Wire.beginTransmission(EC_address);     
    Wire.write(computerdata);               
    Wire.endTransmission();
    delay(300);
    memset(computerdata,0, sizeof(computerdata));               //now that the cirquit has been temperature compensated, take a reading "r" as usual
    computerdata[0] = 'r';
    Wire.beginTransmission(EC_address);     
    Wire.write(computerdata);               
    Wire.endTransmission();
    
    delay(570);

        Wire.requestFrom(EC_address, 48, 1);    //call the circuit and request 48 bytes (this may be more than we need -- from https://www.arduino.cc/en/Reference/WireRead it might only send 1 byte)
        code = Wire.read();             //the first byte is the response code, we read this separately.
        // char P_code = (char)code;    // Erros with type with these lines byte --> const char & char --> const char
        // Particle.publish(P_code);
        switch (code) {                     //switch case based on what the response code is.
            case 1:                         //decimal 1 means the command was successful.
            //Particle.publish("Success");  //commented out to avoid using too much data.
            Serial.println("Success");
            break;                       //exits the switch case.
            
            case 2:                        //decimal 2. means the command has failed.
            //Particle.publish("Failed");
            Serial.println("Failed");
            break;                       //exits the switch case.
            
            case 254:                      //decimal 254 means the command has not yet been finished calculating.
            //Particle.publish("Pending");
            Serial.println("Pending");
            break;                       //exits the switch case.
            
            case 255:                      //decimal 255 means there is no further data to send.
            //Particle.publish("No Data");
            Serial.println("No Data");
            break;                       //exits the switch case.
        }
        
        for (int j=0; Wire.available(); j++) {   //are there bytes to receive.
            in_char = Wire.read();              //receive a byte. (ASCII number)
            _data[j] = in_char;                 //load this byte into our array.

            if (in_char == 0) {                 //if we see that we have been sent a null command.
                Wire.endTransmission();         //end the I2C data transmission.
                break;                          //exit the while loop.
            }
        }
        
        //break up the EC ASCII by comma to get the four different measurements it acutally takes
        //Serial.println(_data);
        
        ec = strtok(_data, ",");                //let's pars the string at each comma.
        tds = strtok(NULL, ",");                //let's pars the string at each comma.
        sal = strtok(NULL, ",");                //let's pars the string at each comma.
        sg = strtok(NULL, ",");                 //let's pars the string at each comma.
        String edata[4] = {ec, tds, sal, sg};   //creates a string of the ASCII values from the EC sensor
        
        for(int j = 0; j < 4; j++){
            createDataStream(econd[j],edata[j]);
            Serial.println(econd[j]+" "+edata[j]);
        }
                
        ECData = String(_data);


        memset(_data,0, sizeof(_data));
    return 1;
}




int take_pH(String command){
    
    computerdata[0] = 't'; //t takes first spot in array
    computerdata[1] = ','; //, takes second spot

    //sets remaining characters in computer data (computerdata+2) to be temperature reading for temperature compensation
    //Since temp is a String, we must convert to c_str for function to work correctly
    strcpy(computerdata+2, tempData.c_str());
    
    Wire.beginTransmission(pH_address);     
    Wire.write(computerdata);               
    Wire.endTransmission();
    delay(300);
    memset(computerdata,0, sizeof(computerdata));               //Since circuit has been compensated for temperature, now take a measurement as usual with "r"
    computerdata[0] = 'r';
    Wire.beginTransmission(pH_address);     
    Wire.write(computerdata);               
    Wire.endTransmission();
    
    delay(815);

        Wire.requestFrom(pH_address, 48, 1);    //call the circuit and request 48 bytes (this may be more than we need -- from https://www.arduino.cc/en/Reference/WireRead it might only send 1 byte)
        code = Wire.read();             //the first byte is the response code, we read this separately.
        // char P_code = (char)code;    // Erros with type with these lines byte --> const char & char --> const char
        // Particle.publish(P_code);
        switch (code) {                     //switch case based on what the response code is.
            case 1:                         //decimal 1 means the command was successful.
            //Particle.publish("Success");  //commented out to avoid using too much data.
            Serial.println("Success");
            break;                       //exits the switch case.
            
            case 2:                        //decimal 2. means the command has failed.
            //Particle.publish("Failed");
            Serial.println("Failed");
            break;                       //exits the switch case.
            
            case 254:                      //decimal 254 means the command has not yet been finished calculating.
            //Particle.publish("Pending");
            Serial.println("Pending");
            break;                       //exits the switch case.
            
            case 255:                      //decimal 255 means there is no further data to send.
            //Particle.publish("No Data");
            Serial.println("No Data");
            break;                       //exits the switch case.
        }
        
        for (int j=0; Wire.available(); j++) {   //are there bytes to receive.
            in_char = Wire.read();              //receive a byte. (ASCII number)
            _data[j] = in_char;                 //load this byte into our array.

            if (in_char == 0) {                 //if we see that we have been sent a null command.
                Wire.endTransmission();         //end the I2C data transmission.
                break;                          //exit the while loop.
            }
        }
        
        createDataStream("pH", String(_data));
        pHData = String(_data);
        

        memset(_data,0, sizeof(_data));
    return 1;
}

int take_temp(String command){
    computerdata[0] = 'r';              //set the command to read
    
    Wire.beginTransmission(RTD_address);
    Wire.write(computerdata);
    Wire.endTransmission();
    
    delay(600);

            Wire.requestFrom(RTD_address, 48, 1);    //call the circuit and request 48 bytes (this may be more than we need -- from https://www.arduino.cc/en/Reference/WireRead it might only send 1 byte)
            code = Wire.read();             //the first byte is the response code, we read this separately.
            // char P_code = (char)code;    // Erros with type with these lines byte --> const char & char --> const char
            // Particle.publish(P_code);
            switch (code) {                     //switch case based on what the response code is.
            case 1:                         //decimal 1 means the command was successful.
            //Particle.publish("Success");  //commented out to avoid using too much data.
            Serial.println("Success");
            break;                       //exits the switch case.
            
            case 2:                        //decimal 2. means the command has failed.
            //Particle.publish("Failed");
            Serial.println("Failed");
            break;                       //exits the switch case.
            
            case 254:                      //decimal 254 means the command has not yet been finished calculating.
            //Particle.publish("Pending");
            Serial.println("Pending");
            break;                       //exits the switch case.
            
            case 255:                      //decimal 255 means there is no further data to send.
            //Particle.publish("No Data");
            Serial.println("No Data");
            break;                       //exits the switch case.
        }
            
            for (int j=0; Wire.available(); j++) {   //are there bytes to receive.
                in_char = Wire.read();              //receive a byte. (ASCII number)
                _data[j] = in_char;                 //load this byte into our array.

                if (in_char == 0) {                 //if we see that we have been sent a null command.
                    Wire.endTransmission();         //end the I2C data transmission.
                    tempData = _data;
                    break;                          //exit the while loop.
                }
            }
        
        createDataStream("Temp", String(_data));
        tempData = _data;
        

        memset(_data,0, sizeof(_data));
     return 1;
}



int getFlushTime(String tim3){
    flushTime = tim3.toInt();
    return flushTime;
}

void water_exchange(){
    digitalWrite(bfc,HIGH); //turn on beefcake (and therefore valve)
    delay(flushTime*1000);              //valve left open for n seconds
    digitalWrite(bfc,LOW); //turn off beefcake
}

int take_pressure(String command){
    // pvolt = map(analogRead(pres_pin),0,4095,0,3);
    pressure = map(analogRead(pres_pin),0,4095,0,100); //ADC reading to pressure (PSI)
    // pressure = map(pvolt,0,3,0,100);
    if(pressure <0){ pressure=0;} 
    createDataStream("pressure",String(pressure));
    return 1;
}

void take_pressure_sd(){
    
    float pressure_sd = map(analogRead(pres_pin),0,4095,0,100); //ADC reading to pressure (PSI)
    String tie = String(Time.now())+"000000000";
    String sdata; 
    sdata = String("pressure,")+" value="+String(pressure_sd)+" "+tie; //removed siteID and NodeID
    printToCard.println(sdata);
}

int take_moisture(String command){
    // int moisture = digitalRead(signal_pin);
    // Particle.publish(moisture);
    if (digitalRead(signal_pin) == HIGH){
        moistState = "Dry";
    }
    else if (digitalRead(signal_pin) == LOW){
        moistState = "Wet";
    }
    createDataStream("Moisture",moistState);
    return 1;
}

int writeinflux(String data){
//function that writes a measurement given as a string to influx and returns 1 if successful and 0 if failed
// Send data over to influx
    
    String DEV_ID = System.deviceID();                  //obtain the deviceid and store as a String object
    String POSTNAME = "/api/v1/write?d=";               //Initialize the String object 
    POSTNAME.concat(DEV_ID);                            //DEV_ID is appended to POSTNAME and stored as POSTNAME
    POSTNAME.concat("&h=REDACTED HTTP/1.1");                      //POSTNAMEtail is appended to POSTNAME and stored as POSTNAME
    
    if(client.connect(HOSTNAME, HOSTNUM)){
    
        Serial.println("Connected");

        
        // Actual function
        client.printlnf("POST %s", POSTNAME.c_str());
        client.printlnf("HOST: %s:%d", HOSTNAME, HOSTNUM);
        client.println("User-Agent: Photon/1.0");
        client.printlnf("Content-Length: %d", data.length());
        client.println("Content-Type: application/x-www-form-urlencoded");
        client.println();
        client.print(data);
        //client.println();  
        delay(1*1000); //delay 10 seconds. function takes in milliseconds
        //client.flush();
        client.stop();
        
        return 1;
    }
    
    else
    {
        Serial.println("Connection Failed!");
    }
    
    return 0;
}

void createDataStream(String name, String meas){
//function to create the data string to send to influx 
    // Get current time in UNIX time 

    //String timestamp = String(Time.now())+"000000000";
    //get device ID to use as tag
    //String myID = System.deviceID();
    // Create data string for the parameter to pass into write influx
    String data;
    data = name+","+" value="+meas+" "+timestamp; //removed siteID and NodeID

        
    //write to influx and will try for 5 times before just taking a new reading
    for(int j=0; j<5; j++){
        if (writeinflux(data) == 1){
            Serial.println("A success!");

            break;
        }
        else{
            Serial.println("Trying again");

        }
    }
}


/* Tinker
 * This is a simple application to read and toggle pins on a Particle device.
 * For the extended version of the Tinker app supporting more pins, see
 * https://github.com/particle-iot/device-os/blob/develop/user/applications/tinker/application.cpp
 */

/* Function prototypes -------------------------------------------------------*/


/* This function is called once at start up ----------------------------------*/


/*******************************************************************************
 * Function Name  : tinkerDigitalRead
 * Description    : Reads the digital value of a given pin
 * Input          : Pin
 * Output         : None.
 * Return         : Value of the pin (0 or 1) in INT type
                    Returns a negative number on failure
 *******************************************************************************/
int tinkerDigitalRead(String pin)
{
	//convert ascii to integer
	int pinNumber = pin.charAt(1) - '0';
	//Sanity check to see if the pin numbers are within limits
	if (pinNumber< 0 || pinNumber >7) return -1;

	if(pin.startsWith("D"))
	{
		pinMode(pinNumber, INPUT_PULLDOWN);
		return digitalRead(pinNumber);
	}
	else if (pin.startsWith("A"))
	{
		pinMode(pinNumber+10, INPUT_PULLDOWN);
		return digitalRead(pinNumber+10);
	}
	return -2;
}

/*******************************************************************************
 * Function Name  : tinkerDigitalWrite
 * Description    : Sets the specified pin HIGH or LOW
 * Input          : Pin and value
 * Output         : None.
 * Return         : 1 on success and a negative number on failure
 *******************************************************************************/
int tinkerDigitalWrite(String command)
{
	bool value = 0;
	//convert ascii to integer
	int pinNumber = command.charAt(1) - '0';
	//Sanity check to see if the pin numbers are within limits
	if (pinNumber< 0 || pinNumber >7) return -1;

	if(command.substring(3,7) == "HIGH") value = 1;
	else if(command.substring(3,6) == "LOW") value = 0;
	else return -2;

	if(command.startsWith("D"))
	{
		pinMode(pinNumber, OUTPUT);
		digitalWrite(pinNumber, value);
		return 1;
	}
	else if(command.startsWith("A"))
	{
		pinMode(pinNumber+10, OUTPUT);
		digitalWrite(pinNumber+10, value);
		return 1;
	}
	else return -3;
}

/*******************************************************************************
 * Function Name  : tinkerAnalogRead
 * Description    : Reads the analog value of a pin
 * Input          : Pin
 * Output         : None.
 * Return         : Returns the analog value in INT type (0 to 4095)
                    Returns a negative number on failure
 *******************************************************************************/
int tinkerAnalogRead(String pin)
{
	//convert ascii to integer
	int pinNumber = pin.charAt(1) - '0';
	//Sanity check to see if the pin numbers are within limits
	if (pinNumber< 0 || pinNumber >7) return -1;

	if(pin.startsWith("D"))
	{
		return -3;
	}
	else if (pin.startsWith("A"))
	{
		return analogRead(pinNumber+10);
	}
	return -2;
}

/*******************************************************************************
 * Function Name  : tinkerAnalogWrite
 * Description    : Writes an analog value (PWM) to the specified pin
 * Input          : Pin and Value (0 to 255)
 * Output         : None.
 * Return         : 1 on success and a negative number on failure
 *******************************************************************************/
int tinkerAnalogWrite(String command)
{
	//convert ascii to integer
	int pinNumber = command.charAt(1) - '0';
	//Sanity check to see if the pin numbers are within limits
	if (pinNumber< 0 || pinNumber >7) return -1;

	String value = command.substring(3);

	if(command.startsWith("D"))
	{
		pinMode(pinNumber, OUTPUT);
		analogWrite(pinNumber, value.toInt());
		return 1;
	}
	else if(command.startsWith("A"))
	{
		pinMode(pinNumber+10, OUTPUT);
		analogWrite(pinNumber+10, value.toInt());
		return 1;
	}
	else return -2;
}

