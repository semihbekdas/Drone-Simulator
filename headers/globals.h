#ifndef GLOBALS_H
#define GLOBALS_H

#include "map.h"
#include "drone.h"    // Güncellenmiş Drone tipi için
#include "survivor.h"
#include "list.h"
#include "coord.h"

// Global Değişkenlerin extern bildirimleri
extern Map map;
extern List *survivors;         // Yardım bekleyen survivor'lar (sunucu yönetir)
extern List *helpedsurvivors;   // Yardım edilmiş survivor'lar (sunucu yönetir)
extern List *drones;            // Bağlı olan aktif drone'ların (Drone* tipinde) listesi (sunucu yönetir)

#endif