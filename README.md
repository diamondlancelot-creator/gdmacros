# GD Speed Macro

Mod Geode untuk Geometry Dash yang menyediakan pengatur kecepatan game serta perekam dan pemutar macro,
dikendalikan dari satu menu yang dibuka dengan hotkey di dalam level.

Versi saat ini: **1.2.0** | Geode: **5.11.2** | Geometry Dash: **2.2081** | Platform: Windows

## Fitur

- **Menu terpisah.** Tekan `Y` di dalam level untuk membuka menu mod. Game dijeda otomatis selama menu terbuka.
  Tombol dapat diganti di pengaturan mod.
- **Pengatur kecepatan.** Preset 0.25x, 0.5x, 1x, 2x, 3x, dan 5x, ditambah penyesuaian halus (-0.5, -0.1, +0.1, +0.5).
- **Rekam dan putar macro.** Perekaman berbasis frame pada 240 TPS. Format keluaran GDR2, sama dengan yang dipakai
  banyak bot lain.
- **Penyimpanan otomatis dengan nama terstruktur.** Rekaman disimpan sebagai `<ID level>_<nama level>.gdr2`.
  Level yang belum diunggah memakai awalan `local`. Jika nama sudah ada, ditambahkan nomor urut sehingga
  rekaman lama tidak tertimpa.
- **Pengelola file macro.** Daftar file di folder macros ditampilkan langsung di menu: klik nama untuk memuat,
  atau gunakan tombol Hapus (dengan konfirmasi) untuk menghapusnya.
- **Muat dari berbagai format.** GDR2/GDR (biner maupun JSON) dan file `.zbf` dari zBot. Macro yang tidak
  direkam pada 240 TPS dikonversi otomatis, dengan peringatan di menu.
- **Safe mode.** Selama kecepatan bukan 1.0 atau macro sedang diputar, level ditandai sebagai test mode,
  sehingga progres, persentase, dan completion tidak tersimpan maupun terkirim.

## Instalasi

1. Unduh file `namakamu.gdspeedmacro.geode` dari artifact **Build Output** pada run GitHub Actions terbaru.
2. Tutup Geometry Dash.
3. Salin file tersebut ke `%localappdata%\GeometryDash\geode\mods`
   (atau folder `geode\mods` di direktori instalasi Geometry Dash untuk versi Steam).
4. Jalankan game dan pastikan **GD Speed Macro** muncul pada daftar mod Geode.

## Penggunaan

| Tindakan | Cara |
| --- | --- |
| Membuka atau menutup menu | Tekan `Y` di dalam level, atau klik **Tutup** |
| Mengubah kecepatan | Klik preset atau tombol penyesuaian pada bagian Kecepatan |
| Merekam | Klik **Rekam**. Level dimulai ulang dan perekaman berjalan dari awal |
| Menghentikan dan menyimpan rekaman | Klik **Stop Rekam**, selesaikan level, atau keluar dari level |
| Memutar macro | Muat macro (atau rekam), lalu klik **Putar**. Level dimulai ulang |
| Memuat macro dari file | Klik nama file pada daftar, atau pilih file di Settings mod lalu klik **Dari Setting** |
| Membuka folder macros | Klik **Folder** |
| Menghapus macro | Klik **Hapus**, lalu klik **Yakin?** untuk mengonfirmasi |

Folder macros berada di direktori penyimpanan mod (`geode\config\namakamu.gdspeedmacro\macros`).
File apa pun dapat ditaruh di folder ini dan akan muncul pada daftar.

## Batasan

- Hanya level klasik, mode normal (bukan practice), dan 240 TPS. Macro platformer ditolak.
- Konversi dari TPS lain (misalnya 360 FPS pada `.zbf`) menggeser timing dan mengubah perilaku fisika,
  sehingga hasilnya bisa meleset, terutama pada level yang menuntut presisi tinggi. Untuk macro seperti itu,
  gunakan bot yang memutarnya pada TPS aslinya.
- Macro dari bot lain dapat meleset bila versi game, mod, atau seed acak berbeda.
- Data tambahan khusus bot lain (misalnya koreksi posisi) tidak dipakai. Hanya input yang diputar ulang.
- Hotkey hanya tersedia di Windows.

## Build

Proyek ini dibangun melalui GitHub Actions (`.github/workflows/build.yml`) menggunakan
`geode-sdk/build-geode-mod`. Versi SDK pada workflow harus sama dengan nilai `geode` di `mod.json`.

Untuk build lokal: pasang Geode CLI dan SDK, lalu jalankan `geode build` di folder proyek.
