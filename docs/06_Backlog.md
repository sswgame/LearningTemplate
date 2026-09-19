# 작업 백로그 — 남은 일과 이어받기

> 목적: 여러 PC·여러 세션에서 이어서 작업하기 위한 **단일 할 일 목록**이다. 무엇이 끝났고
> 무엇이 남았는지, 남은 것을 왜 그 순서로 두었는지, 손대기 전에 알아야 할 함정이 무엇인지를
> 여기 적는다. 작업을 끝내면 이 문서의 해당 항목을 지우거나 "완료"로 옮기고 같이 커밋한다.
>
> 마지막 갱신: 2026-09-20 · 기준 커밋 `9ab3bfae`

---

## 0. 손대기 전에 — 검증 규칙

에디터 패널에는 **단위 테스트가 없다.** 컴파일이 통과해도 화면이 비어 있을 수 있다.
따라서 패널·위젯을 건드렸다면 반드시 실행해서 확인한다.

**화면을 볼 수 없을 때는 `-gv_editorPanelDump=N` 을 쓴다.** N 번째 ImGui 프레임에 창 하나당 한 줄
(이름 · 크기 · 정점 수 · 활성/접힘/숨김)과 요약을 로그에 남긴다. **보이는데 정점이 0인 패널**이
곧 빈 패널이고, 컨테이너(자식이 내용을 든 창)와 순수 오버레이(`NoInputs`, 예: ImGuizmo 의 `gizmo`)는
빼고 센다. 패널을 고치기 전후로 이 블록을 비교하면 된다.

```powershell
cd build/Ninja-Debug/Bin
./App.exe -gv_profileFrames=40 -dx12 -EnableEditor -gv_editorPanelDump=25 > before.log
# ... 패널 수정 후 ...
./App.exe -gv_profileFrames=40 -dx12 -EnableEditor -gv_editorPanelDump=25 > after.log
```

**도구 패널까지 재려면 `-gv_editorOpenAllPanels=1` 을 같이 준다.** 기본 레이아웃에는 도구 패널
(Sequencer·Material·Prefab·TileMap·SpriteClip·AnimGraph·DialogueGraph·DataTable·InputMap)이 닫혀
있어서 덤프가 다섯 개만 본다. 이 스위치는 (1) 등록된 패널을 전부 열고, (2) 저장된 도킹 레이아웃을
적용하지 않으며(도킹하면 같은 노드의 탭 중 앞의 하나만 그려진다), (3) 첫 사용 크기를 900×620 으로
준다(ImGui 기본 크기는 내부 Child 를 9px 로 눌러 "내용 없음" 오탐을 만든다 — 실제로 봤다). 켠
실행의 가시성·레이아웃은 **저장하지 않는다.**

```powershell
./App.exe -gv_profileFrames=60 -dx12 -EnableEditor -gv_editorOpenAllPanels=1 -gv_editorPanelDump=40
```

현재 기준선 (2026-09-19 재측정, 시작 씬 = 테스트 씬): **기본 창 15개 · 내용 없는 패널 0개**, 전부 열면 **창 33개 ·
내용 없는 패널 0개**. (시작 씬이 없던 때는 기본 창 14개였다. 전부 열었을 때의 수는 2026-09-12 에 30개였고,
그 뒤 `RenderTargetPanel` 이 생기며 창 셋(`Render Targets` + 목록·미리보기 자식)이 늘어 33개가 됐다 —
**숫자가 달라지면 먼저 패널이 늘었는지 본다.** 의미 있는 신호는 "내용 없는 패널 0개" 쪽이다.)

> **2026-09-11 부터 기본 실기동도 씬을 본다.** `GameConfig._startupScene` 이 `game/empty/maps/editortest.scene.xml`
> 을 가리키고, Empty 게임이 벤치(`-gv_benchMeshes`)가 아니면 시작 시 그 씬을 연다 — 에디터 유무와 무관하게,
> 배포본도 같다. 예전엔 `SW_ACTIVE_GAME=Empty` 가 맵이 없어 `SceneManager` 가 씬 없이 뜨고 내려갔고,
> 오브젝트를 도는 코드(뷰포트 피킹 · 컴포넌트 시각화 · Hierarchy 트리 · Profiler 분포표 · 씬 세대 변경 훅)가
> 하나도 실행되지 않았다. 단, 테스트 씬의 메시는 `_meshId` 가 비어 있어 **화면에는 기하가 없다** — 스크린샷
> 비교가 필요하면 벤치 큐브(`-gv_benchMeshes`)를 쓴다.
>
> **다른 씬을 열려면** (`-gv_editorStartupScene` 은 게임 요청 뒤에 큐잉되어 이긴다):
>
> ```powershell
> # PowerShell 은 점이 든 인자를 쪼갠다 — **반드시 따옴표로 감쌀 것**
> ./App.exe -gv_profileFrames=60 -dx12 -EnableEditor `
>   "-gv_editorStartupScene=game/empty/maps/editortest.scene.xml" -gv_editorPanelDump=40
> ```
>
> 확인된 차이: Hierarchy 정점 **56 → 228**, Game View **742 → 1766**, 창 14개 → 15개.
> 즉 트리·콜라이더 와이어프레임·카메라 프러스텀이 실제로 그려진다.
> 애셋은 `Resource/game/empty/maps/editortest.scene.xml` 과
> `Resource/game/empty/prefabs/testprop.prefab.xml` 이고, **엔진 직렬화기로 생성**했다(손으로 쓴
> XML 이 아니다 — 임베디드 오브젝트 XML 은 리플렉션 산출물이라 손으로 쓰면 깨진다).
>
> 기본 게이트(스위치 없음)는 네 백엔드 모두 종료 코드 0 · `[Error]` 0건 · vtx=742,
> 테스트 씬을 열면 네 백엔드 모두 종료 코드 0 · `[Error]` 0건 · vtx=1766 이다.

```powershell
cmake --build --preset Ninja-Debug            # 경고 0 이어야 한다
cmake --build --preset Ninja-Shipping         # Debug 가 숨기는 결함이 여기서 드러난다
ctest --preset Ninja-Debug-lint               # 컨벤션·include 순서

# 정적 분석 (게이트 아님 — 판단이 필요한 자료다). 검사 목록은 루트 .clang-tidy 가 정한다.
py -3 Scripts/lint/report/RunClangTidy.py
py -3 Scripts/lint/report/RunClangTidy.py --filter Core

# ASan (Windows) — 2026-09-09 부터 실제로 빌드된다
cmake --preset Ninja-Debug-ASAN
cmake --build --preset Ninja-Debug-ASAN
ctest --test-dir build/Ninja-Debug-ASAN -L nogpu

# 테스트 (현재 기준선)
#   Debug    : CoreTest 209 / EngineTest 529 / ReflectionTest 104 / EditorTest 63 /
#              EditorUiTest 2 / AppTest 6 / SmokeTest 21   ← ctest -L nogpu 는 7/7
#   Shipping : 201(+8 skip) / 525(+3 skip) / 99(+5 skip) / 63 / 2 / 5 / 1   ← 스킵은 전부 Dev 전용 케이스
#   (2026-09-19 실측. Debug EngineTest 는 GPU 포함 전체 수다.
#    ctest 항목이 7개가 된 것은 `EditorUiTest`(2026-09-18)와 `AppTest`(2026-09-19)가 늘어서다.
#    **Debug ReflectionTest 의 스킵이 0이 됐다** — 파서 실행 파일을 `Bin` 에서만 찾던 검사가
#    `BuildTools` 도 보게 되어, 늘 건너뛰던 케이스가 실제로 돈다.)
#   WSL-Debug: ctest 12/12 (린트 6 포함). EngineTest 는 418 통과 + 8 skip = 426 이고,
#              스킵은 DX11/DX12 처럼 리눅스에 아예 없는 타깃들이다. (2026-09-11 실측)
#   ASan     : 5개 전부 통과한다(30초). SmokeTest 는 2026-09-10 부터 다시 돈다 — 아래 3절 참고.
#   (예전 메모: "ReflectionTest 스킵 1건" 은 파서를 Bin 에서만 찾아서였다 — 2026-09-19 에 닫혔다.)
ctest --test-dir build/Ninja-Debug -L nogpu
ctest --test-dir build/Ninja-Shipping -L nogpu

# 실기동 게이트 — **2026-09-19 부터 자동이다** (`AppSmokeTest`, 라벨 hostgpu)
#   네 백엔드 + 에디터(dx12·gl)를 띄워 종료 코드 0 · 로그 [Error] 0건을 본다. 6초.
#   GPU·창이 필요해 CI 는 못 돈다 — **GPU 있는 PC 에서 일을 끝내기 전에 이것을 돌린다.**
ctest --test-dir build/Ninja-Debug -L hostgpu --output-on-failure
ctest --test-dir build/Ninja-Shipping -L hostgpu --output-on-failure

# 손으로 볼 때(무엇이 깨졌는지 눈으로 봐야 할 때)는 그대로 쓴다
cd build/Ninja-Debug/Bin
./App.exe -gv_profileFrames=40 -dx12 -EnableEditor    # 종료 코드 0, 로그에 [Error] 0건
./App.exe -gv_profileFrames=40 -dx11 -EnableEditor
./App.exe -gv_profileFrames=40 -vk   -EnableEditor
./App.exe -gv_profileFrames=40 -gl   -EnableEditor    # 2026-09-09 부터 여기도 [Error] 0건이다
```

**뷰 모드(Lit/Unlit/Wireframe)는 픽셀로 잰다.** `-gv_viewMode=<0|1|2>` 가 에디터 없이도 모드를
고르므로 `-gv_screenshot` 과 같이 쓰면 헤드리스로 확인된다. 눈으로 보지 말고 **배경과 다른 픽셀
수**를 세라 — 와이어프레임은 같은 장면에서 약 1/7 로 준다(실측: 68,000 → 9,800~10,300, 네 백엔드).

```powershell
cd build/Ninja-Debug/Bin
./App.exe -dx12 -gv_benchMeshes=8 -gv_profileFrames=20 -gv_viewMode=2 "-gv_screenshot=wire.ppm"
```

**애니메이션이 도는 장면은 두 판을 그냥 빼면 안 된다.** GPU 인스턴스 회전이 벽시계로 돌아
같은 명령을 두 번 돌려도 픽셀이 조금씩 다르다(실측 잡음: 채널의 0.36%, 평균 |차| 0.054).
차이를 주장하려면 **같은 모드 두 판**을 먼저 재서 잡음 바닥을 정하고 그것과 비교하라
(Lit↔Unlit 은 4.33% · 0.224 로 바닥의 열 배가 넘는다).

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

### 0순위 — 지금 도는 두 패스

**(A) 정확성 훑기 (진행 중).** `Source/` 전체를 함수 하나하나 읽으며 고칠 수 있으면 고친다.
순서는 Core → Engine → ReflectionParser → Editor → GameFramework → Game. 2026-09-20 기준
Core · Engine · ReflectionParser 를 마쳤고 Editor 를 보는 중이다. 되풀이해 만난 모양은
"한 곳에 넣은 고침이 형제에게 안 갔다" 와 "정보를 싣고 와서 읽을 때 버렸다" 둘이다.

**(B) 성능·재사용성 개편 (A 가 끝난 뒤).** 같은 범위(`Source/` 전체 + `Tools/ReflectionParser`)를
이번에는 **성능과 재사용성**으로 다시 훑는다. **구조가 크게 바뀌어도 된다.** (A) 는 "틀린 답을
내는 곳" 만 보느라 성능·중복을 일부러 지나쳤다 — 지나친 것들의 예:

- `XmlSerializer::deserializeSoft` 가 같은 XML 을 **두 번 파싱**한다(버전 읽기용 한 번,
  백엔드용 한 번). 씬·프리팹 로드가 엔티티마다 이 길로 간다.
- `AnnotationApply` 의 토큰 분해가 토큰마다 임시 `string` 을 만든다
  (`trim( string( view ).c_str() )`) — `trim( string_view )` 오버로드가 이미 있다.
- `SpatialHashGrid2D::queryRay` 가 좁은 판정 없이 셀 안 전부를 돌려준다(형제 둘은 좁힌다).
- `ContainerTypeMap::match` 가 규칙 목록 선형 탐색 + 부분 문자열 검색이다.

성능 주장은 **Release 숫자로만** 한다(`measure-in-release-not-debug`), 숫자 없는 최적화는
하지 않는다(`measure-before-optimizing`).



### 1-0a. Engine 폴더 훑기 — 알파벳 순, 다음은 `Audio` (2026-09-18 시작)

`Source/Core` 를 폴더 단위로 훑은 것(2026-09-17, `Common` → `Uuid`, 17커밋)과 **같은 방식으로**
`Source/Engine` 을 훑는다. 폴더 하나 = 커밋 하나, 알파벳 순. 각 커밋은 (1) 찾은 동작 결함,
(2) 그 결함을 잡는 회귀 테스트 + 변이 테스트, (3) 한글 `@brief` 채우기, (4) 헤더 자립성,
(5) 이 문서의 3절 기록을 함께 담는다. 결함이 없으면 **훑은 것과 그 근거만 남긴다**(`Core/Uuid` 처럼).

| 폴더 | 줄 수 | 상태 |
|------|------:|------|
| `Animation` | 1,153 | ✅ 2026-09-18 (3절 참고) |
| `Audio` | 935 | ✅ 2026-09-18 (3절 참고) |
| `Common` | 274 | ✅ 2026-09-18 (동작 결함 없음. 3절 참고) |
| `Compression` | 370 | ✅ 2026-09-18 (3절 참고) |
| `Config` | 480 | ✅ 2026-09-18 (3절 참고) |
| `Dialogue` | 375 | ✅ 2026-09-18 (3절 참고) |
| `Graphics` | 42,770 | ✅ 2026-09-18 (3절 참고 — 훑은 깊이도 적어 두었다) |
| `Input` | 9,056 | ✅ 2026-09-18 (3절 참고) |
| `Localization` | 1,184 | ✅ 2026-09-18 (3절 참고) |
| `Module` | 318 | ✅ 2026-09-18 (동작 결함 없음. 3절 참고) |
| `Object` | 8,428 | ✅ 2026-09-18 (동작 결함 없음. 3절 참고) |
| `Physics` | 1,016 | ✅ 2026-09-18 (3절 참고) |
| `Reflection` | 3,081 | ✅ 2026-09-18 (3절 참고 — 타입 표가 밀집 배열이라는 함정도 적었다) |
| `Resource` | 3,604 | ✅ 2026-09-18 (3절 참고 — 파일에서 온 수를 믿던 자리 넷) |
| `Scene` | 1,438 | ✅ 2026-09-18 (3절 참고 — 대기열 요청의 future 가 거짓말을 했다) |
| `Sequencer` | 546 | ✅ 2026-09-18 (3절 참고 — 이벤트 트랙이 배포본에서 아무 일도 하지 않았다) |
| `Serialization` | 7,120 | ✅ 2026-09-18 (3절 참고 — 손상된 스트림 하나로 프로세스가 멈췄다) |
| `Spatial` | 1,541 | ✅ 2026-09-18 (3절 참고 — 실패한 update 가 원소를 삼켰다) |
| `Utility` | 3,077 | ✅ 2026-09-18 (3절 참고 — 넷 중 하나만 표 크기를 보지 않았다) |
| `Window` | 2,124 | ✅ 2026-09-18 (3절 참고 — **Engine 폴더 전체 완료**) |

**Core 와 다른 점 하나 — 이제는 해결됐다.** Engine 은 생성 헤더가 전 TU 에 `/FI` 로 들어가는데,
예전에는 그 우산이 Graphics 헤더 넷까지 끌고 들어와 **헤더 자립성 검사가 거짓 통과**를 냈다.
2026-09-18 에 우산이 전방 선언만 모으도록 바꿔 닫았다(3절 참고). 폴더를 끝낼 때마다
`py -3 Scripts/lint/report/RunHeaderSelfContained.py --filter Engine/<폴더>` 를 돌리면 된다.

### 1-0b. Editor 폴더 훑기 — ✅ **전부 끝났다** (2026-09-18, 11개 단위 · 11커밋)

`Source/Core`(2026-09-17) · `Source/Engine`(2026-09-18) 과 **같은 방식**. 폴더 하나 = 커밋 하나.
`Common` 은 16,868줄이라 통째로는 커밋 하나에 담기지 않으므로 **하위 폴더를 단위로 삼는다.**

| 폴더 | 줄 수 | 상태 |
|------|------:|------|
| `Common/Asset` | 819 | ✅ 2026-09-18 (3절 참고 — 같은 결정이 두 자리에 있었다) |
| `Common/Backend` | 2,236 | ✅ 2026-09-18 (3절 참고 — 실패를 수습하는 경로가 실패했다. **테스트 없음**) |
| `Common/Commands` | 4,478 | ✅ 2026-09-18 (3절 참고 — 저장이 실패해도 "저장됨" 이 됐다) |
| `Common/Config` | 299 | ✅ 2026-09-18 (3절 참고 — 같은 판정을 네 곳이 손으로 적고 있었다) |
| `Common/Gui` | 3,740 | ✅ 2026-09-18 (3절 참고 — 조용히 어긋날 수 있던 자리 둘을 소리 나게) |
| `Common/Widgets` | 1,277 | ✅ 2026-09-18 (3절 참고 — 죽어 있으면서 함정인 API 하나) |
| `Common/Workspace` | 3,764 | ✅ 2026-09-18 (3절 참고 — nullptr 을 준다고 적어 둔 값을 15곳이 그냥 따라갔다) |
| `Panels` | 11,011 | ✅ 2026-09-18 (3절 참고 — 설정할 수 있는데 아무 일도 안 하는 손잡이 하나) |
| `Popups` | 1,008 | ✅ 2026-09-18 (3절 참고) |
| `Viewport` | 1,945 | ✅ 2026-09-18 (동작 결함 없음. 3절 참고) |
| 루트(`ImGuiEditor` · `IEditor`) | 780 | ✅ 2026-09-18 (3절 참고 — **Editor 폴더 전체 완료**) |

**엔진과 다른 점 — 여기는 이미 한 번 훑었다(2026-09-10, 커밋 7개).** 그때 훑고 **깨끗하다고
확인한 것은 다시 파지 않는다**: `formatstring` 의 `%s`/`%d` 혼용(정상) · 멤버 인덱스 경계 7곳
(전부 검사함) · `EditorBackgroundJob` 수명(shared_ptr+세대로 안전) · 엔진에 남는 콜백
(Logger 구독 · 창 닫기 핸들러 해제됨, 파일 감시자 등록 없음) · `EditorConfig` 16필드.
그 라운드의 방향도 그대로다 — **"쪼개는 것보다 공통된 부분을 빼는 게 낫다"**, 긴 함수를 기계적으로
분해하는 것은 우선순위가 낮다. "하나 더하려면 N 곳을 고쳐야 하는" 구조를 찾는다.

**검증의 사각지대를 먼저 알 것.** 기본 실기동(`SW_ACTIVE_GAME=Empty`)은 **빈 씬**을 본다 — 오브젝트를
도는 코드(뷰포트 피킹 · 컴포넌트 시각화 · Hierarchy 트리 · 씬 세대 훅)는 하나도 태우지 않는다.
오브젝트 경로는 `EditorTest` 단위 테스트로만 덮인다. 실기동으로 보려면 테스트 씬을 지정한다
(PowerShell 은 점이 든 인자를 쪼개므로 **따옴표 필수**):

```powershell
./App.exe -gv_profileFrames=60 -dx12 -EnableEditor `
  "-gv_editorStartupScene=game/empty/maps/editortest.scene.xml" -gv_editorPanelDump=40
```

### 1-0c. GameFramework · Games · RuntimeAPI 훑기 — ✅ **전부 끝났다** (2026-09-18, 2커밋)

`Core` · `Engine` · `Editor` 와 같은 방식. 규모가 작아 폴더 하나 = 커밋 하나로 충분하다.

| 폴더 | 줄 수 | 상태 |
|------|------:|------|
| `RuntimeAPI` | 465 | ✅ 2026-09-18 (3절 참고 — 모듈 경계에 ABI 스탬프가 없었다) |
| `GameFramework` (전체) | 6,777 | ✅ 2026-09-18 (3절 참고 — 널 가능 서비스 사용 린트 확대 · 죽은 등록 경로 제거) |
| `Games/Empty` | 1,054 | ✅ 2026-09-18 (같은 훑기에 포함. 결함 없음) |

### 1-0d. `Source/App` · `Tools` 훑기 — ✅ **끝났다** (2026-09-19, 2커밋)

훑기 표 셋(`Core` · `Engine` · `Editor` · `GameFramework`/`Games`/`RuntimeAPI`)에 `App` 과 `Tools` 가
없어 빠뜨리고 있던 것을 2026-09-19 에 같은 방식으로 훑었다. 둘 다 결함이 나왔다 — 3절 참고.

| 폴더 | 줄 수 | 상태 |
|------|------:|------|
| `App` | 2,512 | ✅ 2026-09-19 (3절 참고 — 디바이스 없는 RHI 에 `getDevice()` 를 묻던 세 자리) |
| `Tools/ReflectionParser` | 6,064 | ✅ 2026-09-19 (3절 참고 — 도구가 자기 자신을 입력으로 세지 않았다) |

**손대기 전에 알 것.** `App` 은 실행 파일이라 **링크할 라이브러리가 없다** — 테스트는 소스를 파일
단위로 가져와야 한다(`SmokeTest` 가 `LiveReloadManager`·`ModuleCompiler`·`ModuleHost` 를, `AppTest` 가
`FrameTimeline` 을 그렇게 쓴다). 창·RHI·모듈이 필요한 것은 단위 테스트로 끌고 오지 말고 실기동으로
본다 — 그 실기동은 이제 `AppSmokeTest`(라벨 `hostgpu`)가 자동으로 돈다.

### 1-0e. 확장점 점검 — 상용 엔진과 견줘 남은 것 (2026-09-19)

판단 기준은 하나다: **"하나 더하려면 몇 곳을 고쳐야 하는가."** 이번에 둘을 닫았고(3절 참고),
나머지를 순서대로 적어 둔다. 이 저장소는 린트·픽서·백엔드·에디터 패널에서 이미 같은 결론에
도달해 있다 — 목록이 여럿이면 한쪽만 늘어난다.

| 확장점 | 지금 | 상용 엔진의 자리 | 남은 일 |
|--------|------|------------------|---------|
| 에셋 종류 | ✅ `IAssetCache` + `ResourceManager` 등록부 (2026-09-19) | Godot `ResourceFormatLoader` · UE `FStreamableManager` | **읽는 쪽**(무엇을 어떻게 로드하는가)은 아직 구체 캐시의 몫이다. 종류가 대여섯이 되면 로더도 등록제로 |
| 엔진 서비스 | X-macro 목록 하나 + 호스트 대조 게이트 (2026-09-19) | — | 채우는 코드 자체의 생성은 하지 않았다 — 호스트마다 소유 멤버 이름이 다르다. 게이트가 그 값을 대신한다 |
| 서브시스템 수명 | ✅ 부분 완료 (2026-09-19) — 생성·소유·바인딩은 목록의 `owned` 열에서 생성된다(`EngineOwnedServices`). **초기화 순서와 종료 순서는 일부러 손으로 남겼다** | UE `USubsystem` 컬렉션(자동 수집 · 의존 순서) | 순서를 자동화하려면 의존 관계를 선언으로 다시 적어야 하는데, 그 지식은 지금 종료 절차의 주석에 있다(디바이스보다 먼저 놓아야 하는 것 등). 옮길 값이 있는지는 다음 사람이 판단 |
| 렌더 패스 | 풀스크린 포스트는 **선언이 곧 바인딩**(파이프라인 XML + `RenderPassInputContract`)이라 이미 표 기반이다. 지오메트리 7종만 `executePass` 분기 | UE RDG · Unity SRP `ScriptableRenderPass` | 새 포스트 패스 = enum 한 줄 + contract 한 줄 + PSO 한 줄. **분기를 표로 바꾸는 이득은 작다** — 지오메트리 패스는 상용 엔진도 특수 취급한다 |
| 모듈이 확장할 수 있는 것 | ✅ 에셋 종류까지 (2026-09-19) — `ResourceManager` 는 게임 모듈에도 노출되고 `IAssetCache` 는 그냥 include 하면 된다. 등록/해제가 짝이고, 두고 가면 종료가 이름으로 경고한다 | UE 모듈이 렌더 패스·에셋 타입·서브시스템을 등록 | **렌더 패스**는 아직이다 — 그러려면 패스 실행 컨텍스트(`FramePassContext`)를 모듈 경계 밖으로 내야 하는데, 그것은 RT 안전성까지 같이 내보내는 일이다. 쓰는 모듈이 생기면 그때 |
| 에디터 패널·커맨드·인스펙터 | 이미 등록표 하나씩(`registerDefault*`) | — | 없음 |
| 입력 바인딩 종류 | ✅ 표 하나 + `static_assert` (2026-09-19) | Unity Input System 의 `InputBindingComposite` 등록 | 없음 — 종류를 더하면 표 한 줄이고, 빠뜨리면 컴파일이 선다. 3절 참고 |

**같은 기준으로 `Source` 전체를 다시 세어 봤다 (2026-09-19).** 여러 파일이 같은 enum 을 `switch`
하는 자리는 다섯 종류였다: `ShaderStage`(3파일) · `RHIBufferState`(3) · `RHIBackend`(3) ·
`GlobalVariableType`(3) · `BindingKind`(3). **앞의 넷은 정당하다** — 백엔드/스테이지마다 정말로
다른 일을 하거나(같은 표로 묶으면 백엔드별 API 를 표에 밀어 넣게 된다), 엔진과 에디터가 서로 다른
질문을 한다(값을 저장하는 쪽 vs 위젯을 그리는 쪽). `BindingKind` 만 **같은 질문을 세 번** 하고
있었고, 그것을 닫았다(3절). 다시 세어 볼 때는 "백엔드마다 다른가" 를 먼저 묻고 시작할 것.

### 1-0. 검토는 했고 결정이 남은 것 (2026-09-12, 백엔드 교체 작업 중 나온 질문)

- ~~GPU 상주를 CPU 에셋에서 떼어낸다~~ → **다르게 풀었다.** 소유를 옮기는 대신 언리얼의 `FRenderResource` 처럼
  **디바이스가 죽기 전에 통보**하게 했다(위 "언리얼의 FRenderResource 를 들여온다"). 통보가 오므로 CPU 쪽이 핸들을
  들고 있어도 되고, 세대 번호는 사라졌다. 남은 축(참조 카운트 RHI 핸들)은 리소스를 여럿이 나눠 들기 시작할 때.
- **Mesh · Material 을 `ObjectHandle`(index|generation) 로 들 수 있나.** 가능하지만 지금은 권하지 않는다. 핸들은 **해석해 줄 표**가
  필요하고(MaterialCache 는 있지만 Mesh 는 없다), 렌더 스레드가 그 표를 프레임이 도는 동안 읽어야 하므로 표가 RT 안전해야 하며,
  "핸들이 죽었다" 는 것을 아는 것과 "이 프레임이 끝날 때까지 살아 있어야 한다" 는 것은 다른 문제다 — 후자를 핸들로 풀면
  방금 지운 retire 큐가 다시 생긴다. `shared_ptr` 은 그 둘을 한 번에 준다. 핸들이 맞는 자리는 해석기가 이미 있고 nullptr 로
  끝나도 되는 씬 오브젝트(`ComponentHandle` · `GameObjectPtr`)다. Mesh/Material 레지스트리를 따로 세우는 날 다시 본다.
- ~~`MaterialInstance::applyToGpu` 의 세대 검사가 Engine 에 있어도 되나~~ → **닫았다.** 결론은 "있어야 한다" 였다. 그 판단은
  교체만의 것이 아니라 종료 순서와 디바이스 유실 복구도 쓰고, 소유자를 모르는 객체(캐시 밖 머티리얼)가 있는 한 열거 방식보다
  지연 검사가 튼튼하다. 다만 두 클래스가 같은 검사를 각자 적던 것은 `RHIResidentBuffer` 하나로 모았다(아래 "소유 정리 셋").
- ~~GPU 리소스 생성·삭제를 워커로~~ → **전부 닫았다.** 메시는 옮겼고(큰 이득), 머티리얼 상수버퍼와 PSO 는 **재 보고
  기각했다** — 아래 "재 보고 둘은 기각, 하나는 고쳤다" 참고. 다시 제안하기 전에 그 숫자를 먼저 볼 것.

### 1-1. clang-tidy 지적 — **버전마다 다른 숫자가 나온다**

`py -3 Scripts/lint/report/RunClangTidy.py` 를 쓴다. 두 PC 가 같은 날 같은 코드를 훑고 **"0건" 과 "72건"**
이라는 다른 답을 받았다. 둘 다 맞다 — clang-tidy 버전이 다르면 검사 목록이 다르다.

**2026-09-10: 그 혼란을 도구가 스스로 막게 했다.** 이제 스크립트가 실행 파일 경로와 버전을 보고
머리에 찍고, `--clang-tidy <경로>` 로 버전을 고정할 수 있다. 숫자를 적을 때 버전을 손으로
덧붙이는 규율에 의존하지 않는다. 한 PC 에 여러 버전이 깔려 있기도 하다 — 이 PC 는
`C:\Utility\LLVM`(21.1.1)과 VS BuildTools(22.1.3)를 함께 갖고 있고, 탐색 순서상
**기본은 21** 이 잡힌다. 22 로 재려면 경로를 명시해야 한다(경로는 `--clang-tidy` 로 넘긴다).

- **clang-tidy 20 기준(한쪽 PC)**: 110 → 65 → 20 → **0건**.
- **clang-tidy 22 기준**: 예전엔 308 → 72건이었다. **2026-09-10 재측정(22.1.3, 전체 트리):
  30건이고 종류는 하나뿐이다** — `bugprone-throwing-static-initialization`.
  예전 72건에 있던 나머지 다섯 종류(`VirtualCall` 13 · `macro-parentheses` 12 · `branch-clone` 8 ·
  `suspicious-stringview-data-usage` 5 · `exception-escape` 2)와 `Padding` 3 은 **이제 뜨지 않는다**
  (`Padding` 은 `.clang-tidy` 에서 껐고, 나머지는 22.1.3 에서 안 짚는다 — 또 버전 차이다).

**남은 30건은 고치지 않는다 — 판단이다.** 전부 정적 저장 기간 객체의 초기화이고, 두 부류다:
전역 변수 등록자(`sw_reg_gv_*`, 약 18건)와 설정 싱글턴·컨테이너 정적(`s_map*`, `s_list*`, 약 12건).
둘 다 **시작 시 실패가 곧 종료**인 자리다.

> `Logger::registerCaller` 를 `noexcept` 로 만들어 150건을 없앤 전례가 있어 같은 수를 쓰고 싶어지지만,
> 여기는 다르다. `GlobalVariableRegistrar` 는 `string` 멤버 넷과 `variant<…, string>` 을 들고 있어
> **실제로 던질 수 있다**(할당). Logger 는 "고정 배열과 뮤텍스뿐" 이라 못 던지는 것이 근거였다.
> 던질 수 있는 생성자에 `noexcept` 를 붙이는 것은 분석기를 침묵시키는 대신 정보를 지우는 거래다 —
> 하지 않는다.

**숫자가 늘어도 회귀가 아닐 수 있다.** 이 건수는 전역 변수 개수를 따라간다 — 29 → 30 이 된 것은
`gv_editorStartupScene` 을 추가했기 때문이다. 새 지적이 뜨면 **종류**를 먼저 보라.

**이 PC 에 clang-tidy 가 아예 없기도 했다** — 저장소가 받아 두는 `Tools/LLVM` 은 clang-tidy 를 뺀
축소판이라 스크립트가 "찾지 못했습니다" 로 끝났다. 이제 Visual Studio 가 같이 설치하는
LLVM(`VC/Tools/Llvm/x64/bin`)까지 찾는다.

**새로 뜨는 지적은 분류하지 않은 새 코드다.** 판단해서 고치거나, 의도한 것이면 그 자리에
`NOLINTNEXTLINE` 과 **이유**를 함께 남긴다. 이유 없는 NOLINT 는 다음 사람이 되살리고 같은 분류를
다시 하게 만든다.

검사 목록은 `.clang-tidy` 가 정한다. 끈 검사는 다섯이고 각각 왜 이 코드베이스에서 쓸 수 없는지
실측과 함께 적혀 있다: `easily-swappable-parameters` · `EnumCastOutOfRange` · `Padding` ·
`invalid-enum-default-initialization` · `derived-method-shadowing-base-method` ·
`std-namespace-modification`.

**clang-tidy 22 의 새 검사 넷은 이렇게 처리했다:**

| 종류 | 처리 |
|---|---|
| `bugprone-throwing-static-initialization` 179 | **150건은 `SW_LOG_CALLER` 하나가 냈다.** `Logger::registerCaller` 를 `noexcept` 로 만들어 없앴다 — 고정 배열과 뮤텍스뿐이라 실제로 던질 것이 없고, 정적 초기화에서 부르는 함수라는 계약을 타입에 못박는 편이 맞다. 남은 29건은 전역 변수 등록자·설정 싱글턴처럼 **시작 시 실패가 곧 종료**인 자리다 |
| `bugprone-invalid-enum-default-initialization` 48 | **껐다.** `D3D11_RASTERIZER_DESC desc{}` 처럼 SDK 구조체를 0 으로 비우면 그 안의 열거형이 짚힌다 — 열거형 19종이 전부 D3D11/D3D12/Vulkan 타입이고 우리 열거형은 0건이다(전수 확인) |
| `bugprone-derived-method-shadowing-base-method` 11 | **껐다.** 11건 전부 `REFLECT_BODY()` 가 만드는 `swReflectSelf` 다(전수 확인). NOLINT 는 매크로 안에 넣을 수 없어 REFLECT 를 쓰는 모든 자리에 붙여야 한다 |
| `bugprone-std-namespace-modification` 10 | **껐다.** 10건 전부 `tuple_size`/`tuple_element`/`hash`/`equal_to` 를 프로그램 정의 타입에 특수화한 것으로 `[namespace.std]` 가 허용한다(전수 확인). 그중 `tuple_size<FormattedValue<T>>` 만 `integral_constant<uint32,2>` 였어서 표준대로 `size_t` 로 맞췄다 |
| `bugprone-command-processor` 1 | **고쳤다.** ContentBrowser 의 "Show in Explorer" 가 `system()` 이었다 — 셸을 거쳐 경로의 `&`·`"` 가 명령으로 해석되고 콘솔 창이 깜빡였다. `EditorAssetCommands::showInFileExplorer` 로 옮겨 셸 없이 프로세스를 띄운다(macOS `open -R`, 리눅스 `xdg-open` 도 같이) |

**분류만 해 두었던 것도 다시 봤다. 절반은 오탐이 아니라 "확신할 수 없게 쓰인 코드" 였다.**

- `bugprone-use-after-move` — `StringBuilder::appendFormat` 은 **재시도 루프 안에서 같은 인자 팩을
  다시 forward** 했고, `TaskFuture::setContinuation` 은 옮긴 델리게이트를 bool 플래그에 기대어 다시
  읽었다. 둘 다 억제가 아니라 코드로 풀었다.
- `clang-analyzer-security.ArrayBound` — `&vec._x` 를 넘겨 인덱스로 읽던 **세 자리는 시그니처를
  `const float3&`/`const float4&` 로 바꿔** UB 자체를 없앴다(내부 호출부뿐이라 C-ABI 제약이 없었다).
  남은 하나는 `float4x4::data()` 를 쓰는 자리라 NOLINT + 이유로 닫았다 — 레이아웃은
  `MatrixMath.h` 의 `static_assert( sizeof(float4x4) == 16 * sizeof(float32) )` 가 지킨다.
- `bugprone-implicit-widening-of-multiplication-result` — 명시적 캐스트.
- `clang-analyzer-deadcode.DeadStores` — Material 의 죽은 계산은 **미완성 코드의 흔적**이었고(빈
  `if` 와 짝), 에디터의 붙여넣기는 **실패를 아무도 읽지 않고 있었다**. 열거형→이름 두 곳은 값을
  먼저 넣고 switch 로 덮어쓰는 대신 돌려주는 함수로 바꿔 억제 없이 없앴다.
- `bugprone-unhandled-self-assignment` — `fs = fs.c_str()` 가 자기 버퍼를 자기에게 memcpy 하고
  있었다(가드 + 테스트 추가). 같은 파일의 다른 자리는 NOLINT 가 `template` 줄에 가려 적용되지 않고
  있었다 — **NOLINTNEXTLINE 은 진단이 붙는 줄 바로 위여야 한다.**

### 1-2. 100줄 넘는 함수 20개 — 우선순위 낮음

분해 자체는 코드 총량을 줄이지 않는다(2절 "쪼개기보다 공통부 추출"). 중복이 남아 있는 자리를
먼저 없애고, 그러고도 긴 함수가 문제로 남으면 그때 본다. 목록이 필요하면 다중 행 시그니처를
중괄호 깊이로 정확히 재는 스크립트를 만들어 뽑는다(단순 정규식은 여러 줄 시그니처를 잘못 잰다).

### 1-2b. WSL(리눅스) 빌드 — **선다**

2026-09-11 에 `WSL-Debug` 를 끝까지 세웠다. 빌드는 **오류·경고 0**, `ctest` 는 **12/12 통과**
(린트 6 + GPU 포함 전체). 그 과정에서 드러난 결함 다섯은 3절 항목으로 고쳤다 — 대부분
"윈도우 밖에서 한 번도 안 돌려본 코드"가 아니라 **플랫폼과 무관한 잠복 버그**였다(Vulkan
스왑체인 교착이 대표적이다). clang-format 조달과 LLVM 경로 손목록도 같은 날 닫았다(3절).

**포맷 관련은 없다 (2026-09-12 확인).** 예전에 "포맷된 적 없는 파일 11 개" 로 적혀 있었고, 같은 날
내가 그것을 "실측 22 개" 로 고쳤는데 **둘 다 틀렸다.** 저장소가 고정한
`Tools/LLVM/bin/clang-format.exe`(20.1.8) 로 전수 조사하면 **0 개**다.

**PATH 의 clang-format 으로 재면 안 된다.** 내 PC 의 PATH 에는 22.1.8 이 있어서 22 개가 나왔다 —
버전이 다르면 같은 파일을 다르게 포맷하므로 그 수는 아무 의미가 없다. `Scripts/common/Host.py` 의
`resolveClangFormat` 이 toolchain_config → `Tools/LLVM/bin` 을 먼저 보고 PATH 는 마지막에 보는 것도
같은 이유이고, 그 함수의 주석이 "한쪽에서 커밋한 줄을 다른 쪽 훅이 거부하는 왕복" 을 이미 적어 두었다.
pre-commit 게이트(`runClangFormatBatch`)도 같은 고정 바이너리를 쓴다.

세려면 저장소 것을 명시한다:

```bash
CF=Tools/LLVM/bin/clang-format.exe
find Source Test Tools/ReflectionParser \( -name '*.cpp' -o -name '*.h' -o -name '*.inl' \) -print0 \
  | xargs -0 -P 8 -n 1 -I{} sh -c "\"$CF\" --dry-run --ferror-limit=0 \"{}\" 2>&1 | grep -q warning: && echo {}"
```

(파일당 한 번씩 돌리는 이유: `--dry-run` 은 여러 파일을 한 번에 주면 결과가 조용히 잘린다.)

> **함정 — `WSL-*` 프리셋을 Windows 체크아웃(`/mnt/d/...`)에서 돌리지 말 것.** 그렇게 하면
> 리눅스 트리플릿이 **같은 `build/vcpkg_installed`** 에 설치되면서 Windows 쪽 설치와
> `vcpkg/compiler-file-hash-cache.json` 을 지운다 — 실제로 그렇게 Windows 트리가 통째로 서지
> 않게 됐고 복구에 27분이 들었다(게다가 DrvFs 라 configure 자체가 끝까지 가지도 못한다, 아래 참고).
> 리눅스 빌드는 **WSL 안의 클론**에서 한다 — 이 PC 에는 `~/LearningTemplate` 이 이미 있고
> 자기 `build/vcpkg_installed` 를 따로 들고 있다. Windows 트리에서 가져올 때는 그 클론에서
> `git fetch /mnt/d/Projects/Personal/LearningTemplate main` 한다.

**그대로 유효한 함정 (환경)**

- **`/mnt/d` (DrvFs) 에서는 configure 가 안 된다.** `configure_file` 이 `Operation not permitted`
  로 죽는다. 리눅스 파일시스템(`~`, ext4)으로 옮겨 빌드해야 한다.
- **`libwayland-dev` 가 필요하다.** vcpkg `vulkan-validationlayers` 는 WSI 백엔드를 끌 수 없어
  `wayland-client.pc` 를 무조건 요구한다. 이제 `SetupLinuxDevEnvironment` 가 미리 알려 준다.
- **GPU 는 없다.** `/dev/dxg` 와 WSLg 가 있어도 Vulkan 은 `llvmpipe`(type=CPU) 하나만 잡히고,
  GL 은 `ARB_gl_spirv` 가 없어 백엔드가 스스로 빠진다. 즉 WSL 에서 도는 렌더링 테스트는
  소프트웨어 래스터라이저 위의 것이다 — API 오용은 잡지만 드라이버 거동은 검증하지 못한다.
- 곁가지: `SetupVcpkg.py --install` 은 `scripts/buildsystems/vcpkg.cmake` 만 보고 "찾았다" 고
  끝낸다. 윈도우에서 클론한 트리를 리눅스에서 쓰면 `vcpkg.exe` 만 있고 `vcpkg` 바이너리가
  없는데도 성공을 보고한다(툴체인이 알아서 부트스트랩하므로 치명적이진 않다).

---

## 2. 작업 방식 — 정해진 방향

- **쪼개기보다 공통부 추출.** 긴 함수를 나누면 코드가 이동할 뿐 총량은 그대로다.
  중복은 증상이고 원인은 "매번 다시 만들어야 하는 구조"다. 원인을 없앤다.
- **추가·변경에 용이한 구조를 먼저 만든다.** 새 타입·새 패널·새 백엔드를 하나 더 넣을 때
  복사해야 할 것이 남아 있으면 그 자리가 다음 리팩터 대상이다.
- 주석과 커밋 메시지는 한국어. 규칙은 [AGENTS.md](../AGENTS.md) 와
  [04_CodingGuidelines.md](04_CodingGuidelines.md).

### 안 하기로 한 것 (다시 제안하지 말 것)

- **도구 버전을 "최신 자동" 으로 두는 것 — 보류(2026-09-11).** ninja·sccache 처럼 출력에 영향이
  없는 도구는 GitHub API 로 최신을 조회하게 할 수 있지만, 네트워크 의존이 생기고 빌드 재현성이
  떨어진다. 지금처럼 버전 키 하나로 고정해 두고 올릴 때만 의도적으로 올린다. clang-format 은
  애초에 고정이 아니면 안 된다(버전이 곧 출력이라, 먼저 올린 PC 가 코드베이스를 재포맷한다).

- **`EditorContext` 의 소유 구조를 더 쪼개는 것.** 컨텍스트가 UI 매니저 전부를 `unique_ptr` 로
  들고 있다. 조회/생명주기를 두 TU 로 갈라 `EditorContext` 를 **조회만** 하는 코드가 ImGui 없이
  링크되게 해 둔 것으로 충분하다(그래서 `Test/EditorTest` 가 성립한다). 패널·팝업 매니저 소유를
  컨텍스트 밖으로 빼는 것은 영향 범위가 크고 얻는 것이 없다 — 필요해지면 그때 소유 구조부터 정한다.

### 편집 함정

- 한 함수에서 **여러 구간을 빼낼 때는 뒤쪽 구간부터** 한다. 앞쪽을 먼저 빼면 뒤쪽 줄 번호가
  밀려 `switch` 중간을 자르는 식으로 깨진다.
- 파일을 스크립트로 고칠 때 CRLF 를 보존한다. 이 저장소는 CRLF 다.
- **`Tools/ReflectionParser` 만 고치면 `.gen.cpp` 가 다시 만들어지지 않는다.** 의존은 걸려 있는데
  (`ninja -t query` 로 `BuildTools/ReflectionParser.exe` 가 입력에 보인다) 실제로는 파서만 다시 빌드되고
  코드젠은 건너뛰는 것을 봤다. 즉 **파서를 고치고 리플렉션 테스트를 돌리면 옛 `.gen.cpp` 를 보고 있을 수
  있다** — 변이 테스트가 "고치기 전에도 통과" 로 보이는 원인이다. 입력 헤더를 `touch` 해서 강제한다.

---

## 3. 최근에 끝낸 일 (2026-09-08 ~ 12)

무엇을 이미 해결했는지 알아야 같은 것을 다시 파지 않는다.

### 2026-09-20 (시퀀스 플레이어가 자산을 바꾸기 전에 멈췄다)

`SequencePlayer::loadFromFile` 과 `setAsset` 이 **자산을 바꾸기 전에** `stop()` 을 불렀다.
`stop()` 안의 `_previousFrame = _asset._frameMin` 이 아직 **옛 자산**의 시작 프레임을 집는다.

100 프레임에서 시작하는 시퀀스를 읽으면 이전 프레임만 0 에 남는다. 이벤트 판정은
`previousFrame < start <= frame`(지나갔는가) 이므로 첫 적용이 `applyFrame(100, 0)` 이 되어
**100 이하의 이벤트가 전부 한꺼번에 발화**한다. `getCurrentFrame()` 은 재생 시각에서 그때그때
구하므로 새 자산을 따르는데 `getPreviousFrame()` 만 옛 자산을 따르는, 둘이 어긋난 상태이기도 했다.
`SequencePlayerComponent` 가 그 둘을 짝으로 `applyFrame` 에 넘긴다.

같은 파일에서 하나 더: 프레임 번호는 JSON 에서 오는데 그대로 믿었다. `frameMin` 이 int32
최대값이면 바로 아래의 `_frameMax = _frameMin + 1` 이 **부호 있는 넘침**(UB)이라 끝이 시작보다
앞이 되고, 트랙의 `_end - _start` 도 같은 식으로 접혔다. 파싱하는 자리에서 `kSequenceFrameLimit`
(= int32 최대의 절반)로 자른다 — 그러면 **어떤 두 값의 차도** int32 안에 들어와서, 빼는 자리마다
넓은 타입으로 올릴 필요가 없다. 30fps 기준 1,000만 일이 넘는 길이라 실사용을 자르지 않는다.

**무는지 확인했다.** `stop()` 순서를 되돌리면 `LoadedAssetResetsPlaybackToItsOwnStart` 가 세 단언
모두(현재/이전 프레임 일치, 값 100, 그리고 "이벤트가 발화하지 않는다")에서 지고, 자르기를 빼면
`OutOfRangeFrameNumbersCannotOverflowSpans` 가 진다.

### 2026-09-20 (공간 색인 넷이 같은 질문에 서로 다르게 답하고 있었다)

`Engine/Spatial` 에는 색인이 넷 있고(`SpatialHashGrid2D` · `SpatialQuadTree` · `SpatialOctree` ·
`BVHTree3D`) `PhysicsWorld` 가 자기 것을 하나 더 든다. **같은 질문 다섯 개에 다섯이 제각각
답하고 있었다.** 다섯 다 실제로 틀린 답을 내거나 돌아오지 않는 경로였다.

| 질문 | 어긋나 있던 모습 | 증상 |
|------|-----------------|------|
| 결과 벡터를 비우는가 | 그리드·물리는 비우고, BVH·트리는 **덧붙이기만** | 벡터를 돌려 쓰면 지난 답이 이번 답인 척 |
| 셀 범위에 상한이 있는가 | 광선 질의만 2048 걸음, 삽입·AABB·원은 무제한 | 큰 상자 하나로 **삽입이 돌아오지 않음** |
| 셀 수를 어떻게 세는가 | `spanX * spanY`(`* spanZ`) 를 그냥 곱함 | 2^64 로 **넘쳐서 0**, 상한 검사를 그대로 통과 |
| 좌표를 셀 번호로 어떻게 바꾸는가 | `static_cast<int32>(floor(x/cell))` | 범위 밖은 UB, x86 에서 최소·최대가 **같은 셀**로 접힘 |
| 구체 판정을 어떻게 하는가 | 옥트리만 "중심 거리 vs 반지름+최장변" | 상자 안에 든 구체조차 **놓침**(거짓 음성) |
| 광선 방향을 정규화하는가 | 그리드는 하고 BVH 는 안 함 | 같은 `maxDist` 가 방향 길이만큼 늘어난 사거리를 뜻함 |

**돌아오지 않는 경로가 실재했다.** `AABB2D::infinite()` 는 이 모듈이 **스스로 제공하는** 값인데,
그것으로 `insert` 하면 `floor(FLT_MAX / cellSize)` 만큼의 셀을 돌려고 한다. 새로 쓴
`SpatialHashGrid2DInfiniteBoundsTerminate` 는 상한을 넣기 전에 실제로 **테스트를 멈춰 세웠고**,
그때서야 상한 자체가 곱셈 넘침으로 무력화돼 있다는 것이 드러났다 — 한 겹 더 아래였다.

**물리 쪽 증상은 조용했다.** x86 의 float→int 변환은 넘치든 모자라든 똑같이 int32 최솟값으로
붙는다. 그래서 `±1e12` 짜리 바디의 최소·최대가 같은 셀 번호가 되어 폭이 1 로 읽히고,
"너무 크다" 판정을 통과한 뒤 **원점과 아무 상관 없는 셀 하나**에만 등록됐다. 겹치는 자리를 보는
질의가 그 바디를 못 찾는다 — 터지지 않고 답만 틀리는 종류라 로그에도 안 남는다.

고친 모양:

- 셀 범위 계산을 `CellRange` **하나**로 모았다(삽입·제거·AABB·원 넷에 복사돼 있었다).
  `PhysicsWorld::CellRange` 와 같은 모양이다 — 넣을 때와 뺄 때가 같은 셀을 보게 하는 자리.
- 상한을 넘는 핸들은 흩뿌리지 않고 `_listOversizedHandle` 에 모으고, **모든 질의가 그것을 함께**
  본다(물리의 `_listOversizedBody` 와 같은 규약).
- `getCellCount()` 는 곱하기 **전에** 넘칠지 보고, 넘치면 `MathUtil::MaxInt64` 로 붙인다.
  호출부는 상한과 견주기만 하므로 그것으로 답이 맞는다. 물리 쪽(세 축)도 같이 고쳤다.
- `toCellCoord` 는 float64 로 나눈 뒤 int32 범위로 접는다(NaN 도 함께). float64 가 모든 int32 를
  정확히 담아서 경계 비교가 어긋나지 않는다. 두 곳 다.
- 질의 결과 벡터는 **언제나 먼저 비운다** — 색인이 비어 할 일이 없을 때도. 규약은
  `Spatial/README.md` 에 적었다.
- `SpatialOctree::querySphere` 는 형제 둘과 같은 **상자 위 최근접점** 판정으로 바꿨다.
- `BVHTree3D::queryRay` 는 방향을 단위로 맞춘다. `maxDist` 는 이제 두 색인에서 같은 뜻이다.
- `SpatialTree::_mapElementLocation` 을 지웠다 — 세 곳에서 쓰기만 하고 **읽는 곳이 없는**,
  `_mapElement[id]._bounds` 의 두 번째 사본이었다.

**무는지 확인했다.** 상한을 10만으로 풀면 `SpatialHashGrid2DOversizedBoundsStayQueryable` 이 지고,
물리의 `toCellCoord` 를 되돌리면 `BodyBeyondCellCoordinateRangeIsStillFound` 가 지며,
`clear()` 를 빼면 `QueriesOverwriteTheOutListInsteadOfAppending` 이, 정규화를 빼면
`BVHTree3DAABBRaySphereQueries` 가 진다.

### 2026-09-20 (액션 메뉴도 순회 중에 콜백을 불렀다 · 서비스 조회의 세 번째 문)

**`EditorActionMenuManager::drawActionMenu` 가 범위 for 로 돌면서 액션과 술어를 불렀다.**
확장 메뉴는 원래 항목을 더하라고 있는 자리이므로, 액션이 같은 위치에 항목을 더하면 벡터가
재할당돼 반복자와 참조가 뜬다. `ReloadFileManager::dispatchEvents` 와 **같은 모양**이고 그쪽은
ASAN 이 `heap-use-after-free` 로 잡았다. 인덱스로 돌고, 목록을 바꿀 수 있는 두 호출(술어·액션)의
델리게이트를 부르기 전에 복사한다. 문자열은 복사하지 않는다 — ImGui 호출은 목록을 건드리지
않으므로 참조로 충분하고, 그 두 호출 뒤에는 참조를 더 쓰지 않는다.

같은 파일에서 하나 더: 위치 수 `4` 가 배열 크기와 경계 검사에 **리터럴로 두 번** 적혀 있었다.
`ActionMenuLocation::Count` 가 정본이 되게 했다.

**서비스 조회에 세 번째 문이 있었다.** 게이트는 `getService<T>()->` 와
`EditorContext::get()->` 를 잡는데, `T* p = getService<T>();` 로 **받아 두고 확인은 안 하는**
것은 한 줄짜리 정규식을 통과한다. 결과는 같다. 백세 자리 중 여섯이 그랬다(ImGuiEditor 1 ·
InputMapEditorPanel 3 · InspectorPanel 2). 여섯을 고치고, 게이트가 선언 뒤를 함수 끝까지 훑어
"확인이 먼저인가 역참조가 먼저인가" 를 보게 했다. `CheckLintsAreAlive` 28 케이스 / 17 게이트.

### 2026-09-20 (로그에 뷰를 `.data()` 로 풀어 넘겨 끝을 넘어 읽고 있었다)

`formatstring` 은 인자가 `string_view` 면 **길이로** 쓰고(`write()` 가 `str.length()` 를 본다),
`const utf8*` 면 `strlen` 으로 읽는다. 그래서 뷰를 `.data()` 로 풀어서 넘기면 **뷰가 끝나는 곳을
지나** 다음 널까지 읽는다 — 부분 뷰(`substr`)일 때 실제로 넘어간다. 열네 자리가 그러고 있었다
(Editor 4 · Engine 10). 고치는 법은 `.data()` 를 **지우는 것**뿐이다.

`.data()` 가 맞는 것처럼 보여서 다시 자라기 쉬운 모양이라 게이트로 옮겼다
(`CheckLogViewArgument`). `string` 의 `.c_str()` 은 언제나 널로 끝나므로 잡지 않는다. 여러 줄로
쪼갠 호출은 놓치는데, 그 대신 오탐이 없다 — 이 저장소의 로그는 거의 한 줄이다.
`CheckLintsAreAlive` 가 27 케이스 / 17 게이트가 됐다.

### 2026-09-20 (에디터 워크스페이스 둘 — 순회 중 목록 변경 · GUID 두 표가 어긋났다)

**`ReloadFileManager::dispatchEvents` 가 범위 for 로 돌면서 콜백을 불렀다.** 리로드 콜백이 자기
감시를 다시 거는 것은 흔한 일인데(에셋을 다시 읽고 다시 arm 한다), 그러면 `_listWatch` 가 순회
도중 재할당돼 **참조가 뜬 메모리를 가리킨다.** 인덱스로 돌고 델리게이트를 부르기 전에 복사하는
것으로 고쳤다 — `MulticastDelegate::broadcast` 가 같은 이유로 같은 모양을 쓴다. 되돌려 놓으면
ASAN 이 `heap-use-after-free` 로 그 자리에서 잡는다.

검사를 위해 `dispatchEvents` 를 공개로 올렸다. **이것이 이 클래스의 절반이기 때문이다** —
나머지 절반(폴링)은 OS 알림에 기대므로 검사가 타이밍에 흔들린다. 이벤트를 직접 넣을 수 있으면
"누구에게 가고 누구에게 안 가는가" 와 "콜백이 목록을 바꿔도 견디는가" 를 파일 시스템 없이
결정적으로 본다.

**`EditorWorkspace::setGuid` 가 한쪽만 끊었다.** "이 오브젝트가 들고 있던 옛 GUID" 는 끊는데
"이 GUID 를 들고 있던 옛 오브젝트" 는 그대로 뒀다. 그러면 옛 오브젝트가 계속 그 GUID 를 가졌다고
답하는데(`getGuid`) 정작 GUID 로 찾으면 새 오브젝트가 나온다 — 두 표가 서로의 역이 아니게 된다.
GUID 는 되돌리기가 오브젝트를 다시 찾는 열쇠이고(`EditorTransaction::findTargetGameObject`),
되돌리기로 오브젝트를 되살릴 때 같은 GUID 를 새 오브젝트에 다시 붙이므로 실제로 지나간다.
증상은 **되돌리기가 엉뚱한 오브젝트에 적용되는 것**이고 그 자리에서 터지지 않는다.

### 2026-09-20 (같은 함정에 문이 하나 더 있었다 — `EditorContext::get()`)

`CheckNullableServiceUse` 는 2026-09-18 에 "nullptr 을 돌려줄 수 있는 조회를 확인 없이 `->` 로
따라가는 곳" 을 잡으려고 만든 게이트다. 그런데 **같은 함정의 다른 문**을 보지 않고 있었다 —
`EditorContext::get()` 은 속으로 `getService<EditorContext>()` 를 부르고 없으면 정적 폴백을
돌려주므로 이것도 nullptr 이 될 수 있다. 세어 보니 **여든두 자리는 받아서 확인하는데 쉰두 자리가
그대로 역참조**하고 있었다. 이름만 달라서 게이트를 지나갔다.

쉰두 자리를 전부 "받아서 확인하고 쓰는" 형태로 바꾸고, 게이트 정규식에 그 문을 더했다. 넷은
**나중에 불리는 람다·델리게이트 안**이라 바깥 포인터를 쓸 수 없다 — 그 자리에서 다시 받고 다시
확인한다(커맨드 팔레트의 액션 셋, 에셋 메뉴 하나). 게이트 힌트에도 그 경우를 적었다.

**게이트가 스스로를 증명한다.** `selfTestCases` 에 `EditorContext::get()->` 조각을 더했고
`CheckLintsAreAlive` 가 26 케이스 / 16 게이트로 늘었다.

### 2026-09-20 (임포트가 같은 이름의 자산을 아무 말 없이 덮었다)

`EditorAssetCommands::importFiles` 는 `ResourceUtil::makeSavePath` 가 준 경로로 그냥 복사했다.
그 함수는 폴더와 파일 이름을 잇기만 하므로, 같은 이름의 파일을 끌어다 놓으면 폴더에 있던 것이
**아무 말 없이 사라진다** — 에디터의 임포트에는 되돌리기가 없으므로 그대로 잃는다. 원래 있던
것과 **같은 파일**인 경우는 바로 위에서 경로 비교로 이미 걸러지므로, 거기까지 오는 것은 다른
파일인데 이름만 같은 경우다.

`ResourceUtil::makeUniqueSavePath` 를 만들어 확장자 앞에 `_2` · `_3` … 을 붙인다(게임 오브젝트
이름을 고르는 `makeUniqueNameUnlocked` 와 같은 규약). 같은 파일에서 하나 더: `deleteAsset` 이
`".meta"` 를 리터럴로 적고 있었다 — `path::kMetaExtension` 이 정본이다.

### 2026-09-20 (같은 날 넣은 이중 반납 단언이 틀렸다 — "가득 참" 은 이중 반납이 아니다)

이번 세션 앞쪽(`b94b2818`)에서 `LockFreeObjectPool::release` 에 "자유 큐가 가득 찼다 = 이 포인터가
이 풀의 것이 아니거나 이미 반납된 것" 이라는 단언을 넣었다. **그 전제가 틀렸다.**

내부 MPMC 큐는 Vyukov 방식이라, 소비자가 칸을 집어간 뒤 그 칸의 **순번을 아직 공개하지 않은
찰나**에도 생산자에게 가득 찼다고 답한다. 자리 수(`Capacity`)와 블록 수가 같으므로 그 찰나는
반드시 지나가는데, 한 번의 실패로 물러서면 그 블록이 자유 목록으로 돌아가지 못한다 — 풀이
**조용히 줄어들고** 오래 돌수록 `acquire` 가 더 자주 널을 돌려준다. Debug 에서는
`SW_LOG_ASSERT` 가 `SW_DEBUG_BREAK` 까지 하므로 `CoreTest` 가 **간헐적으로 exit 3 으로 죽었다**
(직접 실행 25회 중 1회, ctest 로도 재현). 물러서지 않고 다시 시도하도록 고쳤다.

**기존 동시성 케이스는 이것을 못 잡았다.** `LockFreeObjectPoolConcurrent` 는 마지막에
`getAvailableCount() == kCapacity` 를 보지만, Debug 에서는 그 줄에 닿기 전에 프로세스가 죽었고
Release 에서는 그 찰나가 드물어 5회 연속으로 통과했다. 그래서 작은 풀(8칸)에 코어보다 많은
스레드(8개)를 붙여 찰나를 자주 만드는 `LockFreeObjectPoolLosesNoBlockUnderContention` 을 새로
넣었다 — "풀이 여전히 용량만큼 내줄 수 있는가" 하나만 묻는다. 물러서는 쪽으로 되돌리면
Release 에서 **3회 모두** 진다.

### 2026-09-20 (ReflectionParser 셋 — 이름 충돌 · 3주 묵은 파서 · 연속 열거형이 비트플래그였다)

**같은 이름의 헤더 둘이 한 산출물을 노리면 뒤엣것이 조용히 덮었다.** 생성 파일 이름은 소스의
**파일 이름만** 으로 짓는다(`ParserUtil::makeGeneratedPath`). 한 모듈 안에 같은 이름의 헤더가 둘
있으면 나중에 도는 쪽이 앞의 것을 덮고, **앞 헤더의 타입들은 아무 말 없이 등록되지 않는다** —
증상은 한참 뒤 "씬이 그 컴포넌트를 못 찾는다" 로 나타나 원인이 코드젠이라는 것을 짚기 어렵다.
지금은 산출물 머리말의 소스 경로를 대조해 그 자리에서 빌드를 세우고 두 경로를 다 적는다.
헤더를 **옮긴** 경우(옛 경로가 더는 없다)는 정상이므로 조용히 덮어쓴다.

**ASAN 빌드의 파서 검사가 3주 묵은 바이너리를 돌리고 있었다.** `findReflectionParserExecutable`
은 `Bin` 을 먼저 보고 `BuildTools` 를 나중에 봤는데, 지금 빌드가 파서를 놓는 곳은 `BuildTools`
다. 예전 배치에서 `Bin` 에 놓였던 실행 파일이 빌드 디렉터리에 그대로 남아 있었고(정리되지
않는다), ASAN 쪽에는 8월 30일자가 있었다. 새로 넣은 검사가 이유 없이 지는 것으로 드러났는데,
**초록이든 빨강이든 그 결과는 지금 코드에 대한 답이 아니었다.** 함수 주석에는 이미 "`Bin` 옆이
아니라 `BuildTools` 에 있다" 고 적혀 있었다 — 주석은 알고 코드가 몰랐다. 순서를 뒤집었다.

**연속 열거형 셋이 비트플래그로 등록돼 있었다 — 아무도 시키지 않은 자동 감지 때문에.** 초기
커밋부터 "0 이 아닌 값이 모두 2의 거듭제곱이면 BitFlag" 라는 값 모양 자동 감지가 있었는데, 그
조건은 `{ Game = 0, Editor = 1, Custom = 2 }` 같은 **평범한 연속 열거형**에도 그대로 맞는다.
`CameraRole` · `PackEncryptionType` · `SampleStatus` 가 그렇게 등록돼 있었다 — 문자열 변환이
`toStringFlags` 로 가고 인스펙터가 콤보 대신 체크박스를 그린다. 더 얄궂은 것은 **한 번의 파싱이
같은 질문에 두 답을 냈다는 점**이다: `IsBitFlagEnum<>` 특수화는 명시한 `ENUM( Flags )` 로만
나가므로 그 셋에는 없었다. 등록부는 "플래그다", C++ 트레이트는 "아니다" 였다.

자동 감지를 **지웠다.** 비트플래그는 `ENUM( Flags )` 로 말한다 — 말하지 않아 놓친 쪽은 비트
연산자가 없어 그 자리에서 컴파일이 막히지만, 말하지 않았는데 켜지는 쪽은 조용히 틀린다.

그것을 지우려면 **중첩 열거형이 스스로 말할 수 있어야** 했다. 예전에는 `ENUM( Flags )` 가 클래스
안에 있으면 코드젠을 실패시켰다(밖에서 전방 선언할 수 없어 연산자 트레이트를 못 만든다). 그래서
중첩 열거형은 자동 감지에 기댈 수밖에 없었다 — 자동 감지가 못 사라진 이유가 그것이었다. 지금은
**연산자만 건너뛰고** 등록부 표시는 남긴다(인스펙터·문자열 변환은 연산자를 쓰지 않는다).
`|`·`&` 를 쓰면 그 자리에서 컴파일이 막히므로 조용히 잘못될 여지는 없다.

생성물을 바이트로 대조해 딱 그 셋만 `_bIsBitFlag = false` 로 바뀌고 `IsBitFlagEnum<>` 특수화
열 개는 그대로인 것을 확인했다.

### 2026-09-20 (RPC 봉투도 인자 타입을 싣고 오는데 읽을 때 버렸다)

`ReflectionRpc` 의 봉투는 인자마다 **보낸 쪽의 타입 해시**를 싣는다. 그런데 푸는 쪽은 그것을
`(void)typeNameHash;` 로 버리고 받는 쪽 시그니처만 보고 바이트를 읽었다. 시그니처가 어긋난 채
주고받으면(빌드가 다르거나 모듈이 핫리로드된 뒤, 또는 봉투가 망가진 채로) 같은 바이트를 다른
타입으로 읽어 **터지지 않고 값만 조용히 달라진다** — 바로 앞 항목의 스키마 이관과 같은 모양이다.

정규 이름으로 대조한다(`canonicalTypeNameByHash` + `isType`) — 별칭 때문에 스펠링이 다를 수
있기 때문이다. 등록부가 그 해시를 모르면 판단하지 않는다(그때는 아래 타입 분기가 걸러 낸다).

### 2026-09-20 (전선이 타입을 싣고 오는데 읽을 때 버리고 있었다 — 직렬화)

`Engine/Serialization` 을 함수 단위로 읽어 셋을 고쳤다. 첫째가 실제로 값을 망가뜨린다.

**스키마 이관이 payload 크기로 타입을 짐작했다.** `float32` 프로퍼티를 `string` 으로 바꾸는
이관에서 `SchemaMigrateInternal::formatPodToString` 이 크기만 보고 갈래를 골랐는데,
`sizeof(float32) == sizeof(int32)` 라 **float32 가지는 영영 돌지 않았다**(앞의 int32 가지가 먼저
걸린다). `1.5f` 가 그 비트값인 `"1069547520"` 으로 적혔다. `uint32` 도 마찬가지로 int32 로 읽혀
큰 값이 음수가 됐다. 전선 타입은 바이너리 태그(`wireTypeHash`)와 `SchemaOrphanValue._wireTypeHash`
가 **이미 들고 있었는데** `tryCoerceBinaryPayload` 까지 넘겨 주지 않았을 뿐이다. 넘겨 주고,
아는 타입이면 그 타입으로 적는다. 형제 케이스(`int32 -> string`)는 크기 짐작이 우연히 맞아서
줄곧 초록이었다.

**`Archive::readBytesView` 만 덧셈으로 재고 있었다.** `uint64` 길이를 받는 읽기가 셋인데
(`readBytes` · `readSubArchive` · `readBytesView`) 앞의 둘은 이미 `길이 > 남은 바이트` 로 빼서
재고 이것만 `위치 + 길이 > 전체` 였다. 위치가 0 이 아닐 때 큰 길이를 주면 그 합이 넘쳐 작아지고
검사를 통과한다 — 호출자가 받은 포인터에서 버퍼 밖을 그만큼 읽는다.

**`XmlSerializer` 의 `uniqueSeen` 은 쓰기만 하고 읽지 않았다.** 세 자리에서 채우고 마지막에
`(void)uniqueSeen;` 로 버렸다. JSON·Binary 는 같은 집합으로 "안 온 프로퍼티에 기본값" 을 채우는데,
XML 은 그 일을 가지마다 그 자리에서 하므로 집합이 필요 없다. 프로퍼티마다 해시 삽입 한 번씩을
씬·프리팹 로드 경로에서 덜어 냈다.

### 2026-09-20 (최소화한 창의 크기를 0 으로 기억하고 있었다)

Win32 의 `WM_SIZE` 는 최소화를 **클라이언트 영역 0x0** 으로 알린다. `Win32Window::wndProc` 은
그 값을 조건 없이 `_width`/`_height` 에 적고 나서야 `SIZE_MINIMIZED` 를 봤다 — 리사이즈 콜백은
안 부르지만 **크기는 이미 0 으로 덮였다.**

`IWindow::recreate()`(백엔드 교체가 이 길로 온다)는 기억해 둔 `_width`/`_height` 로 창을 다시
만든다. 그래서 최소화한 채 백엔드를 바꾸면 0x0 창이 만들어지고, 복원해도 그 크기가 남는다.
최소화가 말하는 것은 "안 보인다" 지 "0 칸이다" 가 아니다.

`WindowTest.MinimizingDoesNotForgetTheWindowSize` 를 추가했다 — 진짜 창을 띄우고 최소화한 뒤
크기를 보고, 그 상태에서 `recreate()` 까지 해 본다. 가드를 되돌리면 세 단언이 모두 진다.
(`WindowTest` 는 hostgpu 라 CI 가 못 돈다 — Shipping 에서 `-L hostgpu` 로 확인했다.)

### 2026-09-20 (파일과 사용자가 말한 크기를 그대로 잡고 있었다 — 타일맵)

`TileMapXmlData::loadFromXml` 은 `<width> x <height>` 만큼의 칸을 배열 넷에 잡는데, 그 둘은
파일이 주는 int32 였고 `<= 0` 만 걸렀다. `100000 x 100000` 한 줄이면 10^10 칸 요청이다.
에디터도 같은 길이다 — `TileMapPanel` 의 Width/Height 는 `ImGui::InputInt` 이고 Apply Size 가
그 값을 그대로 `resize()` 에 넘긴다. 숫자를 크게 적고 누르면 에디터가 죽는다.

상한은 `TileMapXmlData::kMaxTileCount`(2048 x 2048) 하나로 두고 로더와 패널이 같은
`isSizeSupported()` 를 본다. 곱은 int64 로 낸다 — `65536 x 65536` 은 int32 로 재면 2^32 라
0 으로 접혀 "작다" 가 된다. 무는지 확인할 때 가드를 빼자 그 케이스가 **테스트 프로세스를
데려갔다**(할당 실패).

**`FrameProfiler::Scope::_pName` 은 형식상 데이터 레이스였다.** 슬롯을 잡는 쪽은 아무 스레드나
될 수 있는데(워커가 자기 구간을 처음 만날 때 등록한다) 이름은 평범한 포인터로 적고, 같은 순간
다른 스레드가 중복을 찾느라 그 칸을 읽었다. `atomic<const utf8*>` 로 바꾸고 release/acquire 로
짝지었다. 이름이 같은 구간을 두 스레드가 동시에 처음 등록하면 여전히 슬롯이 둘 생길 수 있는데,
그것까지 막으려면 등록에 잠금이 필요하고 "측정이 실행을 막지 않는다" 는 이 파일의 방침과
맞지 않는다 — 그래서 헤더에 한계로 적어 두었다.

### 2026-09-20 (재진입 깃발을 네 곳에서 보고 한 곳에서 안 봤다 — 그 한 곳은 멈춘다)

`Engine/Utility` 를 함수 단위로 읽어 셋을 고쳤다. 셋 다 "형제는 맞는데 하나만 다르다" 모양이다.

**`CommandStack::jumpTo` 가 재진입 깃발을 안 봤다.** `_bIsExecuting` 은 undo/redo 콜백이 자기
자신을 새 명령으로 기록하지 못하게 막는 깃발이고, `push` · `pushCoalesce` · `undo` · `redo` 가
모두 본다. `jumpTo` 만 안 봤다 — 그런데 여기서는 값이 아니라 **진행**이 걸린다. 콜백 안에서
`jumpTo` 를 부르면 그 안의 `undo()` 가 깃발 때문에 아무것도 하지 않고 돌아오고, `_index` 가 줄지
않으므로 `while ( _index > targetIndex && canUndo() )` 가 영원히 참이다. 틀린 값이 아니라 **멈춘
에디터**다. 무는지 확인할 때 실제로 테스트가 25초 타임아웃까지 붙잡혔다.

같은 파일에서 하나 더: **명령이 하나로 끝난 트랜잭션만 트랜잭션 레이블을 버렸다.** 여러 개일
때는 `_transactionLabel` 을 쓰는데 하나일 때만 안쪽 명령의 레이블을 그대로 썼다 — "Move 3 objects"
로 묶었는데 실제 명령이 하나면 실행 취소 메뉴에 "Set position" 이 뜬다.

**`JsonValue::asInt`·`asFloat` 이 부호 없는 가지에 영영 닿지 않았다.** nlohmann 의
`is_number_integer()` 는 부호 있는 정수와 **부호 없는 정수 둘 다에 참**이다. 그래서 그 검사를
`is_number_unsigned()` 보다 먼저 두면 뒤엣것은 죽은 코드가 된다. `asUint` 만 순서가 맞아 있었고,
`asFloat` 에서는 그 탓에 `18446744073709551615` 가 **`-1.0`** 으로 돌아왔다.

**`KeyValueFile::parse` 는 먼저 적힌 값이 이겼다.** `emplace` 는 이미 있는 키를 덮지 않는다.
손으로 고친 설정 파일에서 같은 키를 아래에 다시 적으면 위의 옛 값이 그대로 읽힌다 — 고쳤는데
아무 일도 일어나지 않는, 원인을 짚기 어려운 모양이다. INI 계열의 통상 규약도, 이 파일의 `dump`
가 키마다 한 줄만 쓰는 것과도 "뒤가 이긴다" 쪽이 맞는다.

**문서만 고친 것 하나:** `JsonValue` 는 문서 안 노드를 가리키는 **빌린 포인터**인데, 객체도 배열도
연속 저장이라 같은 부모에 `set( 새 키 )` 나 `pushBack()` 을 하면 **앞서 꺼낸 형제 핸들이 해제된
메모리를 가리킨다.** 헤더에는 "clear/destroy 이후 무효" 만 적혀 있었다. 지금 저장소의 쓰기
코드는 전부 "하나 받아서 다 채우고 다음 것을 받는" 순서라 걸리지 않지만, 그 순서가 규약이라는
것이 어디에도 없었다.

**무는지 확인했다.** `jumpTo` 의 가드를 빼면 그 케이스가 타임아웃까지 돌아오지 않고, 나머지 셋은
해당 케이스가 그 자리에서 진다.

### 2026-09-20 (TaskManager 테스트 파일이 없었다 — 10 케이스로 채웠다)

`Test/CoreTest` 에 **TaskManager 전용 케이스가 하나도 없었다.** 1,355 줄짜리 동시성 핵심인데
`EngineTest` 의 다른 주제(AssetStreaming · Audio · GpuScene)를 통해 간접적으로만 돌고 있었다 —
그 테스트들은 스케줄러가 아니라 **자기 주제**를 보므로, 스케줄러 자체의 결함은 지나간다.
실제로 `scheduleReadyTask` 의 `notify_one` 결함이 그렇게 지나갔다.

`TestTaskManager.cpp` 에 **스케줄러가 지켜야 하는 약속**만 10 개 담았다:

| 케이스 | 무엇을 못박는가 |
|--------|----------------|
| `SubmittedTaskRunsBeforeWaitAllReturns` | 낸 일은 반드시 돌고 `waitAll` 이 그것을 보장한다 |
| `PrecedeKeepsTheDependencyOrder` | 후속은 선행이 **끝난 뒤에만** 돈다 |
| `ParallelCoversEveryIndexExactlyOnce` | 분할이 빠뜨리지도 겹치지도 않는다 |
| `ParallelBlockCoversTheWholeRangeExactlyOnce` | `[start, end)` 경계를 하나 더/덜 세지 않는다 |
| `WaitStageReturnsOnlyAfterEveryStageTaskIsDone` | 스테이지 대기가 끝을 보장한다 |
| `MainThreadTaskRunsOnlyOnTheMainThread` | 워커가 집어가지 않고, dispatch 로만, 메인에서 돈다 |
| `CancelledTaskDoesNotRunAndStillCompletes` | 취소한 일은 안 돌지만 카운터는 맞는다 |
| `WhenAllRunsOnceAfterEveryDependency` | 합류가 **한 번만**, 전부 끝난 뒤에 |
| `NestedWaitInsideATaskDoesNotStall` | 태스크 안의 대기가 work-helping 으로 풀린다 |
| `ConcurrentSubmitLosesNothing` | 4 스레드 × 500 제출에서 하나도 잃지 않는다 |

**무는지 확인했다.** 병렬 분할이 마지막 인덱스를 빠뜨리게 만들면 덮기 검사가
`Expected [0], Actual [1]` 로 지고, 메인 스레드 친화도를 무시하게 만들면 "워커가 집어갔다" 와
"메인이 아니다" 둘 다 진다.

> **함정 하나를 밟았다.** 처음에는 덮기 표를 `sw::vector` 로 두고 워커 안에서 `listHit[index]`
> 로 만졌는데 `exit 3` 으로 죽었다 — `vector::operator[]` 에 **레이스 탐지기**가 붙어 있어서
> 여러 워커가 동시에 들어오면 그것이 먼저 운다(원소가 원자라 진짜 레이스는 없다).
> `measure-in-release-not-debug` 메모에 *"워커에 sw::vector 인덱싱 금지"* 로 이미 적혀 있던
> 것이다. 시작 전에 `data()` 로 주소만 받아 두는 것으로 풀었다.

**모든 대기에 타임아웃을 건다.** 스케줄러가 멈추는 결함이 회귀하면 이 바이너리가 CTest
타임아웃(30초)까지 붙잡히는 대신 그 자리에서 진다.

### 2026-09-20 (Core 전체를 네 가지 모양으로 훑어 vector 에서 둘을 더 찾았다)

Core 를 함수 단위로 다 읽은 뒤, **이번 훑기가 되풀이해 만난 네 모양**을 Core 전체에 기계적으로
걸어 빠뜨린 곳이 없는지 확인했다:

| 모양 | 후보 | 결과 |
|------|------|------|
| `a + b > cap` 덧셈 경계 검사 | 8 | **2건 실재** (아래), 나머지 6 은 값이 작아 넘칠 수 없다 |
| `notify_one` | 2 | 둘 다 올바르다 — 대기자가 하나이거나(Logger) 조건이 같다(워커 풀) |
| `fetch_sub` 하한 없음 | 18 | 17 은 증감이 짝을 이룬다(참조 수·태스크 수), 1 은 앞선 커밋에서 고쳤다 |
| `SW_ASSERT` 만 두고 그 값으로 첨자 | — | `vector` · `formatstring` 에서 이미 고쳤다 |

**찾은 둘 — `vector` 의 남은 덧셈 가드.**

```cpp
erase( first, last ) : if ( offset + count > _size )      // last < first 면 count 가 거대해진다
insert( pos, count, value ) : if ( _size + count > _capacity )
```

`last < first` 인 이터레이터 쌍이면 `last - first` 가 음수라 `count` 가 `SIZE_MAX` 근처가 되고,
그 합이 **작은 수로 접혀** 가드를 그냥 지나간다. **`SW_ASSERT( offset + count <= _size )` 도 같은
식이라 같이 속는다** — Debug 에서도 울지 않는다. 그 뒤 `_size - count` 와 `fromIndex + count` 가
전부 범위 밖을 가리킨다.

이 파일은 이번 세션에서 이미 두 번 고쳤는데(자기 원소 이동 삽입 · `pop_back`), **덧셈 가드는
그때 함께 보지 않았다.** 기계적으로 훑고 나서야 나왔다 — 읽기만으로는 남는 자리가 있다는 뜻이다.

**검증.** `VectorTest.ReversedRangeAndHugeCountDoNotWrap`(뒤집힌 범위 · 정상 범위 · `SIZE_MAX`
개수). 되돌리면 프로세스가 죽는다(exit 3).

### 2026-09-20 (세지 않은 해제가 프로파일러 카운터를 1.8e19 로 접었다)

`MemoryProfiler::recordFree` 가 태그별 카운터를 그냥 뺐다:

```cpp
_arrStat[tagIdx]._currentAllocatedBytes.fetch_sub( size, relaxed );   // uint64
_arrStat[tagIdx]._currentAllocationCount.fetch_sub( 1, relaxed );
```

둘 다 `uint64` 라 0 아래로 내려가면 **1.8e19** 로 접힌다. 그리고 세지 않은 것을 빼는 경우가
실제로 있다 — **에디터의 프로파일러 패널에 추적 켜기 체크박스가 있어서**
(`ProfilerPanel.cpp:212`), 켜기 전에 잡힌 블록들이 켠 뒤에 풀리면 정확히 그렇게 된다.

**같은 함수 안에서** 콜스택 표는 처음부터 막고 있었다:

```cpp
if ( it->second._currentBytes >= size ) it->second._currentBytes -= size; else ... = 0;
if ( it->second._currentCount > 0 )     it->second._currentCount--;
```

세 줄 아래에 답이 있는데 위쪽만 빠져 있었다. `subtractSaturating` 하나로 맞췄다.

**검증.** `MemoryProfilerTest.FreeWithoutMatchingAllocationDoesNotWrap` — 추적을 끈 채 할당하고
켠 뒤 해제한다. 되돌리면 두 단언이 모두 진다.

**살펴보고 문제 없던 것 (Core/Memory · Core/Concurrency · Core/Process · Core/Log).**
`Memory::freeAligned` 는 헤더 매직으로 이중 해제를 막고, 해제 시 태그를 **헤더에 적힌 것**으로
넘기므로 스레드마다 다른 현재 태그로 어긋나지 않는다. `LinearAllocator` · `FrameArenaAllocator` 는
뺄셈 비교와 `size + alignment` 오버플로 가드가 이미 들어가 있다. `ConcurrentQueue` 는 Vyukov
MPMC 가 정확하고, `LockFreeQueue` 는 SPSC 메모리 순서가 정석이며, `mutex` 는
`condition_variable_any` 와 짝이 맞는다. 크래시 핸들러는 Windows · POSIX 가 **같은 재진입 가드와
같은 기록 순서**를 쓰고 POSIX 는 `sigaltstack` 까지 깐다. `Logger` 는 출력 장치를 **지우지 않으므로**
락 밖에서 생포인터로 쓰는 것이 안전하고, 워커의 조건 대기는 5ms 타임아웃이 안전망이다.
`MulticastDelegate` 는 방송 중 `add`/`remove` 를 인덱스 순회 + 원소 복사로 견딘다.

> 함께 고친 작은 것: `LockFreeObjectPool::release` 가 `enqueue` 실패를 무시하고 `_activeCount` 를
> 줄였다. 자유 큐의 자리 수는 정확히 `Capacity` 이므로 가득 찼다는 것은 **이미 반납된 포인터**
> 라는 뜻인데, 그때도 세면 0 에서 뒤집혀 소멸자의 "다 반납됐나" 단언이 엉뚱한 말을 한다.

### 2026-09-20 (풀이 이중 반납을 조용히 받아들이고 있었다)

`PoolAllocator::free` 는 Debug 에서 **"이 포인터가 내 청크 안인가"** 는 봤지만 **"이미 자유
목록에 있는가"** 는 보지 않았다. 같은 블록을 두 번 넣으면:

```
첫 반납:  p->_pNext = 이전머리 ;  머리 = p
둘째 반납: p->_pNext = 머리(= p) ;  머리 = p      → p->_pNext == p, 자기 고리
```

그 뒤 **모든 할당이 같은 블록을 돌려준다.** 서로 다른 두 객체가 같은 주소에 앉고, 나머지 풀은
통째로 떨어져 나간다. 증상은 한참 뒤 엉뚱한 자리에서 터진다.

**이번 훑기에서 실제로 겪은 모양이다** — `GameObjectManager::destroyObject` 의 check-then-set
경쟁(`3c736f5e`)이 정확히 이 경로였고, 그때는 5/5 세그폴트로만 드러났다. 원인은 고쳤지만
**풀이 그것을 받아 준다는 사실**은 그대로였다.

이제 Debug 에서 두 번째 반납 그 자리에서 멈춘다. 목록을 훑으면 해제가 O(n) 이 되므로
(풀 하나에 1,024 블록이면 매 해제마다 1,024 번) **자유 노드에 표식 하나**를 두고 O(1) 로 본다.
`sizeof( FreeNode )` 가 8 → 16 으로 늘지만 블록 크기는 어차피 16 배수로 올림되므로 **실제
블록 크기는 달라지지 않는다.** Release·Shipping 에는 코드 자체가 들어가지 않는다.

**검증.** 일회성 프로브로 확인했다 — 같은 블록을 두 번 반납하면 exit 3(단언), 탐지기를 빼면
**조용히 exit 0** 이다. 단언은 프로세스를 세우므로 케이스로 남길 수 없어서, 남긴 것은
`MemoryTest.PoolFreeListDoesNotLoopAfterChurn`(정상 순환 뒤 새로 받은 16 블록이 전부 다른
주소인지 — 고리가 생기면 1 이 된다) 이다.

### 2026-09-20 (파일 감시 중복 접기가 폴더를 안 봤다 · 디렉터리 순회가 예외를 던졌다)

**1) `IFileWatcher::pushChange` 의 연속 중복 접기가 `_directory` 를 비교하지 않았다.**

```cpp
if ( last._action == action && last._filename == filename )   // 디렉터리는 안 본다
    return;
```

Windows · Linux 는 감시 루트 하나를 `directory` 로 주고 하위 경로를 `filename` 에 담으므로 이름만
봐도 갈렸다. **macOS 만 이벤트마다 그 파일이 있는 디렉터리를 준다** — 서로 다른 폴더의 같은
이름(`config.json` 둘)이 잇달아 오면 뒤엣것이 조용히 사라진다. 이 저장소는 macOS 를 빌드하지
않으므로 드러날 길이 없었다(이 파일의 헤더 주석이 *"실제로 macOS 쪽만 연속 중복 접기가 빠져
있었다"* 고 적어 둔 것과 같은 자리다 — 한 번 더 같은 곳에서 갈라졌다).

플랫폼 파일을 고치는 대신 **공유 코드 쪽에서 한 번 더 보게** 했다. 어느 플랫폼이 무엇을 넘기든
답이 맞고, 앞의 둘은 값이 늘 같으므로 동작이 달라지지 않는다.
`FileWatcherTest.SameNameInDifferentDirectoriesIsNotCollapsed` — 되돌리면 `Expected [2], Actual [1]`.

**2) `collectFiles` · `collectFolders` 가 `std::error_code` 없이 순회자를 만들었다.**

네 곳 모두(`파일/폴더 × 재귀/비재귀`) `std::filesystem::directory_iterator{ path }` 였다. 이
형태는 권한이 없는 폴더, 순회 도중 지워진 폴더, 윈도우의 보호된 정션에서 **예외를 던진다.**
두 함수는 `bool` 로 실패를 알리는 약속이라 그 예외가 호출부를 뚫고 나간다. 같은 파일에서
`std::error_code` 를 11번 쓰고 있었는데(`makeRelativePath` · `makeAbsolutePath` 등) 그 형태가
순회 쪽으로 옮겨지지 않았다.

네 벌을 헬퍼 하나(`forEachDirectoryEntry`)로 모으고 `skip_permission_denied` 와 `increment( ec )`
를 쓴다. `entry.is_directory()` 도 던지므로 `is_directory( ec )` 로 감쌌다.

> **이 쪽은 무는 테스트가 아니다.** 권한이 없는 폴더나 순회 중 사라지는 폴더를 이식성 있게 만들
> 방법이 없어서, `FileTest.DirectoryWalkCollectsFilesAndFolders` 는 **네 경우가 예전과 같이
> 걷는지**(비재귀/재귀 × 파일/폴더 + 필터 + 없는 폴더)를 지키는 회귀 가드다. 네 벌을 하나로
> 모으는 변경이라 그 가드가 필요했다.

**살펴보고 문제 없던 것 (Core/File).** `FileUtil::readFile` 은 `offset > uFileSize` 를 먼저 보고
뺄셈으로 길이를 낸다. `suffixAfterPathComponent` 는 앞을 확인하지 않지만 **호출부 넷이 모두**
`startsWithPathComponent` 로 먼저 막는다. 파일 다이얼로그는 워커 스레드에서 델리게이트를 부르지
않고 큐에 담으며, 세대 카운터로 취소를 가리고, 델리게이트 호출·파괴를 모두 락 밖에서 한다.
세 플랫폼 워처의 `_bIsWatching` 은 전부 `atomic<bool>` 이다.

### 2026-09-20 (fixed_string::erase 가 큰 길이에서 첨자를 접었다 · formatstring 은 용량 0 에서 무제한으로 썼다)

Core/String 을 함수 단위로 읽다가 둘 나왔다. 둘 다 **같은 파일 안에 올바른 형제가 있다.**

**1) `basic_fixed_string::erase` — 덧셈 판정**

```cpp
if ( length == npos || pos + length >= currentSize )   // 끝까지 지우기
else
    Memory::move( _arrData + pos, _arrData + pos + length, ... );
```

`npos`(`0xFFFFFFFF`) 가 아닌 큰 길이가 들어오면 — 끝-시작 이 뒤집힌 계산 같은 것 — `pos + length`
가 `uint32` 안에서 **접혀** 작은 수가 되고, "끝까지 지우기" 가 아니라 아래 **범위 이동** 으로
빠진다. 거기서 `_arrData + pos + length` 라는 엉뚱한 주소를 읽는다. 변이로 되돌리면
**세그폴트**다.

같은 파일의 `substr` 은 처음부터 뺄셈(`MathUtil::min( length, currentSize - pos )`)이었다.
`erase` 만 덧셈이었다.

**2) `formatstring` — 용량 0**

입구에 `SW_ASSERT( pBuffer != nullptr && capacity > 0 )` 뿐이었다. 그 뒤로 `capacity` 는 어디서나
`capacity - 1` 로 쓰인다(`write` 의 남은 자리 계산, 종결자 위치). 0 이면 그 뺄셈이 뒤집혀
4,294,967,295 가 되고 **길이 제한 없이** 복사한다. 한 칸짜리 버퍼에 `capacity = 0` 으로
`"hello world %#"` 를 찍으면 그대로 다 들어간다(`Expected [Z], Actual [h]`, 힙이 깨져 종료코드 127).

지금 호출부는 0 을 주지 않는다 — `StringBuilder::appendFormat` 은 남은 자리가 2 미만이면 먼저
늘리고, `CrashContext::appendLine` 은 `inOutLength + 1 >= capacity` 를 먼저 본다. 그래도 공개
API 이고 단언은 Debug 밖에서 사라지므로, 이 저장소가 `vector::erase` · `DynamicBitset::operator&=`
에서 한 것과 같은 모양으로 진짜 가드를 둔다.

**검증.** `StringTest.FixedStringEraseWithHugeLengthDoesNotWrap`(되돌리면 세그폴트) ·
`StringTest.FormatStringWithZeroCapacityWritesNothing`(Debug 는 `SW_ASSERT` 가 먼저 우는 것이
의도라 skip, Release·Shipping 에서만 잰다).

**살펴보고 문제 없던 것 (Core/String).** `basic_fixed_string` 의 `insert` · `append` 계열은
`clampToRemaining( currentSize, length )` 로 `N - currentSize` 를 넘지 못하게 막아
`_arrData[N + 1]` 밖으로 나가지 않는다. `hashed_string` 은 아레나 할당이 `_globalAppendMutex`
안에서만 일어나고, 락 순서가 양쪽 경로 모두 shard → global 이라 교착이 없으며, 락 없는
`size()`/`c_str()` 도 **인덱스를 얻는 모든 경로**(같은 스레드 · 객체 전달 · 샤드 맵 조회)에
동기화 간선이 있어 안전하다. `string_splitter` 는 `delimLength >= 1` 이 보장돼 제자리걸음이 없다.

### 2026-09-20 (메인 스레드 일감을 넣고 아무나 깨우고 있었다)

`scheduleReadyTask` 가 **메인 스레드 전용** 일감을 큐에 넣은 뒤 이렇게 알렸다:

```cpp
std::scoped_lock<mutex> waitLock{ _waitAllMutex };
_cvWaitAll.notify_one();      // <- 이 파일의 다른 다섯 통지는 전부 notify_all 이다
```

그 일감을 실행할 수 있는 것은 **메인 스레드뿐인데**(`dispatchMainThreadTasks`), `_cvWaitAll` 에는
`waitAll` · `waitStage` 로 들어온 아무 스레드나 잠들어 있다. `notify_one` 이 엉뚱한 스레드를
깨우면 그쪽은 자기 조건이 그대로임을 보고 다시 잠들고, **메인 스레드는 계속 잔다.**

회복할 길도 없다. 완료 쪽 통지는 `_activeTaskCount` 가 0 이 될 때만 울리는데
(`if ( activeLeft == 1 )`), 방금 넣은 메인 일감이 남아 있으므로 0 이 되지 않는다. 그리고
`waitAll()` · `waitStage()` 의 기본 대기는 **타임아웃이 없다**(`_cvWaitAll.wait( lock )`).
서로를 기다리며 둘 다 멈춘다.

**같은 파일의 `_cvWorker` 쪽 `notify_one` 은 올바르다** — 워커들은 전부 같은 조건("일감이
있나")을 기다리므로 하나만 깨우는 것이 맞고, 다 깨우면 우르르 몰린다. 대기자마다 **조건이
다른** `_cvWaitAll` 에 그 방식을 그대로 가져온 것이 문제였다.

> **무는 테스트를 쓰지 못했다.** 재현하려면 (1) 메인 스레드가 `waitAll()` 에서 스핀을 지나
> 잠들어 있고, (2) 다른 스레드도 `_cvWaitAll` 에 잠들어 있으며, (3) 그 순간 메인 일감이
> 들어오고, (4) `notify_one` 이 (2)를 고르는, 넷이 겹쳐야 한다. 결과가 **영구 정지**라
> 테스트로 만들면 회귀 시 바이너리 전체가 타임아웃까지 붙잡힌다. 고침은 한 단어이고 형제
> 다섯이 이미 그 형태라는 것으로 갈음한다.

**같이 적어 두는 공백:** `Test/CoreTest` 에 **TaskManager 테스트 파일이 없다.** 1,355줄짜리
동시성 핵심인데 `EngineTest` 의 다른 주제(AssetStreaming · Audio · GpuScene)를 통해 간접적으로만
돌고 있다. 위 같은 결함이 테스트로 잡히지 않는 이유가 그것이다.

### 2026-09-20 (StringUtil::strncpy 가 플랫폼마다 다르게 동작했다)

이름은 *"지정된 길이만큼 문자를 **안전하게** 복사합니다"* 인데, 두 갈래가 서로 다른 일을 했다:

```cpp
#if defined( SW_PLATFORM_WINDOWS )
    strncpy_s( pOutDest, length, pSource, length );   // 안 들어가면 목적지를 **비우고** 핸들러 호출
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
    ::strncpy( pOutDest, pSource, length );           // `length` 글자만 복사하고 **끝을 안 맺는다**
#endif
```

- **Windows**: `count >= destsz` 이고 원본이 더 길면 `strncpy_s` 는 목적지를 빈 문자열로 만들고
  잘못된 파라미터 핸들러를 부른다 — 변이 실행에서 **프로세스가 그대로 죽었다**(exit 3).
- **Linux · macOS**: 종결자가 없다. 뒤이어 읽는 쪽이 버퍼 밖까지 훑는다.

어느 쪽도 "안전하게" 가 아니고, 같은 코드가 WSL 빌드에서 다르게 움직였다. 두 오버로드가 한
헬퍼(`StringUtilInternal::copyTerminated`)를 쓰게 해서 **언제나 NUL 로 끝나고 들어가지 않으면
자르는** 하나의 규약으로 맞췄다(strlcpy 형태). `length` 는 목적지 **버퍼 크기**라는 것도
헤더에 적었다 — 그 모호함이 애초 원인이다.

> 엔진 안에는 호출부가 없고 테스트 둘만 쓰고 있었다. 그래서 고르는 길은 "지우거나 바로잡거나"
> 였는데(이 훑기에서 컴포넌트 이동 연산은 지우는 쪽을 골랐다), 이쪽은 **쓰임새가 분명한
> 기본 도구**라 바로잡는 쪽이 맞다 — 첫 엔진 호출자가 밟을 지뢰를 남겨 둘 이유가 없다.

**검증.** `StringTest.StrncpyAlwaysTerminatesAndTruncates`(들어가는 경우 · 자르는 경우 ·
길이 0 · 널 원본 · utf16). 되돌리면 자르는 블록에서 프로세스가 죽는다.

**살펴보고 문제 없던 것 (Core/String).** `stristr` 는 남은 길이를 재지 않고
`string_view( pStr, subLen )` 을 만들지만, `equals` 가 첫 불일치에서 멈추고 건초더미의 NUL 이
바늘의 어떤 글자와도 다르므로 읽기가 종결자에서 끝난다 — 범위 밖으로 나가지 않는다.

### 2026-09-20 (sparse_set::emplace 가 칸을 먼저 지우고 그 지워진 것에서 지었다)

같은 키로 다시 `emplace` 하면 그 칸을 **제자리에서 지우고 다시 지었다:**

```cpp
T* ptr = &_listDenseValue[_listSparse[key]];
ptr->~T();
new ( ptr ) T( std::forward<Args>( args )... );
```

두 가지가 깨진다:

1. **인자가 그 칸 자신을 가리키면**(`set.emplace( k, set[k] )`) 이미 지워진 객체에서 만든다.
   힙을 든 타입이면 해제된 메모리를 읽는다.
2. **생성자가 던지면** 지워진 칸이 그대로 남아, 나중에 벡터가 소멸할 때 **두 번 지워진다.**

임시를 먼저 짓고 옮겨 넣는 것으로 바꿨다(`_listDenseValue[...] = T( args... )`). 둘 다 없어진다.

**검증.** `SparseSetTest.EmplaceFromItsOwnValueDoesNotReadDestroyedMemory`. 되돌리면
`Expected [zzz…], Actual []` 로 진다 — ASAN 없이 결정적이다.

**살펴보고 문제 없던 것 (Core/Container).** `unordered_map` 의 swap-and-pop erase 는 옮겨 온
원소의 버킷 체인 재연결이 네 경우 모두 맞다(마지막 원소가 같은 버킷의 머리 · 앞 노드 · 뒤 노드 ·
다른 버킷). `DynamicBitset` 은 꼬리 비트를 더럽힐 수 있는 다섯 곳(`resize` · `set()` · `flip()` ·
두 시프트)이 전부 `sanitize()` 를 부르고, `&=` · `|=` · `^=` 는 크기가 다르면 **아무것도 하지
않는다**(`SW_LOG_ASSERT` 는 Debug 밖에서 사라지므로 그 뒤에 진짜 가드가 있다 — 이 저장소가
`vector::erase` 에서 한 것과 같은 모양이다). 시프트 연산도 `shift >= _bitCount` 를 먼저 걸러
`kBitsPerBlock - bitShift` 가 폭만큼 시프트되는 일이 없다.

### 2026-09-20 (vector 의 이동 insert 만 자기 원소 가드가 없었다)

Core 를 다시 훑다가 `vector` 의 삽입 계열에서 **한 판만** 빠져 있는 것을 찾았다:

| 함수 | 값을 먼저 떠 두는가 |
|------|--------------------|
| `push_back( const T& )` | O |
| `push_back( T&& )` | O |
| `emplace_back( Args&&... )` | O |
| `insert( pos, count, value )` | O — 주석에 *"`v.insert( v.begin(), 3, v[0] )` 는 적법하다"* 고 적혀 있다 |
| **`insert( pos, T&& )`** | **X** |

그리고 이 오버로드는 **재할당이 없어도 틀린다.** `push_back` 계열은 끝에 붙이므로 위험한
순간이 재할당뿐이지만, `insert` 는 **밀기 루프가 `value` 가 가리키는 칸을 먼저 덮는다**:

```cpp
list = { alpha, bravo, charlie, delta };          // 용량은 넉넉하다
list.insert( list.begin(), std::move( list[2] ) );
// 고치기 전: list[0] == "bravo"   ← index 2 가 이미 덮인 뒤에 읽었다
// 고친 뒤:   list[0] == "charlie"
```

재할당까지 겹치면 `reserveInternal` 이 옛 버퍼를 **해제한 뒤**라 죽은 자리를 읽는다.
값을 무조건 한 번 떠 둔다(`count` 판과 같은 모양이다 — 이동이 하나 더 들지만 `insert` 는
이미 O(n) 이동이다).

**같은 파일에서 하나 더.** `erase` 는 *"`SW_ASSERT` 는 Release 에서 통째로 사라진다"* 는 이유로
진짜 가드를 들고 있는데, **`pop_back` 과 두 `insert` 오버로드는 단언뿐이었다.** 빈 벡터에서
`pop_back` 하면 `_size - 1` 이 뒤집혀 `_pData[SIZE_MAX]` 의 소멸자를 부른다 — Release 에서
**세그폴트**로 확인했다. 셋 다 `erase` 와 같은 모양으로 맞췄다.

**검증.** `VectorTest.InsertMoveAcceptsAnElementOfItself`(되돌리면 `Expected [charlie],
Actual [bravo]` — ASAN 없이 결정적으로 진다) · `VectorTest.PopBackOnAnEmptyVectorIsSafe`
(Debug 에서는 `SW_ASSERT` 가 먼저 울려 프로세스를 세우는 것이 **의도**이므로 그쪽은 skip 하고
Release·Shipping 에서만 잰다. 되돌리면 Release 에서 세그폴트).

### 2026-09-20 (데이터 요청이 존재 확인에 편승해 "성공" 과 빈 버퍼를 돌려줬다)

`AssetStreamingQueue` 에는 요청이 두 가지 있다 — `requestAsset` 은 **있는지만** 보고
(`ResourceUtil::hasResource`), `requestAssetData` 는 **바이트를 읽는다**
(`readBinaryResource`). 그런데 진행 중인 요청을 담는 목록이 **하나**였다:

```cpp
if ( _uniqueActiveRequest.find( pathStr ) != _uniqueActiveRequest.end() )
{
    _mapInFlightDataCallback[pathStr].push_back( onComplete );   // 편승한다
    return true;
}
```

존재 확인이 아직 돌고 있을 때 데이터 요청이 들어오면 그 태스크에 붙는데, **그 태스크는 파일을
읽지 않는다.** 완료가 오면 데이터 콜백이 `bSuccess = true` 와 **빈 버퍼**를 받는다. 성공이라고
말하면서 아무것도 주지 않는, 가장 나쁜 모양의 틀린 답이다.

진행 중인 것이 어느 쪽인지 알아야 "편승해도 되는가" 를 가릴 수 있으므로
`_uniqueActiveDataRequest` 를 둔다. 존재 확인에만 붙게 되는 경우에는 **세대를 올려** 그 태스크의
완료를 버리고 데이터 태스크를 새로 낸다 — `processAssetTask` 는 세대가 어긋나면 콜백 표에
손대기 전에 돌아가므로, 먼저 등록된 존재 확인 콜백도 그대로 살아 새 태스크의 완료에 함께 실린다.

**검증.** `AssetStreamingTest.DataRequestDoesNotPiggybackOnAnExistenceCheck` — 32 라운드,
되돌리면 3/3 진다.

> **테스트를 두 번 잘못 썼다.** (1) 처음에는 `if ( bDataSuccess )` 안에서만 바이트를 단언했는데,
> 기존 케이스들이 쓰던 경로(`Resource/common/shaders/forward_lit.hlsl`)가 **일부러 없는 것**이라
> `bSuccess` 가 false 로 와서 단언을 통째로 지나갔다 — 변이가 3/3 통과했다. 실제로 읽히는
> 에셋을 쓰는 케이스가 이 저장소에 없었던 것이다. (2) 그래서 진짜 셰이더 경로로 바꿨더니
> **Shipping 에서만** 졌다 — 거기서는 리소스가 팩에만 있고 `.hlsl` 원본은 들어가지 않는다.
> 결국 테스트가 **자기 파일을 만들어 절대 경로로** 준다(`ResourceUtil` 은 절대 경로를 디스크에서
> 그대로 읽으므로 프리셋이 달라도 답이 같다).

### 2026-09-20 (팩 헤더는 쟀는데 FAT 항목은 재지 않았다)

`ResourcePackReader::validateHeaderGeometry` 는 헤더가 말하는 구역(인덱스 표 · 스트링 풀)이
실제 파일 안에 있는지 재고, 그 주석은 이유까지 적어 두었다 — *"예전에는 헤더의 수를 그대로 믿고
`resize` 했다 — 잘린 팩 하나가 수십 기가짜리 할당 요청이 될 수 있었다"*.

**그 검사가 FAT 항목에는 적용되지 않았다.** 그런데 `readFile` 은 항목이 적어 둔 크기를 그대로
쓴다:

```cpp
outBytes.resize( entry._uncompressedSize );      // FAT 에서 온 값. 검사 없음
compressedBytes.resize( entry._compressedSize ); // 마찬가지
```

`_compressedSize` · `_uncompressedSize` 는 `uint32` 라 최악이 4GB 다 — 손상된 **32바이트 항목
하나**가 4GB 할당 요청이 된다. 같은 파일 안에서 형제가 갈려 있었던 셈이고, 고친 쪽이 이유를
적어 두었는데도 옮겨지지 않았다(이번 훑기에서 되풀이된 모양이다).

항목 검사는 **여는 시점**에 한 번 한다(`validateFileEntry`) — 그러면 `readFile` 은 그 값을
믿어도 된다. 페이로드가 파일 안에 있는지는 **뺄셈으로** 재고(`offset + size` 는 넘칠 수 있다),
압축 항목의 원본 크기는 파일로 묶이지 않으므로 `CompressionStream::kMaxUncompressedSize` 를
쓴다 — "이 컨테이너가 다루는 가장 큰 조각" 의 답이 두 개일 이유가 없다.

**검증.** `ResourcePackTest.CorruptEntrySizeIsRejected` — 멀쩡한 팩을 만들고 FAT 항목 하나의
크기·오프셋만 망가뜨린 뒤 `open()` 이 거부하는지, 되돌리면 다시 열리는지 본다. 검사를 빼면
`open()` 이 성공해 버려 진다.

### 2026-09-20 (부모 체인을 거는 세 곳이 전부 순환에서 멈추지 않았다)

`TypeInfo` 의 부모 체인(`_parentFQN`)을 거는 곳이 셋 있는데 **셋 다** 순환을 대비하지 않았다:

| 함수 | 모양 | 순환일 때 |
|------|------|-----------|
| `isDerivedFrom` | `while` 루프 | **영원히 돈다** (스택도 안 넘으니 더 알아채기 어렵다) |
| `findPropertyInHierarchy` | 재귀 | 스택 오버플로 |
| `getPropertiesWithBase` | 재귀 | 스택 오버플로 |

같은 저장소의 `ComponentDefaults::collectTypeChain` 은 *"순환 방지 — 이미 담은 타입이면 멈춘다"*
를 **명시적으로** 하고 있었다. 그 가드가 형제들로 옮겨지지 않았다 — 이번 훑기에서 네 번째로
만난 같은 모양이다.

`_parentFQN` 은 코드젠이 적는 값이라 정상 C++ 로는 순환이 나오지 않는다. 그런데
**`registerClass` 는 공개 API 이고 그 값을 검사하지 않는다.** 모듈이 따로따로 등록되는
핫리로드에서는 A 가 B 를 부모로 적고 B 가 나중에 A 를 부모로 적는 조합이 만들어질 수 있다.

지나온 타입을 적어 두는 `markVisitedOrStop` 하나를 두고 앞의 둘이 쓴다(재귀였던
`findPropertyInHierarchy` 는 루프로 바꿨다). `getPropertiesWithBase` 는 부모의 **캐시**를
받아야 해서 평평한 순회로 못 바꾸므로, 재진입 깃발
(`_bBuildingPropertyWithBase`, 남아 있던 예약 비트를 썼다)로 끊는다.

**검증.** `ReflectionTypeRegistryTest.ParentChainLoopDoesNotHang` — A→B→A 를 실제로 등록하고
셋을 모두 부른다. 가드를 빼면 **60초 타임아웃**(`exit=124`)으로 걸린다. 순환이어도 답이 있는
질문(`LoopA` 가 `LoopB` 에서 왔는가)에는 맞게 답하는지도 함께 본다.

### 2026-09-20 (지형 콜라이더 하나가 물리 셀 표를 20만 칸으로 불렸다)

`PhysicsWorld` 는 AABB 가 덮는 모든 셀에 바디 핸들을 적는다. **질의 쪽에는 상한이 있는데
삽입 쪽에는 없었다:**

```cpp
bool PhysicsWorld::shouldScanAllBodies( const CellRange& range ) const   // 질의: 1024 칸 넘으면 그리드를 안 쓴다
void PhysicsWorld::insertBodyToGrid( ... )                               // 삽입: 아무 제한 없이 다 적는다
```

셀 크기는 64 유닛이다. **20,000 x 10 x 20,000 짜리 바닥 콜라이더 하나면 셀 표에 약 196,000 칸
(313 x 2 x 313)이 생긴다** — 바디는 하나인데. `setAabb` 로 움직이기라도 하면 그만큼을 매번
지웠다 다시 적는다. 지형·바닥은 어느 게임에나 있고, 대개 그만큼 크다.

큰 바디는 그리드에 흩뿌리는 대신 목록 하나(`_listOversizedBody`)에 모으고, 그리드로 가는 질의가
그 목록을 **항상 함께** 본다. 그런 바디는 수가 적고 어차피 거의 모든 질의에 걸리므로 셀에
흩어 두는 것이 이득이 되지 않는다. 크기 판정(`isOversizedForGrid`)은 **AABB 만으로** 정해지므로
넣을 때와 뺄 때가 반드시 같은 답을 낸다 — 이 파일이 `CellRange` 를 따로 둔 것과 같은 이유다.

`kMaxBodyCellCount` 는 `kMaxQueryCellCount` 와 값이 같지만 **다른 질문**이라(질의 범위가 넓은가 /
바디가 큰가) 별칭을 두지 않고 따로 적었다 — 이 저장소의 상수 규칙 그대로다.

**검증.** `PhysicsTest.OversizedBodyDoesNotInflateTheGrid` 가 둘을 함께 본다: 셀 표가 작게
남는가, **그리고** 그럼에도 질의가 그 바디를 찾아내는가(셀에 없다는 것이 답을 바꾸면 안 된다).
제한을 없애면 **0.07ms → 750ms** 로 10,000배 느려지고 셀 수 단언이 진다.

### 2026-09-20 (같은 모양을 저장소 전체에서 찾았다 — 언어 코드도 그랬다)

`ComponentDefaults::getPath()` 를 고치고 나서 **"락을 잡은 함수가 뷰나 참조를 돌려주는 자리"**
를 저장소 전체에서 훑었다(27곳). 대부분은 문제가 아니다 — 오래 사는 객체의 **포인터**를 주는
것이라(코덱 · TypeInfo · GameObject) 락은 컨테이너를 지키는 것이고 수명은 별개다. 이 저장소가
의도한 설계다.

문제는 **자기 멤버의 참조**를 주는 자리다. 둘 있었다:

```cpp
const string& LocalizationManager::getCurrentLanguage() const
{
    std::shared_lock<std::shared_mutex> lock{ _mutex };
    return _currentLanguage;     // 락은 여기서 풀린다
}
```

`getFallbackLanguage()` 도 같고, `GameStrings::getLanguage()` 가 그것을 **게임 코드까지 그대로
흘려보내고** 있었다(자기도 `const string&` 로 받아 되돌려준다). 넷 다 값으로 바꿨다.

**검증.** `LocalizationManagerTest.LanguageCodeIsReturnedByValue`. 되돌리면 들고 있던 참조가
나중에 넣은 `aaaa...` 로 **보인다** — 스냅샷이 아니라 살아 있는 참조였다는 증거다.

> **테스트를 한 번 잘못 썼다.** 처음에는 `const auto held = loc.getCurrentLanguage();` 로 받았는데
> 변이를 넣어도 통과했다. `auto` 는 **참조를 벗긴다** — 반환형이 `const string&` 여도 `held` 는
> 복사본이 되어 버린다. 수명을 재는 테스트는 실제 호출 코드와 같은 모양(`const auto&`)으로
> 받아야 한다. 앞선 `ComponentDefaults` 테스트가 `const auto` 로도 물었던 것은 그쪽 반환형이
> `string_view` 라 `auto` 가 **그 뷰를 통째로 복사**했기 때문이다(포인터가 따라온다).

**살펴보고 문제 없던 것.** `ResourcePackReader::getPackPath()` · `getHeader()` 도 멤버 참조를
주지만, 그 둘은 `open()` 에서 한 번 정해지고 그 뒤로 바뀌지 않는다(다시 열면 리더가 새로 만들어
진다). `ShaderBindingLayoutCache::getOrBuild` 는 `unique_ptr` 항목 안을 가리키고 그 항목은 지워
지지 않는다.

### 2026-09-20 (뮤텍스로 지킨 문자열을 뷰로 돌려주고 있었다)

`ComponentDefaults::getPath()` 가 이렇게 생겼다:

```cpp
string_view ComponentDefaults::getPath() const
{
    std::scoped_lock<mutex> lock{ _defaultsMutex };
    return _customDefaultsPath;      // 락은 여기서 풀린다. 뷰는 남는다.
}
```

**지키는 것이 아무 뜻이 없다.** 받아 든 쪽이 뷰를 들고 있는 동안 다른 곳에서 `setPath` 를
부르면(길이가 달라지면 `string` 이 버퍼를 새로 잡는다) 그 뷰는 사라진 메모리를 가리킨다.
`Component::getDefaultGamedataPath()` · `ComponentDefaults::getDefaultsPath()` 가 그대로
그것을 흘려보내고 있었다. 셋 다 값으로 돌려주게 바꿨다.

**검증.** `ComponentDefaultsTest.DefaultsPathIsReturnedByValue`. 되돌리면 ASAN 이
`heap-use-after-free` 로 그 자리를 찍는다(`__msvc_string_view.hpp:206`).

### 2026-09-20 (기본값 로딩의 이중 검사 잠금이 깨져 있었다)

같은 파일에서:

```cpp
void ComponentDefaults::ensureDefaultsLoaded()
{
    if ( _bDefaultsLoaded )              // 락 밖에서 읽는다. 그냥 bool 이다.
        return;
    std::scoped_lock<mutex> lock{ _defaultsMutex };
    ...
    if ( _defaultsDoc.loadResource( ... ) )
        _bDefaultsLoaded = true;         // 락 안이지만 순서를 묶지 않는다
}
```

교과서적인 **깨진 이중 검사 잠금**이다. 문제는 경합 자체보다 **순서**다 — `true` 를 본 스레드가
그 앞에서 지어진 `_defaultsDoc` 을 함께 본다는 보장이 없어서, 다 지어지지 않은 XML 문서를
읽을 수 있다. `atomic<bool>` + release/acquire 로 묶었다.

문서 자체를 읽는 `apply()` 는 여전히 락을 잡지 않는다 — 컴포넌트를 만들 때마다 도는 자리라
잠그면 생성이 직렬화된다. 대신 **`reloadDefaults()` 를 컴포넌트 생성 중에 부르면 안 된다**는
것을 그 자리에 적어 두었다(다시 읽는 일은 개발 중 한 번씩 일어나는 일이다).

> 레이스 쪽은 무는 테스트를 쓰지 못했다. 재현하려면 한 스레드가 `reload` 하는 동안 다른
> 스레드들이 컴포넌트를 만들어야 하는데, 그 조합은 지금 엔진 흐름에 없다.

### 2026-09-19 (컴포넌트의 이동 연산은 전부 죽어 있었고, 전부 틀려 있었다)

`SceneComponent` 쪽을 막고 나서 기반인 `Component` 도 같은 방법으로 재 봤다 — `= delete` 로
바꾸고 빌드하면 쓰는 곳이 다 드러난다. 결과는 같았다: **정의 두 개 말고는 아무것도 깨지지
않는다.** 저장소 전체에서 컴포넌트를 옮기는 코드가 한 줄도 없다.

그런데 그 죽은 코드가 틀려 있었다:

```cpp
Component::Component( Component&& other ) noexcept
    : _componentId{ other._componentId }   // 복사만 한다. other 를 비우지 않는다
```

옮기고 나면 **둘이 같은 `_componentId` 를 갖는다.** `findComponentById` 는 어느 쪽이든 내놓을
수 있고, `ComponentHandle` 은 `(objectId, componentId)` 쌍이므로 핸들 해석도 갈린다.

`= delete` 로 바꾸고 파생 13종의 `= default` 선언을 걷었다. 전부 경고 0 으로 클린 빌드된다.
남은 방어선은 컴파일러이고, `GameObjectTest.ComponentsStayNonMovable` 이 `static_assert` 로
그것을 못박는다.

> **이 방법이 이번 훑기에서 가장 잘 들었다.** "이 코드가 쓰이나?" 를 grep 으로 묻는 대신
> 삭제하고 빌드해 보는 것이다. 컴파일러가 호출부를 빠짐없이 세어 준다 — `SceneComponent`
> (파생 8종)와 `Component`(파생 13종) 양쪽 다 이 방법으로 "아무도 안 쓴다" 를 몇 초 만에
> 확정했다. 안 쓰이는 것으로 확인된 코드는 고치지 않고 **막는 쪽**이 맞다: 고쳐 봐야 검증할
> 방법이 없고, 남겨 두면 다음 사람이 그것을 믿는다.

### 2026-09-19 (오브젝트를 인스펙터에서 보기만 해도 컴포넌트가 붙었다)

`GameObject::getTags()` 에 오버로드가 둘 있었다:

```cpp
TagContainer&       getTags();        // 없으면 TagComponent 를 **만들어 붙인다**
const TagContainer& getTags() const;  // 없으면 빈 컨테이너를 준다
```

`GameObject*` 로 부르면 **읽을 생각이었어도 비-const 쪽이 골라진다.** `InspectorPanel` 이
정확히 그렇게 쓰고 있었다:

```cpp
const vector<TagID>& listTag = pObj->getTags().getTags();   // pObj 는 GameObject*
```

받는 쪽이 `const&` 라 읽기처럼 보이지만, 고른 것은 만드는 쪽이다. **인스펙터에서 태그 없는
오브젝트를 선택하는 것만으로 그 오브젝트에 `TagComponent` 가 생겼다.** 컴포넌트 목록에 나타나고,
저장하면 씬 파일에도 들어간다. 오브젝트의 구성이 바뀌는 일이 오버로드 해석으로 조용히 정해지고
있었던 것이다.

쓰는 쪽을 `getOrCreateTags()` 로 갈랐다 — 저장소에 이미 있는 이름 관례다
(`getOrCreateLanguageTable` · `getOrCreateComponentPool`). **호출부는 한 줄도 고치지 않았는데
빌드가 통과한다** — 인스펙터가 원하던 것이 애초에 const 판이었다는 증거다.

**그 안에 하나 더 있었다.** 만드는 쪽이 실패했을 때(틱 중이라 `addComponent` 가 미뤄져 nullptr
을 줄 때) 이렇게 돌려줬다:

```cpp
return const_cast<TagContainer&>( s_emptyTags );
```

`s_emptyTags` 는 **파일 전역 공용 상수**다. `getTags() const` · `hasTag` · `matchesTagQuery` 가
태그 없는 모든 오브젝트에 대해 이것을 돌려준다. 여기에 한 번이라도 쓰면 **태그가 없는 모든
오브젝트가 그 태그를 갖게 된다.** 버리는 통(`thread_local`)으로 바꾸고 로그를 남긴다.

**검증.** `GameObjectTest.ReadingTagsDoesNotAttachATagComponent` — 비-const 포인터로 태그를 읽고
컴포넌트 수가 그대로인지 본다. 이름을 되돌리면 `Expected [0], Actual [1]` 로 진다.

### 2026-09-19 (~SceneComponent 가 미루는 경로를 타면 멈춘다)

```cpp
while ( _listChild.empty() == false )
{
    SceneComponent* pChild = _listChild.back();
    if ( pChild != nullptr )
        pChild->detachFromComponent();   // 틱 중이면 **미루고 그냥 돌아온다**
    else
        _listChild.pop_back();
}
```

`detachFromComponent` 는 `isParallelTransformReadOnly()` 면 일을 큐에 넣고 돌아온다 —
`_listChild` 가 줄지 않으므로 이 루프는 **끝나지 않고**, 미룬 일만 무한히 쌓인다. 게다가 그
일이 나중에 실행될 때 핸들로 되찾을 자기 자신은 이미 없다.

지금은 닿지 않는다. 파괴는 `processDeferredDestruction` 에서만 일어나고 그것은 틱 창 밖이며,
`removeComponent` 도 얼어 있으면 미룬다. **재현 경로가 없어 무는 테스트를 쓰지 못했다** — 그
점은 분명히 해 둔다. 그래도 닿았을 때의 모습이 "멈춘다" 인 것을 남겨 둘 이유가 없어서,
미루지 않는 `detachFromParentImmediate()` 를 갈라 소멸자가 그쪽을 쓰게 했다.

> `~GameObject` 도 `detachFromParent()` 로 같은 지연 경로에 들어가지만 그쪽은 루프가 아니라
> 멈추지는 않고, 뒤이은 `clearComponents()` 가 `~SceneComponent` 를 태워 결국 끊긴다.

### 2026-09-19 (천천히 움직이는 물체는 영원히 제자리에 있었다)

`SceneComponent` 의 세 setter 가 "정말 바뀌었나" 를 이렇게 물었다:

```cpp
if ( float3::getDistanceSquared( _localPosition, pos ) <= MathUtil::Epsilon )
    return;
```

**제곱 거리를 제곱하지 않은 허용치와 재고 있다.** `Epsilon` 이 `1e-6` 이므로 실제 거리로는
`1e-3` 까지가 "안 움직였다" 로 삼켜진다 — 의도한 부동소수 허용치보다 **1000배 크다.**

그것만이면 1mm 짜리 오차로 끝났을 텐데, 비교 기준이 **매번 현재 값**이라 그 아래 움직임은
**쌓이지도 않는다.** 한 프레임에 `1e-3` 보다 조금씩 가는 물체는 몇 초를 가도 한 번도 움직이지
않는다. 165Hz 에서 그 경계는 **0.165 유닛/초** — 천천히 도는 포탑, 흘러가는 구름, 부드럽게
따라붙는 카메라 암처럼 게임에 흔한 속도다.

테스트로 재 보니 프레임당 `5e-4` 로 200프레임(약 1.2초) 움직인 물체의 최종 좌표가 **정확히
0** 이었다. 0.1 만큼 가 있어야 했다.

`MathUtil::EpsilonSquared` 를 두고 세 자리를 그것으로 바꿨다. 이름을 따로 둔 이유는 제곱을
잊는 그 한 걸음이 눈에 안 보이기 때문이다.

**바꾸지 않은 것 — 정규화 직전의 퇴화 벡터 검사.** `getLengthSquared() < Epsilon` 이 저장소에
열몇 군데 있는데(`MatrixMath` · `MeshUtil` · 라이트 컴포넌트들), 그쪽은 "같은가" 가 아니라
"0 으로 나눌 만큼 짧은가" 를 묻는 것이라 넉넉한 쪽이 오히려 맞다. `EpsilonSquared` 의
`@note` 에 그렇게 적어 두었다.

**`GpuSceneBuilder::bCamSame` 도 같은 모양이지만 성격이 다르다.** `_lastCameraPos` 는 리빌드가
실제로 일어날 때만 갱신되므로(399행) 차이가 **쌓인다** — 카메라가 `1e-3` 만큼 움직이면 그때
리빌드된다. 얼어붙는 것이 아니라 최대 `1e-3` 만큼 늦는 이력(hysteresis)이고, 그 자리는 정지한
씬의 비용을 0 으로 만드는 최적화라 숫자 없이 건드리지 않았다.

### 2026-09-19 (SceneComponent 의 이동 연산은 계층을 부순 채로 놓여 있었다)

`SceneComponent` 는 **자기 주소로 얽혀 있는 계층의 노드**다 — 자식들의 `_pParent`, 부모의
`_listChild` 항목, 매니저의 `_listRootSceneComponent` 가 전부 이 객체의 주소를 들고 있다.
그런데 이동 연산은 그중 **하나도** 고치지 않았다. 옮기고 나면 자식들은 사라진 객체를 부모로
가리키고, 매니저의 루트 등록부는 죽은 포인터를 훑게 된다.

이동 대입은 한 걸음 더 나갔다:

```cpp
_listChild = std::move( other._listChild );   // 받아 오고
...
_listChild.clear();                            // 그 자리에서 비운다
```

이동 생성자에는 없는 줄이다. 둘 중 하나는 틀렸다는 뜻인데, 실제로는 둘 다 틀렸다.

**고치는 대신 막았다.** 컴포넌트는 풀에서 제자리 생성·소멸하므로 옮겨질 일이 없다 —
`= delete` 로 바꾸고 빌드해 보니 **저장소 전체에서 그 두 정의 말고는 아무것도 깨지지 않았다.**
아무도 쓰지 않는 코드가 세 가지를 잘못하고 있었던 것이다. 파생 8종
(`Mesh` · `Camera` · `Sprite` · `SpriteAnimator` · `BoxCollider2D` · `Directional/Point/SpotLight`)의
`= default` 선언도 같이 걷었다.

**덤:** `PrefabManager::spawn` 이 `pAsset` · `pGameObject` · `pTypeInfo` · `pInstanceName` 은 전부
검사하면서 `pGameObjectManager` 만 그냥 역참조하고 있었다. 지금 호출부는 셋 다 널을 막아 주지만
(`EditorUtil` 은 명시적으로, `Scene` 은 소유로) 활성 씬이 없을 때 `getObjectManager()` 는 널을
준다 — 나머지와 같은 모양으로 맞췄다.

### 2026-09-19 (같은 오브젝트를 둘이 없애면 풀이 같은 블록을 두 번 받았다)

`destroyObject` 가 이렇게 생겼다:

```cpp
if ( pObj != nullptr && pObj->isPendingKill() == false )   // 본다
{
    pObj->markPendingKill();                               // 그리고 세운다
    ...
    _listPendingDestroyObject.push_back( pObj );            // 목록에 넣는다
}
```

플래그는 원자적인데 **보고 나서 세우는 두 걸음이 원자적이지 않다.** 두 스레드가 그 사이를
나란히 통과하면 같은 포인터가 파괴 목록에 두 번 들어가고, `_poolGameObject.destroy` 가 같은
블록을 두 번 반납해 자유 목록이 망가진다.

**이것은 흔한 경로다.** `onTick` 은 병렬로 돈다(`dispatchWave` 가 16개부터 `emplaceParallel`).
같은 오브젝트의 컴포넌트끼리는 `splitWaveByObject` 가 갈라 주지만, **서로 다른** 오브젝트의
컴포넌트 둘이 같은 대상을 없애는 것은 못 막는다 — 총알 둘이 같은 프레임에 같은 적을 맞히면
정확히 그 모양이다.

고친 방식은 **자리를 원자적으로 잡는 것**이다(`tryMarkPendingKill` = `exchange`). `true` 를 받은
스레드 하나만 목록에 넣는다. `destroyComponent` 도 같은 모양이라 같이 고쳤다.

**검증.** `GameObjectManagerPoolTest.ConcurrentDestroyDestroysTheObjectOnlyOnce` — 8 스레드가
같은 32개를 24라운드 동안 동시에 없애고, 뒤에 새로 만든 32개의 주소가 전부 다른지 본다
(자유 목록이 같은 블록을 두 번 받았으면 같은 주소가 두 번 나온다). **되돌리면 5/5 세그폴트**,
고친 뒤 5/5 통과. 레이스지만 재현율이 100% 라 한 번으로 끝나지 않고 다섯 번 재 봤다.

### 2026-09-19 (수명이 다한 총알·데미지 숫자·이펙트가 풀로 돌아오지 않았다)

위 레이스를 고치면서 `markPendingKill` 호출부를 전부 훑다가 나왔다. **세 곳이
`destroyObject` 를 거치지 않고 오브젝트를 직접 표시하고 있었다:**

- `EffectBaseComponent::onTick` — 이펙트 알파가 0 이 됐을 때
- `ProjectileComponent::onTick` — 총알 수명이 끝났을 때
- `DamageUIComponent::onTick` — 데미지 숫자가 다 페이드됐을 때

`markPendingKill()` 은 **무덤 표시일 뿐**이다. 파괴 목록에 넣는 것은 `destroyObject` 이므로,
표시만 한 오브젝트는 틱·조회·렌더에서는 빠지지만 `_listGameObject` 에 **영원히 남고** 풀로
돌아오지 않는다. 하필 그 셋이 게임에서 가장 자주 났다 사라지는 것들이라, 플레이가 길어질수록
매 프레임 훑는 오브젝트 수가 단조 증가한다.

셋 다 `pOwner->destroy()` 로 바꿨다(그쪽이 `destroyObject` 를 탄다).

**검증.** `GameFrameworkTest.ExpiredEffectObjectReturnsToThePool`. 틱 웨이브 배선이 아니라
"만료가 무엇을 하는가" 가 검사 대상이라 컴포넌트의 `onTick` 을 직접 부른다. 되돌리면
`getAllGameObjects().empty()` 가 진다 — 레이스가 아니라 **결정적**이다.

> 표시와 파괴를 두 이름으로 나눠 둔 것이 함정이었다. `markPendingKill` 은 공개돼 있고 이름만
> 보면 "없앤다" 로 읽힌다. 세 곳 다 그렇게 읽고 쓴 것으로 보인다.

### 2026-09-19 (지연 로드 훅 경로가 이사 간 자리를 가리키고 있었다)

`Source/Engine/CMakeLists.txt` 의 `SW_DELAYLOAD_HOOK_SOURCE` 가
`Utility/Module/DelayLoadNotifyHook.cpp` 를 가리키고 있었다. 그 파일은 `83b6ea60`(Engine 레이어
정리)에서 `Module/` 로 옮겨졌고, **속성만 옛 경로에 남았다.**

아무도 눈치채지 못한 이유는 `sw_addDelayloadHook` 이 이렇게 생겼기 때문이다:

```cmake
if(NOT swHookSrc OR NOT EXISTS "${swHookSrc}")
    set(swHookSrc ".../Source/Engine/Module/DelayLoadNotifyHook.cpp")   # 매번 여기로 떨어졌다
```

**폴백이 매번 대신 고쳐 주고 있었다.** 속성을 두는 이유가 "훅 소스의 위치를 한 곳에서 안다" 인데
그 한 곳이 틀린 채로 굳어 있었고, 빌드는 멀쩡했으므로 신호가 없었다. 속성이 **있는데** 그 파일이
없으면 그것은 설정 실수이므로 이제 `FATAL_ERROR` 로 멈춘다(속성이 아예 없는 경우만 폴백한다).
되돌려 보면 `configure` 가 그 자리에서 진다.

**살펴보고 문제 없던 것.** `Module/DelayLoadNotifyHook.cpp` 가 Engine 글롭에서 빠져 있는 것은
의도한 것이 맞다 — 확인하는 데 시간이 들었으므로 그 이유를 `coreExclude` 위에 적어 두었다.
훅 변수(`__pfnDliNotifyHook2`)는 **모듈마다 따로**라, 엔진 모듈 DLL 을 지연 로드하는 쪽
(kit · SWGame)이 자기 바이너리에 넣어야 뜻이 있다. Engine 이 지연 로드하는 넷은 전부 시스템
DLL(`d3dcompiler_47` · `mfplat` · `mfreadwrite` · `xaudio2_9`)이라 훅이 돌려줄 핸들이 없고,
넣으면 오히려 **정상 로드마다 "못 찾았다" ERROR** 가 남는다. 훅이 붙은 타깃들이 지연 로드하는
것은 `GameFramework.dll` 과 `GF_*.dll` 뿐이고 전부 Bin 에 있으므로, 그 ERROR 경로는 지금
구성에서는 진짜 실패에만 닿는다.

> 훅이 Engine 에서 한 번도 불리지 않는다는 것은 임시 프로브(`SW_LOG_WARNING` 을 훅 입구에)로
> 확인했다 — 코드만 읽어서는 "안 불린다" 와 "불리는데 로그가 안 남는다" 를 가를 수 없었다.

### 2026-09-19 (구워 낸 `.bin` 언어 파일이 하나도 읽히지 않았다)

`StringTable` 은 확장자를 보고 `.bin` 이면 바이너리로 읽는다(`loadFromFile`·`loadFromResource` 둘 다).
`LocalizationManager` 는 **그 분기를 따로 한 벌 더 들고 있었고, 그 사본만 `.bin` 을 몰랐다** —
파일을 텍스트로 읽은 뒤 확장자 판별의 기본값인 JSON 파서에 넣었다. 그래서:

- `StringTable::saveToBinaryFile` 이 구워 낸 언어 파일은 **매니저로는 한 개도 읽히지 않았다.**
- `initialize` 는 언어 디렉터리에서 `.bin` 을 **일부러 찾아 준다**(`loadLanguageDirectory( dir, ".bin", true )`).
  그 줄은 처음부터 끝까지 실패하는 일만 했다.

기존 바이너리 테스트가 이것을 지나친 이유가 분명하다 — 전부 `StringTable` 을 **직접** 부르거나
(`StringTableAndLocalizationBinaryCooking`) `loadFromBinaryPack` 을 썼다. 매니저의 파일 경로로
`.bin` 이 들어가 본 적이 없었다. 둘 사이의 구멍이라 양쪽 테스트가 다 초록이었다.

고친 방식은 **분기를 없애는 쪽**이다. `loadLanguageFile`/`loadLanguageResource` 가 직접 읽는 대신
`StringTable::loadFromFile`/`loadFromResource` 에 맡긴다. 확장자를 보고 무엇을 할지 고르는 지식이
한 곳에만 남으므로 다시 갈라질 수 없다. 사라진 사본과 함께 `loadLanguageFromText` 와 파서 헤더
셋(`KeyValueFile`/`JsonDocument`/`XmlDocument`)도 필요 없어졌고, 세 텍스트 로더에 세 번 복사돼
있던 "활성 언어가 비어 있으면 세운다" 블록은 `markLanguageLoaded` 하나로 모았다.

**검증.** `LocalizationManagerTest.BinaryLanguageFileLoadsThroughTheManager`. 되돌리면 5개 단언이
진다(파일 하나 직접 + 디렉터리째, 양쪽 다).

### 2026-09-19 (바이너리 헤더가 적어 낸 항목 개수를 그대로 믿었다)

`StringTable::loadFromBinaryBuffer` 는 파일에서 읽은 `count` 를 검사 없이 `_mapTable.reserve` 에
넘겼다. 루프 안에는 범위 검사가 있지만 **`reserve` 가 그보다 먼저 돈다** — 망가진 헤더 하나면
항목을 한 개도 읽어 보기 전에 죽는다.

숫자로 재 봤다. 개수 칸만 `0xFFFFFFFF` 로 바꾼 40바이트짜리 버퍼를 먹이면 **72.5초**가 걸린다
(고친 뒤에는 1ms 다). `reserve` 는 `_listBucket.assign( count, ... )` 이라 실제로 40억 개의 자리를
요구한다. 파일 하나로 프로세스를 세울 수 있었다는 뜻이다.

항목 하나는 아무리 짧아도 키 해시(8) + 길이(4) = 12바이트이므로 **남은 바이트가 곧 정확한 상한**이다
— 임의의 상한을 고르지 않아도 된다. 같은 자리의 `pPtr + strLen > pEnd` 류도 뺄셈 형태로 바꿨다
(버퍼 끝을 한 칸 넘어선 포인터는 만드는 것 자체가 규약 밖이다. 압축 헤더에서 고친 것과 같은 모양이다).

**검증.** `LocalizationManagerTest.BinaryHeaderEntryCountIsBoundedByTheBuffer`. 되돌리면 두 단언이
지는데, 진 이유가 두 가지다 — 개수가 1000 일 때는 **멀쩡한 첫 항목을 넣고 나서** 다음 항목에서야
실패를 알아차려 `size()` 가 1 이고, `0xFFFFFFFF` 일 때는 위의 72.5초가 그대로 나온다.

### 2026-09-19 (`SW_EXPECT_STREQ` 가 널을 받으면 테스트 바이너리를 통째로 죽였다)

위 `.bin` 테스트를 변이로 돌리다 발견했다. 매크로는 받은 값으로 바로 `sw::string` 을 만들었다:

```cpp
const sw::string _sw_expect_streq_a( actual );   // actual 이 널이면 여기서 죽는다
```

`getStringFromLanguage` 처럼 **널을 돌려줄 수 있는** `const utf8*` 게터를 넣으면 프로세스가 그대로
내려간다. 실패를 찍기도 전에 죽으므로 **그 파일의 뒤쪽 케이스가 통째로 사라지고**, 어느 단언이
문제였는지도 남지 않는다. 실제로 처음 변이 실행에서 단언 하나만 `[FAILED]` 로 찍히고 요약도 없이
끝났다 — 다섯 개가 물어야 할 자리였다.

**널을 돌려주기 시작한 회귀야말로 이 매크로가 가장 잡아야 할 것인데, 정확히 그때 못 잡았다.**
`test::toComparableText` / `test::isNullText` 로 널을 `<null>` 로 찍되 **널 여부는 따로 비교한다**
(그러지 않으면 진짜 `"<null>"` 문자열과 널이 같다고 나온다). 고친 뒤 같은 변이가 5개 단언을
`Expected [환영합니다!], Actual [<null>]` 로 정확히 찍는다.

> 이 저장소에서 "테스트가 안 물었다" 를 몇 번 겪었는데, 여기는 **테스트가 물었는데 그 결과가
> 전달되지 못한** 경우다. 변이 테스트를 할 때 *깨끗하게 실패했는지* 까지 봐야 하는 이유다.

### 2026-09-19 (구조 버퍼 크기 곱셈 — DX12 만 고쳐져 있었다)

`createStructuredBuffer( elementSize, elementCount )` 는 네 백엔드가 각자 곱한다. **DX12 만 64비트로
곱하고 나머지 셋은 uint32 로 곱했다.** 넘치면 조용히 **작은 버퍼**가 만들어지고, 셰이더는 원래
개수만큼 쓰므로 그 밖으로 나간다.

DX12 쪽에는 그 함정이 주석으로 이미 적혀 있었다 — *"64비트로 곱한다 — 예전에는 UINT 로 곱해
Width(UINT64)에 넣었고, 넘치면 조용히 작은 버퍼가 됐다"*. **그 수정이 형제 백엔드로 옮겨지지
않은 것이다.** 이 저장소가 여러 번 겪은 "한 백엔드만 고쳐진" 모양이고, 이번엔 고친 쪽이 이유까지
적어 두었는데도 옮겨지지 않았다.

셋은 하위 API 가 전부 32비트 크기를 받으므로 DX12 처럼 넓힐 수 없다 — **담기지 않으면 만들지
않는다**(로그를 남기고 0). DX12 는 `Width` 가 UINT64 라 그 크기를 실제로 표현할 수 있으므로
거절하지 않아도 된다. 새 케이스가 그 **백엔드별 계약을 그대로** 적는다.

> 테스트를 한 번 잘못 썼다. 처음에는 네 백엔드 모두 0 을 돌려주길 기대했는데, Shipping 빌드에는
> DX12 만 링크돼 있어서 그 하나가 6.4GB 버퍼를 **실제로 만들어** 냈다. DX12 가 틀린 것이 아니라
> **내 기대가 틀린 것**이었다 — 능력이 다른 백엔드에 같은 답을 요구하고 있었다.

**검증.** `RenderPassGpuTest.StructuredBufferRejectsSizeThatOverflows32Bit`(hostgpu). 가드를
빼면 세 백엔드가 모두 진다(Debug 빌드에서 네 백엔드가 다 뜬다 — Shipping 은 DX12 뿐이다).
정적 씬 스크린샷 네 백엔드 모두 `d2c61c1f8e57cf2d` 유지, nogpu 7/7 × 3 프리셋, hostgpu 2/2.

**덤:** `D3D12RHIResourceBindless` 에서 `CreateShaderResourceView( ..., offlineHandle )` 가 연속으로
**두 번** 불리고 있었다(복사-붙여넣기). 결과는 같지만 읽는 사람에게는 "둘이 달라야 하는데 잘못
적은 것" 으로 보인다. 저장소 전체를 같은 패턴(연속된 동일 호출)으로 훑었고 이 한 곳뿐이었다.

**살펴보고 문제 없던 것 (DX12):** bindless 인덱스 공간이 SRV·CBV·UAV·텍스처UAV 모두 하나로
통일돼 있고(`acquireBindlessIndex`), 인덱스 반납은 GPU 펜스 뒤로 미루며, 슬롯 테이블은
`static_assert( kOnlineBlockDescriptorCount >= kMaxSlotTableSize )` 로 관계를 컴파일 타임에
못박아 두었다. 슬롯 쓰기는 전부 `slot >= kXxxSlotCount` 로 막는다.

### 2026-09-19 (Engine 훑기 — Graphics/Shader · Renderer · RHI 공유 계층: 발견 없음)

고칠 것이 나오지 않은 구간도 **무엇을 확인했는지** 남긴다 — 다음에 같은 자리를 다시 파지 않도록.

**Shader (5.8k).** `ShaderReflectionSpirv` 는 헤더 20바이트를 먼저 보고, 명령어마다
`offset + instrWords <= wordCount` 로 자르고, **분기마다 `instrWords >= N` 을 다시 본다** — 낡거나
깨진 바이너리가 와도 워드 하나 넘어가지 않는다. `ShaderCache` 는 퍼뮤테이션 해시를 파일 이름에,
유효 소스 해시를 **경로**에 넣어 스테일 판정을 시간이 아니라 내용으로 한다(예전 버그가 주석에
그대로 남아 있다). `ShaderBakeStamp` 는 autocrlf 가 붙인 `\r` 까지 떼어 낸다.
`ShaderCompiler` 의 DXBC 경로는 윈도우 밖에서 **이유가 드러나는 메시지**로 먼저 끝낸다.
`ShaderBindingContract` 는 값을 바꾸지 않는 검증자다.

**Renderer (10.6k).** `RenderThread` 는 패킷을 **실행한 뒤에** `_tail` 을 올린다 — 그래서
`waitIdle()` 의 "큐가 비었다" 가 "프레임이 끝났다" 와 같은 뜻이 된다(그러지 않으면 리사이즈가
in-flight 프레임과 겹친다). 링버퍼는 생산·소비가 다른 슬롯을 만지는 것이 `nextHead != _tail`
대기로 보장된다. `RenderGraph` 는 Kahn 위상 정렬을 웨이브 단위로 돌리고 사이클을 검출한다.
드로우 루프는 `groupEnd < batchCount` 로 병합 구간을 자른다.

> `GpuSceneBuilder` 의 배치 정렬은 **포인터 값**으로 비교한다. 표준상 관련 없는 포인터의 `<` 는
> unspecified 지만(총 순서는 `std::less` 가 보장한다) 평탄한 주소 공간에서는 일관되고, 앞 키가
> `permutationHash` 라 그림에는 영향이 없다 — 정적 씬 해시가 실행마다 같은 것이 그 증거다.

**RHI 공유 계층.** `RHIReleaseQueue` 는 콜백을 잠금 **밖에서** 부른다(해제 콜백이 또 enqueue 해도
재진입 교착이 없다). `RHIConstantBufferSlot` 은 만들 때와 갱신할 때 크기가 어긋날 수 있는
모양이지만, 호출부 다섯 곳이 전부 같은 `sizeof(T)` 를 쓴다 — 헤더가 "용량이 프레임마다 변하지
않는다" 고 이미 못박아 둔 그대로다.

### 2026-09-19 (상수버퍼를 만들 때보다 큰 크기로 갱신할 수 있었다)

`IRHIResource::updateConstantBuffer( 버퍼, 데이터, 크기 )` 에는 **적혀 있지 않은 전제**가 있었다 —
그 크기는 `createConstantBuffer` 에 준 크기를 넘으면 안 된다. 그런데 **네 백엔드 중 셋
(DX12 · Vulkan · DX11)은 그것을 검사하지 않고 받은 크기를 그대로 복사한다.** 넘기면 프레임 슬롯
밖(또는 버퍼 밖)까지 쓴다. GL 만 `glBufferSubData` 가 `GL_INVALID_VALUE` 로 막아 준다 —
**한 백엔드에서만 조용히 안전했다.**

`MaterialInstance::updateRhi` 가 그 전제를 어길 수 있었다. 부모 머티리얼의 상수버퍼는 **셰이더를
다시 구우면 커진다**(레이아웃이 바뀐다). 그런데 `_constant._buffer` 가 0 이 아니면 그대로 쓰고
새 크기로 갱신했다. 라이브 셰이더 편집 + 인스턴스 파라미터 변경이 겹치면 그 자리를 밟는다.

만들 때의 크기(`_constantByteSize`)를 들고 있다가 **커졌으면 버리고 다시 만든다.**
전제 자체도 `IRHIResource.h` 에 적었다 — 다음 호출자가 다시 밟지 않도록.

**검증이 까다로운 종류다.** 넘치는 곳이 GPU 매핑 메모리라 ASan 도 단언도 잡지 못한다. 대신
**버퍼를 다시 만들었는지**로 가른다 — 다시 만들면 bindless 디스크립터 인덱스가 새로 발급된다.
`RenderPassGpuTest.InstanceConstantBufferIsRecreatedWhenLayoutGrows`(hostgpu, 네 백엔드)가
실제 에셋으로 그 순서를 재현하고, 재생성을 빼면 "상수버퍼를 다시 만들지 않고 더 큰 크기로
갱신했습니다" 로 진다.

Debug·Release·Shipping·ASAN 경고 0, nogpu 7/7 × 3 프리셋, hostgpu 2/2.

### 2026-09-19 (forgetRhi 와 releaseRhi 가 서로 다른 상태를 남겼다)

둘 다 **"디바이스가 사라졌다"** 는 통보인데 `Material` 에서 남기는 상태가 달랐다 —
`releaseRhi` 만 빌린 텍스처 목록(`_listAcquiredTexturePath` · `_listMaterialTextureSrv`)을
비웠고 `forgetRhi` 는 핸들만 비웠다.

그래서 forget 뒤에 `initRhi` 가 오면 `resolveTextureAssets` 가 목록에 **덧붙인다.** 그러면
`ordinal`(= `_listMaterialTextureSrv.size()`)이 0 이 아닌 값에서 시작하는데, 네이티브 bindless 가
없는 백엔드(**DX11 · GL**)는 그 서수를 **t5..t8 고정 슬롯 번호**로 쓴다 — 엉뚱한 텍스처를 읽거나
한도(`kMaterialTextureCount`)를 넘어 흰색으로 남는다. "백엔드를 바꾸면 화면이 이상해진다" 로만
보이는 종류다.

`forgetRhi` 도 `releaseTextureAssets( nullptr )` 로 놓게 했다. 널 디바이스면 `TextureCache` 가
참조만 돌려주고 GPU 호출은 하지 않는다 — 디바이스가 이미 없으므로 그것이 맞다.

**왜 평소에 안 보였나.** `forgetRhi` 는 `~IRHIDevice()` 의 **안전망** 경로에서만 온다
(shutdown 을 거치지 않고 사라지는 디바이스 — 초기화 실패 등). 정상 백엔드 교체는
`shutdown()` → `releaseAllFor` 라서 이 자리를 지나지 않는다.

**검증.** 디바이스가 필요하므로 `RenderPassGpuTest`(hostgpu)에 넣었다 — CLAUDE.md 의 규칙이다.
`ForgetThenInitDoesNotDoubleMaterialTextureOrdinals` 는 실제 에셋(`benchtextured.material`,
albedoMap 이 붙어 있다)으로 네 백엔드를 돌며 forget → init 뒤 서수가 누적되지 않는지 본다.
수정을 빼면 "forgetRhi 가 빌린 텍스처 목록을 남겼습니다" 와 "다시 올린 뒤 텍스처 서수가
누적됐습니다" 둘 다 진다. hostgpu 2/2, nogpu 7/7 × 3 프리셋, 정적 씬 스크린샷
dx12·dx11·gl 모두 `d2c61c1f8e57cf2d` 유지.

### 2026-09-19 (머티리얼 패킹이 옆 프로퍼티를 덮을 수 있었다)

상수버퍼에서 **칸 크기는 셰이더 리플렉션**이 정하고(`ShaderVariableInfo::_size`) **쓰는 크기는
머티리얼 XML** 의 `shaderType` 이 정한다 — 둘이 어긋날 수 있다.
`Material::syncPropertiesFromReflection` 이 대부분 재매핑으로 맞춰 주지만, **고칠 수 없는
조합**에서는 경고만 남기고(`bAllPacked = false`) `shaderType` 을 그대로 둔다.

`MaterialUtil::packPropertyIntoBuffer` 의 `writeNumericValue` 는 처음부터 `packSize < need` 를
보고 있었는데, **같은 switch 안에서 직접 `Memory::copy` 하던 형제 경로 여섯**
(Bool · Enum · BitFlag · ChannelMask · Texture · Range)은 그 검사를 건너뛰었다. 그래서 예를 들어
5바이트 칸에 `ChannelMask` + `shaderType="Float4"` 가 오면 **16바이트를 썼다.**

**증상은 크래시가 아니라 "엉뚱한 색" 이다.** 넘친 바이트가 상수버퍼 **안**의 다음 프로퍼티
자리로 들어가기 때문이다 — 메모리 오류로는 안 잡히고 화면에서만 보인다. 이 저장소가 머티리얼에서
여러 번 겪은 모양이다.

여섯 경로를 `writeBoundedValue` 하나로 모아 칸을 넘으면 쓰지 않고 false 를 돌려주게 했다.

**테스트를 무는 것으로 만드는 데 두 번 실패했다 — 그 과정이 이 항목의 요점이다.**

1. `MaterialUtil::packPropertyIntoBuffer` 를 직접 부르려 했다 → **링크 실패.**
   `MaterialUtil.h` 는 머리말에 "Engine TU 전용" 이라고 적혀 있고 실제로 export 하지 않는다.
   테스트를 위해 `SW_API` 를 붙이는 것은 그 의도를 뒤집는 일이라 공개 경로
   (`loadFromXml` + `syncPropertiesFromReflection`)로 돌아갔다.
2. "ASan 이 잡겠지" 로 썼다 → **수정을 빼고도 3/3 통과.**
   `sw::vector` 의 capacity 가 이미 16이라(앞선 `rebuildPackedBuffer` 가 그렇게 잡았다) 16바이트
   쓰기가 **할당 안**에 들어간다. 논리적으로는 `size()` 를 넘었지만 메모리 오류는 아니다.
   즉 **이건 메모리 안전 문제가 아니라 데이터 오염 문제**이고, 검사도 거기에 맞춰야 했다.

최종 케이스 `PackingDoesNotClobberTheNextPropertySlot` 은 `_tint` 를 **먼저** 적어 뒤의 `_mask` 가
덮는지를 본다(패킹은 목록 순서대로 돈다). 수정 전에는 "옆 프로퍼티(_tint)의 값이 _mask 의
16바이트 쓰기에 덮였습니다" 로 지고, 수정 후에는 통과한다 — **ASan 없이 결정적으로** 문다.

**검증.** 정적 씬 스크린샷이 dx12·vk·dx11·gl 네 백엔드 모두 `d2c61c1f8e57cf2d` 유지,
MaterialTest 16/16, nogpu 7/7 × 3 프리셋.

### 2026-09-19 (Engine 훑기 — Common · Compression · Config · Dialogue · Graphics 앞부분)

고칠 것은 나오지 않았고, **말로 적혀 있지 않던 계약 두 개**를 적었다. 둘 다 "지금은 맞지만
누가 한 줄만 옮기면 조용히 새는" 자리다.

**`TextureCache::clear()` · `MaterialCache::clear()` 는 GPU 자원을 돌려주지 않는다.**
`IAssetCache::clear()` 는 디바이스를 인자로 받지 않고(그 이유는 `MaterialCache.h` 머리말에 있다 —
캐시가 디바이스를 들고 있으면 백엔드 교체 때 죽은 포인터가 된다) 캐시도 들고 있지 않으므로
`releaseRhi` 를 부를 방법이 **없다.** 살아 있는 디바이스에서 부르면 텍스처 핸들과 **bindless SRV
인덱스**가 샌다 — 후자는 프리리스트로 영영 안 돌아온다.

지금 안전한 이유는 **순서 하나뿐**이다: `EngineLoop::shutdown` 이 `_rhi->shutdown()` 을 먼저
불러 `RHIRenderResource` 등록부 전체에 `releaseRhi` 를 밀어 둔 뒤에야
`ResourceManager::shutdown` → `clearAssetCaches()` 가 여기에 닿는다. 그 의존이 코드 어디에도
적혀 있지 않아 두 `clear()` 에 계약으로 적었다.

**`Texture2D::loadFromResource` 가 실패하고도 `_pDevice` 를 남겼다.** `registerBindlessTexture`
실패 검사보다 **먼저** 대입하고 있었다. 지금은 무해하지만(`isRhiValid()` 가 핸들을 본다)
`releaseRhi` 는 그 값으로 "남의 디바이스 통보인지" 를 가른다 — 가진 것이 없는데 주인만 적혀 있는
상태를 애초에 만들지 않도록 성공한 뒤에만 적게 옮겼다.

**살펴보고 문제 없던 것:** `EngineServices`(널 참조 반환은 계약이고 `areEngineServicesBound()` 와
`CheckNullableServiceUse` 게이트가 그 짝이다), LZ4·zlib·zstd 코덱 셋(전부 `_safe` 변형과 타입
한계 검사를 갖췄다 — zlib 의 `uLong` 이 윈도우에서 32비트라는 것까지 주석에 있다),
`ConfigManager`(설정 타입별로 시작 때 한 번만 읽으므로 반환한 포인터가 무효화될 일이 없다),
`DialogueGraphAsset`(핀 인코딩이 한 파일로 모였고 `*10` 레거시도 헤더에 적혀 있다),
`GpuUploadQueue`, `Mesh`(`releaseRhi`·`forgetRhi`·소멸자 셋의 역할이 갈려 있다),
`MeshUtil`(입력을 클램프하고 극의 퇴화 삼각형을 거른다), `Texture2D` 업로드
(DX12 는 스테이징에 **동기 복사** 후 제출하므로 지역 `DdsImageData` 가 죽어도 안전하다).

**검증.** 정적 씬 스크린샷 `d2c61c1f8e57cf2d` 유지, Debug·Shipping 경고 0, nogpu 7/7.

### 2026-09-19 (Engine 훑기 — Animation · Audio)

사용자가 **"비현실적이라도 해"** 라고 했으므로 패턴 검사로 대체하지 않고 Engine 도 파일을
하나씩 읽는다. Core 와 같이 알파벳 순.

**오디오를 내리는 순간 워커가 이미 해제된 XAudio2 로 보이스를 만들고 있었다 (메모리 안전).**
`XAudio2System::shutdown()` 만 `_voiceMutex` 를 잡지 않고 보이스 목록을 훑었다 — 그 뮤텍스의
주석이 처음부터 **"보이스·`_musicPath`·`_musicGeneration` 을 지킵니다"** 라고 적고 있었는데
여기만 어긴 것이다. 그리고 `EngineLoop::shutdown` 은 오디오를 **TaskManager 보다 먼저** 내린다
(419행 vs 431행) — 즉 shutdown 이 도는 동안 워커는 아직 `playDecodedClipTask` 안에 있다. 결과:

1. 워커의 `push_back` 이 `_listActiveVoice` 를 재할당하면 shutdown 의 순회 참조가 **해제된
   메모리**를 가리킨다.
2. 잠금을 늦게 얻은 워커가 이미 `Release()` 한 `_pXAudio` 로 `CreateSourceVoice` 를 부른다.

shutdown 이 잠그고 부수게 했고, `_bInitialized` 를 **잠금 안에서 먼저** 내려 기다리던 워커가
그것을 보고 돌아가게 했다. 워커도 잠근 뒤에 다시 확인한다(첫 줄의 검사는 잠금 밖이라 못 믿는다).

**왜 여태 안 보였나 — 기존 테스트가 정확히 이 구간을 비껴갔다.**
`MultithreadedAudioDecodeAndPlayback` 은 `waitAll()` 을 **먼저** 부르고 내린다. 실제 엔진은
그러지 않는다. 새 케이스 `ShutdownWhileDecodeTasksAreStillInFlight` 는 일부러 안 기다린다.

**테스트를 무는 것으로 만드는 데 한 번 실패했다.** 처음에는 같은 WAV 하나를 24번 재생했는데,
클립 캐시 덕에 두 번째부터는 즉시 끝나 워커가 잠금 구간에 들어가기도 전에 shutdown 이 지나갔다 —
**수정을 빼고도 5/5 통과했다.** 파일을 매번 다르게 하고(캐시 우회) 크기를 키워 디코드를 실제로
느리게 만들자 **5/5 재현**된다(ASan: `XAudio2System.cpp:648` 에서 access-violation).
수정을 되돌리면 5/5 죽고, 넣으면 5/5 통과한다.

> 교훈 하나 더: "테스트를 썼다" 와 "그 테스트가 문다" 는 다르다. 변이를 넣어 **실제로 지는지**
> 보지 않았으면 이 케이스는 거짓 안심만 남기고 끝났을 것이다.

**살펴보고 문제 없던 것 (Animation):** `AnimClip`(헤더가 스텁이라고 명시한다 — `sample()` 이
항등을 주는 것은 의도다), `AnimPlayer`(`setSpeed` 가 음수를 0 으로 막아 크로스페이드가 멈추는
경우가 없다), `Skeleton`(`addBone` 이 부모-자식 순서를 입구에서 막고, `updateCharacterSpaceTransforms`
는 assert 말고 **진짜 if 가드**도 함께 둔다), `DualQuaternion`, `BlendSpace`(1D·2D 모두 빈 목록을
입구에서 막고, 거리 0 은 조기 반환으로 나눗셈을 피한다), `AnimationGraphAsset`,
`AnimationGraphPlayer`.

**살펴보고 문제 없던 것 (Audio):** `update()` 의 swap-and-pop, 유휴 보이스 재사용(포맷 일치를
확인한다), `playMusic` 이 **멈추기 전에** 리소스 존재를 보는 것, 세대 번호로 늦게 온 디코드를
거르는 것 — 전부 주석에 이유까지 적혀 있다.

**덤:** `D3D11RHICommandContext::endRenderPass()` 가 네 백엔드 중 유일하게 비어 있어 "빠뜨린 것"
으로 읽히기 쉬워, **왜 비어 있는 것이 맞는지**를 주석으로 적었다(즉시 모드라 패스 객체가 없고,
`D3D11RecordingState` 가 든 것은 패스 경계를 넘어 유지되어야 한다).

### 2026-09-19 (Core 마감 + Win32 문자열 변환 두 곳)

**Core 18개 폴더를 다 봤다.** 남은 여섯(Log · Math · Process · Task · Time · Uuid)에서는 고칠
것이 나오지 않았고, 대신 할당기 바닥에서 하나가 더 나왔다.

**`sw::Allocator<T>::allocate( n )` 이 `n * sizeof( T )` 를 검사 없이 곱했다.** 그 곱이 뒤집히면
**요청보다 훨씬 작은 블록**이 잡히고, 호출부는 원소 n 개를 쓸 수 있다고 믿고 그 밖으로 나간다.
`vector::max_size()` 가 이미 그 한계(`SIZE_MAX / sizeof(T)`)를 말하고 있었는데 **아무도 강제하지
않았다** — 표준 할당기가 같은 자리에서 던지는 이유가 이것이다. 이제 던진다.
(`MemoryTest.AllocatorRejectsElementCountThatOverflows`, 가드를 빼면 두 단언이 진다.)

**`D3D11RHICommandContext::beginEventMarker` 가 배열 밖을 읽을 수 있었다.** 이름을
`utf16 wide[256]` 에 `MultiByteToWideChar` 로 직접 옮겼는데, 그 API 는 이름이 버퍼보다 길면
**0 을 돌려주고 널 종단을 보장하지 않는다.** 그대로 `BeginEvent` 에 넘기면 널을 찾아 배열
밖까지 읽는다.

> 처음에는 고정 버퍼를 둔 채 "잘라서라도 담는" 쪽으로 고쳤는데, 사용자가 **`StringUtil` 을 쓰면
> 되지 않나** 고 지적했다. 맞다 — `StringUtil::utf8ToUtf16` 은 길이 상한이 없으므로 이 종류가
> 통째로 사라진다. 잘라 담을 일도, UTF-8 다중바이트 시퀀스가 중간에서 끊길 일도 없다(내 첫
> 수정은 소스 바이트 수로 잘라서 그 위험이 남아 있었다). 마커는 그래픽스 디버거가 붙었을 때만
> 동작하므로 할당 한 번을 아낄 이유도 없었다.

**IME 조합 문자열 버퍼의 상한이 우연에 기대고 있었다.** `ImmGetCompositionStringW` 가 돌려주는
것은 **바이트 수**인데 버퍼는 와이드 문자 배열이고, 검사 상수는 `kMaxBuffer512`(바이트),
버퍼는 `fixed_wstring<kMaxBuffer256>`(문자)였다. 지금은 맞지만 **둘 중 하나만 고치면 조용히
넘친다.** 상한을 버퍼 길이에서 직접 계산하게 했다.

**검증.** 정적 씬 스크린샷이 dx11 을 포함한 **네 백엔드 모두** 기존 해시 `d2c61c1f8e57cf2d` 와
일치한다. Debug·Release·Shipping·ASAN 경고 0, nogpu 7/7 × 3 프리셋, hostgpu 2/2, lint 19/19.

**남은 Core 여섯에서 살펴보고 문제 없던 것:** `Logger`(디스패치가 락 밖에서 생포인터를 쓰지만
`shutdown` 이 워커를 먼저 join 하고 `_listOutput` 은 비우지 않으므로 성립한다 / `LogRecord`·
`LogEntry` 가 전부 소유하는 string 이라 큐를 건너도 안전하다 / 큐가 가득 차면 동기로 쓴다),
`MathUtil::align`·`isPowerOfTwo`, `TaskManager` 의 참조 계수와 슬랩, `CpuTimer`(생성자가 중지
상태로 시작하는 이유까지 이미 적혀 있다), `Uuid`(`tryParse` 가 하이픈 위치·개수를 다 거른다),
`CrashContext`(할당도 stdio 도 쓰지 않는 크래시 경로).

**Engine 훑기 시작 — 패턴 검사로 먼저 걸렀다.** 첨자 안 뺄셈 0건, 역방향 `>=` 루프 0건,
assert-only 가드 0건(Engine 전체에 assert 가 11개뿐이다), `front()`/`back()` 후보 37건은
**전부 오탐**이었다(가드가 같은 줄이나 함수 앞머리에 있었다 — `forEachContentLine` 이 빈 줄을
거르고, `BlendSpace`·`RenderGraph`·`XmlDocument` 모두 앞에서 막고 있다).

### 2026-09-19 (널 바이트를 게이트가 맡는다 — 린트 19개)

위 항목(주석에 박힌 널 바이트)은 "실수했는데 못 봤다" 가 아니라 **"봐도 안 보이는"** 종류다 —
컴파일은 통과하고, 정작 그것을 발견할 통로인 `git diff` 와 `grep` 이 그 파일에 대해 입을 다문다.
이 저장소는 heredoc 이 이스케이프를 먹는 함정에 이미 여러 번 걸렸으므로 게이트로 옮겼다.

`Scripts/lint/gate/CheckTextFilesAreText.py` — 소스·문서·스크립트 확장자 파일에 0x00 이 한
바이트라도 있으면 막는다. 진짜 바이너리는 확장자로 걸러 내므로 걸리지 않는다. 파일 하나를
떨어뜨린 것뿐인데 CMake 등록도, 커밋 훅 등록도, 자기 검사도 전부 알아서 붙었다
(린트 18 → **19**, `CheckLintsAreAlive` 15 → **16 게이트 / 25 케이스**).

**만들면서 두 번 잡혔다 — 둘 다 이 저장소의 게이트에게.**

1. 이 항목을 백로그에 적는 heredoc 이 **똑같이** `'\0'` 을 널 바이트로 바꿔 놓았고, 방금 만든
   게이트가 그것을 첫 실행에서 잡았다. 함정이 실재한다는 것을 그 자리에서 보여 준 셈이다.
2. 게이트 코드 자신이 f-string **식** 안에 백슬래시(널 바이트 리터럴)를 넣어
   `CheckPythonMinimumVersion` 에 걸렸다 — 그대로 뒀으면 리눅스 CI 가 configure 에서 멈췄을 것이다.

### 2026-09-19 (소스 파일 하나가 git 에게 바이너리였다)

`Source/Core/String/StringBuilder.h` 주석 안에 **널 바이트 하나가 실제로 박혀 있었다.**
쓰려던 것은 `[0] = '\0'` 이라는 두 글자 이스케이프인데, 어느 세션의 heredoc 이 그것을 **진짜
0 바이트로** 바꿔 놓았다(AGENTS 가 경고하는 그 함정이다). 결과:

- `git diff` · `git log -p` 가 이 파일을 **`Bin 12872 -> 14252 bytes`** 로만 보여 준다 —
  리뷰에서 무엇이 바뀌었는지 볼 수 없다.
- `grep` 이 `Binary file ... matches` 만 찍고 줄을 안 보여 준다.
- 줄 끝 정규화(`core.autocrlf`)가 건너뛴다.

이스케이프로 되돌렸다. 저장소 전체를 다시 훑었고(빌드·.git·ThirdParty·Tools 제외, 소스·문서·
스크립트 확장자 전부) **널 바이트가 든 텍스트 파일은 이 하나뿐이었다.**

> 다음 커밋부터 이 파일의 diff 가 정상으로 보인다. 이번 커밋의 diff 는 여전히 `Bin` 인데,
> 비교 대상인 옛 blob 에 널이 남아 있기 때문이다.

### 2026-09-19 (Source 함수 단위 점검 — Core/File · Core/String)

**깊은 경로에 설치하면 엔진이 조용히 아무것도 못 찾는다.** `FileUtil::getExecutablePath` 의
윈도우 경로가 260자 고정 버퍼에 `GetModuleFileNameW` 를 부르고 **반환값을 보지 않았다.** 그 API 는
버퍼가 모자라면 잘라서 돌려주고 그 사실을 반환값(= 버퍼 크기)으로만 알린다. 잘린 경로의
디렉터리를 기준으로 찾는 것이 모듈 DLL · RHI 백엔드 · 셰이더 · 리소스 매니저 · 로그 폴더까지
**열 군데**다 — 전부 "없다" 가 되고 원인은 어디에도 안 남는다. 이제 다 담길 때까지 버퍼를 키우고,
윈도우 경로 상한(32767자)에서 멈춘다.

새 케이스 `FileTest.ExecutablePathPointsAtARealFile` 은 돌려준 경로가 **실제로 있는 파일**인지,
절대 경로인지, 두 번 물어도 같은지를 본다. 260자를 넘는 설치 경로를 여기서 만들 수는 없으므로
잘림 자체는 재현하지 못한다 — 대신 잘림 처리를 잘못 넣으면 이 검사가 먼저 진다.

**`StringBuilder::ensureCapacity` 가 할당 실패를 보지 않았다.** `Memory::allocate` 의 결과를 곧장
`Memory::copy` 의 목적지로 넘겼다 — 지면 널에 복사하고, 이어서 `_pBuffer` 가 널인 채 `_capacity`
만 커져서 **그 뒤의 모든 append 가 널에 쓴다.** `appendFormat` 은 한 술 더 떠서, 못 키운 걸 모르고
같은 크기로 다시 찍는 루프를 **영영 돌았다.** 이제 실패를 돌려주고 호출부 열한 곳이 그대로
빠져나간다(버퍼는 손대지 않으므로 지금까지 쌓은 내용이 살아 있다).

> 로그를 남기지 않는 이유는 **로거 자신이 이 클래스를 쓰기 때문이다** — `formatString.h` 가
> 인자 개수 불일치를 stderr 로 직접 찍는 것과 같은 이유다.
>
> 할당 실패 경로에는 **테스트를 붙이지 못했다.** 실패를 주입할 창구가 없다. 잡지 못하는 검사를
> 두느니 여기 적어 둔다 — 고친 것은 "널에 쓰지 않는다" 이고 그것은 코드를 읽어 확인했다.

**덤:** `FileUtil.cpp` 에서 이제 안 쓰는 `fixed_string.h` include 를 지웠다(헤더 462개 전부
여전히 혼자 선다).

**살펴보고 문제 없던 것:** `FileUtil` 의 경로 조작 전부(`splitPath`·`getDirectoryPart`·
`joinPath`·`removeExtension` 등 — `string_view::substr` 범위가 전부 안전하다), `readFile`·
`readTextFile`(뺄셈으로 경계를 보고 짧은 읽기도 처리한다), `fixed_string`(`c_str()` 이 버퍼를
그대로 돌려주고 `size()` 가 strlen 으로 다시 세므로 `data()` 에 외부 API 가 써도 맞는다).

### 2026-09-19 (Source 함수 단위 점검 — Core/Delegate · Core/Event · Core/Memory)

**할당기 셋이 모두 `size + 무언가` 로 크기를 정하고 있었다.** `size` 가 클수록 그 합이 **뒤집혀
작아진다** — 그러면 (1) 요청보다 작은 블록이 잡히고, (2) `오프셋 + size <= 용량` 검사마저 통과해
**블록 밖을 가리키는 주소**가 정상 할당인 척 돌아간다. 쓰는 순간 남의 메모리다. 넷을 고쳤다:

- `Memory::allocate` / `allocateAligned` — 헤더(+정렬 여유) 크기를 더하다 뒤집히면 헤더 쓰기가
  곧바로 범위를 넘는다. Shipping 이 아닌 빌드의 이야기이므로, 정작 개발·테스트 중에만 났다.
- `LinearAllocator::allocate` — 용량 검사를 덧셈에서 **뺄셈**으로 바꿨다(정렬 때문에 alignedOffset
  이 capacity 를 넘어설 수 있어 그것도 함께 본다).
- `FrameArenaAllocator` — 같은 검사를 `fitsInChunk` 하나로 모으고 빠른 경로·느린 경로가 함께 쓴다.

**그 과정에서 더 아픈 것이 나왔다 — `FrameArenaAllocator::allocateNewChunk` 가 할당 실패를 아예
보지 않았다.** `Memory::allocate` 는 `malloc` 결과를 그대로 돌려주므로 실패하면 nullptr 인데,
그것을 `Chunk{ nullptr, ... }` 로 표에 넣고 다음 줄에서 `allocate` 를 다시 불렀다. 그 안의
`chunk._pBuffer + chunk._offset` 은 **널 포인터 산술(UB)** 이고, 결과로 널 근처 주소가 정상 할당인
척 돌아간다. `LinearAllocator` 는 같은 자리를 이미 제대로 보고 있었다 — 한쪽만 빠져 있었다.

**검증.** `MemoryTest.AbsurdSizesReturnNullInsteadOfAWrappedBlock` 하나로 셋을 모두 건드린다.
넘침 가드 넷을 도로 빼면 이 케이스가 프로세스째 죽는다(정상 코드에서는 12/12 통과).

**살펴보고 문제 없던 것:** `MulticastDelegate`(복사·이동과 `_broadcastDepth` 가 이미 꼼꼼히
다뤄져 있다 — 다만 콜백이 예외를 던지면 깊이가 되돌아오지 않는다. 엔진 코드가 던지지 않으므로
그대로 둔다), `Delegate`, `EventDispatcher`, `PoolAllocator`, `LinearAllocator` 의 lock-free CAS
경로와 `advanceToHeldBlock`.

> `FrameArenaAllocator` 는 **아직 테스트에서만 쓰인다**(프로덕션 사용처 없음). 그래도 Core 의
> 공개 API 이므로 고쳤다.

### 2026-09-19 (해시맵 충돌 체인에 검사가 하나도 없었다)

`sw::unordered_map` 은 밀집 배열 + 버킷 체인이고 `erase` 는 **마지막 원소를 지운 자리로 옮긴다**
(swap-and-pop). 그러면 "마지막 원소를 가리키던 체인 링크" 를 새 자리로 고쳐 줘야 하는데,
**그 고치기를 검사하는 테스트가 하나도 없었다** — 기존 테스트는 전부 실제 해시를 쓰므로 충돌
체인이 만들어질지가 운에 달려 있었다.

읽어 본 결과 로직 자체는 맞다. 다만 검사가 없다는 것이 문제라, 해시를 전부 0 으로 만드는
`AlwaysCollideHasher` 로 한 버킷에 몰아넣고 두 가지를 못박았다.

- `HashMapEraseFixesCollisionChainAtEveryPosition` — 체인의 **모든 위치**를 하나씩 지워 보고,
  지우지 않은 키가 전부 남아 있는지 본다.
- `HashMapEraseAllInSeveralOrdersStaysConsistent` — 지우는 순서를 셋으로 바꿔 가며 **끝까지**
  비운다. swap-and-pop 은 매번 배열 끝을 옮기므로 어긋남이 여러 번 지운 뒤에야 드러난다.

변이 둘로 확인했다: 체인 **중간 링크** 고치기를 지우면 두 번째가 "남아 있어야 할 키가
사라졌습니다" 로 지고, **떼어내기(unlink)** 의 중간 분기를 지우면 체인에 고리가 생겨 첫 번째가
`find` 안에서 돌아 나오지 못한다(CTest 타임아웃이 잡는다). 하나씩만으로는 둘 다 못 잡는다 —
첫 번째 검사가 만드는 체인에서는 마지막 밀집 원소가 **언제나 버킷 머리**라서 중간 링크 분기에
닿지 않기 때문이다. 둘 다 남긴 이유가 그것이다.

**살펴보고 문제 없던 것:** `sw::string`·`sw::deque`·`sw::list`(std 위임 + 레이스 스코프),
`unordered_map` 의 `emplace`/`operator[]`/`rehash_internal`, `erase( iterator )` 가 컨테이너 안의
참조를 자신에게 넘기는 것(옮기기 뒤에 그 참조를 다시 읽지 않아 성립한다 — 아슬아슬하지만 맞다).

### 2026-09-19 (Source 함수 단위 점검 — Core/Container 마저)

**`HandleTable` 의 주석이 코드가 주지 않는 보장을 적고 있었다.** "세대를 먼저 올리고 점유 해제를
release 로 발행해, 락 없이 읽는 쪽이 '점유 중' 으로 보는 동안에는 항상 옛 세대와 비교되어
실패한다" — 그런데 점유 여부(`_bOccupied`)와 세대(`_generation`)가 **서로 다른 원자값**이라
`get()` 이 둘을 두 번에 나눠 읽는다. 그 사이에 `erase()` 가 끼면 "점유 중 + 옛 세대" 라는
있어서는 안 되는 조합이 보이고, **비워지는 중인 슬롯 값의 주소**가 그대로 돌아간다.
x86 의 메모리 모델이 창을 거의 닫아 주지만 arm64(이번에 CI 에 넣은 맥 타깃)에서는 아니다.

둘을 한 워드(`_state` = 최상위 점유 비트 | 31비트 세대)로 합쳐 **한 번의 load 가 둘 다 답하게**
했다. 필드가 하나 줄고, 뜨거운 경로(드로우마다 불린다)의 원자 읽기도 둘에서 하나로 준다.
대가는 세대가 31비트가 된 것뿐이다(2^31 번 재활용까지).

**검증이 특히 중요한 변경이다** — 네 RHI 백엔드의 리소스가 전부 이 테이블 위에 있다.
정적 씬 스크린샷이 dx12·vk·dx11·gl **네 백엔드 모두** 기존 해시 `d2c61c1f8e57cf2d` 와 일치했다.
새 케이스 `ForgedGenerationDoesNotAliasTheOccupiedBit` 는 31비트가 된 세대가 점유 비트와
겹치지 않는지를 못박는다(HandleTableTest 6/6, DataStructureTest 24/24).

> Shipping 빌드에는 **DirectX12 만 들어 있다** — `-vk`/`-dx11`/`-gl` 은 "이 빌드에 없습니다" 로
> 진다. 네 백엔드 스크린샷 대조는 Debug 빌드에서 해야 한다. (기존 동작이고 이번 변경과 무관.)

**살펴보고 문제 없던 것:** `sparse_set`, `PagedArray`(발행 순서가 문서대로 맞다), `ObjectHandle`,
`map`(정렬된 vector), `deque`·`list`(std 위임 + 레이스 스코프), `array`, `pair`.
Core 전체를 부호 없는 뺄셈·역방향 루프·덧셈 경계 검사 세 패턴으로 훑었고, 남은 것은 없다
(`vector::insert` 가 유일한 보유자였다).

### 2026-09-19 (Source 함수 단위 점검 — Core/Concurrency · Core/Container)

**`sw::vector::insert( pos, count, value )` 가 두 가지로 범위 밖을 만졌다 (메모리 안전).**
둘 다 ASan 이 확인해 줬다.

1. **`count > _size`** — "옮길 원소인가" 를 `itemIndex - count >= offset` 으로 갈랐는데, 그 뺄셈이
   size_t 로 뒤집혀 조건이 **언제나 참**이 되고 `_pData[2^64-k]` 에서 move 해 왔다.
   `{10}` 에 `insert( begin, 2, 7 )` — 앞에 두 개 끼워 넣기 — 만으로 걸린다.
2. **`count == 0` 이고 `offset == 0`** — 뒤로 미는 루프의 종료 조건이 `itemIndex >= offset + 0`,
   즉 언제나 참이 되어 인덱스가 0 에서 한 번 더 줄어 뒤집혔다. **끝나지 않으면서 범위 밖에 쓴다**
   (`vector.h:819` heap-buffer-overflow).

밀기/채우기를 인덱스 뺄셈이 아니라 "목적지가 살아 있는 칸인가(`toIndex >= _size`)" 로 가르도록
다시 썼다. 겸사겸사 **자기 원소를 넣는 경우**(`v.insert( v.begin(), 3, v[0] )`, 적법한 호출)도
닫았다 — 재할당이 버퍼를 옮기면 `value` 참조가 죽는다(변이 검사에서 heap-use-after-free 로 확인).

**`erase` 둘**은 `SW_ASSERT` 만 믿고 있었다. 그것은 Release 에서 **통째로 사라지므로**
빈 벡터의 `_size - 1` 이 배포본에서만 뒤집힌다. 실제 가드를 넣었다.

**`DynamicBitset::operator&=/|=/^=`** 도 같은 모양이었다 — `SW_LOG_ASSERT` 로 크기 일치를 적어
두고 그대로 짧은 쪽 범위 밖을 읽었다. 이제 크기가 다르면 아무것도 하지 않는다.
(테스트는 붙이지 못했다: Debug 에서는 assert 가 프로세스를 세우고, ASan 프리셋도 Debug 라
그 경로에 못 닿는다. 잡지 못하는 검사는 두지 않는다.)

**`DeadlockDetector::recordLockAcquired` 가 엉뚱한 콜스택을 적었다.** 획득 지점 스택을
`_waitingCallStack` 재사용으로 때웠는데, `try_lock()` 은 `recordLockIntended` 를 거치지 않는다
(기다리지 않으니 사이클 검사도 필요 없다). 그래서 덤프의 "Acquired at" 이 **전혀 다른 락을 잡던
자리**를 가리켰다 — 탐지기의 유일한 쓸모가 거기인데. 기다린 락이 그 락일 때만 재사용한다.

**`SW_ENABLE_DEADLOCK_DETECTION=ON` 은 아무 데서도 빌드되지 않는다** (기본 OFF, CI 에도 없다).
이번에 켜서 확인했다 — 2026-09-19 기준 컴파일되고 CoreTest 218/218 통과하며 오탐도 없다.
그대로 둘지(썩게 두는 것) CI 에 컴파일 검사만 넣을지는 아직 정하지 않았다.

**살펴보고 문제 없던 것:** `ConcurrentQueue`(Vyukov 원본과 한 줄씩 대조), `WorkStealingDeque`
(Chase-Lev), `LockFreeQueue`(SPSC), `LockFreeObjectPool`, `SpinLock`(TTAS), `sw::atomic`,
`EnumUtil`, `Defines.h`. `DataRaceDetector` 의 `%s`·`%p`·`%u` 는 `formatString` 이 실제로 지원한다.

**검증.** 새 케이스 3개, 셋 다 변이 검사로 문다(둘은 ASan 이 직접 잡는다).
Debug·Release·Shipping·ASAN 빌드 경고 0, nogpu 7/7 × 3, hostgpu 2/2, lint 18/18.

### 2026-09-19 (Source 함수 단위 점검 — Core/Common · Core/Compression)

**손상된 스트림이 버퍼 밖을 읽게 할 수 있었다 (메모리 안전).** `CompressionStream::verifyHeader` 의
길이 검사가 `_compressedSize + sizeof(헤더) > dataSize` 라는 **덧셈**이었다. `_compressedSize` 가
UINT64_MAX 면 `+28` 이 27 로 돌아 검사를 통과했고, 그 값이 그대로 코덱의 srcSize 가 되어
`decompress` 가 SIZE_MAX 바이트를 읽으려 들었다. **28바이트짜리 헤더 하나로 힙 밖을 읽힐 수
있었다는 뜻이다** — ASan 이 `RleCompressionCodec.cpp:124` 의 `pInput[readPos++]` 에서
heap-buffer-overflow 로 찍어 확인했다. 뺄셈으로 비교하게 고쳤다(같은 함정을 `BinarySerializer` 는
이미 뺄셈으로 피하고 있었는데 여기엔 안 옮겨져 있었다).

**해제 크기가 곧 할당 크기였다 (가용성).** `_uncompressedSize` 는 검사 없이 `outBytes.resize()` 에
들어갔다 — 2^60 이 적힌 헤더면 코덱이 한 바이트도 읽기 전에 메모리가 터진다. 코덱마다 팽창률이
달라(RLE 65배, Deflate 1000배 초과) 압축 크기로부터 정확한 상한은 못 내므로,
`CompressionStream::kMaxUncompressedSize`(1 GiB) 를 두고 헤더 검증에서 자른다.

**32비트 varint 디코더가 조용히 잘랐다.** `VarIntUtil` 의 주석은 처음부터 "32비트 오버로드는
캐스팅이 아니라 범위 검사를 한다" 고 적혀 있었는데 **코드는 `static_cast` 한 줄이었다.** 그래서
`Archive::readPooledString` 의 `poolId >= getCount()` 검사가 이미 잘린 값을 보게 되어
`0x1'0000'0000 + n` 이 유효한 n 인 척 통과했다. `narrowToUint32`·`narrowToInt32` 로 규칙을 한 곳에
모으고 `VarIntUtil`·`Archive` 네 군데가 그것을 쓴다. 실패하면 **오프셋도 되돌린다.**

**부수로 고친 것 셋:**
- `encodeVarInt64` 의 ZigZag 가 `value << 1` 이었다 — C++17 에서 **음수의 왼쪽 시프트는 UB** 다.
- LEB128 10번째 바이트의 남는 6비트를 조용히 버리고 있었다(서로 다른 바이트열이 같은 값으로
  읽혔다). 정상 인코더는 그 자리에 0/1 만 내므로 막아도 우리가 쓴 스트림은 다치지 않는다.
- `Macros.h` 가 `std::enable_if_t`(SW_REQUIRES · arrayCountHelper)를 쓰면서 `<type_traits>` 를
  선언하지 않고 `Types.h` 의 `<string_view>` 가 우연히 끌어와 주는 것에 기대고 있었다.

**검증.** 새 케이스 4개(`CompressionTest` 2 · `ArchiveTest` 2), 전부 변이 검사로 문다.
Debug·Shipping·ASAN nogpu 7/7, lint 18/18, 경고 0건.

### 2026-09-19 (Source 함수 단위 점검 — Core/CommandLine · Core/GlobalVariable)

`Source/` 전체를 함수 하나씩 읽는 점검을 시작했다(Core → Engine → ReflectionParser → Editor →
GameFramework → Games, Core 안은 폴더 알파벳 순). 첫 두 폴더에서 **조용히 실패하던 것 셋**이 나왔다.

**1) 값을 빠뜨린 `-gv_*` 스위치가 아무 일도 하지 않았다 (진짜 버그).** `ArgumentInfo` 에는
"값을 받아야 하는가" 를 적는 `_bMustHaveValue` 칸이 따로 있었는데, `GlobalVariableManager::
registerToCommandLine` 이 **전역 변수 전부를 타입과 무관하게 "값 없어도 됨" 으로** 등록했다.
그래서 `-gv_benchMeshes` 처럼 `=값` 을 빠뜨리면 파서가 int32 자리에 **bool `true` 를 밀어 넣었고**,
뒤이어 `readValue` 의 `get_if<int32>` 가 nullptr 이라 `getArgument` 가 false 를 돌려줬다 —
`_bParsed` 는 켜졌으니 기본값으로 돌아가지도 못했다. **경고 한 줄 없이 스위치가 죽었다.**

고친 방법은 칸을 지우는 쪽이다. "값 없이 적을 수 있는가" 는 **저장 타입이 이미 아는 것**이다
(bool 만 가능). `_bMustHaveValue` 와 `ArgumentList.xxx` 의 그 열, `addArgument` 의 그 인자를
모두 없애고 `ArgumentInfo::isFlagArgument()` 하나로 바꿨다. 둘이 어긋날 자리가 사라진다.
비-bool 을 값 없이 적으면 이제 경고를 남기고 무시되므로 **기본값이 그대로 산다.**

**2) 같은 실패의 다른 길 — 모듈 전역 변수의 보류표.** 아직 등록 전인 `-gv_모듈변수` 는 보류표에
문자열로 남고, 값이 없으면 `"true"` 로 적힌다. 모듈이 뜬 뒤 `setValueFromString("true")` 이
int 변수에서 `parseInt` 에 실패하는데 **반환값을 아무도 안 봤다.** 이제 실패하면 경고한다.

**3) `initialize()` 가 앞서 등록된 인자에 밀렸다.** 열거형 조회는 `_listArgument` 를 열거값으로
바로 인덱싱한다. `initialize()` 전에 `addArgument` 가 한 번이라도 불리면 표가 한 칸씩 밀려
`getArgument(WIDTH)` 가 **그 인자를 읽는다.** 막는 것이 줄마다 걸린 assert 뿐이었고 그것은
Debug 에서만 산다. `initialize()` 가 표를 먼저 비우게 했다.

**부수로 고친 것:** 보류 전역 변수를 적용할 때 변경 콜백이 **두 번** 불리고 있었다
(`setValueFromString` 이 타고 가는 `setValueAs*` 가 이미 쏜다).

**검증.** `CommandLineTest` 에 3 케이스를 더했고(13개), 변이 검사로 셋 다 실제로 문다 —
값 검사를 예전 동작으로 되돌리면 `ValuelessNonBooleanArgumentKeepsItsType` 이 7군데서 지고,
`clear()` 를 빼면 `InitializeIsNotShiftedByEarlierArguments` 가 **Shipping 에서** 6군데서 진다
(Debug 에서는 그 전에 assert 가 프로세스를 멈춘다 — 이것이 바로 Shipping 을 못 지키던 이유다).

**다음:** Core 의 나머지 폴더(Common → Compression → Concurrency → Container → Delegate → Event →
File → Log → Math → Memory → Predefined → Process → String → Task → Time → Uuid).

### 2026-09-19 (리눅스에서 늘 빨갛던 셋 — 하나는 진짜 구멍이었고 둘은 환경이었다)

앞 항목에서 "다음 할 일" 로 적은 것을 닫았다. 리눅스 전체 실행에서 늘 지던 셋을 하나씩 봤더니
**성격이 달랐다.**

**1) `LiveShaderTest` 는 환경이 아니라 구멍이었다.** 타깃 포맷이 `DXBC_D3D11` 로 **고정**돼 있었다.
DXBC 를 낼 수 있는 것은 윈도우의 FXC(d3dcompiler)뿐이라 리눅스에서는 컴파일이 시작도 못 했다 —
즉 "`.hlsli` 를 고치면 다시 구운 바이트코드가 달라진다" 는 계약이 **리눅스에서 한 번도 검사된 적이
없었다.** 건너뛰게 만드는 대신 **플랫폼이 낼 수 있는 포맷**을 쓰게 했다(리눅스는 SPIR-V). 이제 같은
계약을 양쪽에서 실제로 태운다. 컴파일러가 아예 없는 기계에서만 건너뛴다(`unavailable` 메시지 기준).

**2) `AppSmokeTest` 둘은 환경이었다 — 다만 이유가 둘이었다.** **백엔드마다 따로 판정**하게 했다:
하나라도 돌면 그것으로 계약을 확인하고, **하나도 못 돌 때만** 케이스를 건너뛴다.

- **백엔드가 이 빌드·플랫폼에 아예 없다**(리눅스의 DX12·DX11):
  `Requested RHI backend is unavailable`.
- **있는데 드라이버가 필요한 기능을 안 준다**: WSLg 의 Mesa 에는 **`GL_ARB_gl_spirv` 가 없고**
  이 엔진의 GL 백엔드는 SPIR-V 를 먹이므로 못 돈다. 백엔드가 그 확장 이름을 로그에 남기고 스스로
  물러난다. (처음에는 "GL 이 간헐적으로 실패한다" 고 봤는데 **6/6 결정적**이었다 — 초기 단계
  로그만 보고 성공으로 오판했던 것이다. 재현율을 재지 않았으면 그대로 넘어갔다.)

> **`Failed to initialize RHI Device!` 만 보고 건너뛰지는 않는다.** 그 한 줄은 **이유를 말해 주지
> 않는다** — 드라이버가 없어서인지 우리가 망가뜨려서인지 구분이 안 된다. 그것으로 건너뛰면 진짜
> 회귀까지 같이 숨는다. 그래서 **백엔드가 스스로 "이 기계에서는 못 돈다" 고 말한 표식**(영문 한
> 문장 · 확장 이름)만 본다. 엔진이 그것을 한 가지 표식으로 말해 주면 더 낫겠다 — 다음 사람 몫.

> **건너뛰는 것에도 대가가 있다.** 조용히 넘기면 윈도우에서 백엔드 하나가 **진짜로** 죽은 날에도
> 초록으로 보인다. 그래서 건너뛴 백엔드는 이름과 함께 로그에 남기고, "하나도 못 돌았다" 는 경우만
> 케이스 자체를 건너뛴다(아무것도 검사하지 않았는데 초록인 것이 제일 나쁘다).

**왜 이것이 중요한가.** 넷이 늘 빨간 상태면 **새 회귀를 아무도 못 본다** — 실제로 그 빨간색 아래에
창 가시성 결함(바로 앞 항목)이 숨어 있었다.

**검증.** 윈도우 Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 ·
린트 18/18 · 윈도우 `LiveShaderTest` 3/3 · `AppTest` 5/5 (**건너뜀 0** — 윈도우에는 백엔드가 있다) ·
**리눅스 전체 실행 29/29 — 100%**(이전 28개 중 24개).

### 2026-09-19 (리눅스 스플래시 — 창 자체가 화면에 올라오지 않고 있었다)

앞의 두 항목(크기 · 지워짐)을 고쳤는데도 여전히 "안 보인다" 였다. 마지막 원인은 **창이 화면에
올라오지 않는 것**이었고, 앞의 둘과는 또 다른 층이다.

**`override_redirect = 1` 이 원인이다.** 그 깃발은 "창 관리자는 이 창에 손대지 말라" 는 뜻이고,
메뉴·툴팁처럼 **이미 떠 있는 창에 딸린 것**을 위한 것이다. 스플래시는 그 앱의 **첫 창**이라 딸릴
곳이 없다 — XWayland(WSLg)에서 그런 창은 서버 안에만 있고 화면에 나타나지 않았다.

**이것이 진단을 오래 끈 이유다.** 서버 안에서는 창이 완벽했다: `mapState` 는 `IsViewable`,
크기 480×280, `XGetImage` 로 되읽으면 그림 약 132,000픽셀 · 글자 · 로딩바가 모두 들어 있다.
**우리가 물어볼 수 있는 모든 것이 "정상" 이라고 답하는데 화면에는 없었다.**

표준 방법으로 바꿨다 — **EWMH `_NET_WM_WINDOW_TYPE_SPLASH`**. 창 종류를 스플래시라고 말해 주면
창 관리자가 장식 없이 위로 띄운다. 우리가 하려던 바로 그 일을 **창 관리자와 싸우지 않고** 얻는다.

**위치는 `PPosition` 이 아니라 `USPosition` 이다.** 처음에 `PPosition` 으로 알렸더니 창 관리자가
자기 배치 규칙으로 덮어써 좌상단 (38, 59) 에 놓았고, 1280×720 메인 창과 겹쳐 잘 보이지 않았다.
`PPosition` 은 "프로그램이 정한 위치" 라 힌트일 뿐이고, `USPosition` 은 "사용자가 정한 위치" 라
존중해야 한다. 매핑 뒤에 `XMoveWindow` 로 한 번 더 못 박는다. 바꾼 뒤 확인: 2560×1440 화면에서
**요청 (1040, 580) → 실제 (1040, 580)**.

> **화면은 도구로 확인할 수 없었다.** 이 WSL 에는 `xwininfo`·`xwd`·`import` 가 없다. 그래서 마지막
> 확인은 **사람이 봐 주는 것**으로 했다 — 5초 붙잡아 두는 임시 코드를 넣고 띄워서 확인받고 지웠다.
> 서버에 묻는 것으로는 끝까지 구분되지 않았다는 점을 적어 둔다: `IsViewable` 도, 되읽은 픽셀도
> "정상" 이었다.

**리눅스 스플래시 세 층 정리** — 하나만 고쳐서는 여전히 안 보인다:
1. **크기**: 1376×768 을 480×280 창에 1:1 로 찍어 좌상단만 나왔다 → 우리가 줄인다.
2. **지워짐**: 이벤트 루프가 없어 `Expose` 마다 배경으로 지워졌다 → 배경 픽스맵으로 서버가 다시 그린다.
3. **안 올라옴**: `override_redirect` 창이 XWayland 에서 표시되지 않았다 → EWMH 스플래시 타입.

**검증.** 윈도우 Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 ·
린트 18/18 · 리눅스 전체 29/29 · **사용자 화면에서 스플래시가 실제로 보이는 것을 확인**.

### 2026-09-19 (리눅스 스플래시는 **그린 것이 곧바로 지워지고** 있었다 — 배경만 남았다)

앞 항목에서 크기를 고쳤는데도 "뒷배경만 보인다 · 글자도 안 보인다 · 로딩바도 안 보인다" 는 제보가
이어졌다. **셋이 같은 원인**이었고, 크기와는 다른 문제였다.

**그린 뒤에 서버가 지운다.** X11 은 창이 노출될 때마다 `background_pixel` 로 지우고 `Expose` 를
보낸다. 그런데 스플래시에는 **이벤트 루프가 없다** — `updateStatus` 가 불릴 때만 그린다. 그래서
그린 족족 지워지고 남는 것은 배경색뿐이었다. 창은 멀쩡히 떠 있으니 "뒷배경만" 으로 보인다.
(`ExposureMask` 를 고르기만 하고 받아 주는 곳이 없었다.)

**진단이 어려웠던 이유 — 로그로는 전부 정상이었다.** 이미지도 읽히고(`1376x768`), 버퍼도 맞고
(`480×280×4 = 537600`), `XCreateImage` 도 성공하고(`depth=24 bpp=32 stride=1920`), `XPutImage` 도
불렸다. **지워지는 것은 그 뒤의 일이라 어느 로그에도 남지 않는다.**

**배경 픽스맵으로 바꿨다.** 픽스맵에 그림·글자·막대를 그리고 그것을 `XSetWindowBackgroundPixmap`
으로 창 배경에 건 뒤 `XClearWindow` 한다. 그러면 **다시 그리는 일을 서버가 한다** — 이벤트 루프
없이도 노출·가림·이동을 견딘다. Win32 가 `WM_PAINT` 로 하는 일을 서버에게 맡기는 셈이다.

**창에서 되읽어 확인했다(`XGetImage`).** 지우기를 일부러 한 번 더 건 **뒤에** 세었다:

| | 고치기 전(창에 직접 그리기) | 고친 뒤(배경 픽스맵) |
|---|---|---|
| 배경이 아닌 픽셀 | **0** | 8400 / 8400 (전수) |
| 그림 | — | 약 132,000 픽셀 |
| 글자 | — | 416 · 507 · 227 · 91 (문구 길이를 따라 변한다) |
| 로딩바 | — | 1,664 = 416 × 4 (트랙 크기와 정확히 일치) |

**폰트는 문제가 아니었다** — 의심해서 재 봤고 `fixed` 도 와일드카드도 열렸다(서버 폰트 6종).
글자가 안 보인 것도 같은 지우기 때문이었다.

> **이 자리는 자동 검증이 어렵다.** 스플래시는 엔진 초기화 중에만 잠깐 뜨고 테스트 하네스가 붙을
> 자리가 없다. 그래서 위 표는 임시 진단 코드로 재고 지웠다 — 남길 수 있는 것은 스케일 계산
> (`SplashImageTest`)까지다. 다음에 이 근처를 고치면 **창에서 되읽어 세는 방법**을 다시 쓸 것.

**검증.** 윈도우 Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 ·
린트 18/18 · **리눅스 되읽기로 그림·글자·막대 셋 다 지우기 뒤에도 남는 것을 확인**.

### 2026-09-19 (리눅스 스플래시는 원본의 **좌상단만** 그리고 있었다)

"리눅스에서 스플래시 이미지가 정상으로 안 보인다" 는 제보. 렌더 문제가 아니라 **크기** 문제였다.

`splash.dds` 는 **1376×768** 인데 스플래시 창은 **480×280** 이다(`ISplashWindow` 기본값). Win32 는
`StretchDIBits` 로 창에 맞춰 늘리는데, **X11 의 `XPutImage` 는 1:1 로만 찍는다** — 늘리거나 줄이는
기능이 아예 없다. 그래서 원본의 **좌상단 480×280 만** 보이고 있었다.

**늘리기를 우리가 한다.** `ISplashWindow::scaleBgraImage` 를 두고 X11 이 창 크기로 줄인 버퍼를
찍는다. **상자 평균**이다 — 2.9배 축소라 최근접으로 뽑으면 글자와 로고 가장자리가 부서진다
(Win32 도 `SetStretchBltMode(HALFTONE)` 로 같은 일을 시킨다).

**같이 나온 두 가지.**
- **채널 정규화가 Win32 안에만 있었다.** `_bIsBgra == false` 면 R↔B 를 뒤집는 루프가 윈도우 구현에
  들어 있었다. 지금 `splash.dds` 가 마침 `B8G8R8A8` 이라 그 루프가 건너뛰어져 드러나지 않았을 뿐,
  **아트를 `R8G8B8A8` 로 다시 내보내는 날 윈도우만 맞고 리눅스는 빨강과 파랑이 뒤바뀐다.**
  `loadSplashImage()` 로 올려 두 경로가 같은 픽셀을 받게 했다(둘 다 BGRA 를 원한다 — Win32 의
  `BI_RGB` 32bpp DIB 도, X11 의 리틀엔디언 TrueColor 비주얼도 메모리에서 B,G,R,X 다).
- **첫 그리기가 버려질 수 있었다.** `XMapRaised` 뒤에 `XFlush` 만 하고 바로 그렸는데, 매핑은
  요청일 뿐이라 서버가 처리하기 전의 그리기는 사라진다. `XSync` 로 한 번 왕복해 확정하고 그린다.

**테스트.** 스케일 계산은 창 없이 검사할 수 있으므로 `SplashImageTest`(nogpu) 로 못 박았다 —
평균 · 목적지 전체가 채워지는가 · 확대 · 빈 입력. 변이 확인: 좌상단 1:1 잘라내기(리눅스에서 났던
증상 그대로)를 넣으면 `DownscaleAveragesTheCoveredPixels` 와 `UpscaleReplicatesSourcePixels` 가 진다.

> **실제 에셋으로 재 보는 케이스는 두지 않았다 — 두 번 시도했고 둘 다 이 버그를 구분하지 못했다.**
> 같은 잘라내기 변이를 넣고 (1) 전체 평균색, (2) 네 분면 평균색을 각각 비교해 봤는데 **둘 다
> 통과했다.** 지금 아트의 좌상단 480×280 이 그림 전체와 색 분포가 비슷해서다. 잡지 못하는 검사를
> 남기면 "실제 그림도 본다" 는 **거짓 안심**만 생기므로 지웠고, 그 사실을 파일 끝에 적어 두었다.
> (다만 이번 수정이 맞다는 증거는 따로 얻었다 — 실제 에셋을 줄인 결과의 평균 BGRA 가 원본과
> **정확히 같았다**: 48/41/38/255.)

> **적어 두는 것 — `CocoaSplashWindow` 는 창을 만들지도 그리지도 않는다.** 이미지를 로드하고
> `_bOpen` 만 세운다. 즉 macOS 에는 스플래시가 **아예 없다.** 방금 macOS 를 CI 에 넣었으니
> 컴파일은 지켜지지만, 그리는 쪽은 맥에서 확인할 수 있는 사람의 몫이다.

**검증.** 윈도우 Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 ·
린트 18/18 · **리눅스 빌드 경고 0 · 리눅스 전체 29/29 · `SplashImageTest` 4/4(양쪽)** ·
리눅스 실기동 `[Error]` 0건.

### 2026-09-19 (macOS 713줄은 **어디서도 컴파일되지 않고 있었다**)

"구조적으로 더 볼 것" 을 다시 재다가 가장 큰 공백을 찾았다. 플랫폼별 소스를 CI 와 대조하니:
Windows 3,676줄(윈도우 CI ✓) · Linux 2,549줄(리눅스 CI ✓) · POSIX 612줄(리눅스 CI ✓) ·
**macOS 713줄 — 아무 데서도 컴파일되지 않는다.**

**그런데 저장소는 지원한다고 말한다.** `cmake/Modules/Platform/MacOS.cmake`, `Source/Core/CMakeLists.txt`
의 `APPLE` 분기 셋, `Vcpkg.cmake` 의 `${arch}-osx` 트리플릿 선택, `IWindow::createPlatformWindow` 의
Cocoa 갈래, 그리고 `vcpkg.json` 이 `directx-dxc`·`directxtex` 를 `windows | linux` 로 막아 둔 것까지 —
**macOS 를 염두에 두고 쓰인 코드**다. 읽는 컴파일러만 없었다.

**그래서 두 번 썩었고 둘 다 눈으로만 찾았다.**
- 2026-09-17: 파일워처의 "같은 이벤트 접기" 규칙이 Windows·Linux 에는 있고 **macOS `pushEvent`
  에만 없었다**.
- `CocoaWindow` 는 `_onResize` 를 **한 번도 부르지 않는다**(Win32·X11 은 각 2회). 창을 줄여도
  스왑체인이 안 따라간다는 뜻이고, **아직 그대로다** — 확인할 방법이 없어 손대지 않은 것이다.

그리고 이번 세션이 그 위험을 그대로 재연했다. `showWindow` → `applyWindowVisibility` 이름 변경에서
**Win32 의 비-윈도우 스텁을 놓쳤고 리눅스 빌드가 잡아 줬다.** `CocoaWindow.cpp` 의 같은 자리 둘은
**아무도 확인하지 않았다.**

**닫는 방법 — CI 에 `macos-14` 빌드 전용 잡 하나.** 테스트는 건너뛴다(`skipTests`) — 러너에 GPU
컨텍스트가 없어 어차피 환경으로 걸러지고, 여기서 막으려는 것은 **컴파일되지 않는다**는 사실
하나다. 같이 필요한 것이 오버레이 트리플릿이었다: `Vcpkg.cmake` 가 `${arch}-osx` 를 고르는데
그 폴더에는 `x64-linux.cmake`·`x64-windows.cmake` 뿐이었다. `arm64-osx.cmake`(러너가 Apple
Silicon 이다)와 `x64-osx.cmake` 를 더했다.

> **첫 실행은 빨갛게 나올 수 있고, 그것이 이득이다.** 지금 상태는 "모른다" 이고, 그때는 "무엇이
> 깨졌는지 안다" 가 된다. 로컬에 맥이 없어 미리 확인할 방법이 없다는 것은 그대로 적어 둔다 —
> 실패하면 로그가 첫 정보다.

**같이 닫은 것 — AGENTS.md 규칙 하나에 게이트가 없었다.** "파일당 익명 네임스페이스 하나" 가
문서에만 있고 아무도 보지 않았다. `Structure/AnonymousNamespaceCount` 로 넣었고 **테스트 셋에서
실제 위반을 찾았다**(`TestEvent` · `TestLog` · `TestResourcePack` — 나중에 헬퍼를 더하면서 블록을
하나 더 연 자리들). 셋 다 첫 블록으로 합쳤다. 규칙 카테고리가 30 → **31**.

> **규칙을 쓰면서 두 번 틀렸고 둘 다 측정이 잡았다.**
> 1. "자유 `static` 함수 금지" 규칙도 같이 넣었다가 **뺐다.** `static sw::vector<...> s_results( n );`
>    같은 **함수 안의 static 지역 변수**를 함수 선언으로 읽었다. `Source/` 에 진짜 위반이 0건이라
>    얻는 것보다 오탐 위험이 컸다 — 과잉 발화하는 게이트는 아무도 린트를 돌리지 않게 만든다.
> 2. 익명 네임스페이스 개수를 **규칙 객체에 들고** 셌더니 결과가 2·3·3 으로 흔들렸다.
>    **게이트는 파일을 동시에 훑는다** — 상태를 경로로 나눠 담아 고쳤다.

**검증.** 윈도우 Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 ·
린트 18/18 · `CheckLintsAreAlive` 24케이스/15게이트 · `CheckCodeConventionsSelfTest` 31카테고리 ·
트리 전체 경고 스윕 **Debug·Release·Shipping 0건**.

### 2026-09-19 (리눅스 CI 가 **설정 단계**에서 멈춰 있었다 — 로컬에서는 볼 수 없는 종류)

리눅스 잡 넷이 **한 줄도 컴파일하지 못하고** 끝나고 있었다. 빌드도 테스트도 아니고 CMake
**configure** 에서였다:

```
File "Scripts/lint/gate/CheckIncludeOrder.py", line 156
    includeFull = f"{includeType}{includeName}{'>' if includeType == '<' else '\"'}"
SyntaxError: f-string expression part cannot include a backslash
```

f-string **식 안의 백슬래시**는 Python 3.12 의 PEP 701 부터 허용된다. 그 줄을 쓴 기계의 파이썬은
**3.14** 라 아무 문제가 없었고 커밋 훅도 린트도 전부 초록이었다. CI 러너(ubuntu-22.04)의 `python3`
만 **3.10** 이라 거기서만 죽었고, 그 자리가 `sw_executePythonScript` 라서 **설정 자체가 실패**했다.

고치는 법은 한 줄이다 — **식을 변수로 먼저 뽑는다.** 그러면 백슬래시가 f-string 밖에 있게 된다.

**하지만 그것만으로는 다시 난다.** 이것은 "실수했는데 못 봤다" 가 아니라 **"내 기계에서는 볼 방법이
없다"** 는 종류다. 그래서 게이트를 하나 뒀다 — `CheckPythonMinimumVersion.py` 가 모든 스크립트의
f-string 식을 **소스에서 잘라** 보고, `kMinimumVersion`(=CI 의 3.10)이 거절할 구문(식 안의
백슬래시 · 여러 줄 식)을 잡는다. 린트 테스트가 17 → **18** 로 늘었다(게이트는 폴더에 떨어뜨리면
CMake·커밋 훅이 알아서 집어 간다).

> **`ast.parse(feature_version=(3, 10))` 은 이것을 못 잡는다 — 해 보고 확인했다.** PEP 701 은 문법이
> 아니라 **토크나이저**를 바꾼 것이라, 새 토크나이저로 읽는 이상 옛 제약이 되살아나지 않는다
> (3.10·3.11·3.12 셋 다 통과했다). 그래서 식의 소스 조각을 직접 본다.

**재현하면서 알게 된 것 둘.**
- CI 프리셋만 **유니티 빌드**(`SW_ENABLE_UNITY_BUILD=ON`)를 쓴다 — 로컬 `WSL-Debug` 와의 유일한
  차이다. 그것까지 WSL 에서 재현해 봤고 **빌드는 깨끗했다**(문제는 오직 설정 단계였다).
- 이 PC 의 WSL(우분투 26.04)에서는 `CI-*` 프리셋이 **링크에서 먼저 막힌다** — 번들
  `Tools/LLVM/bin/ld.lld` 가 `libxml2.so.2` 를 못 찾는다. 재현하려면 시스템 링커를 줘야 한다:
  `-DCMAKE_{EXE,SHARED,MODULE}_LINKER_FLAGS="--ld-path=$(command -v ld.lld)"`.
  (`Scripts/setup/SetupLinuxDevEnvironment.py` 가 같은 안내를 한다.)

**검증.** 윈도우 Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 ·
린트 **18/18** · WSL 에서 `CI-Debug`(유니티) 설정·빌드 성공.

### 2026-09-19 (리눅스에서 창을 다시 만들면 사라졌다 — 화면 상태를 의도로 착각했다)

WSL 클론을 현재 main 으로 맞추고 **진짜 리눅스 빌드**를 돌렸더니 `WindowTest.RecreateKeepsVisibilityAndSize`
가 **3회 모두** 졌다. 레이스가 아니라 결정적이었고, 진 이유가 진짜 결함이었다.

`IWindow::recreate()` 는 "보이던 창이었나" 를 **`isVisible()`** 로 물었다. 윈도우에서 그것은
`IsWindowVisible` — `ShowWindow` 가 세운 WS_VISIBLE 스타일이라 **즉시 참**이다. X11 에서 그것은
`XGetWindowAttributes` 의 `map_state == IsViewable` — **창 관리자가 실제로 매핑한 뒤**에야 참이다.
그래서 리눅스에서는:

- 방금 `showWindow(true)` 한 창도 아직 **거짓**이고,
- **최소화된 창**, **다른 워크스페이스의 창**도 거짓이다.

그 상태에서 백엔드를 바꾸면(`RHI::applyPendingChange` → `recreate()`) **창이 사라진다.** 윈도우에서만
보면 두 질문이 우연히 같아서 끝까지 초록이었다.

**두 질문을 분리했다.** `showWindow()` 가 이제 가상이 아니다 — 기반이 **의도를 기록하고**
플랫폼 훅 `applyWindowVisibility()` 를 부른다(플랫폼이 기록을 빠뜨릴 수 없다). `recreate()` 는
`isVisibleIntended()` 를 보고, `isVisible()` 은 "**지금 화면에 있는가**" 라는 다른 질문으로 남았다
(헤더에 `@warning` 으로 적었다).

테스트도 정직하게 고쳤다 — 엔진 계약인 **의도**를 단언하고, 화면 상태는 **그 플랫폼이 동기로
답할 때만** 추가로 본다(그 여부를 `recreate` 전에 재 둔다). 안 그러면 엔진이 아니라 창 관리자를
시험하게 된다.

> **리눅스 빌드가 아니었으면 못 잡았다.** 그리고 그 과정에서 컴파일 오류도 하나 나왔다 —
> `Win32Window.cpp` 는 **리눅스에서도 컴파일된다**(비-윈도우 스텁 구간이 있다). 이름을 바꾸면서
> 인자 이름이 없는 스텁(`void Win32Window::showWindow( bool )`)을 놓쳤고, 윈도우 빌드는 그 구간을
> 컴파일하지 않으므로 끝까지 초록이었다.

**남은 리눅스 실패 셋은 환경 탓이고, 그대로 두면 위험하다.** WSL 에는 GPU 백엔드도 DXC 도 없다:
`LiveShaderTest.EditedIncludeChangesRecompiledBytecode`(셰이더 컴파일 실패) ·
`AppSmokeTest.EveryBackendStartsRendersAndExitsCleanly` ·
`AppSmokeTest.EditorModeStartsAndExitsCleanly`(둘 다 "Requested RHI backend is unavailable").
**넷이 늘 빨간 상태면 아무도 새 회귀를 못 본다** — 이 저장소가 `SW_TEST_SKIP` 을 두는 이유가 그것이다.
백엔드·DXC 가 없을 때 건너뛰도록 바꾸는 것이 다음 할 일이다(그 전까지 리눅스 기준선은 **28개 중 24개**).

**검증.** 윈도우 Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 ·
린트 17/17 · **리눅스 빌드 경고 0** · 리눅스 `WindowTest` 3회 모두 통과(고치기 전 3회 모두 실패).

### 2026-09-19 (리눅스 파일 다이얼로그 — 도구에 따라 다르게 동작하던 자리를 인자 하나로 드러냈다)

앞 항목에서 "열어 둔다" 고 적은 마지막 후보를 닫았다. `openWithZenity` 와 `openWithYad` 는 명령
조립 스무 줄이 **글자까지 같았는데**, 사본이 이미 갈라져 있었다 — **zenity 만 "All files" 필터를
붙인다.** yad 만 깔린 기계에서는 선언한 확장자 밖의 파일을 **고를 방법이 없다.** 같은 제품이 설치된
도구에 따라 다르게 동작하고, 로그에는 아무것도 남지 않는다.

`buildGtkStyleCommand( toolPath, pFileSelectionFlag, params, bMulti, bAppendAllFilesFilter )` 하나로
모았다. **차이는 이제 인자 둘**이다 — 플래그 이름과 그 bool.

**동작은 바꾸지 않았다.** yad 가 `--file-filter` 를 여러 번 받는지 이 기계에서 확인할 수 없기
때문이다. 잘못 넣으면 "필터가 제한된다" 가 아니라 도구가 인자를 거부해 **다이얼로그가 아예 안 뜬다**
— 지금보다 나쁘다. 대신 확인 방법을 그 자리에 적어 두었다(`yad --file --file-filter='A | *.txt'
--file-filter='All files | *'` 가 뜨는지 본 뒤 호출을 `true` 로). **한 줄만 바꾸면 되는 상태**로
남겨 두는 것이 여기서 할 수 있는 최선이다.

> **리눅스 전용 파일은 WSL 로 문법 검사부터 한다 — 클론을 건드리지 않고.** `/home/ssw/LearningTemplate`
> 클론에는 **남의 작업 변경이 남아 있어** 거기서 빌드하면 그것을 건드린다. 대신 윈도우 트리를
> `/mnt/d/...` 로 읽어 파일 하나만 검사하면 된다:
> ```bash
> WIN=/mnt/d/Projects/Personal/LearningTemplate
> clang++ -std=c++17 -fsyntax-only -DSW_PLATFORM_LINUX -DSW_DEBUG -DSW_COMPILER_CLANG -DSW_X64 >   -DSW_LOG_TAG='"Core"' -I"$WIN/Source/Core" -I"$WIN/Source" -include "$WIN/Source/Core/pch.h" >   "$WIN/Source/Core/File/Linux/LinuxFileDialog.cpp"
> ```
> **이것이 실제로 잡았다** — 옛 `openWithYad` 가 kdialog **뒤에** 정의돼 있어서 새 것을 앞에 넣은 뒤
> 정의가 둘이 됐다. 윈도우 빌드는 이 파일을 아예 컴파일하지 않으므로 끝까지 초록이었을 것이다.

**검증.** 윈도우 Debug·Shipping 빌드(경고 0) · `-L nogpu` 7/7 · 린트 17/17 ·
**WSL clang `-fsyntax-only` 통과(경고 0)**.

### 2026-09-19 (RHI 리소스 조리법 — 남은 중복 후보를 전부 판정했다)

길이순 목록의 나머지를 끝까지 내려가 **전부 판정**했다. 합칠 것은 합치고, 합치지 않을 것은
**왜인지를 탐지기 머리말에 적어** 다음 사람이 같은 판단을 다시 하지 않게 했다.

**합쳤다 — `D3D12RHIResourceRecipe`.** D3D12 에서 버퍼를 만들려면 `D3D12_RESOURCE_DESC` 의 일곱
필드를 버퍼용 고정값으로 채워야 하는데(`Dimension=BUFFER` · `Height=1` · `MipLevels=1` ·
`Format=UNKNOWN` · `SampleDesc.Count=1` · `Layout=ROW_MAJOR` …), 실제로 다른 것은 **크기와 플래그뿐**
이다. 그 일곱 줄이 **여섯 곳**에 있었다(상수버퍼 · 구조버퍼 · 업로드 스테이징 · 리드백 · 정점버퍼 ·
전체화면 정점버퍼). [[VulkanRHISamplerRecipe]] 와 같은 생각이다 — 객체가 아니라 **값을 만드는 방법에
이름을 붙인다.** 힙 종류와 초기 상태는 자리마다 정말 다른 결정이라 호출부가 정한다.

그 과정에서 둘이 같이 닫혔다:
- 구조버퍼 자리에 `heapProps.Type` 대입이 **두 번** 들어 있었다(죽은 줄, 복붙의 지문).
- 같은 자리가 `elementSize * elementCount` 를 **32비트로 곱해** `Width`(UINT64)에 넣고 있었다.
  넘치면 **조용히 작은 버퍼**가 된다. 64비트로 곱하게 했다.

**합쳤다 — Vulkan 전체 밉 배리어 뼈대.** 업로드와 리드백이 "2D 색 이미지 전체 밉" 배리어의 고정
필드 열 줄을 각자 적고 있었다. `makeWholeImageBarrier( image, mipLevels )` 하나로 모았다.
`aspectMask`·`layerCount` 를 빠뜨린 배리어는 검증 계층이 잡아 주지만 **잡히는 자리가 배리어를 건
곳이 아니라 그 뒤의 전이**라 읽기 나쁘다.

**합치지 않았다 — 그리고 그 이유를 `RunDuplicateCode.py` 머리말에 적었다.** 매번 상위권에 올라오는
것들이다:
- **enum 레이블 나열**(`MaterialPacking` 의 두 switch): `case` 줄이 통째로 같고 `-Wswitch-default`
  때문에 `default:` 도 양쪽에 있다. **본체가 다르면 중복이 아니다.**
- **서비스 로케이터 둘**(`sw::editor` / `sw::game`): 서로 다른 DLL 의 서로 다른 레지스트리다
  (`SW_GAMESERVICE_API` 가 그 경계). 미발견 처리도 **의도적으로** 다르다 — 에디터는 문서대로
  `nullptr`(그래서 `CheckNullableServiceUse` 가 있다), 게임은 Debug 에서 `SW_ASSERT` 로 죽는다.
- **컨테이너 래퍼의 미세한 차이**: `VectorWrapper`↔`DequeWrapper` 는 `reserve` 유무,
  `unordered_map.h`↔`unordered_set.h` 는 레이스 래퍼 전달. 접으면 읽기만 나빠진다.

> **하나는 열어 둔다 — `LinuxFileDialog` 의 zenity/yad.** 둘이 거의 같은 명령을 만드는데
> **zenity 만 "All files" 필터를 붙인다.** yad 만 깔린 리눅스에서는 선언한 확장자 밖의 파일을 고를
> 수 없다는 뜻이다. 고치지 않은 이유는 여기서 yad 를 돌려 `--file-filter` 반복 지정을 확인할 수
> 없기 때문이다 — 잘못 넣으면 "제한됨" 이 아니라 **다이얼로그가 아예 안 뜨는** 쪽으로 틀린다.
> 리눅스에서 yad 를 쓸 수 있는 사람이 확인하고 닫을 것.

**검증.** Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 · 린트 17/17 ·
정적 씬 스크린샷 **dx12·vulkan·dx11·gl 네 백엔드 모두 기준 sha(`d2c61c1f8e57cf2d`)와 동일**
(D3D12 버퍼 생성과 Vulkan 배리어를 둘 다 건드렸으므로 픽셀까지 본다).

### 2026-09-19 (컴팩트 스트림의 경계 검사가 두 벌이었고, 어느 쪽도 테스트가 없었다)

`BinarySerializer::deserializeCompact` 의 두 모드(밀집 비트마스크 · 희소 인덱스)가 프로퍼티 하나를
읽는 **열두 줄을 각자** 갖고 있었다. 다른 것은 `propIndex` 를 어디서 얻는가 뿐이다(비트 검사 vs
varint 읽기). 그런데 그 안에 **신뢰할 수 없는 스트림에 대한 경계 검사**가 들어 있다:

```cpp
if ( payloadSize > dataSize - payloadStart )   // 파일에서 온 수다
    return false;
```

한쪽이 이것을 잃으면 손상된 파일 하나로 버퍼 밖을 읽는다 — `applyPropertyPayload` 가 그 크기를
그대로 믿기 때문이다. 저장소에서 **가장 위험한 파싱 코드**를 두 벌로 둘 이유가 없다.
`readAndApplyProperty` 하나로 모았다. (두 사본에 같은 오타까지 들어 있었다 — 주석의 단어가 하나
빠진 채로 양쪽에 복사돼 있었다. 복붙의 지문이다.)

**그리고 재 보니 그 검사에 테스트가 없었다.** 통째로 지워도 `ArchiveTest` 39개가 전부 초록이었다.
이 스위트에는 이미 손상 스트림 케이스가 둘 있는데(`BoundsChecksSurviveSizeOverflow` ·
`CompactDensePropertyCountIsBounded`) 둘 다 **다른 값**을 본다 — 앞엣것은 `Archive`·`reader` 의
길이 인자, 뒤엣것은 `totalProps` 다. 프로퍼티마다의 `payloadSize` 는 아무도 안 봤다.
`CompactPayloadSizeIsBounded` 가 두 모드를 각각 태운다. 변이 확인: 검사를 지우면 희소 모드 단언이
바로 실패한다.

> **처음에 변이를 엉뚱한 바이너리로 쟀다.** 컴팩트 테스트는 `ReflectionTest` 가 아니라
> `EngineTest`(`ArchiveTest`)에 있다. `ReflectionTest` 만 돌리고 "107/107 초록" 을 보고 커버리지
> 구멍이라고 판단했는데, 결론은 우연히 맞았지만 근거는 틀렸다 — **변이를 걸었으면 그 코드를 태우는
> 스위트가 어느 실행 파일에 있는지부터 확인할 것.**

**검증.** Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 · 린트 17/17 ·
`ArchiveTest` 39/39 · `ReflectionTest` 107/107.

### 2026-09-19 (스무딩 마우스 델타는 기본 설정에서 **항상 0** 이었다)

중복 보고서에서 `MouseDevice.cpp` 의 같은 파일 중복(가속·스무딩 식 두 벌)을 열었더니, 합치는 것보다
큰 것이 둘 나왔다.

**1) `poll()` 의 원시 델타 갈래는 도달할 수 없었다.** `InputManager::beginFrame` 이 같은 루프에서
`onFrameBegin()` 을 부른 **바로 다음에** `poll()` 을 부르는데, `onFrameBegin` 이 `_rawDelta` 를
비운다. 그래서 "원시 델타가 있으면 그것을, 없으면 위치 차이를" 이라고 적힌 앞 갈래는 **한 번도
실행되지 않았다** — 읽는 사람만 원시 입력이 거기서 반영된다고 믿게 된다. `poll()` 이 실제로 하는
일은 하나다: 프레임 시작 시점의 위치 차이를 스무딩에 흘려 넣어 **마우스를 멈추면 델타가 0 으로
돌아오게** 한다.

**2) 그리고 진짜 결함 — 기본 구성에서 스무딩 델타가 항상 0 이었다.** `MouseMove` 디스패치는
`setPosition(x, y)` 로 델타를 계산한 **직후에 `addRawDelta( rawDx, rawDy )` 를 무조건** 부른다.
그런데 Win32·X11 둘 다 `makeMouseMove( x, y )` 를 원시 성분 **없이**(기본값 0) 올린다. 스무딩
기본값은 `0.0` 이라 `updateSmoothDelta(0, 0)` 이 EMA 없이 **곧장 대입**하고, 방금 계산한 델타가
지워진다. 즉 `getSmoothMouseDelta()` 는 **양쪽 플랫폼의 기본 구성에서 영원히 0** 이었다.
유일한 소비자인 에디터 Input Map 패널의 "Smooth Delta" 표시가 늘 (0, 0) 이었다는 뜻이다.
`(0,0)` 은 "이 이벤트에 원시 성분이 없다" 는 뜻이지 "멈췄다" 가 아니므로, 그때는 스무딩을 건드리지
않게 했다 — 멈춤은 `poll()` 이 프레임당 한 번 처리한다.

**왜 아무도 몰랐나 — `poll()` 의 스무딩 갱신에 테스트가 아예 없었다.** 통째로 지워도 이 스위트가
전부 초록이었다(변이로 확인). 기존 케이스는 스무딩을 `0.5` 로 켜고 `_x > 0.0f` 만 봤는데, 그
설정에서는 `addRawDelta(0,0)` 이 값을 **절반으로 깎을 뿐 0 으로 만들지 않아** 통과했다.
`SmoothMouseDeltaReturnsToZeroWhenMouseStops` 는 스무딩·가속을 **끄고** 세 프레임을 본다 —
움직인 프레임 · 이벤트 없는 다음 프레임 · 위치가 그대로인 그다음 프레임(여기서 0).

가속·스무딩 식 두 벌은 `updateSmoothDelta` 한 곳으로 모았다 — 감각을 조정하는 사람이 한쪽만
고치면 **입력 경로에 따라 다르게 움직인다.**

**검증.** Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 · 린트 17/17 ·
`InputManagerTest` 20/20 · DX12 에디터 실기동(전 패널) `[Error]` 0건 · 창 33개 · 내용 없는 패널 0개.

### 2026-09-19 (물리 그리드의 "이 AABB 는 어느 셀들인가" 가 여섯 벌이었다)

`PhysicsWorld` 가 AABB 를 셀 범위로 바꾸는 여섯 줄을 **여섯 군데**에 복사해 두고 있었다:
삽입 · 제거 · `setAabb` 의 옛/새 비교 둘 · `queryAabb` · `sweepTest`. 문턱값 `1024` 도 질의 둘에
리터럴로 따로 적혀 있었다.

**어긋났을 때의 증상은 방향마다 다르고, 둘 다 그 자리에서 터지지 않는다.**

- **삽입이 덜 훑으면 충돌을 놓친다.** 바디가 실제로 겹치는 셀에 등록되지 않으므로 그 셀을 보는
  질의가 못 찾는다 — **틀린 답이 조용히** 나온다.
- **제거가 덜 훑으면 그리드가 자란다.** 질의는 후보를 실제 AABB 로 다시 걸러내므로 틀린 답이 되진
  않지만, 옮겨 다닌 바디가 지나온 셀마다 죽은 핸들을 남겨 셀 표가 끝없이 커진다.
  (처음에 이것을 "유령 충돌" 이라고 적었다가, 질의가 다시 거르는 것을 확인하고 고쳤다.)
- `setAabb` 의 "덮는 셀이 그대로면 그리드를 안 건드린다" **지름길**도 같은 계산에 기댄다. 이 비교가
  삽입과 어긋나면 새 셀에 등록되지 않은 채 넘어가 첫 번째 증상이 된다.

`CellRange` 하나로 모았다 — `fromAabb` · `operator==` · `getCellCount` · `forEachCell`.
삽입·제거·지름길·질의 둘이 전부 이것을 쓰므로 `toCellCoord` 는 이제 `fromAabb` 안에만 있다.
질의 둘이 공유하던 "후보 모으고 정렬·중복 제거" 도 `gatherCandidateHandles` 로, 문턱 판단은
`shouldScanAllBodies` + `kMaxQueryCellCount` 로 모았다.

**여기서도 커버리지 구멍이 나왔다.** 이 스위트의 바디는 전부 10~20 단위인데 셀 크기는 **64** 다 —
즉 기존 케이스의 바디는 **한 셀 안에만** 있었고 여러 셀에 걸친 범위 계산은 스트레스 테스트를 빼면
태워지지 않았다. `MultiCellBodyIsFoundInEveryCellItSpans` 가 200 단위(축마다 셀 4개 = 64셀) 바디를
놓고 **걸친 셀 여러 곳에서 각각** 찾아지는지 본다. 질의 박스를 작게 쓰는 것이 중요하다 — 넓은
박스는 셀 수가 바디 수를 넘어 **전수 검사 갈래**로 새서 그리드를 아예 안 본다.

변이 확인: `fromAabb` 가 첫 셀만 덮게 만들면 새 케이스가 0.24ms 만에 실패한다(스트레스 테스트도
같이 지지만, 그쪽은 왜 졌는지 말해 주지 않는다).

**검증.** Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 · 린트 17/17 ·
`PhysicsTest` 12/12.

### 2026-09-19 (Vulkan 리드백은 말없이 실패하고 있었다 — 일회용 커맨드버퍼도 세 벌이었다)

`VulkanRHIResource.cpp` 안에 "일회용 커맨드 버퍼" 절차가 **세 벌** 있었다(할당 → begin → … →
end → submit → waitIdle → free). 셋을 나란히 놓으니 둘이 보였다.

**1) 리드백이 조용히 실패했다.** `readbackTexture2D` 의 실패 네 자리가 **말없이 `false`** 를 돌려줬다 —
형제인 `uploadTexture2D` 는 같은 자리에서 전부 로그를 남기고 있었는데도. 리드백은 오프스크린 렌더
문제가 드러나는 통로라(2026-09 에 이 저장소가 여러 세션을 쓴 자리다) "false 인데 이유가 없다" 가
곧 긴 추적이 된다. 전부 소리 나게 했고, 스테이징 버퍼를 흘리던 실패 경로도 같이 닫았다.

**2) 해제를 소멸자에게 줬다.** `VulkanOneShotCommands` 가 할당·시작을 생성자에서, **해제를
소멸자에서** 한다. 지금은 할당과 제출 사이에 `return` 이 없어 새는 자리가 없지만, **누군가 중간에
검사를 하나 더하는 날 커맨드 버퍼가 샌다** — 풀에서 조용히 자라다 나중에 할당이 실패한다.
세 자리 모두 이 한 벌을 쓰므로 raw `vkAllocateCommandBuffers`/`vkFreeCommandBuffers`/
`vkEndCommandBuffer` 는 이제 헬퍼 안에만 있다.

> **로그를 켜자마자 내가 넣은 버그가 바로 잡혔다.** 헬퍼가 `vkEndCommandBuffer` 를 부르는데 원래
> 있던 호출을 안 지워 **두 번** 불렸고, Vulkan 검증 계층이 그 자리에서 소리를 냈다. 조용했다면
> 스크린샷 해시가 같아서 그냥 지나갔을 것이다 — 이 커밋이 고친 문제가 이 커밋을 고쳐 준 셈이다.

**검증.** Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 · 린트 17/17 ·
정적 씬 스크린샷 **dx12·vulkan·dx11·gl 네 백엔드 모두 기준 sha(`d2c61c1f8e57cf2d`)와 동일,
`[Error]` 0건** — 스크린샷 자체가 `readbackTexture2D` 를 지나므로 이 경로는 네 백엔드에서 실제로
실행됐다.

### 2026-09-19 (Undo 와 Redo 가 서로의 거울인데 코드는 네 벌이었다 — 그리고 그 왕복엔 테스트가 없었다)

`recordCreation` 과 `recordDestruction` 은 **같은 두 절차를 반대로 이은 것**이다 — "없앤다" 와
"저장해 둔 XML 로 되살린다". 그런데 그 두 절차가 **네 벌로 복사**돼 있었다(없애기 둘 · 되살리기 둘).
지금은 바이트까지 같다는 것을 프로그램으로 확인했다 — **아직 갈라지지 않았을 뿐이다.**

되살리기 쪽을 한 번 고치면(부모 복원·이름 충돌 처리 같은 것) 나머지 방향이 조용히 뒤처지고,
증상은 **"Undo 는 되는데 Redo 는 안 된다"** 로 나온다. 사용자가 작업을 잃는 방식이면서 로그에는
아무것도 남지 않는 종류다.

`recordObjectLifetime( pObj, label, ObjectLifetimeEdit )` 하나로 모았다. 두 절차를 만들고
**방향이 순서만 정한다** — `Created` 면 Undo 가 없애고, `Destroyed` 면 Undo 가 되살린다.
`recordCreation`·`recordDestruction` 은 그 호출 한 줄이 됐다.

**더 큰 발견은 그 왕복에 테스트가 하나도 없었다는 것이다.** 이 스위트의 기존 케이스들은 씬이 없어
Undo/Redo 델리게이트가 **곧장 돌아가고 있었다** — 즉 절차의 본체는 한 번도 실행되지 않았다.
`SceneManager` 를 지역 서비스로 걸면(`bindLocalService`) `editor::getActiveScene()` 이 답하므로
델리게이트가 끝까지 지나간다. `ObjectLifetimeUndoRedoAreMirrors` 가 두 방향을 **실제로 실행해**
살아 있는 오브젝트 수로 확인한다.

변이 확인: 방향을 뒤집으면 세 단언이 실패한다(생성의 Undo 가 안 없애고, 생성의 Redo 가 안 되살리고,
삭제의 Undo 가 안 되살린다).

**검증.** Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 · 린트 17/17 ·
`EditorTransactionTest` 4/4 · **테스트 씬**으로 DX12 에디터 실기동 `[Error]` 0건 · 패널 덤프 창 15개 ·
내용 없는 패널 0개.

### 2026-09-19 (future 의 잠금 규약이 두 벌이었다 — `SharedFutureSignal` 하나로)

앞 항목의 결함이 **두 특수화를 따로 고쳐야 했던** 것이 이유다. `SharedFutureState<T>` 와
`SharedFutureState<void>` 는 뮤텍스·조건 변수·`_bReady`·`wait`·`waitFor` 를 각자 갖고 있었고,
무엇보다 **순서 규약**을 각자 적고 있었다 — "락 안에서 완료로 표시하고, 알림과 이어받기 호출은
락 밖에서".

그 규약은 틀리기 쉬운 쪽이다. 이어받기가 **다시 이 future 를 건드릴 수 있어** 락 밖에서 불러야
하고, 옮긴 델리게이트를 조건으로 되살려 읽어서도 안 된다. 실제로 그 자리에서 use-after-move 를
한 번 고쳤는데, **그때도 두 벌을 따로 고쳤다.**

`SharedFutureSignal` 하나에 모았다. 값 저장은 특수화가 람다로 준다:

- `markReadyAndTakeContinuation( storage, storeValue )` — 락 안에서 표시하고 보관된 이어받기를
  **돌려준다**(호출부가 락 밖에서 부른다). `setValue` 세 벌(const&, &&, void)이 이것 하나를 쓴다.
- `takeImmediateOrStore( cont, outStorage )` — 이미 끝났으면 그대로 돌려주고 아니면 보관한다.
  **갈 곳이 하나씩만 정해지므로** 옮긴 값을 되살려 읽는 자리가 없다.

동작은 바뀌지 않는다 — 기존 테스트가 그것을 지킨다. **두 경로가 실제로 태워지는지도 변이로
확인했다**: `takeImmediateOrStore` 의 "이미 끝났으면 즉시" 갈래를 없애면
`CombinatorsDoNotHangOnInvalidOrEmptyInput` 이 2초 타임아웃과 함께 실패한다.
`Core/Task` 의 12줄 이상 중복은 이제 **0건**이다.

**검증.** Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 · 린트 17/17 ·
`TaskTest` 19/19.

### 2026-09-19 (무효한 future 에 `then` 을 걸면 영원히 기다리는 future 가 나왔다)

중복 보고서의 같은 파일 상위권에 `TaskFuture.h` 가 있어 열었더니, **이미 두 번 우회된 결함**이
뿌리에 그대로 있었다.

`TaskFuture::then()` 은 원본이 무효할 때(`_pState == nullptr`) **유효한** future 를 돌려줬다.
그런데 그 future 에는 아무도 값을 넣어 주지 않는다 — `wait()` 하면 조건 변수가 절대 깨어나지 않아
**영원히 멈춘다.** "느리다" 가 아니라 완전한 정지이고, 스택만 보면 그 대기가 정상인지 알 수 없다.

**그 함정을 콤비네이터 둘이 각자 우회하고 있었다.** `whenAllFutures` 는 유효한 것만 세고(안 그러면
카운트다운이 0 에 닿지 못한다), `whenAnyFuture` 는 후보가 없으면 무효한 future 를 돌려준다 —
**후자는 실제로 같은 증상을 내고 고쳐진 자리다.** 둘 다 주석으로 그 이유를 길게 적어 두고 있었다.
우회가 두 벌이면 **세 번째 호출부가 같은 함정에 빠진다.**

뿌리를 고쳤다 — `then()` 은 이제 **무효가 들어오면 무효를 돌려준다.** 그러면 `wait()` 가 곧장
돌아오고 `waitFor` 가 false 이며 `isValid()` 로 물어볼 수 있어, 사슬 전체에 그 사실이 전해진다.
(같은 파일의 `fallback()` 은 처음부터 이 자리를 바르게 다루고 있었다 — 값을 채워 끝낸다.
같은 파일 안에서 두 함수가 같은 조건을 다르게 다루고 있었던 셈이다.)

**`TaskFuture<T>` 와 `TaskFuture<void>` 는 서로 다른 특수화라 한쪽만 고쳐질 수 있다.** 그래서
테스트가 **네 조합**(T→T · T→void · void→T · void→void)을 모두 본다. 변이 확인: 두 특수화를 예전
모양으로 되돌리면 네 블록이 전부 실패한다.

> **테스트는 일부러 `wait()` 를 부르지 않는다.** 회귀가 나면 그 호출이 실패가 아니라 **정지**를
> 만든다 — CI 가 타임아웃까지 매달린다. `isValid()` 와 짧은 `waitFor` 로만 묻는다.

**검증.** Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 · 린트 17/17 ·
`TaskTest` 19/19. 같은 날 훑기 둘도 깨끗했다 — 헤더 자립성 **461개 전부 자립**, 트리 전체 경고
**Debug·Release·Shipping 0건**.

### 2026-09-19 (`sw::array` 를 락-프리 버퍼에 쓰지 않는 이유 — 재 보고 한 곳에 적었다)

"`sw::array` 대신 `std::array` 를 쓴다(레이스 탐지기 오탐)" 는 주석이 **세 곳에 서로 다른 말로**
적혀 있었다(`ConcurrentQueue.h` · `LockFreeQueue.h` · `RenderThread.h`). 앞서 탐지기를 고쳤으니
이 근거가 아직 유효한지부터 확인했다.

**유효하다 — 그리고 고친 것과는 다른 문제다.** 2026-09-19 의 탐지기 수정은 **같은 스레드의 재진입**
오탐을 닫은 것이다. 락-프리 큐는 여러 스레드가 같은 버퍼를 **일부러 동시에** 만지므로, `sw::array` 를
쓰면 그 접근이 그대로 레이스로 보고되고 **보고는 Fatal 이라 프로세스가 죽는다.** 가드는 Debug 에서만
걸리므로 Debug 한정이다.

**다만 "오탐을 유발합니다" 는 실제보다 강한 말이었다.** 재 봤다 — `ConcurrentQueue` 의 버퍼를
`sw::array` 로 바꿔 10만 건 MPMC 스트레스를 **8회** 돌렸는데 **한 번도 나지 않았다.** 이유는
가드가 걸리는 구간이 `operator[]` · `at()` **호출 그 자체뿐**이기 때문이다 — 참조를 돌려준 뒤의
원소 접근은 가드 밖이라, 두 스레드가 정확히 그 몇 개의 명령 안에서 겹쳐야 보고된다.

**드물다는 것이 더 나쁘다.** "테스트가 초록이니 괜찮다" 로 판단하면 **남의 기계에서 언젠가 한 번**
죽는다. 그래서 규칙과 그 측정을 **결정을 내리는 자리**인 `Core/Container/array.h` 머리말에 적고,
세 곳은 한 줄로 그것을 가리키게 했다. 다음 사람이 "여기에 `sw::array` 를 써도 되나" 를 물을 때
읽는 파일이 거기다.

**검증.** Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 · 린트 17/17 ·
`RenderThread.h` 를 건드리므로 정적 씬 스크린샷 **dx12·vulkan·dx11·gl 네 백엔드 모두 기준
sha(`d2c61c1f8e57cf2d`)와 동일** · DX12 에디터 실기동 `[Error]` 0건.

### 2026-09-19 (맵 프로퍼티는 인스펙터에서 **빈 칸 하나**였다)

`drawContainerProperty` 는 시퀀스가 아니면 **아무것도 그리지 않고 돌아갔다.** 그래서 맵 프로퍼티는
이름만 있고 값 칸이 비어 있었다 — 값이 비었는지, 그리지 못하는 것인지, 버그인지 화면만 보고는
구분할 수 없었다. 이 패널은 **모르는 타입에는 "No inspector for ..." 라고 말한다.** 컨테이너만
조용했던 셈이다.

맵 프로퍼티는 가상이 아니라 실재한다 — `GameData::_mapCustomProperty`,
`TurnBattle::SaveGame::_mapFlag`.

**모든 갈래가 무언가를 그리고 끝나게** 했다: 컨테이너를 읽을 수 없으면 그렇게 말하고, 맵이면
**몇 개 들어 있는지와 함께** 아직 그리지 않는다고 말하고, 시퀀스도 맵도 아니면 기존의
"No inspector for" 로 떨어진다. 중첩된 `if` 안에서 `return` 하던 모양도 **앞에서 걸러내고 본문은
평평하게** 바꿨다 — 갈래가 하나 더 생겨도 조용히 빠져나갈 자리가 없다.

> **맵 편집은 아직 안 된다 — 래퍼에 쓸 수 있는 값 접근자가 없다.** `IMapContainerWrapper::forEach`
> 는 키·값을 **모두 const 로만** 준다. 키는 어차피 정렬 키라 제자리 편집이 불가능하고(`set` 과 같다),
> 값을 편집하려면 `getValueForKey` 같은 가변 접근자가 필요하다. 그때까지는 개수만 보여 준다.

**검증.** Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 · 린트 17/17 ·
DX12 에디터 실기동 `[Error]` 0건 · 패널 덤프 전부 열어 **창 33개 · 내용 없는 패널 0개**.
(맵 갈래 자체는 화면으로 확인하지 못했다 — 기본 세션에서 인스펙터에 올라오는 타입 중 맵 프로퍼티를
가진 것이 없다. 도달 가능성은 위 두 타입으로 확인했다.)

### 2026-09-19 (고정 배열 래퍼는 첫 왕복에 데이터를 깨뜨릴 준비가 돼 있었다)

`appendElement` 를 인터페이스에 넣고 나서 **그 기본 구현을 물려받으면 안 되는 래퍼**가 하나 더 있는지
훑었다. `ArrayWrapper`(고정 배열) 가 그랬다.

기본 구현은 `addElementDefault()` 로 자리를 만들고 마지막 칸에 쓴다. 그런데 고정 배열의
`addElementDefault` 는 **아무 일도 하지 않는다**(자랄 수 없으므로). 그래서 개수가 늘지 않고
**들어오는 모든 원소가 마지막 칸 하나에 차례로 덮어써진다.** 역직렬화는 **오류 없이 끝나고**
배열만 틀린 값이 된다 — 앞 항목의 `set` 과 같은 종류지만, 이쪽은 죽지도 않아서 더 조용하다.

**지금은 아무도 안 쓴다 — 그래서 위험하다.** `ArrayWrapper` 는 저장소 어디에서도 인스턴스화되지
않는다(코드젠이 고정 배열 프로퍼티를 내보내지 않는다). 테스트만 이 래퍼를 직접 만들어 쓴다. 즉
**"지원되는 것처럼 보이지만 첫 사용에 깨지는"** 상태였다.

거절하게 했다 — `appendElement` 를 재정의해 `false` 를 돌려주고, 세 직렬화기는 그것을 스트림 거부로
다룬다. 고정 배열을 정말 지원하려면 "뒤에 넣기" 가 아니라 **인덱스로 채우는** 경로가 필요하고,
그것은 인터페이스 변경이다. 그때까지는 **틀린 값보다 거절이 낫다.**

변이 확인: 재정의를 지우면 `FixedArrayRefusesAppend` 가 세 줄 모두 실패한다 —
`Expected [4], Actual [999]` 가 그 덮어쓰기를 그대로 보여 준다.

**검증.** Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 · 린트 17/17.

### 2026-09-19 (인스펙터의 `ReadOnly` 는 컨테이너 프로퍼티에서 조용히 무시되고 있었다)

앞 항목에서 `set` 원소를 인스펙터에서 편집하지 못하게 막았는데, **그 막음이 절반만 닿고 있었다.**
`drawContainerProperty` 의 `bReadOnly` 는 `+ Add`·`Clear` 버튼만 감쌌고, **원소 위젯은 그 밖에서
무조건 편집 가능하게 그려지고 있었다.** 그래서:

- `PROPERTY( ReadOnly )` 로 표시한 **컨테이너**는 원소가 그대로 편집됐다 — 표시가 아무 일도 하지
  않았다. 이 자리의 `ReadOnly` 는 스칼라에만 듣고 있었던 셈이다.
- 그리고 방금 넣은 "연관 컨테이너는 제자리 편집 금지" 도 같은 이유로 새어 나갔다 — 읽기 전용이라고
  적어 두고 **편집은 열려 있었다.**

**두 조건을 한 자리로 올렸다.** `EditorSessionPolicy::areContainerElementEditsAllowed( bReadOnly,
bAllowsInPlaceElementWrite )` 이고, 원소 루프 전체를 `ImGui::BeginDisabled` 로 감싼다. 이 저장소가
에디터 규칙을 다루는 방식 그대로다 — 판단은 패널 밖 정책 클래스에, 테스트가 그 규칙을 지킨다
(`ContainerElementEditsNeedBothConditions`, 네 조합 전부). 변이 확인: `bReadOnly` 항을 지우면 —
즉 예전 동작으로 되돌리면 — 그 케이스가 바로 실패한다.

**그러다 테스트 프레임워크의 빈자리를 하나 메웠다.** `SW_EXPECT_TRUE_MSG` 는 있는데
**`SW_EXPECT_FALSE_MSG` 가 없었다.** 그래서 부정 단언에는 메시지를 붙일 수 없었고, 저자는 메시지를
버리거나 `SW_EXPECT_TRUE_MSG( x == false, ... )` 로 뒤집어 썼다. 실패했을 때 설명이 가장 필요한 쪽이
**부정 단언**인데(무엇이 열려 있으면 안 되는지) 그쪽이 비어 있었다. 짝을 맞췄다.

> **변이 검증에서 겪은 함정 — 백업 파일로 되돌리면 ninja 가 다시 빌드하지 않는다.**
> 변이를 되돌리려고 `Copy-Item` 으로 백업본을 덮었더니 **파일의 mtime 이 과거로 돌아가** 오브젝트보다
> 오래된 입력이 되었고, ninja 는 최신이라고 판단해 넘어갔다. 그 결과 **변이가 들어간 바이너리로
> 테스트를 돌려** 없는 실패를 하나 보고했다. 되돌린 뒤에는 **반드시 파일을 touch 하고** 다시 빌드할 것.

**검증.** Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 · 린트 17/17 ·
DX12 에디터 실기동 `[Error]` 0건 · 패널 덤프 **기본 창 15개 / 전부 열면 33개, 둘 다 내용 없는 패널 0개**.
(렌더 경로는 건드리지 않아 정적 씬 스크린샷은 앞 항목의 결과가 그대로 유효하다.)

### 2026-09-19 (제자리 쓰기가 불가능한 컨테이너 — 리플렉션 `set` 은 역직렬화에서 죽고 있었다)

바로 아래 항목에서 "설계 변경이라 지금은 안 한다" 며 적어 둔 결함을 열었다. 재현은 한 줄이다 —
`set<int32>` 프로퍼티를 가진 타입을 왕복시키면 **프로세스가 죽는다**(exit 3, 메시지 없음).

**원인은 역직렬화가 컨테이너를 채우는 방법을 하나만 알고 있었다는 것이다.** 자리를 먼저 만들고
(`addElementDefault`) 그 자리에 **제자리로 쓴다**(`getElement(i)`). 연속·노드 컨테이너에서는 옳다.
**연관 컨테이너에서는 틀렸다** — `set` 의 원소는 곧 정렬 키라, 트리에 들어간 뒤에 값을 바꾸면
정렬 불변식이 깨진다(`SetWrapper::getElement` 는 `const_cast` 로 const 를 벗기고 있었다). 게다가
**그 자리에서 터지지 않는다.** 깨진 트리는 다음 탐색·다음 삽입·소멸자 중 어디서든 죽고, 그래서
증상이 직렬화와 상관없어 보인다.

**고친 방식 — "어떻게 넣는가" 를 컨테이너에게 돌려줬다.** 호출부가 컨테이너 종류를 묻고 분기하게
만들면 그 분기가 **네 곳(세 직렬화기 + 인스펙터)** 으로 복사되고, 다섯 번째 호출부가 생기는 날
같은 버그가 다시 난다. 그래서 인터페이스에 둘을 넣었다:

- `appendElement( pContainer, fill )` — 원소 하나를 **읽어서 뒤에 넣는다.** 읽기는 호출부가 준
  `fill` 델리게이트가 하고, **넣는 방법은 컨테이너가 정한다.** 기본 구현은 예전 그대로(자리 만들고
  제자리 쓰기)라 `vector`·`deque`·`list` 는 복사가 늘지 않는다. `SetWrapper` 만 **스택 임시에 다 읽은
  뒤 `insert(std::move(...))`** 하도록 재정의한다.
- `allowsInPlaceElementWrite()` — **이미 들어 있는 원소를 고쳐도 되는가.** 역직렬화는 `appendElement`
  로 이 질문을 피하지만, **인스펙터는 피할 수 없다** — 이미 들어 있는 원소를 편집하는 UI 이기
  때문이다. `false` 면 그 원소들을 읽기 전용으로 그리고 이유를 옆에 적는다(예전에는 `set` 원소를
  인스펙터에서 편집하면 같은 이유로 트리가 깨졌다).

새 컨테이너 래퍼를 더할 때 답할 질문은 **"내 컨테이너는 제자리 쓰기가 되는가"** 하나다.

**테스트는 세 포맷을 다 태운다.** `SetPropertyRoundTripsInEveryFormat` 이 Binary·JSON·XML 을 각각
왕복시킨다 — 포맷마다 채우기 루프가 따로 있어서 **하나만 고치면 나머지 둘이 남는다**(같은 자리가
이 저장소에서 이미 한 번 그랬다). 값은 일부러 정렬되지 않은 `{42, 5, 17}` 로 넣는다. 변이 확인:
`SetWrapper::appendElement` 재정의를 지우면 Binary 왕복에서 바로 실패한다.

> **함정 하나를 같이 적어 둔다 — 빌드 로그의 경고가 sccache 가 재생한 옛 경고일 수 있다.**
> 이 작업 중에 `ReflectionContainers.h(87,13): duplicated command '@brief'` 가 빌드마다 나왔다.
> 87 줄에는 `@brief` 가 없었다. **clang 이 같이 찍어 준 소스 줄의 본문이 그 줄 번호의 실제 내용과
> 달랐다** — 그것이 재생의 표시다. sccache 는 캐시가 맞으면 오브젝트뿐 아니라 **그때의 stderr 까지
> 다시 출력한다.** (PCH 가 낡은 줄 알고 `pch.h` 를 건드린 것은 헛짚은 것이었다.)
> 확인·해소는 `SCCACHE_RECACHE=1` 로 그 TU 를 다시 빌드하는 것이고, `RunBuildWarnings.py` 는
> 애초에 캐시를 지나지 않으므로 **빌드 로그와 다르면 그쪽이 맞다.** 두 파일 머리말에 적어 뒀다.

**검증.** Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 · 린트 17/17 ·
정적 씬 스크린샷 **dx12·vulkan·dx11·gl 네 백엔드 모두 기준 sha(`d2c61c1f8e57cf2d`)와 동일** ·
DX11 에디터 실기동 `[Error]`·`[Warning]` 0건.

### 2026-09-19 (레이스 검출기에 스레드 개념이 없었다 — 없는 레이스를 보고하고 있었다)

방향을 "중복 지우기" 에서 **"그 실수가 생길 수 없게"** 로 옮겨 세 가지를 했다.

**1) 탐지기를 저장소의 도구로 만들었다.** 이번 라운드의 발견은 전부 즉석 스크립트에서 나왔고, 그건
버려진다 — 다음 사람은 재현할 수 없고 정리는 다시 썩는다. `Scripts/lint/report/RunDuplicateCode.py`
로 넣었다. **창 병합**(안 하면 한 블록이 수십 건으로 부풀어 숫자가 거짓말을 한다)과 **길이순 정렬**
(안 하면 고를 수 없다)이 핵심이고, 매번 상위권을 채우는 **정당한 중복**(백엔드 인터페이스 선언 ·
레이스 래퍼 전달 · 플랫폼 구현)을 파일 머리말에 적어 다음 사람이 같은 것을 다시 판단하지 않게 했다.

**2) 같은 래퍼 둘을 별칭으로.** `SetWrapper` ≡ `UnorderedSetWrapper`, `MapWrapper` ≡
`UnorderedMapWrapper` 가 **글자까지 같았다.** 별칭으로 바꾸면 한쪽에 메서드를 더할 때 다른 쪽이
뒤처질 **수가 없다**. (기반 클래스는 그대로다 — `unordered_map` 은 전과 같이 `IMapContainerWrapper`
이고, `set` 계열이 `ISequenceContainerWrapper` 인 것도 이전 설계 그대로다. 여기서 "Sequence" 는 STL 의
분류가 아니라 **리플렉션이 필요한 접근 방식**(인덱스 순회·개수·기본 원소 추가)을 뜻한다.)
`VectorWrapper`↔`DequeWrapper`(reserve 유무)와 `ListWrapper`(임의 접근이 없어 `std::advance`)는
정당하게 달라 그대로 뒀다.

**3) 그러다 진짜 결함을 찾았다 — 레이스 검출기가 없는 레이스를 보고하고 있었다.**

`sw::set` 에 초기화 리스트를 대입하는 **한 줄**이 Debug 빌드에서 레이스로 보고됐다:

```
sw::set<int32> values;
values = { 5, 17, 42 };     // ← "Concurrent write/write access detected!"
```

원인은 `set::operator=` 가 쓰기 가드를 잡은 채 `clear()`·`insert()` 를 부르는데 **그 둘도 가드를
잡기** 때문이다. 그런데 `RaceDetectContext` 에는 **스레드 개념이 아예 없었다** — `writers > 0` 이면
바로 보고한다. 데이터 레이스는 정의상 두 스레드가 필요한데, 한 스레드의 재진입을 레이스로 본 것이다.

같은 자리가 `map::operator=` 에도 있고, **쓰기 가드 안에서 const 메서드를 부르는 모든 경우**가 같은
모양이다(읽기 진입도 `writers > 0` 이면 보고한다). 즉 "가드 메서드가 가드 메서드를 부르지 않게
조심한다" 로는 막히지 않는 **구조적** 오탐이다. 검출기가 **주인 스레드를 기억하게** 해서 닫았다 —
카운트가 0 이 되는 순간 주인을 비운다(그 비우기를 안 하면 검출기가 눈이 먼다. 그래서 그것도 케이스로 지킨다).

**진짜 레이스는 그대로 잡힌다** — 다른 스레드가 들어오면 id 가 달라 보고된다. 손으로 확인했다(다른
스레드의 `enterWrite` 가 "Concurrent write/write access detected" 를 그대로 냈다). 다만 보고가
**Fatal 이라 프로세스가 죽으므로** 상시 케이스로는 둘 수 없어, 대신 "주인이 나가면 다음 스레드가
조용히 들어온다" 를 케이스로 남겼다.

> **함께 발견했으나 고치지 않은 것 — 리플렉션 `set` 프로퍼티는 역직렬화에서 죽는다.**
> (**2026-09-19 해결됨** — 바로 아래 항목을 볼 것.)
> 재현: `set<int32>` 프로퍼티를 가진 타입을 `BinarySerializer::serialize` → `deserialize` 하면
> **프로세스가 죽는다**(exit 3, 메시지 없음). 원인은 역직렬화가 시퀀스 컨테이너를
> `addElementDefault()` 로 자리를 만들고 `getElement(i)` 에 **제자리로 쓰는** 패턴을 쓰는데,
> set 에서 그것은 **트리 안의 키를 제자리에서 바꾸는 일**이라 정렬 불변식이 깨지기 때문이다
> (`SetWrapper::getElement` 가 `const_cast` 로 const 를 벗긴다). 래퍼만 두드리면 통과하므로
> 증상은 데이터에 따라 나중에 터진다.
> **왜 그때 안 고쳤나**: 올바른 수정은 "제자리 쓰기가 불가능한 컨테이너" 개념을 래퍼 인터페이스에
> 넣고 **세 직렬화기(Binary·Json·Xml)와 인스펙터**의 채우기 경로를 insert 기반으로 바꾸는 설계
> 변경이다. 긴 세션 끝에 시작할 크기가 아니었다. 지금 이 저장소에 `set` 프로퍼티를 쓰는 타입은 없다
> (그래서 아무도 몰랐다). **그 설계 변경을 다음 항목에서 했다.**

**검증.** Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 · 린트 17/17 ·
`DataRaceDetectorTest` 5/5 · `ReflectionTest` 105/105.

### 2026-09-19 (DX11 드로우 진입점 넷 중 하나는 여전히 파이프라인을 걸지 않고 있었다)

길이순 목록의 다음 둘을 이어서 처리했다.

**1) `D3D11RHICommandContext` — 이미 한 번 버그를 냈던 자리.**

DX11 은 `setPipelineState` 가 **핸들만 기록**하고 VS/PS/InputLayout·정점 버퍼·토폴로지는 **드로우
시점에** 건다. 그래서 드로우 진입점마다 22줄짜리 블록이 필요한데, 그것이 세 벌로 복사돼 있었다.
`drawIndirect` 의 주석이 그 대가를 적어 두고 있었다 — *"이 경로에만 그 블록이 없어서 GPU 드리븐
경로의 모든 드로우가 셰이더도 정점버퍼도 없이 나갔다(화면이 클리어 색만 남던 원인)"*.

**그래서 네 번째 진입점을 확인했더니 `drawIndexedIndirect` 에는 지금도 그 블록이 없었다.**
엔진에서 아무도 부르지 않아(인터페이스와 포워더에만 있다) 드러나지 않았을 뿐, 부르는 순간 예전과
같은 증상이 난다 — "안 쓰이는 경로는 조용히 썩는다" 의 교과서적인 사례다.

`bindGraphicsPipelineForDraw()` 하나로 모으고 **네 진입점 모두** 그것을 부르게 했다. 이제 새 드로우
진입점이 같은 실수를 할 자리가 없다.

**2) `BinarySerializer` — 하나는 합치고, 하나는 합치지 않았다.**

- **합쳤다**: "프로퍼티 하나의 페이로드를 인스턴스에 쓴다"(비트필드·컨테이너·그 외 값의 세 갈래,
  20줄)가 컴팩트 경로 둘에 복사돼 있었다 → `applyPropertyPayload`.
- **합치지 않았다**: 엄격(`deserialize`)과 소프트(`deserializeSoft`)의 태그 읽기 루프는 겉모습이
  비슷하지만 **의미가 반대**다 — 엄격은 실패하면 스트림을 거부하고, 소프트는 orphan 으로 적고
  계속한다. 플래그로 묶으면 여섯 군데 분기가 모드에 따라 갈리는 함수가 되고, 저장소에서 가장
  위험한 파싱 코드가 더 읽기 어려워진다.
- **다만 진짜 결함이 하나 있었다**: 소프트 경로가 `kFastPropBitmaskThreshold` 대신 **리터럴 `64`**
  를 세 곳에서 쓰고 있었다. 값이 같아 증상은 없었지만 메모리의 "상수는 하나로, 별칭도 금지" 규칙
  그대로다 — 한쪽만 바꾸면 프로퍼티가 64개 언저리인 타입에서만 갈리는 차이가 된다. 상수로 통일했다.

**그리고 변이가 커버리지 구멍을 하나 찾아냈다.** `applyPropertyPayload` 의 컨테이너 갈래를 없애는
변이가 **통과했다** — 컴팩트 경로를 태우는 테스트의 타입(`TestReflectedPlayer`)에 컨테이너 프로퍼티가
하나도 없어서, 그 갈래가 **한 번도 실행되지 않고 있었다.** 컴팩트 경로에서 컨테이너를 잘못 읽어도
모든 테스트가 초록이었다는 뜻이다. `CompactRoundTripsContainerProperties` 로 닫았고(vector<int32> ·
vector<string> 왕복), 같은 변이가 이제 그 케이스에서 잡힌다.

**검증.** Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 · 린트 17/17 ·
정적 씬 스크린샷 **dx11·dx12·vulkan·gl 모두 기준 sha(`d2c61c1f8e57cf2d`)와 동일** · DX11 에디터
실기동 `[Error]` 0건.

### 2026-09-19 (탐지기를 고쳐 다시 재니 DX12 bindless 에 같은 열두 줄이 넷 있었다)

"구조적으로 더 볼 것이 있나" 를 추측 대신 **다시 측정**했다. 지금까지의 조사에는 구멍이 둘 있었다:

1. **`.cpp` 만 봤다** — 헤더는 한 번도 훑지 않았다.
2. **파일 간만 봤다** — 같은 파일 안의 중복은 일부러 뺐었다.
3. 게다가 세는 방식이 틀렸다. 겹치는 창을 각각 세어 **한 블록이 수십 건으로 부풀었다**
   (헤더 207건 · 소스 191건으로 보였다). 창을 병합하도록 고치니 헤더 148 · 소스 182 건이고,
   **길이순 정렬**이 생기면서 비로소 "무엇부터 볼지" 가 보였다.

**가장 큰 것들은 대부분 정당했다.** `deque.h`↔`list.h` 의 31줄은 레이스 검출기 래퍼의 기계적 전달
(`SW_SCOPED_RACE_WRITE()` 후 `Base::` 호출)이고, RHI 커맨드 컨텍스트·리소스 헤더의 15~26줄은
**백엔드가 같은 인터페이스를 구현하는 선언**이다. 매크로로 접거나 억지로 합치면 읽기만 나빠진다.

**진짜는 같은 파일 안에 있었다 — `D3D12RHIResourceBindless.cpp`.**

- **할당이 네 벌.** `registerBindlessTexture` · `registerBindlessResource` · `registerBindlessUav` ·
  `registerBindlessTextureUav` 가 "프리리스트에서 꺼내고, 없으면 용량을 확인하고 카운터를 올린다" 는
  **같은 열두 줄**을 각자 갖고 있었다(네 사본이 바이트까지 같은 것을 확인했다).
  `acquireBindlessIndex( lock )` 하나로 모았다.
- **해제가 두 벌.** SRV/CBV 쪽과 UAV 쪽이 "슬롯을 비우고 펜스 뒤 회수에 맡긴다" 를 각자 적고 있었다.
  그 안에는 **이미 빈 슬롯을 다시 돌려주면 같은 인덱스가 두 리소스에 발급된다** 는 검사가 있다 —
  한쪽이 그 검사를 잃으면 디스크립터가 겹친다. `releaseBindlessRecord( registry, index )` 로 모았다.

**잠금을 인자로 받는다.** `acquireBindlessIndex` 는 스스로 잠그지 않고 **호출자가 이미 쥔 잠금을 받는다** —
인덱스를 집는 것과 **그 자리에 뷰를 만드는 것**이 한 임계 구역이어야 하기 때문이다. 여기서 잠갔다 풀면
그 틈에 다른 스레드가 같은 인덱스를 받는다. 인자로 받으면 그 전제가 호출부에도 보이고 `SW_ASSERT` 로도
확인된다. 등록부 구분은 bool 대신 `BindlessRegistry` enum 이다(헤더에서 장치 타입이 불완전해도 쓸 수 있다).

**검증 — 이 자리는 픽셀까지 본다.** 디스크립터가 어긋나면 테스트는 초록인데 화면만 깨진다(이 저장소가
겪은 그대로다). 정적 씬은 바이트까지 결정적이므로 **앞서 검증해 둔 기준 sha `d2c61c1f8e57cf2d` 와
직접 대조**했다 — dx12·vulkan·dx11·gl **네 백엔드 모두 일치**. 여기에 Debug·Shipping·ASan 빌드(경고 0) ·
`-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 · 린트 17/17 · 백엔드 교체 실기동(DX12→OpenGL) 오류·경고 0건 ·
에디터 실기동 `[Error]` 0건.

> **다음에 이 조사를 다시 할 때**: 창을 병합하고 **길이순으로** 볼 것. 그리고 헤더의 상위권은
> "백엔드가 같은 인터페이스를 구현한 선언" 이 대부분이라는 것을 먼저 알고 시작할 것 — 매번 같은 것이
> 올라온다.

### 2026-09-19 (기각했던 넷을 구조를 바꿔 다시 풀었다 — 그러다 실제 버그가 하나 나왔다)

앞 항목에서 "합치려면 구조를 다시 세워야 한다" 며 미뤄 둔 넷을 **구조 변경을 허용하고** 다시 열었다.
미뤄 둔 이유가 비용이었지, 합치지 말아야 할 이유는 아니었기 때문이다.

**1) 그래프 패널 둘은 사실 같은 패널이었다 — 그리고 한쪽이 조용히 틀려 있었다.**

`AnimationGraphPanel` 과 `DialogueGraphPanel` 은 노드·링크 목록과 캔버스를 들고, JSON 으로 오가고,
노드를 옮기면 dirty 를 찍는다. 그 뼈대가 두 벌이었고 **복사본이 갈라져 있었다**:

```
애니메이션: bChanged = ( x 가 다르다 ) || ( y 가 다르다 );
대화      : bChanged = ( x 가 다르다 ) && ( y 가 다르다 );   ← 버그
```

`&&` 쪽에서는 노드를 **정확히 수평으로만**(또는 수직으로만) 옮기면 두 축 중 하나가 그대로라
"안 움직였다" 가 되어, **그 레이아웃 변경이 dirty 로 잡히지 않고 사라졌다.** 캔버스 정렬이 축 하나만
움직이는 일을 흔하게 만들므로 드문 경우도 아니다.

판단을 `EditorSessionPolicy::hasNodeMoved` 로 올려 **테스트가 지키게** 했고(그 클래스는 순수 정책이라
단위 테스트가 이미 있다), 뼈대는 `EditorGraphDocumentPanel<AssetType>` 으로 모았다. 노드·링크 타입은
자산의 목록 멤버에서 **추론**하므로 자산이 별칭을 노출할 필요가 없다. 패널마다 다른 것은 두 문자열
(Undo 이름·합치기 키)과 `ensureDefaults()` 뿐이다. 변이(`||` → `&&`)로 새 케이스가 무는 것을 확인했다.

**2) JSON 배열을 도는 네 줄이 아홉 곳에 있었다.** "배열을 얻고 · 배열인지 묻고 · 개수를 세고 ·
원소가 객체가 아니면 건너뛴다" — `forEachObjectInArray( parent, "이름", 람다 )` 하나로 모았다
(`JsonDocument.h`). 그중 **하나라도 `isObject` 검사를 빠뜨리면 망가진 파일 하나로 그 자산이 통째로
깨진다** — 이제 새 자산이 그 실수를 할 자리가 없다. 다섯 곳을 옮겼다(그래프 자산 둘 · 스프라이트 클립 ·
시퀀스). 남긴 곳 하나: `TextureImportConfig` 의 규칙 배열은 **객체가 아닌 원소도 처리**하고 있어
그대로 옮기면 동작이 바뀐다.

> **그 한 곳은 유지하기로 정했다 (2026-09-19).** 이 설정은 **에디터에서만 쓰이는, 손으로 적는
> 파일**이다 — 런타임·배포 경로가 읽지 않는다. 대신 그 자리의 **날은 알고 두자**: 객체가 아닌
> 원소는 필드를 하나도 못 읽어 **기본 규칙**이 되는데, 기본 규칙은 include 목록이 비어 있어
> `findMatchingRule` 의 검사를 전부 통과한다 — **무엇에나 매칭되는 규칙**이 되고, 그 함수는
> "첫 매칭이 이긴다" 라서 **그 뒤의 규칙이 전부 덮인다.** 다만 그렇게 되면 모든 텍스처가 그 즉시
> 기본 설정으로 임포트되므로 적은 사람이 바로 알아챈다 — 조용히 틀리는 실패가 아니다.
> 코드 옆에도 같은 설명을 적어 두었으니 다음 훑기에서 후보로 올라오면 여기를 보고 넘길 것.

**그리고 그 날에 손잡이를 달았다 (같은 날).** 동작을 바꾸지 않고 **결과만 소리 나게** 했다 —
`findShadowingRuleIndex()` 가 "조건 없이 모두 매칭되는 규칙이 목록 중간에 있는가" 를 답하고,
로드 끝에서 그 규칙의 번호·이름과 **죽은 규칙 수**를 경고로 남긴다.

판단의 경계가 핵심이다: **"조건이 없다" 가 아니라 "조건이 없는데 뒤에 뭔가 더 있다"** 를 본다.
맨 끝의 캐치올(`Fallback_Default`)은 이 설정의 **정상적인 쓰임**이라, 그것까지 짖으면 멀쩡한 설정마다
경고가 떠서 아무도 읽지 않게 된다. 실제 `Config/Editor/TextureImportConfig.json` 은 캐치올이 맨 끝
하나뿐이라 **경고가 뜨지 않는다**(에디터 실기동 `[Warning]` 0건으로 확인).

변이 둘로 양쪽을 확인했다 — 마지막 규칙까지 보게 하면(오탐) `TrailingCatchAllRuleIsNotReported` 가,
아무것도 못 찾게 하면(미탐) `LenientRuleParsingIsKeptButShadowingIsReported` 가 잡는다. 뒤 케이스는
관대한 파싱 자체도 못박으므로, 누군가 엄격하게 바꾸면 그 케이스가 먼저 말해 준다.

**3) 크래시 경로의 관문은 간접 호출 없이 합칠 수 있었다.** 앞에서는 "심볼화 본체는 플랫폼마다 다르고,
크래시 경로에 델리게이트를 끼우는 것 자체가 위험" 이라 기각했다. 그런데 합쳐야 하는 것은 **본체가
아니라 관문**이었다 — 빈 스택 문자열과 "심볼이 잠겼으면 주소만 찍고 빠진다" 는 규칙. 둘을
`kEmptyCallStackText` · `formatRawCallStackFrames()` 로 내리니 **호출부가 오히려 짧아졌고**
간접 호출은 생기지 않았다. 리터럴이 하나가 되어 로그를 grep 하는 쪽도 철자를 하나만 알면 된다.

**4) 샘플러는 객체가 아니라 *조리법*을 공유하면 됐다.** 엔진 기본 샘플러와 에디터 ImGui 샘플러가
같은 아홉 줄이었지만, 합치면 **없는 결합**이 생긴다 — 엔진 쪽이 씬 텍스처를 위해 비등방으로 가면
UI 폰트가 따라간다. 그래서 공유하는 것을 객체에서 **이름 붙은 조리법**으로 바꿨다:
`VulkanRHISamplerRecipe::linearClamp()`. 둘 다 "선형 + 가장자리 고정" 을 원해서 값이 같았을 뿐이고,
한쪽이 다른 성질을 원하면 **이 조리법을 부르지 않으면 된다.** 실제로 달랐던 한 곳(엔진만 세우던
`borderColor`)은 `CLAMP_TO_EDGE` 에서 쓰이지 않는 값이라 조리법에서 뺐다.

> **남는 기각은 include 묶음 10건뿐이다.** 같은 헤더를 여러 파일이 include 하는 것은 중복이 아니다 —
> 8줄 창을 보는 탐지기가 걸러내지 못할 뿐이고, 코드에 고칠 것이 없다.

**검증.** Windows: Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 ·
린트 17/17 · 에디터 실기동(테스트 씬·패널 전부 열기) `[Error]` 0건 · **창 33개 · 내용 없는 패널 0개**,
두 그래프 패널 모두 내용 있음(Animation vtx=1193 · Dialogue vtx=966). 리눅스(WSL): 전체 빌드 경고 0 ·
`-L nogpu` 7/7.

### 2026-09-19 (복붙 후보 21건을 끝까지 따라갔다 — 다섯은 합치고, 여섯은 이유를 적고 남겼다)

앞의 두 항목과 같은 조사(공백·주석을 지운 연속 8줄 창 해시)에서 나온 **나머지를 전부** 처리했다.
21건 중 **10건은 include 묶음**이라 중복이 아니고, 남은 11건을 하나씩 봤다.

**합친 것 다섯.**

| 자리 | 무엇이 같았나 | 왜 위험했나 |
|---|---|---|
| `SerializerUtil` · `JsonSerializer` · `XmlSerializer` | `resolveHandlerTypeName`(13줄) · `isOwnedPointerElementType` | 핸들러 조회 규칙·소유 포인터 판정이 **포맷마다 갈릴 수 있었다** |
| `ComponentPtr` · `GameObjectPtr` | "매니저를 어디서 얻나" 8줄 | 지연 해석의 핵심 계약이다 — 한쪽만 바뀌면 두 핸들이 **다른 씬**을 본다 |
| `RenderPassResource` · `RenderPipelineResource` | XML 읽기·쓰기 배관(≈24줄) | 세 번째 렌더 리소스를 넣을 때 또 복사해야 했다 |
| `InputManagerWin32` · `InputManagerX11` | `registerPlatformGamepads` 전체 | 슬롯 수·0번 캐시·콜백 연결은 **엔진 정책**인데 플랫폼마다 한 벌씩 있었다 |
| `EditorViewportToolbar` · `InspectorComponentManager` | 기즈모 라디오 + Local 체크박스 | 값 `0`·`1`·`2` 가 양쪽에 숫자로 박혀 있어, 모드를 더하면 **두 화면이 서로 다른 모드**를 가리킨다 |
| `D3D11RHIResourcePipeline` · `D3D12RHIResourcePipeline` | 정점 속성 → DXGI 포맷 판단 | 성분 수를 하나 더하면 **그 백엔드만 정점이 어긋난다**(이 저장소가 여러 번 겪은 모양) |

배관을 놓을 자리를 고르는 데도 규칙이 있었다. 렌더 리소스의 XML 배관은 자연스러운 자리가
`XmlSerializer`(직렬화)나 `ResourceUtil`(경로)인데 **둘 다 안 된다** — 직렬화는 티어 2 라 티어 4 인
`Resource` 를 못 보고(`Source/Engine/README.md` 티어 표), `ResourceUtil` 은 헤더에 "경로 I/O 만
담당한다" 고 못박혀 있다. 그래서 쓰는 쪽 옆에 뒀고, 그 이유를 파일 머리에 적었다.

**합치지 않은 것 여섯 — 이유와 함께 남긴다.** (다시 제안하기 전에 여기를 볼 것.)

- **`AnimationGraphPanel` · `DialogueGraphPanel` 의 `applyDocumentText`** — 11줄이 글자까지 같지만,
  그 줄들이 **패널마다 타입이 다른 멤버**(`_listNode`·`_listLink`)를 만진다. 공유하려면 인자를
  넷~여섯 개 받아야 해서 **없애는 줄보다 늘어나는 줄이 많다.** 제대로 하려면 두 패널의 소유 구조를
  `EditorGraphDocument` 같은 믹스인으로 다시 세워야 하는데, 에디터 패널은 **단위 테스트가 없어**
  시각 검증밖에 없다 — 2절의 "EditorContext 소유 구조를 더 쪼개지 않는다" 와 같은 판단이다.
- **`AnimationGraphAsset` · `DialogueGraphAsset` 의 링크 파싱** — 같은 것은 "JSON 배열을 돌며 객체만
  고른다" 는 **여섯 줄짜리 관용구**이고, 안에서 채우는 필드는 완전히 다르다(`_fromNode` vs `_fromPin`).
- **`PosixCallStackCapture` · `WindowsCallStackCapture` 의 `symbolizeFrames`** — 앞부분 가드(빈 스택 ·
  `try_lock` 실패 시 주소만)는 같지만 본체는 완전히 다르다(DbgHelp vs `backtrace_symbols`).
  **크래시 경로라 간접 호출을 끼워 넣는 것 자체가 위험**이다. 대신 Windows 에만 있던 "왜 try_lock 인가"
  설명을 POSIX 쪽에도 적어, 다음 사람이 한쪽만 바꾸지 않게 했다.
- **엔진 Vulkan 기본 샘플러 · 에디터 ImGui 샘플러** — 지금 값이 같지만 **우연이다.** 엔진 쪽은 씬
  텍스처용이라 나중에 비등방 필터링으로 갈 수 있고, ImGui 폰트·아이콘이 그 변화를 따라가면 안 된다.
  합치면 **없는 결합을 만드는 셈**이다. 그 의도를 에디터 쪽 주석에 적었다(`borderColor` 를 세우지
  않는 것도 의도다 — 주소 모드가 CLAMP_TO_EDGE 라 그 값은 쓰이지 않는다).
- **include 묶음 10건** — 같은 헤더를 여러 파일이 include 하는 것은 중복이 아니다. 탐지기가 8줄 창을
  보기 때문에 걸릴 뿐이고, 다음에 이 조사를 다시 할 때도 같은 것이 나온다.

**검증.** Windows: Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 ·
린트 17/17 · 에디터 실기동(테스트 씬 · 패널 전부 열기) `[Error]` 0건 · **창 33개 · 내용 없는 패널 0개**.
리눅스(WSL): 전체 빌드 경고 0 · `-L nogpu` 7/7 — 플랫폼 입력·창 쪽 변경이 실제로 컴파일되는 것까지 봤다.

### 2026-09-19 (창을 다시 만드는 절차가 세 벌이었고, 기반의 것이 창을 숨겼다)

같은 복붙 조사에서 나온 두 번째 자리다. `Win32Window::recreate` 와 `X11Window::recreate` 가 같은
순서를 각자 적고 있었다 — 위치 저장 → 깃발 세우기 → `destroy` → 닫힘 깃발 내리기 → 제목 변환 →
`initializeWindow` → 표시 상태 복원 → 깃발 내리기 → 복원 위치 초기화. 플랫폼이 정말로 다른 것은
**두 줄뿐**이었다(지금 위치를 어떤 API 로 묻는가, "알아서" 위치가 무슨 값인가).

**그리고 세 번째 사본이 기반 클래스에 있었다 — 그것만 모자랐다.** `IWindow::recreate` 는

- `_bRecreating` 을 **세우지 않는다** → 다시 만드는 동안의 리사이즈·닫기 통보가 그대로 새어 나간다.
- **보이던 창을 다시 보이게 하지 않는다** → 그 길로 들어온 창은 다시 만든 뒤 **숨은 채로 남는다.**

이 함수를 부르는 곳은 하나다: `RHI::applyPendingChange` — **백엔드 교체**다(Windows 의 OpenGL 은
픽셀 포맷을 한 번만 정할 수 있어 창을 새로 만들어야 한다). 즉 기반 구현을 쓰는 플랫폼이 하나라도
생기면 GL 로 바꾸는 순간 화면이 사라진다. 지금은 Win32·X11 이 재정의로 가려 주고 있어서 증상이
없었을 뿐이다 — **가려 주는 구조가 곧 문제였다.**

**절차를 기반에 한 벌만 두고**, 플랫폼은 훅 둘만 구현한다:
`captureRestorePosition()`(지금 위치 담기)과 `clearRestorePosition()`("알아서" 값으로 되돌리기).
기본 구현은 아무것도 하지 않으므로, 위치 복원을 지원하지 않는 플랫폼은 그냥 두면 된다.
`_bRecreating` · `_restoreX` · `_restoreY` 도 절차를 따라 `IWindow` 로 올라갔다(플랫폼 메시지
처리기가 깃발을 읽는 것은 그대로다 — 그쪽은 정말로 플랫폼 코드다). macOS 는 예전처럼
`recreate()` 를 재정의해 `false` 를 돌려주고, **절차를 다시 적지 않는다.**

회귀 케이스 `WindowTest.RecreateKeepsVisibilityAndSize` 를 그 자리에 뒀다 — 보이던 창을 다시 만들면
여전히 보이고 크기도 같아야 한다. 변이(표시 복원 한 줄 제거 = 옛 기반 구현과 같은 상태)로 확인했다.
지원하지 않는 플랫폼에서는 `recreate()` 가 false 를 주므로 건너뛴다.

**진짜 호출부로도 확인했다.** `-gv_rhiSwapAtFrame=30 -gv_rhiSwapTo=3`(DX12 → OpenGL, Windows 에서
창 재생성이 **실제로** 일어나는 조합)으로 90프레임을 돌려 오류·경고 0건이다.

**검증.** Windows: Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2
(`WindowTest` 4/4 포함) · 린트 17/17 · 에디터 실기동 `[Error]` 0건 · 백엔드 교체 실기동 0건.
**리눅스(WSL)**: 전체 빌드 경고 0 · `-L nogpu` 7/7 — X11 쪽 변경이 실제로 컴파일되는 것까지 봤다.

### 2026-09-19 (세 포맷이 같은 스무 줄을 갖고 있었고, 그 중 한 줄만 달랐다)

복붙 탐지를 `Source` 전체에 돌렸다(공백·주석을 지운 연속 8줄 창을 해시해 파일을 넘나드는 것만 봤다).
22건이 나왔고 대부분은 include 묶음이거나 **플랫폼마다 정말로 다른 코드**였다. 하나가 달랐다:
`JsonSerializer::deserializeVersioned` 와 `XmlSerializer::deserializeVersioned` 가 **주석까지 글자
그대로 같았다.** 누가 한쪽을 고치고 다른 쪽에 붙여 넣은 흔적이다.

**나란히 놓고 보니 Binary 에 세 번째 사본이 있었고, 거기서 한 줄이 달랐다.**

```
JSON·XML : runSchemaMigrateStep( …, outVersion != currentVersion, … );
Binary   : runSchemaMigrateStep( …, outVersion != currentVersion || listOrphan.empty() == false, … );
```

그 한 줄은 **"버전은 같은데 모르는 필드(orphan)만 있을 때 migrate 없이 통과시킬 것인가"** 를 정한다.
헤더(`SchemaMigrate.h`)는 조건 없이 *"migrate 가 nullptr 인데 버전 불일치나 orphan 이 있으면 false"*
라고 적어 두었는데, **실제로는 포맷마다 답이 달랐다.** 실측으로 확정했다 — 같은 데이터를 세 포맷으로
넣고 `currentVersion` 을 같게 준 결과:

| 포맷 | 결과 |
|---|---|
| Binary | `false` (경고: `schema version 1 -> 1 with no migrate callback`) |
| JSON | **`true`** — orphan 을 버리고 통과 |
| XML | **`true`** — orphan 을 버리고 통과 |

**절차를 하나로 합쳤다.** `runVersionedDeserialize` 가 레거시 스테이징 · soft 역직렬화 두 벌 ·
버전 확정 · `runSchemaMigrateStep` 을 맡고, 포맷은 **본문을 읽는 람다 하나와 두 개의 선택**만 넘긴다:
`SchemaVersionSource`(버전이 스트림 머리에 있나, 본문 안에 있나)와 `SchemaOrphanPolicy`(orphan 만
있을 때 거절하나, 무시하나). bool 인자 대신 이름 붙은 enum 인 이유는 호출부 한 줄만 읽어도 그 포맷이
무엇을 고른 것인지 보이게 하려는 것이다.

**동작은 한 톨도 바꾸지 않았다 — 일부러다.** 텍스트를 Binary 처럼 엄격하게 바꾸면 **모르는 필드가
하나만 있어도 씬·프리팹·머티리얼이 통째로 로드에 실패한다.** 그 결정은 이 리팩터가 혼자 내릴 것이
아니다. 대신 차이에 이름을 주고 테스트로 못박았다
(`ReflectionSerializationTest.OrphanOnlyPolicyDiffersByFormat`) — 그 케이스가 깨지면 정책을 바꾼 것이다.

> **결정이 필요한 질문.** 텍스트 포맷의 orphan 관대함은 의도인가?
> - **의도라면** `SchemaMigrate.h` 의 계약 문구를 포맷별로 나눠 적어야 한다(지금은 Binary 만 설명한다).
> - **사고라면** 텍스트를 `Reject` 로 바꾸는 순간 **기존 콘텐츠가 로드되지 않을 수 있다** — 프로퍼티를
>   지운 적이 있는 에셋이 그대로 걸린다. 바꾼다면 마이그레이션 계획과 함께.

변이 둘로 확인했다: orphan 정책을 무시하게 하면 새 케이스가 잡고, 버전 출처 구분을 없애면 기존
케이스 셋(`BinaryVersionHeaderTest` · `FieldTypeChangeAndTextVersioned` ·
`VersionedDeserializeFailsWithoutMigrate`)이 잡는다.

**검증.** Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 · 린트 17/17 ·
`ReflectionSerializationTest` 31/31 · 에디터 실기동 `[Error]` 0건.

### 2026-09-19 (바인딩 종류 하나를 더하려면 여섯 곳이었다 — 그리고 넷은 조용히 틀렸다)

1-0e 의 판단 기준("하나 더하려면 몇 곳을 고쳐야 하는가")으로 `Source` 를 다시 훑어, 같은 enum 을
여러 파일이 `switch` 하는 자리를 세어 봤다. 대부분은 정당했다 — RHI 백엔드별 상태 변환처럼 **원래
백엔드마다 다른 것**이다. 하나가 달랐다: `BindingKind`.

**하나를 더하려면 여섯 곳이었다.** 열거자 · 충돌 검사의 슬롯 수 switch · 평가 switch · 저장 switch ·
로드의 문자열 if/else 사슬 · 그리고 그 전부를 **손으로 나열한 테스트**(`SaveAndLoadAllBindingKinds`).
게다가 세 switch 에 전부 `default:` 가 있어 **빠뜨려도 아무도 말해 주지 않았다**:

| 빠뜨린 곳 | 예전에 일어나는 일 |
|---|---|
| 충돌 검사 | 슬롯 수 0 — 새 종류를 못 본 채 지나간다. 같은 키를 두 번 걸어도 조용하다 |
| 저장 | `default: break` — `kind` 특성조차 없이 저장돼 **그 바인딩이 파일에서 사라진다** |
| 로드 | 모르는 이름이 조용히 레거시 단일 슬롯 경로로 떨어진다 |
| 평가 | 그 액션이 **영원히 발동하지 않는다** |

**표 하나로 모았다.** `kArrBindingKindTraits` 가 (종류 · XML 이름 · 충돌 슬롯 수)를 들고,
`BindingKinds::toName/fromName/getConflictSlotCount` 가 그것을 읽는다. 충돌 검사의 switch 는
통째로 사라졌고, 저장은 이름을 표에서 가져오며(예전에는 종류마다 리터럴이 있었고 **읽는 쪽에도 같은
리터럴이 따로** 있었다 — 한쪽만 고치면 파일이 조용히 왕복하지 않게 된다), 로드의 if/else 사슬은
`fromName` + switch 가 됐다.

**컴파일러에게 맡기려다 집 규칙을 만났다.** 처음에는 세 switch 의 `default:` 를 지워 `-Wswitch` 가
빠진 종류를 짚게 하려 했는데, 이 저장소는 `-Wswitch-default` 가 켜져 있어 **모든 switch 에 `default:`
를 요구한다**(`-Wno-covered-switch-default` 가 그 짝이다). 규칙을 뒤집지 않고 그 자리를 **소리 나게**
바꿨다 — 저장·로드는 `SW_LOG_ERROR` 로 무엇을 빠뜨렸는지 이름과 함께 남기고, 매 프레임 도는 평가는
로그 대신 `SW_ASSERT` 로 개발 빌드에서 멈춘다. 컴파일 시점의 그물은 표의 `static_assert` 가 맡는다.

**그물 둘을 변이로 확인했다.**

- 열거자만 늘리고 표에 줄을 안 더하면 → **컴파일 오류**(static_assert, 메시지가 무엇을 하라고 말한다).
- 표 한 줄을 복사해 이름을 안 고치면 → **테스트 실패**. `static_assert` 는 줄 수만 세므로 이건 못 잡는다.
  새 케이스 `BindingKindTableCoversEveryKind` 가 `Count` 까지 돌며 (이름이 있는가 · `fromName(toName(k))`
  이 되돌아오는가 · 이름이 겹치지 않는가 · 충돌 슬롯 수가 배열 4칸을 넘지 않는가)를 본다.
  **이 케이스는 종류를 더하는 순간 자동으로 적용된다** — 손으로 나열하는 옛 케이스와 다른 점이다.

**파일 포맷은 그대로다.** XML 이름 아홉 개를 글자 하나 바꾸지 않고 표로 옮겼고, `kind="single"` 이
레거시 단일 슬롯 경로로 떨어지던 동작도 그대로 뒀다(그쪽이 `source`/`key`/`button` 을 읽는다).

**리플렉션을 쓰지 않은 이유.** 같은 폴더의 `KeyCodes`·`MouseButtons` 는 리플렉션 등록부로 이름을
얻는다. 여기서는 (1) XML 이름이 열거자 이름과 **일부러 다르고**(파일 포맷이다), (2) 등록부는 엔진
서비스가 묶여 있어야 답한다 — 묶이지 않은 채 저장하면 `KeyCodes::toName` 이 "Unknown" 을 돌려주듯
종류 이름도 "Unknown" 이 되어 **저장이 조용히 망가진다.** 저장 경로를 서비스 바인딩에 기대게 둘
이유가 없다.

**검증.** Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 · 린트 17/17 ·
`ActionMapTest` 18/18 · 에디터 실기동 `[Error]` 0건.

### 2026-09-19 (리눅스에서 빌드 취소가 아무것도 하지 않았다 — POSIX `Process` 를 fork/exec 으로)

`PosixProcess` 는 `popen` 위에 서 있었다. `popen` 은 **자식 pid 를 주지 않으므로** 한 헤더가
약속한 것을 이 구현은 절반만 지켰다: `terminate` 는 언제나 false, `isRunning` 은 자기 깃발,
`getProcessId` 는 언제나 0, 소멸자는 `pclose` 라 **자식이 끝날 때까지 막혔다.** 그 값을 실제로
쓰는 곳이 있다 — `ModuleCompiler::cancel()` 은 **전적으로** `terminate` 가 자식을 죽여 파이프가
닫히는 것에 기댄다(읽기 루프는 취소 깃발을 보지 않는다). 즉 **리눅스에서는 빌드 취소가 아무것도
하지 않았다.**

`fork` + `pipe` + `dup2` + `execl("/bin/sh", "sh", "-c", …)` 로 바꿔 자식을 직접 들게 했다.
이제 두 구현이 같은 일을 한다.

**세 가지가 그냥 되지는 않았다.**

1. **`isRunning` 이 자식을 거두면 안 된다.** 가장 쉬운 구현은 `waitpid(WNOHANG)` 인데, 그것은 끝난
   자식을 **그 자리에서 거둬 버린다** — 뒤이어 부르는 `waitForExit` 은 줄 것이 없어 -1 을 돌려준다.
   `waitid(P_PID, …, WEXITED | WNOHANG | WNOWAIT)` 로 **묻기만** 한다. 변이로 확인했다: `waitpid`
   판으로 바꾸면 새 케이스가 종료 코드에서 즉시 걸린다.

2. **`terminate` 는 프로세스 그룹째 죽인다.** 자식에서 `setpgid(0,0)`, 부모에서도 한 번 더 부른다
   (누가 먼저 도는지 정해져 있지 않다). 셸이 낳은 손자 — 빌드라면 진짜 컴파일러 — 까지 멈춰야
   취소가 끝나기 때문이다. 대신 자식이 터미널을 읽을 수 없게 되는데, 이 클래스의 용도(명령을 돌리고
   출력을 받는 것)에는 그쪽이 맞다.

3. **하마터면 에디터를 통째로 죽일 뻔했다.** `cancel()` 은 UI 스레드에서 부르고 빌드 스레드는 같은
   객체에서 `waitForExit` 을 돈다. `waitForExit` 이 거둔 뒤 `_processId` 를 0 으로 만드는데,
   `terminate` 가 검사와 사용 사이에 그 0 을 읽으면 `kill( -0, SIGKILL )` 이 된다 — 그것은
   **우리 자신의 프로세스 그룹에 SIGKILL** 이다. pid 를 지역 변수에 한 번만 읽어 창을 없앴다.
   (거둔 뒤 pid 를 놓는 것 자체는 필요하다 — 들고 있으면 소멸자가 언젠가 재사용된 번호로 **남의
   자식**을 거둔다.)

신호로 죽은 자식에는 종료 코드가 없으므로 `waitForExit` 은 셸 규약대로 **128 + 신호번호**를 준다
(SIGKILL 이면 137). 두 구현이 갈리는 곳은 이제 그 한 줄과 "종료 코드를 정해 줄 수 있는가" 둘뿐이고,
헤더에 적었다.

**테스트.** `TerminateProcess` 의 POSIX 건너뛰기를 걷어내 **양쪽에서 돈다.** 이 케이스는 한때 POSIX
에서도 돌았고 *통과했다* — 죽여서가 아니라 `pclose` 가 `sleep 10` 을 10초 기다려 줬기 때문이다.
그 모양이 실제로 재현된다: 죽이지 않도록 변이를 넣으면 **10,006ms 를 쓰고 종료 코드에서 실패한다.**
새 케이스 `IsRunningTurnsFalseWithoutEatingTheExitCode` 는 "곧 false 가 되는가" 와 "묻는 것이 종료
코드를 먹지 않는가" 를 함께 본다.

**부수 수확 — 리눅스에서는 통과할 수 없는 테스트가 하나 있었다.**
`CompressionCodecTest.CodecsRejectSizesTheirLibraryCannotHold` 가 8GiB 에 대해 zlib 이 0 을 준다고
단언했는데, zlib 의 길이 타입 `uLong` 은 **Windows 에서만 32비트**다. 리눅스(LP64)에서는 64비트라
8GiB 가 평범한 크기이고, 그래서 이 줄은 리눅스에서 **언제나 빨갰다**(CI 의 리눅스 잡도 같은 답을
본다). 고친 것은 테스트다 — 코드는 처음부터 `(uLong)-1` 로 플랫폼마다 옳게 재고 있었다. 이제
테스트도 플랫폼을 가르지 않고 **어느 쪽에서도 참인 계약**을 단언한다: 0 이면 "못 담는다" 는 뜻이고,
0 이 아니면 그 값이 진짜 쓸 수 있는 한계여야 한다. 한계 검사를 빼면 잘린 크기(0)의 바운드인 **13**
이 돌아오는데 그것은 둘 중 어느 쪽도 아니다 — Windows 에서 변이로 확인했다. (첫 수정은
`sizeof(unsigned long)` 으로 플랫폼을 갈랐는데, 컨벤션 게이트가 기본 자료형을 막았다. 막힌 김에 보니
**가르지 않는 편이 더 나은 단언**이었다 — 잘린 값을 두 플랫폼 모두에서 잡는다.)

> **함정 (환경) — 번들 `Tools/LLVM/bin/ld.lld` 가 Ubuntu 26.04 에서 실행되지 않는다.**
> 26.04 는 `libxml2.so.16` 만 싣고 `.so.2` 를 주지 않아 링커가 **뜨지도 못한다.** clang 이 남기는
> 말은 `unable to execute command: No such file or directory` 뿐이라 원인이 보이지 않고, 링크는
> 맨 끝에 오므로 수백 개를 다 컴파일한 뒤에 터진다. `SetupLinuxDevEnvironment.py` 가 이제 먼저
> 실행해 보고 이름과 해법을 찍는다. 시스템 lld 를 쓰면 된다 — **셋을 다 줘야 한다**:
> `-DCMAKE_EXE_LINKER_FLAGS` · `-DCMAKE_SHARED_LINKER_FLAGS` · `-DCMAKE_MODULE_LINKER_FLAGS`
> 를 `"--ld-path=/usr/bin/ld.lld"` 로. RHI 백엔드는 MODULE 이라 앞의 둘만 주면 `libRHI_GL.so`
> 에서 똑같이 터진다(실제로 겪었다).

**검증.** Windows: Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 ·
린트 17/17 · 에디터 실기동 `[Error]` 0건. **리눅스(WSL Ubuntu 26.04 · clang 20.1.8): 전체 빌드
경고 0 · `-L nogpu` 7/7 (EngineTest 474 케이스 전부) · `ProcessTest` 5개 중 4통과 + 1건은 Windows
전용 규약이라 건너뜀.** 변이 둘을 리눅스에서 확인했다(죽이지 않기 → 10,006ms 쓰고 실패, 거두는
`isRunning` → 종료 코드 사라짐).

### 2026-09-19 (행렬 곱 둘과 참조 카운트 6N 개 — 게임 스레드를 다시 14% 깎았다)

앞 항목에서 계측을 고치고 나니 다음 자리가 **숫자로** 보였다. 큐브 20,000 · dx12 · Release · 3회 평균.

**1) 로컬 TRS 를 만드는 데 행렬 곱이 두 번 들어갔다.** `makeLocalTRS` 는
`createScale(s) * createFromYawPitchRoll(r) * createTranslation(p)` 였다. 그런데 스케일은 **대각**이고
이동은 **마지막 행뿐**이라 결과가 미리 정해져 있다 — 위 3x3 은 회전 행렬의 각 행에 스케일을 곱한 것,
마지막 행이 곧 위치다. 실수 곱 128번이 **9번**이 된다. `float4x4::createTrs` 로 math 층에 넣었다
(쿼터니언·오일러 두 오버로드). 매 프레임 움직이는 컴포넌트가 전부 지나는 자리다.
→ `GT.Scene.tick.flushTransforms` **1,355 → 908us (-33%)**.

지름길은 "빠른데 값이 다르다" 가 가장 무섭다. 그래서 테스트는 기대값을 손으로 적지 않고 **원래 식
그대로**(S * R * T) 두어 규격이 바뀌면 둘이 같이 움직이게 했다 — 비대칭 스케일 · 세 축 회전 ·
0 이 아닌 이동을 섞어야 행을 바꿔치기한 구현이 걸린다(변이로 확인).

**2) 수집이 매 프레임 후보를 비우고 다시 담았다.** `DrawCandidate` 는 메시·머티리얼·인스턴스의
`shared_ptr` 셋을 든다. `clear()` 가 N×3 번 내리고 `push_back` 이 N×3 번 올린다 — **프레임당 원자
연산 6N 개**인데, 물체가 움직였다고 메시나 머티리얼이 바뀌지는 않으므로 거의 전부 **같은 객체**다.
비우지 않고 제자리에 덮어쓰되, 셋은 **날 포인터로 먼저 비교**해 같으면 대입하지 않는다.
→ `GT.GpuScene.build` **1,667 → 1,414us (-15%)**. 그 중 `collect` 는 606 → 492us 이고 나머지
**약 150us 는 스코프 밖**이던 `clear()` 였다 — 표에서 `build` 와 하위 합의 차이로만 보이던 몫이다.

같은 이유로 **`hasSameBatchKeysAsBuilt()` 에도 스코프를 붙였다**(`GT.GpuScene.build.batchKeys`,
약 206us). 재는 자리가 없으면 아무도 보지 않는다.

**그 대신 생긴 규칙**: 슬롯을 재사용하므로 `DrawCandidate` 의 필드는 **전부** 다시 채워야 한다.
필드를 더하면 수집 루프에도 같이 적을 것. 회귀 케이스(`ReusedCandidateSlotsCarryNoStaleData`)가
앞의 것을 숨겨 **뒤의 것이 앞 슬롯으로 내려오게** 만들고, 그 슬롯이 통째로 바뀌었는지 본다.

> **함정 — 이 케이스는 빌드를 세 번 해야 문다.** 수집 배열과 기준 배열은 끝에서 맞바뀌므로
> 첫 빌드 뒤 수집 배열은 아직 **비어 있다**. 두 번째를 지나야 지지난 값이 돌아오고, 재사용은
> 거기서 처음 일어난다. 두 번만 짓고 검사했을 때는 슬롯을 통째로 무시하는 변이도 통과했다.

**합계(앞 항목 끝 → 지금).**

| | 계측 고치기 전 | 앞 항목 뒤 | 지금 |
|---|---:|---:|---:|
| `GT.Game.update` | 3,259us | 1,919us | 1,944us |
| `GT.Scene.tick` | 1,889us | 1,357us | **908us** |
| `GT.GpuScene.build` | — | 1,667us | **1,414us** |
| `GT.Frame` | 3,688us | 3,304us | **2,570us** |
| 게임 스레드 합 | 6,947us | 5,220us | **4,514us (-35%)** |

**남은 큰 항목은 `GT.Game.update` 1,944us 다.** 호출당 97ns 인데 락도 해시도 아니다 — 큐브마다
슬롯 표 → GameObject → 컴포넌트 벡터 → 컴포넌트로 **포인터를 세 번 쫓는** 캐시 미스다. 이걸 줄이려면
트랜스폼을 배열로 들고 도는 자료 지향 배치로 가야 하는데, 그건 이 크기의 작업이 아니라 따로 세울 일이다.

> **함정 — 움직이는 씬으로는 스크린샷을 비교할 수 없다.** 벤치 씬은 경과 시간으로 흔들려서 **같은
> 빌드를 두 번 돌려도** 30,664 바이트가 달랐다(최대 차 223). 바뀐 빌드와의 차이(33,298 · 224)와
> 구분이 안 된다. 기본(정적) 씬은 **바이트까지 결정적**이라 4백엔드 모두 전후 sha 가 같았다 —
> 렌더 변경의 시각 검증은 정적 씬으로 할 것.

**검증.** Debug·Release·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 2/2 ·
린트 17/17 · 에디터 실기동 `[Error]` 0건 · 정적 씬 스크린샷 dx12·vulkan·dx11·gl **전부 전후 동일**.

### 2026-09-19 (프레임의 절반이 계측 밖에 있었다 — 게임 스레드 26% 단축)

**측정부터 고쳤다.** 표 제목이 "frame breakdown" 인데 `GT.Frame` 은 `EngineLoop::tick` 만 잰다 —
그 앞에서 도는 **게임 모듈 업데이트가 한 줄도 없었다.** `App::run` 에 `GT.Game.update` ·
`GT.Game.fixedUpdate` · `GT.Editor.updateUi` 를 넣고, 깜깜하던 `GT.Scene.tick` 안도
`flushTransforms` · `components` 로 갈랐다. 그러자 그림이 **달라졌다**:

| scope | 계측 전 인식 | 실제(Release, 큐브 20,000, dx12, 60프레임) |
|---|---|---|
| `GT.Game.update` | 없음(안 보임) | **3,259us — 가장 큰 항목** |
| `GT.Scene.tick` | 1,889us (안이 안 보임) | 그 중 `flushTransforms` **1,887us**, `components` **0us** |

**1) 트랜스폼 플러시가 루트마다 벡터를 새로 만들었다.** `flushSceneComponentSubtree` 가 호출마다
`vector` 를 만들고 `reserve(32)` 했는데, 이 함수는 **루트 씬 컴포넌트마다** 불린다 — 큐브 20,000 개면
프레임당 힙 할당 20,000 번이다. 버퍼를 매니저 멤버로 올려 재사용했다(부르는 곳이 게임 스레드 한 곳뿐).
→ `flushTransforms` **1,887 → 1,350us (-29%)**.

**2) 핸들 해석이 호출마다 락 + 해시였다.** `resolveComponent` 는 `findGameObjectById` 로 시작하는데
그것이 매니저 `_mutex` **공유 잠금 + 해시 조회**였다. 핸들은 프레임당 오브젝트 수만큼 풀린다.
실측으로 값을 확정했다 — 벤치를 "미리 푼 포인터" 로 바꿔 보니 `GT.Game.update` 가 3,259 → 1,050us,
즉 **해석에만 프레임당 2.2ms(호출당 110ns)**.

id 는 단조 증가라 **밀집**하므로 배열이면 된다. 다만 늘리면 주소가 옮겨져 읽는 쪽과 부딪히므로
**절대 재배치되지 않는 청크 표**로 뒀다(`LinearAllocator` 의 블록 표와 같은 이유). 쓰기는 전부 기존
매니저 락 안에서 맵과 **같은 자리**에서 하고, 읽기는 원자적 슬롯 로드 하나다. 표가 다루지 못하는
범위 밖 id 는 맵으로 떨어진다 — **표는 빠른 길이지 유일한 진실이 아니다.**
→ `GT.Game.update` **3,259 → 1,919us (-41%)**.

**합계(같은 벤치 3회 평균).**

| | 전 | 후 |
|---|---:|---:|
| `GT.Game.update` | 3,259us | **1,919us** |
| `GT.Scene.tick` | 1,889us | **1,350us** |
| `GT.Frame` | 3,688us | **3,256us** |
| 게임 스레드 합(둘) | 6,947us | **5,175us (-26%)** |

**되돌린 것 — 숫자가 없어서.** `resolveAndTickItem` 이 파도에 담아 둔 포인터를 그냥 쓰게 바꿔 봤지만
(파도는 구조가 바뀔 때마다 다시 세워지므로 안전하다) **이 벤치에서 `GT.Scene.tick.components` 가 0us**
였다 — 틱하는 컴포넌트가 없어 이득을 잴 수 없었다. 되돌리고 그 사실을 코드 주석에 남겼다.
틱이 실제로 도는 워크로드가 생기면 그때 숫자와 함께 다시 본다.

**부수 수확 — `FindLlvmBin` 의 후보 경로가 한 칸씩 어긋나 있었다.** WSL 빌드를 같이 돌려 보다
`build/vcpkg_installed` 를 두 트리플릿이 공유한다는 것을 알게 됐고(동시에 돌리면 Windows 쪽 설치가
지워진다), 복구하다 이 버그를 밟았다: 후보 루트가 이 파일 기준 세·네 단계 위만 있어 **저장소 루트가
목록에 없었다.** 평소에는 `CMAKE_SOURCE_DIR` 이 가려 주지만 **vcpkg 포트 빌드에서는 그것이 vcpkg 의
scripts 폴더**라, PATH/ENV 를 비운 채 도는 그 자리에서 저장소의 `Tools/LLVM` 을 영영 못 찾는다.
`compiler-file-hash-cache` 가 한 번 지워지면 그 뒤로 configure 가 서지 않는다 — 실제로 그렇게 막혔다.

> **WSL 과 Windows 빌드는 동시에 돌리지 말 것.** 둘이 `build/vcpkg_installed` 를 공유한다.

**검증.** Debug·Release·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 양쪽 2/2 ·
린트 17/17 · 에디터 실기동 `[Error]` 0건.

### 2026-09-19 (모듈이 자기 에셋 종류를 올릴 수 있게 — 그리고 내려놓는 규칙을 못박았다)

1-0e 의 "모듈이 확장할 수 있는 것" 을 확인해 보니 **창구는 이미 열려 있었다.** `ResourceManager` 는
게임 모듈에도 노출되는 서비스이고(`gameAllowed=1`), `IAssetCache.h` 는 내부 전용 가드가 없어 모듈이
그냥 include 하면 된다. 실제로 막고 있던 것은 **수명**이었다:

- 등록부는 **포인터만** 든다. 모듈 DLL 이 내려가면 포인터도 가상 함수 표도 같이 사라진다.
- 그런데 **내리는 창구가 없었다** — 올릴 수는 있는데 내려놓을 수가 없었다.

`unregisterAssetCache()` 를 넣어 짝을 맞췄고, 두고 간 캐시는 종료가 **이름으로** 경고하게 했다.
그 진단이 죽은 포인터를 건드리지 않도록 **등록 시점에 이름을 복사해 둔다** — `getAssetKindName()` 을
그때 부르면 진단 자체가 죽는다. 같은 이유로 `findAssetCache` 도 사본으로 비교한다.

**테스트 2건 신규**(`AssetCacheRegistryTest`): 내린 것만 빠지는가(두 번 내려도·널을 내려도 조용한가) ·
두고 간 것을 이름으로 말하는가(그리고 **내장 셋만 있을 때는 조용한가** — 반대 방향을 안 보면 "항상
우는" 구현도 통과한다). 변이로 확인했다(erase 제거 · 내장 제외 조건 제거).

**안 한 것과 그 이유.** 모듈이 **렌더 패스**를 등록하는 것은 하지 않았다. 그러려면 패스 실행
컨텍스트(`FramePassContext` · 커맨드 리스트 · 트랜지언트 풀)를 모듈 경계 밖으로 내야 하는데, 그것은
RT 안전성 계약까지 함께 내보내는 일이다 — 쓰는 모듈이 하나도 없는 지금 그 표면을 여는 것은
"안 쓰이는 경로는 조용히 썩는다" 를 새로 만드는 일이다. 1-0e 에 근거와 함께 남겼다.

**검증.** Debug·Shipping 빌드(경고 0) · `-L nogpu` 양쪽 7/7 · `-L hostgpu` 양쪽 2/2 · 린트 17/17.

### 2026-09-19 (도구를 고쳐도 산출물이 그대로였다 — Tools/ReflectionParser 훑기)

**파서가 자기 자신을 입력으로 세지 않았다.** `isUpToDate` 는 입력 헤더 · 템플릿(`.tpl`) ·
`ReflectBuiltins.xxx` · `AnnotationMeta.txt` 의 시간을 보는데, 정작 **그것을 조립하는 실행 파일**은
보지 않았다. 그래서 `CodeGenerator` 나 `AstVisitor` 나 `AnnotationApply` 를 고치고 다시 빌드하면:

- CMake 는 `DEPENDS ... "$<TARGET_FILE:ReflectionParser>"` 로 파서를 **다시 부르고**,
- 파서는 파일마다 "최신" 이라며 **전부 건너뛴다.**

결과는 옛 모양 그대로의 `.gen.cpp`/`.gen.h` 다. 게다가 그 사이에 헤더를 건드린 파일만 새 모양으로
다시 만들어지므로 **한 빌드 안에 두 모양이 섞인다.** 실측으로 확인했다 — 산출물에 표식을 심고
파서보다 과거로 돌린 뒤 다시 돌리면 표식이 그대로 남았다(2.7ms 만에 끝난다. 실제로 파싱하면 500ms).

`getParserTimestamp()`(자기 실행 파일의 mtime, 한 번만 재고 캐시) 를 비교에 넣어 닫았다. CLI 도
CMake 도 바뀌지 않는다 — 도구가 자기 시간을 스스로 안다.

**그리고 그 검사를 하던 유일한 테스트는 한 번도 돈 적이 없었다.** `ReflectionParserTest` 의
파서 호출 케이스는 실행 파일을 `Bin/` 에서만 찾는데, 이 빌드는 파서를 `BuildTools/` 에 둔다 —
그래서 **늘 스스로 건너뛰었다**(백로그 기준선에 "스킵 1건" 으로 적혀 있던 것이 이것이다).
두 자리를 다 보는 헬퍼로 바꿨더니 그 케이스가 실제로 돌기 시작했고(544ms) 통과한다.
**Debug ReflectionTest 의 스킵이 0이 됐다.**

**테스트 1건 신규** — `RegeneratesWhenTheParserItselfIsNewer`: 임시 헤더를 한 번 생성하고, 산출물에
표식을 심은 뒤 **입력(2시간 전) < 산출물(1시간 전) < 파서(지금)** 로 시간을 벌려 다시 돌린다.
표식이 사라져야 한다. 변이(파서 시간 비교 제거)로 확인했다.
**입력도 같이 과거로 보내는 것이 핵심이다** — 안 그러면 "입력이 더 새롭다" 는 이유로 재생성돼
이 검사가 무엇을 보는지 구분하지 못한다(처음에 그렇게 써서 변이가 통과했다).

**결함이 아니라고 판단한 것.** `CodeGenerator` 의 증분 쓰기(내용이 같으면 파일을 건드리지 않는다)는
의도된 것이다 — 그래서 **산출물의 mtime 으로는 "건너뛰었는지" 를 알 수 없다.** 이번에 그것을
모르고 실험하다 한 번 틀린 결론에 도달했다. 판정은 **내용 표식**으로 해야 한다.

**검증.** Debug·Shipping 빌드(경고 0) · `-L nogpu` 양쪽 7/7 · `-L hostgpu` 양쪽 2/2 · 린트 17/17 ·
ReflectionTest Debug 104/104(스킵 0) · Shipping 99/104(스킵 5, 전부 배포본 전용 사유).

### 2026-09-19 (디바이스가 없는 RHI 에 세 자리가 그대로 물었다 — Source/App 훑기)

훑기 표에서 빠져 있던 `Source/App`(2,512줄)을 다른 폴더와 같은 방식으로 훑었다. 결함 넷.

**1) 디바이스가 없는 RHI 에 `getDevice()` 를 묻는 자리가 셋.** `RHI::getDevice()` 는
`return *_device;` 라 디바이스가 없으면 **널 참조**다 — `EngineLoop::shutdown` 은 이미 그 이유로
`hasDevice()` 를 먼저 묻고 있었고, 그 주석에 "이게 없어서 '요청한 백엔드가 이 빌드에 없다' 라는
정상적인 실패가 종료 경로에서 SEGFAULT 로 끝났다" 고 적혀 있다. 그런데 `ModuleHost` 의 세 자리가
그 검사 없이 묻고 있었다:
- `drainRenderWorkers()` — **종료 경로가 반드시 지나간다.** 백엔드 교체가 실패해 디바이스만
  사라진 상태(`BackendSwapController::applyPendingChange` 의 실패 경로)로 앱을 닫으면 그 자리에서 죽는다.
- `createEditorInstance()` · `createGameInstance()` — 초기화 인자로 디바이스를 넘기는 자리라
  **만들기 전에** 없다는 것을 알 수 있다. 이제 로그를 남기고 false 를 돌려준다.

**2) "콜백을 뗀다" 고 적어 두고 둘만 뗐다.** `ModuleHost::shutdown` 은 배수·배치 델리게이트만
떼고 `setOnBeforeReload`/`setOnAfterReload` 로 **모듈마다 건 것**은 그대로 두었다. 그것도 이
객체의 메서드를 가리키고, `App` 은 ModuleHost 를 먼저 지우고 등록부를 나중에 내린다. 지금 순서로는
그 사이에 리로드가 돌지 않아 터지지 않지만, 절반만 떼면 다음 사람은 뗐다고 읽는다.
이름을 두 곳에 적지 않으려고 등록부에 창구를 뒀다 — `LiveReloadManager::clearReloadCallbacks()`
(키트 모듈 이름은 설정에서 오므로 거는 쪽이 다 알지도 못한다). 등록부 자신의 종료도 그 창구를 쓴다.

**3) 훅을 떼는 일이 엉뚱한 조건에 걸려 있었다.** `BackendSwapController::shutdown` 은
`_pEngineLoop == nullptr` 이면 그대로 돌아갔는데, 전역 변수 훅은 그 포인터와 무관하게
`initialize` 가 건다 — 루프 없이 초기화된 경우 죽은 `this` 를 가리키는 콜백이 전역에 남는다.
떼는 일을 조건 밖으로 옮겼다.

**4) 문서.** `ModuleHost.h` 의 리로드 콜백 일곱·바인딩 둘·인스턴스 수명 여섯에 `@brief` 가 없었다.
채웠다. 헤더 7개는 전부 단독으로 선다(`RunHeaderSelfContained.py --filter App`).

**테스트 2건 신규** — `Test/SmokeTest/TestModuleHost.cpp` 의 `ModuleHostTest`(SmokeTest 가 이미 App
모듈 소스를 파일 단위로 가져오는 타깃이라 거기 얹었다. `ModuleCompiler` 는 배포 구성에도 들어간다 —
`App` 의 소스 규칙과 같게 맞췄다):
- `SurvivesAnRhiThatHasNoDevice` — 디바이스 없는 `RHI` 하나로 초기화·재생성 거절·종료를 지나간다.
  **고치기 전 코드로는 프로세스가 죽는다**(변이로 확인).
- `ShutdownDetachesEveryCallbackItRegistered` — 콜백을 떼고 **실제로 리로드를 돌려**(핸들이 바뀌는
  것이 증거다) 불리지 않는지 본다. `clearReloadCallbacks` 를 비우면 깨진다(변이로 확인).
  배포 구성에는 `LiveReloadManager` 자체가 없어 Dev 전용이다.

**검증.** Debug·Shipping 빌드(경고 0) · `-L nogpu` 양쪽 7/7 · `-L hostgpu` 양쪽 2/2 · 린트 17/17.

### 2026-09-19 (서비스를 만들고 꽂는 코드를 목록에서 생성한다 — 호스트 둘이 같은 스무 줄을 적고 있었다)

`EngineServiceList.xxx` 한 줄이 구조체 멤버 · getter · `areEngineServicesBound()` · `ModuleServiceId` 를
만들고 있었는데, **정작 그것을 만들고 표에 꽂는 코드는 호스트마다 한 벌**이었다 —
`EngineLoop::initialize` 에 `make_unique` 20줄 + 대입 22줄, `Test/TestFramework/main.cpp` 에 또 한 벌.
목록에 줄을 더하고 한쪽을 잊으면 `areEngineServicesBound()` 가 영영 false 가 되고, 그것으로 게이팅되는
스무 곳이 조용히 폴백으로 간다.

**목록에 `owned` 열을 더했다.** 1이면 `EngineOwnedServices`(생성 저장소)가 `make_unique` 로 만들고
`bindInto()` 로 꽂는다. 0은 만드는 방법이 특별한 셋뿐이다 — 오디오(팩토리) · 커맨드 스택(배포본에는
아예 없다) · 메모리 프로파일러(Debug 전용). 결과:
- `EngineLoop.h` 의 멤버가 32 → 13개.
- 두 호스트의 생성·대입 40여 줄이 `createAll()` · `bindInto()` 두 줄로.
- 서비스를 늘리는 일 = **목록 한 줄**(초기화·종료 순서가 필요하면 그 두 자리만 더).

**일부러 생성하지 않은 것: 초기화 순서와 종료 순서.** 그 순서는 저장소가 알 수 없는 사실로 정해지고
(디바이스가 죽기 전에 무엇을 놓아야 하는지, 씬이 사라진 뒤에 모듈을 내려야 한다는 것), 줄마다 과거에
한 번씩 무너진 이유가 주석으로 붙어 있다. 자동으로 정하려면 그 지식을 의존성으로 **다시** 적어야 한다.
대신 종료 끝에 `destroyAll()` 을 둬서, 순서 목록에 한 줄을 잊어도 객체가 새지는 않게 했다.

**함정 둘을 만났고 둘 다 코드에 적어 두었다.**
1. `createAll()` 은 **이미 있는 것을 덮지 않는다.** `EngineLoop` 은 명령줄을 파싱하려고
   `CommandLineManager` 와 `GlobalVariableManager` 를 그보다 앞에서 만든다 — 덮었다면 파싱 결과가
   통째로 사라졌을 것이다(테스트 `CreateAllKeepsWhatTheHostMadeFirst` 가 그 자리를 지킨다).
2. `createAll`/`destroyAll` 의 **정의는 `.cpp` 에 있다.** 헤더에 두면 `EngineLoop.h` 를 include 하는
   모든 TU 가 서비스 스무 개의 완전한 타입을 알아야 한다(`unique_ptr` 소멸자). 실제로 그렇게 두었다가
   엔진 곳곳이 컴파일되지 않았다.

**게이트도 같이 바뀌었다.** `CheckEngineServiceBinding.py` 는 이제 "owned=0 인 필수 행을 호스트가
직접 꽂는가"와 "owned=1 을 쓰려면 `bindInto()` 를 부르는가"를 본다.

**저장소가 사는 자리는 `Engine/Common` 이 아니다.** 처음에 거기 뒀다가 `CheckEngineLayers` 가 12건으로
잡았다 — `Common` 은 티어 0(엔진의 아무것도 참조하지 않는 토대)인데 이 저장소는 서비스 스무 개의
완전한 타입을 안다. **예외 목록에 이름을 적는 대신 맞는 자리로 옮겼다**: `Source/Engine/EngineOwnedServices.*`
(티어 6, "전부를 엮는 자리" — `EngineLoop` 이 사는 곳).

**새 테스트 2건**(목록에서 생성한 검사 — `owned=1` 은 전부 채워지고 `owned=0` 자리는 건드리지 않는가,
그리고 위 함정 1). 변이로 확인했다 — `createAll` 의 널 검사를 지우면 둘째가 깨진다.

**검증.** 이 변경의 그물이 바로 앞 항목에서 만든 실기동 게이트다.
Debug·Shipping·ASan 빌드(경고 0) · `-L nogpu` 세 구성 7/7 · `-L hostgpu` 양쪽 2/2(네 백엔드 × 에디터
기동) · 린트 17/17 · 누수 보고 `no CRT leaks` · 에디터 실기동 `[Error]` 0건.

### 2026-09-19 (실기동 게이트를 자동화했다 — 엔진 기동이 처음으로 그물 안에 들어왔다)

이 저장소의 실질적인 최종 검증은 **App 을 띄워 보는 것**이었는데(0절: 네 백엔드 × 에디터 유무,
종료 코드 0, 로그 `[Error]` 0건), 그 절차가 문서에만 있어서 사람이 기억해야 돌았다. 그리고
`EngineLoop` 은 어떤 단위 테스트도 돌리지 않는다 — **기동 전체가 자동 그물 밖**이었다. 그 절차를
그대로 테스트로 옮겼다: `Test/AppTest/TestAppSmoke.cpp` 의 `AppSmokeTest`(라벨 `hostgpu`).

```powershell
ctest --test-dir build/Ninja-Debug -L hostgpu --output-on-failure   # 6초
```

**무엇을 보는가.** 종료 코드 0 · 로그에 `[Error]` 0건(그 줄을 실패 메시지에 그대로 싣는다) ·
Dev 에서는 출력이 한 줄이라도 있는가. 화면의 그림은 안 본다 — 그건 `-gv_screenshot` 픽셀 비교의
일이고, 섞으면 느려져 아무도 안 돌린다.

**타깃을 가르는 방법은 EngineTest 와 같다.** `AppTest`(전체) · `AppTest_NoGPU`(스모크 제외, CI) ·
`AppTest_HostOnly`(스모크만). 그러면서 `CheckTestSuites.py` 를 **일반화했다** — 예전에는
`Test/EngineTest/CMakeLists.txt` 한 파일만 보고 `EngineTest_NoGPU`/`_HostOnly` 를 대조했다. 이제
`Test/*/CMakeLists.txt` 에서 `<타깃>_NoGPU`·`<타깃>_HostOnly` 짝을 찾아 **마커 ↔ 두 필터**를 같은
방식으로 대조하고, 한쪽만 있는 타깃도 잡는다(빼기만 하면 아무 데서도 안 돌고, 고르기만 하면
CI 에서도 돈다).

**변이로 확인했다.** `App::initialize` 에 에러 한 줄을 심으면 그 줄이 실패 메시지로 나온다.
그 과정에서 두 가지를 알게 돼 테스트와 주석에 적었다:
1. **로거가 서기 전의 실패는 이 게이트가 못 본다.** `Logger` 는 `EngineLoop::initialize` 안에서
   만들어지므로 그 앞의 `SW_LOG_ERROR` 는 아무 데도 남지 않는다(첫 변이가 통과해서 알았다).
   그 구간은 **종료 코드로만** 드러난다.
2. **배포본은 백엔드를 하나만 링크한다**(`SW_SHIPPING_RHI_BACKEND`, 윈도우 기본 DX12). 그래서
   배포본 스모크는 스위치 없이 돌린다 — `-dx11` 을 주면 `RHIBackendRegistry` 가 거절한다.
   배포본은 Info 로그도 컴파일에서 빠져 **깨끗한 실행이 곧 출력 0줄**이라, "출력이 있는가" 단언도
   Dev 에만 둔다.

**이것이 다음 작업의 전제다.** `EngineLoop` 의 서브시스템 수명을 등록표로 바꾸는 일(1-0e)은
그물이 없어 미뤄 뒀던 것인데, 이제 그 그물이 생겼다.

**검증.** Debug·Shipping 빌드(경고 0) · `-L nogpu` 양쪽 7/7 · `-L hostgpu` 양쪽 2/2 · 린트 17/17.

### 2026-09-19 (에셋 종류를 늘리는 자리를 만들었다 — 그리고 종료가 잊고 있던 캐시 하나)

`Source` 와 `Tools/ReflectionParser` 를 **확장점 기준**으로 훑었다("하나 더하려면 몇 곳을 고쳐야
하는가"). 이미 등록표로 되어 있는 곳이 많았다 — 에디터 패널·커맨드·인스펙터, 린트 게이트/픽서,
RHI 백엔드, 압축 코덱, 에셋 포맷 마이그레이션, 리플렉션 애노테이션 필드(`PredefinedAnnotationField.xxx`
한 곳에서 apply 표까지 생성된다 — README 의 "두 곳을 같이 고쳐라" 는 낡은 문장이라 고쳤다).
**손으로 적은 목록이 남아 있던 곳 셋**을 이번에 손봤다.

**1) 에셋 캐시를 이름으로 셋 적고 있었다 — 그래서 종료가 프리팹을 잊었다.**
`ResourceManager::shutdown` 은 머티리얼과 텍스처만 비우고 프리팹 캐시를 지나쳤다. 재초기화하면
옛 프리팹이 남는다. 캐시 셋을 아는 코드가 여럿이면 언제든 한쪽만 늘어난다.
`IAssetCache`(종류 이름 · isCached · reload · getCachedCount · clear) 를 두고 `ResourceManager` 가
**등록부**를 갖게 했다. 내장 셋도 생성자에서 그 등록부를 통해 올라가고, 종료·비우기·진단은 등록부를
훑는다. 새 에셋 종류는 인터페이스 구현 + `registerAssetCache` 한 줄이다(상용 엔진의
`ResourceFormatLoader`·`FStreamableManager` 가 있는 자리).

**2) 머티리얼 캐시만 디바이스를 기억하고 있었다.** `MaterialCache::_pDevice` 는 마지막 `acquire` 가
본 디바이스인데, 이 엔진은 백엔드를 바꿔 끼운다 — 그 뒤 핫리로드가 오면 **죽은 디바이스**로
`waitIdle`·`releaseRhi` 를 부른다. `IAssetCache::reload` 가 디바이스를 인자로 받게 하고 그 멤버를
지웠다(`TextureCache` 는 처음부터 인자로 받고 있었다 — 헤더에 "갈라지는 지점 셋" 으로 적혀 있던 것이
이제 둘이다). 호출부는 에디터 핫리로드와 머티리얼 패널 둘이고, 둘 다 `EditorContext` 의 현재
디바이스를 넘긴다.

**3) 서비스 표는 하나인데 채우는 곳은 둘이었다.** `EngineServiceList.xxx` 한 줄이 구조체 멤버·getter·
`ModuleServiceId` 를 만들지만, 실제로 채우는 대입 22줄은 `EngineLoop::initialize` 와
`TestFramework/main.cpp` 에 한 벌씩 있다. 하나를 빠뜨리면 `areEngineServicesBound()` 가 영영 false 가
되고 **그 함수로 게이팅되는 스무 곳이 전부 조용히 폴백으로 간다**(배포본에서 셰이더 캐시를 건너뛰고
DXC 를 부르다 죽은 적이 있다 — 그 사고가 목록 주석에 적혀 있다). 둘을 넣었다:
- 게이트 `CheckEngineServiceBinding.py` — `bindEngineServices(` 를 부르는 **모든** 파일이 `required=1`
  행을 전부 채우는지, 표에 없는 멤버에 대입하지 않는지 대조한다. **호스트 이름을 적지 않는다**(부르는
  파일이 곧 호스트다). 자기 조각 둘을 들고 있어 `CheckLintsAreAlive` 가 살아 있음을 확인한다.
- `findUnboundRequiredServiceName()` — 비어 있는 필수 서비스의 **이름**을 돌려주고, `bindEngineServices`
  가 바인딩 직후 한 번 크게 경고한다. 표를 인자로 받는 형태라 전역을 흔들지 않고 테스트할 수 있다.

**테스트 7건 신규, 전부 변이로 확인.** `AssetCacheRegistryTest` 3건(등록부에 내장 셋이 보이는가 ·
중복/널 등록 · 비우기가 등록된 것을 전부 지나가는가) + `EngineServiceTest` 1건(빠진 서비스를 이름으로
보고하는가, 선택 서비스는 보고하지 않는가) + 게이트 자체의 조각 2건 + 기존 스위트 유지.
변이: 프리팹 등록 제거 · 중복 검사 제거 · 비우기를 첫 캐시만 → 세 케이스가 각각 깨진다.

**안 한 것과 그 이유.** `EngineLoop` 의 서브시스템 수명(25개 × 4자리)이 가장 큰 "N곳" 이지만
**그물이 없다** — `EngineTest` 도 `SmokeTest` 도 `EngineLoop` 을 돌리지 않아 실기동이 유일한 검증이다.
렌더 패스는 이미 풀스크린 경로가 선언 기반이라 이득이 작다. 둘 다 1-0e 에 근거와 함께 적어 두었다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 7/7 · `-L hostgpu` 양쪽 1/1 ·
린트 17/17(게이트 하나 늘었다) · 에디터 실기동 `-dx12 -EnableEditor` 종료 코드 0 · `[Error]` 0건 ·
새 서비스 경고도 0건(성공은 조용하다).

### 2026-09-19 (테스트가 없던 자리를 훑었다 — 그 중 둘은 실제로 깨져 있었다)

폴더 훑기가 코드는 다 지났지만 **테스트가 한 줄도 없는 단위**가 남아 있었다. "이름이 테스트에
한 번도 나오지 않는 헤더" 를 기계적으로 뽑고(대소문자 무시, 플랫폼 폴더 제외), 위험도(틀렸을 때
증상이 얼마나 엉뚱한 곳을 가리키나) × 테스트 가능성으로 넷을 골랐다. **고른 넷 중 둘에서 실제
결함이 나왔다** — 테스트가 없다는 것은 "괜찮다" 가 아니라 "아무도 안 봤다" 였다.

**1) `LinearAllocator::reset` 이 첫 블록만 다시 썼다 (결함).** 헤더는 "오프셋만 되돌리고 메모리는
유지한다" 고 적어 두었는데, 현재 블록이 가득 차면 `allocate` 가 **언제나 새 블록을 잡았다.** 그래서
reset 뒤에는 앞서 잡아 둔 빈 블록들이 그대로 놀고, 표(64칸)는 reset 마다 한 칸씩 줄며, 블록 용량은
직전의 두 배로 커진다 — **reset 을 반복할수록 메모리가 배로 자란다.** 쓰는 곳이 `EventDispatcher` 의
프레임 할당기 둘(`_arrFrameAllocator[2]`, 64KB)이라, 이벤트가 한 프레임에 64KB 를 넘기는 순간부터
그 프레임마다 128KB → 256KB → … 가 붙는다. 형제인 `FrameArenaAllocator` 는 같은 자리에서 이미
**다음 청크로 넘어간 뒤 새로 잡는다**(`allocateSlow` 의 `_currentChunkIndex++`) — 한쪽만 그 걸음이
빠져 있었다. `advanceToHeldBlock` 을 추가해 맞췄다(가득 찬 블록 **다음**부터, 요청을 담을 수 있는
빈 블록으로 먼저 옮긴다).

**2) `TileMapXmlData::toXml` 이 배열 밖을 읽었다 (결함).** 타일 루프가 `_width × _height` 만 믿고
네 배열(`_listWalkable`·`_listEncounter`·`_listPassThrough`·`_listVisual`)을 **검사 없이** 인덱싱했다.
이 구조체는 필드가 전부 공개라 크기만 바꾸고 칸을 안 늘린 채 저장할 수 있고, 그러면 Debug 는
vector 단언에서 죽고 **배포본은 조용히 남의 메모리를 파일에 적는다.** `Engine/Utility` 훑기에서 고친
"넷 중 하나만 표 크기를 보지 않았다" 와 같은 모양이다. 모자란 칸은 **읽기 쪽 기본값**으로 적게 했다
— `loadFromXml` 이 `<t>` 가 없을 때 넣는 값과 같아서 왕복이 어긋나지 않는다(경고는 남긴다).

**3) 씬 라이트의 CPU 절반에 테스트가 없었다.** `LightRegistry` 와 `collectSceneLights` 는 매 프레임
두 곳(`EngineLoop` · `FrameRenderer`)이 부르는데 한 줄도 덮여 있지 않았다. 결함은 없었고, 대신
**깨지면 셰이더를 먼저 의심하게 되는 계약**들을 못박았다: 등록/해제 대칭(지연 파괴는 플러시 전까지
남는다는 것까지) · 등록의 멱등성 · 활성 판정은 수집하는 쪽의 몫(컴포넌트·소유자·**부모 계층** 셋 다)
· 그림자 슬롯은 그림자를 드리우는 **첫 방향광** 하나 · 원뿔은 코사인으로 실린다 · 안쪽 원뿔이 바깥을
넘지 않는다 · `findActiveDirectionalLight` 는 켜진 것만 돌려준다.

**4) `Source/App` 에는 단위 테스트가 하나도 없었다.** 훑기 표에도 `App` 은 없다(Core · Engine ·
Editor · GameFramework · Games · RuntimeAPI 만 있다). `App` 은 실행 파일이라 링크할 라이브러리가
없어서, `SmokeTest` 가 하듯 **소스를 파일 단위로 가져오는** 작은 타깃을 하나 더 뒀다 — `Test/AppTest`
(`EditorUiTest` 와 같은 방식이다). 첫 손님은 `FrameTimeline`: 가변 델타 클램프 · 고정 스텝 상한 ·
**상한을 넘긴 잔액을 버린다**(남기면 다음 프레임이 더 많은 스텝을 부르는 되먹임이 된다) · 0 이하
설정의 기본값 폴백 · `start` 가 타이머를 되감는다. 시간을 진짜로 재므로 단언은 **한 방향으로만**
건다(충분히 자고 상한에서 잘렸는지). 창·RHI·모듈이 필요한 것은 넣지 않는다 — 그 경계가 무너지면
이 타깃이 "App 을 통째로 세우는 두 번째 자리" 가 된다.

**새 테스트 22건, 전부 변이로 확인했다.** 라이트 7건은 7개의 변이(활성 필터 제거 · 그림자 독점 제거 ·
코사인 제거 · 해제 누락 · 중복 방지 제거 · 원뿔 클램프 제거 · 활성 판정 제거)에 전부 걸리고, 타일맵
6건은 초기화·폴백·경계·틴트·`h` 유도 변이에 걸리며(둘은 변이 시 프로세스가 죽는다), 할당기 5건은
정렬·크기0·CAS·`_blockCount` 변이에 걸린다. 프레임 타임라인 4건은 클램프·잔액·되감기 변이에 걸린다.

**남은 구멍(다음 사람을 위해).** 같은 방식으로 뽑았지만 이번에 손대지 않은 것들이다.

| 대상 | 왜 남겼나 |
|------|-----------|
| `BoxCollider2DComponent::intersects` | **두 경로가 갈린다.** 물리 바디가 등록되기 전(에디터·테스트)에는 CPU AABB 로 답하고, 등록된 뒤(플레이)에는 `PhysicsWorld::overlaps` 가 **레이어 행렬까지** 본다. 기하는 양쪽 다 `<=` 라 같지만 레이어를 끄면 답이 달라진다 — 어느 쪽이 맞는지부터 정해야 테스트를 쓸 수 있다. |
| `FileLogOutput` · `ConsoleLogOutput` | `LogTest` 는 출력 장치를 흉내 낸 것만 본다. 실제 파일 출력(열기 실패·플러시·회전)은 안 덮인다. |
| `CallStackCapture` | `CrashReportTest` 가 있지만 이 헤더를 직접 부르는 테스트는 없다. 심볼 해석이라 플랫폼을 탄다. |
| `PagedArray` · `InlineAllocator` | 컨테이너인데 테스트가 없다. 쓰는 곳이 각각 둘·하나라 위험도는 낮다. |
| `SerializeReflectAny` | `ReflectAnyTest` 1건이 있지만 직렬화 왕복은 안 본다. |
| `App/ModuleHost` · `BackendSwapController` | `EngineLoop` 과 창이 필요하다. 실기동(`-gv_rhiSwapAtFrame`)이 더 싸다 — 3절의 "백엔드 교체" 항목에 절차가 있다. |

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 **7/7**(`AppTest` 추가) ·
`-L hostgpu` 양쪽 1/1 · 린트 16/16 · 새 테스트 22건 · ASan(Debug-ASAN)에서도 22건 전부 통과
(할당기와 XML 쓰기를 건드렸으므로 여기가 본 검사다 — 경계 밖 접근·겹친 할당은 ASan 이 본다).

### 2026-09-18 ("테스트할 수 없다" 고 적은 것 중 셋은 틀린 말이었다 — 테스트 보강)

이번 훑기에서 몇 건을 **"테스트가 없다"** 로 남겼다. 다시 따져 보니 그 이유 중 **셋이 틀렸다.**
틀린 이유를 그대로 두면 다음 사람이 같은 자리를 또 포기하므로, 무엇이 왜 틀렸는지 적어 둔다.

**정정 1 — "서비스 결합이 프로세스 단위라 테스트가 서로 휩쓸린다"(틀림).**
`editor::getService<T>()` 가 보는 모듈 서비스 표(`s_editorService`)는 **모듈 export 매크로의
`bindService` 콜백**이 채운다. 테스트는 그것을 부르지 않는다 — 즉 **EditorTest 안에서는 커맨드
스택 서비스가 처음부터 nullptr 이다.** 상태를 만들 필요가 없었고, 그냥 부르면 됐다.
(내가 엔진 로케이터(`engine::bindEngineServices`, `TestFramework/main.cpp` 가 부른다)와
모듈 로케이터를 같은 것으로 착각했다.) 새 스위트 `EditorTransactionTest` 3건 —
없을 때 안 터지는가 · 있을 때(지역 서비스로 걸어) 실제로 닿는가 · dirty 표시가 남는가.
**고치기 전 코드로 되돌리면 프로세스가 죽는다**(변이로 확인).

**정정 2 — "EditorTest 가 ImGui 를 링크하지 않아 불가능"(절반만 맞음).**
그 제약은 **그 타깃**의 것이지 테스트 전체의 것이 아니다. ImGui 를 링크하는 작은 타깃을
따로 두면 된다 — `Test/EditorUiTest`. ImGui **컨텍스트만** 있으면 도는 것을 넣고(GPU·창이
필요한 것은 넣지 않는다 — 그건 `hostgpu` 라벨이 필요하다), 기존 `EditorTest` 의 경계는 그대로
둔다(`CheckTestSuites` 의 검사도 그 타깃 이름으로 되어 있어 영향 없다).
새 스위트 `EditorUiPlatformBackendTest` 2건 — **초기화 없이 `shutdown()` 해도 살아남는가.**
이것이 실패 수습 경로(`shutdownPartialInitialization`)가 밟는 바로 그 길이다.
**고치기 전 코드로 되돌리면 프로세스가 죽는다**(변이로 확인).

**정정 3 — "저장 커맨드는 패널을 통해야 해서 테스트 불가"(틀림).**
`EditorToolAssetCommands` 는 ImGui 를 include 하지 않는다. `EditorTest` 소스 목록에 넣으니
그대로 링크됐다(`EditorInspectorCommands.cpp` · `EditorData.cpp` 와 `EditorData.h` 코드젠을
같이 넣어야 했다). 새 스위트 `EditorToolAssetCommandsTest` 3건 — 실패는 에러 로그를 남기는가 ·
**성공은 조용한가**(반대 방향을 안 보면 "항상 우는" 구현도 통과한다).
**로그를 다시 지우면 깨진다**(변이로 확인).

**정정 4 — GameFramework 의 "배포본에서만 죽는 널 역참조" 는 도달할 수 없다.**
`game::areGameServicesBound()` 가 무엇을 검사하는지 이름만 보고 "필수 서비스 전체" 로 읽었는데,
실제로는 **`ModuleServiceId::SceneManager` 슬롯 하나**를 본다. 그래서 그 함수가 true 면
`getService<SceneManager>()` 는 널일 수 없고, **내가 그 뒤에 넣은 가드는 도달하지 않는다.**
가드 자체는 남겼다 — `CheckNullableServiceUse` 린트가 `getService<T>()->` 모양을 막으므로
규칙을 예외 없이 같은 모양으로 지키는 편이 낫다 — 대신 **주석을 사실대로 고쳤고**, 진짜 함정인
`areGameServicesBound()` 의 **이름과 실제 검사 범위가 다르다**는 것을 그 선언 옆에 적었다.

**여전히 테스트가 없는 것과 그 이유(이번에는 근거를 확인했다).**

| 대상 | 왜 없는가 |
|------|-----------|
| 모듈 ABI 스탬프(RuntimeAPI) | 대조 대상이 **DLL 심볼**이다. 테스트하려면 버전이 다른 모듈 DLL 을 빌드 산출물로 따로 구워야 한다 — 실기동 재현 절차가 더 싸고 확실하다(3절 RuntimeAPI 항목에 절차가 있다). |
| 도킹 제목 대조(Editor/Gui) | `EditorContext` + 패널 매니저 + ImGui 가 모두 서 있어야 한다. `EditorUiTest` 는 **컨텍스트만** 필요한 것을 담는 타깃이라 여기 넣으면 그 경계가 무너진다. |
| 초기화 실패 시 전역 변수 해제(루트) | `ImGuiEditor::initialize()` 를 실패시켜야 하는데 창·RHI·리소스가 모두 필요하다. |
| 두 그래프 패널의 저장 계약 | 패널 본체는 노드 그래프 에디터까지 끌고 온다. **계약 자체는 기반(`IEditorPanel`)으로 끌어올려 테스트했고**, 커맨드 쪽은 위 정정 3 으로 덮였다 — 남은 것은 그 둘을 잇는 배선뿐이다. |

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 **6/6**(`EditorUiTest` 추가) ·
`-L hostgpu` 양쪽 1/1 · 린트 16/16 · 새 테스트 8건, 전부 변이로 확인.

### 2026-09-18 (배포본에서만 죽는 널 역참조 — GameFramework · Games)

> **정정(2026-09-18, 같은 날 늦게).** 아래 1) 의 마지막 문장이 **틀렸다.**
> `areGameServicesBound()` 는 이름만 보고 "필수 서비스 전체" 로 읽었을 뿐, 실제로는
> **`ModuleServiceId::SceneManager` 슬롯 하나**를 본다 — 그 하나가 바로 여기서 쓰는 서비스다.
> 그래서 **이 자리는 널이 될 수 없었고, 내가 넣은 가드는 도달하지 않는다.** 가드는 남겼지만
> (린트 규칙을 예외 없이 지키는 편이 낫다) **널 역참조를 막았다는 말은 취소한다.**
> 진짜 함정은 그 함수의 **이름이 실제 검사 범위보다 넓게 읽힌다**는 것이고, 그것은
> `GameService.h` 선언 옆에 적었다. 아래 2)·3) 과 Games 항목은 그대로 유효하다.

**1) `game::getService<T>()` 를 확인 없이 따라가는 자리 둘.** 이 함수는 못 찾으면
`SW_ASSERT( false )` 를 거쳐 `nullptr` 을 돌려주는데, **그 단정은 Shipping 에서 사라진다**
(`SW_ASSERT` 가 빈 매크로다). 즉 Debug 에서는 브레이크가 걸려 눈에 띄고 **배포본에서만 조용히
널 역참조**가 된다 — `getService<T>()->` 라는 **모양** 자체가 배포본에서만 터지는 함정이라는
뜻이다. `GameInstanceBase::serializeSceneObjects` · `deserializeSceneObjects` 둘이 그 모양이었다
(위 정정대로, 이 두 자리에 한해서는 앞선 검사 덕에 실제로 널이 되지는 않는다).

받아 두고 확인하게 고쳤고, **에디터에 붙였던 린트를 여기까지 넓혔다**
(`CheckNullableServiceUse` 의 검사 범위: `Source/Editor` → `Editor · GameFramework · Games`).
같은 함정이 세 폴더에 있고 철자가 같으므로 한 게이트가 셋을 다 본다.

**2) `registerGameFrameworkTypes()` 를 지웠다.** GameFramework 의 리플렉션 타입은
`EngineLoop` 이 서비스를 묶은 직후 `engine::registerModuleTypes( "GameFramework" )` 로 직접
등록한다 — 그것이 실제로 도는 유일한 경로다. 그런데 같은 일을 하는 함수가 `SW_GF_API` 로
하나 더 export 돼 있었고 **부르는 곳이 없었다.** 게다가 조건이 달랐다 —
`areGameServicesBound()` 일 때만 등록하므로, 서비스가 아직 안 묶인 시점에 부르면 **조용히
아무 일도 하지 않는다.** 등록 자리가 둘이면 어느 쪽이 도는지 알 수 없다.

**3) 적어 두는 것 — `GameEvents.h` 의 열두 이벤트를 아무도 발행하지 않는다.**
`SaveRequestedEvent` · `SceneTransitionRequestedEvent` 등 **열두 종 전부**, 그리고
`gameEventChannel()` 까지 저장소 안에 **발행자도 구독자도 없다.** 이름만 보면 "세이브를
요청하면 프레임워크가 쏴 주겠지" 로 읽히지만 **영원히 오지 않는다.**

지우지 않았다 — 이것은 프레임워크의 **공개 어휘**라, 게임들이 자기들끼리 주고받을 때 이름이
갈리지 않게 하는 용도라면 소비자가 없는 것이 정상이다. 그 판단은 사용자 몫이므로 헤더에
`@warning` 으로 **프레임워크가 발행하지 않는다**는 사실만 크게 적었다.
**결정이 필요하다**: (a) 그대로 어휘로 둔다, (b) 프레임워크가 실제로 발행하게 한다(세이브·씬
전환 자리를 정해야 한다), (c) 지운다.

**깨끗하다고 확인한 것 — 다시 파지 말 것.** 멤버 222개 중 쓰기 전용 후보 7개는 전부 **이벤트
페이로드 필드**이고(위 3번과 같은 뿌리) 나머지는 정상. 선언 185개 중 "죽은 함수" 후보 52개는
대부분 **키트가 게임에 내주는 세터/게터**다 — 프레임워크는 저장소 안에 소비자가 없는 것이
정상이라 지우지 않는다(`EditorThemeUtil` 팔레트와 같은 판단). 그중 실제로 죽은 배선이었던
것은 위 2번 하나였다. `GameFrameworkTest` 35건이 이 폴더를 두껍게 덮고 있다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 16/16. **새 테스트는 없다** — 널 역참조는 서비스를 푼 상태를 만들어야 하는데 결합이
프로세스 단위라 다른 테스트가 휩쓸린다. 대신 **린트가 회귀를 막는다**.

### 2026-09-18 (모듈 경계에 ABI 스탬프가 없었다 — RuntimeAPI)

**`GameAPI`·`EditorAPI` 는 함수 포인터를 순서대로 늘어놓은 구조체**다. 모듈이 자기가 아는
자리에 채우고 호스트가 자기가 아는 자리에서 읽는다. 그래서 둘이 **서로 다른 헤더로 빌드되면
호스트가 엉뚱한 함수를 부른다** — 끝에 덧붙인 경우는 호스트 쪽이 `nullptr` 로 남아 그나마
티가 나지만, **가운데에 하나 끼우면** 그 뒤가 전부 한 칸씩 밀려 `update` 자리에서 `shutdown`
이 불린다.

그것을 막는 것이 로더의 다음 한 줄뿐이었다:

```
return _editorApi.create != nullptr && _editorApi.destroy != nullptr;
```

**`create`/`destroy` 는 표의 맨 앞**이라 가운데 삽입에서도 멀쩡히 채워진다 — 즉 **가장 위험한
어긋남을 정확히 통과시키는 검사**였다. 그리고 핫 리로드는 모듈 DLL 만 다시 굽는 기능이므로,
이 어긋남이 생기는 바로 그 상황이다.

**RHI 경계는 이미 같은 이유로 스탬프를 갖고 있다** — `RHIModuleAbi.h` 의 버전+지문을
`RHIBackendRegistry` 가 로드할 때 대조하고, CLAUDE.md 의 "RHI ABI stamps" 항목이 그 실패를
설명한다. 같은 장치를 모듈 경계에 그대로 옮겼다:

- `RuntimeAPI/ABI/ModuleAbi.h` — `kModuleAbiVersion` · `kModuleAbiStamp`.
- `SW_IMPLEMENT_GAME_MODULE` / `SW_IMPLEMENT_EDITOR_MODULE` 이 두 심볼을 **같이 내보낸다**
  (표를 채우는 매크로와 같은 자리라 잊을 수 없다).
- `ModuleHost::bindEditorApi`/`bindGameApi` 가 `exportXxxApi` 를 **부르기 전에** 대조한다.
  Shipping 의 게임은 정적 링크라 어긋날 수가 없어 동적 경로만 본다.

**실증.** 모듈 TU 만 버전 2 로 다시 구워 실행하니
`Editor 모듈 ABI 버전이 다릅니다 (기대 1) — 엔진과 모듈을 함께 다시 빌드하세요` 로 거부됐다.
그 전에는 같은 DLL 이 그대로 로드됐다.

**표를 고칠 때.** 항목을 더하거나 순서를 바꾸면 `kModuleAbiVersion` 을 올리고 스탬프 문자열을
고친다. 그러면 옛 DLL 이 이유가 적힌 에러와 함께 거부된다.

**깨끗하다고 확인한 것 — 다시 파지 말 것.** `gameAllowed=0` 게이팅은 양쪽에서 실제로 강제된다
(`engine::fillModuleServices` 와 `ModuleHostInternal::buildModuleService` 가 각각
`if constexpr` / 런타임 분기로 거른다). 불투명 핸들은 `ABI/RuntimeHandles.h` 한 자리에 모여
있고 받는 쪽이 역참조하지 않는다. `ModuleService` 는 X-macro 로 만들어져 열거형·표·traits 가
한 목록에서 나온다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 16/16 · 실기동으로 거부·정상 로드 양쪽 확인. **단위 테스트는 없다** — 이것은 DLL 로드
경로라 프로세스를 띄워야 재현된다(실기동으로 대신했다).

### 2026-09-18 (초기화가 실패하면 이 DLL 주소가 매니저에 남았다 — Editor/Popups · Viewport · 루트, **Editor 전체 완료**)

**1) 초기화 실패 경로가 전역 변수를 걷어내지 않았다 (루트).** `ImGuiEditor::initialize()` 는
**맨 앞에서** `registerGlobalVariables()` 를 부른다(커맨드라인 보류값을 그때 적용해야 해서
`gv_editorStartupScene` 을 읽기 전에 와야 한다). 그런데 실패로 나가는 길 **넷이 전부 그 뒤**인데
`shutdownPartialInitialization()` 은 그것을 걷어내지 않았다.

매니저가 들고 있는 것은 **이 DLL 안의 주소**다 — `shutdown()` 이 맨 앞에서 부르며 주석에
"모듈이 내려가기 전에 반드시 걷어내야 한다" 고 적어 둔 바로 그 이유다. 정상 종료만 지키고
**실패 종료는 빠져 있었다**(Undo 스택이 언맵된 DLL 로 뛰던 2026-09-10 의 결함과 같은 종류).
정리 함수 맨 앞에서 걷어내게 했다(모듈 이름으로 지우므로 두 번 불러도 안전하다). 그리고
인자 검증 실패(`pWindow == nullptr`)만 정리 함수를 **아예 부르지 않고** 반환하던 것도 고쳤다.

**2) `IEditorPopup::getOpenPtr()` 을 지웠다 (Popups).** ImGui 의 `p_open` 에 `&_bOpen` 을 그대로
넘기면 창의 X 버튼이 그 값을 **직접** false 로 쓴다 — `close()` 를 지나가지 않으므로
`onClose()` 가 불리지 않는다. 열 때는 훅이 돌고 닫을 때는 안 도는 **반쪽 수명**이 된다.
부르는 곳이 하나도 없었지만(죽은 API) 남겨 두면 다음 팝업이 정확히 그 함정으로 간다.
지금 팝업 둘(`CommandPalettePopup` · `QuickLauncherPopup`)은 `onOpen` 만 쓰고 있어 아직 손해가
없다 — `onClose` 를 쓰기 시작하는 순간 조용히 안 불렸을 것이다.
**패널 쪽 `IEditorPanel::getOpenPtr()` 은 그대로 둔다** — 거기엔 닫힘 훅이 없고 열림 상태가
플래그 하나뿐이라 ImGui 가 직접 써도 잃는 계약이 없다(실제로 `beginPanel` 이 그렇게 쓴다).

**3) `Viewport` 는 동작 결함을 찾지 못했다.** 2026-09-10 에 피킹을 제공자 표로, 시각화를 표로
옮기고 `EditorViewportPickTest` 7건 · `EditorViewportMathTest` 2건을 붙여 둔 자리다. 멤버
67개에 쓰기 전용 없음, 죽은 함수 후보 하나(`onWindowCloseQuery`)는 **델리게이트로 묶여 있다**.

---

**이것으로 `Source/Editor` 의 모든 폴더를 한 바퀴 돌았다** (11개 단위). `Core`(2026-09-17) ·
`Engine`(2026-09-18) 과 같은 방식이고, 이 라운드에서 가장 많이 나온 모양은 **"같은 결정이 두
자리에 있다"** 였다: 스위즐의 바이트 배치와 포맷 이름 · 상속 해석 두 벌 · 저장 순서 아홉 벌 ·
절대 경로 판정 네 벌 · 도킹 제목과 패널 제목. 그리고 **"설정할 수 있는데 아무 일도 안 하는
손잡이"**(`_menuPath`)와 **"죽어 있으면서 함정인 API"**(`drawSearchFilter` · `getOpenPtr`)가
각각 둘씩 나왔다.

**이 라운드가 남긴 도구.** `Scripts/lint/gate/CheckNullableServiceUse.py` — `getService<T>()` 를
확인 없이 역참조하는 곳을 막는다. 손 검색이 놓친 두 건을 붙이자마자 찾았다.

**테스트가 붙지 않은 부분을 분명히 해 둔다.** `EditorTest` 는 **일부러 ImGui 를 링크하지 않고**
(CMakeLists 에 이유가 적혀 있다) 패널·위젯·백엔드는 전부 ImGui 를 필요로 한다. 그래서 이
라운드의 수정 중 테스트로 덮인 것은 **ImGui 없는 층**뿐이다: 텍스처 베이커 3건 · 설정 경로 1건 ·
문서 저장 계약 1건. 나머지는 코드 수준 근거와 린트로 지킨다.


### 2026-09-18 (설정할 수 있는데 아무 일도 안 하는 손잡이 — Editor/Panels)

**1) `EditorPanelEntry::_menuPath` 는 쓰기만 하고 아무도 읽지 않았다.** 멤버 157개를 훑어
나온 유일한 후보였고, 따라가 보니 손잡이 전체가 죽어 있었다 — `registerPanel` 의 `menuPath`
인자를 **넘기는 등록이 열아홉 중 하나도 없고**(전부 기본값), 필드를 **읽는 곳도 없다**.
Window 메뉴는 `entry._title` 로 항목을 만든다. 같이 있던 템플릿 오버로드
(`registerPanel<TPanel>(…)`)도 호출부가 **하나도 없었다**.

설정은 되는데 아무 일도 하지 않는 손잡이는 다음 사람이 그것으로 메뉴를 옮기려다 시간을
버리게 한다(2026-09-10 의 "뷰 모드 콤보가 아무 일도 하지 않았다" · "`_clearColor` 가
무시되고 있었다" 와 같은 모양). 필드 · 인자 · 템플릿을 걷어내고 왜 걷어냈는지 남겼다.

**2) 저장 커맨드 다섯이 실패를 알리는 방식이 제각각이었다.** `Common/Commands` 에서 둘을
고쳤는데(`saveAnimationGraph` · `saveDialogueGraph`), 나머지 셋을 여기서 마저 맞췄다:

| 커맨드 | 예전 |
|--------|------|
| `saveSpriteClip` | 성공만 말함 (실패 둘 조용) |
| `saveSequence` | **로그 아예 없음**, 경로 해석 실패도 안 봄 |
| `saveTileMap` | **로그 아예 없음** |

호출부는 반환값을 자주 버리므로 **실패가 조용하면 아무 일도 없었던 것처럼 보인다.** 다섯을
"경로 실패 · 쓰기 실패 각각 `SW_LOG_ERROR`, 성공은 `SW_LOG_INFO`" 로 통일했다.

**정정 하나 — 앞 항목의 `TileMapPanel` 기술이 틀렸다.** `Common/Commands` 항목의 표에
`TileMapPanel` 을 "성공해도 dirty 를 안 지움" 으로 적었는데, `saveDocument()` 만 보고 판단한
것이었다. 실제로는 그것이 부르는 `saveXml()` 이 성공 시 `clearDocumentDirty()` 를 부른다.
표를 고쳤다. **잘못 고친 코드는 없다**(그 패널은 손대지 않았다) — 기반이 저장 순서를 드는
변경은 그대로 유효하다.

**깨끗하다고 확인한 것 — 다시 파지 말 것.** Panels 멤버 157개 중 쓰기 전용은 위 하나뿐이고,
선언 156개 중 "죽은 함수" 후보 둘(`onLogWritten` · `onImportDialogResult`)은 **델리게이트로
묶여 있다**(`&Class::method` 형태라 이름+괄호 검색에 안 잡힌다 — 이 훑기의 알려진 오탐).
`InputMapEditorPanel::saveToFile` 과 `SequencerPanel`·`SpriteClipPanel`·`MaterialPanel`·
`GlobalVariablesPanel` 의 저장은 실패 시 dirty 를 유지한다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 16/16. **새 테스트는 없다** — 패널은 ImGui 를 링크하고 `EditorTest` 는 그러지 않는다.

### 2026-09-18 (nullptr 을 준다고 적어 둔 값을 열다섯 곳이 그냥 따라갔다 — Editor/Common/Workspace)

`editor::getService<T>()` 는 **마지막 줄이 `return nullptr;`** 이다 — 지역 등록도 없고 모듈
서비스 표에도 없으면 그렇다. 그런데 `Source/Editor` 를 세어 보니 **열세 자리가 그 값을 확인
없이 `->` 로 따라가고** 있었다(그리고 린트를 붙이자 정규식이 놓친 두 자리가 더 나왔다 —
`getService<const EngineData>()` 처럼 `const` 가 낀 것).

**같은 파일 안에서 갈렸다.** `EditorTransaction::push` 는 포인터를 받아 확인한 뒤 쓰는데,
바로 위 일곱(`beginTransaction` · `endTransaction` · `cancelTransaction` · `push` 네 자리)은
그냥 따라갔다. 커맨드 스택은 `EngineLoop` 소유라 EditorModule 보다 오래 살고 종료할 때 서비스
결합이 먼저 풀린다 — 그 창에서 트랜잭션이 하나라도 돌면 널 역참조다. `ImGuiEditor` 의 두
자리는 **워커 스레드**에서 도는 스플래시 로드였다.

열다섯 곳을 모두 "받아 두고 확인한 뒤 쓴다" 로 바꿨다.

**고치면서 한 번 잘못 고쳤다.** `recordModify` 계열 네 자리에서 스택이 없으면 곧장 `return`
하게 했는데, 그러면 뒤따르는 `markActiveSceneDirty()` 까지 건너뛴다 — **씬은 이미 바뀌었고
되돌리기 기록만 못 남기는 것**이므로 dirty 는 찍어야 한다. 조기 반환 대신 push 만 감쌌다.

**그리고 린트로 옮겼다 — `Scripts/lint/gate/CheckNullableServiceUse.py`.** 이것은 사람이 지킬
규칙이 아니다(같은 파일 안에서도 갈렸다). `getService<…>()->` 꼴만 잡고, 포인터를 받아 두는
형태는 잡지 않는다. **붙이자마자 두 건을 더 찾았다** — 손 검색이 놓친 자리였다.
`CheckLintsAreAlive` 가 자기 self-test 를 돌리고, CMake 는 `gate/` 를 훑으므로 등록할 목록이
없다(린트 테스트 15 → 16건).

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 16/16. **새 런타임 테스트는 없다** — 서비스 결합은 `TestFramework/main.cpp` 가 프로세스
단위로 잡고 있어서 한 테스트가 풀면 다른 테스트가 휩쓸린다. 대신 **린트가 회귀를 막는다**.

### 2026-09-18 (죽어 있으면서 함정이던 API 하나 — Editor/Common/Widgets)

**`EditorWidgets::drawSearchFilter` 를 지웠다.** `string&` 을 받는 검색창인데, 안에서는
**256바이트 스택 버퍼에 베껴** 편집하고 되쓰는 방식이라 **그보다 긴 필터를 조용히 잘랐다**
(길면 첫 그리기에서 잘린 값이 버퍼에 들어가고, 사용자가 한 글자라도 치는 순간 그 잘린 값이
원본에 되쓰인다).

바로 아래 형제 `drawTextField` 는 같은 문제를 **제대로** 풀어 두었다 — `string` 을 그대로
넘기고 `ImGuiInputTextFlags_CallbackResize` 로 필요한 만큼 늘린다(주석에 "길이 상한이 없다"고
적혀 있다). 즉 `string&` 을 받는 입력 둘 중 하나만 옳았다.

**그런데 부르는 곳이 하나도 없었다**(`Source` · `Test` 전체 검색). 그래서 고치는 대신 지웠다 —
죽은 데다 함정인 API 를 남겨 두면 "문자열 검색 위젯" 을 찾는 다음 사람이 정확히 그리로 간다.
왜 지웠는지는 `drawTextField` 문서에 남겼다(`string&` 입력은 이제 이것 하나다). 버퍼를 호출부가
소유하는 `drawSearchField` 는 그대로다 — 거기서는 크기를 고르는 것이 호출부의 결정이다.

**깨끗하다고 확인한 것 — 다시 파지 말 것.** ImGui 스택 짝은 모두 맞다:
`pushInspectorStyle`/`popInspectorStyle`(3/3) · `drawChip`(3/3) · `drawToggleButton`(1/1) ·
`drawVec3Control`(축마다 3/3, ID·StyleVar 짝) · `beginComponentCard`/`endComponentCard`
(닫힌 카드는 begin 이 직접 `PopID` 하고, 유일한 호출부 `InspectorPanel` 이 열린 경우에만 end 를
부른다). 접힌 카드에서 "Remove component" 를 눌러도 동작한다 — `bRemove` 처리가 분기 **밖**에
있다. `updateListSelection` 은 경계를 앞뒤로 다 검사한다. `drawNoSearchResultHint` 는 검색어를
서식 **인자**로 넘긴다(`%` 가 든 검색어에 안전). `EditorListFilter` 는 종단자 없는 조각까지
테스트 5건이 덮고 있다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15. **새 테스트는 없다** — 지운 것이고, 남은 위젯은 ImGui 를 링크해야 한다.

### 2026-09-18 (조용히 어긋날 수 있던 자리 둘을 소리 나게 — Editor/Common/Gui)

**동작 결함은 찾지 못했다.** 대신 **지금은 맞지만 다음에 손대면 조용히 틀어지는** 자리 둘을
컴파일 에러와 경고로 바꿨다.

**1) `EditorCommandKey` 의 번호가 계약인데 아무도 지키지 않았다.** 이 열거형 위에 두 곳이 서
있다 — 이름 표(`_s_arrKeyName`)는 열거형 값을 **그대로 첨자**로 쓰고, `toImGuiKey` 는 A..Z 와
F1..F12 를 **뺄셈**으로 옮긴다. 있던 단정은 **개수**뿐이었다.

개수 단정은 **가운데 삽입을 잡지 못한다** — 키를 하나 끼우면 이름도 하나 늘어나 개수가 다시
맞고, 그 뒤의 이름이 전부 한 칸씩 밀리며 ImGui 키도 어긋난다. 결과는 **단축키가 다른 명령을
실행하는 것**이고, 빌드는 통과한다. 열거형 선언 바로 아래에 자리를 못 박았다(`A==1` · `Z==26` ·
`F1==27` · `F12==38` · `Space==39`). 실증: `M` 과 `N` 사이에 `Escape` 를 끼워 보면 **예전에는
그대로 빌드되고** 지금은 네 줄짜리 컴파일 에러가 이유를 말한다. ImGui 쪽 연속성 단정은 이미
있었다 — 우리 쪽만 없었다.

**2) 기본 도킹 배치가 패널 제목 문자열에 묶여 있었다.** `applyDefaultDockLayout` 이
`"Hierarchy"` · `"Inspector"` · `"Game View"` · `"Profiler"` · `"Content Browser"` ·
`"Output Log"` 여섯을 리터럴로 적는데, 그 제목의 정본은 각 패널의 `getPanelTitle()` 이다.
`ImGui::DockBuilderDockWindow` 는 **모르는 이름도 조용히 받으므로**, 제목을 바꾸면 그 패널은
아무 말 없이 도킹되지 않고 떠 있게 된다. 지금은 여섯 다 맞다(확인함) — 그래서 이것은 결함이
아니라 **깨질 준비가 된 자리**다. 등록된 패널 제목과 대조해 어긋나면 경고하게 했다
(`EditorCommandRegistry::validate` 가 커맨드 표에 하는 일과 같다). 바로 옆 **도구 패널은 이미
레지스트리에서 이름을 받아 오고 있었다** — 고정 여섯만 남아 있었던 것이다.

**깨끗하다고 확인한 것 — 다시 파지 말 것.** `EditorThemeUtil` 의 push/pop 세 쌍은 개수가 맞다
(1/1 · 3/3 · 3/3). 단축키 판정(`isShortcutPressed`)은 수정자를 **정확히** 비교하므로
Ctrl+Shift+S 를 누를 때 Ctrl+S 가 같이 뜨지 않는다. `IEditorPopup::onClose` 는 `close()` 가
부른다(죽은 훅이 아니다). Gui 멤버 55개에 "쓰기만 하고 읽지 않는" 것은 없다.

**손대지 않은 것.** `EditorThemeUtil` 의 아홉(`getBorderColor` · `getPanelBgColor` ·
`getWindowBgColor` · `textAccent` · `textMuted` · accent push/pop 두 쌍)은 **에디터 어디에서도
불리지 않는다.** 그러나 이것은 팔레트를 채우는 API 라서, 지운다고 무엇이 나아지지 않고 오히려
다음에 테두리를 그리는 사람이 색을 인라인으로 다시 적게 만든다(이 클래스가 막으려는 것이 바로
그것이다). 팔레트 **필드**는 죽어 있지 않다 — `_textMuted` 는 `ImGuiCol_TextDisabled` 로 들어간다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `EditorTest` 57건 유지. **새 런타임 테스트는 없다** — 하나는 컴파일 타임 보장이고
(변이로 확인) 다른 하나는 ImGui 를 링크해야 한다.

### 2026-09-18 (같은 판정을 네 곳이 손으로 적고 있었다 — Editor/Common/Config)

**동작 결함은 없었다.** 이 폴더는 최근(`ca976db3` · `aec2c6cd` · `eebd368e`)에 손을 많이 본
자리라 로그 문구까지 사실대로 갈라 적혀 있다(파일 없음 vs 일부 필드 실패). 대신 **구조**에서
하나 나왔다.

**"프로젝트 상대 경로를 절대 경로로" 가 세 곳에 복사되어 있었다** —
`EditorConfig::loadFromHost` · `EditorConfig::saveToHost` · `EditorData::loadFromHostPath`.
셋 다 같은 다섯 줄이고, 그 안에서 **"절대 경로인가" 를 손으로 다시 적고 있었다**:

```
( path.size() >= 2 && path[1] == ':' ) || ( path.empty() == false && ( path[0] == '/' || path[0] == '\' ) )
```

**그런데 이 손 판정은 `FileUtil::isAbsolutePath` 와 다르다** — 정본은 `:` 앞이 **글자**인지까지
보는데 복사본들은 보지 않는다. 그리고 같은 복사본이 엔진에도 하나 더 있었다
(`ResourceUtil::hasResource`). 넷 다 정본을 쓰게 하고, 경로 해석은
`EditorUtil::resolveProjectRelativePath` 한 자리로 모았다.

**답이 갈리는 입력을 만들어 보였는가 — 아니다.** `path[1] == ':'` 인데 앞이 글자가 아닌 상대
경로는 현실의 설정 파일 이름에 나오지 않는다. 그래서 이것은 **결함을 고친 것이 아니라 하나를
고치려면 네 곳을 고쳐야 하는 구조를 없앤 것**이다. 값은 다음에 이 규칙이 바뀔 때(UNC·`~` 등)
나온다.

**테스트** `EditorAssetTypeTest.ProjectRelativePathLeavesAbsoluteAlone` — 드라이브 절대·루트
절대는 구분자만 정규화되고 그대로 나오고, 상대는 루트 아래로 간다. 절대 판정을 뒤집으면
깨진다(변이 확인).
**변이 테스트에서 한 번 헛짚었다**: 처음에 `if ( false )` 로 바꿨더니 테스트가 통과했는데,
그것은 "절대 경로 판정을 없앤다" 가 아니라 "**아무것도 붙이지 않는다**" 라서 절대 경로 케이스가
우연히 맞았기 때문이다. 조건을 없애는 변이는 `if ( true )` 쪽이었다 — 조건을 지우는 변이는
**어느 쪽으로 지우는지**가 중요하다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `EditorAssetTypeTest` 6 → 7건.

### 2026-09-18 (저장이 실패해도 "저장됨" 이 됐다 — Editor/Common/Commands)

**찾은 방법.** 커맨드 폴더가 내놓는 `static bool` 함수 50개를 모아, 그 이름이 **문장으로만**
불리는 곳(반환값을 버리는 곳)을 기계로 훑었다 — 42자리가 나왔다. 대부분은 커맨드가 스스로
로그를 남기므로 정상이다. 그중 **저장**만 골라 들어갔다.

**1) 저장 실패가 완전히 조용했고, 그 결과 편집이 사라졌다.**
`EditorToolAssetCommands::saveAnimationGraph`/`saveDialogueGraph` 는 성공에만
`SW_LOG_INFO("Saved …")` 를 남기고 **실패 두 경로는 로그 없이 `false` 만** 돌려줬다. 그리고
두 패널이 그 반환값을 버렸다:

```
EditorToolAssetCommands::saveAnimationGraph( data, getLoadedAssetPath() );  // 반환값 버림
clearDocumentDirty();        // 실패해도 "저장됨" 으로 표시
syncDocumentUndoBaseline();  // 실패해도 되돌리기 기준점을 옮긴다
...
bool saveDocument() { saveGraphData(); return true; }   // 언제나 성공이라고 답한다
```

그래서 `EditorDocumentPanel` 이 문서를 바꾸기 전에 부르는 `saveDocument()` 가 `true` 를
돌려주고 **전환이 그대로 진행된다.** 종료 확인도 dirty 가 지워져 뜨지 않는다. 즉 저장이
실패하면 사용자는 **아무 신호 없이 편집을 잃는다.** 2026-09-10 에 `InputMapEditorPanel` 에서
고쳤던 "편집이 조용히 사라졌다" 와 같은 결과가 다른 경로로 나 있었다.

**2) 그리고 그 순서를 패널 아홉이 각자 구현하고 있었다.** `saveDocument()` 구현 아홉을 나란히
놓으니 전부 달랐다:

| 패널 | 실패 시 dirty | 반환값 |
|------|------|------|
| `MaterialPanel` · `SpriteClipPanel` · `GlobalVariablesPanel` | 유지 ✓ | 정확 ✓ |
| `AnimationGraphPanel` · `DialogueGraphPanel` | **무조건 지움** ✗ | **언제나 true** ✗ |
| `TileMapPanel` · `InputMapEditorPanel` · `DataTablePanel` · `SequencerPanel` | 내부 위임 | 각자 방식 |

**정정(2026-09-18, `Panels` 훑는 중).** 처음 이 표에 `TileMapPanel` 을 "성공해도 안 지움" 으로
적었는데 **틀렸다** — `saveDocument()` 만 보고 판단했고, 실제로는 그것이 부르는 `saveXml()` 이
성공했을 때 `clearDocumentDirty()` 와 `syncDocumentUndoBaseline()` 을 부른다. 위임 한 겹을
따라가지 않은 것이 원인이다. 잘못 고친 것은 없다(그 패널은 손대지 않았다). 기반이 순서를 드는
변경은 그대로 유효하다 — 두 그래프 패널은 실제로 깨져 있었고, 위임 방식이 넷이나 되는 것 자체가
"각자 구현" 의 증거다.

**기반이 순서를 들게 했다**(`IEditorPanel::saveDocumentAndClearDirty`): 저장에 **성공했을 때만**
dirty 를 지우고, 저장 경로는 둘 다(`trySaveDirtyDocument` · `EditorDocumentPanel` 의 문서 전환)
이것을 거친다. 파생은 **"쓰고, 됐는지 답한다"** 만 한다. 바로 옆 `discardDirtyDocument` 는
처음부터 기반이 순서를 들고 있었다 — 저장 쪽만 빠져 있었던 것이다. 2026-09-10 에 dirty **비트**를
기반으로 올린 것의 다음 한 칸이다.

커맨드 쪽도 실패를 크게 말하게 했다(경로 해석 실패 · 파일 쓰기 실패 각각 `SW_LOG_ERROR`).

**테스트** `EditorPanelDocumentTest.BaseClearsDirtyOnSuccessfulSave` — 가짜 패널이 **일부러**
`clearDocumentDirty()` 를 부르지 않게 바꾸고(새 계약이 그렇다), 그래도 성공한 저장 뒤에는
깨끗해지고 실패한 저장 뒤에는 dirty 가 남는지 본다. 기반에서 지우기를 빼면 깨진다(변이 확인).
**패널 두 개의 수정 자체는 테스트가 없다** — 패널은 ImGui 를 링크하고 `EditorTest` 는 그러지
않는다. 기반 계약으로 끌어올린 덕에 **그 부분은** 테스트가 붙었다.

**깨끗하다고 확인한 것 — 다시 파지 말 것.** `EditorSceneCommands::wouldCreateParentCycle` 의
널 → `true`(거부) 는 의도이고 테스트가 네 경우를 다 박아 두었다. `EditorCommandRegistry::validate`
의 `findSharedChord` 는 주/보조 단축키 **네 조합을 모두** 비교한다. `validate` 는 실제로
`EditorCommandGui` 등록 직후에 돌고 어긋나면 에러 로그를 남긴다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `EditorPanelDocumentTest` 5 → 6건.

### 2026-09-18 (실패를 수습하라고 있는 경로가 실패했다 — Editor/Common/Backend)

**먼저 — 이 폴더의 두 건에는 테스트가 없다.** `EditorTest` 는 **일부러 ImGui 를 링크하지 않는다**
(CMakeLists 에 그 이유가 적혀 있고, `CheckTestSuites` 가 그 경계를 지킨다). 두 결함 모두 ImGui
컨텍스트가 있어야 재현되므로, 테스트를 붙이려면 그 정책을 깨야 한다. **근거는 코드 수준이다** —
아래에 추적 경로를 그대로 적어 둔다.

**1) 초기화가 실패하면 그 뒤처리가 단정에 걸렸다.** `ImGuiEditor::initialize` 는 각 단계가
실패할 때마다 `shutdownPartialInitialization()` 을 부르고, 그 함수는 `_platformBackend->shutdown()`
을 부른다. 그런데 `ImGuiWin32PlatformBackend::shutdown()` 은 `ImGui_ImplWin32_Shutdown()` 을
**무조건** 불렀다 — 짝이 되는 Init 이 없으면 그 함수의 첫 줄 단정
("No platform backend to shutdown, or already shutdown?")에 걸린다.

즉 **플랫폼 백엔드 초기화가 실패하면, 그것을 수습하려는 경로가 곧바로 죽는다.**
추적: `initialize()` → `_platformBackend->initialize(...) == false` →
`shutdownPartialInitialization()` → `_platformBackend->shutdown()` → 단정.

렌더러 백엔드 **넷은 모두** `if ( ImGui::GetIO().BackendRendererUserData != nullptr )` 로 같은
것을 막고 있었다 — 그것이 이 패턴이 필요하다는 증거다. 플랫폼 쪽(Win32 · OSX)만 빠져 있었다.
`BackendPlatformUserData` 로 같은 모양을 맞췄다. (X11 은 ImGui impl 을 쓰지 않고 자기 상태만
정리하므로 해당 없음.)

**2) `ImGuiViewportSizeGuard` 를 설치한 둘 중 하나만 놓았다.** 이 가드는 DXGI 가 요구하는
"HWND 클라이언트 크기 = 스왑체인 크기" 를 맞추려고 ImGui 의 창 생성/리사이즈 콜백을 가로채고,
헤더에 **"백엔드 종료 시 `clear()` 를 부르십시오"** 라고 적혀 있다. 설치하는 곳은 DX11 과 DX12
둘인데 **`clear()` 를 부르는 곳은 DX12 뿐**이었다. 짝을 맞췄다.
(이 가드는 원래 DX11·DX12 가 한 벌씩 들고 있던 것을 한 곳으로 모은 것인데, 그 추출이 한쪽
호출부를 반만 연결한 채 끝나 있었다 — 공통화가 남기는 전형적인 자리다.)

**깨끗하다고 확인한 것 — 다시 파지 말 것.** 렌더러 백엔드 넷의
`registerTexture`/`unregisterTexture` 는 각자 자기 자원을 짝 맞춰 잡고 놓는다(DX11 ComPtr 목록 ·
DX12 디스크립터 힙 · Vulkan `AddTexture`/`RemoveTexture` + 맵 · GL 은 RHI 소유라 no-op).
`createRendererBackend` 는 모르는 백엔드에 **DX11 로 대신 만들어 주지 않고** nullptr 을 돌려준다
(주석에 그 판단이 적혀 있다). `EditorDrawDataSnapshot` 은 공유 `Textures` 리스트를 끊고
`CloneOutput` 의 write 커서를 손으로 맞추는 이유까지 주석에 적혀 있다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15. **새 테스트는 없다**(위 사유).

### 2026-09-18 (같은 결정이 두 자리에 있으면 반드시 어긋난다 — Editor/Common/Asset)

**1) 스위즐이 바이트를 놓는 방식과 그 결과를 부르는 이름이 따로 살았다.** 텍스처 베이커에서
섞기는 `applyChannelManipulations` 의 if 사슬이 정하고, 결과 포맷 이름은 `bakeTexture` 의
`_swizzle == BGRA ? B8G8R8A8 : R8G8B8A8` 삼항이 정했다. **삼항은 `ARGB` 를 몰랐다.**
그래서 `"swizzle": "ARGB"` 로 구우면 바이트는 옮겨졌는데 결과물에는 "RGBA 다" 라고 적혀
나갔다 — 색이 깨진다.

게다가 그 섞기 자체가 틀렸다. 코드는 왼쪽으로 한 칸 돌려 RGBA 를 **GBAR** 로 만들었는데,
ARGB 는 어떤 읽기로도 그게 아니다. **BGRA 와 ARGB 는 사실 같은 것이다** — D3D9 의
`D3DFMT_A8R8G8B8` 은 메모리에서 B,G,R,A 이고 DXGI 가 그것을 `B8G8R8A8` 이라 부른다
(열거형 주석의 "레거시 ARGB" 가 그 뜻이다).

**표 하나로 모았다**(`SwizzleLayoutInternal`): 한 줄이 "원본의 몇 번째에서 가져오는가 ·
알파를 덮는가 · 그 배열을 무슨 이름으로 부르는가" 를 함께 갖는다. 스위즐을 하나 더하는 것이
이제 **줄 하나**다. 그린 반전은 섞기 **앞**으로 옮겼다 — 입력이 언제나 RGBA 라 초록 자리가
1 로 확정인 시점이다(지금 스위즐들에서는 순서를 바꿔도 결과가 같아서 **테스트가 순서를
구별하지는 못한다**. 새 스위즐이 초록을 옮기는 순간 조용히 틀리는 자리를 미리 막은 것이다).
테스트 `EditorTexturePipelineTest.SwizzleLayoutAndFormatAgree` · `InvertGreenHitsGreenUnderEverySwizzle`.

**2) 찾지 못한 `inherits` 가 조용히 사라졌다.** 프리셋 쪽과 규칙 쪽이 상속 해석을 **각자
복사해** 갖고 있었고, 둘 다 부모를 못 찾으면 그냥 넘어갔다. 그래서 이름 오타나 **부모를
아래쪽에 적는 것**(찾기는 그 시점까지 파싱된 프리셋만 본다)이 상속을 통째로 지웠고, 그
텍스처는 아무 말 없이 기본값(`BC7_UNORM`)으로 구워졌다. JSON 을 고친 사람이 알 방법이 없었다.
한 자리(`applyInheritance`)로 모으고 경고를 남기게 했다 — 설정 전체를 버리지는 않는다(하나
틀렸다고 나머지 규칙까지 죽일 이유가 없다). `_inherits` 는 **요청한 이름**을 그대로 남기므로
(무엇을 원했는지가 진단에 필요하다) 상속이 실제로 일어났는지는 **값**으로 확인한다.
테스트 `EditorTexturePipelineTest.UnresolvedInheritsIsReported`(로그 리스너로 경고를 센다 —
`SW_LOG_WARNING` 은 Shipping 에도 남으므로 양쪽 구성에서 돈다).

**곁들여 — `ImageUtil` 을 `DdsLoader` 와 같은 약속으로 맞췄다.** 실패가 출력에 반쯤 찬 상태를
남기지 않게 하고(`outImage = RawImageData{}`), stb 가 길이를 `int` 로 받는다는 사실을 캐스트로
덮지 않고 **넘기기 전에 거절**하게 했다. 둘 다 지금 호출부에서는 도달하지 않는다 — 두 이미지
로더가 같은 약속을 갖게 하려는 것이다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `EditorTexturePipelineTest` 2 → 5건.

### 2026-09-18 (활성 창이 죽어도 전역은 그 자리를 가리켰다 — Engine/Window · **Engine 전체 완료**)

**죽은 창을 가리키는 전역.** `IWindow::getActiveWindow()` 는 전역 하나를 돌려주고, 그것을
읽는 곳이 넷이다(RHI 초기화 · 프레임 트랜지언트 · 에디터 에셋 명령 · ImGui 에디터).
그런데 창이 파괴돼도 그 전역은 그대로 남았다 — **`~IWindow()` 가 `= default` 였다.**

`App::shutdown` 은 파괴 **전에** 손으로 끊고 있었고 주석에 "댕글링 방지" 라고까지 적혀 있다.
그러니 아는 사람은 알고 있었던 셈인데, 그것이 **한 경로의 규율**로만 존재했다. 바로 그 옆
`EngineLoop` 은 App 이 없는 임베드 시나리오에서 창을 전역에 놓아둔 채 소유를 호출자에게
넘긴다(그 주석도 코드에 있다). 테스트도 App 을 거치지 않는다. 소멸자에서 끊으면 어느 경로로
죽어도 참이 된다 — 규율이 불변식이 된다. 테스트 `WindowTest.DestroyedActiveWindowClearsGlobal`
(`hostgpu` 라 CI 는 못 돌린다 — 로컬 Shipping 에서 확인했다).

**세 플랫폼을 나란히 놓고 본 결과.** 닫기 질의는 Win32(`WM_CLOSE` → `tryBeginClose`)와
X11(`ClientMessage`/WM_DELETE → `tryBeginClose`)이 같은 계약을 지킨다. `recreate()` 도 둘 다
`_bRecreating` 로 감싸 재생성 중의 닫기를 걸러낸다. Win32 의 `_bResizing` 재진입 가드는
**OS 가 `SendMessage` 로 같은 스레드에 WM_SIZE 를 되먹이는** Win32 고유의 상황을 막는 것이라
X11 의 `XPending` 폴링 루프에는 해당하지 않는다(그래서 없는 것이 맞다).

**적어 두는 것 — `CocoaWindow` 는 `_onResize` 를 한 번도 부르지 않는다.** macOS 에서는 창을
줄여도 스왑체인이 따라가지 않는다는 뜻이다. 그런데 이 저장소에 macOS 프리셋이 없어
**빌드도 테스트도 되지 않는 코드**라, 고쳐도 확인할 방법이 없어 손대지 않았다. macOS 를
실제로 켤 때 이 대목을 먼저 볼 것.

---

**이것으로 `Source/Engine` 의 모든 폴더를 한 바퀴 돌았다** (Animation → Window, 21개 폴더).
`Source/Core` 와 같은 방식이다: 폴더마다 기계적 훑기 → 형제 비교 → 결함에 회귀 테스트 →
변이 테스트로 그 테스트가 정말 무는지 확인 → 폴더 단위 커밋. 이 사이클에서 가장 많이 나온
결함 모양 셋:

1. **"끝났다" 와 "됐다" 를 같은 것으로 적는다** — 실패한 로드가 성공으로 기록되고
   (`AssetStreamingQueue`), 못 알아본 포맷이 성공으로 나가고(`DdsLoader`), 못 옮긴 원소가
   조용히 사라진다(`SpatialTree::update`).
2. **파일에서 읽은 수를 그대로 믿는다** — 팩 헤더 · 씬 엔티티 수 · 밀집 비트마스크 프로퍼티 수.
   셋 다 거대한 할당이나 끝나지 않는 루프로 이어졌다.
3. **형제 넷 중 하나만 다르다** — 표 크기를 보지 않는 순회 하나(`FrameProfiler`), 재진입
   방지를 보지 않는 push 하나(`CommandStack`), 되돌리지 않는 update 하나(`SpatialTree`).

### 2026-09-18 (넷 중 하나만 표 크기를 보지 않았다 — Engine/Utility)

**1) `FrameProfiler::registerScope` 가 고정 배열 밖을 읽었다 — 프로세스가 죽는다.**
구간 표는 `Scope _arrScope[kMaxScope]`(64개) 고정 배열이고, 등록 수는 `_scopeCount` 다.
표가 꽉 찬 뒤에도 `fetch_add` 는 **계속 카운터를 올리고** 실패만 돌려줬다. 그래서 65번째
등록 뒤 `_scopeCount == 65` 가 되고, 그 다음 등록의 "같은 이름 찾기" 선형 탐색이
`_arrScope[64]` 를 읽는다 — **배열 바로 뒤에 있는 것이 `_scopeCount` 자신**이라, 그 비트가
`const utf8*` 로 읽혀 `StringUtil::equals` 에 들어가고 주소 65 를 역참조한다.

이 파일의 다른 세 순회(`endFrame` · `report` · `reset`)는 **전부** `index < kMaxScope` 로
막고 있었다. 넘침을 **만드는** 이 함수 하나만 막지 않았다 — 넷을 나란히 놓고 보면 바로 보인다.
탐색 상한을 `kMaxScope` 로 자르고, 넘친 뒤에는 카운터를 표 크기에 붙여 둔다(그러지 않으면
등록 시도마다 계속 자라 `uint32` 를 한 바퀴 돌고 남의 슬롯을 내주게 된다). 둘 중 하나만
있어도 죽지는 않지만 카운터가 자라는 것 자체가 따로 틀린 일이라 둘 다 뒀다.
새 스위트 `FrameProfilerTest` 2건 — **이 폴더에 프로파일러 테스트가 하나도 없었다.**

**2) `CommandStack::pushCoalesce` 가 재진입 방지를 보지 않아 지난 명령을 덮어썼다.**
`_bIsExecuting` 은 undo/redo 콜백이 자기 자신을 새 명령으로 기록하지 못하게 막는 깃발이다.
`push` 는 그것을 보는데 `pushCoalesce` 는 보지 않았다. 그래서 undo 콜백 안에서 병합 push 를
하면 `push` 는 거절당하는데 **coalesce 키는 그대로 기록되어**, 그 다음의 정상적인 병합 push 가
같은 키를 보고 `_index - 1` 의 명령 — **아무 상관 없는 지난 명령** — 의 redo 와 레이블을
갈아치웠다. 되돌린 뒤 다시 실행하면 다른 일이 일어난다. 에디터에서 슬라이더를 드래그하다
Ctrl+Z 를 누르고 다시 드래그하면 닿는 자리다.
테스트 `EditorCommandStackTest.CoalesceDuringUndoDoesNotRewriteHistory`.

**따라갔지만 결함이 아니었던 것.** `KeyValueFile::parse` 가 `line.front()` 를 그냥 부르는데,
`forEachContentLine` 이 **빈 줄과 주석을 이미 걸러서** 넘기므로 안전하다(그 함수 안의 trim 도
`empty() == false` 를 매번 확인한다).

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `EditorCommandStackTest` 10 → 11건 · 새 `FrameProfilerTest` 2건.

### 2026-09-18 (실패한 update 가 원소를 삼켰다 — Engine/Spatial)

**형제 셋을 나란히 놓고 보니 하나만 달랐다.** 이 폴더에는 "옮기기" 가 세 벌 있다 —
`SpatialTree::update`(쿼드트리·옥트리 공용) · `SpatialHashGrid2D::update` · `BVHTree3D::update`.
뒤의 둘은 그냥 `insert` 에 맡긴다(insert 가 알아서 기존 것을 지우고 다시 넣는다). 그런데
`SpatialTree::update` 만 **지우고 → 넣는** 두 단계였고, 두 번째가 실패할 수 있었다:

```
remove( id );                       // 지웠다
return insert( id, newBounds, … );  // 월드 밖이면 false — 원소는 사라진 채로 끝난다
```

`Node::insert` 는 `_bounds.intersects( elem._bounds ) == false` 면 false 를 돌려준다. 즉
**새 경계가 월드 밖이면** 삽입이 실패하는데, 그 시점에 원소는 이미 지워진 뒤다. 호출부는
`false` 를 받고 "그대로겠지" 로 읽지만 원소는 트리에서 없어졌고 `getTotalElements()` 도 줄었다.
**월드를 벗어나는 오브젝트에서 바로 일어나는 일이다.** 실패하면 되돌려 놓도록 고쳤다 — 같은
경계로 한 번 들어갔던 원소이고 월드 경계는 그대로이므로 그 되돌리기는 반드시 성공한다.
테스트 `SpatialTest.FailedUpdateKeepsElement`.

**따라갔지만 결함이 아니었던 것.** `QuadTreeTraits::subdivide` 는 네 사분면을 **각각 손으로**
적고 `OctreeTraits::subdivide` 는 비트 인덱스 한 루프로 적는다 — Physics 에서 이런 손 전개가
결함을 숨겼던 자리라 네 블록을 하나씩 맞춰 봤는데, 빈틈도 겹침도 없이 부모를 정확히 덮는다.
BVH 의 `removeLeaf`/`freeNode` 도 Box2D 식 동적 트리 그대로이고 한 번의 제거가 노드를 정확히
하나만(부모) 반납한다 — 이중 반납은 없다.

**적어 두는 것 하나 — `SpatialHashGrid2D` 는 셀 범위를 제한하지 않는다.** `insert` 와
`queryAabb` 는 AABB 를 셀 좌표로 바꿔 `for (cellX = start; cellX <= end; ++cellX)` 두 겹을 돈다.
경계가 거대하거나 `inf` 면 그 범위가 `int32` 전체가 되어 **끝나지 않는다**(게다가 `INT_MAX` 에서
`++` 는 부호 있는 오버플로다). 지금 이 클래스를 쓰는 **제품 코드가 없어서**(테스트뿐이다) 어떤
상한이 맞는지 정할 근거가 없어 손대지 않았다. **물리나 컬링에 실제로 물릴 때 이 대목을 먼저
볼 것** — 셀 범위 상한과 비유한(非有限) 좌표 거부가 그 자리에 필요하다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `SpatialTest` 8 → 9건.

### 2026-09-18 (손상된 스트림 하나로 프로세스가 멈췄다 — Engine/Serialization)

**이 폴더는 대체로 잘 굳어 있다.** 길이 접두사가 `uint32` 인 경로들(`Archive::readString` ·
`readSection` · `operator>>(vector<uint8>&)` · `BinaryStreamReader` 전부)은 **쓰기 전에 경계를
검사하고** 그 다음에 `resize` 한다. `StringPool` 은 `kMaxDynamicStrings` 로, RPC 언팩은 함수의
실제 인자 수로 스트림의 수를 대조한다. 그런데 `uint64` 를 다루는 자리 둘이 새고 있었다.

**1) 밀집 비트마스크의 프로퍼티 수가 검사 없이 세 군데에 쓰였다.**
`BinarySerializer::deserializeCompact` 의 Dense 분기는 모드 바이트 다음에 `totalProps` 를
varint 로 읽는데, **스트림에서 온 그 값을 그대로** 썼다:

- `(totalProps + 7) / 8` 로 비트마스크를 잡는다 — 큰 값이면 거대한 할당이고, `uint64`
  끝자락이면 **덧셈이 넘쳐 0 바이트** 마스크가 나온다.
- 그 0 바이트 마스크를 `PresenceMaskUtil::testBit` 이 그대로 읽는다 — 첫 바퀴에 버퍼 밖이다
  (`testBit` 은 길이를 받지 않는다; 경계는 부르는 쪽 몫이다).
- 순회 변수가 `uint32` 인데 `totalProps` 는 `uint64` 라, 4,294,967,295 를 넘으면 인덱스가
  되감겨 **끝나지 않는 루프**가 된다.

회귀 테스트를 먼저 짜서 확인했더니 **테스트가 돌아오지 않았다**(60초 타임아웃). 비트마스크는
프로퍼티 여덟 개당 한 바이트이므로 `남은 바이트 × 8` 이 상한이고, 그보다 큰 수는 어떤
스키마에서도 거짓이다 — 프로퍼티가 더 많은 **새 스키마는 그대로 허용된다**(이미 있던
`propIndex < numProps` 건너뛰기가 그 경우를 맡는다). 순회 변수도 `uint64` 로 고쳤다.
테스트 `ArchiveTest.CompactDensePropertyCountIsBounded`.

**2) `offset + count > size` 는 큰 수에서 넘쳐 검사를 통과한다.** 이 형태가 세 군데 있었고
셋 다 **호출부가 파일에서 읽은 수를 그대로 넘기는** 자리다:
`Archive::readBytes( uint64 )` · `Archive::readSubArchive( uint64 )` ·
`Archive::hasBytesAvailable( uint64 )` · `BinaryStreamReader::skip( size_t )`.
`count > size - offset` 으로 바꿨다 — 위치는 검사를 통과한 뒤에만 나아가므로 `offset <= size`
가 항상 참이고, 그래서 뺄셈에는 넘침이 없다. `BinarySerializer` 의 페이로드 경계 검사 둘도
같은 형태였다.

**이 결함의 함정은 테스트를 짜는 쪽에 있다.** 처음 짠 테스트는 위치 0 에서 거대한 크기를
넘겼는데 **변이 테스트가 통과했다** — 위치가 0 이면 `0 + count` 는 어떤 `uint64` 로도 넘치지
않아 옛 검사도 우연히 버틴다. 넘치게 하려면 **위치를 먼저 옮기고** 거기서 되감기는 크기를
줘야 한다(위치 4 · 크기 `0xFFFFFFFFFFFFFFFC`). 그렇게 고치니 네 변이가 모두 잡혔다.
테스트 `ArchiveTest.BoundsChecksSurviveSizeOverflow`.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `ArchiveTest` 35 → 37건.

### 2026-09-18 (이벤트 트랙이 배포본에서 아무 일도 하지 않았다 — Engine/Sequencer)

이 폴더에는 테스트가 **하나도 없었다**(`EditorTest` 가 에셋 타입 등록만 건드렸다).
새 스위트 `SequencerTest` 5건을 만들고 시작했고, 그 중 셋이 첫 실행에서 졌다.

**1) 시퀀서 이벤트는 배포본에서 아무 일도 하지 않았다.** 지나간 이벤트에 대한 반응이
`SW_LOG_INFO` **한 줄뿐**이었는데, 그 매크로는 Shipping 에서 통째로 사라진다
(`SW_LOG_LEVEL_COMPILED(2)`). 즉 출시된 게임에서 이벤트 트랙은 없는 것과 같았고, **Dev 에서는
로그가 보이니 그 사실이 드러나지도 않았다.** 이것을 안 방법이 그대로 교훈이다 — 회귀 테스트를
로그 리스너로 짰더니 Debug 는 통과하고 **Shipping 만 졌다**(CLAUDE.md 가 말하는 바로 그 자리).

`applyFrame` 에 `pOutListCrossedEvent` 출력을 붙였다. 이미 그 안에서 계산하던 집합을 내보내는
것뿐이라 새 하부 구조가 아니고, 대신 이벤트가 **모든 구성에서 존재하는 값**이 된다. 로그는 Dev
편의로 남겼다.
**남은 일:** `SequencePlayerComponent` 가 이 목록을 받아 무언가로 내보내는 것(델리게이트·이벤트
디스패처)은 아직 없다. 그것은 설계 결정이라 여기서 짓지 않았다 — 이벤트에 반응하는 기능을 붙일
때 `applyTimeline()` 이 출발점이다.

**2) 시퀀스 첫 프레임에 걸린 이벤트는 영영 발화하지 않았다.** 판정은 "지나갔는가"
(`previousFrame < start <= frame`)인데 `play()` 가 `_previousFrame` 을 `_frameMin` 으로 두었다.
그러면 `_start == _frameMin` 인 이벤트는 처음부터 이미 지난 것이다. **루프를 돌 때도 같다** —
되감긴 뒤 이전 프레임이 `_frameMax` 근처로 남아 있어 매 바퀴 첫 프레임 이벤트가 빠졌다.
둘 다 `_frameMin - 1` 에서 시작하게 고쳤다.

그런데 그러자 기존 "이전 프레임 없음" 표시와 부딪혔다. 예전 기본값은 `-1` 이고 판정도
`previousFrame < 0` 이었다 — `_frameMin` 이 0 인 흔한 시퀀스에서 "첫 프레임을 막 지났다" 를
표현할 값이 **바로 그 -1** 이다. 게다가 음수 프레임을 쓰는 시퀀스에서는 멀쩡한 이전 프레임이
"없음" 으로 읽혔다. `kNoPreviousFrame`(`INT32_MIN`)을 이름 붙여 갈랐다.

**3) `SequenceAsset::loadFromFile` 이 같은 파일을 두 번 파싱했다.** 문서를 읽은 뒤
`parseJson( doc.dump( -1 ) )` — **읽은 것을 문자열로 되돌렸다가 다시 읽는** 경로였다.
`parseRoot( const JsonValue& )` 를 뽑아 파일 경로와 문자열 경로가 한 자리로 모이게 했다.
같은 손질에서 `parseJson` 이 실패할 때 `_listItem` 만 비우고 프레임 범위·노트는 앞 시퀀스의
것을 남기던 것도 고쳤다(`*this = SequenceAsset{}`) — 트랙 없는 옛 시퀀스가 새 시퀀스인 척했다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · 새 `SequencerTest` 5건이 **양쪽 구성에서** 통과.

### 2026-09-18 (대기열에 넣은 요청이 자기 씬을 받지 못했다 — Engine/Scene)

**1) 대기열에 들어간 씬 로드 요청은 자기 future 를 받지 못했다.** 이미 로드가 도는 중에
`requestLoadFuture` 를 다시 부르면 그 요청은 대기열로 가는데, **돌려주던 future 는 도는 중인
로드의 것**이었다. 그리고 `tickTransitions` 는 대기열이 있으면 도는 로드를 버리면서 바로 그
약속에 `nullptr` 을 넣는다 — 그래서 대기열에 넣은 쪽은 **자기 씬이 멀쩡히 활성이 되는데도
"실패" 를 받았다.**

더 조용한 것이 하나 더 있었다. 대기열은 `_queuedPath` 한 자리인데, 세 번째 요청이 오면 두 번째는
경로가 덮이면서 **아무 통지 없이 사라졌다** — 그 요청자가 쥔 future 는 아무도 채우지 않으므로
영원히 끝나지 않는다. `future.get()` 은 조건 변수 대기라 그대로 멈춘다.

요청 경로와 대기열 경로가 같은 자리로 모이도록 `dispatchLoad( path, promise )` 를 뽑았고,
대기열 요청의 약속(`_queuedPromise`)을 따로 들고 있다가 그대로 넘긴다. 밀려나는 요청에는
`nullptr` 을 넣는다(`shutdown` · `cancelPendingAsyncLoads` 포함 — 종료 때도 대기열 요청자가
끝나지 않은 future 를 쥐고 있으면 안 된다).
테스트 `SceneAsyncTest.QueuedRequestGetsItsOwnScene` — A 가 돌고 B 가 대기열, C 가 B 를 밀어낸다.
A·B 는 `nullptr`, C 는 활성 씬. **셋 다 `isReady()` 여야 한다**는 것이 이 테스트의 핵심이다.
기존 `SceneAsyncStressRapidSwitching` 은 세 번 연속 요청을 이미 하고 있었지만 future 를 받아만
두고 보지 않아서 못 잡았다.

**2) 바이너리 씬이 말하는 엔티티 수를 그대로 믿었다.** `arch >> entityCount` 다음이
`_listEntityNode.reserve( entityCount )` 였다. 엔티티 하나가 문자열 넷이라, 손상된 파일의
`0xFFFFFFFF` 는 수백 기가짜리 요청이 된다(변이 테스트에서 그 한 줄로 프로세스가 죽었다).
문자열 넷은 길이만 해도 16바이트이므로 **남은 바이트 / 16** 이 상한이다. 그리고 루프가
`arch.isError()` 를 **끝나고 나서야** 봤다 — 잘린 파일에서 남은 횟수를 마저 도는 것은 빈 노드를
쌓는 일일 뿐이라 안에서 끊는다. 테스트 `SceneTest.BinaryEntityCountIsBoundedByFileSize`.
[`Engine/Resource` 항목의 팩 헤더 검증과 같은 뿌리다 — 파일에서 온 수를 믿는다.]

**따라갔지만 결함이 아니었던 것.** `cancelPendingAsyncLoads()` 만 씬을 버릴 때
`Scene::shutdown()` 을 부르지 않는다(다른 넷은 부른다). 그런데 `~Scene()` 이 `Scene::shutdown()`
을 부르고 `releaseDefaultMaterial()` 은 경로를 비운 뒤라 두 번 불러도 안전하다 — 그래서 이
비대칭은 눈에 띄지만 동작 결함은 아니다. `_mapPrefabSource` 에 남는 항목도 마찬가지다:
오브젝트 ID 가 `_s_nextObjectId` 로 단조 증가라 재사용되지 않으므로 잘못된 프리팹이 붙을 수 없고,
`instantiate` 는 씬 로드마다 한 번이고 `shutdown()` 이 비운다.

**적어 두는 것 하나.** `SceneDocument::loadXml` 은 `getResourceManager().getAssetFormatRegistry()`
를 **가드 없이** 부르는데, 같은 함수의 GUID 해석 블록은 `areEngineServicesBound()` 로 감싸고 있고
`loadBinary` 쪽도 감싼다. 서비스가 안 붙은 채로 XML 씬을 읽으면 그 자리에서 assert 다. 지금
호출부는 모두 서비스가 붙은 상태라 도달하지 않아 손대지 않았다 — `SceneDocument` 를 단독 도구에서
쓰려 할 때 여기를 먼저 볼 것.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `SceneAsyncTest` 8 → 9건 · `SceneTest` 12 → 13건.

### 2026-09-18 (실패를 성공으로 적어 두는 자리가 다섯 — Engine/Resource)

이 폴더의 결함은 전부 한 모양이었다. **"끝났다" 와 "됐다" 를 구별하지 않거나, 파일에서 읽은
수를 그대로 믿는다.** 다섯 건 전부 회귀 테스트와 변이 테스트로 확인했다.

**1) 한 번 실패한 에셋은 영원히 "로드됨" 이었다.** `AssetStreamingQueue` 의 결과 표는
`경로 → 성공 여부` 인데 이름이 `_mapLoadedAsset` 이었고, 읽는 곳 셋 중 **둘이 이름을 믿고 키만**
보고 있었다. `isLoaded()` 는 실패한 경로에 true 를 돌려줬고, 더 나쁜 것은 `requestAsset()` 이
키가 있으면 곧장 `onComplete( path, true )` 를 부르고 끝냈다는 것이다 — 아직 굽지 않은 셰이더,
늦게 마운트되는 팩을 한 번 헛읽으면 **다시는 디스크를 보지 않았다.** 표 이름을 `_mapAssetResult`
로 바꾸고, 성공한 것만 즉답하고 실패는 재요청으로 흘려보낸다.
`sweepUnusedCache()` → `clearCompletionRecord()`: 이 큐는 에셋 바이트를 들고 있지 않아서 버릴
"쓰지 않는 캐시" 자체가 없었다(전부 지우면서 고른다고 말하고 있었다).
테스트 `AssetStreamingTest.FailedRequestIsNotLoadedAndRetries`.

**2) DDS 로더가 못 알아본 포맷을 성공으로 돌려줬다.** 스위치의 `default:` 가 경고 한 줄만 남기고
빠졌고, 함수는 `_dxgiFormat == 0`(DXGI_FORMAT_UNKNOWN) 인 채로 true 를 돌려줬다. `isValid()` 도
포맷을 보지 않아(바이트·가로·세로만) 호출부에서도 걸러지지 않았다.
**이것이 실제로 이 저장소의 DDS 다섯 개에 걸려 있었다** — `engine/textures/perlin.dds` 와
`skybox/env*.dds` 의 `dwFourCC` 는 네 글자 코드가 아니라 **D3DFMT 열거값(113 · 116)** 이다.
D3D9 시절 라이터가 부동소수점 포맷에 이름 대신 정수를 밀어 넣던 관행이라 값이 0x71 같은 작은
수로 보인다. `ResourceTest.DdsLoaderLoadFromResource` 는 perlin 을 성공으로 확인하고 있었지만
포맷은 보지 않아서 **틀린 이유로 통과**하고 있었다. 레거시 부동소수점 여섯(111–116)을 매핑했고,
포맷을 못 정하면 실패로 끝낸다. 실패 경로가 여섯 군데라 "빠져나갈 때마다 비우기" 를 사람이
지키는 대신, 지역 변수에 파싱하고 **성공했을 때만** 출력에 옮긴다.
테스트 `ResourceTest.DdsLoaderRejectsUnknownPixelFormat` + perlin 테스트에 포맷 단언 추가.

**3) 그 연장선 — 스플래시 창은 32bpp 를 전제하는데 아무도 확인하지 않았다.** Win32 경로는
`StretchDIBits` 에 `biBitCount=32` 로 넘기기 전에 폭×높이 개의 픽셀을 **4바이트씩 제자리에서
뒤집는다.** 압축 텍스처가 들어오면(BC1 은 같은 크기의 1/8) 그 루프가 버퍼 밖을 **쓴다.**
지금 들어 있는 `editor/textures/splash.dds` 는 B8G8R8A8(DXGI 87) 이라 맞지만, 아트를 갈아
끼우며 압축으로 저장하는 것은 흔한 일이다. `ISplashWindow::loadSplashImage()` 에서 막는다.
(파일은 `Engine/Window` 소속이지만 같은 결함 사슬이라 여기서 같이 고쳤다.)

**4) 팩 리더가 헤더의 수를 그대로 믿었다.** `_fileCount` · `_indexOffset` · `_stringPoolSize` 는
**파일에서 온 값**인데 검사 없이 `resize` 로 들어갔다 — 잘린 팩 하나가 수십 기가짜리 할당
요청이 된다(변이 테스트에서 그 한 케이스가 6초를 먹었다). 그리고 헤더는 인덱스 크기를
`_indexSize` 로도 말하는데 **리더는 그 값을 읽지도 않았다** — 쿠커와 리더가 레이아웃을 다르게
봐도 아무도 몰랐다. `validateHeaderGeometry()` 로 파일 크기와 대조하고 둘을 맞춰 본다.
같은 함수에서 스트링 풀도 고쳤다: 엔트리의 디버그 경로를 `const utf8*` 로 넘기면 `string` 이
NUL 을 찾아 **풀 밖까지** 훑는다 — 시작 오프셋만 검사해서는 끝을 보장하지 못한다.
테스트 `ResourcePackTest.CorruptHeaderGeometryIsRejected` · `StringPoolReadStopsAtPoolEnd`.

**5) `AssetDatabase` 가 표 안의 원소를 가리키는 포인터를 잠금 밖으로 내보냈다.**
`getGuid()`/`getPath()` 는 `shared_lock` 을 놓은 **뒤에** `&it->second` 를 돌려줬다.
`_mapPathToGuid` 는 `sw::map` — 기본 빌드에서 **정렬된 벡터**이고, `_mapGuidToPath` 는
`sw::unordered_map` — **밀집 배열**이다. 그러니 다른 스레드의 등록 하나가 원소를 통째로 옮긴다
(앞 키 자리에 하나만 끼어들어도 그 뒤가 전부 밀린다). 잠금을 건다는 것은 동시 변경을 예상한다는
뜻인데, 그 잠금이 지키지 못하는 것을 내주고 있었다. **호출부 다섯 곳은 전부 받자마자 값을
복사하고 있었으므로**(SceneDocument 넷 · PrefabAsset 둘) 빌려 주는 쪽을 없애고 이미 있던
복사 쌍둥이 `tryGetGuid`/`tryGetPath` 로 옮겼다 — 동작은 그대로고 함정만 사라진다.
바로 아래 Reflection 항목의 "타입 표는 밀집 배열이다" 와 같은 뿌리다(`findType()` 이 내준 `const TypeInfo*` 도 같은 이유로 무효가 된다).

같은 함수에서 **정규화 비대칭**도 고쳤다: 넣는 쪽 셋(`ensureMeta` · `registerMapping` ·
`registerExisting`)은 전부 `normalizePath` 를 거친 키를 넣는데 `tryGetGuid` 만 받은 문자열을
그대로 찾고 있었다. 씬 XML 의 `prefab` 속성처럼 사람이 적은 값에 대문자가 섞이면 등록돼
있는데도 못 찾고 GUID 가 조용히 비었다. 테스트 `ResourceTest.AssetDatabaseLookupNormalizesPath`.

**고치지 않고 적어 두는 것 하나.** `Resource/engine/textures/random/blend.dds` 는 **DDS 가 아니라
64KB 짜리 GitHub HTML 페이지**다(`<!DOCTYPE html>` 로 시작한다 — 받다 만 파일이 그대로 커밋됐다).
코드에서 이름으로 참조하는 곳은 없다. 지우는 것은 아트 자산 판단이라 손대지 않았다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `EngineTest` 회귀 5건 추가.

### 2026-09-18 (TypeInfo 의 지연 캐시를 워커 둘이 동시에 만들 수 있었다 — Engine/Reflection)

**1) `TypeInfo` 의 조회 캐시는 `const` 객체에서 잠금 없이 만들어진다.** 이름→프로퍼티/메서드 맵
(`buildLookupCache`)과 상속 병합 프로퍼티 목록(`getPropertiesWithBase`)은 **첫 조회 때** `mutable`
멤버에 채워진다. 그 채우기에 잠금이 없고 `_bIsCacheBuilt` 도 평범한 비트필드라, 워커 둘이 같은
타입을 처음 조회하면 **같은 `unordered_map` 에 동시에 삽입한다.** 병렬 틱 중 컴포넌트가 이름으로
프로퍼티를 찾는 경로(직렬화 · 인스펙터 · `addComponentByName`)가 그 창이다.

등록 배치가 끝난 직후 **단일 스레드에서** 한 번 만들어 그 창을 없앤다
(`TypeRegistry::buildLookupCaches()`, `registerPendingTypes` 끝에서 호출).

**2) 그리고 그 과정에서 더 중요한 것이 드러났다 — 타입 표는 밀집 배열이다.**
`_mapFqnToClassType` 은 `sw::unordered_map` 인데, 이 저장소의 기본(`SW_ENABLE_STL_CONTAINER=OFF`)은
**std 가 아니라 데이터 지향 밀집 배열**이다. 그래서 표가 커질 때 원소를 **옮긴다**. 그러면:

- `TypeInfo` 이동 생성자가 `mutable` 캐시를 비우므로, **뒤이은 등록 하나가 앞서 만든 캐시를 전부
  날린다.** 처음에는 `registerClass` 안에서 캐시를 만들게 했는데, 실제로 돌려 보니 등록 직후엔
  `built=1` 인데 테스트 시점엔 주소가 다르고 `built=0` 이었다 — 그렇게 알았다. 그래서 "등록하는
  자리에서 하나씩" 이 아니라 **"배치가 끝난 뒤 한 번"** 이 유일하게 성립하는 자리다.
- 같은 이유로 `findType()` 이 내준 `const TypeInfo*` 는 **다음 등록에서 무효가 된다.** 저장소는
  이미 이것 때문에 한 번 데었다(`registerClass` 주석: 같은 타입에 복사본이 둘이라 컴포넌트 풀이
  조회에 실패해 힙을 깨뜨렸다). `GameObjectManager::rebindAllCachedTypeInfo()` 가 그래서 있다.
  **이 사실이 어디에도 적혀 있지 않았다** — `TypeRegistry::buildLookupCaches` 문서에 적었다.

**테스트** `ReflectionTypeRegistryTest.LookupCachesAreBuiltAfterRegistrationBatch` — 배치 뒤에는
등록된 타입 전부가 캐시를 갖는다. 배치 패스에서 만들기를 빼면 깨진다(변이 테스트로 확인).

**이름에 대해.** 처음에 `warmLookupCaches()` 라고 지었다가 `buildLookupCaches()` 로 바꿨다.
`warm/warmup` 은 상용 엔진에도 있지만(Unity `ShaderVariantCollection.WarmUp`, UE 의 PSO 캐시 워밍)
그것은 **미리 컴파일해 히칭을 없앤다**는 은유다. 여기 목적은 성능이 아니라 병렬 접근 전에 만들어
두는 것이고, 무엇보다 바로 옆에 `TypeInfo::buildLookupCache()` 가 이미 있다 — 한 개념에 동사 하나다
(AGENTS 의 함수 이름 어휘).

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `ReflectionTest` 101 → 102건.

### 2026-09-18 (형제 스윕 둘이 빗나갔을 때 다른 것을 남겼다 — Engine/Physics)

**1) `sweepAabb` 는 빗나가도 결과 구조체를 비우지 않았다.** 형제 함수 `sweepSphere` 는 처음부터
`outHit = SweepHit{}` 로 시작한다. 그래서 **같은 구조체로 여러 대상을 훑으면**(자연스러운 쓰임이다)
`sweepAabb` 가 `false` 를 돌려준 뒤에도 이전 충돌의 `_bHit` 이 그대로 남는다. 지금 `PhysicsWorld` 는
반복마다 새 구조체를 만들어 물리지 않지만, 두 형제가 **다른 약속**을 하고 있으면 어느 관례로 쓰는지가
호출자마다 갈린다. 둘 다 비우게 하고 그 계약을 헤더에 적었다.
**회귀 테스트** `PhysicsTest.MissedSweepLeavesNoStaleHit` — 맞힌 뒤 같은 구조체로 빗나가 본다.
비우기를 되돌리면 깨진다(변이 테스트로 확인).

**2) 슬랩 검사 22줄이 여섯 벌이었다** (두 함수 × 세 축). 축마다 부호와 첨자만 다른 같은 코드라,
한 축을 잘못 적어도 나머지 다섯과 나란히 놓고 보지 않는 한 보이지 않는다 — 증상이 "특정 방향에서만
안 맞는다" 라서 가장 찾기 어려운 종류다. `clipSlab`(축 하나) / `clipAllSlabs`(세 축) 로 모았고,
상자를 부풀리는 민코프스키 합도 `expandBox` 하나로 모았다. 기존 물리 테스트 10건이 그대로 통과한다
(동작을 바꾸지 않았다는 증거다).

**따라가 본 것 — 맞았다.** `AABB::infinite()` 가 `MathUtil::MinFloat` 을 쓰는데, 이것이 C 의
`FLT_MIN`(가장 작은 **양수**)이었다면 `infinite()` 가 양의 팔분공간만 덮는 조용한 버그가 된다.
`MinFloat = std::numeric_limits<float32>::lowest()` 라 문제없다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `PhysicsTest` 10 → 11건.

### 2026-09-18 (한 이름이 두 가지 뜻이라 린트가 일부러 눈을 감는 자리 — Engine/Object)

**동작 결함을 찾지 못했다.** 이 폴더는 이 저장소에서 **가장 촘촘히 테스트된 곳**이다
(`GameObjectManagerTest` 8 · `GameObjectPoolTest` · `ComponentPoolTest` 3 ·
`GameObjectManagerPoolTest` 3 · `ComponentTickGroupTest` · `ComponentSubTickHybridTest` 등).
따라간 것과 그 근거:

- **지연 큐의 순서가 맞다.** `finishTick` 은 ① 병렬 읽기 해제 → ② `_bTicking=false` →
  ③ 지연 트랜스폼 → ④ 지연 포스트틱 → ⑤ `mergePendingAdds` → ⑥ dirty 면 다시 flush →
  ⑦ 지연 파괴 순이다. 인스턴스가 살아 있는 동안 트랜스폼을 적용하고, 그 다음에 파괴한다.
- **지연 큐는 swap 으로 비운다.** 콜백이 큐에 또 넣어도 이번 순회를 건드리지 않는다.
- **`isStructuralMutationFrozen()` 은 포스트틱 실행 시점에 이미 false 다.** 그래서
  `executeOrDeferPostTick` 을 포스트틱 안에서 부르면 즉시 실행이라 굶지 않는다.
- `addComponent` 가 틱 중 `nullptr` 을 돌려주는 계약(CLAUDE.md 의 함정)은 호출부 전부가
  실제로 검사하고 있었다(`Scene` · `BenchScene` 등 — 확인했다).

**고친 것 하나 — 그리고 왜 기계가 못 잡는지.** `PrefabAsset::isValid()` 가 `_bValid != 0` 을,
`FrameRenderer` 가 `packet._bValid == 0` 을 쓰고 있었다. 규칙은 `SW_TRUE`/`SW_FALSE` 다
(AGENTS: "값이 아니라 의미를 적는다"). `Style/BitfieldBoolean` 린트는 이것을 **일부러 건너뛴다** —
`_bValid` 라는 이름이 이 저장소에서 두 가지이기 때문이다:

| 이름 | 타입 | 자리 |
|------|------|------|
| `_bValid` | `uint8 : 1` | `PrefabAsset` · `RenderFramePacket` |
| `_bValid` | `bool` | `EditorWorkspace` · `SceneDocument` |

린트는 이름으로만 판정하므로, 같은 이름이 진짜 `bool` 로도 선언돼 있으면 어느 쪽인지 단정할 수
없어 건너뛴다(오탐보다 누락이 낫다는 설계다 — 그 판단 자체는 맞다). **그래서 이 두 자리는 사람이
지켜야 한다.** 고쳤고, 선언 옆에 왜 손으로 지켜야 하는지 적었다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · 헤더 23개 자립.

### 2026-09-18 (같은 병합을 두 번 했고, 한 파일은 include 게이트가 보지 못한다 — Engine/Module)

**동작 결함을 찾지 못했다.** 따라간 것은 전부 맞다:

- `ModuleHandleProvider` 가 잠금을 쓰지 않는 이유(지연 로드 훅은 **로더 락 안에서** 돈다)와
  `atomic` 포인터 하나로 충분하다는 판단이 맞고, 주석도 그렇게 적혀 있다.
- `unregisterModuleTypes` 가 **인스턴스를 먼저 지우고** 팩토리를 걷는 순서가 맞다 — 반대로 하면
  소멸자가 사라진 객체가 남는다. 그리고 그 함수는 `#if !defined( SW_SHIPPING )` 안에 있다(배포본은
  모듈을 정적 링크하므로 내릴 일이 없다).
- 지연 로드 훅의 폴백(제공자 없음 · 그래프 깨짐 → Bin 에서 직접 로드)도 맞다.

**고친 것 둘.**

**1) 같은 18줄짜리 병합이 두 벌이었다.** `registerModuleTypes` 의 두 오버로드가 **각자** "캐시에
있으면 가져오고 없으면 넣는" 병합을 했다 — 조건만 뒤집혀 있을 뿐 같은 로직이고, 인자 없는 쪽은
그 병합을 한 뒤 **다른 쪽을 불러 또 병합**했다. 인자 없는 쪽은 이제 전역 머리를 걷어 넘기고
비우기만 한다. 병합은 한 자리다.

**2) `DelayLoadNotifyHook.cpp` 의 include 순서가 어긋나 있었는데 게이트가 보지 못했다.**
`CheckIncludeOrder` 는 **첫 `#if` 를 경계로** 삼는다(그 뒤 include 는 조건부라 순서를 강제할 수
없다는 판단이다). 그런데 이 파일은 `#include "pch.h"` 말고는 **전부 `#if` 안**에 있어서, 게이트가
보는 include 가 하나도 없다 — 그래서 `Engine/` 이 `Core/` 보다 앞에 오고, `Core/File` 이
`Core/Common` 보다 앞에 와도 아무도 몰랐다. 손으로 고치고 왜 손으로 지켜야 하는지 파일에 적었다.

**게이트는 고치지 않았다.** 이 상태인 파일을 전부 세어 보니 **3개**뿐이고, 둘
(`Core/Common/PlatformOsHeaders.h` · `Core/Common/X11MacroUndef.h`)은 플랫폼 헤더 우산이라 지금
모양이 맞다. 셋 중 하나 때문에 경계 규칙을 바꾸면 트리 전체에 오탐이 날 위험이 그 값어치보다 크다.
**플랫폼 전용 `.cpp` 를 새로 만들 때는 이 대목을 기억할 것** — 그 파일의 include 순서는 아무도 안 본다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · 헤더 2개 자립.

### 2026-09-18 (같은 내용을 두 번 구우면 다른 파일이 나왔다 — Engine/Localization)

**1) 바이너리 로컬라이제이션 팩이 결정적이지 않았다.** `StringTable::saveToBinaryBuffer` 는
`unordered_map<uint64, string>` 을, `LocalizationManager::saveToBinaryPack` 은
`unordered_map<string, ...>` 을 **순회 순서 그대로** 적었다. 그 순서는 삽입 순서와 할당 상황에
따라 달라지므로, **같은 내용을 두 번 구워도 파일 바이트가 달라진다.** 미리 구워 두는 산출물
(`localization.loc.bin` — 디렉터리에 있으면 텍스트보다 먼저 읽는다)에서 그것은 diff·캐시·검증을
전부 무의미하게 만든다. 키 해시 순 · 언어 코드 순으로 정렬해 적는다.

**회귀 테스트** `LocalizationManagerTest.BinaryPackIsDeterministic` — 넣는 순서만 뒤집어 구워 보고
바이트가 같은지 본다(테이블과 팩 양쪽). 정렬을 빼면 깨지는 것을 변이 테스트로 확인했다.

**2) 앞선 Dialogue 작업에서 들어간 것 둘을 바로잡았다.**
- `LocalizationManager::getString( hashed_string )` 이 문자열 뷰로 내려가면서 **이미 들고 있는
  해시를 버리고 다시 계산**하게 돼 있었다. 공통 경로를 `findByHash( uint64 )` 로 바꿔 두
  오버로드가 각자 방식으로 해시를 구해 넘긴다 — intern 된 키는 O(1) 그대로다.
  활성 언어와 폴백 언어 두 테이블을 훑을 때도 해시를 한 번만 구한다.
- `findInActiveThenFallback` 선언이 **멤버 변수들 사이에** 들어가 있었다(AGENTS: 비공개 함수는
  별도 구역, 멤버 변수가 마지막). 위로 옮겼다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `LocalizationManagerTest` 신규 1건 · 헤더 2개 자립.

### 2026-09-18 (같은 입력을 두 번 저장하면 파일 바이트가 달라졌다 — Engine/Input, 그리고 린트 오탐 하나)

**1) `InputSnapshot::serialize` 가 구조체를 패딩째로 내보냈다.** `_tickNumber`(uint32) 뒤에는
`_buttonMask`(uint64) 정렬을 맞추려는 **패딩 4바이트**가 있다. 그 자리는 아무도 값을 정하지
않으므로, 구조체를 통째로 `memcpy` 하면 **그때 그 메모리에 있던 것이 그대로 파일과 네트워크로
나간다.** 그래서 같은 입력을 두 번 저장해도 바이트가 달라질 수 있었다 — 리플레이 비교·체크섬·중복
제거가 성립하지 않고, 넷코드로 나가면 그 자리에 있던 메모리가 함께 나간다. 헤더가 스스로
"롤백 넷코드 및 리플레이 재생을 위한" 이라고 적어 둔 구조체다.

필드를 순서대로 적게 했다(`kSerializedSize` = 36, `sizeof` = 40). 컴파일러·아키텍처가 달라도 같은
바이트가 나온다. **회귀 테스트**는 서로 다른 쓰레기로 더럽힌 저장 공간에 같은 값을 넣고 바이트가
같은지 본다(`EnhancedInput_SnapshotSerializationIsDeterministic`) — 되돌리면 깨지는 것을 확인했다.

**2) `InputReplay::loadFromFile` 이 매직만 보고 버전을 보지 않았다.** `ReplayHeader` 에 `_version`
필드가 있는데 **아무도 읽지 않았다** — 다른 판의 파일도 지금 판의 배치로 읽어 조용히 엉뚱한
프레임이 나온다. 버전 필드를 두고 쓰지 않은 셈이다. 이제 판이 다르면 거절하고 로그를 남긴다.
1)로 프레임 배치가 바뀌었으므로 판을 2 로 올렸다.

**3) 링버퍼 용량이 2의 거듭제곱이어야 하는데 아무도 확인하지 않았다.** `& (kDefaultCapacity - 1)`
로 감는다 — 아니면 조용히 어긋난다. `static_assert` 를 뒀다.

**4) `_buttonMask{ SW_FALSE }`.** 64비트 비트마스크를 불리언 상수로 초기화하고 있었다. 값은 0 으로
같지만 이름이 거짓말을 한다.

**그리고 — 4)를 고치자 린트가 그것을 지적했다. 린트 쪽이 틀렸다.**
`Style/BitfieldBoolean` 은 `_b` 로 시작하는 이름을 **뒤에 무엇이 오든** uint8 불리언으로 봤다.
그래서 `_buttonMask` · `_bytesWritten` 처럼 `_b` 다음이 소문자인 평범한 이름(비트마스크·바이트
버퍼)이 걸렸다. 게다가 이름 집합이 **트리 전역**이라, `MouseDevice::_buttonMask`(uint8) 하나가
`InputSnapshot::_buttonMask`(uint64)와 `GamepadDevice::_buttonMask`(uint32)까지 불리언으로 만들었다.
- 이름은 `_b` **다음이 대문자**여야 본다(저장소 규칙이 `_bPascalCase` 다).
- 같은 이름이 **다른 폭의 정수**로도 선언돼 있으면 건너뛴다(`bool` 과 같은 이유다).

**자기검사에 전체 스캔 clean 케이스가 없었다 — 그래서 이 오탐을 아무도 보고 있지 않았다.**
기존 오탐 검사는 **파일 하나**만 넘기는데, `Style/BitfieldBoolean` · `Naming/DuplicateInternalHelper` ·
`Style/HeaderMemberInitializer` 는 **전체 스캔일 때만 도는 규칙**이라 그 검사에서는 아예 돌지
않았다. 트리 단위 clean 케이스를 새로 넣었고, 위 두 수정을 각각 되돌리면 각각 깨지는 것을 변이
테스트로 확인했다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `GameFrameworkTest.EnhancedInput*` 10건 · 헤더 15개 자립.

### 2026-09-18 (에디터 프리뷰가 머티리얼을 잡기만 하고 놓지 않았다 — Engine/Graphics)

**Graphics 는 이미 두 차례 구조 작업을 거쳤다**(2026-09-13 두 커밋). 이번 훑기는 그 위에서
**기계적 검사 + 소유·계약이 걸린 자리 정독**으로 했고, 깊이를 그대로 적어 둔다 — 42,770줄을 한
사람이 정독한 것이 아니다.

**기계로 물어본 것(전부 깨끗했다).** TODO/FIXME/HACK 0건 · 헤더 안 매직 버퍼 0건 · 헤더 자립 94/94 ·
`ENUM(Flags)` 트레이트 0건 · 여러 파일에 흩어진 같은 상수(뷰포트 기본값 넷이 백엔드 셋에 각각
있지만 API 가 정한 0~1 이라 합칠 값이 없다) · PSO 캐시의 `_bOwned` 소유 표식(정확하고 주석도 맞다).

**정독한 자리.** `Upload/GpuUploadQueue`(스레드 경계) · `Texture/TextureCache` · `Material/MaterialCache` ·
`Material::acquire/releaseTextureAssets`(획득-해제 짝) · `Renderer/Frame/RenderPsoCache`(소유) ·
`Shader/Binding`(이미 `ShaderBindingContractTest` 가 지킨다).

**1) `EditorViewportPreview::applyMaterial` 이 `acquire` 만 하고 `release` 를 하지 않았다.**
머티리얼 패널에서 한 번 편집할 때마다 참조가 하나씩 올라갔고, 그러면 그 머티리얼은 참조가 0 에
닿지 못해 **캐시에서 영영 지워지지 않는다**. 프리뷰가 드는 참조는 하나뿐이도록 고쳤다 — 같은
경로면 다시 잡지 않고, 다른 경로로 갈 때는 새 것을 메시에 건 **뒤에** 옛 것을 놓는다(먼저 놓으면
참조가 0 이 되어 캐시가 지우는데 메시가 아직 그 포인터를 들고 있다).
**자동 검증이 없다** — `EditorTest` 는 `EditorViewportPreview.cpp` 를 링크하지 않는다(ImGui 를 탄다).

**2) `MaterialCache` 와 `TextureCache` 는 같은 모양인데 어디가 일부러 다른지 아무 데도 없었다.**
둘 다 경로를 키로 참조를 세고 0 에서 지운다. 그런데 갈라지는 지점이 셋 있다 — 소유가
`shared_ptr`/`unique_ptr` 이라 `release` 가 GPU 를 내리느냐가 다르고, 디바이스를 캐시가 기억하느냐가
다르고, `acquire` 가 먼저 세느냐 나중에 세느냐가 다르다. **전부 이유가 있는 차이인데 적혀 있지
않아서**, 다음 사람이 "한쪽만 고쳐졌나" 로 읽고 맞춰 버릴 수 있다. 헤더에 적었고 서로를 가리키게 했다.

참조 감소 가드(`0 에서 한 번 더 내리면 42억`)는 `TextureCache` 에만 있었다. `MaterialCache` 에도
넣었지만 **결함을 고친 것이 아니다** — 참조가 0 이면 항목을 그 자리에서 지우므로 지금은 도달할 수
없다. 변이 테스트가 그것을 알려 줬다(가드를 빼도 테스트가 통과했다). 방어로 남기고 주석에 그렇게
적었다.

**3) `isCached()` 를 둘 다에 넣었다.** 참조 계수 규율을 **밖에서 확인할 수 있는 손잡이가 없었다** —
그래서 `MaterialCacheAcquireReleaseNoGpu` 는 acquire/release 를 부르기만 하고 그 결과를 아무것도
확인하지 못했다. 이제 "두 번 잡고 두 번 놓으면 사라진다" 를 실제로 본다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · 헤더 94개 자립.

### 2026-09-18 (디스크에 저장되는 핀 번호 계약이 두 파일에 따로 적혀 있었다 — Engine/Dialogue)

**1) 핀 번호를 만드는 쪽과 읽는 쪽이 각자 적고 있었다.** 대화 링크는 `노드 id * 100 + 오프셋` 으로
핀을 가리키고 그 숫자가 **그대로 디스크에 저장된다.** 그런데 인코딩(`nodeId * 100 + offset`)과 오프셋
상수(`In=1 · Out=2 · True=3 · False=4 · ChoiceBase=10`)가 `Editor/Panels/DialogueGraphPanel.cpp` 와
`Engine/Dialogue/DialogueGraphAsset.cpp` **양쪽에 따로** 있었다. 한쪽만 바뀌면 대화가 조용히 엉뚱한
분기를 탄다 — 증상이 "가끔 다른 대사가 나온다" 라서 재현도 추적도 가장 어려운 종류다.

계약을 애셋으로 모았다(`kPinScale` · `kPinOffset*` · `encodePin` · `encodeChoicePin` ·
`decodePin*`). 패널은 이제 그것을 부르고, 상수는 그 이름만 짧게 빌린다. 애셋이 on-disk 포맷의
주인이므로 계약이 있어야 할 자리다.

**2) 선택지가 90개를 넘으면 링크가 다른 노드를 가리켰다.** 선택지 핀은 `오프셋 = 10 + 번호` 인데
오프셋이 100 을 넘으면 **자릿수를 넘어 노드 id 를 오염시킨다**(`nodeId*100 + 110` = 다음 노드의 10번
핀). `_listChoice` 에는 상한이 없었고 검사도 없었다. `encodePin` 이 담기지 않는 오프셋에 0(없는 핀)을
돌려주고, `findChoiceNextNodeId` 는 범위 밖 번호를 기본 출력으로 떨어뜨린다.

**3) 노드 타입 이름을 짓는 쪽과 읽는 쪽이 목록을 따로 들고 있었다.** `nodeTypeName` 은 switch 여섯
갈래, `parseNodeType` 은 if 다섯 줄이었다. 열거자를 하나 더하고 한쪽만 고치면 그 노드는 **저장은
되는데 읽을 때 조용히 `Dialogue` 로 떨어진다** — 파일은 멀쩡한데 대화만 달라진다. 표 하나
(`kArrNodeTypeName`)를 둘이 함께 본다.

**4) 대사 원문이 intern 아레나에 영구히 쌓이고 있었다.** `resolveLocalizedText` 는 "이 텍스트가
로컬라이즈 키인가" 를 물으려고 그 텍스트로 `hashed_string` 을 만들었다. `hashed_string` 은 **intern**
이므로 키가 아닌 평범한 대사까지 아레나에 영구 적재된다 — 대화 콘텐츠가 늘수록 함께 늘어나는 누수다.
그런데 `StringTable` 은 애초에 **해시로만 열리는 표**라 조회에 intern 이 필요 없었다.

- `basic_hashed_string::computeHash( string_view )` — intern 없이 같은 해시를 계산한다.
- `StringTable::getString( string_view )` · `LocalizationManager::getString( string_view )` 추가.
  두 오버로드가 `findByHash` / `findInActiveThenFallback` 한 경로를 공유하므로 조회 규칙이 갈라지지 않는다.
- **조회가 영구 할당을 하지 않는다** 는 것이 요점이다.

**5) 껍데기 하나.** `DialogueGraphAssetInternal` 은 자기 클래스의 public static 을 그대로 다시 부르는
함수 둘뿐이었다. 지웠고, 그 자리에 3)의 표가 들어갔다.
`loadFromFile` 의 이중 파싱(`parse → dump → parse`)도 `AnimationGraphAsset` 과 같은 방식으로 없앴다.

**테스트 6건 신규 — `DialogueGraphTest` (`Test/EngineTest/TestDialogueGraph.cpp`).** 이 애셋에는
테스트가 **하나도** 없었는데 `DialogueRunnerComponent`(실제 게임플레이)와 에디터 패널이 쓰고 있었다.
핀 왕복 · 자릿수 넘침 거절 · 노드 타입 왕복(전 열거자) · 선택지/기본 출력 따라가기 · JSON 왕복과
`loadFromFile` 일치 · 키가 아닌 원문 통과. 변이 테스트로 둘을 확인했다 — 넘침 가드를 빼면 2)의
케이스가, `parseNodeType` 을 표에서 떼어 내면 3)의 케이스가 깨진다.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `DialogueGraphTest` 6건 · 헤더 자립 3/3.

### 2026-09-18 (설정의 정체성이 호출부마다 손으로 적는 문자열이었다 — Engine/Config)

**동작 결함은 찾지 못했다** — 따라간 것들은 맞았다. `resolveConfigPath` 의 네 단계(절대 → 작업
디렉터리 → 프로젝트 루트 → 실행 파일 옆)는 올바르고, `FrameTimeline::configure` 가 0 이하의
`_fixedDeltaTime` · `_maxFrameDeltaTime` · `_maxFixedStepPerFrame` 을 내장 기본값으로 바꾸므로
**설정 파일 하나가 프레임 루프를 세우지 못한다**(그 사실을 헤더에 적어 뒀다). `SW_SHIPPING` 매크로도
실제로 정의된다(`cmake/Engine/BuildLayout.cmake`) — Shipping 이 디스크 `Config/` 를 보지 않는 것은
의도대로다. 대신 **구조 하나와 테스트 없음 하나**가 있었다.

**1) 설정의 열쇠가 호출부마다 손으로 적는 문자열이었다.** `ensureConfig<EngineConfig>(
hashed_string( "EngineConfig" ), ... )` 처럼 타입과 이름을 **둘 다** 넘겼고, 그 글자가
`EngineLoop.cpp` · `App.cpp` · `Test/TestFramework/main.cpp` 세 곳에 따로 있었다. 한 곳만 철자가
어긋나면 `getConfig` 가 조용히 nullptr 을 돌려주고, App 은 이유를 말하지 못한 채 기동을 멈춘다.

게다가 표의 열쇠가 그 이름의 **해시**였다(`name.getHash()`). `hashed_string` 자신은 intern
인덱스로 비교하므로 충돌이 없는데, 표만 해시를 쓰고 있었다 — 서로 다른 이름이 같은 칸을 가리킬 수
있었고, 그때 `getConfig<T>` 의 무검사 `static_cast<T*>` 는 **다른 타입의 객체를 T 로 읽는다.**

열쇠를 타입에서 뽑게 했다: `T::StaticType()->_fullyQualifiedName.getIndex()`. 이름 인자는 없앴다.
그래서 (a) 철자가 어긋날 자리가 없고, (b) 열쇠가 intern 인덱스라 충돌이 없으며, (c) 표에 담긴 것이
T 가 아닐 수 없으므로 `static_cast` 가 안전하다. 호출부 셋이 전부 짧아졌다.

**2) `ConfigManager` 에 단위 테스트가 하나도 없었다.** 186줄짜리 헤더가 경로 해석 · Shipping/Dev
분기 · 폴백 사슬을 다 들고 있는데, 잘못되면 증상이 "창 크기·VSync·리소스 우선순위가 조용히
기본값이 된다" 라서 실행해 보고도 원인을 짚기 어렵다. 실제로 **바로 그 버그가 예전에 있었다**
(`setRootDirectory` 가 생긴 이유 — 헤더의 긴 주석 참고). 그 수정을 지키는 테스트가 없었다.

`ConfigManagerTest` 3건 신규 (`Test/EngineTest/TestConfigManager.cpp`):
- `ConfigTableIsKeyedByType` — 타입이 열쇠다. 다른 설정 타입은 같은 표에 섞이지 않는다.
- `RelativePathResolvesAgainstRootDirectory` — 루트를 안 주면 못 찾고, 주면 찾고, 절대 경로는
  루트와 무관하다. **Shipping 에서는 스킵**한다(디스크 Config/ 를 보지 않으므로).
- `MissingFileFallsBackToBakedThenCppDefaults` — 베이크 → C++ 기본값 순. 깨진 JSON 은 로드 실패지
  절반만 채워 넣지 않는다.

변이 테스트로 확인했다 — `resolveConfigPath` 의 루트 디렉터리 분기를 지우면 둘째가 깨진다.

**3) 문서.** `IConfig` 에 파일·타입 `@brief` 가 없었고(파생 타입이 왜 `REFLECT()` 여야 하는지도),
`WindowConfig`·`EngineConfig` 의 PROPERTY 필드 대부분에 설명이 없었다. 채웠다.

**검증.** Debug·Shipping·Unity 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `ConfigManagerTest` Debug 3건 / Shipping 2건+1스킵 · 헤더 6개 자립.

### 2026-09-18 (코덱 하나는 이름만 있고 아무도 등록하지 않았다 — Engine/Compression)

**1) `ZlibCompressionCodec` 이 레지스트리에 한 번도 올라가지 않았다.** `EngineLoop::initialize` 가
"외부 라이브러리 코덱은 **여기서** 등록한다" 고 적어 두고 LZ4·Zstd 둘만 손으로 부르고 있었다.
그래서 `CompressionCodecType::Zlib`(공개된 on-disk 값)을 `CompressionStream` 에 요청하면 경고 한
줄과 함께 **무압축으로 떨어졌고**, 다른 도구가 쓴 Zlib 스트림은 "지원하지 않는 코덱" 으로 읽히지
않았다. 리소스 팩이 쓰는 코덱이라 라이브러리는 이미 링크돼 있었다 — 등록만 빠진 것이다.

한 줄 더 적는 대신 **목록을 코덱 옆으로 옮겼다**: `EngineCompressionCodecUtil::kArrCodecType` +
`registerAll()`. 호출부가 목록을 들고 있으면 코덱을 하나 더 만들고 그 자리를 잊는 일이 또 생긴다.
이제 `EngineLoop` 은 `registerAll` 하나만 부르고, 테스트도 같은 목록을 돈다.

**2) zlib 은 4GB 를 넘는 입력을 조용히 잘라서 압축하고 성공을 보고했다.** zlib 의 길이 타입
`uLong` 은 **Windows 에서 32비트**다. `static_cast<uLong>( srcSize )` 가 잘린 값을 넘기면
`compress2` 는 그만큼만 압축하고 `Z_OK` 를 돌려준다 — 데이터를 버리면서 성공이라고 말하는 셈이다.
`compressBound` 도 같아서, 잘린 크기의 한계를 돌려주면 호출자가 그것을 믿고 작은 버퍼를 잡는다.
입구에서 막고(`compressBound` 는 0, `compress`/`decompress` 는 false + 로그) 이유를 적었다.

**3) LZ4 는 압축 쪽에만 크기 검사가 있었다 — 외부 바이트를 먹는 쪽은 해제인데.** `decompress` 가
`srcSize` 를 검사 없이 `int32` 로 캐스팅했다. 2GB 를 넘으면 **음수**가 되어 LZ4 에 그대로 들어가고
그때 동작은 정의되어 있지 않다. 같은 검사를 해제 쪽에도 뒀다. 대상 용량(`dstCapacity`)은 거절하지
않고 한계까지 **좁은 쪽으로 자른다** — 실제 버퍼보다 작게 보는 것은 안전한 방향이고(넘치면 LZ4 가
실패로 끝낸다), 그냥 캐스팅하면 음수가 될 수 있다. 압축 쪽 `dstCapacity` 도 같이 맞췄다.

**4) 리소스 팩이 실제로 쓰는 코덱에 직접 테스트가 없었다.** `CompressionCodecTest` 는 LZ4·Zstd 만
왕복·손상입력을 보고 있었고 Zlib 은 `TestResourcePack` 이 팩을 굽는 김에 간접적으로만 지나갔다.
셋 다 보게 했다.

**테스트.** `CompressionCodecTest` 4 → 5건.
- `ExternalCodecRoundTrip` · `ExternalCodecRejectsCorruptInput` 에 Zlib 추가.
- `CodecsRejectSizesTheirLibraryCannotHold` 신규 — **`compressBound` 는 버퍼를 받지 않으므로 이
  한계를 메모리 없이 물어볼 수 있다.** 8GiB 를 물으면 LZ4·Zlib 은 0, Zstd 는 64비트라 0 이 아니다.
- `RegisteredExternalCodecsAreReachableFromStream` 이 이제 `kArrCodecType` 을 돈다.

변이 테스트로 확인했다(zlib bound 가드 제거 → 셋째가, Zlib 등록 제거 → 넷째가 깨진다).

**테스트가 없는 것.** `EngineLoop::initialize` 가 `registerAll` 을 **부르는지** 는 확인하지
못한다 — `EngineTest` 도 `SmokeTest` 도 `EngineLoop` 을 돌리지 않는다. 목록을 한 자리로 모은 것이
이 구멍에 대한 답이다(부르는 곳이 하나뿐이면 잊을 자리도 하나뿐이다). 2GB·4GB 실제 버퍼를 쓰는
경로도 단위 테스트가 없다 — `compressBound` 로 한계 계약만 못박았다.

**검증.** Debug·Shipping·Unity 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `CompressionCodecTest` 5건 · 헤더 4개 자립.

### 2026-09-18 (접착 파일 하나가 엔진 전체에 의존하는 척하고 있었다 — Engine/Common)

**동작 결함을 찾지 못했다.** X-매크로 서비스 등록표(`EngineServiceList.xxx`)는 이미 "목록이 정본"
으로 잘 서 있다 — 전방 선언 · 구조체 멤버 · getter · `areEngineServicesBound()` 본문 ·
`ModuleServiceId` · `ModuleServiceTraits` 가 전부 그 한 파일에서 생성된다. 따라간 것과 그 결과:

- `fillModuleServices` 는 표를 **먼저 비우고** 채우며, `ModuleHost::buildModuleService` 가 호스트
  전용 서비스를 **그 뒤에** 채운다. 순서가 맞다(뒤집혔으면 에디터가 모듈 컴파일러를 잃는다).
- `_pCommandStack` 이 `required=0` 인 이유와 `_pRenderTargetRegistry` 가 `OPT` 인 이유는 목록에
  주석으로 남아 있고, 둘 다 맞다.
- `EngineServices.h` 의 `#error` 가드가 허용하는 `SW_TOOL_INTERNAL` 은 실제로 쓰인다
  (`Tools/ReflectionParser` 가 `Engine/Common/Common.h` 를 탄다).

**고친 것 셋.**

**1) `EngineServices.cpp` 의 프로젝트 include 15개가 전부 죽어 있었다.** 이 파일이 하는 일은
포인터를 담아 두고 참조로 돌려주는 것뿐이라 **서비스 타입의 정의가 하나도 필요 없다** — 목록이
만들어 주는 전방 선언으로 충분하다. 그런데 `ShaderCache.h` · `GameObjectManager.h` ·
`ReflectionCore.h` 같은 무거운 헤더까지 끌어와서, 105줄짜리 접착 파일이 엔진 전체에 의존하는
것처럼 보였다. 하나씩 빼며 빌드해 확인했고(15/15 제거 가능), 유니티 빌드(`SW_ENABLE_UNITY_BUILD`)
로도 확인했다 — 같은 청크의 다른 `.cpp` 가 이 include 에 얹혀 있지 않다.
**주의: `.xxx` include 세 개는 빼도 컴파일된다**(함수 본문이 비어질 뿐이다). "컴파일된다 = 필요
없다" 가 성립하지 않는 자리라 기계로 지우면 안 된다.

**2) `areEngineServicesBound()` 문서가 없는 서비스를 가리키고 있었다.** "(MemoryProfiler /
GameData 는 선택)" 이라고 적혀 있는데 **`GameData` 라는 서비스는 저장소에 없다**, 그리고 실제
선택 항목인 `CommandStack`·`RenderTargetRegistry` 는 빠져 있었다. 이름을 다시 적는 대신 "목록이
정본" 이라고 적었다.

**3) `EngineDefines.h` 가 같은 뜻을 두 철자로 적고 있었다.** `constant` 블록은 `inline constexpr`
인데 `path` 블록은 `inline static constexpr` 다. 네임스페이스 스코프에서 `static` 은 내부 연결을
주므로 **`inline` 이 하는 일이 없어지고** TU 마다 사본이 생긴다. `inline constexpr const utf8*` 로
맞추고, 주석 두 개만 있던 `path` 상수 열넷에 전부 `@brief` 를 달았다.

**테스트 3건 신규 — `EngineServiceTest` (`Test/EngineTest/TestEngineService.cpp`).**
`gameAllowed` 열은 **게임 모듈이 손댈 수 있는 것과 없는 것의 경계**인데 그때까지 아무 테스트도
그 경계를 보고 있지 않았다. 검사도 같은 X-매크로에서 생성한다(목록이 정본이므로).
- `GameModuleTableHidesHostOnlyServices` — `gameAllowed=0` 은 게임 표에서 nullptr, `=1` 은 에디터
  표와 같은 포인터.
- `FillClearsTheWholeTableFirst` — 호스트 자리에 넣어 둔 값이 지워진다(= 호스트는 뒤에 채워야 한다).
- `TestHarnessBindsEveryRequiredService` — 많은 테스트가 `areEngineServicesBound()` 로 자기 본문을
  게이팅한다. 이것이 false 면 **그 테스트들이 통과한 척하며 아무것도 하지 않는다.**

변이 테스트로 확인했다: `outService = {}` 를 빼면 둘째가, 게이팅 조건을 뒤집으면 첫째가 깨진다.
**다만 `gameAllowed` 열 자체를 뒤집는 변이는 잡지 못한다** — 검사가 같은 목록에서 생성되므로
기대값도 같이 뒤집힌다. 이 테스트가 보는 것은 "`fillModuleServices` 의 구현이 목록과 같은 말을
하는가" 이지 "목록의 값이 옳은가" 가 아니다.

**검증.** Debug·Shipping·Unity 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `EngineServiceTest` 3건 · 헤더 4개 자립.

### 2026-09-18 (인터페이스로는 절반만 쓸 수 있었고, 없는 곡을 틀면 틀어져 있던 곡만 꺼졌다 — Engine/Audio)

**1) `IAudioSystem` 이 `XAudio2System` 이 하는 일의 절반만 약속하고 있었다.** 저장소 안에서
오디오를 드는 자리는 **전부 `IAudioSystem`** 이다(`EngineLoop._audioSystem` · `game::getService` ·
테스트 셋). 그런데 `setSfxVolume` · `getSfxVolume` · `setMute` · `isMuted` · `pauseMusic` ·
`resumeMusic` · `getMasterVolume` · `getMusicVolume` · `isInitialized` **아홉 개는 `XAudio2System`
에만** 있었다 — 즉 **아무도 부를 수 없었다.** 효과음 볼륨도 음소거도 손잡이가 없는 셈이다.

인터페이스로 끌어올리면서 **볼륨·음소거 상태를 `IAudioSystem` 한 자리로 옮겼다.** 0~1 클램프와
"음소거는 마스터 한 자리" 규칙을 백엔드마다 다시 적을 이유가 없다. 백엔드는 값이 바뀐 뒤
`applyVolume()` 으로 통보만 받는다 — 세 번째 백엔드가 생겨도 그 규칙을 다시 적지 않는다.

**2) 음소거가 세 자리에 걸려 있어서, 켰다 끄면 소리가 돌아오지 않았다.** `setMute` 는 마스터
보이스를 0 으로 만드는데, `setMusicVolume` · `setSfxVolume` **도** 음소거 중이면 자기 보이스를
0 으로 만들었다. 그리고 음소거를 푸는 쪽은 마스터만 되돌린다. 그래서

> 음소거 → (옵션 화면에서) 효과음 볼륨 조절 → 음소거 해제

뒤에는 **효과음 보이스가 0 인 채로 남는다.** 음소거는 이제 마스터 보이스 한 자리에만 건다.

**3) 없는 곡을 요청하면 틀어져 있던 BGM 만 꺼졌다.** `playMusic` 이 **경로를 확인하기 전에**
`stopMusic()` 을 불렀다. 요청은 `false` 를 돌려주는데 결과는 정적이다. 확인을 먼저 한다.
(이건 새 테스트를 쓰다가 잡혔다 — 훑으면서는 못 봤다.)

**4) BGM 이 아무 말 없이 시작되지 않을 수 있었다.** `playInternal` 은 디코드를 워커로 넘기고
`_musicPath` 를 **`.submit()` 뒤에**, 그것도 `_voiceMutex` 없이 적었다. 워커는 그 `_musicPath` 를
잠금 아래에서 읽어 "요청한 곡이 아니면 버린다". 클립이 캐시에 있으면 워커가 먼저 도착하므로
**자기 요청을 남의 것으로 보고 조용히 돌아간다.** 게다가 경로로 거르면 A → B → A 처럼 같은 곡으로
돌아왔을 때 늦게 온 첫 A 도 통과해 **음악 보이스가 둘**이 된다(앞의 것은 멈추지도 않는다).
요청 번호(`_musicGeneration`)로 바꾸고, 제출보다 먼저 잠금 아래에서 적는다. `stopMusic` 도 번호를
올려 날아오던 요청을 취소한다.

**5) `NullAudioSystem` 은 Windows 에서 한 번도 컴파일되지 않았다.** `IAudioSystem.cpp` 가
`#else` 안에서만 include 했다 — `FileWatcher` 의 macOS 구현과 같은 자리다. 헤더 전용이라 비용이
없으므로 **무조건 include** 하도록 바꿨다. 바꾸자마자 아무도 못 보던 경고가 하나 나왔다
(`~NullAudioSystem` 이 `override` 없이 소멸자를 재정의). 그리고 새 인터페이스를 구현하면서
`isInitialized` · `getMusicPath` 같은 상태를 실제로 들게 했다 — 이제 진짜와 같은 판정을 낸다.

**6) 잔가지.** `MFCreateMediaType` 의 결과를 보지 않고 바로 `pPartial->SetGUID` 를 불렀다(실패하면
널 역참조). `XAudio2SystemImpl` 이 **멤버 → 함수 → 멤버** 순으로 흩어져 있었고, 절반은 헤더
기본값과 생성자 초기화 목록에 **값을 두 번** 적고 있었다(이 구조체는 `.cpp` 안에 있어
`Style/HeaderMemberInitializer` 게이트가 보지 못한다). `_bInitialized` 를 `= 1` · `!= 0` 으로
쓰고 있었다(규칙은 `SW_TRUE`/`SW_FALSE`). 출력 파라미터 `out` → `outClip`, `loadWavPcm( absPath )` 는
절대 경로가 아니어도 되므로 `path`.

**7) 테스트.** `AudioSystemTest` 5 → 7건. `VolumeControls` 는 이제 **설정한 값을 되읽어** 확인한다
(예전에는 getter 가 인터페이스에 없어서 호출만 하고 아무것도 확인하지 못했다).
`MuteKeepsVolumeSettings` · `MusicPathTracksRequests` 가 새로 생겼고, 뒤의 것이 위 3)을 잡는다
(변이 테스트로 확인: `stopMusic()` 을 앞으로 되돌리면 깨진다). WAV 바이트를 손으로 쌓던 27줄이
두 케이스에 글자 그대로 복사돼 있던 것을 `writeTestWav` 하나로 모았다.

**테스트가 없는 것 두 가지.** 2)의 보이스 볼륨과 4)의 워커 경합은 **실제 오디오 장치가 있어야**
관찰된다 — 상태(값·요청 경로)까지는 위 테스트가 잡지만 보이스에 실제로 걸린 볼륨은 못 본다.
고친 근거는 위에 적어 두었고, 손대는 사람은 이 대목을 먼저 읽을 것.

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `AudioSystemTest` 7건 · 헤더 3개 자립.

### 2026-09-18 (스케일이 있는 포즈는 회전까지 틀렸고, 33번째 표본부터는 아예 없는 셈이었다 — Engine/Animation)

**Core 폴더 훑기(2026-09-17, Common → Uuid)가 끝나 Engine 으로 넘어왔다.** 같은 방식·같은
알파벳 순서다. 첫 폴더가 `Animation` (1,153줄 · 14파일).

**1) `DualQuaternion::fromMatrix` 가 스케일이 섞인 행렬에서 회전을 틀리게 뽑았다.**
`quaternion::createFromRotationMatrix` 는 **정규직교 회전 행렬**을 전제한다 — 축 길이로 나누지
않고 그대로 걸면 스케일이 회전에 새어 든다. 스케일 `(2,1,1)` 과 Z축 90° 가 섞인 포즈를 넣으면
쿼터니언이 `(0,0,1.06066,0.70711)` 로 나오고, DLB 가 정규화한 뒤에는 `(0,0,0.83205,0.5547)`,
즉 **112.6°** 다 (22.6° 틀렸다). `Core` 에 이미 축 길이로 나눈 뒤 뽑는 `float4x4::decompose` 가
있었는데 그 절반을 여기서 다시, 틀리게 적고 있었다. `decompose` 를 쓰게 했다.

**2) 그리고 듀얼 쿼터니언은 스케일을 담지 못한다 — 아무 데도 그렇게 적혀 있지 않았다.**
그래서 `BlendSpace1D::evaluate` 는 **표본 지점에서는 원본 포즈를 그대로 돌려주고**(스케일 포함)
그 사이에서만 DQ 경로를 타서 스케일이 1 로 주저앉았다. 파라미터를 0 에서 0.001 로 옮기는 것만으로
포즈가 튀었다는 뜻이다. 포즈를 (스케일, 강체 변환) 으로 가르고 — 스케일은 선형 보간, 나머지는
DLB — 다시 곱하도록 고쳤다(`BlendSpaceInternal::splitPose` / `makePose`). 1D 는 두 이웃의 선형
보간, 2D 는 IDW 가중치의 선형 결합이다. 헤더와 폴더 README 에 "DQ 는 스케일을 담지 못한다" 를
적었다.

**3) `BlendSpace2D` 는 33번째 표본부터 한 마디 없이 버렸다.** 가중치를 `float[constant::kMaxBuffer32]`
에 담고 표본 수를 `MathUtil::min( size, 32 )` 로 눌렀다. 목표 바로 옆에 둔 표본이 33번째면 결과가
통째로 달라진다. 거리 제곱을 두 번 구하는 값으로 그 고정 버퍼를 없앴다 — 이제 상한이 없다.
(README 의 8방향 모션은 32 안에 들어가지만, 상한이 있다는 사실 자체가 어디에도 없었다.)

**4) `Skeleton::addBone` 이 아직 없는 본을 부모로 받아들였다.** `updateCharacterSpaceTransforms` 는
배열을 앞에서 뒤로 **한 번만** 훑으므로 부모는 자식보다 앞에 있어야 한다. 그렇지 않으면
`if ( parentIndex < index )` 에 걸려 **그 본을 루트로 취급하고 계층을 통째로 잃는다** — 로그도
없이. 들어오는 자리에서 막고(`-1` 반환 + `SW_LOG_ERROR`), 훑는 쪽에는 `SW_ASSERT` 를 뒀다.
지금은 테스트만 `Skeleton` 을 쓰지만, 메시 임포터가 붙는 순간 터질 자리였다.

**5) `AnimationGraphPlayer` 가 서로 다른 말을 하고 있었다.** 클립이 등록되지 않은 노드로 넘어가면
`_currentNodeName` 만 새 노드로 바꾸고 플레이어는 그대로 뒀다 — `getCurrentNodeName()` 은 "Attack"
인데 `evaluate()` 는 여전히 "Idle" 의 포즈를 돌려줬다. 클립이 없는 노드는 길이 0 으로 보고 재생을
비운다.

**6) `AnimationGraphAsset::loadFromFile` 이 같은 JSON 을 두 번 파싱했다.** 파일을 읽어 파싱한
`JsonDocument` 를 **문자열로 다시 덤프해서** `parseJson` 에 넘기고, 거기서 또 파싱했다. 문서 길이
만큼의 문자열 하나가 덤이었다. 루트를 받는 `parseRoot` 를 갈라 둘 다 그것을 부른다. 쓰지 않는
`Engine/Resource/ResourceUtil.h` include 도 지웠다.

**7) 잔가지.** `AnimSample::_weight` 는 **가중치가 아니라 정규화 시간**이었다(헤더가 그렇게 적어
두고도 이름은 weight 였고, 테스트 주석은 "가중치" 라고 읽고 있었다) → `_normalizedTime`.
`AnimClip::setName( const string& )` 은 `std::move( name )` 을 const 참조에 걸어 아무 일도 하지
않는 이동이었다 → `string_view`. `AnimPlayer::setSpeed` 는 음수를 그대로 받아 `_fadeElapsed` 가
뒤로 흘러 **크로스페이드가 영원히 끝나지 않았다** → `update` 가 음수 델타를 막는 것과 같이 0 으로
막는다. `AnimClip.cpp` 가 `MathUtil` 을 include 없이 쓰고 있었다.

**8) `AnimationGraphAsset.h` 는 혼자 서지 못했다** — `float2` 를 쓰면서 `VectorMath.h` 를 include
하지 않았다. **그런데 빌드는 통과한다.** 아래 항목 참고.

**회귀 테스트 10건** (`AnimationTest` 8 → 13, 새 스위트 `AnimationGraphTest` 5건 —
`Test/EngineTest/TestAnimationGraph.cpp`. 그래프 애셋·플레이어는 그때까지 테스트가 하나도 없었고
에디터 `AnimationGraphPanel` 이 쓰고 있었다). 변이 테스트로 전부 확인했다 — 고친 것을 되돌리면
해당 케이스만 정확히 깨진다(A조 4건 · B조 2건).

**검증.** Debug·Shipping 빌드(경고 0) · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15 · `Animation*` 18건 · 헤더 7개 자립(생성 force-include 를 뺀 상태로).

### 2026-09-18 (우산 하나가 헤더 자립성 검사를 통째로 무의미하게 만들고 있었다 — **해결**)

`cmake/Engine/ReflectionCodeGen.cmake` 가 만드는 `FlagOps.gen.h` 는 타깃의 **모든 TU 에 `/FI` 로
강제 include** 된다 — `ENUM(Flags)` 의 비트 연산자 트레이트(`sw::IsBitFlagEnum<E>` 특수화)는 그
열거형이 보이는 곳이면 어디서나 함께 보여야 하기 때문이다. 그 자체는 맞다. 문제는 **그 우산이
트레이트만이 아니라 열거형을 선언한 원본 헤더까지 `#include` 했다**는 것이다:

```
#include "Engine/Graphics/Material/MaterialTypes.h"   ← 이 줄이 문제였다
#include "MaterialTypes.gen.h"
```

`RHITypes.h` 는 다시 `Engine/Common/Common.h`(File · Math · String · Time · ResourceUtil 우산)를
끌어온다. 그래서 웬만한 이름은 **모든 TU 에 이미 있는 것**이 되어, 어떤 헤더가 include 를
빠뜨려도 보이지 않았다. 게다가 그 우산의 내용은 "플래그 열거형을 가진 헤더가 무엇이냐" 에 따라
바뀌므로 **오늘 서는 헤더가 내 코드를 한 줄도 안 고쳐도 내일 못 설 수 있었다.**

**고친 방법 — 우산이 전방 선언만 모은다.** 트레이트 특수화에는 열거형의 **불투명 선언**이면
충분하다(불투명 열거형 선언은 완전한 타입이다). 그래서 코드젠이 `.gen.h` 에 이렇게 쓴다:

```cpp
namespace sw { enum class RHIBufferUsage : unsigned char; }
template <> struct sw::IsBitFlagEnum<sw::RHIBufferUsage> : std::true_type {};
```

우산은 이제 그 `.gen.h` 들만 모은다 — 원본 헤더는 한 줄도 들이지 않는다. **소스 헤더는 아무것도
바뀌지 않았다**(열거형 헤더가 자기 `.gen.h` 를 손으로 include 하게 하는 안을 먼저 만들었다가
물렸다 — 사람이 매번 기억해야 하는 줄을 헤더마다 심는 방식이었다).

파서가 기반 정수 타입을 알아야 하므로 `ParsedEnumInfo._underlyingType` 을 추가했다
(`clang_getEnumDeclIntegerType` 의 **정본** 철자 — `uint8` 이 아니라 `unsigned char` 여야 재선언이
어긋나지 않는다). **클래스 안에 든 플래그 열거형은 밖에서 전방 선언할 수 없으므로**
`_bNestedInType` 을 같이 모아, 그런 열거형을 만나면 코드젠이 그 자리에서 실패한다(지금은 하나도
없다). 조용히 깨진 헤더를 뱉는 것보다 낫다.

**CMake 쪽 불일치도 같이 닫았다.** `/FI` 를 붙일지 정하는 정규식이 `ENUM( Flags` 만 봤는데, 파서는
`AnnotationMeta.txt` 의 동의어를 전부 받는다(`Flags` · `BitFlag` · `FLAG` · `Bitwise`). 한 타깃의
플래그 열거형이 전부 `BitFlag` 철자였다면 우산은 만들어지는데 `/FI` 는 안 붙어
"invalid operands to binary expression" 으로 깨졌다 — **두 곳이 같은 판정을 따로 내리고 있었다.**
이제 정규식을 없애고 조건 없이 붙인다(플래그가 없는 타깃의 우산은 사실상 빈 파일이다).

**그 결과 드러난 진짜 누락 13건을 고쳤다.** 우산을 걷어내고 `Source/` 헤더 455개를 단독 컴파일하니:

| 헤더 | 빠진 것 |
|------|---------|
| `Engine/Animation/AnimationGraphAsset.h` | `float2` (앞선 커밋에서 이미 고쳤다) |
| `Engine/Dialogue/DialogueGraphAsset.h` | `float2` |
| `Engine/Graphics/Material/MaterialCache.h` | `unique_ptr` |
| `Engine/Graphics/Texture/TextureCache.h` | `unique_ptr` |
| `Engine/Graphics/RHI/Support/RHIReleaseQueue.h` | `constant::kGpuReleaseFrameLatency` |
| `Engine/Utility/Debug/FrameProfiler.h` | `SW_API` — **우산을 뺀 예전 측정에서도 안 보이던 것** |
| `Editor/Common/Backend/Render/ImGuiDX11RendererBackend.h` | `Microsoft::WRL::ComPtr` |
| `Editor/Common/Backend/Render/ImGuiDX12RendererBackend.h` | `Microsoft::WRL::ComPtr` |
| `Editor/Common/Commands/EditorToolAssetCommands.h` | `float2` |
| `Editor/Common/Widgets/ViewportInputOverlay.h` | `SW_FALSE` |
| `Editor/Panels/HierarchyPanel.h` | `GameObject` · `GameObjectManager` · `vector` |
| `Editor/Panels/InspectorPanel.h` | `EnumInfo` |
| `Editor/Panels/PrefabEditorPanel.h` | `float2` |
| `GameFramework/Kits/Overworld/ZoneRuntime.h` | `int2` |

**지금은 455개가 전부 혼자 선다.**

**검사는 `Scripts/lint/report/RunHeaderSelfContained.py` 로 남겼다 — 게이트가 아니다.** 헤더 하나에
컴파일러를 한 번씩 부르므로 6코어에서 약 3분이 걸린다. 린트 스위트 전체가 30초인데 거기에 3분을
얹으면 아무도 린트를 돌리지 않게 된다(`RunBuildWarnings.py` 와 같은 판단이다 — `Run*` 은 보고하고
`Check*` 이 막는다). 한 폴더를 훑어 끝냈을 때, include 를 정리한 뒤, 남의 커밋을 받은 뒤에 돌린다.
**되돌아오는 길은 구조가 막는다** — 우산이 원본 헤더를 들이지 않으므로 이 눈가림이 다시 생기려면
코드젠을 일부러 되돌려야 한다.

### 2026-09-17 (Core/Uuid — 결함 없음. 훑은 것과 그 근거만 남긴다)

**동작 결함을 찾지 못했다.** 다음을 따라가 봤고 전부 맞다:

- `generate()` 의 v4 버전·변형 비트(`_arrBytes[6] = (x & 0x0f) | 0x40`, `[8] = (x & 0x3f) | 0x80`)는
  RFC 4122 그대로다.
- 난수원은 `MathUtil::getRandomRange` → **`thread_local std::mt19937_64`** 이고 `std::random_device` 로
  씨를 받는다. 스레드마다 따로이므로 동시 생성에 레이스가 없다.
- `toString()` 은 36자를 정확히 채우고(하이픈은 바이트 4·6·8·10 앞 = 위치 8·13·18·23), `tryParse` 와
  왕복한다. `tryParse` 는 길이·하이픈 위치를 먼저 보고, 엉뚱한 자리의 하이픈은 짝이 밀려
  `byteIndex != 16` 으로 걸린다.
- 비교는 `Memory::compare` 로 16바이트 사전순이라 `operator<` 가 엄격 약순서를 만족한다.

**씨앗 엔트로피를 문제 삼지 않은 이유.** `std::random_device{}()` 는 32비트 하나로 MT 를 채우므로
"서로 다른 프로세스가 같은 UUID 열을 낸다" 가 이론상 가능하다. 다만 이 저장소의 `Uuid` 는
`EditorWorkspace::getOrAssignGuid` 가 **세션 안에서만** 쓰는 오브젝트 GUID 다 — 두 맵에만 살고 디스크로
나가지 않는다(확인했다). 프로세스 사이 충돌이 의미를 갖는 자리가 아니라 그대로 두었다. **디스크나
네트워크로 나가는 UUID 를 만들게 되면 이 대목을 먼저 다시 볼 것.**

**한 가지만 고쳤다.** `Uuid.h` 가 `std::hash` 특수화 하나 때문에 `StdHeaders.h`(std 헤더 48개 우산)를
당기고 있었다. `<functional>` 로 좁혔다 — `Core/Common/EnumUtil.h` 에 했던 것과 같은 정리다.
헤더는 그 뒤로도 혼자 선다(단독 컴파일로 확인).

### 2026-09-17 (타이머를 만들자마자 쓰면 첫 델타가 "부팅 이후 시간" 이었다 — Core/Time)

**1) 생성자가 문서와 반대로 돌고 있었다.** 헤더는 "초당 카운트를 읽고 **중지 상태로** 둡니다" 라고
적고, `FrameRenderer::initialize` 의 주석도 "CpuTimer 는 만들면 중지 상태다" 라고 적는데, 구현은
`_bStopped{ false }` 였다. 그래서 `startTimer()` 가 `if ( _bStopped )` 에 걸려 **아무 일도 하지 않고**,
`_prevTime` 이 0 인 채로 첫 `updateTimer()` 가 돈다 — 델타가 `현재 QPC - 0`, 즉 **부팅 이후 전체 시간**
이 된다. `getTotalTime()` 도 같다.

지금 아무 데도 안 터지는 이유는 호출부 **다섯 곳이 전부** `resetTimer()` 를 먼저 부르기 때문이다
(`FrameTimeline` · `ModuleCompiler` · `LiveReloadManager` · `FrameRenderer` · X11 백엔드). 그 다섯 곳이
바로 뒤에 `startTimer()` 도 부르는데, `resetTimer()` 가 이미 `_bStopped` 를 내려 놓으므로 그 호출은
**전부 아무 일도 하지 않는다** — 다들 "만들면 멈춰 있다" 고 믿고 의식을 치르고 있었던 셈이다.

문서 쪽이 옳다고 보고 구현을 맞췄다(`_bStopped{ true }`). 그러면 `startTimer()` 가 제 일을 한다 —
`_pausedTime += ( 시작시각 - _stopTime(0) )` 이 기준을 시작 시각으로 옮겨 주므로 reset 없이 만들어 바로
start 해도 누적과 델타가 맞는다. 의식을 잊어도 안전해졌고, 잊은 채 `updateTimer()` 만 부르면 델타는
0 이다(쓰레기 값이 아니라).

**2) `CpuTimer` · `ScopeCpuTimer` 를 전역 이름으로도 내놓고 있었다.** `using CpuTimer = sw::CpuTimer;`
— **저장소에서 `sw::` 클래스를 전역에 별칭으로 내놓는 헤더는 이것 하나뿐이다.** 그리고 이 헤더는
`Engine/Common/Common.h` 를 타고 사실상 모든 TU 에 들어간다. `Types.h` 가 적어 둔 기준(컨테이너·클래스
이름은 `sw` 안, 고정폭 기본형만 전역)과도 어긋난다.

쓰는 곳을 세어 보니 `Source/` 의 다섯 선언은 **전부 `namespace sw` 안**이라 별칭이 없어도 그대로
풀린다. 실제로 이 별칭에 기대고 있던 것은 `TestTime.cpp` 의 세 줄뿐이었고, 그 파일조차 다른 자리에서는
`sw::ScopeCpuTimer` 라고 쓰고 있었다. 지웠고 그 세 줄을 한정했다.

**3) `ScopeCpuTimer::getElapsedTimeInSeconds()` 가 `const` 라고 적고 `const_cast` 로 타이머를 돌렸다.**
이 함수는 부를 때마다 기준점을 옮기므로 **두 번 부르면 두 번째는 ≈0** 이다 — 실제로 예전 소멸자가 그
함정에 빠져 스코프 길이와 무관하게 0 ms 를 찍었고 그 사연이 주석에 남아 있다. 그런 함수가 "읽기만
한다" 고 서명하면 그 위험이 보이지 않는다. `const` 를 뗐다.

**회귀 테스트.** `TimeTest.FreshTimerDoesNotReportTimeSinceBoot` — 만들고 `startTimer()` 만 한 뒤
10ms 자고 재면 델타가 1초 미만이어야 한다. 변이 테스트로 확인했다(생성자를 되돌리면 "델타가 부팅 이후
시간이다" · "누적이 부팅 이후 시간이다" 둘이 깨진다). 기존 `CPUTimerBasic` 의
`SW_EXPECT_FALSE( isStopped() )` 는 구현의 옛 동작을 못박고 있었으므로 문서 쪽으로 뒤집었다.

**검증.** Debug·Shipping 빌드 (이번 변경이 다시 컴파일한 TU 는 경고 0) · `ctest -L nogpu` 양쪽 5/5 ·
`-L hostgpu` 양쪽 1/1 · 린트 15/15 · `TimeTest` 4 → 5건 · 헤더 5개 자립.

### 2026-09-17 (형제 콤비네이터 둘이 빈 입력에 다르게 답했고, 한쪽은 영원히 멈췄다 — Core/Task)

**1) `whenAllFutures` 는 유효하지 않은 future 하나에 영원히 멈췄다.** `TaskFuture::then` 은 상태가 없는
future(기본 생성된 것)에 콜백을 **걸지 않고 그냥 돌아간다**. 그런데 카운트다운은 목록 길이로 잡혀
있었다 — 그래서 `_remaining` 이 0 에 닿지 못하고 결과 future 가 끝나지 않는다. 유효한 것 하나와 기본
생성된 것 하나를 넣으면 그대로 재현된다. 이제 **유효한 것만 세고**, 기다릴 수 없는 자리는 결과 벡터에
기본값으로 남긴다(길이와 순서는 입력 그대로다).

**2) 형제인 `whenAnyFuture` 는 빈 목록에 다르게 답했다.** `whenAllFutures` 는 빈 목록을 곧바로 끝냈는데,
`whenAnyFuture` 는 **유효한** future 를 만들어 돌려주고 아무도 값을 넣어 주지 않았다 — `wait()` 가
영원히 멈춘다. 값을 만들 길이 없으므로 이제 `isValid() == false` 인 future 를 돌려준다. `wait()` 는
곧장 돌아오고 호출부가 물어볼 수 있다. 유효하지 않은 후보는 경주에서도 뺀다.

**3) `setContinuation` 도 쌍둥이 중 하나만 고쳐져 있었다.** `SharedFutureState<void>` 쪽에는 "옮긴 값을
조건으로 되살려 쓰지 않도록 갈 곳을 하나씩만 정한다(예전에는 bool 플래그와 `std::move` 가 서로를
배제한다는 사실에 기대고 있었다)" 는 주석과 함께 고친 모양이 들어 있는데, **본체 템플릿은 그 옛 모양
그대로였고** `NOLINTNEXTLINE(bugprone-use-after-move)` 억제 주석까지 달고 있었다. 동작은 맞지만 읽는
사람도 분석기도 확신할 수 없는 모양이다. `void` 쪽과 같게 맞추고 억제를 걷었다.

**4) 폴더 README 가 `TaskFuture.h` 를 아예 언급하지 않았다.** 524줄짜리 공개 헤더이고 씬 비동기
로드(`SceneManager`)와 에셋 스트리밍(`AssetStreamingQueue`)이 그 위에 서 있는데, 파일 표에도 "더 볼 곳"
에도 없었다. `whenAll` / `whenAny` 는 본문에 한 줄 나오지만 실제 이름(`whenAllFutures` /
`whenAnyFuture`)도 어느 파일에 있는지도 적혀 있지 않았다. 절을 하나 더하고, 위 1·2 에서 정한 계약을
표로 적었다.

**살펴보고 손대지 않은 것.** `TaskManager::clear()` 는 README 가 적은 그대로 `steal()` 로 원자적으로
꺼내 `release()` 한다(`std::erase_if` 를 쓰지 않는다). Affinity 표·스레드 헬퍼 목록도 코드와 맞는다.
`SharedFutureState<T>::get()` 이 `_bHasValue` 를 보지 않고 저장소를 읽지만, `_bReady` 를 세우는 경로가
`setValue` 뿐이라 값 없이 ready 가 되는 상태는 만들어지지 않는다.

**회귀 테스트.** `TaskTest.CombinatorsDoNotHangOnInvalidOrEmptyInput` — whenAll(유효+무효 혼합 · 빈
목록) · whenAny(빈 목록 · 전부 무효 · 무효 혼합). 변이 테스트로 둘을 따로 확인했다: whenAll 을 되돌리면
`waitFor( 2000 )` 이 2006ms 만에 시간 초과로 깨지고, whenAny 를 되돌리면 유효성 단정 둘이 깨진다.

**검증.** Debug·Shipping 빌드 (이번 변경이 다시 컴파일한 TU 는 경고 0) · `ctest -L nogpu` 양쪽 5/5 ·
`-L hostgpu` 양쪽 1/1 · 린트 15/15 · `TaskTest` 17 → 18건.

### 2026-09-17 (에디터의 `tag:` 필터는 한 번도 맞은 적이 없었다 — Core/String)

**1) 태그 ID 를 구하는 코드가 세 곳에 있었고 규칙이 셋 다 달랐다.** `""_tag` 와 `TagID::request` 는
FNV-1a 를 손으로 폈고(대소문자 **구별**, `char` 부호 확장까지), 에디터 Hierarchy 의 `tag:` 필터는
`StringUtil::computeHash64( s, len )` 를 **기본 인자로** 불러 대소문자를 **무시**했다. 저장소의 태그는
전부 대문자로 시작한다(`Collider` · `Sprite` · `UI` · `Physics` · `Faction.Player` …). 두 식을 떼어
돌려 봤더니 **그 전부가 어긋났고**, 소문자 태그 하나만 우연히 맞았다 — 그런 태그는 저장소에 없다.
즉 그 필터는 **한 번도 아무것도 찾지 못했다.**

`TagID::computeId` 하나로 모았다. **대소문자를 무시하는 쪽**으로 맞췄는데, `request` 가 문자열을
`hashed_string` 으로 intern 하고 그 intern 이 이미 대소문자를 무시하기 때문이다 — ID 만 구별하면
`request("Player")` 와 `request("player")` 가 **같은 문자열을 가리키면서 다른 ID** 를 갖는 모순이
남는다. 태그는 ID 가 아니라 문자열로 직렬화되므로(`SerializeContext` 가 `TagID::request( text )` 로
되읽는다) 규칙을 바꿔도 저장된 씬은 그대로다. `isSubtagOf` 의 문자열 비교도 같이 대소문자를 무시하게
했다 — 한쪽만 구별하면 "같은 태그인데 조상이 아니다" 가 나온다.

패널은 ID 와 **문자열을 함께** 넘기도록 고쳤다. 그래야 `tag:Faction` 이 `Faction.Player` 까지 잡는다 —
리터럴 태그는 역조회 표에 등록되지 않으므로(`""_tag` 는 constexpr 이라 런타임 표를 만질 수 없다)
ID 만 든 `TagID` 로는 계층 비교를 할 수가 없었다.

**2) 예전 고침이 64비트 쌍둥이에서 멈춰 있었다.** `computeHash64` 에는 "두 경로 모두 uint8 을
거친다 … `char` 의 부호성은 구현 정의라 플랫폼이 바뀌면 해시가 달라졌다" 는 주석이 붙어 있는데,
**`computeHash32` 의 `bIgnoreCase` 경로는 그대로 부호 확장된 채 남아 있었다.** 그 경로가 기본값이고
`hashed_string` 의 intern 이 바로 그것을 쓴다. 그 고침을 지키던 테스트(`NonAsciiBytesAreUnsigned`)도
64비트만 보고 있어서 보이지 않았다 — 테스트가 쌍둥이의 절반만 든 자리다.

**3) 그리고 그 고침 자체가 `uint8` 로 고정돼 있었다.** 이 해시 템플릿은 `utf16` 으로도 불린다
(`std::hash<basic_fixed_string<utf16, N>>`). `uint8` 로 자르면 **넓은 문자가 하위 한 바이트만 남아**
`0xAC00`(가)과 `0xAD00` 이 같은 해시로 떨어진다 — 한글처럼 상위 바이트가 의미를 갖는 문자열이 통째로
한 버킷에 뭉친다. 부호 없는 타입은 맞되 **폭은 `CharT` 를 따라야** 한다: `std::make_unsigned_t<CharT>`.
ASCII·utf8 값은 그대로라 디스크에 남는 셰이더 베이크 스탬프 같은 것은 영향이 없다(테스트로 못박았다).

**4) 비워진 intern 테이블을 다시 세우지 않았다.** `HashedStringPool::initialize` 의 인스턴스는
**함수 지역 static** 이라 두 번째 호출에서는 생성자가 돌지 않는다. 그런데 `shutdown` 은 `clear()` 로
0번 청크와 사전 정의 이름까지 돌려준다 — 그 뒤 다시 initialize 하면 **빈 테이블**을 가리키게 되고,
`hashed_string( NameType_float3 )` 의 `c_str()` 이 nullptr 이며 새로 intern 되는 첫 문자열이
0번(`NameType_None`)을 받아 기본 생성자와 같아진다. 둘 다 조용한 오답이다. 앞의 단정은 Debug 전용이라
Shipping 에서는 막아 주지도 못한다. 지금은 프로세스당 한 번만 불려서 안 터지는데, `shutdown` 쪽은
이미 여러 번 불려도 안전하게 짜여 있었다 — 그 비대칭이 결함이다. 저장소 세우기를 `initializeStorage`
로 빼고 재초기화 때 다시 부른다.

**살펴보고 손대지 않은 것.** `StringBuilder` 의 이동 생성/대입은 힙·스택 두 경우를 모두 맞게 처리하고
(`_pDynamicBuffer` 이전 순서까지), `appendFormat` 의 재시도 루프는 매번 용량이 최소 두 배가 되므로
끝난다. `fixed_string` · `string_splitter` · `formatString` 은 `StringTest` 가 이미 두껍게 덮고 있다.

**테스트.** `TagSystemTest` 둘 — `IdBuiltFromStringFindsLiteralTag`(필터가 하는 그대로 문자열에서 ID 를
만들어 리터럴 태그를 찾는다 · 대소문자 · 계층 · 없는 태그) · `LiteralAndRuntimeRequestAgree`.
`StringTest` 둘 — `WideCharHashIsNotTruncatedToOneByte`, `ClearedInternTableIsRebuiltNotLeftEmpty`.
32비트 부호 확장 주장은 이미 집이 있는 `NonAsciiBytesAreUnsigned` 에 더했다. 변이 테스트로 확인했다:
`""_tag` 을 옛 손수 해시로 되돌리면 태그 테스트 둘이 깨지고, 해시 캐스트를 되돌리면
`NonAsciiBytesAreUnsigned` 와 `WideCharHashIsNotTruncatedToOneByte` 가 깨진다.

**확인하지 않은 것.** 에디터를 띄워 실제로 `tag:` 를 쳐 보지는 않았다 — 테스트가 패널과 같은 경로
(`hasTag` 에 문자열로 만든 `TagID`)를 태운다.

**검증.** Debug·Shipping 빌드 (이번 변경이 다시 컴파일한 TU 는 경고 0) · `ctest -L nogpu` 양쪽 5/5 ·
`-L hostgpu` 양쪽 1/1 · 린트 15/15 · `StringTest` 35 → 37건 · `TagSystemTest` 11 → 13건.

### 2026-09-17 (한 헤더가 약속한 것을 두 구현이 절반만 지키고 있었다 — Core/Process)

이 폴더는 **한 인터페이스에 Windows/POSIX 두 구현**이 달린 모양이다. 파일워처 때와 같은 자리에서
같은 종류가 나왔다 — 두 벌이 갈렸고, 갈린 쪽이 기능을 잃었다.

**1) 종료 코드 259 로 끝난 프로세스가 영원히 "실행 중" 이었다.** `isRunning` 이
`GetExitCodeProcess` 의 값을 `STILL_ACTIVE` 와 비교하는데 그 상수는 **259** 다. 그러니 259 로 끝난
자식은 끝나지 않은 것으로 보인다(`cmd /c exit 259` 로 바로 재현된다). 끝났는지는 종료 코드가 아니라
핸들이 신호 상태인지로 물어야 한다 — `WaitForSingleObject( h, 0 )` 로 바꿨다.

그 과정에서 **테스트가 틀린 계약을 적고 있었다는 것**도 드러났다. `TerminateProcess` 는
`terminate()` 직후에 `isRunning() == false` 를 기대했는데, `TerminateProcess` 는 **요청**이라
돌아온 시점에 아직 죽지 않았을 수 있다. 예전 구현에서는 그 사이 `GetExitCodeProcess` 가 이미 종료
코드를 주어 우연히 맞았다. 이제 테스트가 `waitForExit` 로 기다린 뒤 **종료 코드가 99 인지**까지 본다 —
그래야 "우리가 죽였다" 와 "ping 이 스스로 끝났다(0)" 가 구별된다. 헤더에도 요청이라고 적었다.

**2) `bIsStdErr` 인자는 언제나 false 였다.** 두 구현 모두 표준 에러를 표준 출력에 **일부러** 합친다
(Windows 는 `si.hStdError = hStdOutWrite`, POSIX 는 `2>&1`). 빌드 로그처럼 두 스트림이 원래 순서대로
섞여야 읽히는 것이 이 클래스의 용도이기 때문이고, 그래서 어느 쪽에서 왔는지 가려낼 방법 자체가 없다.
호출부 둘 다 `(void)bIsStdErr` 로 버리고 있었다. 인자를 지우고 왜 합치는지를 적었다.

**3) POSIX `terminate()` 는 종료가 아니라 UB 였다.** `pclose` 를 부르고 true 를 돌려줬는데, `pclose`
는 **자식이 스스로 끝날 때까지 기다린다**. 게다가 유일한 실제 호출부인 `ModuleCompiler::cancel()` 은
UI 스레드에서 `_mutex` 를 쥔 채 이것을 부르고, 그 시각 빌드 스레드는 **같은 `FILE*`** 위에서 `fgets`
를 돌고 있다 — `pclose` 가 그 스트림을 해제하므로 미정의 동작이고, 그 전에 컴파일이 끝날 때까지 UI 가
멈춘다. 못 하는 일은 못 한다고 말하게 했다(경고를 남기고 false). 진짜 해결은 fork/exec 이고 "남은 일
1-0b" 에 조건까지 적어 두었다. 테스트도 POSIX 에서는 `SW_TEST_SKIP` 이다 — 예전에는 **통과했는데**,
종료해서가 아니라 `sleep 10` 이 스스로 끝나기를 10초 기다려 줬기 때문이다.

**4) 크래시 리포트 본문이 두 벌이었고 이미 갈려 있었다.** `WindowsCrashHandler.cpp` 와
`PosixCrashHandler.cpp` 의 `reportCrash` 는 90% 가 같은 코드인데, **"어느 파일을 보내면 되는지" 적는
블록이 Windows 에만 있었다.** 리눅스 사용자는 리포트가 어디 났는지 알 수 없었다. 그 Windows 쪽
목록마저 미니덤프를 **쓰지 못했을 때도** 적었고(`CreateFileA` 가 실패해도 그냥 돌아왔다), 정작 스택이
든 `*.stack.txt` 는 빠뜨렸다. 본문을 `CrashContext.cpp` 의 `writeCrashReport` 하나로 모았다 —
`CrashContext.h` 가 컨텍스트 파일에 대해 이미 "세 플랫폼이 같은 형식을 쓰도록 여기 한 번만 둔다" 고
적어 둔 그 자리다. `writeMiniDump` 는 이제 성공 여부를 돌려주고, 그 답이 목록에 반영된다.

**살펴보고 손대지 않은 것.** `CallStackCapture` 두 구현은 같은 여섯 함수를 같은 계약으로 채우고
있고(심볼 핸들 참조 카운트 · 교착 회피 · 컨텍스트에서 걷기), 어긋난 곳이 없었다.

**테스트.** 크래시 경로에는 **테스트가 하나도 없었다** — 다른 모든 것이 실패한 뒤에 도는 코드인데.
진짜 크래시를 낼 수는 없으므로 리포트를 만드는 조각을 직접 부르는 `CrashReportTest` 넷을 새로 썼다:
컨텍스트가 파일에 닿는지 · 같은 키 덮어쓰기와 정원 초과 시 안전한 버림 · **보낼 파일 목록** ·
세션 ID 안정성. 세 번째는 변이 테스트로 확인했다(옛 모양으로 되돌리면 "스택 파일이 목록에 없다" 와
"쓰지도 않은 미니덤프를 보내라고 적었다" 둘이 깨진다). `ProcessTest` 에는
`ExitCodeStillActiveIsNotMistakenForRunning` 을 더했다 — 고치기 전 코드에서 실제로 실패한다.

**검증.** Debug·Shipping 빌드 (이번 변경이 다시 컴파일한 TU 는 경고 0) · `ctest -L nogpu` 양쪽 5/5 ·
`-L hostgpu` 양쪽 1/1 · 린트 15/15 · `ProcessTest` 3 → 4건 · `CrashReportTest` 0 → 4건.
**전 트리 경고 스윕(`RunBuildWarnings.py`)은 Core 폴더를 다 끝낸 뒤 한 번에 돈다** — 폴더마다 돌리면
빌드와 겹쳐 엉뚱한 숫자가 나오고(그렇게 한 번 속았다) 폴더당 4분 넘게 든다.

### 2026-09-17 (플래그를 `X = true` 로 적으면 조용히 버려졌다 — Core/Predefined)

`Source/Core/Predefined` 는 코드가 아니라 **목록 여섯 개**다. 나온 것은 두 종류다 — 같은 질문에 두
곳이 다른 답을 하고 있었고, 번호가 계약인 목록에 그 사실이 적혀 있지 않았다.

**1) 단독 토큰과 `X = true` 가 가리키는 필드 집합이 달랐다.** `AnnotationMeta.txt` 는 그 두 형태를
`flag.X` 와 `bool.X` 로 **따로** 적는다(앞은 `_mapBare`, 뒤는 `_mapKey` 로 간다). 그 둘이 어긋나
있었다 — 플래그 열셋 중 여섯(`Abstract` · `Static` · `AssetPath` · `Polymorphic` · `Reliable` ·
`Validate`)에 `bool` 줄이 없어서 `PROPERTY( Polymorphic = true )` 는 `findKey` 가 nullptr 로 돌아오고
`continue` 로 버려졌다. **경고 한 줄 없다.** 별칭 목록도 한쪽만 늘어나 `bool.XmlAttribute` 에는
`xmlAttribute` 가 빠져 있었다 — `PROPERTY( xmlAttribute )` 는 먹고 `PROPERTY( xmlAttribute = true )`
는 안 먹었다. 어느 쪽이 빠졌는지는 애노테이션을 적는 자리에서 보이지 않는다.

줄을 합쳤다. `AnnotationMeta::addAlias` 가 `flag.` 한 줄을 `_mapBare`(Flag)와 `_mapKey`(Bool)에
**함께** 넣는다. 중복이던 `bool.` 일곱 줄은 지웠고, 이제 어긋날 자리가 없다. `netrole.` 은 그대로
단독 토큰뿐이다 — 역할을 고르는 것이라 참/거짓이 없다. `bool.` 은 "단독 토큰은 받지 않는 필드" 용으로
남겨 두고 그렇게 적었다.

**2) ENUM 스코프만 `=` 쪽에서 Bool 을 거부했다.** REFLECT · PROPERTY · FUNCTION 은 `Kind::Bool` 이면
값을 파싱해 적용하는데 `parseEnumAnnotation` 만 `!= String` 이면 버렸다. 넷이 같게 했다.

**3) 주지 않은 단독 플래그를 "주었다" 로 읽고 있었다.** `ArgumentList.xxx` 의 네 번째 열
(`bUseDefaultValue`)이 RHI 백엔드 넷과 `ENABLE_EDITOR` 에만 켜져 있었다. 기본값이 `false` 라 값은
맞았지만, 그 열을 켜면 `getArgument` 가 **주지 않은 인자에도 true** 를 돌려준다 — 반환값이 "이 인자가
주어졌다" 를 뜻하지 않게 된다. `RHI.cpp` 가 `getArgument(...) && bFlag` 로 한 번 더 묻는 것이 그
흔적이고, 같은 파일의 `VSYNC` 는 처음부터 꺼져 있었으며 주석이 그 이유(1280×720 설정이 무시되고 창이
1280×1280 으로 뜬 사건)를 이미 적어 두고 있었다. 다섯 줄을 껐다 — 호출부는 둘 다 이미 값을 보고
판단하므로 동작은 그대로다. 이제 목록의 단독 플래그는 전부 같은 답을 한다.

**4) 번호가 곧 intern 인덱스인데 아무 데도 적혀 있지 않았다.** `.xxx` 넷 중 `PredefinedNameType.xxx`
만 `@file` 머리말이 없는데, 하필 번호를 손으로 적는 유일한 목록이다. 이 목록은 **빈 intern 테이블에
줄 순서대로** 적재되므로 번호 = intern 인덱스이고, `basic_hashed_string` 은 `PredefinedNameType` 을
그대로 인덱스로 캐스팅한다(`getPredefinedType` 이 그 역이다). 중간에 한 줄 끼워 넣고 번호를 다시 매기지
않으면 그 뒤 이름이 전부 다른 문자열을 가리킨다. 게다가 intern 의 비교와 해시는 **대소문자를
무시하므로**(`StringUtil::equals( …, true )`) 이름을 대소문자만 다르게 지으면 두 항목이 하나로 합쳐져
뒤가 한 칸씩 밀린다. 머리말에 적고 **둘 다 기계가 보게 했다** — 번호 연속성은 `hashed_string.h` 의
`static_assert`, 합쳐짐은 `AllocationInfo` 생성자의 개수 단정(`SW_ASSERT` — 이 생성자는 Logger 보다
먼저 돌기 때문에 로그를 태우는 단정을 쓸 수 없다).

**5) `ArgumentList.xxx` 에도 머리말이 없었다.** 다섯 인자가 무엇을 뜻하는지 어디에도 없었다. 적었다.

**살펴보고 손대지 않은 것.** `AnnotationMeta.txt` 의 필드는 `PredefinedAnnotationField.xxx` 의 네
스코프 37개와 정확히 맞는다 — 바인딩 없는 필드도, 필드 없는 바인딩도 없다. `netrole.` 셋도
`PredefinedFunctionNetRole.xxx` 와 맞는다(`Local` 은 기본값이라 애노테이션이 없는 것이 맞다).

**회귀 테스트 둘.** `ReflectionParserTest.AssignedFlagFormMatchesBareToken` — 표본 타입 셋에 여섯
플래그를 전부 `X = true` 로 적고 `TypeInfo` · `PropertyInfo` · `FunctionInfo` 에 실제로 붙는지 본다.
변이 테스트로 확인했다(고치기 전 코드로 되돌리면 단정 여섯이 깨진다 — **단 `.gen.cpp` 를 강제로 다시
만들어야 보인다**, 위 "편집 함정" 참조). `CommandLineTest.UnprovidedFlagIsNotReadable` — `-dx12` 만
주고 나머지 플래그가 "주지 않았다" 로 읽히는지.

**검증.** Debug·Shipping 빌드 경고 0 · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 · 린트 15/15 ·
`CommandLineTest` 9 → 10건 · `ReflectionParserTest` 9 → 10건 · 헤더 5개 자립.

### 2026-09-17 (풀 할당기 문서가 정렬을 절반으로 적어 두었다 — Core/Memory)

**동작 결함은 없었다.** `LinearAllocator::allocate` 의 락프리 경로(블록 내 CAS 범프 → 가득 차면
뮤텍스를 잡고 `_currentBlockIndex` 를 **다시 확인**한 뒤에만 새 블록 추가)는 이중 추가까지 막고 있고,
`PoolAllocator` 의 수동 잠금도 **모든 이른 반환에서 실제로 해제하고 있었다**(전 경로를 확인했다).

**1) `PoolAllocator` 문서가 정렬을 틀리게 적었다.** 생성자 주석은 "최소 sizeof(void*), 포인터 크기
단위로 정렬됨" 인데 구현은 처음부터 **16바이트**다 — 블록 크기를 16 배수로 올리고, 청크 헤더도
16 으로 맞추고, 기반 할당도 `allocateAligned( …, 16 )` 이다. 그 차이는 **SSE 타입을 담아도 되는가**를
가른다. 읽는 사람이 직접 패딩을 붙이거나 아예 못 쓴다고 판단하게 만드는 종류의 오문서다.

**2) 수동 lock/unlock 을 RAII 로 바꿨다.** 해제 지점이 7곳이었고 **하나만 빠져도 데드락**이다. 지금은
맞지만 다음에 이른 반환을 하나 더 넣는 사람이 지켜야 하는 규칙이었다.
`std::unique_lock{ _mutex, std::defer_lock }` + `if ( _bThreadSafe ) lock.lock()` 으로, 잠글지 말지는
그대로 플래그가 정하되 해제는 스코프가 맡는다.

**3) 같은 폴더인데 문서 밀도가 달랐다.** `LinearAllocator` 는 삭제된 복사 연산·소멸자·스레드 계약까지
적는데 `PoolAllocator` 는 그 자리가 비어 있었다. 채웠고, `allocateChunk` 가 **잠금을 잡은 채** 불러야
한다는 것도 적었다(코드에는 그 전제가 있는데 말은 없었다).

**회귀 테스트 둘.** `PoolAllocatorHandsOutSixteenByteAlignedBlocks`(16 배수가 아닌 블록 크기를 주고,
프리 리스트로 되돌아온 블록까지 정렬을 확인) · `ThreadSafePoolNeverHandsOutTheSameBlockTwice`
(네 스레드가 800블록을 받아 중복이 없는지 — 잠금 교체가 상호 배제를 망가뜨리지 않았는지).

**검증.** Debug·Shipping 빌드 경고 0 · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 1/1 · 린트 15/15 ·
`MemoryTest` 9 → 11건 · 헤더 5개 전부 자립.

### 2026-09-17 (Math 헤더 둘이 단독으로 서지 못했고, 특이행렬이 조용히 Identity 로 돌아왔다 — Core/Math)

**1) `VectorMath.h` · `MatrixMath.h` 가 자립하지 못했다** (각 20건). `SW_API` 를 쓰면서
`Core/Common/Macros.h` 를 include 하지 않는다. `Math.h` 우산이 `MathUtil.h` 를 먼저 넣어 주는 덕에
가려져 있었다 — 그 둘만 직접 include 하면 컴파일이 안 된다. 직접 가져오게 했다.
(이 건은 Container 작업 때 발견해 백로그에 적어 두었던 것이다.)

**2) `invert()` 가 특이행렬에 `Identity` 를 돌려주는데 문서에 그 말이 없었다.** 선언 주석은
"역행렬을 구합니다" 한 줄이었다. 실패를 알리는 통로가 없어 그렇게 정한 것 자체는 합리적이지만,
**호출부는 그것을 모른다** — 스케일 0 인 트랜스폼을 뒤집으면 오류 없이 Identity 로 계속 가고,
렌더러에서는 물체가 엉뚱한 자리에 조용히 그려지는 모양으로 나타난다. 판정 기준(`|det| < 1e-7`)까지
적었다.

**3) `float4::isInBounds` 만 비교 방향이 반대였다.** `float2` · `float3` · `double3` 은
`-bound <= v && v <= bound` 로 쓰는데 `float4` 만 `v <= bound && v >= -bound` 였다 — AGENTS 의
"범위 비교는 값을 가운데 둔다" 규칙과도 어긋난다. 결과는 같지만 같은 이름의 함수가 타입마다 다르게
읽히는 것이 다음 실수의 씨앗이다.

**살펴보고 손대지 않은 것**(멀쩡하다): `normalize()` 여덟 개(제자리·복사 × float2/3/4·double3)가
전부 일관되고 0 벡터에서 안전하다 — `MathUtil::invSqrt` 가 `x <= 0` 에서 0 을 돌려주므로
`0 * inf = NaN` 이 나오지 않는다. `double3` 은 배정밀도 경로를 따로 쓴다.

**회귀 테스트 둘.** `IsInBoundsAgreesAcrossVectorTypes`(네 타입이 경계 포함까지 같은 답을 내는지,
w 성분 포함) · `SingularMatrixInvertsToIdentity`(문서가 약속한 값인지, 그리고 정상 행렬은 여전히
제대로 뒤집히는지).

**검증.** Debug·Shipping 빌드 경고 0 · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 1/1 · 린트 15/15 ·
`MathTest` 12 → 14건 · 헤더 4개 전부 자립.

### 2026-09-17 (로그 출력 장치를 받아 놓고 한 줄도 주지 않았다 — Core/Log)

**1) 먼저, 앞 커밋(`0d3f00c7`)에서 내가 넣은 회귀를 고쳤다.** `ConcurrentQueue.h` 에서
`Core/Container/vector.h` 를 지웠는데 `drain( vector<T>& )` 가 그것을 쓴다. 토큰만 세어 "주석뿐" 으로
판단한 것이 틀렸다 — 그 결과 `ConcurrentQueue.h` 와 그것을 타는 **`Logger.h` 가 자립하지 못하게**
되었다(컴파일은 통과한다. 모든 TU 가 pch 로 vector 를 이미 갖고 있어서다). 되돌렸다.
**교훈: include 제거는 토큰 세기가 아니라 단독 컴파일로 확인한다.** 남은 폴더에도 그렇게 적용한다.

**2) 출력 장치가 8개에서 조용히 잘렸다.** `dispatchToOutputs` 는 잠금 안에서 포인터만 **고정 배열**로
떠 와 락 밖에서 쓴다(느린 파일 I/O 가 콘솔을 막지 않게 하는 분리다 — 좋은 설계다). 그런데 그 배열
크기 `8` 은 리터럴이었고 `addOutput` 은 그 사실을 몰랐다. 그래서 9번째부터는 **받아서 `open` 까지 해
놓고 한 줄도 주지 않았다.** 붙인 자리에서는 보이지 않는 실패다. 클래스 주석은 확장성을 광고한다
("에디터 패널·네트워크 등 — 그때 이 클래스를 고칠 일은 없습니다").

처음에는 상한에서 거절만 하게 고쳤는데, **테스트를 쓰다 그것도 틀렸다는 것이 드러났다**: 거절하면
`unique_ptr` 이 그 자리에서 파괴되는데 `addOutput` 은 `void` 라 호출자가 알 길이 없다 — 죽은 포인터를
들고 있게 된다(테스트가 실제로 그렇게 깨졌다). **조용히 무시하나 조용히 파괴하나 호출자에게는 같다.**
그래서 `bool` 을 돌려준다. 상한은 `_s_kMaxOutput` 이라는 이름을 얻었고 배열도 그것을 쓴다.

**3) 회귀 테스트.** `OutputsBeyondTheCapAreRejectedNotSilentlyIgnored` — 받아들여진 장치는 **전부**
그 줄을 받아야 한다. 고치기 전에는 8번째 뒤로 0 이었다.

**살펴보고 손대지 않은 것**(멀쩡하다): `ConcurrentQueue::enqueue` 는 CAS 를 이긴 뒤에만 옮기므로
큐가 가득 찼을 때 `Logger` 가 동기로 쓰는 폴백이 **온전한 레코드**를 쓴다(주석이 주장하는 그대로다).
`_pFileOutput` 는 `unique_ptr` 벡터가 재할당돼도 가리키는 객체가 안 움직이므로 안전하다.
리스너는 잠금 안에서 스냅샷을 뜨고 락 밖에서 방송하며, 리스너가 없으면 복사도 하지 않는다.

**검증.** Debug·Shipping 빌드 경고 0 · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 1/1 · 린트 15/15 ·
`LogTest` 13 → 14건 · 헤더 자립 5/5(회귀 포함해 다시 확인).

### 2026-09-17 (findVariable 이 내주는 포인터가 언제 죽는지 아무도 말하지 않았다 — Core/GlobalVariable)

**`findVariable` 은 맵 내부 주소를 그대로 돌려줬다.** 헤더는 그 사용법을 **권하기까지** 했다 —
"패널 등에서 반복 후 findVariable 로 편집 가능한 포인터를 얻으려는 용도". 실제로 에디터 패널이
그렇게 쓴다(`GlobalVariablesPanel.cpp:202`, 이름을 훑어 `vector<GlobalVariableInfo*>` 를 모은 뒤 정렬해
그린다).

그런데 `sw::unordered_map` 은 **밀집 배열**이다:

- 삽입하면 `_listDenseData` 재할당으로 **모든** 원소가 옮겨 간다.
- 삭제하면 swap-and-pop 으로 **마지막 원소가** 지운 자리로 옮겨 간다
  (`unordered_map.h:599-603`, "마지막 원소를 currentIndex 자리로 옮깁니다").

즉 그 포인터가 언제 죽는지 API 어디에도 적혀 있지 않았고, 실제로는 **다른 변수를 하나 등록하기만
해도** 죽는다. 지금 터지지 않는 이유는 순전히 타이밍이다 — 패널은 한 프레임 안에서만 쓰고,
모듈 등록·해제(`unregisterVariablesByModule`)는 다른 시점에 메인 스레드에서 돈다.

**값을 `unique_ptr<GlobalVariableInfo>` 로 든다.** 맵이 흔들려도 가리키는 객체는 제자리다. 이제
"그 변수가 등록 해제될 때까지 유효" 라는 계약을 실제로 지킬 수 있고, 헤더에 그렇게 적었다.

**회귀 테스트로 못박고 변이 테스트로 확인했다** — `FoundPointerSurvivesOtherRegistrations` 는 변수
하나를 찾아 두고 256개를 더 등록한 뒤 주소가 같은지 본다. 고치기 전 코드로 되돌리면
"등록을 반복했더니 같은 변수의 주소가 바뀌었다" 로 **실제로 실패한다**.

**검증.** Debug·Shipping 빌드 경고 0 · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 · 린트 15/15 ·
`GlobalVariableTest` 6 → 7건 · 실기동 `-dx12` exit 0 · `[Error]` 0건.

### 2026-09-17 (Core/Concurrency — 락프리 큐를 "뮤텍스 기반" 이라고 적어 두고 있었다)

**동작 결함은 없었다.** Chase-Lev 덱의 `pop`/`steal` 경합 경로와 SPSC 링의 메모리 순서를 따라가 봤고
둘 다 맞다. 여기서 나온 것은 **고르는 사람을 오도하는 문서와, 형제 클래스끼리 어긋난 결정**이다.

**1) `ConcurrentQueue.h` 의 `@file @brief` 가 "뮤텍스 기반 스레드 안전 큐" 였다.** 이 파일에는 뮤텍스가
한 줄도 없다 — Vyukov 시퀀스 넘버 기반 **락프리 MPMC 링**이다. `LockFreeQueue` 와 둘 중 무엇을 쓸지
고르는 사람이 정반대로 읽는다(막히는 줄 알거나, 무한 용량인 줄 안다 — 실제로는 가득 차면 false 다).

**2) `WorkStealingDeque` 만 캐시라인을 떼어 놓지 않았다.** `_top` 은 훔치는 스레드들이, `_bottom` 은
소유 스레드가 계속 쓴다. 한 라인에 앉으면 훔치기가 없어도 서로의 라인을 무효화한다(false sharing).
`LockFreeQueue` 와 `ConcurrentQueue` 는 같은 이유로 이미 `alignas( 64 )` 로 떼어 놓았는데 **여기만
붙어 있었다** — 셋 중 하나만 다른 답을 낸 자리다.

**3) `SpinLock.h` 가 `mutex.h` 를 include 했다.** 쓰지 않는다(확인했다) — 그런데 `mutex.h` 는
`DeadlockDetector` 를 끌고 온다. 스핀락 하나 쓰려는 TU 가 데드락 탐지기까지 물었다.

**4) `WorkStealingDeque` 에 `@brief` 가 하나도 없었다.** 저장소에서 가장 미묘한 파일인데(락프리,
소유 스레드와 훔치는 스레드가 규칙이 다르다) 어느 함수를 어느 스레드에서 불러야 하는지가 코드에
적혀 있지 않았다. 셋 다 적었다.

**5) "실제 엔진에서는 resize 로직이 필요하지만 여기서는 생략" 이 오해를 부른다.** 미완성처럼 읽히는데
사실은 **의도된 설계**다 — 늘리면 소유 스레드가 버퍼를 바꾸는 동안 훔치는 쪽이 옛 버퍼를 읽는다.
그래서 상한을 알리고, 넘치는 일감의 갈 곳은 호출부가 정한다(`TaskManager` 는 전역 큐로 흘려보낸다 —
`TaskManager.cpp:1268`). 그대로 적었다.

**6) `LockFreeQueue` 가 `_head`/`_tail` 을 생성자와 헤더 **양쪽에** 0 으로 초기화했다.** AGENTS 의
"초기값의 집은 하나" 규칙 위반이고, 생성자가 이기므로 헤더만 고치면 조용히 안 먹는다. 생성자 쪽을 지웠다.

**검증.** Debug·Shipping 빌드 경고 0 · `ctest -L nogpu` 양쪽 5/5 · `-L hostgpu` 양쪽 1/1 · 린트 15/15 ·
`DataStructureTest` 24/24(락프리 큐 SPSC 스트레스 · 작업 훔치기 덱 멀티스레드 스트레스 · MPMC 포함) ·
헤더 9개 자립.

### 2026-09-17 (파일워처 큐가 세 벌이었고, 아무도 빌드하지 않는 macOS 쪽만 규칙이 빠져 있었다)

`Core/File` 은 3,022줄 17파일인데 대부분 플랫폼 3분할이다. **Windows 는 로컬에서, Linux 는 CI 에서
빌드되지만 macOS 는 어디서도 컴파일되지 않는다** — 그러니 썩는다면 거기다. 실제로 거기였다.

**1) `pollEvents` 가 세 구현에 글자까지 똑같이 있었다.** 큐를 비우고, 넘쳤으면 합성 리스캔 하나를
덧붙이고, 표시를 내린다 — Windows·Linux·macOS 가 같은 함수를 각자 들고 있었다.

**2) 큐에 넣는 쪽은 이미 어긋나 있었다.** Windows 와 Linux 는 "직전과 같은 (동작, 파일)이면 접는다" 를
하는데(저장 한 번에 OS 가 알림을 둘씩 준다 — 리눅스의 IN_MODIFY+IN_CLOSE_WRITE, 윈도우의
LAST_WRITE+SIZE) **macOS 의 `pushEvent` 에는 그 규칙이 없었다.** Windows 주석이 "(Linux 워처와 같다)"
라고 의도를 밝히고 있는데도 그렇다. 같은 부하에서 macOS 만 큐가 먼저 차고 리스캔이 잦아진다.

**3) 그래서 큐를 `IFileWatcher` 로 올렸다.** `_directoryPath` · `_eventMutex` · `_listEventQueue` ·
`_bEventQueueOverflowed` 는 세 구현이 **똑같이** 선언하던 것이고, `pollEvents` 와 새 `pushChange`
(상한 · 오버플로 표시 · 연속 중복 접기)는 이제 한 번만 구현된다. 플랫폼 파일에는 그 OS 만 아는 것
(디렉터리 핸들 · IOCP · inotify · FSEvents 런루프)만 남는다. **컴파일되지 않는 플랫폼일수록 코드가
적어야 한다** 는 것이 이 정리의 근거다.

**4) 테스트가 아예 없었다.** 이제 로직이 한 곳이라 **플랫폼과 무관하게** 검사할 수 있다 —
`FileWatcherTest` 는 OS 를 전혀 건드리지 않고 `IFileWatcher` 를 상속한 스텁으로 규칙만 본다:
연속 중복 접기 · 연속이 아니면 유지 · 상한에서 합성 리스캔 하나로 접기 · 리스캔은 한 번만.

**검증 범위를 분명히 해 둔다.** Windows 는 로컬에서 빌드·테스트했고, **Linux 는 CI 가 빌드한다**
(`CI-Debug` ubuntu). **macOS 는 이번에도 아무도 컴파일하지 않는다** — Mac 파일의 변경은 정독으로만
확인했다. 다만 이 정리로 Mac 전용 코드가 줄었으므로 확인되지 않는 표면 자체가 작아졌다.

**검증.** Debug·Shipping 빌드 경고 0 · `ctest -L nogpu` Debug 5/5(연속 4회 안정) · Shipping 5/5 ·
`-L hostgpu` 양쪽 1/1 · 린트 15/15 · 헤더 9개 자립 · `FileWatcherTest` 3건 신규(0 → 3).

### 2026-09-17 (큐에 남은 이벤트를 파괴하지 않고 버렸다 — Core/Event)

**1) 실제 결함: `clear()` 와 소멸자가 큐에 남은 이벤트의 소멸자를 부르지 않았다.**

이벤트는 프레임 아레나에 placement new 로 올라간다. 아레나를 되감는 것은 **메모리만** 돌려줄 뿐
소멸자를 부르지 않으므로, 멤버가 든 힙은 그대로 남는다. `processEvents` 는 방송 뒤에
`pEvent->~IEvent()` 를 부르는데 **`clear()` 와 `~EventDispatcher()` 에는 그 한 줄이 빠져 있었다.**

게임플레이 이벤트는 대부분 `sw::string` 을 든다(`SaveRequestedEvent::_savePath` 등). 종료 시점에
큐가 비어 있지 않으면 그 문자열들이 샌다. 소멸자 호출 횟수를 세는 시험용 이벤트로 재현해
두 경로 모두 새는 것을 확인하고 `destroyQueuedEvents()` 로 모았다.

**2) `SW_REGISTER_EVENT_ID` 가 `namespace sw` 밖에서는 쓸 수 없었다.**

ID 식은 `sw::eventTypeIdFromString` · `sw::kEvent##Name` 으로 **한정해 두고**(= 밖에서도 쓰라는
뜻인데) 반환 타입 `EventTypeId` 와 `friend class EventDispatcher` 는 맨이름이었다. 밖에서 쓰면
타입을 못 찾고, friend 는 **전역에 새 클래스를 선언**해 진짜 디스패처가 `kType` 에 닿지 못한다.
기존 사용처가 전부 `namespace sw` 안이라 드러나지 않았다 — 테스트를 전역에 쓰다 컴파일 에러로 나왔다.
전부 `sw::` 로 한정하고(한정 friend 는 첫 선언이 될 수 없어 `EventDispatcher` 전방 선언을 더했다),
매크로 뒤가 `private:` 이라는 것도 `@warning` 으로 적었다.

**3) `friend class EventBus` — `EventBus` 는 이 저장소에 없다.** 죽은 friend 선언이라 지웠다.

**4) 같은 키의 맵이 두 벌이었다.** `_mapChannelDelegate`(키 → `shared_ptr<void>`)의 값은
`_mapChannelDispatchTable`(키 → `{방송 함수, shared_ptr<void>}`)의 `_pMulticast` 와 **같은
포인터**였다. 늘 함께 쓰이고 함께 비워지는 표를 두 벌 두면 한쪽만 고치는 날 디스패치가 죽은
멀티캐스트를 부른다. 디스패치 표 하나로 합쳤다.

**5) 구독은 잠금 밖, 해제는 잠금 안이었다.** `getOrCreateChannelDelegate` 가 잠금을 스스로 잡고
곧바로 놓아서, 호출부의 `add` 는 **잠금 밖**에서 돌았다. 반면 `unsubscribe` 는 잠금을 쥔 채
`remove` 를 했다 — 같은 자료구조의 두 변경이 한쪽만 보호됐다. 잠금 범위를 호출부가 정하게 바꿔
둘을 대칭으로 만들었다(`findOrCreateChannelDelegateUnlocked`).

**6) 그 남은 구멍(방송 중 교차 스레드 구독 변경)을 계약으로 닫았다.**

세 방안을 재 봤다.

| 방안 | 결과 |
| --- | --- |
| `broadcast` 를 **스냅샷**으로 | ✗ 같은 스레드 안전성이 깨진다 — 콜백이 자기를 해제해도 사본이 계속 호출된다(UAF). `MulticastDelegate` 가 방송 중 `remove` 를 그 자리에서 무효화하는 것이 바로 그 보호다 |
| `_busSpinLock` 을 **재귀**로 만들어 방송을 감쌈 | ✗ 임의의 콜백이 도는 내내 다른 스레드가 **스핀**한다 — 데드락·CPU 낭비 |
| **계약을 명시하고 강제** | ✓ 실제 설계와 일치하고, 조용히 어길 수 없다 |

진짜로 여는 것은 reader-writer 재설계인데, **`subscribe` 호출부가 프로덕션에 0개**인 기능에 그 비용은
과하다(실제 교차 스레드 경로는 `push`→`processEvents` 이고 그쪽은 이미 온전히 잠겨 있다).

그래서 스레드 계약을 표로 적고(큐 = 아무 스레드나 · 버스 = 퍼내는 스레드만) **Debug 에서 확인한다.**
기준점은 **생성한 스레드가 아니라 `processEvents` 를 부르는 스레드**다 — 어디서 만들었는지는 우연이지만
프레임마다 큐를 빼는 쪽은 설계상 하나로 정해져 있다(언리얼의 게임 스레드와 같은 자리). 첫 `processEvents`
가 주인을 못박고, 그전까지는(시작할 때 구독부터 하는 정상 흐름) 아무 말도 하지 않는다.

검사는 **Debug 전용**이다. `SW_LOG_ASSERT` 는 비-Debug 에서도 Error 를 남기므로(이번 세션에 Macros.h 에
적어 둔 그것이다) `publish` 마다 도는 검사를 그대로 두면 위반 시 배포본 로그가 도배된다.

지원되는 교차 스레드 경로는 테스트로 못박았다 — 워커 4개가 100건을 `push` 하고 퍼내는 스레드가
`processEvents` 로 전부 받는다.

**검증.** Debug·Shipping 빌드 경고 0 · `ctest -L nogpu` Debug 5/5 · Shipping 5/5 ·
`-L hostgpu` 양쪽 1/1 · 린트 15/15 · `EventTest` 5 → 8건(누수 둘은 수정 전에 실패하는 것을 확인) ·
실기동 `-dx12` exit 0 · `[Error]` 0건 · 스레드 어설트 미발생.

### 2026-09-17 (MulticastDelegate 는 이동이 복사로 떨어지고 있었다 — Core/Delegate)

**1) 이동이 없었다.** `MulticastDelegate` 가 복사 생성자·복사 대입을 `= default` 로 **선언** 하고
있었는데, 그 순간 암시적 이동 생성자·이동 대입이 생기지 않는다(C++ 규칙). 그래서 `std::move` 를
써도 구독자 벡터가 통째로 깊은 복사됐다. 프로브로 확인한 값:

```
nothrow_move_constructible = 0
after move: source.isBound() = 1      <- 원본이 그대로 남아 있다 = 복사로 떨어졌다
```

넷을 직접 적어 고쳤다. 고친 뒤 같은 프로브가 `1` / `0` 을 낸다(진짜 이동).

**2) 방송 상태까지 복사되고 있었다.** `_broadcastDepth` 와 지연 제거 큐는 값이 아니라 *그 인스턴스의
호출 스택 상태*다. `= default` 복사는 그 둘도 가져갔다 — **broadcast 중에 복사하면 사본이 깊이 0 이
아닌 채로 태어나, 그 사본에서 한 `remove` 가 영영 반영되지 않는다**(자기 broadcast 는 1→2→1 로만
오가므로 0 이 안 된다). 생성은 깊이 0 · 빈 큐로 시작하고, 대입은 받는 쪽의 깊이를 건드리지 않는다
(그 깊이는 지금 그 객체를 방송 중인 호출 스택의 것이다).

**3) 회귀 테스트 둘, 둘 다 변이 테스트로 확인했다.**

- `MulticastDelegateMovesInsteadOfCopying` — `static_assert` 로 이동 연산 존재를, 옮긴 뒤 원본이
  비는지로 진짜 이동을 본다. `= default` 로 되돌리면 **컴파일이 선다**(의도한 것이다).
- `CopyMadeDuringBroadcastStartsWithCleanBroadcastState` — 방송 중에 뜬 사본이 지연 제거에 갇히지
  않는지 본다. **처음엔 복사 대입으로 짰다가 변이 테스트에서 통과해 버려 다시 썼다** — 대입은 받는
  쪽의 깊이를 건드리지 않으므로 이 결함을 드러내지 못한다. 복사 **생성자**를 타야 한다.

**4) 곁다리.** `Delegate.cpp`(17줄)가 `Core/CoreMinimal.h` 를 끌어오고 있었다 — 필요한 것은 `atomic`
뿐이다. 함수 지역 static 이던 발급 카운터는 익명 네임스페이스로 올렸다(AGENTS 의 TU 로컬 상태 규칙).
`/** @brief */` 다섯 개가 `template <...>` 줄 **아래**에 있어 위로 올렸고(저장소의 다른 헤더는 전부
위에 둔다), 절 제목 "1) 핸들 ID" 가 전방 선언 위에 붙어 있던 것을 내용에 맞게 고쳤다.

**검증.** Debug·Shipping 빌드 경고 0 · `ctest -L nogpu` Debug 5/5 · Shipping 5/5 ·
`-L hostgpu` 양쪽 1/1 · 린트 15/15 · `DelegateTest` 10 → 12건.

### 2026-09-17 (SW_ENABLE_STL_CONTAINER 를 켜면 컴파일이 안 됐다 — 그리고 집합만 0-Alloc 계약이 깨져 있었다)

Container 는 6,946줄 18파일이라 정독 대신 **구조가 반복되는 자리**를 기계로 물었다. 헤더 자립성
검사(Compression 에서 통했던 것)는 18개 전부 통과했고, 대신 다른 축에서 넷이 나왔다.

**1) `SW_ENABLE_STL_CONTAINER` 를 켜면 아예 컴파일이 안 됐다.**

`pair.h` 와 `array.h` 가 `std::tuple_size` · `std::tuple_element` 특수화를 `#if` **밖에** 두고 있었다.
그 옵션을 켜면 `sw::pair` 는 `std::pair` 의 별칭이므로, 그 특수화는 표준 라이브러리가 이미 준 것을
다시 정의하는 꼴이 된다:

```
pair.h:442:  error: redefinition of 'tuple_size<sw::pair<T1, T2>>'
array.h:273: error: redefinition of 'tuple_size<sw::array<T, N>>'
```

`cmake/Config/BuildOptions.cmake:69` 에 살아 있는 옵션인데(기본 OFF) **켜면 서지도 않았다.**
특수화를 `#if !defined( SW_ENABLE_STL_CONTAINER )` 안으로 넣었다.

**2) `map` · `set` 의 기본 비교자가 두 경로에서 달랐다.**

| | STL 경로 | 커스텀 경로 |
| --- | --- | --- |
| `unordered_map` · `unordered_set` | `std::equal_to<>` | `std::equal_to<>` |
| **`map` · `set`** | **`std::less<Key>`** | **`std::less<void>`** |

그래서 `swMap.find( string_view )` 가 기본 빌드에서는 되고 그 옵션에서는 **컴파일이 안 됐다.**
이것은 `unordered_map.h` 가 헤더 주석에 "피하려고 이렇게 뒀다" 고 적어 둔 바로 그 상황이다
("같은 코드가 빌드 옵션에 따라 갈렸을 것이다"). 해시맵 쪽은 그 교훈을 지켰는데 정렬 맵/집합은
빠져 있었다. 둘 다 `std::less<>` 로 맞췄다.

**3) `unordered_set` 에만 이종 검색이 없었다.**

비교자 기본값은 이미 `std::equal_to<>` 였다 — 의도는 있었고 구현만 없었다. `find( const Key& )`
하나뿐이라 `unordered_set<string>` 을 `string_view` 로 찾으면 **키를 하나 만들어서** 찾았다.
`unordered_map` 과 같은 모양으로 이종 `find` · `count` · `contains` 를 붙였다.

**4) 회귀 테스트.** `DataStructureTest.AssociativeContainersFindWithoutBuildingAKey` — 네 연관
컨테이너 모두 키를 만들지 않고 찾는지 본다. **컴파일되는 것 자체가 검사다**(이종 오버로드가 사라지면
빌드가 선다). 수정 전에는 이 코드가 `no viable conversion from const sw::string_view` 로 섰다.

**검증.** 헤더 18개 × 두 경로 전부 컴파일 · Core 헤더 85개를 옵션 켜고 훑어 실패 0 ·
Debug·Shipping 빌드 경고 0 · `ctest -L nogpu` Debug 5/5 · Shipping 5/5 · `-L hostgpu` 양쪽 1/1 ·
린트 15/15.

**남은 것 (이번 범위 밖).** `Core/Math/VectorMath.h` · `MatrixMath.h` 는 **양쪽 경로 모두에서**
단독 컴파일이 안 된다(각 20건, `SW_API` 를 쓰면서 `Macros.h` 를 include 하지 않는다). 옵션과 무관한
자립성 문제이고 Container 밖이라 손대지 않았다. 그리고 **옵션을 켠 전체 빌드는 아직 확인하지
않았다** — 헤더 층까지만 확인했다. CI 가 이 옵션을 돌리지 않으므로 또 썩을 자리다.

### 2026-09-17 (없는 코덱을 요구하면 조용히 무압축으로 바꿔치고 헤더엔 그 코덱이라고 적었다 — Core/Compression)

**1) 실제 결함: `resolveCodec` 이 못 찾으면 무조건 Null 코덱을 돌려줬다.**

그 마지막 한 줄이 호출부의 오류 처리를 **전부 죽은 코드**로 만들었다. `compressBuffer` 의
"코덱 없음 → 경고 후 폴백" 도, `decompressBuffer` 의 `"Unsupported codec type in stream"` 에러도
**한 번도 실행된 적이 없다.** 결과가 둘이다:

- **쓰기**: `compressBuffer( …, Zstd )` 를 Zstd 없이 부르면 헤더에는 `_codecType = Zstd` 라고 적고
  페이로드는 **무압축**으로 썼다. 그 스트림을 Zstd 가 등록된 기계가 읽으면 쓰레기가 나온다.
  `Archive` · `BinarySerializer` 가 코덱을 골라 넘기므로 닿는 경로다.
- **읽기**: 모르는 `_codecType` 이 든 스트림을 Null 코덱으로 "해제" 했다. 체크섬 플래그가 꺼진
  스트림이면 그 쓰레기가 **성공으로** 돌아갔다.

내장 코덱은 자기가 실제로 구현하는 둘(`None` · `RLE`)에만 물러나고, 나머지는 `nullptr` 이다.
이제 쓰기는 경고를 남기고 헤더에 **`None` 이라고 정직하게** 적으며(어디서든 복원된다), 읽기는 선다.

**회귀 테스트 둘을 넣고 둘 다 변이 테스트로 확인했다** — 고치기 전 동작으로 되돌리면 둘 다 실패한다.
읽기 쪽 테스트는 처음에 `vector` 오버로드로 썼다가 **고쳐도 안 고쳐도 통과**하는 것을 보고 다시 썼다:
그 오버로드는 "푼 크기 != 헤더의 원본 크기" 를 한 번 더 보기 때문에 이 결함을 우연히 걸러낸다.
고정 용량 오버로드로 묻고 체크섬 플래그도 꺼야 이 수정이 한 일을 실제로 검사한다.

**2) `CompressionStream.h` 가 자립하지 않았다.** `vector<uint8>` 을 쓰면서
`Core/Container/vector.h` 를 include 하지 않았다 — 소비자가 전부 `pch.h` 를 먼저 타서 우연히
컴파일됐다. 폴더의 헤더 다섯을 전부 단독 컴파일로 확인했고(이것 하나만 깨졌다) include 를 더했다.

**3) 디스크 포맷 상수가 흩어져 있었다.** 매직 `0x53574353` 이 `CompressionHeader::_magic` 기본값과
`CompressionStream::kMagicNumber` 에 **두 벌**, 판 번호 `1` 이 세 곳, 체크섬 플래그 `0x01` 이 주석과
리터럴 두 곳에 있었다. `CompressionHeader::kMagic` · `kVersion` · `kFlagChecksum` 하나씩으로 모았다.
그리고 **28바이트 `static_assert`** 를 걸었다 — 이 구조체는 그대로 디스크에 나가므로 크기가 바뀌면
예전 스트림이 조용히 어긋난다. 주석이 아니라 컴파일러가 지킨다.

**4) `_codecType` 이 `uint8` 에 주석으로 `// CompressionCodecType` 이었다.** enum 의 언더라잉 타입이
`uint8` 이라 레이아웃은 그대로면서 타입만 얻는다 — 읽고 쓰는 쪽의 `static_cast` 두 개가 사라졌다.

**5) 레지스트리 맵 키가 `uint8` 이라 `static_cast` 가 다섯 곳이었다.** enum 으로 키를 바꿔 전부 없앴다.

**6) `_defaultCodecType` 만 동기화 없이 읽고 쓰였다.** 맵은 `_mutex` 로 지키면서 이 필드는 맨몸이었다
(렌더·잡 스레드가 같이 타는 경로다). `_mutex` 로는 못 지킨다 — `getDefaultCodec()` 이 이 값을 읽고
곧바로 `getCodec()` 을 부르는데 그쪽이 같은 뮤텍스를 잡아 재귀 잠금으로 죽는다. `atomic` 으로 바꿨다.

**검증.** Debug·Shipping 빌드 경고 0 · `ctest -L nogpu` Debug 5/5 · Shipping 5/5 ·
`-L hostgpu` Debug 1/1 · Shipping 1/1 · 린트 15/15 · `CompressionTest` 5 → 7건.

### 2026-09-17 (Core/Common 정리 — 문자열 뷰만 전역에 있어 `sw::string_view` 가 컴파일이 안 됐다)

**1) 전역 `string_view` 를 없애고 `sw` 안으로 옮겼다.**

`Types.h` 가 `string_view` · `wstring_view` 를 **전역**에 두고 있었다. 그런데 이 저장소의 컨테이너는
전부 `sw` 안이다 (`sw::string` · `sw::vector` · `sw::unordered_map`). 그래서 `namespace sw` 안에서
`string` 은 `sw::string` 으로, `string_view` 는 `::string_view` 로 풀렸고 — **`sw::string_view` 라고
쓰면 컴파일이 안 됐다.** 이번 세션에 실제로 걸렸다(`TestShaderBakeRecipe.cpp` 를 쓰다 막혀서 맨이름으로
우회했다).

나누는 기준은 "누구의 이름인가" 다. `int32` · `utf8` 은 고정폭 기본형에 붙인 이름이라 `int` 와 같은
층, 즉 전역이다. 문자열 뷰는 **컨테이너 이름**이므로 `sw` 안이다.

옮기고 나서 깨진 곳은 **전역 스코프에서 맨이름을 쓰던 자리뿐**이었다 — 소스는 거의 전부 이미
`namespace sw` 안이라 한 줄도 안 바뀐다. 테스트 13개 파일 29곳을 `sw::` 로 한정했다(테스트 본문은
`SW_TEST_CASE` 라 전역 스코프다).

**빠뜨린 곳이 없는지는 컴파일러만으로 확인하지 않았다.** Windows 빌드는 Linux/Mac 전용 파일을
컴파일하지 않기 때문이다. 중괄호 깊이로 `namespace sw` 안/밖을 판정하는 스크립트로 저장소 전체
(Source · Test · Tools/ReflectionParser)를 훑어 **밖에 남은 맨이름 0건**을 확인했다
(유일한 히트는 `StdHeaders.h` 의 `#include <string_view>` 로 오탐).

**2) `EnumUtil.h` 가 표준 헤더 48개를 끌어오고 있었다.**

`StdHeaders.h`(`<regex>` · `<random>` · `<iostream>` 포함)를 include 했는데 실제로 쓰는 것은
`<type_traits>` 넷뿐이다. 비트플래그 연산자를 보려고 이 헤더를 include 한 쪽이 파싱 테이블까지
같이 물었다. `<type_traits>` 로 좁혔다.

**3) `Macros.h` — 절 번호가 어긋나 있었고, 어서션 안내가 사실과 달랐다.**

- 절 번호가 `1,2,2,3,4,5,6,…,10-a,7,8,9` 였다. 236줄짜리 기반 헤더에서 목차가 흔들리면 찾기가 어렵다.
  `1..11` 로 다시 매겼다.
- **"두 매크로 모두 Release 에서 no-op 입니다" 가 거짓이었다.** `SW_LOG_ASSERT` 는 비-Debug 에서
  **Error 로 로그를 남긴다**(Logger.h 가 그렇게 바뀐 지 오래다 — "예전엔 통째로 no-op 이었고 그것이
  배포본에서 계약이 깨진 순간을 놓치는 가장 큰 구멍이었다"). 어느 어서션을 쓸지 가르치는 자리가
  반대로 적혀 있었다. 둘의 배포본 동작을 나눠 적었다.
- `arrayCountHelper` 가 **전역 스코프의 템플릿 함수**였다. 모든 TU 가 이 헤더를 타므로 전역 이름
  하나가 곧 저장소 전체의 이름 하나다. `sw` 안으로 넣고 `SW_COUNT_OF` 가 한정해 부른다 — 사용처
  7곳은 그대로다.
- `SW_COUNT_OF` 절의 "Macros.h 단독 include 가능 / Types.h 없이도 컴파일" 주석은 사실이 아니었다
  (이 파일은 7번째 줄에서 Types.h 를 include 하고, 헬퍼가 그쪽 `utf8` 을 쓴다). 지웠다.

**검증.** Debug·Release·Shipping 빌드 경고 0 · `ctest -L nogpu` Debug 5/5 · **Shipping 5/5** ·
`-L hostgpu` Debug 1/1 · **Shipping 1/1** · 린트 15/15.

### 2026-09-17 (CI 가 못 도는 집합을 아무도 안 돌고 있었다 — 그리고 Shipping CI 는 CoreTest 하나만 돌았다)

앞 건을 고치고 남은 질문이 "그럼 `RenderPassGpuTest` 는 앞으로 누가 도나" 였다. 파 보니 구멍이
하나가 아니었다.

**1) Shipping CI 가 CoreTest 하나만 돌고 있었다.**

```yaml
cmake --build --preset ${{ matrix.preset }} ${{ ... && '--target App CoreTest' || '' }}
ctest  --test-dir build/${{ matrix.preset }} ${{ ... && '-R CoreTest' || '-L nogpu' }}
```

즉 `EngineTest` · `ReflectionTest` · `SmokeTest` · `EditorTest` 가 **배포 구성에서 한 번도 돌지
않았다.** 그런데 Debug 가 숨기는 결함이 드러나는 곳이 바로 그 구성이다. 로컬에서 재 보니
Shipping `-L nogpu` 는 **2.91초**에 전부 통과한다 — 아낄 것이 없었다. 좁히는 조건을 지웠다.

**2) CI 가 못 도는 집합을 고르는 방법이 "전체를 돌린다" 뿐이었다.**

`EngineTest_NoGPU` 는 다섯 스위트를 **뺀다**. 그 다섯을 고르는 이름은 없었고, CMake 주석은
"GPU 가 있는 개발자가 돌리는 `EngineTest` 전체에는 그대로 있다" 고 적어 두었지만 **배포 구성에서
그것을 돌리라는 말은 어디에도 없었다.** 그래서 아무도 안 돌았다.

`EngineTest_HostOnly` (라벨 `hostgpu`) 를 만들었다 — `NoGPU` 가 빼는 바로 그 집합을 고른다.

```powershell
ctest --test-dir build/Ninja-Shipping -L hostgpu --output-on-failure
```

빼는 목록과 고르는 목록은 형태가 달라 한 문자열로 못 쓴다. 대신 **갈라지지 못하게 막았다**:
`CheckTestSuites.py` 가 두 필터와 `SW_TEST_REQUIRES_HOST` 마커 셋이 모두 같은 집합인지 본다.
한쪽이 빼는데 다른 쪽이 안 고르면 "그 스위트는 아무 데서도 안 돕니다" 로 선다. 자가 테스트
케이스도 같이 넣었다(`CheckLintsAreAlive` 19 → 20건).

**3) 그 명령을 만들자마자 두 건이 더 나왔다.**

`ShaderCompilerTest.BasicCompileAndReflection` · `MultiBackendShaderCacheIsolation` 이 Shipping 에서
졌다. 이건 **결함이 아니라 테스트가 환경을 안 본 것**이다 — 배포 팩은 `CookAssets` 가 고른 타깃
RHI **하나만** 담는데(`Target RHI for shader packaging: dx12`), 두 케이스는 DXBC(dx11)를 요구한다.
디스크에는 있지만 팩에 없다. 배포본은 백엔드를 하나만 쓰므로 넷을 다 담는 것이 답은 아니다.
테스트의 스킵 조건(`isShaderCompilerUnavailable`)이 "컴파일러 없음" 만 보고 "이 팩에 없는 백엔드"
를 안 봤다. 조건을 넓히고 이름을 `isShaderUnavailableInThisBuild` 로 바꿨다 — 이제 두 가지를
뜻하므로.

**검증.** Debug `-L nogpu` 5/5 · Debug `-L hostgpu` 1/1 · **Shipping `-L nogpu` 5/5** ·
**Shipping `-L hostgpu` 1/1** · 린트 15/15 · `CheckLintsAreAlive` 20건 · 두 프리셋 빌드 경고 0.

### 2026-09-17 (베이커는 XML 만 보고 런타임은 C++ 에서 define 을 얹었다 — Shipping 에서 G버퍼가 통째로 사라졌다)

앞 작업 중에 찾은 "1-A" 를 팠고, 끝냈다. **UB 도 미초기화도 아니었다** — 셰이더 베이킹 구멍이다.

증상은 `RenderPassGpuTest` 두 건이 Shipping 에서만 지는 것이었다(Debug 는 24/24). 로그가 원인을
그대로 말하고 있었다:

```
[ShaderCache] Precompiled shader binary not found in shipping pack:
              'engine/shaders/gbuffer.hlsl' [VSMain] — .../gbuffer_vs_9820570b.dxil
[ShaderReflectionLibrary] 리플렉션 매니페스트에 ... (define: SW_PASS_GBUFFER=1) 가 없습니다
```

`gbuffer` 는 구워져 있었다 — **다른 해시로.** 원인은 "이 패스가 얹는 define 은 무엇인가" 에
답하는 자리가 **둘**이었다는 것이다:

| | 어디를 보나 | G버퍼 패스의 답 |
| --- | --- | --- |
| 베이커 (`ShaderBakeRecipe.cpp`) | 파이프라인 XML 의 `_listPermutation` | `{}` (XML 에 `<_listPermutation />`) |
| 런타임 (`FrameRendererResources.cpp`) | XML + **C++ 의 `kPassGBufferDefine`** | `{SW_PASS_GBUFFER=1}` |

그래서 런타임이 요청하는 해시를 **아무도 굽지 않았다.** Shipping 은 런타임 컴파일이 없으므로
G버퍼 드로우가 통째로 사라지고, 디퍼드 화면은 한 색으로 남고 SSAO 는 가림을 하나도 내지 않는다.
Debug 는 런타임에 컴파일해 버리므로 아무 일도 없었다.

이 파일의 헤더 주석이 이미 그 위험을 적어 두고 있었다("define 을 합치는 규칙이 런타임과 같은
자리를 봐야 한다"). 뷰 모드 축(`kViewModeUnlitDefine`)은 그렇게 되어 있었는데, 나중에 들어온
G버퍼 패스 define 만 베이커 쪽에 반영되지 않았다.

**고친 방식은 이 저장소가 이미 여러 번 쓴 것과 같다: 정본을 하나 두고 둘 다 그것을 본다.**
`FrameRendererUtil::getPassDefine( RenderPassType )` — `hasPixelStage` · `usesMaterialShader` 와
같은 자리다. 런타임은 PSO 를 만들 때, 베이커는 레시피를 모을 때 **같은 이 함수**를 부른다.
패스에 define 을 더할 자리는 앞으로도 여기 하나다.

베이크를 다시 돌리니 28개가 새로 구워졌다. `gbuffer` 뿐 아니라 `forwardlit` · `sprite2d` 변형도
들어 있는데 맞는 동작이다 — `usesMaterialShader(GBuffer)` 가 true 라 G버퍼 드로우는 **머티리얼의**
셰이더로 그리므로, (G버퍼 패스 define × 머티리얼) 조합이 전부 빠져 있었다.

그리고 `gbuffer_*_ea908ddb.*` 8개(4 RHI × VS/PS)는 이제 어느 레시피도 만들지 않는 **고아**가
되었다 — 베이커는 낡은 산출물을 지우지 않는다. `CookAssets.py --verify-shaders` 가 "매니페스트에
없는 바이너리" 로 정확히 잡아 패킹을 막았다(그 게이트는 제 일을 했다). 지웠다.

**회귀 테스트를 넣었다** — `ShaderBakeRecipeTest` (`Test/EngineTest/TestShaderBakeRecipe.cpp`).
그림을 그리지 않고 레시피 목록만 대조하므로 **GPU 도 DXC 도 필요 없고, 그래서 CI 가 돌린다.**
이것이 중요한 이유: 원래 증상은 `RenderPassGpuTest` 에서만 보였는데 그 스위트는
`EngineTest_NoGPU` 에서 빠져 있어 **CI 가 한 번도 돌린 적이 없다.** 고치기 전 커밋으로 되돌려
이 테스트가 그 두 줄로 실패하는 것을 확인했다.

**검증.** Debug·Shipping 빌드 경고 0 · `RunBuildWarnings` 전체 훑기 0건 · `ctest -L nogpu` 5/5 ·
린트 15/15 · Shipping EngineTest **456/458 (실패 0)** — 고치기 전에는 452/456 (실패 2) ·
Debug `RenderPassGpuTest` 24/24 · `BackendSmoke.py` 네 백엔드 × 2회 전부 exit 0 · `[Error]` 0건.

### 2026-09-17 (백엔드를 고르는 같은 사슬이 두 벌이었고, 이미 답이 갈려 있었다)

"커맨드라인이 백엔드를 명시했는가" 를 두 곳이 각자 물었다. `EngineLoop` 은 "명시했는가"(안 했으면
`EngineConfig::_defaultRHI` 로 덮는다), `RHI::initialize` 는 "무엇인가"(와, 쓸 수 없을 때 조용히
폴백해도 되는가). 네 플래그(`-dx11`/`-dx12`/`-vk`/`-gl`)를 훑는 사슬이 글자 그대로 두 벌이었다.

**두 벌은 이미 다른 답을 내고 있었다.** `EngineLoop` 쪽만 `-gv_rhiBackend` 를 명시로 쳤고 `RHI` 쪽은
아니었다. 그래서 쓸 수 없는 백엔드를 `-gv_rhiBackend` 로 고르면 **에러 없이 다른 백엔드로 떴고**,
같은 것을 `-vk` 로 고르면 에러였다. 로그에도 "요청과 다른 것으로 떴다" 는 말이 없다 — 이 저장소가
한 번 겪은 "네 백엔드를 검증했다고 믿은 것이 전부 한 백엔드" 와 같은 모양이다.

`RHIBackendUtil::findCommandLineBackend( cli, outBackend )` 하나로 모았다(`RHI.h`, `gv_rhiBackend`
바로 옆). 두 질문 모두 이것으로 답한다 — 반환값이 "명시했는가", `outBackend` 가 "무엇인가".
`EngineLoop.cpp` 의 익명 네임스페이스(`EngineLoopInternal`)는 이 함수 하나뿐이었으므로 통째로 사라졌다.

**의도한 동작 변경 하나**: 이제 `-gv_rhiBackend` 로 고른 백엔드도 쓸 수 없으면 **조용히 폴백하지 않고
에러로 선다.** 짧은 플래그와 같아진다.

부수로 `Scripts/dev/BackendSmoke.py` 의 낡은 경고를 고쳤다 — "`-gv_rhiBackend` 는 무시된다" 고
적혀 있었는데 그건 그 문장을 쓴 뒤에 고쳐진 일이다. 지금은 먹는다(실기동으로 0·2 둘 다 확인).

**검증.** Debug·Shipping 빌드 경고 0 · `ctest -L nogpu` 5/5 · 린트 15/15 ·
`BackendSmoke.py` 네 백엔드 × 불투명/반투명 8회 전부 exit 0 · `[Error]` 0건 · 평균 RGB 서로 1.0 이내 ·
`RenderPassGpuTest` Debug 24/24 · 실기동 `-gv_rhiBackend=2`→Vulkan, `-gv_rhiBackend=0`→DirectX11,
`-gl`→OpenGL.

**이 작업 중에 찾은 별건**: `RenderPassGpuTest` 두 건이 Shipping 에서만 진다 — 내 커밋이 아니고
`97095c4c` 부터 그랬다. 위 "1-A" 참고.

### 2026-09-17 (동의어 등록이 중간에 멈추면 없는 인자를 가리키는 이름이 남았다 — CommandLineManager 정리)

**1) 실제 결함: `addArgument` 가 반쯤 등록된 상태를 남겼다.**

동의어를 하나씩 `_mapArgument` 에 넣다가 이미 쓰는 이름을 만나면 그 자리에서 되돌아갔다.
그런데 `_listArgument.push_back` 은 **맨 끝에** 있었다 — 그 앞에서 이미 넣은 동의어들은
끝내 만들어지지 않는 인덱스를 가리킨 채 표에 남는다. 그 이름으로 조회하면
`_listArgument[없는 인덱스]` 를 읽는다. 지금 등록 목록에서는 첫 이름이 먼저 걸려 터지지 않았을
뿐이고, `{ "새이름", "이미쓰는이름" }` 순서면 그대로 범위 밖 읽기다.
→ **전부 검사한 다음에 넣는다.** 하나라도 겹치면 아무것도 남기지 않는다.

**2) 열거형 조회가 먼 길로 가고 있었다.** `CommandLineArgument` → `switch` 로 이름 문자열 →
해시 → 맵 → 인덱스. 그런데 `ArgumentList.xxx` **한 줄이 열거 멤버 하나와 `_listArgument` 원소
하나를 같은 순서로** 만든다 — 열거값이 곧 인덱스다. 그 일치를 `initialize` 가 줄마다 assert 하고,
조회는 바로 인덱싱한다. 매크로 세 번째 펼침(`argumentEnumToString`)이 통째로 사라졌다.
`Count` 와 `initialize` 이전 조회는 범위 검사로 걸린다.

**3) 값 변환 실패가 조용했다.** `StringUtil::parseInt` 의 반환값을 버리고 있어서 `-WIDTH=abc` 가
아무 말 없이 0 이 됐다. 창이 왜 안 뜨는지 알 길이 없다 — 이제 경고를 남긴다.

**4) 죽은 코드와 중복.**

- `parseInternal` + `kLineDelim`: 선언·정의만 있고 **부르는 곳이 없었다.** 지웠다.
- `StringHash` / `StringEqual`: `sw::` 에 그 이름으로 놓인 구조체 둘인데, `sw::unordered_map` 의
  기본값(`std::hash<sw::string>` 는 transparent, `KeyEqual` 은 `std::equal_to<>`)이 이미 같은 것을
  보장한다 — 컨테이너 헤더가 그렇게 쓰라고 적어 둔 계약이다. 지우고 기본값을 쓴다.
- `getArgument` 의 `if constexpr` 다섯 갈래(48줄)를 `readValue` 하나로 접었다. 저장 타입은 넷뿐이니
  요청 타입 T 를 그중 하나로 접어 variant 에서 한 번만 꺼낸다. 지원하지 않는 T 는
  `static_assert` 로 컴파일 때 선다(예전엔 조용히 false 였다).
- 직접 적은 앞뒤 공백 트리밍 → `StringUtil::trim`.
- 안 쓰는 include 셋(`GlobalVariableManager.h` · `StringBuilder.h` · `string_splitter.h`).
- `isArgumentProvided` 에 열거형 오버로드를 붙였다 — `getArgument` 와 짝이 맞는다.

**함정 하나.** `ArgumentInfo` 의 생성자에서 `SW_API` 를 떼면 **링크가 깨진다.** `addArgument` 가
템플릿이라 호출한 쪽 TU 에서 그 생성자를 찾기 때문이다. 내보내는 대신 **생성자를 헤더에 인라인으로**
두어 DLL 경계 심볼 자체를 없앴다.

**테스트.** `EnumLookupMatchesStringLookup` — 열거형 경로와 문자열 경로가 같은 인자를 가리키는지,
`Count` 가 범위를 넘지 않는지. 동의어 충돌 경로는 **테스트로 만들 수 없다** — `SW_LOG_ASSERT` 가
Debug 에서 `__debugbreak()` 라 테스트 실행이 죽는다(단언을 잠시 끄는 장치가 이 저장소에 없다).

**검증.** Debug·Shipping 빌드 경고 0 · `RunBuildWarnings` 전체 훑기 0건 · `ctest -L nogpu` 5/5 ·
린트 15/15 · CoreTest 178/178(Shipping 170/178, 8 건너뜀) · 실기동 dx12 1000×700 과 vk 각각
종료 코드 0 · `[Error]` 0건, `-PORT=abc` 경고와 `-gv_nosuchvariable` 경고 확인.

### 2026-09-14 (커밋 훅이 게이트 열둘 중 여섯만 돌고 있었고, 그 여섯도 C++ 이 있을 때만 돌았다)

앞 두 커밋이 게이트를 셋 더했는데 **커밋 훅에서는 하나도 돌지 않았다.** 확인해 보니 원래부터
그런 구조였다 — `PreCommitLint.py` 가 게이트를 **이름으로 import** 하고 있었다.

**1) 목록이 짧았다.** 게이트 열둘 중 여섯만 import 되어 있었다.
`CheckEngineLayers` · `CheckDataFileReferences` · `CheckSourceGlob` 은 게이트가 생긴 이래로
커밋 훅에서 한 번도 돈 적이 없다. 여기에 새로 넣은 셋이 더해져 여섯이 빠져 있었다.

**2) 그리고 더 나빴다 — 그 여섯도 `if stagedCppFiles:` 안에 있었다.**

`.cmake` 나 `.py` 만 커밋하면 Resource 소문자 검사와 셰이더 검증 말고는 **아무것도 돌지 않았다.**
바로 앞 커밋(`1b4fb6f0`, CMake 12개 파일)이 실제로 그렇게 통과했다 — 훅 출력에
"검사 대상 C++ 파일 없음" 한 줄만 찍히고 끝났다.

**고친 방식은 이 저장소가 이미 세 번 쓴 것과 같다: 폴더가 목록이다.**

`PreCommitLint` 는 `discoverLintScripts("gate")` 로 폴더를 훑는다. 훅이 알아야 하는 것
(언제 도는가 · staged 부분집합을 어떻게 받는가)은 **게이트가 든다** — `LintCatalog` 가 CMake 등록
정보를 게이트에서 가져가는 것과 같다:

| 선언 | 뜻 |
| --- | --- |
| `preCommitPattern` | 이 패턴(`fnmatch`, 저장소 기준 경로)에 맞는 파일이 staged 되었을 때만 돈다. **비우면 항상** |
| `preCommitFileArgument` | `"--files"` · `"positional"` · `""`(전체를 훑는 게이트) |
| `preCommitSkipReason` | 훅에서 돌 수 없는 이유 (`CheckSourceGlob` 은 `--build` 가 필요하다) |

이제 `PreCommitLint.py` 에 게이트 이름이 **하나도 없다.**

확인: `.cmake` 만 staged → CMake 게이트 둘이 돌고 나머지는 "건너뜀 (해당 파일 변경 없음)".
세 종류(CMake · Python · C++)에 일부러 위반을 넣어 전부 `[FAIL]` 로 서는 것을 봤다.

**3) 속도.** 게이트가 여섯에서 열둘로 늘었으니 훅도 느려진다. 두 곳을 고쳤다:

- `CheckCmakeReadme` 가 `repositoryRoot.rglob()` 으로 `Tools/vcpkg` 를 통째로 걷고 있었다
  (내가 어제 넣으면서 만든 것이다). 가지를 치니 **3,370ms → 541ms**.
- `CheckIncludeOrder` 에 `--files` 가 없어서 staged 한 파일을 봐도 969개를 전부 훑었다.
  붙였다 (**2,425ms → 파일 수에 비례**).

결과: C++ 한 파일 커밋 훅 4.6초. 예전 구조보다 느리지만, 예전이 빨랐던 이유는 **검사의 절반을
건너뛰었기 때문**이다.

**검증.** 린트 CTest 15/15 · 게이트 12/12 · 자가 테스트 3/3 · 훅 3종 변이 테스트 전부 `[FAIL]`.

### 2026-09-14 (레지스트리가 규칙이었는데 강제가 아니었다 — 그래서 EditorModule 이 조용히 빠져 있었다)

앞 커밋이 RHI 만 고친 자리를 끝까지 닫았다.

**1) 레지스트리가 둘이었다 — 하나로 합쳤다.**

앞 커밋에서 `SW_RHI_MODULES` 를 새로 만들었는데, `SW_DYNAMIC_MODULES` 가 이미 있었다.
같은 질문("App 보다 먼저 빌드되어야 하는 것")에 레지스트리 둘을 두는 것이 바로 고치려던 문제다.
`sw_registerDynamicModule(<타겟> <종류>)` / `sw_getDynamicModules(OUT [KINDS ...])` 하나로 합쳤다.
종류는 `rhi | kit | game | gameframework | editor` 이고, 소비자가 필요한 것만 고른다.

**2) 등록 자리가 세 가지였다 — 만드는 자리로 모았다.**

| 모듈 | 예전 |
| --- | --- |
| `GF_*` 키트 | `sw_addGameFrameworkKit` 안에서 append |
| `GameFramework` · `SWGame` | **호출부**에서 직접 append |
| `RHI_*` · `EditorModule` | **등록 안 됨** |

`EditorModule` 은 아무 데도 등록되지 않은 채로 있었고 아무 에러도 나지 않았다 — 소비하는 자리가
이름을 리터럴로 들고 있었으니까. 이제 등록은 **타겟을 만드는 자리에서만** 한다
(`sw_addGameModule` 이 `SWGame` 을 등록한다 — `Source/Games/CMakeLists.txt` 가 아니라).

**3) 그리고 잊을 수 없게 만들었다 — `sw_verifyDynamicModuleRegistry`.**

레지스트리는 규칙이지 강제가 아니다. 구성 마지막에 디렉터리 트리를 훑어 **MODULE 타겟이 전부
등록되어 있는지** 대조하고, 빠지면 `FATAL_ERROR` 로 선다. MODULE 은 정의상 "이름으로 찾아 올리는
플러그인" 이라 App 이 반드시 먼저 빌드해야 하는 것들이다.

> 처음엔 `Source/` 부터 훑게 짰는데 `EditorModule` 을 못 잡았다 — `Source/Editor` 는
> `Source/CMakeLists.txt` 가 아니라 **루트가** 직접 `add_subdirectory` 하므로 `Source/` 의
> `SUBDIRECTORIES` 에 없다. 루트부터 훑는다. (등록을 일부러 빼서 실제로 서는 것을 확인했다.)

부수 효과로 `sw_configureAppDependencies` 가 세 단계에서 두 단계로 줄었다 — RHI·에디터를 따로 걸던
1)·2) 가 "등록된 것 전부" 하나로 합쳐졌고, Dev 분기의 중복 루프가 사라졌다.

**4) 게이트 둘의 범위가 좁았다.**

- `CheckCmakeConventions` 가 `ThirdParty/` 를 통째로 빼고 있었다. 그 아래 `CMakeLists.txt` 는
  **우리가 쓴 얇은 래퍼**이고 `sw_copyDxcDlls` 같은 우리 함수가 거기 있다. vcpkg 포트 파일만 뺀다
  (62 → 74 파일).
- `CheckPythonConventions` 가 `Scripts/` 와 `Tools/` 만 훑었다. 저장소 전체를 본다 — 다른 데 놓인
  파이썬이 조용히 규칙 밖에 있는 것이 바로 이 게이트를 만든 이유다.

**5) `cmake/README.md` 가 또 낡았고, 이번엔 게이트가 잡았다.**

레지스트리를 합치면서 `sw_registerRhiBackend` / `sw_getRhiBackends` 가 사라졌는데 문서는 그대로였다.
**어제 넣은 `CheckCmakeReadme` 가 그 자리에서 잡았다.** DXC 복사가 `RuntimeDependencies.cmake` 에
있다는 설명도 틀렸다(`ThirdParty/dxc/CMakeLists.txt` 다) — 같이 고쳤다.

**손대지 않기로 한 것.**

- **모듈 팩토리 셋의 나머지 골격.** 등록을 모으고 나니 남은 공통은 include 경로·pch·FOLDER 네 줄
  뿐이고, 링크 집합·export 매크로·리플렉션 유무는 셋이 진짜로 다르다. 공통 함수로 묶으면 차이가
  숨는다. 값어치 없는 추상화다.
- **`EngineTest` 의 `GF_*` 링크 목록.** RHI 와 모양은 같지만 이쪽은 **링크**다 — 무엇을 링크할지는
  테스트가 정하는 것이 맞다. 키트를 전부 자동 링크하면 충돌을 만들 수 있다.
- **`EngineTest_NoGPU` 필터 문자열.** `CheckTestSuites.py` 가 소스의 `SW_TEST_REQUIRES_HOST` 마커와
  양방향으로 대조하고 있다. 생성으로 바꿀 만한 이득이 없다.

**검증.** 게이트 12/12 · 자가 테스트 3/3 · 린트 CTest 15/15 · nogpu 5/5 · Debug 경고 0 ·
Shipping 빌드 통과(`/WHOLEARCHIVE` 여섯 개 그대로) · Shipping 테스트 CoreTest 169/177 ·
EngineTest 407/409 · ReflectionTest 96/101 · SmokeTest 1/1 · EditorTest 52/52 ·
`BackendSmoke.py` 네 백엔드 × 두 경로 전부 exit 0 · 에러 0.

### 2026-09-14 (같은 명명 어휘가 세 벌로 적혀 있었고, 이미 정반대 판정을 내고 있었다)

"Script 와 CMake 에 구조적으로 더 개선할 것이 없나" 를 훑어서 나온 다섯 건을 전부 닫았다.

**1) `CheckCodeConventions.py` — 판정을 한 곳으로.**

`AGENTS.md` 의 접두어 표(`p`/`pp` · `list` · `map` · `unique` · `arr` · 단수형)는 **하나**인데
코드는 **세 벌**을 들고 있었다: `checkParameterItemInternal` 252줄 · `checkLocalVariableItemInternal`
376줄 · `ClassMemberNamingRule` 130줄이 각자 정규식과 접두어 목록과 메시지 문구를 적었다.
그리고 **실제로 어긋나 있었다**:

| 이름 | 매개변수 | 지역변수 | 멤버 |
|---|---|---|---|
| `inoutListActors` | 잡힘 | **통과** (`inoutList` 를 빠뜨림) | — |
| `vector<uint8> listBuffer` | "`list` 를 빼라" | "`list` 를 빼라" | **통과** (`buffer` 를 바이트 단어로 안 침) |

같은 이름에 주체에 따라 **정반대 답**이 나왔다.

이제 `kMapContainerVocabulary` (어휘 넷) × `kMapNamingSubject` (주체 셋) 표 하나를
`checkContainerNamingInternal` · `checkPointerNamingInternal` 이 읽는다. **주체마다 다른 것은
선언을 찾는 방법(파싱)뿐이고, 찾은 이름을 어떻게 볼지는 셋이 같은 표를 본다.**

**2) 그 과정에서 멤버 정규식 둘이 눈을 감고 있던 것이 드러났다.**

`_kMemberRawPointerRe` 는 `_[^pP\s]` 였다 — `p`/`P` 로 시작하는 이름을 아예 제외해서
`_pointer` 같은 잘못된 이름을 **볼 수가 없었다**. 게다가 `const` 수식자를 허용하지 않아
`const utf8* _label;` 형태를 통째로 놓쳤다. 긍정 매칭으로 바꾸니 **숨어 있던 위반 14건**이 나왔고
전부 고쳤다(`_label`→`_pLabel` 9건, `_listBuffer`→`_bytes` 3건, `_listLiveCmdList`→`_listLiveCmd` 2건).

**3) 자가 테스트가 그 드리프트를 못 보던 이유도 고쳤다.**

`CheckCodeConventionsSelfTest` 는 "이 **카테고리**가 한 번은 잡히는가" 만 봤다. 그래서 같은 규칙이
주체마다 다르게 적혀 있어도 **하나만 살아 있으면 통과**했다. 주체 × 어휘 **교차표**(12칸)를 더했다 —
표는 손으로 들지 않고 두 목록의 곱이다. 한 칸을 일부러 죽여 실패하는 것까지 확인했다.

**4) Python 과 CMake 는 아무도 보고 있지 않았다 — 게이트 둘을 더했다.**

`AGENTS.md` 는 세 언어의 규칙을 적어 두었는데 게이트는 `kCppAllExtensions` 만 훑었다. **린트를
만드는 코드가 린트를 안 받는 상태**로 Python 11,927줄 · CMake 4,952줄이 쌓여 있었다.

- `gate/CheckPythonConventions.py` — AST 로 본다. 함수 `camelCase`, 모듈 상수 `kPascalCase`,
  파일 `PascalCase.py`. `TypeVar`·타입 별칭은 상수가 아니므로 제외한다(`PathLike` 는 `PathLike` 다).
  잡힌 것: `BackendSmoke.py` 의 `ppm_stats` · `BACKENDS` · `BACKGROUND` → 전부 개명.
- `gate/CheckCmakeConventions.py` — `sw_camelCase` 함수, `SW_UPPER_SNAKE_CASE` option,
  `_` 없는 함수 내부 변수. `set()` 만 보면 `file(GLOB _x ...)` 를 놓치므로 **역참조(`${_x}`)도**
  본다. 잡힌 것: `ReflectionCodeGen.cmake` 의 지역 변수 15개 → 전부 개명.

> 두 게이트를 넣으면서 **CMake 를 한 줄도 고치지 않았다.** 린트 CTest 12 → **15**
> (뒤의 README 게이트 포함). 폴더가 목록이라는 것이 또 한 번 증명됐다.

**5) RHI 백엔드 이름 넷이 세 곳에 글자 그대로 있었다.**

`sw_configureAppDependencies` · `Test/EngineTest` · `Test/SmokeTest` 가 각자
`RHI_DX11 RHI_DX12 RHI_GL RHI_Vulkan` 을 적었다. 그런데 이름을 확실히 아는 곳은 따로 있었다 —
`sw_addRhiBackendModule` 은 타겟을 **만들면서** 이름을 받는다. `sw_registerRhiBackend` /
`sw_getRhiBackends` 를 만들어 **정의하는 자리가 등록하고 쓰는 자리는 묻게** 했다
(키트가 `SW_DYNAMIC_MODULES` 로 이미 하던 방식이다). 덤으로 SmokeTest 는 DX11 하나만 의존하던
것이 넷 전부가 됐다. `ninja -t query` 로 네 백엔드가 실제로 걸린 것을 확인했다.

**6) `GenerateCMakeConstants.py` 가 아직 목록을 들고 있었고, 그 때문에 코드젠이 안 돌고 있었다.**

바로 옆 `GenerateToolchainCMake.py` 는 "키 목록을 여기서도 들지 않는다" 고 적어 놓고 JSON 키를
그대로 변환해 찍는다. 이쪽은 `set(SW_...)` 36줄을 손으로 들고 있었다(상수 86개 중 36개).
이제 `k*` 를 **전부** 기계적으로 변환해 찍는다 (`kDirSourceEngine` → `SW_DIR_SOURCE_ENGINE`).

그런데 손으로 들던 목록에는 **`SW_FILE_PARSER_CONFIG` 만 디렉터리를 앞에 붙이는** 예외가 있었다.
그래서 같은 이름이 파이썬에서는 `parser_config.json`, CMake 에서는
`Config/Environment/parser_config.json` 을 뜻했고, 그걸 모르는 자리가 디렉터리를 **두 번** 붙였다:

```cmake
"${CMAKE_SOURCE_DIR}/${SW_DIR_CONFIG_ENV}/${SW_FILE_PARSER_CONFIG}"   # Config/Environment/Config/Environment/...
```

그 경로는 존재하지 않으니 바로 아래 `if(EXISTS)` 가 **조용히 걸러 냈다**. 결과:
**`parser_config.json` 이 바뀌어도 리플렉션 코드젠이 다시 돌지 않았다.** (예전에 겪은
"FlagOps.gen.h 가 낡아서 Engine 빌드가 깨진" 증상의 뿌리가 이것이다.)

합치는 일은 합칠 줄 아는 쪽이 한다 — `ConfigConstants.h.in` 이 `@SW_DIR_CONFIG_ENV@/@SW_FILE_PARSER_CONFIG@`
로 조립한다(`kFileEnvToolchainConfig` 가 이미 그렇게 하고 있었다). C++ 쪽 값은 그대로다.
`parser_config.json` 을 건드리면 이제 파서가 다시 도는 것을 확인했다.

**7) `cmake/README.md` 는 정본이라면서 아무도 안 보고 있었다.**

`sw_registerLintTests` 를 "`CheckEngineLayers` · `CheckIncludeOrder` · `CheckSourceGlob` 일괄 등록"
이라고 적어 두었는데 그 목록은 두 커밋 전에 사라졌다. 문서를 고치고, `lint/` 의
`CheckLintsAreAlive` 에 해당하는 것을 `cmake/` 에도 놓았다 — `gate/CheckCmakeReadme.py` 가
**문서가 없는 파일·함수를 가리키고 있지 않은지** 본다(트리의 `*.cmake`, 백틱에 싸인 `sw_*`).
반대 방향은 보지 않는다: 표는 "주요" 헬퍼라고 말하지 전부라고 말하지 않는다.

**검증.** `ctest --preset Ninja-Debug-lint` 15/15 · `-L nogpu` 5/5 · Debug 경고 0 ·
Shipping 빌드 통과 · Shipping 테스트(`TestBin/` 을 `Bin/` 에서) CoreTest 169/177 ·
EngineTest 407/409 · ReflectionTest 96/101 · SmokeTest 1/1 (나머지는 skip).

### 2026-09-14 (픽서는 죽어도 아무도 몰랐다 — 그리고 자리가 규칙이라면서 자리를 안 보고 있었다)

앞 커밋들이 열어 둔 자리 둘을 닫고, 훑다가 나온 것 둘을 더 고쳤다.

**1) 픽서 음성 테스트 — `CheckFixersAreAlive.py`.**

`CheckLintsAreAlive` 는 `gate/` 만 봤다. 그런데 픽서는 게이트보다 **더 조용히** 망가진다:

- 게이트가 죽으면 "위반 0건" 이라 통과처럼 보인다. 나쁘다.
- 픽서가 **너무 많이 잡으면** 빨간 줄이 뜨는 게 아니라 **소스가 바뀐다.**
  `py -3 -m Scripts format` 은 969개 파일을 한 번에 고쳐 쓴다.

그래서 `FixPass` 가 조각을 **둘** 든다 — 반드시 고쳐야 하는 `badSample`, 건드리면 안 되는
`goodSample`. 변환 바로 옆에 있다(게이트의 `selfTestCases` 와 같은 이유). 게이트와 달리 프로세스를
띄우지 않는다: 픽서의 변환은 `(텍스트) -> (새 텍스트, 바뀌었는가)` 순수 함수라 그냥 부르면 된다.
`fixer/` 에 있는데 픽서가 아닌 `FormatModified` 는 `kFixerSkipReason` 에 이유를 적는다.

> **이 린트를 넣으면서 CMake 를 한 줄도 고치지 않았다.** `selftest/` 에 파일을 놓은 것이 전부다 —
> 바로 앞 커밋이 만든 폴더-기반 등록이 실제로 그렇게 동작한다는 증거다. 린트 CTest 11 → **12**.

**2) 그런데 그 등록이 "파일을 놓는 것"만으로는 안 돌고 있었다.**

목록을 CMake 에서 걷어내면서 **reconfigure 트리거도 같이 걷어냈다.** 예전에는 게이트를 더할 때
`AssetAndToolTargets.cmake` 를 같이 고쳤고, CMake 는 자기 listfile 을 감시하므로 그게 곧 트리거였다.
이제는 폴더에 파일만 놓으므로 아무 일도 일어나지 않는다 — 실제로 `gate/` 에 파일을 넣고 빌드해
확인했다(`ninja: no work to do`). `file(GLOB ... CONFIGURE_DEPENDS)` 로 두 폴더를 감시하게 고쳤다.
GLOB 결과는 쓰지 않는다 — 디렉터리를 빌드마다 다시 보게 만드는 것이 목적이다.
(확인: `selftest/` 에 파일을 놓고 `cmake --build` → "GLOB mismatch! ... +CheckProbeAlive.py" 로
configure 가 다시 돌고 CTest 에 등록됨.)

**3) `App.exe` 를 찾는 목록이 둘이었고, 이미 갈라져 있었다 — `Scripts/common/AppBinary.py`.**

| | `CookAssets.bakeShadersInternal` | `PreCommitLint.checkStagedShadersInternal` |
| --- | --- | --- |
| Ninja-Debug / Ninja-Release / Bin | 본다 | 본다 |
| **Ninja-Shipping/Bin** | **본다** | **안 본다** |

쿠커 쪽 주석에 Shipping 을 보는 이유가 적혀 있다 — *"Shipping App 도 베이커를 링크한다. 두 번째
Shipping 빌드부터는 Dev 빌드 없이도 스스로 다시 굽는다."* 커밋 훅은 그 줄을 못 봤으므로
**Shipping 만 빌드해 둔 사람은** 셰이더를 고쳐 커밋할 때 "App.exe를 찾을 수 없어 건너뜁니다" 를
받고 지나갔다. 후보 목록·실행·실패 줄 파싱이 이제 한 자리다.

**4) `py -3 -m Scripts` 의 명령 목록이 세 곳에 있었다.** 모듈 독스트링 · `--help` 설명 문자열 ·
`cmdXxx` 함수 여덟과 딕셔너리. `Subcommand` 표 하나로 모았고 `--help` 는 거기서 나온다.

**하지 않기로 한 것 — vcpkg 쪽 CMake 섬.** `FindLlvmBin.cmake` 와 `VcpkgPortsToolchain.cmake` 가
"후보 루트를 거슬러 올라가며 toolchain_config.json 을 찾는" 같은 루프를 각자 들고 있다. 공통 헬퍼로
빼려다 보니 **두 목록의 루트가 다르고**, `FindLlvmBin` 쪽은 `cmake/Environment/../../..` = 저장소의
**부모**부터 본다(저장소 루트 자체는 `CMAKE_SOURCE_DIR` 로만 닿는다). 의도인지 off-by-one 인지
판단하려면 실제 포트 빌드로 확인해야 하는데 그건 이 자리에서 못 한다. `cmake -P` 로 둘 다 로드되는
것까지는 확인해 두었다(`cmake -P cmake/Modules/Toolchain/Vcpkg/VcpkgPortsToolchain.cmake` → 정상).
**다음에 vcpkg 포트를 다시 빌드할 일이 있을 때 같이 본다.**

> **CMake 쪽은 이제 대체로 제자리다.** 남은 것(`TargetRules` · `BuildLayout` · `ThirdPartyLibs` ·
> 컴파일러 모듈)은 타깃 정의와 툴체인 바인딩이라 CMake 가 할 일 그 자체다. 파이썬으로 옮길 수 있는
> 것은 "목록"과 "이미 파이썬이 아는 값" 둘이었고, 그 둘은 앞 두 커밋에서 옮겼다.

**확인**:

- `CheckFixersAreAlive` **양방향**: 변환이 `text, False` 를 돌려주게 만들면 "이 변환은 죽었습니다",
  `_kIfHeadRe` 를 `^(if|for)` 로 넓히면 "건드리면 안 되는 조각을 고쳤습니다" 로 실패. 되돌리면 통과.
- `CookAssets --bake-shaders` 실제 실행(스트리밍 경로)과 `runShaderBake(..., bCapture=True)`
  (커밋 훅 경로) 둘 다 실행 — 종료 0, 컴파일 실패 줄 0, 작업 트리 변화 없음.
- `py -3 -m Scripts` 의 인자 없음(1) · `lint`(0) · `format`(0, 969파일 후 트리 변화 없음) ·
  잘못된 명령(2) 전부 확인.
- 린트 ctest **12/12**(하나 늘었다) · `Ninja-Debug` ctest **18/18**.

### 2026-09-14 (파이썬이 쓴 JSON 을 CMake 가 다시 파싱하고 있었다 — 그것도 키 철자를 둘로 나눠서)

바로 앞 커밋이 린트 목록을 옮겼고, 같은 질문을 툴체인 설정에 던졌다. `toolchain_config.json` 은
**`SetupEnvironment.py` 가 쓰는 파일**인데, CMake 가 그걸 **다시 읽고 있었다** — 그것도 여러 곳에서:

| 읽는 곳 | 키를 적는 방식 |
| --- | --- |
| `DetectToolchain.cmake` | `SW_KEY_*` 상수 일곱 + `if(jsonErr) set("")` 블록 일곱 (44줄) |
| `FindWindowsTools.cmake` | **리터럴** `"windows_sdk_dir"` · `"windows_sdk_version"` · `"msvc_tools_dir"` |
| `Tools/ReflectionParser/CMakeLists.txt` | `SW_KEY_LLVM_PATH` 와 **리터럴** `"libclang_dll_path"` 를 섞어서 |

**같은 키를 두 가지 철자로 적고 있으면 한쪽만 바뀌는 날이 온다.**

`Scripts/setup/GenerateToolchainCMake.py` 가 그 JSON 을 `set(SW_TOOLCHAIN_<키 대문자> "...")` 로
찍고, CMake 는 configure 때 `include()` 만 한다. **키 목록은 파이썬에도 없다** — JSON 에 있는 키를
그대로 찍으므로 `SetupEnvironment.py` 가 키를 하나 더하면 CMake 에서 바로 쓸 수 있다. 값이 없으면
변수도 없고, CMake 에서 정의되지 않은 변수는 빈 값이라 `if(X AND EXISTS ...)` 폴백이 그대로 돈다.

| | 전 | 후 |
| --- | ---: | ---: |
| `DetectToolchain.cmake` | 175줄 | **142줄** |
| `toolchain_config.json` 을 파싱하는 CMake 자리 | 5 | **2** (둘 다 vcpkg 전용, 아래) |
| 키 철자 | `SW_KEY_*` 상수 · 리터럴 두 벌 | **한 벌** |

**둘은 일부러 남겼다.** `FindLlvmBin.cmake` 와 `VcpkgPortsToolchain.cmake` 는 **vcpkg 가 별도 CMake
프로세스로 부른다**(`detect_compiler` 는 PATH·ENV 를 비운 채 툴체인 파일만 로드한다). 우리 빌드
디렉터리가 없는 상태에서도 성립해야 하므로, 후보 루트를 거슬러 올라가며 JSON 을 직접 찾는 그 코드가
거기 있어야 한다.

> **상수를 지우다 C++ 쪽을 한 번 깼다 — 확인하고 되돌렸다.** CMake 에서 안 쓰이게 된
> `SW_KEY_*` 일곱을 `GenerateCMakeConstants.py` 에서 통째로 뺐는데, 그중 셋
> (`MSVC_TOOLS_DIR` · `WINDOWS_SDK_DIR` · `WINDOWS_SDK_VERSION`)은 `ConfigConstants.h.in` 을 거쳐
> **C++ 의 `sw::config::kKey*`** 가 되는 값이었다. `configure_file` 은 빈 값을 조용히 채워 넣으므로
> configure 도 빌드도 통과했고, 생성 헤더의 문자열만 `""` 가 되어 있었다. 생성물을 직접 열어 보고
> 되돌렸다. **상수의 소비자는 하나가 아니다** — 지우기 전에 `.h.in` 도 본다.

**남은 자리**: `VcpkgPortsToolchain.cmake` 의 리터럴 키 셋. 별도 프로세스라 위 방식이 안 통하지만,
`FindLlvmBin.cmake` 처럼 `SW_KEY_*` 상수를 쓰게는 할 수 있다(그 파일도 GenerateConfigConstants 를
OPTIONAL 로 include 한다).

**확인**:

- 세 프리셋(Debug·Release·Shipping)의 `CMakeCache.txt` 에서 툴체인 관련 값 14종
  (`CMAKE_AR` · `CMAKE_MT` · 컴파일러 · 링커 · RC · ninja · vcpkg 툴체인 · sccache 런처 ·
  `SW_CACHED_WIN_*`)을 **전부 비교 — 동일**.
- **캐시를 지우고 처음부터** configure 한 결과를, 같은 조건에서 옛 코드(`git stash`)로 돌린 결과와
  비교 — 동일. (증분 configure 와 최초 configure 는 컴파일러 캐시 항목의 **타입**이 STRING/FILEPATH
  로 갈리는데, 그건 옛 코드도 똑같아서 이 변경과 무관하다는 것을 이 비교로 못박았다.)
- `ReflectionParser.exe` 를 지우고 다시 링크 — 정상, `libclang.dll` 복사 단계도 그대로.
- 생성된 `ConfigConstants.h` 의 `kKey*` 문자열 직접 확인.
- `Ninja-Debug` 전체 빌드 · ctest **17/17** · 린트 **11/11**.

### 2026-09-14 (CMake 가 린트 목록을 세 벌 더 들고 있었다 — 폴더가 목록이라고 해 놓고)

**"`gate/` 에 놓으면 그것이 게이트다" 라고 해 놓고, 게이트를 하나 더하려면 네 곳을 고쳐야 했다.**

1. `lint/gate/` 에 파일을 놓는다                                    ← 여기까지가 "자리가 규칙"
2. `Scripts/common/Constants.py` 에 `kScriptLintCheckXxx` 경로 상수
3. `GenerateCMakeConstants.py` 에 `set(SW_SCRIPT_LINT_CHECK_XXX ...)`
4. `cmake/Engine/AssetAndToolTargets.cmake` 에 `sw_addRepoPythonTarget` 한 덩이 +
   `add_test` + `set_tests_properties` 한 덩이

2~4 는 1 에서 **기계적으로 유도되는 것**이다. 그래서 유도하게 했다 — 이 저장소가 이미
`Constants.py` → `ConfigVars.cmake` 로 하고 있는 방식 그대로, **파이썬이 알고 CMake 는 결과를 읽는다.**

- `Scripts/lint/LintCatalog.py` — `gate/` · `selftest/` 를 훑어 "무엇이 있고 어떻게 돌리는가" 를 만든다.
- `Scripts/setup/GenerateLintTargets.py` — 그것을 `generated/sw/config/LintTargets.cmake` 로 찍는다.
  CMake 는 configure 때 그 파일을 `include()` 하고 함수 둘을 부를 뿐이다.
- 린트마다 다른 값은 **린트 자신이 든다**: 게이트는 클래스 속성(`buildComment` · `timeoutSeconds` ·
  `listCtestArgument`), 셀프테스트 둘은 모듈 상수(`kLintBuildComment` · `kLintTimeoutSeconds`).
  `CheckSourceGlob` 만 쓰는 `--build ${CMAKE_BINARY_DIR} --active-game ${SW_ACTIVE_GAME}` 도
  그 게이트의 클래스에 적혀 있다 — 생성물이 CMake 파일이라 참조가 거기서 풀린다.
- `CheckLintsAreAlive` 도 자기 폴더 훑기를 버리고 같은 카탈로그를 쓴다. **훑기가 하나다.**

| | 전 | 후 |
| --- | ---: | ---: |
| `AssetAndToolTargets.cmake` | 228줄 | **104줄** |
| `SW_SCRIPT_LINT_*` 상수 | 12 | **0** |
| 새 게이트를 넣을 때 고치는 파일 | 4 | **1** |

> **덤으로 타임아웃 하나를 고쳤다.** `CheckCodeConventions` 는 이 PC 에서 **11.6초**가 걸리는데
> TIMEOUT 이 **15초**였다. 조금만 느린 PC 나 CI 에서 그냥 터지는 값이다. 값을 린트 옆으로 옮기면서
> 60 으로 올렸다 — CTest 등록 차이는 이것 하나뿐이고, 나머지 열은 이름·명령·라벨·타임아웃이
> `--show-only=json-v1` 비교로 **완전히 동일**하다.

**하지 않기로 한 것 — `FindWindowsTools.cmake` 의 lib.exe/mt.exe 탐색.** 디스크를 뒤지는 일이라
파이썬 쪽(`HostTools.py`)과 겹쳐 보였지만, 우선순위 1번이 **"지금 CMake 가 고른 컴파일러 옆"**
(`CMAKE_CXX_COMPILER`)이다. 그건 `project()` 가 정하는 값이라 파이썬이 알 수 없다. 나머지 폴백만
옮기면 탐색 하나가 두 언어에 걸쳐 찢어진다 — 지금보다 나쁘다.

**다음 자리(하나 열어 둔다)**: `toolchain_config.json` 을 **CMake 가 다섯 군데서 각자 파싱한다**
(`DetectToolchain` 7키 · `FindLlvmBin` · `FindWindowsTools` · `VcpkgPortsToolchain` ·
`Tools/ReflectionParser/CMakeLists.txt`). 게다가 키 이름을 앞의 셋은 `SW_KEY_*` 상수로, 뒤의 둘은
**리터럴 문자열**로 적는다. 파이썬이 그 JSON 을 쓰는 쪽이니, `set()` 줄로 한 번 찍어 주고 CMake 는
`include()` 만 하면 `string(JSON ... GET)` + 오류 처리 블록 일곱 벌이 사라진다.

**확인**:

- `ctest --show-only=json-v1` 로 **등록된 테스트 17개의 이름·명령줄·라벨·타임아웃을 전부 비교** —
  의도한 `CheckCodeConventions` 타임아웃 하나만 다르고 나머지는 동일.
- `gate/` 에 빈 파일을 놓으면 **양쪽이 다 문다**: `CheckLintsAreAlive` 는 "게이트 클래스가 없습니다",
  `GenerateLintTargets` 는 "`kLintBuildComment` 가 없습니다" 로 configure 를 세운다(트레이스백이
  아니라 한 줄로 말한다 — 여기서 죽으면 빌드가 통째로 서기 때문이다).
- `cmake --build --target CheckRenderOwnership` · `CheckSourceGlob` 정상 (후자는 `--build`·
  `--active-game` 이 제대로 흘러가는지까지 본다).
- 린트 ctest **11/11** · `Ninja-Debug` ctest **17/17** · Release·Shipping configure 정상.

### 2026-09-14 (픽서 셋이 같은 스무 줄을 글자 그대로 복사하고 있었고, 훑는 둘은 풀을 각자 열고 있었다)

게이트를 정리한 바로 다음 자리다. `Scripts/` 를 한 번 더 훑어 **같은 종류의 복사본** 둘을 걷었다.

**1) 픽서 = `LintFixer` 상속 클래스 (`Scripts/lint/LintFixer.py`).**

`fixer/` 는 게이트보다 노골적이었다 — **대상 파일을 고르는 스무 줄이 세 스크립트에 글자 그대로**
있었다(`FormatBranchBraces` · `FormatForwardDeclarations` · `RunClangFormat`):

```python
    if args.files:   ... elif args.all:   ... else: getModifiedCppFiles(root) → 없으면 전체
```

거기에 `--all`/`--check` argparse 블록, `flatMapConcurrent` 배치 함수, "위반이 있고 `--check` 면 1"
종료 규칙까지 같았다. **다른 것은 설명 문자열과 로그 태그뿐이었다.**

픽서가 쓰는 것은 `listPass` 하나다 — `(텍스트) -> (새 텍스트, 바뀌었는가)` 변환과, 그 변환이 잡은 것을
`--check` 모드와 수정 모드에서 각각 뭐라고 부를지. 파일 읽기·쓰기는 기반이 맡는다. 파일 고르기는
`addFileArguments`/`selectTargetFiles` 로 따로 내놓았다 — 픽서가 아닌 `RunClangFormat` 도 같은 규칙을
쓰기 때문이다. `PreCommitLint`·`FormatModified` 가 부르는 `processFile`·`formatXxxBatch` 철자는 그대로
남는다(싱글턴 인스턴스의 메서드 별칭).

> **줄끝 정책이 둘이었다.** `FormatBranchBraces` 는 `newline=""` 로 읽고 쓰고(원본 보존),
> `FormatForwardDeclarations` 는 `read_text`/`write_text` 로 변환해 쓰고 있었다. 보존하는 쪽으로 통일했다 —
> 포맷터가 CRLF 를 LF 로 바꾸면 그 파일 전체가 바뀐 것으로 보이고 진짜 변경이 묻힌다. 헤더 400개로
> `\r` 이 붙은 텍스트에서도 정렬 결과가 같다는 것을 먼저 확인하고 바꿨다.

**2) `RunClangTidy` · `RunBuildWarnings` → `TranslationUnitSweep` (`Scripts/common/TranslationUnits.py`).**

둘 다 컴파일 DB 를 읽고 · TU 를 거르고(`/generated/`·`*.gen.cpp`·`--filter`) · 풀을 열어 TU 마다 자식
프로세스를 돌리고 · 진행률을 찍고 · 출력을 이어 붙였다. 도구만 다를 뿐 앞뒤가 같았다.

**`common.Parallel` 이 "동시 처리 한 자리" 인데 이 둘만 빠져 있던 이유가 진행률이었다** — `mapConcurrent`
에 진행률 콜백이 없어서 각자 `ThreadPoolExecutor` 를 열고 있었다. 콜백을 더하고(워커 수 정책은 그대로),
DB 읽기와 TU 거르기는 `TranslationUnitSweep` 이 맡는다. 두 스크립트에 남는 것은 **자기 도구를 어떻게
부르는가**와 **결과를 어떻게 묶어 읽히는가** 뿐이다.

> **거르는 규칙이 둘이어서 `RunClangTidy` 가 CMake PCH 더미 13개를 분석하고 있었다.**
> `RunBuildWarnings` 는 `cmake_pch.cxx` 를 명시적으로 뺐는데 `RunClangTidy` 는 `/Source/` 만 요구했고,
> 빌드 트리의 `build/*/Source/**/CMakeFiles/*.dir/cmake_pch.cxx` 가 그 조건을 통과한다. 고유 TU 397 →
> **384**. 생성된 스텁에서 나오는 지적은 전부 잡음이다.

**하지 않기로 한 것 — `CheckCodeConventions` 의 매개변수·지역변수 규칙.** 백로그가 "다음 단계" 로 적어 둔
자리지만, 그 둘은 **파싱 한 번 + `if/elif` 사슬**이라 카테고리별로 쪼개면 독립 `if` 가 되어 동작이 바뀐다
(`vector<int>* p` 는 지금 포인터 규칙만 걸리는데, 쪼개면 컨테이너 규칙도 같이 걸린다). 쪼개는 방식을
따로 설계해야 하는 자리이지 옮겨 담는 자리가 아니다. `setup/` 의 부트스트랩들도 봤지만 LLVM(타르볼 +
가지치기)과 vcpkg(git clone + 커밋 고정)는 공통부가 이미 `ToolSpec`/`findToolRoot` 로 빠져 있어 남은 절반은
억지로 묶어야 한다.

**다음 자리(하나 열어 둔다)**: **픽서에는 음성 테스트가 없다.** `CheckLintsAreAlive` 는 `gate/` 만 본다.
이제 픽서도 모양이 하나이고 `--check` 에서 0 이 아닌 값을 주므로, 같은 장치가 `fixer/` 까지 덮을 수 있다.

**확인** (동작이 바뀌지 않았다는 것을 바이트로 확인했다):

- 픽서 둘에 **일부러 어긴 조각 일곱**(한 줄 if · else 사슬 · 여러 줄이 섞인 사슬 · 두 문장 case ·
  for 루프 · 뒤섞인 전방 선언 · 이미 정렬된 전방 선언)을 주고, 리팩터 전/후의 **`--check` 메시지와 고친
  파일 바이트**를 JSON 으로 떠서 비교 — 완전히 동일. 바뀌면 안 되는 조각이 안 바뀌는 것도 같이 고정된다.
- `RunBuildWarnings --preset Ninja-Debug --filter Renderer` 출력이 옛 구현과 한 글자도 다르지 않음.
  TU 선택도 510/510, `--filter Graphics` 107/107 로 동일.
- `RunClangTidy --filter EngineLoop` 정상 동작(지적 3건).
- 린트 ctest **11/11** · `Ninja-Debug` ctest **17/17** · `py -3 -m Scripts format`(969 파일) 후
  작업 트리 변화 없음 · `py -3 -m Scripts lint` 통과.

### 2026-09-14 (게이트 아홉이 같은 껍데기를 각자 적고 있었다 — 그리고 팩 계약을 두 번 파싱하고 있었다)

`Scripts/` 에서 **클래스가 구조를 줄이는 자리**만 골라 옮겼다. 셋을 했고, 넷째는 하지 않기로 했다.

**1) 게이트 아홉 = `LintGate` 상속 클래스 아홉 (`Scripts/lint/LintGate.py`).**

게이트가 하는 일은 제각각이지만 껍데기는 늘 같았다: UTF-8 을 켜고 · `--root` 를 받고 · 루트를 정하고 ·
위반을 찍고 · 있으면 1 을 준다. 아홉 파일이 그 열댓 줄을 각자 적고 있었고 **그래서 각자 달랐다**:

- `--root` 기본값이 두 종류였다. `CheckRenderOwnership` · `CheckTestSuites` 는 `parents[2]` — 그건
  저장소 루트가 아니라 **`Scripts/`** 다. 문서에 적힌 대로 `py -3 Scripts/lint/gate/CheckRenderOwnership.py`
  를 치면 `Scripts/` 를 훑고 "헤더가 없습니다" 로 실패했다. CMake·PreCommitLint 가 늘 `--root` 를 줘서
  아무도 몰랐다. 이제 둘 다 인자 없이 돈다.
- 넷은 `main()` 이 `parse_args()` 를 인자 없이 불러 **프로그램에서 부를 수 없었다**
  (`CheckEngineLayers` · `CheckDataFileReferences` · `CheckResourceCasing` · `CheckSourceGlob`).
- 위반 줄이 어디는 stdout · 어디는 stderr, 머리말은 `  - ` 와 `  ` 가 섞여 있었다.

게이트가 쓰는 것은 **`scan()` 하나**이고 `GateResult(listViolation, listNote, summary)` 를 돌려준다.
"검사할 수 없었다" 는 `GateError` 로 던져 **2** 로 끝난다 — "위반 0건"(0)과 섞이지 않는다.
`CheckCodeConventions` 만 보고서 모양이 달라 `report()` 를 재정의한다(카테고리 묶음 · `--json`).

**증거도 클래스가 든다.** `kSelfTestCases` 모듈 변수가 `selfTestCases` 클래스 속성이 됐고,
`CheckLintsAreAlive` 는 `findGateClass(module)` 로 그것을 읽는다. `gate/` 에 게이트가 아닌 파일을
놓으면 그 자리에서 실패한다 — **자리가 규칙이고, 이제 자리에 들어갈 모양도 하나다.**

> **덤으로 죽은 배선 하나.** `CheckEngineLayers.processFile` 은 `del strict` 을 하고 빈 목록을
> 돌려주고 있었다 — 세 번째 반환값도, `main` 의 경고 블록도 **한 번도 채워진 적이 없다.**
> 반환 타입을 `list[str]` 하나로 줄였다(`--strict` 는 옛 호출부를 위해 계속 받는다).

**2) 팩 바이너리 계약을 읽는 일은 한 곳 — `Scripts/common/PackFormat.py` 의 `PackFormatSpec`.**

`Config/Engine/PackFormat.json` 이 SSOT 라는 것은 그대로였는데, **소비자 둘이 그 파일을 각자
파싱하고 있었다**: 타입 표가 둘(`_kStructTypeCodes`+`_kStructTypeSizes` / `kScalarTypes`), 배열
표기(`uint8[2]`) 해석이 둘, "필드 합계 = 선언 크기" 검증도 둘. 한쪽만 고치면 조용히 어긋나는 모양이고,
이 저장소는 이미 그 사고로 **offset 8 부터 어긋난 헤더**와 "마운트는 되는데 파일이 0개" 인 팩을
만든 적이 있다. 쿠커 쪽은 읽은 결과를 딕셔너리에 되쑤셔 넣고(`spec["_headerLayout"]`) 거기서 뽑은
모듈 상수 **열둘**을 파일 앞머리에 늘어놓고 있었다.

이제 계약은 객체다. `PackStruct.pack()` 은 **필드 이름으로만** 값을 받는다 — 헤더는 필드가 열넷이고
그중 다섯이 같은 `uint64` 라, 자리로 넘기면 두 값을 맞바꿔도 조용히 통과한다. 빠뜨리거나 계약에 없는
이름을 주면 거기서 멈춘다.

**3) `cookPack` 150줄의 상태 뭉치 → `PackWriter`.**

한 함수가 오프셋 커서 · TOC 레코드 · 페이로드 블롭 · 스트링 풀 넷을 동시에 굴렸다. 특히 스트링 풀은
**앞선 별도 루프**에서 쌓은 뒤 인덱스로 다시 맞췄는데, 두 루프가 같은 순서로 돈다는 전제가 코드 어디에도
없었다. 이제 순서가 메서드 이름이다 — `addFile`/`addBytes` 로 담고 `writeTo` 로 굽는다. 풀도 TOC 와
같은 루프에서 쌓이므로 맞출 인덱스가 없다. `cookPack` 은 **무엇을 담을지** 고르는 일만 한다.

**4) 하지 않기로 한 것 — `FormatBranchBraces`.** `listMasked` 를 헬퍼 다섯에 넘기고 있어 후보로 보였지만,
두 패스가 **서로 다른 텍스트**(앞 패스의 결과)를 훑으므로 스캐너 객체를 공유해도 마스킹을 다시 해야 한다.
줄어드는 것이 인자 몇 개뿐이라 멀쩡한 픽서를 흔들 값이 아니다.

**확인** (동작이 바뀌지 않았다는 것을 바이트로 확인했다):

- `GeneratePackFormat.py` 가 만드는 `PackFormat.gen.h` — 이전 산출물과 **바이트 단위로 동일**.
  CMake reconfigure 로 다시 생성해서도 확인.
- 팩 세 개(engine · common · game_empty)를 **옛 쿠커와 새 쿠커로 각각 구워 md5 비교** — 동일.
  스트링 풀이 들어가는 `--include-debug-names` 경로도 따로 구워 동일(이쪽이 인덱스 맞추기를 바꾼 자리다).
- `CheckLintsAreAlive` 양방향 확인: `gate/` 에 빈 파일을 놓으면 "게이트 클래스가 없습니다" 로 실패,
  `CheckEngineLayers` 가 늘 빈 목록을 돌려주게 만들면 "이 검사는 죽었습니다" 로 실패. 되돌리면 통과.
- 린트 ctest **11/11** · `Ninja-Debug` ctest **17/17** · nogpu **5/5** · `py -3 -m Scripts lint` 통과 ·
  `--prefabs-only/--scenes-only` 쿠킹 정상.

### 2026-09-14 (레이스를 잡으려고 만든 테스트가 레이스가 있던 백엔드를 건너뛰고 있었다)

앞 커밋 둘이 DX11 병렬 기록의 레이스를 고쳤다. 그러면 남는 질문은 하나다 — **왜 못 잡았나.**

**전제가 틀린 채로 표 두 곳에 박혀 있었다.** 같은 사실의 출처가 둘이다:

| 어디 | 값 |
| --- | --- |
| `RHICapabilities.h` 정적 표 | DX11 `_bParallelCommandRecording = SW_FALSE` |
| `D3D11RHIDevice::getCapabilities` | `D3D11_FEATURE_THREADING` 조회 결과로 덮어씀 → 이 PC 에서 **TRUE** |

정적 표의 `FALSE` 는 "드라이버를 모를 때의 보수적 기본값" 인데 **"DX11 은 병렬로 기록하지 않는다"** 로 읽혔다.
`RenderGraphExecuteParallelRunsOnRealDevice` 는 그 오독을 주석에 적고("DX12만 …=1") DX12 를 하드코딩했고,
레이스를 잡으려고 만든 `FrameRendererDeferredPipelineParallelWaves` 는 목록이 `{DX12, Vulkan}` 인 데다
**첫 성공에서 break** 였다. 그래서 DX11 병렬 경로에는 테스트가 **한 번도 닿은 적이 없었다.**

- 두 테스트 모두 **네 백엔드를 돌면서 `device->getCapabilities()` 가 런타임에 참이라고 답하는 곳마다**
  실행한다. 백엔드 이름으로 고르지 않는다. 정적 표에도 "디바이스가 있으면 이 값을 믿지 말 것" 을 적었다.

**그런데 그것만으로는 여전히 못 잡았다 — 절반이 더 있었다.**
되돌려 놓고 재 보니 두 테스트가 **5/5 통과**했다. "돌았다"(`execute()` 가 true, GpuScene 이 비지 않음)만
보고 **결과를 보지 않았기** 때문이다. 기록 레이스는 예외도 실패 코드도 내지 않고 **픽셀만** 바꾼다.

- `FrameRendererDeferredPipelineParallelWaves` 가 이제 **직렬 한 판 · 병렬 세 판**을 돌려 `LitColor` 평균을
  대조한다. 허용 오차 1% — 실제 레이스는 38% 를 흔들었다. 되돌린 코드에서 **8번에 7번** 잡고, 고친
  코드에서는 **10/10 통과**(오탐 0)다.
- 반복이 기대만큼 이롭진 않다: 레이스가 **프로세스마다 굳는** 경향이라 한 판이 5번에 4번, 세 판이 8번에
  7번이었다. 재도입을 확실히 잡아 주는 것은 CI 가 커밋마다 돌린다는 사실이다. 그래도 **코드가 옳으면
  언제나 통과한다** — 직렬 == 병렬은 결정적이다.

**같이 정리한 것**

- **DX11 만 `assertRegistryMutableNow` 가 0건이었다** (DX12 10 · Vulkan 7 · DX11 0 · GL 0). GL 은 직렬이라
  무의미하지만 DX11 은 병렬로 기록한다. 지금은 `_bindlessMutex` 덕에 **살아 있는 버그는 아니지만**,
  계약이 가장 필요한 백엔드에서만 검사되지 않고 있었다 — 등록/해제 7곳에 가드를 넣었다.
- **`_bComputeRootConstants` 는 죽은 capability** 였다. 선언 + 네 백엔드가 전부 `SW_TRUE` 로 설정,
  읽는 곳 **0건**. 지웠다.
- **GL 의 바인딩 캐시가 두 곳에 흩어져 있었다** — `OpenGLRecordingState` 와 디바이스 멤버 셋
  (`_boundGraphicsPso` · `_boundComputePso` · `_boundTextureUnitMask`). 구조체로 모았다(동작 변화 없음).

> **처음에 "GL 도 불변식을 위반한다" 고 적었는데 틀렸다.** GL 은 커맨드 버퍼가 없는 상태 머신이고
> `OpenGLRHICommandList` 는 호출을 **즉시** GL API 로 흘린다 — 실제 상태가 하나뿐이므로 캐시도 하나여야
> 맞다. 리스트마다 두면 오히려 진짜 GL 상태와 어긋난다. 기준은 **"리스트마다 하나" 가 아니라 "기록
> 스트림마다 하나"** 다. 두 구조체 주석이 이제 서로를 가리키며 그 대비를 적어 둔다.
>
> 같은 이유로 **"CommandList 는 RecordingState 를 소유해야 한다" 는 린트 게이트는 만들지 않았다** —
> GL 에는 거짓이라 "넷 중 셋은 반드시, 하나는 반드시 아님" 을 인코딩하게 된다. 구조를 흉내 내는 린트보다
> **동작을 재는 테스트**(직렬 == 병렬)가 이 계약의 정본이다.

**확인**: `EngineTest` 460/460 을 3연속 · `Ninja-Debug` ctest **17/17** · 린트 11/11 · 네 백엔드 에디터
실기동 종료 코드 0 · `[Error]` 0건 · Shipping 빌드와 nogpu 5/5 · `RunBuildWarnings.py` 전수 경고 0건.

### 2026-09-14 (리스트마다 Deferred Context 를 줬는데 상태 캐시는 하나였다 — DX11 병렬 기록의 레이스 둘)

`RenderPassGpuTest.AmbientOcclusionReachesBloom` 이 **세 번에 두 번꼴로** 깨졌다. 증상이 세 가지라
한동안 다른 문제로 보였다: 크래시(exit 2173)이거나, `Bloom` 이 하얗게 타서 "AO 를 꺼도 결과가 같다"
는 실패이거나, 그냥 통과였다. **셋 다 DX11 병렬 패스 기록의 같은 뿌리**였다.

**먼저 셌다 — 그리고 AO 는 범인이 아니었다.** 테스트에 계측을 넣어 첨부별 평균을 재 보니
`GBufferAlbedo`(151.485748) · `GBufferNormal`(127.0) · `AOColor`(254.619278, dark 14328)는 **실행마다
완벽히 같았고**, 오직 `LitColor` 만 **183.984192 와 253.789917 두 값 사이를 오갔다.** Bloom 은 그것을
그대로 물려받았을 뿐이다. AO 가 꺼진 쪽이 더 밝게 나온 것도 그래서다 — 두 단계가 서로 다른 LitColor 를
봤다. `c * ao + bright` 는 `ao ≤ 1` 이면 **수학적으로** AO 를 켠 쪽이 더 어두워야 한다. 그 모순이
"입력이 흔들린다" 는 신호였다.

**레이스 1 — 상수버퍼 갱신이 애초에 틀린 스트림으로 나가고 있었다.**
`D3D11RHIResource::updateConstantBuffer` 는 `_deviceContext`(**즉시** 컨텍스트)에 `Map(WRITE_DISCARD)`
한다. 그런데 이 함수는 `ShaderBindingBinder` 가 **드로우마다** 부르므로, 웨이브를 병렬로 기록하면
태스크 워커 여럿이 같은 즉시 컨텍스트를 동시에 Map 한다. `ID3D11DeviceContext` 는 스레드 안전하지
않다 — 안전한 것은 `ID3D11Device` 뿐이다.

**왜 즉시 컨텍스트였나.** 갱신 API 가 `IRHIResource` 에 있기 때문이다 — 커맨드 스트림을 모르는
디바이스 레벨 인터페이스이고, `IRHICommandList`/`IRHICommandContext` 에는 버퍼 갱신 명령이 아예 없다.
나머지 셋은 그래도 된다: DX12 는 영구 매핑된 업로드 힙에 memcpy, Vulkan 은 host-visible 메모리에
map/memcpy, GL 은 `glBufferSubData` 다 — **컨텍스트가 필요 없다.** D3D11 만 영구 매핑이 없어 어떤
컨텍스트로든 `Map` 을 해야 하는데, `IRHIResource` 가 닿을 수 있는 것이 즉시 컨텍스트뿐이었다.

**고친 방식 — 스레드가 자기 Deferred Context 를 든다.** 인터페이스에 커맨드 리스트를 끼우면 RHI 모듈
ABI(`RHIModuleAbi.h`)가 바뀌어 백엔드 넷을 함께 리빌드해야 한다. 그럴 필요가 없다:
`D3D11RHICommandList::beginCommandList`/`endCommandList` 가 **기록 워커 스레드에서** 패스 전체를 감싸므로
(`RenderGraphInternal::recordRenderPassTask`), 그 구간에 스레드별 기록 컨텍스트를 걸어 두면
`updateConstantBuffer` 가 자기 스트림을 찾아간다(`D3D11RHIDevice::bindRecordingContext`). Deferred Context
에 `Map(WRITE_DISCARD)` 하는 것이 **D3D11 이 문서화한 동적 버퍼 갱신 방식**이고, 런타임이 커맨드 리스트
단위로 버퍼를 버저닝하므로 그 리스트의 드로우가 기록 시점의 값을 본다. 컨텍스트가 스레드마다 따로라
**락이 필요 없다.** 실측으로 드로우 경로는 전부 deferred 로 가고 즉시 폴백은 한 번도 타지 않는다.

**즉시 컨텍스트를 정말 쓰는 자리에는 자물쇠를 남겼다** — `_immediateContextMutex`. 구조버퍼 갱신(프레임
셋업) · 텍스처 업로드 · 되읽기 · `ExecuteCommandList` · `Flush` · `RSSetViewports` 가 그 뒤에 있다.
드로우 경로가 빠졌으므로 이 자물쇠는 이제 경합하지 않는다.

**레이스 2 — 그리고 이것이 그림을 바꾸던 진짜 원인이다.**
`D3D11RecordingState` 의 주석은 이미 정답을 적어 두고 있었다: *"리스트마다 자기 Deferred Context 를
소유하는데 이 캐시는 디바이스 전역이면 서로의 바인딩 캐시를 덮어쓴다."* 그런데 **배선이 없었다** —
`D3D11RHICommandList` 가 인자 둘짜리 컨텍스트 생성자를 써서 `_pState` 가 전부 `device._recordingState`
하나를 가리켰다. 드로우 시점에 읽는 것이 `_activeGraphicsPso`(입력 레이아웃)와
`_boundMeshVb/Stride/Offset`(정점 버퍼)이라, **한 패스의 드로우가 다른 패스의 PSO·정점 버퍼로 나갔다.**
디퍼드 파이프라인은 `Shadow` 와 `GBuffer` 가 같은 웨이브라, 진 쪽이 그림자 맵을 엉뚱하게 그리고 →
디퍼드 조명이 그림자 없는 값(253.79)을 냈다. 상태를 **리스트 멤버**로 옮기고 인자 셋짜리 생성자를 썼다.

**루트 상수 에뮬레이션도 같은 병이었다.** DX11 에는 루트 상수가 없어 작은 CB(b2)로 흉내 내는데,
그 버퍼와 CPU 그림자 배열이 **디바이스 전역**이었다. `setIdentityWorld` 가 월드 행렬을 그리로 싣는다 —
즉 병렬 기록에서 월드 행렬이 서로 덮였다. 둘 다 `D3D11RecordingState` 로 옮겼다.

> **캐시가 여럿이 되면 무효화도 여럿을 돌아야 한다.** 버퍼·PSO 가 사라질 때 전역 캐시 한 곳만 지우던
> 코드를 `forgetBufferInRecordingStates` / `forgetPipelineStateInRecordingStates` 로 바꿔 살아 있는
> 리스트를 전부 훑게 했다(`_listLiveCmdList` 는 이미 있었다).

**확인**: `LitColor` 가 **183.984192 로 고정**(12연속 실행, 전부 통과) · `RenderPassGpuTest` 24/24 를
**10연속** · **`EngineTest` 460/460 을 3연속** · `Ninja-Debug` ctest **17/17**(GPU 포함 전체가 처음으로
통과한다) · 네 백엔드 에디터 실기동 종료 코드 0 · `[Error]` 0건 · Shipping 빌드와 nogpu 5/5 ·
`RunBuildWarnings.py` 전수(Debug · Release · Shipping) 경고 0건.

> **`-gv_gpuCulling=0` · `-gv_drawMerge=0` 로는 안 사라진다** — 실제로 먼저 그걸 의심해 껐다가 그대로
> 재현되는 것을 보고 방향을 돌렸다. 병렬 기록 자체를 끄면(`_bParallelCommandRecording = 0`) 사라졌고,
> 그것이 범위를 DX11 기록 경로로 좁혀 준 실험이다.
>
> **Bash 로 돌리면 이 테스트가 "멈춘" 것처럼 보인다.** 창을 만드는 테스트라 그 환경에서는 타임아웃으로
> 끝나 로그가 `[ RUN ]` 에서 잘린다. PowerShell `Start-Process` 로 돌리면 2.7초에 정상적으로 실패/통과가
> 찍힌다 — 진단은 그쪽으로 해야 한다. 표준출력이 버퍼링돼 크래시 때 **마지막 로그가 통째로 날아가는**
> 것도 같이 기억해 둘 것(로그 끝 줄이 크래시 지점이 아니다).

### 2026-09-14 (한 개념에 이름 둘이었다 — 함수 이름 어휘를 정하고 게이트로 못박았다)

`queryAABB` 와 `queryAabb` 가 **같은 트리에** 있었다. `alloc*` 과 `allocate*`, `setup*` 과 `initialize*`,
`check*` 인 술어와 `is*` 인 술어도 그랬다. 규칙이 없어서가 아니라 **아무도 세지 않아서**다 — 다음 사람은
방금 본 쪽을 따라 쓰고, 그렇게 갈라진다.

**먼저 셌다.** 헤더에서 함수 선언 6,554건(고유 이름 3,659)을 뽑아 선두 동사로 묶고, 두문자어 표기·동사
동의어·술어 형태·약어를 교차 대조했다. 눈으로 고른 목록이 아니다 — 그래서 `getHeapVTable` 이나
`hasExtensionVal` 처럼 아무도 기억하지 못하던 것까지 걸렸다.

**이름 84개를 고쳤다(933곳).** 규칙은 넷이고 AGENTS.md · `docs/04_CodingGuidelines.md` 에 적혀 있다.

| 규칙 | 예 |
| --- | --- |
| 두문자어는 camelCase 낱말 하나 | `queryAABB`→`queryAabb` · `getRHI`→`getRhi` · `bindComputeUAV`→`bindComputeUav` · `exportGameAPI`→`exportGameApi` · `updateUI`→`updateUi` · `isValidUTF8`→`isValidUtf8` · `parseUInt64`→`parseUint64` |
| 한 개념에 동사 하나 | `alloc*`→`allocate*` · `setupLocalization`→`initialize` · `cleanup`→`shutdown` · `Memory::allocMemory/freeMemory`→`allocate/free` |
| 술어는 질문처럼 | `checkForCycle`→`hasCycle` · `checkCollision`→`overlapsBox` · `checkCommandPattern`→`wasCommandPatternTriggered` · `inBounds`→`isInBounds` · `ignoreCaseKeys`→`ignoresCaseKeys` · void 로 단언하던 `checkRegistryMutableNow`→`assertRegistryMutableNow` |
| `on*` 은 알림, 등록은 `register*` | `GameStrings::onLanguageChanged`(핸들을 돌려주고 있었다)→`registerLanguageChangedCallback` |

약어는 **이 저장소의 타입 이름이 줄여 쓸 때만** 남겼다. `XmlNode::attr()` 은 옆이 `XmlAttribute` 라서
틀렸고(`attribute()`), `TagQueryExpr::…Expr` 과 `ShaderEngineCbMember` 의 `…Cb…` 는 타입이 같은 약어를
들고 있으므로 그대로 둔다. `Cb`·`Fbo` 를 풀어 쓰자는 제안은 이 기준에서 기각이다.

**곁가지로 죽은 코드 하나.** `CommandLineManager::startup()` 은 선언만 있고 정의도 호출도 없는
`initialize()` 의 별칭이었다 — 지웠다.

**그리고 목록 대신 자리로 못박았다 — `Scripts/lint/gate/CheckFunctionVocabulary.py`.**
헤더 479개를 훑어 (1) 대문자 달리기, (2) 금지 동사, (3) `check*` 술어를 잡는다. **호출부는 보지 않는다** —
보면 우리 것이 아닌 이름(`vkGetPhysicalDeviceSurfaceCapabilitiesKHR`)을 잡는다. 대문자 규칙은 "셋 이상은
어디서든, 둘은 이름 끝에서" 다: `bindVector2DCallback`(`D`+`Callback`)과 `isVSyncEnabled`(`V`+`Sync`)를
오탐하지 않으면서 `queryAABB`·`updateUI`·`getGLTextureName` 을 전부 잡는 경계다.

> **게이트가 만들자마자 셋을 더 잡았다** — 내가 손으로 훑어 놓친 `Process::cleanup` ·
> `ImGuiEditor::cleanupPartialInitialization` · `TransientAttachmentPool::alloc`. 세는 것은 사람이 아니라
> 기계가 해야 한다는 증거가 그 자리에서 나왔다.

**덤으로 찾은 진짜 버그 하나 — `bake.stamp` 가 CRLF 면 트리 전체가 "낡음" 이었다.**
`ShaderBakeStampTest.FreshnessIsJudgedByContentNotFileTime` 이 실패해서 파고들었더니 내 변경과 무관했다.
베이커는 `'\n'` 으로 쓰지만 이 파일은 저장소가 추적하므로 `core.autocrlf` 가 켜진 윈도우에서 **CRLF 로
체크아웃된다.** `readBakeStamp` 가 `'\n'` 으로만 쪼개서 경로 키 끝에 `'\r'` 이 붙었고, 그러면 모든 조회가
빗나가 `_bHeadersCurrent=false` 가 된다 — **갓 클론한 트리가 통째로 재베이크 대상**이었다. 키에서 `'\r'`
을 떼는 것으로 고쳤다. (`Resource/engine/shaders/bin/*/bake.stamp` 넷 모두 CRLF 다.)

**셰이더 주석에 남은 옛 이름도 같이 고쳤다.** `.hlsl`/`.hlsli` 여섯 파일이 C++ 쪽 `bindComputeUAV` ·
`registerBindlessTextureUAV` 를 바인딩 계약으로 적어 두고 있었다 — 이름이 바뀌면 그 주석은 **없는 함수를
가리킨다.** 고치고 `App.exe --bake-shaders` 로 다시 구웠다. 주석은 코드 생성에 영향이 없어 **바이너리
344개는 전부 바이트가 같고, 바뀐 것은 `bake.stamp` 여덟 개뿐**이다(도메인 둘 × 백엔드 넷).

**트리에 남아 있던 경고 셋도 없앴다 — 둘 다 "복사본이 생긴다" 는 같은 이야기였다.**
`RunBuildWarnings.py` 로 전수 조사했더니(빌드 로그 grep 은 이미 컴파일된 TU 를 못 본다) 딱 셋이었다.

- `AssetHotReload.cpp` 의 `-Wnrvo` — `resolveWatchExtensions` 가 **이름이 다른 지역 변수 둘을 각각
  return** 했다. 그러면 NRVO 가 죽어 한쪽이 반드시 복사된다. `const` 를 떼는 것으로는 안 없어진다
  (실측했다 — 이 경고는 move 가 아니라 **elision** 을 본다). 두 갈래 모두 같은 객체를 채워 돌려주게
  고쳤고, 덤으로 원래 있던 복사 한 번이 move 가 됐다.
- `TestGameObjectMocks.h` 의 `-Wunique-object-duplication` 둘 — 풀 재사용 검증용 카운터
  `s_ctorCount`/`s_dtorCount` 를 헤더에서 `inline` 으로 **정의**하고 있었다. EngineTest 는 Engine.dll 과
  링크하므로 사본이 생길 수 있고, 그러면 생성자가 올린 수를 소멸자가 **다른 사본에서** 내린다 —
  테스트가 조용히 거짓말을 하게 된다. 정의를 `TestGameObjectMocks.cpp` 한 곳으로 옮겼다
  (`ComponentPoolTest` 셋 그대로 통과).

**확인**: `RunBuildWarnings.py` 전수 **경고 0건** — Debug 510 TU · Release 510 TU · Shipping 379 TU.
`Ninja-Debug` 린트 ctest **11/11**(`CheckFunctionVocabulary` 추가) · nogpu **5/5** ·
`CheckLintsAreAlive` 조각 7 → **11**(새 게이트가 넷을 든다) · `Ninja-Release`·`Ninja-Shipping` 빌드
오류·경고 0 과 Shipping nogpu 5/5.

> **경고를 재려면 `Ninja-Release` 를 먼저 빌드해야 한다.** configure 만 해 두면 리플렉션 생성 헤더
> (`FlagOps.gen.h` 등)가 없어서 `-fsyntax-only` 가 **경고가 아니라 오류**를 32건 쏟는다
> (`invalid operands to binary expression ('sw::RHIBufferUsage' and …)`). 코드 문제가 아니다.

> **`RenderPassGpuTest.AmbientOcclusionReachesBloom` 은 이 PC 에서 멈춘다 — 이 작업과 무관하다.**
> `git stash` 로 HEAD 를 그대로 다시 빌드해 같은 자리에서 같은 식으로 멈추는 것을 확인했다.
> GPU 포함 전체 `EngineTest` 를 돌릴 때만 걸리므로 `-L nogpu` 는 영향이 없다.
> → **바로 다음 커밋에서 원인을 찾아 고쳤다** (위 "리스트마다 Deferred Context 를 줬는데 상태 캐시는
> 하나였다"). 멈춘 것처럼 보인 것은 Bash 환경에서 창을 만드는 테스트가 타임아웃으로 끝났기 때문이고,
> 진짜 결함은 DX11 병렬 기록의 레이스 둘이었다.

### 2026-09-14 (게이트가 하나 있었는데 실패할 수가 없었다 — 그리고 린트 폴더를 성격으로 갈랐다)

앞 커밋들이 `CheckCodeConventions` 의 규칙 30종에 음성 테스트를 붙였다. 그런데 **린트 자체는
여덟이고, 음성 테스트를 가진 건 하나뿐**이었다. 나머지 일곱은 죽어도 아무도 모르는 상태였다.

**`CheckLintsAreAlive.py` — 게이트마다 위반을 넣어 보고 실패하는지 본다.**

- 각 게이트가 `kSelfTestCases` 에 "이건 반드시 잡아야 한다" 는 조각을 들고 있다. 장치가 임시 트리에
  그 조각을 써서 **실제 진입점을 프로세스로** 돌리고(import 가 아니다 — 전역 캐시가 오염된다),
  0 이 아닌 종료 코드가 나와야 통과다. 조각 표를 장치에 모으지 않았다: **표가 둘이면 어긋난다.**
- 조각을 만들 수 없는 린트는 `kSelfTestSkipReason` 에 이유를 적는다. `CheckCodeConventions` 는 이미
  자기 음성 테스트가 있고, `CheckSourceGlob` 은 본 검사가 실제 빌드 트리의 `compile_commands.json`
  을 필요로 해 임시 트리로는 성립하지 않는다. **이유 없는 예외는 없다.**

**그리고 이 장치가 만들자마자 하나를 잡았다 — `CheckIncludeOrder` 는 실패할 수 없는 게이트였다.**
인자가 `--root` 뿐이라 `main()` 이 늘 **고치는 모드**로 돌았고, 위반을 찍은 뒤에도 무조건 `0` 을
돌려줬다. CI 에 게이트로 등록돼 있는데 **한 번도 아무것도 막은 적이 없고**, 대신 소스를 조용히
고쳐 놓고 있었다. `--fix` 를 옵트인으로 돌리고 위반이 있으면 `1` 을 반환하게 고쳤다
(고치는 일은 `FormatModified.py` 가 `processFile(...)` 을 직접 불러서 한다 — 그 경로는 그대로다).

**린트 폴더를 성격별로 갈랐다.** 열일곱 개가 한 폴더에 평평하게 있었고, 이름 앞머리(`Check`/`Run`)
가 유일한 단서였는데 그건 **틀린 단서**였다: `CheckCodeConventionsSelfTest` 는 코드가 아니라 린트를
보고, `FormatBranchBraces` 는 `Check` 로 시작하지 않지만 `--check` 로 게이트가 된다.

```
Scripts/lint/
  PreCommitLint.py   넷을 조율하므로 여기 남는다
  gate/              위반이 있으면 실패한다 — 빌드와 커밋을 막는 건 이 폴더뿐이다 (8)
  fixer/             파일을 실제로 고쳐 쓴다 (3)
  report/            찍어 줄 뿐, 언제나 0 으로 끝난다 (3)
  selftest/          코드가 아니라 린트를 본다 (2)
```

각 폴더가 패키지(`__init__.py`)라서 사촌 참조는 `from gate import CheckIncludeOrder` 처럼 **어느
폴더의 것인지 말하게** 된다. 경로 상수는 `Scripts/common/Constants.py` 한 곳이라 CMake·CTest 는
따라온다.

**폴더가 목록을 없앴다.** `CheckLintsAreAlive` 가 들고 있던 게이트 이름 여덟 줄을 지우고
`gate/` 를 훑게 했다. 새 게이트는 파일을 거기 놓는 것으로 끝이고, 놓는 순간 증거를 요구받는다 —
**목록이 아니라 자리가 규칙이다.** 같은 이유로 `CheckCodeConventionsSelfTest` 가 손으로 짓던
`Scripts/lint/CheckCodeConventions.py` 경로도 `CheckCodeConventions.__file__` 로 바꿨다.

**확인한 것** (장치가 진짜 무는지 양쪽으로 확인했다):

- `gate/` 에 빈 `CheckNothingAtAll.py` 를 놓았더니 즉시 "`kSelfTestCases` 가 없습니다" 로 실패.
- `CheckEngineLayers.main()` 이 늘 `0` 을 돌려주게 만들었더니 "위반을 넣었는데 통과했습니다 —
  이 검사는 죽었습니다" 로 실패. 되돌리니 통과.
- lint CTest 8 → **10** (`CheckLintsAreAlive` 추가, 8 게이트 · 조각 7). `Ninja-Debug` ctest
  **16/16**. `PreCommitLint` 7단계 전부 통과. `py -3 -m Scripts format` / `lint` 도 확인.

### 2026-09-14 (규칙 하나 = 클래스 하나 — 상속만 하면 등록되고, 규칙이 자기 증거를 든다)

앞 커밋에서 규칙을 함수 + 데코레이터로 뽑았는데, 그것만으로는 **표가 둘** 이었다: 규칙 목록과
음성 테스트의 조각 표. 새 규칙을 넣으려면 두 곳을 고쳐야 했고, 둘은 언제든 어긋날 수 있었다.

**규칙을 `ConventionRule` 상속 클래스로 바꿨다.**

- **등록이 자동이다.** `__init_subclass__` 가 scope 별 레지스트리에 자기를 넣는다. 목록에 이름을
  더할 필요가 없다 — 이 저장소가 반복해서 배운 것 그대로다: **목록이 아니라 자리가 규칙이다.**
- **규칙이 자기 위반 조각(`badSample`)을 든다.** 음성 테스트가 그것을 읽어 간다. 규칙과 그 증거가
  붙어 있으면 둘이 어긋날 수가 없다 — 조각 표에서 16 항목이 사라졌다.
- 카테고리를 여럿 내는 규칙은 `extraSamples` 로 나머지를 증명한다(멤버 변수 규칙이 여섯을 낸다).

**확인한 것**: 새 규칙 클래스를 **하나만** 파일에 붙이고 등록 코드도 조각 표도 건드리지 않은 채
음성 테스트를 돌렸더니 덮인 카테고리가 30 → 31 로 늘었다. 사용자가 할 일은 클래스 하나 쓰는 것뿐이다.

> **아직 클래스가 아닌 것들.** 매개변수·지역변수 규칙(`checkParameterItemInternal` ·
> `checkLocalVariableItemInternal`)과 생성자 상태 기계, 파일 짝이 필요한 교차 검사 셋은 그대로다.
> 앞의 둘은 한 함수가 카테고리를 여러 개 내는 큰 검사라 쪼개는 방식이 따로 필요하고, 뒤의 셋은
> 줄 하나가 아니라 **파일 짝**을 봐야 해서 `onLine` 계약에 안 맞는다 — `_kWholeScanCases` 가
> 그쪽을 덮는다. 필요해지면 `onFileStart`/`onFileEnd` 를 계약에 더하는 것이 다음 단계다.

**확인**: 음성 테스트 30 카테고리 · 저장소 전수 위반 **0건** · 린트 9종 전부 OK ·
`Ninja-Debug` ctest 15/15

### 2026-09-14 (규칙 열일곱을 496줄 함수에서 꺼냈다 — 레지스트리와 컨텍스트로)

바로 앞에서 음성 테스트를 만든 이유가 이 작업이다. 규칙이 조용히 죽으면 바로 드러나는 상태에서만
손댈 수 있는 자리였다.

**무엇이 문제였나.** `checkFileConventionsInternal` 이 496 줄이었고 그 안에서 규칙 스물둘이
`inBlockComment` · `classStack` · `funcStack` · 중괄호 깊이를 **공유하는 한 번의 줄 스캔** 위에 얹혀
있었다. 규칙 하나를 고치려면 그 전체를 읽어야 했고, 규칙의 정규식은 900 줄 떨어진 파일 앞쪽에 있었다.

**어떻게 했나.** `LineScanContext` 하나만 받아 위반 목록을 돌려주는 함수로 규칙을 떼어냈다.
등록은 데코레이터 둘이다 — `@lineRule`(줄 단위 순수 규칙 11), `@classMemberRule`(클래스 본문 직속
멤버 규칙 6). 스캔 함수는 컨텍스트를 만들고 레지스트리를 돌릴 뿐이다.

| | 전 | 후 |
| --- | ---: | ---: |
| `checkFileConventionsInternal` | 496줄 | **239줄** |
| 독립 규칙 함수 | 0 | **17** |

**남겨 둔 것은 상태 기계다.** 클래스/함수 시그니처 감지, 생성자 초기화 리스트 추적(`ConstructorBraces` ·
`ConstructorOnePerLine` · `ConstructorOrder`), 스코프 판정은 줄 사이에 상태를 들고 간다. 순수 함수로
떼면 그 상태를 인자로 꿰어야 해서 오히려 흩어진다 — 스캔 함수가 계속 소유한다.

> **손으로 옮겨 적지 않았다.** 규칙 본문을 정규식으로 잘라 내 `relPath` → `ctx.relPath` 식으로 기계
> 변환했다. 496 줄을 눈으로 옮기면 오타가 난다. 조각을 가르는 경계는 "빈 줄 뒤에 오는 주석 머리" 인데,
> 처음엔 빈 줄만으로 갈랐다가 `NegatedComparison` 처럼 본문 중간에 빈 줄이 있는 규칙이 두 동강 났다.

**확인**: 음성 테스트 30 카테고리 전부 그대로 잡힘 · 저장소 전수 위반 **0건**(전과 동일) ·
린트 CTest 9/9 · `Ninja-Debug` ctest 15/15

> 파일 줄 수 자체는 1918 → 2050 으로 **늘었다**. 함수 껍데기가 붙어서다. 이 저장소의 방침대로
> ("쪼개기보다 공통부 추출") 총량이 목표가 아니었다 — 규칙 하나를 **혼자 읽고 혼자 고칠 수 있게** 된 것이
> 얻은 것이다.

### 2026-09-14 (린트가 살아 있는지 린트가 본다 — 규칙 30종에 음성 테스트)

`CheckCodeConventions.py` 는 1917 줄로 다른 린트의 다섯 배이고, 규칙 카테고리가 **30종**이다. 그런데
규칙 하나가 정규식 한 글자 때문에 아무것도 못 잡게 되어도 결과는 "위반 0건" — 통과처럼 보인다.
이 저장소는 이미 그런 일을 겪었다(죽은 사본 검사에서 자기 독스트링을 참조로 세어 통과한 적이 있다).

**규칙마다 일부러 어긴 조각을 두고 그것이 잡히는지 본다.** `CheckCodeConventionsSelfTest.py` 가
임시 트리에 조각을 쓰고 린트를 돌려, 그 카테고리가 나오는지 확인한다. 파일 하나로 잡히는 25종은
`--files` 경로로, 짝이나 트리가 필요한 5종(`DuplicateInternalHelper` · `HeaderMemberInitializer` ·
`ConstructorOrder` · `BitfieldBoolean` · `PathCasing`)은 전체 스캔으로 돈다.
**아무 규칙도 건드리면 안 되는 조각**도 하나 둬서 오탐도 같이 막는다.

**새 규칙을 넣으면 조각도 넣어야 한다.** 린트 소스에서 `rule_category=` 를 전부 뽑아 조각이 없는
카테고리를 실패로 보고한다 — 규칙을 늘리는 일과 그것이 살아 있음을 증명하는 일을 묶어 둔 것이다.

> **조각을 쓰면서 규칙 넷을 내가 잘못 알고 있었다는 것이 드러났다.** 이것이 이 장치의 부수 효과다.
> - `Style/LogFormatSpec` 은 `%d` 를 잡지 않는다. 타입세이프 포매터가 **못 읽는 것만** 본다 —
>   동적 폭(`%*d`) · `%a` · `%n`.
> - `Style/HeaderMemberInitializer` 는 헤더 멤버에 기본값이 **없는** 것을 잡는 규칙이 아니다.
>   헤더가 기본값을 주는데 생성자도 초기화하는 것 — **정본이 둘인 상태**를 잡는다.
> - `Naming/RawPointer` 는 `int32* _value{ nullptr };` 를 안 잡는다. 정규식이 `;` 로 끝나는 선언만 본다.
> - `Style/AutoUsage` 는 `auto x = 0;` 을 안 잡는다. 문자열·`true`·`false`·`nullptr` 리터럴만 본다.
>
> 넷 다 규칙의 버그가 아니라 **내 오해**였다. 조각이 없었으면 다음 사람도 같은 오해를 한다.

**함정 하나**: 경로 맵(`_s_exactPathMap`)은 **처음 한 번만** 채워지는 모듈 전역이다. 정상 실행은 루트가
하나라 맞는 설계지만, 조각마다 임시 루트가 다른 이 장치에서는 앞 조각의 맵으로 판정해
`Include/PathCasing` 이 조용히 안 걸렸다. 조각마다 캐시를 비운다.

**회귀 확인**: 규칙 하나의 `rule_category` 를 몰래 바꿔 죽여 보고, 장치가 "조각이 잡히지 않았습니다"
와 "조각이 없는 카테고리" 를 **둘 다** 보고하는 것을 확인했다.

> **레지스트리 리팩터는 아직 안 했다.** 규칙 22종이 `checkFileConventionsInternal`(496줄) 안에서
> `inBlockComment` · `classStack` · `funcStack` · 중괄호 깊이를 **공유하는 한 번의 줄 스캔** 위에 얹혀
> 있다. 독립 블록이 아니라, 쪼개려면 그 파싱 상태를 컨텍스트 객체로 노출해야 한다. 이 음성 테스트가
> 그 작업의 안전망이다 — **먼저 이것부터** 두는 것이 순서였다.

**확인**: 린트 CTest **9/9**(자가 테스트 포함) · 30 카테고리 전부 조각으로 덮임 · 오탐 조각 0건

### 2026-09-14 (CMake 정리 — 아카이버 판단을 한 자리로, 그리고 읽는 사람이 없던 파일 하나)

LTO 를 고치며 내가 늘려 놓은 중복을 걷어내고, 그 김에 구조를 훑었다.

**아카이버를 고르는 판단이 세 곳에 있었다.** `FindWindowsTools.cmake` 와 (하루 살았던)
`FindPosixTools.cmake` 가 각자 `toolchain_config.json` 을 직접 읽어 `llvm_path` 를 뒤졌다 — 정작 그 일을
하는 `sw_findLlvmBin` 이 이미 있었는데. `cmake/Environment/ToolchainBinaries.cmake` 하나로 모았고
`FindPosixTools.cmake` 와 `Config/IpoSupport.cmake` 는 사라졌다(후자는 툴체인 코드가 `Config/` 에 있던 것도
같이 바로잡혔다).

**그 과정에서 설계 오류를 하나 찾았다.** `sw_findLlvmBin` 은 "컴파일러를 어디서 찾을까" 에 답하는 함수라
**시스템 설치를 프로젝트 Tools 보다 먼저** 본다(최초 clone 때 Tools 가 없어도 되도록). 그 우선순위를
아카이버에 그대로 쓰면 어긋난다 — 리눅스에서 실제로 그랬다: 시스템 clang 이 잡혀 `/usr/bin` 을 돌려주고
거기엔 llvm-ar 이 없어 LTO 가 조용히 꺼졌다. **아카이버는 "지금 쓰는 컴파일러 옆" 을 먼저 본다** —
컴파일러가 낸 비트코드를 읽어야 하므로 둘은 같은 LLVM 에서 와야 한다. WSL 에서 통과를 확인했다.

**IPO 를 켜는 곳이 둘이었다.** Shipping 은 `BuildType/Release.cmake` 와 `Engine/BuildLayout.cmake` 가
각자 `check_ipo_supported` 를 부르고 각자 메시지를 찍었다(Shipping 도 빌드 타입이 Release 라 둘 다 걸린다).
판정과 활성화는 BuildLayout 이 소유한다.

**`cmake/Engine/GeneratedConstants.cmake` 는 아무도 include 하지 않았다.** 진짜 상수는 configure 때
`build/<preset>/generated/sw/config/ConfigVars.cmake` 로 나온다. 이 문서가 "생성물을 직접 고치면 다음
configure 가 지운다" 고 적어 둔 것도 **사실이 아니었다** — 지우지도, 다시 만들지도 않았다. 커밋된 채
방치된 사본이라 지웠다(오늘 나도 그것을 쓸데없이 재생성했다).

**`FindLlvmBin.cmake` 만 `Modules/Toolchain/` 에 있었다.** 툴체인 탐색의 나머지(DetectToolchain ·
ToolchainBinaries · FindWindowsTools)는 전부 `Environment/` 다. 옮겼다. `Modules/Toolchain/Vcpkg/*` 는
**vcpkg 에게 건네는 파일**이라 성격이 달라 그대로 뒀다.

> **안 하기로 한 것: RHI 백엔드 넷과 GF 키트 셋의 leaf CMakeLists 를 부모 루프로 접는 것.**
> 겉보기엔 복사본이다(각각 6~7줄, 이름만 다르다). 그런데 그 파일들이 있는 이유는
> `file(GLOB_RECURSE ...)` · `target_include_directories(... CMAKE_CURRENT_SOURCE_DIR)` ·
> `sw_addReflectionStep` 이 **디렉터리 스코프**로 동작하기 때문이다. 접으려면 그 셋 모두에 디렉터리를
> 인자로 꿰어야 하고, 그중 하나가 리플렉션 코드젠이다. 7개 파일을 없애자고 건드릴 자리가 아니다.
> (백엔드 디렉터리엔 `ModuleEntry.cpp` 라는 실제 소스도 산다 — 순수 보일러플레이트가 아니다.)

**툴체인을 바꾼 쪽이 PCH 를 치운다.** 오늘 두 번 걸렸다: LLVM 을 다시 깔면
`lib/clang/<major>/include` 헤더가 바뀌어 기존 빌드 트리의 PCH 가 전부 낡는데, 컴파일러가 내는 말은
`file '...intrin.h' has been modified since the precompiled header was built` 뿐이라 무엇을 해야
하는지 안 알려 준다. 게다가 **`.pch` 만 지우면 ninja 가 다시 만들지 않는다** — 짝인 `cmake_pch.cxx.obj`
까지 지워야 한다(이것도 따로 한 번 걸렸다). `SetupLlvm.py` 가 설치 직후 `build/**` 의 그 둘을 지운다.

**확인**: `Ninja-Debug` ctest **14/14** · `Ninja-Release` nogpu 5/5 · 린트 게이트 8종 0건 ·
세 프리셋 모두 `CMAKE_AR=Tools/LLVM/bin/llvm-lib.exe`, Release·Shipping 은 IPO 켜진 채로
(`-flto` Release 368/591 · Shipping 322/452 TU)

### 2026-09-14 (LTO 를 리눅스에서도 — 증상은 다르고 뿌리는 같았다)

앞 항목(Windows LTO)을 하고 나니 리눅스가 반쪽이었다. 설치 스크립트 허용 목록에 `llvm-ar` 을 넣은 것이
전부였고, **그것을 CMake 에 물리는 코드가 없었다** — 아카이버를 찾아 묶는 로직이 전부
`FindWindowsTools.cmake` 안에 있었기 때문이다.

**리눅스도 같은 병을 앓고 있었다 (WSL Ubuntu 26.04 · clang 21.1.8 실측).**
`check_ipo_supported` 가 `NO` 를 낸다. 다만 오류 문구가 다르다 — Windows 는 `LNK1107`,
리눅스는 `"CMAKE_CXX_COMPILER_AR-NOTFOUND" qc libfoo.a` 로 죽는다. 배포판 clang 패키지가 `clang++`
옆에 `llvm-ar` 을 두지 않아서다(그 시스템엔 `/usr/bin/llvm-ar` 도 `/usr/lib/llvm-*/bin/llvm-ar` 도 없었다).

> **틀린 가설 하나**: "GNU ar 이 비트코드를 못 읽어서" 일 거라고 봤는데 아니었다. binutils 2.46 의
> `ar` 은 `clang -flto=thin` 이 낸 비트코드 .o 를 **문제없이** 묶는다(실측). 리눅스에서 막히는 것은
> 아카이버의 능력이 아니라 **CMake 가 IPO 에 쓰는 변수가 비어 있는 것** 이다.

**고친 것**

- `cmake/Environment/FindPosixTools.cmake` 신설. `sw_bindPosixLlvmArchiver()` 가 고정 LLVM 의
  `llvm-ar` · `llvm-ranlib` 을 `CMAKE_AR` · `CMAKE_<LANG>_COMPILER_AR` · `CMAKE_<LANG>_COMPILER_RANLIB`
  에 묶는다. 고정 툴체인이 없으면 컴파일러 옆을 보고, 그것도 없으면 **아무것도 안 한다** — 시스템 ar 로
  정적 라이브러리는 계속 묶이고 LTO 만 안 켜진다(메시지로 알린다).
- **변수만 고쳐선 안 된다는 것을 여기서도 다시 만났다.** CMake 는 IPO 아카이브 명령을 `project()` 시점의
  `CMAKE_<LANG>_COMPILER_AR` 로 **문자열에 구워 둔다**(`Modules/Compiler/Clang.cmake`). 변수를 나중에
  고쳐도 생성된 규칙은 `"CMAKE_CXX_COMPILER_AR-NOTFOUND" qc ...` 그대로였다. `ARCHIVE_CREATE_IPO` ·
  `ARCHIVE_APPEND_IPO` · `ARCHIVE_FINISH_IPO` 를 다시 쓰고서야 규칙에 실제 경로가 들어갔다.
  (Windows 매크로가 명령 문자열을 다시 쓰는 이유가 이것이었다 — 그 코드의 뜻을 이제 알겠다.)
- `SetupLlvm.py` 의 POSIX 허용 목록에 `llvm-ranlib` 추가, `isMinimalLlvmRoot` 의 POSIX 분기에
  `llvm-ar` 검증 추가 — **이미 설치된 리눅스 트리도 복구되게**.

**검증한 것과 못 한 것 (솔직히)**

- 검증함: 매크로가 `llvm_path` 를 읽어 셋을 묶는 것 · `sw_checkIpoSupport` 가 `TRUE` 를 내는 것 ·
  아카이버가 없을 때 조용히 물러나 시스템 ar 로 계속 가는 것 · **생성된 규칙에 실제 경로가 박히는 것**
  (`"/tmp/.../llvm-ar" qc libmylib.a ...`).
- **검증 못 함: 진짜 `llvm-ar` 로 끝까지 링크하는 것.** 이 PC 의 WSL 에는 `llvm-ar` 이 없고, 저장소가
  캐시해 둔 리눅스 LLVM 아카이브가 **12.8MB 로 잘려 있다**(정상은 수백 MB, `.sha256` 사이드카도 없다).
  GNU ar 을 `llvm-ar` 이름으로 놓고 대신 돌려 배선까지는 확인했지만, 그 스텁은 `qc` 로 만든 비트코드
  아카이브에 색인을 제대로 안 넣어 최종 링크가 `archive has no index` 로 진다. 진짜 llvm-ar 은 `qc`
  에서 심볼 테이블을 함께 쓰므로 이 증상이 안 나야 하지만 **확인한 것이 아니라 추정이다.**
  리눅스에서 실제로 돌리려면 `SetupLlvm.py --install` 로 고정 LLVM 을 받은 뒤 확인해야 한다.

> 잘린 캐시는 손댈 필요가 없다. `ensureCachedDownload` 가 `minSize=50_000_000` 으로 거르므로 다음
> 설치 때 다시 받는다.

> **이 작업 중에 개발 환경을 한 번 망가뜨렸다. 복구 방법을 적어 둔다.**
> WSL 로 검증하다 `wsl.exe ... bash -c "cd /tmp/xxx && cmake -S . -B b ..."` 에서 **`cd` 가 실패했고**,
> `&&` 가 아니라 그대로 이어진 `cmake` 가 **저장소 루트에서 리눅스 설정으로** 돌았다. 그 결과 셋:
> ① 루트에 `b/` 가 생기고, ② `Config/Environment/toolchain_config.json` 의 `msvc_tools_dir` 이 비고
> DXC 경로가 arm64 것으로 덮이고, ③ **`build/vcpkg_installed` 의 `x64-windows` 가 `x64-linux` 로 바뀌었다**
> (Windows 빌드 트리 넷이 이 디렉터리 하나를 공유한다).
>
> 게다가 그 실행을 중단시키자 **stale 락이 세 개** 남아 이후 모든 configure 가 무한 대기했다:
> `build/vcpkg_installed/vcpkg/` · `Tools/vcpkg/buildtrees/` · `Tools/vcpkg/packages/` 의
> `vcpkg-running.lock`. 증상은 `note: waiting to take filesystem lock...` 이 끝없이 찍히는 것이다.
>
> 복구 순서: (1) `vcpkg`·`cmake`·`ninja` 프로세스가 정말 없는지 확인하고 남아 있으면 죽인다,
> (2) `vcpkg-running.lock` 셋을 지운다, (3) `toolchain_config.json` 을 되돌린다
> (`SetupEnvironment.py` 는 MSVC 를 다시 못 찾았다 — 값을 직접 써야 했다), (4) Windows 프리셋을
> configure 해서 vcpkg 가 `x64-windows` 를 다시 깔게 한다(바이너리 캐시 436MB 가 있어 대부분 캐시에서 온다).
>
> **교훈**: `wsl.exe -- bash -c "cd X && ..."` 에서 `cd` 실패는 조용하다. 뒤에 `cmake -B` 같은
> **현재 디렉터리에 쓰는 명령**을 붙이지 말 것. 붙일 거면 `set -e` 를 앞에 두거나 절대 경로로 `-S`/`-B` 를 준다.

### 2026-09-14 (LTO 가 켜져 있다고 되어 있는데 한 TU 에도 안 걸리고 있었다 — 원인 넷)

`foo.lib LNK1107` 을 두 번 미루다 끝까지 따라갔더니 그게 표면이었다. 밑에 있던 것은
**`SW_ENABLE_LTO=ON` 인데 `-flto` 가 어디에도 안 걸리는 상태** 였다(Shipping 452 TU 중 **0 개**, 실측).

**원인이 넷이고, 넷 다 막아야 풀렸다.**

1. **`SetupLlvm.py` 가 `llvm-lib.exe` 를 안 받았다.** 설치본을 파일 일곱 개 허용 목록으로 잘라내는데
   거기 없었다. clang 이 `-flto` 로 내는 .obj 는 LLVM 비트코드라 **MSVC `lib.exe` 는 못 읽는다**(LNK1107).
2. **아카이버 탐색이 저장소가 고정한 LLVM 을 아예 안 봤다.** `$ENV{LLVM_DIR}` · `$ENV{LLVM_ROOT}` 만
   보고 없으면 MSVC `lib.exe` 로 떨어졌다. 정작 컴파일러는 `Tools/LLVM` 것을 쓴다 — 짝이 어긋나 있었다.
3. **캐시를 고쳐도 일반 변수가 그것을 가렸다.** `project()` 의 컴파일러 탐지가 같은 이름의 **일반
   변수** `CMAKE_AR` 을 최상위 스코프에 만들고, 일반 변수는 캐시를 가린다. 그래서 캐시엔 llvm-lib 이
   적혀 있는데 읽는 쪽은 lib.exe 를 보고 있었다. `sw_bindClangClWindowsTools` 가 "project() 뒤에 다시
   묶는다" 는 자기 존재 이유대로 동작하지 못하고 있었던 것이다.
4. **CMake 의 `check_ipo_supported` 는 이 툴체인에서 구조적으로 거짓을 말한다.** 그것은
   `try_compile(... PROJECT ...)` 로 별도 프로젝트를 구성해 재는데, 그 형식은 `CMAKE_AR` 을 하위
   프로젝트로 **넘기지 않는다**. 하위 프로젝트는 아카이버를 스스로 찾아 MSVC lib.exe 를 집고 LNK1107 로
   죽는다 — 그리고 CMake 는 그것을 "이 컴파일러는 IPO 를 지원하지 않는다" 로 보고한다. 컴파일러는
   멀쩡한데. 그래서 clang + llvm-lib 조합은 `cmake/Config/IpoSupport.cmake` 에서 직접 판정한다.

**`SW_ENABLE_LTO` 가 이제 진짜 스위치다.** 예전엔 Shipping 경로만 그 옵션을 봤고 Release 의 전역 IPO 는
스위치가 아예 없었다(옵션 설명과도 어긋났다). 이제 한 스위치가 둘 다 끈다 — 그래서 아래 측정이 가능했다.

**측정 (Release, 각 3회, 중앙값)**

| | LTO OFF | LTO ON |
| --- | ---: | ---: |
| 클린 빌드 | 45초 | **47초** |
| `GT.GpuScene.build` | 127us | **115us** |
| `GT.Frame` | 1121us | 1109us |
| `RT.Frame` | 1308us | 1262us |

결정은 **켠다** 이다. 근거는 프레임 총합이 아니라 `GpuScene.build` 다 — OFF 표본 셋(122·127·138)이
전부 ON 표본 셋(108·115·120)보다 크다. 구간이 겹치지 않는 유일한 항목이고, 하필 게임 스레드의 지배적
CPU 비용이다(이 문서 위쪽 "주광 조회" · "프로파일러가 RT 샘플을 버렸다" 참고). 프레임 총합은 구간이
겹쳐서 주장하지 않는다. 빌드 비용은 +2초(+5%)다.

바이너리 크기도 같이 봤다: `Engine.dll` 3.89MB → 4.47MB(+15%, 인라이닝), `SWGame.dll` -3%,
`GF_TurnBattle.dll` -2%, `App.exe` 변화 없음.

> **덤으로 `foo.lib LNK1107` 이 사라졌다.** 그 소음의 정체가 바로 4번의 탐지였다. 잡음이 아니라
> **빌드를 중간에 끊고 있었다** — 어제 그것 때문에 갱신 안 된 Shipping 바이너리로 테스트를 돌려 한 번
> 속았다. 이제 Release·Shipping 구성 출력에 그 문자열이 0 건이다.

**같이 고친 것 셋**

- **`--test_list` 가 `--test_filter` 를 조용히 무시했다.** 하필 "내 필터가 무엇을 고르나" 를 확인할 때
  쓰는 기능이다. 이제 필터를 적용하고 `(410 selected / 460 total)` 처럼 둘 다 보여준다 — 그 410 은
  실제 CI 실행 수와 같다.
- **`Test/TestFramework/CMakeLists.txt` 가 저장소에서 마지막으로 남은 손으로 적는 소스 목록이었다.**
  GLOB 으로 바꿨다. `main.cpp` 만 빼는데, 테스트 실행 파일마다 자기 main 이 필요해서
  `sw_addTestExecutable` 이 타겟마다 따로 붙이기 때문이다 — 그 이유를 목록 자리에 적었다.
- 전체 `ctest` 가 EngineTest 를 두 번 도는 것(460 + 410)은 **의도된 것** 이라고 CMake 에 적었다.
  CI 는 `-L nogpu` 로 부분집합을, 개발자는 전체를 돈다. 48초 중 3초라 없앨 이유가 없다.

**확인**: `Ninja-Debug` ctest **14/14** · `CI-Debug`(유니티) · `Ninja-Release` · `Ninja-Shipping`
nogpu 각 5/5 (**LTO 켠 채로**) · `RunBuildWarnings` 세 구성 0건 · Release·Shipping 구성 출력에
`foo.lib`/`LNK1107` 0건

### 2026-09-14 (어제 넣은 린트가 케이스 둘을 놓치고 있었다 — 그리고 그걸 쫓다 배포본 세그폴트를 찾았다)

어제 만든 `CheckTestSuites.py` 가 **자기 구멍을 못 보고** 있었다. 바이너리 등록 수(809)와 소스에서 센
수(808)가 하나 어긋나는 것을 따라가서 잡았다. 그 한 개 차이가 세 가지를 끌고 나왔다.

**1) 린트가 들여쓴 케이스를 통째로 놓쳤다.**
`_kCaseRe` 가 `^SW_TEST_CASE` 로 **줄 맨 앞만** 봤다. `TestTexturePipeline.cpp` 의 두 케이스는
`namespace sw::editor { ... }` 안에 들여쓰여 있어서 그 파일이 검사에서 그냥 빠졌고, 밑줄 든 스위트 이름
(`Editor_TexturePipeline`)이 규칙 1 을 어긴 채 살아 있었다. 진짜 수는 **122 스위트 · 810 케이스** 였다.

정규식만 고치면 같은 일이 또 난다. **토큰을 따로 세어 대조** 하게 했다 — `SW_TEST_CASE(` 출현 수와
파싱된 수가 다르면 "이 검사가 그 파일을 놓치고 있습니다" 로 실패한다. 여러 줄로 쪼갠 표기를 넣어 확인했다.

**2) 배포 구성 테스트를 자기 폴더에서 돌리면 세그폴트했다.** (원인 둘)
Shipping 은 테스트 바이너리가 `TestBin` 에 나가는데(배포용 `Bin` 에 테스트와 DXC 를 섞지 않으려고),
`CLAUDE.md` 는 `build/<preset>/Bin` 에서 돌리라고 적혀 있었다. 적힌 대로 하면 파일이 없고, 있는 곳에서
돌리면 죽었다. 같은 바이너리가 `Bin` 을 작업 폴더로 주면 407/409 로 통과한다.

- **리소스 루트를 못 찾은 실패를 31 곳이 버리고 있었다.** `SW_LOG_ASSERT` 는 배포본에서 사라지지 않지만
  **브레이크 없이 로그만 남기고 진행** 한다. Debug 는 브레이크가 있어 깔끔히 멈추므로 이 구멍이 안 보였다.
  31 곳을 `SW_ASSERT_TRUE( ResourceUtil::initialize() )` 로 바꿨다(이미 6 곳은 그렇게 쓰고 있었다).
  **프로덕션 코드는 멀쩡했다** — `EngineLoop` 와 `ResourceManager` 는 반환값을 검사한다.
- 그래도 안 죽었다. 두 번째 원인은 `MaterialTest.FastBytePackingDirectMethods` 가 **로드 실패를 약한
  기대로 넘긴 뒤** 빈 버퍼를 `reinterpret_cast` 해서 읽는 것이었다. 뒤가 전부 앞에 달린 자리는 하드 단언이다.

지금은 잘못된 폴더에서 돌려도 21 개가 깨끗이 실패하고 exit 1 로 끝난다(예전엔 exit 139).

**3) 구성마다 도는 케이스 수가 다른데 CTest 는 똑같이 "Passed" 라고만 했다.**

| 실행 파일 | Debug · Release | Shipping |
| --- | ---: | ---: |
| **SmokeTest** | **19** | **1** |
| EngineTest | 460 | 456 |
| CoreTest · ReflectionTest · EditorTest | 177 · 101 · 52 | 같음 |

SmokeTest 19 → 1 은 의도된 것이다(핫 리로드가 배포본에 없다). 문제는 **의도와 사고를 가를 수 없다는**
것이었다. DXC 가 사라지거나 구운 셰이더가 없어지면 케이스가 스스로 스킵하고 CI 는 초록이다.

선을 하나 그었다: **필터로 고른 스위트의 케이스가 전부 스킵되면 실패한다.** 그 실행은 아무것도 검증하지
않았다. 2026-09-14 기준 Debug·Release·Shipping 어디에도 통째로 스킵되는 스위트가 없어서 **예외 목록이
없다** — 정말 필요한 실행은 `--allow_empty_suite` 로 연다. 구성별 실측 표는 `Test/README.md` 에 적었다.

**4) RHI 백엔드 목록은 정말로 무방비였다.**
`RhiBackendSources.cmake` 는 42 개를 손으로 나열한다. `CheckSourceGlob` 이 이미 본다고 생각했는데
**실험으로 아니라는 것을 확인했다** — 목록에서 한 줄을 지우고 재구성하니 검사는 OK 를 냈다. 빠진 파일은
컴파일이 안 되는 게 아니라 **Engine 타겟의 glob 이 주워가서** 모듈 대신 Engine.dll 로 들어가기 때문이다
(그래서 compile_commands 대조로는 영원히 안 잡힌다. 증상은 모듈의 미정의 심볼이고, 실제로
`VulkanRHIRenderPassCache.cpp` 로 겪었다). 목록과 디스크를 양방향으로 맞추는 검사를 `CheckSourceGlob` 에
붙였고, 양쪽 탐침으로 확인했다.

**5) `SW_ASSERT_FALSE` 는 넣고 `SW_ASSERT_NULL` 은 안 넣었다.**
`SW_ASSERT_TRUE( x.empty() == false )` 로 우회하던 자리가 14 곳 있어서 그건 매크로로 만들고 전부 옮겼다.
`SW_ASSERT_NULL` 은 부를 자리가 **하나도 없어서** 만들지 않았다 — 짝을 맞추려고 만들면 어제 걷어낸
`TestFixture` 와 같은 것이 하나 더 생긴다. 왜 없는지는 매크로 자리에 적어 두었다.

> **`foo.lib LNK1107` 는 2026-09-14 에 원인까지 닫혔다** — 아래 "LTO 가 켜져 있다고..." 항목 참고.
> CMake 의 `/showIncludes` 잔재가 아니라 **IPO 지원 탐지**였다.

**확인**: 케이스 이름 전수 비교 **810 → 810 차이 0** · `CheckTestSuites` 122 스위트 810 케이스 ·
`Ninja-Debug` ctest **14/14** · `CI-Debug`(유니티) · `Ninja-Release` · `Ninja-Shipping` nogpu 각 5/5 ·
`RunBuildWarnings` 세 구성 0건 · 새 규칙 넷을 탐침으로 확인(들여쓴 케이스 · 다중 행 표기 · RHI 목록
양방향 · 빈 스위트 실패와 `--allow_empty_suite`)

### 2026-09-13 (테스트 스위트 이름이 CI 필터의 손잡이였다 — 이름을 하나로 통일하고 린트가 지키게 한다)

파일을 가르고 났더니 **남은 문제는 줄 수가 아니라 이름과 목록** 이었다. 넷을 고쳤고, 전부 `CheckTestSuites.py`
하나가 지킨다(린트 CTest 7 → 8).

**1) CI 가 GPU 테스트를 도로 삼킬 자리가 둘 있었다.**
`EngineTest_NoGPU` 는 스위트 이름으로 거른다. 그런데 제외 스위트와 CI 스위트가 **한 파일에** 있었다 —
`TestRHI.cpp` 가 `RHITest`(디바이스를 만든다) 옆에 Support 세 스위트를, `TestShader.cpp` 가
`ShaderCompilerTest` 옆에 굽기·스테이지·캐시 넷을 들고 있었다. TestRHI 는 줄 단위로도 섞여 있어서
비-GPU 케이스 넷이 GPU 케이스 사이에 끼어 있었다. 이러면 새 케이스를 옆 스위트에 붙이기가 너무 쉽고,
그 순간 디바이스가 필요한 케이스가 CI 로 들어간다 — **2026-09-08 에 정확히 그 방향으로 나흘간 빨갰다.**

| 전 | 후 (CI 제외) | 후 (CI 실행) |
| --- | --- | --- |
| `TestRHI.cpp` 1357줄 | `TestRHIDevice.cpp` — `RHIDeviceTest`(15) | `TestRHISupport.cpp` — 핸들 표·해제 큐·셰이더 요청(5) |
| `TestShader.cpp` 837줄 | `TestShaderCompiler.cpp` — `ShaderCompilerTest`(6) | `TestShader.cpp` — 굽기·스테이지·캐시(14) |

**2) 필터 다섯 줄을 손으로 맞추던 것을 마커로 닫았다.**
각 스위트가 자기 파일에 `// SW_TEST_REQUIRES_HOST( 스위트 ): <이유>` 를 적고, 린트가 그 집합과 CMake 필터를
**양방향** 으로 대조한다. 마커가 있는데 필터에 없으면 "CI 가 이것을 돌리고 있습니다", 필터에 있는데 마커가
없으면 죽은 항목이다. `SW_OWNERSHIP_RAW_OK` 와 같은 모양이다 — **목록이 아니라 코드 옆의 이유가 정본이다.**
제외 사유가 셋으로 갈린다는 것도 이때 드러났다: GPU(`RHIDeviceTest`·`RenderPassGpuTest`) ·
디스플레이(`WindowTest`) · DXC(`ShaderCompilerTest`·`LiveShaderTest`). 그래서 `*GpuTest` 같은 이름 규칙으로는
못 묶는다 — 마커가 맞다.

**3) 스위트 이름이 세 관례로 갈려 있었다.** 123 개 중 접두어형 45 · 접미어형 66 · 맨이름 12.
`XxxTest` 하나로 통일했다(121 개). 이름이 CTest 필터의 유일한 손잡이라 이건 미관 문제가 아니었다 —
`Core` 라는 스위트가 있어서 `--test_filter=Core*` 가 `Core_String` 까지 끌어왔다.

- **계층 접두어를 버린 이유**: 실행 파일 이름이 이미 그 말을 한다. `CoreTest.exe` 안의 `Core_String` 은
  같은 말을 두 번 한다. 게다가 **그 접두어는 믿을 수도 없었다** — `Engine_CommandLine` · `Engine_Event` ·
  `Engine_GlobalVariable` 셋은 CoreTest 에 있었다.
- 사실상 같은 스위트가 이름만 갈린 쌍 둘을 합쳤다: `GameObjectHierarchy`(1) → `GameObjectHierarchyTest`,
  `Editor`(1) → `EditorAssetTypeTest`.
- **한 스위트가 두 파일에 걸친 것 둘**도 닫았다. `SceneTest` 는 `TestSceneAsync.cpp` 쪽을 `SceneAsyncTest`
  로(그 파일의 주제가 그거다), 스트리밍은 세 이름(`Engine_Resource`·`Engine_Streaming`·`AssetStreamingTest`)이
  두 파일에 흩어져 있던 것을 `AssetStreamingTest` 하나로 모았다(케이스 둘을 파일째 옮겼다).

**4) 문서가 없는 기능을 가리키고 있었다.**
`TestFixture` / `SW_TEST_FIXTURE` 는 **쓰는 테스트가 하나도 없었다.** 그런데 `Test/README.md` 의 유일한
예제 코드가 그것을 썼다. 지우고 예제를 실제로 쓰이는 `SW_TEST_DEFER_CLEANUP` 으로 바꿨다(`TestCompression.cpp`
에서 그대로 가져왔다). 같은 README 가 린트를 "여섯" 이라 적고 있었는데 그때 이미 일곱이었다 — 여덟으로
고치면서 **"세지 말고 `ctest -L lint -N` 로 확인하라"** 를 같이 적었다.

**곁가지**: `EditorTest` 가 Editor 소스 20 개를 손으로 나열한다(폴더엔 55 개). 그 목록은 "전부" 도
"ImGui 안 쓰는 것 전부" 도 아니라 **테스트가 실제로 링크해야 하는 것** 이라 기계가 못 고른다 — 확인해 보니
ImGui 를 안 쓰는 파일이 제외 목록에도 12 개 있었다. 목록은 손으로 두되 썩는 두 방향(죽은 경로 · ImGui 를
타는 파일 유입)만 린트가 막는다. 그리고 파일 중간에서 열리던 헬퍼 네임스페이스 다섯을 맨 위로 모았다
(`TestVector.cpp` 는 익명 네임스페이스가 둘이었다). **깨질 수는 없었다** — 테스트 타겟은 유니티 빌드를
타지 않는다(확인함). 순수 일관성이다.

> **손대지 않은 것**: `TestRenderPassGpu.cpp` 2869줄 · `TestArchive.cpp` 1582줄 · `TestGameFramework.cpp`
> 1462줄. 전부 **한 스위트 한 주제** 라 더 가를 이유가 없다. 줄 수는 증상이 아니다.
>
> **`foo.lib LNK1107`**: `Ninja-Release` · `Ninja-Shipping` 빌드 끝에 CMake 의 `/showIncludes` 탐지
> 잔재(`CMakeFiles/ShowIncludes/foo`)가 실패한다. 실제 타겟은 전부 빌드되고 테스트도 돈다. 이번 작업 전부터
> 있었고 Test 와 무관해 건드리지 않았다.

**확인**: 케이스 이름 전수 비교 **808 → 808 차이 0**(스위트 이름만 바뀌었다) · `Ninja-Debug` ctest **14/14**
(린트 8 포함) · `CI-Debug`(유니티) nogpu 5/5 · `Ninja-Release` · `Ninja-Shipping` nogpu 각 5/5 ·
`RunBuildWarnings` 세 구성 모두 0건 · **린트 규칙 다섯을 탐침으로 각각 확인했다**(이름 위반 · 두 파일에 걸친
스위트 · 마커↔필터 양방향 둘 · 마커 파일에 다른 스위트 · 죽은 Editor 경로 · ImGui 를 타는 Editor 소스)

### 2026-09-13 (테스트 파일 하나가 스위트 스무 개를 들고 있었다 — 주제별로 가르고, 공유 픽스처를 헤더로)

네 파일이 전체 케이스의 **29%**(808 중 234)를 들고 있었다. 커진 이유는 케이스가 많아서가 아니다 —
**공유 픽스처가 그 파일 안에 있어서**다. 목 컴포넌트든 손으로 지은 `TypeInfo` 든, 쓰려면 케이스를 같은
파일에 써야 했다. 그래서 주제가 전혀 다른 스위트가 계속 붙었다. 픽스처를 헤더로 올리자 가르는 일이 그냥
따라왔다.

| 전 | 줄 | 스위트 | 후 (케이스) |
| --- | ---: | ---: | --- |
| `TestCompressionAndSpatial.cpp` | 623 | 5 | `TestSpatial`(8) · `TestRenderGraph`(2) · `TestAssetStreaming`(2) · `TestPropertyMetaHint`(1) |
| `TestInput.cpp` | 1828 | 11 | `TestInput`(29) · `TestActionMap`(17) · `TestInputRobustness`(6) |
| `TestGameObject.cpp` | 3367 | 18 | `TestGameObject`(32) · `TestGameObjectManager`(15) · `TestComponentTick`(12) · `TestSceneComponent`(5) + 이사 4 |
| `TestReflection.cpp` | 3891 | 20 | `TestReflection`(12) · `TestReflectionSerialization`(31) · `TestReflectionTypeInfo`(24) · `TestReflectionLayout`(16) · `TestReflectionParser`(9) · `TestReflectionEnum`(9) |

`TestCompressionAndSpatial.cpp` 는 **이름이 이미 둘을 이어 붙이고 있었다** — 그런 이름이 보이면 가를 때다.
압축 테스트는 그 파일에 없었다(진작에 옮겨졌고 이름만 남아 있었다).

**픽스처를 헤더로 올릴 때 지킨 것: 캐시와 등록은 TU 하나에만 둔다.**
`TestGameObjectMocks.h` 의 `makeMockComponentTypeInfo` 와 `TestReflectionFixtures.h` 의 `RegisterTypes` ·
`RegisterEnums` 는 **정의를 짝 `.cpp` 에 두었다.** 익명 네임스페이스째 헤더에 두면 include 한 TU 마다 캐시가
갈려 같은 이름의 `TypeInfo` 가 레지스트리에 여러 번 들어간다. 두 헤더 머리에 그 이유를 적었다.
정적 멤버 정의(`MockPoolLifecycleComponent::s_ctorCount`)에는 `inline` 이 필요하다 — 빼먹고 중복 심볼로
링크를 깨뜨렸다.

**스위트 셋(케이스 넷)은 파일이 아니라 주제를 따라 옮겼다.** `TagSystemTest` → `TestTagSystem.cpp`,
`ObjectStateXmlSerializerTest`(2) → `TestObjectStateRoundTrip.cpp`, `MathTest` → `Test/CoreTest/TestMath.cpp`
(그 파일 관례에 맞춰 `Core_Math` 로 개명, `sw::` 한정). 옮긴 케이스는 include 를 다시 맞춰야 했다.

**가르는 도중에 드러난 것 셋.**

- `TestReflection.cpp` 의 `JsonSequenceAcceptsPlainArray` 문서 주석이 **엉뚱한 케이스 위에** 얹혀 있었다.
  사이에 케이스를 끼워 넣으면서 벌어진 것이고, 3891 줄 안에서는 아무도 못 본다. 제자리로 옮겼다.
- 케이스 본문 끝에 군더더기 세미콜론(`};`)이 붙은 자리 여섯 개. 가르면서 `}` 로 통일했다.
- 번호 붙은 구역 배너(`// 16) Reflection_Binding — ...`)는 지웠다. 같은 스위트가 1·3·4·8·11·13 번으로
  여섯 번 등장했고 번호는 이미 어긋나 있었다(`6)` 과 `22)` 가 두 번씩). **파일 이름이 주제를 말하게 한다.**

> **가르는 도구의 함정 넷** (다음에 또 가른다면 그대로 겪는다).
> ① 케이스를 감싼 `#if` 는 산출 파일마다 다시 감싸야 한다(`TestInput.cpp` 에 아홉 개가 있었다).
> ② 파일 **중간에** 있는 `using namespace sw;` 를 흘리면 수백 개의 미선언 오류가 난다 — 케이스 본문에서
> 빼고 산출 파일 머리에 한 번 넣는다.
> ③ 케이스 끝을 `}` 로만 찾으면 위의 `};` 여섯 개에서 다음 케이스까지 삼킨다.
> ④ include 가지치기를 어간 단어 경계로 하면 `Serializer.h` 가 `XmlSerializer` 를 못 알아본다 — 부분
> 문자열로 재고, 모자라면 빌드가 알려 준다(실제로 `ReflectionEnumNames.h` 하나가 걸렸다).

**확인**: 케이스 총수 **808 → 808**(스위트·케이스 이름 전수 비교, 차이는 의도한 `MathTest` → `Core_Math`
하나뿐) · `Ninja-Debug` ctest **13/13** · `CI-Debug`(유니티) nogpu 5/5 · `Ninja-Release` · `Ninja-Shipping`
nogpu 각 5/5 · 린트 게이트 7종 0건 · `RunBuildWarnings` 세 구성 **모두 0건**

### 2026-09-13 (씬이 그리는 쪽을 알 필요가 없었다 — 그리고 소유 검사가 이름 다섯 개만 보고 있었다)

**`Scene::render( IRHIDevice* )` 는 호출자도 오버라이드도 없었다.** 실제 렌더링은 게임 스레드가 씬에서
스냅샷을 뽑아(`GpuSceneBuilder`) 패킷으로 넘기고 렌더 스레드가 그것만 보고 그린다. 그런데 이 죽은 함수가
**`Scene -> Graphics` 의존의 유일한 이유**였다 — 씬이 `FrameRenderer*` 를 멤버로 들고 `execute( this )` 를
부르는 모양이라, 패킷 구조와 정반대로 씬이 그리는 쪽을 알아야 했다. 함수·세터·게터·멤버와 `SceneManager`
전파 두 곳을 걷어냈다. `SceneManager` 는 포인터를 그대로 든다 — 에디터 뷰포트 툴바가 뷰 모드를 바꾸려고
찾아오는 경로 하나가 있고, 왜 남는지를 선언 옆에 적었다.

> **여기서 한 번 헛짚었다.** `MaterialCache.h` 와 `IRHIDevice.h` 도 죽은 include 로 봤는데 둘 다 살아 있었다 —
> `getMaterialManager()` 가 `MaterialCache` 를 돌려주고, `_pRHIDevice->waitIdle()` 이 역참조한다. **타입 이름이
> 본문에 안 보인다고 죽은 include 가 아니다.** 되돌렸다.

**`CheckRenderOwnership` 은 (파일, 구조체 이름) 다섯 쌍만 보고 있었다.** 그래서 그 헤더에 구조체를 **새로
더하면 검사를 그냥 빠져나간다** — 생포인터를 넣은 탐침 구조체가 통과하는 것을 확인했다. 같은 실험에서 두
번째 버그가 나왔다: 본문을 찾는 정규식 `\bstruct\s+Name` 이 문서 주석의 `@struct Name` **언급**에도 걸려
**엉뚱한 구조체의 본문**을 재고 있었다.

고친 방식은 목록을 없애는 것이다. **헤더를 정하고 그 안의 모든 구조체를 본다 — 목록이 아니라 자리가
규칙이다.** 주석은 길이를 유지한 채 공백으로 지운 뒤 파싱하고(오프셋이 밀리면 다른 구조체를 집는다),
예외는 `// SW_OWNERSHIP_RAW_OK: <이유>` 로 **코드 옆에 이유와 함께** 적는다. 검사 대상 5 → 9 개,
예외가 실제로 필요한 구조체는 정체성 키(`GpuMaterialElementKey`) 하나뿐이었다.

### 2026-09-13 (빌드 주변 정리 — 동시 처리를 한 자리로, 손으로 관리하던 목록 하나를 없앤다)

Graphics 를 두 번 훑고 나니 **같은 종류의 결함이 빌드 주변에도 있었다** — 복사된 패턴과 손으로 관리하는 목록.

**Scripts — 동시 처리가 일곱 곳에 복사돼 있었다.**
워커 수를 정하고 `ThreadPoolExecutor` 를 열고 `as_completed` 로 모으는 열 줄이다. 복사본마다 워커 수 정책이
달라서(`min(32, cpu*2)` · `min(16, cpu)` · `min(8, len, cpu)`) "이 저장소는 동시성을 어떻게 정하는가" 에 답할
곳이 없었다. `Scripts/common/Parallel.py` 가 그 자리다 — `mapConcurrent` · `flatMapConcurrent` · `runUntilNonZero`.

**정책을 둘로 남긴 것은 일부러다.** 파일을 읽고 훑는 일(IO 대기가 많다)과 자식 프로세스를 띄우는 일
(clang-format — 그쪽이 이미 코어를 쓴다)은 성질이 다르다. 하나로 합치지 말 것.
**스레드인 이유도 파일 머리에 적었다** — ProcessPool 은 5.3s → 9.4s 로 느려져 되돌린 측정이 있다(2026-09-08).

| 스크립트 | 전 | 후 | 무엇이 바뀌었나 |
| --- | --- | --- | --- |
| `CheckDataFileReferences` | 1927ms | **573ms** | 순차 → 동시, 그리고 **`resolve()` 를 참조파일 × 대상파일만큼 부르던 것**을 미리 한 번만 |
| 나머지 여섯 | — | 동일 | 구조 변경만 (측정 확인) |

> **재고 되돌린 것.** `CheckCodeConventions` 의 전수 검사 셋을 동시에 돌려 봤지만 6.66s → 6.72s(3회)로 이득이
> 없었다. 셋 다 정규식이라 GIL 을 놓지 않아 스레드로 겹치지 않는다 — 파일마다 나누는 위쪽 루프와 성질이 다르다.

**CMake — 유니티 제외 목록 19 개를 없앴다.**
한 클래스를 여러 `.cpp` 로 나눈 TU 를 `sw_skipUnitySources` 로 손수 빼 두고 있었다. 그 목록은 **TU 를 나눌 때마다
같이 고쳐야 하는 자리**였고(오늘 하루에 셋이 늘었다), 빠뜨려도 `Ninja-*` 는 유니티가 꺼져 있어 조용히 지나간다 —
CI 에서만 터진다. 실제로 예전에 "Renderer 재편 뒤 제외 6 개가 죽어 있었다"(그래서 `FATAL_ERROR` 가 붙어 있다).

목록이 있던 이유("익명 네임스페이스가 합쳐져 이름이 부딪친다")는 **이미 이름 규칙으로 막혀 있다** — 익명
네임스페이스 헬퍼는 클래스가 아니라 **TU 이름**을 따고(AGENTS.md), `CheckCodeConventions` 의
`Naming/DuplicateInternalHelper` 가 검사한다. **목록이 아니라 린트가 지킨다.**
목록 없이 유니티 빌드(CI-Debug)와 테스트 5/5, CI-Shipping 빌드를 확인하고 지웠다.
`sw_skipUnitySources` 함수 자체는 남겼다 — 이름으로 못 푸는 자리(플랫폼 `#if` 로 갈리는 블록 등)가 생길 수
있어서다. 다만 **목록이 다시 길어지면 규칙이 깨지고 있다는 신호**라고 함수 머리에 적었다.

> 이 규칙이 왜 필요한지는 같은 날 라이트 컴포넌트 셋에서 실제로 드러났다 — 익명 네임스페이스에 벌거벗은
> 상수를 두어 유니티 빌드가 깨져 있었다(위 항목). 린트는 **Internal 구조체 이름만** 보므로 벌거벗은 상수는
> 검사 밖이다. 유니티 빌드를 돌려야만 드러난다.

**확인**: `Ninja-Debug` · `CI-Debug`(유니티) · `CI-Shipping` 빌드 오류 0 · CI-Debug ctest 5/5 ·
린트 ctest 7/7 · clang-format 전체 952 파일 rc=0 · Shipping 쿠킹(AssetPipeline) 정상

### 2026-09-13 (Graphics 구조 2차 — 파일 이름이 곧 주제가 되게, 그리고 없던 타입 하나)

1차(스레드 경계·상태 뭉치)가 끝난 자리에서 다시 재고 네 가지를 더 했다. 기능 변경은 여전히 0 이다.

**1. `FrameRendererTransients.cpp` 가 새 잡동사니 서랍이 돼 있었다.** 1차에서 상태를 떼어냈더니 함수 23 개가
남았는데 네 가지 다른 일이었다 — 엔진 PSO 를 전부 등록하는 `ensurePassResources`(164줄) · 첨부 · PPM 덤프 ·
Present 변종. Material 에서 고친 것과 **같은 결함을 여기 남겨 둔 셈**이라 같은 규칙으로 갈랐다:

| 파일 | 주제 |
| --- | --- |
| `FrameRendererResources.cpp` | 기록 **전에** 만들어야 하는 것 — 엔진 PSO 등록 · 상수버퍼 링 · 머티리얼 폴백 · Present 변종 |
| `FrameRendererTransients.cpp` | 첨부(렌더타깃)의 수명과 조회 — 창 크기·파이프라인이 바뀔 때만 다시 만든다 |
| `FrameRendererReadback.cpp` | 첨부를 CPU 로 읽는 길(테스트 픽셀 비교 · `-gv_screenshot`). **프레임 경로가 아니다** — GPU 를 기다린다 |

**2. 없던 타입 하나 — `RHIConstantBufferSlot`.** 컴퓨트 디스패치 넷이 `{RHIBufferHandle, RHIDescriptorIndex}` 쌍을
각자 멤버로 들고 있었고(인스턴스 애니메이션 · 메시 모프 · 인스턴스 정렬 + `RenderView` 의 컬링), 타입이 없어서
**해제 순서를 아는 람다가 `releasePassResources` 안에 있었고 다섯 번 불렸다.** 구조버퍼용 `RHIStructuredBufferSlot` 의
형제를 만들어 넷 + `PassConstantRing::Slot` 을 그것으로 바꿨다. 람다는 사라졌다(남은 것은 텍스처인 TAA 히스토리 하나라
그 자리에 풀어 적었다). **에셋(Material·MaterialInstance)은 이 타입이 아니다** — 그쪽은 "어느 디바이스의 것인가" 를
알아야 해서 `RHIResidentBuffer` 가 맞다.

**3. `FrameRenderer.cpp` 의 3분의 1이 컴퓨트 디스패치였다.** 나머지는 수명과 프레임 진입인데 세 번째 주제가 섞여 있었다.
`FrameRendererCompute.cpp` 로 넷을 옮기고(`gv_gpuCulling`·`gv_morphDiag` 도 읽는 코드를 따라갔다) 759 → 459줄.
**래퍼 클래스를 다시 만든 것이 아니다** — 안 쓰이던 `ComputePass` 를 지운 결정은 그대로다(Renderer/README). TU 만 갈랐다.

**4. `ShaderBaker.cpp` 990줄 — 세 주제였다.** 익명 네임스페이스 514줄이 "무엇을 구울지" 였고 나머지가 "굽고 이름 짓기"
였는데, 그 안에 "이미 최신인가" 판정이 또 섞여 있었다. 셋으로 갈랐다: `ShaderBakeRecipe.cpp`(파이프라인·머티리얼 →
레시피) · `ShaderBakeStamp.cpp`(내용 해시 신선도) · `ShaderBaker.cpp`(굽기·이름). `BakeRecipe` 는 두 TU 를 넘으므로
`ShaderBakeRecipe` 로 헤더에 올렸다 — 덤으로 "이 빌드가 무엇을 구웠나" 를 밖에서 볼 수 있게 됐다.
각 TU 의 내부 헬퍼는 **TU 이름을 딴다**(`ShaderBakeStampInternal` 등) — 유니티 빌드에서 익명 네임스페이스가 합쳐지기 때문이다.

**결과 (Graphics 최대 파일 목록에서 셋이 빠졌다)**

| 파일 | 전 | 후 |
| --- | --- | --- |
| `ShaderBaker.cpp` | 990 | 427 + 348(Recipe) + 291(Stamp) |
| `FrameRendererTransients.cpp` | 710 | 269 + 349(Resources) + 108(Readback) |
| `FrameRenderer.cpp` | 759 | 459 + 320(Compute) |

---

**고친 것 둘 — 이번 작업과 무관한 선행 실패다. 둘 다 되돌려서 확인했다.**

**(가) 유니티 빌드(= CI 가 쓰는 프리셋)가 깨져 있었다.** `PointLightComponent` · `SpotLightComponent` ·
`DirectionalLightComponent` 가 **익명 네임스페이스에 벌거벗은 상수**(`kDefaultColor` 등)를 두고 있었다. 유니티 빌드는
여러 `.cpp` 를 한 TU 로 합치고 익명 네임스페이스는 TU 단위로만 숨기므로 셋이 서로 재정의였다 — AGENTS.md 가
`Internal` 헬퍼에 대해 적어 둔 바로 그 규칙인데 상수에는 적용돼 있지 않았다. 오늘 `6eccecec`("라이트를 여러 개로")가
들여왔고 `Ninja-*` 프리셋은 유니티가 꺼져 있어 로컬에서 안 보였다. TU 이름을 딴 구조체로 감쌌다.
> **린트가 못 잡는다.** `CheckCodeConventions` 의 `Naming/DuplicateInternalHelper` 는 Internal **구조체 이름**만 본다.
> 익명 네임스페이스의 벌거벗은 상수는 검사 밖이다 — 다음에 같은 것이 들어와도 유니티 빌드를 돌려야만 드러난다.

**(나) `LiveShaderTest` 가 자기가 더럽힌 캐시를 안 비웠다.** 프로브 `.hlsli` 를 셰이더 폴더에 쓰고 지우는데,
공유 헤더 해시는 `.hlsli` 집합을 한 번 훑고 **캐시**하므로 프로브가 있던 동안의 값이 다음 테스트로 샜다.
그래서 `ShaderBakeStampTest` 가 그 오염된 값을 기준으로 잡고, 스스로 무효화한 뒤 비교해 떨어졌다.
정리 델리게이트에 `invalidateSharedHeaderCache()` 를 넣었다(`ShaderBakeStampTest` 가 자기 임시 헤더에 이미 하던 것과 같다).
> **CI 는 이걸 못 본다** — `EngineTest_NoGPU` 필터가 `LiveShaderTest` 를 뺀다. 필터 없이 `EngineTest.exe` 를 통째로
> 돌리는 개발자만 만난다. 그래서 이번에 **필터 없는 전체 실행**을 기준선에 넣었다: **461 통과 · 0 실패**.

**확인**: Debug·Release·Shipping 경고 0 · **유니티(CI-Debug) 빌드 통과** · 린트 7/7 · ctest nogpu 5/5 양쪽 ·
`EngineTest.exe` 필터 없이 461/461 · 에디터 ON 네 백엔드 오류 0 · 창 15 · 빈 패널 0 · BackendSmoke 8회 오류 0.

> **함정(이번에 두 번 겪었다):** 빌드·스모크·벤치를 **동시에 돌리면 가짜 실패가 난다.** BackendSmoke 의 Vulkan
> `[Error]` 8건은 전부 `copyFile ... used by another process` 였고(내가 App 을 따로 돌리고 있었다), Shipping·Release 의
> `foo.lib LNK1107` 은 동시 빌드 중 CMake 프로브 레이스다. 둘 다 단독 실행하면 0 건이다.

### 2026-09-13 (Graphics 구조 — 스레드 경계·상태 뭉치·파일 이름을 타입으로 옮긴다)

지난 두 점검(09-12 죽은 타입, 09-13 중복)이 "합칠 것" 을 찾았다면 이번엔 **"경계가 타입에 없는 것"** 을 찾았다.
여섯 항목이고, 기능은 하나도 바뀌지 않았다 — 전부 같은 코드가 다른 자리에 산다.

**1. GpuScene 이 한 클래스로 두 스레드를 섬기고 있었다 → 셋으로 갈랐다.**
게임 스레드 인스턴스(`EngineLoop`)는 다섯 메서드만 썼고 렌더 스레드 인스턴스(`FrameRenderer`)는 스무 개를 썼는데,
클래스는 양쪽 절반을 다 열어 두어서 **RT 가 `buildFromScene` 을 부르는 것을 컴파일러가 막지 못했다**(테스트 경로가 실제로 그랬다).

| 새 파일 | 사는 곳 | 든 것 |
| --- | --- | --- |
| `GpuSceneBuilder.h/cpp` | 게임 스레드 | 씬 수집 · 배치 나누기 · 머티리얼 원소 영속 ID · 재구축 판단 캐시. **GPU 핸들이 하나도 없다** |
| `GpuSceneSnapshot.h` | 둘 사이 | 옮겨지는 전부. 두 클래스가 서로를 모르고 이 타입만 안다 |
| `GpuScene.h/cpp` | 렌더 스레드 | 인스턴스 버퍼 · 배치 표 · 간접 인자 · 머티리얼 버퍼 · 정점/모프 풀. **씬을 볼 수 없다** |

`FrameRenderer::execute( pScene )`(에디터·테스트의 직접 경로)도 이제 자기 빌더로 스냅샷을 만들어 **패킷 경로와 같은 길**을 탄다 —
테스트가 런타임과 다른 길을 타던 것이 없어졌다. Graphics 최대 파일이던 1140줄이 481(RT) + 690(GT) 으로 갈리고, 헤더는 668 → 259(RT) + 263(GT) + 219(스냅샷) 이 됐다.
`CheckRenderOwnership.py` 가 보는 파일도 `GpuSceneSnapshot.h` 로 옮겼다.

**2. FrameRenderer 는 TU 만 나뉘고 상태는 안 나뉘어 있었다 → 상태 뭉치 셋을 클래스로.**
헤더 698줄 · 메서드 70 · 멤버 63 · 뮤텍스 넷이었고, 여섯 TU 가 같은 멤버 풀을 공유했다. 각자 자기 뮤텍스와 수명을 가진 셋을 뗐다:

- `PassConstantRing` — 드로우마다 하나씩 나눠 주는 상수버퍼 슬롯 링(원자 커서 · 하이워터 · 고갈 경고 래치). 0번은 프레임 시드 전용이라는 규칙이 이제 한 클래스 안에 있다.
- `RenderPsoCache` — 엔진 패스 PSO · Present 포맷별 PSO · 머티리얼 변형 · 바인딩 레이아웃/desc 표. **해제 순서(변형 → 패스 → Present → 레이아웃)가 주석이 아니라 `releaseAll` 한 함수**가 됐다. 만드는 일은 그대로 `FrameRendererPso.cpp` 가 한다.
- `TransientAttachmentPool` — 이름으로 찾는 첨부 풀 + "이번 프레임에 이미 클리어했는가". 조회와 표시가 한 임계구역이라는 규칙이 타입 안으로 들어갔다.

헤더 698 → 632줄 · 멤버 63 → 51 · 중첩 구조체 7 → 4 · **뮤텍스 3 → 0**(전부 떼어낸 셋이 들고 갔다).
`_mapTransient`·`_listPassCbSlot`·`_mapEnginePso` 같은 이름은 FrameRenderer 에 더 이상 없다.

**3. Vulkan 디바이스 헤더의 절반이 렌더패스 캐시였다 → `VulkanRHIRenderPassCache`.**
멤버 92 · 중첩 구조체 19 로 DX12(28)의 세 배였는데, 그 중 키·해시·레코드·서술 일곱과 맵 셋·뮤텍스 하나가 렌더패스/프레임버퍼 캐시였다.
09-13 에 만든 `VulkanRenderPassSpec` 을 그 클래스로 옮기고 조회·생성·파괴를 통째로 넘겼다. 파괴가 `shutdown` 두 자리에 나뉘어 있던 것이
`destroyAll` 하나가 됐다. **스왑체인 렌더패스(CLEAR/LOAD)와 공용 오프스크린 RP 는 캐시가 아니라 디바이스 상태**라 그대로 뒀다 —
그것들은 스왑체인·텍스처 레코드의 수명을 따른다. 디바이스는 텍스처 레코드를 풀어 키와 서술만 만들어 넘긴다.
헤더 721 → 600줄 · 중첩 구조체 19 → 12 · 멤버 92 → 84.

**4. Material/ 파일 이름이 내용과 달랐다 → 이름이 곧 주제가 되게.**
`MaterialPacking.cpp`(810줄)는 실제로 `MaterialUtil` 구현체였고 타입 표·패킹 뒤에 XML 파싱·직렬화·define 조립이 붙어 있었다.
`MaterialIO.cpp` 는 `Material::load/save` 였다. "XML 을 읽는 코드가 어디 있나" 에 파일 이름이 답하지 못했다:

| 파일 | 담는 것 |
| --- | --- |
| `MaterialPacking.cpp` | 프로퍼티 타입 표 · 값 → CB 바이트 패킹 |
| `MaterialXml.cpp` | XML 읽기/쓰기 전부 (`MaterialIO.cpp` 를 흡수 — 로드/세이브도 XML 이다) |
| `MaterialPermutation.cpp` | define 조립 · 퍼뮤테이션 세대(`Material.cpp` 의 전역 원자도 여기로) |

**5. 테스트 파일이 소스 구조를 안 따랐다.** `TestRenderPass.cpp` 4411줄에 스위트 넷이 섞여 있었다. 스위트 이름이 곧 CTest 필터라 갈라도
동작이 바뀌지 않는다: `TestRenderPass.cpp`(678) · `TestRenderPassGpu.cpp`(2868) · `TestGpuScene.cpp`(779) · `TestMeshPrimitive.cpp`(187).
`RenderPassTest.GpuScene*` 넷은 이름이 자리를 배신하고 있어서 `GpuSceneTest.*` 로 옮겼다(케이스 수는 그대로).
`RHIShaderRequestTest` 도 `TestShader.cpp` → `TestRHI.cpp` 로 (RHI/Support 타입이다).

**6. 자리 문제.** RT 소유 GPU 풀 둘(`GpuMeshVertexPool`·`GpuMeshMorphPool`)이 CPU 에셋 `Mesh` 옆에 있었다 → `Renderer/Scene/`.
`Graphics/Debug/` 와 `Renderer/Debug/` 가 한 단계 차이로 둘 있었다 → `Renderer/Debug/` 하나로.

**README 가 코드보다 늦어 있었다 (이 저장소는 README 가 읽기 순서라 틀린 표가 곧 함정이다).**
`RHI/README.md` 는 DX11 에 `DeviceInit`/`Submission` 이 "없다" 고 표와 이유까지 적고 있었는데 09-12 에 둘 다 만들었다.
폴더 트리에 `RHIRenderResource`·`RHIResidentBuffer`·`RHIStructuredBufferSlot`·`RHICommandListForwarder`·`RHIShaderRequest`·`RHIDxgiTearing` 이 없었고,
`Graphics/README.md` 폴더 표에 `Texture/`·`Upload/` 가, `Renderer/README.md` 트리에 `Light/`·`Debug/` 가 없었다. 셋 다 채웠다.

**고친 것 하나 — 이번 작업과 무관한 Shipping 게이트.** `GameFrameworkTest.EnhancedInput_DebugChordsAndDefaultFallback` 이
Shipping 에서 떨어지고 있었다. `ActionMap::bindDefaultFallback` 이 리로드 조합 키 셋을 `#if !defined( SW_SHIPPING )` 으로 감싼 것은
**2026-09-12 `237335a7`** 인데 테스트(2026-09-02)는 무조건 단언하고 있었다. 배포본에 없는 것이 정답이므로 테스트에 같은 가드를 넣었다.

**확인**: Debug·Shipping 빌드 경고 0 · 린트 7/7(`CheckRenderOwnership` 포함) · ctest nogpu 5/5 ·
`RenderPassGpuTest`+`RHITest`+`ShaderBindingContractTest`+`LiveShaderTest`+`ShaderCompilerTest` 54/54 ·
에디터 ON 네 백엔드 종료 0 · `[Error]` 0 · 창 15개 · 빈 패널 0개(문서된 기준선 그대로).

### 2026-09-13 (배치 871개를 드로우 325번으로 — 정점 풀 · 배치 표 · 인스턴스 슬롯 스트림, 그리고 버린 설계 하나)

**백로그 1-7.** 씬 드로우 루프가 배치마다 `drawIndirect` 를 한 번씩 불러 871 호출이었고, 그 비용이 RT 프레임의 41% 였다.
같은 PSO·머티리얼의 연속 배치를 멀티 드로우 하나로 내려면 셋이 필요했다:
- **정점 풀(`GpuMeshVertexPool`)** — 씬 메시 정점을 한 정점 버퍼에 이어 붙이고 간접 인자의 `startVertex` 를 풀 오프셋으로.
  메시 집합이 같으면 다시 만들지 않는다(순서가 아니라 집합 비교). 풀에 못 든 메시는 자기 버퍼로 그린다.
- **배치 표(`g_SwBatches`, t13)** — 배치마다 다른 값(인스턴스 시작 · 모프 풀 시작 · 정점 풀 시작)을 `GpuBatchInfo` 에 넣어
  패스당 한 번 건다. 컬링 컴퓨트의 t1 과 같은 버퍼(32바이트). 루트 상수에는 이제 머티리얼 원소 수 하나뿐이다.
- **인스턴스 슬롯 스트림(정점 슬롯 1)** — `0,1,2,…` 를 담은 정점 버퍼를 인스턴스 스텝으로 걸고 간접 인자의 `startInstance` 를
  배치 시작으로 둔다. 입력 어셈블러가 네 API 모두 `startInstance + i` 번째 원소를 주므로 정점 셰이더는 자기 전역 자리를
  `SwVertexInput.instanceSlot` 으로 받고, 인스턴스의 `meshBatchIndex` 로 배치 표를 읽는다. **SV_InstanceID 를 더 이상 쓰지 않는다.**

**버린 설계 — 드로우 ID.** 처음엔 DX12 커맨드 시그니처가 레코드의 배치 번호를 루트 상수로 주입하고 Vulkan·GL 은 `DrawIndex`
내장 변수를 더하는 설계였고 네 백엔드에서 그림이 맞았다. 그런데 DX12 의 ExecuteIndirect 가 루트 상수를 바꾸는 시그니처에서
**호출당 두 배 느려져**(871 호출 335→661us) 묶어도 본전이었다 — 런타임이 인자를 패치한다. 스트림 방식은 시그니처가 평범한 DRAW
그대로라 그 비용이 없고, 백엔드별 드로우 ID 기능(shaderDrawParameters · gl_DrawID)에도 기대지 않는다. RHI 에 새 API 도 ABI 변경도
없다 — `drawIndirect( …, drawCount )` 그대로다.

**실측한 API 차이 — SV_VertexID 와 startVertex.** Vulkan(VertexIndex)·GL(gl_VertexID)은 간접 인자의 startVertex 를 **포함**하고
D3D11·D3D12 는 드로우 안의 0 기반 번호다. 처음엔 넷 다 포함한다고 믿고 빼기만 했더니 DX 에서 정점 풀 첫 메시만 모프됐다.
`RHITest.SceneDrawVertexIdStartsAtZeroOnlyOnD3D` 가 provokingvertex.hlsl 을 startVertex 36 으로 그려 네 백엔드의 기대를 고정하고,
`binding.hlsli` 의 `SwMorphElementOf` 가 그 차이를 흡수한다(셰이더 파일엔 분기 없음).

**같이 고친 테스트 약점.** `MorphPoolIdentityMatchesRest` 가 "실루엣 픽셀 수의 차" 를 봤는데 변위가 sin(시간) 이라 실루엣 차가
0 근처를 지나가 흔들렸다(드로우가 빨라지자 DX 에서 떨어졌다). 달라진 픽셀 수(어느 채널이든 8 이상)로 바꿨다.

**수치 (Release · 큐브 2000 · 변형 200 · 200프레임, 조용한 머신)**

| | 호출/프레임 | RT.Draw.gpuBatches | RT.Frame |
|---|---|---|---|
| DX12 | 871 → 325 | 458 → 200us | 1114 → 1140us (동등) |
| Vulkan | 871 → 325 | 448 → 235us | ~1007 → ~1025us (동등) |
| OpenGL | 871 → 325 | 660 → 395us | 2250 → 1340us (**−40%**) |

DX12·Vulkan 은 드로우 루프 절약이 프레임 전체에선 상쇄됐다 — `RT.GpuScene.upload` 가 +25us(정점 풀 집합 비교) 이고 나머지는
GPU 쪽(멀티 드로우가 배치 순서를 바꾸지 않으므로 그림은 같다). 벤치와 경고 스윕을 **같이 돌리면 수치가 오염된다**(최대 프레임
22ms) — 재는 동안 다른 빌드를 돌리지 말 것.

**남은 것(다음 사람에게).** 그룹당 배치가 2.7 개뿐이다 — 그룹 키가 머티리얼 CB(`_materialCb`)와 텍스처 슬롯까지 포함해서다. 씬
셰이더는 b1 을 읽지 않으므로(머티리얼 데이터는 구조버퍼) 키에서 빼면 그룹이 훨씬 길어진다. 다만 그 키가 정말 안 쓰이는지는
레이아웃(리플렉션)으로 확인해야 한다 — 짐작으로 빼지 말 것.

**확인**: `RenderPassGpuTest.MergedSceneDrawsMatchPerBatch` (풀 끔 · 풀 켬 배치마다 · 묶음 셋을 픽셀로 비교 + 호출 수 감소) ·
`RHITest.SceneDrawVertexIdStartsAtZeroOnlyOnD3D` · RenderPassGpuTest 24/24 · RHITest 15/15 · ShaderBindingContractTest 6/6 ·
nogpu 5/5 · BackendSmoke 8회 `[Error]` 0 · 에디터 ON 네 백엔드 오류 0 · 경고 세 구성 0. 진단 스위치 `-gv_drawMerge=0`,
`-gv_vertexPool=0`.

### 2026-09-13 (Graphics 구조 점검 — 합칠 것은 Vulkan 렌더패스 여섯 벌이었고, GL 은 오류를 낼 곳이 없었다)

**합칠 수 있는 것을 다시 쟀다.** 타입 238개 중 참조 3곳 이하는 해시 펑터·레코드·GPU 레이아웃 계약뿐이라
지울 것이 없었고(2026-09-12 결론 유지), 6줄 창 겹침으로 잰 파일 간 중복은 백엔드 헤더의 override 선언(그때
기각)과 **Vulkan 렌더패스 생성** 하나뿐이었다. 그것은 실재했다 — 스왑체인 CLEAR/LOAD · 공용 오프스크린 ·
포맷별 오프스크린 · PSO 호환용 · 합성 프레임버퍼 · desc 기반, 여섯 자리가 같은 40줄을 각자 들고 있었고
서브패스 의존성 마스크가 자리마다 조금씩 달랐다. `VulkanRenderPassSpec` + `createRenderPassFromSpec` 하나로 —
자리마다 다른 것(포맷 · loadOp · storeOp · 레이아웃)만 필드고 나머지는 고정이다. 순 −135줄.

**GL 은 오류가 어디에도 나오지 않고 있었다.** `glGetError` 호출 0곳, 디버그 콜백 없음. DX11/DX12 는 디버그
레이어를 `[Error]` 로 흘리고 Vulkan 은 검증 레이어가 있는데 GL 만 비어 있었다 — "드로우는 나가고 화면만 빈다"
종류의 결함이 GL 에서 유독 오래 살아남은 이유다. 비-Shipping 은 디버그 컨텍스트(`WGL_CONTEXT_DEBUG_BIT_ARB`) +
KHR_debug 콜백(HIGH/ERROR → `[Error]`, MEDIUM → `[Warning]`, 알림은 `glDebugMessageControl` 로 끔). 켜 보니 스모크 ·
에디터 ON 모두 GL 오류 0 — 지금은 깨끗하다는 것이 이제서야 "확인된" 사실이 됐다. 함정: 켰다는 안내 문구에
`[Error]` 를 그대로 적었더니 스모크가 그 줄을 오류로 셌다. 로그 문구에 로그 레벨 토큰을 쓰지 말 것.

**성능은 재고 기각했다.** 배치 정렬을 (PSO → 머티리얼 → 메시) 로 바꾸고 정점 버퍼를 바뀔 때만 거는 가설 —
세 백엔드 모두 잡음 범위. 숫자와 다음 방향은 1-7 에 적었다.

**확인**: RHITest 14/14 · RenderPassGpuTest 23/23 · BackendSmoke 8회 `[Error]` 0 · Vulkan/GL 에디터 ON 종료 0 ·
오류 0 · 린트 0.

### 2026-09-13 (SSAO 결과를 아무도 읽지 않았다 — 선언이 곧 바인딩이 되도록 입력 계약을 한 표로)

**백로그 1-6.** 디퍼드 XML 은 처음부터 Bloom 의 입력으로 `AOColor` 를 적어 두었는데, 엔진은 그 패스에
`SourceColor` 하나만 걸었고 `postbloom.hlsl` 도 그것만 읽었다. SSAO 는 매 프레임 풀스크린 패스를 돌고 결과는
버려졌다. XML 의 `_listInput` 은 **그래프 정렬에만** 쓰였고, 어떤 첨부를 걸지는 패스마다 코드가 후보 목록으로
짐작했다(`pickFirstExisting( { "TransparentColor", "LitColor", … } )`). "선언했는데 안 걸리는 입력" 이 생길 수
있는 구조였고, 그것을 잡는 검사도 없었다.

**고친 것 — 표 하나를 검증과 실행이 같이 본다.**
- `RenderPassInputContract`(Pipeline/): 풀스크린 패스 타입이 읽는 **입력 역할**(SourceColor · SceneDepth ·
  GBufferAlbedo · GBufferNormal · ShadowMap · AmbientOcclusion)의 필수/선택 목록. 첨부 이름·포맷 → 역할은
  `resolveRenderPassInputRole` 하나가 정한다(고정 역할 이름 넷, 그 밖의 깊이는 SceneDepth, 나머지 컬러는 SourceColor).
- **로드 시점 검증 4번**(`RenderPipelineResource::validate`): 선언한 입력의 역할이 계약에 없으면 오류("선언만 있고
  바인딩되지 않는 입력"), 필수 역할이 빠지면 오류, SourceColor 가 둘이면 오류. 역할과 출력 이름은 그때 intern 해
  `RenderGraphPassDesc::_listResolvedInput / _listResolvedOutput` 에 둔다 — 프레임마다 다시 해석하지 않는다.
- **실행**: Lighting · SSAO · Bloom · Outline · Tonemap 다섯 분기가 **하나**가 됐다. 선언 입력을 역할 이름으로
  전부 걸고(`registerDeclaredInputs`), 타깃은 선언한 출력 중 첫 번째로 존재하는 것. TAA·Present 도 선언 입력을
  우선한다(Present 는 선언이 없을 때만 후보 사슬 폴백). 후보 목록은 `resolvePresentSource` 폴백 하나만 남았다.
- **Bloom 이 AO 를 곱한다.** PassCB 의 `g_SourceDepthIndex` 를 `g_AmbientOcclusionIndex` 로 바꿨다(SourceDepth 는
  Outline 만 썼고 그것은 SceneDepth 역할이다). `SampleAmbientOcclusion` 은 인덱스가 무효면 1 — 포워드에는 SSAO 가
  없으니 0 으로 폴백하면 화면이 검게 된다. 에뮬 슬롯 표(t1 = albedo | ao)를 C++ 와 HLSL 양쪽에 같이 고쳤다.
- **쇼 플래그** `FrameRenderer::setInputRoleEnabled( role, bool )` — 그 역할의 입력을 걸지 않는다(언리얼의
  `r.AmbientOcclusion.Levels=0` 자리). 테스트가 "켬/끔의 차이" 를 같은 프레임 안에서 본다.

**검증이 바로 잡아낸 것.** 포워드 XML 의 Outline 이 `SceneDepth` 를 읽으면서 선언하지 않고 있었다 — 코드가
몰래 걸어 주고 그래프는 그 의존성을 몰랐다. 선언을 추가했다. 합성 테스트 둘(입력 없는 Present · 이름 해석용
Tonemap)도 새 규칙에 걸려, 이름 해석 람다는 `_resolvedType` 을 보게 고쳤고 Present 는 SourceColor 를 선택으로 뒀다.

**함정 하나(내가 밟았다).** 다섯 분기를 하나로 접으면서 그 사이에 있던 **Transparent 메시 패스 분기를 같이
잘라냈다.** 스모크의 투명 픽셀 수가 9,150 → 6,350 으로 떨어졌고 `TransparentOrderMatchesAcrossBackends` 가 네
백엔드 모두 "투명 큐브 0 px" 로 떨어졌다. 투명 큐브가 카나리아라는 기록(backend-swap-blank-screen)이 이번에도
맞았다. 큰 if-체인을 접을 땐 잘라낸 구간에 다른 타입이 끼어 있지 않은지 grep 으로 셀 것.

**확인**: `RenderPassGpuTest.AmbientOcclusionReachesBloom` 이 네 백엔드에서 AOColor 에 가림이 있고, AO 를 끄면
Bloom 출력 평균이 오르는 것을 단언한다. RenderPassTest 22/22 · RenderPassGpuTest 23/23 · RHITest 14/14 ·
BackendSmoke 8회 `[Error]` 0 · 투명 9,150~9,300.

### 2026-09-13 (풀스크린 셰이더가 Vulkan·GL 에서 노멀을 색으로 읽고 있었다 — 그리고 계약 검사가 그 VS 를 건너뛰고 있었다)

**1-4c 의 원인은 readback 이 아니라 셰이더의 정점 입력 순서였다.** 두 백엔드 모두 픽셀이 (0,0,0,255) 였다 —
"클리어조차 안 보임" 이 아니라 **검은 삼각형이 클리어를 덮은 것**(2026-09-07 과 같은 증상, 다른 원인).
`fullscreentriangle.hlsl` 이 `struct VSInput { float3 pos : POSITION; float4 col : COLOR; }` 로 선언돼 있어서
Vulkan·GL 은 `col` 을 **location 1 = NORMAL(0,0,1)** 로 읽었고, `(0,0,1) × 빨강 = 검정` 이 됐다. DX 는 시맨틱으로
묶어 맞았다. `ccf87eec` 가 정점에 노멀·UV 를 넣을 때 `sprite2d` 는 고쳤지만 풀스크린 셰이더 열 개는 남아 있었다.

**고친 것.**
- `SwVertexInput` 을 `binding.hlsli` 에서 `common.hlsli` 로 옮기고, 정점을 받는 셰이더 **전부**가 그것을 쓴다
  (deferredlighting · fullscreenblit · fullscreentriangle · postbloom · postoutline ×2 · ssao · taa · tonemap ·
  computetestgeometry). 안 쓰는 속성은 DXC 가 최적화로 떼어 내되 location 은 표대로 남는다(COLOR = 3).
- **리플렉션이 정점 입력을 본다.** `ShaderReflectionData::_listVertexInput` (시맨틱 · 인덱스 · location) — SPIR-V 는
  `OpEntryPoint(Vertex)` + `Input` 저장 클래스 + `Location` 데코레이션 + `in.var.<SEMANTIC>` 이름, DX 는 입력 시그니처.
- **계약 5번 규칙**: 시맨틱이 `constant::arrVertexAttribute` 에 있어야 하고, Vulkan·GL 은 location 까지 같아야 한다.
  합성 테스트(SyntheticViolationsAreDetected 10) + 실제 바이너리로 확인 — 옛 선언으로 되굽자 vulkan·opengl
  `fullscreentriangle_vs` 가 "기대 3, 리플렉션 1" 로 잡히고 DX 둘은 통과한다(맞는 판정).
- **AllBakedShadersMatchContract 가 CB 도 리소스도 없는 VS 를 건너뛰고 있었다.** fullscreentriangle VS 가 정확히
  그 경우라 규칙을 넣어도 통과해 버렸다 — 정점 입력까지 비어야 건너뛴다. "검사가 있다" 와 "검사가 그 파일을
  본다" 는 다른 말이다.

**1-4b 도 닫았다.** `common/shaders/provokingvertex.hlsl` 이 정점마다 다른 `nointerpolation` 값을 실어
FIRST 면 빨강, LAST 면 파랑이 된다. `RHITest.ProvokingVertexIsFirstOnAllBackends` 가 네 백엔드에서 64×64 전부
빨강임을 단언한다 — GL 의 `glProvokingVertex( FIRST )` 한 줄이 이제 검증된다.

**확인**: RHITest 14/14 · RenderPassGpuTest 22/22 · ShaderBindingContractTest 6/6 · nogpu 5/5 · BackendSmoke 8회
종료 0 · `[Error]` 0 · 평균 RGB 0.3 이내. 남은 잡음: Vulkan 검증 레이어의 "Vertex attribute at location 2/3 not
consumed" **경고**(풀스크린 셰이더가 노멀·UV 를 안 쓰므로 DXC 가 떼어 냄) — 오류가 아니고 그림에 영향 없다.

### 2026-09-13 (Vulkan 이 `discard` 를 켜지 않은 기능으로 돌리고 있었다 — 그림은 맞았고 검증 레이어만 알았다)

**네 백엔드 스모크에서 Vulkan 만 `[Error]` 1건이 있었다.** `vkCreateShaderModule(): SPIR-V Capability
DemoteToHelperInvocation was declared, but … shaderDemoteToHelperInvocation is required`
(VUID-VkShaderModuleCreateInfo-pCode-08740). 셰이더는 `-fspv-target-env=vulkan1.3` 으로 굽고, 그 타깃에서 DXC 는
HLSL `discard` 를 `OpKill` 이 아니라 `OpDemoteToHelperInvocation` 으로 낸다 — `deferredlighting.hlsl` 과
`sprite2d.hlsl` 이 이 경로다. 디바이스 생성은 1.2 기능 체인까지만 걸고 있었다(`VulkanRHIDeviceInit.cpp`).
드라이버가 우연히 돌려 줘서 픽셀은 DX12 와 같았고, 그래서 그림 비교로는 보이지 않았다.

**직전 커밋의 재베이크가 드러냈다.** 그전까지 구운 바이너리는 `-Od` 였고(`ef15146b` 에서 고쳤다), 최적화가
켜지며 `discard` 의 코드젠이 바뀌었다. "구운 셰이더가 바뀌면 검증 레이어 로그를 다시 읽어라" 가 교훈이다 —
`[Error]` 수는 스모크 표에 있고, 픽셀 수만 보면 놓친다.

**고친 것**: `VkPhysicalDeviceVulkan13Features` 를 조회·생성 양쪽 체인에 붙여 `shaderDemoteToHelperInvocation`
을 켠다. 1.3 구조체는 1.3 디바이스에서만 유효하므로 `properties.apiVersion` 으로 가드하고, 기능이 없으면
경고를 남긴다(그 디바이스에선 `discard` 가 미정의다 — 1.1 타깃으로 되굽는 길이 남아 있다).

**확인**: `BackendSmoke.py` 네 백엔드 × 불투명/반투명 8회 모두 종료 0 · `[Error]` 0 · 평균 RGB 0.3 이내.
`RenderPassGpuTest` 22/22 · `RHITest` 는 **1-4c(`OffscreenDrawIsReadable`, 기존 결함)만** 실패 — 이 변경과 무관하다.

### 2026-09-13 (GL 모프가 산다 — 드라이버가 early-return 을 잘못 컴파일하고 있었다)

**네 백엔드 전부 GPU 메시 모프가 켜져 있다.** 모프 끔→켬 그려진 픽셀: dx12 40,338→37,588 · dx11
40,370→38,043 · vk 40,374→37,811 · **gl 40,404→38,378**. `_bGpuMeshMorph` 에 예외가 없다.

**원인.** 정점 셰이더의 `SwMorphElementOf` 가 평범한 early-return 이었다:
`if ( base == INVALID || … ) return INVALID; element = base + vid; return element < count ? element : INVALID;`.
DXC 는 이것을 SPIR-V 의 `OpSwitch(0){ default: … }` 구조로 내는데, **OpenGL 드라이버가 그 모양을 잘못
컴파일했다** — 같은 인보케이션에서 같은 UBO 멤버(`g_MorphVertexBase`)를 세 번 읽는 자리 중 덧셈에 쓰인
것만 -1 이 됐다(팩 프로브로 `el=vid-1, vid, base=0` 을 삼각형마다 읽어 확정). 결과는 정점마다 **한 칸 앞
원소**를 읽는 것이고, 마지막 정점은 범위를 넘어 폴백(노란 픽셀 ~730개)이었다. 분기 없는 한 식
(`bValid ? element : INVALID`)으로 바꾸자 GL 도 `el == vid == base + vid`. DX12·DX11·Vulkan 은 같은 소스로
처음부터 멀쩡했다.

**왜 오래 걸렸나.** 두 세션 동안 셰이더 **바깥**만 팠다 — 컴퓨트, 업로드, 버퍼 내용, 바인딩, 인덱싱,
루트 상수, 간접 인자, 컬링, `VertexId` 내장 변수, 구조체 레이아웃(평면 float4 배열로 바꿔도 같았다).
전부 되읽어 맞았고, 그 "전부 맞는데 틀린다" 가 곧 답이었다: 남는 건 셰이더 컴파일뿐이다. 결정타는
프로브를 **보간 없이**(`nointerpolation` 슬롯에 `el|vid<<8|base<<16` 을 실어) 읽은 것 — 보간된 색으로는
"둘의 짝이 어긋난다" 까지만 보이고 어느 쪽이 얼마나인지는 안 보였다.

**같이 닫은 것.**
- **Debug 빌드가 `-Zi -Od` 로 굽고 있었다.** 쿠커가 Debug `App.exe` 를 먼저 집으므로 저장소에 커밋되고
  배포에 실리는 바이너리가 전부 무최적화였다. 디버그 코드젠은 빌드 구성이 아니라 **요청**이 정한다
  (`ShaderCompileDesc::_bDebugCodegen`): 런타임 라이브 컴파일만 Debug 에서 켜고 베이커는 절대 켜지 않는다.
  캐시 키(인메모리·로컬 오버라이드 폴더·컴파일러 디스크)에 그 여부가 들어간다.
- 그 키에 리터럴을 그대로 넘겼다가 `computeHash64( const char*, length, bIgnoreCase )` 오버로드에 묶여
  **키가 상수**가 됐고, 모든 셰이더가 캐시 파일 하나를 공유해 DX12 가 PS 자리에 `vs_5_0` 을 받았다.
  `string_view` 로 넘겨야 한다 — 같은 실수를 할 자리가 그 함수에 하나 더 있다(`to_string(...)` 은 string 이라 안전).
- 모프 풀 원소를 `struct { float4 pos; float4 nrm; }` 에서 **평면 `float4` 배열**(정점당 둘)로 바꿨다. 원인은
  아니었지만 드라이버가 볼 구조체가 하나 줄고 C++ 스트라이드가 `sizeof(float4)` 로 단순해져 그대로 둔다.
- `glProvokingVertex( FIRST )` — 1-4b 에 검증 테스트가 남았다.

**회귀는 픽셀로 잡는다.** `RenderPassGpuTest.MorphPoolIdentityMatchesRest` — (A) 레스트, (B) 컴퓨트 없이
레스트 버퍼를 풀에 물림(`FrameRenderer::setMeshMorphDiag(2)`, 정답은 A 와 같은 그림), (C) 진짜 모프(A 와
달라야 함)를 네 백엔드에서 찍는다. early-return 을 되돌려 돌리면 **GL 에서만** "정점 셰이더가 다른 원소를
읽고 있다 (286,505 vs 561,645)" 로 떨어지는 것을 확인했다(변이 테스트).


### 2026-09-13 (구워 둔 셰이더가 소스와 어긋난 채 커밋돼 있었다)

**커밋된 `forwardlit` 바이너리가 라이트 버퍼 이전 것이었다.** 네 백엔드 모두. 그래서 Vulkan 이
GPU 메시 모프에서 다른 그림을 냈고, 나는 그것을 **백엔드 버그로 오인해** 백로그 1-4 에 "Vulkan 도
깨져 있다" 고 적어 두었다. 다시 구워 재니 DX12·DX11 과 픽셀 수가 같다.

**원인은 "무엇을 기준으로 낡았다고 판정하는가" 가 두 벌이었던 것이다.** `bake.stamp` 는 이미
**내용 해시**를 적고 있었는데(`git clone` 이 모든 mtime 을 체크아웃 시각으로 덮어쓰기 때문에),
정작 "다시 구울까" 를 정하는 쪽은 **파일 시간**을 봤다:

```cpp
const uint64 outMtime = FileUtil::getFileTimestamp( outPath );
bUpToDate             = ( outMtime >= sourceMtime );   // ← 이 저장소에서는 성립하지 않는 가정
```

이 저장소는 **구운 바이너리까지 커밋한다.** 그래서 `git pull` 이 소스와 산출물의 mtime 을 임의의
순서로 덮어쓰고, "산출물이 더 새것" 이 되는 순간 소스가 바뀌었는데도 그대로 넘어간다. 런타임도
같은 비교를 하고 있어서(`ShaderCache`), 낡은 바이너리를 **우선해서** 읽었다.

**고친 방식 — 판정 기준을 하나로.** `ShaderBaker::computeEffectiveSourceHash()`(소스 + 모든
`.hlsli` 의 내용 해시)와 `isBakedOutputCurrent()`(그 해시를 `bake.stamp` 와 대조) 하나만 남기고,
베이커 · `ShaderCache`(구운 것/로컬 오버라이드) · `ShaderReflectionLibrary`(매니페스트) · 컴파일러
디스크 캐시 키가 **전부 그 함수**를 쓴다. 스탬프 헤더는 `SWBAKE 2` → `3` 으로 올렸다 — 버전을
올리는 것이 곧 "옛 스탬프는 못 믿는다, 한 번 다 다시 구워라" 다.

로컬 오버라이드 캐시는 **폴더**에 해시를 끼웠다(`Saved/ShaderCache/<rhi>/<해시>/<이름>`). 파일
이름에 넣지 않은 이유는 그 이름이 베이커가 굽는 이름과 글자 단위로 같아야 하기 때문이다
(`ShaderBakerTest` 가 지키는 계약이고, 실제로 거기서 걸렸다).

**실측 동작**: 스탬프 v2 → 한 번에 360 개 재굽기 · 다시 돌리면 **0 개** · `common.hlsli` 한 줄 수정
→ **332 개**(그 헤더를 include 하는 전부) · `taa.hlsl` 한 줄 수정 → **8 개**(4 백엔드 × 2 스테이지).
회귀 테스트는 `ShaderBakeStampTest.FreshnessIsJudgedByContentNotFileTime`(nogpu).

**교훈 두 개.**
1. **백엔드 하나만 다른 그림을 내면 셰이더 산출물부터 의심한다.** 나는 컴퓨트 · 업로드 · 바인딩 ·
   인덱싱을 먼저 팠는데, 답은 "그 백엔드가 낡은 바이트코드를 돌고 있었다" 였다.
2. **스테일 판정 기준이 둘이면 그중 느슨한 쪽이 이긴다.** 스탬프가 내용 해시를 쓰고 있었는데도
   판정이 파일 시간이었으므로, 스탬프는 "맞다" 고 적으면서 실제로는 낡은 것을 통과시켰다.

**모프 진단 스위치가 남았다.** `-gv_morphDiag=<0|1|2|3>` — 능력표 무시하고 켜기(1), 컴퓨트를
건너뛰고 레스트 버퍼를 정점 셰이더에 물리기(2), 거기에 원소 번호표를 얹기(3). 2 가 "컴퓨트가
범인인가" 를, 3 이 "인덱싱인가 내용인가" 를 한 장으로 가른다. 백로그 1-4 가 이것으로 좁혀졌다.

### 2026-09-13 (백엔드를 바꾼 적이 없었다 — 그리고 그 뒤에서 OpenGL 이 둘 깨져 있었다)

**`-gv_rhiBackend=<n>` 이 아무 일도 하지 않고 있었다.** 이 세션의 "네 백엔드 확인" 은 전부
**DirectX12 를 네 번** 돌린 것이다. 앞의 세 커밋(디퍼드 복구 · 멀티 라이트 · 정점 노멀)에 적힌
백엔드 일치 수치는 그래서 근거가 없었다. 이 항목이 그것을 바로잡는다.

**원인.** `EngineLoop` 이 `EngineConfig::_window._defaultRHI` 를 전역 변수에 **무조건 대입**하고
있었고, 그 자리가 커맨드라인 적용 **뒤**였다. 짧은 플래그(`-dx12`·`-gl`…)는 `cliRequestsBackend`
가 먼저 걸러 주지만 `-gv_rhiBackend` 는 그 검사에 없었다 — 그래서 조용히 설정값으로 덮였다.
`getArgument` 는 **기본값이 있으면 안 적어도 true** 를 돌려주므로 "적혔는가" 를 물을 방법이 없었다.
`CommandLineManager::isArgumentProvided` 를 만들어 그 둘을 가른다.

> 교훈은 이미 저장소에 있던 것과 같다 — "에디터는 명시적으로 켜야 한다"(`-EnableEditor` 없이 돌린
> 검증은 에디터 OFF 검증). **로그로 실제로 무엇이 돌았는지 확인할 것.** 백엔드는
> `Initializing RHI with backend: <이름>` 한 줄이 정본이다.

진짜 백엔드로 다시 돌리자 OpenGL 만 두 군데가 깨져 있었다. 둘 다 **디퍼드 전용 경로**라, 디퍼드가
한 번도 안 돌던 동안 드러날 수 없었다.

**(1) MRT 클리어가 드로우 버퍼를 좁혀 놓고 되돌리지 않았다.** `beginRenderPass` 는 먼저
`glDrawBuffers( colorCount, … )` 로 MRT 목록을 걸어 두는데, 이어지는 클리어 루프가 첨부마다
`glDrawBuffers( 1, &drawBuf )` 로 목록을 하나로 좁혔다 — 그리고 **복구하지 않았다.** 루프가 끝나면
드로우 버퍼가 "마지막으로 지운 첨부" 하나뿐이라, G버퍼 패스의 `SV_TARGET0`(알베도)이 **노멀
첨부**로 가고 `SV_TARGET1` 은 버려졌다. 알베도 첨부는 클리어 값 그대로 남아 디퍼드 조명이
알베도 0 을 읽었다 — **OpenGL 만 화면이 거의 검게**(평균 12.7 vs 122.8) 나왔다.
`glClearBufferfv( GL_COLOR, 인덱스, … )` 로 바꿨다 — 드로우 버퍼 목록을 건드리지 않고 지운다.

**(2) `R16G16B16A16_FLOAT` 를 `GL_FLOAT` 로 매핑하고 있었다.** GL 이 픽셀당 16 바이트를 읽고 쓰는데
엔진이 잡아 둔 버퍼는 8 바이트/픽셀이다(`getRhiFormatBlockInfo` 가 정본) — 되읽기가 버퍼를 두 배로
넘겨 써서 **그냥 죽었다**(세그폴트). HDR 첨부를 CPU 로 읽는 경로(스크린샷 · 렌더 타깃 패널)가
생기기 전에는 이 포맷을 되읽을 일이 없어 드러나지 않았다. `GL_HALF_FLOAT` 로 고쳤다.

**추적 순서**(다음에 같은 증상일 때 그대로 쓸 것): 포워드는 GL 도 맞고 디퍼드만 틀렸다 → 디퍼드
조명 패스의 **입력을 하나씩 픽셀로 찍었다**. 라이트 수·그림자 항·노멀은 DX12 와 **완전히 같았고**
(구조버퍼 읽기는 멀쩡했다는 뜻이다), 알베도만 0 이었다 → G버퍼 알베도 첨부를 되읽으니 비어 있었다
→ MRT 쓰기 경로. 셰이더 식에 값을 대입해 계산으로 좁히는 방식이 또 한 번 결정타였다.

**이제야 진짜 검증** (큐브·구·실린더·캡슐·원뿔 25개 + 바닥, 프레임 25):

| | DX12 | DX11 | Vulkan | OpenGL |
|---|---|---|---|---|
| 디퍼드 평균 RGB | 122.8 | 123.8 | 122.6 | 123.1 |
| 포워드 평균 RGB | 203.4 | 203.1 | 203.7 | 203.0 |

전체 ctest 5/5(nogpu) · `RenderPassGpuTest` 21/21 · 린트 0건.
`Engine_CommandLine.ProvidedIsNotTheSameAsReadable` 이 "읽힌다 ≠ 적혔다" 를 고정한다.

### 2026-09-13 (정점에 노멀과 UV를 넣는다 — 셰이더가 둘 다 지어내고 있었다)

`RHIVertex` 가 위치·색뿐이라 셰이더가 나머지를 **지어내고** 있었다.

- 노멀: `DemoCubeNormal( 위치 )` — 원점 중심 **박스형** 도형에만 맞는 함수다. 구·원뿔은 각져 보이고,
  바닥 평면을 `y = 0` 에 두면 `|y|` 가 0 이라 ±X/±Z 를 받아 **바닥이 옆을 보는 것처럼** 칠해졌다
  (그래서 `createPlane` 은 면을 `y = +0.5` 에 두는 회피를 하고 있었다 — 이제 지웠다).
- UV: `localPos.xy * 0.5 + 0.5` — 도형을 XY 평면에 투영한 값이라 **앞뒤가 같은 자리를 물고**,
  옆면과 뚜껑이 겹치고, 단위 크기가 아니면 범위를 벗어났다.

이제 레이아웃은 `POSITION(3) · NORMAL(3) · TEXCOORD(2) · COLOR(4)` = 48바이트다.

**백엔드 넷이 각자 적던 입력 레이아웃 표를 하나로 합쳤다.** `constant::arrVertexAttribute` 가
정본이고 DX11·DX12·Vulkan·GL 이 그 표를 **돌면서** 자기 구조체를 만든다. 예전에는 네 곳에 같은
표가 손으로 적혀 있어서(`"POSITION", 0, 0` / `"COLOR", 0, 12`) 속성을 하나 더하려면 네 곳을 같이
고쳐야 했고, 한 곳을 빠뜨리면 그 백엔드만 조용히 다른 그림을 냈다. 오프셋도 `SW_OFFSET_OF` 라
구조체를 바꾸면 자동으로 따라온다.

**셰이더 쪽 함정 하나를 같이 닫았다.** DX 는 시맨틱 이름으로 묶지만 **Vulkan·OpenGL 은 선언
순서로 location 을 매긴다** — 중간 속성을 빼면 그 뒤가 한 칸씩 당겨져, 색을 읽으려던 셰이더가
노멀을 읽는다. `shadowdepth` · `gbufferalbedo` · `sprite2d` 가 실제로 `pos` 와 `col` 만 적고 있었다
(`sprite2d` 는 그 색을 **쓴다** — 그대로 뒀으면 Vulkan·GL 에서만 스프라이트 색이 틀렸다).
메시를 그리는 셰이더는 이제 `binding.hlsli` 의 `SwVertexInput` 하나를 쓴다 — 빼먹을 자리가 없다.

**도형 생성기는 해석적 노멀을 낸다.** 구는 원점 기준 방향, 실린더·캡슐 옆면은 **축을 뺀** 방사
방향(위치를 그대로 정규화하면 위아래로 기운다), 캡슐 반구는 **그 반구의 중심** 기준(원점 기준으로
잡으면 캡슐이 길수록 어긋난다), 원뿔 옆면은 기울기를 반영한 `normalize( h·cosθ, r, h·sinθ )`
(방사 방향으로 두면 원뿔이 원통처럼 칠해진다). 평평한 곳(큐브 면·뚜껑·밑면)은 면 노멀이 맞다.
위치·노멀·UV 는 `BuildVertex` 로 묶어 다닌다 — 감김을 바로잡느라 정점을 맞바꿀 때 셋이 같이
따라가야 하는데, 따로 넘기면 하나를 빠뜨려도 컴파일이 통과한다.

**모프도 노멀을 다시 만든다.** 위치만 바꾸면 변형된 표면이 원래 모양의 빛을 받는다 — 물결이 이는데
음영은 가만히 있어서 "변형이 안 걸렸나" 로 보인다. 높이장 변위의 표준 근사(`normalize( n - ∇_t h )`)를
쓴다. 변형 방향도 원점 기준 방향에서 **정점 노멀**로 바꿨다(원점 중심 도형에만 맞던 대용이었다).
풀 원소에서 **색은 뺐다** — 아무도 읽지 않는데 정점당 16바이트였다(풀 상한 400만 정점 = 64MB).

**확인**: 네 백엔드가 같은 그림(비배경 134,731~135,450 · 평균 122.5~122.8). 구·캡슐·원뿔이
부드럽게 칠해지고 그림자도 그대로다. GPU 모프 켬/끔이 50,237 픽셀 다르다. ctest 5/5(nogpu) ·
`RenderPassGpuTest` 21/21 · 린트 0건. 새 테스트 `MeshPrimitiveTest.PrimitiveNormalsAndUvsAreUsable` 이
노멀 길이·바깥 방향·UV 범위와 "구는 면마다 노멀이 달라야 한다"(= 평면 음영으로 되돌아가지 않았나)를 본다.

### 2026-09-13 (렌더 타깃을 에디터에서 본다 — 한 장 볼 때마다 앱을 다시 켜던 것을 끝냈다)

`Render Targets` 패널을 더했다(도구 패널, `Panel` 메뉴). 파이프라인이 만든 렌더 타깃을 **검색해서
고르고 그 자리에서 본다**. 예전에는 "지금 G버퍼에 뭐가 들어 있나" 를 보려면
`-gv_screenshotAttachment=<이름>` 으로 **프로세스를 다시 띄워** PPM 을 한 장 찍는 수밖에 없었다.
이번 세션의 디퍼드 버그 셋(풀스크린 컬링 · G버퍼 노멀 · 그림자 깊이 범위)을 전부 그 방식으로
쫓았고, 중간 단계 하나 볼 때마다 앱을 새로 켰다.

**통로는 `RenderTargetRegistry` 다.** 트랜지언트는 `FrameRenderer` 안의 private 맵이고 에디터는 그
인스턴스를 쥘 방법이 없다(에디터는 별도 모듈이고, 렌더러는 App 이 소유한다). 그래서 렌더러가
목록을 엔진 레지스트리에 **공개**하고 패널이 그것을 읽는다. 넘기는 것은 **스냅샷 사본**이다 —
포인터를 넘기면 읽는 도중에 트랜지언트가 재생성될 수 있다(창 크기가 바뀌면 전부 다시 만들어진다).
갱신은 **구성이 바뀔 때만** 한다(`ensureTransientResources` 안) — 매 프레임 돌 이유가 없고,
세대 번호로 "바뀌었나" 를 패널이 판단한다.

**서비스는 `OPT` 로 등록했다.** `required=1` 로 두면 이것을 바인딩하지 않는 호스트에서
`areEngineServicesBound()` 가 영영 false 가 되고 그 함수로 게이팅되는 경로가 통째로 무력화된다 —
`EngineServiceList.xxx` 의 `CommandStack` 주석이 바로 그 사고의 기록이다. 진단용 목록 하나 때문에
같은 일이 나서는 안 된다.

**패널이 하는 것**: 이름·포맷으로 검색(`EditorListFilter`), 목록에서 선택, 크기·포맷 표시,
미리보기(창에 맞춤 또는 배율 지정). 여는 순간 **화면에 나가는 첨부**가 선택돼 있다(`(screen)` 표시) —
이름순 첫 번째로 두면 `AOColor` 가 잡혀서 "이게 뭐지" 부터 시작하게 된다.
깊이 첨부는 미리보기를 하지 않고 **왜 못 하는지와 대신 무엇을 쓰면 되는지**를 적는다 — 백엔드마다
깊이 SRV 의 ImGui 등록 방식이 다르고, 받아도 D24S8 은 정규화 깊이라 거의 흰 화면이 된다.

**`-gv_editorOpenPanel=<id>` 도 함께 만들었다.** `-gv_editorOpenAllPanels` 는 전부 띄워 서로를 가리고
마지막에 등록된 패널이 위로 온다 — 새 패널을 화면 캡처로 확인하려다 실제로 막혔다. 하나만 띄우면
그 패널이 반드시 보인다.

**확인**: 에디터 실기동 캡처로 목록 10개·검색 필드·미리보기가 보이는 것을 확인했고,
`TonemapColor (screen)` 기본 선택에서 바닥에 진 그림자까지 그대로 보인다(그림자 수정의 독립 확인이
되기도 했다). `RenderPassGpuTest.RenderTargetsArePublishedForTheEditor` 가 **패널이 먹는 데이터**를
고정한다 — 목록이 비거나 이름이 바뀌면 패널은 조용히 빈 창이 되는 것이 유일한 실패 모드라,
G버퍼·조명 타깃이 이름으로 들어 있는지, 크기·핸들이 채워졌는지, 렌더러가 내려가면 목록이
비워지는지를 본다.

### 2026-09-13 (라이트를 여러 개로 — 그리고 그림자가 한 번도 진 적이 없었다는 것을 알게 됐다)

방향광·점광·스포트라이트가 **한 버퍼**(`g_SwLights`, t12)로 올라가고 포워드와 디퍼드가 **같은 함수**
(`lighting.hlsli` 의 `SwShadeLights`)로 읽는다. 조명 식을 두 벌로 두면 두 경로의 그림이 반드시
갈라진다 — 이 저장소에서 "경로마다 다른 그림" 이 가장 비쌌다.

**왜 상수버퍼가 아닌가.** 라이트 수는 씬마다 다르다. 고정 배열로 넣으면 상한이 곧 매 프레임 비용이
되고 넘는 순간 조용히 잘린다. 인스턴스·머티리얼·가시 목록이 이미 전부 구조버퍼이고 셰이더는
인덱스로 읽는다 — 라이트도 같은 결이다. 버퍼는 **패스당 한 번** 걸린다(드로우 사이에 바인딩이
바뀌지 않는다는 규약). 원소는 `float4` 넷(64바이트)이다 — `float3` 을 섞으면 std430 정렬 때문에
**OpenGL 에서만** 값이 어긋난다(모프 정점 풀에서 실제로 겪었다).

**타입은 셋이다.** 방향광(0) · 점광(1) · 스폿(2). 거리 감쇠는 점광과 스폿이 **같은 식**을 쓰고
(반경에서 자른 역제곱 — 언리얼 `InverseSquaredFalloff` 와 같은 모양), 스폿은 거기에 원뿔을 곱한다.
원뿔은 **코사인으로** 실어 보낸다(셰이더가 매 픽셀 `acos` 를 하지 않도록).
**그림자를 드리우는 빛은 방향광 하나뿐이다** — 그림자 맵이 하나라서다. 점광 그림자는 큐브맵이
필요한데 이 엔진에 큐브맵 자원이 없다. 없는 것을 있는 척하지 않는다.

#### 그림자가 한 번도 진 적이 없었다

바닥을 깔고(`-gv_benchGround=1`) 처음으로 **그림자를 눈으로 봤다** — 하나도 없었다.

원인은 그림자 직교 투영의 **깊이 범위**였다. `createOrthographic( w, h, -거리, +거리 )` 로 잡고
있었는데, 라이트 카메라는 원점에서 `거리`만큼 떨어져 원점을 본다. `createOrthographic` 은
`z' = (z_view - near) / (far - near)` 라, 그 범위에서는 씬 전체가 `z' ≈ 1`(원평면)로 뭉친다.
깊이 비교가 늘 "가려지지 않음" 이 되어 **그림자 항이 언제나 1** 이었다. 프로브로 확인한 값:
지오메트리 픽셀 123,329 개 중 라이트 클립 UV 안에 든 것이 18,587 개뿐이고 `ndc.z` 는 대부분 ≥ 0.94.
범위를 눈 기준 `[거리 - 반경, 거리 + 반경]` 으로 바꿨다. 같은 실수가 `FrameRendererConstants` 의
폴백에도 있었다(값을 두 군데 두면 갈라진다는 그 파일 주석의 실례다).

그리고 **샘플링 자체가 그림자가 아니었다.** 디퍼드는 그림자 맵을 **화면 UV** 로 읽고 있었고,
포워드는 한술 더 떠 **로컬 좌표로 만든 UV**(`localPos.xy * 0.5 + 0.5`)로 읽었다. 그건 그림자가
아니라 깊이 텍스처를 화면/물체에 붙인 무늬다 — 카메라를 움직이면 그늘이 물체를 따라오지 않는다.
둘 다 월드 위치를 **라이트 클립 공간으로 투영해** 읽도록 바꿨다(`SwSampleShadowAtWorld`).
디퍼드의 월드 위치는 깊이에서 복원한다(`g_InvViewProj` 를 PassCB 에 더했다) — G버퍼에 위치를
굽지 않는다(첨부 하나를 통째로 아낀다. 언리얼도 같은 선택이다).

> 회귀 방지는 **행렬 자체**로 한다 — `SceneTest.ShadowMatrixDepthRangeContainsScene` 이 원점이 깊이
> 구간 한가운데(0.5)로 가는지, 빛 방향으로 떨어진 두 점의 깊이가 뚜렷이 갈리는지 본다. 픽셀로
> 잡으려면 그림자를 받을 바닥이 있어야 하는데, 그 바닥이 없어서 이 버그가 여태 안 보였다.

#### 바닥 평면과 정점 노멀 (남은 것)

`MeshUtil::createPlane( segmentCount )` 을 더했다. 면이 로컬 `y = +0.5` 에 있다 — **이 엔진의 정점에는
노멀이 없다.** 셰이더가 위치로 노멀을 만드는데(`DemoCubeNormal`), `y = 0` 인 평면은 `|y|` 가 0 이라
±X/±Z 노멀을 받아 바닥이 옆을 보는 것처럼 칠해진다. 면을 큐브 윗면 자리에 두면 그 함수가 +Y 를
돌려준다. **진짜 해법은 `RHIVertex` 에 노멀을 넣는 것**이고, 그건 네 백엔드의 입력 레이아웃 ·
모든 셰이더 · 모프 풀 원소를 함께 건드리는 일이라 따로 잡아야 한다(1절에 적어 두었다).

#### 측정 (Release · DX12 · 큐브 2000개 · 120프레임)

| 파이프라인 | 라이트 0 | 16 | 64 | 256 |
|---|---|---|---|---|
| 포워드 `RT.Frame` | 900 | 902 | 1100 | **2111** |
| 디퍼드 `RT.Frame` | 2374 | 2417 | 2556 | **3013** |

라이트 0 → 256 에서 포워드는 **+134%**, 디퍼드는 **+27%** 다. 디퍼드가 화면 픽셀당 한 번만
셰이딩하는 반면 포워드는 오버드로만큼 거듭 셰이딩하기 때문이고, 이게 디퍼드를 쓰는 이유 그대로다.

**디퍼드의 고정 비용은 채움률이다.** 해상도만 줄여 재면 1280×720 = 2503us → 640×360 = 864us →
320×180 = 605us 다. 라이트 루프가 아니라 풀스크린 패스 여덟 개가 비용이다 — **타일/클러스터
라이트 컬링을 지금 넣는 것은 측정이 가리키는 자리가 아니다**(라이트 256개의 몫이 ~600us 인데
기본값이 2400us 다).

그래서 채움률 쪽을 하나 줄였다: 디퍼드의 `TransparentColor` 첨부를 없애고 Transparent 패스가
`LitColor` 에 바로 그리게 했다(포워드가 이미 그 모양이다 — 입력과 출력이 같은 이름이다).
그 첨부가 있어서 **매 프레임 풀스크린 블릿**(RGBA16F 1280×720 읽고 쓰기)이 하나 돌고 있었다.
`RT.Frame` 2455 → 2089 (**-15%**), 그림은 같다(평균 RGB 0.1 이내, 네 백엔드 모두).

#### 확인

- 그림자: 네 백엔드 × 포워드/디퍼드 8조합 모두 같은 그림(포워드 비배경 평균 194.8~195.0,
  디퍼드 119.1). 고치기 전 스크린샷과 나란히 두면 바닥의 그늘이 없다가 생긴다.
- 라이트: 포워드·디퍼드 모두 `-gv_benchLights` 로 그림이 바뀐다. 전체 ctest 5/5(nogpu) ·
  `RenderPassGpuTest` 20/20 · 린트 0건.
- 새 스위치: `-gv_benchLights=N` · `-gv_benchLightRadius=<유닛>` · `-gv_benchGround=1`.

### 2026-09-13 (디퍼드 파이프라인은 한 번도 안 그리고 있었다 — 고를 수가 없어서 세 겹으로 썩었다)

`-gv_deferred=1` 로 기본 파이프라인을 디퍼드로 고를 수 있다. **예전에는 고를 방법이 아예 없었다** —
`FrameRenderer::initialize` 의 파이프라인 인자를 주는 호출부가 하나도 없어서 `EngineLoop` 는 늘
`_defaultForwardPipeline` 로 초기화했다. 그 사이 디퍼드 경로가 조용히 세 겹으로 썩어 있었다.
셋 다 **오류도 경고도 없이** 화면만 틀렸다 — 그래서 그림을 봐야만 드러난다.

**(1) 풀스크린 패스가 컬링되고 있었다.** `createPsoForPassType` 의 "컬 모드 기본값 None" 이
`pPassDesc == nullptr` 일 때만 걸렸다. 즉 **XML 에 패스를 적어 둔 파이프라인은 컬 모드를 반드시
`None` 이라고 써야** 했고, 디퍼드 XML 은 열 패스 전부 `Back` 이라고 적고 있었다. Shading·SSAO·
Bloom·Outline·TAA·Tonemap·Present 일곱 패스가 삼각형을 통째로 잃어 화면이 배경색뿐이었다.
뎁스 테스트에 이미 같은 판단이 적혀 있었다 — "호출부의 값은 *이 패스가 지오메트리인가 풀스크린인가*
라는 구조적 사실이고 XML 은 그 안에서의 조정이다". 컬 모드도 같은 구조라 같은 모양으로 맞췄다
(`drawsSceneMeshes` 가 아니면 `None` 고정, XML 은 지오메트리 패스 안에서만 고른다).

**(2) G버퍼의 노멀 타깃이 클리어 값 그대로였다.** 머티리얼이 셰이더 경로를 정하므로
(`usesMaterialShader`) G버퍼 패스도 머티리얼의 `.hlsl`(전부 `forwardlit.hlsl`)로 그린다. 그런데
그 셰이더는 `SV_TARGET` **하나**만 냈다 — MRT 의 두 번째 타깃은 아무도 안 쓴 채 남았고, 디퍼드
조명은 화면 전체를 같은 노멀 `(0,0,1)` 로 계산하고 있었다. 언리얼이 같은 머티리얼을 패스별 셰이더
**타입**(`TBasePassPS` 대 G버퍼)으로 감싸는 자리다. 여기서는 패스가 define 을 얹고
(`SW_PASS_GBUFFER`, 정본은 `FrameRendererUtil::kPassGBufferDefine`) 출력 서명이 그 define 을
따라가게 했다 — `SW_SURFACE_OUTPUT` / `SwStoreSurface`(binding.hlsli 4절). 패스 PSO 의 define 은
`createMaterialPsoVariant` 가 desc 를 통째로 복사하므로 **머티리얼 변형까지 같이** 따라온다.
G버퍼 경로는 조명 계산이 통째로 컴파일 아웃된다(런타임 분기가 아니다).

> 여기서 한 번 물렸다: 포워드 쪽 출력을 `#define SW_SURFACE_OUTPUT float4` 로 뒀더니 반환
> 시맨틱이 사라져 DXC 가 `Semantic must be defined for all outputs` 로 거절했다. 머티리얼 셰이더가
> 통째로 컴파일되지 않아 **포워드가 네 백엔드 모두 빈 화면**이 됐다. 양쪽 다 구조체로 둔다.

**(3) 스크린샷이 디퍼드에서는 한 장도 안 찍혔다.** 기본 첨부가 `"SceneColor"` 리터럴인데 디퍼드
첨부 목록에 그 이름이 없다 — 읽기 실패 로그만 남고 파일은 안 생겼다. 찍고 싶은 것은 늘 "지금
화면에 보이는 것" 이므로 파이프라인에 물어본다(`getPresentedAttachmentName` = Present 패스의 입력).

**덤으로: HDR 첨부를 PPM 으로 덤프하면 무의미한 그림이 나왔다.** `R16G16B16A16_FLOAT` 를 8비트로
가정하고 `pPixel[0..2]` 를 집어 왔다 — 가수 하위 바이트가 색이 된다. `bytesPerPixel` 은 8 이라
"덤프할 수 없는 포맷" 검사도 통과했다. 디퍼드는 LitColor·TransparentColor·BloomColor·TaaColor 넷이
이 포맷이라 **중간 단계를 눈으로 확인할 길이 없었다**. half → unorm8 변환을 넣었다
(`FrameRendererUtil::halfToUnorm8`, 톤매핑 없이 자르기만 한다 — 이 덤프는 "무엇이 들어 있나" 를
보려는 것이다).

**검증** (큐브 200개, 프레임 25):

| 첨부 | 고치기 전 | 고친 뒤 |
|---|---|---|
| GBufferAlbedo | 38,223 (셰이딩된 색) | 38,419 (**알베도**) |
| GBufferNormal | **0** (클리어 그대로) | 38,402 |
| LitColor | **0** | 38,402 |
| 화면(TonemapColor) | **0** | 40,541 |

네 백엔드 모두 같은 그림이다 — DX12 40,547 / DX11 40,592 / Vulkan 40,572 / OpenGL 40,544
(스핀 타이밍 지터 범위). 포워드도 회귀 없음(47,049). 전체 ctest 5/5(nogpu) · 린트 0건.

**회귀 방지**: `RenderPassGpuTest.DeferredPipelineDrawsGeometry` 가 디퍼드로 초기화해 몇 프레임
돌린 뒤 **화면에 나간 첨부를 되읽어 고유 색이 둘 이상인지** 본다. 화면이 통째로 한 색이면
(= 아무것도 안 그렸다) 고유 색은 반드시 1 이다 — 비배경 픽셀 수와 달리 클리어 색·톤매핑에 안 무너진다.

### 2026-09-13 (GPU 가 정점을 바꾼다 — 언리얼·유니티를 확인하고 이 엔진의 결로 옮겼다)

`-gv_benchMeshMorph=1` 이면 컴퓨트(`meshmorph.hlsl`)가 레스트 포즈를 읽어 변형 결과를 쓰고, 정점
셰이더가 `SV_VertexID` 로 그 결과를 읽는다. **CPU 는 정점을 한 번도 다시 올리지 않는다.**

**CPU 로 하면 안 되는 이유.** `Mesh::setVertices` 는 `releaseVertexBuffer()` 를 먼저 부른다 — 정점을
매 프레임 바꾸면 **메시마다 GPU 버퍼를 파괴하고 다시 만든다.** 게다가 그 호출은 게임 스레드인데,
버퍼 생성·파괴를 아무 스레드에서나 해도 되는지는 백엔드가 정한다(OpenGL 은 `false`).

**상용 엔진 둘에서 가져온 것.** 언리얼 GPU Skin Cache — 결과를 **원본과 다른 버퍼**에 쓰고, 캐시가
차면 **일반 경로로 폴백**하고, 버퍼 하나를 할당해 섹션마다 나눠 쓴다. 유니티 `Mesh.GetVertexBuffer`
— **옵트인**해야 GPU 가 만질 수 있고(`vertexBufferTarget |= Raw`), 레이아웃을 **질의해 상수로 넘기며**,
GPU 쪽 변경은 **CPU 사본에 반영되지 않는다**. 넷 다 그대로 가져왔다(`Mesh::setGpuMorphEnabled`,
`GpuMeshMorphPool` 의 예산과 폴백, 풀 구간 나누기, stride 를 상수로).

**가져오지 않은 것은 "정점 스트림 교체" 다.** 둘 다 "정점 버퍼이면서 UAV" 를 요구하는데 이 엔진에서는
그게 가장 비싼 길이다 — `createBuffer` 가 `Vertex` 를 보면 `UnorderedAccess` 를 버리고(DX12·Vulkan 은
이 함수를 재정의하지 않는다), DX12 의 `createVertexBuffer` 는 UPLOAD 힙이라 UAV 가 될 수 없고, DX11 은
구조버퍼와 정점 버퍼를 겸할 수 없다. 대신 **정점 풀링**으로 갔다 — 결과를 구조버퍼에 두고 정점
셰이더가 인덱스로 읽는다. 이 엔진은 인스턴스·머티리얼·가시 목록이 이미 전부 그 모양이고,
`RHIStructuredBufferSlot` 이 SRV·UAV 등록과 3단 폴백까지 네 백엔드에서 이미 돌고 있다 —
**백엔드 분기가 한 줄도 없다.**

**계약에 더한 것**: SRV 슬롯 `t11`(`g_SwMorphVertices`), PassCB 둘(`g_SwMorphVerticesIndex` ·
`g_SwMorphVertexCount`), 루트 상수 하나(`g_MorphVertexBase`). 풀 SRV 는 **패스당 한 번** 걸리고 배치는
시작 오프셋만 싣는다 — "드로우 사이에 바인딩이 바뀌지 않는다" 는 규약을 깨지 않는다.
`forwardlit` · `shadowdepth` · `gbuffer` 셋이 같은 헬퍼를 쓴다(그림자를 빼면 물체와 그림자 모양이 어긋난다).

**정렬에서 한 번 물렸다.** 풀 원소를 `RHIVertex`(float3+float4 = 28바이트) 그대로 뒀더니 **GL 만**
기하가 무너졌다 — std430 은 vec4 를 16 바이트 경계에 맞추는데 DX/Vulkan 은 DXC 가 명시 오프셋을 적어
넘어간다. 원소를 float4 둘(32바이트)로 맞췄다. 풀 원소는 엔진 내부 형식이라 `RHIVertex` 와 같을 이유가 없다.

**GL 은 아직 꺼 두었다(`_bGpuMeshMorph = SW_FALSE`).** 정렬을 고쳐도 GL 만 그림이 다르다. 확인한 것:
(1) 모프 수식이 아니라 **데이터**다 — 컴퓨트가 레스트를 그대로 쓰게 해도(항등) GL 만 다르다.
(2) 정렬도 아니다 — 고치니 비배경 픽셀 15,829 → 30,551 로 좋아졌지만 기대값 42,300 에 못 미친다.
(3) 폴백을 타는 것도 아니다 — 폴백이면 모프 끈 그림과 같아야 하는데 다르다.
남은 의심은 정점 스테이지의 SSBO 바인딩(t11 이 이 스테이지의 **네 번째** SSBO 다)과 `ARB_gl_spirv` 의
`gl_VertexID` 의미다. **원인을 찾기 전에는 켜지 않는다** — 백엔드 하나만 조용히 다른 그림을 내는 것이
이 저장소에서 가장 비싼 버그였다. 지금은 GL 이 레스트 포즈로 깨끗이 폴백한다(아래 표).

**검증** (큐브·구·실린더·캡슐·원뿔 100개, 같은 프레임 25):

| 백엔드 | 모프 끔 | 모프 켬 | 다른 픽셀 |
|---|---|---|---|
| DX12 | 50914 | 52808 | 16119 |
| DX11 | 50662 | 52831 | 15014 |
| Vulkan | 50889 | 52895 | 15531 |
| OpenGL | 50682 | 50685 | **187** (폴백 — 스핀 타이밍 지터뿐) |

그 밖에: 전체 ctest 13/13(`ShaderBindingContractTest` 6/6 포함 — 계약을 바꿨으므로 재베이크했다) ·
린트 7/7 · `BackendSmoke` 8/8 오류 0(모프 기본 꺼짐이라 평균 RGB 불변) · 4백엔드 오류 0.

**덤으로 잡은 것**: `EngineData::_shaderMeshMorph` 에 `PROPERTY()` 를 빼먹어 XML 의 그 속성이 "모르는
필드" 가 되고 **매 실행 경고**가 났다. 리플렉션 매크로가 없으면 값은 기본값으로 조용히 돌아간다.

### 2026-09-13 (머티리얼 스트레스를 넣었더니 퍼뮤테이션 경로가 네 겹으로 죽어 있었다)

**벤치가 머티리얼을 한 번도 흔들지 않고 있었다.** 값이 시작할 때 한 번 올라간 뒤 영원히 그대로라
"바뀐 것만 올린다" 경로도, 원소 표의 회수·재사용도, 퍼뮤테이션 교체도 한 번도 지나지 않았다.
스위치 셋을 넣어 매 프레임 흔들게 했다:

| 스위치 | 무엇을 흔드나 |
|---|---|
| `-gv_benchMaterialChurn=N` | **값** — 색·러프니스를 무작위로. 인스턴스 CB 재작성 · 구조버퍼 재업로드 |
| `-gv_benchMaterialChurnAdd=N` | **집합** — 인스턴스를 붙였다 뗀다. 원소 자리 할당·회수·재사용 · 소유 해제 |
| `-gv_benchMaterialChurnKeyword=N` | **퍼뮤테이션** — 키워드·멀티컴파일. 배치 키 · PSO |

난수는 고정 시드 xorshift 다 — 같은 프레임 수를 돌리면 같은 순서가 나와야 비교가 된다.

**그리고 세 번째 스위치가 아무 일도 하지 않았다.** 인스턴스의 해시는 분명히 바뀌는데
(`12813330958780597584 → 11774003297266822238` 확인) 배치 수가 1 도 움직이지 않았다. 파 보니 **네 겹**이었다:

1. **배치 키가 퍼뮤테이션 해시 대신 "대표 머티리얼 포인터" 를 썼다.** 합치기가 켜지면 대표는
   `퍼뮤테이션 해시 → 처음 그 해시를 들고 온 머티리얼` 인데, 퍼뮤테이션이 **인스턴스**에서 오면
   부모가 같아 대표도 같아진다 — 서로 다른 셰이더로 그려야 할 것이 한 배치로 접힌다.
   해시를 `SortKey` 에 직접 넣었다.
2. **증분 경로가 퍼뮤테이션을 보지 않았다.** `DrawCandidate::hasSameBatchKey` 가 메시·머티리얼·
   인스턴스·블렌드만 비교해서, 포인터가 그대로면 "배치 구성 그대로" 로 판정하고 다시 나누지 않았다.
   후보가 해시를 들고 다니게 하고(수집에서 한 번 구한다) 비교에 넣었다.
3. **정지한 씬은 수집 자체를 건너뛴다.** 머티리얼을 바꿔도 프리미티브는 더러워지지 않으므로
   "등록·해제도 없고 움직인 것도 없다" 에 걸린다. `MaterialUtil::getPermutationGeneration()` 을 두고
   define 이 달라질 수 있는 **공개 설정자**에서만 올린다(캐시 재계산 함수에서 올리면 순환이다 —
   정지한 씬은 애초에 묻지 않는다).
4. **배치가 등록하는 퍼뮤테이션을 키에서 뽑았다.** 합치기가 켜지면 키의 인스턴스는 nullptr 이라
   대표 머티리얼의 define 만 실리고 인스턴스가 켠 키워드가 통째로 빠진다 — 그 배치는 **잘못된
   셰이더로 그려진다.** 배치에 실제로 든 후보(`headCand`)에서 뽑게 했다.

고친 뒤 같은 명령의 배치 수가 **184.0 → 249.3** 이 됐다.
`GpuSceneTest.InstancePermutationChangeRebuildsBatches` 가 이 자리를 고정한다 — 부모가 같고,
인스턴스만 다르고, **첫 빌드 뒤에** 바꾸는 세 조건이 겹쳐야 재현된다.

---

**도형을 다섯으로 늘렸다.** 큐브 하나만으로는 삼각형이 12개뿐이고 면이 축에 정렬돼 있어 래스터화·
보간·컬링을 거의 흔들지 않는다. `-gv_benchMeshShapes=N`(기본 1) 으로 큐브 · 구 · 실린더 · 캡슐 ·
원뿔을 섞는다. 기본이 1 인 이유는 **기존 측정·스크린샷을 그대로 두기 위해서**다.

**생성기는 `Mesh` 가 아니라 `MeshUtil` 에 있다.** `Mesh` 가 책임지는 것은 정점 버퍼와 그 수명이고,
어떤 기하를 만들지는 다른 일이다(`MaterialUtil` 이 `Material` 옆에 따로 있는 것과 같은 자리).
`Mesh::create()` 가 빈 메시를 만들고 `MeshUtil` 이 채운다 — 생성 열쇠 규칙은 그대로다.

**감김은 손으로 맞추지 않는다.** 처음 판에서 구·실린더·원뿔이 뒤집혀 있었다(구는 **전부**).
뒤집힌 면은 후면 컬링에 걸려 **화면에서 그냥 사라지고**, 그림으로는 "안 그려진다" 로만 보여
렌더러 버그로 오인하기 쉽다. `pushOutwardTriangle` 이 면 법선과 삼각형 중심의 부호를 보고 바로잡는다
(원점 중심 볼록 도형에서만 맞는 판정이라 주석에 못박았다). `MeshPrimitiveTest.PrimitivesAreClosedAndOutwardFacing`
이 다섯 도형의 크기·퇴화·감김을 고정한다.

**검증.** 전체 ctest 13/13 · 린트 7/7 · `BackendSmoke` 8/8 오류 0(평균 RGB 불변) ·
4백엔드 × (도형 5종 + 값·집합·퍼뮤테이션 스트레스 동시) 종료 0 · 오류 0.

### 2026-09-13 (프로파일러가 렌더 스레드 샘플을 버리고 있었다 — 그리고 게임 스레드 복사 세 자리)

**중첩된 구간의 합이 바깥 구간보다 큰 표가 나왔다.** `RT.GpuScene.upload 2628us` + `submitGraph 2703us`
> `RT.Frame 4271us`. 둘 다 `RT.Frame` 안에 있는데 합이 더 크다 — 표가 틀렸다는 뜻이다.

**원인: 창을 여는 쪽과 채우는 쪽이 다른 스레드였다.** `FrameProfiler::beginFrame` 이 누적을 **0 으로
지우고** `endFrame` 이 그 값을 읽어 접었다. 둘 다 게임 스레드가 부른다. 렌더 스레드는 자기 박자로
도니까 `endFrame` 과 다음 `beginFrame` 사이에 더한 샘플은 **통째로 버려진다.** 두 스레드의 박자가
거의 고정이라 **매 프레임 같은 구간들**이 그 틈에 걸렸고, 그 구간만 골라 표에서 사라지거나
(`sampledFrames=0`) 평균이 부풀었다.

고친 방식은 한 줄이다 — `endFrame` 이 `exchange` 로 **읽으면서 0 으로 바꾼다.** 창이 닫힌 뒤 더해진
샘플은 버려지지 않고 다음 창으로 넘어간다. `beginFrame` 은 이제 창이 열렸다는 표시일 뿐이다.

고친 뒤 표가 달라졌다 — `RT.Pass.execute` 는 프레임당 4.0 회가 아니라 **6.0 회**였고,
`RT.Draw.gpuBatches` 는 1.0 이 아니라 **3.0** 이었다. 그동안 렌더 스레드 숫자를 **적게** 보고 있었다.

> **주의: 이 날 이전에 적힌 렌더 스레드 수치는 이 버그 위에서 잰 것이다.** `Graphics/README.md` 의
> 드로우 경로 최적화 표(2026-09-08)를 포함해, 전후 비교로서는 유효하지만 절대값은 실제보다 작다.

---

**그리고 게임 스레드에서 값을 두 번 나르던 자리 셋.** 프로파일러를 고치자 게임 스레드가 프레임의
임계 경로가 되어(`GT.Packet.submit` 8us = 렌더 스레드를 기다리지 않는다) 어디가 비싼지 보였다.

1. **`vector` 의 복사가 원소 루프였다.** 복사에도 소멸에도 사용자 코드가 없는 타입(`is_bitwise_copyable_v`)
   이면 `Memory::copy` 한 번으로 옮긴다 — 복사 생성·복사 대입·**성장(`reserveInternal`)** 셋 다.
   렌더 패킷이 매 프레임 `GpuInstance`(96바이트) 20,000 개를 나르는 자리가 이 경로였다.
   `GT.Packet.export` 멤버별 실측: 인스턴스 329us · 배치 137us · 머티리얼 그룹 2us — **인스턴스가 전부**였다
   (머티리얼 그룹의 `unordered_map` 이 비쌀 거라 짐작했는데 원소가 4개뿐이라 2us 였다. 짐작이 틀렸다).
2. **수집이 후보를 만들고 또 복사했다.** `_listScratchCandidate.push_back( cand )` — `cand` 는 그 자리에서
   죽는데 복사였다. 프리미티브마다 `shared_ptr` 셋의 참조 카운트를 올렸다 내렸다.
3. **배치를 두 목록에 복사로 넣었다.** `_listOpaqueBatch` 와 `_listAllBatch` 에 같은 배치를 두 번 복사한다.
   앞쪽은 복사가 맞지만 **마지막 하나는 옮길 수 있다**(투명 쪽도 같다).

측정 (DX12 Release, 큐브 20,000 · 메시 종류 400, 3회씩 A/B — 프로파일러 수정은 양쪽에 다 들어간 상태):

| 구간 | 이전 | 이후 |
|------|------|------|
| `GT.Frame` | 4534 / 4590 / 4619us | 4203 / 4161 / 4294us |
| `GT.GpuScene.build` | 2327us | 2132us |
| `GT.Packet.export` | 484us | 372us |

**큐브 2000 에서는 프레임 시간이 안 변한다** — 그 워크로드는 렌더 스레드가 임계 경로라
(`GT.Packet.submit` 474us = 게임 스레드가 그만큼 기다린다) 게임 스레드를 줄여도 프레임은 그대로다.
게임 스레드 이득은 오브젝트가 많아질수록 나온다.

`Core_Vector.BitwiseCopyKeepsValues` 와 `Core_Vector.NonTrivialCopyKeepsLifetimeBalance` 가 두 경로를
고정한다(빠른 경로의 값 보존 · 루프 경로의 생성자·소멸자 짝). **빠른 경로를 일부러 한 원소 덜 복사하게
바꿔 보니 `ReflectionParser` 가 `--builtins` 파일을 못 읽어 빌드가 통째로 섰다** — 도구 자신이 `Core` 를
링크하기 때문이다. 이 경로가 얼마나 넓게 쓰이는지를 보여 준다.

**남은 것 (재 보고 고를 것).** 큐브 20,000 기준 게임 스레드 `GT.GpuScene.build.batches` 1047us ·
`GT.Scene.tick` 1685us, 렌더 스레드 `RT.Draw.gpuBatches` 2386us(프레임당 5,700 배치 = 배치당 ~0.42us).
렌더 쪽은 `Graphics/README.md` 가 적어 둔 대로 **통합 정점 버퍼 풀**이 다음 구조 변경이다.

**검증.** Debug · Release · Shipping 빌드 종료 0 · `RunBuildWarnings` 세 구성 경고 0 · 전체 ctest 13/13 ·
ASAN nogpu 5/5 · 린트 7/7 · `BackendSmoke` 8/8 오류 0(평균 RGB 불변).

### 2026-09-13 (VSync 가 한 번도 걸린 적이 없었다 — 그리고 그 벽 뒤에 게임 스레드의 38%가 숨어 있었다)

**발단은 "프레임이 정확히 6061us 다" 였다.** 1/6061us = **165.0Hz**, 이 PC 모니터 주사율과 같다.
`Config/Engine/EngineConfig.json` 은 `"_bVSync": false` 인데도 그랬다.

**뿌리 셋.**

1. **`RenderThread::executeFrameBody` 가 `endFrame( true )` 를 못박고 있었다.** 설정값은
   `EngineLoop` → `RHI::setPreferredVSync` → `RHISwapChainDesc::_bVSync` 까지 성실히 흘러간 다음
   **아무도 읽지 않았다.** `_bVSync` 는 채워지기만 하는 필드였다(전수 확인 — 소비자 0). CLI `--VSYNC`
   도 같은 자리에서 죽었다. 이제 `IRHIDevice` 가 채택값을 들고(`isVSyncEnabled`), 프레젠트 경로가
   그걸 읽는다.
2. **`Present( 0, 0 )` 만으로는 VSync 가 안 꺼진다.** 플립 모델이어도 창이 DWM 합성을 거치는 동안에는
   런타임이 vblank 에 맞춰 넘겨 준다. 1번만 고쳤을 때 **포그라운드 여부에 따라 붙었다 안 붙었다** 해서
   같은 명령이 947us 와 6061us 를 오갔다(3회 재실행으로 확인). 실제로 끄려면 스왑체인을
   `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` 으로 만들고(**ResizeBuffers 에도 같은 플래그**)
   `Present( 0, DXGI_PRESENT_ALLOW_TEARING )` 이어야 한다 — 짝이 안 맞으면 DXGI 가 `INVALID_CALL` 이다.
   지원 질의는 DX11·DX12 공유 헤더 `RHI/DX/RHIDxgiTearing.h` 하나가 갖는다.
3. **Vulkan 은 `endFrame( vsync )` 인자를 `(void)` 로 버리고 스왑체인이 `FIFO` 를 못박고 있었다.**
   Vulkan 에는 present 호출의 동기화 인자가 없고 **present 모드**가 그 자리다. 이제 끈 경우에만
   서피스 목록에서 `MAILBOX` → `IMMEDIATE` 순으로 고르고, 둘 다 없으면 FIFO 로 남기고 로그를 남긴다.

**`-vsync` 는 오타가 아니라 없는 이름이었다.** 다른 스위치는 전부 소문자 별칭이 있는데(`dx11`·`vk`·
`EnableEditor`) `VSYNC` 만 없어서 `-vsync` 가 "해당 Argument는 없습니다" 로 무시됐다. 별칭을 넣었다.

**결과 (DX12 Release, 큐브 2000 · 메시 종류 200):**

| 구간 | 이전 | 이후 |
|------|------|------|
| `GT.Frame` | 5838us | 917us |
| `GT.Packet.submit`(GT 가 RT 를 기다린 시간) | 4906us | 508us |
| `RT.Frame` | 6061us | 1058us |
| `RT.Present` | 4845us | 97us |

---

**계측이 렌더 경로만 보고 있었다.** 보고서에 `GT.GpuScene.build` 는 있는데 **프레임 전체가 없어서**
"이게 프레임의 몇 %인가" 에 답할 수 없었다. 구간 6개를 넣었다 — `GT.Frame` · `GT.Scene.tick` ·
`GT.Packet.export` · `GT.Packet.submit` · `RT.Frame` · `RT.Present`. 쓰는 법은 두 뺄셈이다:

- `GT.Packet.submit` 이 크면 **게임 스레드가 렌더 스레드를 기다리고 있다**(링이 차면 submit 이 막는다).
- `RT.Frame - RT.Present` 가 기록 시간, `RT.Present` 가 제출·표시 대기다. 위 표의 이전 열이 딱
  "전부 Present 에서 기다리는 중" 이었다.

**넣자마자 답이 나왔다.** 큐브 20,000 개에서 `GT.Frame` 7659us 중 **3016us 가 어느 구간에도 안 잡혔다.**
쪼개 보니 한 줄이었다.

---

**`Scene::findActiveDirectionalLight` 가 매 프레임 씬 전체를 훑고 있었다.** `EngineLoop::tick` 이 프레임마다
부르는데, 구현은 **모든 GameObject** 를 돌며 `getComponent<DirectionalLightComponent>()` 를 묻고,
찾은 뒤에도 멈추지 않았다 — `forEachGameObject` 에는 중단이 없다. 큐브 20,000 개에서 이 한 줄이
**2.9ms**(게임 스레드 프레임의 38%)였다. **빛은 하나였다.**

고친 방식은 이 저장소가 이미 쓰던 것이다 — `PrimitiveRegistry` 의 "찾지 말고 등록받는다". 새
`LightRegistry`(`Source/Engine/Object/GameObject/`)를 만들고 `DirectionalLightComponent` 가
`onRegister`/`onUnregister` 에서 자기를 등록한다. 비용이 "오브젝트 수" 에서 "빛의 수" 가 됐다.

- 등록부를 프리미티브 쪽에 합치지 않았다. 그 목록은 "그릴 수 있는 것만 들어 있다 — 타입 검사가 없다"
  는 계약이라(`GpuScene::buildFromScene`) 빛을 섞으면 그 계약이 깨진다.
- 더티 표시도 인덱스도 두지 않았다. 빛은 몇 개뿐이라 제거가 선형 탐색으로 충분하고, 렌더러는 매 프레임
  값을 새로 읽는다. 점광·스포트가 생기는 날 프리미티브 쪽 구조를 따라가면 된다.
- 원시 포인터가 안전한 근거: 컴포넌트는 `PoolAllocator` 에 placement-new 되고 등록 뒤 **주소가 움직이지
  않는다**(`PrimitiveRegistry` 가 `MeshComponent*` 를 그대로 드는 근거와 같다).
- `SceneTest.DirectionalLightLookupFollowsRegistry`(nogpu)가 없음·있음·컴포넌트 비활성·오브젝트 비활성·
  파괴 다섯 상태를 고정한다. 등록부에 죽은 포인터가 남으면 마지막 단언에서 잡힌다.

**결과 (DX12 Release, 큐브 20,000 · 메시 종류 400):** `GT.Frame` **7659us → 4688us**. 미계측 구간은
3016us → 18us 로 사라졌다 — 이제 게임 스레드가 전부 설명된다.

---

**빌드 경고가 다섯 종류 쌓여 있었다.** 지금까지의 검증 기준이 "빌드 종료 0" 이었는데, 경고는 종료
코드를 바꾸지 않는다. Debug · Release · Shipping 셋을 각각 훑어 전부 없앴고, 지금은 **셋 다 0건**이다.

| 경고 | 정체 |
|---|---|
| `-Wunused-variable` ×2 | `ActionMapEvaluate.cpp` 의 `curMouseX`/`curMouseY` — `int2` 전환 때 남은 죽은 지역변수 |
| `-Wduplicate-decl-specifier` ×2 | `LiveReloadManager.cpp` 의 `inline static inline static uint32` — 붙여넣기 오타 |
| `-Wunused-function` (Release) | DX11 `isHazardMessage` 가 `SW_DEBUG` 밖에 있었다 — 쓰는 쪽은 안이다 |
| `-Wdocumentation` ×6 | `ModuleHost.h` 의 `initialize` 문서 주석이 중간에 끼어든 함수 때문에 떨어져 나갔다 |
| `-Winconsistent-missing-destructor-override` | `~LiveReloadManager` — 붙이자 그 뒤에 `IModuleHandleProvider` 의 암시적 복사 deprecated 가 드러나 복사·이동을 명시적으로 막았다 |
| `-Wunused-but-set-variable` / `-Wunused-private-field` (Shipping·Test) | `TestInput.cpp` 의 `smoothDy`, 그리고 **소비자가 로그뿐**이라 Shipping 에서 죽는 둘 — `ModuleCompiler::_pLiveReloadManager`, `ReloadFileManager` 의 `found` |

**그런데 "종료 코드" 는 원인이 아니었다.** 빌드 로그를 한 줄도 빠짐없이 읽어도 그 경고들은 보이지
않는다 — **경고는 그 TU 가 컴파일되는 순간에만 출력되고, ninja 는 바뀌지 않은 파일을 다시 컴파일하지
않는다.** 오늘 들어온 경고는 한 번 지나가고 그 뒤로는 영원히 안 보인다. 실제로 확인했다: 일부러
`unused variable` 하나를 넣고 빌드하면 한 번 나오고, **바로 다시 빌드하면 0건**이다.

그래서 진짜 문제는 "경고를 흘려봤다" 가 아니라 **"지금 트리에 경고가 몇 개인지 물어볼 방법이
없었다"** 였다. clean 빌드를 하면 답이 나오지만 아무도 매번 clean 빌드를 하지 않는다.

**게이트 대신 질문할 창구를 만들었다 — `Scripts/lint/report/RunBuildWarnings.py`.** 컴파일 DB 의 **실제 빌드
명령 그대로**, 코드 생성 없이(`-fsyntax-only`) 전 TU 를 훑는다. 무엇이 dirty 인지와 무관하게 늘 같은
답이 나온다. 프리셋당 약 1분 30초(TU 470개, 병렬 16).

```powershell
py -3 Scripts/lint/report/RunBuildWarnings.py                       # Debug · Release · Shipping 전부 (기본)
py -3 Scripts/lint/report/RunBuildWarnings.py --preset Ninja-Debug --filter Graphics
```

- **기본이 세 구성인 이유**: 경고 집합이 구성마다 다르다. `-Wunused-function` 은 Release 에만,
  `-Wunused-private-field` 는 Shipping 에만 나온다 — 로그 매크로가 컴파일에서 빠지면서 소비자가
  사라지는 자리들이다. 한 구성만 보면 그만큼을 놓친다.
- **PCH 를 끄고 훑는다**(`/Y-`). `/Yu` 로 미리 파싱된 헤더를 불러오면 그 헤더들의 경고가 다시 나오지
  않는다 — 경고가 숨는 것을 없애려는 도구가 같은 방식으로 숨으면 안 된다.
- **`Run*` 은 보고하고 `Check*` 이 막는다**(`RunClangTidy.py` 와 같은 규칙). 경고가 있어도 0 을
  돌려준다. `-Werror` 를 걸지 않은 이유는 **구성·컴파일러 버전마다 집합이 다르기** 때문이다 —
  CI 는 윈도우(저장소가 고정한 `Tools/LLVM`)와 리눅스(배포판 clang)를 함께 돌리므로, 막아 두면
  리눅스 clang 이 올라가는 날 새 경고 하나로 빨개진다. clang-tidy 가 "0건과 72건" 을 오간 것과
  같은 문제다(1-1). **막는 대신 보이게 만든다.**

**검증.** Debug · Release · Shipping 빌드 종료 0 · **`RunBuildWarnings` 세 구성 경고 0** ·
nogpu 5/5 · 전체 ctest 13/13 ·
린트 7/7 · `BackendSmoke` 4백엔드 × opaque/transparent 8/8 오류 0(평균 RGB 불변) ·
VSync 켜고 끄기 4백엔드 8구성 오류 0 · 창 리사이즈 4백엔드 오류 0(스크린샷이 684×481 로 실제로 따라온다) ·
교체 5구성 종료 0 · 오류 0 · 에디터 창 15 · 빈 패널 0.

### 2026-09-13 (본문이 여러 문장인 case 에 중괄호를 강제한다 — 그리고 #if 가 끼면 손대지 않는다)

`case` 본문이 여러 줄인데 중괄호가 없는 자리를 강제하기로 했다. **206곳**에 중괄호가 붙었다(39개 파일).

**`break;` 를 한 문장으로 세기로 했다.** 이것이 유일한 갈림길이었다 — `문장 하나 + break;` 두 줄짜리가
54곳 있었고, 이걸 "한 줄짜리"로 볼지가 결과를 가른다. 세기로 한 근거는 **저장소 자신**이다: 이미
중괄호가 있던 19곳이 **예외 없이** `break;` 를 중괄호 안에 두고 있었다. 이 저장소에서 break 는 본문의
일부지 라벨의 종결자가 아니다. 규칙도 이쪽이 단순하다 — "본문이 한 문장이 아니면 중괄호", 린트에
break 예외 조항이 없다. (다르게 보고 싶으면 `_countCaseStatementsInternal` 에서 `break;` 를 빼면 된다.
그러면 54곳이 중괄호를 벗는다.)

**라벨 1164개를 전수로 센 분포.** 같은 줄 본문 559 · 본문 1줄 415 · 폴스루 라벨 79 · `문장+break;` 54 ·
본문 2문장 이상 38 · 이미 중괄호 19. 그대로 두는 쪽이 84% 다 — 면제가 규칙보다 훨씬 크다.

**린트는 `FormatBranchBraces.py` 가 맡는다.** 새 스크립트를 만들지 않았다. 그 파일이 이미 "중괄호의
모양"을 소유하고 있고(한 줄짜리 `if` 본문의 중괄호를 **벗긴다**), 문자열·주석 마스킹과 `--check` 와
clang-format 앞단이라는 실행 순서를 전부 갖추고 있었다. clang-format 은 이 규칙도 표현하지 못한다 —
`InsertBraces` 는 if/for/while 만 보고 case 라벨은 아예 건드리지 않는다. `processFile` 에 끼워 넣어서
`FormatModified` · `PreCommitLint` · `RunClangFormat` 세 호출처가 자동으로 따라왔다.

**순서가 중요하다.** `if` 중괄호를 먼저 벗기고 나서 case 문장을 센다. 반대로 하면 곧 벗겨질 중괄호가
문장 수를 부풀려, 벗기고 나면 한 문장이 될 본문에 중괄호를 씌운다.

**리눅스 빌드가 진짜 버그를 잡았다.** 윈도우 3구성이 전부 초록인데 WSL 에서 5개 오류가 났다.
`RHIBackendRegistry.cpp` 의 이 모양이다:

```cpp
switch ( backend )
{
#if defined( SW_PLATFORM_WINDOWS )
    case RHIBackend::DirectX11:  return tryLoadBackendModule( ... );
    case RHIBackend::DirectX12:  return tryLoadBackendModule( ... );
#endif
    case RHIBackend::OpenGL:     ...
```

탐지기가 `#endif` 를 **문장으로 세서** DirectX12 본문을 2문장으로 판정하고, 여는 중괄호는 `#if` 안에,
닫는 중괄호는 `#endif` **밖에** 놓았다. 윈도우에선 `#if` 가 켜져 짝이 맞아 조용히 컴파일됐고, 리눅스에선
`{` 와 본문이 통째로 사라져 `}` 만 남았다. **본문에 전처리기 지시문이 있으면 손대지 않는다**로 고쳤다 —
본문의 끝이 글자만으로 정해지지 않는 자리다. 이 가드로 대상이 40파일 210곳에서 39파일 206곳이 됐다.

교훈은 두 가지다. 하나, **플랫폼 전용 파일이 바뀌면 그 플랫폼에서 빌드해야 한다** — 리눅스 전용 파일 3개가
섞여 있었고 윈도우 컴파일러는 그 파일들을 아예 읽지 않는다. 둘, `#if` 가 든 코드에 텍스트 변환을 걸 때
**한쪽 구성만 초록인 것은 검증이 아니다**.

**덤 — 줄 끝이 섞였다.** 처음 판은 끼워 넣는 줄에 `\r` 을 안 붙여 CRLF 파일 안에 LF 줄이 생겼다. 파일의
우세한 줄 끝을 따라가게 고쳤다. (작업 트리 자체가 이미 CRLF 522 / LF 247 로 섞여 있다 — autocrlf=true 라
저장소에는 LF 로 들어가므로 파일 **사이**의 불일치는 원래 그런 것이고, 파일 **안**을 섞지 않는 것이 목표다.)

**검증.** Debug · Release · Shipping · **WSL-Debug** 빌드 종료 0 · 윈도우 nogpu 5/5 · 리눅스 nogpu 5/5 ·
린트 7/7 · GPU 43/43(RHI 13 · RenderPassGpu 19 · ShaderCompiler 6 · LiveShader 3 · Window 2) ·
교체 5구성 종료 0 · 오류 0 · 에디터 전부 열기 창 30 · 빈 패널 0 · 오류 0.
추가된 줄은 **전부 `{` 아니면 `}`** 이고 C++ 삭제 줄은 0 이다 — 순수하게 중괄호만 붙었다.

### 2026-09-13 (int2 를 만든다 — 언리얼의 FIntPoint 자리, 그리고 산술이 있는 곳만 옮긴다)

앞 항목에서 정수 쌍 20묶음을 "Math 에 `int2` 가 없어서" 남겨 뒀다. 그때 "소비자 없는 타입이 하나 는다" 고
적었는데 **틀렸다** — 소비자가 이미 20군데 있었다.

**언리얼은 어떻게 하나.** C++ 쪽에 `int2` 라는 이름은 없고 `FIntPoint`(2D int32) · `FIntVector`(3D) ·
`FIntRect` 가 그 자리다. 해상도 · 타일 좌표 · 뷰포트 크기에 압도적으로 쓰인다. `int2`/`uint2` 라는 철자는
HLSL 내장 타입이고 언리얼 **셰이더** 코드가 쓴다. 우리는 `FVector2D` 가 아니라 `float2` 로 이름 지었으니
HLSL 철자를 따르고 있고, 그래서 `int2` 가 이름까지 일관된다.

**`int2` 만 만들었다.** `uint2` 는 후보 20묶음이 전부 `int32` 라 소비자가 0 이고, `int3` 는 유일한 후보였던
`PhysicsWorld::CellCoord` 가 **공간 해시의 키**라 벡터 연산을 하지 않고 전용 해시 함자를 단다 — 벡터 타입으로
바꿀 이유가 없다. 타입 자체도 **일부러 얇다**: 정규화 · 길이 · 보간 · 행렬 변환은 정수 좌표에서 뜻이 없거나
실수로 나가야 하는 연산이라 넣지 않았다. `min`/`max`/`toFloat2` 와 `+ - == !=` 만 있다.

**옮긴 기준은 "산술이 있는가".**

| 옮김 | 근거 |
| --- | --- |
| `MouseDevice::_mouse` · `_prevMouse` · `_delta` | `_delta = _mouse - _prevMouse` 가 한 줄이 된다 |
| `ActionMap::_lastPress` | 더블클릭 거리 판정이 `curMousePos - _lastPress` 가 된다 |
| `PlayerController::_tile` · `_pendingWarpSpawn` | `_tile._x + deltaX` 같은 타일 이동 |
| `ZoneRuntime::ZoneBounds` | 산술은 없지만 **정수판 AABB** 다 — 실수판 `AABB2D` 를 방금 `float2 _min/_max` 로 바꿔 놔서 둘이 어긋나 있었다 |

**안 옮긴 것 11묶음은 전부 그냥 나르기만 한다** — `TileMapXml` · `OverworldEvents` · `TileMapPanel` · `TileMap` 의
`_spawnX/Y` · `_targetTileX/Y` 같은 것들로, 읽어서 담고 넘기는 레코드다. 바꿔도 얻는 것이 없다.
`TileMap::isWalkable( x, y )` 류의 질의 API 도 그대로 뒀다 — 구현이 x · y 루프로 도는 자리라 `int2` 로 받으면
오히려 풀어 쓰게 된다. `PlayerController` 가 `_tile._x, _tile._y` 로 넘기는 이음매가 남지만, 그쪽이 맞다.

**API 도 같이 바꿨다** (float2 때와 같은 이유로 저장만 바꾸면 반쪽이다):
`MouseDevice::getPosition` · `getDelta`, `InputManager::getMousePosition` · `getMouseDelta` 가 출력 인자 두 개
대신 `int2` 를 돌려준다. 출력 인자 판은 지웠다.

**덤 — 린트가 산문을 코드로 읽고 있었다.** `ZoneBounds` 에 "실수판 `AABB2D` 가 같은 모양이다" 라는 주석을
달았더니 `Style/BasicTypeAlias` 가 **주석 안의 "float"** 를 타입 사용으로 신고했다. 문자열 리터럴은 지우면서
주석은 안 지우고 있었다. 주석도 지우게 고쳤고, 진짜 `float` 멤버는 여전히 잡히는 것을 확인했다.

**검증.** Debug · Release · Shipping 빌드 종료 0 · nogpu 5/5 · 린트 7/7 · GPU 19/19 ·
입력 스위트(Mouse 1 · Gamepad 3 · InputManager 19 · ActionMap 17 · EdgeCase 2 · Stress 2).
에디터 전부 열기 창 30 · 빈 패널 0 · 오류 0. 교체 5구성 종료 0 · 오류 0.

### 2026-09-13 (축을 나눠 들던 스칼라 쌍을 float2 로 — 저장만이 아니라 API 까지)

`float32 _accumulatedRawDx; float32 _accumulatedRawDy;` 처럼 **축만 다른 스칼라 쌍**을 전수로 찾았다.
헤더의 연속 멤버 중 접미사(`X/Y/Z/W`, `Dx/Dy`)만 다른 묶음을 세니 **41개**였다.

**바꾼 것 (float32 묶음은 Math 타입 자신을 빼면 전부 처리했다).**

| 자리 | 전 | 후 |
| --- | --- | --- |
| `MouseDevice` | `_rawDeltaX/Y` · `_smoothDeltaX/Y` | `float2 _rawDelta` · `_smoothDelta` |
| `GamepadDevice` | `_leftStickX/Y` · `_rightStickX/Y` | `float2 _leftStick` · `_rightStick` |
| `RawInputEvent` | `_rawDeltaX/Y` | `float2 _rawDelta` |
| `AABB2D` | `_minX/_minY/_maxX/_maxY` | `float2 _min` · `_max` |
| `AnimationGraphNode` · `DialogueAssetNode` | `_x/_y` | `float2 _position` |
| `EditorSpriteClipKey` | `_x/_y` | `float2 _position` |
| `ActionRoom` | `_playerX/Y` · `_x/_y` · `_vx/_vy` | `float2 _playerPos` · `_position` · `_velocity` |
| `InputMapEditorPanel` | `_simStickX/Y` | `float2 _simStick` |

**저장만 바꾸면 반쪽이다 — API 도 바꿨다.** 축을 나눠 받던 출력 인자 일곱을 `float2` 반환으로 돌렸다:
`MouseDevice::getRawDelta` · `getSmoothDelta`, `GamepadDevice::getLeftStick` · `getRightStick`,
`InputManager::getRawMouseDelta` · `getSmoothMouseDelta` · `getMousePositionNormalized`.
`ActionMap::getVector2D` 가 이미 `float2` 를 돌려주고 있었으니 그쪽이 집 스타일이었다. 그리고 결정적으로
`InputManager` 안에 **`_pGamepad->getRightStick( snapshot._lookVector._x, snapshot._lookVector._y )`** 가 있었다 —
이미 `float2` 인 대상을 축으로 쪼개 채우고 있었다는 뜻이고, 지금은 `snapshot._lookVector = getRightStick()` 이다.
`InputManager::getLeftStick` 의 활성 판정도 `stickX*stickX + stickY*stickY` 에서 `stick.getLengthSquared()` 가 됐다.

**`AABB2D` 가 자기 형제와 어긋나 있었다.** 3차원 `AABB` 는 이미 `float3 _min/_max` 인데 2차원만 스칼라 넷이었다.
같은 모양으로 맞췄다.

**덤 — 죽은 필드 넷.** `MouseDevice` 의 `_accumulatedRawDx/Dy` 는 주석이 "현재 미사용(항상 0으로 리셋만 됨)"
이라고 스스로 말하고 있었고, `_mouseWheelAccum` · `_mouseWheelHorizontalAccum` 은 `+=` 로 쓰기만 하고
**아무도 읽지 않았다.** float2 로 바꿀 것이 아니라 지울 것이었다.

**안 바꾼 것과 이유.** 남은 **20묶음은 전부 `int32`** 다(`_mouseX/_mouseY`, `_tileX/_tileY`, `_spawnX/_spawnY` …).
Math 에 `int2`/`uint2` 가 **없다.** 없는 타입을 쓸 수는 없으니 그대로 뒀다 — 픽셀 좌표와 타일 좌표가 계속
스칼라 쌍으로 다녀야 하는 것이 불편하면 그때 `int2` 를 만드는 것이 순서다(지금 만들면 소비자 없는 타입이 하나 는다).
`ZoneBounds`(`int32 _minX…`)는 같은 이름이지만 **타일 경계**라 위 `AABB2D` 와 무관하다 — 탐지기가 잠깐 같이
바꿔 버려서 되돌렸다.

**검증.** Debug · Release · Shipping 빌드 종료 0 · nogpu 5/5 · 린트 7/7 · GPU 19/19 · `Engine_Spatial` 8/8 ·
입력 스위트 전부(MouseDevice 1 · Gamepad 3 · InputManager 19 · RawInputEvent 1 · ActionMap 17 · EdgeCase 2 ·
Stress 2 · Replay 2). 에디터 전부 열기 창 30 · 빈 패널 0 · 오류 0(그래프·스프라이트클립 패널을 건드렸다).
교체 5구성 종료 0 · 오류 0.

### 2026-09-13 (SW_TRUE/SW_FALSE 를 써야 할 자리 415곳 — 그리고 린트가 이 규칙을 아예 안 보고 있었다)

`uint8` 불리언(`_b*`)에 `SW_TRUE`/`SW_FALSE` 를 쓰라는 규칙은 `AGENTS.md` · `.cursorrules` ·
`04_CodingGuidelines.md` **세 곳에** 적혀 있는데, `CheckCodeConventions.py` 에는 검사가 **하나도 없었다.**
그래서 아무도 모르는 채로 415곳이 쌓였다. 한 파일 안에서 `_bGpuDirty = SW_TRUE` 와 `_bGpuDirty = 1` 이
섞여 있는 자리도 있었다(`MaterialInstance.cpp`).

**찾는 과정에서 탐지기를 세 번 고쳤다 — 오탐이 셋 다 이름 충돌이었다.**
1. 처음엔 `true`/`false` 만 찾았다 → 2건. 둘 다 오탐이었다. `.cpp` 안의 지역 `struct` 가 헤더의 `uint8`
   멤버와 **같은 이름**(`_bOwned`)을 쓰고 있었는데, 선언을 헤더에서만 모아서 타입을 잘못 붙였다.
2. 선언을 `.cpp` 까지 모으고 이름이 양쪽 타입에 다 있으면 빼도록 고쳤다 → `true`/`false` 는 **0건**.
3. 진짜 문제는 `true`/`false` 가 아니라 **생 `1`/`0`** 이었다. 그제야 415건이 나왔다.

교훈은 탐지기 쪽이다 — **이름만으로 타입을 단정하면 안 된다.** 최종 판정은 "이 이름이 저장소 전체에서
`uint8` 로만 선언되는가" 를 확인하고, 아니면 판단을 포기한다(오탐보다 누락이 낫다).

| 형태 | 건수 |
| --- | --- |
| 생 `1`/`0` 대입 (`_bFlag = 1;`) | 185 |
| 생 `1`/`0` 비교 (`_bFlag != 0`) | 192 |
| 생 `1`/`0` 초기화 (`_bFlag{ 0 }`) | 37 |
| 비교 없이 조건절 (`if ( _bDefinesDirty )`) | 2 |
| `true`/`false` | 0 |

**문자열 리터럴은 건드리지 않았다.** `ReflectionParser/CodeGenerator.cpp` 는 `"p._metadata._bReadOnly"` ·
`"SW_TRUE"` 같은 **생성될 코드 문자열**을 들고 있으면서 동시에 `prop._bReadOnly != 0` 이라는 진짜 비교도
한다. 치환 전에 리터럴 구간을 마스킹해 바깥만 바꿨고, 생성물이 그대로인 것은 `ReflectionTest` 통과로 봤다.

**규칙을 린트가 지킨다.** `CheckCodeConventions.py` 에 `Style/BitfieldBoolean` 을 넣었다. 선언 타입을 알아야
하므로 전체 스캔에서만 돈다. 일부러 한 줄을 `= 1` 로 되돌려 잡히는 것과, 되돌리면 다시 0 이 되는 것을
확인했다. `AGENTS.md` 의 규칙 문장도 실제 범위에 맞췄다 — 예전에는 "`true`/`false` 대신" 이라고만 적혀
있어서 **정작 415건이던 생 `1`/`0` 은 규칙 문장에 없었다.**

**검증.** Debug · Release · Shipping 빌드 종료 0 · GPU 19/19 · RHITest 13/13 · nogpu 5/5(ReflectionTest 포함) ·
린트 7/7 · 컨벤션 위반 0. 교체 5구성 종료 0 · 오류 0. 에디터 4백엔드 창 15 · 빈 패널 0 · 오류 0.

### 2026-09-13 (범용 RHI 인터페이스에서 Vulkan 을 걷어낸다 — 그리고 그중 절반은 중복이었다)

`IRHIDevice` 는 네 백엔드를 추상화하는 인터페이스인데 Vulkan 전용 API 를 들고 있었다:
구조체 `RHIVulkanImGuiNative` 와 가상 함수 `queryVulkanImGuiNative` · `queryVulkanTextureView`.
DX12 · DX11 · GL 은 "나는 Vulkan 이 아니다" 라고 답하는 빈 구현을 지고 있었고, 이 헤더를 여는 39개 파일이
Vulkan 어휘를 함께 졌다. 이름도 **부르는 쪽(ImGui)** 의 것이 주는 쪽에 새어 들어와 있었다.

**절반은 애초에 중복이었다.** `queryVulkanTextureView( tex, out )` 와 이미 있던 범용 가상 함수
`getNativeTexturePointer( tex )` 는 **같은 일을 같은 방법으로** 한다 — 둘 다 텍스처를 해석해
`_imageView` 를 돌려준다. 전용 함수를 지우고 에디터가 범용 쪽을 쓰게 했다.

**남은 하나(초기화 핸들 묶음)는 백엔드로 내렸다.** `RHIVulkanNativeHandles` 와 `queryNativeHandles` 는
이제 `VulkanRHIDevice.h` 에 있다. 받는 쪽(에디터의 Vulkan ImGui 백엔드)은 `getBackendType()` 으로
Vulkan 임을 확인하고 그 타입으로 캐스팅해 부른다.

**왜 캐스팅해도 되는가 — 실제로 확인했다.** RHI 백엔드는 `add_library(... MODULE)` 이고 `EditorModule` 은
`Engine` · `RuntimeAPI` · imgui 만 링크한다. 그래서 백엔드의 심볼을 링크할 수 없다. 그런데 **호출이
가상이면 vtable 을 타므로 심볼이 필요 없다** — 캐스팅해 부르는 코드를 실제로 넣고 빌드해 링크가 되는 것을
확인했다. 그래서 `queryNativeHandles` 는 아무것도 override 하지 않지만 `virtual` 로 둔다. 그 이유가
주석에 적혀 있으니 지우지 말 것.

**치른 값.** 에디터가 `VulkanRHIDevice` 의 **클래스 레이아웃**에 의존하게 된다. `RHIModuleAbi.h` 의 스탬프는
"IRHIDevice / IRHICommandList / IRHICommandContext 의 public 표면" 만 덮고 concrete 클래스 레이아웃은 덮지
않는다. 즉 백엔드만 다시 빌드하고 에디터를 안 고치면 스탬프가 잡아 주지 않는다. 지금은 한 CMake 빌드가
전부를 같이 짓고 핫리로드 대상도 EditorModule · SWGame 뿐이라 실질 위험은 없지만, **RHI 백엔드를 따로
배포하기 시작하면 이 자리가 먼저 깨진다.**

**결과.** `IRHIDevice.h` 에 남은 "Vulkan" 은 전부 "이 인터페이스가 네 백엔드를 추상화한다" 는 설명 문장뿐이고,
Vulkan 전용 API 는 0 이다.

**검증.** Debug · Shipping 빌드 종료 0 · GPU 19/19 · RHITest 13/13 · nogpu 5/5 · 린트 7/7.
에디터 4백엔드 기동 전부 종료 0 · 창 15 · 빈 패널 0 · 오류 0. **Vulkan 에디터의 Game View 정점 1798** —
바꾼 텍스처 경로(`getNativeTexturePointer` → VkImageView)가 실제로 그린다는 뜻이다.
교체 5구성 종료 0 · 오류 0.

> **함정: 에디터 교체 스크린샷의 비배경 픽셀 수는 세션 간 비교용이 아니다.** 이 값이 5,7xx 에서
> 15,xxx 로 뛰어 회귀를 의심했는데, 변경을 빼고 같은 실행을 재 보니 **15,824 로 같았다.** 원인은 코드가
> 아니라 `Config/Editor/imgui.ini`(git 미추적 로컬 상태)에 저장된 도킹 레이아웃이 `-gv_editorOpenAllPanels=1`
> 실행 뒤에 바뀐 것이었다. 에디터 없는 교체 4구성(10,2xx)은 레이아웃과 무관하므로 그쪽이 비교 가능한 지표다.

### 2026-09-12 (안 쓰이는 공개 API 를 "앞으로 필요한가" 로 가른다 — 지울 것과 이어야 할 것)

Engine 공개 메서드 중 **선언 파일 밖에서 아무도 부르지 않는 것이 248개**였다. 지금 안 쓰인다는 것만으로는
아무 판단도 못 한다 — 엔진 API 는 소비자보다 먼저 있는 것이 정상이다. 그래서 **앞으로 불릴 자리가 있는가**
로 갈랐다. 가장 큰 덩어리인 `ActionMap`(37개)부터 봤다.

**① 지웠다 — 앞으로도 여기서 불릴 일이 없다.** `ActionMap` 의 포인터 네 개
(`isPointerHovering` · `wasPointerHoverEntered` · `wasPointerHoverLeft` · `isPointerOverRect`).
포인터 상태가 **세 겹으로 전달**되고 있었다:

```
MouseDevice::isPointerInside()      ← 상태를 가진 곳
  InputManager::isPointerInside()   ← 전달. X11 입력·테스트가 이것을 쓴다
    ActionMap::isPointerHovering()  ← 전달 + 이름만 바꿈. 아무도 안 쓴다
```

`ActionMap` 은 **액션 매핑**이다. 포인터가 창 안에 있는지는 액션이 아니라 장치 상태이고, 기존 소비자는
전부 `InputManager` 로 간다. 셋을 지웠다. 넷째 `isPointerOverRect` 만 실제 로직(사각형 히트 테스트)이라
**`InputManager` 로 옮겼다** — 쓰는 값이 전부 거기 있다.

**② 이었다 — 앞으로가 아니라 이미 필요했다.** `ActionMap::hasBindingConflict` 는 아무도 안 불러 죽은
것처럼 보였다. 그런데 `InputMapEditorPanel` 에는 **"Key Conflict Matrix" 탭이 있다.** 즉 이 질문을 하는
화면이 이미 있는데, 그 패널은 자기 루프로 따로 세고 있었고 — 더 나쁘게 — **Rebind 버튼은 충돌을 아예
확인하지 않았다.** 이미 다른 액션이 쓰는 키로 바꿔도 아무 말이 없고, 사용자는 다른 탭에 가서야 안다.
`rebindSelectedAction()` 을 두어 두 자리(키 감지·버튼 격자)가 그것을 지나가게 하고, 바꾸기 전에 어느
액션과 겹치는지 로그로 남긴다. **되돌리지는 않는다** — 덮어쓰기를 원할 수 있고, 그 판단은 사람 몫이다.
회귀 테스트 `ActionMapTest.DetectsBindingConflictInSameLayer` 로 계약을 고정했다(같은 레이어면 충돌,
빈 키는 아님, 레이어가 다르면 아님 — 레이어가 있는 이유가 그것이다).

**③ 남겼다 — 부를 자리가 분명하다.** `ActionMap` 의 조율 설정 17쌍(`setMouseSensitivity` ·
`setDeadzoneShape` · `setNavRepeatDelay` · `setHoldThreshold` …)과 `setToggleMode`/`isActionToggled`,
`enableOnlyLayer`. 전부 **게임 옵션 화면과 리바인딩 UI 가 부를 것들**이고 구현도 살아 있다. 같은 이유로
`InputManager` 의 게임패드 트리거·진동·커서 표시·텍스트 입력 콜백도 남겼다(그 31개 중 대부분은 애초에
`/Input/` 안의 플랫폼 배선이 부르고 있어서 "안 쓰임" 으로 보였을 뿐이다).

**판단 기준 한 줄.** 참조 수 0 이 뜻하는 것은 셋 중 하나다 — (a) 같은 답을 주는 다른 길이 이미 있다
→ 지운다, (b) 부를 자리가 있는데 안 부르고 있다 → **잇는다**, (c) 소비자가 아직 없다 → 남긴다.
(a) 와 (b) 를 (c) 로 착각하면 중복과 구멍이 그대로 쌓인다.

**덤.** 에디터 전부 열기 기준선이 문서에는 29 인데 실제로는 **30** 이었다. 내 변경 때문인지 확인하려고
변경을 빼고 다시 재 봤고(30 그대로) 문서 쪽을 고쳤다.

**검증.** Debug · Shipping 빌드 종료 0 · ActionMapTest 17/17 · GPU 19/19 · nogpu 5/5 · 린트 7/7.
에디터 전부 열기 창 30 · 빈 패널 0 · 오류 0.

### 2026-09-12 (Graphics 에서 합칠 것을 찾다 — 나온 것은 중복이 아니라 죽은 타입이었다)

Graphics 의 타입 209개를 전수로 재고 "합칠 수 있나" 를 봤다. **합칠 중복은 없었다.** 대신 아무도 안 쓰는
타입 넷이 나왔다. 찾은 근거와 기각한 근거를 같이 남긴다 — 다음에 같은 질문을 다시 파지 않도록.

**지웠다 (Source 어디에서도 안 쓰임).**
- `RHIInputElement` · `VertexLayoutBuilder` — 정점 레이아웃을 조립하는 빌더인데, **네 백엔드가 전부**
  `RHIVertex` 에서 직접 하드코딩한다(`D3D11_INPUT_ELEMENT_DESC` · `VkVertexInputAttributeDescription` ·
  `glVertexAttribPointer`). 빌더를 살아 있게 하던 것은 빌더를 시험하는 테스트 하나뿐이었다. 셋 다 지웠다.
- `GpuSceneSortKey` · `GpuSceneSortEntry` — 예전 CPU 정렬 설계의 잔재다. 지금 `sortTransparent` 는
  `vector<uint32>` 를 카메라 거리로 정렬한다. 게다가 이 키는 `Mesh*` · `Material*` · `MaterialInstance*` 를
  생포인터로 들고 있어, 되살아나면 최근에 걷어낸 "스냅샷 안의 생포인터" 가 그대로 돌아온다.

**안 지웠다 — 죽어 보였지만 아니었다.** `RHIDispatchIndirectCommand` · `RHIDrawIndexedIndirectCommand` 는
C++ 어디에서도 이름이 불리지 않는다. 그런데 `dispatchIndirect` · `drawIndexedIndirect` 는 **네 백엔드에 다
구현돼 있고**, 그 인자 버퍼를 채우는 것은 C++ 이 아니라 컴퓨트 셰이더다 — 두 구조체가 그 버퍼의 레이아웃
정본이다. 이름이 안 불리는 이유는 두 함수에 **주석이 아예 없어서**였다. 주석을 달아 구조체 이름을 부르게
했다. 참조 수만 보고 지웠으면 GPU 가 쓰는 계약을 지울 뻔했다.

**기각한 합치기 — 숫자와 함께.**
- **네 백엔드의 `*RHIResource.h` · `*RHICommandContext.h` (겹침 45~63%).** 겹치는 것은 전부 인터페이스
  override **선언**이다. 멤버 상태는 넷 다 `<Backend>Device* _pDevice` **하나뿐**이라 기반 클래스로 뽑아도
  백엔드당 네 줄이 준다. 몸통이 같았던 `CommandList` 는 이미 `RHICommandListForwarder` 로 합쳐져 있다 —
  거기서 멈춘 것이 맞다.
- **`MaterialCache` · `TextureCache` (정규화 후 겹침 61%).** 나머지 39%가 소유 방식 자체다 — 머티리얼은
  `shared_ptr`(렌더 패킷이 소유를 빌린다), 텍스처는 `unique_ptr`. 초기화 호출도 `release` 시그니처도 다르다.
  둘뿐인 사용처를 위해 traits 를 끼운 `ResourceCache<T>` 를 만들면 70줄짜리 두 클래스보다 읽기 어려워진다.
- **백엔드 이름을 지우면 같아지는 함수:** 전수로 세어 **0건**. 네 백엔드의 몸통은 진짜로 다르다.

**검증.** Debug · Shipping 빌드 종료 0 · GPU 19/19 · RHITest 13/13 · GpuScene 5/5 · nogpu 5/5 · 린트 7/7.
교체 5구성 종료 0 · 오류 0. 에디터 4백엔드 창 15 · 빈 패널 0 · 오류 0.

### 2026-09-12 (Linux 와 macOS 가 같은 코드를 두 벌 들고 있었다 — POSIX 한 벌로)

파일 간 유사도를 전수로 재 보니(6줄 묶음 겹침) `Core/Process` 의 Linux · Mac 쌍이 맨 위에 나왔다.

| 쌍 | 겹침 | 실제 차이 |
| --- | --- | --- |
| `LinuxProcess.cpp` · `MacProcess.cpp` | **90%** | `#if` 가드와 `SW_LOG_CALLER` 이름 **둘뿐** |
| `LinuxCallStackCapture.cpp` · `MacCallStackCapture.cpp` | 68% | 시그널 컨텍스트에서 폴트 PC 꺼내기 |
| `LinuxCrashHandler.cpp` · `MacCrashHandler.cpp` | 43% | 스레드 ID API |

셋을 `Core/Process/Posix/Posix*.cpp` 한 벌로 합쳤다(6파일 → 3). **진짜로 갈리는 곳만** 안쪽 `#if` 로 남겼고,
각 파일에서 그 자리가 어디인지 주석이 가리킨다.

**합치면서 드러난 것 — macOS 크래시 핸들러가 썩어 있었다.** Linux 쪽은 `sigaction` + `SA_SIGINFO` 로
폴트 주소와 레지스터 컨텍스트를 받고, `sigaltstack` + `SA_ONSTACK` 으로 **스택 오버플로에서도** 핸들러가
돌게 해 놓았다. macOS 쪽은 `std::signal` 그대로였다 — 폴트 주소가 늘 `nullptr` 이고, 스택이 폴트 지점이
아니라 핸들러 안에서 시작하며, 스택 오버플로 크래시는 **아무 기록도 없이** 죽는다. Linux 코드는 전부 POSIX 라
macOS 에서도 그대로 서므로, 합치는 것만으로 macOS 가 그 셋을 받는다. 아무도 안 보는 경로는 조용히 썩는다는
같은 이야기다.

`CallStackCapture` 의 폴트 PC 추출은 **일부러 Linux 만** 남겼다. Darwin 의 mcontext 는 모양이 다르고 여기서
컴파일해 볼 수 없다 — macOS 는 `nullptr` 을 받아 트리밍만 건너뛰고 스택 자체는 그대로 남으므로, 합치기 전
동작과 같다. 고치려면 macOS 에서 실제로 빌드·크래시를 내 봐야 한다.

**빌드 배선 두 곳.** `Source/Core/CMakeLists.txt` 는 플랫폼 소스를 GLOB 이 아니라 `if(WIN32)/APPLE/UNIX`
명시 목록으로 고른다 — APPLE 과 UNIX 가 이제 같은 세 파일을 가리킨다. 그리고 `CheckSourceGlob.py` 는
호스트에 따라 반대편 OS 폴더를 무시하는데 `/Posix/` 라는 이름을 몰라 Windows 빌드에서 3개를 "빠졌다" 고
잡았다 — Windows 에서만 빠지는 것이 정상인 자리로 넣었다.

**검증.** Linux 쪽은 WSL 의 clang 으로 세 파일 모두 `-fsyntax-only -DSW_PLATFORM_LINUX` 통과
(가드 때문에 조용히 넘어간 것이 아님을 일부러 깨뜨려 확인했다 — 에러가 난다).
**macOS 쪽은 여기서 컴파일할 수 없다** — Darwin 시스템 헤더가 없어 전처리가 거기서 멈춘다. `#if` 중첩
균형과 `pthread_threadid_np` 가 macOS 가드 안에 있다는 것만 확인했다. 실제 확인은 macOS 빌드가 필요하다.
Windows 쪽은 Debug · Release · Shipping 빌드 종료 0 · 린트 7/7(`CheckSourceGlob` 포함).

### 2026-09-12 (DX11 만 한 덩어리였던 디바이스를 다른 셋과 같은 축으로 가른다)

DX12 · Vulkan · GL 은 모두 `<Backend>RHIDevice` / `…DeviceInit` / `…DeviceSubmission` 으로 갈라져 있는데
DX11 만 576줄 한 덩어리였다. "이 백엔드는 어떻게 초기화하나" 를 찾을 때 셋은 파일 이름이 답이고 하나만
본문을 뒤져야 한다. 세 백엔드가 이미 쓰는 축을 그대로 적용했다:

| 파일 | 담는 것 |
| --- | --- |
| `D3D11RHIDeviceInit.cpp` | `initializeInternal` · `shutdownInternal` · `resize` · `bind/unbindGraphicsContext` |
| `D3D11RHIDeviceSubmission.cpp` | `beginFrame` · `endFrame` · `waitIdle` · `createCommandList` · `register/unregisterCommandList` · `executeCommandList` |
| `D3D11RHIDevice.cpp` | 생성·소멸 · 접근자 · `resolve/store` · `flushDebugMessages` · `ensureComputeRootConstantCb` |

GL 이 컨텍스트 바인딩을 Init 에 두므로 DX11 도 같은 자리에 뒀다. 익명 네임스페이스의 상수도 쓰는 쪽을
따라갔다 — `kDefaultNumerator` · `kDefaultDenomiator` 는 `initializeInternal` 만 쓰므로 Init 으로,
`arrHazardMessageId` · `isHazardMessage` 는 `flushDebugMessages` 가 쓰므로 그대로 남았다.

**함정 하나.** 파일을 더해 놓고 빌드하니 `Engine.dll` 링크가 DX11 심볼을 통째로 못 찾았다. RHI 백엔드는
GLOB 이 아니라 `cmake/Engine/RhiBackendSources.cmake` 의 **명시 목록**으로 타깃이 갈린다(모듈 ON 이면
`RHI_DX11.dll`, OFF 면 Engine 정적 링크). 목록에 없는 새 파일은 Engine 의 GLOB 에 떨어져, 모듈 안에 있는
심볼을 Engine 에서 찾게 된다. 목록에 넣어 해결했다. **RHI 백엔드에 파일을 더할 때는 그 목록도 같이 고칠 것.**

### 2026-09-12 (Core 시설이 있는데 STL 을 쓴 자리 — 전수로 훑으니 거의 없었다)

AGENTS 의 "Core·Engine 시설을 STL 보다 먼저" 를 기준으로 `Source` 전체를 심볼별로 셌다. **대부분 이미
지켜지고 있었다** — 지키지 않은 자리를 찾는 것보다, 지켜지고 있다는 것을 숫자로 남기는 것이 이 항목의 값이다.

| 심볼 | Core 대체 | Source 안 직접 사용 |
| --- | --- | --- |
| `std::vector` · `string` · `unordered_map` · `set` · `deque` · `list` · `pair` | `sw::` 동명 | **0** |
| `std::unique_ptr` · `shared_ptr` · `make_unique` · `make_shared` | `Core/Memory/Memory.h` | **0** |
| `std::max` · `min` · `clamp` · `abs` · `sqrt` | `MathUtil` | **0** |
| `std::to_string` · `stoi` · `ifstream` · `printf` · `cout` | `StringUtil` · `FileUtil` · `SW_LOG` | **0** |
| `std::atomic` | `Core/Concurrency/atomic.h` | 8 → **고쳤다** |
| `std::string_view` | `Types.h` 의 `string_view` 별칭 | 8 → **고쳤다** |

**고친 것 셋.**
1. `std::atomic` 8개(`FrameRenderer.h` 7 · `D3D12RHIDevice.h` 1)를 `sw::atomic` 으로. 나머지 코드
   (`Component` · `HandleTable` · `TaskManager` …)는 이미 `sw::atomic` 을 쓴다. 한 군데를 손으로 더 고쳤는데,
   `compare_exchange_weak` 를 **성공 순서만** 주고 부르던 자리다: `std::atomic` 은 실패 순서를 성공 순서에서
   약하게 유도하지만 `sw::atomic` 은 기본값이 `seq_cst` 라 조용히 더 센 순서가 된다. 뜻이 바뀌지 않도록
   둘 다 명시했다.
2. `std::string_view` 8개(`ShaderBindingLayout.cpp` · `ShaderBindingContract.cpp`)를 `string_view` 로.
   `Types.h` 가 같은 타입에 별칭을 두고 있고, AGENTS 는 프로젝트 별칭을 쓰라고 한다.
3. `std::lock_guard` 34개를 `std::scoped_lock` 으로. STL 대 Core 문제는 아니지만 같은 일에 이름이
   둘이었다 — 227:34 로 `scoped_lock` 이 이미 정본이었다.

**고치지 않기로 한 것과 그 이유.**
- `std::shared_mutex` 169개. Core 에 대응물이 **없다**(`Core/Concurrency/mutex.h` 는 `mutex` 만 준다).
  없는 시설을 쓰라고 할 수는 없다. `sw::mutex` 의 값인 데드락 탐지를 공유 잠금에서도 받으려면
  `sw::shared_mutex` 를 새로 만들어야 하는데, 그건 "있는 걸 쓰라" 가 아니라 새 기능이다.
- `std::array` 두 곳(`RenderThread.h` · `ConcurrentQueue.h` · `LockFreeQueue.h`). 셋 다 주석에
  **왜 `sw::array` 가 아닌지**가 적혀 있다 — DataRaceDetector 오탐. 근거가 적힌 예외다.
- `PlatformFileUtil` 이 `FILE*` 을 그대로 내주고 `fread`/`fclose` 는 직접 부르는 것. 플랫폼마다 갈리는
  부분(UTF-8 경로 열기, 64비트 seek/tell)만 감싸는 **의도된 설계**다.
- `std::move` · `forward` · `swap` · `sort` · `numeric_limits`: 대응물이 없고 있을 이유도 없다.

**덤으로 함정 하나를 막았다.** `PagedArray` 의 `new T[]` 를 `sw_new` 로 바꾸려다 멈췄다 —
`sw_delete_array` 는 **원소 소멸자를 부르지 않는다.** `PagedArray` 는 임의의 `T` 를 담으므로 바꿨다면
원소가 새고, 컴파일러가 배열 앞에 넣는 개수 쿠키 때문에 해제 주소까지 어긋났을 것이다. 매크로에 그 계약이
어디에도 적혀 있지 않았다 — 주석을 달고 `static_assert( is_trivially_destructible_v<T> )` 를 넣었다.
유일한 사용처(`WorkStealingDeque<atomic<T*>>`)는 통과한다.

**검증.** Debug GPU 19/19 · GpuScene 5/5 · nogpu 5/5 · 린트 7/7. ASAN GPU 19/19 · Core 통과 · 리포트 0.
Release · Shipping 빌드 종료 0. 교체 5구성 종료 0 · 오류 0. `sw::atomic` 이 렌더 스레드 핫 패스에 들어가므로
Release 로 다시 쟀다 — `GT.GpuScene.build` 29~31us, `RT.Graph.executeParallel` 294~308us 로 바꾸기 전(33 · 331)과
같거나 낫다.

### 2026-09-12 (초기값의 정본을 한 곳으로 — 그리고 그 규칙을 린트가 지키게 한다)

열어 두었던 결정을 닫았다: **규칙을 살린다.** 헤더와 생성자 양쪽에 초기값을 적으면 어느 쪽이 이기는지
읽어서는 알 수 없고(생성자가 이긴다), 값을 고칠 때 한쪽만 고치게 된다.

**먼저 숫자가 틀렸다는 것부터.** 앞 항목의 "헤더 33개 · 멤버 217개" 는 **과장이었다.** 그때 쓴 탐지기가
`class` 만 보고 `struct` 는 안 봐서, 클래스 안에 중첩된 구조체(`PreparedShadow` · `CompressionHeader` ·
`ThreadState` …)의 멤버를 바깥 클래스 것으로 셌다. 그 중첩 구조체들은 **생성자가 아예 없어** 규칙의 대상이
아니다. 제대로 세니 **클래스 28개 · 멤버 86개**였고, 그중 **52개는 헤더와 생성자 양쪽에 같은 값이**
**이미 적혀 있었다** — 이 규칙이 막으려는 바로 그 상태다. 나머지 34개만 생성자로 새로 들어갔다.

**옮긴 방식.** 값을 그대로 옮기기만 하는 기계 변환으로 하되, 증명 못 하는 자리는 손대지 않고 남겼다가
따로 봤다. 도중에 실제로 두 번 물렸고 둘 다 도구가 잡게 고쳤다:
- **위임 생성자.** `FrameResourceRing::FrameResourceRing() : FrameResourceRing( 0 )` 에 멤버 초기화자를
  붙이면 컴파일이 안 된다. 위임 생성자는 건드리지 않고 **위임 대상**이 채우게 한다.
- **리스트 안의 `#if`.** `VulkanRHIDevice` 생성자는 초기화 리스트 한가운데에
  `#if defined( SW_DEBUG )` 가 있다. 리스트를 다시 짓는 대신 **선언 순서상 앞 멤버 바로 뒤에 끼워 넣는**
  방식으로 바꿨다 — 전처리기 줄을 건드리지 않는다.
- 그 밖에 배열 멤버(`_arrSlot[N]{}`) · 비트필드 · `alignas` 멤버를 파서가 놓치고 있었다.

**남겨 둔 것은 남겨야 할 이유가 있다.** `AnimClip` 과 `DualQuaternion` 은 `= default` 기본 생성자를 쓰고
있어 헤더 기본값이 유일한 초기화였다 — 그래서 기본 생성자를 `.cpp` 에 **실제로 써서** 옮겼다
(`quaternion` 이 이미 그 모양이다). `D3D11RHIDevice` · `D3D12RHIDevice` 는 같은 헤더에 Windows 클래스와
비-Windows 스텁이 **같은 이름으로 두 번** 정의돼 있어 기계 변환이 둘을 섞을 뻔했다 — 손으로 했다.

**덤으로 드러난 것.** 헤더 기본값을 떼자 린트가 가려져 있던 위반 하나를 잡았다 —
`D3D12RHIDevice::_activeFrameList` 는 원시 포인터인데 `_p` 접두어가 없었다(`_pActiveFrameList` 로).
초기값이 붙어 있으면 선언이 린트의 패턴에서 벗어난다는 뜻이기도 하다.

**규칙을 린트가 지킨다.** `CheckCodeConventions.py` 에 `Style/HeaderMemberInitializer` 를 넣었다.
헤더와 `.cpp` 를 같이 봐야 알 수 있어 `DuplicateInternalHelper` 처럼 **전체 스캔에서만** 돈다.
넣자마자 `Test/TestFramework/TestFixture.h` 두 건을 더 잡았다 — 기계 변환이 `Source` 만 훑었던 자리다.
면제 셋(= default 기본 생성자 · 위임 생성자 · 생성자 없는 타입)은 `AGENTS.md` 에 적었다.

**검증.** Debug 빌드 종료 0 · **-Wreorder 경고 0**(선언 순서가 어긋났으면 여기서 잡힌다) · GPU 19/19 ·
GpuScene 5/5 · nogpu 5/5 · 린트 7/7 · 컨벤션 위반 0. ASAN GPU 19/19 · Core 통과 · 리포트 0.
Release · Shipping 빌드 종료 0. 교체 5구성 종료 0 · 오류 0. 에디터 4백엔드 창 15 · 빈 패널 0 · 오류 0.

### 2026-09-12 (컨벤션 전수 점검 — 어긴 곳은 내가 방금 만든 셋뿐이었다)

`Source` 전체를 AGENTS 규칙별로 기계 점검했다. 대부분 이미 지켜지고 있었다:

| 규칙 | 결과 |
| --- | --- |
| `CheckCodeConventions` (명명·스타일) | 위반 0 |
| `CheckIncludeOrder` | 910 파일 OK |
| `CheckEngineLayers` | 481 파일 OK |
| `i`/`j`/`k` 루프 변수 금지 | 0 건 |
| 부정 비교(`if ( !x )`) 금지 | 0 건 |
| 매직 버퍼 크기 금지 | 0 건 (탐지된 둘은 주석과 `StringBuilder` 정의 자체) |
| 익명 네임스페이스 파일당 하나 | 위반 1 건 — **아래** |
| 네 RHI 백엔드 간 이름 일치 | 어긋난 곳 없음 (다른 것은 전부 create/destroy 짝) |

**고친 셋은 전부 내가 최근 커밋에서 만든 것이다.**
1. `RHIRenderResource.cpp` 에 익명 네임스페이스가 둘이었다(등록부 헬퍼 + `broadcastInternal`). 하나로 합쳤다.
   `ImGuiOpenGLRendererBackend.cpp` 의 둘은 `#if WINDOWS` / `#elif LINUX` 로 배타적이라 규칙이 명시한 예외다.
2. `Texture2D::_pDevice` 를 헤더에서 `{ nullptr }` 로 초기화했는데, 이 클래스는 초기화 리스트를 가진 생성자가
   있다 — 선언 순서대로 생성자로 옮겼다. `Material::_assetPath` 도 같은 이유로 리스트에 넣었다.
3. `~Texture2D` 의 경고 문구가 **이제 없는 함수**를 가리키고 있었다("call shutdown first" → `releaseRhi`).

### 2026-09-12 (Engine 이 리로드 콜백을 받지 않고, 에디터를 알지도 않게 한다)

`Source` 를 다시 훑어 Engine 에 남은 리로드·에디터 자국을 셋 찾아 없앴다.

**1) 리로드 콜백을 받는 공개 API.** `EngineLoop::pollDebugHotkeys( const Delegate<void(const utf8*)>& forceReloadCallback )`
가 Engine 헤더에 있었다. 본문은 Shipping 에서 통째로 비지만 **선언과 델리게이트 타입은 배포 헤더에 그대로 남는다.**
둘로 갈랐다:
- 셰이더 강제 리로드는 Engine **자신의** 개발 도구다(`LiveShaderManager` 를 `EngineLoop` 이 소유한다). 콜백을
  달라고 하지 않고 `pollShaderReloadHotkey()` 로 내부에서 끝낸다 — private 이고 `SW_DEBUG` 안이다.
- 모듈을 다시 올리는 일은 App 것이다. App 이 `wasDebugActionTriggered( kReloadGameAction )` 을 직접 묻고 자기
  핸들러를 부른다(에디터 모듈은 이미 그렇게 하고 있었다 — 이제 둘이 같은 모양이다).
덤으로 App 의 `_forceReloadHandler` 멤버가 사라졌다(Engine 에 넘기려고만 있던 델리게이트다).
`EngineLoop::getLiveShaderManager()` 도 공개에서 내렸다 — **바깥 호출자가 하나도 없었다.**

**2) Engine 의 RHI 가 에디터를 알고 있었다.** `RHICapabilities::_bEditorSupported` · `_bImGuiHooks`.
둘 다 **네 백엔드에서 전부 1** 이라, 이것을 보는 세 자리(App 의 에디터 비활성화, `BackendSwapController` 의 교체
되돌리기, `EditorMenuBar` 의 "Vulkan 에디터 지원" 툴팁)가 모두 도달 불가한 죽은 분기였다. 그리고 애초에
"이 백엔드로 에디터가 도는가" 는 디바이스가 아니라 **에디터가 아는 사실**이다. 두 필드와 죽은 분기 셋을 지웠다.

정본은 `IImGuiRendererBackend::createRendererBackend` 하나로 모았다. 그 팩토리의 `default:` 가
**DX11 백엔드를 대신 만들어 돌려주고 있었다** — 새 백엔드가 붙으면 엉뚱한 API 로 그리다 조용히 무너진다.
`nullptr` 을 돌려주게 고쳤고, `ImGuiEditor::initialize` 는 원래 그 경우를 이미 다루고 있었다(에디터만 안 뜬다).

**3) Shipping 이 리로드 핫키를 바인딩하고 있었다.** `ActionMap::bindDefaultFallback` 의 Ctrl+F6/F7/F8 세 줄.
배포 빌드에는 다시 올릴 모듈이 없으므로 아무도 읽지 않는 바인딩이었다 — `SW_SHIPPING` 으로 감쌌다.
액션 **이름** 상수(`kReloadEditorAction` 등)는 남긴다: `constexpr string_view` 라 비용이 없고, 이름이 있어야
App 과 Engine 이 같은 액션을 가리킬 수 있다.

**결과.** Shipping `App.exe` 2,627,072 → 2,622,976 B. Engine 공개 헤더에서 리로드 델리게이트와 에디터 능력
필드가 사라졌다.

**검증.** Debug GPU 19/19 · nogpu 5/5 · 린트 7/7. 에디터 4백엔드 기동 전부 종료 0 · 창 15 · 빈 패널 0 · 오류 0.
교체 5구성 종료 0 · 오류 0. Shipping 빌드 종료 0.

### 2026-09-12 (인스턴스 채우기를 워커로 나눈 것이 **모든 크기에서 지고 있었다**)

Release 로 재 보니 게임 스레드 비용을 `GT.GpuScene.build.fill` 이 혼자 먹고 있었다 — 400개 메시에서
`build` 394us 중 **361us**. 원소당 하는 일은 필드 다섯 개 복사다. 그 일을 워커로 나누느라 디스패치
(`createAnonymousStage` · `emplaceParallelBlock` · `submit` · `waitStage`)를 매 프레임 왕복했다.

A/B (Release, 각 3회, `-gv_benchMeshVariants=8`, `fill` 평균 us):

| 메시 수 | 병렬(기존) | 인라인 |
| --- | --- | --- |
| 400 | 358 / 370 / 344 | 1 / 1 / 2 |
| 4,000 | 175 / 230 / 241 | 20 / 16 / 19 |
| 20,000 | 196 / 219 / 221 | 99 / 101 / 97 |

**어느 크기에서도 병렬이 이기지 못한다.** 20,000개에서도 인라인이 두 배 빠르다 — 일이 메모리 대역폭
바운드라 스레드를 더 붙여도 얻을 것이 없고, 대기 비용만 남는다. 그래서 병렬 경로를 **지웠다**(폴백으로
남기지 않는다 — 안 쓰이는 경로는 조용히 썩는다).

`GT.GpuScene.build` 전체 평균: 400개 390/402/376us → **40/36/34us**. 4,000개 515/599/612 → 331/360/308.
덤으로 `buildFromScene` 의 `TaskManager*` 인자가 사라졌다(그 인자를 쓰던 곳이 여기뿐이었다) — 테스트
서른 자리가 넘기던 `nullptr` 도 함께. `fillScratchRange` · `_snapshotStage` · `_pScratchCandidateBase` ·
`_pScratchRawBase` 삭제.

400개 기준 지금 프레임은 GT 33us · RT `executeParallel` 331us 로, 게임 스레드는 더 이상 지배적이지 않다.
20,000개에서는 `batches` 1,020us · `collect` 732us · `RT.GpuScene.upload` 2,824us 가 남는데, 그 규모를
실제로 쓰는 워크로드가 생기면 그때 잰다.

### 2026-09-12 (FrameRenderer::execute 의 죽은 Material 인자를 뗀다)

머티리얼은 렌더 패킷(`GpuSceneSnapshot`)을 타고 간다. 그 전 시절의 인자가 시그니처에 남아 있었다 — 함수 본문이
한 번도 쓰지 않았고(`unused parameter 'pMaterial'` 경고로 드러났다), 테스트 열두 자리가 전부 `nullptr` 을
넘겼으며, 유일한 실사용자인 `Scene::render` 만 `_pMaterial` 을 넘기고 무시당했다. 인자를 뗐다.
`Scene::_pMaterial` 자체는 남는다 — 씬 기본 머티리얼의 참조를 쥐고 있고, `getMaterial()` 로 `GpuScene` 이
"머티리얼 없는 메시" 의 폴백에 쓴다(언리얼의 기본 머티리얼과 같은 자리).

### 2026-09-12 (명칭 감사 — 헤더 선언 전수를 동사군으로 훑고, 진짜 불일치만 고친다)

`Source` 와 `Tools/ReflectionParser` 의 **헤더 멤버 함수 선언 전수**를 선행 동사로 묶어 세었다(정의·호출은
중복이라 세지 않는다). 어휘가 섞여 있으면 같은 일에 다른 이름을 쓰고 있다는 뜻이다.

```
해제/파괴   clear=94, destroy=65, shutdown=56, reset=28, release=22, forget=7, unload=5, free=4
생성       create=118, make=21, build=12, construct=10, new=8, spawn=7, alloc=3
초기화     initialize=61, ensure=36, prepare=21, init=11, setup=4, configure=3
조회       get=915, find=86, resolve=26, acquire=14, query=11, peek=2
상태질의    is=246, has=50, was=47, can=8, wants=5, should=3, needs=2
적용/반영   update=61, apply=44, sync=11, flush=9, refresh=9, upload=8, rebuild=8, submit=5, commit=3, invalidate=3
등록       register=83, bind=65, unregister=38, add=32, insert=27, remove=23, unbind=7, erase=5
```

**대부분은 불일치가 아니라 서로 다른 뜻이다 — 그래서 건드리지 않았다.** `create`(소유를 만들어 돌려준다) ·
`make`(값을 조립한다) · `build`(여러 입력에서 짓는다)는 STL 도 구분한다. `free*` 는 전부 할당자와 짝이고
(`free` · `freeSrvDescriptor` · `freeNode`), `unload*` 는 전부 `load*` 와 짝이다. `was*` 47 건은
입력 엣지 질의(`wasPressed` 류)로 `is*` 와 다른 것을 묻는다.

**진짜 불일치는 둘이었다.**
1. GPU 자원 수명 동사 — 위 항목에서 닫힌 목록으로 고쳤다.
2. `init` 이라는 **축약**. 이 저장소의 단어는 `initialize` 인데 같은 뜻을 줄여 쓴 자리가 넷 있었다:
   `initPipelineCache` · `initPredefined` · `initXmlSerialization` · `initXmlDeserialization` →
   전부 `initialize*` 로. `initRhi` 는 남긴다 — 축약이 아니라 언리얼 `InitRHI` 와 짝을 이루는 닫힌 어휘의
   구성원이고, `releaseRhi` · `forgetRhi` · `isRhiValid` 와 함께여야 뜻이 선다.
   `setup*`(4) · `configure*`(2) 도 남긴다 — "자원을 만든다" 가 아니라 "설정을 채운다" 라서 다른 단어가 맞다.

### 2026-09-12 (GPU 자원 수명의 어휘를 닫힌 목록으로 — 그리고 되살리는 절반도 통보로)

**1) 어휘부터.** 같은 일을 하는 함수가 클래스마다 다른 이름이었다: `Mesh::upload` · `MaterialInstance::applyToGpu` ·
`Material::shutdown( device )` · `Texture2D::shutdown( device )` · `isUploaded` · `isReady` · `releaseGpu`. 상용 엔진은
GPU 자원 수명 동사를 **닫힌 목록**으로 둔다(언리얼 `InitRHI` / `ReleaseRHI`). 우리도 닫았다 —
`initRhi` · `updateRhi` · `releaseRhi` · `forgetRhi` · `isRhiValid` 다섯이고, `AGENTS.md` 의 표가 정본이다.
이 어휘가 뜻을 갖는 이유는 그 이름들이 곧 **`RHIRenderResource` 의 가상 함수**이기 때문이다 — 이름을 맞추는 일과
아래 등록부는 같은 작업의 두 면이다.
`GpuScene` 은 **일부러 두었다**(`upload` · `isUploaded` · `releaseGpu`). 에셋이 아니라 프레임마다 도는 버퍼
매니저이고 등록부 구성원이 아니다 — 같은 이름을 쓰면 오히려 거짓말이 된다.

**2) 되살리는 절반도 통보로 — 기억해서 불러야 하던 다섯 줄을 지운다.**

앞 항목에서 **죽을 때** 알리는 축은 세웠는데, 절반이 남아 있었다. `Material` 과 `Texture2D` 는 GPU 자원을 들면서도
등록부 밖에 있어서, 디바이스가 바뀔 때마다 바깥이 **기억해서** 훑어 줘야 했다:

```
EngineLoop::shutdown          getMaterialManager().shutdownAllGpu( ... );  getTextureManager().shutdownAllGpu( ... );
EngineLoop::recreate          getMaterialManager().shutdownAllGpu( ... );  getTextureManager().shutdownAllGpu( ... );
EngineLoop::rebindScene       getMaterialManager().reinitializeAll( ... );
```

방금 없앤 "기억해야 하는 구조" 그대로다. 그리고 실제로 **한 칸이 이미 비어 있었다**: 텍스처를 되살리는 줄이 없어,
교체 뒤 텍스처가 돌아오는 유일한 길이 "머티리얼을 다시 초기화하면 그 안에서 `acquire` 가 다시 올린다" 라는
**부수 효과**였다. 어떤 머티리얼도 참조하지 않는 텍스처는 교체 뒤 빈 채로 남는다.

**고친 방식 — 언리얼과 같게, 양쪽 다 통보로.** `FRenderResource` 는 `ReleaseRHI` 만이 아니라 `InitRHI` 도 등록부
전체에 밀어 넣는다(`InitRHIForAllResources`). 우리도 그렇게 했다:

- `RHIRenderResource::initRhi( pDevice )` 가상 함수 추가. 기본은 아무것도 하지 않는다 — 그릴 때 알아서 다시
  올라가는 것은 낄 이유가 없다. `Mesh` 는 이미 시그니처가 같아 그대로 `override` 가 됐다.
- `Material` 은 `_assetPath` 를 기억해 스스로 `initialize` 한다. `Texture2D` 는 이미 들고 있던 `_path` 로 다시 읽는다.
  둘 다 **이미 올라가 있으면 그대로 true** — 통보 순서가 정해져 있지 않아서(머티리얼이 먼저 살아나며 자기 텍스처를
  올려 놓는다) 이 가드가 없으면 두 번 올려 그대로 샌다.
- `Material` · `Texture2D` 가 `RHIRenderResource` 가 됐다. 이제 등록부는 `Mesh` · `Material` · `MaterialInstance` ·
  `Texture2D` 넷이다.
- `MaterialCache::_bGpuInit` 삭제. 캐시가 따로 세던 표식이 **디바이스가 죽으면 거짓말이 됐고**, 그 거짓말을 지우려고
  바깥에서 일괄 해제를 불러 주어야 했다. 이제 `Material::isRhiValid()` 에게 묻는다 — 표식은 자원을 든 쪽에 있어야 한다.
- `MaterialCache::shutdownAllGpu` · `MaterialCache::reinitializeAll` · `TextureCache::shutdownAllGpu` 와 `EngineLoop`
  의 호출 다섯 줄 삭제. 남은 것은 교체 뒤 `RHIRenderResource::initAllFor( device )` 한 줄이다.

**덤으로 통보 자체의 구멍 둘.**
1. `forgetAll()` 은 **누가 죽었는지 묻지 않고** 전부 잊게 했다. 디바이스가 하나뿐이라 드러나지 않았을 뿐,
   테스트처럼 디바이스가 여럿인 자리에서는 남이 죽었다고 내 멀쩡한 핸들까지 비운다. `releaseRhi` 와 대칭이 되도록
   `forgetRhi( pDevice )` · `forgetAllFor( pDevice )` 로 바꾸고 넷 모두 소유를 가린다.
2. 통보 루프가 **사본을 떠서 돌기만** 했다. 머티리얼이 자기 자원을 놓으며 빌린 텍스처를 돌려주는데 그게 마지막
   참조면 `Texture2D` 가 그 자리에서 파괴된다 — 사본에 남은 주소는 그 순간 댕글링이다. 부르기 직전에 "아직
   등록부에 있나" 를 잠금 아래에서 다시 묻도록 고쳤다(파괴자가 같은 잠금으로 자기를 지우므로 정확하다).

**회귀 테스트.** `RenderPassGpuTest.RegistryRestoresResourcesOnNewDevice` — 큐브를 **이름으로 부르지 않고**
`initAllFor` 한 줄로 되살아나는지, 남의 디바이스 통보에 내 핸들이 살아남는지, 내 디바이스 통보에는 비는지를 본다.
빨강 확인 둘: `initAllFor` 를 no-op 으로 만들면 "되살리지 않았다" 로, `Mesh::forgetRhi` 의 소유 가드를 지우면
"내 핸들까지 비웠다" 로 각각 실패한다.

**검증.** Debug GPU 19/19 · GpuScene 5/5 · nogpu 5/5 · 린트 7/7. ASAN GPU 19/19, ASAN 교체 실행 종료 0 · ASAN 리포트 0.
교체 5구성(dx12→vk, vk→dx11, dx11→gl, gl→dx12, dx12→vk 에디터) 종료 0 · `[Error]` 0 · 교체 후 비배경 픽셀
10,206~10,227(에디터 5,726). **텍스처 경로 따로** — `-gv_defaultMaterial=engine/materials/benchtextured.material` 로 4백엔드
교체를 다시 돌려 평균 RGB 33.9~34.1 / 38.6~38.7 / 45.3~45.4 로 일치(머티리얼 재초기화 2회 = 시작 1 + 교체 1 로그 확인).
패널 덤프 창 15 · 빈 패널 0. Shipping 빌드 종료 0, `App.exe` 2,625,024 → 2,627,072 B (+2,048).

### 2026-09-12 (언리얼의 FRenderResource 를 들여온다 — 죽은 뒤에 묻지 않고, 죽기 전에 알린다)

`_s_deviceGeneration` 을 **지웠다.** 대신 언리얼이 같은 문제를 푸는 방식을 가져왔다.

**언리얼은 두 축으로 푼다.**
1. **참조 카운트 RHI 리소스**(`TRefCountPtr` · `FBufferRHIRef`) — 핸들을 든 쪽이 있으면 리소스가 살아 있어 댕글링이
   구조적으로 불가능하다.
2. **`FRenderResource` 전역 등록부** — RHI 리소스를 든 CPU 객체가 자기를 등록해 두고, 디바이스 수명 이벤트가 목록
   전체에 `InitRHI`/`ReleaseRHI` 를 **밀어 넣는다.**

둘은 **다른 문제**를 푼다. 1번은 "리소스 객체가 언제 죽는가", 2번은 "디바이스가 새로 만들어지면 전부 다시 만들어야
한다" 이다. 참조 카운트만으로는 2번이 풀리지 않는다(옛 디바이스의 리소스는 참조가 남아 있어도 새 디바이스에서 쓸 수 없다).

**우리가 겪은 문제는 2번이었다.** 그래서 2번을 가져왔다: `RHIRenderResource` 바탕 클래스에 `Mesh` 와 `MaterialInstance` 가
올라가고, `IRHIDevice::shutdown()` 이 **자원을 내리기 전에** 등록된 전부에게 알린다. 그 시점엔 디바이스가 아직 살아 있으니
든 쪽이 **제대로 돌려준다**. 죽은 뒤에 "살아 있었나" 를 되물을 필요가 없어졌고, 그래서 세대 번호도 사라졌다.

통보는 둘로 나뉜다 — 이것이 이 설계에서 가장 중요한 구분이다:
- `releaseRhi( pDevice )` : 디바이스가 **아직 살아 있다** → 제대로 돌려주고 핸들을 비운다. 정상 경로.
- `forgetRhi( pDevice )` : 디바이스가 **이미 없다**(shutdown 없이 사라진 경우) → 핸들만 비운다. `~IRHIDevice` 의 안전망.

**1번(참조 카운트)은 지금 하지 않는다 — 기각이 아니라 순서다.** 우리 RHI 핸들은 생 `uint64` 라 4백엔드의 create/destroy
API 를 전부 바꿔야 하고, 지금은 **핸들마다 소유자가 하나뿐이다**(정점 버퍼는 Mesh, 상수버퍼는 MaterialInstance, 구조버퍼는
GpuScene, 텍스처는 TextureCache). 참조 카운트가 값을 하는 것은 **한 리소스를 여럿이 나눠 들 때**다. 그 순간이 오면(예:
메시 버퍼를 여러 LOD·인스턴스가 공유) 그때가 1번을 들일 때다.

**결과.** `RHI::getDeviceGeneration()` · `_s_deviceGeneration` · `RHIResidentBuffer::_generation` 삭제. `RHIResidentBuffer` 는
{핸들, 디바이스} 두 값만 남았고 `isResident()` 는 `_buffer != 0` 이다 — 통보가 오기 때문에 그것으로 충분하다.
Shipping `App.exe` 2,618,368 → 2,625,024 B (등록부 +6.6 KB — 정확성에 치른 값이다).

**검증.** Debug GPU 18/18(`DeviceDeathInvalidatesGpuHandles` 가 `forgetRhi` 경로를 탄다), SmokeTest 19/19, nogpu 5/5,
린트 7/7, ASAN GPU 18/18. 교체 4구성 종료 0 · 경고 0 · 스크린샷 4.5k/30.8k/31.7k/31.0k. 패널 덤프 창 15 · 빈 패널 0.
Shipping 빌드·실행 종료 0.

### 2026-09-12 (세대를 올리는 일을 디바이스에게 맡긴다 — 그리고 그 변수가 필요한 이유 자체를 적어 둔다)

`_s_deviceGeneration` 이 왜 있는지 이력을 봤다. 모듈 리로드 때문이 아니다 — `ccdb7d52`(2026-09-06)에서 **`Mesh::releaseGpu`
가 소멸자에서 이미 죽은 디바이스를 역참조하던 UAF** 를 막으려고 들어왔다. 트리거는 셋이다: 엔진 종료(소멸자가 디바이스
뒤에 돈다) · 백엔드 교체 · 디바이스 유실. 모듈 리로드는 그중 하나도 아니다.

**구멍 하나를 찾아 막았다.** 주석은 "디바이스가 파괴될 때마다 올라간다" 였는데, 실제로 올리는 곳은 `RHI` 매니저의 두
경로뿐이었다. `RHI::createDevice` 로 직접 만든 디바이스(**테스트가 그렇게 쓴다**)는 죽어도 세대가 그대로라, 그 디바이스에
올린 메시가 계속 "상주" 라고 답했다 — `upload()` 는 새 디바이스에 옛 핸들을 돌려주고 `releaseGpu()` 는 죽은 디바이스에
destroy 를 부른다(세대를 도입한 바로 그 UAF). 세는 일을 **죽는 자리**(`IRHIDevice::shutdown` · 소멸자)로 옮겨 빠질 길을
없앴다. `RHI` 의 손수 올리던 두 줄은 지웠다 — 정본은 하나여야 한다.
재현/회귀: `RenderPassGpuTest.DeviceDeathInvalidatesGpuHandles` (수정 전 빨강). 이 테스트는 **동작만** 단언한다(세대든
다른 방식이든 구현이 바뀌어도 남는다).

**그런데 더 근본적인 지적이 있었다 — 맞는 말이다.** 이 변수는 "CPU 에셋(`Mesh` · `MaterialInstance`)이 GPU 핸들과 디바이스
포인터를 들고 직접 해제까지 한다" 는 설계의 **증상**이다. 디바이스는 이미 `shutdownInternal` 에서 자기가 만든 리소스를
전부 해제한다 — 즉 소유자는 디바이스인데, CPU 쪽이 핸들 사본을 들고 공동 소유자인 척하다 보니 "내 디바이스가 아직
살아 있나" 를 물어야 하고, 죽은 포인터로는 물을 수 없어 전역 숫자가 필요해졌다.

**그래서 다음 일은 "변수를 없애는 것" 이 아니라 "묻지 않아도 되게 만드는 것" 이다.** → 아래 1-0 에 설계 선택지를 적었다.

### 2026-09-12 (Engine 전수 감사 — 리로드 · 에디터 · 테스트 축 셋, 하나만 남아 있었다)

"Engine 에서 리로드 · 에디터 · 테스트 관련을 최대한 없앤다" 로 **포함 그래프를 기계로 전수 조사**했다. 결론부터:
세 축 중 둘은 이미 깨끗했고, 남은 하나(모듈 등록 해제)를 Shipping 에서 걷어냈다.

**에디터 축 — 이미 깨끗하다.** Engine 헤더 중 "Engine 밖 사용자가 Editor 뿐인" 것은 15개였지만, **전부 Engine 내부에서도
쓰는 실제 기능**이었다(AssetDatabase · Sequencer · 입력 장치 · 2D 컴포넌트 등). 즉 **에디터만을 위해 존재하는 Engine 파일은
하나도 없다.** 코드에 남은 "Editor" 어휘는 리플렉션 속성(`CallInEditor` — 인스펙터가 읽는 메타데이터)과
`RHICapabilities::_bEditorSupported`(그 백엔드에서 에디터가 뜨는지) 정도이고, 둘 다 Engine 이 **선언**하고 에디터가
**소비**하는 형태라 레이어 위반이 아니다. include 방향은 `CheckEngineLayers` 린트가 이미 강제한다.

**테스트 축 — 이미 깨끗하다.** "Engine 밖 사용자가 Test 뿐인" 헤더가 56개였지만 역시 전부 Engine 내부에서 쓰는 기능이다.
테스트 전용 API 는 남아 있지 않다(2026-09-11 에 다섯 개를 걷어낸 뒤로 늘지 않았다).

**리로드 축 — 등록 해제 경로를 Shipping 에서 걷어냈다.** Shipping 은 모듈을 정적 링크해 프로세스가 끝날 때까지 내리지
않는다. 그런데 `ModuleHost` 가 종료 때 `engine::unregisterModuleTypes` 를 불렀고, 그것이 `destroyComponentsOfModule` ·
`unregisterFactoriesByModule` · `unregisterTypesByModule` 까지 끌고 있었다 — **끝나는 프로세스에서 등록부를 비우는 일**이다.
전부 `#if !defined( SW_SHIPPING )` 로 갈랐다. 등록(`registerModuleTypes`)은 Shipping 도 쓰므로 그대로 둔다.
> **`rebindAllCachedTypeInfo` 는 리로드 전용이 아니었다.** 가드를 걸었다가 빌드가 깨져 알았다 — `registerModuleTypes` 가
> 매 등록마다 부른다(Shipping 의 최초 등록 포함). 되돌렸다.
> **테스트도 같은 기준으로 갈랐다.** `ModuleComponentsPurgedBeforeUnload`(EngineTest)와 SmokeTest 의 Dev 소스 목록
> (`LiveReloadManager.cpp` · `ModuleCompiler.cpp`)을 Shipping 에서 뺐다. SmokeTest 의 `TestSmoke.cpp` 는 예전부터
> Dev/Shipping 으로 갈라져 있었고 CMake 목록만 무조건이었다.

**결과.** Shipping `App.exe` 2,627,584 B → **2,618,368 B**. 오늘 하루 누적으로는 2,656,256 → 2,618,368 (**-37 KB**).

**검증.** Debug: SmokeTest 19/19, GameObjectManagerPoolTest 3/3, GPU 17/17, nogpu 5/5, 린트 7/7. Dev 실기동(에디터)
종료 0 · 크래시 0 · `Unloading module` 5줄 · 패널 덤프 창 15 · 빈 패널 0. Shipping: 빌드 · 앱 실행 종료 0 · SmokeTest
(정적 fillGameAPI 경로) 1/1.

**남겨 둔 것과 이유.** `GameObjectPtr` · `ComponentPtr` · `ObjectStateSerializer` 는 핫리로드가 만든 타입이지만
선택(SelectionManager) · 세이브게임 · 직렬화가 함께 쓰므로 리로드 전용이 아니다. `MaterialCache::reload` ·
`TextureCache::reload` 는 에디터가 쓰는 **에셋** 리로드다. RHI 백엔드 핫스왑은 런타임 기능이지 모듈 리로드가 아니다.

### 2026-09-12 (그래픽스의 Dev 전용 리로드도 같은 기준으로 — RHI 는 파일 감시자의 집이 아니다)

모듈 리로드를 App 으로 옮긴 뒤, 그래픽스 쪽 "리로드" 코드도 같은 기준(Shipping 이 안 쓰면 싣지 않는다)으로 훑었다.
**먼저 가려야 할 것이 있다: 그래픽스 쪽은 모듈 리로드가 아니라 셰이더·에셋 리로드다.** 성격이 다르므로 처리도 다르다.

**1) `LiveShaderManager` — RHI 에서 EngineLoop 으로, 그리고 Shipping 에서 제외.** 셰이더 파일을 지켜보다 다시 컴파일하는
개발 도구(211줄)인데 `RHI` 가 소유했다. RHI 는 **디바이스 추상**이지 파일 감시자의 집이 아니고, 정작 돌리는 쪽은
`EngineLoop`(등록 · update · triggerReloadAll)이었다. 소유를 EngineLoop 으로 옮기고 `RHI` 에서 멤버·접근자·include 를
지웠다. Shipping 빌드는 파일째 제외한다(Engine CMake). 생성은 예전처럼 `SW_DEBUG` 에서만 한다.
> **테스트도 함께 빠진다.** `TestLiveShader.cpp` 를 `#if !defined( SW_SHIPPING )` 으로 감쌌다 — 없는 기능의 테스트는
> 그 빌드에 있을 수 없다. (Shipping 에서 테스트가 빌드되지 않는 줄 알았는데 `TestBin/` 에 빌드된다. `Bin/` 만 보고
> 판단해서 링크 에러로 배웠다.)

**2) `Material::reloadShader` — 죽어 있었다.** 선언과 정의만 있고 **호출자가 0** 이었다(38줄). 지웠다. 머티리얼의 셰이더
레이아웃을 맞추는 일은 지금 `ensureShaderLayout` 이 로드 경로에서 한다 — 그 주석이 아직 `reloadShader` 를 가리키고
있어 사실대로 고쳤다.

**3) 남긴 것과 그 이유.** `MaterialCache::reload` · `TextureCache::reload` 는 **에셋 핫리로드**이고 호출자는 에디터
(`AssetHotReload` · `MaterialPanel`)다. Shipping 에는 EditorModule 이 없어 도달할 수 없지만, 이들은 "디스크에서 다시
읽는다" 는 평범한 리소스 캐시 API 라 지우면 에디터가 깨진다. `Mesh` 쪽에는 리로드 전용 코드가 남아 있지 않다
(디바이스 세대 판단은 `RHIResidentBuffer` 로 이미 정리됐다).

**결과.** Shipping `App.exe` 2,631,680 B → **2,627,584 B**. `RHI` 의 공개 표면에서 셰이더 도구가 사라졌다.

**검증.** Debug 에서 LiveShaderTest 3/3(기능이 그대로 동작한다), GPU 17/17, SmokeTest 19/19, nogpu 5/5, 린트 7/7.
Dev 실기동(에디터) 종료 0 · 크래시 0 · 패널 덤프 창 15 · 빈 패널 0. Shipping 빌드·실행 종료 0 · 스크린샷 배경 아닌 픽셀 48.6k.

### 2026-09-12 (모듈 리로드를 Engine 에서 App 으로 — Shipping 이 안 쓰는 기계를 싣지 않는다)

**문제.** `LiveReloadManager`(864 줄)가 `Source/Engine/Module/` 에 있고 `EngineLoop` 이 소유했다. 쓰는 쪽은 전부 App
(ModuleHost · ModuleCompiler · 리로드 단축키 · BackendSwapController)이었고, Shipping 에서는 **만들지도 않으면서**
코드가 바이너리에 그대로 실렸다. Engine 이 "모듈 리로드" 라는 개념을 알고 있는 것 자체가 레이어 위반이다 —
Engine 은 Editor·GameFramework·Games 를 모르기로 한 것과 같은 이유다.

**옮겼다.** `Source/App/Module/LiveReloadManager.*` 로 가고, Shipping 빌드에서는 `Source/App/CMakeLists.txt` 가 **파일째
제외**한다. `EngineLoop` 에서 멤버·생성·업데이트·종료·접근자가 전부 사라졌다.

**Engine 에 남긴 것은 창구 하나다.** 지연 로드 훅(`DelayLoadNotifyHook.cpp`)은 **모듈 DLL 안에** 컴파일되므로 App.exe
심볼을 링크할 수 없다. 그래서 훅이 묻는 것만 `IModuleHandleProvider`(그래프가 깨졌나 · 이 이름의 핸들이 뭔가)로 잘라
Engine.dll 에 두고, App 의 `LiveReloadManager` 가 그것을 구현해 스스로 꽂는다. Shipping 에서는 아무도 꽂지 않으므로
조회가 늘 nullptr 이고 훅은 곧장 폴백(Bin 에서 직접 로드)으로 간다.

**종료 순서는 훅으로 맞췄다 — 여기서 한 번 죽었다.** 모듈 DLL 은 "씬은 사라졌고 서비스는 아직 살아 있는" 좁은 구간에서
내려야 한다. 더 일찍 내리면 씬의 `GameObjectManager` 가 든 **컴포넌트 팩토리 델리게이트**가 사라진 코드를 가리켜
소멸자에서 죽고(실제로 그 스택으로 죽었다), 더 늦게 내리면 언로드가 쓰는 SceneManager·로거가 이미 없다(그렇게 하니
exit 3 에 언로드 로그가 통째로 사라졌다). 예전에는 그 자리가 `EngineLoop::shutdown` 한가운데였다 — 지금은
`EngineLoop::setOnScenesReleased( Delegate<void()> )` 훅으로 그 지점만 내주고, **Engine 은 거기서 무엇을 하는지 모른다.**

**곁다리로 치운 것 둘.** `BackendSwapController` 가 `EngineLoop` 을 거쳐 리로드 내부에서 모듈 핸들을 꺼내던 것을
`ModuleHost::getLoadedModuleHandle()` 로 바꿨다(모듈 수명을 아는 것은 ModuleHost 다). `Test/TestFramework/main.cpp` 는
`LiveReloadManager` 를 만들어 두고 **쓰지 않았다** — 지웠다.

**결과.**

| | 전 | 후 |
|---|---|---|
| Shipping `App.exe` | 2,656,256 B | **2,631,680 B** (-24 KB) |
| Engine 의 리로드 코드 | `LiveReloadManager` 864 줄 + EngineLoop 소유 | 없음 (창구 인터페이스 ~50 줄) |

**검증.** SmokeTest 19/19(핫리로드 시나리오 전부 — 그림자 복사 · 의존 캐스케이드 · ModuleCompiler 연동), Debug GPU 17/17,
nogpu 5/5, 린트 7/7. Dev 실기동(에디터) 종료 0 · 크래시 0 · `Unloading module` 로그 5줄로 예전과 같음 · 패널 덤프 창 15 ·
빈 패널 0. Shipping 빌드·실행 종료 0.

### 2026-09-12 (재 보고 둘은 기각, 하나는 고쳤다 — 그리고 Debug 로 성능을 재면 안 된다)

"머티리얼 상수버퍼와 PSO 도 워커로" 를 하려고 먼저 쟀고, **측정이 답을 바꿨다.** 세 가지를 기록해 둔다 — 특히 마지막은
앞으로의 모든 성능 작업에 걸린다.

**(1) Debug 숫자로 판단하면 틀린다.** 같은 워크로드(큐브 3000 · 메시 64종)의 프레임당 비용:

| scope | Debug avg | Release avg |
|---|---|---|
| `RT.GpuScene.uploadMaterials` | 668 us | 87 us |
| `RT.Pso.ensureMaterial` (교체 프레임 max) | 12,196 us | 3,170 us |
| `RT.GpuScene.applyInstanceCbs` | 11 us | 6 us |

Debug 는 sw 컨테이너마다 데이터 레이스 검출기가 원자적 진입/이탈을 하므로 **컨테이너를 많이 만지는 코드가 과장된다.**
성능 판단은 Release 로, 정확성 판단은 Debug·ASAN 으로.

**(2) 머티리얼 상수버퍼 — 기각.** `applyInstanceCbs` 가 Release 프레임당 **6us** 다. 워커로 옮길 이유가 없다. 게다가
`updateConstantBuffer` 는 DX11 이 즉시 컨텍스트, GL 이 현재 컨텍스트라 옮기려면 생성과 갱신을 갈라야 한다 — 6us 를 위해
치를 값이 아니다.

**(3) PSO 병렬 생성 — 기각.** 교체 프레임 변형 8개가 Release 에서 합계 ~3.2ms 다(Debug 12.2ms 는 위 (1)). 실제로 병렬로
만들어 보니 Debug 12.2 → 11.1ms, **9%** 뿐이었다 — 변형별 비용은 고르게 퍼져 있는데(2.9ms 셋 + 0.5ms 여럿) 이득이 안 난 것은
드라이버·셰이더 컴파일러 내부가 직렬화되기 때문이다. 3.2ms 를 9% 깎으려고 렌더 스레드에 스레딩을 더할 값이 아니라 되돌렸다.
> 되돌리는 길에 **데이터 레이스를 하나 배웠다**: 워커에게 `sw::vector` 를 넘겨 `listEntry[index]` 로 쓰면 비-const
> `operator[]` 가 검출기에 **쓰기**로 기록돼, 원소가 겹치지 않아도 "동시 쓰기" 로 잡힌다. 워커에는 `data()` 로 받은 원시
> 포인터를 주거나 `const` 참조로 읽어야 한다(메시 큐가 `const` 참조라 안 걸렸던 이유이기도 하다).

**(4) 대신 진짜를 찾아 고쳤다 — 배치마다 그룹 조회.** `uploadMaterialGroups` 의 마지막 루프가 배치마다 셰이더 **경로
문자열**로 해시 조회를 했다. 그룹은 한둘인데 배치는 수백이라 같은 답을 배치 수만큼 다시 구한 셈이다. 그룹당 한 번 풀어
표로 만들고 배치는 인덱스로 집게 했다:

| | Release 프레임당 |
|---|---|
| `RT.GpuScene.uploadMaterials` | 87 us → **6 us** |
| `RT.GpuScene.upload` (합계) | 198 us → **114 us** |

RT 렌더 시간(`RT.Graph.executeParallel` 약 680us)의 12% 를 차지하던 것이 사라졌다. 스레딩 없이, 매 프레임.

**덤: 계측을 남겼다.** `RT.GpuScene.applyInstanceCbs` · `uploadMeshes` · `instanceBuffer` · `RT.Pso.ensureMaterial` 스코프를
추가했다. 위 판단을 다시 확인하려면 이 줄들을 Release 로 보면 된다.

**검증.** Debug GPU 17/17, 4백엔드 실기동 종료 0 · 에러 0 · 배경 아닌 픽셀 43.7k 로 일치(그룹 조회 변경 전후 같다).

### 2026-09-12 (업로드를 그리기 앞으로 — 렌더 스레드가 만들던 것을 워커가 미리 만든다)

**문제.** 렌더 스레드가 그리다가 새 메시를 만나면 그 자리에서 정점 버퍼를 만들었다(`GpuScene::upload` → `Mesh::upload`).
새 메시가 한꺼번에 등장하는 프레임 — 첫 프레임, 그리고 **백엔드 교체 뒤 전량 재업로드** — 에서 RT 가 그 비용을 통째로
뒤집어썼다. 렌더 스레드는 그리기만 해야 한다.

**한 일.** `GpuUploadQueue` 를 두고, 게임 스레드가 `buildFromScene` 뒤 **스냅샷을 내보내기 전에** 아직 안 올라간 메시를
워커로 넘겨 병렬로 만든다. 워커 생성 가능 여부는 백엔드가 답한다(새 capability `_bThreadSafeResourceCreation`: DX12 · DX11 ·
Vulkan 은 버퍼 생성이 디바이스 레벨이고 핸들 테이블도 잠겨 있어 1, OpenGL 은 `glGen*` 이 현재 컨텍스트를 요구해 0 —
인라인으로 돈다). `-gv_gpuUploadQueue=0` 으로 끌 수 있다(A/B 와 비상 스위치).

**측정 (DX12, 큐브 5000 · 메시 400종, 측정 창 안에서 Vulkan 으로 교체해 전량 재업로드).**

| | RT.GpuScene.upload 최악 프레임 | GT.GpuUpload.flush |
|---|---|---|
| 큐 끔 (예전 동작) | 103,284 us | — |
| 큐 켬 | 8,281 us | 31,558 us |

RT 의 최악 프레임이 **12.5배** 줄었고, 같은 일을 워커가 나눠 해서 절대 시간도 103ms → 31ms 다. 평균이 3256 → 2506 us
로만 움직인 것은 120 프레임 중 한 프레임의 일이기 때문이다 — **이 최적화는 평균이 아니라 히치를 없애는 것이다.**

**덤으로 고친 것.** `Mesh::isUploaded()` 가 "핸들이 0 이 아니다" 만 봐서, 백엔드 교체 뒤 **죽은 핸들을 보고 "올라갔다"**
고 답했다(그래서 큐가 교체 뒤 아무것도 다시 올리지 않았다 — 측정하다 드러났다). 세대까지 보도록 고쳤다. 호출자가 큐
하나뿐이라 의미를 바꾸는 편이 옳았다.

**왜 RT 와 워커가 같은 메시를 동시에 만들지 않는가.** GT 가 스냅샷을 내보내기 **전에** flush 하므로, 어떤 메시든 그것이
든 패킷이 RT 에 닿기 전에 이미 상주한다. RT 가 직전 패킷을 그리며 같은 메시에 `upload()` 를 불러도 값을 읽을 뿐이다.
불변식이 깨지는 길은 "flush 를 기다리지 않고 내보내는 것" 하나뿐이라, flush 는 동기다.

**검증.** 새 테스트 `RenderPassGpuTest.UploadQueueMakesMeshesResidentBeforeDraw`(상주 · 중복 요청 dedupe · 멱등).
4백엔드 실기동(메시 64종 × 큐브 2000) 종료 0 · 에러 0 · 배경 아닌 픽셀 43.7k 로 일치, OpenGL 은 로그가 "인라인" 으로 뜬다.
교체 4구성 종료 0 · 경고 0. Debug GPU 17/17, nogpu 5/5, 린트 7/7. ASAN GPU 17/17 + 워커 업로드 경로 실기동(메시 128종)
sanitizer 0.

### 2026-09-12 (죽은 기계 둘을 지운다 — 그리고 하나는 죽지 않았다)

`FrameDoubleBuffer` **엔진 서비스**를 지웠다. 아레나를 프레임마다 스왑하는데 `getFrameDoubleBuffer()` 를 부르는 곳이
저장소에 하나도 없었다 — 매 프레임 스왑만 돌던 배선이다. Core 의 `FrameDoubleBuffer` 클래스와 그 테스트는 남는다(쓸 수 있는
도구다; 죽은 것은 배선뿐이다). `TextureCache::reinitializeAll` 도 호출자가 없어 지웠다 — 백엔드 교체 뒤 텍스처는 `acquire` 가
`isRhiValid()==false` 를 보고 다시 올리므로 동작에는 구멍이 없다. (그 뒤 되살리기도 등록부 통보가 됐다 — 아래 2026-09-12
"되살리는 절반" 항목.)

**`DebugDrawQueue` 는 남긴다.** 감사 때 "죽었다" 고 적었던 것은 틀렸다 — `ActionRoom` 이 실제로 채우고 있고, 없는 것은
소비 측(GameView ImGui)뿐이다. 죽은 폴백이 아니라 **아직 안 쓰는 기능**이고, 그 기준은 이미 정해 두었다(Graphics README 의
"남은 것" 절에 P1 으로 적혀 있다).

### 2026-09-12 (소유 정리 셋 — 모듈보다 오래 사는 씬, GPU 상주, 이름 재사용)

백엔드 교체 작업에서 나온 감사 결과를 실제로 닫았다. 셋 다 "무언가가 통째로 바뀌는데 그 안의 것을 가리키던 값이 살아남는다" 는
같은 병이다.

**1) 모듈이 내려가기 전에 그 모듈 타입의 인스턴스를 걷는다.** 언로드는 팩토리·타입·전역 변수만 등록 해제했다. 씬은 엔진이
소유해 모듈보다 오래 살므로, 모듈이 정의한 컴포넌트의 인스턴스가 남으면 vtable 이 사라진 객체가 씬에 남는다(지금 씬은 엔진
컴포넌트만 써서 드러나지 않았을 뿐, 게임 컴포넌트를 씬에 넣는 순간 터진다). `GameObjectManager::destroyComponentsOfModule`
을 두고 `unregisterModuleTypes` 가 **팩토리를 걷기 전에**(소멸자가 아직 있는 동안) 부른다. 지연 파괴 목록도 먼저 비운다 —
거기 남은 컴포넌트의 소멸자도 모듈 코드다.

**2) GPU 상주를 타입 하나로.** `RHIResidentBuffer`(핸들 · 디바이스 · 세대)를 두고 `Mesh::_vertex` · `MaterialInstance::_constant`
가 그것을 든다. `isResident()` 가 "올라가 있다" 와 "이 디바이스에 올라가 있다" 를 가르고 `getLiveDevice()` 가 해제해도 되는
디바이스만 돌려준다 — 두 클래스가 각자 적던 세대 검사 네 군데(업로드·해제·소멸·재업로드)가 한 곳으로 모였다. 이것이 백로그
1-0 의 "절충안: {핸들, 세대} 를 묶은 작은 타입" 이고, 그래서 그 항목은 닫았다.

**3) 파괴 대기 이름은 비어 있다.** `makeUniqueNameUnlocked` 가 이름 맵만 보고 판단해서, 리로드·교체 때 새 인스턴스가
`BenchMesh_0_2` 를 받고 `Duplicate name` 경고가 매번 둘씩 찍혔다. 이름은 **살아 있는** 오브젝트만 차지한다
(`isNameTakenUnlocked`). 짝으로, 파괴 쪽은 이름 맵 항목이 **자기 것일 때만** 지운다 — 같은 이름을 새 오브젝트가 이미
차지했을 수 있다.

**검증.** 새 테스트 둘(`GameObjectManagerPoolTest.ModuleComponentsPurgedBeforeUnload` ·
`PendingKillNameIsFreeForReuse`). 교체 4구성(DX12→Vulkan 에디터, DX12→DX11/GL, Vulkan→DX12) 종료 0 이고 **경고·에러 0**
(전에는 구성마다 `Duplicate name` 둘), 교체 뒤 스크린샷 30.8k~31.7k / 에디터 4.5k 로 그대로. Debug GPU 16/16, nogpu 5/5,
린트 7/7, 패널 덤프 창 15 · 빈 패널 0. ASAN GPU 16/16 · RenderPassTest 22/22 · 새 스위트 3/3.

### 2026-09-12 (백엔드 교체가 죽고, 살아도 빈 화면이던 것 — 뿌리 셋과 캐시 둘)

**증상.** 에디터에서 백엔드를 바꾸면 세그폴트(DX12→Vulkan · DX12→GL · Vulkan→DX12), DX12→DX11 은 살아도 그 뒤로 화면이
비었다. 재현은 메뉴에서만 가능했다 — `-gv_rhiSwapAtFrame=N -gv_rhiSwapTo=<0..3>` 을 두어 헤드리스로 돌린다(요청은 에디터
패널과 같은 `GlobalVariableInfo::setValueAsInt` 경로다. **C++ 대입은 변경 콜백을 부르지 않는다** — BackendSwapController 의
"되돌림 대입이 콜백을 다시 부른다" 주석이 틀려 있어 고쳤다). 한 번만 요청한다 — 프로파일러가 워밍업 뒤 프레임 수를 되돌려
같은 번호가 다시 오기 때문이다.

**뿌리 1 — 세대가 안 올랐다.** `RHI::recreateDevice` 가 `_s_deviceGeneration` 을 올리지 않았다(`shutdown` 만 올렸다).
`Mesh::upload` 는 디바이스 **포인터** 비교라 새 디바이스가 옛 주소를 받으면 옛 정점 버퍼를 그대로 넘겼고, `~MaterialInstance`
는 옛 디바이스에 해제를 요청했다. 세대를 올리고, `Mesh::upload` · `MaterialInstance::applyToGpu` 가 "핸들이 0 이 아니다" 와
"이 디바이스 것이다" 를 세대로 가른다.

**뿌리 2 — 게임 모듈의 생포인터.** `BenchScene::_listBenchMesh` 가 `MeshComponent*` 였다. 새 게임 인스턴스가 `onInitialize`
에서 큐브를 만든 직후 상태 복원이 씬을 통째로 지웠고(`deserializeSceneObjects` 의 `clear()`), 다음 `update` 가 죽은 주소에
`setLocalPosition` 했다(`GameObjectManager::isParallelTransformReadOnly` 에서 세그폴트). `ComponentHandle` 로 바꾸고, 벤치
오브젝트는 `onBeforeStateSerialize` 에서 걷고 `onAfterStateDeserialize` 에서 다시 만든다 — 절차 생성물은 스냅샷에 실을 이유가
없다(실리면 메시 없는 유령이 된다). 엔진 API 는 건드리지 않았다(`GameObject::setTransient` 를 넣었다가 뺐다 — 리로드는
Shipping 에 없는 개념이라 API 를 더럽히지 않는다).

**뿌리 3 — 빈 화면은 `GpuScene::clear()` 였다.** 그룹 목록(`_snapshot._listMaterialGroup`)만 지우고 셰이더 경로→인덱스 맵
(`_mapShaderPathToGroup`)을 남겨, `materialGroupFor` 가 범위 밖 인덱스를 돌려주고 배치에 머티리얼 버퍼가 안 실렸다. 불투명은
폴백(0)으로도 보이지만 **투명은 알파 0 이라 사라진다** — 벤치의 큐브 하나는 25% 규칙으로 유리다. `clear()` 가
`resetMaterialRegistry()` 를 부른다. 이걸 찾기까지 확인한 것들(전부 정상이었다): GT 후보·월드 행렬, RT 인스턴스·배치·VB·
PSO·레이아웃·뷰프로젝션·패스 CB, 컬링 게이트, 셰이더 캐시 비우기 유무, 모듈 재생성 유무, 병렬/직렬 기록, 인스턴스 애니메이션.
결정타는 **불투명 큐브만으로 돌리니 그려졌다** 는 것 — 그 다음 인프로세스 테스트가 유리에서만 `matBuf=0` 을 보여줬다.

**덤으로 닫은 캐시 둘.** 새 디바이스의 첫 PSO·버퍼는 옛 것과 **같은 번호**를 받는다(할당이 결정적). `FramePassContext` 의
마지막 바인딩 캐시(`_lastLayoutPso` + 파괴된 레이아웃 포인터, `_lastBindPso`, `_lastCbBuffer`)와
`RenderGraphExecutionContext` 의 리소스 상태 추적이 디바이스를 넘어 살아남고 있었다 — `releasePassResources` 와 `shutdown`
에서 잊는다. 이번 증상의 원인은 아니었지만(같은 번호가 나온 것을 확인했다) 언제든 같은 병이 될 자리다.

**검증.** `RenderPassGpuTest.RendererSurvivesDeviceRecreate`(유리 큐브 · 프레젠트 3프레임 · 같은 렌더러/새 렌더러) — 수정 전
빈 화면, 수정 후 통과. 실기동 7구성(DX12→Vulkan/DX11/GL 에디터 유무, Vulkan→DX12) 전부 종료 0, 교체 뒤 100프레임
스크린샷 배경 아닌 픽셀 헤드리스 30.8k~31.8k(기준 34.5k — 큐브가 흔들리는 위상 차이), 에디터 4.5k(기준 4.5k). 나머지
스위트는 커밋 메시지에.

**여기서 남겨 뒀던 둘은 아래 두 항목에서 닫았다** (중복 이름 경고, 죽은 `TextureCache::reinitializeAll`).

### 2026-09-12 (소유를 타입에 적었다 — 검사가 아니라 컴파일러가 막는다)

같은 뿌리의 세 사고 뒤에 나온 질문: "누가 누구를 소유하고 어떻게 참조하는지가 구조에 없다." 파이썬 검사는
사후 트립와이어일 뿐이라, **그 코드가 컴파일되지 않게** 했다. 무엇이 무엇을 지키는지:

| 사고 | 이제 막는 것 |
|---|---|
| RT 가 만든 값(`_indirectCommandCount`)을 GT 의 0 이 매 프레임 덮음 | **타입** — 옮겨지는 것은 `GpuSceneSnapshot` 하나뿐. RT 전용 상태는 그 타입에 없으니 옮겨질 수 없다. export/adopt 는 구조체 통째 복사/이동이라 손으로 고르는 목록이 사라졌다 |
| 스냅샷의 생포인터를 RT 가 역참조(UAF) | **타입** — 스냅샷 필드는 `shared_ptr` (Mesh 도 포함, 이번에 잡았다). C++ 가 "필드 추가" 자체는 못 막으므로 그것 하나만 `CheckRenderOwnership.py` 가 본다 |
| 모듈이 `make_shared` 한 객체를 엔진이 모듈 사후에 놓음 | **패스키 생성자** — `Material` · `MaterialInstance` · `Mesh` 의 생성자가 `create()` 만 만들 수 있는 `CreateKey` 를 요구한다. `make_shared<Material>()` 도 스택의 `Material m;` 도 **컴파일되지 않는다** (프로브 TU 로 확인: 넷 다 exit 1, `create()` 만 exit 0) |

덤으로 정리된 것: 패킷의 `_pSceneMaterial` 과 렌더러의 `_pBoundMaterial` 은 쓰는 곳이 없어 지웠다(생포인터 하나 더 제거).
`MaterialPanel` 의 값 멤버 `Material _material` · `ShaderBaker` 의 지역 `Material` · 테스트 여덟 곳이 `create()` 로
바뀌었다 — 컴파일러가 전부 찾아 줬다. `shareMaterial` 의 "빌릴 수 없으면 그리지 않는다" 분기는 그런 머티리얼이
존재할 수 없어 사라졌다.

> **`Material*` 인자와 ADL.** `Material` 이 `std::enable_shared_from_this` 를 상속하자 `make_shared<X>( Material* )` 가
> `std::make_shared` 와 `sw::make_shared` 사이에서 모호해진다. 팩토리 안에서는 `sw::make_shared` 로 한정한다.
> 바깥에서는 이제 부를 수도 없다.

> **정규식으로 `.` → `->` 를 바꿀 때 문자열 리터럴을 조심할 것.** `"defaultmaterial.material.meta"` 가
> `material->meta` 가 되어 nogpu 하나가 깨졌다 — 회귀가 아니라 내 편집이었다.

검증: nogpu 5/5 · Debug GPU 15/15 · ASAN(재현 1/1 · 렌더 15/15 · 22/22 · 머티리얼 15/15 · 리소스 21/21, sanitizer 0) ·
린트 7/7(+음성 시험 2/2) · 벤치 3구성 종료 0 · 픽셀 변화 없음 · 패널 덤프 15/0.

### 2026-09-12 (렌더 패킷이 머티리얼의 소유를 쥔다 — 죽은 retire 큐를 지우고, ASAN 으로 전후를 쟀다)

**증상(재현)**: `RenderPassGpuTest.MaterialLifetimeFollowsPacket` — 패킷을 내보낸 **뒤에** GT 가 머티리얼·인스턴스의
소유를 전부 놓고 그 패킷을 실행한다(에디터에서 오브젝트 삭제·인스턴스 교체가 RT 보다 먼저 일어나는 순서).
수정 전 ASAN: `heap-use-after-free` @ `MaterialInstance::applyToGpu` ← `applyInstanceCbsVal` ← `GpuScene::upload`
← `executePacket`, 해제 주체는 GT 가 놓은 `shared_ptr`. 예측한 경로 그대로였다.

**근본 수정 — 패킷이 소유한다.** 스냅샷(`GpuMeshBatch` · `GpuMaterialGroup::_listEntry` · 내부 후보)이 `Material` 과
`MaterialInstance` 를 `shared_ptr` 로 싣는다. `Material` 은 `enable_shared_from_this` 가 됐고 소유자(MaterialCache ·
BenchScene · 테스트)는 전부 `shared_ptr` 다. shared 로 소유되지 않은 머티리얼은 실을 수 없다 — 생포인터로 조용히 싣는
것이 곧 예전의 해제 후 사용이므로, 한 번 크게 말하고 그 메시는 그리지 않는다(`GpuSceneInternal::shareMaterial`).
`GpuMaterialRetireQueue` · `syncMaterialPins` · `advanceMaterialRetireFrame` 은 지웠다 — 소유가 패킷을 따라가면 큐가 할
일이 없다. 사용처 0 이던 공개 `GpuSceneDrawCandidate` 도 같이 지웠다.

**그 뒤 두 번 더 잡혔다 — 둘 다 소유가 제대로 넘어갔기 때문에 드러난 것이다.**
1. `~MaterialInstance` 가 **죽은 디바이스**에 `shutdown()` 을 불렀다(ASAN). 인스턴스가 이제 정당하게 RT 쪽 `GpuScene`
   에 살아남는데, `FrameRenderer::shutdown()` 이 그 스냅샷을 놓지 않아 렌더러 소멸(디바이스 사후)까지 들고 있었다.
   `shutdown()` 이 `releaseGpu` + `clear()` 로 디바이스가 살아 있을 때 놓는다. `EngineLoop::shutdown` 도 `_gtGpuScene`
   을 같은 시점에 비운다.
   > 소멸자의 `_gpuDeviceGeneration == RHI::getDeviceGeneration()` 가드는 종료 순서에서는 막지 못한다 — 세대는
   > `RHI::shutdown` 에서만 올랐고(정정: "올리는 코드가 없다" 고 적었던 것은 틀렸다) 그 시점엔 이미 늦다. 순서를
   > 맞춘 것이지 가드를 고친 것이 아니다. `recreateDevice` 가 세대를 안 올리던 것은 아래 백엔드 교체 항목에서 고쳤다.
2. **벤치가 종료에서 세그폴트했다** — 로그의 "Shutdown cleanly" 뒤, 즉 `SWGame.dll` 이 내려간 뒤다. BenchScene 이
   `make_shared` 로 만든 인스턴스의 **제어 블록(소멸 코드)이 게임 모듈 안에** 있고, 이제 엔진(GpuScene)이 마지막
   참조를 들고 있다가 모듈이 사라진 뒤 놓는다 → 없는 코드로 뛰어든다. "모듈의 정적은 핫리로드에서 죽는다" 의
   `shared_ptr` 판이다. `Material::create()` / `MaterialInstance::create()` 를 Engine 에 두고 모두 그것으로 만든다 —
   누가 마지막에 놓든 Engine 코드다. (부수: `Material*` 인자는 ADL 로 `std::make_shared` 를 끌어와 모호해지므로
   팩토리 안에서도 `sw::make_shared` 로 한정한다.)

**덤으로 고쳐진 잠복 버그**: 후보 수집에서 인스턴스를 블렌드 판단 **뒤에** 채워서 "인스턴스의 부모 머티리얼이 블렌드를
정한다" 는 폴백이 한 번도 걸리지 않았다. 순서를 바로잡았고, 그 폴백이 안 걸리는 데 기대던 테스트
(`GpuSceneTransparentDifferentKeysStaySeparate`) 는 투명 머티리얼(`glassmaterial`)을 부모로 준다.

**검증**: ASAN — 재현 테스트 수정 전 UAF → 수정 후 1/1, `RenderPassGpuTest` 15/15 · `RenderPassTest` 22/22 (sanitizer 0).
Debug — GPU 스위트 15/15, nogpu 5/5, 린트 4/4. 실기동 — 벤치 3구성(에디터 DX12/Vulkan · 에디터 없음) 종료 0,
배경 아닌 픽셀 18.6k/27.6k(변경 전과 같다), 패널 덤프 창 15개 · 빈 패널 0개.

### 2026-09-12 (같은 병 — "한쪽만 만드는 값을 스냅샷이 매 프레임 옮긴다" — 를 다시 훑었다)

`_indirectCommandCount` 의 모양으로 `GpuScene` 을 전수 조사했다: 필드 74개 각각의 **작성 함수(GT 빌드 vs RT 업로드)**
와 **스냅샷 전송 여부(export/adopt)** 를 표로 만들었다. 옮기는 필드는 여덟이고, 그중 RT 가 쓰는 값이 옮겨지던 것은
그 하나뿐이었다. 배치 안의 핸들(`_vertexBuffer` · `_materialCb`)과 머티리얼 그룹의 GPU 버퍼는 RT 가 **생략 경로 전에
매 프레임 다시 채우거나**(`applyInstanceCbsVal` · `uploadMeshesVal`) **RT 소유 맵에 셰이더 경로로 보관해**(`_mapMaterialGpu`)
스냅샷을 넘어 살아남는다 — 개수 필드가 따랐어야 할 설계다. GT 가 RT 전용 게터를 읽는 곳도 0.

**그런데 같은 이유로 생긴 다른 결함이 하나 있었다 — `GpuMaterialRetireQueue`.** (같은 날 아래 항목에서 ①안으로 고쳤다.)
- `GpuScene` 은 GT 빌더와 RT 소유자로 **같은 타입이 두 인스턴스**다. 큐도 인스턴스마다 하나씩이라
  `syncFromBatches`(pin) 는 GT 쪽(`buildBatches`)에서, `advanceFrame` · `flushAfterGpu` 는 RT 쪽에서 돈다 —
  **프로토콜의 두 반쪽이 서로 다른 객체 위에 있다.** GT 큐는 pin 만 쌓이고 RT 큐는 늘 빈 목록을 세고 있다.
- 게다가 큐의 공개 API 는 sync/advance/flush/clear 뿐이고 `_uniquePinned` · `_listRetiring` 을 읽는 코드가 **저장소에
  없다.** "GT 는 pin 이 풀리기 전에 파괴하면 안 된다" 는 헤더의 계약을 확인하는 곳이 하나도 없다.
- 그 계약이 지키려던 위험은 실재한다. 스냅샷은 `Material*` · `MaterialInstance*` 를 **생포인터로** 싣고
  (`GpuMeshBatch::_pMaterialInstance`, `GpuMaterialGroup::_listEntry`), RT 는 매 프레임 그것을 역참조한다
  (`applyInstanceCbsVal` 의 `applyToGpu`, `uploadMaterialGroups` 의 `getBuffer`). 소유는 `MeshComponent` 의 `shared_ptr`
  하나이고, 오브젝트 파괴나 `setMaterialInstance` 교체에 RT 동기화가 없다(`waitIdle` 은 씬 전환 한 곳뿐이고 그것도
  디바이스 idle 이지 **패킷 링(깊이 3)** 을 비우는 게 아니다). 즉 실행 중 메시 하나를 지우거나 인스턴스를 바꾸면
  최대 세 프레임 동안 RT 가 해제된 메모리를 읽을 수 있다. **재현은 하지 않았다** — 헤드리스로 인스턴스를 교체하는
  스위치가 없다(벤치는 시작 시 한 번만 `setMaterialInstance`). ASAN 프리셋 + 에디터에서 큐브 삭제로 확인할 수 있다.
- 고치는 방향(둘 중 하나, 앞이 낫다): ① 스냅샷이 `shared_ptr<MaterialInstance>` 를 실어 **패킷이 곧 수명**이 되게
  하고 큐를 지운다 — 배치·그룹 원소는 머티리얼 종류 수만큼이라 참조 카운트 비용은 작다. ② 큐를 RT 인스턴스 하나에
  두고 스냅샷이 pin 목록을 실어 나르며, GT 가 파괴 전에 RT 의 답을 묻는다 — 프로토콜이 늘고 지연이 생긴다.

**`FrameDoubleBuffer`(프레임 아레나 GT/RT 더블 버퍼)는 아무도 쓰지 않는다.** 서비스로 등록되고 매 프레임
`swapAndResetPrevious` 가 돌지만 `getFrameDoubleBuffer()` 호출부가 0이다. 결함은 아니고 죽은 장치다 — 남긴다/지운다는
메모리 설계의 결정이라 목록만 둔다.

### 2026-09-12 (에디터에서 카메라를 움직일 때만 메시가 보이던 것 — 렌더 스레드 값이 매 프레임 0 으로 덮였다)

**증상**: 에디터 뷰포트에서 WASD 로 날 때만 큐브가 보이고, 손을 떼면 사라진다. 그리드는 그대로다.

**헤드리스 재현**: `-EnableEditor -gv_benchMeshes=1 -gv_screenshot -gv_screenshotFrame=60` 의 `SceneColor` 가
배경 아닌 픽셀 **0** (렌더 스레드 on/off · Vulkan 전부). 에디터 없이는 26k. `-gv_gpuCulling=0` 을 주면 에디터에서도
18k → **GPU 커맨드 생성 경로**의 문제로 좁혀졌다.

**원인**: `GpuScene::_indirectCommandCount` 는 `upload()` 만이 세우는 값인데(마지막 업로드의 간접 인자 개수),
`upload()` 는 렌더 스레드에서만 돈다. 게임 스레드의 `_gtGpuScene` 은 업로드를 하지 않으므로 그 값이 늘 0 이고,
`exportCpuSnapshot` → `adoptCpuSnapshot` 이 그 0 을 **매 프레임** RT 로 옮겨 RT 값을 덮어썼다.
- 더티 프레임(카메라 이동 · 씬 변경): 뒤이어 `upload()` 전체 경로가 돌며 값을 다시 세운다 → 보인다.
- 조용한 프레임: `upload()` 가 재업로드 생략 경로로 가서 값이 0 인 채 `dispatchCullAndSort` 에 들어간다 →
  컬링 컴퓨트가 **배치 0개**로 디스패치 → 간접 인자가 0 으로 남고 `drawIndirect` 가 아무것도 안 그린다.

동기 프로브로 확정: 에디터 조용한 프레임 `inst=1 cmds=0`, 에디터 없는 벤치는 `cmds=1` — 벤치는 매 프레임
`dirty=1` 이라(회전 큐브) 조용한 프레임이 아예 없어서 기준선이 이 구멍을 가렸다.

**수정**: 이 필드는 스냅샷에 싣지 않는다 — RT 가 `upload()` 에서 정한 값이 다음 업로드까지 유효하다.
수정 후 같은 재현 4구성 전부 18.6k(에디터) / 27.7k(에디터 없음). 패널 덤프 창 15개 · 빈 패널 0개.

> **같은 모양을 조심할 것.** 스냅샷은 "GT 가 만든 것" 만 옮겨야 한다. RT 가 업로드하면서 파생하는 값을
> 함께 옮기면 GT 의 빈 값이 정본을 덮는다. `_listOpaqueBatch`/`_listTransparentBatch` 도 GT 가 만들고 RT 가
> `upload()` 에서 핸들만 채워 넣는 구조라 지금은 괜찮지만, RT 만 아는 값을 GpuScene 에 더할 때 이 목록을 보라.

**같은 세션 — 3D 그리드의 축 색이 X·Z 가 뒤바뀌어 있었다.** `drawAdaptiveGrid` 의 3D 분기는 `x == 0` 인 선
(Z 방향으로 뻗는 **Z 축**)을 빨강으로, `z == 0` 인 선(**X 축**)을 파랑으로 그렸다. 오리엔테이션 큐브 · ImGuizmo 는
X 빨강 · Y 초록 · Z 파랑이다. 축 색을 `EditorViewportClientInternal::_s_kColorAxisX/Y/Z` 하나로 모아 그리드와
큐브가 같은 값을 보게 했다(2D 분기는 원래 맞았다).

### 2026-09-12 (`ComputePass` 를 지웠다 — 컴퓨트 셰이더는 살아 있고, 래퍼만 죽어 있었다)

호출부 0 배선 API 목록에서 `ComputePass::bindSrv/bindUav` 가 나와 "컴퓨트가 안 쓰이나" 를 확인했다.
아니다 — `instanceanim` · `gpucull` · `instancesort` 세 셰이더는 매 프레임 돌고, `FrameRenderer::dispatchInstanceAnimation`
/ `dispatchCullAndSort` 가 **커맨드 리스트에 직접** 건다. 죽은 것은 그 위에 얹으려던 `ComputePass` 래퍼 —
메서드 둘이 아니라 **클래스 전체**가 생성처 0이었다(자기 파일 둘 + 테스트의 `#include` 한 줄). 앞서 문서를
"실제 헤더에 맞춰" 고친 적이 있는데, 그 헤더를 만드는 코드가 없다는 것은 그때 보지 못했다 — 문서와 헤더가
서로 맞는 것과 둘 다 실제 경로와 무관한 것은 다른 문제다. 클래스·테스트 include·README 5.11 예제·Renderer README
항목을 함께 지우고, 5.11 은 실제 경로(직접 바인딩 + 전이)로 다시 썼다.

### 2026-09-12 (같은 병을 앓을 자리를 다시 훑었다 — 넷은 깨끗했고, 워처 오버플로 하나가 진짜였다)

이번 세션에서 잡은 결함의 **모양**으로 저장소를 다시 봤다: 조용히 기본값으로 떨어지는 설정 ·
영영 매칭될 수 없는 필터 · 두 벌로 적힌 목록 · 호출부가 0인 배선 API.

**1) 설정 폴백 — 이상 없음.** `JsonSerializer::loadFile` 은 키 하나만 어긋나도 파일 전체를 실패로
돌리므로, `Config/**/*.json` 의 모든 `_키` 를 저장소의 `PROPERTY()` 이름과 정적으로 대조했다 →
고아 키 0 (`PackConfig.json` 의 `_compression_note` 는 파이썬 쿠커가 읽는 파일이라 무관하다).
실기동 경고·에러 0. 공백 구분 float 벡터도 더는 없다.
> 남은 것: `EditorConfig::loadFromHost` 는 실패 시 "cpp defaults" 라고 적지만 실제로는 **부분적으로
> 채워진** 구조체를 그대로 쓴다 — 메시지가 거짓이다. 고치지 않았다(동작은 무해).

**2) 매칭 — 이상 없음.** `hasExtension` 에 복합 접미사를 넘기는 곳 0. `startsWithPathComponent` 의
열두 호출부는 전부 절대↔절대 또는 상대↔상대다. Windows·Linux 워처의 이벤트 모양(`_directory` =
감시 루트 절대 경로, `_filename` = 그 아래 상대 경로)이 같다 — 그래서 절대 접두어 수정이 양쪽에 맞는다.

**3) 워처 오버플로 — 진짜였고, 고쳤다.** 세 워처 모두 알림을 잃으면 `_filename` 이 빈 `Modified`
하나(리스캔 신호)를 보내는데, 확장자 필터가 그것을 **조용히 버리고 있었다**. "파일 변화가 수백
개면 반영되나" 의 답이 "아니오, 그리고 아무도 모른다" 였다. 게다가 Windows·Mac 은 큐 상한이 없어
폴링이 밀리면 끝없이 자랐고, Windows 는 Linux 가 하는 연속 중복 병합도 없었다.
- 리스캔 신호는 `ReloadFileManager::expandRescanEvents` 가 **직전 드레인 이후 mtime 인 파일**로
  펼친다. 소비자는 평소와 같은 파일 단위 이벤트만 받는다 — 리스캔이 있었는지 알 필요가 없다.
  **평소에는 폴링이 없다**: OS 가 유실을 알린 그 한 번만, 감시 트리 안에서, stat 으로 끝낸다.
  (기준선 맵을 시작 시 심는 안도 있었지만 시작 훑기와 이벤트마다의 맵 갱신이 든다 — 시각 창이 더 싸다.
  `FileUtil::getCurrentFileTimestamp` 를 더해 파일 시각과 **같은 시계** 로 잰다; `system_clock` 은 기원이 다르다.)
- 큐 상한 `IFileWatcher::_s_kMaxQueuedEvent`(4096) 를 셋이 공유한다. 처음엔 `constant` 네임스페이스에
  뒀는데, 이 계층 밖에서 쓰지 않는 값이라 클래스 정적으로 옮겼다 — `constant` 는 가로지르는 값만.
- Windows 도 같은 파일·같은 동작 연속은 하나로 합친다(한 번 저장에 LAST_WRITE·SIZE 가 잇달아 온다).

실측(상한을 2로 낮춰 강제): 파일 셋을 20번씩 저장 → 리로드 **60/60**, 리스캔 19회가 유실분을 되찾았고
경고는 0. 상한 4096 복구 후 정상 경로는 1건 → 1건. Linux 워처는 WSL 에서 문법 검사 통과.
**Mac 워처는 이 저장소에 프리셋이 없어 컴파일된 적이 없다** — 이번 수정도 읽어서만 확인했다.

**4) 두 벌 목록 — 하나 남음(사소).** `{ ".ini", ".kv" }` 가 `LocalizationManager` 와 `StringTable`
에 각각 있다. 같은 모듈 안이고 둘 다 한 줄이라 두었다.

**5) 호출부 0 인 배선 API — 다섯, 손대지 않았다.** 정규식 스캔은 매크로 호출(`SW_LOG_CALLER` →
`registerCaller`)을 못 보므로 후보 34개를 "저장소 전체 언급 ≤ 2회" 로 다시 걸렀다:
`ComputePass::bindSrv` · `bindUav` / `KeyboardDevice::notifyTextInput`(텍스트 입력을 읽는 쪽도 없다) /
`AssetEditorManager::registerAssetEditor`(오버라이드 표가 항상 비어 있다) / `InputManager::unregisterDevice`
(등록만 셋, 해제는 0). 지우거나 잇거나는 각 기능의 결정이라 여기서는 목록만 남긴다.

### 2026-09-12 (확장자를 한 표로 모으고, 죽은 것은 지우고, 남은 것은 실제 동작에 붙였다)

에셋 확장자가 **세 곳**에 적혀 있었다. `EditorAssetType.cpp` 안에서만 두 벌(매칭 규칙 표와 패널
접미사 매핑), 그리고 Engine 의 `ReflectionConstants.h` 에 또 한 벌. 두 벌이면 한쪽만 늙는다 —
실제로 `._material` 과 `.mat` 은 양쪽에 있었고 `.hlsli` 는 양쪽에 없었다.

**1) 에디터 표 여섯을 하나로 묶었다.** 종류 하나를 고치려면 매칭 규칙 · 패널 제목 · 도구 패널
목록 · 패널 접미사 매핑 · 브라우저 필터 · "Other" 제외 목록 여섯 군데를 찾아야 했다. 이제
`AssetKindRow` 한 줄이 그 종류에 대해 에디터가 아는 전부를 들고, 나머지 다섯은 **거기서
만들어진다**. 한 종류가 줄을 둘 이상 가질 수 있다(SpriteClip 은 문서 접미사 + 이미지 확장자).
줄 순서가 곧 브라우저 필터와 도구 패널의 표시 순서다.

**2) Engine 의 필터 표는 지웠다.** `ReflectionConstants.h` 의 `kArrAssetFilters` 는 Engine 에 있는데
확장자는 에디터가 아는 것이다(Engine 은 Editor 를 못 본다). 게다가 **아무도 읽지 않았다** —
인스펙터의 에셋 필드는 드래그앤드롭 + 텍스트라 파일 다이얼로그를 열지 않는다. 유일한 독자는
테스트 한 줄이었다. 표와 `PropertyMetaHint::getAssetFilter` 를 함께 지웠다. 에셋 피커를 실제로
만들 때는 에디터 쪽 `EditorAssetTypeRegistry` 를 쓰면 된다.

**3) 죽은 확장자를 걷어냈다.** `._material`(오타로 보인다) · `.mat`(파일도 코드도 0) ·
`.pfb`(`loadPrefab` 이 모르는 이름) · `.glsl` `.vert` `.frag`(엔진은 HLSL 전용) · `.csv`(참조 0) ·
`.mp3` `.ogg`(디코더 없음 — XAudio2 는 `.wav` 만 읽는다). `.spv` 도 뺐다 — **구운 산출물**이라
셰이더 소스로 세면 콘텐츠 브라우저가 빌드 출력 178개를 에셋으로 보여 준다.
빠져 있던 `.hlsli` 는 넣었다(공유 헤더는 진짜 셰이더 소스다). 지운 것은 테스트에 **음성으로**
못 박았다 — 되살아나면 거기서 걸린다.

**4) 분류만 되고 아무 데도 안 닿던 확장자를 붙였다.** 지금까지 핫리로드 처리기는 머티리얼
하나였다.
- **프리팹** — `PrefabManager::reload` 추가(캐시만 버린다. 이미 스폰된 오브젝트는 그대로다 —
  그건 오버라이드 전파라는 다른 기능이다).
- **텍스처** — `TextureCache::reload` 추가. `Texture2D` 객체는 그대로 두고 내용만 갈아 끼운다
  (머티리얼이 포인터를 빌려 가 있다). bindless SRV 인덱스가 곧바로 프리리스트로 돌아가므로
  `waitIdle` 이 필요하다 — `MaterialCache::reload` 와 같은 이유다.
- **소스 이미지**(`.png` `.jpg` `.jpeg` `.tga` `.bmp`) — `textures_raw/` 아래면 옆 `textures/` 의
  DDS 로 **굽는다**. 런타임이 읽는 것은 DDS 뿐이다(`Texture2D::loadFromResource` → `DdsLoader`).
  구운 DDS 가 다시 이벤트로 돌아와 캐시를 갱신한다.
- **`.hdr` 은 굽지 않는다.** 디코더가 stb_image 의 8비트 경로라 구우면 값이 잘린다. 조용히
  망가뜨리는 대신 **이유를 로그로 말한다**.

**`TextureWatcher` 를 지웠다.** 프로덕션 호출부가 0인 두 번째 감시자였고, 그 베이크 로직이
이제 `AssetHotReload` 안에 있다. 감시자는 하나다.

**복합 접미사가 한 번도 안 걸리고 있었다.** `ReloadFileManager::isExtensionAllowed` 가
`FileUtil::hasExtension`(마지막 점 뒤만 본다)을 써서 `.prefab.xml` 이 영영 매칭되지 않았다.
접미사 비교로 바꿨다 — 프리팹 리로드가 등록은 되는데 이벤트를 못 받던 원인이다.

> **`editordata.json` 의 `_listHotReloadExtension` 은 비워 두는 것이 기본이다**(= 처리기가 있는
> 확장자 전부). 손으로 목록을 박아 두면 처리기가 늘어도 따라오지 않는다 — 실제로 `.material`
> 하나만 적힌 채로 텍스처·프리팹 처리기가 감시 밖에 있었다. 좁히고 싶을 때만 적는다.

실측(`-EnableEditor`, 동기 프로브): 감시 확장자 12개, `.material` · `.prefab.xml` · `.dds` ·
`.hdr` 수정이 각각 정확히 1건씩 디스패치됐고 `.hdr` 은 "8비트로 잘린다" 안내를 남겼다.
패널 덤프 기본 **창 15개 · 빈 패널 0개**, 전부 열기 **창 30개**(빈 것으로 보이는 넷은 컨테이너
둘 · `gizmo` 오버레이 · `Sequencer/00000379` — **변경 전 실행과 완전히 같다**. 위 기준선 줄의
"29개" 는 이 PC 실측과 다르다).

### 2026-09-12 (설정 파일의 경계를 "누가 쓰는가" 로 다시 그었다)

`EditorConfig.json` 과 `editordata.json` 의 차이가 이름만으로는 보이지 않았다. 실제 차이는
**앱이 다시 쓰는가** 다 — `EditorConfig::saveToHost()` 는 테마를 저장할 때 구조체에서 파일
**전체를 새로 생성**한다(저장소의 `_themeWindowRounding: 10` 이 C++ 기본값 `4.0f` 와 다른 것이
그 흔적이다). `EditorData` 에는 저장 경로가 아예 없다 — 읽기 전용이다.

그런데 경계가 그 선을 따르지 않았다. 손으로 정하는 경로·파일명(`_configFolder` ·
`_editorConfigFolder` · `_imguiIniFile` · `_windowsIniFile` · 툴 데이터 파일 넷)이 **기계가
덮어쓰는 파일** 안에 있었다. 그 파일에 주석이나 손으로 고른 순서를 남기면 테마 한 번 바꿀 때
사라진다. 여덟 필드를 `EditorData` 로 옮겼다. `EditorConfig` 에는 이제 테마만 남는다.

`EditorConfig._editorData`(editordata.json 의 경로)도 없앴다. 기본값 아닌 값을 넣은 곳이 없었고,
경로의 정본은 이미 `Scripts/common/Constants.py` 다 — 손으로 적는 경로를 기계가 다시 쓰는 파일에
두는 것 자체가 위 문제였다. `loadFromHostPath()` 는 인자 없이 상수로 간다.

검증: 실기동에서 `imgui.ini` · `windows.ini` 가 그대로 `Config/Editor/` 에 해석됐고(옮긴 필드가
실제로 읽힌다는 뜻), 패널 덤프 창 15개 · 내용 없는 패널 0개.

### 2026-09-12 (에셋 핫리로드를 에디터로 내렸다 — 그리고 그것도 한 번도 돈 적이 없었다)

`ResourceManager` 가 `Resource/` 전체에 파일 감시를 걸고 있었다. 배포본에도 감시 스레드가 뜨고
이벤트가 쌓이는데, 그 이벤트로 할 일(에셋을 고쳐서 바로 보기)은 **에디터에만 있다.**
`ReloadFileManager` 를 `Source/Engine/Module/` → `Source/Editor/Common/Workspace/` 로 옮기고,
`EditorContext` 가 `AssetHotReload` 로 소유한다. 에디터가 없으면 감시도 없다.
(`SW_API` 도 뗐다 — Engine.dll export 매크로라 EditorModule 에 붙으면 dllimport 가 된다.)

**옮기려고 열어 보니 이 기능도 돌고 있지 않았다.** 원인이 두 겹이다.

1. **감시 접두어가 상대 경로였다.** `registerWatch( "Resource/", ... )` 인데, 워처가 올리는
   이벤트의 `_directory` 는 워처가 연 **절대 경로**(`D:/…/Resource`)다. 접두어 비교
   (`startsWithPathComponent`)가 **항상** 실패한다. 이제 `ResourceUtil::getRootFolderPath()` 를 넘긴다.
2. **확장자가 `.mat` 이었다.** 저장소의 머티리얼은 전부 `.material` 이라 하나도 안 걸린다.
   게다가 필터에는 일곱(`.mat .prefab .json .xml .glTF .gltf .obj`)이 적혀 있고 처리는 `.mat` 하나뿐이라,
   나머지 여섯은 이벤트를 받아 놓고 조용히 버렸다 — 감시 비용만 내는 자리였다.

**그래서 목록을 하나로 만들었다.** 코드에는 "다시 읽는 방법이 있는 종류" 표만 둔다
(`AssetHotReload` 의 `_s_arrReloadRule`, 지금은 Material 하나). 확장자는 이미 정본이 있다 —
`EditorAssetTypeRegistry`(`appendSuffixes(kind)` 추가). 감시 필터와 디스패치가 같은 곳에서 나오므로
"필터는 통과했는데 처리기가 없다" 가 생길 수 없다.

**무엇을 감시할지는 설정이 정한다** — `editordata.json` 의 `_listHotReloadExtension`.
비우면 처리기가 있는 확장자 전부. 처리기가 없는 확장자를 적으면 **경고를 남기고 뺀다**
(조용히 버리지 않는다). 변경이 쏟아지는 폴더를 잠시 빼려면 이 목록을 좁히면 된다.

실측(`-EnableEditor`, 동기 프로브 — 비동기 로거는 강제 종료 때 마지막 줄을 잃는다):
`.material` 수정 → `RELOAD engine/materials/defaultmaterial.material` 1건, 무관한 `.txt` 수정 → 0건.
설정을 `[.material, .png]` 로 두면 워치 1개 + `'.png' 는 처리기가 없어 무시합니다` 경고.
패널 덤프 **창 15개 · 내용 없는 패널 0개**(기준선과 같다).

**`editordata` 를 XML → JSON 으로 바꿨다** (`Config/Editor/editordata.json`). 읽는 쪽은
`XmlSerializer::loadFile` → `JsonSerializer::loadFile` 한 줄이고, 경로 상수는
`Scripts/common/Constants.py`(SSOT) → `GenerateCMakeConstants.py` 로 다시 생성했다.

> **float4 를 JSON 에 쓸 때는 쉼표다.** 텍스트 핸들러
> (`SerializeContext` 의 `registerVectorTextHandler`)가 `','` 로만 자른다. 공백 구분
> (`"0.12 0.15 0.18 1.0"`)은 파싱에 실패하고, `JsonSerializer::loadFile` 은 필드 하나만 실패해도
> **파일 전체를 버리고 기본값으로 돌아간다**. `Config/Engine/EngineConfig.json` 의 `_window._clearColor`
> 가 그 형태로 들어 있었다 — 값이 마침 C++ 기본값과 같아 아무도 눈치채지 못했다. 같이 고쳤다.
> (JSON 에는 주석이 없다. XML 에 달아 두던 설명은 `EditorData.h` 와 `Config/README.md` 로 옮겼다.)

**아직 남은 것 — 감시 자체의 한계** (이번 이동과 별개, `Core/File`):
- Windows `_listEventQueue` 에 상한이 없다 (Linux 는 `kMaxQueuedEvent = 4096` + 합성 리스캔).
- Windows 오버플로 리스캔 이벤트는 `_filename` 이 비어 있어 확장자 필터에 걸려 조용히 버려진다.
- `Editor/Common/Asset/TextureWatcher` 는 프로덕션 호출부가 **0개**다 — 같은 자리에 감시자가 둘 있는 셈이다.

### 2026-09-12 (기동 순서를 바로잡았다 — 보정 재실행이 있다는 것 자체가 신호였다)

`ResourceUtil` 싱글턴 이야기에서 시작했는데, 값어치 있는 것은 싱글턴이 아니라 **그 주위의 순서**였다.
넷 다 "소유와 전제가 흐릿해서" 생긴 것이다.

**1) 실패해도 "초기화됨" 으로 남았다** (`84f89d38`). `ResourceUtil::initialize` 가 플래그를 본문
**맨 앞에서** 켰다. 루트를 못 찾아 `false` 로 나가도 플래그는 켜진 채라, 실패를 검사하는 유일한
호출부(`ResourceManager::initialize`)가 그 다음에 `true` 를 받아 **빈 경로로** 팩 마운트와 레지스트리
로드를 진행했다. Debug 는 어설트가 먼저 멈추지만 Release/Shipping 은 로그만 남기고 계속 간다.
`std::once_flag` 로 바꾸고 플래그의 뜻을 "시작했다" → **"성공했다"** 로 바꿨다.
함정: `call_once` 는 콜러블이 예외 없이 반환하면 완료로 표시한다 — 본문을 그냥 감싸면 `false` 가
사라지므로 성패를 플래그에 남기고 그것을 돌려줘야 한다.

**2) 진단이 통째로 사라지고 있었다** (`ff2ec3fe`). `App::initialize` 맨 앞, **로거가 서기도 전에**
`ResourceUtil::initialize()` 를 불렀다. `RootFolder` 로그와 어설트 메시지가 갈 곳이 없었고 반환값도
보지 않았다. once_flag 라 두 번째 호출은 다시 찍지 않으므로 **첫 호출이 로거 뒤여야** 한다.
`EngineLoop::initialize` 의 로거·크래시핸들러 직후로 옮겼다.
실측 — 정상 실행: 전 `(아무 줄도 없음)` → 후 `RootFolder : D:/...`.
`Resource/` 없는 곳: 전 출력 없음 → 후 `ASSERT failed / RootFolder를 찾지 못했습니다`.

**3) 설정이 리소스 초기화보다 뒤였다** (`cca44202`, 테스트 호스트는 `366a615d`).
`ResourceManager::initialize`(팩 마운트 + 레지스트리 로드)가 `GameConfig` 활성화 **전에** 돌아서
게임 도메인이 빠진 채 실렸고, 그래서 뒤에서 **같은 일을 한 번 더** 해서 메우고 있었다.
ConfigManager 가 필요한 것은 `ResourceUtil::getProjectFolderPath()` 하나뿐이라 설정을 앞으로 옮겼다.
테스트 호스트는 한 가지 더 나빴다 — `setSearchPriority` 가 `GameConfig::setActive` **보다 먼저**라
그 보정에서도 "game" 토큰이 안 풀렸다.
실측(`App.exe` 의 "에셋 레지스트리" 로그 줄 수): **2줄 → 1줄**. EngineTest 도 같다.

**4) 전제를 인자로 끌어올렸다** (`0ab705e0`). `ResourceManager::initialize()` 가 콘텐츠 적재까지
하면서 "설정이 먼저" 라는 전제가 시그니처에도 호출부에도 없었다. `initialize()`(메커니즘) /
`mountContent( priority )`(콘텐츠)로 갈랐다. 우선순위 적용과 마운트는 `mountContent` **안에서**
붙여 뒀다 — 호출자에게 두 단계로 맡기면 순서를 뒤집거나 사이에 다른 것을 끼울 수 있다.
경로 해석만 필요한 쪽은 이제 `initialize()` 만 부르고 팩 마운트 비용을 내지 않는다.

**전수 확인은 런타임 계측으로 했다.** 정적 grep 은 무해한 것만 잔뜩 나온다. "게임 팩이 검색 루트에
들어오기 전에 도는 리소스 조회" 를 세니 App·에디터·테스트 전 실행에서 **0건**이었고, 음성 대조로
수정 전 순서를 되돌리니 **12건**(`assetregistry.txt`·머티리얼 `.meta`)이 잡혔다 — 계측이 실제로
동작함을 확인한 뒤의 0 이라야 의미가 있다. "로거가 서기 전에 버려지는 로그" 도 같은 방식으로 셌다:
App 0건, 테스트의 17건은 전부 `ScopedLogSuppressor` 가 일부러 끈 것이었다.

**남은 것**: `ResourceUtil` 을 소유 객체로 바꾸는 전면 전환은 하지 않았다 — 호출부 247곳인데
`SW_API` 로 export 되어 모듈들이 이미 같은 실체를 본다(DLL 경계 이득 없음). 실익이던 위 두 가지만 가져왔다.

### 2026-09-12 (셰이더·코드 핫리로드 — 셋 다 "돌고 있다고 믿었지만 안 돌던" 것이었다)

**셰이더 자동 재컴파일은 한 번도 동작한 적이 없었다** (`81b027b8`).
`LiveShaderManager::attachReloadFileManager` 의 호출부가 **하나도 없어서** `registerWatch` 가 영영
돌지 않았다. 앞으로도 계획이 없으므로 감시 경로를 지웠다(`notifyFileChanged` · include 역추적 표 ·
`ShaderIncludeResolver` 포함). 수동 리로드(Ctrl+F8)는 남긴다.

**그런데 수동 리로드도 안 돌고 있었다** (`84b9d42f`). `watchShader` 역시 프로덕션 호출부가 0이라
`triggerReloadAll` 이 **빈 표**를 돌았다. 등록표를 없애고 **`ShaderCache` 를 정본**으로 썼다 —
이 실행에서 실제로 컴파일된 셰이더가 곧 리로드 대상이다(`ShaderCacheEntry` 가 `ShaderCompileDesc`
를 함께 든다). 표를 둘 두고 한쪽만 채워지는 구조가 원인이었다.

**`.hlsli` 수정이 무시됐다.** 컴파일 캐시 키는 `max(.hlsl mtime, 공유 헤더 타임스탬프)` 인데 뒤쪽이
`ShaderBaker::getSharedHeaderTimestamp` 의 함수 지역 static 이라 **프로세스당 한 번**만 계산된다.
처음엔 캐시를 통째로 우회해서 고쳤는데, 그러면 **편집과 무관한 셰이더까지 전부** 다시 컴파일된다
(실측 35개 전부). 우회 대신 `invalidateSharedHeaderTimestamp()` 로 키를 정확하게 만들고 캐시가
거르게 했다(`8bd0e265`). 바이트코드가 같으면 로컬 캐시에 쓰지도 않는다 — 덮어쓰면 mtime 이
새로워져 무관한 셰이더까지 다시 읽히고 PSO 도 전부 재생성된다.

실측(실행 중 편집 → 리로드): 아무것도 안 고침 **35개 확인 / 0개 갱신**,
`fullscreenblit.hlsl` 픽셀 수정 **35개 확인 / 1개 갱신**(PSMain=WRITE, VSMain=SKIP, 나머지 33개 캐시 히트).

> **측정 함정 둘.** (1) 앱을 띄우기 **전에** 고치면 기동 시점 `getOrCompile` 이 이미 반영하므로
> "0개 갱신" 이 나온다 — 리로드는 실행 중 편집이 대상이다. (2) `FileUtil::getFileTimestamp` 는
> **초 단위**라 같은 초에 두 번 쓰면 값이 같다. 테스트는 수정 시각을 명시적으로 밀어야 결정적이다.

**코드 핫리로드: 게임은 정상, 에디터는 멈췄다** (`f30f228d`).
SWGame 은 실행 중 소스 수정 → 컴파일 → 스왑 → 새 코드 실행 → 씬 상태 복원까지 전부 확인했다.
에디터 모듈은 컴파일과 스왑은 성공하는데 **3회 중 2회 영구 정지**했다.

원인은 draw 스냅샷 프로토콜의 빠진 전이다. `_inFlightDrawSlot` 은 `updateUi`(UI 스레드)가
publish 하면서 **세우고** `postPresent`(렌더 스레드)만 **푼다**. 리로드 경로는
`drainRenderWorkers()` 로 렌더 워커를 먼저 재우므로 풀 주체가 사라지는데,
`waitForDrawSnapshotIdle()` 은 타임아웃도 탈출 조건도 없는 spin 이다. 게임 모듈 리로드가 멀쩡했던
이유도 같다 — `suspendModules(Game)` 은 `destroyEditorInstance` 를 부르지 않아 이 spin 을 안 탄다.

전이를 하나 추가했다(획득 → present **또는 → 포기**): `IEditor::abandonPendingDraw()` +
EditorAPI C-ABI 한 항목, `drainRenderWorkers()` 가 재운 직후 호출. 재운 쪽이 알려 주는 것이 맞다 —
에디터는 소비자가 사라졌는지 알 방법이 없다. `shutdown()` 도 기다리지 않고 버린다.
검증: 수정 전 2/3 정지 → 수정 후 **4/4 정상 종료**, 네 번 모두 스왑이 실제로 일어난 상태에서.

> **EditorAPI 테이블이 바뀌었다.** App 과 EditorModule 을 **같이** 다시 빌드할 것.
>
> **행·크래시를 쫓을 때 `SW_LOG_*` 를 믿지 말 것.** 비동기 로거라 정지 직전 메시지가 통째로
> 사라진다. 이번에도 그것 때문에 "계측이 안 돌았다" 고 한 번 잘못 읽었다 —
> `fopen`+`fprintf`+`fflush`+`fclose` 동기 기록으로 바꾸니 한 번에 지점이 잡혔다.

### 2026-09-12 (싱글턴 걷어내기 — 넷은 옮겼고, 옮길 수 없는 것에는 이유가 있다)

저장소 전체에서 명시적 `instance()` / `get()` 싱글턴은 **아홉 개**였다. 그중 넷을 명시적 소유로
옮겼다. 기준은 하나다 — **소유자가 존재할 수 있는가.**

**옮긴 것**

1. `FrameProfiler` (`3afcaa21`) — `Source/Core/Profile/` → `Source/Engine/Utility/Debug/` 로 옮기고
   `EngineLoop` 이 `unique_ptr` 로 소유, `EngineServiceList.xxx` 에 등록. 옮길 수 있었던 이유는
   **`Source/Core` 안에서 쓰는 곳이 자기 자신 말고 0개**였기 때문이다 — 호출부가 전부 Engine 이면
   헤더가 Core 에 있을 이유가 없다.
2. `CompressionCodecRegistry::getDefault()` (`7445bb94`) — Logger 패턴(`setGlobalSink`)과 같은
   모양으로, 인스턴스는 `EngineLoop` 이 들고 Core 에는 **포인터 슬롯만** 둔다. 여기서 **실재하던
   갈라짐**을 하나 닫았다: `EngineLoop` 은 서비스에 싱글턴을 꽂았는데 `TestFramework/main.cpp` 는
   자기 `unique_ptr` 인스턴스를 꽂아서, 테스트에서는 `engine::getCompressionCodecRegistry()` 와
   `CompressionStream` 이 서로 다른 레지스트리를 보고 있었다.
3. 에디터 전역 상태 (`72fa7020`) — 플레이 세션(재생 상태·스텝·롤백 스냅샷)과 활성 테마를
   `EditorContext` 소유로. 정적 파사드(`EditorPlaySession` · `EditorThemeUtil`)는 그대로 두어
   호출부 65곳은 안 건드렸다.
4. ReflectionParser 의 `instance()` 넷 (`4163684a`) — `ParserSession` 을 `main` 이 소유하고
   아래로 내려준다. `TypeNameMap::normalize` 를 감싸던 `normalizeTypeName()` 자유 함수도 삭제했다
   — 호출부 열아홉 곳이 전역을 쓴다는 사실을 감추고 있었다.

**옮기지 않은 것과 이유**

- `CrashContextStore::get()` — 크래시 시그널/SEH 핸들러가 읽는다. 콜백에 컨텍스트를 못 넘기고,
  그 시점엔 소유자가 이미 파괴됐을 수 있다.
- `MemoryProfiler::s_activeProfiler`, `LiveReloadManager::s_delayLoadManager` — 전역
  `operator new/delete` 훅과 delay-load 훅. **둘 다 이미 바인딩 슬롯이지 싱글턴이 아니다**
  (인스턴스는 `EngineLoop` 이 `unique_ptr` 로 든다).
- 등록자 헤드 넷(`TypeRegistrar` · `EnumRegistrar` · `ComponentFactoryRegistrar` ·
  `GlobalVariableRegistrar`), `TestRegistry::getInstance()` — `main` **이전** 정적 초기화 시점에
  자기 등록한다. 소유자가 존재할 수 없는 시점이다.
- `TagID` · `hashed_string` 인터닝 테이블 — 인터닝은 "같은 문자열 → 같은 ID" 가 프로세스 전역에서
  성립해야 의미가 있다. 소유자를 두면 ID 수명이 소유자에 묶인다.
- `ResourceUtil` 의 정적 멤버 다섯 — 형태상 가장 싱글턴이지만 **호출부가 247곳**이다. 이득 대비
  변경량이 가장 나빠 미뤘다.
- `ParserContext::s_sharedConfig` — `once_flag` 로 한 번 채우는 설정. `getSharedConfig()` 호출부
  20곳이고 성격이 `instance()` 와 다르다.

**함정 둘**

- **테마를 컨텍스트로 옮기자 순서 버그가 생겼다.** `EditorThemeUtil::loadFromConfig()` 가
  `EditorContext::initialize()`(그 안에서 `setActive`) 보다 **먼저** 불리고 있었다. 상태를 컨텍스트로
  옮기면 그 시점에는 갈 곳이 없어 **조용히 버려진다**. `_themePreset` 을 MidnightBlue 로 바꿔 두고
  프로브를 박아 실측했다: 고친 뒤 applyPreset 전 0 → 후 2, 순서를 되돌리면 후에도 0.
  **상태를 소유자에게 옮길 때는 그 소유자가 언제 서는지부터 볼 것.**
- **핫리로드 근거는 틀렸다.** 처음에 "에디터 정적을 `EditorContext` 로 옮기면 핫리로드를 견딘다"
  고 적었는데, `ImGuiEditor` 가 `EditorContext` 를 소유하고 **둘 다 EditorModule 에 살아서**
  리로드되면 같이 죽는다. 이 변경의 이득은 수명이 명시적이 되는 것이지 리로드 내성이 아니다.

**코드젠 도구는 생성물로 검증한다.** ReflectionParser 변경은 컴파일 통과로 부족하다 —
`build/<preset>/generated` 를 지우고 새 파서로 재생성, 변경을 stash 하고 옛 파서로 다시 재생성해
`diff -r` 로 맞췄다. **149개 파일 차이 0.**

### 2026-09-12 (한 줄짜리 if 의 중괄호를 기계로 걷어낸다 — 규칙은 있었지만 아무도 강제하지 않았다)

`if` 본문이 한 줄이면 중괄호를 생략한다는 규칙은 `AGENTS.md` · `.cursorrules` · `GEMINI.md` 에
**처음부터 적혀 있었다.** 다만 검사하는 것이 없어서 저장소 전역 **124개 파일 · 290곳**이 규칙과
어긋난 채였다. 손으로 지키던 규칙이라 `Core` 같은 초기 코드일수록 많이 남아 있었다.

**clang-format 으로는 이 규칙을 쓸 수 없다.** `RemoveBracesLLVM: true` 가 정확히 이 일을 하지만
`if` 만 고를 수 없고 **`for` / `while` / `do` 의 중괄호까지 같이 벗겨낸다**. 실제로 켜 봤더니 제거
144곳 중 89곳이 반복문이었고, `while (...)` 바로 아래에 중괄호 없는 `if/else` 가 오는 형태가 33곳
생겼다. 이 저장소의 규칙은 **반복문은 한 줄이어도 중괄호를 유지**하는 쪽이므로 폐기했다.

**대신 `Scripts/lint/fixer/FormatBranchBraces.py` 를 만들었다** (`FormatForwardDeclarations.py` 와 같은
자리 — clang-format 앞단에서 돌고, clang-format 은 `InsertBraces` 를 안 켜 두었으므로 되돌리지
않는다). 규칙:

- `if` / `else if` / `else` 만 대상. 반복문은 건드리지 않는다.
- 사슬은 **모든 갈래가 한 줄일 때만** 벗긴다 (한 갈래라도 여러 줄이면 전부 유지).
- 블록 안에 주석 줄·전처리기 지시문·매크로 줄바꿈이 있으면 한 줄이 아니므로 건드리지 않는다.
- 본문이 그 자체로 `if`/`for`/`while` 이면 건드리지 않는다 (달랑거리는 `else` 방지).
- 문자열·문자·주석·원시 문자열은 마스킹한 사본에서 판정하므로 `"{ }"` 같은 내용에 속지 않는다.

`RunClangFormat.py` · `FormatModified.py` · `PreCommitLint.py` 세 곳에 연결했다. 앞으로는
pre-commit 이 잡는다.

**함정 — `clang-format --dry-run` 을 파일 여러 개에 한 번에 돌리면 결과가 조용히 잘린다.**
작업 초기에 "영향받는 파일 45개, `Engine`/`Editor`/`Test` 는 0개" 라는 숫자를 이 방식으로 뽑았는데
**틀린 숫자였다.** 같은 `GameObject.cpp` 를 단독으로 돌리면 위반 13건, 903개와 함께 돌리면 0건이
나온다. `--ferror-limit=0` 을 줘도 같다. 위반 총량이 커지면 앞쪽 파일들만 보고하고 멈춘다
(전역 219건에서 끊겼고, 보고된 파일은 `find` 순서상 맨 앞인 `Source/App` · `Source/Core` 뿐이었다).

- `PreCommitLint.py` 의 `runClangFormatBatch(checkOnly=True)` 도 같은 방식이다. 스테이지된 파일에
  위반이 대량으로 쌓이면 게이트가 조용히 통과시킬 수 있다.
- **숫자를 도구 하나로만 세지 말 것.** 이번에도 독립적으로 센 스캔이 어긋나서 잡았다.
- **그런데 같은 날 나는 또 틀렸다.** 여기 "트리에 22 개가 남아 있다" 고 적었는데, 그건 PATH 의
  clang-format **22.1.8** 로 센 수였다. 저장소가 고정한 `Tools/LLVM/bin/clang-format.exe`(20.1.8)
  로는 **0 개**다. 도구를 바꿔 세는 것은 세는 법을 바꾸는 것과 같다 — 1-2b 절에 정정해 두었다.

**부수 발견**: `Source/Core/String/StringBuilder.h` 는 한글 주석 안에 실제 NUL 바이트가 한 개 들어
있어 git 이 이 파일을 **바이너리로 취급**한다(diff 가 `Bin 12931 -> 12893 bytes` 로만 나온다).
이번 변경 이전부터 그랬다. 고치려면 그 주석의 널 문자 표기를 손봐야 한다.

검증: 스크립트 자체 케이스 20종(중첩·다중 줄 조건·`if constexpr`·문자열 속 중괄호·매크로 줄바꿈·
빈 줄 뒤 `else`)과 멱등성, 변경 124파일이 **삭제 580줄 / 추가 0줄**, 재검사 0건, clang-format 이
추가로 고칠 것 없음, `CheckCodeConventions` 0건, Ninja-Debug 빌드 + `ctest -L nogpu` 통과.

### 2026-09-11 (스크립트가 한글을 찍으면 Windows CI 가 선다 — 빌드 단계에서 돌면 빌드째로)

`36f30a54` 푸시에서 **Windows Shipping 만** 졌다(리눅스 셋 + Windows Debug 는 초록). 로그는 받을 수
없었지만 주석 API 의 한 줄이 답이었다:

```
sccache stats: 0% - 0 hits, 0 misses, 0 errors
```

**컴파일이 한 건도 시작되지 않았다.** 빌드 맨 앞의 쿠킹 단계에서 죽은 것이다.

**원인.** 그 커밋에서 쿠커에 `print(f"[PackCooker] 압축 코덱: …")` 를 넣었다. Windows 는 stdout 인코딩이
시스템 코드페이지로 잡히는데, GitHub 러너는 한글을 담지 못하는 코드페이지라 `UnicodeEncodeError` 로
죽는다. **한국어 Windows(cp949)나 리눅스(UTF-8)에서는 재현되지 않는다** — 내 PC 도 CI 의 다른 잡도
전부 통과한 이유다.

`PYTHONIOENCODING=cp1252` 로 재현했다:

```
$ py -3 -c "print('압축 코덱')"
UnicodeEncodeError: 'charmap' codec can't encode characters in position 0-1
```

**이미 있던 한글 print 들은 왜 안 터졌나.** 전부 오류·경고 경로라 평소 실행에서 찍히지 않는다.
그런데 그게 더 고약하다 — **오류 메시지가 먼저 죽는다.** `"셰이더를 다시 구우십시오"` 같은 해결
안내(`CookAssets.py` 883~889행)가 정작 필요한 순간에 트레이스백으로 바뀐다.

**고친 방식.** `Scripts/common/__init__.py` 에서 `sys.stdout`·`sys.stderr` 를 UTF-8 로 재설정한다
(`errors="replace"` — 터미널이 글자를 못 그려도 예외는 안 난다). 모든 스크립트가 이 패키지를
import 하므로 한 번에 막힌다. 내 한 줄만 ASCII 로 바꾸면 잠복한 나머지가 그대로 남는다.

**남길 것**: 이 저장소의 스크립트 메시지는 한국어다. **빌드 단계에서 도는 스크립트는 특히**, 콘솔
인코딩을 가정하지 말 것.

검증: `PYTHONIOENCODING=cp1252` 로 쿠커 실행 종료 코드 0, 유니티 Shipping 빌드 통과, 린트 6/6.

### 2026-09-11 (팩 코덱을 Config 로 고른다 — 그리고 실제 팩에서는 코덱 차이가 거의 없었다)

쿠커가 코덱을 고를 수 있게 했다. `Config/Engine/PackConfig.json`:

```json
"compression": { "codec": "Zlib", "level": 0 }
```

- 이름은 팩 포맷 계약(`PackFormat.json` 의 `compression.codecs`)에 있는 것만 받는다. 없는 이름이면
  가능한 값을 나열하며 멈춘다.
- 계약에 **`Zstd = 4` 를 덧붙였다**(append). 생성기가 `packformat::kCompressionZstd` 를 만들고
  `ResourcePackTypes.h` 의 static_assert 가 C++ enum 과 묶는다. 리더에도 분기를 넣었다.
- **`PackFormat.json` 을 고치면 configure 를 다시 돌려야 한다.** 생성 헤더가 갱신되지 않아
  `no member named 'kCompressionZstd'` 로 한 번 섰다.

**파이썬 의존성 — 이 저장소의 새 범주다**

`Scripts/` 는 지금까지 **표준 라이브러리만** 썼다(requirements.txt 도 pip 호출도 없었다). zlib 은
표준이라 기본값으로 남기고, LZ4·Zstd 는 **선택적 pip 의존성**으로 두었다:

- `LZ4` → `py -3 -m pip install lz4`
- `Zstd` → `py -3 -m pip install zstandard`

**모듈이 없으면 그 자리에서 멈춘다 — 조용히 zlib 으로 물러나지 않는다.** 설정이 LZ4 인데 zlib 으로
구우면 설정과 산출물이 어긋나고, 그 사실은 한참 뒤 배포본에서야 드러난다.

**실측 — 합성 벤치와 실제 팩이 달랐다** (세 코덱으로 실제 쿠킹, 2026-09-11)

| 팩 | Zlib | LZ4 | Zstd |
|---|---|---|---|
| `engine.pack` | 254,558,515 | 327,207,121 (**+28%**) | 254,202,331 (−0.1%) |
| `common.pack` | 87,267 | 91,826 | 87,315 |
| `game_empty.pack` | 20,812 | 20,944 | 20,851 |

**앞 항목의 합성 벤치(1MB 모사 버퍼)에서는 Zstd 가 Zlib 보다 훨씬 작았는데, 실제 팩에서는 0.1% 다.**
팩 내용이 이미 압축된 것(DDS 텍스처·셰이더 바이너리)이라 더 줄 여지가 없기 때문이다. **LZ4 만 28%
커진다** — 이미 압축된 바이트에 LZ4 의 빠른 경로는 손해만 본다.

읽을 것: **벤치마크 표본이 곧 결론이 아니다.** 지금 이 프로젝트에서 팩 코덱을 바꿀 이유는 없다
(기본값 `Zlib` 을 유지했다). 압축되지 않은 자산(대량의 JSON·XML·오디오 PCM)이 팩에 들어오면 그때
다시 재라 — 그러라고 손잡이를 만들어 둔 것이다.

검증: Debug·Shipping 빌드 경고 0, nogpu 5/5 양쪽, 린트 6/6,
`Engine_ResourcePack.EveryPackCodecRoundTrips` 가 None·RLE·Zlib·LZ4·**Zstd** 다섯을 굽고 읽는다.
세 코덱으로 실제 쿠킹도 돌려 봤다(위 표).

### 2026-09-11 (코덱 **구현**은 공유하고 **키**는 각자 고른다 — 팩이 LZ4 를 읽는다)

앞 항목에서 "팩과 스트림의 enum 을 엮으면 안 된다" 는 결론까지 갔는데, 그것 때문에 팩이 자기 포맷에
선언해 둔 `LZ4` 를 못 읽는 상태가 남아 있었다. 갈림길을 잘못 본 것이었다:

- **엮으면 안 되는 것**: 두 on-disk **enum**(`PackCompressionType` ↔ `CompressionCodecType`). 값이 다르고
  각자 다른 파일에 박히므로 `static_cast` 로 건너다니면 한쪽 포맷 변경이 다른 쪽을 끌고 간다.
- **엮어도 되는 것 — 오히려 엮어야 하는 것**: 코덱 **구현**(`ICompressionCodec` 클래스들). 알고리즘은
  포맷과 무관하다.

그래서 구현만 공유하고, **어떤 코덱을 쓸지는 각 포맷이 자기 enum 으로 스스로 고른다.**

**한 것**

- `ZlibCompressionCodec` 을 만들었다 — 팩 리더 안에 박혀 있던 `uncompress` 호출을 꺼낸 것이다.
- `ResourcePackReader::decompressData` 가 `PackCompressionType` 으로 코덱을 고른다(None·RLE·Zlib·**LZ4**).
  해제 뒤 **엔트리가 말한 크기와 실제 푼 크기가 같은지** 확인하는 검사도 넣었다 — 예전엔 zlib 만
  그 검사가 있었다.
- `CompressionCodecType` 에 `Zlib = 4` 를 **덧붙였다**(값은 append 만 — 스트림 헤더에 기록된다).
  팩의 `Zlib = 2` 와 **일부러 다르다**. 그 이유를 enum 옆에 적어 뒀다.
- 테스트 헬퍼 `createTestPackFile` 도 같은 코덱 클래스로 압축한다 — 이제 LZ4 팩을 만들 수 있다.

**증명**: `Engine_ResourcePack.EveryPackCodecRoundTrips` — None·RLE·Zlib·LZ4 네 코덱으로 팩을 굽고
읽어 내용이 같은지 본다(4 KB 반복 문자열 포함).

**남은 것 — 팩을 굽는 쪽은 파이썬이다**

`Scripts/generate/CookAssets.py` 가 팩을 쓰고, 파이썬 표준 라이브러리에는 `zlib` 만 있다(3.13 기준.
zstd 는 3.14+, lz4 는 애초에 없다). 그래서 **쿠커가 LZ4 팩을 굽게 하려면 pip 의존성**(`lz4`)을 도구
환경에 추가해야 한다 — 별개의 결정이라 이번에는 하지 않았다. 지금 상태는:

- 엔진은 LZ4 팩을 **읽을 수 있다**(C++ 리더 + 테스트가 굽고 읽는다).
- 배포 쿠킹은 여전히 zlib 로 굽는다.

검증: Debug·Shipping 빌드 경고 0, nogpu 5/5 양쪽, 린트 6/6, `Engine_ResourcePack` 10/10,
에디터 DX12 실기동 종료 0 · `[Error]` 0 건.

### 2026-09-11 (LZ4 · Zstd 를 실제로 붙이고 재 봤다 — 확장 슬롯이 이제 비어 있지 않다)

`CompressionCodecType::LZ4` · `Zstd` 는 "확장 슬롯" 이라고만 적혀 있고 구현이 없었다. vcpkg 로
`lz4 1.10.0` · `zstd 1.5.7` 을 받아 코덱을 넣고, 같은 데이터로 재 봤다.

**실측** (`Engine_Compression.CodecComparisonMeasurement`, Debug 빌드, 1 MB 표본 — 반복되는 JSON 같은
헤더 + 유사 난수 바이트가 섞인, 엔진 데이터를 닮은 버퍼)

| 코덱 | 크기 | 비율 | 압축 | 해제 |
|---|---|---|---|---|
| RLE | 1031 KB | **100.8%** | 3.0 ms | 0.09 ms |
| LZ4 | 412 KB | 40.3% | 6.5 ms | 0.51 ms |
| LZ4-HC(9) | 403 KB | 39.4% | 24.8 ms | 0.53 ms |
| Zstd(기본 3) | 355 KB | **34.7%** | 23.9 ms | 1.50 ms |
| Zstd(15) | 353 KB | 34.5% | 91.5 ms | 1.44 ms |

읽히는 대로 적으면:

- **RLE 는 이런 데이터에서 쓸모가 없다 — 오히려 커진다(100.8%).** 반복 런만 잡으므로 잡음 섞인
  바이트에서는 런 헤더만 붙는다. 지금 이게 `CompressionStream` 의 기본 코덱이다.
- **해제 속도는 LZ4 가 Zstd 의 약 3 배다**(0.51 vs 1.50 ms). 로딩 시간이 곧 해제 시간인 자리(런타임
  스트리밍)는 LZ4 가 맞다.
- **크기는 Zstd 가 확실히 낫다**(34.7% vs 40.3%). 배포물 크기가 중요한 자리는 Zstd 다.
- **고압축 레벨은 값을 거의 못 한다.** LZ4-HC 는 압축이 4 배 느린데 0.9%p, Zstd(15)는 4 배 느린데
  0.2%p 줄 뿐이다. 이 표본에서는 **기본 레벨이 옳은 선택**이다.
- LZ4-HC 는 압축만 느리고 **해제는 기본과 같다**(0.53 vs 0.51). 한 번 굽고 여러 번 읽는 데이터라면
  고려할 만하지만, 위 숫자를 보면 그 0.9%p 가 굽는 시간 4 배를 정당화하기 어렵다.

**어디에 두었나 — Core 가 아니라 Engine 이다**

`Source/Engine/CMakeLists.txt` 에 **"Core 는 압축 라이브러리에 종속되지 않게 두고, 실제로 쓰는 Engine 에만
붙인다"** 는 의도가 이미 적혀 있었다(zlib 이 같은 이유로 거기 있다 — ReflectionParser 가 Core 를 링크한다).
처음엔 Core 에 넣었다가 그 주석을 보고 옮겼다. 그래서:

- 코덱은 `Source/Engine/Compression/` 에 있고 `Engine` 이 lz4/zstd 를 링크한다.
- 등록은 `EngineLoop::initialize` 가 기본 레지스트리에 한다(내장이 아니라 **엔진이 올리는** 코덱이다).
- `CheckEngineLayers` 의 티어 표에 `Compression: 0` 을 더했다 — Engine 의 어느 것도 참조하지 않는다.

**함정 넷** (전부 실제로 밟았다)

- **OBJECT 라이브러리는 링크를 전파하지 않는다.** `Core_objects` 에 `target_link_libraries` 를 걸어도
  `$<TARGET_OBJECTS:Core_objects>` 로 가져가는 `Engine` 은 물려받지 않는다 — `undefined symbol: LZ4_*` 로
  선다. 링크는 `Core`(STATIC)·`Engine` 양쪽에 따로 적어야 한다(기존 `comdlg32` 들이 그렇게 돼 있다).
- **zstd 의 임포트 타겟 이름은 구성마다 다르다** (`zstd::libzstd` / `_shared` / `_static`). 하나만 적으면
  다른 구성에서 configure 가 조용히 지나가고 링크에서 터진다 — zlib 이 준 교훈 그대로라 셋 다 본다.
- **`%#` 에 폭을 붙이면 안 된다.** `"%-12#"` 로 적었더니 `formatstring` 이 "모르는 %…" 로 보아 인자를
  소비하지 않았고 Debug 인자 수 단언에 걸려 테스트가 죽었다. 폭·정밀도가 필요하면 printf 형(`%-12s`,
  `%.2f`)을 쓴다.
- **배포본에서 `SW_LOG_INFO` 가 사라지면 그 값만 쓰이는 지역 변수가 미사용 경고가 된다.** `[[maybe_unused]]`
  로 막았다(FrameProfiler 가 같은 처방을 쓴다).

**테스트 호스트는 EngineLoop 을 돌리지 않는다.** 그래서 "등록된 코덱을 스트림이 집어 쓴다" 테스트는
스킵으로 빠졌는데, **스킵은 아무것도 증명하지 못하므로** 테스트가 직접 등록하고 끝나면 되돌리게 바꿨다.

**손대지 않은 것**: 팩 포맷의 `PackCompressionType::LZ4` 는 여전히 리더가 거부한다. 팩은 자기 포맷을
직접 해제하므로(위 항목 참고) LZ4 를 팩에 넣으려면 **팩 쪽에 따로** 구현해야 한다 — 레지스트리를 끌어다
쓰면 두 on-disk 포맷이 엮인다.

검증: Debug·Shipping 빌드 경고 0, nogpu 5/5 양쪽, 린트 6/6, `Engine_Compression` 4/4(스킵 0),
에디터 DX12 실기동 종료 0 · `[Error]` 0 건.

### 2026-09-11 (코덱 레지스트리가 구조적으로 고립돼 있었다 — 문서가 약속한 확장점이 죽어 있었다)

"`EngineLoop::getCompressionCodecRegistry()` 호출부가 0" 은 증상이었다. 파고드니 계통 전체가 떠 있었다.

**무엇이 끊겨 있었나**

`CompressionCodecRegistry` 는 정식 엔진 서비스였다(`EngineServiceList.xxx`, `required=1`). `EngineLoop` 이
만들고·초기화하고·공개하고·종료했다. 그런데:

- `engine::getCompressionCodecRegistry()` 호출부 **0**
- `registerCodec` / `unregisterCodec` 호출부 **0**
- `CompressionStream::resolveCodec( type, pRegistry )` 는 레지스트리를 **받도록 설계돼 있는데** 넘기는
  호출부가 없어 **항상** 하드코딩 코덱(`s_nullCodec`/`s_rleCodec`)으로 갔다

**원인은 레이어였다.** 레지스트리 인스턴스는 `Engine` 이 들고, 그것을 봐야 하는 `CompressionStream` 은
`Core` 에 있다. Core → Engine 은 역행이라 닿을 수 없다. 그래서 `pRegistry` 매개변수는 영원히 널이었다.
README §4.3 의 "LZ4/Zstd 등록" 예제는 **아무 일도 하지 않는 코드**였다.

**고친 방식 — 소유를 Core 로 내렸다**

`CompressionCodecRegistry::getDefault()`(프로세스 하나, 함수 지역 static). `resolveCodec` 은 레지스트리를
못 받으면 그것을 본다. `EngineLoop` 은 인스턴스를 만들지 않고 `initialize()` 만 부른 뒤 서비스 포인터를
거기로 맞춘다 — 기존 호출부(`engine::getCompressionCodecRegistry()`)는 그대로 동작한다.

- **엔진 종료 때 `shutdown()` 하지 않는다.** 프로세스가 소유하므로 비우면 엔진을 내린 뒤 도구·테스트가
  쓰는 압축 경로에서 내장 코덱이 사라진다.
- `EngineLoop` 의 멤버와 (쓰이지 않던) 접근자는 걷어냈다.

**팩은 손대지 않았다 — 지금이 맞다**

처음엔 `ResourcePackReader` 를 레지스트리에 물리려 했는데 **틀린 방향이었다.** 두 enum 은 서로 다른
파일의 **독립된 on-disk 포맷**이다:

| 값 | `PackCompressionType` (팩 엔트리) | `CompressionCodecType` (스트림 컨테이너 헤더) |
|---|---|---|
| 2 | **Zlib** | **LZ4** |
| 3 | **LZ4** | **Zstd** |

0·1·255 가 같아서 평행해 보이지만 2·3 에서 갈리고, `CompressionCodecType` 에는 **Zlib 이 없다.**
`static_cast` 로 이으면 팩의 Zlib 이 LZ4 로 읽힌다. 엮으면 한쪽 포맷 변경이 다른 쪽을 끌고 간다.
**팩이 자기 포맷을 직접 해제하는 지금이 맞다.**

**증명**

`Core_Compression.RegisteredCodecIsUsedByStream` — 바이트를 0xA5 와 XOR 하는 시험 코덱을 기본
레지스트리에 등록하고, **레지스트리를 넘기지 않은 채** `CompressionStream` 으로 압축해 페이로드가
XOR 되었는지 본다. 구코드에서는 `Zstd` 가 하드코딩 분기의 `s_nullCodec` 으로 가므로 반드시 진다.

**남은 것 (알고 두는 것)**

- 팩 포맷이 선언한 `LZ4`·`Custom` 은 리더가 `"Unsupported compression type %# in pack"` 으로 **거부**한다.
  포맷이 약속만 하고 구현이 없다 — 팩에 LZ4 를 넣으려면 팩 쪽에 따로 구현해야 한다.
- 모듈이 등록한 코덱은 **그 모듈이 거둬야 한다.** 레지스트리는 `Engine.dll` 에 살아 모듈보다 오래 간다
  — Undo 스택·전역 변수와 같은 함정이라 `registerCodec` 주석과 README 에 적었다.

검증: Debug·Shipping 빌드 경고 0, nogpu 5/5 양쪽, 린트 6/6, `Core_Compression` 5/5,
에디터 DX12 실기동 종료 0 · `[Error]` 0 건.

### 2026-09-11 (단순 래퍼 정리 — 473 후보 중 실제로 걷어낼 것은 넷뿐이었다)

본문이 "다른 함수 호출 한 줄"뿐인 함수를 전부 뽑았다(473 개). **대부분은 정당했다** — 남긴 이유를
적어 둔다. 다음에 같은 조사를 하는 사람이 같은 길을 다시 걷지 않도록.

**유지 (래퍼처럼 보이지만 아니다)**

- **주소로 등록되는 것.** `commandSave` · `commandUndo` 등 에디터 커맨드 20여 개는 커맨드 표에
  `&commandSave` 로 **함수 포인터가 실린다.** 인라인하면 표가 성립하지 않는다.
- **X-매크로 디스패치 표.** `applyReflectAlias` 등 8 개는 `PredefinedAnnotationField.xxx` 의
  `REGISTER_ANNOTATION_FIELD( Reflect, StringFn, Alias, applyReflectAlias )` 가 **이름으로** 집는다.
- **리플렉션 노출 함수.** `UnitStatsComponent::heal20` 은 `FUNCTION( … CallInEditor )` 다 — 에디터가
  인자 없이 부를 버튼을 만들려고 **일부러** 인자를 박아 둔 것이다.
- **컨테이너/인터페이스 API 별칭.** `push`→`enqueue`, `reserve`→`rehash`, `reset`→`set`,
  `shutdown`→`clearCache` 류. 표준 라이브러리 관례이거나 인터페이스 구현이다.
- **접근자.** `getResource()`→`_resourceImpl.get()` 같은 것. 래퍼가 아니라 캡슐화다.

**걷어낸 것 넷**

- `InputManager::injectRawEvent` — `postRawEvent` 의 순수 별칭인데 **호출부가 0 개**였다. 게다가
  `postRawEvent` 의 `bool` 반환을 버려서, 썼더라도 실패를 삼켰을 것이다.
- `quaternion::toEuler` — `getEulerAngles()` 별칭. 쓰는 곳은 테스트 하나뿐이었다. 같은 것에 이름이
  둘이면 읽는 사람이 "다른가?" 를 확인해야 한다. 테스트를 실제 이름으로 바꾸고 지웠다.
- `VarIntUtil::encodeVarUInt32` / `encodeVarInt32` — `static_cast` 한 줄을 감싼 것이고 **호출부 0**.
  32비트 값을 64비트 인코더에 넘기면 승격되어 같은 바이트가 나온다(`BinaryStream` 이 이미 그렇게 쓴다).

> **인코딩만 32비트 짝이 없는 것은 의도된 비대칭이다.** 헤더에 그 이유를 적어 뒀다 — 안 적으면
> 누군가 "대칭을 맞춘다"며 되살린다. **디코딩에는 32비트가 있다**: 그쪽은 캐스팅이 아니라
> uint32/int32 범위를 벗어난 값을 거르는 실제 검사를 한다.

**에디터 커맨드 표 — 여덟을 걷어내고 여섯을 둘로 접었다**

커맨드 표는 `Delegate<void()>` / `Delegate<bool()>` 을 든다. 그러면 판단 기준이 분명해진다 —
**대상이 이미 그 모양이면 표가 직접 가리키면 되고, 감싸는 것은 이름만 하나 늘리는 것이다.**

- 걷어낸 여덟: `commandSave` · `commandSaveScene` · `commandQuickOpen` · `commandCommandPalette` ·
  `commandExit` · `commandThemeSettings` · `commandSnapToGround` · `isPlayStopped`. 대상이 전부
  `static void f()` / `static bool f()` 라 표가 `&EditorAssetCommands::saveFocusedOrScene` 처럼
  **직접** 가리킨다.
- 축만 다른 여섯(`commandAlignX/Y/Z` · `commandDistributeX/Y/Z`)은 표가 `void()` 를 요구하므로
  **함수 자체는 있어야 한다**(대상이 인자를 받는다). 다만 축마다 하나씩 적을 이유는 없어서
  `commandAlign<TAxis>` · `commandDistribute<TAxis>` 템플릿 둘로 접었다 — 표는 그대로
  `&commandAlign<AlignAxis::X>` 를 가리킨다.
- 남은 것의 이유를 **파일 머리에 적어 뒀다**(반환형 맞추기 · 인자 박기 · 대상 찾아오기 셋 중 하나).
  안 적으면 다음 사람이 "마저 정리" 하다 `commandNewScene`(대상이 `bool`)이나
  `commandUndo`(`getService<CommandStack>()`)를 깨뜨린다.

**곁들여 — 호출부가 0 인 접근자 여섯** (이번엔 손대지 않았다, 판단 필요)

`ModuleHost::getModuleCompiler` · `EngineLoop::getCompressionCodecRegistry` ·
`DebugDrawQueue::getLineCount` · `getSphereCount` · `BattleState::getFoeName` · `getStatusText`.
전부 정의만 있고 부르는 곳이 없다. 템플릿 저장소라 "확장하는 쪽이 쓸 API" 로 남겨둔 것일 수 있어
기계적으로 지우지 않았다.

검증: Debug·Shipping 빌드 경고 0, nogpu 5/5 양쪽, 린트 6/6.

### 2026-09-11 (로거를 파사드와 장치로 가른다 — 콘솔·파일이 뮤텍스 하나를 함께 잠그고 있었다)

`Logger` 하나가 다섯 가지 일을 하고 있었다: 포맷·타임스탬프 · 리스너 멀티캐스트 · 비동기 큐/워커 ·
**콘솔 쓰기** · **파일 롤오버**. 그중 뒤 둘이 진짜 문제였다 — `_mutex` **하나**가 콘솔과 파일 쓰기를
함께 잠갔다(`workerLoop` · `flushQueue` · 동기 폴백 두 곳 전부). 파일 I/O 가 느리면 콘솔도 멈춘다.

**`ILogSink` 로 가르지 않았다.** 그건 출력 장치 인터페이스가 아니라 **전역 파사드**다 — `writeLog`
말고도 `addLogWrittenListener` · `getLogFolderPath` 를 들고 있다. 콘솔 전용 싱크를 상상하면 바로
어색하다(로그 폴더 경로를 뭐라고 답하나). 게다가 이 인터페이스는 이미 **데코레이터 이음매**로 쓰인다 —
`TestFramework` 가 구현해 기존 싱크를 감싸고 로그를 가로챈다. 그 역할은 그대로 뒀다.

대신 **장치용 인터페이스를 하나 더** 뒀다:

```
ILogSink  ← 전역 파사드 (그대로)
└─ Logger  … 포맷 · 타임스탬프 · 리스너 · 비동기 큐/워커 · 상세도 · Caller 표
   └─ vector<unique_ptr<ILogOutput>>
      ├─ ConsoleLogOutput   (Windows 콘솔 색 / POSIX ANSI)
      └─ FileLogOutput      (Saved/Logs, 시간별 + 세션별 롤오버)
```

**`LogRecord` 가 이미 `_level` · `_formatted` · `_year/_month/_day/_hour` 를 들고 있어서** 두 장치가
필요로 하는 것이 정확히 그것뿐이었다 — 자료구조가 이 분리를 이미 예고하고 있었던 셈이라 깔끔하게 떨어졌다.

- 장치는 **자기 락을 스스로 갖는다.** `Logger::dispatchToOutputs` 는 목록만 잠깐 잠그고 **쓰기는 락
  밖에서** 한다. 콘솔·파일이 서로를 막던 것이 사라졌다.
- `getLogFolderPath()` 는 `ILogSink` 에 남겼다 — `Logger` 가 `FileLogOutput` 에 물어 답한다. 호출부
  변경 0(테스트 2 곳 + 데코레이터 1 곳뿐이었다). 파사드가 "로그 어디 있나" 에 답하는 건 정당한 위임이다.
- 출력을 더 붙이려면 `Logger::addOutput` — 에디터 패널·ETW·네트워크를 넣을 때 `Logger` 를 고칠 일이 없다.
- 값 타입은 `LogTypes.h` 로 뺐다. 두 층이 함께 쓰므로 한쪽에 두면 장치가 파사드를 include 하게 된다.
  `Logger.h` 가 그 헤더를 include 하므로 **기존 include 는 그대로**다.

**함정 둘**

- `Source/Core/CMakeLists.txt` 는 GLOB 이 아니라 **경로 목록**이다. 새 `.cpp` 둘을 거기 적어야 한다
  (Core README 가 경고하고 있던 그대로다 — 잊으면 컴파일러가 알려주지 않는다).
- 린터가 지역 배열 `arrOutput` 을 **출력 매개변수로 오해**했다(`Naming/OutParameter`). 이름에 `out`
  이 들어가면 지역 변수라도 걸린다. `arrDevice` 로 바꿨다.

검증: Debug·Shipping 빌드 경고 0, nogpu 5/5 양쪽, 린트 6/6, `Core_Log` 13/13,
에디터 DX12 실기동 종료 0 · `[Error]` 0 건 · 로그 파일 정상 기록(세션별 롤오버 확인).

### 2026-09-11 (문서 점검 — 없는 API 를 설명하던 README 절 둘을 실제 API 로 다시 썼다)

프로젝트 `.md` 41 개(+ `.cursorrules`)를 기계적으로 대조했다. **링크·경로·예제 include 를 실제 트리와
맞춰 보는 방식**이라 "읽어 보니 이상하다" 가 아니라 검증된 것만 고쳤다.

**가장 큰 것 — 루트 `README.md` 의 5.11 · 5.12 절이 존재하지 않는 클래스를 설명하고 있었다**

- `sw::IndirectDrawBuffer` — **저장소 어디에도 없다.** 간접 인자는 전용 클래스가 아니라 일반 RHI
  버퍼에 담고 `IRHICommandList::drawIndexedIndirect` 가 읽는다. 인자 구조체는
  `RHIDrawIndexedIndirectCommand`(`RHITypes.h`)다.
- `sw::BindlessTable` — **없다.** 인덱스 발급은 `IRHIResource` 가 직접 한다
  (`registerBindlessTexture` / `registerBindlessResource` / `registerBindlessUav`, 해제는 짝을 맞춰야 한다).
- `sw::ComputePass` 는 있지만 **API 가 달랐다.** 문서는 `initialize(device, pso)` + `dispatch(ctx,64,1,1)`
  였는데 실제는 `setComputePipelineState` · `bindUav` · `bindSrv` · `dispatch(pCmdList, ComputeDispatchParams)` 다.
  → **2026-09-12 에 클래스 자체를 지웠다.** 문서를 헤더에 맞춰 고쳤지만 그 헤더를 만드는 곳이 저장소에 하나도
  없었다 — 컴퓨트는 `FrameRenderer` 가 커맨드 리스트에 직접 건다(아래 같은 날 항목).

둘 다 실제 헤더를 보고 다시 썼다. 필드 이름(`_indexCountPerInstance` 등)까지 대조했다.

**나머지**

- **깨진 마크다운 링크 5 개** — 전부 상대 경로 깊이 오류였다(`../../../../README.md` 등). 이제 0 개다.
- **예제 `#include` 14 개가 실제 경로와 달랐다.** `Renderer/FrameRenderer.h` → `Renderer/Frame/FrameRenderer.h`,
  `Renderer/RenderGraph.h` → `Renderer/Graph/RenderGraph.h`, `Audio/AudioSystem.h` → `Audio/IAudioSystem.h`,
  `Physics/PhysicsSystem.h` → `Physics/PhysicsWorld.h`, `Object/Component.h` → `Object/Component/Component.h` 등.
- **`Resource/README.md` 가 `editordata.xml` 을 `editor/data/` 에 있다고 했다.** 실제로는
  `Config/Editor/editordata.xml` 이고 `Resource/editor/` 에는 스플래시 텍스처만 있다(헤더 주석도
  "배포 Resource data 아님" 이라고 적고 있다).
- **`Test/README.md` 가 테스트 프로젝트를 4 개라고 했다** — `EditorTest` 가 빠져 있었다. 라벨 목록에도
  `editor` 가 없었고, lint 를 둘이라고 했는데 실제로는 여섯이다. `ctest -C Debug` 도 고쳤다 — Ninja 는
  단일 구성 생성기라 `-C` 는 아무 일도 하지 않는다.
- **`ARCHITECTURE.md` · `CLAUDE.md`** — CTest 타깃 목록에 `EditorTest` 추가, `EngineTest_NoGPU` 필터 설명을
  `RenderPassGpuTest` 포함해 갱신, 라벨 8 개 전부 표기.
- **`docs/01_GettingStarted.md`** 가 Windows 전용 시절에 멈춰 있었다 — WSL/리눅스 경로(`SetupLinuxDevEnvironment.py`,
  DrvFs 함정)와 git 훅 설치를 넣고 테스트 명령을 실제 형태로 고쳤다.
- **규칙 문서 넷**(`AGENTS.md` · `GEMINI.md` · `.cursorrules` · `docs/04_CodingGuidelines.md`)에 이번에
  저장소 전체에 강제한 **익명 네임스페이스 규칙**을 적었다(파일당 하나 · 최상단 · free `static` 금지 ·
  예외 넷 · "올리기 전에 빌드로 확인").

**점검 방법** (다음에도 쓸 수 있다): 마크다운 링크와 백틱 경로를 뽑아 `os.path.exists` 로 대조하고,
예제 `#include` 를 소스 루트들과 대조한다. 산문 축약 경로(`Common/Gui/...`)는 오탐이므로 슬래시가 든
것만 보되 링크는 전부 본다.

검증: 린트 6/6. 깨진 링크 0 · stale include 0.

### 2026-09-11 (익명 네임스페이스 마무리 — 최상단 정렬, 중첩 0, TU 지역 free 함수 26 개를 안으로)

**규칙.** `.cpp` 의 TU 지역 헬퍼는 **익명 네임스페이스 하나**에, **스코프 최상단**에 둔다. `static` 자유
함수는 쓰지 않는다(AGENTS.md "Helpers: Util vs Internal" — 흔한 이름이 외부에 안 보일 뿐 TU 마다 하나씩
생겨 유니티 빌드에서 부딪힌다).

**한 것 셋**

1. **최상단 정렬 9 개.** 블록이 하나지만 스코프 중간에 있던 파일들. 넷은 그대로 올라갔고
   (`EngineLoop`·`GlobalVariablesPanel`·`GameService`·`FrameRendererPso` 등), `WindowsCallStackCapture` ·
   `MacCallStackCapture` 는 블록보다 앞에 있던 파일 스코프 `static` 둘을 **블록 안으로** 넣어 올렸다
   (`LinuxCallStackCapture` 와 같은 처방 — 같은 내부 링키지이고 이쪽이 현대 관용구다).
2. **중첩 익명 네임스페이스: 0 건.** 훑어서 확인했다.
3. **free 함수 26 개를 익명 네임스페이스 안으로.** 헤더에 선언된 공개 API 와 `main` 은 당연히 제외하고,
   TU 지역인 것만 골랐다(GL/Vulkan 변환 헬퍼, ImGui 백엔드 훅, 테스트 헬퍼, 코드젠 템플릿 헬퍼).
   `static` 키워드는 뗐다 — 익명 네임스페이스가 이미 내부 링키지를 준다.

**끝난 상태**

- 익명 네임스페이스가 둘 이상인 파일: **2 개** (둘 다 정당하다)
  - `ImGuiOpenGLRendererBackend.cpp` — `#if WINDOWS` / `#elif LINUX` 로 **상호 배타적**이라 실제 번역
    단위에는 하나뿐이다. 합치면 두 플랫폼 코드가 섞인다.
  - `TestGameObject.cpp` — 두 블록 사이에 `REFLECT_BODY()` 로 코드젠되는 Mock 컴포넌트 11 개가 있다.
    **REFLECT 타입은 익명 네임스페이스로 못 옮긴다**(생성된 `.gen.cpp` 가 `sw::MockX` 를 이름으로 참조한다).
- 익명 네임스페이스 밖 free 함수: **6 개** (전부 정당하다) — `main` 셋과, Tools 헤더에 선언된
  `loadReflectBuiltins` · `emitReflectBuiltinsGen` · `normalizeTypeName`.
- `Scene.cpp` 은 블록이 최상단이 **아닌 채로 둔다.** 바로 위 `SW_GLOBAL_VARIABLE_STRING( gv_defaultMaterial, … )`
  을 블록 안의 `SceneInternal` 이 쓰는데, 그 매크로는 `extern` 을 붙여 **외부 링키지를 의도**하므로
  익명 네임스페이스에 넣을 수 없다.

**작업하며 배운 것 — 올리기 전에 반드시 확인할 것**

블록을 위로 올리면 **그 사이에 선언된 것에 대한 의존이 깨진다.** 이번에 컴파일러가 네 번 잡았다:
`TaskManager`(`TaskNode` 불완전 타입) · `Scene`(`gv_defaultMaterial`) · `WindowsCallStackCapture`·
`MacCallStackCapture`(`s_symbolMutex`) · `OpenGLRHIResource`(`kGl*` 상수) · `TestEvent`(`s_b*` 플래그) ·
`TestGameObject`(Mock 클래스). **기계적으로 올리지 말고 매번 빌드로 확인한다.**
곁들여 `OpenGLRHIResource.cpp` 에 **비어 있는 익명 네임스페이스**가 방치돼 있어 걷어냈다.

검증: Debug·Shipping 빌드 경고 0, nogpu 5/5 양쪽, 린트 6/6.

### 2026-09-11 (익명 네임스페이스를 파일당 하나로 — 합칠 수 없는 둘은 사유를 적어 둔다)

`.cpp` 에 익명 네임스페이스가 여러 개로 흩어져 있으면 하나로 합쳐 스코프 최상단에 둔다. 프로젝트
코드에서 둘 이상이던 파일은 **7 개**였다(`Tools/vcpkg/buildtrees` 는 외부 의존성이라 제외).

**합친 것 4 개** — `Logger.cpp` · `D3D11RHIDevice.cpp` · `ShaderBindingLayout.cpp` ·
`LinuxCallStackCapture.cpp`. 앞 셋은 첫 블록이 이미 `namespace sw` 최상단이라 뒤 블록 내용만 올렸다.
`LinuxCallStackCapture.cpp` 는 파일 스코프 `static` 둘(`s_initRefCount`·`s_symbolMutex`)이 블록보다
앞에 있어서 그대로는 못 올렸다 — 그 둘을 익명 네임스페이스 안으로 넣었다(같은 내부 링키지이고 이쪽이
현대 관용구다). 곁들여 직전 커밋에서 `FileUtil.cpp` 중간에 새로 만든 블록도 최상단으로 올렸다.

**나머지 3 개 — 막고 있던 것을 없애서 결국 다 합쳤다**

처음엔 "스코프/의존성 때문에 불가"로 적었는데, 셋 다 **장애물 자체가 설계 흠**이었다.

- **`Source/Core/Task/TaskManager.cpp`.** 뒤 블록의 `setTaskName` 이 완전한 `TaskNode` 를 요구하는데,
  `TaskNode` 는 앞 블록의 `kTaskNameCapacity` 를 쓴다 — 앞뒤로 묶여 순서를 바꿀 수 없었다. 그런데 그
  함수가 하는 일은 **전부 `TaskNode` 자기 필드 조작**이었다. `TaskNode::setName` 멤버로 옮기니 블록이
  통째로 필요 없어졌다. 호출부 넷은 바로 앞줄에서 이미 포인터를 역참조하고 있어 널 검사도 군더더기였다.
- **`Test/SmokeTest/TestSmoke.cpp`.** 파일 스코프 블록에 든 것은 `fillGameService` **한 줄짜리 래퍼**
  하나뿐이었고, 그것 때문에 스코프가 다른 블록이 하나 더 있었다. 호출부 둘에서
  `sw::engine::fillModuleServices( gameService, true )` 를 직접 부르게 하니 블록이 사라졌다.
  (`#if !defined(SW_SHIPPING)` 영역이 둘로 갈려 있어 래퍼를 어느 한쪽으로 옮길 수는 없었다.)
- **`Tools/ReflectionParser/ReflectionParser.cpp`.** 전역 스코프 블록의 `LoggerScope` 를 `namespace sw`
  익명 블록으로 옮기고 `main` 에서 `sw::LoggerScope` 로 쓴다. 같은 파일의 `main` 이 이미
  `sw::CommandLineArgs` 를 그렇게 쓰고 있어서 형태가 맞는다 — 이 도구의 TU 지역 헬퍼는 원래 거기 산다.

**결과: 프로젝트 `.cpp` 에 익명 네임스페이스가 둘 이상인 파일은 0 개다.**

> **남은 것(이번엔 손대지 않음):** 블록이 **하나뿐이지만 스코프 최상단이 아닌** 파일이 9 개 있다 —
> `WindowsCallStackCapture.cpp`(앞 75줄) · `VulkanRHISwapChain.cpp`(320줄) · `FrameRendererPso.cpp`(109줄) ·
> `LocalizationManager.cpp`(159줄) · `MacCallStackCapture.cpp`(47줄) · `GameService.cpp`(7줄) ·
> `GlobalVariablesPanel.cpp`(2줄) · `EngineLoop.cpp`(1줄) · `Scene.cpp`(1줄).
> 올리려면 TaskManager 때와 같은 의존성 확인이 파일마다 필요하다(앞에 있는 타입 정의를 쓰는지).

검증: Debug·Shipping 빌드 경고 0, nogpu 5/5 양쪽, 린트 6/6.
(`LinuxCallStackCapture.cpp`·`MacCallStackCapture.cpp` 는 여기서 컴파일할 수 없다 — CI 의 Linux 잡이 본다.)

### 2026-09-11 (Source/ 훑기 — 파일 다이얼로그 콜백이 분리 스레드에서 씬을 고치고 있었다)

`Source/` 792 파일 157k 줄을 결함 *부류* 별로 훑었다. 확인된 넷을 고쳤다.

**1. 파일 다이얼로그 콜백이 분리 스레드에서 라이브 상태를 고쳤다 (원인 한 곳을 고쳤다)**

`FileUtil::openFileDialog` 는 `std::thread(...).detach()` 로 다이얼로그를 띄우고 **그 스레드에서**
델리게이트를 불렀다. 네이티브 다이얼로그는 사용자가 닫을 때까지 안 돌아오므로 스레드 자체는 옳다.
문제는 **결과를 그 스레드에서 처리**한 것이다:

- `onSaveSceneDialogResult` 가 `saveActiveScene()` 으로 **메인 스레드가 tick·렌더 중인 씬을 직렬화**했다.
- 프리셋 콜백 둘이 `loadComponentPreset()` 으로 **살아 있는 컴포넌트에 역직렬화**했다.
- 셋 다 `NotificationManager::push()` 로 UI 스레드가 매 프레임 순회·삭제하는 벡터에 락 없이 넣었다.

올바른 방식은 저장소 안에 이미 둘 있었다 — `requestLoadScene`(뮤텍스 큐) 과
`ContentBrowserPanel::onImportDialogResult`(뮤텍스 큐 + `processPendingImports`). 즉 규칙이 아니라
**적용이 절반만** 돼 있었다. 호출부를 하나씩 고치는 대신 **`openFileDialog` 의 계약을 바꿨다**:
결과는 큐에 담기고 `FileUtil::pumpFileDialogResults()` 가 **메인 스레드에서** 델리게이트를 부른다
(`EngineLoop::tick` 의 "핫 리로드 / 씬 트랜지션 / 이벤트" 블록에서 매 프레임). 호출부는 전부 자동으로
옳아졌고, 앞으로 다이얼로그를 새로 쓰는 사람이 같은 함정을 밟을 자리가 없다.

수명도 같이 닫았다. 델리게이트는 `EditorModule.dll` 안의 `this`·함수 포인터를 그대로 들고,
`Delegate::isBound()` 는 함수 포인터가 널인지만 본다(생존 확인 없음). 다이얼로그를 열어 둔 채 핫
리로드/종료가 나면 언맵된 코드로 뛴다 — Undo 스택이 겪은 것과 같은 함정이다. `cancelFileDialogResults()`
를 `ImGuiEditor::shutdown` 의 **바로 그 자리**(전역 변수 해제 옆)에 두고, 세대 번호로 이미 열려 있는
다이얼로그의 결과까지 버린다.

> 알림 매니저에는 **락을 넣지 않았다.** 다이얼로그가 메인 스레드로 옮겨진 뒤 push 호출부 16 곳이
> 전부 메인 스레드다(백그라운드 IO 는 `publish` 로 이미 메인에서 꺼낸다). 대신 그 불변식을 헤더
> 주석으로 못박았다 — 근거 없는 동기화를 늘리는 것보다 낫다.

**2. 세이브가 씬 스냅샷 실패를 흔적 없이 삼켰다 — 처음 본 것보다 가벼웠다**

`serializeState` 가 `serializeSceneObjects` 의 반환값을 버리고 빈 섹션을 썼다. 처음엔 실패로 끊었는데
**그게 틀렸다** — 씬 없이 커스텀 상태만 스냅샷하는 것은 지원되는 사용법이고
(`GameFrameworkTest.GameInstanceBaseSnapshotAndFileRoundTrip` 이 그렇게 쓴다) 끊자마자 그 테스트가 졌다.
동작은 그대로 두고 **경고만** 남겼다. 실제 결함은 "빈 세이브가 나와도 아무 흔적이 없다" 쪽이었다.

**3. `InspectorPropertyUndo` 의 두 함수가 38 줄 동일했다** — `trackActiveItemEdit` 하나로 합쳤다.
각자 제 static 맵을 들고 있어 동작은 맞았지만 한쪽만 고치면 갈라진다. 첫 인자가 유효성 가드로만
쓰인다는 것도 주석으로 남겼다(스냅샷은 값이 아니라 오브젝트 XML 로 뜬다).

**4. 죽은 API 둘을 걷어냈다** — `ObjectStateSerializer::openSaveFileDialog`/`openLoadFileDialog` 는
호출부가 없었다(`[[maybe_unused]]` 로 억제돼 있었다). 아이러니하게도 다이얼로그 수명 규칙을 제대로
지킨 유일한 코드였는데, 그 규칙이 이제 `FileUtil` 에서 강제되므로 같이 걷었다.

**확인했고 깨끗한 것** (다시 파지 않도록)

- `onTick` 16 개 전부 공유 상태·정적·매니저 접근 없음 — CLAUDE.md 함정 #1 은 지켜지고 있다.
- `attachToParent` 호출부 어디도 tick 경로가 아니다.
- `ResourceUtil` 경로 캐시는 `setSearchPriority` 끝에서 무효화된다.
- `XmlNode::attributeInt/attributeFloat` 의 반환값 무시는 fallback 선주입 관용구라 정상. `Archive::readBytes` 는
  sticky `isError()` 가 받는다.
- 로그의 `%s` 는 지원되는 printf 형이다(`%#` 관례와의 편차일 뿐, 버그 아님).

검증: Debug·Shipping 빌드 경고 0, nogpu 5/5 양쪽, 린트 6/6, 에디터 DX12 실기동 종료 0 · `[Error]` 0 건.

### 2026-09-11 (GPU 가 필요한 테스트를 스위트로 갈라 `nogpu` 를 실제로 nogpu 로 만든다)

Windows Debug CI 가 여섯 건으로 졌다 — 전부 **픽셀 리드백** 테스트다.

```
 Tests passed: 403 / 409 (0 skipped)
 Tests failed (6):
   - RenderPassTest.MainPassCullsWithCameraFrustumNotLight
   - RenderPassTest.TransparentOrderMatchesAcrossBackends
   - RenderPassTest.GpuGeneratedCommandsDrawOnlyVisibleInstances
   - RenderPassTest.PerBatchMaterialColorsReachShader
   - RenderPassTest.MultiBatchPassKeepsPerBatchConstants
   - RenderPassTest.InstanceAnimationKeepsInstancesReadable
```

**`0 skipped` 가 핵심이다.** 리눅스 러너는 X11 디스플레이가 없어 창이 안 열리고 그래서 디바이스
테스트가 조용히 스킵된다. 그런데 **Windows 러너는 GPU 가 없는데도 DX11/DX12 가 Basic Render
Driver(WARP)로 초기화에 성공한다.** 그래서 스킵되지 않고 픽셀 검증이 실제로 돌아서 진다.
PSO 수준만 보는 형제들(`MaterialPermutationDrivesBatchPso`·`ViewModeSelectsDistinctPipelineStates`)은
같은 러너에서 통과한다 — 갈리는 선은 "디바이스를 만드는가" 가 아니라 "픽셀을 읽는가" 다.

**왜 새는 구멍이었나.** 필터가 이름 목록이었다: `-RenderPassTest.FrameRenderer*`. 그 이름 규칙을
정할 당시의 디바이스 테스트는 전부 `FrameRenderer…` 였다. 2026-09-08 에 들어온 여섯은 이름이 달라
그대로 CI 에 들어갔고, 그날부터 Windows Debug 가 빨갛다. 이름 목록은 또 썩는다.

**고친 방식.** 실제 RHI 디바이스를 만드는 케이스 **14 개를 `RenderPassGpuTest` 스위트로 옮기고**,
필터를 `-RenderPassGpuTest.*` 하나로 줄였다. 앞으로는 디바이스가 필요하면 그 스위트에 넣으면 된다.
`RenderPassTest` 에는 디바이스 없이 도는 22 개가 남는다. **테스트가 사라지지는 않는다** — GPU 가
있는 개발자가 돌리는 `EngineTest` 전체(435)에는 그대로 있고, 거기서 넷 다 통과한다.

```powershell
# GPU 스위트만 따로 (실제 GPU 필요)
build/Ninja-Debug/Bin/EngineTest.exe --test_filter=RenderPassGpuTest.*
```

> **PowerShell 함정:** `--test_filter=...` 처럼 쉼표가 든 인자는 **반드시 따옴표로 감싸라.**
> 안 감싸면 PowerShell 이 쪼개서 앞의 한 토큰만 먹는다 — 필터가 안 듣는데 오류도 안 난다.
> (실제로 한 번 속았다. `'--test_filter=-RHITest.*,-RenderPassGpuTest.*'`)

**남은 의문 — WARP 에서 왜 지는지는 미확인이다.** 실제 GPU 넷에서는 여섯 다 통과한다. 픽셀이 아예
0 이면 WARP 가 컴퓨트 컬링/인디렉트를 못 하는 것이고, 어중간하면 소프트웨어 래스터라이저의 렌더링
차이다. 판정하려면 실패 메시지(`… 큐브가 사라졌다 (좌 N, 우 M …)`)가 필요하다. CI 를 초록으로
되돌리는 것과 이 판정은 별개다 — `nogpu` 라벨에 픽셀 검증을 넣은 것 자체가 계약 위반이었다.

검증: Debug·Shipping 빌드 경고 0, nogpu 5/5 양쪽(Debug EngineTest_NoGPU 398/398,
Shipping 396+2 skip), 린트 6/6, `EngineTest` 전체 435/435.

**CI 가 `7e64cde7` 에서 여섯 잡 전부 초록이 됐다** — `b0c52f7c`(2026-09-07 16:20) 이후 처음이다.
세 커밋이 걸렸다: `e8ecd8c3`(경로 소문자화) → `02b0b6f2`(리눅스 둘) → `7e64cde7`(Windows 여섯).
빨간 CI 는 그 자체로 다음 결함을 숨긴다 — 이 셋도 앞의 것을 고쳐야 뒤가 보이는 순서였다.

### 2026-09-11 (CI 가 2026-09-07 부터 빨갛다 — 리눅스 실패 둘을 닫았다, Windows 는 남았다)

**CI 는 `b0c52f7c`(2026-09-07 16:20) 를 끝으로 계속 실패해 왔다.** 55 개 런이 연속 실패인데
로컬은 늘 초록이라 아무도 눈치채지 못했다. 실패 잡은 셋 — Windows Debug · Linux Debug · Linux ASan
이고, 전부 `Test` 단계다. Shipping 둘은 `-R CoreTest` 만 돌려서 통과한다.

> 런 상태는 공개 API 로 볼 수 있다(로그 본문은 관리자 권한이 필요하다):
> `curl -s "https://api.github.com/repos/sswgame/LearningTemplate/actions/runs?per_page=30&branch=main"`

**닫은 것 1 — `RenderPassTest.MaterialPermutationDrivesBatchPso` 만 스킵이 아니라 단언이었다.**
CI 러너에는 X11 디스플레이가 없어 창이 안 열리고(`Failed to open X11 Display!`) 네 백엔드가 전부
초기화에 실패한다. 형제 여덟(카메라 컬링·투명 정렬·뷰 모드 등)은 그때 `SW_TEST_SKIP` 으로 빠지는데
이 하나만 `SW_EXPECT_TRUE_MSG( attemptedCount > 0 )` 이라 혼자 졌다. 같은 규칙으로 맞췄다.

**닫은 것 2 — 리플렉션이 빈 것을 "레이아웃이 없다" 로 셌다.**
`ShaderBindingContractTest.ReflectionNamesAreUniformAcrossBackends` 가
`forwardlit_ps g_SwMaterials 원소 없음: dx11 / dx12` 로 졌다. DXBC/DXIL 리플렉션은 Windows 전용
(FXC/DXC)이라 리눅스에서는 그 둘이 **통째로 빈 결과**를 낸다. 그걸 `bFound = true` 로 세니
비교 기준(`pRef`)이 dx11 이 되고, 있지도 않은 원소를 요구해 리눅스에서만 졌다 — 셰이더가 아니라
도구가 없어서 나는 실패다. 형제 `AllBakedShadersMatchContract` 는 이미 같은 규칙을 쓰고 있었다.

> **이 둘은 바로 앞 커밋(`e8ecd8c3`, 경로 소문자화 수정)이 드러낸 것이다.** 그전까지는 리눅스에서
> 구운 바이너리를 **하나도 못 읽어** 셋이 통째로 스킵됐다. 읽히기 시작하자 비로소 이 계약 검사가
> 리눅스에서 실제로 돌았고, 그제서야 플랫폼 구멍이 드러났다. 스킵은 실패보다 조용해서 더 오래 숨는다.

**남은 것 — Windows Debug.** GH Windows 러너에는 GPU 가 없고 DX11/DX12 가 Basic Render Driver
(WARP)로 **초기화에 성공한다.** 그래서 리눅스처럼 스킵되지 않고 픽셀 검증 테스트들이 실제로 돌아서
진다(`b0517ba1` 런에서 6 건). `MainPassCullsWithCameraFrustumNotLight` ·
`TransparentOrderMatchesAcrossBackends` 둘이 확인됐고 나머지 넷은 미확인이다. 실패 메시지를
받아야 방향이 정해진다 — `좌 0, 우 0` 이면 WARP 가 컴퓨트 컬링/인디렉트를 못 하는 것이므로
초기화에서 끊어 스킵시켜야 하고(백로그의 GL `ARB_gl_spirv` 건과 같은 처방), 숫자가 어중간하면
소프트웨어 래스터라이저의 실제 렌더링 차이라 허용 오차나 필터 쪽을 손봐야 한다.

**곁들여 확인한 것:** CI 전용 `SW_ENABLE_UNITY_BUILD=ON` 은 범인이 아니다. 로컬에서
`cmake --preset Ninja-Debug -B build/Unity-Debug -DSW_ENABLE_UNITY_BUILD=ON` 으로 그대로 빌드해
경고·오류 0 을 확인했다.

검증: Debug 빌드 경고 0, nogpu 5/5, 린트 6/6, ShaderBindingContractTest 7/7 (Windows 로컬은 네
리플렉터가 다 있어 가드가 걸리지 않는다 — 커버리지는 그대로다).

### 2026-09-11 (리눅스 CI 가 구운 셰이더를 전부 못 읽던 것 — 디렉터리 열거가 경로를 소문자로 눌렀다)

리눅스 CI 로그가 구운 `.spv` 를 줄줄이 "File not found" 로 뱉었다. 그런데 그 파일들은 **전부 커밋되어
있었다.** 단서는 경로 자체였다:

```
resource/engine/shaders/bin/opengl/deferredlighting_vs.spv 를 못 찾음
 -> /home/runner/work/LearningTemplate/LearningTemplate/Source/Core/File/FileUtil.cpp:545
```

같은 로그 줄 안에서 **소스 위치는 `LearningTemplate/…/Resource`** 인데 **찾는 경로만
`learningtemplate/…/resource`** 다. 환경이 아니라 우리 코드가 경로를 소문자로 누른 것이다.

**원인.** `FileUtil::collectFiles`/`collectFolders` 의 `bNormalizePath` 기본값이 `true` 였고, 그 기본값은
**방금 파일시스템에서 훑어 온 절대 경로를 통째로 `normalizePath`(= 전체 소문자화)** 했다. `Resource/` 아래는
`CheckResourceCasing` 이 소문자를 강제하므로 소문자화가 바꾸는 것은 사실상 **루트 접두사뿐**이다 — 그건
그 PC 의 실제 디렉터리 이름이라 절대 건드리면 안 되는 구간이다. 윈도우는 대소문자를 안 가려서 그대로
열렸고, 리눅스에서만 열거한 파일이 즉시 "없는 파일"이 됐다.

`FileUtil` 헤더에 이미 규칙이 적혀 있었다 — *"맵 키는 normalizePath, open 은 normalizeSeparators"*.
열거 결과는 **여는 경로**이므로 소문자화는 애초에 규칙 위반이었다.

**고친 방식.** 플래그를 끄는 게 아니라 **매개변수를 없앴다.** 호출부 30 곳을 전부 확인했더니 결과를 쓰는
곳은 예외 없이 실제 I/O(`readFile`·`getFileTimestamp`·`removeFile`·`loadFromFile`·`mountPack`·텍스처 베이크)
였고, 소문자가 필요한 곳은 **하나도 없었다.** 확장자 비교(`hasExtension`)와 팩 우선순위
(`matchesTokenCaseInsensitive`)는 원래 대소문자를 안 가리므로 영향이 없다. 키가 필요한 쪽은 예전부터
받은 뒤에 직접 `normalizePath` 한다(`AssetDatabase::toRelativePath`, `ShaderBaker::writeBakeStamp`).

이미 호출부 절반 이상이 `false` 를 명시하고 있었다 — 같은 함정을 한 곳씩 피해 온 흔적이다.
`AssetDatabase::scanMetaFiles` 에는 그 사연이 주석으로 남아 있었는데, 그때 **함수 쪽을 고치지 않아서**
나머지 호출부가 그대로 남았다. 기본값이 함정이면 호출부마다 다시 밟는다.

**회귀 테스트.** `Core_File.CollectPreservesPathCase` — 대문자가 섞인 임시 디렉터리
(`SwCollectCaseRoot/MixedCaseSub/MixedCaseAsset.Bin`)를 만들고, 수집한 경로가 **만든 문자열과 바이트까지
같은지** 본다. 윈도우에서는 소문자 경로로도 파일이 열려 버리므로 `readFile` 성공 여부만으로는 못 잡는다 —
그래서 문자열 동등성을 본다.

검증: Debug 빌드 경고 0, nogpu 5/5, 린트 6/6.

### 2026-09-11 (Shipping 에서만 지던 테스트 둘 — 테스트 호스트가 앱과 다른 세상을 보고 있었다)

`Engine_Resource.AssetDatabaseKnowsAssetsBeforeTheyAreLoaded` 와
`SceneTest.EditorTestSceneResolvesMovedPrefabByGuid` 가 **Shipping 에서만** 졌다(Debug 통과).
원인이 둘이었고, 둘 다 "테스트 호스트가 앱과 다른 상태에서 돈다" 는 같은 종류였다.

**1. 테스트 호스트가 설정을 활성화하지 않았다.** `ResourceManager::loadAssetRegistries` 는 게임 도메인
레지스트리 경로를 `GameConfig::getActive()._packRoot` 로 만드는데, `TestFramework/main.cpp` 는
`GameConfig::setActive` 를 부른 적이 없어 그 항목이 늘 비었다. 그래서 시작 시점 GUID 표에 engine·common
**3 항목**만 실렸다(앱은 5). 느슨한 `Resource/` 트리만 있는 Dev 에서는 `.meta` 스캔 폴백이 대신 채워
줘서 안 드러났다 — 팩이 하나라도 실리면(Shipping) `registered > 0` 이 되어 그 폴백은 돌지 않는다.
이제 테스트 호스트도 앱과 같은 순서로 `EngineConfig`(검색 우선순위) · `GameConfig`(setActive) 를
활성화하고 `loadAssetRegistries()` 를 다시 부른다. **리플렉션 등록을 리소스 초기화 앞으로 옮겼다** —
설정 역직렬화가 `TypeInfo` 를 쓰므로 순서를 어기면 세그폴트다(실제로 한 번 났다). `EngineLoop` 도
같은 순서다.

**2. 팩 테스트가 전역 VFS 를 비우고 되돌리지 않았다.** `Engine_ResourcePack` 의 셋이 우선순위·오버라이드를
보려고 `ResourceUtil::getPackManager().unmountAll()` 을 부른다. 그 판은 프로세스 전체가 쓰는 것이라,
뒤에 도는 `SceneTest` 가 팩을 통째로 잃고 쿠킹된 씬 바이너리를 못 찾았다. **단독으로 돌리면 통과해서**
오래 원인이 안 잡혔다 — ctest 로 스위트 전체를 돌려야 재현된다. `GlobalVfsScope` RAII 를 그 셋에 붙여
마운트·검색 우선순위·loose 허용을 시작 시점으로 되돌린다. 되돌릴 때 쓰라고 `ResourceManager::initialize`
의 팩 탐색을 `mountStartupPacks()` 로 뽑았다(후보 경로 목록이 두 곳에 복사되지 않도록).

**추적 순서가 핵심이었다.** "Shipping 만 진다" 에서 바로 코드를 고치지 않고, (a) Debug 테스트 바이너리를
**Shipping 팩이 있는 디렉터리에서** 돌려 Info 로그를 보고("에셋 레지스트리 3 항목"), (b) `readFile` 에
임시 Warning 프로브를 박아 팩 조회가 실제로 성공하는지 확인하고(성공했다 — 앱과 테스트 호스트가 같은
지점에서 갈리지 않는다는 뜻), (c) ctest 와 단독 실행이 다르다는 것을 보고 상태 오염으로 좁혔다.

**검증**: Shipping nogpu **5/5**(EngineTest_NoGPU 407 통과 + 2 스킵, 실패 0) · Debug nogpu 5/5 ·
Debug EngineTest 전체 **435/435** · 린트 6/6 · 빌드 경고 0 · 에디터 DX12 실기동 종료 0 · Shipping 앱
실기동 종료 0. 0절의 기준선 숫자도 이 실측으로 갱신했다(예전 값은 2026-09-10 것이라 어긋나 있었다).

### 2026-09-11 (API 에 섞여 있던 테스트 전용 함수 다섯을 걷어냈다)

"공개 API 에 에디터/테스트 목적 함수가 있는가" 로 `RuntimeAPI` → `Engine`/`Core` 공개 헤더 → RHI 인터페이스를
훑고, 후보마다 **호출자를 전부 확인해** 다섯을 걷었다. 판정 기준은 "이름이 수상한가" 가 아니라 **누가 부르는가**
였다 — 이름만 보면 `getMapEntry` 는 평범한 접근자이고, `executeOffscreenPipelineSmoke` 는 이름이 스모크라고
말하고 있었는데도 `SW_API` 인터페이스 안에 있었다.

- **`IRHIDevice::executeOffscreenPipelineSmoke`** — RT 만들고 → 렌더패스 열고 → 그리고 → 읽고 → 지우는 80줄짜리
  검증 루틴이 RHI 디바이스 인터페이스의 한 섹션(`9) 오프스크린 검증`)을 통째로 차지하고 있었다. 부르는 곳은
  `TestRHI.cpp` 셋뿐인데 `SW_SHIPPING` 가드가 없어 **배포 바이너리에도 들어갔다.** 쓰는 것이 전부 공개 RHI
  인터페이스라 `TestRHI.cpp` 의 익명 네임스페이스로 그대로 내렸다(`IRHIDevice&` 를 첫 인자로). 덤으로 헤더에
  선언 없이 떠 있던 `/** 스왑체인과 백버퍼 크기를 바꿉니다 */` 고아 주석과, `.cpp` 에서 쓸모없어진 include
  셋(`IRHICommandList.h`·`IRHIResource.h`·`ShaderBindingSlots.h`)도 같이 걷혔다.
- **`ReflectionRpc::packAndInvoke`** — 주석부터 "테스트용 왕복" 이었고 호출자는 `TestReflection.cpp` 하나. 테스트가
  `packCall` + `unpackAndInvoke` 두 줄을 직접 부르게 하고 지웠다.
- **`InputManager::injectSnapshot`** — 히스토리 버퍼에 합성 스냅샷을 밀어 넣는 백도어. 호출자는 테스트 하나였고,
  정작 리플레이 재생 경로(`InputReplay::updatePlayback`)는 `postRawEvent` 를 쓴다. 지우고 테스트는 실제 경로로
  바꿨다 — 마우스 버튼을 눌러 한 프레임 돌리고 `recordSnapshot` 이 남긴 것을 본다(`InputMutingAndInjection` →
  `InputMutingAndSnapshotRecording`). **검사 범위가 늘었다**: 예전 테스트는 버퍼 왕복만 봤고 아무도
  `recordSnapshot` 의 마스크 조립을 보지 않았다.
- **`ResourcePackReader::getMapEntry`** — 주석은 "디버깅/테스트용" 인데 저장소 전체에 호출자가 **0** 이었다. 내부
  FAT 맵을 통째로 노출만 하고 아무도 안 쓰는 죽은 API 라 삭제.
- **`EngineLoop` 의 공개 계측 멤버 넷** (`_profileFrameTarget`·`_bProfileReported`·`_bWantsQuit`·`_bProfileWarmedUp`)
  — `SW_API` 클래스의 public 구역에, 그것도 함수들 사이에 끼어 있었다. 만지는 곳은 `EngineLoop.cpp` 자신뿐이고
  밖으로 알릴 것은 `wantsQuit()` 하나다. **private 로 내린 뒤 `FrameProfileSession`(`Engine/Utility/Debug/`)
  으로 아예 빼냈다** — 상태 넷과 `tick` 한가운데 박혀 있던 워밍업·보고·종료 판정 스무 줄이 한 타입으로 모이고,
  `EngineLoop` 은 `begin` / `onFrameEnd` 두 줄과 `wantsQuit()` 위임만 남는다. **상속이 아니라 합성인 이유**:
  `EngineLoop` 은 가상 함수가 하나도 없고 App 이 값으로 들고 있다(`App.h` 의 `EngineLoop _engineLoop;`).
  계측 하나 때문에 `SW_API` 클래스에 vtable 을 만들고 App 의 소유 모델을 포인터로 바꿀 일이 아니다.
  동작은 그대로다 — Debug 실측 `-gv_profileFrames=40` 이 워밍업 60 폐기 → 40 프레임 측정 → 표 출력 →
  종료 코드 0 으로 예전과 같다.

**이어서 — 검증용 `gv_` 들을 쓰는 자리로 내렸다.** `EngineLoop.cpp` 이 gv_ 선언의 잡동사니 서랍이 돼 있었다.
선언은 거기 있고 읽는 쪽은 다른 파일에서 `extern` 으로 끌어 쓰는 형태라, **타입이 어긋나도 링커까지 가야
걸린다.** 소비자가 Engine 안에 있는 여섯을 소비자 파일로 옮기고 `extern` 재선언을 지웠다.

| 옮긴 것 | 간 곳 | 소비자 |
|---|---|---|
| `gv_profileFrames` | `Utility/Debug/FrameProfileSession.cpp` | `FrameProfileSession::begin()` (이제 인자 없이 스스로 읽는다) |
| `gv_screenshot` · `gv_screenshotAttachment` · `gv_screenshotFrame` | `Graphics/Renderer/RenderThread.cpp` | `RenderThread` 스크린샷 경로 |
| `gv_gpuCulling` | `Graphics/Renderer/Frame/FrameRenderer.cpp` | `FrameRenderer` 컬링 디스패치 판정 |
| `gv_defaultMaterial` | `Scene/Scene.cpp` | `resolveDefaultMaterialPath` |

**그리고 모듈이 자기 전역 변수를 스스로 등록하게 만들어, 남은 일곱도 제 모듈로 보냈다.** 묶여 있던 것은
규칙이 아니라 **순서**였다: `EngineLoop::initialize` 가 `registerPendingVariables("Engine")` →
`registerToCommandLine` → `parse(argv)` → `updateFromCommandLine` 순으로 도는데 모듈은 그 **뒤에**
로드되고, `CommandLineManager::parseArgumentLine` 이 모르는 키를 경고만 찍고 **값을 버렸다**.
기계장치는 절반이 이미 있었다 — 모듈별 헤드 체인(`GlobalVariableRegistrar::linkTo`),
`registerPendingVariables`, 그리고 **호출자가 0이던** `unregisterVariablesByModule`. 배선만 없었다.

- **보류표**: 미등록 키가 `gv_` 로 시작하면 버리지 않고 `_mapPendingGlobal` 에 남긴다. 표는 비우지
  않는다 — 핫 리로드로 모듈이 다시 올라와도 커맨드라인 값은 프로세스 수명 내내 유효해야 한다.
- **늦은 적용**: `GlobalVariableManager::registerVariable` 이 등록 직후 보류값을 꺼내 적용한다.
  엔진 자신의 변수는 `registerToCommandLine` **이전에** 등록되므로 여기 걸리지 않는다(기존
  `updateFromCommandLine` 경로 그대로).
- **오타 감지**: 즉시 경고가 사라지므로 `App::warnUnclaimedGlobalOverrides()` 가 모듈이 다 올라온 뒤
  임자 없는 키를 한 번 경고한다. 표는 비우지 않으니 나중에 로드되는 모듈이 여전히 가져갈 수 있다.
- **모듈 쪽 보일러플레이트는 매크로 한 쌍**(`SW_DECLARE_MODULE_GLOBAL_VARIABLES` /
  `SW_IMPLEMENT_MODULE_GLOBAL_VARIABLES`)으로 접었다. 모듈이 쓸 것은 헤드 매크로를 갈아 끼우는 두 줄
  (전처리기 지시자는 매크로 안에 못 넣는다)과 이 매크로 두 개가 전부다. 등록/해제 호출 지점은
  `ImGuiEditor::initialize/shutdown` 과 `EmptyGame::onInitialize/onShutdown` 이다 — `ModuleHost` 가
  `bindService` **뒤에** `initialize` 를, `shutdown` **뒤에** `bindService(nullptr)` 을 부르므로 둘 다
  서비스가 살아 있는 구간이다.

부수 효과 하나: `BenchScene` 이 이제 `findVariable("gv_benchMeshes")` 문자열 조회 대신 변수를 **직접**
읽는다(같은 모듈이니까). 예전 조회는 이름을 잘못 쓰면 조용히 0 으로 읽혔다.

`EngineLoop.cpp` 에 남은 전역 변수는 **자기가 읽는 `gv_crashTest` 하나**다.

**검증**(전부 실측)
- Engine 쪽 여섯: `-gv_screenshot` 이 1280×720 PPM 을 뽑고, `-gv_defaultMaterial=…benchtextured.material`
  이 픽셀을 바꾼다(평균 R 33.73→33.53, B 46.72→45.59). `-gv_gpuCulling=0` 은 픽셀이 같은데 이건 문서대로다
  (간접 인자를 GpuScene 이 CPU 에서 이미 채우므로 디스패치만 빼도 화면은 같다).
- 에디터 셋: `-gv_editorOpenAllPanels=1 -gv_editorPanelDump=40` → "전부 열었습니다" + 창 30개·내용 없는
  패널 0개. `-gv_editorStartupScene=game/empty/maps/editortest.scene.xml` → "시작 씬을 엽니다" + 창 15개.
- 벤치 넷: `-gv_benchMeshes=64 -gv_benchMeshVariants=4` → "메시 종류 4개", "큐브 64개". **Shipping(정적 링크)**
  에서도 같은 스위치가 먹는다 — Info 로그가 없으므로 스크린샷으로 확인했다(평균 R 31.00→34.20).
- 오타: `-gv_typoHere=3` → `gv_typoHere: 그런 전역 변수가 없습니다` (Debug·Shipping 양쪽).
- 어느 실행에도 `already registered` 경고가 없다(= 등록/해제가 짝이 맞는다).

**남긴 것과 이유.** 에디터 전용인데 엔진 쪽에 있는 것들(RHI 의 ImGui 네이티브 핸들 넷, `CommandStack`,
`SW_API` 로 내보낸 `gv_editor*` 셋, `SceneManager::getFrameRenderer`, RuntimeAPI 의 `EditorAPI`·`IModuleCompiler`)은
**의도된 경계**다 — 전부 그 자리에 있는 이유가 주석에 있고, 이미 `gameAllowed=0`·Shipping 미생성으로 좁혀져 있다.
테스트만 부르는 미배선 기능들(`Component::registerSubTick`·`SceneDocument::saveBinary`·`SceneManager::createScene`·
`FrameRenderer::findPsoDesc` 등)도 "아직 런타임이 안 쓰는 기능" 이지 테스트 목적 함수가 아니라 그대로 둔다.

**검증**: Debug/Shipping 빌드 경고 0, 린트 6/6, Debug nogpu 5/5, `RHITest.*` 13/13(GPU, `OffscreenDrawIsReadable`
포함), 에디터 DX12 실기동 종료 0 · `[Error]`/`[Warning]` 0건.

> **Shipping nogpu 에 선행 실패 2건이 있다(이 작업과 무관).** `Engine_Resource.AssetDatabaseKnowsAssetsBeforeTheyAreLoaded`
> 와 `SceneTest.EditorTestSceneResolvesMovedPrefabByGuid` 가 Shipping 에서만 진다(Debug 는 둘 다 통과).
> 이 작업을 `git stash` 하고 HEAD 를 다시 빌드해도 **같은 둘이 같은 자리에서** 지는 것을 확인했다 —
> 0절의 "Shipping 426(+2 skip)" 기준선은 지금 실측과 다르다(EngineTest_NoGPU 405/409, 실패 2 · 스킵 2).
> 둘 다 팩의 `assetregistry.txt` / 쿠킹된 씬 바이너리를 읽는 경로라 그쪽을 봐야 한다.

### 2026-09-11 (리눅스 전용 코드를 Windows 수준으로 — 크래시 리포트가 핵심)

WSL 빌드가 선 뒤 리눅스 전용 코드 2791 줄을 훑어 나온 여섯 가지다. 하나하나가 별도 커밋이다.

- **크래시 리포트에 폴트 주소도 폴트 지점 스택도 없었고, 스택 오버플로는 기록이 아예 안 남았다.**
  `std::signal` 은 siginfo·ucontext 를 주지 않아 폴트 주소가 **항상 nullptr** 이었고,
  `captureFromContext` 는 컨텍스트를 버려서(`(void)pPlatformContext;`) 스택이 핸들러 안에서
  시작했다. 게다가 `sigaltstack` 이 없어 스택이 바닥난 SIGSEGV 는 핸들러 진입 자체가 다시
  폴트났다 — 가장 알고 싶은 크래시가 조용히 사라지고 있었다. `sigaction(SA_SIGINFO|SA_ONSTACK)`
  + 대체 스택 + ucontext 의 폴트 PC 로 셋 다 닫았다. Windows 는 `EXCEPTION_POINTERS` 로 이미
  같은 정보를 쓰고 있었다.
- **XIC 캐시가 Display 만 비교했다** — 주석은 "창이 바뀌면 재생성" 인데 코드가 아니었다. 같은 X
  서버에서 창을 다시 만들면 파괴된 Window 를 문 XIC 가 남아 텍스트 입력이 죽는다. 커서도 같은
  형태였고(무효화·해제 없음), 둘 다 shutdown 훅에서 해제하도록 했다.
- **진동 지원 검사가 반쪽이었다.** `EVIOCGBIT` 비트맵을 받아만 두고 `FF_RUMBLE` 을 보지 않아
  럼블 없는 노드가 통과했다. 그리고 호출마다 이펙트를 지웠다 올려 ioctl 3 회를 물었다 — 값이
  바뀔 때만 하도록 했다(XInput 은 값싼 호출이라 Windows 엔 없던 비용이다).
- **파일 워처 큐에 상한이 없었다.** 4096 에서 끊고 합성 rescan 하나로 알린다(커널 큐 넘침과 같은
  방식). 저장 한 번에 `IN_MODIFY`+`IN_CLOSE_WRITE` 로 중복 나가던 것도 합쳤고, 감시 한도
  초과를 성공으로 보고하던 것도 고쳤다.
- **`XQueryPointer` 를 매 프레임 물고 있었다** — 서버 동기 왕복이다. 포인터가 창 안이고 포커스가
  있으면 이벤트가 빠짐없이 오므로 그때는 건너뛴다.

> 검증: 프로브로 0x1234 접근 → `at address: 4660` 과 폴트 지점 0번 프레임 확인. 무한 재귀 →
> 완전한 리포트. `SA_ONSTACK` 없이 같은 실험을 하면 핸들러가 아예 안 돌고 exit 139 로 죽는 것을
> 따로 확인했다. 빌드 경고 0, ctest 12/12.
> **Windows 실측은 아직이다** — 크래시 핸들러는 플랫폼별 파일이라 영향이 없지만, 확인은 남았다.

### 2026-09-11 (clang-format 을 버전 하나로 고정하고, LLVM 경로 손목록을 glob 으로)

**왜 갈렸나.** 두 갈래였다. `llvm_download_urls` 의 핀이 Windows 20.1.8 / Linux 18.1.8 로 서로
달랐고(누군가 한쪽만 올렸다), 더 근본적으로는 `llvm_search_roots` 가 **시스템 LLVM 을 먼저**
쓰기 때문에 실제 버전이 "그 PC 에 뭐가 깔렸느냐" 로 정해졌다. 핀을 맞춰도 이쪽으로 계속 갈린다.

빈말이 아니다 — 18 과 20 은 이 저장소 400 파일 중 **2 개**를 다르게 포맷한다. 동아시아 문자 폭
계산이 달라져 한글 주석이 든 정렬 블록이 갈린다. 두 PC 가 서로의 커밋을 되돌리는 종류의 차이다.

- **clang-format 은 고정, 빌드용 LLVM 은 최신.** 포매터는 "설치된 것" 이 아니라 "정해진 것" 을
  써야 한다(최신을 자동으로 따라가면 한 PC 가 업데이트되는 순간 코드베이스가 재포맷된다).
  반대로 clang/libclang 은 최신을 찾아 쓰는 게 맞다. 그래서 둘을 갈랐다.
- **`clang_format_version` 하나만 남겼다(20.1.8 — 저장소가 이미 이걸로 포맷돼 있다).** URL 은
  PyPI 에서 받아 온다. LLVM 공식 릴리스는 전체 배포판만 올려서, clang-format 하나 때문에
  335 MB(리눅스 18) 나 2 GB(20.1.8 의 `LLVM-*-Linux-X64.tar.xz`) 를 받아야 했다. PyPI 의
  `clang-format` 패키지는 **바이너리 하나만** 담은 휠(1.4~1.7 MB)을 플랫폼별로 내고 버전이
  LLVM 릴리스를 그대로 따른다. 휠 URL 엔 해시가 박혀 손으로 적어 둘 수 없으므로 버전만 고정하고
  URL 조회는 `resolveClangFormatWheelUrlInternal` 이 한다 — 버전을 올릴 땐 문자열 하나만 고친다.
- **tarball 에서 clang-format 만 꺼내던 `extractClangFormatFromArchiveInternal` 은 지웠다.**
  따로 구할 길이 없어서 있던 코드였고, 이제 필요 없다. `llvm_download_urls` 자체는 clang-cl /
  libclang 부트스트랩에 계속 쓰이므로 남긴다.
- **찾은 clang-format 이 실제로 뜨는지 확인한다.** 파일이 있다는 것만으로는 부족했다 —
  ubuntu-18.04 빌드는 `libtinfo.so.5` 를 찾는데 요즘 배포판엔 `.so.6` 뿐이고 심볼 버전이 달라
  실행 자체가 안 된다. 그런데도 훅은 "포맷팅 규칙에 어긋나는 파일이 있습니다" 라고 **거짓
  보고**했다. 이제 버전까지 맞는 것만 고르고, 고정본을 못 구하면 있는 것으로 돌리되 결과가
  달라질 수 있음을 분명히 알린다.
- **도구 버전을 키 하나로 올리고 URL 은 그것을 참조한다.** `llvm_version`·`ninja_version`·
  `sccache_version` 을 두고, `loadSearchPaths` 가 설정 안의 `${key}` 를 같은 설정의 최상위
  스칼라로 치환한다(`expandSelfReferencesInternal`). 버전이 URL 안에 두 번씩 박혀 있으면 올릴 때
  여러 줄을 손대야 하고 플랫폼 하나를 빠뜨리면 그때부터 PC 마다 다른 버전을 쓴다 — 실제로
  Windows 20.1.8 / Linux 18.1.8 로 갈려 있었다. `${sourceDir}`·`${ProgramFiles}` 처럼 여기서 값을
  알 수 없는 것은 건드리지 않는다(경로 확장은 쓰는 자리에서 맥락을 갖고 한다).
- **clang-format 만은 버전을 따로 둔다.** `llvm_version` 에 묶으면 컴파일러를 올리는 순간
  코드베이스가 통째로 재포맷된다 — 그 둘은 같은 이유로 움직이지 않아야 한다.
- **`platformSearchRoots` 가 `*` 를 펼친다.** `llvm_search_roots.linux` 의
  `/usr/lib/llvm-20 … llvm-14` 손목록을 `/usr/lib/llvm-*` 로 바꿨다. `8bda8366` 이
  `ReflectionParser/CMakeLists.txt` 와 `ci.yml` 에서 걷어낸 함정이 이 JSON 에는 남아 있어
  llvm-21 인 이 PC 를 비켜가고 있었다. 펼친 결과는 자연순 내림차순이라 최신이 앞에 온다
  (`llvm-9` 가 `llvm-21` 을 이기지 않는다).

> 검증: `Tools/LLVM/bin/clang-format` 을 지우고 `ensureClangFormat()` 을 돌려 1 MiB 를 받아
> 20.1.8 이 서는 것을 확인했다. 재실행은 받지 않는다. `platformSearchRoots` 는
> `/usr/lib/llvm-21` 을 찾아내고 `SetupLlvm.py` 가 그걸 쓴다.

### 2026-09-11 (WSL 빌드를 세우자 드러난 결함 다섯 — 대부분 플랫폼과 무관한 잠복 버그였다)

`WSL-Debug` 를 끝까지 돌린 결과다. 빌드 오류·경고 0, `ctest` 12/12. 하나하나가 별도 커밋이다.

- **리플렉션 코드젠이 `1.000000ff` 를 뱉었다.** `%#` 을 순수 자리표로 확정한(`50c57a18`) 뒤
  `CodeGenerator.cpp` 의 `"%#ff;"` 두 줄만 옛 규칙(변환 문자를 먹던 시절)에 남아 있었다.
  뒤따르는 `ff` 가 리터럴이 되어 범위가 붙은 PROPERTY 의 `.gen.cpp` 가 전부 깨졌다.
  Windows 에서 안 드러난 건 그쪽 생성 파일이 규칙 변경 전 것이라 그대로였기 때문이다.
- **Vulkan 이 present 없는 프레임마다 스왑체인 이미지를 새로 물었다.** `beginFrame` 은 항상
  acquire 하는데 `endFrame` 은 `bPresent` 일 때만 present 한다. Vulkan 엔 present 말고 이미지를
  돌려주는 길이 없어, 이미지 개수를 넘기는 순간 `UINT64_MAX` acquire 가 영원히 막힌다.
  `GpuSceneBufferReusedAcrossPackets` 가 8 프레임을 그렇게 돌아 교착했다(180 초 타임아웃).
  **플랫폼 문제가 아니다** — Windows 는 그 테스트가 DX11 을 먼저 잡아 Vulkan 에 닿지 않았을 뿐.
  덤으로 아무도 기다리지 않는 `renderFinished` 를 매 프레임 거듭 시그널하던 규약 위반도 닫았다.
- **GL 이 `ARB_gl_spirv` 없이도 초기화 성공을 보고했다.** 셰이더를 SPIR-V 로만 올리는 백엔드가
  PSO 를 하나도 못 만드는 상태로 "정상" 을 반환해, 렌더링 검증 7 건이 "큐브가 그려지지 않았다"
  로 떨어졌다. 실패 문구에 원인이 전혀 안 드러나는 종류다. 이제 초기화에서 끊어 상위가 건너뛴다.
- **`.meta` 스캔이 절대 경로를 소문자로 내렸다.** `scanMetaFiles` 만 `bNormalizePath=true` 로
  수집해 `/home/…/LearningTemplate/Resource` 까지 소문자가 되고, 루트 비교가 어긋나 전부 버려졌다
  (레지스트리 0 항목). 바로 위 `refreshFolder` 는 `false` 로 대소문자를 보존하고 있었다.
  대소문자를 가리는 파일시스템에서만 드러난다. 두 테스트가 함께 풀렸다.
- **비 Windows 의 DXBC 타깃이 `invalid profile vs_5_0` 로 죽었다.** FXC 블록이 `#if` 로 빠지는
  플랫폼에서 타깃이 DXC 로 흘러가 SM5 프로파일을 거부당했다. 사유를 분명히 돌려주고, 테스트는
  해당 타깃만 건너뛴다(테스트 전체를 스킵하면 리눅스의 DXIL/SPIR-V 검증까지 날아간다).
- 곁들여: `CheckSourceGlob` 의 "다른 OS 소스" 무시 목록이 Windows 기준으로 굳어 있어 리눅스에서
  DX11/DX12/Windows 소스 23 개를 오탐했다 — 호스트에 맞춰 반대편만 무시하게 했다.
  `.vscode/launch.json` 은 전부 Windows 전용이었어서 CodeLLDB 기반 WSL 구성 7 개를 넣었고,
  `SetupLinuxDevEnvironment` 에 `libwayland-dev`·lldb 검사를 더했다.

> 검증: `cmake --build --preset WSL-Debug` 경고 0, `ctest --test-dir build/WSL-Debug` 12/12.
> `EngineTest` 단독 418 통과 + 8 skip. 교착하던 `engine` 스위트는 180 초 타임아웃 → 11.7 초.
> **Windows 에서는 아직 안 돌렸다** — 위 다섯 중 Vulkan·GL·ShaderCompiler 는 Windows 경로도
> 건드리므로(정상 경로 동작은 그대로일 것이나) 그쪽 실측이 남아 있다.

### 2026-09-11 (리눅스 LLVM 탐색 — 손으로 든 버전 목록이 새 배포판에서 조용히 비켜간다)

- `Tools/ReflectionParser/CMakeLists.txt` 가 `/usr/lib/llvm-20 … llvm-14` 를 **손으로 나열**하고 있었다.
  이 PC 의 WSL 은 **llvm-21** 이라 목록 밖이고, 그러면 libclang 을 못 찾아 `SW_REQUIRE_REFLECTION=ON`
  에서 configure 가 죽는다. 목록 대신 `/usr/lib/llvm-*` 를 glob 해 **설치된 것 중 가장 높은 메이저**를
  고른다(자연순 내림차순).
- `find_library` 의 이름 목록도 같은 문제였다(`clang-20 … clang-14`). distro 는 `libclang-21.so` 처럼
  버전이 붙은 이름만 두기도 하므로 `libclang-*.so` 를 glob 해 이름을 만들어 넘긴다.
- `.github/workflows/ci.yml` 의 `LLVM_DIR` 탐색 루프도 같은 손목록이었다 — `ls -d /usr/lib/llvm-* | sort -V -r`
  로 바꿨다. 러너 이미지가 LLVM 을 올릴 때마다 워크플로를 고쳐야 하는 함정을 없앤다.

> 검증: 두 블록만 떼어 WSL 에서 `cmake -P` 로 돌렸다 — `Using distro LLVM: /usr/lib/llvm-21`,
> `LIBCLANG_LIB=/usr/lib/llvm-21/lib/libclang.so`. 전체 빌드로는 확인하지 않았다(1-2b 보류).

### 2026-09-11 (`formatstring` 정리 — tuple 프로토콜 제거, 사연 주석 정리; `.cpp` 분리는 재 보고 폐기)

- **`std::tuple_size/tuple_element` 특수화와 `FormattedValue::get<I>()` 를 지웠다.** 이게 있어야 되는 코드는
  `auto [value, format] = Fmt( 255, Format().hex() );` 같은 구조적 바인딩뿐인데, 포매터는 `getValue()/getFormat()` 을
  쓰고 저장소에 그 바인딩은 0곳이다.
- **주석에서 사연을 뺐다.** 규칙과 "왜" 는 한 줄로 남기고, "예전엔 …였다" 류 역사는 백로그(이 문서)에 이미 있으므로
  헤더에서 걷었다. 변환 문자 주석 하나는 **틀린 채 남아 있었다** — "`%#x%#` 는 가로를 16진수로 찍는다" 는 순수 자리표
  규칙 이후 거짓이다.
- **비템플릿 부분을 `formatString.cpp` 로 빼는 것은 재 보고 폐기했다.** 헤더 변경으로 촉발되는 전체 재빌드가 분리 전
  41 s, 분리 후 44 s — 이 헤더의 파싱 비용은 빌드 시간을 좌우하지 않는다(병렬·sccache 가 지배). 얻는 게 없으니
  파일을 늘리지 않는다. Core 의 소스 목록이 glob 이 아니라 **명시 목록**(`Source/Core/CMakeLists.txt` 의 `cfSources`)
  이라 새 `.cpp` 를 적지 않으면 ReflectionParser 링크가 깨진다는 것도 그때 확인했다.
- 4번 후보(재귀 템플릿 → 타입 소거 인자 배열)는 실행 시간을 줄이지 않는다(간접 호출 하나 손해)는 것을 확인하고 **하지
  않기로 했다** — 지금 구조는 빌드 위생을 조금 내주고 인라인을 얻는 거래이고, 그 둘 다 아직 숫자로 아프지 않다.

### 2026-09-11 (`formatstring` 구조 — 실수 폴백 UB 제거, 두 add 경로를 하나로, 값은 목적지에 바로 쓴다)

"구조를 더 개선할 수 있나" 에 대한 답이었다. 네 후보 중 실행 시간을 줄이지 않는 것(재귀 템플릿 → 타입 소거 배열, fmt
방식)은 **하지 않기로 했다** — 얻는 게 컴파일 시간·바이너리 크기뿐이고 실행에선 인자당 간접 호출 하나 손해다. 헤더
분할은 측정 없이는 하지 않는다. 남은 둘을 했다:

- **실수 폴백이 UB 였다.** `kFloatBufferSize` 가 128 이라 |x| ≥ 1e121 이면 고정소수점 `to_chars` 가 실패하고
  `fallbackFloatToString` 으로 떨어졌는데, 그 첫 줄이 `static_cast<uint64>( value )` — 1e300 을 uint64 로 캐스트하는 UB.
  버퍼를 고정소수점 최대 길이(부호 + 309 + '.' + 255 + NUL = 567 → 640)로 키우고 폴백을 지웠다. 실패는 이제 불가능하고,
  그래도 나면 조용한 쓰레기 대신 `?` 와 단언이다. `Core_String.FormatStringHugeFloatAndDirectWrite` 가 1e300 의 301자리를 본다.
- **`addValue` / `addValueWithFormat` 를 하나로.** 문자열 지름길·널 가드·Fmt 풀기가 두 번 복제돼 있었다(오늘 널 가드를
  두 곳에 넣어야 했던 이유). 서식 유무는 `const Format* pSpec` 하나로 받고, 변환 서식(Fmt 우선)과 너비 서식(pSpec 우선)을
  한 곳에서 정한다.
- **값을 목적지에 바로 쓴다.** 예전엔 문자열이 아닌 값은 항상 스택 임시(kTempBufferSize)에 변환한 뒤 복사했다. 너비
  맞춤이 없고 목적지에 변환 최대 길이만큼 남아 있으면(8 KB 로그 버퍼는 거의 늘 그렇다) 임시도 복사도 없다. 패딩이 필요하거나
  버퍼 끝에 가까울 때만 임시를 거치고, 잘림 규칙(앞부분만 남는다)은 그대로다 — 같은 테스트가 두 경로의 결과를 대조한다.

### 2026-09-11 (`formatstring` 남은 구멍 — 미지원 타입은 컴파일 오류로, 널 문자열은 (null) 로, 인자 수는 Debug 단언으로)

규칙을 확정하고 나서 남은 것은 문법이 아니라 "실패가 조용한" 자리였다. 잘림(호출자가 버퍼를 정한 것 — `fixed_string`
때와 같은 판단으로 API 를 바꾸지 않는다)과 `%g/%e`(쓰는 곳이 없다)는 두고, 둘을 닫았다.

- **string 으로 변환할 수 없는 타입은 컴파일 오류다.** 예전엔 컴파일되고 런타임에 `[unsupported type]` 이 찍혔다. 이걸
  `static_assert` 로 바꾸자 죽은 경로가 하나 드러났다: printf 서식(`%5d`)에 `Fmt(v, …)` 를 주면 래퍼를 풀지 않고
  변환에 넘겨 `[unsupported type]` 이 나오던 자리 — 이제 값 변환은 Fmt 의 서식(기수·정밀도), 너비·정렬은 서식 문자열이다.
- 널 포인터 표기 통일: `(void*)nullptr` 도 `(null)` (예전엔 `0`). 그 테스트가 **SEGFAULT** 로 하나를 더 잡았다 —
  문자열 지름길(`string_view{ value }`)이 `nullptr` 리터럴과 널 `const utf8*` 를 그대로 받아 `strlen(nullptr)` 했다.
  `SW_LOG_ERROR( "%#", pMessage )` 에 null 을 주면 로거가 죽던 자리다. 지름길이 널을 거르고 `(null)` 을 쓴다.

**인자 수 불일치는 Debug 실행 시점 단언으로 잡는다 — 따로 세지 않고, 포맷을 훑는 자리에서.** `formatInternal` 은 인자마다
자리표를 하나씩 소비하므로 불일치는 구조적으로 드러난다: 인자가 남았는데 자리표가 없으면 마지막 분기에 도달하고(단언),
마지막 인자를 쓴 뒤 나머지에 자리표가 남아 있으면 인자가 모자란 것이다(나머지 한 번 `findNextPlaceholder` 후 단언),
인자가 0개면 자리표가 있으면 안 된다. 비용은 나머지 문자열 한 번 memchr 이고 Release/Shipping 은 아무것도 안 한다.
요구는 "`formatstring( data, capacity, "…", … )` 그대로, 함수인 채로 검사" 였다. 컴파일 시점은 C++17 에서 그 셋을 동시에
만족할 수 없다(함수 인자로 들어온 리터럴은 상수식이 아니다) — 두 매크로 형태(`SW_FORMAT_CHECKED`, `formatstring` 이름을
매크로로)를 만들어 봤고 로그 854곳이 전부 통과하는 것까지 봤지만 둘 다 **쓰지 않기로 했다**. 함수 안을 한 겹 더 감싸
안쪽에서 매크로를 쓰는 것도 안 된다 — 매크로는 텍스트 치환이라 함수 안에서는 매개변수 이름만 보인다. C++20 으로 올리면
`consteval` 포맷 타입으로 함수인 채로 컴파일 시점에 옮긴다(그때 데이터 포맷 13곳은 `runtimeFormat( str )` 로 표시).
실행 시점 검사는 그 대신 현지화처럼 데이터에서 오는 포맷까지 본다. `countPlaceholders`(constexpr)는 리터럴 규칙을
`static_assert` 로 고정하는 테스트와 C++20 전환의 정본으로 남긴다.
**첫 실행에서 진짜 버그 하나를 잡았다**: Hierarchy 의 배지 라벨 `"%# %#%###go%#"` — 자리표 넷에 인자 셋. `#` 이 플래그로
읽히던 시절엔 `%###go` 가 자리표 하나로 뭉개져 우연히 맞았고, `%#` 가 순수 자리표가 되자 어긋났다(로그 854곳의 컴파일
시점 검사엔 안 걸린 이유: 이건 매크로 밖 직접 호출이다). 단언은 브레이크 전에 **어느 포맷인지 stderr 에 찍는다** —
처음엔 브레이크만 했더니 크래시 덤프에 주소만 남아 찾을 수 없었다.

`PlaceholderNeverTakesSpecifiers` 에 널 포인터 넷·`%6d`+Fmt 케이스.

### 2026-09-11 (`formatstring` 규칙 확정 — `%#` 은 순수 자리표, 서식은 printf 형, 모르는 `%…` 는 리터럴)

`%#` 을 어떻게 쓸 생각이었는지가 결정의 근거였다: **`%#` 에 `%3d` 처럼 옵션을 붙일 생각은 처음부터 없었다.**
그런데 파서는 `#` 을 printf 의 플래그(대체 형식)로 읽어 뒤를 계속 서식으로 해석했고, 그 문법을 쓰려던 사람은
없는데 사고만 셋 냈다 — `%#x%#`(가로x세로)가 가로를 16진수로(16곳), `%#s`(초) 가 단위 `s` 를 삼킴(5곳, 아직
살아 있었다), `%#.txt` 가 `.tx` 를 잃음(로그 파일 이름). 어제의 "정밀도는 숫자가 따라올 때만" 은 증상 하나를 막은
것이었다. 이제:

- **`%#` 은 두 글자짜리 순수 자리표**다 — `findNextPlaceholder` 가 `%#` 을 보면 바로 소비하고 서식 파서에
  넣지 않는다. `#` 은 플래그 목록과 변환 문자 목록에서 뺐다. `%#dB` → `3dB`, `%#x%#` → `1280x720`,
  `%#s` → `2.5s`, `%#.txt` → `….txt`.
- **서식은 printf 형**이다: `%3d`, `%-20s`, `%08x`, `%.2f`, `%+d`. 타입은 인자가 정한다(`%d` 에 문자열을 줘도
  문자열). 정밀도도 printf 규약으로 되돌렸다(`%.f` = 0). 의도된 16진수 두 곳(`CompressionStream` 의 체크섬)은
  `%x` 로 바꿨다.
- **알아볼 수 없는 `%…` 는 리터럴이고 인자를 소비하지 않는다.** 예전엔 두 글자를 먹고 인자 하나를 삼켜
  `"100% done %#"` 의 `% d` 가 뒤 인자를 한 칸씩 밀었다. `%%` 는 그대로 퍼센트다.

`Core_String.PlaceholderNeverTakesSpecifiers`(옛 `FormatSpecifierFollowsPlaceholder` 를 뒤집었다)가 셋을 고정한다.
규칙은 `FormatString` 클래스 주석이 정본이다.

### 2026-09-11 (배포본이 씬을 연다 — 시작 씬을 걸자 쿠커의 씬 바이너리가 두 곳에서 어긋나 있었다)

"테스트 씬을 일단 Shipping 의 시작 씬으로 하고 GUID 복구를 배포본에서도 보자" 는 요청이었다. 시작 씬은
`GameConfig._startupScene`(JSON → Shipping 은 `ShippingHostDefaults.h` 로 베이크)이고, Empty 게임이 벤치가 아닐 때
`onInitialize` 에서 `requestLoadAsync` 한다. 걸자마자 배포본이 **한 번도 씬을 연 적이 없어** 숨어 있던 것 둘이 나왔다:

1. **쿠커의 씬 파일 이름이 런타임과 달랐다.** `SceneDocument::load` 는 `<name>.scene.xml` 의 짝을
   `<name>.scene.bin` 으로 찾는데 `CookScenes` 는 `.scene` 까지 벗겨 `<name>.bin` 을 만들었다 — 배포본은
   "Shipping requires cooked binary scene" 으로 끝났을 경로다.
2. **SCN1 에 `prefabGuid` 가 없었다.** C++ `saveBinary/loadBinary` 는 엔티티마다 이름·프리팹·GUID·본문 넷을
   읽는데 Python 쿠커는 셋만 썼다 — 스트림이 어긋나게 읽히고, 읽혔다 해도 배포본은 옮긴 프리팹을 GUID 로
   찾을 길이 없었다. 순서를 C++ 과 맞췄다.

테스트 씬에 **옮긴 프리팹 항목**을 넣었다: `TestProp` 은 일부러 옛 경로(`prefabs/old/`)를 가리키고 GUID 가 정본이다.
- Dev(에디터 없이): 시작 씬 로드 → `Loaded 'TestProp' from .../prefabs/testprop.prefab.xml` — .meta 스캔 표로 찾았다.
- **Shipping A/B**: GUID 가 있는 팩 → `[Error]` **0**. 같은 씬에서 `prefabGuid` 만 지우고 다시 쿠킹 → `[Error]` 2
  (`Binary read failed ... prefabs/old/testprop.prefab.bin`, `Shipping requires cooked binary`). 배포본이 팩의
  `assetregistry.txt` 로 GUID → 경로를 해석한다는 것이 이 차이다.
- `SceneTest.EditorTestSceneResolvesMovedPrefabByGuid`(nogpu) 가 Dev 쪽을 고정한다. Python SCN1 과 C++ 리더의
  형식 일치는 이 A/B 로만 확인했다 — Python 쪽 단위 테스트 기반이 없다.

부수 효과: 기본 실기동이 이제 씬을 본다(0절 기준선 14 → 15 창). 테스트 씬은 기하가 없어(메시 id 비어 있음)
스크린샷으로는 안 보인다 — 화면 검증엔 여전히 벤치 큐브를 쓴다.

### 2026-09-10 (백엔드 넷의 서술체 해석을 한 곳으로 + 로그 파일 이름이 `.txt` 를 잃던 것)

**`RHIShaderRequest`** — `RHIPipelineStateDesc` 를 셰이더 컴파일 요청으로 해석하는 규칙(진입점 기본값, define 파싱,
"RT 0 개 + 뎁스 테스트 = 픽셀 스테이지 없음", RT 수 정규화, 캐시-아니면-컴파일)이 DX11·DX12·GL·Vulkan 에 네 번
복사돼 있었다. 오늘 하루에만 그 복사본 중 Vulkan 은 define 을, DX12 는 뎁스 전용 판정을 빠뜨리고 있었다는 것이
드러났다 — 네 곳에 있는 규칙은 한 곳만 빠져도 조용히 어긋난다. 이제 `Support/RHIShaderRequest.h` 의
`resolveGraphics( desc, targetFormat )` 이 한 번 해석한 `RHIGraphicsShaderRequest`(VS/PS 요청 + 판정 사실)를
백엔드가 **받기만** 한다. 백엔드 고유의 것(입력 레이아웃·상태 객체·API 호출)은 그대로다 — 정책은 Engine,
메커니즘은 디바이스. 다음에 축이 들어오면(마스크드 그림자의 PS) 고칠 자리는 이 함수와 `hasPixelStage` 둘이다.
`RHIShaderRequestTest.ResolvesEntryPointsDefinesAndDepthOnly`(nogpu) 가 규칙을 고정한다. DX11 도 이제 뎁스 전용
파이프라인에 PS 를 붙이지 않는다(예전엔 경로만 있으면 붙였다).

**`%#.txt` 가 정밀도로 읽혔다.** 로그 파일 이름이 `LOG_2026-9-10-21_<id>t` 로 끝나고 있었다 — 포맷 파서가
`%#` 뒤의 `.` 을 정밀도(숫자 없음 = 0), `t` 를 길이 수식어, `x` 를 16진수 변환 문자로 읽어 `.tx` 를 삼켰다.
그래서 Shipping 이 `Saved/Logs` 에 Warning 이상을 **잘 쓰고 있었는데도** `*.txt` 로 찾으면 아무것도 안 나왔다
(오늘 "Shipping 은 로그를 안 남긴다" 고 오해한 이유다 — Info 가 컴파일 아웃되는 것은 의도이고 언리얼과 같다).
정밀도는 **숫자가 따라올 때만**이다(`%.f` 꼴은 저장소에 없다). `Core_String.FormatPlaceholderFollowedByExtensionIsLiteral`
로 고정했다(`%#.%#` 버전 표기와 `%.2f` 는 그대로).

### 2026-09-10 (쿠킹 산출물 스테이징 — 프리팹·씬 .bin 을 소스 트리 밖으로)

"쿠커 산출물이 Resource 에 없으면 패키징이 안 되지 않나" 가 질문이었다. 답은 **팩은 "디스크 어디에 있었나"가
아니라 "팩 안의 상대 경로"로 정해진다** 는 것이다 — 그래서 산출물을 밖에 두고 쿠커가 같은 상대 경로로 병합하면
팩은 그대로다(`assetregistry.txt` 가 이미 그 방식이었다).

- `CookPrefabs`/`CookScenes` 는 이제 `build/<preset>/Cooked/<Resource 기준 상대 폴더>/` 에 쓴다
  (`--cooked-dir`, cmake 가 `${CMAKE_BINARY_DIR}/Cooked` 를 넘긴다). `cookPack` 은 도메인마다 그 스테이징
  폴더를 소스 트리와 같은 상대 경로로 병합한다.
- 소스 트리에 남은 옛 산출물(`*.prefab.bin`, `maps|scenes/*.bin`)은 팩에 **넣지 않고** 경고로 이름을 찍는다 —
  Dev 런타임이 소스가 없을 때 그것으로 물러나 실패를 가리는 파일이므로 지우는 것이 맞다. 그래서 `.gitignore`
  의 세 줄도 걷었다: 잔재가 untracked 로 보여야 지운다.
- **빼지 않는 것**: `shaders/bin/` 은 커밋되는 산출물이고 Dev 도 2순위로 읽으므로 그대로 Resource 안이다.
- 검증은 바이트로 했다: 바꾸기 전 팩 셋과 바꾼 뒤 팩 셋(잔재가 아직 있을 때, 지운 뒤)을 `--include-debug-names`
  로 구워 `cmp` — engine/common/game_empty **셋 다 동일**. Shipping 빌드가 `build/Ninja-Shipping/Cooked/` 에 쓰고
  Resource/ 는 깨끗하며 실행 `[Error]` 0. PrefabTest·SceneTest·Engine_Resource 42/42.

### 2026-09-10 (상용 엔진과 대조해 남은 셋을 닫았다)

앞 항목이 남긴 "확인만 한 것" 셋을 언리얼·유니티가 같은 자리를 어떻게 두는지와 대조했다.

- **머티리얼 없는 배치의 Unlit 변형** — 언리얼은 모든 메시에 (기본) 머티리얼이 있어 이 축 자체가 없다.
  여기는 머티리얼 없는 메시가 패스 PSO 로 그려지므로 런타임이 만드는 (패스 define + Unlit) 을 베이커도
  굽는다(머티리얼 루프 밖에서 한 번 더). 다시 굽자 새 바이너리가 생겼다 — 이전엔 정말 빠져 있었다.
- **배포본의 에셋 GUID** — 유니티는 시작 시 GUID 표를, 언리얼은 AssetRegistry 를 통째로 안다. 여기는
  Dev 에서도 `refreshFolder` 를 아무도 안 불러 `ensureMeta` 를 거친 에셋만 알았고(이름 바꾼 프리팹 복구가
  "그 세션에서 먼저 로드됐을 때만" 동작), 배포본은 `.meta` 를 싣지 않아 빈 표였다. 이제
  `ResourceManager::initialize` 가 표를 채운다: 팩이면 쿠커가 도메인마다 넣는 `assetregistry.txt`
  (`<guid> <sourcePath>` 한 줄씩, `CookAssets.py buildAssetRegistryInternal`), 느슨한 트리면 `.meta` 재귀
  스캔(`AssetDatabase::scanMetaFiles`). `.meta` 자체는 여전히 싣지 않는다 — 배포본이 읽을 것은 표 하나면
  된다(유니티도 .meta 를 싣지 않는다). `Engine_Resource.AssetDatabaseKnowsAssetsBeforeTheyAreLoaded`
  (아무 테스트도 로드하지 않는 `readme.md` 의 GUID 를 시작 시점에 아는가)와
  `AssetRegistryTextRegistersMappings`(형식·깨진 줄) 로 고정했다.
- **Vulkan 스테이지 이름** — 컴파일에 쓴 진입점을 그대로 `pName` 에 준다. 언리얼도 진입점 문자열 하나를
  컴파일과 파이프라인에 같이 쓴다.
- **덧붙인 확장점** — `hasPixelStage` 주석에 언리얼이 마스크드 머티리얼의 그림자에만 PS 를 붙인다는 것을
  적었다. 알파 마스크가 들어오면 "머티리얼이 픽셀 폐기를 요구하는가" 축이 그 함수 한 곳에 더해진다.

**함정 하나**: `ResourceManager::initialize` 는 `GameConfig::setActive` **보다 먼저** 돈다. 그래서 처음엔
engine/common 의 3 항목만 실리고 게임 도메인 2 항목이 빠졌다 — Shipping 팩을 Dev `Bin/Packs` 에 복사해 돌려
보고 잡았다(로그가 "(팩 assetregistry.txt)" / "(.meta 스캔)" 으로 출처를 찍는다). 지금은 EngineLoop 이 설정
활성화와 팩 마운트 뒤에 `loadAssetRegistries()` 를 한 번 더 부른다.

**검증**: 느슨한 트리 "5 항목 (.meta 스캔)", 팩 "5 항목 (팩 assetregistry.txt)". Debug 전 스위트 통과
(EngineTest **433/433**, 새 케이스 2) · nogpu 5/5 · GPU 파이프라인 15/15 · 린트 OK · 경고 0. Shipping(DX12)
벤치+머티리얼 Unlit/Lit `[Error]` 0, 팩에 `assetregistry.txt` 포함(engine 249→266 파일, game_empty 5→6).
새 바이너리 32개(머티리얼 없는 Unlit 변형)와 매니페스트 넷이 같이 커밋된다.

### 2026-09-10 (1-3 의 두 항목을 닫으면서 셋을 더 찾았다 — 그중 하나는 셰이더 퍼뮤테이션 전체)

"확인만 하고 넘어간 것" 둘을 재현을 기다리지 않고 코드로 풀었다. 그 과정에서 세 결함이 더 나왔고,
마지막 것이 가장 넓었다.

1. **그림자 패스의 픽셀 스테이지 — 베이커와 런타임이 다른 규칙을 보고 있었다.** Shipping 의
   `리플렉션 매니페스트에 'shadowdepth.hlsl' 가 없습니다` 는 (shadowdepth · **PSMain** · 머티리얼
   define 해시) 조합이었다. 베이커는 타입 **문자열**(`_type != "Shadow"`)로 "그림자엔 PS 없음" 을 정해
   VS 만 굽고, 런타임 `createPsoForPassType` 은 PS 경로를 늘 채웠다. 그림자 패스에 머티리얼 define 을
   얹은 변형(`createMaterialPsoVariant` 는 패스 desc 를 그대로 물려받는다)이 DX12 에서만 PS 를
   컴파일·리플렉션했고(GL·Vulkan 은 자기 판단으로 떼고 있었다 — 백엔드마다 달랐다), 그 키가 매니페스트에
   없었다. 이제 규칙은 하나다: **컬러 출력이 없는 패스에는 픽셀 스테이지가 없다.**
   `FrameRendererUtil::collectColorOutputFormats/hasPixelStage` 를 PSO 생성과 베이커가 같이 부르고,
   `createPsoForPassType` 은 RT 0 개면 PS 경로를 비운다. DX12 백엔드도 GL·Vulkan 과 같은 `bDepthOnly`
   규칙을 갖게 됐다(NumRenderTargets 도 0 — 예전엔 1 로 올려 R8G8B8A8 을 선언하면서 DSV 만 바인딩했다).
   `parseAttachmentFormat` 은 `FrameRendererUtil` 로 옮겼다(베이커도 써야 해서).
   테스트: `ShaderBakerTest.DepthOnlyPassesHaveNoPixelStage`(nogpu, 실제 파이프라인 둘 + 합성 선언),
   `RenderPassTest.MaterialPermutationDrivesBatchPso` 에 "그림자 패스와 그 머티리얼 변형에 PS 없음" 단언.
   매니페스트 미스 메시지는 이제 **찾던 파일 이름과 define 목록**을 찍는다 — "그 셰이더는 구웠는데?" 로
   끝나지 않도록.

2. **DX12 업로드 얼로케이터가 자기 펜스를 기다리지 않았다.** `openUploadSlot` 은 "펜스 값이 바뀌었다 =
   앞 구간의 복사가 끝났다" 로 보고 Reset 했다. 그런데 `signalCurrentFrame` 은 Signal 을 **올리기만
   하고 기다리지 않는다.** 프레임 끝 Signal 직후, 링이 아직 앞 슬롯을 가리키는 동안 업로드가 오면
   GPU 가 그 복사를 도는 중에 Reset 이 나갔다 — GPU 가 붐빌 때만 보이던 이유다. 링 슬롯 대기가 가려
   줄 것이라 기대하지 않고 `D3D12RHIDevice::waitForFenceValue( slot._resetFence )` 로 그 얼로케이터의
   펜스를 직접 기다린다(보통 이미 지나 있어 비용 없음). `waitForQueueDrain` 도 같은 헬퍼를 쓴다.

3. **Vulkan `createPipelineState` 가 `_listShaderDefine` 을 아예 안 읽고 있었다.** 다른 세 백엔드의
   `fillDefines` 가 여기만 없었다. 디스크립터에는 define 이 있으니 디스크립터를 보는 테스트는 초록이었다.

4. **Shipping 실기동이 소스 트리의 `.meta` 를 덮어썼다.** PackConfig 가 `*.meta` 를 팩에서 빼므로
   `ensureMeta` 는 로드에 실패하고 → GUID 를 **새로 만들어** → `Resource/engine/materials/
   defaultmaterial.material.meta` 에 썼다. 실행마다 GUID 가 바뀌고 작업 트리가 더러워진다(이 세션의
   첫 Shipping 실행 한 번에 그렇게 됐다). 배포 빌드는 GUID 를 지어내지도 쓰지도 않는다 — 없으면 null 이고
   호출부는 전부 그것을 허용한다. `Engine_Resource.EnsureMetaNeverRewritesExistingMetaFile` 이 mtime 으로
   지킨다(Shipping 에서는 null GUID 까지).

5. **`ShaderCache` 가 베이크 바이너리를 (스템·스테이지)만으로 찾았다 — 퍼뮤테이션 해시가 빠졌다.**
   가장 넓은 결함이다. 베이커는 `forwardlit_ps_6866dd9f.spv` 처럼 해시마다 굽는데 캐시는 늘
   `forwardlit_ps.spv`(define 없음)를 읽었다. 그래서 베이크 바이너리가 소스보다 새로운 한
   `SW_FORWARD=1` · `MATERIAL_BLEND_TRANSLUCENT` · `SW_VIEWMODE_UNLIT=1` 이 **네 백엔드 어디서도 GPU 에
   닿지 않았다.** 리플렉션 매니페스트는 해시 키로 찾았으니 레이아웃은 맞고 바이트코드만 틀린, 가장 조용한
   어긋남이다. 로컬 라이브 캐시(`Saved/ShaderCache`)도 같은 이름이라 처음 컴파일된 퍼뮤테이션이 나머지를
   덮었다. 이름 규칙은 이제 `ShaderBaker::computeBinaryFileName` 하나이고 캐시·LiveShaderManager 가
   `ShaderCache::makePrebakedRelativePath/makeLocalCachePath(desc)` 로만 경로를 만든다.
   `ShaderBakerTest.CachePathCarriesPermutationHash` 가 베이커 이름과 글자 단위로 대조한다.
   **왜 지난 검증이 통과했나:** 뷰 모드 커밋 때는 방금 고친 `forwardlit.hlsl` 이 베이크보다 새로워
   런타임 컴파일(define 포함)로 갔고, 그 뒤 재베이크하자 해시 0 파일이 이기며 조용히 죽었다.

6. **커밋된 리플렉션 매니페스트가 커밋된 바이너리보다 낡아 있었다.** 5번을 고치자 Shipping 의 Unlit 이
   처음으로 **맞는** 바이트코드(`forwardlit_*_bed29a95.dxil` 등, Unlit x 머티리얼)를 집었는데 매니페스트
   (74 항목이어야 할 것이 그 키들 없이 커밋돼 있었다)에는 리플렉션이 없어, 바인더가 빈 레이아웃으로
   드로우를 내고 DX12 가 **DEVICE_HUNG** 으로 죽었다 — `tryGet` 주석이 경고하던 바로 그 경로다. 5번
   이전에는 해시 0 바이트코드(리플렉션 있음)를 읽었으니 우연히 안 죽었을 뿐이다. `App.exe --bake-shaders`
   로 다시 굽자(바이너리 0개 갱신, 매니페스트만 74 항목으로) 사라졌다. **Shipping 쿠킹도 이 상태를 못
   잡았다** — `bake.stamp` 는 소스 해시만 보므로 레시피 축이 늘어 바이너리는 있는데 매니페스트가 낡은
   경우는 통과시켰다. `CookAssets.py --verify-shaders` 가 이제 **매니페스트에 없는 바이너리**도 문제로
   보고 재베이크를 걸며, 그래도 남으면 낡은 파일이라고 이름을 찍고 세운다(HEAD 의 옛 매니페스트로 되돌려
   실제로 잡히는 것을 확인했다).

**검증은 분포로 했다.** 벤치 큐브는 벽시계로 돌아 픽셀 단위 비교는 잡음 바닥이 3~4% 라 판정이 안 된다
(에디터 씬 덤프는 SceneColor 가 비어 있었다). 대신 모서리 색을 배경으로 빼고 전경 평균 휘도를 비교했다.
Lit ↔ Unlit 의 전경 평균 휘도 차: **고치기 전** DX12 −0.09 · Vulkan −0.34(같은 모드 두 판의 잡음
바닥 −0.04 / −0.64 안), **고친 뒤** DX12 **+60.8** · Vulkan **+60.1** · DX11 **+59.9** · GL **+60.2**
(잡음 바닥 전부 ±0.8 안). 수단은 `-gv_screenshot` PPM 을 모서리 색 기준으로 전경만 남겨 평균 휘도를 비교하는
스크립트다 — 뷰 모드·머티리얼 퍼뮤테이션처럼 "define 이 닿았는가" 를 볼 때 이 지표를 쓸 것.

**검증**: Debug CoreTest 169/169 · ReflectionTest 100/101(스킵 1) · EditorTest 51/51 · SmokeTest 18/19(스킵 1) ·
EngineTest **431/431**(새 케이스 3) · nogpu 5/5 · 린트 전부 OK · 컨벤션 0건 · Debug·Shipping 경고 0.
Shipping(DX12): 벤치+머티리얼 Lit **`[Error]` 0**(1-3 의 그 오류가 사라졌다), 벤치 Unlit `[Error]` 0(고치는
도중엔 6번의 DEVICE_HUNG 이 났었다), 소스 트리 `.meta` 변경 없음. DX12 업로드 레이스 스트레스: 에디터 +
테스트 씬으로 네 백엔드 × 뷰 모드 둘 연속 8회 + DX12 3000 큐브·머티리얼 인스턴스·에디터 5회 — 종료 코드 0,
`[Error]` 0, "is being reset"/DEVICE_HUNG 0. (이 PC 의 Shipping 프리셋은 테스트 실행 파일을 만들지 않아
Shipping 테스트 수는 재지 않았다.) 리플렉션 매니페스트 넷이 이 커밋에 같이 갱신된다(항목 74).

### 2026-09-10 (뷰 모드를 렌더러에 연결하고, 그 과정에서 결함 둘을 찾았다 — 예전 1-1)

뷰포트 툴바의 Lit/Unlit/Wireframe 콤보가 **아무 일도 하지 않아** 비활성으로 막아 두었던 항목이다.
"연결하려면 파이프라인 리소스에 채우기 모드를 노출하고 와이어프레임 PSO 변형을 만들고 프레임
렌더러가 모드를 골라야 한다" 고 적어 두었는데, **셋 다 필요 없었다.** 이미 있는 것을 잘못 읽고
있었다 — `RHIPipelineStateDesc` 에 `_fillMode` 와 `_listShaderDefine` 이 있고, 배치마다 PSO 를
고르는 캐시(`_mapMaterialPso`)도 있다. 없던 것은 **그 캐시의 키에 뷰 모드라는 축**뿐이었다.

- `materialPsoKey( 패스 PSO, 퍼뮤테이션 해시, 뷰 모드 )` — 모드가 키의 한 축이 되어, 모드를
  되돌리면 이미 만든 PSO 가 캐시에서 다시 나온다(다시 컴파일하지 않는다).
- 뷰 모드는 머티리얼 퍼뮤테이션 **뒤에** 얹는다. 순서가 반대면 머티리얼이 셰이더 경로를 갈아탈 때
  방금 넣은 define 이 다른 셰이더로 넘어가 의미가 달라진다.
- 머티리얼이 **없는** 배치에도 변형이 필요하다(퍼뮤테이션 해시 0). 이걸 빼면 머티리얼 없는 메시만
  솔리드로 남아 화면이 섞인다.
- 그림자·뎁스 프리패스는 **제외한다**(`FrameRendererUtil::appliesViewMode`). 와이어프레임으로
  그림자를 구우면 그림자가 선 몇 개로 남고, 뎁스 프리패스를 와이어프레임으로 채우면 이후 패스의
  뎁스 테스트가 삼각형 내부를 전부 버려 화면이 빈다.
- Unlit 은 `SW_VIEWMODE_UNLIT=1` 퍼뮤테이션이다. `forwardlit.hlsl` 이 조명·그림자·림 계산을 통째로
  컴파일 아웃한다 — 런타임 분기가 아니라서 그림자 맵 샘플까지 빠진다. 정본 문자열은
  `FrameRendererUtil.h` 의 `kViewModeUnlitDefine` 하나다(셰이더와 어긋나면 조용히 안 바뀐다).
- 툴바는 값을 들고 있지 않고 **매 프레임 렌더러에서 읽어** 표시한다 — 커맨드라인이나 다른 경로가
  모드를 바꿔도 콤보가 거짓을 보이지 않는다.

**연결하고 나서야 드러난 결함 둘.** 둘 다 뷰 모드보다 훨씬 넓은 문제였다.

1. **GT→RT 스냅샷이 퍼뮤테이션 표를 안 보내고 있었다.** 배치의 `_shaderPermutation` 은
   `GpuScene::getShaderPermutations()` 의 **인덱스**인데 `exportCpuSnapshot`/`adoptCpuSnapshot` 이
   그 표를 옮기지 않았다. 받는 쪽에서 `findShaderPermutation` 이 **언제나 nullptr** 이었고, 그래서
   패킷 경로(= 실제 앱과 에디터가 쓰는 경로)에서는 **머티리얼 퍼뮤테이션이 하나도 걸리지 않았다** —
   유리 머티리얼의 `MATERIAL_BLEND_TRANSLUCENT` 가 화면에 닿은 적이 없다.
   `MaterialPermutationDrivesBatchPso` 가 통과하고 있었던 이유는 그 테스트가 동기 `execute()`
   경로만 태우기 때문이다. **두 경로를 가르는 테스트가 없으면 한쪽만 죽어도 초록이다** —
   `GpuSceneTest.CpuSnapshotCarriesShaderPermutations` 가 그 자리를 메운다(GPU 불필요).
   보낼 때는 복사, 받을 때는 이동이다 — 표는 GT 가 계속 늘려 가는 정본이라 빼앗아 오면 다음
   프레임의 인덱스가 0 부터 다시 매겨진다.
2. **Vulkan 은 `fillModeNonSolid` 를 켠 적이 없었다.** 백로그에 "RHI 는 네 백엔드 모두 준비되어
   있다" 고 적어 두었지만 절반만 맞았다 — Vulkan 은 `desc._fillMode` 를 **읽기는** 하는데, 그 기능을
   디바이스 생성 때 켜지 않으면 `VK_POLYGON_MODE_LINE` 파이프라인이 검증에서 거절된다. 켜고,
   못 켜는 디바이스에서는 Solid 로 물러난다(화면이 비는 것보다 다르게 보이는 편이 낫다).

**검증은 픽셀로 했다.** PSO 디스크립터만 보면 "PSO 는 제대로 만들었는데 화면은 그대로" 를 못 잡는다
— 실제로 처음 통과한 단위 테스트가 그 상태였고, 스크린샷을 보고서야 1번 결함을 찾았다.
네 백엔드 모두 배경과 다른 픽셀이 약 68,000 → 9,800~10,300 으로 줄었다(6.6~6.9배).
Unlit 은 같은 모드 두 판의 잡음 바닥(0.36% · 0.054) 대비 4.33% · 0.224 로 갈린다.
`RenderPassTest.ViewModeSelectsDistinctPipelineStates` 가 네 백엔드에서 PSO 쪽을 함께 지킨다
(모드마다 다른 PSO, 와이어프레임의 fill/cull, Unlit 의 define, 그림자 패스 제외, 되돌리면 캐시 재사용).

**검증**: Debug 428/428 · Shipping 426/428(스킵 2는 Dev 전용) · nogpu 5/5 · 린트 6/6 · 컨벤션 0건 ·
Debug·Shipping 경고 0 · 네 백엔드 × 뷰 모드 둘로 에디터 실기동(테스트 씬) 종료 코드 0 · `[Error]` 0건.

### 2026-09-10 (드물게 SEGFAULT 하던 RHITest 의 원인을 찾았다 — 예전 1-5)

`RHITest.CommandListCreationAndExecution` 이 Shipping 에서 **한 번** SEGFAULT 하고 그 뒤 8회
재현되지 않아 "다시 보이면 그때 본다" 로 남겨 두었던 항목이다. 백로그 자신의 조언대로
"그 테스트가 실제로 무엇을 태우는가" 를 먼저 읽어서 원인을 찾았다 — 재현을 기다릴 필요가 없었다.

**테스트가 디바이스를 커맨드 리스트보다 먼저 파괴한다.** `shutdownDeviceWithWindow` 가
`device->shutdown(); device.reset();` 을 하는데 그 시점에 `cmdList` 는 아직 살아 있고, 스코프를
벗어날 때 소멸자가 죽은 디바이스를 만진다. 두 백엔드의 소멸자가 모두 `_pDevice` 를 역참조한다 —
DX11 은 `unregisterCommandList`, DX12 는 `releaseOnlineBlocksDeferred` + `recycleCommandListEntryDeferred`.

**그런데 DX11 만 보호가 있었다.** `D3D11RHIDevice::shutdown` 은 살아 있는 커맨드 리스트를 모두
`detachFromDevice()` 하고, 코드에 이유까지 적혀 있다("커맨드 리스트는 디바이스보다 오래 살 수
있다"). **DX12 에는 그 등록부가 아예 없었다.** 테스트가 DX11 → DX12 → GL 순으로 시도하므로
보통은 DX11 이 잡혀 가려졌고, DX11 이 실패하는 환경에서만 터졌다 — 재현율이 낮았던 이유다.

두 곳을 고쳤다:

1. **DX12 에 라이브 커맨드 리스트 등록부를 넣었다**(DX11 과 같은 형태) — 생성 시 등록, 소멸 시
   해제, `shutdownInternal` 이 남은 것을 `detachFromDevice()` 한다. detach 는 디바이스를 다시
   부르지 않고 자기 것만 놓는다. 이 결함은 테스트만의 문제가 아니었다 — **커맨드 리스트를 든 채
   디바이스를 내리는 모든 코드**가 같은 함정이었다.
2. **테스트가 올바른 순서를 보이게 했다** — `cmdList.reset()` 을 디바이스 종료 앞에 둔다.
   디바이스에 보호를 넣었더라도 테스트가 잘못된 순서를 가르치면 안 된다.

**검증**: 그 케이스만 10회 반복 — 실패 0. `RHITest.*` 13/13. Debug·Shipping 경고 0,
nogpu 5/5(양쪽), 린트 6/6.

### 2026-09-10 (컴포넌트 풀을 TypeInfo 포인터로 키잡던 것 — 예전 1-B 크래시의 원인)

테스트 씬을 로드하면 **종료할 때 반드시 죽었다**(`PoolAllocator::free` 의 "Pointer does not
belong to any allocated chunk" 단정, 종료 코드 0x80000003). 이제 고쳤다.

**추적 과정** (같은 방법이 다음에도 쓸 만하다):

1. 엔티티를 하나만 남긴 씬 7개로 이분했다 → **CameraComponent 를 담은 씬만** 죽었다
   (메시·스프라이트·콜라이더·라이트·부모자식 씬은 모두 종료 코드 0).
2. 생성·해제를 짝지어 로그했다 → 카메라 3개가 **같은 매니저**에서 풀 할당되고, 죽는 것은
   **첫 번째**였다. 그 포인터만 나머지 둘과 1.7MB 떨어져 있었다(= 다른 청크).
3. `clear()` 는 종료 시 한 번뿐이고 그 전에 풀을 비운 적도 없었다 → "청크가 미리 해제됐다"는
   가설은 탈락.
4. `TypeInfo` **포인터**를 찍어 확정: 카메라 #1 은 `typeInfo=…657064` 로 만들어졌는데 해제할 때는
   `typeInfo=…351976` 였다. **같은 클래스에 TypeInfo 인스턴스가 둘** 있었다.

**원인**: `_mapComponentPool` 이 `const TypeInfo*` 를 키로 썼다. 모듈(SWGame·EditorModule)이
로드되며 리플렉션을 다시 등록하고 `rebindAllCachedTypeInfo()` 가 기존 컴포넌트의 캐시된 TypeInfo 를
새 인스턴스로 갈아끼운다. 그래서 **모듈 로드 전에 만든 컴포넌트**는 생성 때의 풀(옛 TypeInfo 키)과
해제 때 찾는 풀(새 TypeInfo 키)이 달라지고, 엉뚱한 풀에 블록을 반납한다.

**왜 카메라만이었나**: 카메라는 모듈 로드 **전**(엔진의 기본 카메라)과 **후**(에디터 카메라·씬의
카메라) 양쪽에서 만들어지는 유일한 컴포넌트라, 한 타입에 풀이 둘 생겼다. 다른 타입은 로드 후에만
만들어져 풀이 하나뿐이었고, 풀이 없으면 `sw_delete` 로 빠져 우연히 짝이 맞았다.

**수정**: 키를 `TypeInfo::_fullyQualifiedName`(재등록에도 변하지 않는다)으로 바꿨다. 근거를
`getOrCreateComponentPool` 주석에 남겼다.

**영향**: 이 결함은 씬 로드에 국한되지 않는다 — **모듈 핫 리로드 후 생성 이전 컴포넌트를 파괴하는
모든 경로**가 같은 함정이었다. 지금까지 드러나지 않은 이유는 저장소에 씬 애셋이 없어서 로드된
컴포넌트가 존재한 적이 없었기 때문이다.

**단위 테스트는 붙이지 못했다** — 재현에 같은 클래스의 두 번째 `TypeInfo` 등록(=모듈 로드)이
필요하고 그것은 테스트 프로세스에서 만들 수 없다. 대신 실기동 재현이 게이트다: 테스트 씬을 열고
종료 코드 0 이어야 한다(위 0절 명령).

**검증**: Debug·Shipping 경고 0, nogpu 5/5(양쪽), 린트 6/6, 컨벤션 0건.
네 백엔드 × 두 경로 모두 종료 코드 0 · `[Error]` 0건 — 기본 vtx=742, 테스트 씬 vtx=1766.

### 2026-09-10 (직렬화기가 상속 PROPERTY 를 빠뜨리던 것을 고쳤다 — 예전 1-A)

`TypeInfo::forEachProperty( func, bIncludeBase = false )` 의 **기본값** 때문에 `XmlSerializer`·
`JsonSerializer`·`ObjectDiffSerializer`·`SchemaMigrate` 가 상속된 PROPERTY 를 저장도 로드도 하지
않았다. 컴포넌트에서는 그것이 곧 `SceneComponent` 의 트랜스폼이라, 씬·프리팹·Undo 스냅샷에서
메시·스프라이트·카메라의 **위치가 사라졌다**.

**의도가 어느 쪽인지는 코드가 말해 준다**: `BinarySerializer` 는 처음부터
`getPropertiesWithBase()` 를 쓴다. 즉 쿠킹된 바이너리에는 트랜스폼이 있고 XML/JSON 원본에는
없는 상태였다. 여덟 자리(Xml 3 · Json 2 · ObjectDiff 1 · SchemaMigrate 2)에 `true` 를 넘긴다.

**`ComponentDefaults` 는 일부러 그대로 뒀다.** 거기는 상속 체인을 **호출부가 직접** 돌면서
레벨마다 자기 XML 노드(`<SceneComponent>`, `<MeshComponent>` …)를 찾아 자기 프로퍼티만 적용한다.
`true` 를 주면 같은 기반 값을 여러 노드에서 중복 적용한다. 판단 근거를 `forEachProperty` 선언부
주석에 남겼다 — 다음 사람이 여기서 같은 고민을 반복하지 않도록.

중복 방출 걱정은 없다: `getPropertiesWithBase()` 가 기반 먼저 넣고 파생이 같은 이름을 **덮어쓰는**
순서로 평탄화하며 결과를 캐시한다.

**검증**: `TestObjectStateRoundTrip.DerivedComponentInheritedTransformSurvivesXml` 의 `SW_TEST_SKIP`
을 걷었고 통과한다(고치기 전에는 위치·스케일 6개 단정이 모두 실패했다). Debug·Shipping 경고 0,
nogpu 5/5 **양쪽 모두 회귀 없음**, 린트 6/6, 컨벤션 0건, 기본 게이트 네 백엔드 vtx=742 동일.
테스트 씬 애셋을 다시 생성해 `<MeshComponent _localPosition=… _localScale=…>` 처럼 트랜스폼이
실제로 들어간 것을 확인했다(프리팹도 같다).

**곁들여 알게 된 것**: `SpriteComponent` 는 `SceneComponent` 가 아니라 **`MeshComponent` 를**
상속한다(이제 `_meshId`·`_boundsRadius` 가 함께 직렬화된다).

**1-B(종료 시 풀 해제 단정)는 이 수정과 무관했다** — 고친 뒤에도 그대로 재현된다. 별개 버그다.

### 2026-09-10 (프리팹 저장이 엉뚱한 폴더에 쓰고 있었다 — 저장 경로 해석 통일)

테스트 씬 애셋을 만들려고 프리팹을 저장하다 드러났다. 두 결함이 겹쳐 있었다:

1. **상위 폴더를 만들지 않았다.** `PrefabAsset::saveToXmlFile`/`saveToJsonFile`/`saveToBinaryFile`
   은 폴더가 없으면 쓰기가 실패하는데 **로그도 남기지 않고** false 만 돌려줬다(로더는 실패를
   모두 로그한다). `SceneDocument::saveXml` 은 `createParentDirectory` 를 부른다. 맞췄고,
   세 세이버 모두 실패 시 오류를 남긴다.
2. **경로 해석이 달랐다.** `PrefabAsset` 은 `ResourceUtil::getResourcePath` 만 쓰고 실패 시
   **상대 경로를 그대로** 넘겼다 — `getResourcePath` 는 *이미 있는* 파일만 찾으므로 새 파일에는
   늘 empty 다. 그래서 애셋이 `Resource/` 가 아니라 **실행 파일의 작업 디렉터리**에 떨어졌다.
   실제로 확인했다: 프리팹은 `build/Ninja-Debug/Bin/game/empty/prefabs/` 에, 그 `.meta` 는
   `Resource/game/empty/prefabs/` 에 — **애셋과 메타가 다른 폴더로 갈렸다.**

해석 순서를 `ResourceUtil::getWritePath` 하나로 모았다(절대 → 기존 리소스 → 도메인 루트 조합 →
인자 그대로). `SceneDocument` 의 사설 헬퍼 `absoluteWritePath` 를 지우고 양쪽이 같이 쓴다 —
앞으로 세이버를 추가할 때 올바른 동작이 기본값이 된다.

**검증**: Debug·Shipping 경고 0, nogpu 5/5, 린트 6/6. 프리팹이 `Resource/game/empty/prefabs/`
에 저장되는 것을 실기동으로 확인했다(고치기 전에는 로그가 상대 경로를 찍었다).

### 2026-09-10 (씬을 바꿔도 이전 씬을 가리키는 상태가 남아 있었다)

"선언만 있고 아무도 부르지 않는 함수" 를 훑었다(Editor 헤더 선언 528개 → 트리 전체 등장이
2회 이하인 것 **27개**). 대부분은 그냥 죽은 편의 API 였지만, `EditorWorkspace` 의 세 개는
**불려야 하는데 안 불리고 있었다** — 이름 그대로 씬이 바뀔 때 버려야 하는 상태다.

`EditorAssetCommands::syncAfterSceneGenerationChange` 는 씬 세대가 바뀐 것을 아는 **유일한**
자리다(`setObservedSceneGeneration` 호출부가 여기뿐이다). 여기서 dirty·선택을 지우고 프리팹
맵을 다시 만들지만, 다음 셋은 그대로 남겼다. 오브젝트 ID 는 `GameObjectManager` 마다 다시
시작하므로 남은 상태는 **새 씬의 엉뚱한 오브젝트에 붙는다**:

- **Undo 스택** — 커맨드가 든 XML 스냅샷은 사라진 씬의 것이다. Edit 메뉴는 Undo 를 켜 둔 채로
  두고, 누르면 아무 일도 없거나(guid 조회 실패) 재사용된 ID 를 통해 다른 오브젝트를 덮어쓴다.
  (`EditorPlaySession` 은 플레이 전이마다 비운다 — 씬 전환만 빠져 있었다.)
- **GUID 맵**(`clearGuidMap`) — Undo·PIE 복원이 오브젝트를 다시 찾는 신분증이다
  (`findGameObjectByGuid`). 낡은 `guid → 옛 ID` 가 남으면 새 씬에서 같은 ID 를 쓰는 오브젝트가
  잡히고, `getOrAssignGuid` 는 새 오브젝트에 **옛 guid** 를 돌려준다. 씬을 갈아탈수록 늘기도 한다.
- **프리팹 Isolation**(`clearPrefabIsolation`) — 프레임이 옛 씬의 오브젝트 ID 를 든다. 격리 중에
  씬을 열면(막는 가드가 없다) `isPrefabIsolationActive()` 가 계속 true 라 UI 는 격리 중이라 믿고,
  `exitPrefabIsolation` 이 새 씬의 무관한 오브젝트를 되살린다.

세 clear 함수는 모두 순수 상태 리셋이라(오브젝트를 만지지 않는다) 사라진 씬에 대해 부르는 것이
정확히 맞다. 프리팹 맵만 버리지 않고 다시 만든다(새 씬에도 인스턴스가 있다).

**검증의 한계 — 중요**: 이 훅은 **빈 씬 실기동에서 한 번도 실행되지 않는다.** 활성 게임이
`Empty` 라 씬이 로드되지 않아 세대가 0에 머문다(임시 로그로 확인: 60프레임 동안 훅 미발동).
그래서 이 수정의 근거는 코드 수준이다 — 세 함수의 본문, 훅이 유일한 세대 감지 지점인 것,
GUID 맵/Isolation 프레임이 ID 로 옛 씬을 가리키는 것을 읽어서 확인했다. 실기동은 "빈 씬
경로를 깨지 않는다" 까지만 보증한다. 0절에 이 게이트의 사각지대를 적어 두었다.

**측정해 둔 죽은 API 27개** (지우는 것은 판단이 필요해 두었다):
`EditorThemeUtil` 9개(`getBorderColor`·`getPanelBgColor`·`getWindowBgColor`·`push/popAccentButton`·
`push/popAccentHeader`·`textAccent`·`textMuted`) · `EditorWidgets` 4개(`acceptAssetDrop`·
`drawHelpMarker`·`drawPropertyRowBegin/End` 짝 전체·`drawSearchFilter` — 이건 쓰이는
`drawSearchField` 의 죽은 사촌이라 잘못 부르기 쉽다) · `AssetEditorManager` 2개(등록 확장점) ·
`EditorWorkspace` 의 `removeGuid`·`isGameObjectPrefabInstance`·`clearGameObjectPrefabMap`
(마지막은 rebuild 가 대신하므로 정상) · `EditorTransaction::captureBinarySnapshot` ·
`EditorInspectorCommands::pushStringEdit` · `EditorDataTableCommands::hasModifiedLocalization` ·
`EditorGlobalVariableCommands` 2개 · `EditorAssetTypeRegistry::getPanelMappings`.

**검증**: Debug·Shipping 경고 0, nogpu 5/5(양쪽), 린트 6/6, 네 백엔드 실기동 종료 코드 0 ·
`[Error]` 0건 · `Game View` 정점 수 742 동일, 전부 열기 창 29개/빈 패널 0개.

### 2026-09-10 (핫 리로드·종료 때 언맵된 DLL 로 뛰는 Undo 스택, 그리고 조용한 실패 셋)

**Undo 스택이 에디터 모듈보다 오래 산다.** `CommandStack` 은 `EngineLoop` 이 소유하고
(`_commandStack`), 거기에 쌓는 것은 **전부 에디터**다 — `EditorTransaction` 이 넣는 커맨드는
패널의 `this` 를 잡은 **람다** 델리게이트이고, 그 람다의 코드와 소멸자
(`Delegate::_managerFunc`)는 `EditorModule.dll` 안에 있다. 그런데 `ImGuiEditor::shutdown()` 이
이 스택을 비우지 않았다. 그래서 두 경로가 언맵된 이미지로 뛴다:

- **핫 리로드**: EditorModule 을 다시 컴파일하면(Build 메뉴에 있다) DLL 이 언맵·재맵되는데
  스택에는 옛 이미지의 델리게이트가 남는다 → 그 뒤 `Ctrl+Z` 가 사라진 코드를 부른다.
- **종료**: `App::shutdown()` 이 `_moduleHost->shutdown()`(모듈 내림) → `_engineLoop.shutdown()`
  (`_commandStack.reset()`) 순서다. 즉 델리게이트 **소멸자**가 언맵된 DLL 로 점프한다
  (`Delegate::release()` 가 `_managerFunc` 를 부른다 — 람다 델리게이트만 이 필드가 채워진다).

넣은 쪽이 치우게 했다 — `ImGuiEditor::shutdown()` 이 모듈이 내려가기 전에 스택을 비운다.
`EditorPlaySession::setState` 가 전이마다 같은 이유로 비우고 있어 선례도 있고, 에디터 밖에서
이 스택에 push 하는 코드는 **없음을 확인**했다(`Source/Editor` 밖에는 소유·바인딩만 있다).
**트레이드오프**: 에디터 핫 리로드 후 Undo 히스토리가 사라진다. 유지하려면 커맨드가
직렬화 가능해야 하고(모듈에 묶인 람다가 아니라), 그건 별개의 큰 작업이다. 지금 선택지는
"비우기" 와 "언맵된 코드로 뛰기" 뿐이다.

**실증에 대한 한계**: 헤드리스 실행에서는 편집이 없어 스택이 **0개**다(임시 로그로 확인).
즉 이 결함은 사용자가 실제로 편집한 뒤 리로드하거나 종료할 때만 드러난다 — 그래서 지금까지
안 잡혔다. 크래시 재현은 만들지 않았고, 근거는 코드 수준이다(언로드 순서 + `release()` 가
모듈 함수 포인터를 부른다는 점).

**그리고 조용히 실패하던 자리 셋에 피드백을 붙였다.** 백로그가 예전에 같은 종류
("에디터의 붙여넣기는 실패를 아무도 읽지 않고 있었다")를 고친 적이 있는데 남아 있었다:

- `EditorAssetCommands::loadScene` — `requestLoadAsync` 실패 시 **로그도 알림도 없이** false 만
  돌려줬고 호출부도 그 값을 읽지 않는다. 사용자가 씬을 골랐는데 아무 일도 일어나지 않는다.
- `EditorTransformCommands::loadComponentPreset` — 파일이 없거나 XML 이 그 컴포넌트 타입과
  맞지 않으면 실패하는데 반환값을 버렸다. "프리셋을 골랐는데 아무 일도 없다".
- `EditorTransformCommands::saveComponentPreset` — 같은 형태(쓰기 실패).

**이번 라운드에 훑고 결함이 없다고 확인한 것** (같은 곳을 다시 파지 않도록):

- `formatstring` 의 `%s`·`%d` 혼용 — 정상이다. 이 포매터는 `%s`/`%d`/`%f` 등을 모두
  "다음 인자" 자리표로 받는다(`%#` 이 기본형일 뿐).
- **멤버 인덱스 경계** — `_selectedFrame`·`_selectedKey`·`_selected`·`_selectedGameDataIndex`·
  `_filterIndex`·`_historyIndex`·팔레트 `_selectedIndex` 전부 접근 직전에 범위를 검사한다.
- **백그라운드 잡 수명** — `EditorBackgroundJob` 이 상태를 `shared_ptr` 로 워커에 넘기고 세대
  번호로 낡은 결과를 버린다. 패널이 먼저 사라져도 안전하다.
- **엔진에 남는 다른 콜백** — Logger 구독은 `ConsolePanel` 이 소멸자·shutdown 양쪽에서 해제하고,
  창 닫기 핸들러는 `ImGuiEditor::shutdown` 이 비운다. 에디터는 엔진 파일 감시자에 등록하지 않는다.
- `EditorConfig` 필드 16개 — 읽히지 않는 것 없음.

**검증**: Debug·Shipping 경고 0, nogpu 5/5(양쪽), 린트 6/6, 네 백엔드 실기동 종료 코드 0 ·
`[Error]` 0건 · `Game View` 정점 수 742 동일, 전부 열기 창 29개/빈 패널 0개.

### 2026-09-10 (구조를 정리하다 드러난 결함 다섯 개)

리팩터 뒤에 도구를 다시 돌리고(clang-tidy 85 TU **0건**, ASan nogpu **5/5**) "쓰기만 하고 읽지
않는 필드" 를 기계로 훑었다(멤버 442개 중 후보 4개). 나온 것을 하나씩 확인해 고쳤다.

**1. 프레임마다 씬 전체를 힙에 복사하고 있었다.** `GameObjectManager::getAllGameObjects()` 의
값 반환 오버로드는 헤더가 "매 프레임 도는 곳에는 쓰지 말라" 고 못박아 둔 것인데, 에디터가
프레임마다 네 번 그렇게 부르고 있었다 — 통계 오버레이가 **개수만 알려고** 한 번,
디버그 시각화가 한 번(내가 표로 쪼개며 두 번으로 늘렸다), `EditorCamera::find` 가 뷰포트
update/draw 에서 두 번. 지금은 뷰포트가 프레임당 스냅샷 **하나**를 재사용 버퍼에 만들어
시각화와 통계가 함께 보고, `EditorCamera::find` 는 복사조차 하지 않는 `forEachGameObject` 를
쓴다. `HierarchyPanel` 은 스냅샷이 **필요하다**(트리를 그리는 도중 오브젝트가 지워질 수 있고,
매니저를 잠근 채 그리면 같은 스레드가 배타 락을 다시 잡아 교착한다) — 그래서 순회로 바꾸지 않고
재사용 버퍼 + out 파라미터 오버로드로 옮겼다.

**2. DX12 만 채우기 모드를 무시했다.** `D3D12RHIResourcePipeline` 이 바로 다음 줄에서
`desc._cullMode` 는 읽으면서 `FillMode` 는 `D3D12_FILL_MODE_SOLID` 로 못박혀 있었다 —
`RHIFillMode::Wireframe` 을 요청한 파이프라인이 DX12 에서만 조용히 솔리드로 그려진다.
DX11·Vulkan·GL 과 같은 형태로 고쳤다. 지금 Wireframe 을 요청하는 파이프라인이 없어 동작 변화는
없고, 백엔드 간 어긋남만 없어진다.

**3. 뷰포트 뷰 모드 콤보가 아무 일도 하지 않았다.** 위 1-0 항목으로 옮겼다.

**4. `editordata.xml` 의 `_clearColor` 가 무시되고 있었다.** Game View 렌더 타깃의 클리어 색은
`EditorContextLifecycle` 에 **같은 값이 손으로 박혀** 있었다(`0.12,0.15,0.18,1` — XML 과 정확히
일치했다. 즉 XML 이 이걸 몰게 하려던 것이었다). XML 을 고쳐도 아무 일도 없었다. 이제 XML 값을
쓴다 — `_clearColor` 를 `0.9,0.1,0.7,1` 로 두고 실기동해 그 값이 렌더 타깃까지 가는 것을 임시
로그로 확인한 뒤 되돌렸다.

**5. 죽은 시드 필드 둘을 걷어냈다.** `EditorData::_defaultMaterial` 은 `EngineData::_defaultMaterial`
과 기본값까지 똑같은 복사본이고 읽는 곳이 없었다(엔진 쪽이 정본이다). `_playerSpeed` 는 Source
전체에서 **선언 한 줄만** 있는, 게임 템플릿에서 흘러온 잔재였다 — 에디터 도구 시드에 있을 값도
아니다. 구조체와 XML 양쪽에서 지웠다. (`_fontSize`·`_clearColor` 는 읽는 곳이 있어 남겼다.)

**검증**: Debug·Shipping 경고 0, nogpu 5/5(양쪽), ASan 5/5, 린트 6/6, 컨벤션 0건,
clang-tidy(Editor 85 TU) 0건, 네 백엔드 실기동 종료 코드 0 · `[Error]` 0건.
그리기가 그대로인지는 네 백엔드 모두 `Game View` 정점 수 **742** 로 같은 것을 확인했다.

**교훈으로 남길 도구**: "쓰기만 하고 읽지 않는 멤버" 훑기는 값이 있었다 — 뷰 모드 콤보와 죽은
시드 필드가 이 방법으로 나왔다. 정확한 판정은 못 하지만(대입만 쓰기로 세는 어림) 후보를 4개로
좁혀 주므로 사람이 확인할 만하다.

### 2026-09-10 (뷰포트 피킹·시각화의 컴포넌트 종류 나열 제거 — 예전 1-0)

**게임이 만든 컴포넌트는 뷰포트에서 클릭으로 집히지 않았다.** `EditorViewportClient.cpp` 안에
`considerMeshPick`·`considerSpritePick`·`considerBoxPick`·`considerScenePick` 네 함수가 엔진 타입을
손으로 나열했고, 마지막 폴백은 `getPrimarySceneComponent()` **하나만** 봤다 — 게임 컴포넌트가 주
컴포넌트가 아니면 후보에 아예 들어가지 못했다.

피킹을 `Common/Commands/EditorViewportPick` 으로 옮겼다(ImGui 없음 → **테스트가 붙는다**).
종류를 아는 제공자는 표의 한 줄이고(`_order3D`/`_order2D` 가 동거리 우선순위를 정한다 — 2D 에서
스프라이트가 메시보다 앞서는 규약을 그대로 옮겼다), 전용 제공자가 못 잡은 오브젝트는 그
오브젝트의 **모든** `SceneComponent` 를 기본 반지름 0.35 로 훑는다. RTTI 가 꺼져 있어
`dynamic_cast` 를 쓸 수 없으므로 리플렉션 `castTo` 로 판별한다.

`TestEditorViewportPick.cpp` 7케이스 — 레이-구 교차(맞음·빗나감·뒤쪽·원점이 안에 있는 경우) ·
제공자 표가 비지 않았는지 · 전용 종류 없는 컴포넌트가 집히는지 · **주 컴포넌트가 아닌
SceneComponent 가 집히는지** · 가까운 오브젝트 우선 · 비활성 제외 · 빈 씬/널 안전.
폴백을 옛 코드(주 컴포넌트만)로 되돌리면 `NonPrimarySceneComponentIsPickable` 이 실패하는 것까지
확인했다. EditorTest 44 → **51**.

**디버그 시각화도 같은 문제였다.** `drawDebugVisualizers` 가 BoxCollider2D 와 CameraComponent 를
손으로 나열하고, 각자 `ViewportToolbarSettings` 의 bool 하나 + 툴바 체크박스 하나에 짝지어 있었다 —
시각화를 하나 더하려면 세 파일 네 곳을 고쳐야 했다. `Viewport/EditorViewportVisualizer` 의 표로
모았고(라벨·툴팁·기본값·그리기 함수가 한 줄), **툴바 체크박스가 그 표에서 만들어진다.** 설정은
bool 두 개 대신 `_visualizerMask` 하나이고 기본값은 표가 정한다(뷰포트 설정은 저장되지 않으므로
표현을 바꿔도 마이그레이션이 없다).

공유 투영 헬퍼는 `Viewport/EditorViewportProjection` 으로 뺐다. 헤더는 `struct ImVec2;` 전방
선언만 두어 ImGui 를 include 하지 않는다(`EditorViewportClient.h` 의 `ImDrawList` 와 같은 방식) —
그래서 호출부를 고칠 필요가 없었다.

`EditorViewportClient.cpp` 는 1278줄 → **1026줄**(62KB → 48KB)로 줄었고, 남은 것은 카메라 조작·
기즈모·그리드·자·통계 오버레이다.

**검증**: Debug·Shipping 경고 0, nogpu 5/5(양쪽), 린트 6/6, 컨벤션 0건, 네 백엔드 실기동 종료
코드 0 · `[Error]` 0건, 덤프 창 14개·전부 열기 29개 / 빈 패널 0개.
**그리기가 그대로인지**는 스태시로 기준선을 다시 빌드해 `Game View` 창의 정점 수를 비교했다 —
고치기 전·후 모두 **742**로 같다(그리드·자·오버레이가 전부 이 창에 그려진다).
**한계**: 클릭 자체는 실기동으로만 확인할 수 있다. 어느 컴포넌트가 선택되는지는 이제 단위
테스트가 잡지만, ImGui 의 히트 판정·기즈모 우선순위는 사람이 눌러 봐야 한다.

### 2026-09-10 (테마 프리셋 표 + ClassicDark 가 저장되지 않던 버그)

프리셋을 하나 더하려면 `EditorThemeUtil.cpp` **네 곳**을 맞춰 고쳐야 했다 — `applyPreset` 의
팔레트 switch(데이터라 불가피), `loadFromConfig` 의 문자열→열거형 사다리, `saveToConfig` 의
열거형→문자열 switch, `drawThemeSettingsDialog` 의 이름 배열. 뒤 셋은 같은 사실(이름↔열거형)을
세 번 적은 것이고, 이름 배열은 `static_cast<int32>( _preset )` 을 인덱스로 써서 **열거형 순서에
묶여** 있었다 — 순서를 바꾸면 콤보가 조용히 틀린 이름을 보여 준다.

정의를 표(`getPresetRows`) 하나로 모았다: `{ 열거형, 저장 이름, 콤보 라벨, ImGui 기본색 사용 여부,
팔레트 }`. 콤보는 표 순서로 만들고 현재 항목은 **열거형 비교**로 찾으므로 인덱스 결합이 없다.
표와 열거형의 개수는 `static_assert` 로 맞춘다(`EditorThemePreset::Count` 추가).

**그 과정에서 찾은 버그: Classic Dark 선택이 저장되지 않았다.** `applyPreset( ClassicDark )` 가
`ImGui::StyleColorsDark()` 를 부르고 **그대로 return** 해서 `s_activeTheme` 이 이전 프리셋에 머물렀다.
그래서 (1) `saveToConfig()` 가 옛 이름을 써 다음 실행에 옛 테마로 돌아갔고 (2) 대화상자 콤보가
옛 프리셋을 선택된 것으로 보여 줬고 (3) `textSuccess`/`textWarning`/... 상태색 API 가 옛 테마의
색을 냈다. 이제 프리셋과 상태색을 기록하고, 지오메트리는 현재 스타일에서 되읽어 기록이 화면과
어긋나지 않게 한다 — **창 색은 여전히 `StyleColorsDark()` 그대로여서 보이는 모습은 안 바뀐다.**

**실증**: `EditorConfig.json` 의 `_themePreset` 을 `ClassicDark` 로 두고 실기동해 `loadFromConfig`
직후의 활성 프리셋을 임시 로그로 찍었다 — 고치기 전 `0`(ModernDark), 고친 뒤 `3`(ClassicDark).
확인 후 로그와 설정 파일을 되돌렸다.

**남긴 것**: Classic Dark 에서 액센트·라운딩을 편집하면 우리 팔레트가 적용되어 사실상 프리셋을
벗어난다. 예전에도 그랬고(다만 *이전* 테마의 팔레트로 튀었다) 지금은 최소한 예측 가능하다.

**검증**: Debug·Shipping 경고 0, nogpu 5/5, 린트 6/6, 네 백엔드 실기동 종료 코드 0 · `[Error]`
0건, 전부 열기 덤프 창 29개/빈 패널 0개.

### 2026-09-10 (미저장 문서 계약을 기반으로)

**InputMapEditorPanel 의 편집이 조용히 사라지고 있었다.** 이 패널은 자기 `_bDirty` 를 들고
화면에 "* Unsaved changes" 까지 띄웠지만, `IEditorPanel` 의 문서 계약
(`isDocumentDirty`/`trySaveDirtyDocument`/`discardDirtyDocument`)을 **하나도 구현하지 않았다.**
그래서:

- `Ctrl+S` 를 이 패널에 포커스를 두고 눌러도 `saveFocusedDirtyDocument()` 가 이 패널을 dirty 로
  보지 못해 false 를 돌려주고, `saveFocusedOrScene` 이 대신 **씬을** 저장했다.
- 종료·새 씬·씬 열기의 미저장 확인은 `countDirtyDocuments()` 로 세는데 이 패널이 0 으로 세어졌다.
  `saveAllDirtyDocuments()`·`discardAllDirtyDocuments()` 도 건너뛰었다 — 즉 편집이 사라졌다.
- 제목의 미저장 표시(`UnsavedDocument`)도 붙지 않았다.

원인은 계약이 **가상 함수 넷 + getPanelFlags 재정의**여서, 패널마다 자기 dirty 플래그를 들고
같은 것을 다시 구현해야 했다는 것이다. 실제로 세 패널(`EditorDocumentPanel`·`DataTablePanel`·
`GlobalVariablesPanel`)이 네 메서드와 `getPanelFlags` 의 같은 분기를 각자 복사하고 있었고, 넷째는
복사하지 않아 반쪽이 되었다.

**dirty 비트를 `IEditorPanel` 이 들게 했다.** `isDocumentDirty()` 는 더 이상 가상이 아니고,
파생은 `markDocumentDirty()` 로 알리고 `saveDocument()`/`revertDocument()` 만 구현한다.
`trySaveDirtyDocument`/`discardDirtyDocument` 는 기반이 dirty 를 보고 그 둘을 부른다. 미저장
표시는 `draw()` 가 dirty 를 보고 스스로 더하므로, 다른 플래그 때문에 `getPanelFlags()` 를
재정의한 패널도 표시를 잃지 않는다. 이제 **반쪽 구현이 불가능하다** — 패널이 dirty 를 알리는
유일한 방법이 기반 비트이기 때문이다.

계약은 ImGui 없이 컴파일되므로(두 함수를 헤더 인라인으로 내렸다) 가짜 패널로 테스트가 붙는다 —
`TestEditorPanelDocument.cpp` 5케이스: 알림이 잡히는지 · dirty 일 때만 저장하는지 ·
**저장 실패 시 dirty 가 남는지**(종료를 멈추는 근거다) · 버리기가 되돌리기를 부르는지 ·
문서 없는 패널이 조용히 지나가는지. `EditorTest` 40 → **44**
(쓰이지 않게 된 `EditorSessionPolicy::isToolSessionDirty` 와 그 케이스 1개를 걷어냈다).

감사로 확인한 것: 이제 문서 계약을 재정의하는 자리는 `saveDocument`/`revertDocument` 뿐이고,
패널에 남은 `*Dirty` 비트는 전부 **캐시 무효화** 플래그다(ContentBrowser 의 루트·폴더 목록,
GlobalVariables 의 프리셋 목록, Profiler 의 카탈로그, Inspector 의 프리셋 목록) — 문서 dirty 가
아니다. `DataTablePanel` 의 `_bLocDirty`/`_bGameDataDirty` 만 남았고, 이는 문서가 둘이라 어느
쪽을 저장할지 알아야 하기 때문이며 `syncDocumentDirty()` 한 곳에서 기반 비트와 맞춘다.

**검증**: Debug·Shipping 경고 0, nogpu 5/5(양쪽), 린트 6/6, 네 백엔드 실기동 종료 코드 0 ·
`[Error]` 0건, 전부 열기 덤프 창 29개/빈 패널 0개.
**한계**: 계약 자체는 테스트가 잡지만, "어떤 패널이 계약을 쓰는가" 는 테스트가 잡지 못한다.
새 패널이 또 자기 플래그를 만들면 위 감사 grep 으로만 보인다.

### 2026-09-10 (에디터 커맨드 SSOT)

**에디터 커맨드 하나가 세 곳에 따로 적혀 있었다.** `EditorMenuBar` 의 메뉴 항목(라벨·아이콘·
단축키 **문자열**·툴팁·활성 조건·동작), 같은 파일의 `processHotkeys` 키 사다리(조합 → 동작),
`CommandPalettePopup` 의 정적 목록(분류·라벨·설명·동작). 세 곳이 서로를 모르니 실제로 어긋났다:

- **`Ctrl+Z` 가 두 번 되돌렸다.** 전역 `processHotkeys` 와 `InspectorPanel::drawContent` 가 각각
  `undo()` 를 불렀고, ImGui 의 `IsKeyPressed` 는 소비되지 않으므로 같은 프레임에 두 호출자가
  모두 true 를 본다. Inspector 가 포커스면 두 칸 되돌아갔다. 게다가 Inspector 경로에는 플레이
  중 가드도, `WantTextInput` 가드도 없어서 값을 타이핑하다 Ctrl+Z 를 누르면 씬 편집이 되돌아갔다.
- **`F7`(게임 컴파일)은 어느 라벨에도 없었다.** 키 사다리에만 있어서 아무도 모른다.
- **`Ctrl+Shift+Z`(다시 실행)는 Inspector 가 포커스일 때만 먹었다.** 메뉴는 `Ctrl+Y` 만 알렸다.
  게다가 전역 `Ctrl+Z` 는 Shift 가 눌렸는지 확인하지 않아서 `Ctrl+Shift+Z` 에 undo 까지 함께 발동했다.
- **팔레트의 "Save Scene" 은 `saveFocusedOrScene()` 을 불렀다** — 메뉴의 "Save" 와 같은 동작이고
  메뉴의 "Save Scene"(`saveActiveSceneOrPrompt`)이 아니다. 라벨이 거짓이었다.
- **정렬/분배 7개는 세 경로였다** — 뷰포트 툴바는 `EditorWorkspace` 전달자를, 팔레트는
  `EditorTransformCommands` 를 직접 불렀고 라벨도 달랐다("Align X" vs "Align X (Center)").

정의를 `Common/Gui/EditorCommandGui.cpp` 의 표 **하나**로 모았다(커맨드 27개). 세 표면은 그것을
읽기만 한다 — 메뉴는 `drawMenuItem( "<id>" )` 한 줄, 단축키는 표를 훑는 루프 하나, 팔레트는
열릴 때 레지스트리를 읽는다. 커맨드를 하나 더하면 세 곳에 동시에 나타난다. 단축키 라벨과 툴팁의
`(Ctrl+S)` 도 표의 조합에서 만들어 붙으므로 라벨이 실제 처리와 어긋날 수 없다.

모델(`Common/Commands/EditorCommandRegistry`)은 **ImGui 없이** 컴파일되므로 테스트가 붙는다
(`EditorTest` 34 → 40). 그중 하나가 **중복 조합 검사**다 — 두 커맨드가 같은 키 조합을 주장하면
`validate()` 가 잡고, 에디터 시작 시 `[Error]` 로 남으므로 실기동 검증(0절)이 게이트가 된다.
위의 Ctrl+Z 중복이 다시 들어올 수 없다는 뜻이다.

단축키 조합 비교를 **정확 비교로 바꿨다**(예전엔 필요한 수정자만 확인했다). Alt+F4 처럼 OS 가
가로채는 것은 `kDisplayOnly` 비트로 라벨에만 남긴다 — 처리하는 척하지 않는다.

**곁들여 없앤 것**: `EditorWorkspace` 의 정렬·분배·바닥 스냅 전달자 3개(유일한 호출자가
뷰포트 툴바였다), `CommandPalettePopup::registerCommand`/`registerCommandInstance`(호출자 0),
그리고 그 파일의 정적 커맨드 목록. 팔레트는 이제 커맨드를 **가지지 않고 읽기만** 한다.

**검증**: Debug/Shipping 빌드 경고 0, nogpu 5/5(Debug·Shipping), 린트 6/6, 컨벤션 0건,
네 백엔드 실기동(`-dx12/-dx11/-vk/-gl`) 종료 코드 0 · `[Error]` 0건,
패널 덤프 기본 **창 14개/빈 패널 0개** · 전부 열기 **창 29개/빈 패널 0개**(기준선과 같다).
런타임에 커맨드 27개가 실제로 등록되는 것도 임시 로그로 확인한 뒤 로그를 걷었다.

**남은 구멍 하나**: 메뉴가 부르는 id 가 표에 없으면 그 항목은 조용히 사라진다(경고만 남고,
메뉴를 열지 않는 헤드리스 실행에서는 그 경고조차 나오지 않는다). 메뉴를 손댔으면
`Source/Editor/README.md` 의 "커맨드를 하나 더하려면" 절에 있는 `comm` 한 줄로 대조할 것.
린트로 승격하는 것은 스크립트 등록(GeneratedConstants·AssetAndToolTargets·PreCommitLint)까지
건드려야 해서 이번에는 하지 않았다.

### 2026-09-10 (재검증)

**억제한 것들이 정말 안전한지 하나씩 실증했다.** 억제는 판단이고, 판단은 근거가 남아야 다음 사람이
다시 세우지 않는다.

- **ASan `report_globals=0`(SmokeTest 전용)** — 네 가지 결함을 일부러 내는 12줄 프로브를 ASan 으로
  빌드해 같은 옵션으로 돌렸다: heap-buffer-overflow · heap-use-after-free · stack-buffer-overflow 는
  **그대로 잡히고**, global-buffer-overflow 만 안 잡힌다(끈 검사가 그것 하나라는 뜻이다). 기본
  옵션으로 돌리면 그 global 도 잡히는 것까지 확인해 프로브 자체가 유효함을 보였다. 다른 네 테스트는
  기본값이라 global 검사도 그대로다.
- **`.clang-tidy` 에서 끈 검사 셋** — 전수로 확인했다. `invalid-enum-default-initialization` 48건은
  열거형 종류가 19가지인데 **전부 D3D11/D3D12/Vulkan SDK 타입**이고 `sw::` 열거형은 0건이다.
  `derived-method-shadowing-base-method` 11건은 **전부 `swReflectSelf`** 다.
  `std-namespace-modification` 10건은 전부 `tuple_size`/`tuple_element`/`hash`/`equal_to` 를
  프로그램 정의 타입에 대해 특수화한 것으로 `[namespace.std]` 가 허용하는 형태다 — 다만 그중
  `tuple_size<FormattedValue<T>>` 이 `integral_constant<uint32,2>` 였다. 표준은 `size_t` 를 요구하므로
  (구조적 바인딩이 우연히 동작했을 뿐) `size_t` 로 맞췄다.
- **NOLINT 두 곳** — `BVHTree3D` 의 근거로 든 `static_assert( sizeof(float4x4) == 16 * sizeof(float32) )`
  가 `MatrixMath.h:471` 에 실제로 있다. `fixed_string` 의 자기대입은 주장만 있고 시험이 없었으므로
  두 경로(`fs = fs`, `fs = fs.c_str()`)를 테스트로 고정했다.
- **패널 덤프의 "정수 id 자식 창" 규칙** — 실제 덤프에서 이 규칙에 걸리는 창은 `Sequencer/00000379`
  하나뿐이다. 우리 자식 창은 전부 문자열 id 라 가려지지 않는다.

**그리고 이번 세션의 변경 하나가 동작을 바꿨다는 것을 찾아 고쳤다.** `Component` 생성자에서
기본값 적용을 걷어내면서, gamedata 의 **기반 타입 노드**(`<SceneComponent>` 같은)가 적용될 자리가
사라졌다. 예전 코드도 온전하지는 않았다 — 기반 생성자에서는 가상 `getTypeInfo()` 가 파생으로
디스패치되지 않아 언제나 `Component` 노드 하나만 봤고, 중간 기반은 한 번도 적용된 적이 없다.
이제 `applyTypeDefaults` 가 상속 체인을 **뿌리 → 파생** 순서로 전부 적용한다(파생이 마지막에
덮어쓴다). `EngineTest.ComponentDefaults.BaseTypeDefaultsApplyBeforeDerived` 로 고정했고, 고치기
전 코드로 되돌리면 기반 값 세 개가 실패하는 것까지 확인했다.
(곁들여 배운 것: 벡터 기본값의 텍스트 형식은 **쉼표 구분**이다 — `"2,3,4"`. 공백으로 적으면
파싱이 조용히 실패하고 값이 그대로 남는다.)

### 2026-09-10

**`fixed_string` 이 용량을 넘는 입력에서 버퍼 밖을 썼다.** 생성자·대입·`insert`·`append`·
`push_back` 이 전부 같은 모양이었다 — 길이가 N 을 넘는지 `SW_LOG_ASSERT` 로 **알리기만 하고 원래
길이 그대로 복사**했다. 단정은 실행을 멈추지 않는다(Debug 는 브레이크, 그 밖은 로그만). 에디터가
이 함정을 16곳에서 밟고 있었다(`fixed_string<128> buf{ text.c_str() }` 패턴). 모든 쓰기 경로를
`clampToCapacity`/`clampToRemaining` 두 헬퍼로 모으고, 보고는 **단정에서 경고로** 내렸다 — 넘치는
길이는 데이터에서 오지(긴 대사·긴 경로) 프로그래밍 계약 위반이 아니다.
`Core_String.FixedStringTruncatesInsteadOfOverflowing` 으로 고정했다(문자열 뒤에 감시값을 두고
잘림과 이웃 보존을 함께 본다). 옛 코드로 되돌리면 첫 케이스에서 프로세스가 죽는 것까지 확인했다.

**에디터 텍스트 입력을 `EditorWidgets::drawTextField` 하나로.** 8개 패널이 임시 버퍼를 만들어
넣었다 빼는 다섯 줄을 각자 적고 있었고, 버퍼 크기(64·128·256·512)를 자리마다 골랐다. ImGui 의
리사이즈 콜백으로 `string` 자체를 버퍼로 쓰므로 **길이 상한이 없다.** 16곳을 옮겼고 손으로 만든
임시 버퍼 InputText 는 남아 있지 않다(타입이 다른 `hashed_string` 3곳 제외).

**`-gv_editorOpenAllPanels=1`** — 0절 참고. 이걸로 바로 드러난 것: DataTable 패널이 열릴 때마다
빈 경로로 파일을 읽어 `[Error]` 3건(활성 게임에 `data/localization` 도메인이 없다), 그리고
`Config/Editor/DialogueGraphEditor.json` 이 `.gitignore` 에 빠져 있던 것.

**`bake.stamp` 해시가 PC 마다 달랐다.** 셰이더 소스를 바이트 그대로 해싱하는데 `.gitattributes`
가 없어 줄 끝이 체크아웃마다 다를 수 있다 — 실제로 `instancesort.hlsl` 하나만 LF 였다. 두 PC 가
서로의 스탬프를 번갈아 덮어쓰고 있었다(Shipping 을 빌드할 때마다 작업 트리가 더러워진 원인).
CR 을 뺀 바이트로 해싱하고(굽는 쪽·확인하는 쪽 같은 정규화) 형식을 `SWBAKE 2` 로 올렸다.

**ASan: 모듈을 해제한 뒤 적재하면 실패하던 문제 — 원인 규명, SmokeTest 복귀.** 예전에는 스위트째
Disabled 였고 원인을 "못 찾았다" 고 적어 두었다. `detect_odr_violation=0` 덕분에 ODR 보고가
앞을 가리지 않게 되자 진짜 보고가 나왔다: `RHI_DX12.dll` 의 정적 초기화가 `SW_LOG_CALLER` 의
`__FILE__` 를 읽는 자리에서 global-buffer-overflow. 이유는 **이 ASan 런타임이 DLL 을 내려도 그
모듈의 전역 등록을 지우지 않는 것**이다 — `report_globals=2` 로 세어 보면 스위트 한 번에
"Added Global" 5241건, 제거 **0건**이다. 그래서 다음 모듈이 그 주소 범위에 매핑되면 자기 전역을
읽는데도 앞 모듈이 남긴 레드존을 밟는다. 보고하려고 주소를 설명하는 순간 이미 언매핑된 모듈의
디스크립터를 역참조해 "nested bug in the same thread, aborting" 으로 죽는다 — 예전에 보이던
`LoadLibrary err=1114` 와 맨 세그폴트가 이것이다. 이 스위트는 존재 이유가 모듈 적재·해제라
우회할 수 없으므로 **SmokeTest 에서만** `report_globals=0` 을 주고 스위트를 되살렸다(힙·스택·
use-after-free 는 그대로 잡힌다). ASan nogpu 5/5, 30초.

**Shipping 테스트가 도구가 없어서 실패하고 있었다.** `ShaderBindingContractTest.
ReflectionNamesAreUniformAcrossBackends` 가 "forwardlit_ps g_SwMaterials 원소 없음: dx12" 로
떨어졌다. 셰이더가 아니라 `dxcompiler.dll` 이 없어서 DXIL 리플렉션을 못 얻은 것이었다 —
`sw_copyDxcDlls(EngineTest)` 가 `NOT SW_SHIPPING_BUILD` 로 막혀 있었다. 테스트 바이너리는 `Bin`
이 아니라 `TestBin` 으로 나가므로 배포물에 섞이지 않는다. 가드를 걷었다.

**정적 분석·기타** — 위 1-2 참고. `Component` 의 기반 생성자 가상 호출(파생 기본값이 한 번도
적용되지 않던 것), ContentBrowser 의 `system()`, `Logger::registerCaller` noexcept, 그리고
`SW_ACTIVE_GAME` 이 낡은 캐시를 가리킬 때의 오류 메시지(가능한 게임 목록과 고치는 법을 같이
알려준다 — Ninja-Debug-ASAN 이 없어진 'Demo' 를 들고 있어서 configure 조차 못 했다).

**요청한 "나머지 문제" 처리 (3건 전부)**

1. **Engine 강결합 해체 (10 → 7)** — 아래 항목.
2. **에디터 패널 검증 수단** — `-gv_editorPanelDump=N` (0절 참고). 공용 위젯 채택·목록형 골격 항목은 이제 화면을
   보지 않고도 검증할 수 있다. 리팩터 자체는 사람이 한 패널씩 가는 편이 맞아 남겨 두었다.
3. **GL 플랫폼 컨텍스트 분리** — `Graphics/RHI/GL/Platform/` 에 `IOpenGLPlatformContext` +
   WGL/GLX/NSGL 세 구현. `OpenGLRHIDevice` 의 플랫폼 분기 **14개 → 0개**. 플랫폼을 하나 더
   지원하려면 파일 한 쌍과 팩토리 한 줄이면 된다(예전에는 멤버 함수 여섯 곳의 `#if` 사다리를
   모두 찾아 고쳐야 했다).
   - Linux/macOS 코드는 **내용을 고치지 않고 그대로 옮겼다.** 여기서 컴파일할 수 없기 때문이다
     (WSL 없음). Linux 는 CI(ubuntu-22.04 × 3) 가 컴파일한다. **macOS 는 CI 에도 없어 여전히
     어디서도 컴파일되지 않는다** — 옮기기 전과 같은 상태다.
   - 동작을 바꾸지 않으려고 "프레임 시작에 컨텍스트 되찾기" 를 `reacquireForFrame()` 로 두고
     WGL 만 구현했다. GLX·NSGL 은 예전에도 여기서 아무것도 하지 않았다.
   - 검증: 네 백엔드(`-dx12 -dx11 -vk -gl`) 모두 종료 코드 0 / `[Error]` 0건.
     GL + 에디터의 에러 3건은 이 변경과 무관한 기존 버그였고(스태시로 기준선을 다시 빌드해
     확인했다), **그 다음에 따로 고쳤다** — 아래 "GL 컨텍스트" 항목.

**전수 조사 3회차 — 남은 지적을 0 으로 (65 → 0건)**

남은 65건을 하나씩 판단했다. 고칠 수 있으면 고치고, 구조적으로 불가능한 자리는 **이유를 적어**
NOLINT 했다. 이유 없는 억제는 남기지 않았다.

**찾은 실제 결함**

- **`Component` 생성자가 가상 함수를 불러 기본값이 틀린 타입으로 적용됐다.** 생성자가
  `initialize()` → 가상 `getTypeInfo()` 를 부르는데, 생성 중에는 객체가 아직 `Component` 라
  **파생 타입이 아니라 기반 타입**의 TypeInfo 가 나온다. 즉 `MeshComponent` 를 만들어도
  "Component" 이름으로 기본값을 찾았다. 실제 생성 경로는 타입을 아는 쪽이 이미 올바르게 넘겨
  준다 — `GameObject::addComponent<T>` 와 `GameObjectManager` 의 이름 기반 생성이 둘 다
  `applyTypeDefaults( 파생 TypeInfo )` 를 부른다. 즉 생성자 호출은 **중복이면서 틀린 조회**였고,
  컴포넌트를 만들 때마다 헛일을 했다. 호출과 (호출자가 없어진) `initialize()` 를 걷어냈다.
  안전한지 확인한 근거: `initialize()` 는 `private` 이고 호출자가 생성자뿐이며, 기본값 데이터는
  게임이 `setPath` 를 부르지 않으면 아예 로드되지 않는다(`Resource/` 에 `<Defaults>` 노드도 없다).
- **`FrameRendererPassExecute` 의 SSAO PSO 폴백이 자기 자신이었다.**
  `getEnginePso(SSAO) != 0 ? getEnginePso(SSAO) : getEnginePso(SSAO)` — 참·거짓이 같아 아무 효과가
  없고 함수만 세 번 불렀다. 형제 패스는 전부 **다른** PSO 로 폴백한다(DepthPrepass→Shadow,
  GBufferAlbedo→GBuffer, Tonemap→Present). 복사하면서 대체 대상을 바꾸지 않은 자리다. 폴백을
  짐작해 넣지 않고, PSO 가 0 이면 `drawFullscreen` 이 건너뛰므로 형제들처럼 그대로 넘기게 했다.
- **`StringBuilder::appendFormat` 이 재시도 루프에서 매번 `std::forward` 했다.** 버퍼가 모자라면
  같은 인자로 다시 포맷하는데, 그때는 이미 이동된 값을 쓰게 된다. `formatstring` 은 값을 읽어
  찍기만 하므로 lvalue 로 넘겨 위험 자체를 없앴다.
- **컴포넌트 붙여넣기가 실패를 삼켰다.** `pasteComponentAsNew` 가 역직렬화 결과를 계산해 놓고
  **아무도 읽지 않았다.** 둘 다 실패해도 빈 컴포넌트를 붙이고 "붙여넣기" 실행 취소 항목까지
  남겨서, 쓰는 사람은 왜 비었는지 알 수 없었다. 경고 로그를 남긴다.
- **`Material` 의 죽은 코드.** 첫 순회가 `packSize` 를 계산했지만 뒤따르는 `if` 는 본문이 주석뿐인
  빈 블록이었고 값은 아무도 읽지 않았다. 실제 패킹은 두 번째 순회가 다시 계산해서 한다.
- **콘텐츠 브라우저 필터 라벨이 `string_view` 였다.** 쓰는 쪽 셋이 모두 곧바로 `.data()` 를 ImGui 로
  넘기는데 ImGui 는 널 종단을 요구한다. 지금 표가 전부 리터럴이라 우연히 맞을 뿐이라, 타입을
  `const utf8*` 로 바꿔 계약을 적었다.
- 소멸자·생성자에서의 가상 호출 12곳을 클래스 이름으로 한정했다. 파괴 중에는 파생 재정의가 이미
  사라진 뒤라 이 클래스의 것이 불린다 — 지금 동작이 의도한 것이므로 코드로 적었다.
- `.bin`/`.xml` 은 -4, `.json` 은 -5 처럼 확장자 길이를 손으로 쓰던 자리를 표로 돌게 했다.
  `Json`/`Xml` 직렬화의 같은 본문 두 분기는 조건으로 합쳤다. `ShaderBaker` 의 SPIR-V 두 케이스는
  묶어서 "같아야 한다" 를 드러냈다.

**오탐이라 이유만 남긴 것** — 다시 판단하지 말 것:
`Base{ std::move(other) }` 뒤의 파생 멤버 읽기(기반 부분객체만 이동한다), 타입 이름·`##` 인자를
받는 매크로(괄호를 씌우면 문법이 깨진다), 순서가 규약인 분기(vector 재할당의 이동/복사 우선순위,
Windows 전용 DX11·DX12, "여기서 안 하고 아래서 한다" 는 케이스 묶음), 크기를 함께 넘기는
`append(data(), count)`(커스텀 string 의 오버로드를 인식하지 못한다), `float3`/`float4x4` 를
연속 float 로 훑는 자리(배치는 옆의 `static_assert` 가 보장한다), 뮤텍스 락이 던질 수 있어
noexcept 와 어긋난다는 지적(잠그지 못하는 상황은 복구 대상이 아니다).

**끈 검사 하나 추가** — `clang-analyzer-optin.performance.Padding`. 걸린 셋이 전부 인스턴스가
하나뿐인 매니저이거나 20행짜리 정적 표라 아끼는 양이 무의미한데, 이 저장소는 "생성자 초기화는
선언 순서" 규약이라 멤버를 옮기면 초기화 목록도 같이 옮겨야 한다. 대량 배열로 쓰이는 뜨거운
구조체가 생기면 그때 개별로 재는 편이 낫다.

**전수 조사 2회차 — 분류해 둔 것을 실제로 고친다 (65건까지)**

1회차에서 110건으로 분류해 둔 것을 하나씩 판단해 처리했다. 고친 것은 전부 **실물로 확인한 뒤**
고쳤고, 오탐은 자리마다 이유를 남겼다.

**가장 위험했던 것 — `StringUtil` 이 비-ASCII 바이트를 부호 있게 다뤘다.** `char` 의 부호성은 구현
정의이고 UTF-8 의 0x80 이상 바이트는 signed char 에서 음수다. 그대로 int/uint64 로 넓히면서 둘이 깨졌다:

- `compare` 가 한글처럼 비-ASCII 가 섞인 문자열을 ASCII 보다 **작다고** 답했다(`strcmp` 규약의
  반대다). 같은 함수의 대소문자 **구분** 경로는 이미 `uint8` 로 비교하고 있어서, 두 모드가 서로
  다른 순서를 냈다 — 정렬·이진 검색에 쓰면 일관성이 깨진다.
- `computeHash64` 는 `bIgnoreCase` 경로만 부호 확장됐다. **그 인자는 기본값이 true 라 거의 모든
  호출이 이 경로다.** 같은 바이트가 경로에 따라 다른 값으로 해싱되고, `char` 가 unsigned 인
  플랫폼(ARM)에서는 해시 자체가 달라진다.

둘 다 `uint8` 을 거치게 고쳤다. ASCII 는 동작이 그대로이고(부호 확장이 없다), 바뀌는 것은 비-ASCII
뿐이다 — 저장되는 경로·타입 이름은 소문자 ASCII 로 강제돼 있어 안전하다.
`Core_String.NonAsciiBytesAreUnsigned` 로 고정했다(수정 전이라면 `compare` 가 -119 를 돌려줘 실패한다).
**주의**: 같은 텍스트라서 utf16 오버로드까지 치환됐던 것을 되돌렸다 — UTF-16 코드 단위를 `uint8` 로
자르면 값이 잘린다. 고칠 때 오버로드를 반드시 구분할 것.

**널 역참조 둘.**
- `ImGuiOpenGLRendererBackend::initialize` 가 널·비-OpenGL 을 걸러 `_pRHIDevice` 를 nullptr 로 만들어
  두고도, Windows 분기에서 **검증되지 않은 원시 매개변수**를 역참조했다. 바로 아래 Linux 분기는
  `&& pRhiDevice != nullptr` 로 막고 있었으니 Windows 쪽만 빠진 것이다. 양쪽을 검증된 멤버로 맞췄다.
- `HashedStringPool::shutdown` 이 `SW_ASSERT` 만 믿고 역참조했다. Shipping 에서 단정은 사라지므로
  initialize 전에 또는 두 번 불리면 죽는다. shutdown 은 여러 번 불려도 안전해야 한다.

**`int` 곱셈 후 확대** — `Win32SplashWindow` 의 픽셀 수·바이트 오프셋(포인터 오프셋으로 쓰인다),
`TileMapPanel` 의 `_width*_height`·`indexOf`, `ProfilerPanel` 의 바이트 경계, D3D12 디스크립터 오프셋
8곳. `EventDispatcher` 는 마침 같은 값의 상수(`kDefaultLinearCapacity`)가 이미 있어 그걸 쓴다.

**`float3`/`float4x4` 에 `data()` 와 `static_assert` 를 추가했다.** `&m._11` 을 받아 `[0..15]` 로 읽는
코드가 여럿 있었다 — 형식적으로 배열이 아닌 멤버를 배열로 읽는 것이라 UB 다. `data()` 로 모으면
가정이 한 곳에 있고, `static_assert( sizeof(float4x4) == 16*sizeof(float32) )` 가 그 가정(패딩 없음)을
**컴파일 타임에** 지킨다. 찾은 호출부 4곳을 옮겼다.

**ContentBrowser 의 체커보드를 정수 루프로.** float 변수를 루프 카운터로 써서 반복마다 오차가 쌓였고
(칸 수가 경계에서 달라질 수 있다) 칸 색을 정하려고 다시 나눗셈을 했다. 인덱스로 돌면 위치는 곱셈
한 번, 색은 인덱스 합의 홀짝이다.

**`BlendSpace2D::evaluate` 의 가중치 배열을 0 으로 시작한다.** 위에서 `empty()` 를 걸러 실제로는 쓰기
전에 읽히지 않지만, 그 사실이 `MathUtil::min` 을 거친 `sampleCount` 에 숨어 있어 읽는 사람도 분석기도
확신할 수 없다. 32개 float 을 0 으로 두는 비용은 없다.

**ASan 설정을 두 번 고쳤다 — 측정해 보고 판단이 바뀌었다.**

- `detect_odr_violation` 을 **1 → 0** 으로. 1 로 두면 "크기가 다른 중복만 보고" 하니 진짜 ODR 버그는
  남는다는 계산이었는데, **레벨 1 도 등록된 전역 전체를 훑는 비용은 그대로** 치른다. 모듈을 반복해
  올리고 내리는 SmokeTest 가 비-ASan 6.9초 → ASan **1800초 초과**였고, 0 으로 바꾸자 **5초**였다.
  260배는 오버헤드가 아니라 사용 불가다.
- 타임아웃 배수를 `sw_addTestExecutable` 안에만 두었더니 **손으로 `add_test` 한 `EngineTest_NoGPU` 가
  빠졌다.** 그쪽만 평시 타임아웃을 써서 혼자 타임아웃으로 떨어졌고, "스위트 전체 20초인데 한
  테스트가 시간 초과" 라는 모순된 결과가 나왔다. 보정을 `sw_applySanitizerTestProperties()` 한 곳으로
  빼고 양쪽이 부른다.

결과: ASan 스위트가 **1820초/1건 실패 → 21초/전부 통과**(SmokeTest 는 Disabled).

**잠재 결함 전수 조사 — 도구를 먼저 고쳐야 결함이 보였다**

ASan 과 clang-tidy 를 돌렸더니 **둘 다 쓸 수 없는 상태**였다. 도구를 고치는 것이 조사의 절반이었다.

**Windows ASan 빌드는 한 번도 완주한 적이 없었다** (CI 는 Linux ASan 만 돌린다 — `ci.yml`).
세 군데가 독립적으로 막고 있었고 전부 고쳤다:

1. ASan 런타임 DLL 을 `Bin/` 에만 복사했다. 코드젠 도구는 `BuildTools/` 에 있어 못 찾고
   `0xc0000135` 로 뜨지 못한다 → **빌드가 코드젠 단계에서 죽었다.** 두 곳 모두에 복사한다.
2. `/MD` 를 FORCE 하는데(clang-cl ASan 은 디버그 CRT 를 지원하지 않는다 — `/MDd` 로 바꾸면
   컴파일러가 거부한다) vcpkg 는 Debug 구성에서 `/MDd` 라이브러리를 물려줬다.
   `_ITERATOR_DEBUG_LEVEL` 이 어긋나 `EditorModule.dll` 링크가 섰다. 임포트 구성을 Release 로
   매핑하고 런타임 DLL 도 릴리스 쪽을 직접 staging 한다(applocal 경로는 툴체인에 박혀 있어
   그냥 두면 `z.dll` 이 없어 `CoreTest.exe` 가 뜨지 못한다).
3. `/fsanitize=address` 를 켜면 MSVC STL 이 컨테이너에 ASan 주석을 달고 오브젝트에
   `annotate_string`/`annotate_vector`/`annotate_optional` 표시를 심는다. ASan 없이 빌드된 vcpkg
   라이브러리와 또 어긋난다. 개별 매크로를 막으면 다음 것이 나오므로 우산 매크로
   `_DISABLE_STL_ANNOTATION` 하나로 끈다(잃는 것은 STL 컨테이너 오버플로 탐지뿐이다).

그 위에 ASan 을 **실제로 쓸 수 있게** 한 것:

- **타임아웃.** 평시 기준을 그대로 써서 5개 테스트가 **전부 타임아웃으로 실패**하고 있었다.
  결함처럼 보이지만 설정 문제다. `SW_ENABLE_SANITIZER` 면 10배로 둔다.
- **ODR 오탐.** 플러그인 DLL 이 여럿이고(RHI_*, SWGame, GF_*, EditorModule) 같은 SDK·CRT 헤더를
  포함하니, 헤더가 박는 전역이 DLL 마다 생겨 ASan 이 ODR 위반으로 본다 — 나온 것이 `d3d11.h` 의
  `D3D11_DEFAULT` 와 CRT 내부 `_Avx2WmemEnabledWeakValue` 다. 영구 오탐이므로 CTest 환경에
  `detect_odr_violation=1` 을 둔다. **끄지(0) 않는다** — 크기가 다른 진짜 ODR 버그는 계속 잡힌다.
- 결과: `CoreTest` 167/167, `ReflectionTest` 100, `EditorTest` 34/34 가 ASan 하에서 보고 0건.

**clang-tidy 는 오탐이 신호를 덮고 있었다.** 설정을 저장소에 넣어 구조적으로 막았다:

- `.clang-tidy` — 이 코드베이스에서 **쓸 수 없다고 실측한** 두 검사만 끈다.
  `bugprone-easily-swappable-parameters` 158건, `clang-analyzer-optin.core.EnumCastOutOfRange`
  39건(비트 플래그 조합). 끈 이유를 파일에 적었다.
- `Scripts/lint/report/RunClangTidy.py` — 한 명령으로 같은 설정. 가장 큰 것은 **`/Y-` 로 MSVC PCH 옵션을
  무효화**한 것이다. clang-tidy 는 그 PCH 를 쓸 수 없는데 `/Yu`·`/FI` 가 남아 헤더를 두 경로 표기로
  두 번 파싱한다. `#pragma once` 가 같은 파일로 보지 못해 **"redefinition of ..." 오류가 쏟아졌다**
  — 정의는 하나뿐인데도. Core/Memory 기준 지적 3건 → 진짜 1건으로 줄었다.
- 결과: 노이즈 포함 ~300건 → 고유 110건. 종류별 판정은 위 1-2 에 적었다.

**그렇게 해서 찾은 실제 결함 (전부 고쳤다)**

- **`BattleState::applyMove` 널 역참조.** 종족·카탈로그 조회가 실패하면 `pMove` 가 nullptr 인데
  `dmg == 0` 분기가 `pMove->_name` 을 무방비로 읽었다. `dmg > 0` 분기는 `pMove != nullptr` 을
  함의해 안전했기 때문에 **정상 데이터로는 절대 걸리지 않았다** — 기술 데이터가 빠지면 크래시다.
- **XML 역직렬화가 길이를 버렸다.** `IXmlBackend::initXmlDeserialization` 이 `const utf8*` 를 받아
  호출부가 `string_view::data()` 를 넘기며 길이를 잃었다. 뷰가 더 큰 버퍼의 일부면 널 종단이 없어
  파서가 끝을 넘어 읽는다. `XmlDocument::parse` 는 원래부터 `string_view` 를 받아
  `load_buffer(data, size)` 로 안전하게 읽으므로 **이 중간 계층만 구멍이었다.** 인터페이스를
  `string_view` 로 바꿔 길이를 끝까지 흘려보낸다(구현체 셋 — 테스트의 `SimpleXmlBackend` 포함).
- **Windows ASan 에서 누수 검사가 도는 척만 했다.** Linux/macOS 는 ASan 이면 LSAN 으로 갈라 두었는데
  **Windows 만 ASan 여부를 보지 않고** CRT 검사를 골랐다. ASan 이 힙을 대체하므로 `_Crt*` 가 전부
  무효가 된다. 유일한 증상이 "set but not used" 경고 둘이었고 Windows ASan 빌드를 처음 돌리고
  나서야 보였다.
- **`int` 곱셈 후 size_t 확대** 네 곳(`TileMap::indexOf`, `TileMap`/`TileMapXml` 의
  `_width*_height`, `Defines.h`·`hashed_string.h` 의 크기 상수). 넘친 뒤 확대되므로 캐스트가 값을
  지켜 주는 것처럼 보이지만 이미 틀린 값이다.
- `FrameDoubleBuffer` 의 기본 용량이 숫자로 박혀 있었다 → 같은 파일이 이미 쓰는
  `constant::kDefaultFrameArenaCapacity` 로.

**확인했지만 결함이 아니었던 것** (같은 의심을 반복하지 않도록): `fixed_string`·`Delegate` 의
자기대입(가드 있음), `Memory.h` 의 `static_assert(sizeof(T)>0)`(불완전 타입 가드 관용구),
`'0'+digit` 좁힘(48–57), DX11·DX12 동일 분기(Windows 전용), `XmlDocument` 의
`append(data(), count)`(크기를 넘긴다), `StringUtil::stristr`(가드 페이지 테스트로 확인 —
`equals` 의 단축 평가가 종단자에서 끊는다).

**검색 필터를 한 곳으로, 그리고 "0건" 을 말하게 한다 (예전 1-2 "목록형 패널 골격")**

백로그에 "여러 패널이 툴바 → 검색 → 목록 → 상태줄을 각자 조립한다" 고 적어 두고 `EditorListPanel`
골격을 만들자고 했다. **전제가 또 틀렸다** — 17개 패널을 세어 보니 조립은 이미 공유되고 있었다:
`EditorChrome::beginToolbar` 10개, `EditorWidgets::drawSearchField` 6개(= 검색 있는 패널 전부),
`drawCountLabel` 4개. 골격을 또 만들면 이미 공유된 것을 한 겹 더 싸는 일이다. 게다가 목록형으로
보이는 패널조차 모양이 제각각이어서(ContentBrowser 2분할, DataTable 탭, GlobalVariables 표 3개,
Hierarchy 트리+드래그드롭+단축키) 하나의 템플릿에 들어가는 패널이 사실상 없다.

**실제로 복사되고 있던 것은 판정 로직이었다.** 검색 가능한 6개 패널이 `matchFilter` 를 각자 썼다 —
"필터가 비면 전부 통과" 가드 + 필드 수만 다른 `stristr` 체인(Console 3 · ContentBrowser 1 ·
DataTable 4 · GlobalVariables 3 · Hierarchy 1 · Inspector 3). 이 가드를 빼먹으면 조용히 반대로
동작한다 — `stristr( x, "" )` 는 nullptr 을 주므로 **필터가 비었을 때 목록이 전부 사라진다.**

- `Common/Widgets/EditorListFilter` — 필터를 한 번 정규화(trim)해 들고 항목마다 판정만 한다.
  ImGui 에 의존하지 않으므로 단위 테스트가 붙는다(EditorTest +5). 필드는 `string_view` 로 받는다 —
  종단자 없는 조각도 안전하다(`stristr` 은 널 종단 문자열만 받는다).
- 6개 패널이 전부 이것을 쓴다. `Source/Editor/Panels` 에 손으로 쓴 검색 술어는 남아 있지 않다.
- `DataTablePanel` 은 행 루프 안에서 필터링하던 것을 표 열기 **전에** 걸러 두도록 바꿨다. 0건 여부를
  알아야 안내를 그릴 수 있고(표가 남은 영역을 다 차지해서 표 뒤에 그리면 화면 밖으로 밀린다),
  행마다 필터를 다시 만들지 않는 효과도 같이 온다.

**진짜 구멍은 "0건" 이었다.** 기존 안내 문구 20곳은 **전부** 데이터·서비스가 없다는 뜻이고,
"검색어가 아무것도 맞히지 못했다" 를 말하는 곳은 **한 곳도 없었다.** 아무것도 맞지 않는 검색어를
치면 설명 없는 빈 상자가 남는다 — 고장처럼 보인다. 복사할 선례가 없으니 새 패널도 똑같이 빠뜨린다.
`EditorWidgets::drawNoSearchResultHint( filter )` 를 만들어 6개 패널에 붙였다. `ConsolePanel` 은
0건의 이유를 셋으로 나눈다(로그가 아직 없음 / 검색어가 걸러냄 / 레벨을 전부 끔) — 고치는 방법이
서로 다르기 때문이다.

**이번에는 UI 변경을 실제로 측정했다.** 생성자에서 필터를 아무것도 안 맞는 값으로 시작시켜
`-gv_editorPanelDump` 를 찍고, 힌트만 끈 빌드와 비교했다(측정 후 계측은 걷었다):

```
힌트 없음: Output Log/##log_scroll  vtx=0  <== BLANK      ← 빈 패널 1개, 경고 발생
          Content Browser/##cb_assets vtx=24
힌트 있음: Output Log/##log_scroll  vtx=104               ← 빈 패널 0개
          Content Browser/##cb_assets vtx=128
```

**도구가 실제 결함을 잡았다** — 맞지 않는 검색어가 패널을 진짜로 비웠고, 힌트가 그것을 없앤다.
다만 `Hierarchy`·`Inspector`·`DataTable`·`GlobalVariables` 의 0건 분기는 **런타임에서 확인하지
못했다**: 활성 게임이 `Empty` 뿐이라 씬 오브젝트가 없어 Hierarchy 는 "No active scene." 로 빠지고,
Inspector 는 선택이 없어 "Nothing selected." 로 빠지며, DataTable 은 도구 패널이라 닫힌 채 뜬다.
이 네 곳은 술어 단위 테스트 + 코드 검토까지다. 씬이 있는 게임이 생기면 같은 방법으로 확인할 수 있다.

**곁가지로 확인한 것 — `StringUtil::stristr` 은 정상이다.** `string_view( pStr, subLen )` 를 만들어
비교하므로 남은 길이가 검색어보다 짧으면 종단자를 지나 읽을 것처럼 보였다. 가드 페이지 테스트로
확인하니 **넘어가지 않는다** — `equals` 의 비교 루프가 첫 불일치에서 끊기고, 널 종단자는 검색어의
어떤 문자와도 반드시 불일치한다. 안전한 이유가 단축 평가라는 구현 세부에 걸려 있으므로(비교를 여러
바이트씩 처리하도록 "최적화" 하면 바로 깨진다) 테스트는 남겼다: `Core_String.StristrStopsAtTerminator`.

**GL 컨텍스트를 기다려서 가져온다 — `-gl -EnableEditor` 에러 3건 해결**

`-gl -EnableEditor` 이 늘 뱉던 에러 3건(`wglMakeCurrent failed (err=170)` ×2 +
`Failed to resolve GL texture`)이 **한 원인**이었고 GL 안에서 끝났다. 네 백엔드 × 에디터
유무 8조합 모두 `[Error]` 0건이다.

**원인.** GL 디바이스는 `requiresExclusiveContextThread()==true` — 컨텍스트는 한 스레드만
current 로 가질 수 있고, 렌더 워커가 프레임마다 쥐고 놓는다(`RenderThread::executePacket`).
그 사이에 다른 스레드가 GL 리소스를 만들려 하면 `wglMakeCurrent` 가 `ERROR_BUSY` 로 실패한다.
문제는 실패를 다룬 방식이었다 — `ScopedOpenGLContext` 가 로그만 남기고 `_bNeedsUnbind=false`
로 두는데, **가드의 본문은 그대로 실행됐다.** 그래서 컨텍스트 없이 `glGen*` 이 나가 리소스가
조용히 만들어지지 않고, 한참 뒤 "Failed to resolve GL texture" 로 드러났다. 세 에러가 한 뿌리다.

**고친 방법.** 한 번 시도하고 포기하는 대신 **차례를 기다린다.** 워커가 프레임 끝마다 놓으므로
한 프레임 안에 온다.
- `OpenGLRHIDevice::acquireGraphicsContextBlocking( timeoutMs=250 )` 를 추가했다 — 1ms 간격으로
  조용히 다시 집고, 제한 시간을 넘길 때만 로그를 남긴다.
- `ScopedOpenGLContext` 가 이것을 쓴다.
- 플랫폼 `makeCurrent()` 는 **실패해도 로그를 남기지 않는다** (경합은 정상이다. 재시도마다
  에러를 찍으면 정상 동작이 오류로 보인다 — 실제로 그 2건이 그랬다). 알릴 책임은 호출부로
  옮겼다: 기다리지 않는 `bindGraphicsContext()` 는 예전처럼 실패를 로그한다.

**앞선 진단이 너무 넓었다.** 이 자리에 있던 1-3 은 원인을 "UI 스레드가 GL 컨텍스트를 요구하는
경로가 둘" 로 보고 "어느 스레드가 컨텍스트를 갖는가" 라는 규약을 새로 세워야 한다고 적었다.
호출부마다 태그를 붙여 측정하니 **에디터 백엔드 `initialize`/`shutdown` 과
`OpenGLRHICommandList::beginCommandList` 는 한 번도 실패하지 않았다** — 실패 2건은 전부
`ScopedOpenGLContext` 였다. 스레드 소유권 재설계는 필요하지 않았다.

**요청한 "나머지 문제" 처리 — Engine 강결합 해체 (10 → 7)**
자세한 내용은 아래 3절의 해당 항목과 `Source/Engine/README.md` 의 티어 표에 있다.
부수로 드러난 버그 둘도 같이 고쳤다:
- **초기화 실패 경로가 SEGFAULT 로 끝났다.** 요청한 백엔드가 이 빌드에 없으면
  `RHI` 객체는 생기지만 디바이스가 없는데, `EngineLoop::shutdown` 이 `hasDevice()` 검사 없이
  `getDevice()` 를 역참조했다. 설정에 `DirectX11` 을 적은 Shipping 빌드(DX12 전용)에서 재현되고,
  Debug 에서도 `RHI_DX11.dll` 을 숨기면 같다. 이제 정상 종료(`main` 의 `return -1`)한다.
- **그 오류 메시지가 `No factory registered for backend 0` 이었다.** 설정 파일을 고친 사람이
  원인을 알 수 없다 → `DirectX11 백엔드는 이 빌드에 없습니다. 사용 가능: DirectX12`.

**폴더 개편 중에 드러난 실제 버그 셋** (각각 별도 커밋)
- `formatstring` 이 공백을 플래그로 받아 `%#` 뒤 단어의 첫 글자를 먹고 있었다. 테스트 요약이
  `0ailed`, `%# of %#` 는 8진수, `%# present` 는 16진수 — 해당 로그가 82곳. 호출부가 아니라
  파서를 고쳤다(`6c0548f7`). 치수 로그 18곳의 `%#x%#` 는 설계상 16진수가 맞아 `×`(U+00D7)로.
- **창 크기 설정이 한 번도 반영된 적이 없었다.** `WIDTH`/`HEIGHT` 만 `bUseDefaultValue=true` 라
  인자를 주지 않아도 `getArgument` 가 등록된 기본값 1280 으로 `true` 를 돌려주고, 호출부의
  "설정값을 씨앗으로 두고 커맨드라인이 있으면 덮어쓴다" 패턴이 **항상** 덮어썼다. WIDTH 기본값이
  설정값과 같아 폭만 우연히 맞아서 가려져 있었다(창이 1280×1280 으로 떴다).
- **`Source/Core/CommandLine/ArgumentList.xxx` 는 죽은 사본이었다.** 실제로 include 되는 것은
  `Core/Predefined/ArgumentList.xxx` 이고, 죽은 쪽은 `LANGUAGE`·`BAKE_SHADERS` 가 빠진 낡은
  상태였다. 폴더 이름상 먼저 찾게 되는 자리라 고쳐도 아무 일이 없는 덫이었다 — 삭제했다.
- Shipping 경고 3건(`FrameProfiler`)도 없앴다. `report()` 는 Info 로그로만 출력하는데 배포본에서
  `SW_LOG_INFO` 가 사라져 **출력 없는 계산**을 하고 있었다 — 로그가 컴파일될 때만 돌게 묶었다.
  이제 Debug/Shipping 양쪽 모두 빌드 경고 0 이다.

**Source 폴더를 순서대로 개편 중 (App → RuntimeAPI → Core → Engine → …)**

진행한 폴더: `App`(`6efa4fd2`) · `RuntimeAPI`(`06889dc7`) · `Core`(`58c1ac30`) ·
`Engine`(`83b6ea60`) · `GameFramework`(아래).
**모두 끝났다.** `App`(`6efa4fd2`) · `RuntimeAPI`(`06889dc7`) · `Core`(`58c1ac30`) ·
`Engine`(`83b6ea60`) · `GameFramework`(`bfb00e1d`) · `Games`(`71c4e0aa`) · `Editor`(`cb55af53`) ·
`Tools/ReflectionParser`(아래).

**ReflectionParser — 죽은 X-macro 사본을 없애고 재발을 검사로 막았다**
- `.xxx` 목록 파일 **사본 6개**가 아무도 include 하지 않는 상태로 있었다. 이게 왜 나쁜지는
  내가 직접 겪었다 — 창 크기 버그를 고치려고 `Source/Core/CommandLine/ArgumentList.xxx` 를
  수정했는데 빌드 결과가 바뀌지 않았다. 실제로 include 되는 것은
  `Source/Core/Predefined/ArgumentList.xxx` 였고, 죽은 쪽은 폴더 이름상 먼저 찾게 되는 자리에
  있으면서 `LANGUAGE`·`BAKE_SHADERS` 가 빠진 낡은 상태였다.
- 지운 것: `Core/Predefined/PredefinedEnumBitFlagTags.xxx`,
  `Engine/Reflection/PredefinedEnumBitFlagTags.xxx`(둘 다 `REGISTER_ENUM_BITFLAG_TAG` 를 쓰는
  곳이 없다 — 비트플래그는 이제 명시 애노테이션과 2의 거듭제곱 자동 판정으로 잡는다),
  `Engine/Reflection/PredefinedContainerKind.xxx`, `Engine/Reflection/PredefinedFunctionNetRole.xxx`,
  `Tools/ReflectionParser/PredefinedAnnotationKind.xxx`(전부 `Core/Predefined/` 쪽을 include 한다),
  `Config/Reflection/AnnotationMeta.txt`(CMake·Constants.py 둘 다 `Source/Core/Predefined/` 를 쓴다
  — 이 폴더는 비어서 사라졌다).
- `Scripts/lint/gate/CheckDataFileReferences.py` 신설 + CTest `lint` 등록(이제 lint 6개). 규칙:
  `Source/**`·`Tools/**` 의 모든 `.xxx` 는 include 또는 경로 참조가 하나는 있어야 한다.
  음성 테스트로 확인했다 — 죽은 사본을 되살리면 실패한다.
  (그 과정에서 검사 스크립트의 **독스트링에 적은 예시 경로**가 참조로 집계되어 한 번 통과해
  버렸다. 자기 자신은 세지 않게 고쳤다.)
- lint 경로 상수는 `Scripts/common/Constants.py` 와 `Scripts/setup/GenerateCMakeConstants.py` 에
  넣어야 한다. **생성물은 `build/<preset>/generated/sw/config/ConfigVars.cmake` 로 나온다** —
  저장소 안이 아니다. (2026-09-14 정정: 여기 `cmake/Engine/GeneratedConstants.cmake` 라고 적혀
  있었는데, 그 파일은 아무도 include 하지 않는 **커밋된 사본**이었다. configure 가 지우지도,
  다시 만들지도 않았다. 지웠다.)
- ReflectionParser README 의 파일 트리가 죽은 사본을 싣고 실제 파일
  (`PredefinedAnnotationField.xxx`)은 빠뜨리고 있었다 — 고쳤다.

**Editor — README 가 이미 정확했다. 고칠 것은 하나였다**
- `Source/Editor/README.md` 는 "어디에 두나" 표(ImGui 그림 / 상태만 / 바꾸거나 읽고 쓰기)까지
  갖춘 이 저장소에서 가장 잘 적힌 문서다. 폴더 구조도 실제와 맞는다 — 손대지 않았다.
- `HierarchyPanel` 이 계층 뱃지를 고르려고 타입 **이름** 7개를 if/else 로 비교하고 있었다
  (`UnitStatsComponent`, `HPBarBaseComponent` 처럼 게임플레이 타입까지 포함). **같은 파일이
  215행에서는 이미 `TypeInfo::getCategory()` 로 "컴포넌트 추가" 메뉴를 묶고 있었다** — 데이터는
  있는데 한쪽만 안 쓰고 있었다. `EditorUtil::appendCategoryBadge` 로 옮기고 Category 에서
  끌어온다. 등록된 어떤 컴포넌트든(게임이 만든 것 포함) 뱃지가 붙고, 같은 Category 는 한 번만
  나온다. `EditorTest.HierarchyBadgeComesFromReflectionCategory` 로 규약을 고정했다.
  (뱃지 글자가 `[Cam]` → `[Camera]`, `[Mesh]` → `[Rendering 3D]` 로 길어진다. 임의의 약어표를
  새로 만드는 것보다 리플렉션에 적힌 값을 그대로 쓰는 편이 낫다고 봤다.)
- `EditorViewportPreview::isDialogueRunnerType` 의 이름 비교는 **남겼다.** 에디터는
  GameFramework 를 링크할 수 없으므로 리플렉션으로 찾는 것이 정해진 탈출구다(짧은 이름과 FQN 을
  둘 다 본다). 결함이 아니다.
- **하지 않은 것**: 공용 위젯 채택(1-1)·목록형 패널 골격. 이 둘은 검증 수단이 **화면을 보는
  것**뿐이고(0절), 나는 종료 코드와 `[Error]` 개수만 볼 수 있다. 패널을 빈 화면으로 만들어 놓고도
  통과했다고 보고할 수 있는 작업이라 손대지 않았다. 사람이 띄워 보면서 한 패널씩 가는 편이 맞다.

**Games — 온보딩 안내가 존재하지 않는 전역 변수를 가리키고 있었다**
- README 가 `kGameFrameworkModuleName` · `kGameKitModules` · `kGameModuleName` 세 전역 변수로
  핫리로드 대상을 설명했다. **코드베이스에 그 이름은 하나도 없다.** 정본은
  `Config/App/AppConfig.json` 의 `_listGameKitModule` 이다. "새 게임 추가" 5단계 중 5번을
  그대로 따르면 아무 일도 일어나지 않았다.
- `Empty` 템플릿의 95% 가 메시 벤치 하네스였다(375줄 중 ~340줄). README 1번이 "이 폴더를
  복사하라" 이므로 새 게임은 벤치를 같이 들고 시작했다 → `BenchScene.{h,cpp}` 로 떼어내
  `EmptyGame` 을 ~40줄 템플릿으로 되돌렸다. 지울 경계를 파일 경계와 맞췄다.
  `Scripts/dev/BackendSmoke.py` 가 `-gv_benchMeshes` 에 기대므로 타깃·플래그는 그대로 두었다.

**GameFramework — 리플렉션이 조용히 누락되는 덫 둘, 그리고 키트 소속 기준이 없던 것**
- 리플렉션 대상 헤더를 소스와 **다른 규칙**으로 모으고 있었다. `GameFramework` 는
  `Base`/`Data`/`Transition`/`UI` 네 폴더를 **이름으로** 적어 두었고(소스는 재귀 GLOB),
  키트는 루트의 `*.h` 만 모았다(소스는 `GLOB_RECURSE`). 최상위 폴더나 키트 하위 폴더를 새로
  만들면 그 안의 `REFLECT()` 타입이 **컴파일은 되고 등록만 안 되는** 상태가 된다 — 역직렬화가
  조용히 기본값으로 떨어지므로 원인 찾기가 어렵다. 키트는 `sw_addReflectionStep` 의 자동 탐색에
  맡기고(재귀 + 매크로 필터), GameFramework 는 소스와 같은 재귀 GLOB + `Kits/` 제외로 맞췄다.
- 키트 소속 기준을 README 에 적었다. **의존 관계로는 판별되지 않는다** — 키트 컴포넌트는 전부
  `Engine` 만 include 해서 컴파일러에는 어디든 같다. 그래서 `HPBarBaseComponent`,
  `DamageUIComponent`, `GravityComponent` 가 `Kits/ActionCombat` 에 있었다. HP 바와 데미지
  숫자는 턴제도 쓰고 중력은 플랫포머도 쓴다 → `UI/` · `Base/` 로 옮겼다.
- README 의 폴더 목록이 낡아 있었다(`Events`/`Save` 폴더는 없다).
- **다음 차례(Editor)에서 같이 볼 것**: `HierarchyPanel.cpp:399` 가 컴포넌트 타입 이름 7개를
  if/else 로 하드코딩해 계층 뱃지를 고른다(`UnitStatsComponent`, `HPBarBaseComponent` 포함).
  에디터가 게임플레이 컴포넌트 이름을 알고 있고, 다른 게임의 컴포넌트는 뱃지가 없다.
  `REFLECT( Category = "UI" )` 메타데이터가 이미 있으니 거기서 끌어내면 if/else 가 사라진다.

**Engine — 문서에 적힌 레이어 순서가 코드와 달랐다**
- `Audio/XAudio2System` 이 모든 플랫폼에서 컴파일되며 `#if` 22개로 몸통을 비우고 있었다.
  `IAudioSystem::create()` 는 이미 Windows 에서만 이 클래스를 만드는데도 그랬다
  → `Audio/Windows/` 로 옮기고 파일 전체 가드 1개로(= `Window/Windows`·`Input/Windows` 형태).
- `Utility/Module` → `Module` 로 승격. LiveReloadManager 가 로드된 모든 Scene 의
  GameObjectManager 를 다시 묶는 **상위 서브시스템**인데 최하위 티어 폴더에 있었다.
- `ResourcePackReader` 의 플랫폼 분기 5벌을 Core 의 `PlatformFileUtil` 로 (Core 차례의 남은 일).
- DX11 `isHazardMessage` 의 `switch` 가 `-Wswitch-enum` 경고를 냈다(1328개 중 10개만 다룸)
  → 목록 순회로. 경고를 억누르지 않고 없앴다.

**`CheckEngineLayers` 의 내부 레이어 규칙을 근거 있는 것으로 바꿨다.**
예전에는 손으로 고른 네 쌍(`Utility->Graphics` 등)만 **경고로 찍고 실패시키지 않았다**.
지금은 include 그래프를 Tarjan SCC 로 줄여 얻은 티어 표를 쓰고, 위반은 실패다.

> ### Engine 코어 묶음 — 열에서 일곱으로 줄였다 (남은 것은 컴포넌트 모델 설계)
>
> **해결:** `Reflection`·`Serialization`·`Config` 가 묶음에서 빠졌다. 원인은 세 줄이었다.
>
> - 직렬화기가 `TagID`·`ComponentHandle` 때문에 `Object` 를 include 했다. 두 타입 모두 **Core
>   기능만 쓰는 값 타입**인데 `Object/Component/` 에 분류되어 있었다 → `Core/String/TagID.h`,
>   `Core/Container/ComponentHandle.h`(형제 `ObjectHandle` 옆). 이 엣지 하나로 10→8 이 됐다.
> - `Reflection` 이 `ReflectAny`·`Rpc` 의 **인코딩** 때문에 `Serialization` 을 include 했다 →
>   규칙 하나로 정리: **리플렉션 타입의 인코딩은 Serialization 이 갖는다**
>   (`SerializeReflectAny.cpp`, `SerializeReflectionRpc.cpp`). 선언은 Reflection 에 남는다.
> - `EngineConfig` 가 `RHIBackend` **이름 하나** 때문에 `RHITypes.h`(732줄)를 전부 끌어왔다 →
>   `Config/RHIBackendType.h`. "어느 백엔드를 쓰는가" 는 설정값이고 `Graphics` 는 이미 `Config` 를
>   참조한다(14곳). 8→7.
>
> 이제 아래 절반이 **완전히 정렬**된다: Common/Physics/Utility → Reflection → Serialization →
> Config → 코어 7 → Input → 루트. `CheckEngineLayers` 의 티어 표를 그대로 갱신했고 위반은 0이다
> (음성 테스트로 잡히는 것도 확인했다).
>
> **남은 7 묶음은 성격이 다르다** — 컴포넌트가 머티리얼을 들고 씬이 에셋을 읽는 것은 컴포넌트
> 모델 자체의 설계다. 측정된 엣지:

> ### (옛 측정) Engine 코어 열 폴더는 하나의 강결합 묶음이다
>
> `Config` `Graphics` `Module` `Object` `Reflection` `Resource` `Scene` `Sequencer`
> `Serialization` `Window` 이 서로 도달 가능하다. README 가 주장했던 5단 순서는 **사실이
> 아니었다.** 묶음 안의 엣지 수(측정값, 소수 방향이 고칠 후보):
>
> | 엣지 | 수 | 내용 |
> |---|---|---|
> | `Object -> Graphics` | 3 | `MeshComponent` 가 Material·Mesh·RHITypes 를 든다 |
> | ~~`Scene -> Graphics`~~ | ~~5~~ | **2026-09-13 해소** — 아무도 안 부르던 `Scene::render` 를 걷어내니 남은 것은 `MaterialCache` 하나다 |
> | `Reflection -> Serialization` | 4 | `ReflectAny.cpp` 가 직렬화기를 부른다 |
> | `Serialization -> Object` | 3 | `SerializeContext`·`SchemaMigrate` 가 TagSystem·ComponentHandle 을 안다 |
> | `Object -> Scene` | 6 | `ComponentPtr.cpp` 가 SceneManager 로 핸들을 푼다 |
> | `Object -> Sequencer` | 3 | `SequencePlayerComponent` (반대 방향도 3) |
> | `Config -> Graphics` | 1 | `EngineConfig` 가 `RHIBackend` 열거형을 든다 |
> | ~~`Graphics`/`Resource` `-> Module`~~ | ~~3~~ | **2026-09-12 해소** — `ReloadFileManager` 는 에디터 소유가 됐다 |
>
> 풀어내는 순서 제안(작은 것부터, 각각 독립): ① `Config -> Graphics` — `RHIBackend` 를
> `Config` 나 더 아래로 내린다. ② `Serialization -> Object` — 컴포넌트 핸들 해석을 콜백으로
> 받는다. ③ `Reflection -> Serialization` — `ReflectAny` 의 직렬화를 등록 가능한 훅으로.
> ④ 나머지(`Object`/`Scene` ↔ `Graphics`)는 컴포넌트 모델 자체의 설계라 별개의 큰 일이다.

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
  게임 뷰포트와 씬 틱 여부는 `updateEditorUi`(에디터가 입력을 처리한 이후)다. 후자를 프레임 앞으로
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
