# GD Speed Macro (mod Geode)

Fitur: kontrol kecepatan game, rekam/putar macro berbasis frame, simpan dan muat macro format GDR2
(format yang dipakai bot lain). Safe mode: saat speed bukan 1.0 atau macro sedang diputar, level
ditandai test mode sehingga progres/persen/completion tidak tersimpan.

## Build (Windows)
1. Install Visual Studio 2022 (workload "Desktop development with C++"), CMake, Git.
2. Install Geode CLI dari github.com/geode-sdk/cli/releases, lalu:
   geode config setup
   geode sdk install
3. Di folder ini:
   geode build
   File .geode hasilnya ada di folder build/ dan biasanya otomatis dipasang ke profil Geode.
   Kalau tidak, salin ke folder geode/mods di folder Geometry Dash.
4. Kalau ada error CMake/mod.json: jalankan `geode new`, lalu salin src/main.cpp,
   bagian "settings" dari mod.json, dan 2 baris CPM/libGDR dari CMakeLists.txt ke proyek baru itu.

## Pakai
- Buka menu pause di level. Tombol di sisi kanan: Rekam, Putar, Simpan, Muat, Speed -, Speed +.
- Rekam: mulai dari awal level (normal mode). Berhenti saat tekan lagi, selesai level, atau keluar level.
  Macro otomatis disimpan di folder save mod (geode/config/<id>/macros) sebagai .gdr2.
- Muat macro orang lain: Settings mod -> "File macro untuk dimuat" -> pilih file -> menu pause -> Muat -> Putar.
- Speed juga bisa diatur lewat slider di Settings mod.

## Batasan
- Hanya level klasik, mode normal (bukan practice), 240 TPS. Macro platformer / TPS lain ditolak.
- Macro orang lain bisa meleset jika versi game, mod, atau random-nya berbeda.
- Data tambahan khusus bot lain (misal koreksi posisi) dilewati; hanya input yang dipakai.

## Build tanpa install apa-apa (lewat GitHub Actions)
1. Buat akun GitHub, buat repository baru (boleh private), upload semua isi folder ini
   (pastikan folder .github ikut ter-upload).
2. Buka tab Actions di repository, tunggu workflow "Build Geode Mod" selesai (hijau).
3. Buka run tersebut, download artifact "Build Output", ekstrak, ambil file .geode.
4. Salin file .geode ke GeometryDash/geode/mods lalu restart game.
Kalau workflow merah, buka log-nya dan kirim pesan error ke Claude.
