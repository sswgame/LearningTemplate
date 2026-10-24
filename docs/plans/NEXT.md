# 다음 세션 인계

> 질문: 지금 어디까지 왔고, 다음 세션이 처음에 할 일은 무엇인가?
> 독자: 새로 시작하는 세션(에이전트와 사람).
> 결론: main 은 통합 G 와 그 검증(Debug `nogpu` · `lint` · `race` 통과, Release 쿠킹 통과)까지 끝났고 GPU 확인에서 글자 깨짐 · `_roughness` Error · 에디터 시나리오 위젯 미표시 같은 결함이 나왔다. 그 수정과 정리 브랜치 열 개가 아직 main 에 안 들어갔다(`## 000000`, 병합 순서 포함). 그다음이 아키텍처 이음매와 리팩토링 트랙([ArchitectureSeams](ArchitectureSeams.md))이다.

이 문서는 지금 상태만 둡니다. 한 일은 `git log` 가 원본이고, 설계는 계획 문서, 순서는 [Roadmap](Roadmap.md)에 있습니다.

| 알고 싶은 것 | 어디 |
|---|---|
| 남은 일 한 건 한 건 | 후보 문서 `Candidates-<영역>.md` — `py -3 -m Scripts plans --area <영역>` |
| 설계와 구현 단위 | 계획 문서 — `py -3 -m Scripts plans`, `--file <이름>` |
| 앞으로 할 일의 순서 | [Roadmap](Roadmap.md) |
| 결정과 근거 | [결정 기록](../09_Decisions.md) |
| 작업 방식 · 검증 | [작업 방식](../11_Workflow.md), [검증과 측정](../08_Verification.md) |
| 에이전트에게 맡길 때 · 토큰 | [에이전트와 토큰](../12_Agents.md), [작업자 규칙](WorkerRules.md) |
| 환경 · 도구 함정 | [함정](../14_Pitfalls.md) |

## 000000. 2026-10-25 통합 H 대기 — 수정 · 정리 브랜치 열이 main 에 안 들어감 (이것부터 읽는다)

**먼저 볼 것 — main CI 가 졌다**: 실행 40121848026(`954666ef1`, 통합 H 검증 뒤 push)에서 Lints 는 통과했지만 빌드 잡 아홉이 졌다. Linux 빌드 다섯(Shipping · Debug Wayland · Debug · Dedicated Server Shipping · ASan)은 컴파일 단계에서, 로그에 `Test/CoreTest/Common/TestLowLevelReport.cpp:48` 의 `return dup( descriptor );` 가 찍혀 있다(`lowlevellog` 가 들인 테스트 — Linux 에서 `dup` 선언 헤더 `unistd.h` 가 빠진 것으로 보이나 로그를 끝까지 읽지 않았다). Windows 는 컴파일은 되고 ctest 가 졌다(Shipping 5 건 · STL containers 1 건 · Dedicated Server Shipping 4 건, 시한 초과 아님). Linux TSan 은 Setup 단계 시한(exit 124). Windows Debug 는 초록. `py -3 -m Scripts ci-jobs 40121848026 --annotations` 로 잡마다 원인을 읽고, 위 병합 전에 이것부터 고친다(통합 H 의 Debug · Release 검증은 Linux · Shipping 컴파일을 안 봤다). 이번 세션의 작업자 브랜치들이 같은 테스트 파일을 건드렸는지(`lowlevelgaps` · `scenlogs` · `logids`)도 본다.

**상태**: main 은 `1e94e0381`(문서 커밋 셋이 `954666ef1` 위에 있다) 이고 push 하지 않았다. 아래 브랜치는 모두 `D:/w/<이름>` 워크트리에 커밋만 있고(린트 통과, 빌드 · 테스트 · 실행 안 함) 병합 전이다. 작업자 보고는 각 브랜치의 `git log main..wt/<이름>` 과 커밋 메시지가 원본이다.

| 브랜치 | 내용 | 통합이 볼 것 |
|---|---|---|
| `wt/glyphbudget` | 글자 깨짐의 근본 원인: 프로파일러가 꺼진 기본 설정에서 `FrameProfiler::getFrameCount()` 가 늘 0 이라 글리프 래스터 예산이 프레임마다 되살아나지 않았다. 프레임 시계를 `GlyphCache` 가 쥐고 `Engine::updateFrameServices` 가 올린다. 탐침 `UI.DeferredGlyphs`, `uidemo` 시나리오가 글리프가 다 채워지길 기다린다 | 컴파일(`WidgetProbes.cpp`), `WaitUntil` 시한 초과가 종료 14 가 아니라 실패인지, Shooter3D HUD 프레임 3 · 300 |
| `wt/roughnessparam` | `MaterialScalar` 드라이브가 스칼라 파라미터에 float4 글을 써서 `_roughness` Error 299 회. `MaterialInstance` 가 모양이 어긋나면 `Material.ParameterShapeMismatch` 로 거절 | Water · Terrain · Foliage · Environment 에서 새 Error 가 없는지 |
| `wt/editorscen` | 에디터 시나리오 위젯 표식: 입력 상태를 서브시스템이 쥐게 한 변경 뒤 `setEnabled` 가 주인 없이 버려졌다. 상태를 `EditorStateStore` 로, `ContentWatchFence` 비교를 전체 경로로, `editor.meshmodifiers` 한 프레임 겹침 분리 | 에디터 시나리오 8 개 단독, `EditorScenariosPassOn<백엔드>` 체인 |
| `wt/scenfixes` | `X11DevConsoleTest` 빈 스위트, `nilecity.pause`(가상 커서가 마우스 오른쪽으로 새던 것 — `UIInputConsumption::isSlotConsumed`), HUD 이름 불일치. **`shooter3d.crowd` 는 시나리오 단언만 `WaitUntil` 로 바꿨고 근본 원인은 못 찾았다** | `nilecity` pause · control · gamepadcursor, crowd 의 `WaitUntil` 시한 |
| `wt/scenlogs` | 시나리오 Warning/Error 강제: 종료 코드 15 `UnhandledLog`, 허용 목록 `allowedlogs.json`(id · 범위 · 사유 · 계획 필수), 래칫, `py -3 -m Scripts scenario-logs`. `KnownLog` 계열 삭제 | 컴파일(`HostLogJudge`), `hostgpu` 로 15 가 서는지, `--regen` 은 `--dry-run` 먼저 |
| `wt/logids` | `countContaining` 약 120 곳을 로그 id 로, 로그 소스 약 50 개 파일에 `SW_LOG_*_ID`, 후속 커밋으로 뭉뚱그린 기본 id 분리 | **`scenlogs` 와 충돌**: 둘 다 `KnownLog` 를 고쳤다 — `scenlogs` 기준(허용 목록)으로 풀고 `logids` 의 `KnownLog` 변경은 버린다. `registrylogs` 를 먼저 병합(`Registration.*` id) |
| `wt/registrylogs` | `RegistrationList::addLogged` · `NameRegistry::addLogged`, 손 로그 18 곳 통일, `add` 에 `[[nodiscard]]`, `addOrReplace` 빈 이름 거절 | `nodiscard` 경고가 남은 호출처, `getList()` 로 받는 호출 |
| `wt/apptestfeature` | `AppTestUtil` 이 필요 기능을 표로 확인, 구성 · 플랫폼 차이를 `SW_TEST_SKIP` 하던 22 곳을 `SW_TEST_REQUIRES_FEATURE` 로, `BuildFeature` 일곱 개 추가 | `TestCatalog.cpp` 컴파일(매크로 보임). **`scenfixes` 와 `TestCase.h` 열거형 끝 · `TestCatalog.cpp` 표 끝에서 충돌** — 둘 다 남긴다 |
| `wt/lowlevelgaps` | `SW_LOG_ASSERT` 가 멈추기 전에 stderr 에 쓰고, 싱크 없을 때 줄을 버리지 않는다. 후속(`registerGlobalListener` 빈 핸들 등)이 돌고 있었다 — 워크트리 `git log` 로 확인 | stderr 테스트 유무 |
| `wt/lowtier` | Core 낮은 티어를 `Core_low_objects` + `pchLow.h` 로 분리해 pch 로 새는 위 티어 선언을 컴파일 오류로. 규칙 4 의 `_ID` 변형 구멍을 고침. CMake 가 `CheckCoreLayers.toml` 의 티어를 직접 읽는다 | Debug · Release · Debug-Cold · 정적 Shipping · CI-Debug-STL 링크, `PoolAllocator.cpp` 에 `SW_LOG_ERROR` 임시로 넣어 실패 확인, 유니티 빌드 |
| `wt/gputhresholds` (`018f61df0`) | 게임 시나리오 11 개 문턱을 measured 로(잰 값의 70 % 하한 · 1.5 배 상한, `sky` R−B 는 atMost -0.004, `vfxgpulights` atLeast 0.009). `useroptions` dx12: DX12 PSO 의 `RasterizerState.DepthClipEnable` 이 0 이던 것을 TRUE 로(추정 — 코드 · 수치 근거). `shippingframe` 골든 8 어두워짐은 의도된 포워드 림 변경(`f81daa9ff`)이라 골든을 다시 뜬다(절차 `GpuVerification.md` 3.1, 매니페스트 `shipping-golden-dx12`). **gl 의 GPU 라이트는 원인을 못 찾았다**(쿠킹 SPIR-V 의 라이트 버퍼 레이아웃은 GL · Vulkan 같고 로그에 오류 없음 — 라이트 버퍼를 `AsyncBufferReadback` 으로 되읽는 것이 첫 단계. 부수: RW 이미지가 Rgba32f 로 선언되는데 깊이 히스토리는 R32F) | Release 네 백엔드에서 `useroptions` dx12 `distance` 가 0.665 근처로 오르는지(안 오르면 추정이 틀림) 후 그 값을 dx12 measured 에 더함, 골든 다시 뜨고 통과 확인, DepthClip 수정이 shadow 시나리오 · 벤치 골든을 바꾸는지. 같은 모양: 0 으로 시작하는 PSO 서술이 백엔드 기본값과 조용히 다른 칸이 더 있을 수 있다(래스터라이저 칸을 한 함수가 정하게 정리) |
| `wt/staticgate` · `wt/poseaudit` · `wt/staticleft` | 이전 묶음. 아래 "다음 묶음" 그대로 | `staticgate` 병합 뒤 `CheckMutableStatics --regen-baseline` |

**병합 순서**: ① `registrylogs` → ② `scenlogs` → ③ `logids`(`KnownLog` 충돌은 ② 기준) → ④ `apptestfeature` + `scenfixes`(열거형 · 표 충돌은 둘 다 남김) → ⑤ `lowlevelgaps` · `lowtier` → ⑥ `glyphbudget` · `roughnessparam` · `editorscen` · `gputhresholds` → ⑦ `staticgate` · `poseaudit` · `staticleft`. 통합은 새 main 위에 워크트리를 만들고 `-k 0` 빌드로 오류를 한 번에 모은 뒤 고친다. 빌드 · 검증 · 측정은 각각 한 번.

**GPU 확인에서 나온 것(main `00a6fc842`, Release · Shipping, 네 백엔드, `gpu-verifier` 보고)**
- 글자 깨짐은 재현 · 확정(위 `glyphbudget`). 자간은 정상(글자가 다 그려진 상태에서 SDF · MSDF · 백엔드 네 조합의 글자 시작 위치가 화소 단위로 같다). 사용자가 본 자간 이상은 글리프 누락이었다.
- CCTV 줄무늬는 결함이 아니다: `cctvmonitor.material` 의 `_scanlineCount=90`(UV 기준) 가로 주사선이 셰이더 의도대로 그려진다. 불만이면 줄 수 · 세기(데이터)를 바꾼다.
- 석궁은 Shooter3D 에 없다(소총 · 산탄총 · 권총뿐, 활은 다른 게임 영역). 3 인칭 카메라가 뒤라 무기를 못 봤다 — 측면 카메라 캡처가 필요하다. 사용자가 본 위치 이상이 어느 장면인지 다시 물을 것.
- 졌던 것: `ShippingSceneMatchesGoldenImage`(dx12, 중앙 상자 옆면이 균일하게 8 어두워짐 — 의도면 골든 갱신), `vfxgpulights`(문턱 틀림 + gl 은 GPU 라이트가 사실상 안 그려짐), `sky`(문턱 어림값 틀림), `useroptions` 의 `distance`(dx12 만 0.3073, 나머지 0.665), 에디터 시나리오 · 체인, `_roughness` Error, `crowd`(`Animation.CrowdSharedUnits >= 2, got 1`), `nilecity.pause`.
- 종료 코드 14(Unconfirmed) 14 개와 측정값은 `gputhresholds` 가 문턱으로 옮긴다(보고에 `metric` 값 표가 있었다).
- 지형 여덟 레이어 · 지오모프 전용 시나리오는 저장소에 없다 — 시나리오로 만든다.

**아직 못 고친 것**: `editor.charactereditor` 체인(`charactereditor.slider.last` 가 창 밖 — `CharacterEditorPanel.cpp` 의 첫 슬라이더에 `note`), `startAfter="Immediately"` 시나리오가 `ContentWatchFence` 를 못 막는 것(`AutomationRunner::onFrameBegin` 이 시작 차단을 무시), `shooter3d.crowd` 근본 원인, `scenlogs` 가 15 로 끝나게 만드는 **id 없는 `SW_LOG_WARNING/ERROR` 약 3,500 곳**(`scenario-logs --regen` 의 "id 없음" 표를 영역별로 나눠 소스에 `_ID` 를 붙인다 — 병렬 작업자), 한영 숫자 혼합에서 중립 문자가 앞 런의 글꼴을 이어받는 동작(의도로 정할지), 가짜 굵게가 레이아웃 폭을 안 바꿈, 한글 · 숫자 자간 측정, MSDF 베어링 단언, HarfBuzz 경로의 `.ttc` face index 0 고정(복합 문자 빌드를 켜면 문제).

**다음 세션에서 할 일(순서)**
1. 위 병합 순서대로 통합(`integrator`), 컴파일 → `nogpu` · `lint` · `race` → Release 쿠킹 → GPU 확인(`gpu-verifier`, 건너뜀 규칙과 "Warning/Error 는 통과가 아니다" 규칙을 지시문에 적는다).
2. [ArchitectureSeams](ArchitectureSeams.md)의 0 단계(측정, 통합 혼자)와 1a · 1b · 2a · 3 · 4(병렬 가능) 착수. 리팩토링 트랙은 같은 문서 3 절(중복 기능 통합 · 불필요한 래핑 걷기).
3. 후보로 올린 것: ① **`half` · `half2~4` 저장 전용 압축 타입**(+ `unorm` · `snorm` · 옥타헤드럴 법선 · `RGB10A2`, 이식 가능한 비트 연산 구현, 산술은 `float` 로 풀어서, 월드 좌표 · 큰 시간값에는 쓰지 않음을 문서화. 단위: Core/Math 타입과 테스트 → 직렬화 · 리플렉션 → 정점 · 인스턴스 데이터 적용. 결정 기록에 한 줄). ② **시나리오 단계 인자를 리플렉션 구조체로 선언**해 스키마를 자동으로 만들고 린트에서 시나리오 XML 을 검증, 탐침을 `Probe` 속성이 달린 프로퍼티에서 노출(속성을 단 것만 공개 계약). 단계 인자가 이미 스키마를 갖는지 먼저 코드로 확인한다. ③ `scenario-logs` 래칫을 `suite` 항목에도.
4. 사용자 결정은 [ArchitectureSeams](ArchitectureSeams.md) 4 절의 둘(GPU CI 자동화, 작업자 빌드 금지 규칙 재검토)뿐이다.

**교훈(이번 세션)**: ① 기본 설정에서만 깨지는 결함(프로파일러 꺼짐)은 단위 테스트가 프레임 번호를 직접 넣어 못 본다 — 기본 구성으로 도는 시나리오가 잡아야 한다. ② 종료 코드 0 · 14 만 보면 Error 가 있는 시나리오가 초록이었다(`scenlogs` 가 막는다). ③ 작업자가 단언을 완화하는 쪽(`crowd` 의 `WaitUntil`)으로 풀면 근본 원인이 남는다 — 보고에서 "원인을 못 찾았다" 를 놓치지 말 것. ④ 워크트리에는 `graphify-out/graph.json` 이 없다: MCP 에 `project_path="D:/Projects/Personal/LearningTemplate"` 를 넘기고, `add` 같은 흔한 이름은 그래프가 섞이니 grep 으로 보강. ⑤ 이 세션의 셸은 PATH 등록 전에 시작해 `graphify` 를 못 찾았다(사용자 PATH 에 `...\Python314\Scripts` 를 등록했으니 새 터미널부터 된다).

## 00000. 2026-10-25 통합 G — 브랜치 14 개가 main 에 들어감(push), 테스트 · 쿠킹 · GPU 대기 (이것부터 읽는다)

main 에 `wt/integG` 를 fast-forward 하고 push 했다. 검증은 **Debug `full-build` 경고 0 · 오류 0 과 `lint-suite` 까지만** 돌렸다(사용자가 범위를 줄임). GPU 확인만 남았다. 통합 H 가 Debug `ctest -L nogpu` 69 · `lint` 47 · `race` 5 통과, 바뀐 영역 테스트(Core 26/27 1 건너뜀, Engine 42/42)와 Release `--cook-shaders` · `--verify-shaders` 종료 0 을 확인했다. 진 것은 `AppTest_Race` 하나 — thresholdaudit 이 어림값 문턱 시나리오를 UNCONFIRMED 로 끝내게 했는데 `ChainedScenariosShareOneProcess` 가 `chain PASS` 만 찾았다(테스트를 고침). 같은 결함 가능성: `AppTestUtil` 밖에서 `PASS` 문구를 직접 찾는 테스트는 이것뿐이었으나, 시나리오 로그 문구를 grep 하는 Python 쪽 검사는 보지 않았다.

**들어간 브랜치(14)**: `lowlevellog` · `staticengine` · `registries` · `staticeditor` · `staticselftest` · `singletons2` · `cookfix` · `cookers2` · `plansclean` · `gfxfix`(`backendaudit` 에 포함) · `backendaudit` · `editorsc` · `shipscen` · `thresholdaudit`. 내용은 각 병합 커밋 제목과 브랜치 커밋이 원본이다.

**병합에서 고친 것**: ① `09_Decisions.md` 충돌 셋(정적 지역 상태 결정 둘과 단일체 게이트 줄은 양쪽을 남김, `plansclean` 이 지운 `RuleExceptionAudit` 링크는 `cookers2` 의 `-Wundef` 문장을 살려 뺌), `GpuVerification.md`(shipscen 2.1 절과 thresholdaudit 3 절을 둘 다 두고 절 번호 정리), README 진행 표는 `status-board --write` 로 다시 만듦. ② `sky` 2 개 · `vfxgpulights` 1 개의 `ExpectImage` 에 `provisional` 근거를 달았다(gfxfix 가 고친 무대 기준 어림값). ③ 컴파일 오류 둘 — `editorsc` 의 `noteInputFrame` 이 `staticselftest` 가 지운 `getState()` 를 불렀다(`findState()` 로), `lowlevellog` 의 `TestLowLevelReport` 중첩 구조체 기본 멤버 초기화 오류(이름공간의 `LowLevelReceived` 로 꺼냄).

**CI · 결정 · 미푸시**: ① push(`8767a5e18`)한 main 의 CI Lints 잡이 `CheckDocPaths` 로 졌다 — `docs/09_Decisions.md:250` 이 생성되는 폴더 `Source/Engine/Pch/` 를 경로로 적었다(로컬에는 있고 새 체크아웃에는 없음). 문장을 "새 pch 전용 폴더" 로 고쳐 커밋 · push 했다(이 절을 읽을 때 `py -3 -m Scripts ci-jobs --summary` 로 초록인지 먼저 본다; 계속 지면 `--annotations`). 교훈: 로컬에서만 있는 생성 폴더를 문서 경로로 적지 않는다. ② **사용자 결정 대기 없음**(정적 상태 게이트는 (b) 로 정해졌다). ③ 미푸시 변경 없음(이 절 이후 변경은 이 커밋에 포함). ④ 워크트리: 병합한 14 개는 지웠다. 남은 것은 `staticgate` · `poseaudit` · `staticleft`(다음 묶음) 뿐이다.

**병렬로 띄울 묶음(Roadmap 의 `[병렬 가능]`, 파일 소유가 겹치지 않을 때)**: 위 1~3 의 검증(통합 한 명)이 끝난 뒤에 — A. `staticgate` · `poseaudit` · `staticleft` 병합(통합 한 명, `-k 0` 빌드). B. 로그 id 이전(`countContaining` 약 120 곳) · 레지스트리 중복 로그 약 15 곳(작업자 둘, 파일이 다르다). C. 측정대기 문서 59 개의 판정(GPU 확인 뒤, `끝` 으로 올려 지우기 — `plans --check` 가 막는다). D. `Roadmap.md` 의 `[병렬 가능]` 항목(GpuVerification · Vfx 실행 확인 · 카메라 디렉터 · 미리보기 창 · 오디오 · 애니메이션 · 런타임 UI · 키트 조립 · Wayland 등)은 `py -3 -m Scripts plans` 와 Roadmap 에서 고르되 **작업자 여섯 안팎, 작업자는 빌드하지 않고 커밋만**.

**다음 세션이 처음에 할 일(순서대로)**
1. GPU 확인(`gpu-verifier`, Release `ctest -L hostgpu` · 네 백엔드): 아래 "졌던 것" 전부와 `RHIDeviceTest` 의 새 완료 계약 테스트(`backendaudit`). `sky` · `vfxgpulights` 등 `provisional` 문턱은 `metric` 줄로 `measured` 로 바꾼다(`GpuVerification` 3 절). 어림값이 맞으면 종료 코드 14(`Unconfirmed`)로 끝나 CI 는 초록이고 로그로만 구분된다.

**GPU 확인에서 졌던 것(main d679388a5, 위 브랜치가 고침)**: `sky`, `vfxgpulights`(gl 은 `Vfx.md` 의 "GL R32 실수 RW 텍스처 읽기" 가 후보), `RHIDeviceTest.AsyncBufferReadback…`(DX11), `shooter3d.crowd`, `nilecity.gamepadcursor`, 에디터 시나리오 6 개, Shipping `gv_firstScene` `LOAD ERROR` 18 건, Shipping nogpu 7 건, `ShippingSceneMatchesGoldenImage`(dx12 에서 32565 화소 초과 — 아직 안 봄). 지형 여덟 레이어 · 지오모프 전용 시나리오는 저장소에 없다 — 시나리오로 만든다.

**다음 묶음(끝났다고 보고 받음, 이번에 병합하지 않음 — 모두 컴파일 대기)**: 각 워크트리 `D:/w/<이름>` 의 `git log main..` 와 `git status` 로 본다.
- `staticgate`(커밋 `f0deeebc4` 게이트 + `c8f253333` 래칫 · lint-suite 켬): `CheckMutableStatics` 가 정적 저장 기간의 가변 변수를 모양으로 판정(`MutableStatic/Local` · `/Namespace` · `/Member`). **정책 (b)(사용자 결정, 2026-10-25)**: 신규 위반부터 막고 기존은 `Scripts/lint/rules/CheckMutableStatics.toml` 의 `[pending]` 파일별 위반 총수 상한(292 개 파일, 합 628 — 옛 main 기준)으로 줄여 간다(표에 없는 파일 · 상한 초과 · 줄었는데 표 그대로면 실패, 범주를 옮겨도 막힘). **병합 직후**: 이번에 들어간 `staticeditor` · `staticselftest` · `staticengine`(그리고 `staticleft`) 때문에 줄인 파일마다 "표를 줄여라" 로 실패한다(의도) — `py -3 -m Scripts gate CheckMutableStatics --regen-baseline` 을 한 번 돌려 커밋한다(약 542 건 남을 것). 부수 변경: `CppScope.py` 로 옮긴 `classifyHead` · `isLambdaOpen`, `CodeText.py` 의 원시 문자열 안 `{` 가림 결함 수정(전 게이트 공용 — 병합 뒤 lint-suite). 못 보는 모양: 괄호 초기화 변수 · `#define` 본문 · `struct S{…} g;`.
- `poseaudit`(커밋 7 개): `getPoseAccess` 가 늘 `Writes` 이던 구현체(후처리 리그 · 방향 보정 · 리타깃 · 얼굴 · 체형 비율)가 실제 쓰기와 같은 판정을 쓴다. Debug 가드(`SkeletalMeshComponent::runTasks` 가 `ReadsOnly` 전후 포즈 지문 비교 → `SW_LOG_ASSERT`), 새 테스트 `AnimationPoseAccessTest` 6 건, 매니페스트 `pose-access-test`. 볼 것: 래그돌 · 군중 · 리그 · 리타깃 · 얼굴 테스트에서 새 단언, FacialRig 최소 XML, 지문이 `float3` · `quaternion` 패딩 없음을 가정.
- `staticleft`(`wt/staticeditor` 기준, 커밋 6 개 — `staticeditor` 가 main 에 들어갔으니 그대로 병합된다): `EditorSettingsInstance<T>` → `EditorSettingsRegistrar<T>`(등록 안 된 섹션 읽기는 로그 후 abort), `ScopeOwned` 11 곳, X11 `_s_active` → `ImGuiIO::BackendPlatformUserData`, `EditorChrome` `thread_local` 7 개, `setMeshModifierTaskManager` 삭제. 컴파일 위험: `LifetimeScope.h` include 를 지워 전이 include 가 빠진 파일, `EditorSettingsRegistration` 의 `mutable uint8` 과 집합 초기화, X11 가드 파일.
- `lowtier`(`wt/lowtier`, 컴파일 대기 — `LowLevelReportingGaps` (3)): Core 를 `Core_objects`(높은 티어) · `Core_low_objects`(티어 표 `Log` 미만 폴더, `pchLow.h` — Logger · File · String 없음)로 나눴다. CMake 가 `CheckCoreLayers.toml` 의 `[tier]` 를 직접 읽어 가르고 Engine 은 `Core` 의 `SW_CORE_OBJECTS` 속성을 읽는다. 린트 규칙 4 는 헤더 방어로 남고 `pchLow.h` = `CoreMinimal.h` − Log 이상 헤더 검사(규칙 5)가 늘었다. **통합이 볼 것**: Ninja-Debug · Release · Debug-Cold(pch 끔) · 정적 Shipping · CI-Debug-STL 의 Core · Engine · ReflectionParser 링크(낮은 `.cpp` 가 pch 로 암묵 받던 이름이 있으면 컴파일 오류 — 그 파일에 정당한 include 를 더한다), `Memory/PoolAllocator.cpp` 에 `SW_LOG_ERROR` 한 줄을 임시로 넣어 컴파일 실패 확인, `Logger.h` 수정 시 재컴파일 TU 수(낮은 쪽 약 26 개가 빠진다). 다 되면 후보 절 `LowLevelReportingGaps` 에서 (3)을 지운다.

**아직 시작하지 않은 것**: ① 남은 `countContaining` 약 120 곳과 `HostLogJudge` 의 `KnownLog` 부분 문자열을 로그 id 로. ② (끝) 공용 형식을 쓰면서 중복 로그를 손으로 쓰던 레지스트리 18 곳을 `RegistrationList::addLogged` · `NameRegistry::addLogged` 로 옮겼다(로그 id `Registration.DuplicateName` · `EmptyName` · `NullItem`, 문구는 `<대상> '<이름>' is already registered - keeping the first`). 테스트의 옛 문구 검사는 이 id 로 옮긴다. `add` 는 `[[nodiscard]]` 라 결과를 버릴 수 없고, 버리던 호출처와 `addOrReplace`(빈 이름은 `Registration.EmptyName`)도 `addLogged` 로 옮겼으며 `ServiceFactoryList` 의 중복 로그도 같은 보고 함수를 쓴다. 남은 틈: `OnlinePushRelay` · `OnlineProtocolRegistry` 의 정적 초기화 중 stderr 직접 쓰기와 abort 는 로거가 없을 때를 위해 두었다. ③ 측정대기 계획 문서 59 개의 판정(GPU 확인이 초록이어야 `끝` 으로 올려 지울 수 있다). ④ `SW_LOG_ASSERT` 가 멈추기 전에 동기로 쓰지 않는 것, `SW_LOG_*` 가 싱크 없을 때 줄을 버리는 것(후보 `LowLevelReportingGaps`). ⑤ `AppTestSupport` 가 Shipping 에서 안 도는 게임 케이스에 `SW_TEST_REQUIRES_FEATURE( SourceResources )` 가 빠졌는지. ⑥ `CheckServiceAccess` 를 `.cpp` 로 넓히기(남은 영역을 옮긴 뒤 — `staticgate` 몫).

**교훈**: 작업자가 빌드하지 않는 대신, 서로 다른 브랜치가 같은 함수를 하나는 지우고 하나는 새로 부르는 "의미 충돌" 은 텍스트 병합이 잡지 못하고 통합 빌드에서만 드러난다(이번 `getState()`). 통합은 `-k 0` 빌드로 오류를 한 번에 모은 뒤 고친다. 세션이 갑자기 끊기면 브랜치 참조 파일이 NUL 로 깨질 수 있다(`git fsck`, `.git/logs/refs/heads/wt/<이름>` 의 마지막 커밋으로 복구).

## 0000. 2026-10-25 통합 E · F — main 에 들어감, GPU 확인 대기

### 상태

- **main 에 들어감(미푸시)**: 통합 E(`wt/integD` 의 34 브랜치 + `wt/plangfx2`)와 통합 F(`wt/plangfx2` 의 지형 레이어 여덟 · 지오모프 d9e050414, `wt/shaderfmt3` 의 셰이더 필드 `_camelCase` 6c1416635)를 합쳐 main 을 fast-forward 했다. **push 는 하지 않았다.**
- **검증(Debug)**: `full-build` 경고 0 · 오류 0(증거 남음), `lint-suite` 46/46, `ctest -L nogpu|lint` 통과(CheckTextureFolders 는 동시 부하에서만 시한을 넘고 혼자 통과). 요청받은 GPU 장치 테스트 — `RenderPassGPUTest` · `MaterialTest` · `ShaderBindingValidatorTest` · `QaGoldenImages` · `AppTest_Race` 통과. 셰이더는 새로 빌드한 **Release App** 으로 구웠고 다시 구워도 커밋과 같다(`--verify-shaders` 통과). **GPU 확인 대기** — Shipping · `-L hostgpu` 전체 · 네 백엔드 × 대표 셋(Empty · 에디터 · Shooter3D · NileCity) · 새 시나리오(fog · sky · realtimegi · vfxgpulights · modulepass · 지형 여덟 레이어)는 돌리지 않았다.
- **통합이 고친 것**(커밋 메시지가 정본): E — 리플렉션 · 컴파일 · 링크(프로파일 매크로 창구, `make_unique<T>.createWith`, `basic_string::substr`), 셰이더 패치 01~04 적용 · 쿠킹, 린트, 자식 프로세스 상속 핸들 수명, 뗀 뒤 새 누름 소비, 고도 0 하늘, PassCB 768 넘침(슬롯 1024 · 넘치면 오류). F — shaderfmt3 규칙을 새 셰이더 · 지형(패치 05 포함) · 머티리얼 · C++ 이름에 입힘, `App -help`(표에서 만든 인자 목록을 찍고 끝냄 + AppHelpTest), Vulkan 구조 버퍼의 TRANSFER_SRC(차폐 컬링 되읽기), avatarsample_d 임포트 스탬프.
- **병합하지 않은 것**: `plangfx2` 워크트리에 남은 커밋하지 않은 변경(셰이더 원본 — 이미 패치로 들어간 것의 사본)은 버려도 된다. `lambdaser` · `singletonget` 의 커밋하지 않은 변경은 공유 stash(`stash@{0}` 망가진 정규식, `stash@{1}` 중복 줄)에 있다.

### 다음 세션이 처음에 할 일

1. **GPU 확인**: Shipping 빌드 → `-L hostgpu` → `verify-batch --owner <이름>` 의 Shipping 항목(새 시나리오 fog · sky · vfxgpulights · modulepass · realtimegi, `Empty` 렌더 테스트 씬 + Shooter3D × 네 백엔드). 실패하면 그 단위를 고치거나 되돌린다. 특히 이번에 처음 GPU 에서 도는 것: 볼륨 안개 컴퓨트, 대기 산란 하늘, 프로브 갱신 컴퓨트(`probeupdate.hlsl` — FXC 때문에 루프 모양을 바꿨다), GPU 파티클 라이트, 모듈 패스.
2. **push**: 통합 G 와 함께 GPU 확인 전에 push 됐다(사용자 결정) — GPU 확인에서 지는 단위는 고치거나 되돌린다.
3. **정리**: 공유 stash, 셰이더 주석의 옛 이름 `MaterialShaderHooks`.

### 남은 것

끝난 일은 `git log` 에 있고, 여기에는 안 끝난 결함 · 확인만 한 줄씩 둔다.

- **같은 모양이 남았을 곳(이번 통합에서 본 것)**: ① 셰이더 쿠킹이 새 퍼뮤테이션 해시로 다시 구울 때 옛 바이너리를 지우지 않아 `--verify-shaders` 가 진다(통합이 손으로 지웠다) — `ShaderCooker` 가 매니페스트에 없는 바이너리를 스스로 지우게 할 후보. ② `App --cook-shaders` 가 쿠킹 실패가 있어도 종료 코드 0 이다(폴백). ③ 정의되지 않은 매크로를 `#if` 로 보는 include(AuthorityGhost)는 `-Wundef` 가 잡는다 — 켤지 정한다. ④ 로그 문장을 맞추는 테스트(`countContaining("…")`)는 로그 문구를 바꾸면 진다 — 로그 id 로 맞출 후보. ⑤ `TestResourceDataSchema` 의 종류 표는 엔진 등록표(`SW_RESOURCE_DATA_KIND`)의 사본이다(게임 파이프라인 XML 을 몰랐다).
- **싱글턴 · 람다**: `ResourceDataKindRegistry` 단일체(후보 `SingletonGetToServices`), 싱글턴 `get()` 게이트가 `instance()` · `shared()` 이름을 못 봄.
- **누수 탐지**: 설계 검토의 2 ~ 4단계([LeakDetection](LeakDetection.md)).
- **레지스트리 · 파서**: 이름을 손으로 거절하는 레지스트리(`LocalStoreFactory` · `SQLDriverRegistry` · `LoginService` · `ModuleRenderPassRegistry` · `EditorAssetTypeActions`)는 `RegistrationList` 후보, 손 불리언 파서는 `StringUtil::tryParseBool` 후보, `ElementGrid.cpp` 의 `cellBytes` 손 계산.
- **셰이더 확인**: 툰이 프로브(간접 조명)를 받지 않는 것이 의도인지.
- **층 게이트**: GameFramework 표에 "표 = 계산값" 검사를 붙일지, Bootstrap 13(계산값 9)이 의도인지.
- **확인 항목**: 클라이언트 근접 피해 소실(meleeunify) · 활의 가는 줄과 투사체 방향 · `PoseModifierComponent::setSocketTable` 호출처 없음([ShooterCharacterIntegration](ShooterCharacterIntegration.md)).
- **CI**: 야간 `Load (Linux | Release, nightly)` 실패의 근본 원인을 모른다(후보 `DiskPathVsResourceId`).
- **시나리오**: 게임 시나리오가 `Resource/` 에 쓰는지 점검하지 않았다. 클릭한 순간의 방향으로 사격이 나가는지는 시나리오로 보지 못했다.
