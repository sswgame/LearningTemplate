# 로드맵 — 앞으로 할 일의 순서

이 문서는 앞으로 할 일의 **순서의 정본**입니다. 일의 식별자는 이름입니다 — 계획 문서의 파일 이름, 또는 `Candidates-<영역>.md` 의 `### 이름 — 제목` 절입니다. 옛 번호(`C-30` 등)는 `py -3 -m Scripts plans --find <옛번호>` 가 새 이름을 찾아 줍니다. 이 문서는 순서만 가지고, 항목 설명을 복사하지 않으며 링크로만 가리킵니다.
끝난 단위는 이 목록에서 지웁니다. 작업 전에 항목의 현재 완성도를 코드로 확인합니다. 같은 파일을 건드리는 단위는 같은 작업자에게 묶고, 동시 빌드는 둘까지입니다([작업 방식](../11_Workflow.md)). GPU 확인이 필요한 단위는 구현만 하고 `gpu-verifier` 로 캡처합니다.
항목 끝의 `[병렬 가능]` 은 다른 작업자와 같은 시간대에 병합해도 건드리는 파일이 겹치지 않는다는 뜻이고, `[단독: 이유]` 는 혼자 하는 시간대가 필요하다는 뜻입니다. 근거는 그 일이 건드리는 파일 영역입니다.

## 지금 순서

1. **`integrator` 설계의 미구현 단위**(설계는 끝났고 코드가 없음).
   - 데이터화: D6 ~ D9 · P1 · P4 · G1 코드가 들어갔다(`wt/datadriven`, 빌드 미확인). 남은 것: 묶음 빌드와 검증(P2 의 클리어 색 · 원본 이름 폴백은 지웠다 — 네 백엔드 골든 확인), G2 의 Shipping 골든 그림 뜨기(시나리오 · 케이스는 들어감) — [DataDriven](DataDriven.md) 7.5. [단독: 기본값 · 로더 형식을 바꿔 여러 모듈이 다시 빌드됨]
   - E3 · E4 의 남은 확인(빌드 · 테스트 · 큐브 20,000 개 벤치) — [EnginePartition](EnginePartition.md) 6.4. 구현은 `wt/enginepart` 에 있다. [단독: 모든 모듈 재빌드]
   - V2 — [GpuVerification](GpuVerification.md) 2절. 지는 케이스 수정은 들어갔고(로컬 확인, CI 미확인) 리눅스 `gl` 원인 확인 뒤 해제. [병렬 가능]
2. **새 시스템**:
   - Vfx V4 → V5(V0 ~ V3 · V6 · V7 들어감) — [Vfx](Vfx.md). [병렬 가능]
   - 클러스터드 라이팅 S2 → S5 — [ClusteredLighting](ClusteredLighting.md). [단독: 셰이더 재베이크]
   - 간접 조명 P0 부터(ClusteredLighting 뒤) — [IndirectLighting](IndirectLighting.md). [단독: 셰이더 재베이크]
   - 월드 스트리밍 W7 측정 — [WorldStreaming](WorldStreaming.md) (W2 → W6 와 W7 데이터는 빌드 미확인으로 들어갔다 — 빌드 · 쿠킹 · World Cells 창 캡처 확인이 먼저). [단독: W7 의 히치 측정은 Release 에서 혼자]
3. **기능 묶음**(분류는 결정 기록 5-12, 한 묶음에 단위가 여럿):
   1. 카메라 디렉터 · 직교 리그 [CameraRendererIntegration](CameraRendererIntegration.md)(S2 피사계 심도, S3 · S4 는 렌더러 기능 선행) → [GamepadVirtualCursor](GamepadVirtualCursor.md)(G2 게임 바인딩 정하기 · G3 실기), 에디터 시나리오 [GraphSaveScenarioRun](Candidates-editor.md#graphsavescenariorun) · [ScenarioRecorderAndInspectorGaps](Candidates-editor.md#scenariorecorderandinspectorgaps) (옛 `D4-5` → `D4-3` → `D3-5` · `D3-8` 에 대응), 에디터 기능의 남은 것 [EditorFeatureGaps](EditorFeatureGaps.md)(스무드 브러시 · 디자이너 손잡이 · 슈터 무적 · 집합 핸들 막기는 빌드 미확인으로 들어갔다 — 지형 크기 · 구멍은 엔진 API 가 먼저). [병렬 가능]
   2. 미리보기 창 · 스카이의 확인 — [EditorMultiWorld](EditorMultiWorld.md) 6절 (3) 스카이 패스(컴포넌트 · 패킷 · 상수 · 패스 · 셰이더 들어감), (4) 창을 다시 여는 시간 · 메모리 측정, (5) `previewreopen` · (6) `previewswap` 네 백엔드 실행. [병렬 가능]
   3. 지형 · 식생 [TerrainEditCost](TerrainEditCost.md)(T3 Release 측정 — T4 교체 프레임의 메시 교체는 빌드 미확인으로 들어갔다), 서버 파괴 호스트 [DestructionNetSessionScene](Candidates-network.md#destructionnetsessionscene) → [EncryptedTickCost](Candidates-network.md#encryptedtickcost). [단독: 지형 · 식생이 셰이더 재베이크를 부르고 측정은 Release 에서 혼자]
   4. 오디오 [AudioEngineExtensions](AudioEngineExtensions.md)(E6 측정이 남음), 온라인 키트 마감 [OnlineKits](OnlineKits.md)(아홉 후보 절을 단위로 옮겼다). [병렬 가능]
   5. 애니메이션 — [PairedAnimation](PairedAnimation.md) · [RigEditing](RigEditing.md) · [AnimationLodAndCrowd](Candidates-gameplay.md#animationlodandcrowd), 천 동적 메시 [ClothFollowups](ClothFollowups.md), 물리 확장 [PhysicsExtensions](PhysicsExtensions.md)(부력 · 파워드 래그돌 API · 캐릭터 속 바디 코드는 들어갔고 래그돌 컴포넌트 잇기 P4 가 남았다), Shooter3D [ShooterCharacterIntegration](ShooterCharacterIntegration.md), 서버 디버깅 [ServerDebug](ServerDebug.md)(S5 흐름 이벤트 · S8 한 프로세스 재생 들어감 — 빌드 미확인), 네트워크 [RollbackPrediction](RollbackPrediction.md)(R2 클라이언트-서버 관심 영역) · [PersistentRecordUnknownFields](PersistentRecordUnknownFields.md)(호출부 옮기기) · [NetChannelSchemaHashHandshake](NetChannelSchemaHashHandshake.md)(Mech · Horror 스냅샷). [병렬 가능]
   6. 그래픽스 형식 묶음(한 작업자, 순서대로, 셰이더 재베이크 · 에셋 재작성): [ViewModeCompletion](Candidates-graphics.md#viewmodecompletion)(Shaded Wireframe 은 RHI 깊이 비교 · 바이어스가 먼저). 디퍼드의 툰은 포워드 전용 패스로 들어갔다(결정 기록 5-5). 머티리얼 텍스처 칸 16개는 들어갔다(`TerrainWaterVegetationRemainders` 의 함정). [단독: 셰이더 재베이크 · 에셋 재작성]
   7. 런타임 UI — [UiInputRemaining](UiInputRemaining.md)(I1 게임 뷰 포인터 · I2 리눅스 클립보드는 빌드 미확인으로 들어갔다. I3 빌드 · 실기 확인, 모듈 ABI 가 8 로 올라 App · 에디터 · 게임 모듈을 함께 짓는다) · [UiTransitionCurveEvents](UiTransitionCurveEvents.md)(C2 빌드 · 테스트) · [UiNotificationRemaining](UiNotificationRemaining.md)(N2 빌드 · 테스트와 게임에 붙여 보기). [병렬 가능]
   7-1. 조건부에서 착수한 것(2026-10-22 사용자 결정, `wt/condB` 에 빌드 미확인으로 들어감 — 빌드 · 테스트가 먼저): 에디터 [PanelIDLiterals](PanelIDLiterals.md)(P3 확인) · [ScenarioWritesSourceTree](ScenarioWritesSourceTree.md)(S3 두 시나리오 이관 · 실행) · [DesignerDataToolsAudit](DesignerDataToolsAudit.md)(D1 행동 트리 데이터 → D2 편집기, D3 머티리얼 그래프 결정), 네트워크 [MmoUpdateAckViaDelivery](MmoUpdateAckViaDelivery.md)(A4 확인 · 재측정) · [SubtickInput](SubtickInput.md)(B4 발사 · B6 입력 스레드) · [PatchDeltaDelivery](PatchDeltaDelivery.md)(P4 시작 마운트 → P5 쿠커 → P6 배포), UI [UiTooltipAndCursor](UiTooltipAndCursor.md)(T4 실기), 서버 버스 형식의 "남은 확인"(`ServerBusFormat` 문서 — 테스트 실행 · Release 측정, 해지 알림 서버 둘 테스트는 썼다). [병렬 가능]
   8. 뒤 순서: Wayland 창 계층 [WaylandWindowLayer](WaylandWindowLayer.md)(코드는 들어갔고 CI 리눅스 잡 확인이 남았다), 서버 렌더러 제거 [DedicatedServerNoRenderer](DedicatedServerNoRenderer.md)(코드는 들어갔고 Server 타깃 빌드 · 측정 · 리눅스 CI 가 남았다), 워프 분리 [MeshModifierStack](Candidates-editor.md#meshmodifierstack)(워프 내보내기(`4174d9b8c` · `c82a29a77`) · 앞부분 캐시 · 시간 재생 · 표본기 상한은 들어갔고 절에는 큰 메시 비용 측정 · 셰이딩 미리보기 비용과 캡처 확인이 남았다). [병렬 가능]
4. **파일이 겹쳐 미룬 정리**: [PreludeAndFwdCleanup](Candidates-engine.md#preludeandfwdcleanup) · [CommentRewriteRemaining](Candidates-project.md#commentrewriteremaining) · [NamingAuditNext](Candidates-project.md#namingauditnext) · [LongFunctions](LongFunctions.md) — 위 1~2 뒤에. HeaderIncludeSlimming · EngineServiceInjection 은 끝났다(2026-10-21, `wt/cleanup347` — 레지스트리를 값 멤버로 두는 판정, 쿠커 · 초상화 · 글꼴 서비스가 엔진 묶음을 받음, 빌드 미확인). 앞서 병합된 [NameReferenceAudit](Candidates-engine.md#namereferenceaudit) 는 린트로 더 풀 것이 없어 `SW_DEV_COMMAND` 같은 선언 등록 형식의 결정이 먼저다. [단독: 헤더 · include 를 바꿔 여러 모듈이 다시 빌드됨]
5. **새로 쓴 정리**: [SerializerSilentSkips](SerializerSilentSkips.md)(S1 바이너리 · S2 텍스트 형식 · S3 원소 안 객체째 실패 · S4 바이너리 객체째 실패와 비트필드 이관 들어감, 병합 때 테스트 · 씬 로드 확인) · [FunctorLambdaCommonization](Candidates-engine.md#functorlambdacommonization) · [DiskPathVsResourceId](DiskPathVsResourceId.md)(안 B 들어감 — 리소스 입구는 리소스 id 만, 루트 밖은 `mountDiskDomain`. 빌드 · 테스트로 남은 OS 경로 호출부 확인) · [InvalidMeasurementCleanup](Candidates-project.md#invalidmeasurementcleanup) · [DecisionMeasurementColumn](Candidates-project.md#decisionmeasurementcolumn) · [CarriedOverChecks](Candidates-project.md#carriedoverchecks) — 순서는 정리 → 기능. [병렬 가능]
6. **구조 단순화 네 정책**: 필수 설정(HC) → 팩 쿠킹을 C++ 로(PK) → 에셋 종류 등록 줄 하나(AT) → 텍스트 형식 역할별 하나(TX) — [StructureSimplification](StructureSimplification.md). DataDriven D8 · D9 와 겹치는 지점은 그 문서 2절. [단독: AT1 은 `AssetFormat.h`, HC3 은 `GameConfig.h` 를 바꿔 여러 모듈이 다시 빌드됨]
7. **Scripts 구조**(import 루트 하나 · 3층 · 층 게이트) — [ScriptsStructure](ScriptsStructure.md). 구현은 끝났고 5절 측정 · 6절 확인이 남았다. [병렬 가능]
8. **빌드 속도 12 단계**(리플렉션 헤더 팬아웃) — [BuildSpeed](BuildSpeed.md). [단독: 빌드가 도는 동안은 측정이 무효라 다른 작업이 모두 끝난 뒤 혼자]

## 지금 하지 않는 것

조건부 · 보류 · 관찰 항목과 조건이 붙은 기능은 영역별 후보 문서의 `## 조건부` / `## 보류` / `## 관찰` 절에 있고 순서에 올리지 않는다. 조건이 오면 해당 후보 문서에서 골라 온다: [engine](Candidates-engine.md) · [graphics](Candidates-graphics.md) · [gameplay](Candidates-gameplay.md) · [editor](Candidates-editor.md) · [network](Candidates-network.md) · [ui](Candidates-ui.md) · [quality](Candidates-quality.md) · [project](Candidates-project.md) · [audio-ai-localization](Candidates-audio-ai-localization.md). 끝난 단위는 이 목록에서 지운다.
