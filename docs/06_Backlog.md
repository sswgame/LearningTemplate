# 작업 백로그 — 남은 일과 이어받기

> 목적: 여러 PC·여러 세션에서 이어서 작업하기 위한 **단일 할 일 목록**이다. 무엇이 끝났고
> 무엇이 남았는지, 남은 것을 왜 그 순서로 두었는지, 손대기 전에 알아야 할 함정이 무엇인지를
> 여기 적는다. 작업을 끝내면 이 문서의 해당 항목을 지우거나 "완료"로 옮기고 같이 커밋한다.
>
> 마지막 갱신: 2026-09-12 · 기준 커밋 `3b2d9920`

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

현재 기준선 (2026-09-11, 시작 씬 = 테스트 씬): **기본 창 15개 · 내용 없는 패널 0개**, 전부 열면 **창 29개 ·
내용 없는 패널 0개**. (시작 씬이 없던 때는 기본 창 14개였다.)

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
py -3 Scripts/lint/RunClangTidy.py
py -3 Scripts/lint/RunClangTidy.py --filter Core

# ASan (Windows) — 2026-09-09 부터 실제로 빌드된다
cmake --preset Ninja-Debug-ASAN
cmake --build --preset Ninja-Debug-ASAN
ctest --test-dir build/Ninja-Debug-ASAN -L nogpu

# 테스트 (현재 기준선)
#   Debug    : CoreTest 171 / EngineTest 435 / ReflectionTest 100(+1 skip) / EditorTest 51 / SmokeTest 19
#   Shipping : 163(+8 skip) / 396(+2 skip) / 96(+5 skip) / 51 / 1   ← 스킵은 전부 Dev 전용 케이스
#   (2026-09-11 실측. Debug EngineTest 는 GPU 포함 전체 수이고 ctest 의 EngineTest_NoGPU 는 398,
#    Shipping 의 396(+2 skip)도 그 NoGPU 수다 — Shipping 은 GPU 스위트를 애초에 돌리지 않는다.
#    409 → 398 은 실제 디바이스를 만드는 14 개를 `RenderPassGpuTest` 스위트로 갈라 뺀 결과다.)
#   WSL-Debug: ctest 12/12 (린트 6 포함). EngineTest 는 418 통과 + 8 skip = 426 이고,
#              스킵은 DX11/DX12 처럼 리눅스에 아예 없는 타깃들이다. (2026-09-11 실측)
#   ASan     : 5개 전부 통과한다(30초). SmokeTest 는 2026-09-10 부터 다시 돈다 — 아래 3절 참고.
#   ReflectionTest 의 스킵 1건은 Shipping·Debug 공통이다 — Bin/ 에 ReflectionParser.exe 가 없으면
#   ReflectionParser.MultiBitBitfieldCompilationErrorDiagnosis 가 스스로 빠진다(실패가 아니다).
ctest --test-dir build/Ninja-Debug -L nogpu
ctest --test-dir build/Ninja-Shipping -L nogpu

# 에디터 실기동 — 패널 변경의 유일한 실질 검증 수단
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

`py -3 Scripts/lint/RunClangTidy.py` 를 쓴다. 두 PC 가 같은 날 같은 코드를 훑고 **"0건" 과 "72건"**
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

---

## 3. 최근에 끝낸 일 (2026-09-08 ~ 12)

무엇을 이미 해결했는지 알아야 같은 것을 다시 파지 않는다.

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
(`freeMemory` · `freeSrvDescriptor` · `freeNode`), `unload*` 는 전부 `load*` 와 짝이다. `was*` 47 건은
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

**복합 접미사가 한 번도 안 걸리고 있었다.** `ReloadFileManager::extensionAllowed` 가
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

원인은 draw 스냅샷 프로토콜의 빠진 전이다. `_inFlightDrawSlot` 은 `updateUI`(UI 스레드)가
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

**대신 `Scripts/lint/FormatBranchBraces.py` 를 만들었다** (`FormatForwardDeclarations.py` 와 같은
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
  (`registerBindlessTexture` / `registerBindlessResource` / `registerBindlessUAV`, 해제는 짝을 맞춰야 한다).
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
- `XmlNode::attrInt/attrFloat` 의 반환값 무시는 fallback 선주입 관용구라 정상. `Archive::readBytes` 는
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
- `Scripts/lint/RunClangTidy.py` — 한 명령으로 같은 설정. 가장 큰 것은 **`/Y-` 로 MSVC PCH 옵션을
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
- `Scripts/lint/CheckDataFileReferences.py` 신설 + CTest `lint` 등록(이제 lint 6개). 규칙:
  `Source/**`·`Tools/**` 의 모든 `.xxx` 는 include 또는 경로 참조가 하나는 있어야 한다.
  음성 테스트로 확인했다 — 죽은 사본을 되살리면 실패한다.
  (그 과정에서 검사 스크립트의 **독스트링에 적은 예시 경로**가 참조로 집계되어 한 번 통과해
  버렸다. 자기 자신은 세지 않게 고쳤다.)
- `cmake/Engine/GeneratedConstants.cmake` 는 **자동 생성물**이다. lint 경로 상수는
  `Scripts/common/Constants.py` 와 `Scripts/setup/GenerateCMakeConstants.py` 에 넣어야 한다 —
  생성물을 직접 고치면 다음 configure 가 지운다(이것도 한 번 겪었다).
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
> | `Scene -> Graphics` | 5 | `Scene.cpp` 가 FrameRenderer·MaterialCache·IRHIDevice 를 부른다 |
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
