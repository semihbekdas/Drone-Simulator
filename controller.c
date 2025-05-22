#include "headers/globals.h"   // Global değişken tanımları (map, lists)
#include "headers/map.h"       // init_map, freemap prototipleri
#include "headers/drone.h"     // initialize_drones, cleanup_drones prototipleri
#include "headers/survivor.h"  // survivor_generator prototipi
#include "headers/ai.h"        // ai_controller prototipi
#include "headers/list.h"      // create_list, destroy prototipleri (globals.h'den de gelebilir)
#include "headers/view.h"      // SDL fonksiyon prototipleri

#include <stdio.h>
#include <stdlib.h>    // exit, EXIT_FAILURE için
#include <pthread.h>
#include <signal.h>    // Opsiyonel: Sinyal yakalama için
#include <time.h>      // time, srand için

// Global Değişkenlerin Tanımlanması (globals.c yerine burada da olabilir, ama globals.c daha organize)
// Eğer globals.c kullanıyorsak, bu tanımlar orada olmalı ve burada sadece extern edilmeli (globals.h aracılığıyla).
// Şimdilik, projenin ilk halindeki gibi globals.c'de tanımlandığını ve globals.h ile extern edildiğini varsayıyorum.
// Bu yüzden burada tekrar tanımlamaya GEREK YOK.
// Map map;
// List *survivors = NULL;
// List *helpedsurvivors = NULL;
// List *drones = NULL;

// Opsiyonel: Sinyal işleyici fonksiyon
volatile sig_atomic_t program_running = 1;

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\nSignal %d received, initiating shutdown...\n", signum);
        program_running = 0;
        // SDL event kuyruğuna QUIT event'i ekleyerek ana döngüyü sonlandırabiliriz.
        SDL_Event quit_event;
        quit_event.type = SDL_QUIT;
        SDL_PushEvent(&quit_event);
    }
}


int main(int argc, char *argv[]) {
    (void)argc; (void)argv; // Kullanılmayan parametreler

    // Rastgele sayı üreteci için seed ayarla (programda bir kere)
    srand(time(NULL));

    // Opsiyonel: SIGINT (Ctrl+C) ve SIGTERM sinyallerini yakala
    // struct sigaction sa;
    // sa.sa_handler = signal_handler;
    // sigemptyset(&sa.sa_mask);
    // sa.sa_flags = 0; // SA_RESTART gerekebilir bazı durumlarda
    // sigaction(SIGINT, &sa, NULL);
    // sigaction(SIGTERM, &sa, NULL);

    printf("Emergency Drone Coordination System - Phase 1 Simulator Starting...\n");

    // Global listeleri oluştur
    // create_list(data_boyutu, kapasite)
    // Listeler Survivor* ve Drone* saklayacak.
    survivors = create_list(sizeof(Survivor*), 100); // Maks 100 bekleyen survivor
    if (!survivors) { fprintf(stderr, "Failed to create 'survivors' list.\n"); exit(EXIT_FAILURE); }
    
    helpedsurvivors = create_list(sizeof(Survivor*), 500); // Maks 500 yardım edilmiş survivor (log için)
    if (!helpedsurvivors) { fprintf(stderr, "Failed to create 'helpedsurvivors' list.\n"); /*önceki listeyi destroy et*/ exit(EXIT_FAILURE); }
    
    drones = create_list(sizeof(Drone*), 50); // Maks 50 drone
    if (!drones) { fprintf(stderr, "Failed to create 'drones' list.\n"); /*öncekileri destroy et*/ exit(EXIT_FAILURE); }

    printf("Global lists created.\n");

    // Haritayı initialize et (listelerden sonra veya önce olabilir, bağımlılığı yok gibi)
    init_map(20, 30); // Örnek boyutlar: 20 yükseklik, 30 genişlik
    printf("Map initialized.\n");

    // Drone'ları initialize et (drone thread'lerini spawn eder)
    // num_drones drone.c içinde tanımlı, initialize_drones map'i kullanıyor.
    initialize_drones(); 
    printf("Drones initialized and threads started.\n");

    // Survivor generator thread'ini başlat
    pthread_t survivor_gen_thread_id;
    if (pthread_create(&survivor_gen_thread_id, NULL, survivor_generator, NULL) != 0) {
        perror("Failed to create survivor_generator thread");
        // Temizlik yap ve çık
        exit(EXIT_FAILURE);
    }
    printf("Survivor generator thread started.\n");
 
    // AI controller thread'ini başlat
    pthread_t ai_controller_thread_id;
    if (pthread_create(&ai_controller_thread_id, NULL, ai_controller, NULL) != 0) {
        perror("Failed to create ai_controller thread");
        // Temizlik yap ve çık
        exit(EXIT_FAILURE);
    }
    printf("AI controller thread started.\n");

    // SDL görselleştirme ana thread'de çalışsın
    if (init_sdl_window() != 0) {
        fprintf(stderr, "Failed to initialize SDL window. Exiting.\n");
        // Temizlik...
        exit(EXIT_FAILURE);
    }
    printf("SDL window initialized. Starting main event loop.\n");

    // Ana SDL event döngüsü
    // `program_running` sinyal ile kontrol edilebilir veya check_events() SDL_QUIT döndürebilir.
    while (program_running && !check_events()) { // check_events() 1 dönerse (QUIT) döngü biter.
        draw_map();      // Haritayı çiz
        SDL_Delay(100);  // CPU kullanımı için küçük bir gecikme (10 FPS)
    }

    printf("Exiting main loop. Initiating cleanup...\n");
    program_running = 0; // Diğer thread'lerin de sonlanması için bir işaret olabilir (eğer kullanıyorlarsa)

    // Thread'leri sonlandır
    printf("Cancelling survivor_generator thread...\n");
    if (pthread_cancel(survivor_gen_thread_id) != 0) perror("Error cancelling survivor_gen_thread");
    else pthread_join(survivor_gen_thread_id, NULL); // Bitmesini bekle
    printf("Survivor_generator thread finished.\n");

    printf("Cancelling ai_controller thread...\n");
    if (pthread_cancel(ai_controller_thread_id) != 0) perror("Error cancelling ai_controller_thread");
    else pthread_join(ai_controller_thread_id, NULL); // Bitmesini bekle
    printf("AI_controller thread finished.\n");

    cleanup_drones(); // Drone thread'lerini iptal eder, mutex/cond'ları destroy eder, drone_fleet'i free eder.
    printf("Drones cleanup finished.\n");

    // Bellek Temizliği
    // 1. Yardım edilmiş survivor'ları temizle (helpedsurvivors listesindeki Survivor nesneleri)
    printf("Cleaning up 'helpedsurvivors' list objects...\n");
    if (helpedsurvivors) {
        pthread_mutex_lock(&helpedsurvivors->lock);
        Node *node = helpedsurvivors->head;
        while (node != NULL) {
            Survivor *s = *(Survivor**)node->data; // node->data Survivor* içerir
            if (s) {
                // printf("Freeing helped survivor: %s\n", s->info);
                free(s); // Asıl Survivor nesnesini free et
            }
            Node *nextNode = node->next; // Önce next'i sakla, çünkü node free_list'e dönebilir
            // helpedsurvivors->removenode(helpedsurvivors, node); // Bu şekilde listeden de çıkarılabilir
                                                              // ama destroy zaten tüm listeyi temizleyecek.
                                                              // Eğer destroy node'ları free_list'e ekliyorsa,
                                                              // bu veriyi de temizlemez, sadece node'u.
                                                              // Bu yüzden elle free etmek önemli.
            node = nextNode;
        }
        pthread_mutex_unlock(&helpedsurvivors->lock);
        helpedsurvivors->destroy(helpedsurvivors); // Listenin kendi yapısını free et
        helpedsurvivors = NULL;
        printf("'helpedsurvivors' list destroyed.\n");
    }

    // 2. Bekleyen/atanmış ama yardım edilmemiş survivor'ları temizle (survivors listesi)
    printf("Cleaning up remaining 'survivors' list objects...\n");
    if (survivors) {
        pthread_mutex_lock(&survivors->lock);
        Node *node = survivors->head;
        while (node != NULL) {
            Survivor *s = *(Survivor**)node->data;
            if (s) {
                // printf("Freeing unhelped survivor: %s\n", s->info);
                free(s);
            }
            node = node->next; // Sadece iterate ediyoruz, destroy tüm listeyi halledecek.
        }
        pthread_mutex_unlock(&survivors->lock);
        survivors->destroy(survivors);
        survivors = NULL;
        printf("'survivors' list destroyed.\n");
    }
    
    // 3. Drones listesini temizle (içindeki Drone*lar drone_fleet ile yönetiliyordu, drone_fleet cleanup_drones'da free edildi)
    if (drones) {
        drones->destroy(drones); // Sadece liste yapısını free eder
        drones = NULL;
        printf("'drones' list structure destroyed.\n");
    }

    // 4. Haritayı ve harita hücrelerindeki survivor listelerini temizle
    // freemap fonksiyonu, hücrelerdeki listelerin destroy'unu yapmalı.
    // Hücrelerdeki listelerdeki Survivor*lar zaten yukarıda (survivors veya helpedsurvivors üzerinden) free edildi.
    // Bu yüzden freemap sadece liste yapılarını temizlemeli, içindeki Survivor*ları tekrar free ETMEMELİ.
    freemap();
    printf("Map freed.\n");

    // SDL kaynaklarını serbest bırak
    quit_all();
    printf("SDL resources freed.\n");

    printf("Shutdown complete. Exiting program.\n");
    return 0;
}