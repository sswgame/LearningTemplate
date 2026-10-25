# 다음 세션 인계

> 질문: 지금 어디까지 왔고, 다음 세션이 처음에 할 일은 무엇인가?
> 독자: 새로 시작하는 세션(에이전트와 사람).
> 결론: main 은 통합 I(브랜치 27 개)에 이어 통합 J(결함 수정 `fixa`~`fixh` 8 개 + 통합이 찾아 고친 결함 넷 — 에디터가 기동하자마자 멈추던 것 둘 포함)와 통합 K(CI 실행 40147477351 의 실패 둘 — `cifixui` · `cifixcook`)까지 검증했고 **push 하지 않았다**(사용자 확인 대기). 남은 것은 push 뒤 CI(리눅스 쿠킹 수정은 CI 만 확인한다), GPU 확인(`## 000000` 목록 + 통합 I 의 "실행해서 봐야 하는 것"), 그다음 아키텍처 이음매([ArchitectureSeams](ArchitectureSeams.md))다 — 0 단계(측정 기준선)는 끝났다(`## 00000000000`).

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

## 00000000000. 2026-10-25 아키텍처 이음매 0 단계 — 빌드 · 기동 기준선을 다시 쟀다(문서만, push 안 함) (이것부터 읽는다)

**상태**: main `eb2a1fa59` 를 새 워크트리 `arch0` 에서 빌드 칸 독점으로 쟀다(워크트리 · 브랜치는 지웠다). 표는 [08 검증](../08_Verification.md) "아키텍처 이음매 0 단계 기준선". Debug 콜드 풀 빌드 266.5 s(Lean 236.3 s), `.cpp` 증분 2.4 ~ 13.9 s, `FrameRenderer.h` 증분 18.5 s · 27 TU, Dev 기동 중앙값 5.3 s(`ModuleTypes` 2.1 s).

**판정**: DLL 링크가 `.cpp` 증분의 29 ~ 61 %(Engine `.cpp` 52 ~ 61 %)로 30 % 기준을 넘는다. 그러나 5 단계(`Engine_Render` OBJECT)는 같은 `Engine.dll` 에 링크돼 링크를 줄이지 못하므로 링크 이유로는 열지 않고, 새 5a(Debug `-gcodeview-ghash` + `/DEBUG:GHASH` A/B)를 먼저 한다. 6 단계는 팬아웃 27 TU 중 16 개가 `FrameRenderer*.cpp` 자신이라 빌드 이득이 작다 — 병합 충돌 이유로만, 1 ~ 4 단계 뒤에.

**남은 일**: 5a 측정(통합 혼자, CMake 옵션 두 줄 — 통합 L 병합 뒤 조용할 때). ArchitectureSeams 3.3 에 새 후보 둘 — Core `.cpp` 하나가 `ReflectionParser.exe` 재링크로 코드젠 64 개를 다시 돌리는 것(13.9 s), 기동의 `ModuleTypes` 2.1 s 나눠 재기.

## 0000000000. 2026-10-25 통합 L 대기 — 병합 안 된 브랜치 24 개와 도는 에이전트 (위 절 다음에 읽는다)

**상태**: main 은 `eb2a1fa59`(통합 K)까지 push 했다. CI 실행 `40160506131` 은 끝나지 않았다 — `py -3 -m Scripts ci-jobs --summary` 로 먼저 본다(Linux 쪽 `CookJobTest.RegistryCookFollowsTheMetaListing` · `SceneCookTest.*` · Wayland `UIDeterminismTest.*` 가 통합 K 의 Linux 수정 확인 대상). 아래 브랜치는 전부 **병합 · 빌드 · 테스트 안 했다**(린트만). 워크트리 `D:/w/<이름>` 은 병합 전이라 지우지 않는다.

**병합 순서(위에 얹힌 사슬을 먼저)**
- 문서 분할: `arch1x`(`docs/09_Decisions.md` → `docs/decisions/5-N_*.md`, `StatusBoard.md` 생성 파일, `integ-gate`). **가장 먼저 병합**하고 이후 브랜치의 결정 문구(`fixn` 의 불리언 인자 규칙, `cachec` 의 IAssetCache 결정, `arch2a` 의 EditorCore + Model 폴더 규칙, `fixl`/`fixr` 의 결정)를 영역 파일에 옮긴다. 병합 뒤 `status-board --write`.
- 테스트 소스 목록: `arch2c` → `fixl` → `fixn`(= fixl + dup31 병합 + 검토 권고). `dup31` 은 `fixn` 에 이미 병합돼 있다.
- 에디터 코어: `arch2a`(2a + 2b) → `fixp`(EngineTest 가 EditorCore 링크, gen stem 중복 FATAL, 테스트 미러 33 개 이동). `fixi`(에디터 후보), `fixk`(게이트) → `fixq`(곡선 · 셀 · 프리팹 O(N²)).
- 게임플레이 데이터: `fixj` → `fixo` → `fixt`(끝남 · `a9d3c6cb4`; `SkirmishDirectorComponent` 에 PROPERTY `_unitModelPath` 가 늘어 **리플렉션 재생성**, `unitmodels.xml` 신설, 시나리오 skirmish · frontier · shooter arena 로 시작 오류 로그 확인 필요).
- 정적 상태 · 설정 소유: `arch3`(끝남 · 커밋 5 개; `EngineConfig` → `ConfigManager` 소유, `GameConfig` 정적 사본 삭제, `Render2DSettings` → 새 `Render2DSettingsSubsystem`; 서비스 표 `EngineServiceList.xxx` 에 행 셋이 늘어 엔진 ABI 도장이 바뀌고 CMake 재구성 필요. 서비스 미바인딩 구간(`ScopedUnboundEngineServices`: `TestScene` 1815 · `TestMaterial` 1368 · `TestPackLayoutStability` 141)에서 읽으면 단언으로 멈춤 — 통합에서 확인. 렌더러 초기화의 `reloadActive()` 를 뺐으므로 **백엔드 교체 시나리오**에서 `render2d.xml` 이 낡지 않는지 확인. 다음 커밋 단위는 `ArchitectureSeams.md` 6절. 충돌: `CheckMutableStatics.toml`, `CheckServiceAccess` 기준선).
- `fixv`(끝남 · `3cf9f643d`, base `fixq`: 주석 4 곳 + 기준선 표만, 소켓 · 모션 워핑 · 로코모션 워핑이 위 유닛 최종 포즈와 어긋나지 않음을 표로 확인). `fixw`(끝남 · `3899ceb59`, base `fixr`: `TaskHandleTracker::waitIdle` 이 메인이면 메인 큐를 돕고, `TaskManager::shutdown` 은 정지 신호 뒤 10초 안에 워커가 안 나오면 이름 로그 + `std::abort()`, `describeFirstUnfinished` 는 `tryRetain` 으로 경쟁 제거, `JoltJobSystem` 소멸자와 `AsyncFileIO` 의 영구 대기를 30초 제한 + abort 로. **abort 정책이 느린 콜백이나 TSan 에서 오탐으로 터지는지는 통합 `nogpu` · `race` 로 확인**). **미수정 결함**: `RenderPipelineAsset::loadFromXMLFileAsync` · `RenderPassAsset::loadFromXMLFileAsync` 가 raw `this` 를 태스크에 넘겨 호출자가 핸들을 기다리지 않고 객체를 먼저 지우면 use-after-free(Material 은 공유 `AsyncLoadState` 로 막음) — 다음 세션 우선 후보. `AsyncFileIO::waitIdle` 을 워커 안에서 부르면 워커 1개일 때 교착.
- `fixu`(끝남 · `2b8bb3c65`, base `packed`: `readNumberList` 가 남는 칸 · 모자란 칸을 거절하고 임시 배열에 읽은 뒤에만 씀, `readSource` 의 NaN/Inf 정수 캐스트 제거, `ReflectBuiltinClass` + `getReflectBuiltinClass<T>` 가 숫자 분류의 유일한 정의, Transient 아닌 압축 타입 PROPERTY 는 고정 코덱 생성기가 오류. **파서 도구 재빌드 + 리플렉션 재생성 필요, 파서는 테스트 없음**. 동작 변화: `ReflectValue` 가 NaN/Inf 를 다른 숫자 타입으로 못 바꿈, `isNumericTypeName` 이 더 엄격(문자열 blob → TagID 글 파싱 경로 닫힘). 후보: `UIAnimatedProperty` · `ReflectionInvoke` · `PropertyRoleUtil` 의 `kint8..kfloat64` 위치 가정 → `getBuiltinClass` 로).
- 도는 중이던 후속(결과는 `git log main..wt/<이름>` 으로 확인): `fixx`(끝남 · `227db4392`, base `fixp`: gen stem 전역 검사 `SW_REFLECT_GEN_STEM_<stem>` · SmokeTest/EditorUITest 직접 컴파일 목록을 `SW_APP_SMOKE_TEST_SOURCES`/`SW_EDITOR_UI_TEST_SOURCES` 선언으로. **`fixl` 과 `Test/EditorUITest/CMakeLists.txt` · `Test/SmokeTest/CMakeLists.txt` 가 충돌** — fixx 쪽을 택하고 fixl 의 Platform glob 은 버린다. 첫 구성의 가장 큰 위험: EngineTest 가 Engine `/FI` · 자기 FlagOps · EditorCore FlagOps 세 우산을 받아 같은 enum 트레이트를 두 번 정의하지 않는지 확인).
- 태스크 대기: `fixm` → `fixr`.
- PackedTypes: `packed`(P3 끝남 · 커밋 `253fde28b`; P4 법선 RG16 은 설계만 — 알파 "그려졌다" 표시를 머티리얼 칸 알파로 옮겨야 하고 `R16G16_SNORM` 렌더 타깃 지원은 네 백엔드에서 확인 필요, GPU 검증과 같이). 충돌: `PackedTypes.md` · `Roadmap.md` · `VerifyBatch.json` 의 `packed-types-test`. 같은 결함 후보: `readNumberList` 가 남는 칸을 안 봄(`float3` 가 `1,2,3,4` 를 받음), `readSource` 의 `static_cast<int64>(NaN/Inf)`(지금 고칠 만함).
- 캐시: `cachec` → `fixs`(끝남 · `ad3d7c5a5`, 29 개 파일; `ResourcePaths::makeAbsolutePath` 삭제 → 읽기 `findLooseFile`, 쓰기 `resolveWritePath`; 동작 변화: `..` id 는 빈 경로; `FileWatchDispatcher::update` 가 사건마다 `clearPathCache()`; `FileUtil` 에 삭제 에포크 `getRemovalEpoch` 신설; **`FileUtil.h` · `ResourcePaths.h` 변경 → Engine 이상 전체 재빌드**; Linux 전용 테스트 `WritePathKeepsTheDiskSpellingOfExistingFolders` 는 Linux CI 에서만 확인). 후보: `ResourcePakReader::_mapEntry` · `GPUMaterialRegistry` · `RenderPSOCache::_mapMaterialPSO` · `GPUSceneBuilder::_mapShaderRepresentative` · `ShaderBindingLayout::_mapKeyToIndex` 의 uint64 해시 단독 키(충돌하면 틀린 파일 · 퍼뮤테이션 · PSO), `makeSavePath*` 의 Linux 소문자 폴더. (빈 stdin `python -` PID 2176 은 사용자가 허락해 종료했다.)
- 애니메이션: `anim1` → `anim2`(끝남 · `4c5ac11c2`; U3 성분 단위 평가 — 전체 대기 2 번, 성분 안 유닛 먼저, 정합 테스트 둘은 코드 추적상 통과 예상; U4 레벨 API 삭제 · `hasCycle()` 이동; 단계별 스코프 → `phasePose` + `phase<단계>CpuNanos` 카운터. 게임 스레드 순회가 레벨 순 → 성분 순으로 바뀜. **옛 전역 장벽이 없어져 선언 없는 교차 유닛 읽기는 레이스 — `race`(TSan) 필수 확인**. 컴파일 위험: `PoseJob::kArrPosePhase` static constexpr, 생성자 초기화 순서. 측정 질문은 `AnimationComponentChain.md` 4 절: K=0·2·4 대조군 벤치, 무기 L=3. 낡은 서술: `PairedAnimation.md` "의존 레벨", `BenchScene` 변수 설명 → `GlobalVariables.md` 재생성).
- 건드린 파일이 겹쳐 충돌 예상: `Scripts/lint/rules/CheckMutableStatics.toml`(`fixi` · `cachec` · `fixp` · `arch3` — 병합 뒤 `--regen-baseline`), `cmake/Engine/TestTargets.cmake` · `UnbuiltSources.cmake`(`fixl` · `arch2c`), `Test/EngineTest/CMakeLists.txt`(`arch2c` · `fixl` · `fixp`), `Source/Editor/CMakeLists.txt`(`arch2a` · `fixp`), `README.md`/`StatusBoard.md`.
- **병합하지 않는다**: `rhiasync`(옛 동기 가상 함수 3 개를 지워 B0 번들 전까지 컴파일 안 됨) — `rhiasync2`(계약 수정 + B0 골격, 진행 중)와 함께 GPU 검증 담당(`gpu-verifier`)과 같이 다룬다. `arch3`(정적 상태 · 설정 소유, 진행 중 · 커밋 3 + 작업 파일 15)은 끝났는지 `git log main..wt/<이름>` 과 `git -C D:/w/<이름> status` 로 확인한다.

**첫 일(순서)**: ① `ci-jobs --summary`. ② 끝난 브랜치를 위 순서로 `integrator` 한 명이 병합 → `-k 0` 빌드(컴파일 위험은 각 브랜치 보고에 있다: 새 IAssetCache 생성자 → 전체 재빌드, `sw_linkEditorCore` 의 FlagOps 충돌, `std::erase_if`/`unordered_map` 해시, 리플렉션 재생성(`ShooterPlayerComponent`)) → Debug · Shipping · Shipping-Server `nogpu`/`lint`/`race` → **에디터 실제 기동 확인**(교훈 ⑪, 통합 J 의 규칙) → 기동 시나리오 네 백엔드. ③ 안 끝난 에이전트의 결과 수집. ④ `gpu-verifier`: `## 000000` 목록 + `hostgpu` 격리 H1~H4 + RHI B0. ⑤ [ArchitectureSeams](ArchitectureSeams.md) 5a(GHASH 링크 A/B) — 0 단계 판정은 위 절.

**알려진 불확실**: 애니메이션 정합 테스트 둘(`FollowerReadsLeaderFinalPoseAfterAttachmentPhase`, `PoseModifierTest.FollowerReadsLeaderPoseAfterRig`)은 `anim1` 만 병합하면 빨갛다(결함 재현 테스트) — `anim2`(U3)와 함께 병합한다. `fixn` 의 `overlay.show <바> 2` 거절, `profile.trace 1|0|true|false` 수용은 동작 변화다. `fixj` 의 `SceneAudio._emitter` 는 순회 순서가 보존되지 않는다. `fixr` 의 `describeFirstUnfinished` 는 진단 근사값이라 TSan 에서 걸릴 수 있다. `IRHIDevice.cpp:317` 환경변수 `equals(pSoftwareEnv, "1")` 손 비교는 `rhiasync2` 가 끝나면 `tryParseBool` 로.

**아직 고치지 않은 같은 결함 후보(소유 밖이거나 막 나온 것)**: `SkeletalAnimatorComponent.cpp:778` `_listRootMotionModifier` contains+push_back, `SocketBindingComponent`/`MotionWarpingComponent`/`LocomotionWarpingComponent` 가 위 유닛의 최종 포즈를 읽는 계약(U3 뒤 검증), `stem` 중복 검사를 `sw_addReflectionStep` 전역으로, 테스트 실행 파일이 소스를 직접 컴파일하는 곳(`SmokeTest` 의 `EditorModuleHost.cpp`, `EditorUITest` Backend 4 개), `whenAny` 숨은 간선((가) 안은 `SmallTaskThroughput` 으로 분기 비용을 먼저 잰다), `TextMeasureCache` 문맥 해시 2^-64 잔여, `GPUUploadQueue`·`AsyncFileIO`·`JoltJobSystem`·`NavTileRebaker`·`MaterialXML`·`RenderPipelineAsset`·`RenderPassAsset` 의 자체 대기 경로(읽지 않음), `EditorViewportPreview::applyMaterial` 의 `ensureMeta` 동기 비용(측정 안 함), 3.2 래퍼 걷기(범위가 넓어 병합 뒤 다음 묶음), `hostgpu` 격리 H1~H4.

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

**아직 안 한 것(후보, 순서)**: 애니메이션 유닛 사슬(`AnimationSystem` 레벨 5L 번 전체 대기를 의존으로 — 유닛 단위가 아니라 레벨 청크 단위로 비교 설계; `09_Decisions.md` 103 행과 충돌 주의, 먼저 측정), `TaskManager` 의존 API 의 프로덕션 첫 사용처(`taskdeps` 가 사이클 검출을 채움), 모델 한 개 안 LOD · 클립 병렬(이득 작음), Detour 군중(측정 먼저: `GT.Scene.tick.navigation`), G 버퍼 법선 RGBA16F → RG16 옥타헤드럴(`RHIFormat` 추가 · 셰이더 재쿠킹), 메시 정점 64 → 28 B(`.mesh` 버전 4, `PreloadAndLod` 와), 캐시 계약(`IAssetCache` 계측, `ResourcePaths` 음성 결과 · 세대, 측정 캐시 전체 비우기 → LRU), RHI 업로드 비동기(`RhiAsyncUploads.md` 의 단위), `hostgpu` 격리 H1~H4(`HostGpuIsolation.md`), 락 쥔 채 로그 금지 규칙, 순서가 주석에만 있는 목록 3종(`SceneFrameStepList` · `SW_SCENE_SERVICE` · 컴포넌트 틱 그룹), `RunLintSuite` · `ci-jobs` 의 `ConflictSchedule` 적용, `Test/` 의 static 카운터 경쟁 후보, 같은 정규식 모양 3곳(`RunForwardDeclarationCandidates.py` 170, `CheckServiceAccess.py` 165, `FileScan.py` 121 — 1 s 미만이라 둠). `PackedTypes` · `RhiAsyncUploads` · `HostGpuIsolation` 은 [Roadmap](Roadmap.md) 11 에 넣었다. 컴파일 캐시는 sccache 를 PCH 때문에 기각했으니 다시 제안하지 않는다(`bd5b28125` 계열 기록).

**그다음** [ArchitectureSeams](ArchitectureSeams.md) 0 단계(측정, 통합 혼자)와 1a · 1b · 2a · 3 · 4.

**끝난 작업자가 남긴 마무리(결함이거나 약속한 것 — 빼먹지 않는다)**
- **실행해서 봐야 하는 것**: `--jobs 2` 에서 `nogpu` · `race` 겹침의 시간 제한(`verify-batch`), `containers3` 의 1 만 개 테스트 시간, `packedtypes` 의 옥타헤드럴 오차 상한 1e-4(Debug `nogpu` 는 통과), 썸네일 · 반사 프로브 · 핫 리로드가 UI 스레드에서만 적용되는지(자동화 시나리오에 `WaitUntil` 탐침), `logdeadlock` 의 `FileLogOutput::open` 이 폴더 생성 실패에서 false 를 돌려 `LoggerSubsystem` 쪽 영향이 없는지.
- **속도 실측이 아직 없는 것**: 임포트 전체(텍스처 + 모델 약 1,255 개, `importpar` 전후), 참조 색인 스캔(`refindexpar`, Debug 6~19 s → ?), 시동 단계별(`Startup phases:` 로그), `GT.Material.waitIdle` 등 대기 스코프 크기, 캐시 적중률(`[CacheStats]` 줄 — 측정 창 끝에 찍힌다). 이 숫자가 있어야 위 후보(애니메이션 사슬, RHI 비동기, Detour, 캐시 계약)의 순서를 정한다.
- **확인 못 한 안전 가정**: 썸네일 로드 중 디바이스가 바뀔 때 `Texture2D::releaseRHI`, 반사 프로브 베이크의 캡처 카메라가 편집 카운터를 올려 다음 프레임에 취소시키는지.
- **`taskdeps` · `containers1` · `halfunify` · `hotreloadq`**: 자식 → 부모 완료 기다림은 사이클 검사가 못 본다(주석, `whenAny` 는 후보 `WhenAnyHiddenEdge`). `AnimationOrderedList::getItems` 가 접기를 해서 게임 스레드 전용(`GPUSceneBuilder::populateFromScene` 스레드 미확인). `halfunify`: HLSL f16 해독 안 봄. `hotreloadq`: 한 잡 안 원본은 순차(체크아웃으로 모델 다수가 바뀌면 전보다 느림), 규칙 파일만 바뀐 다른 원본은 자동 재임포트 안 함, 핫 리로드 텍스처의 밴드 압축은 이제 그 워커에서 직렬이다(통합 ②).
- **구조 후속**: 에디터의 다른 UI 스레드 동기 `acquire` 점검(`AssetManager` 사용처), `ConfigHotReload.cpp` 의 동기 reload, `TextureCache::resizeRenderTarget`(`RT` 에서 `waitIdle`, `RhiAsyncUploads` U13), `erase` 71 건(`CheckIndexLoopErase` 기준선) · `contains` 뒤 `push_back` 72 건(`CheckContainsPushBack` 기준선, `EditorThumbnailCache` 가 진짜 후보), `setMembership` 의 `findBatchMembership`, 쓰기 I/O 를 `ThreadedLocalStore` 로 거치는 규칙의 기존 호출처 감사, 락 쥔 채 로그 호출 규칙의 나머지 호출처(`logdeadlock` 보고 목록), `CookRecord::make` 의 출력 재읽기 해시.
- **hostgpu 격리**: `HostGpuIsolation.md` 의 H1(쓰기 위치 스냅샷 점검)은 앱을 돌려야 해서 통합이 H1 → H2 → H3 → H4 순서로 한다. 풀기 전에 `AppBin` 잠금이 지키는 위치(`Resource/scratch/<이름>`, `Saved/Automation/<시나리오>/`, 시나리오 리터럴 `Saved/…` 35 곳, `-EnableEditor` 스모크의 `Saved/Editor`)를 격리한다.
- **GPU 에서만 보이는 것**: `hostgpu` 로만 도는 테스트에 남았을 `countContaining` 치환 오류, 병렬 쿠킹 바이너리가 직렬과 같은지(`--verify-shaders` 는 통과), 두 GPU 칸 동시 사용의 타이밍 · 골든(`HostGpuIsolation.md` 7절).

**다음 세션의 첫 일과 병렬로 띄울 묶음**
- 첫 일(순서): ① 사용자가 허락하면 push 뒤 `py -3 -m Scripts ci-jobs --summary`. ② `gpu-verifier` 로 아래 `## 000000` 절의 GPU 확인 + 위 "실행해서 봐야 하는 것" 중 GPU 가 필요한 것(네 백엔드 × Empty · 에디터 · Shooter3D · NileCity, 자동화 시나리오). ③ [ArchitectureSeams](ArchitectureSeams.md) 0 단계.
- 병렬로 띄울 묶음(파일 소유가 겹치지 않을 때, 작업자 여섯 안팎 — 빌드하지 않고 커밋만): A. 에디터 범위 밖 · 워커에서 에디터 서비스를 찾는 남은 곳 점검(통합 J ③ · ④ 의 같은 모양). B. `hostgpu` 격리 H1~H4(실행이 필요해 통합 · gpu-verifier 와 같이). C. `RhiAsyncUploads.md` 의 U1~U14 중 GPU 없이 되는 것. D. `PackedTypes.md` P3(리플렉션 접점) · P4(G 버퍼 법선 — 재쿠킹 · GPU 확인 동반). E. 애니메이션 유닛 사슬 설계 비교(먼저 측정). F. 캐시 계약(`ResourcePaths` 음성 결과 · 세대). G. 측정대기 계획 문서 59 개의 판정(GPU 확인 뒤).

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
