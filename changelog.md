# v1.2.0
- Tampilan menu dirombak: panel dengan bagian Kecepatan, Macro, dan File Macro, serta animasi pembukaan singkat
- Rekaman disimpan otomatis dengan nama `<ID level>_<nama level>.gdr2`; nomor urut ditambahkan jika nama sudah ada
- Tambah opsi hapus file macro langsung dari menu, dengan konfirmasi
- Nama file dengan karakter non-ASCII kini ditangani dengan benar
- Perbaikan: input dari macro sebelumnya ikut terblokir oleh hook sendiri sehingga karakter tidak melompat saat macro diputar

# v1.1.0
- Menu dipindahkan dari layar pause ke menu tersendiri yang dibuka dengan hotkey (default Y, dapat diubah)
- Preset kecepatan dan tombol penyesuaian halus
- Macro dapat dimuat dari berbagai format, termasuk `.zbf` dari zBot; macro non-240 TPS dikonversi
- Penyesuaian untuk Geode 5.11.2 (signature `processCommands`)

# v1.0.0
- Rilis awal: pengatur kecepatan, rekam dan putar macro GDR2, safe mode
