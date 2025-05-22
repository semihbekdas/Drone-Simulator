# Acil Durum Drone Koordinasyon Sistemi

Bu proje, acil durumlarda drone'ların merkezi bir sunucu aracılığıyla koordine edilerek kazazedelere yardım ulaştırmasını simüle eden bir istemci-sunucu uygulamasıdır. Sistem, gerçek dünya drone sürü koordinasyon sistemlerini yansıtmak için thread senkronizasyonu, thread-safe veri yapıları ve ağ iletişimi kullanmaktadır.

## Proje Özeti

Acil Durum Drone Koordinasyon Sistemi, aşağıdaki bileşenlerden oluşmaktadır:

- **Merkezi Sunucu**: Drone'ların durumlarını takip eder, kurtarılacak kişileri yönetir ve görev ataması yapar.
- **Drone İstemcileri**: Sunucuya bağlanarak görevleri yerine getiren otonom birimlerdir.
- **Görüntüleyici İstemci**: Sistemin genel durumunu gerçek zamanlı olarak görselleştiren bir izleme aracıdır.
- **AI Kontrolcü**: Drone'lara görev atama mantığını yöneten bir bileşendir.

## Özellikler

- Thread-safe çift yönlü bağlı liste veri yapısı
- TCP/IP üzerinden JSON formatında iletişim
- SDL kullanarak gerçek zamanlı görselleştirme
- Çoklu thread senkronizasyonu (mutex, koşul değişkenleri)
- Dinamik drone ve kurtarılacak kişi yönetimi
- Heartbeat mekanizması ile bağlantı durumu izleme
- Free list optimizasyonu ile bellek yönetimi

## Gereksinimler

- C Derleyicisi (GCC önerilir)
- POSIX Thread Kütüphanesi (pthread)
- SDL2 Kütüphanesi (görselleştirme için)
- json-c Kütüphanesi (JSON işlemleri için)

## Kurulum

```bash
# Gerekli kütüphaneleri yükleyin
sudo apt-get update
sudo apt-get install build-essential libsdl2-dev libjson-c-dev

# Projeyi derleyin
make all
```

## Kullanım

### Sunucuyu Başlatma

```bash
./server
```

### Drone İstemcilerini Başlatma

```bash
./drone_client 1  # 1 numaralı drone'u başlatır
./drone_client 2  # 2 numaralı drone'u başlatır
# İstenilen sayıda drone başlatılabilir
```

### Görüntüleyici İstemciyi Başlatma

```bash
./viewer_client
```

## Proje Yapısı

```
.
├── headers/                # Başlık dosyaları
│   ├── ai.h               # AI kontrolcü tanımları
│   ├── connection_handling.h # Bağlantı işleme tanımları
│   ├── coord.h            # Koordinat yapısı tanımları
│   ├── drone.h            # Drone yapısı ve fonksiyonları
│   ├── globals.h          # Global değişkenler
│   ├── list.h             # Thread-safe liste veri yapısı
│   ├── map.h              # Harita yapısı ve fonksiyonları
│   ├── survivor.h         # Kurtarılacak kişi yapısı ve fonksiyonları
│   └── view.h             # Görselleştirme fonksiyonları
├── drone_client/
    ├── drone_client.c         # Drone istemci uygulaması
├── ai.c                   # AI kontrolcü implementasyonu
├── connection_handling.c  # Bağlantı işleme implementasyonu
├── controller.c           # Ana kontrol modülü
├── drone.c                # Drone fonksiyonları implementasyonu
├── globals.c              # Global değişkenler implementasyonu
├── list.c                 # Thread-safe liste implementasyonu
├── map.c                  # Harita fonksiyonları implementasyonu
├── server.c               # Sunucu uygulaması
├── survivor.c             # Kurtarılacak kişi fonksiyonları implementasyonu
├── view.c                 # Görselleştirme fonksiyonları implementasyonu
├── viewer_client.c        # Görüntüleyici istemci uygulaması
├── Makefile               # Derleme kuralları
├── communication-protocol.md # İletişim protokolü dokümantasyonu
└── README.md              # Bu dosya
```

## İletişim Protokolü

Sistem, TCP/IP üzerinden JSON formatında mesajlar kullanarak iletişim kurar. Temel mesaj tipleri şunlardır:

- **HANDSHAKE**: Drone'un sunucuya ilk bağlantısı
- **STATUS_UPDATE**: Drone'un periyodik durum güncellemeleri
- **MISSION_COMPLETE**: Görev tamamlama bildirimi
- **ASSIGN_MISSION**: Sunucudan drone'a görev ataması
- **HEARTBEAT**: Bağlantı durumu kontrolü

Detaylı protokol bilgisi için [communication-protocol.md](communication-protocol.md) dosyasına bakınız.

## Tasarım Seçimleri

### Mimari Tasarım

Sistem, merkezi bir sunucu etrafında organize edilmiş çoklu istemci mimarisi kullanmaktadır. Bu mimari, gerçek dünya drone sürülerinin koordinasyonunu doğru bir şekilde yansıtmak için seçilmiştir.

### Veri Yapıları

Sistemin temel veri yapısı olarak thread-safe çift yönlü bağlı liste kullanılmıştır. Bu liste, drone'ları, kurtarılacak kişileri ve görüntüleyicileri yönetmek için kullanılır. Bellek yönetimini optimize etmek için "free list" yaklaşımı uygulanmıştır.

### Senkronizasyon

Sistem, veri tutarlılığını sağlamak ve yarış koşullarını önlemek için mutex kilitleri ve koşul değişkenleri kullanır. Deadlock'ları önlemek için kilit sıralaması, kısa kritik bölgeler ve timeout kullanımı gibi stratejiler uygulanmıştır.

## Performans Özellikleri

- **Free List Optimizasyonu**: Bellek tahsisi ve serbest bırakma işlemlerinin maliyetini azaltır.
- **Contiguous Memory Allocation**: Bellek erişim desenlerini optimize eder ve cache locality'yi artırır.
- **TCP Socket Optimizasyonları**: SO_REUSEADDR ve TCP_NODELAY seçenekleri ile ağ performansı iyileştirilmiştir.
- **Non-Blocking I/O**: select() kullanılarak I/O işlemlerinin etkin yönetimi sağlanmıştır.
- **Önbellek Kullanımı**: Görüntüleyici istemcide, sunucudan gelen verilerin önbelleğe kaydedilmesi performansı artırır.


