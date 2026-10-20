# 다음 세션 인계

이 문서는 **지금 상태**만 둡니다. 새 세션은 이 문서를 먼저 읽고, 나머지는 아래에서 일에 맞는 것만 찾습니다. 한 사실은 한 곳에만 씁니다.

| 알고 싶은 것 | 어디 |
|---|---|
| 남은 일 한 건 한 건 | 후보 문서 `Candidates-<영역>.md` — `py -3 -m Scripts plans --area <영역>`(목록), `--file <이름>`(머리 줄과 절 목록) |
| 설계와 구현 단위 | 계획 문서 — `py -3 -m Scripts plans`(목록) |
| 앞으로 할 일의 순서 | [로드맵](Roadmap.md) |
| 결정과 근거 | [결정 기록](../09_Decisions.md) |
| 작업 방식 · 검증 | [작업 방식](../11_Workflow.md), [검증과 측정](../08_Verification.md) |
| 에이전트에게 맡길 때 · 토큰 | [에이전트와 토큰](../12_Agents.md), [작업자 규칙](WorkerRules.md) |
| 환경 · 도구 함정 | [함정](../14_Pitfalls.md) — 해당하는 일을 할 때만 |

한 일은 `git log` 가 정본입니다.

## 1. 지금 상태 (2026-10-20, 밤 세션 끝)

- **main 은 한 번에 push 한다**(`full-build` 증거 있음, 병합 작업자 브랜치는 모두 들어감). 이번 세션 병합분: `kitsxml` · `hashutil` · `hashutil2`(FNV 단일 정본 `HashUtil`) · `hc1` · `hc2` · `hc3`(`PackEngineSettings`, `GameConfig` 는 `_packRoot` 만) · `dead`(죽은 인자 · 호출처 없는 선언 · `UnionFindUtil`) · `cifail`(애니메이션 의존 고리 레이스) · `respclock`(`IRespClock`) · `silentdef`(`parseFloat2/3/4` 정확한 개수, 재질 `value` 는 `readRequiredShaderValue` 1~4 개) · `textaddr` · `camdir` · `paired`(P3a 스냅샷) · `tcbench`(측정 하네스) · `warnfix`(컴파일러 경고 0, `SW_LOG_DISCARDED`) · 다른 세션의 `edcand`(패널 열기 요청 형식, 후보 `ScenarioWritesSourceTree`).
- **검증한 것**: Debug `full-build` 경고 0, `ctest -L "lint|nogpu|race"` 115/115, Shipping 빌드, Shipping `ctest -L hostgpu` 8 통과(QaGoldenImages 는 Shipping 에 벤치 옵션이 없어 건너뜀 — 골든 이미지 비교는 아직 한 번도 못 봄), Debug 네 백엔드 에디터 체인 종료 코드 0, `projectconfigedit` 네 백엔드 PASS(`packenginesettings.png` 네 장 확인), 게임 11 개 창 제목(Debug DX12).
- **이어서 확인한 것(같은 날 밤)**: 새로 지은 Shipping 전용 서버(테스트 8 통과, 기동 · 종료 0), Release `QaGoldenImages`(Empty 네 백엔드 통과 — 골든 이미지 비교를 처음 봄), Shooter3D 기본 FOV 80(Release DX12, HUD 85.2° = 세로 렌즈 75° 에 80 적용, `-gv_cameraFieldOfView=70` 이면 75.0°), 일시정지 수정(`pausefix`)을 NileCity 네 백엔드와 8 개 게임에서. **일시정지 결함**: Esc 를 2 프레임 이상 누르면 메뉴가 열린 다음 프레임에 닫혔다(`UIInputRouter` 가 소비 여부를 안 봄) — 고쳤다. 같은 모양이 `InputMap`(꺼진 층의 눌림 추적 없음)에 남을 수 있다.
- **남은 확인 한계**: 플로팅 창 캡처는 창이 별도 플랫폼 창(뷰포트)으로 떠서 합본에 든 경우까지는 보지 못했고 수정 전 코드와 대조하지 않았다. Shipping 은 `-game=` 을 무시하고 에디터 시나리오를 못 돌려 게임 선택이 필요한 검증은 Release(`SW_GAMES=ALL`)로 한다. 바인딩이 만들어진 프레임에 눌렸다 바로 뗀 단발 탭은 Held 로 시작해 다음 프레임 Pressed 가 안 읽힐 수 있다(홀드는 정확).
- **같은 날 밤 끝낸 결함 넷**: 일시정지 메뉴가 Esc 를 쥐면 바로 닫힘(`UIInputRouter` 소비 검사 → 이후 `InputMap` 이 꺼진 레이어 · 리바인딩 · 초기화 직후 눌림을 `_bLive` 하나의 규칙으로 Held 시작), OpenGL 에디터 캡처가 연 패스를 닫지 않아 플로팅 창이 한 번 더 그려짐(`EditorUICapture::renderJob`), 시나리오 `ExpectVariable` 스텝 신설(`fieldofview.scenario.xml` 로 Shooter3D 기본 FOV 80 단언). 콘솔이 쥔 키는 `InputManager` 의 captured 마스크가 이미 뗄 때까지 가린다(콘솔 경로는 결함이 아니었고 테스트 기대를 고쳤다). Release 로 일시정지 네 백엔드 · 8 개 게임, FOV 통과와 틀린 값 실패, 플로팅 창 캡처 네 백엔드, 에디터 체인 네 백엔드를 확인했다.
- **CI**: push 뒤 Windows Debug · Linux TSan 이 통과해야 GpuVerification V2 의 "연속 3 회" 를 센다. 정책 수정이 든 main 의 CI 를 세 번 읽은 뒤 `continue-on-error` 해제와 `softwareRenderSeconds` 수정.
- **TypedComponents 앱 측정 넷**(독점 Release)은 아직. 하네스는 `ea3c769c4`, 옛 바이너리는 `worktree-make tcbefore` 로 `1828fc879^` 에서. 측정 뒤 `끝` 으로.
- **TextAddress 후속**: `arePropertyValuesEqual` · `applyPropertyDefault` · `tryCoerceBinaryPayload` · `applyOrphanBinary` · 컨테이너 방문자 인터페이스가 아직 `void*`.
- **그 밖의 남은 작은 것**: `FrameRenderer` 의 `cbIndex` 죽은 인자, `GameDataXML::parseFloats` 직접 사용처(`SRPGCatalog:235` · `WesternCatalog:24` · `GimmickNodeRegistry:499` · `GimmickCircuit:47`), `TradeService` 의 `serverID * kFnvPrime64`, `TickRegistry::containsSorted` · `AIDirector::containsAny`, `GameSettings` 문자열 경로 필드, `using Internal` 복붙 별칭(구조로 못 막음 — 클린 빌드의 경고로만 보임).
- **AudioChannelLayout U1** Release 비교, **EditorMultiWorld 6절 5·6**(열고 닫기 반복, `-gv_rhiSwapAtFrame`)은 시나리오를 새로 만들어야 할 수 있다.
- **사용자 결정 대기**: (1) PairedAnimation P3b 세 질문 — 세션 시작 메시지와 핸들→객체 id 대응을 어디에 둘지(권고: 기존 네트워크 킷 복제 재사용), 늦게 들어온 사람은 `makeAtElapsedTime` 인지 서버의 정확한 진입 시각인지(권고: 서버가 정확한 시각을 보냄), 게임이 고른 역할의 `exitState` 복원(권고: 지금 규칙 그대로). (2) 키트 51 개를 견본으로 둘지 기본 빌드에서 뺄지. (3) PK3 · TX2 권고안(해당 단위 전에 알림).

### 1.1 리팩토링 후보(읽기 전용 감사 셋의 결과, 코드는 안 고침)

작업자 묶음은 서로 파일이 겹치지 않게 나눈다. 위험이 낮은 것부터, [동작] 이 붙은 것은 테스트 · 스크린샷과 함께.
- **먼저(기계적, 낮음)**: 이번 세션에 끝났다(`kitsxml` · `hashutil` · `dead`). 남은 것은 위 1절의 "그 밖의 남은 작은 것".
- **그다음(중간)**: 배치 목록 3 벌 복사를 1 벌 + 구간 뷰로(`GPUSceneSnapshot.h`, 성능 이득이 큼), 캐시 클래스 10 개를 `SharedAssetCache` 템플릿으로, `captureSnapshot` 의 직렬화 두 번 돌기 · `makeGameObjectXMLContext` 반복 등록, 
- **마지막(GPU · 벤치 · 셰이더 회귀 필요)**: `FrameRendererPassExecute` 462 줄, `GPUSceneBuilder::populateMeshesFromScene` 383 줄, `GPUMeshVertexPool` ↔ `MorphPool` 공용화, `FrameRenderer::execute(Scene&)` 이중 경로 합치기, 2D · 3D 물리 컴포넌트 쌍 템플릿화, `reflectSpirv` 416 줄.
- **하지 않는다**: `VfxCPUSimulation` 의 `lerp` 를 `MathUtil::lerp` 로 바꾸기(부동소수 결과가 달라지고 GPU 시뮬레이션과 수열이 어긋난다). `StateArchiveUtil` 쓰기 · 읽기 두 벌 구조는 큰 구조 변경이라 건별로 하지 않는다.

### 1.2 구조 단순화(상용 엔진 비교)

결정: 셰이더 바이너리 커밋 유지, 백엔드 등급제 안 함(결정 기록 5-12). 하기로 한 것은 `StructureSimplification`(계획 문서, `structplan` 이 작성 중)의 순서 — 5(HC1~HC3 끝, HC4 남음) → 2(팩 쓰기 · 스탬프를 C++ 로) → 3(에셋 종류를 등록 줄 하나로, "에셋 로더 등록제" 보류를 다시 연다) → 4(텍스트 형식을 역할마다 하나로, `KeyValueFile` 제거). 키트 51 개(게임이 이름으로 부르는 것은 9 개)를 견본으로 둘지 기본 빌드에서 뺄지는 사용자가 아직 정하지 않았다.

- **vcpkg**: 워크트리는 메인의 `Tools/vcpkg` · `build/vcpkg_installed` · `build/vcpkg_cache` 를 정션으로 공유한다(사용자 지시). 워크트리를 지울 때는 `worktree-remove`. 빈 폴더 `D:/w/audiolayout` · `D:/w/vfx` 는 다른 프로세스가 쥐고 있어 남았다.
- GPU 는 이 PC 에서 된다(RTX 3070 Ti, DX12). 못 한 것: TypedComponents 앱 측정 넷(8.4), 다른 세 백엔드 화면, Vfx V2 · V3, 패널 메뉴 토글 등 X 가 아닌 닫는 길(`panelgap` 이 다룸).
- NetSettingsRemaining 은 설정 데이터 형식을 새로 정해야 해서 DataDriven 과 함께 다룬다.
- PairedAnimation 남은 것: P3 본체(복제), 시나리오 테스트(활 · 클립 에셋 없음), M3 에디터.
- 야간 `Load (Linux | Release, nightly)` 근본 원인은 모른다. 근본 원인 후보(미해결): `DiskPathVsResourceId`.

## 2. 첫 일

1. 사용자 지침(2026-10-20): 되묻지 않고, 레이어가 변하지 않는 범위에서 상용 엔진과 우리 엔진이 발전할 방향을 기준으로 구조적 해결을 한다. 각 작업이 끝날 때마다 push 한다(`full-build` 증거 게이트). 구현을 먼저 하고(빌드 없이, 호출처 전수 grep) 병합한 뒤 `full-build` 한 번, 그다음 테스트 · GPU 검증. **GPU 검증은 Shipping `hostgpu` 가 기본이고 네 백엔드를 처음부터 한 번에** 돌린다(Debug 는 Debug 에서만 의미 있는 것만).
2. 이 세션의 push 와 CI 통과를 확인하고 워크트리를 지운다(위 1절).
3. **다음 일**: `StructureSimplification` HC4(`ConfigCatalog.py` · `docs/Config/*` · `Config/README.md` 의 `ensureConfig` 문장, `Scripts/common/Config.py` 가 `{}` 를 돌려주는 곳 W5) → PK → AT → TX(진행 3/21). 각 단위 전에 위 결정 대기 PK3 · TX2 권고를 알린다.
   병렬로 띄울 묶음(`[병렬 가능]`, 파일 겹침 없이, 동시 빌드 최대 2): (가) 1.1 "그다음(중간)" — `GPUSceneSnapshot.h` 배치 목록 3 벌 복사, `SharedAssetCache` 템플릿, `captureSnapshot` 직렬화 두 번 돌기. (나) 위 1절 "그 밖의 남은 작은 것"과 TextAddress 후속 `void*`. (다) Roadmap 의 `[병렬 가능]` — Vfx V2 · V3(`gpu-verifier` 와), 카메라 디렉터, 애니메이션 · PairedAnimation P3b(결정 뒤), SerializerSilentSkips, AsyncResourceLoading A2(설계 검토 뒤).
4. TypedComponents 앱 측정 넷(독점, Release)과 AudioChannelLayout U1 은 PC 를 혼자 쓸 때 한꺼번에.

## 3. 새 세션 시작 절차

1. 이 문서만 먼저 읽는다. 일을 정하면 그 일의 계획 문서를 `py -3 -m Scripts plans --file <이름>` 으로 읽고, [작업 방식](../11_Workflow.md) 은 필요한 절(빌드 2절 · 검증 3절 · push 4절)만 읽는다. 에이전트를 띄울 때만 [에이전트와 토큰](../12_Agents.md) 과 [작업자 규칙](WorkerRules.md). C++ 을 쓰기 전에 [AGENTS.md](../../AGENTS.md) 목차에서 일에 맞는 파일만.
2. `git worktree list` 로 남은 워크트리를 보고 병합이 끝난 것을 지운다.
3. 큰 일은 사용자와 순서를 확인한 뒤 시작한다. 시작 전에 graphify 그래프를 갱신한다(`graphify update Source`, 약 4 분).
