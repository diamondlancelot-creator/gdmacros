{
	"geode": "5.11.2",
	"gd": {
		"win": "2.2081",
		"android": "2.2081"
	},
	"id": "namakamu.replaytools",
	"name": "Replay Tools",
	"version": "v1.3.1",
	"developer": "namakamu",
	"description": "In-game menu (hotkey Y) for speed control, macro recording/playback and No Clip. Safe mode is on by default.",
	"settings": {
		"safe-mode": {
			"type": "bool",
			"name": "Safe mode",
			"description": "Bila aktif, level ditandai test mode saat speed diubah, macro diputar, atau No Clip menyala, sehingga progres dan completion tidak tersimpan atau terkirim. Dapat diubah juga dari menu mod.",
			"default": true
		},
		"menu-key": {
			"type": "string",
			"name": "Tombol menu",
			"description": "Huruf atau angka untuk membuka menu mod di dalam level (default: Y).",
			"default": "Y"
		},
		"speed": {
			"type": "float",
			"name": "Kecepatan game",
			"description": "1.0 = normal. Selain 1.0, progres level TIDAK disimpan (safe mode). Lebih mudah diatur lewat menu hotkey.",
			"default": 1.0,
			"min": 0.1,
			"max": 5.0,
			"control": {
				"slider": true,
				"input": true
			}
		},
		"macro-file": {
			"type": "file",
			"name": "File macro untuk dimuat",
			"description": "Pilih file macro apa saja, lalu tekan 'Dari Setting' di menu mod.",
			"default": "",
			"control": {
				"dialog": "open",
				"filters": [
					{
						"files": [
							"*.*"
						],
						"description": "Semua file"
					}
				]
			}
		}
	}
}
