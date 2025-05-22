#ifndef AI_H
#define AI_H

// Bu header'da Drone veya Survivor tipine doğrudan ihtiyaç yok,
// çünkü ai_controller argümanı void* ve içinde cast ediliyor.
// Fonksiyon prototipleri için de spesifik tiplere gerek yok.
// Ama okunabilirlik için eklenebilir.
// #include "drone.h"
// #include "survivor.h"

// AI Mission Assignment
void* ai_controller(void *args);
// Drone* find_closest_idle_drone(Coord target_coord); // Prototipi eklenebilir, ai.c içinde static değilse.
// void assign_mission(Drone *drone, Survivor *survivor); // Prototipi eklenebilir.

#endif