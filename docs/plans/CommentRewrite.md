# 주석을 새 문체로 다시 쓰기 (6 차 이후 남은 구역)

> 상태: 구현중 · 선행: 없음 · 영역: project · 옛번호: C-16 · 진행: 0/6

새 문체(문서 작성 규칙 `docs/10_WritingDocs.md` 의 용어 대조표, 이름 풀이 · 코드가 이미 말하는 말 지우기)로 주석을 다시 쓰는 작업이 아래 구역에서 끝나지 않았다. 구역마다 작업 워크트리(`py -3 -m Scripts worktree-make c6<이름>`)를 따로 만들어 이어서 한다. 같은 폴더를 다른 작업이 건드리는 중이면 미룬다(파일이 겹쳐 미뤄진 정리다).

로그 문자열의 한글(KoreanLogStrings)은 같은 파일을 건드리므로 구역을 맡은 작업자가 그 구역의 [KoreanLogStrings](KoreanLogStrings.md) 단위를 함께 한다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| W1 | Engine: Graphics · Renderer 의 13KB 넘는 큰 파일 약 100 개(작은 파일은 끝났다) | 대기 |
| W2 | Engine 나머지: Object · Scene · Resource · Serialization · Spatial · Common 대부분, UI 의 `Console`, Compression · Observability · Profiling · Telemetry 전부. `SceneNavigation.h` · `GameObjectManager.h` 는 충돌 때문에 다시 쓰지 못했다 | 대기 |
| W3 | Editor: 2026-10-22 `wt/onlineclean` 에서 대조표 낱말(표 · 칸 · 자리 · 정본 · 창구 · 묶음 · 뿌리 · 든다 · 떼다 · 올리다 · 내리다 등) 239 개 파일을 뜻을 가려 다시 썼다. 남은 것: 생성 파일 `EditorIconGlyphs.h` 의 "뗀" 은 생성기 `GenerateEditorIcons.py` 에서 고친다. 이전 기록: 주석 25 줄 이상인 파일(`Common/GUI`, `Commands`, `Viewport` 등)과 그보다 짧은 파일, `Common` 은 옛 어휘가 가장 많던 22 개(2026-10-21), `Panels` 는 23 개 파일(2026-10-21). 나머지는 옛 어휘가 파일당 10 곳 미만 | 대기 |
| W4 | GameFramework: Base 헤더는 끝났다(2026-10-21). 남은 것: 다른 작업이 고치던 헤더 열한 개(`GameInstanceBase.h` · `IGame.h` · `GameSettings.h` · `GameDataXML.h` · Online 의 `ServerDirectoryControl.h` · `BlockingHTTPClient.h` · `GroupChannelMembership.h` · `ServiceReport.h` · `NetSecurity.h` · `OnlineServerKit.h` · `OnlineServiceHost.h`), Base 의 .cpp · .inl, Kits/Feature/Online 의 Account 밖 폴더(Kits/Feature 전체는 2026-10-21 `wt/kitclean` 에서 끝). `NetPairedAnimation` 은 `wt/animation` 이 새로 만든 파일이라 뺐다 | 대기 |
| W5 | Core: 열 폴더(Math · Process · Delegate · Memory · Diagnostics · CommandLine · Container · Task · LogSink · String)는 대조표 용어만 2026-10-21 정리했다(Network · File 도 같다). 문체 다듬기는 Container 의 표준 컨테이너 대응 헤더만 2026-10-22 에 끝났고, 나머지 아홉 폴더와 Container 의 다른 파일이 남았다. RuntimeAPI 는 손대지 않았다 | 대기 |
| W6 | Games: Empty 벤치 씬 일부와 VoxelCraft 전체 | 대기 |

## 2. 하는 법

- 구역마다 규칙: 동작을 바꾸지 않는다(주석만). 용어 대조표 낱말을 뜻을 가려 고친다(기계적 치환 금지).
- 끝낸 단위는 `상태` 칸을 `끝남(날짜)` 로 고친다.
