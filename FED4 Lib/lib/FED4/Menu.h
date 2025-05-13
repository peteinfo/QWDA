#ifndef MENU_H
#define MENU_H

#include <Arduino.h>
#include <RTClib.h>
#include <Adafruit_SharpMem.h>

extern Adafruit_SharpMem *menu_display;
extern RTC_PCF8523 *menu_rtc;

#define BLACK 0
#define WHITE 1

#define LEFT_POKE 6
#define RIGHT_POKE 5

#define COL_1_X     10
#define COL_2_X     100
#define START_Y     15
#define ROW_HEIGHT  20

#define SEL_DONE    -1
#define SEL_BACK    -2

typedef enum {
  ITEM_T_INT,
  ITEM_T_FLOAT,
  ITEM_T_LIST,
  ITEM_T_SUBMENU
} ItemType;

typedef enum {
  MENU_T_LIST,
  MENU_T_CLOCK
} MenuType;

typedef struct MenuItem MenuItem;
typedef struct Menu Menu;

typedef struct MenuItem {
  const char *name;
  ItemType type;

  void *value;

  void *minValue;
  void *maxValue;
  void *step;

  int valueIdx;
  const char **list;
  int listLen;

  Menu *submenu;
} MenuItem;
MenuItem* initItem(char* name, int* value, int min, int max, int step);
MenuItem* initItem(char* name, float* value, float min, float max, float step);
MenuItem* initItem(char *name, const char** list, int listLen);
MenuItem* initItem(char *name, Menu *submenu);
void freeMenuItem(MenuItem *item);
const char** initList(int listLen);
void increaseInt(MenuItem *item);
void decreaseInt(MenuItem *item);
void increaseFloat(MenuItem *item);
void decreaseFloat(MenuItem *item);
void nextList(MenuItem *item);
void previousList(MenuItem *item);


typedef struct Menu {
  Menu *parent;
  MenuType type;
  MenuItem **items;
  int itemNo;
  MenuItem *selectedItem;
  int selectedIdx;
} Menu;
Menu* initMenu(Menu *parent, int itemNo);
Menu* initClockMenu(Menu *parent);
void freeMenu(Menu *menu);
void handleRightBtn(Menu *item);
void handleLeftBtn(Menu *item);
void runMenu(Menu *menu, int batteryLevel = -1);

#endif