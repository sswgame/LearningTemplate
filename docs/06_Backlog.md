# 작업 백로그 — 남은 일

> 여러 PC · 여러 세션이 함께 쓰는 **할 일 목록**입니다. 아직 열린 일과, 그 일을 시작하기 전에 알아야 할 함정만 둡니다.
>
> - 일을 끝내면 그 항목을 같은 커밋에서 **지웁니다.** 남길 교훈은 그 영역 README 의 "함정 · 계약" 절에, 정한 방향과 기각한 안은
>   [결정 기록](09_Decisions.md) 에, 검증 · 측정 방법은 [검증과 측정](08_Verification.md) 에 한두 줄로 옮깁니다. 무엇을 고쳤는지의 사연은 커밋 메시지에 씁니다.
> - 새로 찾은 결함과 미룬 일은 아래 분류표에서 **한 곳**을 골라 적습니다. 결정을 기다리는 질문은 F 에 적습니다.
> - 항목 번호(`A-3` 같은 것)는 **다시 매기지 않습니다.** 항목을 지워도 다른 항목의 번호는 그대로이고, 새 항목은 그 분류의 마지막 번호 다음을 씁니다.
>   코드 주석과 문서는 번호로 가리키고, 번호 뒤 따옴표 안에 항목 제목의 첫 낱말을 적습니다.
> - 여러 PC 에서 고치는 문서라 작업 중에 pull 과 충돌이 날 수 있습니다. 커밋 전에 받아서 합칩니다.
> - 지나간 일의 이력은 `git log` 에 있습니다. 2026-10-03 까지의 전체 이력이 든 옛 백로그(약 19,400 줄)는 커밋 `7ce95fc8` 의
>   `docs/06_Backlog.md` 입니다 — `git show 7ce95fc8:docs/06_Backlog.md`. 2026-10-13 에 영역 번호(1-1 ~ 1-12)를 아래 분류로 바꿨습니다.
>   옛 번호로 쓴 글은 맨 아래 "옛 번호 대응표" 에서 찾습니다.

---

## 분류표

| 분류 | 뜻 | 항목 수 |
|---|---|---|
| **A. 결함 · 정확성** | 재현됐거나 코드로 확인된 열린 결함. 다른 어떤 일보다 먼저 본다. | 6 |
| **B. 검증 · 측정 대기** | 코드는 들어갔고 돌려 보는 일이 남은 것. 이 PC 에서 못 돌린 환경(리눅스 · 실기)과 사용자 눈이 필요한 것도 여기다. | 14 |
| **C. 구조 · 성능 · 정리** | 기능이 아니라 고칠 곳을 줄이는 일: 구조 분할 계획, 중복 · 의존 정리, 측정 뒤 정할 성능 후보, 린트 · 문서 정리. | 19 |
| **D. 기능 로드맵** | 영역별로 묶은 새 기능 · 기능의 남은 조각. 위에 있을수록 먼저 볼 것이다. | 51 |
| **E. 관찰 중** | 간헐 실패 · 원인 미상. 다시 보이면 그때 원인을 판다. CI 읽는 법은 [검증과 측정](08_Verification.md) 4절. | 8 |
| **F. 결정 대기** | 사용자 결정이 있어야 움직이는 질문. | 1 |
| **G. 조건부 · 낮은 우선순위** | 조건이 오면 하는 일(쓰는 게임이 생기면, 측정해서 이기면 …)과 이득이 작은 후보. | 12 |
| **H. 보류 (사용자 결정)** | 사용자가 "나중에"로 정한 것. 풀 때까지 하지 않는다. | 3 |

**일을 고르는 순서**: A(결함) → B(검증 · 측정) → C(구조 · 성능) → D(기능). E 는 다시 보일 때만, F 는 사용자 답이 오면, G · H 는 조건이 오거나 풀릴 때 본다.

---

## A. 결함 · 정확성

재현됐거나 코드로 확인된 열린 결함. 다른 어떤 일보다 먼저 본다.

- `A-1` **XML 문자열 속성은 읽을 때 끝 공백(줄바꿈)을 잘라 왕복이 고정점이 아니다**(2026-10-06, `SerializationRoundTripTest` 가 찾음). `MissingComponent::_originalText`
  가 줄바꿈으로 끝나 다시 쓰면 `&#10;` 이 빠진다 — 지금은 오브젝트 상태 직렬화기가 원문을 그대로 다시 써서 데이터는 잃지 않고, 시험은 이 타입을 뺀다.
  끝 공백이 뜻을 갖는 문자열 칸이 생기면 XML 읽기의 자르기를 속성 값에서 걷어낸다.

- `A-2` **JSON 소유 포인터 원소가 `{ "타입이름": {...} }` 꼴이 아니면 말없이 건너뛴다**(`JSONSerializerInternal::ContainerReader::readOwnedPointer`). 실패로 알리면 `_listComponent`
  칸 전체가 실패하므로 그 원소만 orphan 으로 남기는 길이 필요하다(XML 은 태그가 곧 타입이라 이 모양이 없다).

- `A-3` **머티리얼 폴백 원소(`FrameRenderer::ensureMaterialFallbackBuffers` — 0 바이트)의 텍스처 인덱스 칸이 0 이다** — Vulkan 은 bindless 0 번 실제 텍스처를 읽는다(DX12 는 0 번을 null SRV 로 비웠다, 머티리얼은 `MaterialPacking` 이 `kInvalidIndex` 를 넣는다). 원소 레이아웃(`ShaderBindingSlot`)은 stride 만, 리플렉션 원소 칸은 `uint` 뿐이라 텍스처 칸을 가를 수 없다 — 셰이더 쪽 표식(텍스처 칸 매크로가 리플렉션에 남기는 이름 규칙)으로 칸을 알아 `kInvalidIndex` 로 채우거나, Vulkan 도 0 번을 null 서술자로 비운다.

- `A-4` **`editor/materialpreview` 가 OpenGL 에서 한 번 졌다**(2026-10-11, `AppScenarioTest.EditorScenariosPassOnOpenGL` 샤드 안 — 단독 3/3 통과). `WaitUntil Editor.MaterialPreviewRedMinusBlue <= -40` 이
  1200 프레임을 기다리고도 +180(빨강)이었다 — 시간이 아니라 미리보기 렌더 타깃이 갱신되지 않은 상태에 머문 것이다. 재현되면 미리보기 재렌더 요청(속성 변경 → 렌더 타깃 갱신) 경로를 GL 에서 본다.

- `A-5` **에디터 체인에 남은 것**(체인 · 초기화 · 리로드 4 종은 들어갔다 — [Automation README](../Source/Engine/Automation/README.md) "에디터 시나리오를 한 프로세스로 묶기"). ① **Vulkan 에디터 체인에서 `readbackTexture2D: texture … or the device is not usable` 오류 3 줄이 난다** — 리셋 시나리오가 창 크기를 바꾼 직후, 이전 시나리오가 열어 둔 RenderTarget 패널(`RenderTargetPanel.cpp` ~203)이 다시 만들어지기 전의 옛 텍스처 핸들로 읽기를 요청한다(`RenderThread::readRequestedRenderTarget`). 실사용에서도 패널을 연 채 창 크기를 바꾸면 같은 오류가 상태바에 뜬다. 핸들이 죽었으면 요청을 건너뛰거나(패널 · 레지스트리가 크기 변경 세대를 본다) 읽기 실패를 조용한 실패로 돌려야 한다. 체인 테스트는 이 줄을 시나리오 밖 `[Error]` 로 센다.
  ② `isolated` 로 뺀 시나리오 중 **상태가 남는 것**(`graphaddnode` · `profilertimeline` · `tilemaptools` · `sequencerkeys` · `contentthumbnails`)은 씬 교체로 안 비워지는 패널 상태다 — 패널 쪽에 "문서 닫기 / 초기화" 를 두고
  `panel.closeOptional` 이 그것을 부르게 하면 체인에 합칠 수 있다(지금은 프로세스 다섯 개가 더 뜬다). ③ 체인 사이의 초기화가 프레임 50 개(≈ 체인 시간의 20 %)다 — 씬이 다 열렸는지를 탐침으로 기다리면 줄일 수 있다.

- `A-6` **명령줄로 준 Engine 전역 변수를 사용자 설정 기동 적용이 덮는다**(2026-10-11, 디버그 HUD 확인 중). `UserSettingsManager::applyToTarget` 은
  `CommandLineManager::findPendingGlobalValue` 로 "명령줄 값이면 건너뛴다" 를 묻는데, 보류표에는 **모듈** 전역 변수만 남고 Engine 전역 변수(파싱 때 이미 등록된 것)는 없다.
  그래서 `-gv_debugHUD=1` · `-gv_uiScale=1.5` 같은 설정 대상 변수는 기동 적용이 기본값 · 저장값으로 덮는다(App `-gv_debugHUD=1` 스크린샷에 HUD 가 없었다).
  고칠 곳은 명령줄이 준 이름 전부를 묻는 질의(등록된 변수 포함)다. 지금은 HUD 를 `hud on` · Ctrl+F3 로 켠다.

---

## B. 검증 · 측정 대기

코드는 들어갔고 돌려 보는 일이 남은 것. 이 PC 에서 못 돌린 환경(리눅스 · 실기)과 사용자 눈이 필요한 것도 여기다.

- `B-1` **8000 무버의 `components`(onTick) ~325 us — 구조 변경은 들어갔고 전/후 측정이 남았다(2026-10-13).** 타입별 일괄 틱(`TickBatchRegistry`, 무버가 첫 사용처) ·
  대기 칸 목록을 청크마다 한 번 잡기 · 틱 뒤 대기 칸 적용을 고른 구간으로. 재는 법: Release `App -gv_benchMeshes=8000 -gv_benchTickMovers=1 -gv_profileFrames=600`
  을 `-gv_tickBatch=1` · `0` 으로 번갈아 3 회(같은 바이너리), 그 앞뒤 커밋 바이너리도 번갈아 — `GT.Scene.tick.components` · `objectRange`(CPU 합) ·
  `pendingSlotApply` · `queuedTransforms` 의 p50 · p99 와 카운터 `objectEntries` · `batchElements` · `pendingSlots`(per_frame 열). 지면 되돌린다.
  다음 단계 후보: 무버 데이터를 타입 배열(SoA)로 옮겨 일괄 함수가 컴포넌트를 읽지 않게(대상 씬 컴포넌트의 칸 번호를 원소에) — 측정 뒤에 정한다.

- `B-2` **직렬화기(이름 대조 · 텍스트 파싱)와 리소스 로드의 리플렉션 비용, 비동기 씬 로드 중 최악 프레임을 측정하지 않았다.** 큰 씬 · 쿠킹본으로 측정한다(Dev 는 `Cooked/`
  를 마운트하지 않는다).

- `B-3` **에디터 구조 개편(층 게이트 · 의존 역전 · Panels 하위 폴더) 뒤에 돌리지 않은 검증.** 3 · 4 단계 뒤 네 백엔드 에디터 시나리오(`AppTest --test_filter=*Editor*`),
  Shipping 빌드와 `-L hostgpu`, 게임별 프리셋 빌드(ThemePark 밖의 에디터 확장 모듈)를 배치 끝 검증에서 본다. 결정은 [결정 기록](09_Decisions.md) 5-11.

- `B-4` **에디터 작은 단위(E5 · G2 · G3 · H1)와 실기동으로 아직 확인하지 못한 것.** 리눅스 X11 · dlopen 분기(CI), 실제 RenderDoc 캡처(이 PC 에 RenderDoc 이 없다),
  단언 대화상자의 세 단추(모달), Tracy 0.14.1 뷰어 연결(주소 `localhost:8086` 을 직접 입력해 연결되는지), 배율 1.5 · 2 의 에디터, Dialogue Graph 첫 열기,
  스크린샷 토스트의 "Show in Explorer" 단추(알림 매니저에 동작 단추가 없다), `screenshotbutton` 시나리오가 `Bin/Saved/Screenshots` 에 남기는 PNG, Engine README 개발 명령 줄의 경로.
  RenderDoc 은 RHI 단계 맨 앞에서 로드하고, `ThirdParty/renderdoc` 에 원문 헤더를 두며 그 폴더의 `.clang-format` 은 `DisableFormat` 이다.

- `B-5` **커버리지 안내 퍼저(`LoaderFuzzer`)는 아직 한 번도 지어지지 않았다**(2026-10-06 들임, 리눅스 전용 — 이 PC 는 WSL 을 쓰지 않았다). 첫 `fuzz.yml` 실행
  (밤 또는 `workflow_dispatch`)이 구성 · 링크(`-fsanitize=fuzzer-no-link` 엔진 + ASan, `-fsanitize=fuzzer` 실행 파일)와 대상마다 60 초가 끝나는지 본다.
  지면 그 실행의 주석 · 아티팩트(`fuzz-findings`)로 고친다.

- `B-6` **리눅스 RPATH 는 CI 결과로 확인한다**(사용자 결정 2026-10-10 — 로컬 WSL 빌드는 하지 않는다).

- `B-7` **리눅스에서 아직 자동으로 안 도는 것**: X11 입력(좌표 · `XkbSetDetectableAutoRepeat` — WSLg 의 `DISPLAY=:0` 이 있으니 손으로 한 번), yad 파일 대화상자의 두 번째
  `--file-filter`(man 으로만 확인), 리눅스 CI 가 초록인지 · IPO 가 실제로 켜졌는지(2026-10-05 실행은 네 잡 모두 Configure 의 vcpkg 설치에서 졌다 — 다음 실행부터
  `CiFailureReport.py configure` 가 포트 로그 끝을 주석으로 올린다). 2026-10-05 WSL-Debug 로 돌려 확인한 것(net-ci-rest 제안서 조사): POSIX `pipe2` · `close_range` ·
  `launchDetached`, `alarm` 시한, dlsym 도장, `parseWriteTime`(int64 를 넘는 스탬프 시각), 리눅스 ThinLTO(`llvm-ar`) 링크.

- `B-8` **150 % 모니터 실물 확인** — 대체 시험(`dpi.monitorScaleFollows`: 창 DPI 질의를 +0.5 로 바꿔 끼움 · WM_DPICHANGED)이 글자 · 여백 · 창 · 백버퍼를 본다.
  남은 것은 글자 래스터 선명도 — 150 % 모니터에서 에디터를 띄워 눈으로 한 번(2026-10-06 이 PC 의 기동 로그가 `Editor UI scale 1.5 (monitor DPI)` 였다).

- `B-9` **패킷 압축의 남은 것**(UDP 압축을 끈 결정과 측정값은 [결정 기록](09_Decisions.md) 5-9). WSL Release 로 `NetCompressionBenchTest` 를 한 번 돌린다(압축 시간은
  컴파일러 · libc 에 따라 다르다). 채팅 수치는 채팅 키트가 실제 기록을 내면 다시 잰다. 스트림 프레임 압축은 채팅 기록 · 거래 내역을 실을 키트가 zstd 로 켤지 정한다.

- `B-10` **MMO 규모 서버의 UDP 소켓 계층.** 지금은 호스트당 논블로킹 UDP 소켓 하나 + 전용 스레드 하나(`poll`/`WSAPoll`), 데이터그램마다 `recvfrom`/`sendto`, `NetHost` 잠금 하나.
  UDP 는 소켓이 하나라 IOCP · epoll 은 지렛대가 아니다. ① Windows 측정은 [검증과 측정](08_Verification.md) 2절 "UDP 소켓 계층" 에 있다(패킷당 약 6.5 us, 대부분 `recvfrom`/`sendto`).
  리눅스(WSL Release, `ulimit -n 8192` 뒤 같은 필터)는 아직이다 — 수치를 나란히 놓는다. 그다음 일은 ②.
  ② 시스템 호출이 지배적이면 `INetTransport` 일괄 받기 · 보내기 하나의 계약 — 리눅스 `recvmmsg`/`sendmmsg`(+ GSO/GRO), Windows RIO(+ USO/URO, 없으면 `WSARecvMsg`)
  ③ 스레드 하나가 차면 포트 샤딩(두 플랫폼 같은 계약 — 로그인 토큰이 포트를 준다, 리눅스는 `SO_REUSEPORT` 를 선택) + 샤드마다 `NetHost`. 둘 다 `NetUDPBenchTest` 숫자로 전후를 측정한다.

- `B-11` **어노테이션 키 완성은 clangd 만 켜져 있다 — Visual Studio IntelliSense(EDG)는 확인하지 못했다.** `ReflectionMacros.h` 의 조건에
  `defined( __INTELLISENSE__ )` 를 더하면 VS 도 같은 풀이(`ReflectionIntellisense.h`)를 읽는다. 풀이는 MS 호환 모드의 dependent base 찾기 ·
  `_Pragma( "clang diagnostic" )` · static_assert 안의 제네릭 람다 지역 클래스에 기댄다. EDG 가 이것을 오류 없이 읽는지 먼저 본다.
  DTE `ErrorList.ErrorItems` 로는 IntelliSense 오류가 나오지 않아(일부러 넣은 오류도 0건) 자동 확인 길을 찾지 못했다.
  손 확인 순서: 조건에 `__INTELLISENSE__` 를 더하고 VS 로 저장소 폴더를 연다 → `CameraComponent.h` 의 `PROPERTY( ` 안에서 Ctrl+Space 로 키 목록을 본다 →
  오류 목록(IntelliSense 만)에서 어노테이션 헤더에 새 오류가 없는지 본다.

---

- `B-12` **통합 3(`8fce26aaf..6f3373831`, 81 커밋)의 검증이 반쯤 남았다.** 옛 빌드 폴더는 `cmake --preset` 을 먼저 다시 돌린다(파일을 옮겨 코드젠 목록이 낡으면 `missing and no known rule`).
  - **자동**: Debug `-L hostgpu`(`ctest -j 1`, 약 16 분), Shipping `-L nogpu` · `-L hostgpu`, 게임 프리셋 넷(`Ninja-Debug-Shooter3D` · `-StarSkirmish` · `-HarvestValley` · `-VoxelCraft`) 빌드(경고 0 · 링크 0),
    `ctest -N` 개수를 병합 전과 견줘 옮긴 테스트(`TestDismemberment` · `TestWitcherRPG` · `TestTacticsSRPG` · `TestProgression` · `TestSpatial` · `ProfilerTimeline`)가 사라지거나 두 번 잡히지 않았는지.
  - **처음 돌린 테스트는 기대값부터 의심한다**: `ReflectionEnumInfoTest`(`CameraRole::Editor` → `Viewport`), `GameObjectManagerTest.PostTickOnComponentSkipsPendingDestroy`, `GameObjectManagerPoolTest.ModuleUnloadDrains{Deferred,Requeued}PostTickWork`,
    `ServiceClientCallTableTest.ForeignResponseDuringSendDoesNotTakeTheSendingDelegate`, 온라인 클라이언트 취소 테스트 둘 · `KitDestroyedWhileFailingQueuedCallsGetsNoMoreCalls` ·
    `KitDestroyedDuringReadyNotificationIsSkipped`(후자는 테스트 서버 Hello 가 `kGame+0x100` · `+0x200` 키트를 받아 주는지에 달렸다 — 판 불일치로 Ready 에 못 가면 테스트 결함부터), `TestGameService`(회수 · `ExplicitOwnerDecidesWhichImageReleasesTheService`),
    `ModuleHostTest.SharedModuleCannotBeRegisteredForReload`(Windows 대소문자 케이스 포함), `PrefabTest.CookedPrefabMatchesItsSource`(틀리면 쿠킹본이 어긋난 것인지 쓰기가 불안정한 것인지부터),
    `TestRHISupport` 두 스레드 `tickCompleted`, `TestRenderPassGPU` 동시 `acquire`(DX12 · DX11 만 — 작업 스레드 안 `SW_TEST_DEFENSIVE_SCOPE` 가 스레드마다 따로 동작하는지), `EditorPreferencesStoreTest.ReloadedSectionReadsTheFileOnce`.
  - **실기동으로 볼 동작 변화**(`-gv_profileFrames=60` 으로 스스로 끝나게): 에디터 씬 뷰 카메라가 게임 뷰 수집에서 빠지고 BugIt 캡처가 정상 · 씬을 바꾼 뒤 되돌리기 기록이 비고 종료가 깨끗 ·
    탈것 · 말 · 카메라 감독 · 물 · 지형 · 식생 · 플레이어 청크가 틱 뒤 실행 교체 뒤에도 정상(탑승 중 내림 · 오브젝트 파괴에서 크래시 없음) · 키트 하나만 리로드했을 때 `GameService` 가 새 인스턴스를 가리킴 ·
    에디터 복사 → 에디터 모듈 리로드 → 붙여넣기(`ObjectCopyBuffer`) · 환경설정을 고친 직후 리로드해도 값이 남음(새 섹션 읽기 포함) · `registerModule` 이 공용 모듈(대소문자만 다른 이름 포함)을 거절 ·
    Shooter 적 체력이 `Vitality`(0 아래로 안 내려가고 최대 1 미만이면 1 — **데이터 체력 0 이 예전엔 즉사, 지금은 1 로 산다**; 쓰러짐 · 절단 확인) ·
    텍스처 로드 중 다른 키 조회가 막히지 않고 같은 키 동시 요청이 한 번만 만듦, 리로드 직후 한 프레임 안에 머티리얼 SRV 가 새 것으로(네 백엔드) · DX12 에서 두 스레드 `tickCompleted` 안전 ·
    온라인 응답이 보내는 중 다른 id 로 와도 현재 호출이 안 사라짐 · 로그 범주 이름 변경(`AbilitySystemComponent*` · `ScheduleSystem*` 가 파일 이름으로 — 로그 필터 도구가 있으면 맞춘다) ·
    에디터 HUD 섹션 · 개발 콘솔 명령(락 추가, 모듈 로드 교착 없음) · 디버그 HUD 섹션을 켜고 끄면 프로파일러가 따라 켜지고 꺼짐 · `GameInstanceBase` 의 `GameSettings` 소유자가 게임 모듈 리로드 뒤 회수 · 다시 걸림 ·
    실패 콜백 · Ready 알림 중 키트가 파괴돼도 크래시 없음 · 모듈 언로드 때 `Deferred work kept re-queueing` 오류 로그가 정상 경로에서 안 남.
  - **정적**: `dumpbin /exports` 로 `GameFramework.dll` 이 `SW_GF_API` 심볼과 `bindGameService` · `unbindGameService`(C 링크)를 내보내고 `SWGame.dll` 이 가져오는지. 같은 이름의 반사 헤더 둘이면 구성이 `FATAL`, 구성 뒤 기존 헤더에 반사 매크로를 넣으면 빌드가 서는지
    (`cmake/Engine/CheckReflectionCandidates.cmake`, `--target <타깃>` 만 지으면 검사가 안 돈다). `StringBuilder::append( string )` 과 `VulkanRHIRequiredVersion` include 빌드.
  - **CI**: push 뒤 첫 전체 CI 다. 리눅스: `__builtin_return_address` 분기(`GameService` 소유자 태그), GLX 컨텍스트, 다중 상속 경고, `SceneNavigation` 의 `NavDebugView` 값 멤버(내보내지 않은 타입), `OpenGLRHICommandList.cpp`.
  - **Release 측정**: 텍스처 로드 중 다른 스레드의 `find` · `acquire` 대기, `EngineLoop.tick` 의 틱 뒤 실행 경로 · `AudioEngine` 게시 · `GT.GPUScene.build`, `GameObject.h` 가 `TagSystem.h` 를 끌어오지 않는지(`-ftime-trace` 전후).
  - 끝나면 워크트리를 `py -3 -m Scripts worktree-remove <이름>` 으로 지운다(`tcore` `tinv` `tcombat` `tgfs` `tgfd` `tposttick` `utex` `upipe` `uxml` `ugfa` `ugfb` `uaud` `uonl` `umod` `uinc` `integ3`), 이어서 `git branch -d wt/<이름>`.

- `B-13` **구조 리팩토링 이동(S12 ~ S22)의 측정과 테스트가 남았다**(2026-10-12 측정 없이 옮김 — 사용자 결정. 나쁘게 나오면 그 단위를 되돌리거나 훅을 합친다).
  - **측정(Release, `-gv_profileFrames`, 3 회, VSync 에 붙지 않았는지 먼저)**: `GT.GPUScene.build`(S17), `GT.Editor.updateUI` p50/p99(S12; `-EnableEditor -dx12 -gv_profileFrames=600`, 전의 `commandsAndWatchers + panels` 대 후의 `frameSteps`),
    프레임당 `EngineLoop.tick` · `UISystem` · `FrameRenderer` 구간 전후(`tick` 은 이제 `engine::getDebugDrawQueue()` 를 매 프레임 부른다 — 비가상 호출 셋 증가).
  - **S21 · S22 는 Debug 빌드 · 링크까지만 확인했다**: SmokeTest 소스 목록에 `EditorModuleHostHeadless.cpp` 가 있는지(새 `SW_API` 클래스 포함), `-L nogpu`(`NavMesh*` · `UserSettingsFileTest` · `AppHeadlessTasksTest` · `EditorFolderListing` 테스트),
    Shipping(단일 백엔드 정적 링크, `SW_PROFILE_COMPILED` 꺼짐), Linux(GLX 컨텍스트 · 다중 상속 경고), `OpenGLRHICommandList.cpp`(compile_commands 에 없어 문법 검사도 못 했다), 에디터 실기동(Present 훅),
    `assertdialog.scenario.xml`, `-gv_reloadGameAtFrame`, `-gv_standardInputCommands`. S18 의 새 클래스는 ImGui 에 의존해 단위 테스트가 없다 — 에디터 자체 테스트 · `AppScenarioTest` 에디터 케이스가 본다.

- `B-14` **셰이더 쿠킹 스탬프에 컴파일러 도구 지문을 넣는 커밋 `8021b9337`(`SWCOOK 3` → `4`)이 통합에서 일부러 빠졌다**(`54b50db93` 이 되돌림). Shipping 쿠킹 단계가 `--verify-shaders`(`cmake/Engine/AssetAndToolTargets.cmake` 71 줄)로 쿠킹본과 스탬프를 대조하고,
  체크인된 쿠킹 셰이더 약 3424 개와 `cook.stamp` 가 `SWCOOK 3` 이라 다시 쿠킹 없이는 빌드가 선다. 쿠킹 가능한 환경에서: `54b50db93` 을 되돌린다 → `App.exe --cook-shaders` → 쿠킹 셰이더와 `cook.stamp` 를 같은 커밋에 넣는다 → Shipping 빌드가 `--verify-shaders` 를 통과하는지 본다.

## C. 구조 · 성능 · 정리

기능이 아니라 고칠 곳을 줄이는 일: 구조 분할 계획, 중복 · 의존 정리, 측정 뒤 정할 성능 후보, 린트 · 문서 정리.

계획 문서가 있는 일: **엔진 분할** [docs/plans/EnginePartition.md](plans/EnginePartition.md) · **빌드 속도** [docs/plans/BuildSpeed.md](plans/BuildSpeed.md) ·
**서버 디버깅** [docs/plans/ServerDebug.md](plans/ServerDebug.md)(D7). 단위를 끝내면 계획 문서에서 지운다.

- `C-1` **엔진 분할 — 계획은 [docs/plans/EnginePartition.md](plans/EnginePartition.md).** `Engine.dll` 을 티어 경계로 나누기 전에 의존 방향의 약한 고리를 푼다. 단위를 끝내면 계획 문서에서 지운다.

- `C-2` **빌드 속도 — 계획은 [docs/plans/BuildSpeed.md](plans/BuildSpeed.md).** 남은 것은 5 단계(유니티 범위 — 한 쌍만 잼, 재측정 필요)와 6 단계(`EngineLoop.cpp`)다. 범위 결정은 [결정 기록](09_Decisions.md) 5-2.

- `C-3` **키트 DLL 이 Base 클래스의 암시적 멤버를 내보낸다**(2026-10-12 통합 3 에서 확인, `llvm-readobj --coff-exports GF_WitcherRPG.dll` 에 `Inventory` · `Crafter` · `StatBlock` 의 생성자 · 소멸자).
  키트 TU 는 강제 포함 헤더로 `SW_GF_EXPORTS` 를 정의하고(`cmake/Engine/ModuleTargets.cmake` 의 `GF_EXPORTS`), Base 헤더와 키트 헤더가 같은 `SW_GF_API` 를 써서 Base 선언도 dllexport 로 풀린다.
  고치려면 키트 자기 헤더용 매크로를 따로 두고(언리얼의 모듈마다 `<MODULE>_API`) 키트 헤더를 기계적으로 바꾼다. `GameServiceBinding.h` 의 `SW_GAMESERVICE_API` 도 같은 규칙을 따른다.

- `C-4` **GameFramework 구조 정리(2026-10-04 리뷰, 사용자 승인).** 남은 것 —
  - 중간: Overworld `TileMap` 의 칸 손셈(`indexOf` · `isInBounds` — 크기는 파일 스키마 `TileMapXMLData` 가 든다) · `Habitat`(CreatureLife) · NetConnection 메시지 버퍼 재사용 ·
    게임 손셈(HarvestValley `FarmCropComponent` · `FarmSoilComponent`, NileCity `NileDirectorComponent` 의 `index % getWidth()`)은 키트가 `getTopology()` 를 열면 같이.

- `C-5` **GameFramework 구조 리뷰에서 남은 것(2026-10-05 — 공통 모듈 · 디렉터 베이스 · 층 게이트는 끝남).**
  ⑤ `EngineLoop.cpp` 의 절반이 기동 단계 구조체(낮음).

- `C-6` **파괴 잎 셰이프를 플레이 첫 프레임에 짓는 비용**(측정값은 [검증과 측정](08_Verification.md) 2절 "파괴 첫 프레임"). 파괴물이 많은 맵이면 선형으로 는다.
  워커로 나누기는 졌다(결정 기록 3절) — 남은 후보는 쿠킹 때 Jolt 셰이프를 직렬화해 `.fracture` 에 싣기(Chaos 가 지오메트리 컬렉션에 충돌을 같이 굽는 자리) 또는
  잎 셰이프를 처음 깨질 때까지 미루기.

- `C-7` **조건부 후보 묶음.** 병렬 틱 임계값의 교차점 · GameObject 레이아웃 · 적응형 틱 임계값 · 스폰 비용(~1.1 us, 잠금 여섯) · 시퀀서 성능 수치. TickItem 인라인
  재시도는 오브젝트의 틱 부기 49 B 를 먼저 줄여야 한다. 측정해서 이기면 한다.

- `C-8` **월드 공간 UI 를 대량으로 그리는 비용을 재고, 넘으면 GPU 로 옮긴다.** RTS 이름 · HP 바 2,000 개 시나리오로 게임 스레드(마커 투영 · 그리기 목록)와 렌더 스레드
  비용을 Release 로 측정한다. 게임 스레드가 0.5 ms 를 넘으면 위치 투영 · 컬링 · 간접 드로우를 컴퓨트로 옮긴다(메시의 GPU 커맨드 생성과 같은 구조; 언리얼은 대량
  머리 위 표시를 Niagara 로 권한다). 데미지 숫자처럼 시간 함수로만 움직이는 UI 의 GPU 애니메이션도 같은 측정으로 본다.

- `C-9` **글리프 SDF 를 컴퓨트로 만들지 재고 정한다.** 한글 장문을 처음 띄우는 프레임(새 글리프 수백 개)의 최악 시간을 Release 로 측정한다. 히치가 4 ms 를 넘으면 FreeType 은
  외곽선만 읽고 SDF 는 컴퓨트(점프 플러딩)로 만든다.

- `C-10` **FrameRenderer 진단 세터**(`setMeshMorphDiag` · `setDrawMergeEnabled` · `setVertexPoolEnabled`)는 Shipping 제외 후보.

- `C-11` **규칙 예외 감사(2026-10-07)에서 정한 셋의 적용** — 두 플랫폼 공통 명시적 경고 목록, `(void)` 이유 주석은 `[[nodiscard]]` 실패 가능 함수에만,
  출처를 모르는 리소스 교체.
  결정과 근거는 [결정 기록](09_Decisions.md) 5-2.

- `C-12` **imgui-node-editor vcpkg 오버레이**(`ThirdParty/imgui-node-editor/vcpkg-port/`, `<exception>` 패치)는 업스트림이 같은 고침을 받으면 지운다(2026-10-06 확인: 업스트림 vcpkg port-version 4 · 원본 master · develop 모두 아직 없음. vcpkg PR 은 내지 않는다 — 사용자 결정, 외부 공개).

- `C-13` **include · 전방 선언 남은 후보.** ① (끝) OS 헤더는 `EngineMinimal.h` 에서 뺐다(2026-10-11 — Win32 · POSIX API 를 쓰는 파일이 `Core/Common/PlatformOsHeaders.h` 를 직접 include,
  효과는 [검증과 측정](08_Verification.md)). Windows 는 전 구성이 빌드로 확인됐고 리눅스 쪽 직접 include 는 CI 로 확인한다 — 빠진 파일이 나오면 그 파일에 include 를 더한다.
  ② `GameObjectManager.h` 의 값 멤버 서브시스템 7 개(`ScenePhysics` · `SceneAudio` · `SceneOverlapWorld2D` · `PrimitiveRegistry` ·
  `ComponentRegistry` · `CameraRegistry` · `AnimationSystem`)를 `unique_ptr` 로 바꾸면 GOM 을 include 하는 234 TU 에서 헤더 32 개(약 4,700 줄, 서드파티 없음 —
  Physics/* · Navigation/* · Animation 보조)가 빠지지만, getter 를 쓰는 85 파일이 직접 include 해야 해 실제로 덜어지는 것은 약 150 TU × 4.7k 줄이다 — 보류
  (2026-10-05 재측정, `_store` · `_transformHierarchy` · `_structuralChangeBuffer` · `_tickScheduler` 는 헤더의 인라인 · 템플릿이 써서 포인터로 못 뺀다).
  이득은 `ninja -t deps` 전후 TU 수로 판정한다. `SceneNavigation` 은 엔진 분할 2 단계 첫 단위로 `unique_ptr` 로 뺐다(2026-10-11 — 코어 헤더가 Navigation 을 퍼뜨리지 않게).

- `C-14` **CI 린트 잡이 처음으로 린트 전부를 리눅스에서 돈다**(`RunLintSuite`, 2026-10-06 — 그 전엔 `CheckCodeConventions` 하나). 첫 실행에서 지는 게이트가 있으면
  그 게이트의 결함(경로 대소문자 · 줄끝 · 외부 도구)이다 — 고치고, 급하면 그 단계에 `continue-on-error` 를 한 번 두고 여기 적는다. 훅 상한
  `kHookBudgetSeconds`(1: 8 · 10: 12 s)는 이 PC 기준 짐작이다 — 첫 `lint-timing` 아티팩트를 보고 러너 값의 1.5 배로 고친다.

- `C-15` **Scripts 정리의 남은 후보.** `CheckFunctionVocabulary` 의 손으로 짠 주석 가리기를 `common.CodeText` 로 바꾼다.
  `dev/MoveEditorState.py`(옛 위치의 에디터 로컬 상태를 옮기는 PC 마다 한 번짜리 이동기)는 모든 PC 가 돌렸으면 지운다 — 추적되지 않는 파일을 옮기므로 돌렸는지 저장소에서는 알 수 없다.
  커밋 훅 바닥을 더 내리는 안은 [결정 기록](09_Decisions.md) 3절 "`CheckConfigReference` 를 파일 단위로" 에서 기각했다.

- `C-16` **6 차 주석 작업(새 문체로 다시 쓰기)에서 처리하지 못한 구역.** 같은 방식으로 `py -3 -m Scripts worktree-make c6<이름>` 후 이어서 한다.
  Engine 은 Graphics · Renderer(`Graphics/Mesh`, RHI 하위, 작은 파일 약 220 개), Object · Scene · Resource · Serialization · Spatial · Common 대부분, UI 의 `Console`, Compression · Observability · Profiling · Telemetry 전부다.
  `SceneNavigation.h` · `GameObjectManager.h` 는 충돌 때문에 다시 쓰지 못했다.
  Editor 는 주석 25 줄 이상인 약 45 개(`Common/GUI`, `Commands`, `Panels/Animation` · `TileMap` · `Viewport` 등)와 그보다 짧은 파일이다. `EditorIconGlyphs.h` 는 생성 파일이라 제외한다.
  GameFramework 는 Base 의 짧은 헤더 약 230 개와 .cpp · .inl, Kits/Feature 의 옛 어휘(꼬리표, 그새, 거두다, 짓다)가 남은 약 35 개(Network · Online · Voxel 등), Kits/Feature/Online 의 Account 밖 폴더다.
  Core 는 Math, File, Process, Delegate, Memory, Diagnostics, CommandLine, Container, Task, Network 나머지, LogSink, String 이다. RuntimeAPI 는 손대지 않았다. Games 는 Empty 벤치 씬 일부와 VoxelCraft 전체다.
  '시험' → '테스트' 는 `Scripts/**` · `Test/**` · `docs/Config/**` 278 곳과 셰이더 주석 2 곳(`waterwaveprobe.hlsl` · `lighting2d.hlsli` — 바꾸면 `App --cook-shaders` 로 다시 쿠킹)이 남았다.

- `C-17` **다음 이름 점검**(2026-10-13 이름 점검에서 남긴 것). 기준은 [AGENTS.md](../AGENTS.md) "Class suffixes" 절과 [결정 기록](09_Decisions.md) 5-1 이다. 아직 보지 않은 것:
  ① `Context` 접미어 타입이 무엇을 담는지, ② `*Util` 정적 클래스 중 한 타입의 메서드가 된 것이 나은 것,
  ③ `Unit` 계열(812 회), ④ 남은 지역 · 멤버 변수 단독 `value` 약 50 곳,
  ⑤ `Settings` · `Config` 접미어(`NetHostSettings` 등)를 고르는 기준.

- `C-18` **100 줄 넘는 함수 정리.** 분해는 총량을 줄이지 않는다. 중복을 먼저 없애고, 그래도 문제면 본다. 목록이 필요하면 여러 줄 시그니처를 중괄호 깊이로 측정하는 스크립트로
  뽑는다(단순 정규식은 틀린다).

---

- `C-19` **구조 리팩토링의 남은 후보.**
  - **자체 포맷 로더를 리플렉션으로(R4)**: `SpriteClipAsset` · `SequenceAsset` · `Skeleton` · `AnimationGraphAsset` · `DialogueGraphAsset` · `LocalizationDocuments` · `CultureInfo`. 파일 하나가 한 단위라 서로 충돌하지 않는다.
    먼저 판정한다 — 적지 않은 값의 의미가 필요한가(부모 상속, "없음" 과 "0" 구분)? 그렇다면 옮기지 않는다(기준은 [결정 기록](09_Decisions.md) 5-4). 옮긴다면 구조체에 `REFLECT` · `PROPERTY`,
    `ConfigManager::readConfigJSON` 으로 읽기(모르는 키 · 읽지 못한 값 · 범위는 거기서 오류), 의미 검사만 직접. 데이터 파일은 키를 `_이름` 규약으로 다시 쓰고(옛 이름 별칭 없음) 설정 카탈로그를 `typeName` 방식으로 바꿔 문서를 생성한다.
    검증: 그 로더의 기존 테스트, `ResourceDataSchemaTest`, 설정 · 리소스 스키마 게이트.
  - **`getBoundEngineServices()` 직접 읽기**: `SceneEngineServices` 주입(S10)과 같은 패턴이 범위 밖에 남았다 — `FacialAnimationComponent` · `WidgetComponent` · `UISystem` · `FontService`,
    `SceneCooker` · `SceneNavigationCooker` · `PortraitRenderer` 의 인자 없는 `Scene` 생성.
  - `UIAutomationSteps::ensureLinked` 호출(`UISystem::initialize`)은 Engine 이 통째 링크라 불필요해 보인다 — 지울 수 있는지 확인.
  - `Joint2DComponent` 는 Cone 을 고르면 `createJoint` 를 프레임마다 다시 시도해 오류 로그를 반복한다.
  - 시나리오 체인의 에디터 쪽 후속(R7)은 `A-5` 의 "에디터 체인에 남은 것" ②③ 과 `D3-6` "백엔드 교체 단계" 가 원본이다. `ImGuiEditor` 는 만지지 않고 패널 파일만 다룬다.

## D. 기능 로드맵

영역별로 묶은 새 기능 · 기능의 남은 조각. 위에 있을수록 먼저 볼 것이다.

### D1. 오브젝트 · 물리 · 내비게이션

- `D1-1` **중첩 프리팹이 정식으로 없다.** 프리팹은 GameObject 하나(컴포넌트 트리)라 메시 · 이펙트 여럿을 담을 수는 있지만, 프리팹 안에 다른 프리팹의
  인스턴스를 오버라이드와 함께 두는 형식은 없다 — 지금은 컴포넌트가 로드 뒤 스폰하는 우회뿐이다(순환 스폰 방어는 있다, `PrefabTest.CircularReferenceSpawnProtection`).
  아이템 부품 · 탈것 · 건물처럼 조립하는 것이 늘면 필요하다: 상태 안의 프리팹 인스턴스 노드(경로 + 오버라이드), 원본 프리팹을 고치면 모든 상위가
  따라감, 순환은 로드 오류, 쿠킹은 펼치거나 참조 유지(로드 시간으로 고름), `collectReferencedPrefabPaths` 가 프리로드 수집에 그대로 쓰임.

- `D1-2` **강체 물리의 다음 조각.** (1) 볼록 껍질 · 삼각 메시 셰이프를 `.mesh` 에서 채우는 길이 없다 — 지금은 셰이프 서술자에 점 · 인덱스를 직접 적는다
  (`MeshCache` 의 CPU 정점이 필요하다) (3) 에디터에 물리 컴포넌트의 셰이프 시각화 · 기즈모가 없다(`gv_physicsDebugDraw` 가 씬 뷰의 디버그 선으로만 그린다)
  (4) Box2D 는 한 스레드로 돈다 — 2D 바디가 수천이 되면 전용 워커를 붙인다 (5) 3D 질의는 가장 가까운 것 하나 · 겹침 목록뿐이다(레이의 모든 닿음 ·
  스윕 다중 닿음이 필요해지면 더한다) (6) 겹침 월드(`PhysicsOverlapWorld` · `BoxCollider2DComponent`)는 강체 씬과 따로 돈다 — 키트의 투사체 · 근접 판정이
  Box2D 센서로 옮겨 가면 겹침 월드를 걷어낸다.

- `D1-3` **3D 내비메시(Recast & Detour, `Source/Engine/Navigation/README.md`)의 남은 것.** 베이크(쿠킹 · 런타임) · 경로 · 레이캐스트 · 군중 · 장애물 · 파괴 재베이크 ·
  디버그 선 · Shooter3D 적은 들어갔다. (1) 오프 메시 링크(사다리 · 점프 · 문)와 그 애니메이션 (2) 지형 높이장 · 식생을 베이크 기하로(`INavGeometrySource`)
  (3) 장애물이 많고 자주 움직이면 TileCache(압축 층) — 지금은 타일을 통째로 다시 베이크 (4) 에디터의 내비메시 보기 · 베이크 버튼 (5) 행동 트리 이동 노드가
  `INavMover` 를 쓰게(격자 · 내비메시 공통) (6) 다른 테스트 게임(AbilityArena 등)의 적도 내비메시로.

- `D1-4` **Jolt 소프트 바디(천 · 헤어 카드)는 아직 감싸지 않았다.** 들인 서드파티와 감싼 위치는 [결정 기록](09_Decisions.md) 5-2. vcpkg 를 바꿀 때의 잠금은 [작업 방식](11_Workflow.md) 1절.

### D2. 그래픽스 · RHI · 셰이더

- `D2-1` **2D 의 남은 것(2026-10-04 twod-basics)** — (1) 파이프라인을 게임이 데이터로 고르는 자리(지금은 `-gv_renderPipeline` 뿐 — 게임 프리셋 · gamesettings 에)
  (2) 픽셀 퍼펙트의 Upscale Render Texture(기준 해상도 타깃 + 정수 업스케일 패스) (3) 2D 빛 텍스처 · 자유 모양 빛 · 부드러운 그림자 · 빛 블렌드 스타일
  (4) 타일맵 청크(한 레이어 65536 칸)와 편집 중 미리보기(지금은 플레이 때 배치를 만든다) (5) 테두리를 픽셀로 적는 9-슬라이스.

- `D2-2` **방향광 그림자의 남은 것(shadow-fix)** — (1) 볼륨 맞춤(`_shadowViewDistance`)을 쓰는 게임은 ThemePark 하나다 — 직교 리그를 쓰는 NileCity · StarSkirmish ·
  HarvestValley 와 envshowcase(고정 150 m)를 옮겨 볼 것(스크린샷 전후). 바이어스가 텍셀 단위로 줄어 다른 여섯 게임의 여드름은 아직 눈으로 안 봤다(게임별 빌드). (2) 원근 카메라는
  캐스케이드 하나라 멀리 갈수록 텍셀이 크다 — CSM(분할 2~4, 경계 섞기). (3) 추가 뷰(CCTV · 분할 화면)도 주 시점 볼륨을 쓴다 — 다른 곳을 보는 뷰는 그림자가 빠진다
  (뷰별 맞춤이면 뷰별 그림자 패스 상수). (4) 깊이 첨부 readback 이 없어 `-gv_screenshotAttachment=ShadowMap` 이 실패한다(`readbackTexture2D 실패`).
  (5) 카메라 회전 중 2 m 양자화 경계에서 가장자리가 한 번씩 튈 수 있다 — 거슬리면 경계 구(회전 불변)로.

- `D2-3` **점광 · 스폿 그림자** — RHI 텍스처 차원(배열 · 큐브, 면 단위 타깃 · 올리기 · 읽기)은 있다. 남은 것: 그림자 패스 다중 뷰(면 여섯) → 셰이더 쪽(DX12 · Vulkan
  큐브 · 배열 bindless 테이블, DX11 · GL TextureCube 슬롯) + `swSampleShadowAtWorld`. 3 단계 전에 큐브 대신 2D 아틀라스(Unity URP · Godot — RHI 변경 없음)로 갈지 먼저 정한다.

- `D2-4` **컷 준비(프리웜)의 선행 조건 셋**(1-6 카메라 항목의 5 단계가 기다린다) — LOD 시스템이 없다, 밉 단위 텍스처 스트리밍이 없다(`AssetStreamingQueue` 는 에셋
  단위 비동기 읽기), PSO 를 미리 만드는 창구가 없다(DX12 PSO 생성 히치).

- `D2-5` **환경(지형 · 식생 · 물, `Engine/Environment`)의 남은 것** — 들어간 것과 계약은 [Environment/README.md](../Source/Engine/Environment/README.md).
  (1) Jolt 가 들어오면 지형 → `HeightFieldShape`(`TerrainHeightfield::getHeightSamples` · 구멍 칸) · 부력(`WaterBodyComponent::computeSurfaceHeight` · `isUnderwater`, 시간은
  `getWaveTime`)을 잇는다. (2) 강 경로가 점 목록이다 — 스플라인 컴포넌트가 들어오면 그것을 경로로 받는다. (3) 하늘 · 시간 · 높이 안개 + 물속 안개 패스(값은
  `WaterBodyComponent::findUnderwaterFog` 가 이미 준다) — 다중 뷰 병합 뒤. (4) 물의 굴절 · 화면 공간 두께는 반투명 패스가 장면 색 사본 · 장면 깊이를 입력으로 받는
  계약이 있어야 한다(지금은 지형 깊이를 정점에 굽는다). (5) 흔드는 식생의 그림자는 흔들리지 않는다(`shadowdepth.hlsl` 이 머티리얼 정점 변형을 모른다 — 풀은 그림자를 끔).
  (6) 지형 LOD 교체 프레임의 최악 시간이 기준(GT 2 ms)을 넘는다(측정값은 [검증과 측정](08_Verification.md) 2절 "지형 LOD 교체"). 메시 집합이 바뀌어 RT 는 정점 풀을
  통째로 다시 만든다(`RT.GPUScene.vertexPool`, 벤치에는 없다 — App 표로). LOD 메시를 미리 만들어 두거나 청크 정점을 풀에서 부분 갱신한다. 지오모프(LOD 튐) · 레이어 다섯 이상(두 번째 스플랫 — 머티리얼 텍스처 칸이 넷이다) · 에디터 칠하기 도구가 없다.

- `D2-6` **툰 머티리얼(`toon.hlsl`, MToon 1.0 체계)의 남은 것** — 노멀 맵(정점에 탄젠트가 없다) · UV 스크롤 애니메이션 · 셰이딩 시프트 / 림 곱 / 외곽선 두께 텍스처(머티리얼 텍스처 칸이 넷이라 기본 · 그림자 · 발광 · 맷캡만 받는다) · 디퍼드의 계단 셰이딩(G버퍼는 표면만 적어 램버트로 칠해진다) · 그림자 패스의 알파 컷오프
  (`shadowdepth.hlsl` 은 픽셀 스테이지가 없어 머리카락 카드가 사각형 그림자를 드리운다 — 모든 컷오프 머티리얼이 같다).

- `D2-7` **VRM 임포트의 남은 것** — 머티리얼(MToon) · 구간 메시 · 스켈레톤만 옮긴다. 표정(모프 타깃 · `blendShapeMaster`) · 스프링 본(`secondaryAnimation`) · humanoid 본 표 · firstPerson 은 읽지 않는다(0.x · 1.0 모두). 본 메시(`<이름>.mesh`)는 구간들을 다시 합친 것이라 디스크에 두 벌이다(VRoid 34k 삼각형 7 MB × 2) — 엔진 메시에 머티리얼 구간이 생기면 하나로 줄인다. VRoid 텍스처는 BC3 이다(Debug DirectXTex 의 BC7 은 512×256 한 장도 10 분이 넘는다 — Release 로 BC7 임포트를 다시 할 것). `ModelImporterTest.SkinnedModelImportsSkeletonClipsAndAttachments` 는 Debug 에서 혼자 31 초라 EditorTest 한도를 30 → 120 초로 올려 두었다 — 임포트를 줄이면 되돌린다.

- `D2-8` **캔버스(화면 2D)의 남은 것(runtime-ui 4-4)** — (1) 화면 사각형 뷰(PiP · 분할 화면)는 주 시점의 Canvas 뒤에 그려져 UI 를 덮는다 — 뷰를 다 그린 뒤 주 출력에
  한 번 그리게 옮긴다(언리얼은 모든 장면 뷰 뒤 Slate). (2) 렌더 텍스처 대상(월드 공간 UI)은 텍스처가 처음 만들어질 때의 크기로 그린다 — 위젯이 크기를
  나중에 바꾸면 텍스처를 다시 만들지 않는다(`TextureCache::declareRenderTarget` 은 이미 만든 것을 바꾸지 않는다).

- `D2-9` **반해상도 후처리** — 첨부별 `_resolutionDivisor`(1 · 2 · 4)는 있다. 남은 것:
  `deferredpipeline.xml` 블룸을 반해상도로 나누기, Release 로 p50 · p99 측정.

- `D2-10` **월드 좌표 UV(트라이플래너) 프로토타입 머티리얼** — `engine/materials/prototypegrid_*` 는 메시 UV 를 따라 큐브 크기마다 칸이 늘어난다. 언리얼 `WorldGridMaterial` 처럼 월드 1 m 격자로 보이려면 셰이더 분기(월드 위치로 샘플)가 필요하다.

### D3. 에디터

- `D3-1` **에디터 로드맵 — 미룬 영역 패널을 그 영역 옆에(에디터 보강에서 남김).** 엔진 영역 패널은 EditorModule, 기반 GameFramework 영역은 `GF_EditorPlugIn_Base`(아직 없음 — 
  `Source/GameFramework/PlugIn/Editor/Base/`), 키트 영역은 `GF_EditorPlugIn_<이름>` 확장 모듈에 둔다. 체감 · 선행 순서로:
  ① 프로젝트 설정 창 — 생성 메타데이터 `docs/Config/ConfigReference.json` 으로 `Config/` 와 팩 설정을 찾아 `EditorPropertyGrid` 로 고치고, `ConfigManager::reloadConfigFile` 로 다시 읽고,
  기본값과 같은 값은 파일에서 지운다(언리얼 · 유니티 Project Settings). ② 물리 셰이프 시각화와 콜라이더 크기 손잡이(`SW_EDITOR_VISUALIZER`, 1-2 강체 물리 (3)).
  ③ 내비메시 보기와 베이크 커맨드 `navigation.bake`(`GF_EditorPlugIn_Base`, 1-2 내비메시 (4)). ④ 오디오 믹서 패널(버스 미터 · 음소거 · 솔로 · 볼륨, 1-6 오디오).
  ⑤ GM 도구 패널(`GF_EditorPlugIn_Admin` — `AdminClient` 로 조회 · 지급 · 제재 · 감사 열람). ⑥ 기믹 회로 그래프 편집 창(`EditorGraphDocumentPanel` 틀, 1-6 기믹).
  ⑦ 카탈로그 편집기 — 카탈로그 계약 하나가 `ResourceDataSchemaTest` 의 종류 표를 대신하고, `CityCatalog.cpp` 의 하드코딩 개수를 포함해 25 곳에 흩어진 enum 이름 표를 모은다.
  Data Table 의 게임 데이터 탭(지금 XML 글 상자)을 리플렉션 구조체 행의 표 편집으로 바꾸는 일도 여기다(언리얼 DataTable · DataAsset 편집 + Fix Up Redirectors).
  ⑧ 지형 칠하기 도구 모드(1-3 환경). ⑨ 다중 월드 툴 창과 그 위의 애니메이션 · 리그 · 프리팹 · 머티리얼 미리보기 창(언리얼 FPreviewScene · Asset Editor Toolkit).
  ⑩ UI 문서 디자이너(⑨ 뒤). 에디터에 이미 있는 것(전역 변수 표, 커맨드 팔레트, 핫 리로드, Undo, Play 와 Step, InputReplay, 기즈모, RenderTargetPanel, 프로파일러와 Tracy)은 다시 만들지 않는다.

- `D3-2` **Hierarchy 형제 순서 끌어 바꾸기 · 정리용 폴더(에디터 보강 N2 에서 남김).** 루트 줄은 오브젝트 id 순이고(`EditorSceneCommands::collectRootsInOrder`) 씬 파일에 순서 칸이 없다.
  끌어 바꾸려면 씬 형식에 형제 순서를 적고(유니티 sibling index) 그 순서로 그려야 한다 — 형식을 바꾸니 데이터를 다시 쓴다. 폴더는 저장되지 않는 빈 오브젝트(언리얼 Outliner 폴더)로 같이.

- `D3-3` **UI 미리보기 패널의 남은 것(runtime-ui 8-5 뒤, `Editor/Panels/UIPreview/UIPreviewPanel`).** (1) 언어 고르기(의사 문화권 `qps-ploc` · `qps-plocm` 포함) — 문화권이 전역이라
  미리보기 화면에만 거는 길(화면별 `LocalizationManager` 출처)이 필요하다. (2) 고른 위젯의 PROPERTY 를 인스펙터로 고치고 `UIDocumentWriter` 로 저장(되돌리기 —
  `CommandStack`) — 지금은 이름 · 사각형만 보인다. (3) 미리보기 안 입력 흉내(마우스 · 탐색 방향). (4) 콘텐츠 브라우저에서 `*.ui.xml` 두 번 누르면 이 패널로 —
  에셋 종류 `UIDocument` 의 열기 동작. (5) 애니메이션 미리 보기(재생 · 시간 막대 — 미리보기 화면은 Open 을 틀지 않아 문서 값 그대로다). (6) UI 문서 디자이너(팔레트 → 끌어 놓기 · 슬롯 손잡이) — 미리보기 패널 위에.

- `D3-4` **콘솔 · 치트의 남은 것(옛 "에디터 · 개발 편의 기능" 의 D)** — 게임 · 키트의 치트 명령(무적 · 아이템 주기 · 돈 …)을 각 게임 · 킷에 `SW_DEV_COMMAND` 로 단다(레지스트리 · 콘솔 ·
  엔진 명령은 들어갔다 — `Source/Engine/README.md` "개발 콘솔"). 리눅스 오버레이(`X11DevConsoleWindow`)는 실기로 띄워 보지 않았다.
  게임 창 콘솔은 셸 InputMap 액션 + `InputManager` 키보드 포커스로 받는다(`Input/DevConsoleController`). 남은 것: 패드는 포커스 밖이라 콘솔이 열린 동안
  패드 A · B · 십자키가 게임에도 간다(shooter3d 는 패드 `Back` 이 `CycleCamera` 와 겹친다), 플레이어별 재배치(`InputMap::loadUserBindings`)가 셸 맵에 걸려
  있지 않다, X11 그리기는 `XDrawString`(Latin-1)이라 한글이 깨진다.

- `D3-5` **입력 흉내 창구(`EditorSelfTestInput` · `EditorSelfTestMarks::note`)로 아직 안 덮은 것** — 그래프 패널 ↔ 저장 커맨드 배선, 인스펙터 콤보 직접 편집, 드래그 드롭.

- `D3-6` **백엔드 교체 단계**(`<SwapBackend backend="vulkan"/>` 같은 것) — 체인이 한 프로세스에서 네 백엔드를 차례로 건너게 한다(지금은 백엔드마다 프로세스 하나). `gv_rhiSwapAtFrame` 의 경로(`RHI::requestBackendChange`)를 단계로 감싸고
  교체 뒤 `Module.*` · 씬 개수 탐침을 같이 본다.

- `D3-7` **다중 월드 · 에디터 툴 창(로드맵, 카메라 4 단계와 한 덩어리).** 지금 `EngineLoop` 는 활성 씬 하나만 틱하고 그리며, GPUScene 빌더 · 스냅샷도 하나다 —
  그래서 프리팹 격리는 활성 씬을 빌리고(씬이 더러우면 막는다) 머티리얼 미리보기는 메인 뷰포트의 선택 오브젝트에 입혀 보인다. ① 다중 월드(미리보기 월드를
  따로 살림, 월드마다 시간 · 틱 정책 · 조명 환경 · 물리 월드) ② 월드별 렌더(씬마다 GPUScene, 뷰 목록의 뷰가 "어느 월드 · 카메라 · 렌더 타깃" 을 고름,
  안 보이는 창은 안 그림 — 카메라 4 단계의 다중 뷰를 다중 월드 × 다중 뷰로) ③ 툴 창 틀(미리보기 월드를 가진 도킹 창, 자기 렌더 타깃 · 에디터 카메라 ·
  기즈모, 창마다 선택 · Undo 범위 — 지금은 에디터 전체에 하나, 문서 계약은 있는 것, 키트 · 게임이 등록하도록 1-4 의 C 확장 지점 위에) ④ 위에 올릴 창:
  프리팹(격리 월드) · 머티리얼(미리보기 구체) · 메시/모델(LOD 비교) · 애니메이션(스켈레톤 · 타임라인 · 압축 오차) · 카메라 프리셋(블렌드 미리보기) ·
  래그돌/물리 에셋(관절 한계) · 이펙트 · 장르 도구(코스터 트랙 · 리듬 차트). 언리얼 FPreviewScene · 애셋 에디터 툴킷, 유니티 PreviewRenderUtility · Prefab Stage.

### D4. 게임플레이 · 캐릭터 · 카메라 · 애니메이션

- `D4-1` **GameCurve(키트 꺾은선)는 FloatCurve 로 옮기지 않았다** — 데이터(`<Curve time scale>`)를 다시 써야 한다. 쓰는 곳(SpawnTable · AIDirectorProfile)을 고칠 때 같이.
  엔진의 커브 값 타입은 `Engine/Utility/FloatCurve`(키 · Constant · Linear · Cubic · 접선, 에디터 그리기 확장 있음)다.

- `D4-2` **사용자 설정(옵션 메뉴 백엔드)의 남은 것** — 백엔드 · 바인딩 API 는 `Source/Engine/UserSettings/README.md`, 메뉴는 `Engine/UI/Screen/OptionsMenuScreen`.
  (2) 값만 있고 읽는 곳이 없는 대상: `gv_renderScale`(업스케일 패스) · 시야 거리 · 후처리 ·
  텍스처 · 이펙트 품질 · 모션 블러 · `gv_colorVisionMode` 의 톤맵 쪽(톤맵에 상수 버퍼가 없어 미뤘다 — 함수는 `colorvision.hlsli`, UI 캔버스는 이미 쓴다),
  카메라 `gv_cameraFieldOfView` · `gv_cameraShakeScale` · `gv_cameraHeadBob`(cam-views 가 읽을 자리). (4) 해상도 선택지를 모니터 모드에서(선택지 공급자) · GPU 사양 조회(RHI 어댑터 · 전용 메모리)로 품질 자동 선택.
  (5) 게임 스키마에 키 바인딩 설정 — 테스트 게임 일곱이 모두 입력 맵(`data/<게임>.input.xml`)을 쓴다(그 액션부터). (6) X11 `setDisplayMode`(EWMH 전체 화면)는 리눅스 실기 미확인.

- `D4-3` **패드 스틱 시점 · 가상 커서(a4-input 이 남긴 것).** 입력 맵에 바인딩마다의 배율 · 프레임 시간 곱(언리얼 Enhanced Input 의 Scale · Scale By Delta Time 모디파이어)이
  없어 오른쪽 스틱을 `Look` · `Camera.Look`(마우스 이동량 = 픽셀/프레임)에 묶으면 프레임률을 따르는 느린 값이 된다 — 그래서 시점 액션은 아직 마우스만이다.
  명령형 게임(StarSkirmish · NileCity)의 패드 A · B 는 커서 자리를 쓰는데 커서를 패드로 옮기는 가상 커서(언리얼 CommonUI 의 아날로그 커서)가 없다.

- `D4-4` **키트 조립 — 남은 것(2026-10-06).** 틀 · 키트 전부 · 조립 시험 둘(`KitCompositionTest` — Farming + CreatureLife, RTS + CityBuilder)과 `MeadowVillage` 는
  들어갔다(규칙: `Source/GameFramework/Kits/README.md` "키트 여럿을 한 게임에"). 남은 것 — RTS 가 땅 리비전마다 땅 격자 전체를 다시 칠하는 비용(64 × 64 에서 재고
  결정, 1-8), 둘째 조립(RTS + City)의 시험 게임(필요해지면 MeadowVillage 모양). 땅을 쓰는 게임은 아직 없다 — 격자 키트를 섞는 게임이 생기면 `SessionStateSettings::_land*` 와 키트의 `bindLand` 를 건다(SRPG 는 전투 끝에
  `releaseLand`, 공원 놀이기구 발자국은 상태 바이트에 싣지 않는다 — 지은 칸은 땅이 든다).

- `D4-5` **카메라 — 프리셋 데이터 · 블렌드 · 시퀀서(사용자 승인 로드맵).** 1~3 단계(프리셋 XML · 블렌드 · 디렉터, 모드(직교 · 궤도 · 따라가기 · 1인칭 · 3인칭 ·
  CCTV) · 입력 · 카메라 매니저(뷰 타깃 블렌드), 흔들림 · 제약 · 프레이밍 · 스프링 암)와 4 단계(다중 뷰 렌더 · 컷 프레임 신호 · 초상화 굽기)는 들어갔다
  (`Source/GameFramework/README.md` "카메라" 절, `Source/Engine/Renderer/README.md` "다중 뷰"). 남은 것 —
  - 2 단계에서 남은 것: 직교 리그 네 게임(ThemePark · Harvest · Nile · StarSkirmish)은 리그 값이 곧 데이터다(모드 · 블렌드는 공유) — 리그를 지우고 디렉터 + 프리셋
    XML 로 옮기려면 게임 디렉터의 `setViewOverride` · `findGroundPoint` 를 디렉터 창구로 바꿔야 한다. 프레이밍의 가로 존은 16:9 로 센다(모드가 화면 비율을 모른다).
    2D(XY 평면) 카메라는 디렉터 밖의 `Follow2DCameraComponent` 다 — 2D 게임이 프리셋 · 블렌드를 원하면 디렉터 모드에 "XY 평면 따라가기" 를 더하고 그 컴포넌트를 지운다.
  - 4 단계에서 남은 것: 예산 · 보임 판정이 거칠다 — 보임은 "지정한 오브젝트가 주 카메라 절두체 안" 하나(가려짐 · 화면 크기 안 봄), 예산은 프레임당 뷰 수
    (`gv_renderViewBudget`)지 시간이 아니다. 화면 사각형 뷰는 에디터 GameView(ImGui 이미지)에서 검증하지 않았다.
  - 5 단계(시퀀서): 값 커브 트랙(시야각 · 초점 · 노출 — 블렌드 곡선과 같은 보간 함수), 카메라 컷 트랙(구간마다 프리셋 · 카메라, 프리셋의 블렌드로 전환 — 언리얼
    Camera Cut Track), 흔들림 트랙, 게임 ↔ 시네마틱 블렌드(시퀀스 시작 · 끝). 지금 시퀀서는 Clip(트랜스폼 보간) · Event 두 종류뿐이다.
    - 컷 준비(프리웜) — 컷 순간 LOD · 텍스처 · 셰이더가 바뀌는 게 보이지 않게. 컷 트랙은 다음 컷을 미리 안다 → 컷 N 초(또는 프레임) 전에 카메라 매니저에
      "곧 볼 시점(대기 뷰)" 을 등록한다(컷마다 · 시퀀스마다 프리롤 값, 언리얼 Sequencer 의 preroll frames). 게임 쪽 뷰 타깃 전환도 같은 창구를 쓴다.
      대기 뷰를 듣는 쪽(시스템이 생기면 붙는다): LOD 선택은 대기 뷰 기준 LOD 도 구해 컷 첫 프레임부터 그 LOD(컷을 넘는 LOD 크로스페이드는 끈다), 스트리밍은
      대기 뷰에서 보일 메시 · 텍스처 밉을 먼저 요청(언리얼 streaming source · AddViewLocation), 셰이더 · PSO 는 대기 뷰 절두체 안 머티리얼의 PSO 를 미리 만든다,
      그림자는 대기 뷰 기준 캐스케이드를 미리 맞춘다. 컷 프레임 신호(`RenderViewSettings::_bCut`)는 TAA 기록만 리셋한다 — 렌더러에 모션 벡터 · 자동 노출 ·
      모션 블러가 아직 없어서다. 그것들이 생기면 같은 신호로 0 · 즉시 맞춤 · 끔을 붙인다. 다중 뷰 렌더(4 단계)로 컷 1~2 프레임 전에
      대기 뷰를 저해상도로 숨겨 그려 업로드 · PSO 를 강제로 데운다. 선행 조건은 1-3 에 있다(LOD · 밉 스트리밍 · PSO 미리 만들기).
  - 6 단계: 렌즈 물리값(초점거리 · 센서 · 초점 거리 · 조리개 · 렌즈 시프트 · 더치) + 피사계 심도 패스(렌더러에 없다), 카메라별 후처리 덮어쓰기(노출 · 블룸 ·
    비네트 · 색 보정 · 레터박스 — 지금은 카메라별로 덮을 길이 없다), FOV kick · 줌 펀치, 돌리 레일(스플라인 — Spline Dolly · Rig Rail).
  - 1 단계에서 미룬 것: 디렉터의 켠 프리셋 · 블렌드 진행은 저장하지 않는다(다시 읽으면 시작 프리셋부터). 블렌드 도중 재활성은 섞인 포즈를 **고정**해 출발한다 —
    Cinemachine 처럼 나가는 블렌드를 살려 둔 채 겹쳐 섞으려면 블렌드 스택이 필요하다.

- `D4-6` **애니메이션(로드맵).** 지금 있는 것은 `Source/Engine/Animation/README.md`(임포트 · 코덱 · 재생 · 상태 기계 · AnimationSystem · GPU 스키닝 · 2D/3D 공용 재생).
  알림 디스패치(구간 알림 · 처리기 레지스트리 · `*.notifies.xml`)는 `Source/Engine/Character/README.md`.
  남은 것 — ① 그래프의 블렌드 스페이스 노드(지금 `BlendSpace` 는 행렬 하나라
  포즈 블렌드 스페이스로 다시 짓는다) · 그래프에 레이어 · 동기 그룹을 데이터로(지금 레이어는 `addLayer` 코드)
  ② 후처리 리그 — 들어갔다(`PoseModifierComponent`, `Source/Engine/Animation/README.md` "후처리 리그" 절 · `Source/Engine/Character/README.md`). 남은 것: 시퀀서 트랙이 `setSlotWeight` 를 쓰기(칸은 있다),
  해석된 소켓 표의 표면 기준 소켓 체형 보정을 리그 대상에도, 에디터 리그 패널(노드 목록 · 대상 · 기즈모) ③ 애니메이션 LOD(가시성 · URO · 보간 · 본 LOD · 예산 · 2D 스프라이트는 들어갔다 —
  `AnimationLOD.h`) — 남은 것: 거리별 IK/물리 끔을 `AnimationLODState`(화면 크기)로(지금 스프링 본은 거리 기준점) · 메시 LOD 가 생기면 본 LOD 를 메시 LOD 와 묶기 ④ 군중 공유(묶음 · 사본 풀 · VAT 쿠킹은
  들어갔다 — `AnimationCrowd.h`) — 남은 것: 섞기 묶음(언리얼 Animation Sharing 의 블렌드 액터 — 지금 섞는 유닛은 사본으로 혼자 평가), Shooter3D 군중이 켜기 · 리타기팅(본 이름 표 · 비율) ·
  얼굴(모프 타깃 임포트 · 표정 커브 · 립싱크 · 깜빡임 · 시선은 들어갔다 — `FacialAnimationComponent`) — 남은 것: 음소 인식 립싱크(지금은 세 대역 모양
  분류 — 모음 넷 · 치찰음 정도만 가른다) · 실제 얼굴 에셋(KayKit 은 모프가 없다 — 합성 테스트 머리뿐) · 스킨 없는 메시의 모프(GPU 모프 풀이 스키닝 컴퓨트 안에서만
  가중치를 건다) · 실시간 얼굴 입력(Live Link Face 자리) ⑤ 파워드 래그돌(관절 모터가 애니메이션 포즈를 쫓음 — 전신 · 부분 래그돌 · 맞음 반응 · 기상 섞기는 `RagdollComponent`) ·
  2 차 움직임: 스프링 본은 들어갔다(리그 노드 `SpringChain`) → 헤어 카드 · 천(Jolt 소프트 바디), 가닥 헤어(TressFX)는 나중 ⑥ KayKit 텍스처(원본 GLB 에 든 `knight_texture` 등)를 `textures_raw/` 로
  옮겨 머티리얼을 만들 것 — 지금 캐릭터는 씬 기본 머티리얼(흰색)로 그려진다. Shooter3D 팩에는 이미 꺼낸 아틀라스(`textures_raw/kaykit_*.png`) · `materials/kaykit_*.material` 이 있다. ⑦ Shooter3D 는 이동 속도와 걷기 · 달리기 클립의 발 속도가 맞지 않아 발이 미끄러진다(재생 속도를 이동 속도에 맞추거나 루트 모션 · 거리 매칭).

- `D4-7` **프리로딩 · LOD · 사전 준비(로드맵).** ① 프리로드 세트(미리 올릴 에셋 + 미리 만들 프리팹 · 우선순위, 쿠킹 때 레벨 · 시퀀스 · 샷의 참조를 따라 자동 수집 —
  `collectReferencedPrefabPaths` 가 있다), `requestPreload` 가 진행률 · 완료를 준다, 프레임 예산(IO · 업로드 · PSO · 인스턴스 수), 참조 수 · LRU 로 내림(지금 캐시는
  약한 참조라 고정 단계가 필요하다) — 언리얼 AssetManager 번들 · Addressables ② 프리팹 풀(숨겨 둔 인스턴스를 켜고 돌려받기 — 탄 · 손님 · 유닛) · 시퀀서
  사전 스폰(프리롤 동안 캐릭터를 숨겨 만들어 첫 포즈 · LOD · 머티리얼을 준비) ③ 메시 LOD(임포트 때 `meshopt_simplify` 로 단계 생성, `.mesh` 판 올림, GPU 컬링이
  화면 크기로 고름, 디더 크로스페이드, 컷 · 대기 뷰 강제 LOD, HLOD 는 나중) ④ 컷 준비가 이것들을 묶는다(카메라 5 단계).

- `D4-8` **캐릭터 외형 편집(로드맵).** 지금: 형상 쪽(아래 ①~④ · ⑤ 의 소켓 이름 공간)은 `Source/Engine/Character` 에 있다(README "외형 조립 순서"), GPU 모프 풀(`Mesh::setGPUMorphEnabled`)은
  있다, 스켈레톤 에셋(`.skeleton.json`)은 본 · 레퍼런스 포즈 · 역 바인드와 임포트가 적은 본 부착 메시 표(소켓 파일을 처음 만들 근거)뿐이고 편집 창구가 없다.
  ①~④ 남은 것 — **통합**: `Mesh` · 포즈 ↔ `AppearanceGeometry` · `CharacterBoneArray` 변환, 체형 모프 · 피팅 델타(`FitPartResult::_listVertexDelta`)를 GPU 모프 풀에
  싣기(스키닝 앞), 병합 결과(`MeshMerger`)를 인덱스 · 정점 버퍼와 구간 그리기로, 애니메이션 시스템이 본이 움직인 프레임에만 `SocketBindingComponent::updateSocketTransform`, 표면 상태(`CharacterSurfaceState`)를 머티리얼 파라미터 · 마스크 텍스처로. **쿠킹**: 장비 정점 → 몸 전이
  (`SurfaceTransferUtil` — 모프 · 스킨 가중치)를 임포트 · 쿠킹 단계에, 아틀라스 굽기(`IMeshMergeHooks` 구현). **핫 리로드**: 소켓 · 레퍼런스 포즈 · 체형 · 피팅 표 · 부품
  피팅 · 표면 채널 파일을 고치면 외형을 다시 조립 — 소비자(외형 컴포넌트)가 `IAssetCache` 로 올린다(로더는 다 있다). **나중**: 천 시뮬레이션(Jolt 소프트 바디)이 같은 겹
  정보를 충돌체로(Mutable 의 Clip with Mesh · Clip Deform 이 같은 문제를 푼다). 참고: 언리얼 Mutable(Customizable Object) ·
  Skin Weights 전이, Character Creator 스마트 핏, Daz 오토핏 ⑤ 장비 해석 — 데이터 · 해석기는 있다(`GameFramework/Base/Gameplay/Appearance` — 슬롯 표 · 세트 · 아이템 외형 · 꾸미기 스키마 · 규칙 · `CharacterAppearance`
  프리셋, 장착 조건은 `Equipment`, 공유 코드 · 플레이어 프리셋 · 네트워크 동기화, 형식은 그 README). 외형 컴포넌트(`CharacterAppearanceComponent` — 프리셋 · 칸 덮어쓰기 → 몸 메시 ·
  소켓 부품 스폰 · 부착 · 포즈 따라가기 · 염색, 소켓 이름 공간 `AppearanceSocketRig`)는 들어갔다. 남은 것: 장비(`Equipment`)를 `CharacterAppearanceState` 로 잇기,
  부품 풀(스폰 대신 숨겨 둔 인스턴스), 외형 상태가 바뀌면 같은 인스턴스를 다른 소켓으로, 떨어져 나감 이벤트를 `SocketBindingComponent` ReleasedPhysics 로,
  결과 해시로 병합 결과를 캐시해 같은 차림의 NPC 가 나눠 쓰고 다시 조립하는 동안 이전 외형 유지(비동기), `finishLoad` 에 몸 영역 표 이름 넘기기, 데이터 파일 감시 →
  `AppearanceDatabase` 다시 읽기, 시퀀서 트랙 · 스폰 · 프리팹이 프리셋을 이름으로 가리키기 · 프리로드 세트가 프리셋 참조를 따라 모으기, 편집 창의
  "지금 모습을 프리셋으로 저장" · 썸네일 렌더 · 규칙 설명(`_listTrace`) 표시. 칸 점유(양손 무기)는 아직 외형만 본다 — 게임플레이에서 보조 손을 막을지는
  게임이 정한다 ⑥ 캐릭터 편집 창(다중 월드 툴 창 위) — 본 트리 + 기즈모 포즈 편집 · 소켓 추가/이동 ·
  체형 · 얼굴 슬라이더 · 장비 입히기와 체형을 바꿔 가며 피팅 확인(관통 표시 · 잘린 면 · 조임 강도 · 보정 조각 · 숨김 영역) · 장비 조합 미리보기(어느 규칙이 무엇을 숨기고 바꿨는지 설명 · 세트 입히기 · 규칙 충돌 표시) · 제약 리그 미리보기 · 애니메이션
  재생 · 좌우 대칭 편집.

- `D4-9` **남은 대기열 — 빠른 순(2026-10-04 사용자 지시, 다른 세션에서 워크트리 에이전트로).** 2026-10-04~05 의 병렬 웨이브(캐릭터 · 물리 · 외형 · 카메라 · 에디터 ·
  기믹 · 환경 · 옵션 · 리플렉션 · 오디오 · 2D · 현지화 · 코어 인프라 · QA 도구 · 애니메이션 셋 · 파괴 · 툰 · 내비 · Tracy · 네트워크 결함)는 모두 main 에 들어갔다.
  진행 방식은 메모리 "워크트리 에이전트 절차" — 결함 먼저 · 겹치는 파일 없게 나누고, 병합은 준비되는 대로, 전체 검증은 큰 묶음 뒤 한 번. **2D · 3D 에 다 쓰이는 기능은 공통 코어로**(사용자 지시).
  - **작음(S)**: 에디터 H(버그 리포트 · 시험 패널) · 단축키 편집기 · 환경설정 창 · 모듈 켜고 끄기 · DPI 실물 확인.
  - **중간(M)**: 대역폭 프로파일러 · 게임플레이 디버거 · 비주얼 로거 · 모듈 패키지 관리 · 점광/스폿 그림자 ·
    HZB 가림 컬링 · 메시 LOD(meshopt) · PSO 미리 만들기 · 에디터 G(보기 모드 — 프로파일러 표 · GPU 타임스탬프 · Tracy · RenderDoc 은 들어갔다) · 에디터 C(확장 지점) · 에디터 F
    (카탈로그 편집기) · 공용 커브 편집기 · 공용 노드 그래프 틀 · 인스펙터 개선 · 에셋 브라우저 · 맵 검사 패널 · 패키징 UI · 시나리오 녹화(실행 중 입력 → `.scenario.xml`) ·
    로딩 흐름의 남은 것(로딩 화면 · 페이드는 `LoadingScreenController` — 진행률(씬 매니저에 진행 질의가 없다 — 스트리밍 큐 바이트로) · 프리로드 세트 연결 ·
    팁 목록 데이터(`LoadingScreenSettings::_listTip` 은 있으나 gamesettings 칸 · 글 수집 규칙이 없다)) · 입력 확장 · 에셋 DCC 내보내기 · 포토 모드 · 리플레이/킬캠(바탕인 의도 기록 `.swintent` 은 있다 — 남은 것: 재생 UI · 카메라 · 되감기) · SSR · 업스케일러 · HDR 출력 · 데칼 · 하늘/시간대/높이
    안개 · 2D 스켈레탈 · 학습용 몫(장르 시작 템플릿 · 튜토리얼 · API 문서).
  - **큼(L)**: 제약 ·
    파티클/VFX · 텍스처
    밉 스트리밍 · 카메라 5 · 6 단계 · 에셋 레지스트리 · DDC · 증분 쿠킹 · 월드 편집 도구 ·
    천/머리카락 · 전술 AI · 볼류메트릭 안개/빛/구름 · 캐릭터 셰이딩 · 모션 캡처 공정 · 대규모 좌표 · PCG 저작 그래프 · GI/반사 프로브 · 플랫폼 서비스 ·
    패치/DLC · 모드/UGC.
  - **아주 큼(XL)**: 비주얼 스크립팅 · 월드 파티션/스트리밍/HLOD · 음성 채팅(온라인 구성은 1-7 "네트워크 서비스 계층").

- `D4-10` **Shooter3D 캐릭터 통합의 남은 것(shooter-int, 2026-10-04).** KayKit 기사 플레이어 · 스켈레톤 적 · 외형 프리셋 · 입력 맵 · 탄도선 풀은 들어갔다(`Source/Games/Shooter3D/README.md`).
  연결할 것(anim-rig · anim-gameplay 는 main 에 들어왔다 — 연결 작업은 따로 정한다) — 리그: 왼손을 총에 붙이는 손 IK(목표는 무기 소켓 `MainHand.SupportHand`), 1인칭에서 머리만 숨기기(지금은 몸 전체를 숨긴다 — 본 숨김 ·
  스케일은 PoseModifier 자리), 스프링 본(망토). 게임플레이: 래그돌 쓰러짐(지금은 클립), 부위 히트박스(지금은 발에서 키까지 캡슐 하나, 중심 소켓 `Chest`),
  물리 질의 히트스캔 · 시야(지금 상자 목록 · 캡슐), 쓰러질 때 무기 떨어뜨리기(`SocketBindingComponent::release( Physics )` — 무기 프리팹에 강체를 더하면 된다),
  알림으로 휘두름 피해 · 발소리(지금 휘두름 피해는 `_attackHitTime` 초). 그 밖: 외형 부품 풀(스켈레톤마다 프리팹을 세우고 지운다), 1인칭에서 숨긴 몸의 애니메이션
  LOD(숨겨도 평가한다), 걷기 · 달리기 발 미끄러짐(위 애니메이션 ⑦).

- `D4-11` **어빌리티 시스템의 다음 조각(쓰는 게임이 생기면).** 언리얼 GAS 에 있고 여기 없는 것: 이펙트가 주는 어빌리티(장비가 스킬을 준다), 걸린 동안의 태그 조건
  (`OngoingTagRequirements` — 기절 중 버프 정지), 태그가 붙을 때 발동(`OwnedTagAdded` 트리거), 큐를 데이터로 이어 주는 큐 매니저(큐 태그 → 프리팹 · 사운드),
  어트리뷰트를 `SaveGame` 에 싣는 도우미, 에디터의 런타임 상태 패널(걸린 이펙트 · 태그 개수 · 쿨다운). 넣을 때마다 `AbilitySystemTest` 에 테스트 하나.

- `D4-12` **상호작용 · 기믹(2026-10-04 들어감 — `GameFramework/Base/Gameplay/Interaction` · `Gimmick` · `Spline`) 병합 뒤 남은 것.**
  - 물리: 기믹 프리팹의 `BoxCollider2DComponent` 에 3D 게임용 3D 트리거 · 강체 콜라이더 변형을 더한다(`Resource/common/prefabs/gimmicks`). 월드 질의 · 카메라 암 ·
    집기 · 눌림판 무게 · 발사대 · 컨베이어는 강체 물리에 이어졌다(`GameFramework/Base/Gameplay/Interaction` · `Gimmick` README).
  - 애니메이션: 상호작용 단계에 몽타주(클립)를 이름으로 잇기 — 맞춤 마커 → 워프 목표는 들어갔다(`InteractorComponent` 가 시작할 때 넣는다).
  - 렌더러: `InteractableComponent::getHighlightRequest`(Outline · Sense)를 읽는 외곽선 · 감각 모드 패스.
  - 에디터: 기믹 회로 그래프 편집 창(노드 · 배선 · 검증 오류 표시, 대상 오브젝트 고르기) — 지금은 인스펙터의 목록 편집뿐.
  - 네트워크: 회로 상태 바이트(`GimmickCircuit::saveState`)를 `NetClientServer` 스냅샷 · `RollbackSession` 상태에 싣기(모양은 준비됨, 배선 없음).

- `D4-13` **의도 기록 · 원격 조종자의 남은 연결(possess-auto C3 뒤).** `NetClientServer` 의 입력 바이트(`ReplicationClient::sendInput` 이 게임이 준 바이트를 싣는 자리)를
  `ControlIntent::write` 로, 서버 폰은 `RemoteControllerComponent` 로 — 폰을 쓰는 넷 테스트 게임이 생기면(지금 0). 의도 기록을 켜고 `.swintent` 를 쓰는 명령줄 ·
  에디터 단추와 재생 UI · 킬캠 카메라 · 되감기는 아직 없다(`ControlSystem::setRecording` · `getHistory().saveToFile` · `IntentTrackControllerComponent` 는 있다).

### D5. 오디오 · 현지화 · AI

- `D5-1` **오디오 엔진(2026-10-04, `Engine/Audio/README.md`)의 남은 것.** 믹서 · DSP · 공간화 · 이벤트 · 스냅샷 · 적응형 음악 · 씬 묶기는 들어갔다. (1) 데이터 핫 리로드 —
  `loadEventLibrary` · `loadMixer` 는 같은 이름이면 바꾸지만 파일 감시(에디터 `FileWatchDispatcher`)에 걸려 있지 않다. (2) 에디터 — 믹서 패널(버스 미터 · 음소거/솔로),
  이벤트 브라우저 · 미리 듣기, 보이스 · 가상화 프로파일러. (3) 긴 음악 스트리밍(지금은 클립을 통째로 디코드해 메모리에 든다 — 3 분 스테레오 ≈ 69 MB float).
  (4) 출력: HRTF(바이노럴) · 5.1/7.1 · 다중 리스너 믹스(지금은 가장 크게 들리는 리스너 하나), 리눅스 출력 백엔드(지금 Null — 오프라인 렌더). (5) 2D 물리(Box2D) 가림 ·
  포털/방 기반 가림(Wwise Rooms & Portals) · 회절. (6) 사이드체인 덕킹(대사 때 음악 낮추기 — 지금은 스냅샷으로), 컨볼루션 리버브, 그래뉼러 · 절차 소리
  그래프(MetaSounds 급). (7) 일곱 시험 게임 중 Shooter3D · StarSkirmish 만 이벤트로 바뀌었다 — 나머지는 아직 `GameSound::play( 경로 )`.

- `D5-2` **로컬라이제이션 — 남은 것(데이터 쪽 파이프라인은 끝, `Engine/Localization/README.md`).** UI 글 위젯은 글 판을 따라 다시 푼다(runtime-ui 6-2). 아직 없는 것: `selectordinal`(서수) · 화폐 · 시간대 · XLIFF · 쿠킹된 이진 표(언리얼 `.locres` — 지금은 JSON 을
  그대로 읽는다) · 아랍어 이외 RTL 문화권 데이터 · `ja` 번역. 아이템 이름(Shooter3D)은 표에 모이지만 화면에 쓰는 코드가 아직 없다(무기 이름은 HUD 가 `getStringByText` 로 쓴다).

- `D5-3` **NPC 하루 일정(`GameFramework/Base/Actor/AI/Schedule`, 2026-10-04) — 연결할 것.** 일정 데이터 · 런타임 · 화면 밖 LOD · 잠 · 저장 · 추적은 들어갔다(`AI/Schedule/README.md`).
  남은 것 — (1) gimmick 병합 뒤: 스마트 오브젝트 시스템이 `IScheduleActivityLocator` 를 구현(`reserve` 는 NPC · 종류 · 날 · 시간 창에 멱등, 지금 점유만 되면
  어댑터가 예약표를 들고 칸 시작에 점유), 끼어들기(말 걸기)를 상호작용 프레임워크에서 `pushInterruption( "Talk" )` 로. (2) char-anim 병합 뒤:
  `IScheduleActivityAnimator` 구현 — `_animation` 이름을 애니메이터 그래프 상태로. (3) 쓰는 게임이 없다 — HarvestValley 마을 사람(데이터
  `game/harvestvalley/data/villagers.schedules.xml` 은 있다, 몸은 `NavAgent` 가 `ScheduleNpcView` 를 따라감, 저장은 `ScheduleSaveState` 를 게임 세이브에).
  (4) 에디터 패널(`dumpTimeline` · `explainNpc` 글을 그대로). (5) 주 단위 · 날짜 범위 일정, 자정을 넘는 칸, 관계 단계(호감도 수치 조건 — 지금은 태그로) 는 없다.

### D6. 런타임 UI

- `D6-1` **런타임 UI 애니메이션 · 전환의 남은 것(runtime-ui 7-1 · 7-2 뒤, `Engine/UI/README.md`).** (1) 스타일 전환은 부모의 보간 값을 물려받는 글 칸에 내리지 않는다
  (자식 규칙에 `_transition` 을 따로 적는다) — CSS 는 물려받은 값도 보간된다. (2) 한쪽만 정한 칸(위젯 자기 칸 ↔ 시트 값)은 전환 없이 바뀐다 — 위젯 칸 값을 읽는 길이 필요.
  (3) 전환 지연(`transition-delay`) · 키 사이 사건 말고 곡선 위 사건.
  (4) 그림 캐시는 조상의 `kTransform` 이면 자손까지 다시 칠한다 — 변환을 캐시 밖에서 곱하는 쪽은 10-4 의 위젯 1 만 개 측정으로 판단.

- `D6-2` **런타임 UI 입력의 남은 것(위젯 트리 코어 뒤, `Engine/UI/README.md`).** (1) 명령 조종자(디렉터 넷) · 플레이어 뷰 카메라는 아직 UI 가 먹은 입력
  (`UISystem::isActionConsumed` · `isGameInputBlocked`)을 보지 않는다 — 플레이어 조종자만 본다. 그 게임이 메뉴를 띄우게 되면 같은 질의를 그 자리에 둔다.
  (2) 에디터 게임 뷰(Game 패널) 안의 포인터 좌표(창 픽셀 → 게임 뷰 렌더 타깃) — UI 가 게임 뷰에 그려지는 단계와 함께. (3) UI 행동 맵(`engine/input/ui.input.xml`)의
  키 리바인딩 — 키 바인딩 창(`KeyRebindScreen`)은 있다, UI 맵을 설정 대상으로 두는 길(`UserSettingsTargets` 의 입력 맵이 게임 맵 하나)이 남았다. (4) 글 입력 칸(`TextInputWidget`)은 끝에 붙이기 · Backspace(`UI.TextBackspace`) · Enter 확정만 — 커서 이동(좌우 · Home/End) · 선택 · 붙여넣기 · 조합 글 밑줄이 남았다.

- `D6-3` **런타임 UI 접근성의 남은 것(runtime-ui 9-1 뒤, `Engine/UI/README.md`).** (1) 음성 재생 쪽 자막 — 음성 이벤트(`GameSound`)에 자막 키를 실어
  `UISubtitleService::post` 로(오디오 키트의 모양을 보고 정한다 — 지금은 대화 러너만 보낸다). (2) "글자 배율 2 에서 옵션 메뉴가 넘치지 않는다" 테스트는 옵션 메뉴(8-2)가 없어
  견본 문서로 한다(`UIAccessibilityTest.OptionsMenuFitsAtDoubleTextScale`) — 8-2 가 들어오면 엔진 옵션 메뉴 문서로 바꾼다.

- `D6-4` **옵션 · 일시정지 메뉴의 남은 것(runtime-ui 8-2 뒤, `Engine/UI/README.md`).** (1) 일시정지 메뉴에 타이틀로 · 끝내기 — 게임 흐름(`GameInstanceBase`)의
  명령이라 엔진 화면이 모른다(게임이 `PauseMenuScreen` 을 덮어쓰거나 명령 표를 거는 길). (2) 명령 조종자 게임(NileCity · StarSkirmish · ThemePark)과
  MeadowVillage 는 `_bUIPauseMenu` 를 켜지 않았다 — Esc 를 게임이 따로 쓰는지 보고 켠다. (3) 열거형 행은 콤보(펼침)뿐 — 패드에 맞는 좌우 고르기 위젯이 없다.
  (4) 키 바인딩 창의 Esc 길게 누르기는 키보드 Esc 만 — 패드 사용자는 취소가 없다(패드 B 를 바인딩할 수 있어야 해서). (5) 해상도 바꾸고 15 초 되돌림 ·
  키 바인딩 재시작 뒤 유지의 실기동 확인(Shooter3D · 패드)은 게임별 빌드 때. (6) 자동 크기 옵션 창은 1280×720 골든(`options.layout.txt`)에서 화면 안에 든다 —
  150 % 조건(853×480 UI 단위)을 보는 테스트는 없다(`UIAccessibilityTest` 꼴로 더한다).

- `D6-5` **알림 · 힌트 · 목표 마커의 남은 것(runtime-ui 8-3 뒤, `Engine/UI/README.md`).** (1) 들어오기 · 나가기 애니메이션 — 항목은 화면이 아니라 조각이라 문서 Open · Close 가 닿지 않는다,
  `showEntry` · 제거 때 `UISystem::tween` 으로 `_opacity` 를 걸고 지우기를 페이드 뒤로 미룬다. (2) 목표 마커를 퀘스트 키트의 목표 오브젝트에 붙이는 한두 줄 — 키트 쪽 목표 오브젝트 모양을 보고. (3) 코드가 올리는 알림 글
  ("Game saved" · "다시 시작하면 적용")은 글 그대로라 글 수집에 들지 않는다 — 코드 글 키(`SW_LOCTEXT` 꼴)로 바꿀 것. (4) 입력 힌트 위젯을 따로 두지 않았다 —
  리치 텍스트 태그 하나로 충분한지 게임 HUD(8-1)에서 본다. (5) 항목마다 이름 `Message` · `Count` 가 트리 안에 겹쳐
  둘째 항목부터 "name is used twice" 경고가 난다 — 항목 조각을 `UserWidget` 으로 감싸 이름을 `<번호>.Message` 로.

- `D6-6` **런타임 UI 그리기 성능의 남은 것(runtime-ui 10-4 뒤, `Engine/UI/README.md` "성능").** 목표(`GT.UI` p50 0.3 ms) 안이다(측정값과 걷기 분해는 [검증과 측정](08_Verification.md) 2절 "런타임 UI 벤치").
  남은 비용은 보이는 위젯을 걷는 비용이고, 더 줄이려면 패널마다 하위 출력 캐시(Slate Invalidation Panel)다 — 일괄 합치기 결정이 이어 붙이는 순서에 달려 있어, 하위 목록은 합치지 않은 일괄 그대로 들어야
  바이트가 같다. 창 크기 바꿈(전체 재배치) 한 프레임 · 첫 프레임 글리프 래스터화가 몇 프레임에 퍼지는지는 표 밖이다(워밍업 60 프레임이 버린다).

- `D6-7` **런타임 UI 오른쪽에서 왼쪽(RTL)의 남은 것(runtime-ui 9-3 뒤, `Engine/UI/README.md`).** (3) 가로 스크롤 패널은 RTL 에서도 왼쪽부터 보인다(Slate · CSS 는 오른쪽) —
  내용 자리를 거울로 놓으려면 `scrollIntoView` 의 부호도 바꿔야 한다.

### D7. 네트워크 · 서버 · 온라인 서비스

- `D7-1` **네트워크 — 파괴 · 가상 서버에서 남은 것**(2026-10-05, `GF_NetSimulation` · `GF_NetDestruction`). ① 파괴 사건은 "신뢰 · 순서 없음" 채널(N21b) — 250 ms · 손실 15 % 의 사건 지연
  평균 · 최대 전/후는 `NetSimDestructionMatrixTest` 로그(`max event lag … mean …`)로 잴 것(빌드 뒤). 남은 최대는 한 메시지가 거듭 잃는 몫이다. 덩어리 멈춤 확정(신뢰 자세)도
  받는 쪽이 틱으로 끼우므로 옮길 수 있다 — 재고 나서. ② 전용 서버 프로세스(`Server`, 타깃 Game · Server)는 섰다 — 남은 것은 그 위에 파괴 시뮬레이션을 UDP 서버로
  돌리고 Windows 클라이언트(App)가 붙어 파괴 해시가 컴파일러 · 플랫폼(WSL 리눅스 서버 ↔ Windows 클라이언트)을 넘어 같은지 보는 것(서버 쪽 `NetSimulation` 호스트 모드 ·
  클라이언트 접속 인자 · 해시 로그 비교 스크립트). 같은 입력의 해시가 구성을 넘어 같은 것은 `DestructionDamageTest.EventLogHashMatchesTheRecordedValueOnEveryBuild`(해시 다섯)가
  지킨다(Windows Debug · Release · Shipping 같은 값 확인, 리눅스는 CI 잡 — 리눅스 clang 에서 다르면 그 잡이 진다).

- `D7-2` **전용 서버 타깃에서 남은 것**(2026-10-06, server-target). ① 서버의 크래시 묶기 · 텔레메트리 — 기동 표의 `Telemetry` 는 Client 대상(동의가 플레이어 설정)이라
  서버는 크래시 보고를 묶지 않는다; 서버용 동의 = 운영 설정으로. ② 서버 프로세스 지표 — 틱 시간 · 버린 틱은 `/metrics`(`_opsPort`)로 나간다; 메모리 ·
  접속 수 지표와, 게임 · 키트 모듈이 서버의 지표 · 상태 등록부를 받는 창구(엔진 서비스 줄 — 첫 서버 서비스 조립 때)는 남음. ③ 게임별 서버 기동 확인 — Empty 말고 각 게임을 `Server -gv_serverExitAfterTicks=60` 으로(게임 · 키트가 디바이스 · 창을 null 확인 없이 쓰는지).
  ④ 서버 빌드에서 렌더러 코드를 아예 빼기(지금은 컴파일만 되고 돌지 않는다 — 크기 · 링크 시간을 재고). ⑤ 리눅스 서버(`WSL-*-Server`) 빌드 · `readelf -d` 로
  libX11 없음 확인 · `ServerTest` — 이 묶음은 Windows 만 확인했다.

- `D7-3` **서버 디버깅 — 계획은 [docs/plans/ServerDebug.md](plans/ServerDebug.md).** 다음 단위는 S4(회선 조절)이고, 운영 단위 O1 ~ O7 도 같은 문서에 있다. 방향 결정은 [결정 기록](09_Decisions.md) 5-10. 단위를 끝내면 계획 문서에서 지운다.
  S1 에서 남은 것: ① 사용자 프리셋 저장("서버 + 클라이언트 2" 를 이름 붙여 에디터 상태에 — 지금은 마지막 구성만 세션이 들고 있고 에디터를 다시 띄우면 Standalone) ② 리슨 호스트(App)의 줄에 틱 번호(전용 서버 줄에는 `[tick=N]` 이 붙는다 — 리슨 호스트는 고정 틱 루프가 없어 프레임 번호로 정해야 한다) ③ Stop 뒤 합친 트레이스를 Profiler 창 Timeline 탭에 저절로 열기(지금은 경로를 로그 · Session 탭에 남기고 Load Trace 로 연다) ④ 게임 모듈 핫 리로드를 넘어 세션 유지 — 게임 인스턴스가 다시 서면 `GameNetSession` 도 닫고 다시 열어 클라이언트가 끊긴다(리로드가 새 인스턴스를 먼저 만들면 포트가 아직 잡혀 있어 다시 열지 못한다; 세션을 엔진 쪽 수명으로 옮긴다 — 언리얼 넷 드라이버는 월드 소유) ⑤ 운영 서버가 `ServerConfig::_gamePort` 를 저절로 여는 길(지금은 `-gv_netListenPort` 를 줄 때만 연다 — 결정 기록 5-10) ⑥ 클라이언트 창 배치(여러 App 창이 겹쳐 뜬다 — 언리얼은 창 위치를 나란히 둔다).

- `D7-4` **네트워크 — 포화된 연결의 비신뢰 줄**(N21a 뒤). 대역폭 몫을 다 쓰면 메시지가 기다리는데, 비신뢰 줄(`NetConnection::_listOutgoingUnreliable`)에는 상한이 없어 오래 포화되면
  옛 비신뢰가 쌓였다 몰려 간다. 언리얼은 포화면(`IsNetReady` 거짓) 액터 복제를 건너뛴다 — 키트가 `NetHost` 에 "이 연결이 포화인가" 를 묻거나 줄에 나이 상한을 둔다. 재고 나서.

- `D7-5` **네트워크 서비스 계층(로그인 · 채팅 · 거래 키트).** 구성과 원칙(공통 기반 · 드라이버 키트 · 기능 키트, DB 와 캐시)은 [결정 기록](09_Decisions.md) 5-9. 남은 것 —
  ① 스트림 전송(Core `Network/Transport/` — IOCP · epoll · 루프백)은 있다. 리눅스 epoll(`EpollStreamTransport`)은 WSL 빌드 · `StreamTransportTest` 미확인 —
  `WSL-Debug` · `WSL-Shipping` 에서 `--test_repeat=20` 과 변이(`setReceivePaused( false )` 의 할 일 빼기 → 백프레셔 시험이 진다)를 본다. io_uring 은 측정 뒤.
  **들어간 기반**(`GameFramework/Base/Online/`): `Store`(영속 계약 `IServiceStore` — 비동기 일 · 트랜잭션 · 조건부 쓰기 · 멱등 기록, 메모리 구현 · 계약 시험) ·
  `Guard`(토큰 버킷 · 크기 상한) · `Identity`(`AccountID` · `IAccountDirectory`) · `Cache`(휘발성 계약 `IEphemeralStore` — 만료 · 원자 증감 · 임대 · 정렬 집합 · 발행/구독, 메모리 구현 · 계약 시험) ·
  `Audit`(감사 줄) · `Bus`(서버 간 버스 — 캐시 위 · 프로세스 안) · `Schedule`(예약 작업 — 회차 차지 · 임대 이어받기) · `Config`(원격 설정 · 기능 플래그 출시 비율) ·
  `Ledger`(원장 — 복식 이동 · 분개 키 멱등 · 보존 검사 · 환불 회수 빚) · `Mail`(우편 넣기 — 첨부 맡김 · 멱등 · 만료 색인) ·
  `Sanction`(계정 제재 레코드) · `IAccountSessionControl`(세션 끊기 창구).
  관측은 들어갔다 — 지표 · 상태 확인 · 운영 HTTP(`Engine/Observability`, 서비스 묶음 `Online/Observability/ServiceMetrics`), 로그 문맥(Core `LogContext` —
  요청 머리의 추적 id 128 비트 → 처리기 · 저장소 일의 줄 꼬리표). 관측 남은 것: 서버 로그 JSON lines 출력 장치, OpenTelemetry 내보내기, 감사 줄의 추적 id 칸.
  남은 기반: `EphemeralServerBus` 를 호스트의 캐시 라우터(`EphemeralStoreRouter`) 위로(지금은 자기 캐시 앞을 혼자 쓴다), PostgreSQL · RESP 계약 시험을 실제 서버로 한 번(`SW_TEST_POSTGRES_URL` · `SW_TEST_RESP_URL` — Valkey(WSL) · Garnet(Windows) 각각 — 이 PC 에 서버가 없어 아직 돌리지 않았다, Windows · WSL), 마이그레이션 SQL(`Resource/common/sql/servicestore`)을 Shipping 서버가 읽는 길(지금은 디스크 폴더를 훑는다 — 팩에는 폴더 목록 API 가 없다) —
  계약 시험(`ServiceStoreContract.h`)을 SQL 구현에도 같이 돌린다.
  **계정**: 서버 키트 `GF_Server_Account`(`Kits/Feature/Online/Account/Server`)에 로그인 서비스 본체(`LoginService` — 저장소 일로 맡기고 거둠 · `LoginStoreLogic` · `LoginTicketAuthority`),
  공유 `GF_Account`(와이어 타입 · `AccountClient`), 게스트 · 연동 · 외부 로그인 자리 · 빌드 판 · 제재 확인 · 탈퇴, 스트림 바인딩(`AccountServer` · `AccountClient`) · UDP 접속
  인증기가 들어갔다. 암호는 `NetSecurityLoginCrypto`(제공자의 Argon2id · HKDF).
  외부 로그인은 공통부(기반 `Online/HTTP` · OIDC 확인기 + JWKS 캐시 · 프로필 API 틀 · 제공자 설정 데이터 · PC 루프백 PKCE 클라이언트)와 가짜 제공자까지 — 실제 제공자 설정
  (구글 · 애플 · 카카오 · 네이버 · 스팀 — 앱 등록 · client id 는 쓰는 게임이 생기면), 호스트 이름 해석(Core 주소는 IPv4 뿐 — 실제 제공자 호스트에 필요), 애플 client secret JWT ·
  탈퇴 때 토큰 철회(`Provider/Apple/`), 모바일 SDK 클라이언트, OS 브라우저 열기(`IExternalBrowser` 구현 — 게임 몫)가 남았다. 텔레메트리의 `IHTTPClient`(Engine, 막는 창구 · 기본 Null)를
  이 HTTP 클라이언트로 잇는 일도 남았다(Engine 은 GameFramework 를 모른다 — 게임이 어댑터). 서버 실행 파일에 계정 서비스 조립(주 키는 서버 설정 비밀 — 키 배포 · 교체 절차는 아래 "서버 여럿").
  **채팅**: `GF_Chat`(타입 · `ChatProtocol` · `ChatClient`) · `GF_Server_Chat`(거르개 · 도배 막이 · `ChatService` · 바인딩 `ChatServer`)이 들어갔다. 남은 것: 서버 실행 파일에
  채팅 조립(계정 키트 창구 · 친구 키트 차단 표 · 금칙어 경로), 신고(메시지 스냅숏 → GM 도구), 채널 샤딩(world 채널 수천 명 — 부하 봇 수치로 정함, 거르개 비용은 `ChatService.send` p99),
  클라이언트 UI 위젯, 실제 금칙어 목록(운영).
  **서버 디렉터리**: 기반 `Online/Directory`(등록 · 하트비트 · 읽기 캐시 · 고르기)와 `GF_ServerDirectory` · `GF_Server_ServerDirectory`(점검 · 공지 · 배정 · 바인딩 ·
  클라이언트)가 들어갔다. 남은 것: GM 도구 패널(GF_Admin · 에디터 확장 지점 뒤)에 점검 · 공지 바꾸기 잇기, 서버 고르기 UI 위젯, 서버 실행 파일에 디렉터리 서비스 ·
  등록 조립(서버 설정에 종류 · 지역 · 공개 주소), 오케스트레이터(Agones · 쿠버네티스) 상태와 서버 등록 잇기(지금은 서버가 스스로 캐시에 하트비트).
  **친구 · 길드**: `GF_Social` · `GF_Server_Social`(친구 · 차단 · 접속 상태 · 이름으로 신청 · 길드 · 바인딩 · 클라이언트)이 들어갔다. 남은 것: 채팅 정책(차단)과 길드 채널을
  서버 조립에서 잇기(`SocialService::isBlockedLocal` · `GuildService::setEventDelegate` → 채팅 키트), 길드 창고(원장 맡김 `esc/guild/<id>`) · 길드 레벨 · 공개 길드 가입 신청,
  친구 목록 UI 위젯, 부하 봇의 친구 접속 상태 팬아웃 측정(친구 200 명 × 캐시 읽기).
  **순위표**: `GF_Leaderboard` · `GF_Server_Leaderboard`(순위표 · 통계 · 업적 · 시즌 정산 · 바인딩 · 클라이언트)가 들어갔다. 남은 것: 정산을 페이지 단위 일 여럿으로
  (지금은 시즌 하나가 저장소 워커 하나를 잡는다 — 참가 수십만이면 부하 봇 숫자로), 지난 기간의 캐시 정렬 집합 지우기, 친구 순위표(친구 목록 × 점수 — 게임 조립),
  업적 진행률 표시 · 숨김 업적 목록, 서버 조립에서 `setScheduler` · 표 정의 데이터 읽기.
  **매칭**: `GF_Matchmaking` · `GF_Server_Matchmaking`(매처 · 파티 · 로비 · 대기열 권한 서버 · 전용 서버 배정 · `MatchServerAgent` · 바인딩 · 클라이언트)이 들어갔다.
  남은 것: 서버 실행 파일에 매칭 조립(모드 표 · 실력 공급 `IMatchRatingSource` · 게임 서버의 `MatchServerAgent` 를 UDP 접속 인증기에 잇기), 백필(경기 중 빈자리),
  오케스트레이터(Agones · 쿠버네티스) 할당 · 서버 띄우기, 재접속 유예 중 파티 · 로비 유지(지금은 끊기면 바로 뺀다), 파티 채팅 채널 잇기(게임 조립 — 파티 알림 → Chat),
  다른 서버에서 깨진 파티 표는 낸 서버에 Cancelled 를 버스로 알리지만 권한 매처에서는 모든 모드의 빼기로 뺀다(모드를 모른다). 팀 나누기 · 채우기는 그리디다 — 나눌 수 없는 조합은 그 닻을 건너뛴다;
  작은 n 전수 탐색은 부하 봇의 대기 시간 p99 를 보고 정한다.
  **라이브 운영**: `GF_LiveOps` · `GF_Server_LiveOps`(기간 이벤트 · 원격 설정 값 · 경계 알림 · 푸시 제공자 계약 · 기기 등록 · 발송기 · 가짜 제공자 · 바인딩 · 클라이언트)가
  들어갔다. 남은 것: 실제 푸시 제공자 둘(`Push/Provider/<제품>/` — 공통 HTTPS 클라이언트 위 HTTP/2 · 토큰 인증, 인증서 · 서비스 계정 키는 운영 비밀 — 외부 계정 · 비용이라
  사용자 결정 전에는 하지 않는다), A/B 실험(원격 설정 출시 비율 + 지표), 이벤트 운영 화면(GF_Admin 패널), 서버 실행 파일에 라이브 운영 조립, 우편 · 이벤트에서 "오프라인이면 푸시" 잇기(게임 조립).
  **부하 시험 봇**: `Tools/OnlineLoadBot`(키트 클라이언트 그대로 · 공유 끝점 연결 수천 · 시나리오 JSON · 지연 백분위 · `--local-server` 한 프로세스 서버 조립)이 들어갔다.
  남은 것: 첫 측정 — Release · 서버 한 대 · 봇 1000/5000 으로 `chat_and_match` · `login_storm` 을 돌려 동작별 p99 와 서버 틱 p99 를 적을 것(채팅 world 채널 수천 명의
  알림 수 · 정산 일의 길이 · 매처 대기 p99 가 위 키트 항목의 판단 근거, Debug 참고값은 [검증과 측정](08_Verification.md) 2절 "온라인 부하 봇"). 전용 서버(`Server`)가 온라인 서비스를 조립하면 `--server` 로
  같은 시나리오를 돌린다(봇 머신 따로).
  **거래**: 공유 `GF_Trade` · 서버 `GF_Server_Trade`(`TradeService` — 상태 기계 · 거래 레코드 · 원장 분개 하나로 정산 · 재시작 복구 · 시한)와 스트림 바인딩이 들어갔다.
  **서버 여럿**: 접속 상태(`OnlinePresence` — 캐시 `presence:` 키, 시한 60 초 · 30 초마다 다시 적기)와 버스(`account.revoke` 로 다른 서버의 옛 세션 닫기 · `push.<서버>` 로
  다른 서버의 계정에게 알림)가 들어갔다(`OnlineMultiServerTest`). 계정 · 거래 몫의 남은 것: 게임 UDP 접속 표 서명 키(로그인 · 게임 서버 공유, 지금 서버 설정 파일)의
  배포 · 교체 절차, 인스턴스 아이템(지금 거래 다리는 가산 자산만 — 인벤토리 인스턴스 상태 칸은 거절), 실제 지연(루프백이 아닌 망)에서의 버스 · 캐시 왕복 측정.
  **경제 · 우편함 · GM**: `GF_Economy`/`GF_Server_Economy`(화폐 · 상품 카탈로그 · 구매 · 영수증 계약 + 가짜 제공자 · 서비스 · 클라이언트 · Wallet/Inventory 거울 — 로컬 지갑은
  원장 사본), `GF_Mailbox`/`GF_Server_Mailbox`(수령 = 원장 이동 · 모두 받기 · 만료 쓸기 · 전체 우편 캠페인 — 기반 `Online/Mail/ServiceMailCampaign`), `GF_Admin`/`GF_Server_Admin`
  (권한 등급 넷 · 모든 조작이 효과와 같은 트랜잭션의 감사 줄 · 환불 회수 빚)이 들어갔다. 남은 것 — ① 영수증 제공자 셋(`Receipt/Provider/<제품>/`): Apple(App Store Server API —
  서명된 거래 JWS 의 x5c 사슬을 Apple 루트로 + 거래 조회), Google(Play Developer API `purchases.products` — 서비스 계정 JWT → OAuth 토큰), Steam(`ISteamMicroTxn`
  InitTxn → FinalizeTxn — 서버가 주문을 여는 흐름) — 외부 스토어 계정 · 비용이 들어 계약 + 가짜까지만(사용자 결정), 호스트 이름 해석(Core 주소는 IPv4 뿐)이 먼저다
  ② 환불 · 결제 취소 알림 받기(Apple 서버 알림 v2 · Google RTDN · Steam 환불 → GM 회수와 같은 `refund.revoke` 빚 이동) · 유상 재화 청약철회(7 일 · 미사용분) 규칙
  ③ 인스턴스 아이템(내구도 · 강화 — 원장은 개수만, `item.<정의>#<id>` 개수 1 + 상태 레코드) ④ 잔액 · 새 우편 알림(서버 → 클라이언트 `sendPush` — 지금은 응답에 실린 잔액 · 다음 목록)
  ⑤ 원장 보존 검사 · 만료 쓸기를 예약 작업(`ServiceScheduler`)으로(지금 쓸기는 우편함 서비스 틱 주기), 분개 · 감사 보존 기간 정리, `mail_sent` · 멱등 기록 · `econ_purchase` 정리
  ⑥ GM: 에디터 패널(1-4 C 확장 지점 뒤), 접속하지 않은 계정 이름 찾기(계정 키트 저장 부분을 기반 계약으로), 첫 관리자 · GM 전용 호스트 포트를 서버 설정에(`AdminService::seedRole`)
  ⑦ 서버 실행 파일에 경제 · 우편함 · GM 서비스 조립(카탈로그 경로 · 영수증 등록부 · 지표 등록부를 서버 설정으로) ⑧ 발행 · 소각 총량 지표(보존 검사가 `economy_issued_total{asset}`).
  상용 비교: 언리얼은 Online Subsystem/EOS 등 외부 백엔드에 맡기고, 자체 MMO 서버는 IOCP/epoll 서비스 서버를 따로 둔다.

- `D7-6` **네트워크 보안의 남은 것**(방향은 [결정 기록](09_Decisions.md) 5-9). Core 창구(`Network/Security/`) · OpenSSL 3.6 구현(`GameFramework/Base/Online/Security/OpenSSL`, `NetSecurity`) · 스트림 TLS 1.3(`StreamEndpointSettings::_security`) ·
  UDP 보안(`NetHostSettings::_security` — X25519 + 패킷 AEAD + 재전송 창 + 토큰 결속, 인증기 없는 암호화는 개발 빌드만)은 있다. 남은 것: 암호화 켠 하니스로 서버 틱
  시간을 재어 [결정 기록](09_Decisions.md) 3절에 숫자 한 줄(N18a 벤치에 `_security` 를 켠 판), 서버 호스트가 `ServerConfig::_tlsCertificateFile` · `_tlsPrivateKeyFile` · `_tlsPrivateKeySecretEnvironment`(→ `ServerSecret::read`)를 `NetSecurity::createServerTLSContext` 에 넘기는 배선.

### D8. 핫 리로드 · 모듈

- `D8-1` **바깥 빌드(터미널 · IDE)의 리로드 트리거는 여전히 mtime 디바운스뿐이다** — 에디터가 시킨 빌드는 성공 뒤에만 올린다(`LiveReloadManager::notifyBuildStarted/Finished`).
  바깥 빌드도 "빌드 성공" 신호(ninja 종료 · 스탬프 파일)를 받으려면 빌드 쪽 협조가 필요하다.

---

## E. 관찰 중

간헐 실패 · 원인 미상. 다시 보이면 그때 원인을 판다. CI 읽는 법은 [검증과 측정](08_Verification.md) 4절.

- `E-1` **다음 CI 실행으로 확인할 것**(리눅스는 컴파일까지 되고 테스트 단계에서 진다, CI 읽는 법은 [검증과 측정](08_Verification.md) 4절).
  고쳤고 통과를 확인할 것: `OnlineLoadBotTest.TwoHundredBots…`(캐시 TTL 을 서버 틱 시각으로), `EpollStreamTransport` 경합 보고, `StreamTransportTest.PlatformAbortWithNothingPendingStillCloses`(전송 모델별 기대),
  ASan 둘(OpenSSL 스택 수명, `NetRequestTest` 선언 순서), `ServerBootTest` 접속 로그, Linux Debug `EngineTest_NoGPU` 샤드의 180 초 초과(멈춤이 아니라 느림 — 샤드를 여섯으로 늘렸다).
  원인을 아직 모르는 것: `UIOptionsMenuTest.RebindIgnoresUIActionsWhileListening`(리눅스 Debug · Shipping · ASan, 포커스 위젯 ID 839 ≠ 823 `Tab.controls` — 창이 닫힌 뒤 탐색 Up 이 한 번 발화한 것으로 추정, 다음 실패 메시지가 포커스 위젯 이름을 보인다),
  ASan 중단 두 건(`PlatformLoginTest.Oidc…` · `NetRequestTest.IdempotentRetry…`), TSan 샤드의 새니타이저 보고.
  Windows STL containers 잡에서 건너뛰게 한 테스트 셋(`MemoryTagTest` 둘, `DestructionDamageTest.EventLogHash…`)이 기본 컨테이너 구성에서도 가끔 지는지 지켜본다.

- `E-2` **Windows CI Debug · STL 잡의 키트 테스트 22 건 실패**(run 37251047781, `a380e2ee3` — 유니티 켬 + PCH 끔 Debug 에서만): 재현 안 됨 — 이 PC 의 같은 구성
  (`CI-Debug -DSW_ENABLE_PCH=OFF`)에서 통과했다. B4 로 실패 보고(`CiFailureReport.py`)가 나아진 뒤 다음 CI 주석으로 본다.
  그 뒤 액션 키트 테스트 넷이 네 실행 연속으로 지는 간헐 실패가 되었다 — 일회성으로 보지 않는다.

- `E-3` **Vulkan(FIFO) + VSync 에서 가끔 한 프레임이 여러 주기를 놓친다**(Release 큐브 100, 165 Hz — 조사 결과는 [검증과 측정](08_Verification.md) 2절 "Vulkan VSync 프레임 놓침",
  스왑체인 이미지 4 개는 결정 기록 3절에서 기각). 다시 보이면 `-gv_tracy=1` 로 acquire · 펜스 · present 를 시간축으로 보고, 전체 화면(독점) · NVIDIA "Vulkan/OpenGL 프레젠트 방식" 설정을 바꿔 가른다.

- `E-4` **CI Windows Debug `EngineTest_NoGPU_Shard3` 가 모두 통과한 뒤 0 이 아닌 종료 코드로 졌다**(run 37457443235 · `28d5627e3`, 한 번 — 직전 실행은 통과).
  로그는 `TaskManager Shutdown cleanly` 에서 끝나고 크래시 스택 주석이 없었다. 이 PC Debug 로 같은 조각(`--test_shard=2/3 --host_suites=exclude`) 15 회 순차 ·
  12 회 4 개 동시 — 재현 안 됨. 하네스가 이제 마지막 줄 `[TestHost] shut down - exit code N` 을 찍는다: 다시 나면 그 줄이 없으면 `TestHostRuntime::stop`
  안(부트스트랩 · 로거 · 메모리 프로파일러 종료), 있으면 정적 소멸자다.
  **같은 증상이 리눅스 Shipping 에서 나왔다**(run 38163565253, `734f7fa08`, 조각 여섯으로 늘린 뒤 `EngineTest_NoGPU_Shard2`): 테스트는 10 초에 끝나 요약까지 찍혔는데
  마지막 줄이 없이 180 초 한도로 졌다 — `stop()` 안에서 멈춘 것이다. 어느 단계인지 가르려고 `stop()` 이 단계 앞마다 `[TestHost] stopping: …` 을 찍는다. 다시 나면 그 줄로 좁힌다.

- `E-5` **CI Windows STL 잡이 파일 잠금으로 두 번 연속 졌다**(`334cae46e` · `734f7fa08`): 첫 번은 `OnlineLoadBot.exe` 링크("being used by another process"), 둘째는 모듈 링크 뒤 `vcpkg z-applocal`
  실행("resource busy or locked"). 둘 다 `BuildTestBinaries`(Test 단계)의 ninja 에서 났다. `StageModuleRuntimeDlls.cmake` 가 z-applocal 을 다섯 번까지 다시 띄운다.
  `OnlineLoadBot.exe` 쪽은 막지 못했다. 근본 의문: `Build` 단계가 끝난 트리에서 `BuildTestBinaries` 가 또 294 단계를 링크한다(ninja 가 `Re-checking globbed directories` 뒤
  `Re-running CMake` 를 도는 것을 로컬 드라이런에서 봤다) — 빌드 직후 두 번째 빌드가 빈손이 아닌 이유를 찾으면 Test 단계의 재링크와 잠금 경합이 함께 사라진다.

- `E-6` **Shipping `EngineTest_NoGPU` · HostOnly 간헐 세그폴트**(09-20 · 21 · 22 에 한 번씩, 2026-10-06 nogpu 3 회 · host 3 회 재실행 깨끗). 다시 나면: 크래시 핸들러의
  스택 파일(`Bin/Saved/Logs/crash_<세션>.stack.txt` — Shipping 도 이제 PDB 가 있어 함수 이름), CI 는 그 파일을 아티팩트로 · 스택을 주석으로 올린다(`CiFailureReport.py`).

- `E-7` **WSL lavapipe 가 가끔 서피스를 잃는다**(`AppTest_HostOnly` 43 회 중 3 회, 첫 `vkAcquireNextImageKHR` 가 `VK_ERROR_SURFACE_LOST_KHR`) — 이제 서피스 · 스왑체인을
  다시 만들고(사양대로) `Vulkan surface lost at acquire|present (N time(s) …)` 경고를 남긴다. WSL 에서 50 회 돌려 경고 수 · 실패 수를 본다(복구가 되면 항목을 지운다):
  `cd build/WSL-Debug/Bin && for i in $(seq 50); do ./AppTest --host_suites=only --test_filter=AppSmokeTest.* || echo FAIL $i; done` 와 `Saved/Logs` 의 경고 줄 수.

- `E-8` **CI Windows 러너(WARP)의 픽셀 시험 실패**(`RenderPassGPUTest` 를 host 스위트로 빼서 우회) — 이 PC 에서 `SW_RHI_SOFTWARE_ADAPTER=1`(`-gv_rhiSoftwareAdapter=1`)로
  같은 래스터라이저를 고를 수 있다. 2026-10-06 이 PC 의 WARP(DX12 · DX11)로 `RenderPassGPUTest.*` 71 개 픽셀 시험이 모두 통과했다(Vulkan 은 CPU 디바이스가
  없어 빠지고 GL 은 하드웨어) — WARP 자체는 컴퓨트 컬링 · 인디렉트를 한다. 러너 쪽(WARP 판 · 창 없는 세션)을 다음 CI 실패의 주석으로 가른다.

---

## F. 결정 대기

사용자 결정이 있어야 움직이는 질문.

- `F-1` **전용 서버의 지형 레이어 질의는 레이어 0 이다** — 스플랫 가중치가 텍스처(.dds)라 서버 패키지에서 빠진다(쿠킹 표 `Texture`). 서버 게임 로직이 지형 레이어
  (발소리 판정 · 표면 마찰 등)를 쓰게 되면 스플랫을 데이터 종류로 쿠킹하거나 그 경로를 서버 패키지에 남긴다.

---

## G. 조건부 · 낮은 우선순위

조건이 오면 하는 일(쓰는 게임이 생기면, 측정해서 이기면 …)과 이득이 작은 후보.

- `G-1` **모듈이 렌더 패스를 등록하는 창구가 없다.** `FramePassContext` · 커맨드 리스트 · 트랜지언트 풀을 모듈 경계 밖으로 내야 하고, 그것은 RT 안전 계약까지
  내보내는 일이다. 쓰는 모듈이 생기면 그때.

- `G-2` **CS2 식 서브틱 입력(2026-10-05 제안 — 넷 게임이 생기면; 지금 넷 키트를 쓰는 시험 게임은 0).** `RawInputEvent` · `NativeWindowEvent` 에 시각이 없다(`MSG::time` 을 버림).
  B1 사건 시각 + 입력 리플레이 판 4 · B2 프레임 안 자리와 그 순간까지의 시선 누적(시계 주입) · B3 `InputMap` 이 행동별 "누른 순간" 을 준다 · B4 클릭 순간의 시선으로 발사
  (쿨다운 남은 몫은 이미 고침) · B5 넷 입력 항목마다 스탬프 · `viewTick`(지금은 메시지마다 하나라 다시 보낸 옛 입력도 최신으로 판정 — 자리는 있다:
  `NetInputFormat::_bStamped` · `NetInputSendWindow::push( …, stamp )` · `NetInputEntry::_stamp`, 켜면 `kClientServer` 판을 올린다) · 서버 클램프(랙 보정
  `LagCompensationHistory::raycastAt(float)` 는 있다) · B6(선택) Win32 입력 스레드 — 먼저 `GetMessageTime` 해상도를 재고, 에디터 · 콘솔이 먼저 소비하는 판정을 우회하지 않게.
  롤백 · 락스텝 키트에는 넣지 않는다(프레임 단위 결정성이 계약).

- `G-3` **월드 공간 위젯의 입력 · 가려짐(runtime-ui 4-6 뒤)** — World 위젯은 그리기만 한다: 레이로 사각형을 맞혀 위젯 좌표로 사건을 보내는 길(언리얼
  `WidgetInteractionComponent`)이 없다. 화면 마커의 "벽에 가려지면 숨김" 은 레이캐스트 질의 · 깊이 버퍼 읽기 둘 다 없다. World 렌더 텍스처는 내용이 바뀔 때만 다시
  그리지만 화면에 안 보일 때도 칠하기는 돈다(보일 때만 칠하기 — 절두체 판정은 조건: 월드 위젯이 수십 개를 넘을 때).

- `G-4` **상호작용 대상의 공유 공간 격자**(조건: 하는 쪽 · 대상이 모두 수천 — 지금 하는 쪽은 등록된 대상 목록을 한 번 돈다, 대상 16 · 10k 오브젝트에서 +2~6 us).
  대상이 움직일 수 있어 격자를 프레임마다 맞추는 비용이 목록을 도는 비용과 같으므로, 하는 쪽이 여럿일 때만 씬당 하나(`SpatialHashGrid2D`, 셀 = 최대 상호작용 반경)를 틱 앞에 맞춘다.

- `G-5` **씬 · 프리팹 파일을 넘는 오브젝트 참조**(조건: 레벨 스트리밍 · 하위 레벨 · 다른 씬의 오브젝트를 가리키는 데이터가 생기면). 그때 오브젝트마다
  영속 GUID 를 싣는다(언리얼 `FSoftObjectPath` 의 하위 오브젝트 경로 자리). 지금은 파일 안에서 엔티티 `id` 로 가리키고, 파일을 넘는 데이터 · 코드가 0 이다.

- `G-6` **타일맵 칸 데이터를 일반 레이어로**(두 번째 장르가 칸마다 다른 값 — 지형 비용 · 발소리 — 을 원하면): 지금 레이어는 0/1(`kArrTileFlagLayerInfo`)이고 워프 · 역할 · 스폰은
  전용 원소다. 값 종류를 정수로 넓히고 에디터 페인트 · 형식 테스트를 같이 바꾼다(Godot TileSet custom data 모양).

- `G-7` **걸음 조우 판정 둘**(Overworld `shouldEncounterOnStep` 의 결정적 주기 · ClassicJRPG `JRPGEncounterWalker` 의 확률 + 유예) — 오버월드 위에 JRPG · 몬스터 수집 게임이 서면 기반 `World/` 로 하나를 올린다.

- `G-8` **MMO 갱신 확인을 `NetConnection` 전달 통지로**: 패킷 확인 → 메시지 전달 통지가 Core 에 생기면 키트 확인 메시지(`kUpdateAck`)를 지우고 그 통지로 판정한다(언리얼 NAK 자리).

- `G-9` **스크린 리더**(위젯 접근성 이름 · 역할 → OS 내레이터 — 언리얼 Slate 접근성 · Xbox 접근성 지침 107): 런타임 UI 의 접근성은 글자 크기 · 자막 · 색각 · 고대비까지다.

- `G-10` **MSDF 글리프**(직접 — 윤곽 모서리 칠하기 · 채널별 거리, 큰 글자의 모서리가 날카롭다) — 지금은 단일 채널 SDF(`Engine/Text/GlyphCache`, FreeType `sdf` 렌더러,
  결정 R2). 아틀라스 페이지가 R8 이라 MSDF 는 RGB 페이지 · 셰이더 median 이 함께 든다.

- `G-11` **서드파티 빈자리(2026-10-05 후보 중 사용자가 고르지 않은 것).** 리눅스 오디오 출력 없음(`XAudio2AudioBackend` 만, 리눅스는 `NullAudioBackend`) → miniaudio(퍼블릭 도메인/MIT-0) ·
  `gv_renderScale` 을 읽는 업스케일 없음 → AMD FidelityFX FSR(MIT) · 아랍어 셰이핑 없음(양방향은 `Engine/Text/TextBidi` 단순판 — 포개진 방향 제어 문자 ·
  숫자 앞뒤 기호 규칙 미지원) → HarfBuzz(MIT) + SheenBidi(Apache 2.0)
  (꽂을 자리는 `Engine/Text/ITextShaper` — 런 하나, 같은 글꼴 바이트 `IFontRasterizer::findFaceBytes` 로 `hb_face` 를 만든다. SheenBidi 는 `TextBidi` 를 대신한다). 들이면 Jolt · Recast · Tracy 처럼
  엔진 인터페이스 뒤 + 격리 게이트, vcpkg 변경은 main 에서 먼저.

- `G-12` **임포터가 1 채널(R8) DDS 를 내는 규칙**(조건: 마스크 · 1 채널 텍스처 에셋이 생기면) — RHI · 로더는 `R8_UNORM`(DXGI 61, DX10 머리)을 읽는다. `App --import-textures` 의
  규칙 표(`TextureImportConfig.json`)에 R8 출력이 없다.

---

## H. 보류 (사용자 결정)

사용자가 "나중에"로 정한 것. 풀 때까지 하지 않는다.

- `H-1` **(보류 — 사용자 결정 "안정화된 뒤 개발") 게임별 CC0 리소스 배치 + SD 메카 테스트 게임 MechArena.** 라이선스는 **CC0 급만**(출처 표기 의무 · 재배포 금지가 붙은 것은
  넣지 않는다 — 내려받기 스크립트 우회도 안 함). 확인한 원본: VRoid 알파판 샘플 D · E · F · G · 남녀 기본(OpenGameArt `vroid-studio-cc0-models`, VRM 0.x — 툰 셰이더 ·
  VRM 임포트는 있음) · KayKit(GitHub 사용자 `KayKit-Game-Assets` 의 `*-1.0` 저장소, LICENSE.txt CC0 — Space Base · Restaurant · City Builder · Dungeon Remastered ·
  Furniture · Halloween) · Quaternius(Poly Pizza 낱개 glb — 도리이 · 일본식 문 · 대나무 · 단풍 · 가을 소나무) · 음악 OpenGameArt CC0 `jaoan`(고토) · `menu-music-2`(도장풍 루프).
  배치안: NileCity ← City Builder · StarSkirmish ← Space Base · Shooter3D ← Dungeon · ThemePark ← Restaurant · Halloween · HarvestValley ← 일본 시골(Quaternius) + jaoan ·
  AbilityArena ← VRoid + menu-music-2. 프리팹 · 데이터 연결까지(안 쓰면 에셋 검증 `orphans`). 건담풍 사람형 메카는 CC0 로 쓸 만한 것이 없다(Quaternius 메카는 동물이 탄
  보행기, OGA Shock Bot 은 리얼풍) → 파이썬으로 SD 메카 부품 메시를 만들어 부품마다 뼈 하나에 붙이고 KayKit 사람형 뼈대 이름에 맞춰 KayKit 애니메이션(CC0)을 쓰는 생성기 +
  MechArena 테스트 게임(`GF_MechArena` 키트는 있고 테스트만 씀). 건담 고유 요소(V 안테나 · 얼굴 마스크 · 흰 · 파랑 · 빨강 · 노랑 배색)는 피한다.
  탈것: 말(`MountMovementComponent` · 서부극 `WesternHorseMountComponent`) · 차(`ArcadeVehicleComponent`)는 기본 도형(캡슐 말 · 상자 차)으로만 테스트했다 —
  CC0 말 메시 · 걸음새 애니메이션(Gait 0..4 · Turn)과 차 모델을 찾으면 서부극 · 카트 테스트 게임에 붙인다.
  카트 키트(`Kits/Genre/Casual/KartRacing`)의 플레이어 경로를 `ArcadeVehicleComponent::toVehicleInput`(의도 → 모터 입력)으로 바꾸는 것은 카트 테스트 게임이 생길 때
  (지금 키트는 InputMap 을 읽지 않고 `KartRace::resolveInput` 이 이미 "같은 모터에 다른 조종자" 다).
  물리 차(`WheeledVehicleComponent`)는 바퀴 메시를 바퀴 자세(조향 · 회전 · 서스펜션)로 옮기지 않는다 — 차 모델이 생기면 바퀴 자식 이름 표와 `getVehicleWheelPose` 창구를 더한다.

- `H-2` **(보류 — 사용자 결정 2026-10-03) `hashed_string` 에 FName 숫자 꼬리를 둘지.** 지금은 비교 · 표시 인덱스 두 칸(8 바이트)이라 `"Enemy_12"` · `"Enemy_13"` 이
  이름 표에 각각 영구 적재된다. 런타임에 번호 붙은 이름을 대량으로 만드는 경로(복제 · 스폰 이름 자동 부여)가 생기면 다시 본다 — 넣으면 `_숫자`(앞자리 0 제외)를
  떼어 정수 칸에 두고 비교는 (인덱스, 숫자) 쌍.

- `H-3` **(보류 — 사용자 결정 2026-10-13) 운영과 배포.** 서버 운영 단위 O1 ~ O7([서버 디버깅 계획](plans/ServerDebug.md) 운영 단위: 사고 기록 · 로그 · 종료 드레인 · 관리 채널 · 부하 기준선 · 컨테이너 · 쿠버네티스 · Agones · 운영 사고 재생)과
  `D7-2` 의 운영 쪽(크래시 묶기 · 텔레메트리 전송)은 "아직 할 예정이 없다"로 정했다. 폴리싱(결함 · 성능 증거 · 검증 자동화 · 다중 월드 · 에디터)이 먼저다. 풀 때까지 하지 않는다.

---

## 옛 번호 대응표

2026-10-13 이전의 글(커밋 메시지 · 주석 · 문서)이 쓰던 영역 번호가 지금 어느 항목들로 갈렸는지.

| 옛 번호 | 옛 영역 | 지금 |
|---|---|---|
| 1 | (머리 항목) | `C-1`, `C-2` |
| 1-1 | 직렬화 · 리플렉션 | `A-1`, `A-2`, `B-11` |
| 1-2 | 오브젝트 · 씬 · 틱 · 물리 | `D1-1`, `D1-2`, `D1-3` |
| 1-3 | 그래픽스 · RHI · 셰이더 | `A-3`, `D2-1`, `D2-2`, `D2-3`, `D2-4`, `D2-5`, `D2-6`, `D2-7`, `D2-8`, `D2-9`, `D2-10` |
| 1-4 | 에디터 | `A-4`, `A-5`, `B-3`, `B-4`, `D3-1`, `D3-2`, `D3-3`, `D3-4`, `D3-5`, `D3-6` |
| 1-5 | 핫 리로드 · 모듈 | `D8-1`, `G-1` |
| 1-6 | 게임프레임워크 · 킷 · 게임 | `C-3`, `C-4`, `C-5`, `D1-4`, `D3-7`, `D4-1`, `D4-2`, `D4-3`, `D4-4`, `D4-5`, `D4-6`, `D4-7`, `D4-8`, `D4-9`, `D4-10`, `D4-11`, `D4-12`, `D4-13`, `D5-1`, `D5-2`, `D5-3`, `D6-1`, `D6-2`, `D6-3`, `D6-4`, `D6-5`, `D6-6`, `D6-7`, `G-2`, `H-1` |
| 1-7 | Core · 태스크 · 네트워크 | `A-6`, `B-9`, `B-10`, `D7-1`, `D7-2`, `D7-3`, `D7-4`, `D7-5`, `D7-6` |
| 1-8 | 성능 | `B-1`, `B-2`, `C-6`, `C-7`, `C-8`, `C-9`, `C-10` |
| 1-9 | 빌드 · 린트 · CI · 테스트 | `B-5`, `C-11`, `C-12`, `C-13`, `C-14`, `C-15`, `C-16`, `C-17`, `E-2` |
| 1-10 | 관찰 중 | `B-6`, `B-7`, `E-1`, `E-3`, `E-4`, `E-5`, `E-6`, `E-7`, `E-8` |
| 1-11 | 결정이 필요한 것 | `F-1`, `H-2` |
| 1-12 | 낮은 우선순위 | `B-8`, `C-18`, `G-3`, `G-4`, `G-5`, `G-6`, `G-7`, `G-8`, `G-9`, `G-10`, `G-11`, `G-12` |
