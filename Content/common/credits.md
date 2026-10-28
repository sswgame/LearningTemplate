# 공용 팩 — 에셋 출처

| 파일 | 원본 | 라이선스 |
|------|------|----------|
| `sounds/kenney_interface_sounds/*.ogg` | Kenney "Interface Sounds" 1.0 (<https://kenney.nl/assets/interface-sounds>) — `Audio/*.ogg` 100 개 | CC0 1.0 |
| `sounds/kenney_ui_audio/*.ogg` | Kenney "UI Audio" (<https://kenney.nl/assets/ui-audio>) — `Audio/*.ogg` 51 개(`Preview.ogg` 는 뺐다) | CC0 1.0 |
| `sounds/kenney_impact_sounds/*.ogg` | Kenney "Impact Sounds" (<https://kenney.nl/assets/impact-sounds>) — `Audio/*.ogg` 130 개 | CC0 1.0 |
| `sounds/kenney_rpg_audio/*.ogg` | Kenney "RPG Audio" (<https://kenney.nl/assets/rpg-audio>) — `Audio/*.ogg` 51 개(`Preview.ogg` 는 뺐다) | CC0 1.0 |
| `sounds/kenney_scifi_sounds/*.ogg` | Kenney "Sci-fi Sounds" (<https://kenney.nl/assets/sci-fi-sounds>) — `Audio/*.ogg` 73 개 | CC0 1.0 |
| `music/subspaceaudio_chiptune_adventures/*.ogg` | OpenGameArt "4 Chiptunes (Adventure)" — 만든 이 Juhani Junkala(SubspaceAudio) (<https://opengameart.org/content/4-chiptunes-adventure>) — OGG 묶음의 네 곡 | CC0 1.0 |
| `music/yd_out_there/out_there.ogg` | OpenGameArt "Space Music: Out There" — 만든 이 yd (<https://opengameart.org/content/space-music-out-there>) — `OutThere_0.ogg` | CC0 1.0 |
| `sounds/subspaceaudio_512_sfx/**/*.ogg` | OpenGameArt "512 Sound Effects (8-bit style)" — 만든 이 Juhani Junkala(SubspaceAudio) (<https://opengameart.org/content/512-sound-effects-8-bit-style>) — wav 512 개 가운데 `General Sounds/Weird Sounds` 16 개를 뺀 496 개를 Ogg Vorbis 로 바꿈(원본 폴더 구조 유지) | CC0 1.0 |
| `music/subspaceaudio_chiptunes_action/*.ogg` | OpenGameArt "5 Chiptunes (Action)" — 만든 이 Juhani Junkala(SubspaceAudio) (<https://opengameart.org/content/5-chiptunes-action>) — wav 다섯 곡을 Ogg Vorbis 로 바꿈 | CC0 1.0 |
| `music/cynicmusic_town_theme/town_theme.ogg` | OpenGameArt "Town Theme RPG" — 만든 이 cynicmusic (<https://opengameart.org/content/town-theme-rpg>) — `TownTheme.mp3` 를 Ogg Vorbis 로 바꿈 | CC0 1.0 |
| `music/ville_nousiainen_fast_fight/fight.ogg` | OpenGameArt "Fast fight / battle music" — 만든 이 Ville Nousiainen (<https://opengameart.org/content/fast-fight-battle-music>) — `fight.ogg` | CC0 1.0 |

- 파일마다 받는 주소 · 압축 안 경로 · SHA-256 · 라이선스 원문 인용은 `audio.sources.json` 이 정본이다. `py -3 -I Scripts/dev/MakeCommonAudio.py verify` 가 저장소 파일과 대조하고, `fetch` 가 공식 페이지의 CC0 표시를 확인한 뒤 다시 받는다.
- Kenney 묶음은 동봉 `License.txt` 원문으로 CC0 인지 확인했다("License: (Creative Commons Zero, CC0) http://creativecommons.org/publicdomain/zero/1.0/"). 페이지에도 "Creative Commons CC0" 가 있다.
- "4 Chiptunes (Adventure)" 는 페이지의 License 칸(CC0)과 동봉 `INFO.txt` 원문("These music tracks have been released under CC0 creative commons license. You can do anything you want with these tunes.")으로 확인했다.
- "Space Music: Out There" · "Fast fight / battle music" 은 페이지의 License 칸이 CC0 하나다. 뒤의 것은 "credit … (totally optional)" 라고 적혀 있어 표기 의무가 없다.
- "512 Sound Effects (8-bit style)" 는 페이지의 License 칸(CC0)과 동봉 `INFO.txt` 원문("These sound effects have been released under CC0 creative commons license. You can do anything you want with these sounds."),
  "5 Chiptunes (Action)" 은 License 칸(CC0)과 동봉 `INFO.txt` 원문("These music tracks have been released under CC0 creative commons license. You can do anything you want with these tunes."),
  "Town Theme RPG" 는 License 칸(CC0)과 설명의 "Now CC0 Public Domain License!" 로 확인했다.
- 이 셋은 wav · mp3 로만 배포되어 python-soundfile(libsndfile)로 Ogg Vorbis q4(`compression_level` 0.6, 스테레오 공칭 128 kbps)로 바꿨다.
  원본 해시(`source_sha256`) · 변환 매개변수 · 도구 버전은 `audio.sources.json` 의 `convert` 에 있고, `fetch` 가 같은 버전에서 같은 바이트를 다시 만든다.
- 저장소에는 Ogg Vorbis 만 둔다. 파일 이름은 원본의 camelCase · 공백 · 기호를 소문자 `_` 로 바꾼 것이다(곡은 앞의 "Juhani Junkala [Chiptune Adventures] " · "[Retro Game Music Pack] " 를 뗐다).
  배포 원본이 ogg 인 팩은 바이트가 그대로다.
- 아직 오디오 이벤트(`AudioEventLibrary`)로 등록하지 않았다. 게임이 쓰는 소리는 각 게임 팩의 `sounds/` 가 그대로다.
- CC0 는 표기 의무가 없지만 고마움을 적어 둡니다 — Kenney (www.kenney.nl), Juhani Junkala(SubspaceAudio), yd, Ville Nousiainen, cynicmusic.
