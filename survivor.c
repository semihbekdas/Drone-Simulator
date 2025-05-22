#include "headers/survivor.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "headers/globals.h" // map, survivors listeleri için
#include "headers/map.h"     // map için (dolaylı yoldan globals.h'den de gelebilir ama açıkça eklemek iyi)
// list.h globals.h içinde olduğundan tekrar include etmeye gerek yok.

/**
 * @brief Creates a new survivor object.
 * @param coord Coordinates of the survivor.
 * @param info Information about the survivor.
 * @param discovery_time Time of discovery.
 * @return Pointer to the newly created Survivor object, or NULL on failure.
 */
Survivor *create_survivor(Coord *coord, char *info, struct tm *discovery_time) {
    Survivor *s = malloc(sizeof(Survivor));
    if (!s) {
        perror("Failed to allocate memory for Survivor");
        return NULL;
    }
    // memset(s, 0, sizeof(Survivor)); // Gerekli değil, tüm alanlar atanacak.

    s->coord = *coord;
    if (discovery_time) { // Null check
        memcpy(&s->discovery_time, discovery_time, sizeof(struct tm));
    } else {
        // discovery_time NULL ise, mevcut zamanı ata veya bir hata logla
        time_t t_now;
        time(&t_now);
        localtime_r(&t_now, &s->discovery_time);
    }
    memset(&s->helped_time, 0, sizeof(struct tm)); // Başlangıçta helped_time boş.

    strncpy(s->info, info, sizeof(s->info) - 1);
    s->info[sizeof(s->info)-1] = '\0'; // Null terminate
    
    s->status = WAITING; // Initial status
    
    return s;
}

/**
 * @brief Thread function to periodically generate survivors.
 */
void *survivor_generator(void *args) {
    (void)args; // Unused parameter
    time_t t;
    struct tm current_discovery_time;

    // srand(time(NULL)); // srand bir kere main'de çağrılmalı, her thread'de değil.
                         // Eğer main'de çağrılmıyorsa, buraya eklenebilir ama bir kerelik olmalı.
                         // En iyisi controller.c (main) içinde çağırmak.

    printf("Survivor generator thread started.\n");

    while (1) {
        // Harita boyutlarını globals.h üzerinden map nesnesinden alıyoruz.
        // map.height ve map.width'in initialize edildiğinden emin olmalıyız.
        if (map.height <= 0 || map.width <= 0) {
            fprintf(stderr, "Error: Map dimensions are not initialized in survivor_generator.\n");
            sleep(5); // Hata durumunda bekleyip tekrar dene.
            continue;
        }
        Coord coord = { rand() % map.height, rand() % map.width };
        
        char info[25];
        snprintf(info, sizeof(info), "SURV-%04d", rand() % 10000);
        
        time(&t);
        localtime_r(&t, &current_discovery_time);

        // Survivor nesnesini oluştur (heap'te)
        Survivor *new_survivor = create_survivor(&coord, info, &current_discovery_time);
        if (!new_survivor) {
            fprintf(stderr, "Survivor creation failed in generator. Skipping.\n");
            continue; // malloc başarısız olursa atla
        }

        // Listeye Survivor* adresini ekle.
        // list->add fonksiyonu, verilen adresteki veriyi (Survivor*) kendi içine kopyalar (memcpy ile).
        // Bu yüzden &new_survivor (Survivor**) gönderiyoruz.
        // `survivors` listesi global olduğu için doğrudan erişilebilir.
        if (survivors->add(survivors, &new_survivor) == NULL) {
            fprintf(stderr, "Failed to add new survivor to main 'survivors' list.\n");
            free(new_survivor); // Eklenemeyen survivor'ı free etmeliyiz.
            continue;
        }
        
        // Harita hücresindeki listeye de aynı Survivor* pointer'ını ekle.
        // map.cells[coord.x][coord.y].survivors listesinin var olduğundan emin olmalıyız.
        if (coord.x < map.height && coord.y < map.width && map.cells[coord.x][coord.y].survivors) {
            if (map.cells[coord.x][coord.y].survivors->add(
                map.cells[coord.x][coord.y].survivors, &new_survivor) == NULL) {
                fprintf(stderr, "Failed to add new survivor to map cell list at (%d,%d).\n", coord.x, coord.y);
                // Hata durumu: Ana listeden de çıkarmak gerekebilir, bu durum tutarsızlığa yol açabilir.
                // Şimdilik, ana listeden çıkarmayı deneyelim. Bu işlem atomik olmalı normalde.
                // Basitlik adına, eğer map listesine eklenemezse, ana listeden çıkarıp free edelim.
                // Bu, removedata'nın doğru implementasyonuna bağlıdır.
                if (survivors->removedata(survivors, &new_survivor) == 0) {
                     // Ana listeden başarıyla çıkarıldıysa logla.
                    printf("Survivor %s removed from main list due to map cell add failure.\n", new_survivor->info);
                } else {
                    // Ana listeden çıkarılamadıysa (zaten yoksa veya hata olduysa) logla.
                    fprintf(stderr, "Survivor %s could not be removed from main list after map cell add failure.\n", new_survivor->info);
                }
                free(new_survivor); // Her durumda free et.
                continue;
            }
        } else {
            fprintf(stderr, "Error: Map cell or cell survivor list not available for coord (%d,%d).\n", coord.x, coord.y);
            // Ana listeden çıkar ve free et.
            survivors->removedata(survivors, &new_survivor); // Sonucu kontrol etmesek de olur, zaten free edilecek.
            free(new_survivor);
            continue;
        }
         
        printf("[Survivor Gen] New survivor: %s at (%d,%d). Total in main list: %d\n",
               new_survivor->info, coord.x, coord.y, survivors->number_of_elements);
        fflush(stdout); // Buffer'ı hemen yazdır.
        
        // Eski log: printf("New survivor at (%d,%d): %s\n", coord.x, coord.y, info);
        // Bu, üsttekiyle aynı bilgiyi veriyor, kaldırılabilir.
        
        sleep(rand() % 2 + 1); // Rastgele 2-4 saniye bekle
    }
    return NULL;
}


/*
// Bu fonksiyonun mevcut bellek yönetim stratejimizle (Survivor** için malloc yok)
// ve merkezi temizlik (main sonunda) ile konsepti değişti.
// Eğer belirli bir survivor'ı anında ve kontrollü bir şekilde silmek gerekiyorsa
// Survivor* alan ve ilgili listelerden çıkaran bir fonksiyon yazılabilir.
// Şimdilik yorumda bırakıyorum.

void survivor_cleanup(Survivor *s) { // Argüman Survivor* olmalı artık.
    if (!s) return;

    // 1. map.cells[s->coord.x][s->coord.y].survivors listesinden çıkar
    // Bu işlem için listeye s'nin adresini (Survivor**) göndermek gerekebilir,
    // eğer liste hala Survivor* tutuyorsa.
    // map.cells[s->coord.x][s->coord.y].survivors->removedata(
    //     map.cells[s->coord.x][s->coord.y].survivors, &s
    // );

    // 2. Ana 'survivors' veya 'helpedsurvivors' listesinden çıkar (durumuna bağlı)
    //    Bu genellikle zaten görev tamamlanınca veya AI atama yapamayınca oluyor.
    //    Eğer bu fonksiyon genel bir temizlik içinse, listelerden çıkarmak
    //    liste iterasyonu sırasında sorun yaratabilir.

    // 3. Survivor nesnesini free et.
    // printf("Cleaning up survivor: %s\n", s->info);
    // free(s);
}
*/