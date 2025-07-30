#include <FED4.h>

FED4 fed4;
long lastLogTime = 0;

#ifdef __arm__
// should use uinstd.h to define sbrk but Due causes a conflict
extern "C" char* sbrk(int incr);
#else  // __ARM__
extern char *__brkval;
#endif  // __arm__

int freeMemory() {
  char top;
#ifdef __arm__
  return &top - reinterpret_cast<char*>(sbrk(0));
#elif defined(CORE_TEENSY) || (ARDUINO > 103 && ARDUINO != 151)
  return &top - __brkval;
#else  // __arm__
  return __brkval ? &top - __brkval : &top - __malloc_heap_start;
#endif  // __arm__
}

int init_freeMem = 0;

void setup() {
    Serial.begin(9600);

    fed4 = FED4();

    fed4.begin();

    // pinMode(FED4Pins::LFT_POKE, INPUT_PULLUP);
    // pinMode(FED4Pins::RGT_POKE, INPUT_PULLUP);
    // pinMode(FED4Pins::WELL, INPUT_PULLUP);

    // menu_display = &fed4.display;
    // menu_rtc = &fed4.rtc;

    // Menu* menu = makeMenu();
    // menu->run();

    // delete menu;
}


void loop() {
    // int ocupiedMem = init_freeMem - freeMemory();
    // Serial.println();

    fed4.run();
}