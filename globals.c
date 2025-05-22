/* globals.c */
#include "headers/globals.h"

// Global Değişkenlerin Tanımlanması
Map map; // Henüz initialize edilmedi, init_map ile edilecek.
List *survivors       = NULL;
List *helpedsurvivors = NULL;
List *drones          = NULL; // Bağlı drone'ları tutacak liste