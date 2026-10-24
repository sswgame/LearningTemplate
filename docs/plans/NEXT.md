# 다음 세션 인계

> 질문: 지금 어디까지 왔고, 다음 세션이 처음에 할 일은 무엇인가?
> 독자: 새로 시작하는 세션(에이전트와 사람).
> 결론: main 은 통합 H(수정 · 정리 브랜치 14 개 + 셰이더 쿠킹 속도 `cookfast` · `cookdeps`)와 그 검증(Debug · Shipping · Shipping-Server `nogpu`, `lint`, `race`, Release 쿠킹)까지 끝났다. 남은 것은 push 뒤 main CI 확인과 GPU 확인(`## 000000` 목록)이고, 그다음이 아키텍처 이음매와 리팩토링 트랙([ArchitectureSeams](ArchitectureSeams.md))이다.

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

## 0000000. 병렬 · 비동기 · 구조 정리 트랙 — 작업자 브랜치 열아홉 개 대기 (이것부터 읽는다)

**1. 커밋이 끝난 브랜치**(빌드 · 테스트 · 실행 안 함, 린트 통과). 보고는 `git log main..wt/<이름>` 과 커밋 메시지가 원본이다. `cookfast` · `cookdeps` 는 통합 H 에서 병합했다. 나머지: `ciflow`(3cd00ea78), `refindexpar`(f1ae5a483), `binisolate`(c74ca44f2, 계획 문서 `HostGpuIsolation.md` 만), `parrules`(c8899c38e, `docs/agents/Parallel.md` + 게이트 `CheckLazyInitStaticFlag` · `CheckGateFileWalk`), `verifypar`(a47d7a685), `lintscan`(a09526f16, 문서만 — 이득이 작아 구현 안 함), `framesysaccess`(a945d250c), `importpar`(5d9ac8210), `packfast`(b8e5d4b9c), `verifyscan`(10d7cb0e9), `profscopes`(5281abdbc), `samefix2`(eb345a306 · fa646797c, 기준 `parrules`), `containers3`(cf74b3310, 기준 `framesysaccess`). 아직 돌고 있어 커밋이 없을 수 있는 것: `editorasync`, `packedtypes`, `rhiasyncplan`(계획 문서), `containers1`, `containers2`, `hotreloadq`(기준 `importpar`), `samefix1`(기준 `parrules`), `taskdeps`, `logdeadlock`, `editorscope`(기준 `importpar`).

**2. 병합 순서 제약**: `framesysaccess` → `containers3`; `importpar` → `hotreloadq` · `editorscope`; `parrules` → `samefix1` · `samefix2`(· `CheckLazyInitStaticFlag` 기준선); `verifypar` 와 `verifyscan` 은 파일이 겹치지 않는다. 충돌 예상: `docs/09_Decisions.md`, `docs/08_Verification.md`, README 진행 표(`status-board --write README.md` 로 다시 만든다), `Test/QA/VerifyBatch.json`(`packfast` 가 항목을 더함), `TestShader.cpp`.

**3. 통합이 볼 것**: 컴파일 위험은 각 브랜치 보고에 있다(`SW_DELEGATE_METHOD` 에 private · 익명 중첩 타입, `TaskManager::initialize(4)` 테스트, `ISceneFrameAccess` 의 constexpr 로컬 배열, `VulkanOneShotCommands.h` 가 `FrameProfiler.h` 를 include 하는 순환). 프로파일 스코프 `GT.Material.waitIdle` · `RT.Upload.waitIdle` · `RT.PSO.create` · `GT.Load.sync` · `GT.Settings.save` 는 서비스 바인딩 전에 닿으면 멈출 수 있다(시동 때 확인). `ctest-python` 으로 `verifypar` 의 새 테스트를 확인하고, `--jobs 2` 에서 `nogpu` · `race` 가 겹쳐 시간 제한이면 `--jobs 1`. 속도 실측(쿠킹 compile · reflect · total ms, 팩 압축 wall/cpu, 임포트 전체, 참조 색인 스캔, 린트 `CheckVerifyPending` 43 → 2 s)을 이번 통합에서 한 번 재서 문서에 남긴다. 통합 H 실측: 쿠킹 전체 다시 굽기 272 s(합 컴파일 3790 s), 최신 확인 1.96 s.

**4. 사용자 결정 대기**: ① `verifypar` 작업자가 실수로 띄운 `python -` 프로세스 둘(PID 13924 · 15132, CPU 점유)을 권한 거부로 종료하지 못했다 — 사용자가 종료. ② `ciflow`: 린트 실패 시 `gh run cancel` 로 린트 잡이 cancelled 로 끝나는 것이 브랜치 보호의 필수 확인과 충돌하는지 GitHub 설정에서 확인. ③ push 를 GPU 확인 앞으로 당기는 정책(`11_Workflow.md` "검증 전 push 금지" 는 사용자 지시)을 바꿀지. ④ [ArchitectureSeams](ArchitectureSeams.md) 4 절의 둘(GPU CI 자동화, 작업자 빌드 금지 규칙 재검토).

**5. 아직 안 한 것(후보, 순서)**: 애니메이션 유닛 사슬(`AnimationSystem` 레벨 5L 번 전체 대기를 의존으로 — `containers1` 병합 뒤, 유닛 단위가 아니라 레벨 청크 단위로 비교 설계; `09_Decisions.md` 103 행과 충돌 주의, 먼저 측정), `TaskManager` 의존 API 의 프로덕션 첫 사용처(`taskdeps` 가 사이클 검출을 채움), 모델 한 개 안 LOD · 클립 병렬(`importpar` 뒤, 이득 작음), Detour 군중(측정 먼저: `GT.Scene.tick.navigation`), G 버퍼 법선 RGBA16F → RG16 옥타헤드럴(`packedtypes` 뒤, `RHIFormat` 추가 · 셰이더 재쿠킹), 메시 정점 64 → 28 B(`.mesh` 버전 4, `PreloadAndLod` 와), 캐시 계약(`IAssetCache` 계측, `ResourcePaths` 음성 결과 · 세대, 측정 캐시 전체 비우기 → LRU), RHI 업로드 비동기(`rhiasyncplan` 문서의 단위), `hostgpu` 격리 H1~H4(`binisolate` 문서), 락 쥔 채 로그 금지 규칙, `ResourcePaths` 락 없는 전역(병렬 단계 중 쓰기 금지), 중첩 `runParallel` 스택 깊이 상한, 순서가 주석에만 있는 목록 3종(`SceneFrameStepList` · `SW_SCENE_SERVICE` · 컴포넌트 틱 그룹), `RunLintSuite` · `ci-jobs` 의 `ConflictSchedule` 적용, `Test/` 의 static 카운터 경쟁 후보, 같은 정규식 모양 4곳(`RunForwardDeclarationCandidates.py` 170 · 233, `CheckServiceAccess.py` 165, `FileScan.py` 121). 컴파일 캐시는 sccache 를 PCH 때문에 기각했으니 다시 제안하지 않는다(`bd5b28125` 계열 기록).

**6. 그다음** [ArchitectureSeams](ArchitectureSeams.md) 0 단계(측정, 통합 혼자)와 1a · 1b · 2a · 3 · 4. 위 5 의 "아키텍처와 같이" 로 분류된 것은 해당 단계에 끼운다.

## 000000. 2026-10-25 통합 H — 브랜치 16 개가 main 에 들어감(push), GPU 확인 대기 (위 절 다음에 읽는다)

**상태**: 수정 · 정리 브랜치 14 개(`registrylogs` → `scenlogs` → `logids` → `apptestfeature` · `scenfixes` → `lowlevelgaps` · `lowtier` → `glyphbudget` · `roughnessparam` · `editorscen` · `gputhresholds` → `staticgate` · `poseaudit` · `staticleft`)와 셰이더 쿠킹 `cookfast` · `cookdeps` 가 main 에 들어갔다. 검증은 Debug `full-build`(경고 0 · 오류 0) · `ctest -L nogpu` 69 · `lint` · `race` 통과, `lint-suite` 47/47, Shipping 전체 빌드 + `nogpu` 64 통과, Shipping-Server 전체 빌드 + `nogpu` 66 통과, Release `--cook-shaders`(12740 개 다시 굽기 272 s, 두 번째는 0 binaries generated 1958 ms) · `--verify-shaders` 통과(바이너리 바이트 변경 없음 — 스탬프 12 개만 새 판). 커밋 메시지가 정본이다.

**main CI(실행 40121848026)가 졌던 것을 고쳤다**: 마지막으로 빌드까지 돈 초록이 `89b535b7e` 였고 그 뒤 여러 커밋은 Lints 가 먼저 져 빌드 잡이 건너뛰어져 결함이 쌓여 있었다. Linux 컴파일 여섯 곳(`TestLowLevelReport` 의 `unistd.h`, `TestX11DevConsole` 의 매크로 `False`, `WaylandWindow::State` private 이름), Windows Shipping ctest(활성이 아닌 게임 원본을 읽는 케이스 8 개에 `SourceResources`), 전용 서버(RHI 를 이름으로 찾는 케이스 5 개에 `ClientCode`). 못 푼 것: **Windows STL containers 의 `UIDeterminismTest.RebuildGivesSameDump`** 는 글리프 래스터 예산이 시간제이고 프레임 시계가 죽어 있던 결함(`glyphbudget`)과 같은 증상이라 병합으로 풀리길 기대한다 — 로컬에서 STL 구성을 안 돌렸다. Linux TSan 은 Setup 단계 시한(exit 124)이라 환경으로 본다. **Linux 컴파일은 로컬에서 못 본다** — push 뒤 `py -3 -m Scripts ci-jobs --summary` 로 본다(특히 `cookdeps` 의 `RecordingDxcInclude` 가 Linux DXC WinAdapter 에서 `IsEqualGUID` · `__uuidof(IUnknown)` 로 컴파일되는지).

**통합이 병합에서 고친 것**: `KnownLog` 충돌은 `scenlogs`(허용 목록) 기준. `BuildFeature` 에 `X11ConsoleOverlay` 를 `apptestfeature` 항목 뒤에 더함. `EditorStateStore.h` 를 층 표의 잎 헤더(`GenerateEditorLayers.toml` prelude)에 올림(`staticleft` 가 토대 층 ImGui 백엔드에서 씀). `ScopedLogSuppressor` 가 싱크를 비우지 않고 버리는 싱크를 건다(`lowlevelgaps` 가 싱크 없는 리스너 붙임을 멈춤으로 바꿔 시나리오 로그 포착이 죽던 것). `logids` 의 `countContaining` → `countWithID` 치환 오류 넷(`TestFile` · `TestMaterial` · `TestXMLDocument` · `TestConfigManager`)을 고쳤다 — **같은 모양의 치환 오류가 `hostgpu` 로만 도는 테스트에 남았을 수 있다**. `cookdeps` 는 빌드 없이 커밋돼 컴파일 오류 둘(문자열 삼항식, `unordered_set` 범위 삽입)이 있었고 `cookfast` 의 병렬 합치기 위에 다시 얹었다(작업 스레드는 결과 칸에만 쓴다). `CheckMutableStatics` 대기 표를 다시 만들었다(292 → 233 파일, 상한 합 628 → 505).

**GPU 확인 대기(`gpu-verifier`, Release · Shipping, 네 백엔드 — "Warning/Error 는 통과가 아니다", 건너뜀 규칙 적용)**
1. `hostgpu` 전체 한 번. 새로 보는 것: `scenlogs` 의 종료 코드 15(`UnhandledLog`) — id 없는 `SW_LOG_WARNING/ERROR` 약 3,500 곳이 있어 허용 목록(`allowedlogs.json`)이 비어 있는 동안 Warning/Error 를 남기는 시나리오는 15 로 끝난다. 먼저 `py -3 -m Scripts scenario-logs --regen --dry-run`.
2. `glyphbudget`: `uidemo` 시나리오(글리프가 다 채워지길 `WaitUntil`), Shooter3D HUD 프레임 3 · 300, `WaitUntil` 시한 초과가 종료 14 가 아니라 실패인지.
3. `roughnessparam`: Water · Terrain · Foliage · Environment 에서 `_roughness` 등 새 Error 가 없는지.
4. `editorscen`: 에디터 시나리오 8 개 단독, `EditorScenariosPassOn<백엔드>` 체인. `scenfixes`: `nilecity` pause · control · gamepadcursor, `shooter3d.crowd` 의 `WaitUntil` 시한.
5. `gputhresholds`: `useroptions` dx12 `distance` 가 0.665 근처로 오르는지(안 오르면 `DepthClipEnable` 추정이 틀림) 후 그 값을 dx12 measured 에 더함; `shippingframe` dx12 골든을 다시 뜨고(`GpuVerification.md` 3.1, 매니페스트 `shipping-golden-dx12`) 통과 확인; DepthClip 수정이 shadow 시나리오 · 벤치 골든을 바꾸는지. gl 의 GPU 라이트(`vfxgpulights`)는 원인을 못 찾았다(라이트 버퍼를 `AsyncBufferReadback` 으로 되읽는 것이 첫 단계). 같은 모양: 0 으로 시작하는 PSO 서술이 백엔드 기본값과 조용히 다른 칸이 더 있을 수 있다.
6. `lowtier`: Debug · Release · Shipping · Shipping-Server 링크는 봤다. **Debug-Cold(pch 끔) · 정적 Shipping · CI-Debug-STL 링크와 유니티 빌드, `PoolAllocator.cpp` 에 `SW_LOG_ERROR` 를 임시로 넣어 컴파일 실패하는지는 안 봤다**(CI 가 정적 Shipping · STL 을 본다).
7. `poseaudit`: 래그돌 · 군중 · 리그 · 리타깃 · 얼굴 테스트의 새 단언은 Debug nogpu 에서 통과했다 — `hostgpu` 에서 따로 볼 것 없음.

**아직 못 고친 것**: `editor.charactereditor` 체인(`charactereditor.slider.last` 가 창 밖 — `CharacterEditorPanel.cpp` 의 첫 슬라이더에 `note`), `startAfter="Immediately"` 시나리오가 `ContentWatchFence` 를 못 막는 것(`AutomationRunner::onFrameBegin` 이 시작 차단을 무시), `shooter3d.crowd` 근본 원인(`scenfixes` 는 단언만 `WaitUntil` 로 바꿨다), id 없는 `SW_LOG_WARNING/ERROR` 약 3,500 곳(`scenario-logs --regen` 의 "id 없음" 표를 영역별로 나눠 소스에 `_ID` 를 붙인다 — 병렬 작업자), 한영 숫자 혼합에서 중립 문자가 앞 런의 글꼴을 이어받는 동작(의도로 정할지), 가짜 굵게가 레이아웃 폭을 안 바꿈, 한글 · 숫자 자간 측정, MSDF 베어링 단언, HarfBuzz 경로의 `.ttc` face index 0 고정, Shipping 빌드의 `TestAppUI` 미사용 상수 경고 셋.

**다음 세션에서 할 일(순서)**
1. push 뒤 `py -3 -m Scripts ci-jobs --summary` 로 main CI 를 본다(Linux 컴파일 · STL containers `UIDeterminismTest` · 서버 Shipping). 지면 `--annotations` 로 원인을 읽는다.
2. 위 GPU 확인(`gpu-verifier`).
3. [ArchitectureSeams](ArchitectureSeams.md)의 0 단계(측정, 통합 혼자)와 1a · 1b · 2a · 3 · 4(병렬 가능) 착수. 리팩토링 트랙은 같은 문서 3 절. **팩 쿠킹 병렬화**(`ResourcePakWriter::makePackBytes` 항목별 압축을 `runParallel`, `ContentHash::computeBytes` 의 바이트별 `push_back` 복사 줄이기)를 아키텍처 이음매 작업과 같이 진행한다. 씬 · 프리팹 · 레지스트리 · VAT · 내비메시 쿠킹은 `CookStore` 공유 상태(`clearAssetCaches` 전역 호출, `record()` 락 없음) 때문에 병렬화하지 않는다. 먼저 팩 단계별 시간을 잴 것.
4. 후보로 올린 것: ① **`half` · `half2~4` 저장 전용 압축 타입**(+ `unorm` · `snorm` · 옥타헤드럴 법선 · `RGB10A2`, 이식 가능한 비트 연산 구현, 산술은 `float` 로 풀어서, 월드 좌표 · 큰 시간값에는 쓰지 않음을 문서화. 단위: Core/Math 타입과 테스트 → 직렬화 · 리플렉션 → 정점 · 인스턴스 데이터 적용. 결정 기록에 한 줄). ② **시나리오 단계 인자를 리플렉션 구조체로 선언**해 스키마를 자동으로 만들고 린트에서 시나리오 XML 을 검증, 탐침을 `Probe` 속성이 달린 프로퍼티에서 노출(단계 인자가 이미 스키마를 갖는지 먼저 코드로 확인). ③ `scenario-logs` 래칫을 `suite` 항목에도.
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
