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

## 0. 2026-10-21 세션 끝 — 구현만 끝낸 브랜치 약 40 개가 병합을 기다린다 (이것부터 읽는다)

사용자 지시: "모두 다 해 · 미루지 말 것 · 측정은 나중, 구현 먼저 · 별칭은 없앤다". **전부 구현만 했고 빌드 · 테스트 · 측정 · GPU 검증은 한 번도 안 돌렸다(빌드 미확인). push 도 없다.** main 은 이 세션에서 문서 하나(이 절)만 늘었다.

**다음 세션 첫 일**: `git worktree list` 와 `git log main..wt/<이름> --oneline` 으로 브랜치별 커밋을 읽는다. 작업자 일부는 이 세션이 끝난 뒤에도 커밋을 더하고 있을 수 있어 먼저 `git worktree list` 로 새 커밋이 멈췄는지 본다(보고는 이 세션에 쌓였고 새 세션은 못 받는다 — 커밋 메시지와 각 계획 문서가 정본). 그다음 `integrator`(opus, high)에게 맡겨 아래 순서로 병합하고 **단계마다** `full-build` 한다(한 번에 다 합치지 않는다).

**병합 묶음과 순서** (같은 줄의 브랜치는 앞의 것 위에 쌓였다 — 앞에서부터 병합):
1. 작은 것 · 도구: `wt/wtclean` · `wt/hc4` → `wt/hc4b` · `wt/docsclean` · `wt/smallfix`
2. 직렬화: `wt/silentskip` → `wt/silentskip2` → `wt/silentskip3` · `wt/voidstar` → `wt/voidstar2` (같은 Serializer 파일, silentskip3 · voidstar2 가 서로를 따라갔는지 확인)
3. 저장 · 설정 · 상태: `wt/capsnap` → `wt/abisink`(ABI v7, 옛 게임 DLL 거부) · `wt/gsdatapath` → `wt/gspaths2`
4. 렌더러 자료: `wt/batchview` → `wt/oparity` · `wt/assetcache` → `wt/assetcache2`(별칭 제거, `Macros.h` 로 전 모듈 재빌드)
5. 구조: `wt/enginepart`(E3 · E4, 전 모듈 재빌드, batchview · oparity 를 이미 병합함) · `wt/scriptsstruct`(Scripts 구조 개편, 설계는 아래) · `wt/meshbuild`(BuildSpeed, 헤더 팬아웃)
6. 도메인: `wt/vfx2` `wt/lighting` `wt/wstream` `wt/datadriven` `wt/camera` `wt/previewwin` `wt/gfxformat` `wt/audioext` `wt/onlinekits` `wt/animation` `wt/netdebug`
7. 정리: `wt/corenet`(묶음 1) `wt/kitclean`(2a) `wt/baseclean`(2b) `wt/longfn`(5) `wt/editorcommon`(6a)
충돌이 가장 많은 곳은 `Roadmap.md` 와 README 표(생성물 — `status-board --write` 로 다시 만든다), `Candidates-engine.md`, 렌더러 파일, `GameInstanceBase.cpp`, `docs/Config/*`(`GenerateConfigReference.py`). 린트 기준선 `Scripts/lint/rules/CheckDataDrivenStructure.toml` 에 한 작업자가 main 체크아웃에 잘못 쓴 줄이 한 번 있었다(되돌림) — 병합 전 `git status` 로 main 이 깨끗한지 본다.

**직렬화 병합 메모**: `wt/silentskip3`(S3, `FieldReadResult`)는 `wt/voidstar` 를 이미 병합해 두 변경을 합쳤다. `wt/voidstar2`(남은 `void*`)는 시작할 때 `silentskip3` 를 가져오게 했으니 둘 중 나중 것 하나만 병합해도 된다. **S4(남은 단위)**: 바이너리 `BinarySerializer::deserialize` · `deserializeValueBinary` · `applyPropertyBinary` 가 아직 필드 실패와 객체 실패를 `bool` 하나로 돌려준다 — `deserializeTagged` 등이 `FieldReadResult` 를 돌려주게 해야 한다(`SerializerSilentSkips` 3절). 추측: `tryCoerceBinaryPayload` 가 기록 타입이 다른 비트필드의 바이트 주소에 바로 쓸 수 있다.

**세션 후반 갱신 (2026-10-21)**:
- 통합 파동 A 가 `wt/integA`(워크트리 `D:\w\integA`)에서 진행 중이다 — 위 1~5 묶음 20 개 브랜치를 순서대로 병합하며 묶음마다 `full-build`. 보고는 이 세션으로 가므로 새 세션은 `git log wt/integA --oneline` 과 그 브랜치의 병합 커밋으로 어디까지 갔는지 읽는다. 끝나지 않았으면 같은 순서로 이어서 한다. push 는 하지 않았다.
- 구현이 끝난 것이 더 늘었다(빌드 미확인): 도메인 `wt/gfxformat`(지형 편집 비용 T1·T2, 디퍼드 툰 `ForwardOnlyOpaque`, 툰 머티리얼 19 개 다시 씀) `wt/wstream`(WorldStreaming W2~W5, 씬 19 개 판 2 변환, W6 일부, W7 데이터) `wt/vfx2`(V7·V6) `wt/previewwin`(PSO 공유, 탐침, 스카이 컴포넌트) `wt/meshbuild`(MeshModifierStack, BuildSpeed 12) / 정리 `wt/corenet`(묶음 1) `wt/kitclean`(묶음 2a) `wt/voidstar3`(인스펙터·UIBinding 주소 형식, `wt/voidstar2` 위, `wt/meshbuild` 병합함). `wt/voidstar3` 가 있으므로 voidstar → voidstar2 → voidstar3 순으로 병합한다.
- **커밋 못 한 셰이더 변경 둘**(커밋 훅 `CheckShaderStamps` 가 쿠킹 바이너리 없는 셰이더를 막는다): `wt/vfx2` 의 `Resource/engine/shaders/particle.hlsl` 띠 경로(워크트리에만 있음, 사본 `C:\Users\ssw\AppData\Local\Temp\claude\d--Projects-Personal-LearningTemplate\af0db063-9628-4229-951e-ac003a2b3cd8\scratchpad\particle.hlsl.v6`) 와 `wt/previewwin` 의 스카이 패스 15 파일(`RenderPassType::Sky`, `sky.hlsl`, `PassCB` 세 칸 등, 사본 같은 폴더의 `previewwin_skypass.patch`). 병합하고 빌드한 뒤 `App.exe --cook-shaders` 로 네 백엔드를 굽고 소스와 바이너리를 함께 커밋한다. 그때까지 띠 방출기는 더하기 빌보드로 잘못 그려지고 하늘은 안 그려진다. 사본이 사라졌으면 두 워크트리의 `git status` 에서 그대로 읽는다.
- **결정**: Vfx V4 는 Niagara 식(방출기마다 시뮬레이션 대상을 정함, 빌보드·띠만 GPU, CPU 전용 모듈을 쓰는 방출기는 모든 빌드에서 CPU)으로 가기로 했다 — "Shipping 은 CPU 경로 없음" 이라는 기존 결정과 다르므로 구현할 때 결정 기록 5-12 에 바꾼 이유를 적는다. `Vfx.md` V4 계약 절이 정본이다. V4 구현은 빌드 · 셰이더 쿠킹이 되는 통합 뒤에 한다.
- 남은 단위: 직렬화 S4(위), TerrainEditCost T3 Release 측정 · T4 GPUScene 수집, WorldStreaming W6 에디터 UI · W5 서버 게임 코드 연결, ToonShadingCompletion 의 `#error` 쿠킹 차단(셰이더 재굽기 뒤), Shaded Wireframe(RHI 깊이 비교 · 바이어스가 먼저), Online 쪽 FunctorLambda · 긴 함수 · 주석(`wt/onlinekits` 병합 뒤), Core 나머지 폴더의 주석(`wt/enginepart` 병합 뒤), `std::remove_if` 쓰기 막는 게이트(마지막 디렉터리 정리 뒤). 프리팹 판을 올릴 때는 `Scripts/dev/MoveSceneFormat.py` 방식의 변환 도구가 필요하다.
- 실행 중이던 작업자(이 세션이 끝나면 보고가 새 세션에 전달되지 않는다. 커밋을 읽는다): `wt/lighting` `wt/datadriven` `wt/camera` `wt/audioext` `wt/onlinekits` `wt/animation` `wt/netdebug` `wt/baseclean` `wt/longfn` `wt/editorcommon` `wt/panelclean` `wt/scriptsstruct` 와 `wt/integA`.

**세션 끝 갱신 (2026-10-21 밤)**:
- **(a) 진행 중**: 통합 `wt/integA` 는 묶음 1(도구) · 2(직렬화) · 3(저장 · 설정: abisink · gspaths2 까지 병합)을 마쳤고 묶음 4(batchview · oparity · assetcache · assetcache2)와 5(enginepart · meshbuild)가 남았다. 사용자 지시로 **테스트는 마지막에 한 번만** 돌린다(묶음마다는 빌드 확인과 컴파일 오류 수정까지). 작업자 중 보고가 안 온 것: `wt/lighting` `wt/datadriven` `wt/onlinekits` `wt/baseclean` `wt/longfn` `wt/panelclean` `wt/scriptsstruct` `wt/serials4` `wt/pathform` `wt/maxcount`.
- **(b) 새로 끝난 브랜치(빌드 미확인)**: `wt/editorcommon`(d836a61e1) `wt/netleft`(2b9d0fde5) `wt/audiovalid`(6a27a49b5) `wt/scatterleft`(d5062d407) `wt/passtail`(0f69b5943, vfx2 위) `wt/lensleft`(a28f82248, camera 위) 와 앞서 적은 것들. 병합 순서: audiovalid 는 audioext 뒤, netleft 는 netdebug · corenet 뒤, scatterleft 는 gfxformat 뒤, passtail 은 vfx2 뒤, lensleft 는 camera 뒤, editorcommon 은 previewwin · meshbuild 뒤.
- **(c) 사용자 결정**: `OperationsAndDeployment`(2026-10-13 보류, 맞다고 확인) · `CcZeroAssetsMechArena` · `FNameNumberSuffix`(설계 제외) 는 하지 않는다. Vfx V4 Niagara 식(위)은 권고안으로 갔다.
- **(d) 병합 · push 못 한 변경**: 전부. push 없음. 커밋 못 한 셰이더 변경 셋(`vfx2` 띠 · `previewwin` 스카이 · `camera` 후처리, 사본은 스크래치패드) 은 빌드 뒤 `--cook-shaders` 로 굽고 함께 커밋.
- **(e) 병렬로 띄울 묶음**(통합이 끝난 뒤): 게임플레이 기능 후보 약 17 개(`Candidates-gameplay.md` 기능 절)를 영역별로, 그래픽스 5 · 에디터 4 · UI 5 · 네트워크 4 · 엔진 3 · 오디오 2 개. 정리 묶음 3(HeaderIncludeSlimming · EngineServiceInjection) · 4(그래픽 긴 함수 · DiskPathVsResourceId) · 7(시험→테스트) · 8(NamingAuditNext, 마지막 단독). Online 쪽 FunctorLambda · 긴 함수 · 주석, Core 나머지 폴더 주석, `std::remove_if` 게이트.
- **작업자가 범위 밖으로 남긴 조용한 폴백**: `EditorNotificationManager::push` 의 `durationSec > 0.5f ? : 4.0f`, `EditorNetProfilerSeries` 의 `max(capacity, 1)`. 같은 모양: 렌즈 시프트(비대칭 투영)는 읽는 쪽이 생길 때 `CameraDepthOfFieldValues` 방식으로 옮긴다.
- **환경 함정**: 작업자가 `python - <<'EOF'` heredoc 을 본문 없이 써서 표준입력을 기다리며 헛도는 `python.exe -` 프로세스 16 개가 코어 하나씩 쓰며 CPU 100% 를 만든 일이 있었다(2026-10-21, 종료함). 통합 빌드가 느리면 `Get-Process python | ? CPU -gt 1000` 으로 먼저 본다.

**못 한 것 모음 (2026-10-21, 작업자 보고에서 모음)** — 한 건도 지우지 않고 나눠 둔다.
- **A. 빌드 · 셰이더 쿠킹이 있어야 하는 것(통합 뒤 첫 파동)**:
  - 셰이더 소스 커밋 넷: `lighting`(`bindingslots.hlsli` `binding.hlsli` `lighting.hlsli` `canvas.hlsl` `lightgrid.hlsl` `lightgridcommon.hlsli`) · `vfx2`(`particle.hlsl` 띠) · `previewwin`(스카이 패스 15 파일) · `camera`(`postgrade.hlsli` `tonemap.hlsl` `postchain.hlsl`). 전부 `binding.hlsli` · 슬롯 번호를 건드리므로 **병합해 슬롯 충돌을 푼 뒤 한 번에** `--cook-shaders` 로 굽고 소스와 바이너리를 함께 커밋한다. `particle.hlsl` · `VfxParticleRenderer` 주석의 옛 번호 `t12` 를 고친다.
  - Vfx V4(Niagara 식), 피사계 심도 패스(CameraRendererIntegration S2: 새 패스 종류 + 파이프라인 XML 넷), ToonShadingCompletion 의 `toon.hlsl` `#error`(define 빠진 머티리얼 쿠킹 차단), Shaded Wireframe(RHI 깊이 비교 · 바이어스가 먼저), IndirectLighting P4 · P5(선행 D2-6.4 G버퍼 셋째 타깃).
  - 임포트가 필요한 것: 활 · 화살(`MakeBowProps.py` → `--import-models` → 프리팹 → 시나리오), 휘두름 피해 알림(사이드카 변경 후 재임포트, Shooter3D 적의 `_attackHitTime` 을 데이터로), `wstream` 씬 변환 실행(`--cook-scenes`), 프리팹 판을 올릴 때의 변환 도구.
  - 링크 오류를 보며 하는 것: `DedicatedServerNoRenderer`, ServerDebug S5 흐름 이벤트(타임라인 + Core 네트워크), BuildSpeed 남은 것(`Component.h` · `RHITypes.h` 가 `ReflectionCore.h` 묶음 전체를 읽는 문제, `GameObjectManager.h` 재검토, 5 · 6 절).
  - 문자열: 새 문자열 ko/ja 번역(`App --gather-text` + `LocalizationGatherTest`): `EdMeshModifiers.PlayPreviewTime`, 오디오 `audio.speakerMode` 설정, 네트워크 툴팁 9 개.
  - 실행해 봐야 하는 에디터 일: `GraphSaveScenarioRun`(`dialoguegraphsave` · `animgraphsave`), 에디터 패널 스크린샷, 시나리오 uiaudit.
- **B. 지금 띄울 수 있었으나 아직 안 띄운 것(다음 파동)**:
  - 게임플레이 기능 후보 약 17 개(`Candidates-gameplay.md` 기능 절), 그래픽스 5 · 에디터 4 · UI 5 · 네트워크 4 · 엔진 3 · 오디오 2.
  - StructureSimplification: HC4 마무리(Shipping 구성 컴파일 확인), **PK → AT → TX** 는 이번에 손도 못 댔다(AT1 은 `AssetFormat.h` 를 바꿔 전 모듈 재빌드). AsyncResourceLoading A2(설계 검토 뒤), GpuVerification V2(CI 세 번 확인 후 `continue-on-error` 해제), EditorCharacterEditor 0/6(선행 EditorMultiWorld 6절 끝난 뒤), WorldStreaming W6 에디터 UI · W5 서버 게임 코드 연결 · `MMOReplicator` 격자를 `WorldCellGrid` 로, TerrainEditCost T3 Release 측정 · T4 교체 프레임의 GPUScene 수집.
  - 애니메이션: AnimationLodAndCrowd(선행: 적 몸 외형 소켓 작업), 대상 본 기즈모, 1인칭에서 머리만 숨기기, 천 제자리 갱신(`GPUScene` 변경이 들어온 뒤), 천 Skinned 구속, 파괴 복제의 이름순 번호표가 씬 밖 생성 오브젝트를 못 다루는 한계.
  - 오디오: E5 절차적 · 그래뉼러 소리 그래프(설계 전), E6 에디터 비용 측정, 버스별 레이아웃.
  - 카메라: S3 컷 신호 · S4 가려짐(렌더러 기능이 먼저), GamepadVirtualCursor G2 Nile · Skirmish 바인딩 결정 · G3 실기, 렌즈 시프트(비대칭 투영)를 `CameraDepthOfFieldValues` 방식으로.
  - 네트워크: ServerDebug S1 포트 샤딩(키트 재설계 + 측정이 착수 조건) · S6 게임 뷰 겹치기(디버그 선 패스 신설) · S7 클라이언트 쪽 기록(서버 틱 번호 노출) · S8(EditorMultiWorld 선행), 파괴 쇼케이스 `destructionnet.scene.xml` 저장.
  - 정리: 묶음 3 · 4 · 7 · 8, Online 쪽 FunctorLambda · 긴 함수 5 개 · 주석, Core 나머지 폴더 주석(Math · Process · Delegate · Memory · Diagnostics · CommandLine · Container · Task · LogSink · String), Editor 구역 CommentRewrite 약 240 개, `std::remove_if` 게이트, 조용한 폴백 둘(위), GUI · Widgets 등 EditorModule 폴더의 층 경계를 빌드 형식으로(`listEditorModelDir` 방식), `RunCoreLayerGraph` 숨은 폴백 · `SessionGuard.py:33` 종료 코드 베낌(scriptsstruct 가 하는지 확인).
  - 문서: 후보 `OperationsAndDeployment` 외 보류 3 개는 풀지 않는다. `lighting` 이 거절당한 `Roadmap.md` 줄(클러스터드 · 간접 조명)을 직접 고친다. `NetKitMessageRange.h` 의 0x60 이 다른 키트와 겹치지 않는지 병합 때 본다.
- **C. 확인 대기 · 조건부 · 관찰**: 후보 문서 `## 확인 대기` 21 · `## 조건부` 18 · `## 관찰` 7 개는 측정 · 사용자 결정 · 외부 조건이 필요해 이번에 건드리지 않았다.

**panelclean**(묶음 6b: FunctorLambda Panels/Inspector 2 · LongFunctions Editor/Panels 27 · CommentRewrite Panels 약 25)은 띄워서 진행 중이다. **아직 못 띄운 것**: 이후 묶음 3(HeaderIncludeSlimming · EngineServiceInjection, 전 모듈 재빌드, GameObjectManager.h 는 enginepart 가 먼저) · 4(그래픽 긴 함수 + DiskPathVsResourceId, 경로 형식 결정이 먼저) · 7(시험→테스트) · 8(NamingAuditNext, 전역이라 마지막 단독). 보류: PreludeAndFwdCleanup(프렐류드 위치 미확인), NameReferenceAudit ③(`SW_DEV_COMMAND` 구조 결정), CarriedOverChecks 의 bindless 진입 구조 · 겹침 도구(사용자 확인). 동시 실행 한도는 20 명(`CLAUDE_CODE_MAX_CONCURRENT_SUBAGENTS`)이라 일곱 건이 거절됐고 지금은 위 둘만 남았다.

**병합 뒤에 해야 할 측정 · 검증 목록**(구현 중 모두 미뤘다):
- 빌드: Debug `all AllTests`(특히 `SW_SHARED_ASSET_CACHE` dllexport 명시 인스턴스화, `Macros.h` 의 `SW_API_EXTERN_TEMPLATE`, `ScenePrimitive` 리플렉션 사슬), Shipping 구성 컴파일(HC4), unity · Linux 빌드.
- 테스트: `GameFrameworkTest.*`(스냅샷 왕복, `GameSettingsUnknownElementIsALoadError`, `BootstrapGameSettingsIsBoundAndApplied`), `ModuleHostTest.ReloadBatchKeepsTheGameWhenItsStateCannotBeCaptured`, `ReflectionSerializationTest.*`(S1~S3, `ValueAddressTextDoesNotAddPropertyOffset` static_assert), `ObjectStateRoundTripTest.*`, `GPUScene*` · `TestRenderPassGPU` · `TestGPUTransparentSorter`, `TestAssetCacheRegistry` · `TestSpriteClipAsset` · `TestRagdoll` · `TestPairedAnimation` · `TestPoseModifier`, `PackEngineSettingsTest`, EditorTest 의 트랜잭션 · 플레이 세션 · 씬.
- 화면: 네 백엔드 투명 · 추가 뷰 · 모프 · VAT(batchview · oparity), 렌더러를 만진 도메인(lighting · vfx2 · gfxformat), 에디터 패널(previewwin · editorcommon). 셰이더를 바꾼 브랜치는 `App.exe --cook-shaders` 재베이크 후 낡은 바이너리 삭제. 씬 형식 판이 오른 wstream 은 씬 에셋 변환 실행.
- 측정: 씬 로드 · 스냅샷 복원 시간(S2 의 필드별 담기 비용), `tickCost*` · 큐브 20,000 개 `GT.Frame` p50 · p99 3~5 판 전후(enginepart), `exportCPUSnapshot` 복사 비용(batchview), BuildSpeed 빌드 시간, EncryptedTickCost, WorldStreaming W7 히치(Release 에서 혼자), AudioChannelLayout U1 Release 비교, 결정 기록 3절의 재측정 칸(스핀 · hot pool · `flatMapInProcesses`).
- 데이터: 개수가 가변인 기믹(`GimmickCircuit` · `GimmickNodeRegistry`)이 이제 최대 개수 초과 · 숫자 아닌 토큰을 오류로 막는다 — 기존 데이터가 로드되는지, 소프트 읽기가 객체째 실패하도록 바뀐 곳(읽다 만 소유 포인터 목록, XML 구조체 칸의 금지된 필드)이 기존 씬 · 세이브를 깨지 않는지.

**설계 결정과 이유**: 사용자 결정 대기였던 것은 권고안으로 갔다 — PairedAnimation P3b(기존 네트워크 킷 복제 재사용, 서버가 정확한 진입 시각을 보냄, 게임이 고른 역할의 `exitState` 는 지금 규칙 그대로), 키트 51 개는 견본으로 유지. Scripts 구조: 권고안 B(폴더는 옮기지 않고 import 루트를 `Scripts/` 하나로, 층 0 common · 1 린트 틀 · 2 명령 · 3 `__main__`, `CheckScriptLayers` 게이트, `common` 파사드 제거, 파일 목록 문서를 `--list` 생성으로) — 설계 문서는 세션 임시 폴더라 사라졌을 수 있고 `docs/plans/ScriptsStructure.md`(scriptsstruct 가 만들 계획 문서)가 정본이다. 별칭은 없앤다(사용자 지시) — `SW_SHARED_ASSET_CACHE` 로 해결했다.

**남은 작은 후보**(작업자가 범위 밖으로 둔 것): `ModuleHost` 외 ABI 의 크기 묻기 모양은 없음 확인. 편집용 경로 칸의 별도 형식(`InputMapPanel::_inputMapPath` · `UIPreviewPanel::_documentPath`), `EditorPreferences::_startupScene` · `ServerConfig` TLS 경로. `MaterialPreviewShading::parseFloats`(Editor, 표시용). 같은 모양의 Mesh · Texture · Material · Prefab · UITextAsset 캐시는 코드 근거로 합치지 않기로 했다(`SharedAssetCache.h` 머리 주석). `SessionGuard.py:33` 종료 코드 복사 · `RunCoreLayerGraph` 의 숨은 폴백은 scriptsstruct 가 다룬다.

## 1. 지금 상태 (2026-10-20, 밤 세션 끝)

- **main 은 한 번에 push 한다**(`full-build` 증거 있음, 병합 작업자 브랜치는 모두 들어감). 이번 세션 병합분: `kitsxml` · `hashutil` · `hashutil2`(FNV 단일 정본 `HashUtil`) · `hc1` · `hc2` · `hc3`(`PackEngineSettings`, `GameConfig` 는 `_packRoot` 만) · `dead`(죽은 인자 · 호출처 없는 선언 · `UnionFindUtil`) · `cifail`(애니메이션 의존 고리 레이스) · `respclock`(`IRespClock`) · `silentdef`(`parseFloat2/3/4` 정확한 개수, 재질 `value` 는 `readRequiredShaderValue` 1~4 개) · `textaddr` · `camdir` · `paired`(P3a 스냅샷) · `tcbench`(측정 하네스) · `warnfix`(컴파일러 경고 0, `SW_LOG_DISCARDED`) · 다른 세션의 `edcand`(패널 열기 요청 형식, 후보 `ScenarioWritesSourceTree`).
- **검증한 것**: Debug `full-build` 경고 0, `ctest -L "lint|nogpu|race"` 115/115, Shipping 빌드, Shipping `ctest -L hostgpu` 8 통과(QaGoldenImages 는 Shipping 에 벤치 옵션이 없어 건너뜀 — 골든 이미지 비교는 아직 한 번도 못 봄), Debug 네 백엔드 에디터 체인 종료 코드 0, `projectconfigedit` 네 백엔드 PASS(`packenginesettings.png` 네 장 확인), 게임 11 개 창 제목(Debug DX12).
- **이어서 확인한 것(같은 날 밤)**: 새로 지은 Shipping 전용 서버(테스트 8 통과, 기동 · 종료 0), Release `QaGoldenImages`(Empty 네 백엔드 통과 — 골든 이미지 비교를 처음 봄), Shooter3D 기본 FOV 80(Release DX12, HUD 85.2° = 세로 렌즈 75° 에 80 적용, `-gv_cameraFieldOfView=70` 이면 75.0°), 일시정지 수정(`pausefix`)을 NileCity 네 백엔드와 8 개 게임에서. **일시정지 결함**: Esc 를 2 프레임 이상 누르면 메뉴가 열린 다음 프레임에 닫혔다(`UIInputRouter` 가 소비 여부를 안 봄) — 고쳤다. 같은 모양이 `InputMap`(꺼진 층의 눌림 추적 없음)에 남을 수 있다.
- **남은 확인 한계**: 플로팅 창 캡처는 창이 별도 플랫폼 창(뷰포트)으로 떠서 합본에 든 경우까지는 보지 못했고 수정 전 코드와 대조하지 않았다. Shipping 은 `-game=` 을 무시하고 에디터 시나리오를 못 돌려 게임 선택이 필요한 검증은 Release(`SW_GAMES=ALL`)로 한다. 바인딩이 만들어진 프레임에 눌렸다 바로 뗀 단발 탭은 Held 로 시작해 다음 프레임 Pressed 가 안 읽힐 수 있다(홀드는 정확).
- **같은 날 밤 끝낸 결함 넷**: 일시정지 메뉴가 Esc 를 쥐면 바로 닫힘(`UIInputRouter` 소비 검사 → 이후 `InputMap` 이 꺼진 레이어 · 리바인딩 · 초기화 직후 눌림을 `_bLive` 하나의 규칙으로 Held 시작), OpenGL 에디터 캡처가 연 패스를 닫지 않아 플로팅 창이 한 번 더 그려짐(`EditorUICapture::renderJob`), 시나리오 `ExpectVariable` 스텝 신설(`fieldofview.scenario.xml` 로 Shooter3D 기본 FOV 80 단언). 콘솔이 쥔 키는 `InputManager` 의 captured 마스크가 이미 뗄 때까지 가린다(콘솔 경로는 결함이 아니었고 테스트 기대를 고쳤다). Release 로 일시정지 네 백엔드 · 8 개 게임, FOV 통과와 틀린 값 실패, 플로팅 창 캡처 네 백엔드, 에디터 체인 네 백엔드를 확인했다.
- **CI**: push 뒤 Windows Debug · Linux TSan 이 통과해야 GpuVerification V2 의 "연속 3 회" 를 센다. 정책 수정이 든 main 의 CI 를 세 번 읽은 뒤 `continue-on-error` 해제와 `softwareRenderSeconds` 수정.
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
- GPU 는 이 PC 에서 된다(RTX 3070 Ti, DX12). 못 한 것: Vfx V2 · V3, 패널 메뉴 토글 등 X 가 아닌 닫는 길(`panelgap` 이 다룸).
- NetSettingsRemaining 은 설정 데이터 형식을 새로 정해야 해서 DataDriven 과 함께 다룬다.
- PairedAnimation 남은 것: P3 본체(복제), 시나리오 테스트(활 · 클립 에셋 없음), M3 에디터.
- 야간 `Load (Linux | Release, nightly)` 근본 원인은 모른다. 근본 원인 후보(미해결): `DiskPathVsResourceId`.

## 2. 첫 일

1. 사용자 지침(2026-10-20): 되묻지 않고, 레이어가 변하지 않는 범위에서 상용 엔진과 우리 엔진이 발전할 방향을 기준으로 구조적 해결을 한다. 각 작업이 끝날 때마다 push 한다(`full-build` 증거 게이트). 구현을 먼저 하고(빌드 없이, 호출처 전수 grep) 병합한 뒤 `full-build` 한 번, 그다음 테스트 · GPU 검증. **GPU 검증은 Shipping `hostgpu` 가 기본이고 네 백엔드를 처음부터 한 번에** 돌린다(Debug 는 Debug 에서만 의미 있는 것만).
2. 이 세션의 push 와 CI 통과를 확인하고 워크트리를 지운다(위 1절).
3. **다음 일**: `StructureSimplification` HC4(`ConfigCatalog.py` · `docs/Config/*` · `Config/README.md` 의 `ensureConfig` 문장, `Scripts/common/Config.py` 가 `{}` 를 돌려주는 곳 W5) → PK → AT → TX(진행 3/21). 각 단위 전에 위 결정 대기 PK3 · TX2 권고를 알린다.
   병렬로 띄울 묶음(`[병렬 가능]`, 파일 겹침 없이, 동시 빌드 최대 2): (가) 1.1 "그다음(중간)" — `GPUSceneSnapshot.h` 배치 목록 3 벌 복사, `SharedAssetCache` 템플릿, `captureSnapshot` 직렬화 두 번 돌기. (나) 위 1절 "그 밖의 남은 작은 것"과 TextAddress 후속 `void*`. (다) Roadmap 의 `[병렬 가능]` — Vfx V2 · V3(`gpu-verifier` 와), 카메라 디렉터, 애니메이션 · PairedAnimation P3b(결정 뒤), SerializerSilentSkips, AsyncResourceLoading A2(설계 검토 뒤).
4. AudioChannelLayout U1 Release 비교는 PC 를 혼자 쓸 때(TypedComponents 는 2026-10-20 에 끝남).

## 3. 새 세션 시작 절차

1. 이 문서만 먼저 읽는다. 일을 정하면 그 일의 계획 문서를 `py -3 -m Scripts plans --file <이름>` 으로 읽고, [작업 방식](../11_Workflow.md) 은 필요한 절(빌드 2절 · 검증 3절 · push 4절)만 읽는다. 에이전트를 띄울 때만 [에이전트와 토큰](../12_Agents.md) 과 [작업자 규칙](WorkerRules.md). C++ 을 쓰기 전에 [AGENTS.md](../../AGENTS.md) 목차에서 일에 맞는 파일만.
2. `git worktree list` 로 남은 워크트리를 보고 병합이 끝난 것을 지운다.
3. 큰 일은 사용자와 순서를 확인한 뒤 시작한다. 시작 전에 graphify 그래프를 갱신한다(`graphify update Source`, 약 4 분).
