#include <FED4.h>

FED4 fed4;
long lastLogTime = 0;

void setup() {
    Serial.begin(9600);

    fed4 = FED4();

    fed4.begin();

    lastLogTime = fed4.getDateTime().unixtime();
    Event event = {
        .time = fed4.getDateTime(),
        .message = "Bat Log"
    };
}


void loop() {
    fed4.run();
}