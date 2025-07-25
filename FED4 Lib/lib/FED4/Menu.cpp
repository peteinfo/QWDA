#include "Menu.h"

Adafruit_SharpMem *menu_display;
RTC_PCF8523 *menu_rtc;

void drawMenu(Menu *menu, int batteryLevel = -1);

void debPrint(int n) {
    menu_display->clearDisplay();
    menu_display->setTextColor(BLACK);
    menu_display->setCursor(20, 20);
    menu_display->print(n);
    menu_display->refresh();
    while(1){};
}

    void debPrint(const char* str) {
    menu_display->clearDisplay();
    menu_display->setTextColor(BLACK);
    menu_display->setCursor(20, 20);
    menu_display->print(str);
    menu_display->refresh();
    while(1){};
}

void debPrint(float f) {
    menu_display->clearDisplay();
    menu_display->setTextColor(BLACK);
    menu_display->setCursor(20, 20);
    menu_display->print(f);
    menu_display->refresh();
    while(1){};
}

void debPrint() {
    menu_display->clearDisplay();
    menu_display->setTextColor(BLACK);
    menu_display->setCursor(20, 20);
    menu_display->print("got here");
    menu_display->refresh();
    while(1){};
}

MenuItem::MenuItem(char* name, int* value, int min, int max, int step) {
    const char* const_name = name;

    this->name = const_name;
    this->type = ItemType::ITEM_T_INT;
    this->value = value;
    this->minValue = new int(min);
    this->maxValue = new int(max);
    this->step = new int(step);
}

MenuItem::MenuItem(char* name, float* value, float min, float max, float step){
    const char* const_name = name;

    this->name = const_name;
    this->type = ItemType::ITEM_T_FLOAT;
    this->value = value;
    this->minValue = new float(min);
    this->maxValue = new float(max);
    this->step = new float(step);
}

MenuItem::MenuItem(char *name, const char** list, int listLen) {
    const char* const_name = name;

    this->name = const_name;
    this->type = ItemType::ITEM_T_LIST;
    this->value = (void*)list[0];
    this->valueIdx = 0;
    this->list = list;
    this->listLen = listLen;
}

MenuItem::MenuItem(char* name, bool* value) {
    const char* const_name = name;

    this->name = const_name;
    this->type = ItemType::ITEM_T_BOOL;
    this->value = (void*)value;
}

MenuItem::MenuItem(char *name, Menu *submenu) {
    const char* cont_name = name;

    this->name = cont_name;
    this->type = ItemType::ITEM_T_SUBMENU;
    this->submenu = submenu;
}

MenuItem::~MenuItem() {
    switch (type) {
    case ITEM_T_INT:
        delete (int*)minValue;
        delete (int*)maxValue;
        delete (int*)step;
        break;
    
    case ITEM_T_FLOAT:
        delete (float*)minValue;
        delete (float*)maxValue;
        delete (float*)step;
        break;

    case ITEM_T_SUBMENU:
        delete submenu;
        break;

    default: // ITEM_T_BOOL & ITEM_T_LIST
        break;
    }
}

const char** initList(int listLen) {
    const char** list = (const char**)malloc(listLen * sizeof(void*));
    return list;
}

void increaseInt(MenuItem *item) {
    int newValue = *(int*)item->value + *(int*)item->step;
    if (newValue > *(int*)item->maxValue) {
        newValue = *(int*)item->maxValue;
    }
    *(int*)item->value = newValue;
}

void decreaseInt(MenuItem *item) {
    int newValue = *(int*)item->value - *(int*)item->step;
    if (newValue < *(int*)item->minValue) {
        newValue = *(int*)item->minValue;
    }
    *(int*)item->value = newValue;
}

void increaseFloat(MenuItem *item) {
    float newValue = *(float*)item->value + *(float*)item->step;
    if (newValue > *(float*)item->maxValue) {
        newValue = *(float*)item->maxValue;
    }
    *(float*)item->value = newValue;
}

void decreaseFloat(MenuItem *item) {
    float newValue = *(float*)item->value - *(float*)item->step;
    if (newValue < *(float*)item->minValue) {
        newValue = *(float*)item->minValue;
    }
    *(float*)item->value = newValue;
}

void changeBool(MenuItem *item) {
    bool newValue = !*(bool*)item->value;
    *(bool*)item->value = newValue;
}

void nextList(MenuItem *item) {
    int newIdx = (item->valueIdx + 1) % item->listLen;
    item->value = (void*)item->list[newIdx];
    item->valueIdx = newIdx;
}

void previousList(MenuItem *item) {
    int newIdx = (item->valueIdx - 1) % item->listLen;
    item->value = (void*)item->list[newIdx];
    item->valueIdx = newIdx;
}

Menu::Menu(int itemNo) : Menu::Menu(nullptr, itemNo) {
}

Menu::Menu(Menu *parent, int itemNo) {
    MenuItem** items = new MenuItem*[itemNo];

    this->parent = parent;
    this->type = MENU_T_LIST;
    this->items = items;
    this->itemNo = itemNo;
    this->selectedItem = this->items[0];
    this->selectedIdx = 0;
}

ClockMenu::ClockMenu(Menu *parent) : Menu::Menu(parent, 5) {
    int *day = new int;
    int *month = new int;
    int *year = new int;
    int *hour = new int;
    int *minute = new int;

    MenuItem *day_itm = new MenuItem((char*)"day", day, 1, 31, 1);
    MenuItem *month_itm = new MenuItem((char*)"month", month, 1, 12, 1);
    MenuItem *year_itm = new MenuItem((char*)"year", year, 2000, 2099, 1);
    MenuItem *hour_itm = new MenuItem((char*)"hour", hour, 0, 23, 1);
    MenuItem *minute_itm = new MenuItem((char*)"minute", minute, 0, 59, 1);

    this->type = MENU_T_CLOCK;
    this->items[0] = day_itm;
    this->items[1] = month_itm;
    this->items[2] = year_itm;
    this->items[3] = hour_itm;
    this->items[4] = minute_itm;
}

Menu::~Menu(){
    for (int i = 0; i < itemNo; i++) {
        delete(items[i]);
    }
    delete[] items;
}

void handleRightBtn(Menu *menu) {
    switch (menu->selectedItem->type) {
    case ItemType::ITEM_T_INT:
        increaseInt(menu->selectedItem);
        break;

    case ItemType::ITEM_T_FLOAT:
        increaseFloat(menu->selectedItem);
        break;

    case ItemType::ITEM_T_LIST:
        nextList(menu->selectedItem);
        break;

    case ItemType::ITEM_T_BOOL:
        changeBool(menu->selectedItem);
        break;

    case ItemType::ITEM_T_SUBMENU:
        runMenu(menu->selectedItem->submenu);
        drawMenu(menu);
        break;

    default:
        break;
    }
}

void handleLeftBtn(Menu *menu) {
    switch (menu->selectedItem->type) {
        case ItemType::ITEM_T_INT:
        decreaseInt(menu->selectedItem);
        break;
        
        case ItemType::ITEM_T_FLOAT:
        decreaseFloat(menu->selectedItem);
        break;

        case ItemType::ITEM_T_BOOL:
        changeBool(menu->selectedItem);
        break;

        case ItemType::ITEM_T_LIST:
        previousList(menu->selectedItem);
        break;

        default:
        break;
    }
}

void printValue(MenuItem* item) {
    ItemType _type = item->type;
    switch (_type) {
    case ITEM_T_INT: {
        int value = *(int*)item->value;
        menu_display->print(value);
        break;
    }
    case ITEM_T_FLOAT: {
        float value = *(float*)item->value;
        menu_display->print(value);
        break;
    }
    case ITEM_T_BOOL: {
        bool value = *(bool*)item->value;
        if (value == true) {
            menu_display->print("YES");
        }
        else {
            menu_display->print("NO");
        }
        break;
    }
    case ITEM_T_LIST: {
        item->value = (void*)item->list[item->valueIdx];
        char *value = (char*)item->value;
        menu_display->print(value);
        break;
    }
    case ITEM_T_SUBMENU: {
        menu_display->print(">");
        break;
    }
    }
}

void drawListMenu(Menu* menu) {
    if (menu->selectedIdx >= 5) {
        menu_display->drawLine(84 - 20, 10, 84, 4, BLACK);
        menu_display->drawLine(84 + 20, 10, 84, 4, BLACK);
        menu_display->drawLine(84 - 20, 11, 84, 5, BLACK);
        menu_display->drawLine(84 + 20, 11, 84, 5, BLACK);
    }

    int offset = menu->selectedIdx - 4;
    if (offset < 0) {
        offset = 0;
    }
    int lastItem = 5;
    if (menu->itemNo < 5) {
        lastItem = menu->itemNo;
    }
    for (int i = 0; i < lastItem; i++) {
        int y_pos = START_Y + i * ROW_HEIGHT;
        menu_display->setCursor(COL_1_X, y_pos);
        int itemIdx = i + offset;
        if (itemIdx > menu->itemNo) {
            itemIdx = menu->itemNo - 1;
        }
        menu_display->print(menu->items[itemIdx]->name);
        menu_display->print(":");
        menu_display->setCursor(COL_2_X, y_pos);
        printValue(menu->items[itemIdx]);
    }

    if (menu->selectedIdx < 5 && menu->itemNo > 5) {
        menu_display->drawLine(84-20, 110, 84, 116, BLACK);
        menu_display->drawLine(84+20, 110, 84, 116, BLACK);
        menu_display->drawLine(84-20, 111, 84, 117, BLACK);
        menu_display->drawLine(84+20, 111, 84, 117, BLACK);
    }
}

void setClock(Menu *menu) {
    MenuItem *day = menu->items[0];
    MenuItem *month = menu->items[1];
    MenuItem *year = menu->items[2];
    MenuItem *hour = menu->items[3];
    MenuItem *minute = menu->items[4];

    *(int*)day->value = menu_rtc->now().day();
    *(int*)month->value = menu_rtc->now().month();
    *(int*)year->value = menu_rtc->now().year();
    *(int*)hour->value = menu_rtc->now().hour();
    *(int*)minute->value = menu_rtc->now().minute();
}

void drawClockMenu(Menu *menu) {
    MenuItem *day = menu->items[0];
    MenuItem *month = menu->items[1];
    MenuItem *year = menu->items[2];
    MenuItem *hour = menu->items[3];
    MenuItem *minute = menu->items[4];

    menu_display->setCursor(20, 40);
    if (*(int*)day->value < 10) menu_display->print("0");
    printValue(day);

    menu_display->setCursor(45, 40);
    menu_display->print("/");

    menu_display->setCursor(60, 40);
    if (*(int*)month->value < 10) menu_display->print("0");
    printValue(month);

    menu_display->setCursor(90, 40);
    menu_display->print("/");

    menu_display->setCursor(105, 40);
    printValue(year);

    menu_display->setCursor(50, 70);
    if (*(int*)hour->value < 10) menu_display->print("0");
    printValue(hour);

    menu_display->setCursor(78, 70);
    menu_display->print(":");
    
    menu_display->setCursor(90, 70);
    if (*(int*)minute->value < 10) menu_display->print("0");
    printValue(minute);
}

void drawBattery(int batteryLevel) {
    menu_display->fillRect(10, 125, 37, 14, WHITE);

    menu_display->drawRect(15, 127, 31, 12, BLACK);
    menu_display->fillRect(12, 130, 3, 6, BLACK);

    if (batteryLevel > 75) {
        menu_display->fillRect(17, 129, 6, 8, BLACK);
    }
    if (batteryLevel > 50) {
        menu_display->fillRect(17 + 7, 129, 6, 8, BLACK);
    }
    if (batteryLevel > 25) {
        menu_display->fillRect(17 + 14, 129, 6, 8, BLACK);
    }
    if (batteryLevel > 10) {
        menu_display->fillRect(17 + 21, 129, 6, 8, BLACK);
    }
    if (batteryLevel <= 10) {
        menu_display->setTextSize(2);
        menu_display->fillRect(25, 126, 12, 16, WHITE);
        menu_display->setCursor(26, 126);
        menu_display->print("!");
    }
}

void drawMenu(Menu *menu, int batteryLevel) {
    menu_display->clearDisplay();
    menu_display->setTextColor(BLACK);
    menu_display->setTextSize(2);

    if (menu->type == MENU_T_LIST) {
        drawListMenu(menu);
    }
    else if (menu->type == MENU_T_CLOCK) {
        drawClockMenu(menu);
    }

    menu_display->drawLine(5, 120, 163, 120, BLACK);

    if (batteryLevel != -1) {
        drawBattery(batteryLevel);
    }

    if (menu->parent != nullptr) {
        menu_display->setCursor(COL_1_X - 4, 124);
        menu_display->print(" Back");
    } else{
        menu_display->setCursor(COL_2_X, 124);
        menu_display->print("Done");
    }

    menu_display->refresh();
}

void drawListSelection(Menu* menu) {
    int x = COL_2_X - 2;
    int y;
    y = START_Y + menu->selectedIdx * ROW_HEIGHT - 2;
    if (menu->selectedIdx >= 5) {
        y = START_Y + 4 * ROW_HEIGHT - 2;
    }
    menu_display->fillRect(x, y, 65, ROW_HEIGHT, BLACK);
    y = START_Y + menu->selectedIdx * ROW_HEIGHT;
    if (menu->selectedIdx >= 5) {
        y = START_Y + 4 * ROW_HEIGHT;
    }
    menu_display->setCursor(COL_2_X, y);
    if (menu->selectedItem->type != ITEM_T_SUBMENU) {
        menu_display->print("<");
    }
    printValue(menu->selectedItem);
    if (menu->selectedItem->type != ITEM_T_SUBMENU) {
        menu_display->print(">");
    }
}

void drawClockSelection(Menu* menu) {
    MenuItem *selection = menu->selectedItem;

    if (menu->selectedIdx == 0) {
        menu_display->fillRect(18, 35, 30, 22, BLACK);
        menu_display->setCursor(20, 40);
        if (*(int*)selection->value < 10) menu_display->print("0");
        printValue(selection);
    }
    else if (menu->selectedIdx == 1) {
        menu_display->fillRect(58, 35, 30, 22, BLACK);
        menu_display->setCursor(60, 40);
        if (*(int*)selection->value < 10) menu_display->print("0");
        printValue(selection);
    }
    else if (menu->selectedIdx == 2) {
        menu_display->fillRect(103, 35, 50, 22, BLACK);
        menu_display->setCursor(105, 40);
        printValue(selection);
    }
    else if (menu->selectedIdx == 3) {
        menu_display->fillRect(48, 65, 28, 22, BLACK);
        menu_display->setCursor(50, 70);
        if (*(int*)selection->value < 10) menu_display->print("0");
        printValue(selection);
    }
    else if (menu->selectedIdx == 4) {
        menu_display->fillRect(88, 65, 28, 22, BLACK);
        menu_display->setCursor(90, 70);
        if (*(int*)selection->value < 10) menu_display->print("0");
        printValue(selection);
    }
}

void drawSelection(Menu *menu) {
    menu_display->setTextSize(2);
    menu_display->setTextColor(WHITE);

    if (menu->selectedIdx == SEL_BACK) {
        menu_display->fillRect(COL_1_X - 6, 122, 64, 20, BLACK);
        menu_display->setCursor(COL_1_X - 4, 124);
        menu_display->print("<Back");
    }
    else if (menu->selectedIdx == SEL_DONE) {
        menu_display->fillRect(COL_2_X - 2, 122, 64, 20, BLACK);
        menu_display->setCursor(COL_2_X, 124);
        menu_display->print("Done>");
    }
    else 
    
    if (menu->type == MENU_T_LIST) {
        drawListSelection(menu);
    }
    else if (menu->type == MENU_T_CLOCK) {
        drawClockSelection(menu);
    }

    menu_display->refresh();
}

typedef enum {
    I_LEFT,
    I_RIGHT,
    I_BOTH,
    I_MISS
} InputType;

bool inputDetected() {
    if (digitalRead(LEFT_POKE) == LOW || digitalRead(RIGHT_POKE) == LOW) {
        return true;
    }
    return false;
}

InputType getInput(int sameLastInputs) {
    const int firstDelay_ms = 400;

    int delay_ms = firstDelay_ms;
    if (sameLastInputs >= 5 && sameLastInputs < 10) delay_ms /= 2;
    if (sameLastInputs >= 10) delay_ms /= 8;

    while(!inputDetected()) { }
    delay(delay_ms);
    
    InputType input = I_MISS;
    if ( 
        digitalRead(LEFT_POKE) == LOW
        && digitalRead(RIGHT_POKE) == HIGH
    ) {
        input = I_LEFT;
    }
    else if (
        digitalRead(LEFT_POKE) == HIGH
        && digitalRead(RIGHT_POKE) == LOW
    ) {
        input = I_RIGHT;
    }
    else if (
        digitalRead(LEFT_POKE) == LOW 
        && digitalRead(RIGHT_POKE) == LOW 
    ) {
        input = I_BOTH;
    }

    return input;
}

long lastInputMillis = 0;
void runMenu(Menu *menu, int batteryLevel) {
    if (menu->type == MENU_T_CLOCK) {
        setClock(menu);
    }

    menu->selectedIdx = 0;
    menu->selectedItem = menu->items[0];
    menu_display->clearDisplay();
    drawMenu(menu, batteryLevel);
    drawSelection(menu);
    menu_display->refresh();

    InputType lastInput = I_MISS;
    int sameLastInputs = 0;
    while (true) {
        InputType input = getInput(sameLastInputs);
        long dt = millis() - lastInputMillis;
        lastInputMillis = millis();
        if (input == lastInput && dt <= 700) {
        sameLastInputs++;
        }
        else {
        sameLastInputs = 0;
        }


        if (input == I_LEFT) {
        if (menu->selectedIdx == SEL_BACK) {
            break;
        }
        handleLeftBtn(menu);
        }
        else if (input == I_RIGHT) {
        if (menu->selectedIdx == SEL_DONE) {
            break;
        }
        handleRightBtn(menu);
        }
        else if (input == I_BOTH) {
        if (menu->selectedIdx == SEL_BACK || menu->selectedIdx == SEL_DONE) {
            menu->selectedIdx = 0;
        }
        else {
            menu->selectedIdx++;
        }

        if (menu->selectedIdx >= menu->itemNo) {
            if (menu->parent != nullptr) {
            menu->selectedIdx = SEL_BACK;
            }
            else {
            menu->selectedIdx = SEL_DONE;
            }
        }

        if (menu->selectedIdx != SEL_BACK && menu->selectedIdx != SEL_DONE) {
            menu->selectedItem = menu->items[menu->selectedIdx];
        }

        menu_display->clearDisplay();
        drawMenu(menu);
        }
        if (input != I_MISS) {
        drawSelection(menu);
        menu_display->refresh();
        }
        
        lastInput = input;
    }
    
    if (menu->type == MENU_T_CLOCK) {
        DateTime now = DateTime(
        *(int*)menu->items[2]->value - 2000,
        *(int*)menu->items[1]->value,
        *(int*)menu->items[0]->value,
        *(int*)menu->items[3]->value,
        *(int*)menu->items[4]->value
        );
        menu_rtc->adjust(now);
    }
}