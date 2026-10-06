<!-- 생성 문서 — 손으로 고치지 않는다. 정본은 코드다. 다시 만들기: py -3 Scripts/generate/GenerateConfigReference.py -->

# 명령줄 인자

[설정 색인](README.md)

앞의 `-` · `--` 는 몇 개든 같다. 전역 변수는 [`-gv_<이름>=<값>`](GlobalVariables.md) 로 따로 받는다. 목록 밖: `--crash-reporter=<묶음 폴더>`(App 의 main 이 엔진을 세우기 전에 읽고 끝낸다).

| 철자 | 값 | 설명 | 코드 이름 |
|---|---|---|---|
| `-W` | 숫자 값(`-이름=값`) | 창 클라이언트 영역 너비 · 높이(픽셀) — EngineConfig · 플레이어 해상도보다 이긴다 | `WIDTH` |
| `-H` | 숫자 값(`-이름=값`) | 창 클라이언트 영역 너비 · 높이(픽셀) — EngineConfig · 플레이어 해상도보다 이긴다 | `HEIGHT` |
| `-vsync` | 플래그(`-이름`) | 수직 동기화를 켠다 — EngineConfig · 플레이어 값보다 이긴다 | `VSYNC` |
| `-dx11` · `-d3d11` · `-directx11` | 플래그(`-이름`) | DirectX11 백엔드로 띄운다. 이 백엔드로 띄운다(EngineConfig `_defaultRHI` 를 이긴다). 줄과 철자는 쿠킹 표(Config/Engine/CookContract.json)의 `rhi_backends` — 셰이더 폴더 · 쿠커와 같은 이름 | `DIRECTX_11` |
| `-dx12` · `-d3d12` · `-directx12` | 플래그(`-이름`) | DirectX12 백엔드로 띄운다. 이 백엔드로 띄운다(EngineConfig `_defaultRHI` 를 이긴다). 줄과 철자는 쿠킹 표(Config/Engine/CookContract.json)의 `rhi_backends` — 셰이더 폴더 · 쿠커와 같은 이름 | `DIRECTX_12` |
| `-vk` · `-vulkan` · `-spirv` | 플래그(`-이름`) | Vulkan 백엔드로 띄운다. 이 백엔드로 띄운다(EngineConfig `_defaultRHI` 를 이긴다). 줄과 철자는 쿠킹 표(Config/Engine/CookContract.json)의 `rhi_backends` — 셰이더 폴더 · 쿠커와 같은 이름 | `VULKAN` |
| `-gl` · `-opengl` | 플래그(`-이름`) | OpenGL 백엔드로 띄운다. 이 백엔드로 띄운다(EngineConfig `_defaultRHI` 를 이긴다). 줄과 철자는 쿠킹 표(Config/Engine/CookContract.json)의 `rhi_backends` — 셰이더 폴더 · 쿠커와 같은 이름 | `OPENGL` |
| `-EnableEditor` | 플래그(`-이름`) | 에디터 모듈을 올린다(Dev 만) — 없으면 에디터 없이 게임만 뜬다 | `ENABLE_EDITOR` |
| `-lang` | 글 값(`-이름=값`) | 시작 언어(`ko_kr` · `en_us` …) | `LANGUAGE` |
| `-cook-shaders` | 플래그(`-이름`) | 셰이더를 모든 백엔드 바이너리로 굽고 끝낸다(헤드리스) | `COOK_SHADERS` |
| `-cook-scenes` | 플래그(`-이름`) | 씬 · 프리팹을 바이너리(SCN1 · PFB2)로 굽고 끝낸다 — `-cooked-dir=<폴더>` 가 쓸 곳(CookAssets.py 가 부른다) | `COOK_SCENES` |
| `-cooked-dir` | 글 값(`-이름=값`) | 씬 · 프리팹을 바이너리(SCN1 · PFB2)로 굽고 끝낸다 — `-cooked-dir=<폴더>` 가 쓸 곳(CookAssets.py 가 부른다) | `COOKED_DIR` |
| `-server-config` | 글 값(`-이름=값`) | 전용 서버(Server)의 운영 설정 파일(`ServerConfig`) — 비우면 `Config/Server/<SW_ACTIVE_GAME>.json`. Shipping 도 디스크에서 읽는다. | `SERVER_CONFIG` |
| `-service` | 플래그(`-이름`) | 전용 서버를 Windows 서비스(SCM)로 돈다(`Server --service`). SCM 이 띄운 프로세스만 된다. | `SERVICE` |
| `-write-reflection-docs` | 글 값(`-이름=값`) | 등록된 리플렉션 타입(엔진 · GameFramework · 킷 · 게임)으로 Markdown API 문서를 그 폴더에 쓰고 끝난다(헤드리스). 빌드 산출물이라 커밋하지 않는다. | `WRITE_REFLECTION_DOCS` |
| `-import-textures` | 플래그(`-이름`) | 텍스처 원본(`textures_raw/`)을 DDS 로 임포트한다 · 원본과 DDS 가 맞는지 보기만 한다. 에디터 모듈의 일이라 Dev 빌드에서만 된다. | `IMPORT_TEXTURES` |
| `-check-textures` | 플래그(`-이름`) | 텍스처 원본(`textures_raw/`)을 DDS 로 임포트한다 · 원본과 DDS 가 맞는지 보기만 한다. 에디터 모듈의 일이라 Dev 빌드에서만 된다. | `CHECK_TEXTURES` |
| `-import-models` | 플래그(`-이름`) | 모델 원본(`models_raw/` 의 glTF)을 `.mesh` 로 임포트한다 · 원본과 `.mesh` 가 맞는지 보기만 한다. 에디터 모듈의 일이라 Dev 빌드에서만 된다. | `IMPORT_MODELS` |
| `-check-models` | 플래그(`-이름`) | 모델 원본(`models_raw/` 의 glTF)을 `.mesh` 로 임포트한다 · 원본과 `.mesh` 가 맞는지 보기만 한다. 에디터 모듈의 일이라 Dev 빌드에서만 된다. | `CHECK_MODELS` |
| `-import-lipsync` | 플래그(`-이름`) | 음성(`voice/` 폴더의 .wav · .ogg)을 분석해 곁에 비즘 트랙(`.visemes.json`)을 쓴다 — 립싱크 오프라인 분석(엔진 일, 헤드리스). | `IMPORT_LIPSYNC` |
| `-bake-retarget` | 글 값(`-이름=값`) | 리타깃 굽기 — 값은 `<프로필 *.retarget.json>,<원본 .animclip>,<출력 .animclip>`. 프로필의 두 스켈레톤으로 원본 클립을 옮겨 굽고 끝낸다(헤드리스). | `BAKE_RETARGET` |
| `-render-portraits` | 글 값(`-이름=값`) | 프리팹 초상화(썸네일)를 격리된 스튜디오에서 그려 DDS · PNG 로 쓰고 끝낸다 — 값은 쉼표로 나눈 프리팹 경로, 크기는 정사각 한 변(px), 폴더는 쓸 곳(기본 Saved/Portraits). | `RENDER_PORTRAITS` |
| `-portrait-size` | 숫자 값(`-이름=값`) | 프리팹 초상화(썸네일)를 격리된 스튜디오에서 그려 DDS · PNG 로 쓰고 끝낸다 — 값은 쉼표로 나눈 프리팹 경로, 크기는 정사각 한 변(px), 폴더는 쓸 곳(기본 Saved/Portraits). | `PORTRAIT_SIZE` |
| `-portrait-dir` | 글 값(`-이름=값`) | 프리팹 초상화(썸네일)를 격리된 스튜디오에서 그려 DDS · PNG 로 쓰고 끝낸다 — 값은 쉼표로 나눈 프리팹 경로, 크기는 정사각 한 변(px), 폴더는 쓸 곳(기본 Saved/Portraits). | `PORTRAIT_DIR` |
| `-import-heightfields` | 플래그(`-이름`) | 높이장 원본(`heightfields_raw/` 의 16 비트 PNG · `.r16`)을 `.heightfield` 로 임포트한다 · 맞는지 보기만 한다. 에디터 모듈의 일이라 Dev 빌드에서만 된다. | `IMPORT_HEIGHTFIELDS` |
| `-check-heightfields` | 플래그(`-이름`) | 높이장 원본(`heightfields_raw/` 의 16 비트 PNG · `.r16`)을 `.heightfield` 로 임포트한다 · 맞는지 보기만 한다. 에디터 모듈의 일이라 Dev 빌드에서만 된다. | `CHECK_HEIGHTFIELDS` |
| `-gather-text` | 플래그(`-이름`) | 로컬라이제이션 글 수집(`LocalizationTools`) — 코드 · 데이터에서 모아 원문 표를 고친다 · 고치지 않고 최신인지만 본다(CI). 소스 트리가 있어야 한다. `-loc-project=<프로젝트 파일>` 이 없으면 엔진 프로젝트와 활성 게임 팩의 프로젝트 전부다. | `GATHER_TEXT` |
| `-check-text` | 플래그(`-이름`) | 로컬라이제이션 글 수집(`LocalizationTools`) — 코드 · 데이터에서 모아 원문 표를 고친다 · 고치지 않고 최신인지만 본다(CI). 소스 트리가 있어야 한다. `-loc-project=<프로젝트 파일>` 이 없으면 엔진 프로젝트와 활성 게임 팩의 프로젝트 전부다. | `CHECK_TEXT` |
| `-loc-project` | 글 값(`-이름=값`) | 로컬라이제이션 글 수집(`LocalizationTools`) — 코드 · 데이터에서 모아 원문 표를 고친다 · 고치지 않고 최신인지만 본다(CI). 소스 트리가 있어야 한다. `-loc-project=<프로젝트 파일>` 이 없으면 엔진 프로젝트와 활성 게임 팩의 프로젝트 전부다. | `LOC_PROJECT` |
| `-export-po` | 플래그(`-이름`) | 번역 교환 — 문화권마다 `po/<culture>.po` 를 쓴다 · PO 하나를 번역 표로 가져온다(`-import-po=<파일>`). | `EXPORT_PO` |
| `-import-po` | 글 값(`-이름=값`) | 번역 교환 — 문화권마다 `po/<culture>.po` 를 쓴다 · PO 하나를 번역 표로 가져온다(`-import-po=<파일>`). | `IMPORT_PO` |
| `-scenario` | 글 값(`-이름=값`) | 자동화 시나리오 — 값은 시나리오 파일(리소스 경로 `game/<팩>/automation/x.scenario.xml` 또는 절대 경로). 끝나면 결과를 종료 코드로 낸다 (0 통과 · 10 실패 · 11 읽기 오류 · 12 시간 초과 · 13 건너뜀). 형식은 `Source/Engine/Automation/README.md`. | `SCENARIO` |
| `-scenario-report` | 글 값(`-이름=값`) | 시나리오 결과 JSON 을 쓸 경로(비면 쓰지 않는다). | `SCENARIO_REPORT` |
