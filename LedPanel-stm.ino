/*
LED Display with STM32
Controlling LED Panel via MQTT

Using standdart DMD library and custom modified ELClient library (for getting device ID)

MQTT topic: /STMpanel1/command
On start panel show only current time (mode 0) and wait for MQTT data
    data need to be in format: "mode#messege",
                                where mode - int 1-3
                                messege - string 200 char maximum

Modes:  0 - default mode, show screen current time, 2 rows, screen odc.org.ua, tel. number 7-45-63
        1 - display "Doctor say" custom messege, static or dynemic, depends on string width
        3 - display additional screen with custom message
        10 - change brightness 0-255 (in practice 70-200)

After arduinop get mode 1-3, it began to change screens сyclically/

Develop: Mozok Evgen

*/
#include <itoa.h>
#include <ELClient.h>
#include <ELClientCmd.h>
#include <ELClientMqtt.h>

#include <SPI.h> //SPI.h must be included as DMD is written by SPI (the IDE complains otherwise)
#include <DMDSTM.h>
// #include <TimerOne.h>            //
#include "UkrRusSystemFont5x7.h" //small Font
#include "UkrRusArial14.h"       //big font

//Fire up the DMD library as dmd
SPIClass SPI_2(2);
#define DISPLAYS_ACROSS 3
#define DISPLAYS_DOWN 1
DMD dmd(DISPLAYS_ACROSS, DISPLAYS_DOWN);

byte mode = 0;                                 //mode to show
byte lastMode = mode;                          //last mode shown
byte screen = 0;                               //current screen
const char time1[] = {"Поточний час\0"};       //1st row for time screen
char time2[] = {"00:00\0"};                    //current time from getTime
const char screenSite[] = "www.YourSite.ua\0"; //screen with site url
// char screenTel[] = "(05449)7-45-63";    //screen with tel number
long timerScreenChange = 0; // timer for screen controll
long timerScroll = 0;       //timer for scrolling string
long timerGetTime = 0;      //timer for getTime update
char strToShow1[200];       //string to save custom msg
char strToShow2[200];       //string to save custom msg
bool flagScroll = false;    //scroll msg, if it`s too long
uint16_t lngth;             //Length of custom msg

const byte tree[] = {0x00, 0x00, 0x00, 0x80, 0xe0, 0xb8, 0xf6, 0xfd, 0xee, 0xb8, 0xe0, 0x80, 0x00, 0x00, 0x00, 0x30, 0x78, 0xfe, 0xed, 0xff, 0xdf, 0xff, 0xfe, 0xf7, 0xbf, 0xfd, 0xff, 0xde, 0x78, 0x30};
const byte snowMan[] = {0x40, 0x60, 0x80, 0x80, 0x00, 0x1c, 0x22, 0xc9, 0xcd, 0xc9, 0xc5, 0xc1, 0x22, 0x1c, 0x00, 0x80, 0x80, 0x60, 0x40, 0x00, 0x00, 0x00, 0x00, 0x1d, 0x22, 0x41, 0x80, 0x80, 0x80, 0x83, 0x80, 0x41, 0x22, 0x1d, 0x00, 0x00, 0x00, 0x00};

/*--------------------------------------------------------------------------------------
  Interrupt handler for Timer1 (TimerOne) driven DMD refresh scanning, this gets
  called at the period set in Timer1.initialize();
--------------------------------------------------------------------------------------*/
void ScanDMD()
{
    dmd.scanDisplayBySPI(SPI_2);
}

#pragma region ESPinit
// Initialize a connection to esp-link using the normal hardware Serial2 port both for
// SLIP and for debug messages.
ELClient esp(&Serial2, &Serial2);

// Initialize CMD client (for GetTime)
ELClientCmd cmd(&esp);

// Initialize the MQTT client
ELClientMqtt mqtt(&esp);

// Callback made from esp-link to notify of wifi status changes
// Here we just print something out for grins
void wifiCb(void *response)
{
    ELClientResponse *res = (ELClientResponse *)response;
    if (res->argc() == 1)
    {
        uint8_t status;
        res->popArg(&status, 1);

        if (status == STATION_GOT_IP)
        {
            Serial2.println("WIFI CONNECTED");
        }
        else
        {
            Serial2.print("WIFI NOT READY: ");
            Serial2.println(status);
        }
    }
}

bool connected;

char panel[] = {"/STMpanel1/command\0"}; //topic to subscribe, chage in setup() according to devID

// Callback when MQTT is connected
void mqttConnected(void *response)
{
    Serial2.println("MQTT connected!");
    mqtt.subscribe(panel);

    connected = true;
}

// Callback when MQTT is disconnected
void mqttDisconnected(void *response)
{
    Serial2.println("MQTT disconnected");
    connected = false;
}

//Callback when an MQTT message arrives for one of our subscriptions
void mqttData(void *response)
{
    //dmd.end();
    ELClientResponse *res = (ELClientResponse *)response;

    Serial2.print("Received: topic=");
    // String topic = res->popString();
    char topic[res->argLen() + 1];
    res->popChar(topic);
    Serial2.println(topic);

    Serial2.print("data=");
    char data[res->argLen() + 1];
    res->popChar(data);
    Serial2.println(data);

    modeSwitch(data);

    // dmd.selectFont(UkrRusArial_14);
    // uint8_t textSize = dmd.stringWidth(data);
    // Serial2.println(dmd.stringWidth(data, strlen(data)));

    // modeSwitch(data);

    // dmd.begin();
    // esp.DMDFlag = false;
    // flagBug = false;
    // screenControl();
    // timerScreenChange = millis();
}

void mqttPublished(void *response)
{
    Serial2.println("MQTT published");
}

// Callback made form esp-link to notify that it has just come out of a reset. This means we
// need to initialize it!
void resetCb(void)
{
    //Serial2.println("EL-Client (re-)starting!");
    bool ok = false;
    do
    {
        ok = esp.Sync(); // sync up with esp-link, blocks for up to 2 seconds
        if (!ok)
            Serial2.println("EL-Client sync failed!");
    } while (!ok);
    Serial2.println("EL-Client synced!");
}
#pragma endregion

void setup()
{
    Serial2.begin(9600);

    SPI_2.begin();                          //Initialize the SPI_2 port.
    SPI_2.setBitOrder(MSBFIRST);            // Set the SPI_2 bit order
    SPI_2.setDataMode(SPI_MODE0);           //Set the  SPI_2 data mode 0
    SPI_2.setClockDivider(SPI_CLOCK_DIV64); // Use a different speed to SPI 1
    pinMode(SPI2_NSS_PIN, OUTPUT);

    dmd.clearScreen(true); //true is normal (all pixels off), false is negative (all pixels on)
    // Sync-up with esp-link, this is required at the start of any sketch and initializes the
    // callbacks to the wifi status change callback. The callback gets called with the initial
    // status right after Sync() below completes.
    esp.wifiCb.attach(wifiCb); // wifi status change callback, optional (delete if not desired)
    esp.resetCb = resetCb;
    resetCb();

    // Set-up callbacks for events and initialize with es-link.
    mqtt.connectedCb.attach(mqttConnected);
    mqtt.disconnectedCb.attach(mqttDisconnected);
    mqtt.publishedCb.attach(mqttPublished);
    mqtt.dataCb.attach(mqttData);
    mqtt.setup();
    /*
    //  Led Panel setup
    //initialize TimerOne's interrupt/CPU usage used to scan and refresh the display
    Timer1.initialize(5000);         //period in microseconds to call ScanDMD. Anything longer than 5000 (5ms) and you can see flicker.
    Timer1.attachInterrupt(ScanDMD); //attach the Timer1 interrupt to ScanDMD which goes to dmd.scanDisplayBySPI()

    //clear/init the DMD pixels held in RAM
    dmd.clearScreen(true); //true is normal (all pixels off), false is negative (all pixels on)
    drawTest();*/
}

bool initialStart = true;
bool isScreenCleared = true;

void loop()
{
    esp.Process();

    if (connected && initialStart)
    {
        delay(200);
        initialStart = false;

        //  Led Panel setup
        Timer3.setMode(TIMER_CH1, TIMER_OUTPUTCOMPARE);
        Timer3.setPeriod(3000);          // in microseconds
        Timer3.setCompare(TIMER_CH1, 1); // overflow might be small
        Timer3.attachInterrupt(TIMER_CH1, ScanDMD);

        //clear/init the DMD pixels held in RAM
        dmd.clearScreen(true); //true is normal (all pixels off), false is negative (all pixels on)
        dmd.selectFont(UkrRusArial_14);
        dmd.setBrightness(5000);
        ESPGetTime();

        timerGetTime = millis();
        timerScreenChange = millis();
    }

    if (connected)
    {
        unsigned long currentMillis = millis();

        //change screen every ScreenChangeTime sec (6 s default)
        if (mode == 0)
        {
            if (currentMillis - timerScreenChange > 6000)
            {
                //Serial2.println("Screen gona change");   //DEBUG

                screenControl();

                timerScreenChange = currentMillis;
            }
        }
        else
        {
            if (flagScroll && ((timerScroll + 50) < currentMillis))
            {
                dmd.stepMarquee(-1, 0);
                timerScroll = currentMillis;
            }
        }

        isScreenCleared = false;
    }
    if (!connected && !isScreenCleared)
    {
        isScreenCleared = true;
        dmd.clearScreen(true);
    }
}

//parse MQTT data for mode switch and string to show
void modeSwitch(char *dataRes)
{
    lastMode = mode;

    char *pch;
    pch = strtok(dataRes, "#");
    mode = atoi(pch);

    switch (mode)
    {
    case 0: //show time only
    {
        //ESPGetTime();
        flagScroll = false;
        screen = 0;
        break;
    }
    case 1: //show msg
    {
        pch = strtok(NULL, "#");
        screen = 3;
        flagScroll = false;

        // strcpy(strToShow1, "");

        strChange(pch, strToShow1);

        // Serial2.println("modeSwitch");
        // Serial2.print("Data: ");
        // Serial2.println(strToShow1);
        // Serial2.print("Length: ");
        // Serial2.println(strlen(strToShow1));
        // dmd.selectFont(UkrRusArial_14);
        // Serial2.println(dmd.stringWidth(strToShow1, strlen(strToShow1)));
        dmd.selectFont(UkrRusArial_14);
        lngth = dmd.stringWidth(strToShow1, strlen(strToShow1));
        if (lngth > 96)
        {
            flagScroll = true;
            // mode = 2;
            screen = 4;
        }

        screenControl();

        break;
    }
    case 10: //brightness
    {
        pch = strtok(NULL, "#");
        uint16_t br = atoi(pch);
        dmd.setBrightness(br);

        mode = lastMode;
    }
    }

    // timerScreenChange = millis();
}

//Controlling what screens to show
void screenControl()
{
    switch (screen)
    {
    case 0:
    {
        flagScroll = false;
        ESPGetTime();

        screen = mode;

        break;
    }
    case 1:
    {
        flagScroll = false;
        dmd.clearScreen(true);
        dmd.selectFont(UkrRusArial_14);
        dmd.drawString(0, 1, screenSite, strlen(screenSite), GRAPHICS_NORMAL);

        break;
    }
    case 2:
    {
        flagScroll = false;
        dmd.clearScreen(true);
        dmd.selectFont(UkrRusArial_14);

        dmd.drawImg(1, 0, tree, sizeof(tree) / 2);
        dmd.drawImg(40, 0, snowMan, sizeof(snowMan) / 2);
        dmd.drawImg(79, 0, tree, sizeof(tree) / 2);

        break;
    }
    case 3:
    {
        flagScroll = false;
        dmd.selectFont(UkrRusArial_14);
        dmd.clearScreen(true);
        uint8_t xPos = ((DISPLAYS_ACROSS * DMD_PIXELS_ACROSS) - lngth) / 2;
        dmd.drawString(xPos, 1, strToShow1, strlen(strToShow1), GRAPHICS_NORMAL);

        break;
    }
    case 4:
    {
        dmd.selectFont(UkrRusArial_14);
        dmd.clearScreen(true);
        dmd.drawMarquee(strToShow1, strlen(strToShow1), (32 * DISPLAYS_ACROSS) - 1, 1);
        timerScroll = millis();
        flagScroll = true;

        break;
    }
    }

    if (mode == 0)
    {
        screen++;
        if (screen >= 3)
        {
            screen = 0;
        }
    }
}

// void drawTest()
// {
//     dmd.selectFont(UkrRusArial_14);
//     dmd.drawChar(0, 3, '2', GRAPHICS_NORMAL);
//     dmd.drawChar(7, 3, '3', GRAPHICS_NORMAL);
//     dmd.drawChar(17, 3, '4', GRAPHICS_NORMAL);
//     dmd.drawChar(25, 3, '5', GRAPHICS_NORMAL);
//     dmd.drawChar(15, 3, ':', GRAPHICS_OR); // clock colon overlay on
//     delay(1000);
//     dmd.drawChar(15, 3, ':', GRAPHICS_NOR); // clock colon overlay off
//     delay(1000);
//     dmd.drawChar(15, 3, ':', GRAPHICS_OR); // clock colon overlay on
//     delay(1000);
//     dmd.drawChar(15, 3, ':', GRAPHICS_NOR); // clock colon overlay off
//     delay(1000);
//     dmd.drawChar(15, 3, ':', GRAPHICS_OR); // clock colon overlay on
// }

//call SNTP server for current time
void ESPGetTime()
{
    //dmd.end();

    uint32_t t = cmd.GetTime();
    // Serial2.print("Get Time: ");
    // Serial2.println(t);

    char t2[3];

    itoa(((t % 86400L) / 3600), t2, 10);
    // Serial2.print("Hour: ");   //DEBUG
    // Serial2.println(t2);   //DEBUG
    strcpy(time2, t2); // get the hour (86400 equals secs per day)
    strcat(time2, ":");
    //Serial2.println((t % 3600) / 60); //DEBUG
    //Serial2.println(time2);           //DEBUG
    if (((t % 3600) / 60) < 10)
    {
        Serial2.println("Add 0"); //DEBUG
        // In the first 10 minutes of each hour, we'll want a leading '0'
        strcat(time2, "0");
    }
    //else {Serial2.println("min > 10");}    //DEBUG
    // Serial2.println(time2); //DEBUG
    itoa((t % 3600) / 60, t2, 10);
    // Serial2.print("Min: ");   //DEBUG
    // Serial2.println(t2);   //DEBUG
    strcat(time2, t2); // print the minute (3600 equals secs per minute)

    // Serial2.print("Time: ");   //DEBUG
    // Serial2.println(time2);   //DEBUG

    //dmd.begin();
    //esp.DMDFlag = false;
    //flagBug = false;
    // if mode 0 - show only time
    // if (mode == 0)
    // {
    dmd.selectFont(UkrRusSystemFont5x7);
    dmd.clearScreen(true);

    dmd.drawString(12, 0, time1, /*sizeof(time1) / sizeof(*time1)*/ strlen(time1), GRAPHICS_NORMAL);
    dmd.drawString(34, 8, time2, /*sizeof(time2) / sizeof(*time2)*/ strlen(time2), GRAPHICS_NORMAL);
    // }

    // free(t);
    // free(t2);
}

/*
  parse data for Ukrainian characters
  rus and ukr characters consist of 2 bytes, so we ignore 1st byte that are > then 0xCF
*/
void strChange(char *strToChange, char *strNew)
{
    byte lastChar;
    //char * buffer2 = malloc(strlen(buffer)+1);

    byte n = 0;

    while (*strToChange != '\0')
    {
        if ((byte)*strToChange < 0xCF)
        {
            strNew[n] = *strToChange;
            n++;
            strToChange++;
        }
        else
        {
            lastChar = *strToChange;
            strToChange++;

            switch (lastChar)
            {
            case 0xD0:
            {
                switch ((byte)*strToChange)
                {
                case 0x84: //D084 - Є
                {
                    strNew[n] = 0xC0;
                    n++;
                    strToChange++;
                    break;
                }
                case 0x86: //D086 І
                {
                    strNew[n] = 0xC1;
                    n++;
                    strToChange++;
                    break;
                }
                case 0x87: //D087 Ї
                {
                    strNew[n] = 0xC2;
                    n++;
                    strToChange++;
                    break;
                }
                case 0x81: //D081 Ё
                {
                    strNew[n] = 0x95; //Е
                    n++;
                    strToChange++;
                    break;
                }
                }
                break;
            }
            case 0xD1:
            {
                switch ((byte)*strToChange)
                {
                case 0x94: //D196 є
                {
                    strNew[n] = 0xC3;
                    n++;
                    strToChange++;
                    break;
                }
                case 0x96: //D196 і
                {
                    strNew[n] = 0xC4;
                    n++;
                    strToChange++;
                    break;
                }
                case 0x97: //D197 ї
                {
                    strNew[n] = 0xC5;
                    n++;
                    strToChange++;
                    break;
                }
                case 0x91: //D191 ё
                {
                    strNew[n] = 0xB5; //е
                    n++;
                    strToChange++;
                    break;
                }
                }

                break;
            }
            case 0xD2:
            {
                switch ((byte)*strToChange)
                {
                case 0x90: //D290 Ґ
                {
                    strNew[n] = 0x93; //замінюю на Г
                    n++;
                    strToChange++;
                    break;
                }
                case 0x91: //D291 ґ
                {
                    strNew[n] = 0xB3; //замінюю на г
                    n++;
                    strToChange++;
                    break;
                }
                }
                break;
            }
            }
        }
    }
    strNew[n] = '\0';
    // strcpy(strNew, strNew);

    // Serial2.print("buffer2: ");   //DEBUG
    // Serial2.println(buffer2);

    //return buffer2;
}
