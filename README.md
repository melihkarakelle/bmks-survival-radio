# UV-K5 Firmware Projesi

Quansheng UV-K5 telsiz için özel firmware geliştirme. Başlangıç: 2026-10-29.

## Bizim telsiz

| | |
|---|---|
| Stok firmware | `k5_2.01.26` (kablodan okundu) |
| Bootloader | `2.00.06` (flash modunda `0x0518` paketinden okundu) |
| Donanım | **V1** — MCU **DP32G030** (V3/UV-K1 PY32F071 kullanır, onun firmware'i farklı!) |
| Kablo | CH340 USB-seri (`0x1a86:0x7523`), port `/dev/cu.usbserial-140` |
| EEPROM yedeği | `backups/ORIGINAL_stock_2.01.26_eeprom.bin` (8 KB, salt okunur) |
| SHA-256 | `5d6f87270826802d9d7ad4df4042bc44d1f6b4965317c816a429a5b155beb317` |

Kalibrasyon `0x1E00–0x1FFF` aralığında, dolu. Bu yedeği **asla silme**: kalibrasyon telsize özgü.

## Klasör

```
README.md             bu dosya (donanım + protokol özeti)
backups/              EEPROM yedekleri
builds/               derlenmiş firmware (.bin ham, .packed.bin flash için)
tools/k5link.py       kendi okuyucumuz: sürüm + EEPROM dump (sadece okur)
tools/build-f4hwn.sh  F4HWN Bandscope'u Docker'da derler -> builds/
tools/k5flash.py      kendi flasher'ımız: ham .bin'i bootloader modunda yükler
toolchain/            Arm GCC 10.3 (x86_64, Rosetta) + 14.2 (arm64), dl/ = arşivler
refs/                 indirilen referans repolar (dokunma, sadece oku)
.venv/                python (pyserial, crcmod)
```

Komutlar:
- `.venv/bin/python tools/k5link.py --dump backups/eeprom_YYYYMMDD.bin`
- `tools/build-f4hwn.sh`

## Derleme (2026-10-29)

| Derleyici | Sonuç |
|---|---|
| Arm GCC 10.3.1 (`toolchain/gcc-arm-none-eabi-10.3-2021.10`) | F4HWN için **olmaz**: Makefile `-Oz` kullanıyor, GCC 12+ ister. DualTachyon/egzumer için uygun. |
| Arm GCC 14.2.1 (`toolchain/arm-gnu-toolchain-14.2.rel1-...`) | Derler ama Bandscope **224 bayt taşar** (Arm newlib-nano daha büyük). |
| Alpine 3.22 Docker, GCC 15.1.0 (resmi yöntem) | **Başarılı.** Image adı `uvk5-f4hwn`, colima ile. |

F4HWN v4.3 Bandscope (commit `fbcf26d`, 2025-12-02):
- `builds/f4hwn.bandscope.bin`: 61 216 bayt, 60 KB sınırında **224 bayt boş**.
  SHA-256 `1f42ef2193f97e8aea43816d409220f7e607170a54ec8674fe081bbf571dc28b`
- `builds/f4hwn.bandscope.packed.bin`: 61 234 bayt, sürüm etiketi `*F4HWN v4.3` (0x2000'de).
  SHA-256 `1246def1e0a001d1f7d676e5d670baf754d02f8c9399d85351f0aeb520b3c14c`
- Kontroller: vektör tablosu SP=0x20003ff0, Reset=0x10b. Paketi çözünce ham .bin ile birebir aynı.

## Yükleme kaydı

| Tarih | Firmware | Sonuç |
|---|---|---|
| 2026-10-29 13:44 | F4HWN v4.3 Bandscope (`builds/f4hwn.bandscope.bin`) | 240/240 blok onaylandı, 23 s. Açılışta `F4HWN v4.3` cevap verdi. |

Yüklemeden sonraki EEPROM: `backups/eeprom_after_f4hwn_20261029_1345.bin`. Orijinale göre 3 bayt farklı:
`0x0E2D` (kanal öznitelik bayrağı) ve `0x1FF0–0x1FF1` (F4HWN ayar alanı, stokta 0xFF).
Kalibrasyon `0x1E00–0x1FEF` aynı kaldı.

Stok firmware'e dönüş: github.com/amnemonic/Quansheng_UV-K5_Firmware arşivinden `k5_2.01.26`
(henüz indirilmedi; packed ise önce çözülmeli) + `tools/k5flash.py`.

## Flash nasıl yapılır

1. Telsiz normal modda açıkken EEPROM yedeği al (`tools/k5link.py --dump`).
2. Telsizi kapat, **PTT basılı tutarak aç** (ekran boş, fener yanar).
3. `.venv/bin/python tools/k5flash.py builds/<dosya>.bin --dry-run` (dosya kontrolü)
4. `.venv/bin/python tools/k5flash.py builds/<dosya>.bin --yes`
5. Telsizi kapat-aç (PTT'siz). `tools/k5link.py` ile sürümü kontrol et.

`tools/k5flash.py` kendi flasher'ımız (protokol k5prog'dan): **ham `.bin`** alır, packed'i reddeder,
vektör tablosunu ve 0xF000 sınırını kontrol eder (bootloader 0xF000 üstünde, yazılmaz).

Notlar:
- Upstream `compile-with-docker.sh` kullanma: `docker system prune -f --volumes` çalıştırıyor, diğer Docker verilerini siler.
- Mac'te bir kez `make` yapıldıysa `.d` dosyaları image'a kopyalanır; script container içinde siler.
- Flash'ta yer çok dar (224 B). Yeni özellik eklerken başka bir `ENABLE_*` kapatmak gerekecek.

## Donanım özeti (V1)

| Parça | Görev | Bağlantı |
|---|---|---|
| DP32G030 | Cortex-M0, 48 MHz, 64 KB flash (60 KB uygulama), 16 KB RAM | — |
| BK4819 | RF transceiver 18–1300 MHz (630–840 MHz boşluk, donanım) | bit-bang 3-wire SPI: PC0 SCN, PC1 SCL, PC2 SDA |
| BK1080 | FM yayın alıcı | PB15 enable, I2C paylaşımlı |
| BL24C64 | 8 KB EEPROM (ayarlar, kanallar, kalibrasyon) | bit-bang I2C: PA10 SCL, PA11 SDA (klavye ile paylaşımlı) |
| ST7565 | 128×64 LCD | SPI0: PB7 SSN, PB8 CLK, PB10 MOSI, PB9 A0, PB11 RES (SWDIO ile paylaşımlı) |
| Klavye | matris | PA3–PA6 giriş, PA10–PA13 (I2C/ses çipi ile paylaşımlı) |
| UART1 | programlama kablosu (Kenwood 2-pin jack) | PA7 TX, PA8 RX, 38400 8N1 |
| ADC | pil gerilimi/akım | PA9 (CH4), PA14 (CH9) |
| Diğer | PB6 ekran ışığı, PC3 fener, PC4 ses yolu (hoparlör amfi), PC5 PTT | |
| SWD | PB14 SWCLK, PB11 SWDIO (test noktaları) | |

Kaynak: `refs/DualTachyon_uv-k5-firmware/driver/gpio.h`, `board.c`.
Şema (tersine mühendislik, KiCad): `refs/ludwich66_Quansheng_UV-K5_Wiki/hardware/KiCad/`
(`UVK5_reversing-70MHz_modv_20230702.pdf` + `UVK5_reversing_06-02-2024/`).
Datasheet'ler aynı `hardware/` klasöründe: DP32G030 referans manual (EN), BK4819 register list, BK1080, ST7565.
Blok diyagram: `hardware/Block_Diagram/`, interaktif PCB: `hardware/ibom.html`.

## Programlama yapısı

- **Bootloader** MCU ROM'unda değil, flash'ın başında; DP32G030 "flash masking" ile onu gizler.
  SWD ile rastgele adrese yazma bootloader'ı silebilir. Normal yol: seri bootloader.
- **Flash moduna giriş**: telsiz kapalı, **PTT basılı tutarak aç** → ekran boş, fener LED'i yanar.
  Bootloader `0x0518` mesajını tekrar tekrar yollar (içinde bootloader sürümü, ör. `2.00.06`).
- **Firmware dosyası**: `firmware.bin` (ham) → `fw-pack.py` ile şifrelenmiş `firmware.packed.bin`
  (resmi updater ve web flasher bunu ister). k5prog ham `.bin` alabilir.
- **Normal modda UART protokolü** (CHIRP de bunu kullanır):
  - çerçeve: `AB CD | len16 | XOR(payload+crc16) | DC BA`, XOR anahtarı 16 bayt (bkz. `tools/k5link.py`)
  - `0x0514` hello → `0x0515` sürüm; `0x051B` EEPROM oku → `0x051C`; `0x051D` EEPROM yaz → `0x051E`
  - CRC16-XMODEM; telsiz cevaplarında CRC yerine `0xFFFF` yollar.
- **Derleyici**: `arm-none-eabi-gcc` 10.3 önerilir (yenisi/eskisi binary'yi 60 KB sınırını aşacak kadar büyütebilir).
  Mac'te kurulu olan 8.2.1 (2018) — eski. Seçenek: Docker build (`compile-with-docker.sh`) veya Arm GNU toolchain.

## Referans firmware'ler (`refs/`)

| Repo | Ne |
|---|---|
| `DualTachyon_uv-k5-firmware` | Stok 2.1.27'nin açık kaynak yeniden yazımı. Temiz, öğrenmek için en iyisi. |
| `egzumer_uv-k5-firmware-custom` | OneOfEleven + fagci spektrum birleşimi. Çok kullanılan temel (son push 2024). |
| `armel_uv-k5-firmware-custom` | **F4HWN** — egzumer fork'u, en aktif V1 firmware'i. Bizim temel adayımız. |
| `armel_uv-k1-k5v3-firmware-custom` | F4HWN'in PY32F071 (V3/UV-K1) portu — **bizim telsize yükleme!** |
| `sq5bpf_k5prog` | C ile EEPROM oku/yaz + firmware flash aracı, protokol belgesi. |
| `qrp73_K5TOOL` | C# araç, V3 kurtarma prosedürü. |
| `ludwich66_Quansheng_UV-K5_Wiki` | Şema, PCB, datasheet, parça listesi. |

Web flasher (tarayıcıdan, Chrome Web Serial): egzumer / armel sayfalarında bağlantı var.

## Güvenlik kuralları

1. Flash'tan önce her zaman taze EEPROM yedeği al.
2. Sadece DP32G030 (V1) için derlenmiş firmware yükle.
3. İlk denemelerde hazır, bilinen-iyi bir `.packed.bin` yükle; kendi build'imizi sonra.
4. Geniş RX serbest; TX yalnızca lisanslı/izinli bantlarda (amatör lisans gerekir).

## Kaynaklar

- https://github.com/DualTachyon/uv-k5-firmware
- https://github.com/egzumer/uv-k5-firmware-custom
- https://github.com/armel/uv-k5-firmware-custom
- https://github.com/armel/uv-k1-k5v3-firmware-custom
- https://github.com/sq5bpf/k5prog
- https://github.com/qrp73/K5TOOL
- https://github.com/ludwich66/Quansheng_UV-K5_Wiki
- https://spectrum.ieee.org/quansheng-uv-k5-hacking

## BMKs Survival Radio (2026-10-29)

Kaynak: `firmware/` (F4HWN v4.3 fork, branch `bmk`), derleme: `EDITION=survival tools/build-f4hwn.sh "$PWD/firmware"`
→ `builds/bmks.survival.bin` (59 100 bayt, 2 340 bayt boş). Kod: `firmware/app/survival.c`.

Amaç: tüm basit iletişim kanallarını hem seçilebilir hem hızlı taranabilir yapmak. Mod = sabit kanal grubu.

| Mod | Kanal (MR) | İçerik | TX |
|---|---|---|---|
| PMR | 1–16 | PMR446 16 kanal, dar, LOW4 (~0.5 W) | **Açık** (varsayılan TxLock=PMR) |
| FRS | 17–38 | FRS/GMRS 22 kanal | Kilitli |
| MAR | 39–59 | Gemi 16 (imdat), 6, 67, 8–15, 17, 68–77, M1, M2 | Kilitli (gerçek imdatta menüden aç) |
| AIR | 60–65 | 121.5 GUARD, 243.0 MIL GUARD, 123.1 SAR, 123.45 AIR2AIR, 122.8 UNICOM, 129.975 GLIDING — **AM** | Yok (AM TX yok) |
| HAM | 66–68 | 145.500 2M CALL, 433.500 70CM CALL, 145.800 ISS | Kilitli |
| CB | 69–108 | CEPT 40 kanal (CB 9 = acil), FM dar. Stok antenle zayıf. | Kilitli |
| FM | — | BK1080 yayın radyosu | Yok (FM bandında yayın bilerek eklenmedi) |

Tuşlar (ilk açılışta ayarlanır):
- **Yan tuş 2 kısa**: sonraki mod (PMR → FRS → MAR → AIR → HAM → CB → FM radyo → PMR)
- **Yan tuş 2 uzun**, **F + \*** veya **\* uzun**: taramayı başlat/durdur. Tarama modun içinde kalır, aktivite olan kanalda **durur** (ScnRev = STOP).
- **Yukarı/aşağı**: modun içinde kanal değiştirir, başa sarar.
- **Yan tuş 1 kısa**: monitör (squelch aç), **uzun**: fener.
- FM radyoda: **F + \*** tüm bandı tarar, bulunan istasyonları kaydeder; yukarı/aşağı ile gezilir.

İlk açılış: EEPROM `0x1FF8` işareti (`BMKSRV\0\1`) yoksa kanal 1–108 ve ayarlar yazılır (~4 s boş ekran).
Menü → Reset ALL işareti siler, sonraki açılışta yeniden yazılır. Tablo değişirse işaretin son baytını artır.

Acil durumda TX: Menü → `TxLock` → `NONE` (ör. gemi kanal 16'dan MAYDAY). Normalde PMR446 dışında TX yasal değil.

Logo: **Codex** üretti (`design/codex/logo_src.png`), `tools/logo2c.py` ile 128×40 1-bit'e çevrildi
(`design/survival_logo.c`, önizleme `design/survival_logo_preview.png`), `firmware/bitmaps.c` içinde `BITMAP_SurvivalLogo`.

Yükleme kaydı: 2026-10-29 14:15 `bmks.survival.bin` 231/231 blok. Sonrası EEPROM: `backups/eeprom_after_survival_20261029_1416.bin`
(kalibrasyon aynı). Önceki kanallar: `backups/eeprom_before_bmk_20261029_1356.bin`.

Flash: DP32G030 64 KB, bootloader 4 KB, uygulamaya 60 KB. Survival kodu toplam ~1.8 KB (logo 640 B, tablolar ~600 B).
Yer gerekirse kullanılmayan F4HWN özellikleri (DTMF, scramble, 1750 Hz, AM fix, menü öğeleri) kapatılabilir.

### v1.1 (2026-10-29) — şu an telsizde

- Telsiz kendini `BMKs v1.1` olarak bildirir, ekranda F4HWN yazısı yok (radyo çekirdeği F4HWN/egzumer, Apache 2.0).
- Logo 4 sn (`boot_counter_10ms` survival'da 16 bit).
- Modlar: PMR446 → FRS → MARINE → AIR → HAM → CB → FM RADIO → **CUSTOM** (yan tuş 2 veya Menü → `Group`).
- **CUSTOM**: serbest frekans (VFO). Tuş takımından frekans gir; Menü → `CStart`/`CEnd` = tarama aralığı,
  `Step` = kanal aralığı. Taramada bu aralık adım adım taranır, aktivitede durur. Aralık EEPROM `0x1FE8`
  (2 × uint32), varsayılan 144.000–146.000 MHz.
- **TX izinleri** Menü → `TxPMR`, `TxFRS`, `TxMAR`, `TxHAM`, `TxCB` (varsayılan PMR + FRS açık). Maske EEPROM
  `0x1FFD`. CUSTOM modda da geçerli; kanal başına TX_LOCK biti yok sayılır. AIR TX yok (BK4819 AM gönderemez),
  FM yayın bandında TX yok (dar FM, yayın onaylı değil).
- Sade menü: Group, Sql, Power, Mode (FM/AM/USB), W/N, Step, Rx/Tx CTCS/DCS, TxODir, TxOffs, ScnRev, CStart, CEnd,
  Tx izinleri, ChSave/ChDele/ChName, F1/F2 tuşları, KeyLck, TxTOut, BatSav, BatTxt, Mic, BLTime, Beep, SysInf, Reset.
- **F + \*** ana ekranda artık kanal/frekans taraması (F4HWN'de CTCSS/DCS ton taramasıydı).
- **FM radyo listesi**: otomatik tarama eski listeyi baştan silmez; tarama bitince veya FM'den çıkınca en az bir istasyon
  bulduysa yeni listeyi kaydeder, bulamadıysa eskisini tutar. Tarama sırasında kapatılırsa eski liste kalır.
- EEPROM düzeni: `0x1FE8` CUSTOM aralığı, `0x1FF0` F4HWN, `0x1FF8` `BMKS` + tablo sürümü (2) + TX maskesi.
  Tablo sürümü artarsa ilk açılışta kanallar ve varsayılanlar yeniden yazılır.

### v1.2 (2026-10-29)

- Menü → **`TxAll`** (varsayılan kapalı): açıkken aşağıdaki yasaklı aralıklar dışında her frekansta TX serbest,
  band izinleri (`TxPMR`..`TxCB`) o sırada devre dışı. Maske biti 5.
- Yasaklı (TxAll açıkken de): 88.0–137.0 MHz (FM yayın + sivil havacılık), 225.0–400.0 MHz (askeri havacılık,
  243.0 dahil), 406.0–406.1 MHz (COSPAS-SARSAT beacon). Tablo: `firmware/app/survival.c` → `TX_BLOCKED`.
- Verici 136–174 / 400–520 MHz için ayarlı; dışında güç düşük, harmonik riski var.
- Tablo sürümü değişmedi, yüklemede ayarlar korunur.

### v1.3 (2026-10-29)

- MARINE 54 kanal: önceki 21 simpleks + 33 dupleks (1–5, 7, 18–28, 60–66, 78–86). Dupleks kanallar kıyı istasyonu
  frekansından dinler (sahil güvenlik MSI hava/fırtına uyarıları, liman, VTS), TX 4.6 MHz aşağıda gemi frekansına gider
  (`TxMAR` gerekir). 87/88 (AIS) ve 70 (DSC) dijital, eklenmedi.
- AIR 10 kanal: + London VOLMET Main 135.375, South 128.600, North 126.600, Scottish VOLMET 125.725 (AM, sürekli hava raporu).
- Kanal düzeni: PMR 1–16, FRS 17–38, MAR 39–92, AIR 93–102, HAM 103–105, CB 106–145. Tablo sürümü 3 (ilk açılışta yeniden yazılır).
- Karayolu trafik anonsları VHF'de yok, FM yayın istasyonlarında; BK1080 RDS-TA desteklemiyor.

### v1.4 (2026-10-29) — dünya geneli

Hedef: firmware dünyanın her yerinde anlamlı olsun, ülkeye özel kanal yok.

- MARINE 59 kanal: uluslararası simpleks 19 + dupleks 33 + **NOAA WX1–WX7** (ABD/Kanada hava durumu, sadece dinleme).
  İngiltere'ye özel M1/M2 çıkarıldı.
- AIR 5 kanal: GUARD 121.5, MIL GUARD 243.0, SAR 123.1, AIR2AIR 123.45, UNICOM 122.8. Yerel kule/ATIS/VOLMET → CUSTOM.
- HAM 5 kanal: 2M CALL EU 145.500, 2M CALL US 146.520, 70 CALL EU 433.500, 70 CALL US 446.000, ISS 145.800.
- TX bantları: HAM 144–148 ve 420–450 MHz, CB 26.565–27.99125 MHz. Örtüşen bantlarda herhangi bir izin yeterli.
- Ekran: ilk açılışta normal (negatif değil), kontrast 10, tuş kilidi PTT'yi kilitlemez. Menüye `Contr` eklendi.
  Neden: stok → F4HWN geçişinde EEPROM 0x1FF0 boş (FF) kalıyor, F4HWN bunu "ters ekran + kontrast 15" okuyor.
- Kanal düzeni: PMR 1–16, FRS 17–38, MAR 39–97, AIR 98–102, HAM 103–107, CB 108–147. Tablo sürümü 4.
- Flasher: v1.3 yüklemesinde blok 0 onayı geç geldi, tek blok tekrarı bootloader'ı bozdu (adres 0, durum 1 cevabı).
  `tools/k5flash.py` artık hatada tüm imajı baştan yazar, onay için 5 sn bekler. Telsiz bootloader'da kaldığı için
  ikinci denemede kurtarıldı.

### v1.5 (2026-10-29) — tuş düzeni

| Tuş | Ana ekran | FM ekranı |
|---|---|---|
| **M kısa** | sonraki mod (tarama sırasında da) | sonraki mod |
| **M uzun** | menü | istasyon kaydet/sil |
| **\* kısa** | taramayı başlat/durdur (DTMF girişi yok) | sonraki istasyonu ara |
| **\* uzun / F + \*** | tarama | F + \*: tüm bandı tara, listeyi kaydet |
| **F + 0** (veya 0 uzun) | FM radyo aç (taramayı durdurur) | FM'den çık |
| **F uzun** | tuş kilidi aç/kapa (PTT ve yan tuşlar kilitte de çalışır) | aynı |
| Yan tuş 1 | kısa: monitör, uzun: fener | |
| Yan tuş 2 | kısa: sonraki mod, uzun: tarama | |

FM + arka plan tarama yok: FM açıkken BK4819 sadece o anki kanalı dinler (konuşma gelirse FM susar, 5 sn sonra döner).
Tüm grubu FM çalarken taramak için ayrı geliştirme gerekir (v1.6 adayı).

### v1.6–v1.8 (2026-10-29) — FM + arka plan tarama

- FM çalarken BK4819 bir modu arka planda tarar (iki ayrı alıcı: BK1080 FM, BK4819 telsiz). Kanal değişirken
  hoparlör yolu kapatılmaz (`RADIO_SetupRegisters` survival'da FM+tarama sırasında `AUDIO_AudioPathOff` atlar).
- Menü → `FmScan`: OFF / PMR446 / FRS / MARINE / AIR / HAM / CB / **LAST MODE** (varsayılan). LAST = FM'e geçmeden
  önceki mod, o anki kanaldan devam; CUSTOM'dan gelindiyse CStart–CEnd aralığı. EEPROM 0x1FFE (0xFF = LAST).
- Konuşma yakalanınca: FM kapanır, telsiz o kanalda kalır (tarama biter). FM'e dönüş elle: F+0 veya M.
- FM ekranı: üstte küçük `FM 98.4`, satır 2 durum, çizgi, satır 4–5 arka plan (`SCAN PMR 5` / `WATCH MAR 16` + frekans),
  satır 6 bant.
- Hata (v1.6): `GUI_SelectNextDisplay` ekran değişince `gScanStateDir = SCAN_OFF` yapıyordu, FM ekranı açılınca tarama
  ölüyordu → v1.7'de FM arka plan taraması bu sıfırlamadan muaf.
- SQL 1 gürültüde açılıyor, taramayı her kanalda durduruyor. İlk açılış varsayılanı SQL 3 (v1.7).
- Ekran görüntüsü: `tools/k5screen.py out.png --seconds N [--sequence klasör]` (F4HWN screenshot, kablo takılıyken,
  sadece ekran değişince kare gelir). Görüntüler `design/screens/`.
- Flasher: v1.7 yüklemesinde blok 0 onayı yine gelmedi; baştan yazma ile ikinci denemede bitti.

### v1.9–v2.0 (2026-10-29)

- v1.9: FM ekranında bant aralığı (`87.5-108M`) çizginin üstünde, 3. satırda ortalı; arka plan bilgisi alt iki satırda.
- v2.0: **AIR = havacılık bandının tamamı.** Sabit 5 kanal kaldırıldı; AIR modu 108–137 MHz bandının kendi VFO kaydını
  kullanır: AM, 25 kHz adım (Step menüsünden 8.33 yapılabilir), tarama 118.000–136.975 MHz, ilk açılışta 121.500.
  Ana ekranda bant etiketi yerine `AIR`, CUSTOM'da `CUST` yazar. FM arka plan taraması AIR'den gelinirse bandı tarar.
- Kanal düzeni (tablo sürümü 5): PMR 1–16, FRS 17–38, MAR 39–97, HAM 98–102, CB 103–142.
- Ekran kaydıyla doğrulandı (`design/screens/air_v20_sheet.png`): AIR etiketi, AM, 25K, ScnRng 118–136.975,
  tarama ilerliyor; FM ekranında `SCAN AIR 123.12500 MHz`.

### v2.1 (2026-10-29)

- **Yukarı/aşağı basılı tut**: o yönde tarama başlar, aktivitede durur (tarama sırasında yönü değiştirir).
  Kısa basış eskisi gibi tek adım. FM ekranında basılı tutmak o yönde sonraki istasyonu arar.
- `*` artık kanal modunda da taramayı durdurur (F4HWN kanal modunda tarama listesini değiştiriyordu).

### v2.2 (2026-10-29)

- FM arka plan taraması konuşma yakalayınca artık ekran da bulunan kanala geçer. Hata: `gRequestDisplayScreen`
  sadece tuş işleme sonunda (`ProcessKey`) uygulanıyor; sinyal algılamada tuş olmadığından ekran FM'de kalıyordu.
  `SURVIVAL_FmBgFound` artık `GUI_SelectNextDisplay(DISPLAY_MAIN)` çağırıyor.

### v2.3–v2.4 (2026-10-29)

- v2.3: **Yan tuş 2 kısa = FM radyo aç/kapa** (mod mantığından geçer: taramayı durdurur, arka plan taramasını başlatır).
  Mod değiştirme M kısa. İlk açılış squelch 1 (Melih'in tercihi). Marker bayt 7 = ayar revizyonu: kanalları yeniden
  yazmadan mevcut telsizlere küçük ayar geçişi uygular (rev 1: yan tuş 2 = FM). Telsizde doğrulandı.
- v2.4: FM arka plan taramasında "tık tık" sesi giderildi. Neden: her kanal değişiminde `BK4819_RX_TurnOn`
  `REG_30 = 0` yazıp ses DAC'ını kapatıp açıyordu; FM için hoparlör yolu açık kaldığından tık duyuluyordu.
  Arka plan taramasında ses DAC'ı açık kalır (`gBK4819_KeepAfDac`), PLL/VCO yine yeniden başlatılır.

### v2.5 (2026-10-29)

- FM arka plan taraması sadece FM'e geçerken bir tarama çalışıyorsa devam eder. Tarama yokken FM'e geçilirse
  FM sadece çalar, BK4819 o anki kanalı dinler (`WATCH`); orada konuşma olursa yine kanala geçer.
- Tık sesi notu: Melih'in testi — anteni tutunca ya da gövdeyi bırakınca kesiliyor. Muhtemelen SQL1'de gürültünün
  squelch'i anlık açıp kapaması (kablo da gürültü taşıyabilir). Öneri: SQL2 ile dene; gerekirse FM arka plan
  taramasına ayrı (daha yüksek) squelch.

### v2.6 (2026-10-29)

- AIR ve CUSTOM ana ekranda adlı kanal gibi görünür: üstte kanal adı fontuyla **AIR BAND** / **CUSTOM**, altta büyük
  frekans; soldaki küçük etiket `VFO`. Tek VFO ekranında alt yarıyı kaplayan `ScnRng` yazısı çizilmez (aralık geçerli).
  FM ekranında arka plan satırı `SCAN AIR BAND` / `WATCH CUSTOM`. Ekran kaydı: `design/screens/air_v26_sheet.png`.

### v2.7 (2026-10-29) — hızlı tarama

- Her adımda önce ~20 ms beklenir, RSSI okunur (`app/chFrScanner.c`, `PrecheckEmpty`). Gürültü tabanından
  `4 + 2×SQL` yarım-dB (SQL1 = 3 dB, SQL3 = 5 dB) fazla değilse hemen sonraki adıma geçilir; fazlaysa kalan 70 ms
  beklenir, squelch karar verir. Taban boş adımlardan öğrenilir (EMA 1/8), her tarama başında sıfırlanır.
- Ekran taramada 4 adımda bir yenilenir (kısa beklemede LCD darboğaz olmasın). Ekran çizimi zaten bekleme süresi
  içinde yapılıyordu (bekleme zamanlayıcı kesmesiyle sayılıyor).
- Beklenen: 2–3 kat hız (AIR turu ~1,5 dk → ~30–45 sn). Eşik ayar isteyebilir: zayıf sinyaller atlanırsa margin düşür,
  gürültüde çok duruyorsa artır.

### v2.8–v3.0 (2026-10-29)

- v2.8/v2.9: AIR/CUSTOM tararken 10 kHz ve küçük haneler tire (`123.4----`); 100 kHz hanesi akar, ekran 4 adımda bir
  yenilendiği için diğerleri yanıltıcıydı. Durunca tam frekans. FM arka plan satırında da aynı.
- v3.0: F + yukarı/aşağı ile değiştirilen squelch artık kaydediliyor (F4HWN sadece RAM'de değiştiriyordu; her açılışta
  EEPROM'daki değer, v2.0 kurulumundan kalan 3, geri geliyordu).
- Temizlenebilir kod (ölçüm, v2.9): CTCSS/DCS kod tarayıcı 1.5 KB (erişilemiyor), menüden çıkan ~45 öğenin kodu ~2–2.5 KB,
  seçenek metinleri ~0.6 KB, küçük F4HWN özellikleri ~0.5–0.8 KB, DTMF kalıntısı ~0.2 KB → toplam ~4.5–5.5 KB.
  Kalmalı: RSSI çubuğu, AM fix, 1750 Hz; ekran görüntüsü geliştirme bitince çıkarılabilir.

### v3.1 (2026-10-29) — temizlik

- Derleme seçenekleri kapatıldı: mikrofon çubuğu, açılışta durum geri yükleme, otomatik kapanma, RX/TX sayacı,
  Kanada TX planı, kod tarama zaman aşımı (−1.6 KB).
- CTCSS/DCS kod tarayıcı stub (`app/scanner.c`, `ui/scanner.c`), F+4 ve menüdeki kod taraması bip verir (−3.0 KB).
- Survival menüsünde olmayan ~45 menü ID'sinin `case`'leri `#ifndef ENABLE_SURVIVAL` ile derleme dışı (−1.3 KB);
  normal F4HWN derlemesi aynı ağaçtan sorunsuz.
- Boş flash: 832 → **6796 bayt**. Ekran görüntüsü özelliği geliştirme için bırakıldı.
- Codex özellik önerileri: `design/codex/feature_ideas.md`.

### v3.2–v3.3 (2026-10-29) — yeni özellikler (Codex ile seçildi)

| Tuş / menü | Ne yapar |
|---|---|
| **Yukarı/aşağı kısa** (tararken) | Taramayı olduğu yerde durdurur. `*` da artık başa dönmeden olduğu yerde durdurur. |
| **F + 4** (veya 4 uzun) | **Son duyulanlar**: son 8 alım (ad/frekans, kaç dk önce). Yukarı/aşağı seç, **M** ile o kanala/frekansa git, EXIT çık. RAM'de, kapatınca silinir. Kod tarayıcının boşalan ekran slotunu kullanır. |
| **F + 5** (veya 5 uzun) | Bulunulan kanalı **öncelikli kanal (PRIO)** yap / kaldır. Kanal taramalarında (FAV ve FM arka plan dahil) 20 adımda bir PRIO'ya tam süre bakılır, aktivite varsa orada durur. AIR/CUSTOM bant taramasında yok. İşaret: kanalın scan list 1 biti (ekranda "1" simgesi). |
| **F + 7** (veya 7 uzun) | Bulunulan kanalı/frekansı **FAV**'a ekler (143–200). Aynısı varsa veya dolu ise çift bip. |
| **FAV modu** | Mod sırasında CUSTOM'dan sonra. Kendi kanalların ayrı bir grup gibi seçilir/taranır. Boşsa PMR'ye geçer. ChDele ile silinir. |
| Menü **Eco** | Açık: güç tasarrufu 1:5, arka ışık 10 sn, parlaklık 5/10. Kapalı: 1:4, 60 sn, 10/10. |

Düzeltmeler: gemi TX sadece gemi tarafında (156.000–157.425), kanal 70 (DSC) her durumda yasak; yasak aralıklar her
izinden önce kontrol edilir. Hızlı taramada gürültü tabanı daha düşük okumaya anında iner.
Tablo sürümü 6 (ilk açılışta kanallar yeniden yazılır, FAV alanı temizlenir). Boş flash 5548 bayt.

### v3.4 (2026-10-29)

- Menü **Reset → VFO** F4HWN'de kanal frekanslarını ve ayarları siliyor (adlar ve öznitelikler kalıyor). Survival işareti
  bu seçenekte silinmediği için telsiz bozuk kanallarla kalıyordu (Melih'in yaşadığı). Artık her iki Reset de işareti
  siler; sonraki açılışta kanallar ve varsayılanlar baştan yazılır. Kurtarma: Menü → Reset → ALL.

### v3.5 (2026-10-29)

- F+7, F+5 ve boş FAV artık ekranda 1,5 sn mesaj gösterir: `ADDED TO FAV` + ad, `ALREADY IN FAV`, `FAV FULL`,
  `PRIORITY` + ad, `PRIORITY OFF`, `CHANNELS ONLY / F+7: FAV FIRST`, `FAV EMPTY / F+7 ADDS`. Herhangi bir tuş kapatır.
  Mesaj, son duyulanlar listesiyle aynı (boşalan kod tarayıcı) ekran slotunu kullanır.
- Son duyulanlar başlığı `HEARD   M:GO`.
- Ekran kaydıyla doğrulandı: F+7 ile M143 `PMR 1` FAV'a eklendi, F+5 ile öncelikli (sağda "1" simgesi),
  mesaj `PRIORITY / PMR 1`, FAV modu mod sırasında görünüyor (`design/screens/msg_v35_sheet.png`).

### v3.6–v4.0 (2026-10-29)

- v3.6/v3.8: FAV kanallarında soldaki küçük etiket `M143` yerine kalın **FAV**.
- v3.7: Son duyulanlar listesinde M (git), EXIT ve mesaj kapatma tuş **bırakılınca** çalışır. Basınca çalışması,
  bırakma olayını ana ekrana gönderiyordu (M bırak = sonraki mod).
- v3.9: Gürültü tabanı sadece `taban − margin`'den düşük okumada hemen iner. v3.3'teki "her düşük okumaya in" tabanı
  gürültünün dibine oturtuyor, sıradan gürültü aday sayılıp tarama yavaşlıyordu.
- v4.0: Kanal taramalarında ekran her adımda yenilenir (4 adımda bir yenileme küçük gruplarla örtüşüyordu: 2 FAV
  kanalında hep aynısı görünüyordu). AIR/CUSTOM bant taramasında 4 adımda bir (kHz haneleri zaten tire).
- Flasher notu: bekleyen bir yükleyici durdurulacaksa önce logda "bootloader:" satırı olmadığını kontrol et
  (`python -u` ile log dosyasına yaz; pipe tamponlanıyor).

### v4.1 (2026-10-29)

- **F + 7 aç/kapa:** kanal/frekans FAV'da yoksa ekler (`ADDED TO FAV`), varsa çıkarır (`FAV REMOVED` + ad).
  FAV kanalının üstündeyken çıkarılırsa sonraki FAV kanalına, FAV boşaldıysa PMR'ye geçer. Silinen kanal öncelikliyse
  öncelik de kalkar. ChDele'ye gerek kalmadı.

## Repo yapısı (2026-10-29)

- Tek repo: `~/Documents/UV-K5`. README, `tools/`, `design/`, `backups/` (EEPROM yedekleri, kalibrasyon dahil),
  `builds/bmks.survival.bin` / `.packed.bin` ve **`firmware/`** (F4HWN v4.3 tabanlı kaynak).
- `firmware/` geçmişi korunarak eklendi: kök commit "F4HWN v4.3 upstream snapshot" (armel/uv-k5-firmware-custom
  fbcf26d, upstream geçmişi dahil değil), üstünde her Survival sürümü ayrı commit.
- Repoda olmayanlar, yeniden kurulabilir: `refs/` (indirilen referans repolar), `toolchain/` (Arm GCC arşivleri),
  `.venv/` (pyserial, crcmod, pillow), ara build dosyaları, ekran kaydı kareleri (`design/screens/seq/`).

## Arayüz v2 (Codex ile, design/ui_v2/)

### Aşama 1 — v4.2/v4.3: ana ekran, durum satırı, FM ekranı (telsizde, ekran kaydıyla doğrulandı)

- Yeni `firmware/ui/survival.c`; F4HWN'in ana ekranı, RSSI çubuğu ve durum satırı Survival'da derleme dışı.
  Boş flash 4532 → 8392 bayt.
- Durum satırı: durum kelimesi (READY / SCAN UP / SCAN DN / RX / TX / MONITOR / FM / FM SCAN / F / LOW BAT),
  kilitliyken LOCK, alımda sinyal çubuğu, voltaj, 3 bölmeli pil.
- Ana ekran: R0 grup (tarama/alım/gönderimde ters renk) + PRI; R1–R2 büyük kanal adı (AIR BAND / CUSTOM);
  R3–R4 frekansın tüm haneleri büyük; R5 güç/bant genişliği/squelch veya RX ONLY (bantlarda modülasyon/adım), DUP, CT;
  R6 o anki tuş ipucu ya da TX BLOCKED gibi durum.
- FM: FM RADIO, büyük frekans, SAVED/TUNE/SEEK/FIND ALL ya da kaydet/sil sorusu (HOLD M), ters renkli arka plan alıcı
  satırı ve frekansı, tuş ipucu.
- v4.3: Reset sonrası FM bandı 64–76 MHz'e düşüyordu (silinmiş EEPROM = bant 3); ilk kurulum ve ayar revizyonu 2
  87.5–108 MHz yapar.

### v4.4 (2026-10-29): başlık durum satırında, tarama durdurma düzeltmesi

- Durum satırı: sol üstte ekran başlığı (PMR446, FRS, MARINE, AIR BAND, HAM, CB, CUSTOM, FAV, FM RADIO, MENU);
  F'ye basılınca `F`, kilitliyken kilit simgesi; pil simgesi 7 px, durum satırının tam yüksekliği.
- Ana ekran R0: yalnızca bir şey olurken ters renkli çubuk: SCAN UP/DN, RX + sinyal çubuğu, MON, TX; öncelikli kanalda PRI.
- AIR BAND / CUSTOM'da R1–R2 tarama aralığını gösterir (ör. `118.000-136.975`).
- FM ekranı: büyük frekans R0–R1, durum R2, arka plan alıcı R4–R5.
- Taramada yukarı/aşağı kısa basış: 0,4 sn'den uzun basınca "basılı tut" olayı taramayı yeniden başlatıyordu;
  taramayı durduran basış artık bırakılana kadar başka bir şey yapmaz.

### Aşama 2 — v4.5/v4.6 (2026-10-29): Basic / Advanced menü

- M basılı tut: menü her zaman BASIC seviyesinde, Group üzerinde açılır.
- BASIC: Group, Heard, Favs, Prio, Quiet (squelch), Power, Eco, Radio, Adv.
  ADV: FmScan, Mod, Width, Step, RxTone, RxCode, TxTone, TxCode, Shift, Offset, Resume, Start, End,
  TxPMR…TxCB, TxAll, Rename, Lock, TxTime, Mic, Light, Contr, Beep, About, Reset (+ gizli F Lock, BatCal, BatTyp).
- Düzen: R0 sol BASIC/ADV, sağ sade başlık (SQUELCH, TX POWER…); R1 önceki öğe, R2–R3 seçili öğe büyük ve ters,
  R4 sonraki öğe; sağda değer, düzenlerken ters; R6 tuş ipucu (M:EDIT / M:SAVE EXIT:CANCEL / M:OPEN / M:DO, onayda M:YES EXIT:NO).
- Liste uçlarda durur (dönmez), rakamla numaraya atlama yok. ADV'de EXIT → BASIC (Adv üzerinde), BASIC'te EXIT → ana ekran / FM.
- Eylemler M bırakılınca çalışır (v4.6): Heard listeyi açar, Favs ekler/çıkarır, Prio ayarlar/kaldırır, Radio FM'i açar/kapatır,
  Adv ADV seviyesine geçer. v4.5'te eylem basışta çalışıyordu, M'nin bırakılması ana ekrana gidip sonraki moda geçiyordu.
- Değer metinleri: TxPMR…TxAll ALLOW/BLOCK, Quiet 0 = OPEN, FmScan OFF = WATCH ONLY, LAST MODE = LAST GROUP.
- Menüden çıkanlar: ChSave, ChDele (FAV için F+7 / Favs), F1/F2 tuş atamaları (sabit), BatSav (Eco yönetir), BatTxt.
- Sonraki aşama: Help öğesi ve F ile açılan kısayol yardım sayfaları.

### Aşama 3 — v4.7 (2026-10-29): yardım sayfaları

- F tap = F öneki görünür hale gelir: 3 sayfa yardım (R0 ters başlık, R1–R5 metin, R6 `M:NEXT EXIT:BACK`).
  - 1/3 F + KEY: 0 FM RADIO, 4 HEARD, 5 PRIORITY, 7 FAVORITE, UP/DN SQUELCH
    (FM'de: 0 EXIT FM, 3 SAVED / TUNE, * FIND ALL FM, HOLD M SAVE, UP/DN TUNE).
  - 2/3 KEYS: M NEXT GROUP, HOLD M MENU, * SCAN / STOP, HOLD UP/DN SCAN, HOLD F LOCK.
  - 3/3 SIDE KEYS: SIDE1 MONITOR, HOLD SIDE1 LIGHT, REPEAT HOLD: SOS, SIDE2 FM RADIO, HOLD SIDE2 SCAN.
- M tap (bırakınca) sonraki sayfa; 2/3 ve 3/3'te UP/DOWN sayfa çevirir; 1/3'te F+UP/DOWN squelch.
  EXIT, F, PTT ya da 8 sn bekleme kapatır. Yardım açıkken F + 0/4/5/7/* eskisi gibi çalışır. Hold M yardımı kapatıp menüyü açar.
- BASIC menüye `Help` (Adv'den önce): aynı sayfaları açar.
- F + 1/2/3/6/8/9 (F4HWN VFO A/B, VFO/MR, güç, ters, çağrı kanalı) Survival'da kapalı; yardım açık kalır.
  FM'de F+1 (FM bandı değiştir) kapalı: bant 87.5–108 MHz'de kalır.

### v4.8/v4.9 (2026-10-29): yardım süresi, fener modları, yan tuş 1

- Yardım sayfaları 32 sn açık kalır (F öneki için 8 sn yerine); sayfa çevirince süre baştan başlar.
- Fener: F4HWN'de yalnızca aç/kapat vardı (BLINK/SOS kodu sadece RESCUE_OPS'ta); Survival'da açıldı.
- Yan tuş 1: tap = fener açık → yanıp sönme → SOS → kapalı; hold = monitor aç/kapat.
  Ayar revizyonu 3: mevcut telsizlerde yan tuş 1 ataması açılışta bu düzene çevrilir.
- Yardım 3/3: SIDE1 LIGHT, AGAIN: BLINK, SOS, HOLD SIDE1 MONITOR, SIDE2 FM RADIO, HOLD SIDE2 SCAN.

### v5.0 (2026-10-29): strobe

- Fener sırası (yan tuş 1 tap): açık → yanıp sönme → strobe → SOS → kapalı.
- Strobe: 3 × (30 ms açık, 30 ms kapalı), saniyenin kalanı (820 ms) kapalı; 10 ms zaman diliminde sayılır.
- Yardım 3/3 R2: BLINK,STROBE,SOS.

### v5.1 (2026-10-29): yardım metni biçimi

- Yardım satırları `TUŞ>İşlev`: tuş adı büyük harf, işlev normal yazı (ör. `SIDE1>Light, blink`, `HOLD M>Menu`).
  Alt satır `M>Next  EXIT>Back`. v5.2: SIDE1 ikinci satırı `      Strobe, SOS` (aynı tuşun devamı, girintili).

### v5.3 (2026-10-29): atlama listesi (skip)

- Amaç: gürültülü / sürekli taşıyıcılı kanallar taramayı durdurmasın. Elle seçilen kanal yine dinlenir.
- F+8: tararken o anki frekansı listeye ekler ve taramaya devam eder (F4HWN tarama hariç tutma yolu gibi);
  taramıyorken mevcut frekansı ekler / çıkarır (SKIPPED / NOT SKIPPED mesajı).
- Liste frekans tutar (16 adet, 10 Hz birimi), EEPROM 0x1C00 (DTMF rehber alanı, Survival'da DTMF kapalı).
  İlk kurulum / Reset'te boşalır. Aynı frekans her grupta atlanır.
- Tarayıcı: `CHFRSCANNER_ContinueScanning` listedeki frekansta ön kontrolü ve dinlemeyi atlar (squelch açık olsa da);
  kanal grupları, AIR/CUSTOM ve FM arka plan taraması dahil.
- Menü BASIC > Skip: liste (`SKIP n/16  M:DEL`), UP/DOWN seç, M sil (onaysız), EXIT çık.
- Ana ekran: boştayken atlanan frekansta R0'da SKIP. Yardım 1/3: `5>Prio  7>Favorite`, `8>Skip in scan`.
- Not: tarama sırasında M basılı tutmak hâlâ F4HWN'in RAM'de kalan kanal hariç tutmasını yapıyor (menü açmıyor).

### v5.4 (2026-10-30): M basılı tut taramada da menü

- Tarama sırasında M hold: tarama olduğu yerde durur, menü açılır (BASIC / Group).
  F4HWN'in RAM'de kalan "M hold = kanalı taramadan çıkar" özelliği Survival'da kapalı; kalıcı yol F+8 (skip).

### v5.5 (2026-10-30): F yardımı yenilendi, F+6 kılavuz

- F yardımı: başlık durum satırında ters renk (`F+KEY` / `KEYS` / `SIDE KEYS`, sağda `1/3`), alt ipucu satırı yok,
  7 satır içerik. 1/3: 0, 4, 5, `6>Help/Manual`, 7, 8, UP/DN (FM'de 0, 3, *, 6, HOLD M, UP/DN).
  2/3 ve 3/3 kılavuzun KEYS / SIDE KEYS konularından okunur (metin tek yerde).
- F+6 (ana ekran ve FM) ve BASIC > Help: kılavuz. 3 sn giriş sayfası (tuşlar), sonra indeks (`MANUAL n/27`).
  İndeks: UP/DN seç, M aç, EXIT/F kapat. Konu: UP/DN satır kaydır (sonda sonraki, başta önceki konu), M sonraki konu,
  EXIT indeks, F kapat. Başlıkta konu adı solda, `n/27` sağda.
- 27 konu: kullanım (Keys, Side keys, F shortcuts, Groups, Scan, Skip list, Favorites, Priority, Heard list, FM radio,
  Air band, Custom, Sending, Squelch, Menu, Battery, Key lock, Reset) ve survival notları (Emergency, Mayday call,
  Radio tips, Phonetic, SOS signals, Rule of 3, Water, Direction, First aid).
- Metin kaynağı `design/manual.txt`; `tools/manual2c.py` → `firmware/app/survival_manual.c` (satır ≤ 18, başlık ≤ 12 kontrol).
  Kılavuz 4015 bayt; boş flash 780 bayt. Yeni özellikten önce kullanılmayan F4HWN kodu temizlenmeli.
