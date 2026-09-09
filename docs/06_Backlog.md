# 작업 백로그 — 남은 일과 이어받기

> 목적: 여러 PC·여러 세션에서 이어서 작업하기 위한 **단일 할 일 목록**이다. 무엇이 끝났고
> 무엇이 남았는지, 남은 것을 왜 그 순서로 두었는지, 손대기 전에 알아야 할 함정이 무엇인지를
> 여기 적는다. 작업을 끝내면 이 문서의 해당 항목을 지우거나 "완료"로 옮기고 같이 커밋한다.
>
> 마지막 갱신: 2026-09-09 · 기준 커밋 `e6973186`

---

## 0. 손대기 전에 — 검증 규칙

에디터 패널에는 **단위 테스트가 없다.** 컴파일이 통과해도 화면이 비어 있을 수 있다.
따라서 패널·위젯을 건드렸다면 반드시 실행해서 확인한다.

```powershell
cmake --build --preset Ninja-Debug            # 경고 0 이어야 한다
cmake --build --preset Ninja-Shipping         # Debug 가 숨기는 결함이 여기서 드러난다
ctest --preset Ninja-Debug-lint               # 컨벤션·include 순서

# 테스트 (현재 기준선)
#   Debug    : CoreTest 165 / EngineTest 420 / ReflectionTest 100 / EditorTest 27 / SmokeTest 19
#   Shipping : 157 / 418 / 96 / 27 / 1        ← 차이는 전부 Dev 전용 케이스의 정상 스킵
ctest --test-dir build/Ninja-Debug -L nogpu
ctest --test-dir build/Ninja-Shipping -L nogpu

# 에디터 실기동 — 패널 변경의 유일한 실질 검증 수단
cd build/Ninja-Debug/Bin
./App.exe -gv_profileFrames=40 -dx12 -EnableEditor    # 종료 코드 0, 로그에 [Error] 0건
./App.exe -gv_profileFrames=40 -dx11 -EnableEditor
```

**함정**

- `-gv_rhiBackend` 는 App 이 무시한다. `-dx11 / -dx12 / -vk / -gl` 을 쓴다.
- Shipping 테스트 바이너리는 `build/Ninja-Shipping/TestBin/` 에 있고 **작업 디렉터리는 `Bin/`**
  이어야 한다(리소스를 상대 경로로 찾는다). `Bin/` 에 낡은 테스트 exe 사본이 남아 있을 수 있으니
  `TestBin/` 쪽을 직접 실행할 것.
- 셰이더 소스(.hlsl/.hlsli)를 고쳤으면 재베이크가 필요하다. Shipping 쿠킹은
  `bake.stamp` 의 내용 해시로 이를 검증하고 어긋나면 빌드를 세운다
  (`Scripts/generate/CookAssets.py --verify-shaders`). 베이커가 있으면 스스로 다시 굽는다.

---

## 1. 남은 일 (우선순위 순)

### 1-1. 공용 위젯을 안 쓰는 패널 정리 — **진행 중**

공용 위젯은 이미 충분하다(`Common/Widgets/EditorWidgets.h` 27개 +
`Common/Gui/EditorChrome.h` 12개). 문제는 **채택률**이다.

| 패널 | ImGui 직접 호출 | 공용 위젯 사용 |
|---|---|---|
| `InputMapEditorPanel` | 313 | **0** |
| `ProfilerPanel` | 94 | **0** |
| `PrefabEditorPanel` | 52 | **0** |
| `TileMapPanel` | 42 | 1 |
| `SpriteClipPanel` | 40 | 1 |

**빈 상태 안내는 마쳤다.** 82곳의 `ImGui::TextDisabled` 를 한 곳씩 보고 **본문 빈 상태인 12곳만**
`EditorWidgets::drawEmptyHint` 로 옮겼다(ContentBrowser 1 · GlobalVariables 1 · InputMapEditor 3 ·
Inspector 3 · Material 1 · Profiler 3).

옮기지 **않은** 자리와 이유 — 다음에 같은 판단을 반복하지 않도록 적어 둔다:

- **메뉴 안 3곳** (`Hierarchy` 의 컴포넌트 추가 메뉴 2곳, `Inspector` 의 프리셋 메뉴 1곳).
  `EndMenu()` 로 닫히는 팝업 안이라 백로그의 경고대로 두었다.
- **상태줄 2곳** (`Sequencer` 의 파일명 라벨, `Inspector` 의 "Scene edits locked" 칩 옆).
  `SameLine` 으로 붙은 인라인 라벨이고, 한쪽 분기만 바꾸면 같은 줄이 두 API 로 갈린다.
- **진행 상태 1곳** (`GlobalVariables` 의 "Scanning presets...").  비어 있는 게 아니라 **기다리는**
  중이다. 빈 상태와 로딩 상태는 나중에 다르게 보여야 할 자리다.

남은 것은 표의 **호출 수** 자체다 — `InputMapEditorPanel` 313 · `ProfilerPanel` 94 등은 빈 상태가
아니라 위젯·레이아웃 조립이라 1-2 의 골격 추출과 같이 가야 줄어든다.

### 1-2. 목록형 패널 골격 추출

여러 패널이 "툴바 → 검색 → 목록/표 → 상태줄" 이라는 같은 뼈대를 각자 조립한다.
문서형 패널은 `Common/Gui/EditorDocumentPanel` 이 이미 그 역할을 하지만 채택이 제한적이다.
목록형 골격(가칭 `EditorListPanel`)을 두면 새 패널이 본문만 채우면 된다.

범위가 넓고 검증이 실기동뿐이므로 **한 패널씩 옮기고 매번 실기동 확인**한다.

### 1-3. 100줄 넘는 함수 20개 — 우선순위 낮음

분해 자체는 코드 총량을 줄이지 않는다. 공통부 추출(1-1, 1-2)을 먼저 한다.
목록이 필요하면 다중 행 시그니처를 중괄호 깊이로 정확히 재는 스크립트를 만들어 뽑는다
(단순 정규식은 여러 줄 시그니처를 잘못 잰다).

### 1-4. 되살리지 못한 테스트

`Test/EditorTest/TestEditorSceneCommands.cpp` 는 되살렸지만(현재 EditorTest 27개에 포함),
`EditorContext` 가 UI 매니저 전부를 `unique_ptr` 로 소유하는 구조는 그대로다. 더 깊은 분리
(패널·팝업 매니저 소유를 컨텍스트 밖으로)는 영향 범위가 커서 하지 않았다. 필요해지면
그때 소유 구조부터 정한다.

### 1-5. 확인만 하고 넘어간 것

Shipping `EngineTest` 에서 `RHITest.CommandListCreationAndExecution` 이 **한 번** SEGFAULT
했고 재실행 3회는 모두 통과했다. EngineTest 는 Editor 를 링크하지 않으므로 에디터 변경과는
무관하다. 재현되면 따로 볼 것.

> 2026-09-09 추가: Shipping 빌드에 `FrameProfiler.cpp` 경고 3건(`avgUs`/`perFrameX10` 미사용,
> `pTitle` 미사용 파라미터)이 **예전부터** 있다. 보고 경로가 Shipping 에서 컴파일 아웃되면서 남은
> 변수들이다. App 개편과 무관하므로 건드리지 않았다.
>
> 2026-09-09 추가: "테스트는 초록인데 앱만 깨진다" 의 실례가 하나 나왔다(3절의 DX11 항목).
> 원인은 테스트 씬이 그 코드 경로를 **아예 안 태우고 있던** 것이었다. 위 SEGFAULT 도
> 재현을 기다리기보다 "그 테스트가 실제로 무엇을 태우는가" 를 먼저 보는 편이 빠를 수 있다.

---

## 2. 작업 방식 — 정해진 방향

- **쪼개기보다 공통부 추출.** 긴 함수를 나누면 코드가 이동할 뿐 총량은 그대로다.
  중복은 증상이고 원인은 "매번 다시 만들어야 하는 구조"다. 원인을 없앤다.
- **추가·변경에 용이한 구조를 먼저 만든다.** 새 타입·새 패널·새 백엔드를 하나 더 넣을 때
  복사해야 할 것이 남아 있으면 그 자리가 다음 리팩터 대상이다.
- 주석과 커밋 메시지는 한국어. 규칙은 [AGENTS.md](../AGENTS.md) 와
  [04_CodingGuidelines.md](04_CodingGuidelines.md).

### 편집 함정

- 한 함수에서 **여러 구간을 빼낼 때는 뒤쪽 구간부터** 한다. 앞쪽을 먼저 빼면 뒤쪽 줄 번호가
  밀려 `switch` 중간을 자르는 식으로 깨진다.
- 파일을 스크립트로 고칠 때 CRLF 를 보존한다. 이 저장소는 CRLF 다.

---

## 3. 최근에 끝낸 일 (2026-09-08 ~ 09)

무엇을 이미 해결했는지 알아야 같은 것을 다시 파지 않는다.

**Source 폴더를 순서대로 개편 중 (App → RuntimeAPI → Core → …)**

진행한 폴더: `App`(`6efa4fd2`) · `RuntimeAPI`(`06889dc7`) · `Core`(아래).
남은 폴더: `Engine` · `GameFramework` · `Games` · `Editor`, 그리고 `Tools/ReflectionParser`.

**Core — 같은 플랫폼 분기가 세 파일에 복사돼 있었다**
- `PlatformFileUtil` 신설. `fopen_s`↔`fopen`, `_fseeki64`↔`fseeko`, `_ftelli64`↔`ftello` 의
  `#if` 가 `FileUtil.cpp` 에 5벌, `Logger.cpp` 에 1벌, Engine 의 `ResourcePackReader.cpp` 에
  5벌 있었다. **`ResourcePackReader` 는 아직 안 고쳤다 — Engine 차례에 같이 한다.**
- `FileUtil.cpp` 의 플랫폼 분기 26 → 10, `Logger.cpp` 5 → 3, `StringUtil.cpp` 12 → 4.
- `FileUtil::getFileSize` 가 크기만 알려고 파일을 열고 끝까지 탐색했다 → `std::filesystem::file_size`.
  바로 위 `getFileTimestamp` 는 이미 그렇게 하고 있었다.
- `readTextFile` 이 BOM 바이트를 손으로 세고 있었다(같은 파일 아래 `skipUtf8Bom` 이 정본).
- `StringUtil` 의 로케일 변환 두 방향이 "크기 질의 → 버퍼 → 변환" 전체를 각자 적어 같은 `#if` 가
  네 벌이었다 → 원시 연산 2개(`wideToMultiByte` / `multiByteToWide`)로 내리고 나머지는 공유.
- **건드리지 않은 것**: `Container/` 의 `vector`/`map`/`string` 은 std 호환 커스텀 구현체다
  (3800줄). 표면이 std 와 같아야 하는 물건이라 "중복" 처럼 보이는 것이 실은 계약이다.
  `Memory/` 의 할당기 3종도 정렬 계산만 닮았고 수명 모델이 달라 묶을 공통부가 아니다.

**App 폴더 구조 개편 — 상용 엔진과 대조해서**
- **시간 정책을 `App/Frame/FrameTimeline` 로.** 루프 안에 `constexpr 1.0f/60.0f` 로 박혀 있던
  고정 스텝을 `EngineConfig::_fixedDeltaTime` / `_maxFixedStepPerFrame` 로 올렸다. 예전 코드의
  스텝 수 상한은 `_maxFrameDeltaTime` 클램프에 **우연히 의존**하고 있었다 — 누산기가 잔액을
  남기므로 보장이 아니었다. 이제 상한이 명시적이고, 넘긴 잔액은 버린다(고정 스텝 스파이럴 차단).
  UE 의 `MaxPhysicsDeltaTime`/`MaxSubsteps`, Unity 의 `fixedDeltaTime`/`maximumDeltaTime` 과 같은 자리.
- **프레임당 에디터 상태 래치 — `ModuleFrameState`.** `isPlaying`/`isPaused`/`getGameViewport` 를
  프레임 안에서 8~10회 따로 묻던 것을(고정 스텝마다 DLL 경계를 다시 넘었다) 두 지점 래치로 바꿨다.
  **래치 지점이 두 개인 것은 의도다**: 게임플레이 활성 여부는 `beginFrame`(게임 업데이트 이전),
  게임 뷰포트와 씬 틱 여부는 `updateEditorUI`(에디터가 입력을 처리한 이후)다. 후자를 프레임 앞으로
  옮기면 **에디터 Step 한 칸이 틱 없이 소비**되어 아무 일도 일어나지 않는다 — `ImGuiEditor::isPaused`
  가 `paused && !pendingStep` 이기 때문이다. 옮기려면 이 사실부터 확인할 것.
- **`ModuleHost::suspendModules( ModuleScope, bReleaseApiTable )` 로 4경로 통합.**
  `shutdown` / `onBeforeEditorReload` / `onBeforeGameReload` / RHI 핫스왑이 "배수 → 상태 보존 →
  파괴" 를 각자 조립하고 있었다. 덤으로 두 가지가 정리됐다: (1) 핫스왑 경로가 `drainRenderWorkers`
  를 **두 번** 돌았다(App 이 부르고 `onBeforeRhiSwap` 이 또 불렀다), (2) `onBeforeGameReload` 는
  `_game == nullptr` 이면 조기 반환해서 **언로드 직전에도 API 테이블과 타입 등록을 놓지 않았다**.
- **RHI 백엔드 교체를 `App/Rhi/BackendSwapController` 로.** 재진입 가드와 "배수 → 파괴 → 디바이스
  재생성 → 재생성" 순서를 아는 자리를 하나로 뺐다. `gv_rhiBackend` 훅을 `shutdown` 에서 **떼어 낸다** —
  예전에는 끊지 않아 GlobalVariableManager 가 죽은 뒤에도 훅이 App 을 가리켰다(실제로 터지진 않았다).
- **Shipping 에서 셸 ActionMap 을 돌리지 않는다.** `updateShellActions` 는 리소스에서 ActionMap 을
  올려 매 프레임 갱신하는데, Shipping 에는 그것을 질의하는 코드가 **하나도 없었다**.
  `pollReloadHotkeys` 를 `#if !defined( SW_SHIPPING )` 로 통째로 비웠다.
- 헤드리스 분기도 줄었다. `shutdown`/`run` 의 `isHeadless()` 특수 경로는 실제 선행 조건
  (`_window == nullptr`) 과 같았다 — 모드 플래그 대신 조건을 적는다.
- 검증: Debug/Shipping 빌드 경고 0(App 기준), lint 5/5, nogpu 테스트 Debug·Shipping 전부 통과,
  실기동 `-dx12 -EnableEditor` / `-dx11 -EnableEditor` / `-dx12`(에디터 없음) / Shipping `-dx12`
  모두 종료 코드 0 · `[Error]` 0건.
- **남은 것**: 백엔드 **실제 교체**(gv_rhiBackend 런타임 변경)는 에디터 UI 로만 낼 수 있어
  자동 검증을 못 했다. 다음에 에디터를 띄울 일이 있으면 백엔드를 바꿔 보고 `[Error]` 0건을 확인할 것.

**Shipping 이 아예 돌지 않던 문제 (4종)** — Debug 는 전부 조용히 삼키고 있었다.
- `SceneComponent::_pManager` 미초기화 → 소멸자에서 쓰레기 포인터 역참조 (`cdcd92ef`)
- `TypeRegistry` 가 타입 하나당 `TypeInfo` 를 **두 벌**(FQN·짧은 이름) 만들어, 그것을 키로 쓰는
  컴포넌트 풀이 항상 조회 실패 → 풀 메모리를 힙 해제로 반납 → 힙 손상. 즉 컴포넌트 풀은 한 번도
  회수된 적이 없었다 (`7b6d16c0`)
- 리소스 팩 경로·`/WHOLEARCHIVE` 누락(열거형 리플렉션 탈락)·`Archive` 읽기/쓰기 경로 비대칭
  (`eb954da3`)
- 셰이더 베이커가 런타임이 요구하는 퍼뮤테이션을 굽지 않던 문제 + 재발 방지용 `bake.stamp`
  내용 해시 검증 (`6d164397`, `3030c06c`)

**App/Editor 구조 정리**
- App 부팅 시퀀스 분리, ModuleHost 생명주기 중복 제거 (`bdfbf96e`)
- Editor 백그라운드 잡 6벌 → 공통 뼈대 1벌 + 테스트 3개 신설 (`03fb47c9`)
- `EditorUtil` 을 ImGui 에서 분리, 빌드에서 빠져 있던 테스트 복구 (`b4ba276e`, `02b0e7cc`)
- 활성 씬 접근자로 24곳 통일 + 널 검사 없던 역참조 3곳 수정 (`2c237fe2`)
- DXGI 뷰포트 보정을 백엔드 밖으로 (`cb99db8f`)
- 타입 이름·노드 id 변환·도구 패널 순회 통합 (`d9e0a21a`)
- 툴팁을 공용 위젯 하나로 — 지연·줄바꿈이 제각각이었다 (`488f6b4f`)
- 빈 상태 안내 12곳을 `EditorWidgets::drawEmptyHint` 로 (1-1 의 앞부분)

**에디터·Shader 폴더 재편** (`799484f3`)
- `Shader/` 를 `Compile/` · `Reflection/` · `Binding/` 으로. `Renderer/` 를 나눴을 때와 같은 기준.
- `Common/Workspace/` 에 섞여 있던 ImGui 드로잉 둘(토스트·우클릭 메뉴)을 `Common/Gui/` 로,
  세션 상태를 `Common/Workspace/` 로, `EditorCamera` 를 `Viewport/` 로.
- `EditorUtil::isXxxAssetPath` 7개 삭제 — `EditorAssetTypeRegistry::matches` 가 정본인데 입구가
  둘이었다.
- `EditorModule` Unity 빌드 ON. "ODR 충돌이 정리될 때까지" 라던 주석은 이미 유효하지 않았다.

**파일을 옮기면 include 말고도 깨지는 것들** (`799484f3`)
- `ReflectionParser` 가 헤더 이동을 감지 못 했다. `git mv` 는 mtime 을 안 바꾸는데 `.gen.cpp` 는
  원본을 절대경로로 include 한다 → 산출물 머리말의 `// Source:` 경로를 대조하게 했다.
- `sw_skipUnitySources` 가 없는 경로를 조용히 넘겨서 Renderer 재편 이후 제외 6개가 죽어 있었다
  → `FATAL_ERROR`.
- 병합에서 또 드러났다: `Test/EditorTest/CMakeLists.txt` 의 소스 목록. **경로를 문자열로 적어 둔
  곳**은 컴파일러가 안 잡는다.

**빌드 시스템·프레임워크·도구 정리** (`b5264c3e` ~ `3782977d`)
- CMake: `ModuleBuildRules.cmake` 598줄을 역할별 셋으로(BuildLayout / TargetRules / ThirdPartyLibs).
  `sw_flag_libraries` 를 `sw_global_options` 가 흡수해 타겟마다 쓰던 가드 11곳 제거.
  Clang.cmake 의 `$<BOOL:${MSVC}>` 32곳을 `if(MSVC)` 로 — 설정 시점 변수에 제너레이터 표현식은
  필요 없고, 그것 때문에 같은 개념을 드라이버마다 두 번 적고 있었다.
- GameFramework: 키트에서 게임 하나의 스키마를 걷어냈다(기술 슬롯 2칸 고정 → 데이터가 정함,
  보상 2종 고정 → 이름 맵). id 조회를 두 키트가 다른 방식으로 하던 것도 통일.
- ReflectionParser: `logger->shutdown()` 11곳을 RAII 하나로. 생성 파일을 지우면 플레이스홀더가
  최종 산출물로 굳어 영구히 링크가 깨지던 덫을 스탬프로 해소.
- Scripts: CheckSourceGlob 이 빌드 트리를 역알파벳순으로 골라 Unity 트리를 읽고 210개를
  오탐하던 것을 `.clangd` 기준으로. 출력 인코딩을 `common.useUtf8Stdout()` 한 곳으로(9개 전부).

**커밋된 `bake.stamp` 가 소스보다 낡아 있었다.** 네 백엔드 스탬프 모두 `instancesort.hlsl` 의
해시를 옛 값(`cd4ffd32…`)으로 적고 있었다. 구워진 `.dxbc`/`.spv` 자체는 현재 소스와 같았으므로
**기록만** 낡은 것이었고, Shipping 을 빌드할 때마다 작업 트리가 더러워졌다. 스탬프만 갱신해
따로 커밋했다. (처음 관찰에서 "94개 바이너리가 다시 구워진다" 고 적었는데, 그건 그 빌드 디렉터리의
첫 전체 쿠킹이었고 소스 변경 때문이 아니었다.)

**성능은 세 번 재고 세 번 기각했다.** GpuScene 배치 키 중복 계산(3670→3761us, 차이 없음),
린트 ProcessPool 전환(5.3→9.4s, 더 느림), 짝 헤더 파싱 메모이즈(차이 없음). 남긴 변경은
중복 제거·경계 정리 때문이지 성능 때문이 아니다. `GT.GpuScene.build.batches` 는 여전히 게임
스레드의 지배적 비용이고 원인은 못 찾았다 — 다음에 볼 때는 그 안을 더 잘게 재는 것부터.

**DX11 이 앱에서 아무것도 안 그리던 문제** (`913f13ae`)
- `instanceanim` 컴퓨트가 인스턴스 버퍼를 UAV 로 쓴 뒤 정점 셰이더가 SRV 로 읽는데, D3D11 은
  같은 리소스를 출력과 입력에 동시에 걸 수 없어 런타임이 **SRV 를 NULL 로 강제**했다.
  `transitionBuffer` 가 no-op 이라 UAV 를 안 뗐다 — 배리어는 없어도 의무는 있었다.
- 로그에 한 줄도 안 나온 이유: D3D11 은 이 해저드를 WARNING 으로 낸다. 심각도로 거르고 있었다
  → 해저드 ID 만 ERROR 로 올린다.
- 테스트가 못 잡은 이유: 패리티 테스트 씬에 `spinSeed` 가 없어 컴퓨트가 **아예 안 돌았다**
  → `RenderPassTest.InstanceAnimationKeepsInstancesReadable` 신설.
