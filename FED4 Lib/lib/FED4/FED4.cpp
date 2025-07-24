#include "FED4.h"

FED4 *FED4::instance = nullptr;

void __delay(uint32_t ms) {
    unsigned long startT = millis();
    while(millis() - startT > ms);
}

void HardFault_Handler(void) {
    __asm("BKPT #0");
    while(1){}
}

void FED4::begin() {
    instance = this;

    // Motor pins
    pinMode(FED4Pins::MTR_EN, OUTPUT);
    pinMode(FED4Pins::MTR_1, OUTPUT);
    pinMode(FED4Pins::MTR_2, OUTPUT);
    pinMode(FED4Pins::MTR_3, OUTPUT);
    pinMode(FED4Pins::MTR_4, OUTPUT);

    // Input pins
    pinMode(FED4Pins::LFT_POKE, INPUT_PULLUP);
    pinMode(FED4Pins::RGT_POKE, INPUT_PULLUP);
    pinMode(FED4Pins::WELL, INPUT);
    digitalWrite(FED4Pins::WELL, HIGH);

    // Interrupts
    attachInterrupt(digitalPinToInterrupt(FED4Pins::LFT_POKE), leftPokeIRS, CHANGE);
    attachInterrupt(digitalPinToInterrupt(FED4Pins::RGT_POKE), rightPokeIRS, CHANGE);
    attachInterrupt(digitalPinToInterrupt(FED4Pins::WELL), wellISR, FALLING);

    // Wakeup sources
    EIC->WAKEUP.reg |= (1 << 4);   // FED4Pins::LFT_POKE
    EIC->WAKEUP.reg |= (1 << 15);  // FED4Pins::RGT_POKE
    EIC->WAKEUP.reg |= (1 << 16);  // RTC peripheral

    // Clock setup
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
    uint8_t alarmSeconds = rtcZero.getSeconds() + LP_AWAKE_PERIOD;
    if (alarmSeconds >= 59) {
        alarmSeconds -= 60;
    }
    rtcZero.setAlarmTime(0, 0, alarmSeconds);
    rtcZero.attachInterrupt(alarmISR);
    rtcZero.enableAlarm(RTCZero::MATCH_SS);

    randomSeed(rtc.now().unixtime());

    display.begin();
    display.clearDisplay();
    display.setRotation(3);
    display.refresh();

    digitalWrite(FED4Pins::MTR_EN, HIGH);
    __delay(2);
    strip.begin();
    strip.clear();
    strip.show();
    
    SdFile::dateTimeCallback(dateTime);
    initSD();
    
    loadConfig();
    
    menu_display = &display;
    menu_rtc = &rtc;
    runConfigMenu();
    if (mode == Mode::FR) {
        runFrMenu();
    }
    if (mode == Mode::VI) {
        runViMenu();
    }
    if (mode == Mode::CHANCE) {
        runChanceMenu();
    }
    if (mode == Mode::OTHER) {
        runOtherModeMenu();
    }

    saveConfig();

    ignorePokes = true;

    initLogFile(); 
    displayLayout();

    ignorePokes = false;
}

void FED4::run() {
    setLightCue();
    
    updateDisplay();    
    
    if (checkCondition()) {
        feed(reward);
    }

    if (!checkFeedingWindow()) {
        sleep();
    }
}

bool FED4::checkFeedingWindow() {
    if (!feedWindow) return false;

    DateTime now = getDateTime();

    if ( 
        windowEnd > windowStart
        && now.hour() >= windowStart
        && now.hour() < windowEnd
    ) {
        return true;
    }

    if (
        windowEnd < windowStart
        && ( 
            now.hour() >= windowStart
            || now.hour() < windowEnd
        )
    ) {
        return true;
    }

    return false;
}

bool FED4::checkCondition() {
    if (feedWindow && !checkFeedingWindow()) {
        return false;
    }

    bool conditionMet;
    switch (mode) {
    case Mode::FR:
        conditionMet = checkFRCondition();
        break;

    case Mode::VI:
        conditionMet = checkVICondition();
        break;
    
    case Mode::CHANCE:
        conditionMet = checkChanceCondition();
        break;
    
    default:
        if(checkChanceCondition == nullptr) {
            conditionMet = false;
        }
        else {
            conditionMet = checkOtherCondition();
        }
        break;  
    }

    return conditionMet;
}

bool FED4::checkFRCondition() {
    bool conditionMet = false;
    switch (activeSensor) {
    case ActiveSensor::BOTH:
        if ( (leftPokeCount + rightPokeCount) % ratio == 0 ) {
            if (getLeftPoke()) {
                reward = leftReward;
                conditionMet = true;
            }
            if (getRightPoke()) {
                reward = rightReward;
                conditionMet = true;
            }
        }
        break;

    case ActiveSensor::LEFT:
        if (getLeftPoke() && leftPokeCount % ratio == 0) {
            reward = leftReward;
            conditionMet = true;
        }
        break;

    case ActiveSensor::RIGHT:
        if(getRightPoke() && rightPokeCount %ratio == 0) {
            reward = rightPoke;
            conditionMet = true;
        }
        break;
    }
    
    return conditionMet;
}

bool FED4::checkVICondition() {
    if (viSet) {
        if (viCountDown <= 0) {
            viSet = false;
            viCountDown = 0;
            return true;
        }
        else {
            viCountDown  = (int)(feedUnixT - getDateTime().unixtime());
        }
    }
    else {
        bool pokedLeft = getLeftPoke();
        bool pokedRight = getRightPoke();

        bool conditionMet = false;
        switch (activeSensor) {
        case ActiveSensor::BOTH:
            if (pokedLeft || pokedRight) {
                conditionMet = true;
            }
            break;
        
        case ActiveSensor::LEFT:
            if (pokedLeft) {
                conditionMet = true;
            }
            break;

        case ActiveSensor::RIGHT:
            if (pokedRight) {
                conditionMet = true;
            }
        }

        if (conditionMet) {
            viCountDown = getViCountDown();
            viSet = true;

            if (pokedLeft) {
                reward = leftReward;
            }
            else if (pokedRight) {
                reward = rightPoke;
            }

            Event e = Event {
                .time = getDateTime(),
                .message = EventMsg::SET_VI
            };
            logEvent(e);

            feedUnixT = getDateTime().unixtime() + viCountDown;
        }
    }

    return false;
}

bool FED4::checkChanceCondition() {
    int r = random(0, 100);
    if (
        getLeftPoke()
        && ( activeSensor == ActiveSensor::LEFT || activeSensor == ActiveSensor::BOTH )
        && r <= int(chance * 100)
    ) {
        reward = leftReward;
        return true;
    }
    else if (
        getRightPoke() 
        && ( activeSensor == ActiveSensor::RIGHT || activeSensor == ActiveSensor::BOTH ) 
        && r <= int(chance * 100)
    ) {
        reward = rightReward;
        return true;
    }

    return false;
}

void FED4::setLightCue() {
    pauseInterrupts();
    if (checkFeedingWindow()) {
        digitalWrite(FED4Pins::MTR_EN, HIGH);
        __delay(2);

        switch (activeSensor) {
        case ActiveSensor::BOTH:
            strip.setPixelColor(8, 5, 2, 0, 0);
            strip.setPixelColor(9, 5, 2, 0, 0);
            strip.setPixelColor(0, 0, 0, 0);
            break;
        
        case ActiveSensor::LEFT:
            strip.setPixelColor(9, 5, 2, 0, 0);
            strip.setPixelColor(0,0,0,0);
            break;

        case ActiveSensor::RIGHT:
            strip.setPixelColor(8, 5, 2, 0, 0);
            break;
        }

        strip.show();
    } else {
        digitalWrite(FED4Pins::MTR_EN, HIGH);
        __delay(2);
        strip.clear();
        strip.show();
        digitalWrite(FED4Pins::MTR_EN, LOW);
    }

    startInterrupts();
}

void FED4::logError(String str) {
    char errorMsg[100] = "";
    snprintf(errorMsg, sizeof(errorMsg), "Error: %s", str.c_str());
    Event event = {
        .time = getDateTime(),
        .message = (const char *)errorMsg
    };
    logEvent(event);
}

void FED4::loadConfig() {
    if (!sd.exists("CONFIG.json")) return;

    File configFile = sd.open("CONFIG.json", FILE_READ);
    JsonDocument config;
    deserializeJson(config, configFile);
    
    deviceNumber = config["device number"]; 

    if (config["mode"]["name"] == "FR") {
        mode = Mode::FR;
        ratio = config["mode"]["ratio"];
    } else if (config["mode"]["name"] == "VI") {
        mode = Mode::VI;
        viAvg = config["mode"]["avg"];
        viSpread = config["mode"]["spread"];
    } else if (config["mode"]["name"] == "CHANCE") {
        mode = Mode::CHANCE;
        chance = config["mode"]["chance"];
    }

    if (config["active sensor"] == "left") {
        activeSensor = ActiveSensor::LEFT;
    } else if (config["active sensor"] == "right") {
        activeSensor = ActiveSensor::RIGHT;
    } else if (config["active sensor"] == "both") {
        activeSensor = ActiveSensor::BOTH;
    }

    leftReward = config["reward"]["left"];
    rightReward = config["reward"]["right"];
    if (config["reward"]["window"] == true) {
        feedWindow = true;
        windowStart = config["reward"]["time"]["start"];
        windowEnd = config["reward"]["time"]["end"];
    } else {
        feedWindow = false;
    }

    configFile.close();
}

void FED4::saveConfig() {
    sd.remove("CONFIG.json");
    File configFile = sd.open("CONFIG.json", FILE_WRITE);
    JsonDocument config;

    config["device number"] = 0;

    switch (mode) {
    case Mode::FR:
        config["mode"]["name"] = "FR";
        config["mode"]["ratio"] = ratio;
        break;

    case Mode::VI:
        config["mode"]["name"] = "VI";
        config["mode"]["avg"] = viAvg;
        config["mode"]["spread"] = viSpread;
        break;

    case Mode::CHANCE:
        config["mode"]["name"] = "CHANCE";
        config["mode"]["chance"] = chance;
    
    default:
        break;
    }

    switch (activeSensor) {
    case ActiveSensor::LEFT:
        config["active sensor"] = "left";
        break;

    case ActiveSensor::RIGHT:
        config["active sensor"] = "right";
        break;

    default:
        break;
    }

    config["reward"]["left"] = leftReward;
    config["reward"]["right"] = rightReward;
    if (feedWindow) {
        config["reward"]["window"] = true;
        config["reward"]["time"]["start"] = windowStart;
        config["reward"]["time"]["end"] = windowEnd;
    } else {
        config["reward"]["window"] = false;
    }
    
    serializeJson(config, configFile);
    configFile.close();
}

void FED4::startInterrupts() {
    noInterrupts();

    EIC->INTFLAG.reg = (1 << digitalPinToInterrupt(FED4Pins::LFT_POKE));
    EIC->INTFLAG.reg = (1 << digitalPinToInterrupt(FED4Pins::RGT_POKE));
    EIC->INTFLAG.reg = (1 << digitalPinToInterrupt(FED4Pins::WELL));
    rtcZero.attachInterrupt(alarmISR);
    __DSB();

    interrupts();
}

void FED4::pauseInterrupts() {
    noInterrupts();
    rtcZero.detachInterrupt();
}

void FED4::sleep() {
    sleepMode = true;
    __DSB();
    while(sleepMode) {
        __WFI();
    }
}

int FED4::getViCountDown() {
    int offset = (float)viAvg * viSpread;
    int lowerBound = viAvg - offset;
    int upperBound = viAvg + offset;
    return random(lowerBound, upperBound);
}

bool FED4::getLeftPoke() {
    if (leftPoke)
    {
        leftPoke = false;
        return true;
    }
    return false;
}

bool FED4::getRightPoke() {
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

void FED4::leftPokeHandler() {
    sleepMode = false;

    if (ignorePokes)
        return;

    if (digitalRead(FED4Pins::LFT_POKE) == LOW)
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
            .message = EventMsg::LEFT
        };
        logEvent(event);
        leftPokeStarted = false;
        leftPoke = true;
        dtLftPoke = 0;
    }
}

void FED4::rightPokeHandler() {
    sleepMode = false;

    if (ignorePokes)
        return;

    if (digitalRead(FED4Pins::RGT_POKE) == LOW)
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
            .message = EventMsg::RIGHT
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
        if (checkFeedingWindow()) {
            sleepMode = false;
        }

        updateDisplay(true);
        flushToSD();

        uint8_t alarmSeconds = rtcZero.getSeconds() + LP_AWAKE_PERIOD - 1;
        uint8_t alarmMinutes = rtcZero.getMinutes() + (alarmSeconds / 60);
        uint8_t alarmHours = rtcZero.getHours() + (alarmMinutes / 60);
        alarmSeconds = alarmSeconds % 60;
        alarmMinutes = alarmMinutes % 60;
        alarmHours = alarmHours % 24;
        rtcZero.setAlarmTime(alarmHours, alarmMinutes, alarmSeconds);

        startInterrupts();
    }
}

void FED4::feed(int pellets, bool wait) {
    if (jamError) return;

    pelletDropped = false;

    long startOfFeed;
    for (int i = 0; i < pellets; i++) {
        startOfFeed = millis();
        while (getWellStatus() == false)
        {
            long deltaT = millis() - startOfFeed;
            if (deltaT < 15000)
            {
                rotateWheel(1);
            }
            else if (deltaT < 30000)
            {
                rotateWheel(-3);
                rotateWheel(5);
            }
            else if (deltaT < 90000)
            {
                rotateWheel(-10);
                rotateWheel(15);
            }
            else {
                logError("Clogged or No Pellets");
                jamError = true;
                updateDisplay();
                return;
            }
        }
        pelletsDispensed++;
        Event event = {
            .time = getDateTime(),
            .message = EventMsg::PEL
        };
        logEvent(event);
        __delay(200);
    }

    leftPoke = false;
    rightPoke = false;

    updateDisplay();
}

void FED4::rotateWheel(int degrees) {
    digitalWrite(FED4Pins::MTR_EN, HIGH);

    int steps = (STEPS * degrees / 360);
    stepper.step(steps);

    digitalWrite(FED4Pins::MTR_EN, LOW);
}

void FED4::makeNoise(int duration) {
    for (int i = 0; i < duration / 50; i++)
    {
        tone(FED4Pins::BUZZER, random(50, 250), 50);
        __delay((uint32_t)(duration / 50));
    }
}

void FED4::print(String str, uint8_t size) {
    display.setTextSize(size);
    display.fillRect(2, 106, DISPLAY_W - 4, 16, WHITE);
    display.setTextColor(BLACK);
    display.setCursor(4, 106);
    display.print(str);
    display.refresh();
}

void FED4::initSD() {
    digitalWrite(FED4Pins::MTR_EN, LOW);

    while (!sd.begin(FED4Pins::CARD_SEL, SD_SCK_MHZ(4)))
    {
        showSdError();
    }
}

void FED4::showSdError() {
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

void FED4::initLogFile() {   
    digitalWrite(FED4Pins::MTR_EN, LOW);
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
    
    logFile.open(fileName, FILE_WRITE);
    logFile.createContiguous(fileName, FILE_PREALLOC_SIZE);
    logFile.rewind();

    logFile.print("TimeStamp,");
    logFile.print("Battery,");
    logFile.print("Device Number,");
    logFile.print("Mode,");
    logFile.print("Event,");
    logFile.print("Active Sensor,");
    logFile.print("Left Reward,");
    logFile.print("Right Reward,");
    logFile.print("Left Poke Count,");
    logFile.print("Right Poke Count,");
    logFile.print("Pellet Count");

    switch (mode) {
    case Mode::VI:
        logFile.print(",VI Count Down");
        break;

    case Mode::FR:
        logFile.print(",Ratio");
        break;

    case Mode::CHANCE:
        logFile.print(",Chance");
        break;
    
    default:
        break;
    }

    logFile.print("\n");

    logFile.flush();
}

void FED4::flushToSD() {
    pauseInterrupts();

    if (logBufferPos == 0) {
        return;
    }

    digitalWrite(FED4Pins::CARD_SEL, LOW);
    digitalWrite(FED4Pins::SHRP_CS, HIGH);

    logFile.write(logBuffer, logBufferPos);
    logBufferPos = 0;

    logFile.truncate();
    char logFileName[30];
    logFile.getName(logFileName, 30);
    logFile.close();
    logFile.open(logFileName, FILE_WRITE);

    startInterrupts();
}

void FED4::writeToLog(char row[ROW_MAX_LEN], bool forceFlush) {
    pauseInterrupts();

    int rowLen = strlen(row);
    if(logBufferPos + rowLen >= FILE_RAM_BUFF_SIZE) {
        flushToSD();
        lastFlush = millis();
        return;
    }
    memcpy(&logBuffer[logBufferPos], row, rowLen);
    logBufferPos += rowLen;

    if ( (millis() - lastFlush > 50 * 1000) || forceFlush) {
        flushToSD();
    }

    startInterrupts();
}

void FED4::logEvent(Event e) {
    char row[ROW_MAX_LEN] = "";
    DateTime now = getDateTime();
    
    char date[20];
    sprintf(date, "%d/%d/%d ", now.day(), now.month(), now.year() % 1000);
    char  time[20];
    sprintf(time, "%d:%d:%d", now.hour(), now.minute(), now.second());
    strcat(row, date);
    strcat(row, time);
    strcat(row, ",");

    char battery_str[8];
    snprintf(battery_str, sizeof(battery_str), "%d", getBatteryPercentage());
    strcat(row, battery_str);
    strcat(row, ",");
    
    char deviceNumber_str[8];
    snprintf(deviceNumber_str, sizeof(deviceNumber_str), "%d", deviceNumber%100);
    strcat(row, deviceNumber_str);
    strcat(row, ",");

    char mode_str[10];
    switch (mode) {
    case Mode::FR:
        sprintf(mode_str, "FR");
        break;
        
    case Mode::VI:
        sprintf(mode_str, "VI");
        break;
        
    case Mode::CHANCE:
        sprintf(mode_str, "CHANCE");
        break;

    default :
        sprintf(mode_str, "OTHER");
        break;
    }
    strcat(row, ",");

    strcat(row, e.message);
    strcat(row, ",");

    char activeSensor_str[10];
    switch (activeSensor) {
    case ActiveSensor::BOTH:
        sprintf(activeSensor_str, "Both");
        break;
    
    case ActiveSensor::LEFT:
        sprintf(activeSensor_str, "Left");
        break;
    
    case ActiveSensor::RIGHT:
        sprintf(activeSensor_str, "Right");
        break;
    }
    strcat(row, activeSensor_str);
    strcat(row, ",");

    char leftReward_str[5];
    char rightReward_str[5];
    switch (activeSensor) {
    case ActiveSensor::LEFT:
        snprintf(leftReward_str, sizeof(leftReward_str), "%d", leftReward);
        snprintf(rightReward_str, sizeof(rightReward_str), "nan");
        break;
        
    case ActiveSensor::RIGHT:
        snprintf(leftReward_str, sizeof(leftReward_str), "nan");
        snprintf(rightReward_str, sizeof(rightReward_str), "%d", rightReward);
        break;
        
    case ActiveSensor::BOTH:
        snprintf(leftReward_str, sizeof(leftReward_str), "%d", leftReward);
        snprintf(rightReward_str, sizeof(rightReward_str), "%d", rightReward);
        break;    
    }
    strcat(row, leftReward_str);
    strcat(row, ",");
    strcat(row, rightReward_str);
    strcat(row, ",");
    
    char leftPokeCount_str[8];
    snprintf(leftPokeCount_str, sizeof(leftPokeCount_str), "%d", leftPokeCount);
    char rightPokeCount_str[8];
    snprintf(rightPokeCount_str, sizeof(rightPokeCount_str), "%d", rightPokeCount);
    strcat(row, leftPokeCount_str);
    strcat(row, ",");
    strcat(row, rightPokeCount_str);
    strcat(row, ",");
    
    char pelletsDispensed_str[8];
    snprintf(pelletsDispensed_str, sizeof(pelletsDispensed_str), "%d", pelletsDispensed);
    strcat(row, pelletsDispensed_str);
    
    switch (mode) {
    case Mode::VI:
        strcat(row, ",");
        char viCountDown_str[10];
        sprintf(viCountDown_str, "%d", viCountDown);
        strcat(row, viCountDown_str);
        break;

    case Mode::FR:
        strcat(row, ",");
        char ratio_str[10];
        sprintf(ratio_str, "%d", ratio);
        strcat(row, ratio_str);
        break;

    case Mode::CHANCE:
        strcat(row, ",");
        char chance_str[8];
        sprintf(chance_str, "%.2f", chance);
        strcat(row, chance_str);
        break;
    
    default:
        break;
    }
    
    strcat(row, "\n");
    
    writeToLog(row);
}

void FED4::drawDateTime() {
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

void FED4::drawBateryCharge() {
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

void FED4::drawStats() {
    display.setTextSize(2);

    display.fillRect(98, 24, 100, 80, WHITE);

    display.setTextSize(2);

    display.setCursor(100, 26);
    display.print(leftPokeCount);
    display.setCursor(100, 46);
    display.print(rightPokeCount);
    display.setCursor(100, 66);
    display.print(pelletsDispensed);

    if (mode == Mode::VI) {
        display.setCursor(100, 86);
        if (viCountDown >= 0) {
            display.print(viCountDown);
        }
    }
}

void FED4::displayLayout() {
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
    if (mode == Mode::VI)
    {
        display.setCursor(4, 86);
        display.print("VI CD: ");
    }

    display.setTextSize(1);
    display.setCursor(4, 124);
    if (mode == Mode::FR)
        display.print("Fixed Ratio");
    else if (mode == Mode::VI)
        display.print("Variable Interval");
    display.setCursor(4, 134);
    char logFileName[30];
    logFile.getName(logFileName, 30);
    display.print(logFileName);

    updateDisplay(true);
}

void FED4::updateDisplay(bool statusOnly) {
    pauseInterrupts();
    digitalWrite(FED4Pins::SHRP_CS, LOW);
    digitalWrite(FED4Pins::CARD_SEL, HIGH);
    
    if (!statusOnly) {
        drawStats();
    }
    
    drawDateTime();
    drawBateryCharge();

    if (jamError) {
        print("ERROR: JAM OR NO PELLETS");
    }

    display.refresh();

    startInterrupts();
}

void FED4::runConfigMenu() {
    ignorePokes = true;

    Menu *modeMenu = new Menu(9);

    Menu *clockMenu = new ClockMenu(modeMenu);

    modeMenu->items[0] = new MenuItem((char *)"Time", clockMenu);
    modeMenu->items[1] = new MenuItem((char *)"Dev no", (int*)&deviceNumber, 0, 99, 1);

    const char *modes[] = {"FR", "VI", "%"};
    modeMenu->items[2] = new MenuItem((char *)"Mode", modes, 3);
    modeMenu->items[2]->valueIdx = mode;

    const char *sensors[] = {"L", "R", "L&R"};
    modeMenu->items[3] = new MenuItem((char *)"Sensor", sensors, 3);
    modeMenu->items[3]->valueIdx = activeSensor;

    modeMenu->items[4] = new MenuItem((char *)"L Rew", (int*)&leftReward, 0, 255, 1);
    modeMenu->items[5] = new MenuItem((char *)"R Rew", (int*)&rightReward, 0, 255, 1);

    modeMenu->items[6] = new MenuItem((char*)"Rew Win", &feedWindow);
    modeMenu->items[7] = new MenuItem((char*)"Rew Beg", (int*)&windowStart, 0, 23, 1);
    modeMenu->items[8] = new MenuItem((char*)"Rew End", (int*)&windowEnd, 0, 23, 1);

    int batteryLevel = getBatteryPercentage();
    runMenu(modeMenu, batteryLevel);

    activeSensor = modeMenu->items[3]->valueIdx;

    mode = modeMenu->items[2]->valueIdx;
    if(mode > 2) {
        mode = Mode::OTHER;
    }

    delete(modeMenu);
    ignorePokes = false;
}

void FED4::runViMenu() {
    ignorePokes = true;

    Menu *menu = new Menu(2);

    menu->items[0] = new MenuItem((char*)"Avg T", (int*)&viAvg, 0, 120, 5);
    menu->items[1] = new MenuItem((char*)"Spread", &viSpread, 0.0, 1.0, 0.05);

    runMenu(menu);
    
    delete(menu);
    ignorePokes = false;
}

void FED4::runFrMenu() {
    ignorePokes = true;

    Menu *menu = new Menu(1);

    menu->items[0] = new MenuItem((char*)"Ratio", (int*)&ratio, 1, 10, 1);

    runMenu(menu);

    delete(menu);
    ignorePokes = false;
}

void FED4::runChanceMenu() {
    ignorePokes = true;

    Menu *menu = new Menu(1);

    menu->items[0] = new MenuItem((char*)"Chance", &chance, 0.0, 1.0, 0.1);

    runMenu(menu);

    delete(menu);
    ignorePokes = false;
}

int FED4::getBatteryPercentage() {
    pauseInterrupts();
    float batteryVoltage = analogRead(FED4Pins::VBAT);
    startInterrupts();

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
    pauseInterrupts();

    DateTime now = rtc.now();

    startInterrupts();
    return now;
}

void FED4::leftPokeIRS() {
    if (instance) {
        instance->leftPokeHandler();
    }
}

void FED4::rightPokeIRS() {
    if (instance) {
        instance->rightPokeHandler();
    }
}

void FED4::wellISR() {
    if (instance) {
        instance->wellHandler();
    }
}

void FED4::alarmISR() {
    if (instance) {
        instance->alarmHandler();
    }
}

void FED4::dateTime(uint16_t *date, uint16_t *time) {
    DateTime now = instance->getDateTime();
    *date = FAT_DATE(now.year(), now.month(), now.day());
    *time = FAT_TIME(now.hour(), now.minute(), now.second());
}
