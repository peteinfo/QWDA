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
#include <ArduinoJson.h>

#define DEBUG

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
#define SHRP_CS_PIN     10
#define MTR_EN_PIN      13
#define MTR_1_PIN       A2
#define MTR_2_PIN       A3
#define MTR_3_PIN       A4
#define MTR_4_PIN       A5

#define LP_DISPLAY_REFRESH_RATE 30 // seconds

#define MODE_FR 0
#define MODE_VI 1
#define MODE_CHANCE 2
#define MODE_OTHER -1

#define LEFT    0
#define RIGHT   1
#define BOTH    2

#define DISPLAY_H 144
#define DISPLAY_W 168
#define BLACK 0
#define WHITE 1

#define ROW_MAX_LEN        500
#define FILE_RAM_BUFF_SIZE 1024
#define FILE_PREALLOC_SIZE 25 * 1024UL * 1024UL

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
    const char *event;
    ~Event() {};
};

class FED4 {
private:
    volatile bool sleepMode = false;

    volatile bool leftPokeStarted = false;
    volatile bool rightPokeStarted = false;

    volatile bool leftPoke = false;
    volatile bool rightPoke = false;
    bool pelletDropped = false;

    volatile long startLftPoke = 0;
    volatile long startRgtPoke = 0;

    long dtLftPoke = 0;
    long dtRgtPoke = 0;

    long lastStatsUpdate= 0;
    long lastStatusUpdate = 0;

    bool jamError = false;
public:
    FED4();
    static FED4* instance;
    int deviceNumber = 0;

#ifdef DEBUG
    String function;
    String interrupedFunction;
#endif

    volatile int leftPokeCount = 0;
    volatile int rightPokeCount = 0;
    int pelletsDispensed = 0;
    int leftReward = 1;
    int rightReward = 1;
    uint8_t activeSensor = BOTH;
    bool feedWindow = true;
    int windowStart = 9;
    int windowEnd = 12;

    int ratio = 1;
    int viAvg = 30;
    float viSpread = 0.75;
    int viCountDown = 0;
    uint32_t feedUnixT = 0;
    bool viSet = false;
    float chance = 0.5;

    int getViCountDown();

    int8_t mode = MODE_OTHER;

    int reward;
    bool checkFRCondition();
    bool checkVICondition();
    bool checkChanceCondition();
    std::function<bool()> checkOtherCondition = nullptr;

    std::function<void()> entryPoint = nullptr;
    void begin();
    void run();
    void reset();
    void logError(String str);

    void loadConfig();
    void saveConfig();

    bool checkCondition();
    bool checkFeedingWindow();

    void setCue();

    void attachInterrupts();
    void detachInterrupts();

    bool enableSleep = true;
    void sleep();
    
    bool getLeftPoke();
    bool getRightPoke(); 
    bool getWellStatus();
    
    void feed(int pellets = 1, bool wait = true);
    void rotateWheel(int degrees);
    
    SdFat sd;
    SdFile logFile;
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

    void runModeMenu();
    void runViMenu();
    void runFrMenu();
    void runChanceMenu();
    std::function<void()> runOtherModeMenu = nullptr;
    
    void makeNoise(int duration = 300);
    void print(String str, uint8_t size = 2);

    int getBatteryPercentage();
    DateTime getDateTime();
    
    bool ignorePokes = false;
    void leftPokeHandler();
    void rightPokeHandler();
    void alarmHandler();
    void wellHandler();

    Adafruit_SharpMem display = Adafruit_SharpMem(SHRP_SCK_PIN, SHRP_MOSI_PIN, SHRP_CS_PIN,  DISPLAY_H, DISPLAY_W);
    Stepper stepper = Stepper(STEPS, MTR_1_PIN, MTR_2_PIN, MTR_3_PIN, MTR_4_PIN); 
    RTC_PCF8523 rtc;
    RTCZero rtcZero;
    Adafruit_NeoPixel strip = Adafruit_NeoPixel(10, NEOPXL_PIN, NEO_GRBW + NEO_KHZ800);

private:
    char logBuffer[FILE_RAM_BUFF_SIZE];
    size_t logBufferPos = 0;
    unsigned long lastFlush = 0;
    void writeToLog(char row[ROW_MAX_LEN], bool forceFlush=false);
    void flushToSD();

    static void leftPokeIRS();
    static void rightPokeIRS();
    static void wellISR();
    static void alarmISR();
    static void dateTime(uint16_t* date, uint16_t* time);
};

#endif