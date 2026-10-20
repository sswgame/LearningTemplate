# 로드맵 — 앞으로 할 일의 순서

이 문서는 앞으로 할 일의 **순서의 정본**입니다. 일의 식별자는 이름입니다 — 계획 문서의 파일 이름, 또는 `Candidates-<영역>.md` 의 `### 이름 — 제목` 절입니다. 옛 번호(`C-30` 등)는 `py -3 -m Scripts plans --find <옛번호>` 가 새 이름을 찾아 줍니다. 이 문서는 순서만 가지고, 항목 설명을 복사하지 않으며 링크로만 가리킵니다.
끝난 단위는 이 목록에서 지웁니다. 작업 전에 항목의 현재 완성도를 코드로 확인합니다. 같은 파일을 건드리는 단위는 같은 작업자에게 묶고, 동시 빌드는 둘까지입니다([작업 방식](../11_Workflow.md)). GPU 확인이 필요한 단위는 구현만 하고 `gpu-verifier` 로 캡처합니다.
항목 끝의 `[병렬 가능]` 은 다른 작업자와 같은 시간대에 병합해도 건드리는 파일이 겹치지 않는다는 뜻이고, `[단독: 이유]` 는 혼자 하는 시간대가 필요하다는 뜻입니다. 근거는 그 일이 건드리는 파일 영역입니다.

## 지금 순서

1. **`integrator` 설계의 미구현 단위**(설계는 끝났고 코드가 없음).
   - 데이터화: D6 → D7 → D8 → D9, 이어서 P1 · P2 → P4(D9, P1 뒤), G1 · G2 — [DataDriven](DataDriven.md) 7.5. [단독: 기본값 · 로더 형식을 바꿔 여러 모듈이 다시 빌드됨]
   - E3(E4 가 뒤따름) — [EnginePartition](EnginePartition.md) 6.4. `GameObjectManager.h` 를 바꿔 모든 모듈이 다시 빌드된다. [단독: 모든 모듈 재빌드]
   - V2 — [GpuVerification](GpuVerification.md) 2절. 지는 케이스 수정은 들어갔고(로컬 확인, CI 미확인) 리눅스 `gl` 원인 확인 뒤 해제. [병렬 가능]
2. **새 시스템**:
   - Vfx V4 → V5(V0 ~ V3 · V6 · V7 들어감) — [Vfx](Vfx.md). [병렬 가능]
   - 클러스터드 라이팅 S2 → S5 — [ClusteredLighting](ClusteredLighting.md). [단독: 셰이더 재베이크]
   - 간접 조명 P0 부터(ClusteredLighting 뒤) — [IndirectLighting](IndirectLighting.md). [단독: 셰이더 재베이크]
   - 월드 스트리밍 W2 → W7 — [WorldStreaming](WorldStreaming.md) (W1 은 들어갔고 W2 부터). [단독: W2 가 씬 파일 형식 판을 올려 씬 직렬화를 쓰는 모듈이 다시 빌드되고, W7 의 히치 측정은 Release 에서 혼자]
3. **기능 묶음**(분류는 결정 기록 5-12, 한 묶음에 단위가 여럿):
   1. 카메라 디렉터 · 직교 리그 [CameraRendererIntegration](CameraRendererIntegration.md)(S2 피사계 심도, S3 · S4 는 렌더러 기능 선행) → [GamepadVirtualCursor](GamepadVirtualCursor.md)(G2 게임 바인딩 정하기 · G3 실기), 에디터 시나리오 [GraphSaveScenarioRun](Candidates-editor.md#graphsavescenariorun) · [ScenarioRecorderAndInspectorGaps](Candidates-editor.md#scenariorecorderandinspectorgaps) (옛 `D4-5` → `D4-3` → `D3-5` · `D3-8` 에 대응). [병렬 가능]
   2. 미리보기 창 · 스카이 — [EditorMultiWorld](EditorMultiWorld.md) 6절 (3) 스카이(엔진에 스카이 컴포넌트가 먼저 필요), (4) 풀 공유 · (5) 내리기 안전 · (6) 백엔드 교체(같은 파일은 한 작업자). [병렬 가능]
   3. 지형 · 식생 [TerrainWaterVegetationRemainders](Candidates-graphics.md#terrainwatervegetationremainders)(지형 LOD 교체 프레임 시간 → 지형 쓰기 남은 것 → 식생), 서버 파괴 호스트 [DestructionNetSessionScene](Candidates-network.md#destructionnetsessionscene) → [EncryptedTickCost](Candidates-network.md#encryptedtickcost). [단독: 지형 · 식생이 셰이더 재베이크를 부르고 측정은 Release 에서 혼자]
   4. 오디오 [AudioEngineExtensions](Candidates-audio-ai-localization.md#audioengineextensions), 온라인 키트 [OnlineAccountProviders](Candidates-network.md#onlineaccountproviders) · [OnlineChatModeration](Candidates-network.md#onlinechatmoderation) · [OnlineServerDirectoryAdmin](Candidates-network.md#onlineserverdirectoryadmin) · [OnlineFriendsGuild](Candidates-network.md#onlinefriendsguild) · [OnlineLeaderboardExtras](Candidates-network.md#onlineleaderboardextras) · [OnlineMatchmakingIntegration](Candidates-network.md#onlinematchmakingintegration) · [OnlineLiveOps](Candidates-network.md#onlineliveops) · [OnlineEconomyReceipts](Candidates-network.md#onlineeconomyreceipts) · [OnlineEconomyItems](Candidates-network.md#onlineeconomyitems)(옛 `D7-5.1 → 5.10 → 5.3 → 5.6` 의 하위 번호는 백로그에 정의된 적이 없어 절과의 대응을 알 수 없다. 이 순서에 얽매이지 않고 절의 선행 관계로 고른다). [병렬 가능]
   5. 애니메이션 — [PairedAnimation](PairedAnimation.md) · [RigEditing](Candidates-gameplay.md#rigediting) · [AnimationLodAndCrowd](Candidates-gameplay.md#animationlodandcrowd), 천 동적 메시 [ClothFollowups](Candidates-engine.md#clothfollowups), Shooter3D [ShooterCharacterIntegration](Candidates-gameplay.md#shootercharacterintegration), `GameNetSession` [OnlineNetworkBaseRename](Candidates-network.md#onlinenetworkbaserename) + [ServerDebug](ServerDebug.md)(한 작업자). [병렬 가능]
   6. 그래픽스 형식 묶음(한 작업자, 순서대로, 셰이더 재베이크 · 에셋 재작성): [ToonShadingCompletion](Candidates-graphics.md#toonshadingcompletion)(디퍼드의 계단 셰이딩) → [ViewModeCompletion](Candidates-graphics.md#viewmodecompletion). 머티리얼 텍스처 칸 16개는 들어갔다(`TerrainWaterVegetationRemainders` 의 함정). [단독: 셰이더 재베이크 · 에셋 재작성]
   7. 뒤 순서: 서버 렌더러 제거 [DedicatedServerNoRenderer](Candidates-network.md#dedicatedservernorenderer), 워프 분리 [MeshModifierStack](Candidates-editor.md#meshmodifierstack)(워프 내보내기는 `4174d9b8c` · `c82a29a77` 로 들어갔고 절에는 셰이딩 미리보기 비용 등 다른 남은 것만 있다). [병렬 가능]
4. **파일이 겹쳐 미룬 정리**: [GameFrameworkStructureCleanup](Candidates-engine.md#gameframeworkstructurecleanup) · [HeaderIncludeSlimming](Candidates-engine.md#headerincludeslimming) · [PreludeAndFwdCleanup](Candidates-engine.md#preludeandfwdcleanup) · [CommentRewriteRemaining](Candidates-project.md#commentrewriteremaining) · [NamingAuditNext](Candidates-project.md#namingauditnext) · [LongFunctions](LongFunctions.md) — 위 1~2 뒤에. 앞서 병합된 [EngineServiceInjection](Candidates-engine.md#engineserviceinjection) · [NameReferenceAudit](Candidates-engine.md#namereferenceaudit) 는 남은 조각이 있다 — EngineServiceInjection 은 `SceneNavigationCooker` · `PortraitRenderer` 가 아직 인자 하나짜리 `Scene` 생성자를 쓰고, NameReferenceAudit 은 린트로 더 풀 것이 없어 `SW_DEV_COMMAND` 같은 선언 등록 형식의 결정이 먼저다. [단독: 헤더 · include 를 바꿔 여러 모듈이 다시 빌드됨]
5. **새로 쓴 정리**: [SerializerSilentSkips](Candidates-engine.md#serializersilentskips) · [WorkspaceImGuiSplit](Candidates-engine.md#workspaceimguisplit) · [FunctorLambdaCommonization](Candidates-engine.md#functorlambdacommonization) · [NameLookupAndWatchMask](Candidates-engine.md#namelookupandwatchmask) · [DiskPathVsResourceId](Candidates-engine.md#diskpathvsresourceid) · [NetSettingsRemaining](Candidates-engine.md#netsettingsremaining) · [InternalErasureRemaining](Candidates-engine.md#internalerasureremaining) · [InvalidMeasurementCleanup](Candidates-project.md#invalidmeasurementcleanup) · [DecisionMeasurementColumn](Candidates-project.md#decisionmeasurementcolumn) · [CarriedOverChecks](Candidates-project.md#carriedoverchecks) — 순서는 정리 → 기능. [병렬 가능]
6. **구조 단순화 네 정책**: 필수 설정(HC) → 팩 쿠킹을 C++ 로(PK) → 에셋 종류 등록 줄 하나(AT) → 텍스트 형식 역할별 하나(TX) — [StructureSimplification](StructureSimplification.md). DataDriven D8 · D9 와 겹치는 지점은 그 문서 2절. [단독: AT1 은 `AssetFormat.h`, HC3 은 `GameConfig.h` 를 바꿔 여러 모듈이 다시 빌드됨]
7. **빌드 속도 12 단계**(리플렉션 헤더 팬아웃) — [BuildSpeed](BuildSpeed.md). [단독: 빌드가 도는 동안은 측정이 무효라 다른 작업이 모두 끝난 뒤 혼자]

## 지금 하지 않는 것

조건부 · 보류 · 관찰 항목과 조건이 붙은 기능은 영역별 후보 문서의 `## 조건부` / `## 보류` / `## 관찰` 절에 있고 순서에 올리지 않는다. 조건이 오면 해당 후보 문서에서 골라 온다: [engine](Candidates-engine.md) · [graphics](Candidates-graphics.md) · [gameplay](Candidates-gameplay.md) · [editor](Candidates-editor.md) · [network](Candidates-network.md) · [ui](Candidates-ui.md) · [quality](Candidates-quality.md) · [project](Candidates-project.md) · [audio-ai-localization](Candidates-audio-ai-localization.md). 끝난 단위는 이 목록에서 지운다.
