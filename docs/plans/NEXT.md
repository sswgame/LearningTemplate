# 다음 세션 인계

> 질문: 지금 어디까지 왔고, 다음 세션이 처음에 할 일은 무엇인가?
> 독자: 새로 시작하는 세션(에이전트와 사람).
> 결론: main 은 통합 K 위에 통합 L(브랜치 24 개 + RHI 전송 B0)과 통합 M(CI 실패 수정 · 창 핸들 try 접근 · 프레임 경로 전송 비동기화)을 검증해 넣고 push 했고, 그 위에 **통합 N(RHI 후속 브랜치 19 개, Vulkan 전송 비동기화 U6 포함)을 검증해 fast-forward 했다 — push 는 사용자 확인 대기**(`## 0000000000000`). 다음 세션의 첫 일은 ① 사용자가 허락하면 push 하고 CI 결과 확인(`py -3 -m Scripts ci-jobs --summary`; 리눅스 Wayland 스모크 · 클립보드 INCR · TSan 은 이때 처음 돈다), ② main 에서도 지는 시나리오 둘(`shooter3d.crowd`, GL `vfxgpulights`)의 원인, ③ 작업자 브랜치 체인을 `integ-o` 로(`## 00`), ④ 프레임 경로 남은 대기 U8 · U9(전송 감시 목록)다. 아키텍처 이음매 5a 는 그 뒤 조용할 때다.

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

## 0000000000000. 2026-10-25 통합 N — RHI 후속 브랜치 19 개가 main 에 들어감(push 안 함, 사용자 확인 대기) (이것부터 읽는다)

**상태**: `wt/integ-n` 을 main 으로 fast-forward 했다. **push 하지 않았다** — 사용자에게 먼저 묻는다. 들어간 브랜치: 선행 6 개(`dx11ctx` · `glpbo` · `glpbo2` · `glrb` · `cifixl5` · `vkwsi`)와 `dx11up` · `dx11rb` · `dx11ctx2` · `gltsflag` · `lostrej` · `dx11loss` · `glrb2` · `glrb3` · `bigup` · `vku6` · `bbfmt` · `gvnum` · `gvnum2`. RHI ABI 는 41 이다(병합마다 38 → 41).

**병합하며 고친 것(통합 커밋)**
- `dx11ctx2` × `cifixl5`: `withNativeContext` 를 `withNativeContext( RHIBackend 기대 백엔드, 콜백 )` 의 `[[nodiscard]] bool` 로 합쳤다(종류 확인 결정과 잠금 콜백 결정을 한 함수에). ImGui 네 백엔드는 디바이스를 `queryNativeHandles`, 컨텍스트를 이 함수로 받는다. `RHINativeHandlesTest` 에 다른 백엔드 거절을 더했다.
- `vku6` × `glrb2` · `glrb3` · `bigup`: Vulkan 링 장부를 `RHIStagingAllocator` 로, 링보다 큰 한 장을 일회성 호스트 가시 버퍼(`VulkanStagingRing::_soloBuffer`, `_soloMutex`)로 받게 했고 되읽기 링은 `gv_rhiReadbackRingMegabytes` 를 따른다. `RHIStagingRingGPUTest.VulkanTransferLargerThanTheRingIsAcceptedAlone` 를 더했다.
- GPU 확인에서 드러난 결함 둘: DX11 `takeTextureReadback` 의 `Map( DO_NOT_WAIT )` 가 끝난 복사에도 `WAS_STILL_DRAWING` 을 돌려 면 되읽기가 졌다 → 기다리는 `Map`([결정 5-5](../decisions/5-5_GraphicsRHI.md) 끝 항목). `LostDeviceRejectsTransferSubmitsAtEntry` 의 방어 스코프가 장치 제거 경고 뒤에 열렸다 → 앞으로.
- `gvnum` 의 결정 한 줄은 `docs/decisions/5-3_CorePlatform.md` 로 옮겼다.

**검증(통합 N, 이 PC)**: Debug `integ-gate`(`-k 0` 빌드 경고 0, `nogpu|lint` 119/119), Debug `race` 5/5, Shipping · Shipping-Server 빌드 경고 0과 `nogpu|lint|race` 113/113 · 115/115(Shipping-Server 의 `CheckTextureFolders` 는 한 번 시간 초과, 단독 4.5 s 통과), Shipping `hostgpu` 통과, Release 매니페스트 `rhi-transfer-gl1-staging-ring` · `rhi-transfer-gl-readback-ring` · `rhi-transfer-dx11-staged-upload` · `rhi-transfer-vk-u6` · `rhi-completion-contract-gpu` 통과, DX11 에디터 `uiaudit`(1600×900 크기 변경 포함) PASS · Warning/Error 0 · 캡처 39 장 중 기본 · 씬 뷰 · 애니메이션 그래프를 눈으로 봤다(글꼴 · 패널 정상), Vulkan Debug 검증 레이어: U6 필터와 `uidemo` 모두 Validation Error · VUID 0. UI 예제 한 판(Release, GPU 독점, `-lang=ko -gv_benchMeshes=100 -gv_benchUiHangulGlyphs=400 -gv_profileFrames=600`) 프레임 벽시계 DX12 2.38 · DX11 2.49 · GL 3.02 · Vulkan 2.79 ms — Vulkan 이 DX12 수준으로 내려왔다(다른 작업자 부하가 있던 PC 의 한 판).
- **Release `AppScenarioTest` 에서 진 시나리오 둘은 main(1d9680081)에서도 같은 값으로 진다(회귀 아님, 따로 지은 main Release 로 확인)**: `game/shooter3d/automation/crowd.scenario.xml`(네 백엔드, frame 301 `Animation.CrowdSharedUnits >= 2` 에 1), `game/empty/automation/vfxgpulights.scenario.xml` `-gl`(frame 310 `ExpectImage differingFraction >= 0.009` 에 4.6e-05 — GL 에서 GPU 조명 VFX 가 안 보인다). 다음 통합 전에 원인을 찾는다.
- 확인하지 못한 것: 에디터 보조 뷰포트(창 밖으로 뺀 패널)를 띄우는 시나리오가 없어 DX11 ImGui 의 플로팅 창 렌더 스레드 경로는 돌지 않았다(후보: 시나리오 하나). 리눅스(Wayland 스모크 종료 코드 0, `WindowTest.X11ClipboardRoundTripsBetweenWindowsIncludingIncr`, `X11DevConsoleTest`, TSan `EngineTest_NoGPU_Shard1`, `fixs` 의 Linux 전용 테스트)는 push 뒤 CI 가 본다.

**동작이 바뀌는 곳(사용자에게 알린다)**
- 명령줄: `-gv_rhiBackBufferFormat` = `RGBA8|BGRA8`, `-gv_lightCulling` = `None|CPU|GPU`, `-gv_viewMode` = 이름, `-gv_morphDiag` = `Normal|ForceOn|SkipDispatch|IndexTag`, `-gv_rhiBackend`/`-gv_vfxSimulation` 도 숫자 인자는 오류. 켬/끔 gv 는 `0/1` 이 그대로 통한다.
- DX11 · GL · Vulkan 업로드가 스테이징 예산 `gv_rhiStagingRingMegabytes`(64)와 단독 상한 `gv_rhiStagingSingleMaxMegabytes`(512)를 따른다. 호스트 가시 메모리가 GL · Vulkan 에서 업로드 64 MiB + 되읽기 64 MiB 더 든다.
- 새 아틀라스 페이지는 첫 업로드가 끝난 다음 프레임부터 그려진다. 회전 스냅 설정은 라디안이다(통합 L).

**같은 결함 후보(근거 있음, 아직 안 함)**
- DX11 `readReadbackBuffer` 도 `Map( DO_NOT_WAIT )` 라 같은 지연으로 거짓 실패가 날 수 있다. `WaitIdleLeavesTheSubmittedCopyReadableOnEveryBackend` 가 그 실패로 `waitIdle` 결함을 가려내므로 그대로 두었다 — 그 테스트가 흔들리면 같은 고침을 적용하고 테스트를 쿼리 기반으로 바꾼다.
- `CheckTextureFolders` · `CheckAssetRules` 같은 짧은 시한(15 ~ 60 s)의 린트와 `EngineTest_NoGPU` 조각이 다른 작업자 부하(`python -` 다섯 개가 코어를 하나씩 썼다) 아래서 시간 초과로 졌다 — 통합 게이트를 부하 없는 때 다시 돌려 통과했다. 게이트가 시작 전에 CPU 부하를 보고 기다리는 안이 후보다.
- 프레임 경로의 남은 GPU 왕복(U8 · U9 나머지, 전송 감시 목록 설계), `...AndWait` 단언 범위, DX11 `allocateUploadStaging` 을 `RHIStagingAllocator` 로 합치기, Vulkan 프레임 밖 펜스 대기 · 전송 작업 수 상한 · 되읽기 링 `HOST_CACHED` · PSO 워커 안전(U12) · 큐 소유권 이전, 켬/끔 `int32` gv 열한 개 안팎과 사용자 설정 숫자 코드, DX11 `Present failed` 로그 반복, GL 비트필드, `WM_MOUSELEAVE` 확인, 아키텍처 이음매 5a 는 통합 N 대기 절에 적었던 그대로 남았다(근거는 그 브랜치들의 커밋 본문과 [RhiAsyncUploads](RhiAsyncUploads.md)).

**정리**: 병합한 브랜치의 워크트리는 지웠다. `D:/w` 바로 아래의 작업자 산출물(`e.py` · `edit.py` · `kh_*.log` · `sv_*.log` · `vb.bak` 등)은 지우기가 권한 분류기에 막혀 남았다 — 사용자가 지우거나 허락한다.

## 000000000000. 2026-10-25 통합 M — `cifixl4`(CI 실패 수정 + 창 핸들 try 접근)와 `rhiu9`(프레임 경로 전송 대기 제거)가 main 에 들어감(push 안 함) (위 절 다음에 읽는다)

**상태**: `D:/w/integ-m` 에서 `wt/cifixl4`(사슬 cifixl → cifixl4) → `wt/rhiu9` 순으로 병합해 main 에 fast-forward 로 넣었다. **push 하지 않았다**(사용자 확인 대기). 충돌은 `Roadmap.md` 한 줄뿐이었고(양쪽을 합침), 두 브랜치가 커밋 메시지에만 남긴 결정은 [5-3](../decisions/5-3_CorePlatform.md)(창 핸들은 `try...` 로만) · [5-5](../decisions/5-5_GraphicsRHI.md)(프레임 경로 전송은 티켓 + `pollTransfer`)에 옮겼다. 컴파일 오류 0(`-k 0`) — 지시서가 꼽은 위험(`RHISwapChainDesc` 리플렉션 구조체 안의 `[[nodiscard]]` 멤버, `WGL_WindowData`, `const RHITransferSubmit`, `string_view` 인자)은 모두 통과했다. 통합이 고친 것: `uidemo.scenario.xml` 의 임시 문턱을 측정값(`all:0.1710`)으로 옮겨 종료 14(UNCONFIRMED)가 0 이 됐다.

**검증**: Debug `integ-gate`(`-k 0` 경고 0 + `nogpu|lint` 119/119) + `race` 5/5. Shipping `full-build` 경고 0 + `nogpu` 65/65 + `hostgpu` 9/9(QaGoldenImages 건너뜀). Shipping-Server `full-build` 경고 0 + `nogpu` 67/67. Release(`SW_GAMES=ALL`) 네 백엔드 모두 PASS · Warning/Error 0: Empty `shippingframe`, Shooter3D `actions`, NileCity `control`, 에디터 `editor.addcomponent`(`-EnableEditor`), `engine.uidemo`. `AppSmokeTest.EditorSelfTestsPassInsideTheEditor`(DPI 케이스가 새 `tryGetViewportWin32Handle` 을 지남) 통과. `verify-batch` 의 `rhi-transfer-u9-canvas-gpu` 통과 — 매니페스트 끝남.

**UI 예제 전후**(전 = main `ab3b917cb`, 후 = 통합 M, Release, GPU 독점, 번갈아 잼): 새 글리프가 몰리는 판(한글 음절 400 개)에서 `RT.Canvas.Upload` 최악 DX12 62.9 → 4.2 ms · DX11 93.8 → 1.8 ms · GL 62.5 → 1.0 ms, 프레임 벽시계 DX12 10.9 → 3.4 · DX11 12.5 → 3.4 · GL 11.9 → 4.1 ms. Vulkan 은 5.4 ms 그대로(U6 전까지 제출 안에서 펜스 대기). 글리프가 이미 찬 정적 `-gv_uiDemo` 화면과 `perf-compare uiPaintOrder` 는 퍼짐 안. `uidemo` 스크린샷은 네 백엔드 모두 전후 픽셀이 같다. 자세한 값은 [RhiAsyncUploads](RhiAsyncUploads.md) "B0 의 구현 수준과 알려진 비용".

**리눅스는 로컬에서 돌리지 않았다 — push 뒤 CI 로 본다**: Wayland 스모크 종료 코드 0(`X11DevConsoleWindow` SIGSEGV), `TestWindow` INCR 클립보드, `TestX11DevConsole`(CI 에 `xfonts-base`), `CI-Debug-TSAN` 의 DXC 경쟁(compileDxc 직렬화), 리눅스 컴파일(`X11Window.h` 의 `#if` 재배치, `X11MacroUndef.h` 뒤 `IWindow.h` include 순서, `WaylandWindow` 보호 구역, GLX 뷰포트 훅).

**남은 위험**: 새 아틀라스 페이지는 첫 업로드가 끝난 다음 프레임부터 그려진다(DX12 는 프레임 지연만큼). 에디터 보조 뷰포트(창 밖으로 뺀 패널)의 GL · Vulkan Win32 경로는 시나리오가 지나지 않는다 — 핸들이 없으면 오류를 남기고 그 뷰포트를 만들지 않는 쪽으로 바뀌었다. 남은 프레임 경로 대기는 새 텍스처 업로드(U8)와 렌더 타깃 읽기(U9), Vulkan 제출 안의 펜스(U6)다.

**같은 결함 가능성**: `...AndWait` 를 프레임 경로에서 부르는 곳은 이제 두 함수만 Debug 단언으로 막혀 있다 — 셋업 구간 전체의 `ScopedForbidBlockingTransfer` 는 U10 몫이라 그 전까지 다른 호출처(`completePendingLoads` · `updateStreaming` · 동기 `acquire`)가 같은 왕복을 남긴다. 창 핸들 쪽은 `void*` 를 내주는 다른 접근자(`getNativeDevice` 같은 RHI 쪽)가 같은 모양인지 아직 보지 않았다.
## 00000000000. 2026-10-25 아키텍처 이음매 0 단계 — 빌드 · 기동 기준선을 다시 쟀다(문서만, push 안 함) (위 절 다음에 읽는다)

**상태**: main `eb2a1fa59` 를 새 워크트리 `arch0` 에서 빌드 칸 독점으로 쟀다(워크트리 · 브랜치는 지웠다). 표는 [08 검증](../08_Verification.md) "아키텍처 이음매 0 단계 기준선". Debug 콜드 풀 빌드 266.5 s(Lean 236.3 s), `.cpp` 증분 2.4 ~ 13.9 s, `FrameRenderer.h` 증분 18.5 s · 27 TU, Dev 기동 중앙값 5.3 s(`ModuleTypes` 2.1 s).

**판정**: DLL 링크가 `.cpp` 증분의 29 ~ 61 %(Engine `.cpp` 52 ~ 61 %)로 30 % 기준을 넘는다. 그러나 5 단계(`Engine_Render` OBJECT)는 같은 `Engine.dll` 에 링크돼 링크를 줄이지 못하므로 링크 이유로는 열지 않고, 새 5a(Debug `-gcodeview-ghash` + `/DEBUG:GHASH` A/B)를 먼저 한다. 6 단계는 팬아웃 27 TU 중 16 개가 `FrameRenderer*.cpp` 자신이라 빌드 이득이 작다 — 병합 충돌 이유로만, 1 ~ 4 단계 뒤에.

**남은 일**: 5a 측정(통합 혼자, CMake 옵션 두 줄 — 통합 L 병합 뒤 조용할 때). ArchitectureSeams 3.3 에 새 후보 둘 — Core `.cpp` 하나가 `ReflectionParser.exe` 재링크로 코드젠 64 개를 다시 돌리는 것(13.9 s), 기동의 `ModuleTypes` 2.1 s 나눠 재기.

## 0000000000. 2026-10-25 통합 L — 브랜치 24 개와 RHI 전송 B0(`rhiasync2`)가 main 에 들어감(push 안 함) (위 절 다음에 읽는다)

**상태**: `arch1x` → `arch2c` → `fixl` → `fixn` → `arch2a` → `fixp` → `fixi` → `fixk` → `fixq` → `fixj` → `fixo` → `fixt` → `arch3` → `fixv` → `fixw` → `fixu`(+`packed`) → `fixx` → (`fixm` · `fixr` 는 `fixw` 로 이미 들어옴) → `cachec` → `fixs` → `anim1` → `anim2` → `rhiasync2`(+`rhiasync`, 맨 마지막) 순서로 `D:/w/integ-l` 에서 병합해 main 에 fast-forward 로 넣었다. **push 하지 않았다**(사용자 확인 대기). 병합한 워크트리 · 브랜치(`dup31` 포함)는 지웠다. 커밋 메시지가 정본이다.

**CI(실행 `40160506131`, 통합 K)의 남은 실패 둘** — Wayland 의 `TestWindow.cpp:385` INCR 클립보드 · `TestX11DevConsole.cpp:33` X 글꼴, TSan 의 `EngineTest` data race — 은 통합 M(`cifixl4`)에서 고쳐 들어갔다(리눅스 확인은 push 뒤 CI).

**병합 충돌과 푼 방법**: `Test/EngineTest/CMakeLists.txt`(fixl 의 `sw_getEditorExtensionModelSources` + fixp 의 서버 전용 `ImageUtil.cpp`), `CheckMutableStatics.toml`(fixi 가 지운 줄, 옮긴 테스트 경로 하나), `docs/09_Decisions.md`(분할본을 두고 arch3 의 결정은 [5-4](../decisions/5-4_DataConfig.md), cachec 는 [5-3](../decisions/5-3_CorePlatform.md)), `ArchitectureSeams.md`(양쪽), `README.md`(StatusBoard 로 옮긴 쪽 + `status-board --write`), `Test/EditorUITest/CMakeLists.txt`(fixx 쪽, Platform glob 은 `Source/Editor` 가 선언), `Roadmap.md`(rhiasync2 쪽). 커밋 메시지에만 있던 결정(fixn 불리언 인자, 테스트 소스 목록의 주인, 자기 태스크만 기다림 · 시간 제한 abort, EditorCore 폴더 규칙)은 영역 파일 5-2 · 5-3 · 5-11 끝에 한 항목씩 적었다.

**통합이 고친 결함**(`-k 0` 으로 모은 컴파일 오류는 셋뿐이었다 — B0 번들은 오류 없이 컴파일됐다): ① cachec · fixs 의 `FileUtilInternal` · `ResourcePathsInternal` 이 기본 멤버 초기화가 있는 중첩 구조체를 같은 클래스의 `inline static` 멤버로 써 clang 이 멈췄다 — 구조체를 익명 네임스페이스로. ② fixn 테스트의 `sw::utf8`(전역 별칭). ③ **EditorCore 의 링크가 닫혀 있지 않았다** — 코어 파일 아홉이 코어 밖 `EditorPaths` · `EditorBackgroundTask` · `EditorExternalToolJob` · `EditorDrawReleaseQueue` · `EditorListFilter` 를 불러 EngineTest 링크가 졌다(EditorTest 만 손 목록으로 가렸다). 폴더 규칙으로 코어에 넣었다(`Common/Config`, 새 모델 층 `Common/Task`, `Common/Backend/Model`, `Common/Widgets/Model`). ④ fixs 회귀: `findLooseFile("game/shooter3d")` 처럼 팩 폴더 자신을 가리키는 id 가 빈 경로가 돼 로컬라이즈 수집이 shooter3d 에셋 문자열 17 개를 지우려 했다 — `tryUnderRoot` 가 빈 키를 도메인 루트로 푼다(`ResourceTest.PackFolderIDResolvesToTheFolder`). ⑤ 코어를 통째로 링크하자 코어 파일의 정적 등록이 올라 빈 레지스트리를 가정한 `EditorFrameStepTest` · `EditorExtensionSubsystemsTest` 가 졌다 — `ScopedEmptyEditorExtensionRegistry<T>`. ⑥ `PropertyUnitsTest` 가 코어 타입 `EditorPreferences::_rotationSnapValue`(도)를 잡았다 — 라디안(`Units = rad`) 저장, 도구 막대로 넘길 때 도로. ⑦ fixt 의 `unitmodels.xml` 데이터 종류 등록. ⑧ editor · shooter3d 문자열 표 재수집(출처만). ⑨ fixw 의 `TaskManager::_runningWorkerCount` 초기값 두 곳. ⑩ **B0 의 GL 동기 업로드가 10 초 시간 초과로 졌다**(`AppTest_Race` 의 GL 재생): 컨텍스트 없는 스레드의 `pollTransfer` 가 RT 의 워터마크만 봤는데 게임 스레드가 동기 로드로 막혀 RT 가 프레임을 돌리지 않았다 — 컨텍스트가 비어 있으면 기다리지 않고 한 번 집어 펜스를 본다.

**검증**(새 워크트리): Debug `full-build` 경고 0 + `ctest -L lint|nogpu` 119/119 + `race` 5/5. Shipping `full-build` 경고 0 + `nogpu` 65/65 + `hostgpu` 9/9(QaGoldenImages 건너뜀). Shipping-Server `full-build` 경고 0 + `nogpu` 67/67. Release(`SW_GAMES=ALL`) 네 백엔드 모두 종료 0 · PASS · Warning/Error 0: Empty `shippingframe`, Shooter3D `actions`, NileCity `control`, StarSkirmish `control`(fixt), FrontierTown `month`, `rhiswap/swapother`(arch3 의 `render2d` — 낡지 않음), 에디터 `editor.addcomponent`(`-EnableEditor`). Debug 에디터 `editor.addcomponent` dx12 PASS · Warning/Error 0. `verify-batch` 의 `rhi-transfer-b0-gpu`(Release 네 백엔드) 통과 — 매니페스트 끝남.

**다음 세션 첫 일(순서)**: ① 사용자가 허락하면 push → `py -3 -m Scripts ci-jobs --summary`(Linux 컴파일: B0 네 장치 중 GL · Vulkan, EditorCore 이동, fixs 의 Linux 전용 `WritePathKeepsTheDiskSpellingOfExistingFolders`). ② `gpu-verifier`: `## 000000` 목록, [HostGpuIsolation](HostGpuIsolation.md) H1~H4, B0 의 테스트 없는 경로(DX12 폴링만, 업로드 직후 텍스처 파괴, GL 렌더 스레드 모드의 동기 `acquire` — 이번 GL 수정이 닿는 곳, Vulkan 검증 레이어의 풀 락, 끝나지 않은 전송이 있는 텍스처의 bindless 등록 거절), `rhi-transfer-b0-measure`(세는 스코프부터). ③ [ArchitectureSeams](ArchitectureSeams.md) 5a(GHASH 링크 A/B, 통합 혼자). ④ [AnimationComponentChain](AnimationComponentChain.md) 4 절 측정 + 낡은 서술(`PairedAnimation.md` "의존 레벨", `BenchScene` 변수 → `GlobalVariables.md` 재생성).

**병렬로 띄울 묶음(작업자 여섯 안팎, 파일 소유가 겹치지 않게 — 빌드하지 않고 커밋만)**: B. `Core_objects` 가 `ReflectionParser.exe` 에도 링크돼 Core `.cpp` 하나에 코드젠 64 개가 다시 도는 문제. C. 비동기 로드 `raw this` use-after-free(`RenderPipelineAsset` · `RenderPassAsset::loadFromXMLFileAsync`) + `AsyncFileIO::waitIdle` 워커 교착. D. uint64 해시 단독 키(`ResourcePakReader::_mapEntry`, `GPUMaterialRegistry`, `RenderPSOCache::_mapMaterialPSO`, `GPUSceneBuilder::_mapShaderRepresentative`, `ShaderBindingLayout::_mapKeyToIndex`)와 `UIAnimatedProperty` · `ReflectionInvoke` · `PropertyRoleUtil` 의 `kint8..kfloat64` 위치 가정을 `getBuiltinClass` 로. E. 3.2 래퍼 걷기. F. [RhiAsyncUploads](RhiAsyncUploads.md) U5 · U6 · U7 · U8 · U9 · U11(소유 폴더가 다르다, GPU 확인은 통합). G. `ArchitectureSeams.md` 6 절의 다음 단위(`GameConfig::getActive()` 전달 창구 지우기). H. [Roadmap](Roadmap.md) 의 `[병렬 가능]` 항목.

**동작 변화 · 남은 위험**: fixn — `overlay.show <바> 2` 거절, `profile.trace 1|0|true|false` 수용. fixj — `SceneAudio._emitter` 순회 순서가 보존되지 않음. fixw — 종료 대기 10 · 30 초 뒤 `std::abort()`(Debug `race` 는 통과, 느린 콜백 · TSan 오탐은 CI TSan 에서 본다). fixr — `describeFirstUnfinished` 는 진단 근사. B0 — DX11 · DX12 · GL 프레임 경로에 GPU 왕복이 새로 생겼다(글리프 아틀라스 · 구간 갱신은 통합 M 이 걷었고 새 텍스처 업로드 U8 · 렌더 타깃 읽기 U9 가 남음), `ScopedForbidBlockingTransfer` 는 그 두 함수에만 설치(셋업 구간 전체는 U10). 회전 스냅 설정이 라디안이 됐다 — 사용자 `Saved/Editor` 의 옛 도 값(15)은 범위(최대 90°) 밖이라 읽기에서 거절된다. `IRHIDevice.cpp:317` 의 `equals( pSoftwareEnv, "1" )` 손 비교는 `tryParseBool` 로.

**아직 고치지 않은 같은 결함 후보**: `SkeletalAnimatorComponent.cpp:778` `_listRootMotionModifier` contains + push_back, `whenAny` 숨은 간선(`SmallTaskThroughput` 으로 분기 비용 먼저), `TextMeasureCache` 문맥 해시 2^-64 잔여, `GPUUploadQueue` · `AsyncFileIO` · `JoltJobSystem` · `NavTileRebaker` · `MaterialXML` 의 자체 대기 경로, `EditorViewportPreview::applyMaterial` 의 `ensureMeta` 동기 비용, `readNumberList` 를 거치지 않는 다른 숫자 목록 읽기, B0 의 `D3D12RHIDevice::_fenceValue` 락 없는 읽기(U5) · GL `_frameSerial` · DX11 · GL bindless 중복 등록. 정적 등록을 세는 테스트가 무엇이 링크됐는지에 기대는 모양(이번 둘은 고쳤다 — 커맨드 · 패널 레지스트리 테스트는 통과).

**교훈(이번 통합)**: ⑪ 코어 오브젝트를 통째로 받는 타깃이 늘면 코어의 링크 닫힘이 처음 시험된다 — 한 타깃(EditorTest)의 손 목록이 빠진 의존을 가렸다. ⑫ 병합 커밋의 훅은 한쪽 부모와 같은 파일을 다시 검사하지 않는다 — 작업자가 린트를 건너뛴 위반(`CheckCodeConventions`)은 통합의 `ctest -L lint` 에서 처음 보인다.

## 000000000. 2026-10-25 통합 K — CI 실패 수정 2 개가 main 에 들어감(push 함) (위 절 다음에 읽는다)

**상태**: `cifixui`(UI 결정성 고정물이 글리프 캐시 프레임 시계를 올린다) · `cifixcook`(Linux 에서 소문자 id 를 붙인 디스크 도메인의 실제 표기로 푼다 — `FileUtil::findPathIgnoringCase`)를 `D:/w/integk` 에서 병합해 main 에 fast-forward 로 넣었다. 충돌 없음. **push 하지 않았다**. 워크트리 · 브랜치는 지웠다. 통합이 같은 모양 하나를 고쳤다: 엔진 없이 UISystem + 가짜 글꼴을 돌리는 `TestUIAccessibility` · `TestUILocalization` · `TestUITextInput` 의 프레임 함수도 시계를 안 올렸다 — `FakeFontServiceFixture::beginFrame` 도우미로 네 테스트가 같은 순서를 쓴다.

**검증**: Debug `full-build` 경고 0 + `ctest -L nogpu|lint|race` 122/122. Debug 직접: `UIDeterminismTest` 6/6(원래 Windows Debug 가 지던 것) · UI 셋 21/21 · `CookJobTest` 4/4 · `SceneCookTest` 3/3 · `FileTest` 24/24. Shipping 전체 빌드 경고 0 + `nogpu` 64/64 + `hostgpu` 9/9(QaGoldenImages 건너뜀) + 같은 케이스 58/58. `Ninja-Debug-ASAN` EngineTest 같은 케이스 34/34. 네 백엔드 시나리오는 건너뛰었다 — Windows 런타임에 닿는 변경이 없다(엔진 쪽은 `SW_PLATFORM_LINUX` 안과, Windows 가 부르지 않는 새 함수 하나).

**남은 일**: push 뒤 CI 리눅스 잡에서 `CookJobTest.RegistryCookFollowsTheMetaListing` · `SceneCookTest.*` · Wayland 의 `UIDeterminismTest.*` 를 본다. 같은 결함 후보(리눅스): 아직 없는 파일의 소문자 id 를 `makeAbsolutePath` 가 소문자 그대로 이어 붙여 쓰기 경로(`saved/...`)가 대소문자가 다른 새 폴더를 만들 수 있다 — 쓰기 호출처(약 50 곳)를 CI 로그로 먼저 본다. 팩 쿠킹의 제외 목록 · 설정 해시는 양쪽 다 소문자 비교 키라 디스크로 풀지 않아 해당 없음.

## 00000000. 2026-10-25 통합 J — 결함 수정 브랜치 8 개가 main 에 들어감(push 안 함) (위 절 다음에 읽는다)

**상태**: `fixa` · `fixb` · `fixc` · `fixd` → `fixe`(fixa 위) · `fixf`(fixc 위) · `fixg`(fixb 위) · `fixh`(fixd 위) 순서로 `D:/w/integj`(`wt/integj`)에서 병합해 main 에 fast-forward 로 넣었다. **push 하지 않았다**(사용자 확인 대기). 병합한 `fix*` 와 `integj` 워크트리 · 브랜치는 지웠다. 커밋 메시지가 정본이다.

**병합 충돌**: `AnimationSystem.{h,cpp}` 하나 — LOD 클라이언트 목록을 `fixa`/`fixe` 의 `AnimationOrderedList`(등록 순서 유지, `forEach` 가 순회 중 해제를 견딘다)로 정하고 `fixh` 의 `IndexedPtrList` 는 `PrimitiveRegistry` 에만 둔다(결정 기록 5-3). 예상했던 `Half.h` · `TaskTypes` · 게이트 toml · `FontService` · `GameDataCache` · README 는 자동 병합됐고, `fixb` 의 `std::erase_if` 는 `fixg` 가 고친 그대로다. 병합 직후 전체 게이트(`ctest -L lint`)에서 새 게이트 `CheckContainsPushBack` 은 기준선 72 안이었다.

**통합이 찾아 고친 결함**(컴파일 오류는 없었다 — `-k 0` Debug 전체 빌드 경고 0 · 오류 0): ① `fixf` 의 태그 충돌 단언이 철자만 다른 같은 태그(`Collider` · `collider`)에 걸렸다 — 이름 풀은 같음만 대소문자를 무시하고 글자는 철자마다 따로라 포인터 비교가 틀렸다. `registerTag` 가 `hashed_string` 으로 이름 같음을 본다. ② `fixd` 의 `DependencyAcrossManagersIsRejected` 가 `TaskManager` 둘(전역 큐를 품어 하나가 수백 KB)을 스택에 둬 스택 넘침 — 힙으로. ③ **에디터가 기동하자마자 멈췄다**(main 에 이미 있던 결함, 통합 H `250437ba4`): ImGui 렌더러 백엔드의 창 크기 가드가 원본 콜백을 에디터 상태 저장소에 뒀는데 백엔드가 에디터 컨텍스트보다 먼저 초기화된다 — 원본은 백엔드 멤버가 갖고 ImGui 컨텍스트의 사용자 칸으로 찾는다. ④ **Debug 에디터가 시작 직후 멈췄다**(main 에 있던 결함, `refindexpar`): 참조 색인 스캔 워커가 `findSubsystem<TaskManager>` 로 에디터 범위를 읽었다 — 범위를 찾는 `scan` 겹지정을 지우고 UI 스레드가 `TaskManager*` 를 넘긴다. ⑤ `VerifyManifest.py` 독스트링의 `SyntaxWarning`(커밋 훅마다 찍힘).

**검증**(새 워크트리 — 낡은 산출물 없음): Debug `full-build` 경고 0 + `ctest -L nogpu|lint|race` 122/122. Shipping 전체 빌드 경고 0 + `nogpu` 64/64 + `hostgpu` 9/9(QaGoldenImages 건너뜀). Shipping-Server 전체 빌드 경고 0 + `nogpu` 66/66. Release(`SW_GAMES=ALL`) Empty `shippingframe` 네 백엔드 종료 0 · Warning/Error 0(`fixc` 의 새 단언 · `fixd` 의 `isOnPoolWorker` 단언 걸리지 않음), 에디터 시나리오 `editor.addcomponent` 네 백엔드 종료 0 · Warning/Error 0 · 3~8 초(`fixb` 의 추적기 종료 대기가 10 초 지연을 만들지 않음), Debug 에디터 `editor.addcomponent` dx12 종료 0 · 단언 0. Debug `AppScenarioTest.EditorScenariosPassOnDirectX12`(에디터 체인 201 단계, 755 s): 단언 0, 실패는 이미 알려진 `editor.charactereditor` 하나(`charactereditor.slider.last` 가 창 밖 — 통합 H 의 '아직 못 고친 것').

**실측(Release, 한 번씩)**: 시동(`Startup phases:` total) DX12 1123 ms · DX11 621 · Vulkan 4552(FrameRenderer 2332 · RHI 1754) · GL 2946(FrameRenderer 2363) — 통합 I 와 같은 모양이다. `perf-compare --cases tickCostChurn --runs 3`(독점, dx12, 600 프레임 — 스위치 A/B `-gv_tickCostDispatch`): `GT.Frame` p50 1441 · p99 2359 µs 양쪽 같음, `GT.Scene.tick.components` p50 65 → 73 µs, `GT.Scene.tick.registry` p50 4 · p99 13 µs. `[CacheStats]` 줄은 측정 창 끝에 찍혔고 이 합성 씬에서는 모든 캐시가 적중 · 실패 0 이다(실제 게임 창에서 다시 본다).

**같은 결함 후보**: ③ · ④ 는 둘 다 "에디터 범위가 없는 때 · 스레드에서 에디터 서비스를 찾는" 모양이다 — 렌더러 · 플랫폼 백엔드, 스플래시, 워커 본문의 `getEditorState` · `findSubsystem`. 에디터를 띄우는 검증이 Shipping `hostgpu` 에 없고 Debug `nogpu` 는 백엔드를 띄우지 않아 두 통합 동안 안 보였다 — Debug 에디터 시나리오 한 바퀴를 통합 검증에 넣을 것(`08_Verification.md` 3절에 적었다).

**남은 일**: 아래 통합 I 절의 목록에서 이번에 끝난 것(`containers1` 호출처, 같은 O(N) 모양, `ResourcePaths` 전역 쓰기 가드, 중첩 `runParallel` 깊이, 해시 단독 키, 계약 주석, `recordInput` O(N²), `decodeHalf` 테스트, `lazyFill` 근거, 다른 풀 간선 거부, 에디터 종료 대기, `EditorService` 전역 가드, `GameDataCache` 옛 표 쌓임, `contains` 뒤 `push_back` 게이트)은 지웠다. `whenAny` 숨은 간선은 후보 `WhenAnyHiddenEdge`(`Candidates-engine.md`)로 갔다.

## 0000000. 2026-10-25 통합 I — 병렬 · 비동기 · 구조 정리 브랜치 27 개가 main 에 들어감(push 안 함) (위 절 다음에 읽는다)

**상태**: `wt/*` 27 개(`binisolate` `rhiasyncplan` `lintscan` `ciflow` `regexscan` `verifyscan` `verifypar` → `parrules` → `samefix1` · `samefix2` → `samefix3` → `framesysaccess` → `containers3` → `packedtypes` → `halfunify` → `importpar` → `editorscope` → `hotreloadq` → `taskdeps` `logdeadlock` `containers1` `containers2` `profscopes` `refindexpar` `packfast` `editorasync` `cacheinstr`)가 통합 워크트리 `D:/w/integi`(브랜치 `wt/integi`)에서 병합돼 main 에 fast-forward 로 들어갔다. **push 하지 않았다**(사용자 확인 대기 — 이번 세션 지시). 워크트리 · 브랜치는 지우지 않았다(`D:/w/<이름>` 27 개 + `integi` + `mainchk`). 커밋 메시지가 정본이다.

**검증**: Debug `full-build` 경고 0 · `ctest -L nogpu|lint|race` 122/122(그중 `nogpu` 70, `python` 51 — `verifypar` 의 새 파이썬 테스트 포함), Shipping 전체 빌드 경고 0 + `nogpu` 64/64 + `hostgpu` 9/9(QaGoldenImages 건너뜀), Shipping-Server 전체 빌드 + `nogpu` 66/66. 속도 실측은 아래 "실측".

**통합이 병합 뒤 고친 것**(`-k 0` 한 번에 모은 컴파일 오류 둘과 게이트): ① `hotreloadq` 의 `EditorAssetImportJob` 이 Commands 층(3)에 있어 Workspace 층(2)의 `AssetHotReload` 가 위 층을 include — `Workspace/EditorAssetImportJob.{h,cpp}` 로 옮기고 잡 제출 도우미를 토대 층 `submitEditorBackgroundJob` 하나로. ② `editorscope` 의 `TextureRawImporter( config, TaskManager* )` 와 `hotreloadq` 의 옛 호출 충돌 + 워커에서 `importChains` 가 `findEditorScope()` 를 읽던 것 — `AssetImportStampUtil::importAll` · `importSources` 가 `TaskManager*` 를 인자로 받는다(헤드리스 `importAll*` 은 찾아 넘기고 워커의 `importChanged*` 는 nullptr). ③ 브랜치가 `CheckMutableStatics` 이전 기준이라 새 정적 칸 다섯 — `ContentHash` 시간 누적은 `PackCooker` · `CookStore` 멤버로, `EditorService` 범위 + 등록 스레드는 한 칸으로, `AsyncLogSink` 재진입 깊이 둘은 `thread_local` 구조체 하나로, `CacheStats` 등록부와 `TestAssetHotReloadImport` 의 함수 포인터 신호 칸은 이유를 적어 예외로. 대기 표 233 → 232 파일, 상한 합 505 → 496. ④ `CheckLazyInitStaticFlag` `total` 1 → 0(`lazyFill=6` 유지). ⑤ `SerializationRoundTripTest` 가 낡은 게임 팩으로만 통과하던 것(새 빌드 폴더 Shipping-Server 63 건 실패) — `SW_TEST_REQUIRES_FEATURE( SourceResources )` 를 환경 건너뜀보다 먼저. ⑥ Shipping `TestAppUI` 미사용 경고 셋. ⑦ Shipping 첫 화면 골든 `shippingframe_dx12` 를 다시 떴다(`GpuVerification.md` 3.1 판정대로, 매니페스트 `shipping-golden-dx12` 끝남). README 진행 표는 병합마다 `status-board --write`, `VerifyBatch.json` 은 충돌 없이 `pack-cook-parallel-tests` · `packed-types-test` 가 둘 다 있다. 병합 커밋 머리가 PowerShell 5.1 의 BOM 없는 스크립트 문자셋 탓에 깨져 `filter-branch --msg-filter` 로 고쳤다(교훈 ⑨).

**실측(Release, `SW_GAMES=ALL`, 이번 통합, 한 번씩 — 다른 빌드 없음)**: 셰이더 `--cook-shaders` 최신 확인 2.9 s(0 binaries, compile 7554 ms · reflection 3634 ms 워커 합, total 1273 ms), `--verify-shaders` 3.9 s 통과. `-cook-all` 새 기록(새 워크트리 — 스테이징 · 팩 폴더 비어 있음) 3.6 s, 팩 요약 `14 packs in 1850 ms - read 56, compress 465 wall / 1183 cpu(2.5 배), layout 5, hash 201, write 11, other 1110 ms (1235 files, 28 MB)`; 바뀐 것 없는 두 번째 1.8 s(`988 ms`, hash 154 · other 833). 린트 `CheckVerifyPending` 1.5 s(전 43 s), `RunForwardDeclarationCandidates` 4.8 s(전 34.6 s, `regexscan`). 시동(`Startup phases:`, Empty `shippingframe` 시나리오, 네 백엔드 모두 종료 0 · Warning/Error 0 — `profscopes` 스코프가 시동에서 멈추지 않음): DX12 1162 ms(첫 실행, Audio 554), DX11 595, Vulkan 4784(FrameRenderer 2175 · RHI 1746), GL 2786(FrameRenderer 2229) — 벌어진 Vulkan · GL 의 FrameRenderer 몫이 다음 시동 측정의 첫 질문이다. `[CacheStats]` 줄은 측정 창(`perf`)에서만 찍혀 이번 실행에는 없다.

**push 전에 볼 것**: 없음 — Debug · Shipping 증거가 마지막 커밋 트리에 있다(`full-build`). push 하면 `py -3 -m Scripts ci-jobs --summary` 로 Linux 컴파일(`Half.h` 의 `__builtin_bit_cast`, `RecordingDxcInclude`)과 STL containers 를 본다.

**사용자 결정 대기**: ① push(이번 세션 지시로 보류). ② `ciflow`: 린트 실패 시 `gh run cancel` 로 린트 잡이 cancelled 로 끝나는 것이 브랜치 보호의 필수 확인과 충돌하는지 GitHub 설정에서 확인. ③ push 를 GPU 확인 앞으로 당기는 정책(`11_Workflow.md` "검증 전 push 금지" 는 사용자 지시)을 바꿀지. ④ [ArchitectureSeams](ArchitectureSeams.md) 4 절의 둘(GPU CI 자동화, 작업자 빌드 금지 규칙 재검토).

**아직 안 한 것(후보, 순서)**: `TaskManager` 의존 API 의 프로덕션 첫 사용처(`taskdeps` 가 사이클 검출을 채움), 모델 한 개 안 LOD · 클립 병렬(이득 작음), Detour 군중(측정 먼저: `GT.Scene.tick.navigation`), G 버퍼 법선 RGBA16F → RG16 옥타헤드럴(`RHIFormat` 추가 · 셰이더 재쿠킹), 메시 정점 64 → 28 B(`.mesh` 버전 4, `PreloadAndLod` 와), 측정 캐시 전체 비우기 → LRU, `hostgpu` 격리 H1~H4(`HostGpuIsolation.md`), 락 쥔 채 로그 금지 규칙, 순서가 주석에만 있는 목록 3종(`SceneFrameStepList` · `SW_SCENE_SERVICE` · 컴포넌트 틱 그룹), `RunLintSuite` · `ci-jobs` 의 `ConflictSchedule` 적용, `Test/` 의 static 카운터 경쟁 후보, 같은 정규식 모양 3곳(`RunForwardDeclarationCandidates.py` 170, `CheckServiceAccess.py` 165, `FileScan.py` 121 — 1 s 미만이라 둠). `PackedTypes` · `RhiAsyncUploads` · `HostGpuIsolation` 은 [Roadmap](Roadmap.md) 11 에 넣었다. 컴파일 캐시는 sccache 를 PCH 때문에 기각했으니 다시 제안하지 않는다(`bd5b28125` 계열 기록).

**그다음** [ArchitectureSeams](ArchitectureSeams.md) 0 단계(측정, 통합 혼자)와 1a · 1b · 2a · 3 · 4.

**끝난 작업자가 남긴 마무리(결함이거나 약속한 것 — 빼먹지 않는다)**
- **실행해서 봐야 하는 것**: `--jobs 2` 에서 `nogpu` · `race` 겹침의 시간 제한(`verify-batch`), `containers3` 의 1 만 개 테스트 시간, `packedtypes` 의 옥타헤드럴 오차 상한 1e-4(Debug `nogpu` 는 통과), 썸네일 · 반사 프로브 · 핫 리로드가 UI 스레드에서만 적용되는지(자동화 시나리오에 `WaitUntil` 탐침), `logdeadlock` 의 `FileLogOutput::open` 이 폴더 생성 실패에서 false 를 돌려 `LoggerSubsystem` 쪽 영향이 없는지.
- **속도 실측이 아직 없는 것**: 임포트 전체(텍스처 + 모델 약 1,255 개, `importpar` 전후), 참조 색인 스캔(`refindexpar`, Debug 6~19 s → ?), 시동 단계별(`Startup phases:` 로그), `GT.Material.waitIdle` 등 대기 스코프 크기, 캐시 적중률(`[CacheStats]` 줄 — 측정 창 끝에 찍힌다). 이 숫자가 있어야 위 후보(애니메이션 사슬, RHI 비동기, Detour, 캐시 계약)의 순서를 정한다.
- **확인 못 한 안전 가정**: 썸네일 로드 중 디바이스가 바뀔 때 `Texture2D::releaseRHI`, 반사 프로브 베이크의 캡처 카메라가 편집 카운터를 올려 다음 프레임에 취소시키는지.
- **`taskdeps` · `containers1` · `halfunify` · `hotreloadq`**: 자식 → 부모 완료 기다림은 사이클 검사가 못 본다(주석, `whenAny` 는 후보 `WhenAnyHiddenEdge`). `AnimationOrderedList::getItems` 가 접기를 해서 게임 스레드 전용(`GPUSceneBuilder::populateFromScene` 스레드 미확인). `halfunify`: HLSL f16 해독 안 봄. `hotreloadq`: 한 잡 안 원본은 순차(체크아웃으로 모델 다수가 바뀌면 전보다 느림), 규칙 파일만 바뀐 다른 원본은 자동 재임포트 안 함, 핫 리로드 텍스처의 밴드 압축은 이제 그 워커에서 직렬이다(통합 ②).
- **구조 후속**: 에디터의 다른 UI 스레드 동기 `acquire` 점검(`AssetManager` 사용처), `ConfigHotReload.cpp` 의 동기 reload, `TextureCache::resizeRenderTarget`(`RT` 에서 `waitIdle`, `RhiAsyncUploads` U13), `erase` 71 건(`CheckIndexLoopErase` 기준선) · `contains` 뒤 `push_back` 72 건(`CheckContainsPushBack` 기준선, `EditorThumbnailCache` 가 진짜 후보), `setMembership` 의 `findBatchMembership`, 쓰기 I/O 를 `ThreadedLocalStore` 로 거치는 규칙의 기존 호출처 감사, 락 쥔 채 로그 호출 규칙의 나머지 호출처(`logdeadlock` 보고 목록), `CookRecord::make` 의 출력 재읽기 해시.
- **hostgpu 격리**: `HostGpuIsolation.md` 의 H1(쓰기 위치 스냅샷 점검)은 앱을 돌려야 해서 통합이 H1 → H2 → H3 → H4 순서로 한다. 풀기 전에 `AppBin` 잠금이 지키는 위치(`Resource/scratch/<이름>`, `Saved/Automation/<시나리오>/`, 시나리오 리터럴 `Saved/…` 35 곳, `-EnableEditor` 스모크의 `Saved/Editor`)를 격리한다.
- **GPU 에서만 보이는 것**: `hostgpu` 로만 도는 테스트에 남았을 `countContaining` 치환 오류, 병렬 쿠킹 바이너리가 직렬과 같은지(`--verify-shaders` 는 통과), 두 GPU 칸 동시 사용의 타이밍 · 골든(`HostGpuIsolation.md` 7절).

**교훈(이번 통합)**: ⑧ 게이트가 생기기 전 기준에서 갈라진 브랜치는 병합 뒤 그 게이트를 넘는다(이번 `CheckMutableStatics` 다섯) — 작업자 지시문에 "기준 커밋 뒤에 생긴 게이트" 를 적거나 통합이 병합 직후 전체 게이트를 한 번 돈다. ⑨ PowerShell 5.1 은 BOM 없는 `.ps1` 의 한국어를 CP949 로 읽는다 — 커밋 메시지 같은 한국어 문자열은 스크립트 파일에 넣지 말고 `git commit -F <UTF-8 파일>` 로 넘긴다. ⑩ 오래 쓴 빌드 폴더는 낡은 산출물(게임 팩)로 테스트를 통과시킨다 — 배포 구성 검증은 새 워크트리에서 한 번 돈다(`08_Verification.md` 3절).

## 000000. 2026-10-25 통합 H — 브랜치 16 개가 main 에 들어감(push), GPU 확인 대기 (위 절 다음에 읽는다)

**상태**: 수정 · 정리 브랜치 14 개(`registrylogs` → `scenlogs` → `logids` → `apptestfeature` · `scenfixes` → `lowlevelgaps` · `lowtier` → `glyphbudget` · `roughnessparam` · `editorscen` · `gputhresholds` → `staticgate` · `poseaudit` · `staticleft`)와 셰이더 쿠킹 `cookfast` · `cookdeps` 가 main 에 들어갔다. 검증은 Debug `full-build`(경고 0 · 오류 0) · `ctest -L nogpu` 69 · `lint` · `race` 통과, `lint-suite` 47/47, Shipping 전체 빌드 + `nogpu` 64 통과, Shipping-Server 전체 빌드 + `nogpu` 66 통과, Release `--cook-shaders`(12740 개 다시 굽기 272 s, 두 번째는 0 binaries generated 1958 ms) · `--verify-shaders` 통과(바이너리 바이트 변경 없음 — 스탬프 12 개만 새 판). 커밋 메시지가 정본이다.

**main CI(실행 40121848026)가 졌던 것을 고쳤다**: 마지막으로 빌드까지 돈 초록이 `89b535b7e` 였고 그 뒤 여러 커밋은 Lints 가 먼저 져 빌드 잡이 건너뛰어져 결함이 쌓여 있었다. Linux 컴파일 여섯 곳(`TestLowLevelReport` 의 `unistd.h`, `TestX11DevConsole` 의 매크로 `False`, `WaylandWindow::State` private 이름), Windows Shipping ctest(활성이 아닌 게임 원본을 읽는 케이스 8 개에 `SourceResources`), 전용 서버(RHI 를 이름으로 찾는 케이스 5 개에 `ClientCode`). 못 푼 것: **Windows STL containers 의 `UIDeterminismTest.RebuildGivesSameDump`** 는 글리프 래스터 예산이 시간제이고 프레임 시계가 죽어 있던 결함(`glyphbudget`)과 같은 증상이라 병합으로 풀리길 기대한다 — 로컬에서 STL 구성을 안 돌렸다. Linux TSan 은 Setup 단계 시한(exit 124)이라 환경으로 본다. **Linux 컴파일은 로컬에서 못 본다** — push 뒤 `py -3 -m Scripts ci-jobs --summary` 로 본다(특히 `cookdeps` 의 `RecordingDxcInclude` 가 Linux DXC WinAdapter 에서 `IsEqualGUID` · `__uuidof(IUnknown)` 로 컴파일되는지).

**통합이 병합에서 고친 것**: `KnownLog` 충돌은 `scenlogs`(허용 목록) 기준. `BuildFeature` 에 `X11ConsoleOverlay` 를 `apptestfeature` 항목 뒤에 더함. `EditorStateStore.h` 를 층 표의 잎 헤더(`GenerateEditorLayers.toml` prelude)에 올림(`staticleft` 가 토대 층 ImGui 백엔드에서 씀). `ScopedLogSuppressor` 가 싱크를 비우지 않고 버리는 싱크를 건다(`lowlevelgaps` 가 싱크 없는 리스너 붙임을 멈춤으로 바꿔 시나리오 로그 포착이 죽던 것). `logids` 의 `countContaining` → `countWithID` 치환 오류 넷(`TestFile` · `TestMaterial` · `TestXMLDocument` · `TestConfigManager`)을 고쳤다 — **같은 모양의 치환 오류가 `hostgpu` 로만 도는 테스트에 남았을 수 있다**. `cookdeps` 는 빌드 없이 커밋돼 컴파일 오류 둘(문자열 삼항식, `unordered_set` 범위 삽입)이 있었고 `cookfast` 의 병렬 합치기 위에 다시 얹었다(작업 스레드는 결과 칸에만 쓴다). `CheckMutableStatics` 대기 표를 다시 만들었다(292 → 233 파일, 상한 합 628 → 505).

**GPU 확인 대기(`gpu-verifier`, Release · Shipping, 네 백엔드 — "Warning/Error 는 통과가 아니다", 건너뜀 규칙 적용)**
1. `hostgpu` 전체 한 번. 새로 보는 것: `scenlogs` 의 종료 코드 15(`UnhandledLog`) — id 없는 `SW_LOG_WARNING/ERROR` 약 3,500 곳이 있어 허용 목록(`allowedlogs.json`)이 비어 있는 동안 Warning/Error 를 남기는 시나리오는 15 로 끝난다. 먼저 `py -3 -m Scripts scenario-logs --regen --dry-run`.
2. `glyphbudget`: `uidemo` 시나리오(글리프가 다 채워지길 `WaitUntil`), Shooter3D HUD 프레임 3 · 300, `WaitUntil` 시한 초과가 종료 14 가 아니라 실패인지.
3. `roughnessparam`: Water · Terrain · Foliage · Environment 에서 `_roughness` 등 새 Error 가 없는지.
4. `editorscen`: 에디터 시나리오 8 개 단독, `EditorScenariosPassOn<백엔드>` 체인. `scenfixes`: `nilecity` pause · control · gamepadcursor, `shooter3d.crowd` 의 `WaitUntil` 시한.
5. `gputhresholds`: `useroptions` dx12 `distance` 가 0.665 근처로 오르는지(안 오르면 `DepthClipEnable` 추정이 틀림) 후 그 값을 dx12 measured 에 더함; (`shippingframe` dx12 골든은 통합 I 에서 다시 떴다); DepthClip 수정이 shadow 시나리오 · 벤치 골든을 바꾸는지. gl 의 GPU 라이트(`vfxgpulights`)는 원인을 못 찾았다(라이트 버퍼를 `AsyncBufferReadback` 으로 되읽는 것이 첫 단계). 같은 모양: 0 으로 시작하는 PSO 서술이 백엔드 기본값과 조용히 다른 칸이 더 있을 수 있다.
6. `lowtier`: Debug · Release · Shipping · Shipping-Server 링크는 봤다. **Debug-Cold(pch 끔) · 정적 Shipping · CI-Debug-STL 링크와 유니티 빌드, `PoolAllocator.cpp` 에 `SW_LOG_ERROR` 를 임시로 넣어 컴파일 실패하는지는 안 봤다**(CI 가 정적 Shipping · STL 을 본다).
7. `poseaudit`: 래그돌 · 군중 · 리그 · 리타깃 · 얼굴 테스트의 새 단언은 Debug nogpu 에서 통과했다 — `hostgpu` 에서 따로 볼 것 없음.

**아직 못 고친 것**: `editor.charactereditor` 체인(`charactereditor.slider.last` 가 창 밖 — `CharacterEditorPanel.cpp` 의 첫 슬라이더에 `note`), `startAfter="Immediately"` 시나리오가 `ContentWatchFence` 를 못 막는 것(`AutomationRunner::onFrameBegin` 이 시작 차단을 무시), `shooter3d.crowd` 근본 원인(`scenfixes` 는 단언만 `WaitUntil` 로 바꿨다), id 없는 `SW_LOG_WARNING/ERROR` 약 3,500 곳(`scenario-logs --regen` 의 "id 없음" 표를 영역별로 나눠 소스에 `_ID` 를 붙인다 — 병렬 작업자), 한영 숫자 혼합에서 중립 문자가 앞 런의 글꼴을 이어받는 동작(의도로 정할지), 가짜 굵게가 레이아웃 폭을 안 바꿈, 한글 · 숫자 자간 측정, MSDF 베어링 단언, HarfBuzz 경로의 `.ttc` face index 0 고정.

**다음 세션에서 할 일(순서)**
1. push 뒤 `py -3 -m Scripts ci-jobs --summary` 로 main CI 를 본다(Linux 컴파일 · STL containers `UIDeterminismTest` · 서버 Shipping). 지면 `--annotations` 로 원인을 읽는다.
2. 위 GPU 확인(`gpu-verifier`).
3. [ArchitectureSeams](ArchitectureSeams.md)의 0 단계(측정, 통합 혼자)와 1a · 1b · 2a · 3 · 4(병렬 가능) 착수. 리팩토링 트랙은 같은 문서 3 절. (팩 쿠킹 병렬화 `packfast` · 저장 전용 압축 타입 `packedtypes` 는 통합 I 에서 들어갔다.)
4. 후보로 올린 것: ① **시나리오 단계 인자를 리플렉션 구조체로 선언**해 스키마를 자동으로 만들고 린트에서 시나리오 XML 을 검증, 탐침을 `Probe` 속성이 달린 프로퍼티에서 노출(단계 인자가 이미 스키마를 갖는지 먼저 코드로 확인). ③ `scenario-logs` 래칫을 `suite` 항목에도.
5. 사용자 결정은 [ArchitectureSeams](ArchitectureSeams.md) 4 절의 둘(GPU CI 자동화, 작업자 빌드 금지 규칙 재검토)뿐이다.

**교훈(이번 세션)**: ① 기본 설정에서만 깨지는 결함(프로파일러 꺼짐)은 단위 테스트가 프레임 번호를 직접 넣어 못 본다 — 기본 구성으로 도는 시나리오가 잡아야 한다. ② 종료 코드 0 · 14 만 보면 Error 가 있는 시나리오가 초록이었다(`scenlogs` 가 막는다). ③ 작업자가 단언을 완화하는 쪽으로 풀면 근본 원인이 남는다 — 보고에서 "원인을 못 찾았다" 를 놓치지 말 것. ④ 워크트리에는 `graphify-out/graph.json` 이 없다: MCP 에 `project_path="D:/Projects/Personal/LearningTemplate"` 를 넘긴다. ⑤ **빌드하지 않은 브랜치의 일괄 치환(`countContaining` → `countWithID`)은 기대 수까지 틀린다**: 기계적 치환도 그 테스트를 한 번 돌려야 한다. ⑥ 문서만 바꾼 커밋은 CI 빌드를 건너뛰므로 Lints 가 지는 동안 빌드 결함이 몇 커밋씩 쌓인다 — push 뒤 CI 가 `Lints` 만 보고 초록이면 빌드 잡이 돌았는지 본다. ⑦ 한 구성(Debug 클라이언트)에서만 돌면 `ClientCode` · `SourceResources` 선언이 빠진 케이스가 안 보인다 — 통합이 Shipping · Shipping-Server `nogpu` 를 한 번 돌리면 CI 보다 먼저 잡힌다(이번에 64 · 66 통과).

## 0000. 이전 통합에서 남은 것

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

## 00. 2026-10-25 통합 N 뒤 작업자 브랜치 체인 — 코드만 짜 둠, 병합 · 빌드 · 검증 전 (이 절을 먼저 읽는다)

통합 N 은 검증을 마치고 main 에 fast-forward 했다(맨 위 `## 0000000000000` 절). **push 하지 않았다.** 아래 브랜치는 모두 린트만 통과했고 빌드 · 테스트는 한 번도 돌지 않았다 — 통합이 체인 순서로 병합하며 한 번에 짓는다.

**병합 체인(앞 브랜치 위에 쌓였다. 각 브랜치는 `wt/<이름>`, 워크트리 D:/w/<이름>)**
- 종료 코드: exitcodes → exit77 → exit77b → exitreturn → exitreturn2 (`AutomationExitCode.h` 정본, `ExitCode` 값, `[exitCode]` 규칙)
- 자동 분리: autosplit
- 해상도 배율: qualityadj → qualityadj2 → hostview → hostscale
- 서버 디버깅: serverdbg → serverdbg2 → serverdbg3 → serverdbg4 (한 줄 답 상한 3500, `sync-tab-object-diff-scenario` 시나리오 프레임 간격은 통합이 맞춘다)
- 카메라 컷: camcut → camcut2 (`GameObjectManager::getUpdateSerial()` 추가로 광범위 재빌드. 렌더 스레드 `_bCut` 소비 — `FrameRenderer.cpp:562` `makeMainSettings` 가 어느 스레드인지 확인)
- 경로 매처 테스트: pathpat5 (pathpat4 위)
- 임포트 규칙 · 경로 패턴: importrule → importrule2 → pathpat2 → pathpat3 → pathpat4 (pathpat4 는 매처를 Core 로 옮긴다 — 컴파일 확인이 가장 큰 위험)
- 소스 스캔 게이트: scangates → scangates2
- 서버 빌드 목록: x11scope → serverlist → modtargets (wlserver 와 겹침 확인)
- 프리팹: prefabmove → … → prefabmove6 (`findReferenceTo` 가 TypeRegistry 필수, 거절 로그는 경고 — 하네스가 실패로 잡는지 본다)
- Online: onlinekits → onlinetables → onlinedrain2 → drainsweep (`OnlineDrainGate` 가 Core `DrainGate` 로 올라가 이름이 바뀐다. OnlineKits.md 102행 근처 충돌 가능) ; onlinekits → onlinemax → maxkits → minmaxconst → rangeconst2 (두 가지가 Online 헤더에서 겹친다. `Min`/`Max` 상수 이름 코드젠이 들어가므로 **파서(`Tools/ReflectionParser`)를 가장 먼저 짓고 온라인 스키마 해시 테스트로 해시가 그대로인지 본다**)
- 에디터 서비스 무음 null — 두 가닥, 둘 다 prefabmove6 위: ① svcnullA → svcnullD → svcnullE → svcnullG → svcnullI ② svcnullB → svcnullC → svcnullF → svcnullH → svcnullJ. 가닥 ① 을 먼저 병합하고, 가닥 ② 의 `// 필수 서비스: 없으면 버그` 줄(grep)을 `editor::getSubsystem` 으로 한 번에 치환한다. `refuseSceneEdit` 경고는 ① 의 상태 억제와 ② 의 `SW_LOG_WARNING_ONCE`(`Core/Log/LogOnce.h` 에 `CallSite` 추가) 중 하나로 통일한다.
  - **가장 큰 위험**: svcnullE 가 `assertEditorThread` 를 Release 에서도 켰다(어기면 오류 로그 + abort). 병합 직후 Release/Shipping `nogpu` 와 에디터 자동화 시나리오로 먼저 확인한다. `TestEditorInspectorCommands` 의 `PrefabCommandsRefuseWithAReasonWhileSceneEditsAreHeld`(로그 3건)도 본다.
- 단독 한 개씩: charint(E6 `surfacelit`), msdf, modrp, xfileref, wlserver, unreadcands, maxcountconst

**통합 몫(작업자가 못 한 것)**
1. charint: `docs/plans/pending/surfacelit/Resource/` 의 `.pending` 파일을 `Resource/` 로 옮기고 `App --cook-shaders`.
2. importrule2: `App --import-textures` 한 번(engine/textures/ui DDS 19 개와 `import.stamp` 만 바뀌어야 한다). `TestAssetValidation.py` 시간(캐시 뒤 5초).
3. serverdbg: ko 번역(ko.translation.xml · po/ko.po) 4 키. 새 시나리오 `netsyncobjects` 실행.
4. exitreturn2 이후 `py -3 -m Scripts lint-suite` 전체. 새 `[exitCode]` · `CheckModuleTargets` · `CheckX11Isolation` 규칙 모두.
5. 수정된 `Tools/OnlineLoadBot` 컴파일(전엔 깨져 있었다 — 어느 게이트에도 안 들어 있다면 넣는다).
6. 예전부터 남은 통합 몫: `complex-text-install`(ThirdPartyGaps T2/T3b), SilentFallbackAudit 의 RenderTargetFormat PSO 경로 열거(임시 로그 + 네 백엔드), 글리프 MSDF 측정, module-pass-scenario 문턱 0.02, Q3 자동 배율 실측.

**이 세션에서 훅으로 막은 것**: `.claude/hooks/GuardCommands.py` 가 `python - <<EOF` · `bash -c` 로 감싼 것 · `cat/tee > 파일 <<EOF` 와 `find /` 를 실행 전에 거절한다. 작업자 지시문 첫 줄에도 적는다. 이 세션에 훅 이전 구간에서 작업자 7 명이 heredoc 편집을 했고 `python -` 7 개가 CPU 를 돌았다.

**사용자 결정 대기**: push 여부(마지막 CI 는 Build Linux Shipping(Clang) 실패였고, Wayland smoke · X11 INCR · TSan 은 push 뒤 처음 돈다), `D:/w` 바로 아래 작업자 산출물(`e.py`, `kh_*.log`, `vb.bak` 등) 삭제 허락, 말 모델(WildWestHorseMount W3) CC0 에셋과 라이선스.

**같은 결함 후보로 남긴 것(후속 작업자 후보)**
- `NetStatsReport` 의 `net.stats` 줄 길이 상한, Sync 탭 클라이언트 질의 시간 초과(오류 응답을 영원히 기다린다).
- 프리팹 참조 검사: 프리팹 파일 XML 안 덮어쓰기 값 · 씬 수준 데이터(참조를 담을 자리가 아직 없어 하지 않음).
- 에디터 `EditorContext::get()` 는 null 을 허용해 그리기 안에서도 확인이 붙는다. svcnullJ(0c93e8463)가 그리기 경로 약 20 곳을 `getSubsystem` 으로 바꾸고 센 결과: `Source/Editor` 에 209 곳(73 파일) + `GameFramework/PlugIn/Editor` 11 곳, 그리기 경로로 보이는 곳 약 90(Panels 54 · Popups 17 · Common/GUI 13 · Viewport 4, 호출 문맥은 일부만 읽음), 명령 · 셀프테스트 · 수명주기 · 도구 약 120. 같은 모양 후보(미확인): `EditorViewportCamera.cpp:140·170·211`, `SelectionBoundsVisualizer.cpp:106`, `CommandPalettePopup`(8) · `QuickLauncherPopup`(5) · `BoneHierarchyPopup`(4). 이 수로 `get()` → `find()`/`require()` 이름 분리(그리기 쪽 약 40%)를 판단한다. `getSubsystem` 은 Release 에서 단언이 빠져 null 역참조가 된다(폴백 금지 원칙으로는 Release 에서도 멈추게 해야 한다).
- `ConfigReference.py` 는 다른 헤더의 상수를 못 푼다(같은 파일만, 못 찾으면 멈춤). 한 줄 답 상한(serverdbg4)은 `net.stats` 줄에 아직 안 걸려 있다.
- `dx11rbwait` 는 끝났다(c3c27ab7f · 8dbc1bab0): 코드는 안 바꿨고 `readReadbackBuffer` 의 `DO_NOT_WAIT` 는 `WaitIdleLeavesTheSubmittedCopyReadableOnEveryBackend` 탐침 때문에 그대로이며 주석 2 줄과 5-5 결정 줄만 고쳤다. 완료 콜백 경로에서도 `WAS_STILL_DRAWING` 거짓 실패가 나면 콜백 경로만 기다리는 Map 으로 가른다.
- 마지막 작업자 둘(`crowdfix`: `shooter3d.crowd` 의 `CrowdSharedUnits` 가 1, `vfxgl`: GL 에서 `vfxgpulights` 가 안 보임)은 base 가 main(a8f380c83)이다. 끝났는지는 `git branch --list "wt/dx11rbwait" "wt/crowdfix" "wt/vfxgl"` 와 각 브랜치 `git log -3` 로 본다. 앞의 둘은 main 에서도 지는 결함이다(통합 N 이 main(1d9680081)을 따로 지어 확인).
- RHI 파일 후보: DX11 allocateUploadStaging 의 RHIStagingAllocator 합치기, Vulkan 프레임 밖 펜스 대기, 큐 소유권 이동, DX11 Map(DO_NOT_WAIT) vs 장치 소실, DX11 Present 실패 로그 반복, GL 비트필드.

**사용자 결정 대기**: 말 모델(WildWestHorseMount W3)의 CC0 에셋과 라이선스 선택뿐이다. push 는 통합 N 이 main 에 들어간 뒤 사용자에게 먼저 묻는다.

**다음 세션의 첫 일**: ① 통합 N 은 main 에 들어갔다(push 는 사용자 확인 뒤). ② 위 체인을 순서대로 하나의 `integ-o` 에 병합하고 통합 몫 1 ~ 6 을 한 번에 돈다. ③ 그 사이 `[병렬 가능]` 인 Linux · GPU 검증, Vfx 실행 확인, 카메라 디렉터, 미리보기 창, 오디오, 런타임 UI 묶음은 파일이 겹치지 않으니 작업자를 따로 띄울 수 있다([Roadmap](Roadmap.md)). 작업자 지시문에는 "python/py heredoc 편집 금지, `find /` 금지" 를 맨 앞줄에 적는다(이번 세션에서 7 번 어겼다).
