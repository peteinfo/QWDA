#ifndef FED4_H
#define FED4_H

#include <Arduino.h>
#include <Adafruit_BusIO_Register.h>
#include <Adafruit_I2CDevice.h>
#include <Adafruit_I2CRegister.h>
#include <Adafruit_SPIDevice.h>
#include <Wire.h>
#include <SPI.h>
#include <Stepper.h>
#include <RTClib.h>
#include <RTCZero.h>
#include <SdFat.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SharpMem.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_AHTX0.h>
#include <functional>

#include "Menu.h"

#define NEOPXL_PIN      A1
#define GRN_LED_PIN     8
#define WELL_PIN        1
#define LFT_POKE_PIN    6
#define RGT_POKE_PIN    5
#define BUZZER_PIN      0
#define VBAT_PIN        A7
#define CARD_SEL_PIN    4
#define BNC_OUT_PIN     A0
#define SHRP_SCK_PIN    12
#define SHRP_MOSI_PIN   11
#define SHRP_SS_PIN     10
#define MTR_EN_PIN      13
#define MTR_1_PIN       A2
#define MTR_2_PIN       A3
#define MTR_3_PIN       A4
#define MTR_4_PIN       A5

#define LP_DISPLAY_REFRESH_RATE 30// seconds

#define MODE_VI 0
#define MODE_FR 1
#define MODE_OTHER -1

#define LEFT    0
#define RIGHT   1
#define BOTH    2

#define DISPLAY_H 144
#define DISPLAY_W 168
#define BLACK 0
#define WHITE 1

#define STEPS 2048

#define EVENT_LEFT  "Left Poke"
#define EVENT_RIGHT "Right Poke"
#define EVENT_PEL   "Dropped Pellet"
#define EVENT_WELL  "Well Cleared"
#define EVENT_SET_VI "Set VI"
#define EVENT_RESET "Reset Device"
#define EVENT_NONE  ""

struct Event {
    DateTime time;
    String event;
    ~Event() {};
};

class FED4 {
private:
    bool sleepMode = false;

    bool leftPokeStarted = false;
    bool rightPokeStarted = false;

    bool leftPoke = false;
    bool rightPoke = false;
    bool pelletDropped = false;

    long startLftPoke = 0;
    long startRgtPoke = 0;

    long dtLftPoke = 0;
    long dtRgtPoke = 0;

    long lastStatsUpdate= 0;
    long lastStatusUpdate = 0;

public:
    FED4();
    static FED4* instance;
    int deviceNumber = 0;

    String function;

    int leftPokeCount = 0;
    int rightPokeCount = 0;
    int pelletsDispensed = 0;
    int leftReward = 1;
    int rightReward = 1;
    uint8_t activeSensor = BOTH;

    int ratio = 1;
    int viAvg = 30;
    float spread = 0.75;
    int viCountDown = 0;
    uint32_t feedUnixT = 0;
    bool viSet = false;
    int getViCountDown();

    int8_t mode = MODE_OTHER;
    
    std::function<void()> entryPoint = nullptr;
    void begin();
    void run();
    void reset();

    bool enableSleep = true;
    void sleep();
    
    bool getLeftPoke();
    bool getRightPoke(); 
    bool getWellStatus();
    
    void feed(int pellets = 1, bool wait = true);
    void rotateWheel(int degrees);
    
    SdFat sd;
    String logFileName;
    void initSD();
    void showSdError();
    void initLogFile();
    void logEvent(Event e);
    void deleteLines(int n);

    void displayLayout();
    void updateDisplay(bool timeOnly = false);
    void drawDateTime();
    void drawBateryCharge();
    void drawStats();
    // void showSdError

    void runModeMenu();
    void runViMenu();
    void runFrMenu();
    
    void makeNoise(int duration = 300);
    void print(String str, uint8_t size = 2);

    int getBatteryPercentage();
    DateTime getDateTime();
    
    bool ignorePokes = false;
    void leftPokeHandler();
    void rightPokeHandler();
    void alarmHandler();
    void wellHandler();

    Adafruit_SharpMem display = Adafruit_SharpMem(SHRP_SCK_PIN, SHRP_MOSI_PIN, SHRP_SS_PIN,  DISPLAY_H, DISPLAY_W);
    Stepper stepper = Stepper(STEPS, MTR_1_PIN, MTR_2_PIN, MTR_3_PIN, MTR_4_PIN); 
    RTC_PCF8523 rtc;
    RTCZero rtcZero;

private:
    static void leftPokeIRS();
    static void rightPokeIRS();
    static void wellISR();
    static void alarmISR();
    static void dateTime(uint16_t* date, uint16_t* time);
};

#endif