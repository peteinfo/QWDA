#include <FED4.h>

FED4 fed4;
long lastLogTime = 0;
int init_freeMem = 0;

void setup() {
    Serial.begin(9600);

    fed4 = FED4();

    fed4.begin();
}


void loop() {
    fed4.run();
}