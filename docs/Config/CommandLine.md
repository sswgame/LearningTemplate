<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# 명령줄 인자

[설정 색인](README.md)

키 앞의 하이픈은 `-` 든 `--` 든 같습니다. 전역 변수는 [`-gv_<이름>=<값>`](GlobalVariables.md) 로 따로 받습니다.

| 철자 | 값 | 설명 | 코드 이름 |
|---|---|---|---|
| `-help` | 플래그(`-이름`) | 받는 인자 목록(철자와 값 모양)을 표준 출력에 찍고 끝낸다 — 창 · 렌더러를 세우지 않는 헤드리스 작업이다. 설명은 이 문서다 | `HELP` |
| `-W` | 숫자 값(`-이름=값`) | 창 클라이언트 영역 너비 · 높이(픽셀) — EngineConfig · 플레이어 해상도보다 이긴다 | `WIDTH` |
| `-H` | 숫자 값(`-이름=값`) | 창 클라이언트 영역 너비 · 높이(픽셀) — EngineConfig · 플레이어 해상도보다 이긴다 | `HEIGHT` |
| `-WinX` | 숫자 값(`-이름=값`) | 창 틀 왼쪽 위의 화면 좌표(픽셀) — 둘을 함께 준다(하나만 주면 창을 만들지 않고 멈춘다). 없으면 플랫폼이 놓는다. 에디터 멀티 프로세스 재생이 클라이언트 창을 나란히 놓는다 | `WINDOW_X` |
| `-WinY` | 숫자 값(`-이름=값`) | 창 틀 왼쪽 위의 화면 좌표(픽셀) — 둘을 함께 준다(하나만 주면 창을 만들지 않고 멈춘다). 없으면 플랫폼이 놓는다. 에디터 멀티 프로세스 재생이 클라이언트 창을 나란히 놓는다 | `WINDOW_Y` |
| `-vsync` | 플래그(`-이름`) | 수직 동기화를 켠다 — EngineConfig · 플레이어 값보다 이긴다 | `VSYNC` |
| `-dx11` | 플래그(`-이름`) | DirectX11 백엔드로 띄운다. 이 백엔드로 띄운다(EngineConfig `_defaultRHI` 를 이긴다). 줄과 철자는 쿠킹 표(Config/Engine/CookContract.json)의 `rhi_backends` — 백엔드마다 `command_line_name` 하나(`-dx11` · `-dx12` · `-vk` · `-gl`) | `DIRECTX_11` |
| `-dx12` | 플래그(`-이름`) | DirectX12 백엔드로 띄운다. 이 백엔드로 띄운다(EngineConfig `_defaultRHI` 를 이긴다). 줄과 철자는 쿠킹 표(Config/Engine/CookContract.json)의 `rhi_backends` — 백엔드마다 `command_line_name` 하나(`-dx11` · `-dx12` · `-vk` · `-gl`) | `DIRECTX_12` |
| `-vk` | 플래그(`-이름`) | Vulkan 백엔드로 띄운다. 이 백엔드로 띄운다(EngineConfig `_defaultRHI` 를 이긴다). 줄과 철자는 쿠킹 표(Config/Engine/CookContract.json)의 `rhi_backends` — 백엔드마다 `command_line_name` 하나(`-dx11` · `-dx12` · `-vk` · `-gl`) | `VULKAN` |
| `-gl` | 플래그(`-이름`) | OpenGL 백엔드로 띄운다. 이 백엔드로 띄운다(EngineConfig `_defaultRHI` 를 이긴다). 줄과 철자는 쿠킹 표(Config/Engine/CookContract.json)의 `rhi_backends` — 백엔드마다 `command_line_name` 하나(`-dx11` · `-dx12` · `-vk` · `-gl`) | `OPENGL` |
| `-EnableEditor` | 플래그(`-이름`) | 에디터 모듈을 올린다(Dev 만) — 없으면 에디터 없이 게임만 뜬다 | `ENABLE_EDITOR` |
| `-lang` | 글 값(`-이름=값`) | 시작 언어(`ko_kr` · `en_us` …) | `LANGUAGE` |
| `-cook-shaders` | 플래그(`-이름`) | 셰이더를 모든 백엔드의 바이너리로 쿠킹하고 끝낸다(헤드리스) | `COOK_SHADERS` |
| `-cook-scenes` | 플래그(`-이름`) | 씬과 프리팹을 바이너리(SCN1, PFB2)로 쿠킹하고 끝낸다(GUID 레지스트리 · VAT · 내비메시 포함). 쓸 폴더는 `-cooked-dir=<폴더>` 다 | `COOK_SCENES` |
| `-cook-packs` | 플래그(`-이름`) | 리소스 팩(engine · common · game_<팩>.pack)을 `-pack-dir=<폴더>` 에 굽고 끝낸다. 스테이징(`-cooked-dir`)의 쿠킹 산출물을 함께 담는다. `-game=` 을 주면 그 게임 팩만, 없으면 모든 게임 팩이다 | `COOK_PACKS` |
| `-cook-all` | 플래그(`-이름`) | 씬 쿠킹(`-cook-scenes`)과 팩 쿠킹(`-cook-packs`)을 차례로 하고 끝낸다 — 빌드의 CookAssets 타깃과 배포 패키징이 부른다 | `COOK_ALL` |
| `-cooked-dir` | 글 값(`-이름=값`) | `-cook-scenes` · `-cook-packs` · `-cook-all` 이 쿠킹 결과를 쓸 스테이징 폴더 | `COOKED_DIR` |
| `-pack-dir` | 글 값(`-이름=값`) | `-cook-packs` · `-cook-all` 이 팩을 쓸 폴더 | `PACK_DIR` |
| `-pack-build` | 글 값(`-이름=값`) | `-cook-packs` · `-cook-all` 이 팩과 함께 `-pack-dir` 에 남기는 빌드 내용 목록(`base.packbuild.xml` — 설치된 빌드의 출처)의 빌드 번호. 비우면 번호도 내용 목록도 없는 `patching="none"` 기록을 남긴다(패치를 쓰지 않는 빌드). `-make-patch` 에서는 새 빌드 번호다 | `PACK_BUILD` |
| `-make-patch` | 플래그(`-이름`) | 새 빌드의 팩(`-pack-dir`)과 이전 빌드의 내용 목록(`-patch-base=<파일>.packbuild.xml`)의 차이로 패치 팩 · 패치 목록 · 새 빌드의 내용 목록을 `-patch-dir` 에 만들고 끝낸다. 새 빌드 번호는 `-pack-build` 다 | `MAKE_PATCH` |
| `-patch-base` | 글 값(`-이름=값`) | `-make-patch` 의 이전 빌드 내용 목록 파일 | `PATCH_BASE` |
| `-patch-dir` | 글 값(`-이름=값`) | `-make-patch` 가 패치를 쓸 폴더 | `PATCH_DIR` |
| `-verify-shaders` | 플래그(`-이름`) | 커밋된 셰이더 바이너리가 지금 소스에서 나온 것인지(`cook.stamp` · 매니페스트) 본다. 혼자면 쿠킹 표의 모든 백엔드를 보고 문제가 있으면 실패로 끝난다(커밋 훅). `-cook-all` · `-cook-packs` 와 함께면 팩에 담을 백엔드만 보고, 낡았으면 다시 쿠킹한 뒤 다시 보며 그래도 맞지 않으면 팩을 굽지 않는다(배포 빌드) | `VERIFY_SHADERS` |
| `-server-config` | 글 값(`-이름=값`) | 전용 서버(Server)의 운영 설정 파일(`ServerConfig`) — 비우면 `Config/Server/<게임>.json`(`-game=` 으로 고른 게임). Shipping 도 디스크에서 읽는다. | `SERVER_CONFIG` |
| `-game` | 글 값(`-이름=값`) | 돌릴 게임(Dev 만) — `Source/Games/<이름>`. 비우면 빌드의 활성 게임(`SW_ACTIVE_GAME`), 다른 게임은 같은 빌드가 함께 지은 것(`SW_GAMES`, `Bin/Games/<이름>/`)이어야 한다. Shipping 은 구운 게임 하나라 보지 않는다(`GameSelection`) | `GAME` |
| `-service` | 플래그(`-이름`) | 전용 서버를 Windows 서비스(SCM)로 돈다(`Server --service`). SCM 이 띄운 프로세스만 된다. | `SERVICE` |
| `-write-reflection-docs` | 글 값(`-이름=값`) | 등록된 리플렉션 타입(엔진 · GameFramework · 킷 · 게임)으로 Markdown API 문서를 그 폴더에 쓰고 끝난다(헤드리스). 빌드 산출물이라 커밋하지 않는다. | `WRITE_REFLECTION_DOCS` |
| `-import-textures` | 플래그(`-이름`) | 텍스처 원본(`textures_raw/`)을 DDS 로 임포트한다 · 원본과 DDS 가 맞는지 보기만 한다. 에디터 모듈의 일이라 Dev 빌드에서만 된다. | `IMPORT_TEXTURES` |
| `-check-textures` | 플래그(`-이름`) | 텍스처 원본(`textures_raw/`)을 DDS 로 임포트한다 · 원본과 DDS 가 맞는지 보기만 한다. 에디터 모듈의 일이라 Dev 빌드에서만 된다. | `CHECK_TEXTURES` |
| `-import-models` | 플래그(`-이름`) | 모델 원본(`models_raw/` 의 glTF)을 `.mesh` 로 임포트한다 · 원본과 `.mesh` 가 맞는지 보기만 한다. 에디터 모듈의 일이라 Dev 빌드에서만 된다. | `IMPORT_MODELS` |
| `-check-models` | 플래그(`-이름`) | 모델 원본(`models_raw/` 의 glTF)을 `.mesh` 로 임포트한다 · 원본과 `.mesh` 가 맞는지 보기만 한다. 에디터 모듈의 일이라 Dev 빌드에서만 된다. | `CHECK_MODELS` |
| `-import-lipsync` | 플래그(`-이름`) | 음성(`voice/` 폴더의 .wav · .ogg)을 분석해 곁에 비즘 트랙(`.visemes.xml`)을 쓴다 — 립싱크 오프라인 분석(엔진 일, 헤드리스). | `IMPORT_LIPSYNC` |
| `-bake-retarget` | 글 값(`-이름=값`) | 리타깃 베이크. 값은 `<프로필 *.retarget.xml>,<원본 .animclip>,<출력 .animclip>` 이다. 프로필의 두 스켈레톤으로 원본 클립을 옮겨 베이크하고 끝낸다(헤드리스). | `BAKE_RETARGET` |
| `-render-portraits` | 글 값(`-이름=값`) | 프리팹 초상화(썸네일)를 격리된 스튜디오에서 그려 DDS · PNG 로 쓰고 끝낸다 — 값은 쉼표로 나눈 프리팹 경로, 크기는 정사각 한 변(px), 폴더는 쓸 곳(기본 Saved/Portraits). | `RENDER_PORTRAITS` |
| `-portrait-size` | 숫자 값(`-이름=값`) | 프리팹 초상화(썸네일)를 격리된 스튜디오에서 그려 DDS · PNG 로 쓰고 끝낸다 — 값은 쉼표로 나눈 프리팹 경로, 크기는 정사각 한 변(px), 폴더는 쓸 곳(기본 Saved/Portraits). | `PORTRAIT_SIZE` |
| `-portrait-dir` | 글 값(`-이름=값`) | 프리팹 초상화(썸네일)를 격리된 스튜디오에서 그려 DDS · PNG 로 쓰고 끝낸다 — 값은 쉼표로 나눈 프리팹 경로, 크기는 정사각 한 변(px), 폴더는 쓸 곳(기본 Saved/Portraits). | `PORTRAIT_DIR` |
| `-import-heightfields` | 플래그(`-이름`) | 높이장 원본(`heightfields_raw/` 의 16 비트 PNG · `.r16`)을 `.heightfield` 로 임포트한다 · 맞는지 보기만 한다. 에디터 모듈의 일이라 Dev 빌드에서만 된다. | `IMPORT_HEIGHTFIELDS` |
| `-check-heightfields` | 플래그(`-이름`) | 높이장 원본(`heightfields_raw/` 의 16 비트 PNG · `.r16`)을 `.heightfield` 로 임포트한다 · 맞는지 보기만 한다. 에디터 모듈의 일이라 Dev 빌드에서만 된다. | `CHECK_HEIGHTFIELDS` |
| `-gather-text` | 플래그(`-이름`) | 로컬라이제이션 글 수집(에디터 모듈의 `LocalizationTools`) — 코드 · 데이터에서 모아 원문 표를 고친다 · 고치지 않고 최신인지만 본다. 소스 트리가 있어야 하고 Dev 빌드에서만 된다. `-loc-project=<프로젝트 파일>` 이 없으면 엔진 프로젝트와 활성 게임 팩의 프로젝트 전부다. | `GATHER_TEXT` |
| `-check-text` | 플래그(`-이름`) | 로컬라이제이션 글 수집(에디터 모듈의 `LocalizationTools`) — 코드 · 데이터에서 모아 원문 표를 고친다 · 고치지 않고 최신인지만 본다. 소스 트리가 있어야 하고 Dev 빌드에서만 된다. `-loc-project=<프로젝트 파일>` 이 없으면 엔진 프로젝트와 활성 게임 팩의 프로젝트 전부다. | `CHECK_TEXT` |
| `-loc-project` | 글 값(`-이름=값`) | 로컬라이제이션 글 수집(에디터 모듈의 `LocalizationTools`) — 코드 · 데이터에서 모아 원문 표를 고친다 · 고치지 않고 최신인지만 본다. 소스 트리가 있어야 하고 Dev 빌드에서만 된다. `-loc-project=<프로젝트 파일>` 이 없으면 엔진 프로젝트와 활성 게임 팩의 프로젝트 전부다. | `LOC_PROJECT` |
| `-export-po` | 플래그(`-이름`) | 번역 교환 — 문화권마다 `po/<culture>.po` 를 쓴다 · PO 하나를 번역 표로 가져온다(`-import-po=<파일>`). 에디터 모듈의 일이라 Dev 빌드에서만 된다. | `EXPORT_PO` |
| `-import-po` | 글 값(`-이름=값`) | 번역 교환 — 문화권마다 `po/<culture>.po` 를 쓴다 · PO 하나를 번역 표로 가져온다(`-import-po=<파일>`). 에디터 모듈의 일이라 Dev 빌드에서만 된다. | `IMPORT_PO` |
| `-crash-reporter` | 글 값(`-이름=값`) | 크래시 보고 프로세스 — 값은 크래시 묶음 폴더. 앞 실행이 띄운다(`CrashReportService::launchReporterProcess`). 엔진은 명령줄까지만 세우고(크래시 핸들러 · 리소스 루트 · 서비스 없이) 대기 중인 묶음을 보낸 뒤 끝낸다(헤드리스). | `CRASH_REPORTER` |
| `-scenario` | 글 값(`-이름=값`) | 자동화 시나리오 — 값은 시나리오 파일(리소스 경로 `game/<팩>/automation/x.scenario.xml` 또는 절대 경로). 끝나면 결과를 종료 코드로 낸다 (0 통과 · 10 실패 · 11 읽기 오류 · 12 시간 초과 · 13 건너뜀). 형식은 `Source/Engine/Automation/` 의 헤더 주석. | `SCENARIO` |
| `-scenario-report` | 글 값(`-이름=값`) | 시나리오 결과 JSON 을 쓸 경로(비면 쓰지 않는다). | `SCENARIO_REPORT` |
| `-scenario-keep-going` | 플래그(`-이름`) | 시나리오 여럿(`-scenario=a,b,c`)에서 한 단계가 실패해도 다음 단계를 돌려 실패를 한꺼번에 모은다(기본은 첫 실패에서 멈춘다). 건너뜀(13)은 실패로 세지 않고 이어 간다. | `SCENARIO_KEEP_GOING` |
| `-scenario-record` | 글 값(`-이름=값`) | 플레이를 시나리오로 녹화한다 — 값은 `Saved/Automation/` 아래의 `.scenario.xml`. 씬 플레이 첫 프레임부터 입력을 적고 끝날 때 쓴다(`-scenario` 와 함께면 그 가상 입력도 적는다). | `SCENARIO_RECORD` |
| `-scenario-record-probes` | 글 값(`-이름=값`) | 녹화 끝 프레임의 검사점 — 쉼표로 가른 `탐침`, `탐침@대상`, `탐침@대상@키`. 값이 녹화 끝에 `<Expect>` 로 실린다. | `SCENARIO_RECORD_PROBES` |
| `-renderdoc` | 플래그(`-이름`) | RenderDoc 을 RHI 디바이스보다 먼저 설치 경로에서 올린다(Dev 만 — `RenderDocCapture`). 없어도 RenderDoc UI 로 띄운 실행이면 캡처할 수 있다. | `RENDERDOC` |
| `-unattended` | 플래그(`-이름`) | 사람이 지켜보지 않는 실행이다 — 에디터를 켜도 단언 대화상자를 걸지 않는다(Debug 단언은 지금처럼 멈춘다). App 을 띄우는 테스트 · 스크립트가 준다. | `UNATTENDED` |
