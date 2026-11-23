# FolderLayout

> 결론: 이름 낱말(Util · Registry · Component)이나 처음 쓰는 쪽이 아니라 **하는 일**로 자리를 정한다. 두 자리가 다 맞으면 낮은 층에 둔다. 층 규칙은 빌드가 막고([CppStyle](CppStyle.md) "Includes and layers"), 표에 없는 폴더는 configure 가 멈추므로 새 폴더는 층을 정하고 들어온다(층 표 `Config/Rules/Layers/*.toml`). 옮기기는 `py -3 -m Scripts move-source --base . <옛>=<새>`(`git mv` 금지).

## 1 타깃

| 타깃 | 두는 것 | 두지 않는 것 |
| --- | --- | --- |
| `Core` | 엔진 · 게임을 모르는 기반(형식 · 컨테이너 · 글자 · 수학 · 스레드 · 파일 · 로그 · 네트워크 전송) | 씬 · 에셋 · 리플렉션 · 게임 낱말 |
| `Engine` | 런타임 제품 기능과 그 확장점 | 에디터만 부르는 함수(`WITH_EDITOR` 없음), 개발 도구 구현 |
| `DevTools` | 엔진 확장점에 스스로 붙는 개발 도구(배포 제외). 클라이언트 전용은 `DevTools/Client`, 머티리얼 그래프 컴파일러는 `DevTools/Material`, 쿠킹 명령 · 쿠커는 `DevTools/Cook`(셰이더는 `DevTools/Client/Cook`) | 게임 · GF · ModuleManager 내부를 아는 것 |
| `ModuleManager` | 모듈 로드 · 매니페스트 · 호스트 공용 기동. 개발 전용 핫 리로드는 `Dev/LiveReload` | 엔진 기능 |
| `ModuleAPI` | 모듈 C-ABI 계약(헤더만) | 구현 |
| `App` · `Server` | 그 실행 파일에만 있는 호스트 배선 | 엔진 기능, 개발 도구 구현(붙이는 줄만) |
| `Editor` | 에디터 기능 전부(키트 하나의 에디터 확장은 그 키트 `Editor/`) | 런타임이 쓰는 것 |
| `GameFramework` | 장르 공통 틀(`Runtime`) · 키트(`Kits`) · 에디터 플러그인(`Plugin`). 클라이언트 전용은 `Client/` | 범용 수학 · 컨테이너 |
| `Programs` | 따로 빌드하는 도구 실행 파일 | |
| `Games` | 게임 하나(새 게임 폴더를 만들지 않는다 — Playground) | |

## 2 Core 폴더 (티어 = `CoreLayers.toml`, 숫자 큰 쪽이 위)

| 폴더(티어) | 두는 것 |
| --- | --- |
| `Foundation`(0) | 형식 별칭 · 매크로 · OS 헤더 · enum 깃발 · 글자 하나 수준 도구 |
| `Hash`(0) | 해시 · 씨앗 상수 · 비밀 비교 · PRF |
| `Predefined`(0) | `.xxx` 이름 표 |
| `Concurrency`(1) | 원시 동기화와 경합 검출 훅. 스레드 사이 자료 구조는 `Concurrency/Queue`(Container 위 티어) |
| `Math`(1) | 벡터 · 행렬 · 쿼터니언 · 색 · 압축 수치 · 결정적 난수 · 노이즈 |
| `Memory` · `Algorithm`(2) | 할당기 · 메모리 태그 · 블록, 컨테이너에 묶이지 않은 알고리즘 |
| `Container`(3) | 컨테이너 · 핸들 표 · 슬롯 · 링 · 큐 · 그래프 자료 구조 |
| `String` · `Encoding` · `Serialization`(4) | 로그 없는 글자 도구, 바이트 ↔ 글자 인코딩, 바이트 · 비트 직렬화 |
| `Delegate` · `Path` · `UUID` · `Subsystem` · `Spatial`(4) | 이름 그대로 |
| `Log`(5) | 로그 매크로 · 창구 · 싱크 포트(구현 `Log/Sink` 는 맨 위) |
| `Name` · `Registry`(6) | intern 이름(`hashed_string` · `TagID`), `RegistrationList` · `NameRegistry` |
| `CommandLine` · `Compression` · `Process` · `Time`(6), `GlobalVariable` · `Task`(7), `File` · `Network`(8), `Module`(9), `Diagnostics` · `Event` · `Testing`(10) | 이름 그대로 |

## 3 Engine 폴더 (층 = `EngineLayers.toml`)

| 폴더(층) | 두는 것 |
| --- | --- |
| `Foundation` · `Compression`(0) | 아래 층이 앞으로 선언만 하는 핸들 · 엔진 상수 · 서비스 창구, 압축 코덱 |
| `Reflection` · `Serialization` · `Config` · `Content` · `Console` · `Observability` · `Curve` · `CoreServices` · `Behavior`(1) | 이름 그대로(`Behavior` 는 조건식 · 동작 어휘) |
| `Profiling`(1) | 런타임 계측과 그 포트만 — 보고서를 쓰는 쪽은 DevTools |
| `Asset` · `Text` · `Localization` · `Presentation`(2) | 에셋 · 글 배치 · 번역, 게임 코드가 UI · 오디오 없이 아는 표현 창구(I*Sink · I*Driver · 뷰모델) |
| `Window` · `Input`(3), `Graphics`(4), `Object` · `Scene`(5) | |
| 기능 폴더(6~9) | 컴포넌트는 `<기능>/Component`, 씬 서비스 · 시스템은 `<기능>/Scene`, 데이터는 `<기능>/` 바로 아래. 층: 바닥(6) → 애니메이션(7) → 합성(8) → 알림 디스패치(9). 같은 층끼리 include 하면 린트 `layer-cycles` 가 멈춘다 |
| `Renderer`(10) | 렌더 스레드 · 프레임 그래프 · GPU 파이프라인 |
| `Module` · `Automation`(11) | 엔진 루프 조립 · 핫 리로드 바인딩 · 자동화 창구. 실행기 · 판정 · 녹화는 `DevTools/Client/Automation` |

GameFramework 런타임(`GameFrameworkLayers.toml`): `Foundation`(0) 게임 서비스 · 저장 · 게임 데이터 · 격자 · 난수 · 조건 어휘 → `Online`(1) 세션 · HTTP · 보안 제공자 → `World`(2) 땅 · 환경 값 · 월드 질의 → `Character`(3) 조종 · 이동 · AI · 전투 · 카메라 · 길찾기 → `Gameplay`(4) 어빌리티 · 외형 · 인벤토리 · 퀘스트 · 판 상태 · 연출 · 존 · 게임 인스턴스 조립.

CMake(`cmake/` — 폴더 하나에 파일 종류 하나): `Modules/<Build·Environment·Config>/` 는 `include()` 만 하는 모듈, `Toolchain/` 플래그 · 툴체인, `Scripts/` 는 `cmake -P` 전용, `Templates/` 는 `configure_file` 입력, 빌드 정책 데이터는 `Config/Build/`, 테스트 전용 `cmake -P` 드라이버는 `Test/<영역>/Run*.cmake`.

## 4 그 밖의 규칙

| 규칙 | 까닭 |
| --- | --- |
| `Util` · `Utility` · `Helpers` · `Misc` 폴더 금지, `Common` 도 새로 만들지 않는다(`Editor/Common` 만). 린트 `folder-layout` 강제(예외 `Config/Rules/Lint/FolderLayout.json`) | 잡동사니 폴더는 하는 일로 찾을 수 없다 |
| `<기능>/Component` 에는 `*Component` 만(린트 `folder-layout` 강제) | 씬 서비스 · 질의는 `<기능>/Scene` |
| 키트 폴더는 평평하게: 장르 키트는 카탈로그 · 규칙 파일(엔진 시계 · 렌더를 모른다)을 키트 바로 아래, 하위 폴더는 `Component/`(디렉터 · 컴포넌트) · `Client/`(뷰 · 입력 · HUD — 따로 짓는 `GF_Client_<X>`, 디렉터의 명령 · 읽기 API 만 쓴다) · `Editor/` · `Tests/` 와 하위 게임만. 뼈대는 `new-kit`. 온라인 키트는 `Runtime/` · `Server/` 바로 아래(뼈대 `new-online-kit`) | 키트는 파일 10 ~ 20 개 |
| 개발 기능은 `Source/DevTools/`. `Dev/` 폴더는 `Config/Rules/Layers/DevFolders.json` 에 까닭과 함께 적은 자리만(configure 강제) | DevTools 는 GF · 게임 · ModuleManager · App 을 못 본다 — 엔진이 확장점을 내고 DevTools 가 붙는다 |
| 실행 중 바꾸기 · 핫스왑은 개발, 기동 때 고르기는 제품 | 언리얼도 RHI · 모듈 교체는 재시작 |
| 층 경계는 실제 순환 · 독립 배포 요구가 확인된 층만 별도 링크 타깃으로 나눈다 | 타깃 수만 는다 |
| 옮기지 않는 것: `Core/Foundation/BitFlagTrait.h`(생성 헤더가 include 하는 잎), `Engine/UI/Screen/UIScreenSwitch`(확장점 자체), `Core/Diagnostics/SubsystemTimingReport` · `Engine/Profiling/FrameProfileReport`(보고 값 형식) | |

## 5 콘텐츠

- Everything under `Content/` is lowercase (`[a-z0-9_.-]+`); the one exception is `README.md`.
- **Commit sources only.** Results and `import.stamp` are generated and git-ignored. Delete an unreferenced source instead of moving it.
  - Textures: runtime reads DDS only; sources under `textures_raw/` at the same relative path; `App --import-textures` writes, `App --check-textures` fails on drift. Import rules (`Config/Editor/TextureImportConfig.json`) must not overlap.
  - Models: glTF under `models_raw/`; `App --import-models` writes `models/<name>.mesh` (`--check-models`). PBR glTF materials become `models/<name>/materials/<material>.material` from `engine/materials/pbrmaterial.material`.
- **Never reference `_shared` import results** (`<domain>/models/_shared/…`, `<domain>/textures_raw/_shared/…`) from hand-written data — reference the material slot or a clip set.
- **Animation sets** (`*.animset.xml`): a hand-written set derives with `_base="<set>"` and borrows with `<clip name set [source]/>`, never a `path`. Look clips up by name.
- **Domains**: `engine` · `common` · `game/<pack>` · `dlc/<name>` each cook into their own pack; an import writes only into its own domain. A game references only `common/` and `engine/`; `common/` never references a game, nor one game another. A source two domains need moves to `common/` with `move-asset`, with its `credits.md` line. A content type's module belongs to that domain (configure 강제, `Scripts/generate/content_modules.py`).
- **Move, rename or delete an asset** with `py -3 -m Scripts move-asset <old id> <new id>` (pre-commit lint `asset_references.py` lists dangling refs). A moved file with `SW_LOCKEY` needs `App --gather-text` and the `Content/**/localization/*.strings.xml` commit.
- **Scanned assets (Poly Haven, CC0)**: `models_raw/polyhaven/`, `textures_raw/polyhaven/<asset>/`, binaries in Git LFS, each in the pack's `credits.md`. Tiling surfaces use `pbr_lit` with `_worldTileSize`, not stretched UVs. 라이선스 기준은 [NEXT](../NEXT.md) "지킬 결정".
- **Localization key**: entry in `editor.strings.xml` / `engine.strings.xml` → `App -export-po` → fill `po/<culture>.po` → `App -import-po=<po>` → `App --gather-text` → `App --check-text` (0 errors). No English literal UI text, no key without translations.
