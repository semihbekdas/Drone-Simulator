#ifndef SURVIVOR_H
#define SURVIVOR_H

#include "coord.h"
#include <time.h>
// #include "list.h" // List.h survivor.h içinde include edilmemeli, circular dependency olabilir.
                     // Eğer Survivor içinde List* yoksa gerek yok.
                     // Globals.h zaten List.h'ı içerecek.

typedef enum { WAITING = 0, ASSIGNED = 1, HELPED = 2 } SurvivorState;

typedef struct survivor {
    SurvivorState status;
    Coord coord;
    struct tm discovery_time; // tm struct'ı zaten time.h ile gelir.
    struct tm helped_time;
    char info[25];
} Survivor;

// Global survivor lists (extern)
// extern List *survivors;          // Bu global değişken tanımları globals.h'de olmalı.
// extern List *helpedsurvivors;    // Survivor.h sadece Survivor tipi ve fonksiyon prototiplerini içermeli.
// Bu extern'leri globals.h'ye taşıyacağız.

// Functions
Survivor* create_survivor(Coord *coord, char *info, struct tm *discovery_time);
void *survivor_generator(void *args);
// void survivor_cleanup(Survivor *s); // Prototipi güncelleyebilir veya kaldırabiliriz.

#endif