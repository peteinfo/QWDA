#include "FED4.h"

FED4 *FED4::instance = nullptr;

FED4::FED4() {
}

void FED4::begin()
{
    function = "begin";

    instance = this;

    pinMode(MTR_EN_PIN, OUTPUT);
    pinMode(MTR_1_PIN, OUTPUT);
    pinMode(MTR_2_PIN, OUTPUT);
    pinMode(MTR_3_PIN, OUTPUT);
    pinMode(MTR_4_PIN, OUTPUT);

    pinMode(LFT_POKE_PIN, INPUT_PULLUP);
    pinMode(RGT_POKE_PIN, INPUT_PULLUP);

    pinMode(WELL_PIN, INPUT);
    digitalWrite(WELL_PIN, HIGH);

    attachInterrupt(digitalPinToInterrupt(LFT_POKE_PIN), leftPokeIRS, CHANGE);
    attachInterrupt(digitalPinToInterrupt(RGT_POKE_PIN), rightPokeIRS, CHANGE);
    attachInterrupt(digitalPinToInterrupt(WELL_PIN), wellISR, FALLING);

    EIC->WAKEUP.reg |= (1<<4); // Enable wakeup on pin 6 (LFT_POKE_PIN)
    EIC->WAKEUP.reg |= (1<<15); // Enable wakeup on pin 5 (RGT_POKE_PIN)
    EIC->WAKEUP.reg |= (1<<16); // Enable wakeup on pin RTC peripheral

    SYSCTRL->XOSC32K.reg |= (SYSCTRL_XOSC32K_RUNSTDBY | SYSCTRL_XOSC32K_ONDEMAND);
    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID(GCM_EIC) |
                      GCLK_CLKCTRL_CLKEN |
                      GCLK_CLKCTRL_GEN_GCLK1;
    while (GCLK->STATUS.bit.SYNCBUSY);

    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk; // Enable deep sleep mode
    
    rtc.begin();
    if (rtc.lostPower())
    {
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    rtcZero.begin();
    rtcZero.setTime(rtc.now().hour(), rtc.now().minute(), rtc.now().second());
    rtcZero.setDate(rtc.now().day(), rtc.now().month(), rtc.now().year() - 2000);
    uint8_t alarmSeconds = rtcZero.getSeconds() + LP_DISPLAY_REFRESH_RATE;
    if (alarmSeconds >= 59) {
        alarmSeconds -= 60;
    }
    rtcZero.setAlarmTime(0, 0, alarmSeconds);
    rtcZero.attachInterrupt(alarmISR);
    rtcZero.enableAlarm(RTCZero::MATCH_SS);

    randomSeed(rtc.now().unixtime());

    stepper.setSpeed(7);

    display.begin();
    display.clearDisplay();
    display.setRotation(3);
    display.refresh();

    logFileName.reserve(20);

    SdFile::dateTimeCallback(dateTime);
    initSD();

    menu_display = &display;
    menu_rtc = &rtc;
    runModeMenu();

    if (mode == MODE_FR) {
        runFrMenu();
    }
    if (mode == MODE_VI) {
        runViMenu();
    }

    ignorePokes = true;

    initLogFile(); 
    displayLayout();
    delay(2000);

    ignorePokes = false;
}

int timesDisplayUpdated = 0;
long lastBatteryLog = 0;
void FED4::run()
{
    function = "run";   

    long now = rtc.now().unixtime();
    if (now - lastBatteryLog >= 1200) {
        lastBatteryLog = now;
        Event event = {
            .time = getDateTime(),
            .event = String("Battery Log")
        };
        logEvent(event);
    }

    if (sleepMode) {
        updateDisplay(true);
        timesDisplayUpdated++;
        String lastUpdate = String("Last update (") + String(timesDisplayUpdated) + "): " + String(rtcZero.getHours()) + ":" + String(rtcZero.getMinutes()) + ":" + String(rtcZero.getSeconds());
        print(lastUpdate, 1);
        sleep();
        return; 
    }

    uint8_t leftRead = digitalRead(LFT_POKE_PIN);
    uint8_t rightRead = digitalRead(RGT_POKE_PIN);

    if (leftRead == LOW)
    {
        dtLftPoke = millis() - startLftPoke;
    }
    if (rightRead == LOW)
    {
        dtRgtPoke = millis() - startRgtPoke;
    }

    if (leftRead == LOW && rightRead == LOW && dtLftPoke > 3000 && dtRgtPoke > 3000)
    {
        reset();
    } 
    else updateDisplay();

    if (mode == MODE_FR)
    {
        if (
            activeSensor == BOTH 
            && (leftPokeCount + rightPokeCount) % ratio == 0 
        ) {
            if ( getLeftPoke() ) {
                feed(leftReward);
            }
            if ( getRightPoke() ) {
                
                feed(rightReward);
            }
        }

        else if (
            activeSensor == LEFT
            && getLeftPoke()
            && leftPokeCount % ratio == 0
        ) {
            feed(leftReward);
        }

        else if (
            activeSensor == RIGHT
            && getRightPoke()
            && rightPokeCount % ratio == 0
        ) {
            feed(rightReward);
        }
    }

    else if (mode == MODE_VI)
    {
        if (viSet) {
            if (viCountDown <=0) {
                if (activeSensor == LEFT) {
                    feed(leftReward);
                }
                else if (activeSensor == RIGHT) {
                    feed(rightReward);
                }
                else if (activeSensor == BOTH) {
                    int rand = random(0, 2);
                    if (rand == 0) {
                        feed(leftReward);
                    }
                    else {
                        feed(rightReward);
                    }
                }
                viSet = false;
                viCountDown = 0;
            }
            else {
                detachInterrupt(digitalPinToInterrupt(LFT_POKE_PIN));
                detachInterrupt(digitalPinToInterrupt(RGT_POKE_PIN));

                viCountDown  = (int)(feedUnixT - rtc.now().unixtime());
                
                attachInterrupt(digitalPinToInterrupt(LFT_POKE_PIN), leftPokeIRS, CHANGE);
                attachInterrupt(digitalPinToInterrupt(RGT_POKE_PIN), rightPokeIRS, CHANGE);
            }
        }
        else {
            if (
                ( getLeftPoke() && ( activeSensor == LEFT || activeSensor == BOTH ) )
                || ( getRightPoke() && ( activeSensor == RIGHT || activeSensor == BOTH) )
            ) {
                if (viCountDown == 0 && !viSet) {
                    viCountDown = getViCountDown();
                    viSet = true;
                    Event event = {
                        .time = getDateTime(),
                        .event = EVENT_SET_VI
                    };
                    logEvent(event);
                    feedUnixT = rtc.now().unixtime() + viCountDown;
                }
            }
        }
    }

    sleep();
}

void FED4::reset()
{
    ignorePokes = true;

    makeNoise(300);
    print("Resetting...");
    
    Event event;
    event.time = getDateTime();
    event.event = "Reset";
    logEvent(event);
    
    long startWait = millis();
    while (millis() - startWait < 5000);
    
    leftPokeCount = 0;
    rightPokeCount = 0;
    pelletsDispensed = 0;
    viCountDown = 0;
    viSet = false;
    
    startLftPoke = millis();
    startRgtPoke = millis();
    
    leftPoke = false;
    rightPoke = false;

    if(mode == MODE_FR) {
        runFrMenu();
    }
    if (mode == MODE_VI) {
        runViMenu();
    }
    
    initLogFile();
    
    displayLayout();

    ignorePokes = false;

    if (entryPoint) entryPoint();
}

void FED4::sleep() {
    sleepMode = true;
    __DSB();
    __WFI();
}

int FED4::getViCountDown() {
    int offset = (float)viAvg * spread;
    int lowerBound = viAvg - offset;
    int upperBound = viAvg + offset;
    return random(lowerBound, upperBound);
}

bool FED4::getLeftPoke()
{
    if (leftPoke)
    {
        leftPoke = false;
        return true;
    }
    return false;
}

bool FED4::getRightPoke()
{
    if (rightPoke)
    {
        rightPoke = false;
        return true;
    }
    return false;
}

bool FED4::getWellStatus() {
    if(pelletDropped) {
        pelletDropped = false;
        return true;
    }
    return false;
}

void FED4::leftPokeHandler()
{
    sleepMode = false;

    if (ignorePokes)
        return;

    if (digitalRead(LFT_POKE_PIN) == LOW)
    {
        long millis_now = millis();
        if (millis_now - startLftPoke < 50)
            return;
        startLftPoke = millis_now;
        leftPokeStarted = true;
    }
    
    else
    {
        if (!leftPokeStarted)
            return;
        leftPokeCount++;
        Event event = {
            .time = getDateTime(),
            .event = EVENT_LEFT
        };
        logEvent(event);
        leftPokeStarted = false;
        rightPoke = true;
        dtLftPoke = 0;
    }
}

void FED4::rightPokeHandler()
{
    sleepMode = false;

    if (ignorePokes)
        return;

    if (digitalRead(RGT_POKE_PIN) == LOW)
    {
        long millis_now = millis();
        if (millis_now - startRgtPoke < 50)
            return;
        startRgtPoke = millis_now;
        rightPokeStarted = true;
    }
    
    else
    {
        if (!rightPokeStarted)
            return;
        rightPokeCount++;
        Event event = {
            .time = getDateTime(),
            .event = EVENT_RIGHT
        };
        logEvent(event);
        rightPokeStarted = false;
        rightPoke = true;
        dtRgtPoke = 0;
    }
}

void FED4::wellHandler() {
    pelletDropped = true;
}

void FED4::alarmHandler() {
    if (sleepMode) {
        updateDisplay(true);

        uint8_t alarmSeconds = rtcZero.getSeconds() + LP_DISPLAY_REFRESH_RATE - 1;
        uint8_t alarmMinutes = rtcZero.getMinutes() + (alarmSeconds / 60);
        uint8_t alarmHours = rtcZero.getHours() + (alarmMinutes / 60);
        alarmSeconds = alarmSeconds % 60;
        alarmMinutes = alarmMinutes % 60;
        alarmHours = alarmHours % 24;
        rtcZero.setAlarmTime(alarmHours, alarmMinutes, alarmSeconds);
    }
}

void FED4::feed(int pellets, bool wait)
{
    function = "feed";

    pelletDropped = false;

    long startOfFeed;
    for (int i = 0; i < pellets; i++) {
        startOfFeed = millis();
        while (getWellStatus() == false)
        {
            long deltaT = millis() - startOfFeed;
            if (deltaT < 5000)
            {
                rotateWheel(1);
            }
            else if (deltaT < 15000)
            {
                rotateWheel(-3);
                rotateWheel(5);
            }
            else
            {
                rotateWheel(-10);
                rotateWheel(15);
            }
        }
        pelletsDispensed++;
        Event event = {
            .time = getDateTime(),
            .event = EVENT_PEL
        };
        logEvent(event);
        delay(200);
    }

    updateDisplay();
}

void FED4::rotateWheel(int degrees)
{
    digitalWrite(MTR_EN_PIN, HIGH);

    int steps = (STEPS * degrees / 360);
    stepper.step(steps);

    digitalWrite(MTR_EN_PIN, LOW);
}

void FED4::makeNoise(int duration)
{
    for (int i = 0; i < duration / 50; i++)
    {
        tone(BUZZER_PIN, random(50, 250), 50);
        delay(duration / 50);
    }
}

void FED4::print(String str, uint8_t size)
{
    display.setTextSize(size);
    display.fillRect(2, 106, DISPLAY_W - 4, 16, WHITE);
    display.setTextColor(BLACK);
    display.setCursor(4, 106);
    display.print(str);
    display.refresh();
}

void FED4::initSD()
{
    digitalWrite(MTR_EN_PIN, LOW);

    while (!sd.begin(CARD_SEL_PIN, SD_SCK_MHZ(4)))
    {
        showSdError();
    }

    File deviceNumberFile = sd.open("DEVICE_NUMBER", FILE_WRITE);
    deviceNumberFile = sd.open("DEVICE_NUMBER", FILE_READ);
    deviceNumber = deviceNumberFile.parseInt();
    deviceNumberFile.rewind();
    deviceNumberFile.flush();
    deviceNumberFile.close();
}

void FED4::showSdError()
{
    display.setTextSize(2);
    display.setTextColor(BLACK);
    display.clearDisplay();
    display.setCursor(20, 40);
    display.println("   Check");
    display.setCursor(10, 60);
    display.println("  SD Card!");
    display.refresh();
    delay(1000);
    display.clearDisplay();
    display.refresh();
    delay(1000);
}

void FED4::initLogFile()
{
    digitalWrite(MTR_EN_PIN, LOW);
    char fileName[30] = "";

    DateTime now = getDateTime();
    
    strcat(fileName, "FED");
    if (deviceNumber < 10) strcat(fileName, "0");
    strcat(fileName, String(deviceNumber).c_str());
    strcat(fileName, "_");
    if (now.day() < 10) strcat(fileName, "0");
    strcat(fileName, String(now.day()).c_str());
    strcat(fileName, "-");
    if (now.month() < 10) strcat(fileName, "0");
    strcat(fileName, String(now.month()).c_str());
    strcat(fileName, "-");
    if (now.year() % 100 < 10) strcat(fileName, "0");
    strcat(fileName, String(now.year() % 100).c_str());
    strcat(fileName, "_");
    strcat(fileName, "01.csv");
    
    // fed1_06-03-25_01.csv
    
    int fileIndex = 1;
    while (sd.exists(fileName))
    {
        fileIndex++;
        fileName[15] = '0' + fileIndex / 10;
        fileName[16] = '0' + fileIndex % 10;
    }
    
    File logFile = sd.open(fileName, FILE_WRITE);
    logFile.print("TimeStamp,Battery,Device Number,Mode,Event,Active Sensor,Left Reward,Right Reward,Left Poke Count,Right Poke Count,Pellet Count");
    if (mode == MODE_VI)
    logFile.print(",VI Count Down");
    if (mode == MODE_FR)
    logFile.print(",Ratio");
    logFile.print("\n");
    
    logFile.flush();
    logFile.close();
    logFileName = fileName;
}

void FED4::logEvent(Event e)
{
    detachInterrupt(digitalPinToInterrupt(LFT_POKE_PIN));
    detachInterrupt(digitalPinToInterrupt(RGT_POKE_PIN));

    char row[500] = "";
    DateTime now = getDateTime();
    
    char date[20];
    sprintf(date, "%d/%d/%d ", now.day(), now.month(), now.year() % 1000);
    char  time[20];
    sprintf(time, "%d:%d:%d", now.hour(), now.minute(), now.second());
    strcat(row, date);
    strcat(row, time);
    strcat(row, ",");

    char battery_str[5];
    sprintf(battery_str, "%d", getBatteryPercentage());
    strcat(row, battery_str);
    strcat(row, ",");
    
    char deviceNumber_str[5];
    sprintf(deviceNumber_str, "%d", deviceNumber%100);
    strcat(row, deviceNumber_str);
    strcat(row, ",");

    char mode_str[10];
    if (mode == MODE_FR)
        sprintf(mode_str, "FR");
    else if (mode == MODE_VI)
        sprintf(mode_str, "VI");
    else if (mode == MODE_OTHER)
        sprintf(mode_str, "OTHER");
    strcat(row, mode_str);
    strcat(row, ",");

    strcat(row, e.event.c_str());
    strcat(row, ",");

    char activeSensor_str[10];
    if (activeSensor == LEFT)
        sprintf(activeSensor_str, "Left");
    if (activeSensor == RIGHT)
    sprintf(activeSensor_str, "Right");
    if (activeSensor == BOTH)
        sprintf(activeSensor_str, "Both");
    strcat(row, activeSensor_str);
    strcat(row, ",");

    char leftReward_str[5];
    if (activeSensor == LEFT || activeSensor == BOTH)
    sprintf(leftReward_str, "%d", leftReward);
    else
        sprintf(leftReward_str, "nan");
    char rightReward_str[5];
    if (activeSensor == RIGHT || activeSensor == BOTH)
        sprintf(rightReward_str, "%d", rightReward);
    else
    sprintf(rightReward_str, "nan");
    strcat(row, leftReward_str);
    strcat(row, ",");
    strcat(row, rightReward_str);
    strcat(row, ",");
    
    char leftPokeCount_str[5];
    sprintf(leftPokeCount_str, "%d", leftPokeCount);
    char rightPokeCount_str[5];
    sprintf(rightPokeCount_str, "%d", rightPokeCount);
    strcat(row, leftPokeCount_str);
    strcat(row, ",");
    strcat(row, rightPokeCount_str);
    strcat(row, ",");
    
    char pelletsDispensed_str[5];
    sprintf(pelletsDispensed_str, "%d", pelletsDispensed);
    strcat(row, pelletsDispensed_str);
    
    if (mode == MODE_VI)
    {
        strcat(row, ",");
        char viCountDown_str[10];
        sprintf(viCountDown_str, "%d", viCountDown);
        strcat(row, viCountDown_str);
    }
    if (mode == MODE_FR)
    {
        strcat(row, ",");
        char ratio_str[10];
        sprintf(ratio_str, "%d", ratio);
        strcat(row, ratio_str);
    }
    
    strcat(row, "\n");
    

    File logFile = sd.open(logFileName, FILE_WRITE);
    logFile.print(row);
    logFile.flush();
    logFile.close();

    attachInterrupt(digitalPinToInterrupt(LFT_POKE_PIN), leftPokeIRS, CHANGE);
    attachInterrupt(digitalPinToInterrupt(RGT_POKE_PIN), rightPokeIRS, CHANGE);
}

void FED4::deleteLines(int n)
{
    File logFile = sd.open(logFileName, O_RDWR);
    
    int newLineCount = 0;
    uint lastNewLinePos = 0;
    uint fileSize = logFile.size();

    for (uint i = fileSize - 1; i >= 0; i--)
    {
        logFile.seek(i);
        if (logFile.read() == '\n')
        {
            newLineCount++;
            if (newLineCount == n + 1)
            {
                lastNewLinePos = i + 1;
                break;
            }
        }
    }

    if (newLineCount <= n)
    {
        lastNewLinePos = 0;
    }

    logFile.truncate(lastNewLinePos);
    logFile.flush();
    logFile.close();

    return;
}

void FED4::drawDateTime()
{
    function = "drawDateTime";

    DateTime now = getDateTime();

    display.setTextSize(1);
    display.setTextColor(BLACK);

    // cover up the date and time
    display.fillRect(2, 6, 95, 10, WHITE);

    display.setCursor(4, 8);
    if (now.day() < 10)
        display.print(0);
    display.print(now.day());
    display.print("/");
    if (now.month() < 10)
        display.print(0);
    display.print(now.month());
    display.print("/");
    display.print(now.year() % 1000);

    display.setCursor(65, 8);
    if (now.hour() < 10)
        display.print(0);
    display.print(now.hour());
    display.print(":");
    if (now.minute() < 10)
        display.print(0);
    display.print(now.minute());
}

void FED4::drawBateryCharge()
{
    function = "drawBateryCharge";

    // cover up the battery symbol
    display.fillRect(129, 3, 37, 14, WHITE);

    // draw the battery symbol
    display.drawRect(134, 5, 31, 12, BLACK);
    display.fillRect(131, 8, 3, 6, BLACK);

    int batteryPercentage = getBatteryPercentage();

    if (batteryPercentage > 75)
    {
        display.fillRect(136, 7, 6, 8, BLACK);
    }
    if (batteryPercentage > 50)
    {
        display.fillRect(136 + 7, 7, 6, 8, BLACK);
    }
    if (batteryPercentage > 25)
    {
        display.fillRect(136 + 14, 7, 6, 8, BLACK);
    }
    if (batteryPercentage > 10)
    {
        display.fillRect(136 + 21, 7, 6, 8, BLACK);
    }
    if (batteryPercentage <= 10)
    {
        display.setTextSize(2);
        display.fillRect(144, 4, 12, 16, WHITE);
        display.setCursor(145, 4);
        display.print("!");
    }
}

void FED4::drawStats()
{
    function = "drawStats";

    display.setTextSize(2);

    display.fillRect(98, 24, 100, 80, WHITE);

    display.setTextSize(2);

    display.setCursor(100, 26);
    display.print(leftPokeCount);
    display.setCursor(100, 46);
    display.print(rightPokeCount);
    display.setCursor(100, 66);
    display.print(pelletsDispensed);

    if (mode == MODE_VI) {
        display.setCursor(100, 86);
        if (viCountDown >= 0) {
            display.print(viCountDown);
        }
    }
}

void FED4::displayLayout()
{
    display.clearDisplay();

    // draw the top line
    display.drawLine(5, 20, DISPLAY_W - 5, 20, BLACK);

    display.setTextSize(2);
    display.setTextColor(BLACK);

    display.setCursor(4, 26);
    display.print("Left: ");
    display.setCursor(4, 46);
    display.print("Right: ");
    display.setCursor(4, 66);
    display.print("Pellets: ");
    if (mode == MODE_VI)
    {
        display.setCursor(4, 86);
        display.print("VI CD: ");
    }

    display.setTextSize(1);
    display.setCursor(4, 124);
    if (mode == MODE_FR)
        display.print("Fixed Ratio");
    else if (mode == MODE_VI)
        display.print("Variable Interval");
    display.setCursor(4, 134);
    display.print(logFileName);

    updateDisplay(true);
}

void FED4::updateDisplay(bool statusOnly)
{
    function = "updateDisplay";

    // if (millis() - lastStatusUpdate < 1000) {
    //     return;
    // } 

    detachInterrupt(digitalPinToInterrupt(LFT_POKE_PIN));
    detachInterrupt(digitalPinToInterrupt(RGT_POKE_PIN));
    
    if (!statusOnly) {
        drawStats();
    }
    
    drawDateTime();
    drawBateryCharge();

    display.refresh();

    attachInterrupt(digitalPinToInterrupt(RGT_POKE_PIN), rightPokeIRS, CHANGE);
    attachInterrupt(digitalPinToInterrupt(LFT_POKE_PIN), leftPokeIRS, CHANGE);

    lastStatusUpdate = millis();
}

void FED4::runModeMenu()
{
    ignorePokes = true;

    Menu *modeMenu = initMenu(nullptr, 6);

    Menu *clockMenu = initClockMenu(modeMenu);
    modeMenu->items[0] = initItem((char *)"Time", clockMenu);

    int dev = deviceNumber;
    modeMenu->items[1] = initItem((char *)"Dev no", &dev, 0, 99, 1);

    const char *modes[] = {"FR", "VI"};
    modeMenu->items[2] = initItem((char *)"Mode", modes, 2);

    const char *sensors[] = {"L&R", "L", "R"};
    modeMenu->items[3] = initItem((char *)"Sensor", sensors, 3);

    modeMenu->items[4] = initItem((char *)"L Rew", &leftReward, 0, 255, 1);
    modeMenu->items[5] = initItem((char *)"R Rew", &rightReward, 0, 255, 1);

    int batteryLevel = getBatteryPercentage();

    runMenu(modeMenu, batteryLevel);

    if (modeMenu->items[3]->valueIdx == 0)
    {
        activeSensor = BOTH;
    }
    else if (modeMenu->items[3]->valueIdx == 1)
    {
        activeSensor = LEFT;
    }
    else
    {
        activeSensor = RIGHT;
    }

    if (modeMenu->items[2]->valueIdx == 0)
    {
        mode = MODE_FR;
    }
    else if (modeMenu->items[2]->valueIdx == 1)
    {
        mode = MODE_VI;
    }
    else
    {
        mode = MODE_OTHER;
    }

    deviceNumber = dev;
    File deviceNumberFile = sd.open("DEVICE_NUMBER", FILE_WRITE);
    deviceNumberFile.truncate(0);
    deviceNumberFile.print(deviceNumber);
    deviceNumberFile.flush();
    deviceNumberFile.close();

    freeMenu(modeMenu);
    ignorePokes = false;
}

void FED4::runViMenu() {
    ignorePokes = true;

    Menu *menu = initMenu(nullptr, 2);

    int avg = viAvg;
    float s = spread;

    menu->items[0] = initItem((char*)"Avg T", &avg, 0, 120, 5);
    // print("got here");
    menu->items[1] = initItem((char*)"Spread", &spread, 0.0, 1.0, 0.05);

    runMenu(menu);

    viAvg = avg;
    spread = s;

    freeMenu(menu);
    ignorePokes = false;
}

void FED4::runFrMenu() {
    ignorePokes = true;

    Menu *menu = initMenu(nullptr, 1);

    menu->items[0] = initItem((char*)"Ratio", &ratio, 1, 10, 1);

    runMenu(menu);

    freeMenu(menu);
    ignorePokes = false;
}

int FED4::getBatteryPercentage()
{
    float batteryVoltage = analogRead(VBAT_PIN);
    batteryVoltage *= 2;
    batteryVoltage *= 3.3;
    batteryVoltage /= 1024;

    int batteryPercentage = 0;
    if (batteryVoltage >= 4.2)
    {
        batteryPercentage = 100;
    }
    else if (batteryVoltage >= 4.15)
    {
        batteryPercentage = 95;
    }
    else if (batteryVoltage >= 4.11)
    {
        batteryPercentage = 90;
    }
    else if (batteryVoltage >= 4.08)
    {
        batteryPercentage = 85;
    }
    else if (batteryVoltage >= 4.02)
    {
        batteryPercentage = 80;
    }
    else if (batteryVoltage >= 3.98)
    {
        batteryPercentage = 75;
    }
    else if (batteryVoltage >= 3.95)
    {
        batteryPercentage = 70;
    }
    else if (batteryVoltage >= 3.91)
    {
        batteryPercentage = 65;
    }
    else if (batteryVoltage >= 3.87)
    {
        batteryPercentage = 60;
    }
    else if (batteryVoltage >= 3.85)
    {
        batteryPercentage = 55;
    }
    else if (batteryVoltage >= 3.84)
    {
        batteryPercentage = 50;
    }
    else if (batteryVoltage >= 3.82)
    {
        batteryPercentage = 45;
    }
    else if (batteryVoltage >= 3.8)
    {
        batteryPercentage = 40;
    }
    else if (batteryVoltage >= 3.79)
    {
        batteryPercentage = 35;
    }
    else if (batteryVoltage >= 3.77)
    {
        batteryPercentage = 30;
    }
    else if (batteryVoltage >= 3.75)
    {
        batteryPercentage = 25;
    }
    else if (batteryVoltage >= 3.73)
    {
        batteryPercentage = 20;
    }
    else if (batteryVoltage >= 3.71)
    {
        batteryPercentage = 15;
    }
    else if (batteryVoltage >= 3.69)
    {
        batteryPercentage = 10;
    }
    else if (batteryVoltage >= 3.61)
    {
        batteryPercentage = 5;
    }
    else
    {
        batteryPercentage = 0;
    }

    return batteryPercentage;
}

DateTime FED4::getDateTime() {
    return rtc.now();
}

void FED4::leftPokeIRS()
{
    if (instance)
        instance->leftPokeHandler();
}

void FED4::rightPokeIRS()
{
    if (instance)
        instance->rightPokeHandler();
}

void FED4::wellISR()
{
    if (instance)
        instance->wellHandler();
}

void FED4::alarmISR()
{
    if (instance)
        instance->alarmHandler();
}

void FED4::dateTime(uint16_t *date, uint16_t *time)
{
    DateTime now = instance->getDateTime();
    *date = FAT_DATE(now.year(), now.month(), now.day());
    *time = FAT_TIME(now.hour(), now.minute(), now.second());
}