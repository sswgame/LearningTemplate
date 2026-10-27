# 로드맵 — 앞으로 할 일의 순서

> 질문: 남은 일을 어떤 순서로 하는가?
> 독자: 다음 일을 고르는 통합 담당과 작업자.
> 결론: 이 문서는 순서와 병렬 여부만 가진다. 일의 설계 · 진행 · 상태는 링크한 계획 문서의 머리 줄이 원본이다. 끝난 일은 이 목록에서 지운다.

일의 식별자는 이름입니다. 계획 문서의 파일 이름이거나 `Candidates-<영역>.md` 의 `### 이름` 절입니다. 옛 번호(`C-30` 등)는 `py -3 -m Scripts plans --find <옛번호>` 로 풉니다.
작업 전에 항목의 현재 완성도를 코드로 확인합니다. 같은 파일을 건드리는 단위는 같은 작업자에게 묶습니다([작업 방식](../11_Workflow.md)). GPU 확인이 필요한 단위는 구현만 하고 통합에서 `gpu-verifier` 로 캡처합니다.
`[병렬 가능]` 은 다른 작업자와 같은 시간대에 병합해도 건드리는 파일이 겹치지 않는다는 뜻이고, `[단독: 이유]` 는 혼자 하는 시간대가 필요하다는 뜻입니다.

## 지금 순서

1. **설계는 끝났고 코드가 없거나 검증이 남은 단위**
   - 데이터화 [DataDriven](DataDriven.md) 7.5. [단독: 기본값 · 로더 형식을 바꿔 여러 모듈이 다시 빌드됨]
   - Linux 와 GPU 검증 [GpuVerification](GpuVerification.md) 2절. [병렬 가능]
2. **새 시스템**
   - 누수 탐지 [LeakDetection](LeakDetection.md). [단독: `Memory.h` 를 바꿔 여러 모듈이 다시 빌드됨, 측정은 Release 에서 혼자]
   - 클러스터드 라이팅 [ClusteredLighting](ClusteredLighting.md). [단독: 측정은 Release 에서 혼자]
   - 환경 남은 것 [EnvironmentRemainders](EnvironmentRemainders.md). [단독: 측정은 Release 에서 혼자]
3. **기능 묶음**(분류는 결정 기록 5-12)
   1. 에디터 시나리오 GraphSaveScenarioRun. [병렬 가능]
   2. 미리보기 창 [EditorMultiWorld](EditorMultiWorld.md) 6절. [병렬 가능]
   3. 지형 · 식생 [TerrainEditCost](TerrainEditCost.md), 서버 파괴 호스트 [DestructionNetSessionScene](Candidates-network.md#destructionnetsessionscene) → [EncryptedTickCost](Candidates-network.md#encryptedtickcost). [단독: 셰이더 재베이크, 측정은 Release 에서 혼자]
   4. 오디오 [AudioEngineExtensions](AudioEngineExtensions.md), 온라인 키트 마감 [OnlineKits](OnlineKits.md). [병렬 가능]
   5. 애니메이션 · 게임플레이: Shooter3D [ShooterCharacterIntegration](ShooterCharacterIntegration.md), [AnimationAdvancedFeatures](AnimationAdvancedFeatures.md), 서버 디버깅 [ServerDebug](ServerDebug.md), 네트워크 [RollbackPrediction](RollbackPrediction.md), 게임플레이 키트 [WildWestHorseMount](WildWestHorseMount.md). [병렬 가능]
   7. 런타임 UI: [MsdfGlyphs](MsdfGlyphs.md) · [GlyphRasterizationHitch](GlyphRasterizationHitch.md) · [UiDrawPerformance](UiDrawPerformance.md)(측정이 이득을 보일 때만). [병렬 가능]
   11. 프리로딩 · LOD [PreloadAndLod](PreloadAndLod.md). [단독: `.mesh` 판 올림 · 셰이더 재베이크]
   12. 뒤 순서: [WaylandWindowLayer](WaylandWindowLayer.md) · [DedicatedServerNoRenderer](DedicatedServerNoRenderer.md) · [MeshModifierPreviewCost](MeshModifierPreviewCost.md). [병렬 가능]
4. **파일이 겹쳐 미룬 정리**(위 1 ~ 2 뒤에): [LongFunctions](LongFunctions.md). [단독: 헤더 · include 를 바꿔 여러 모듈이 다시 빌드됨]
5. **새로 쓴 정리**(정리 → 기능 순서): [FunctorLambdaCommonization](Candidates-engine.md#functorlambdacommonization) · [InvalidMeasurementCleanup](Candidates-project.md#invalidmeasurementcleanup) · [DecisionMeasurementColumn](Candidates-project.md#decisionmeasurementcolumn) · [CarriedOverChecks](Candidates-project.md#carriedoverchecks) · [SilentFallbackAudit](SilentFallbackAudit.md). [병렬 가능]
8. **조건부에서 착수한 일**: [ModuleRenderPass](ModuleRenderPass.md) · [CrossFileObjectReference](CrossFileObjectReference.md) · [ThirdPartyGaps](ThirdPartyGaps.md) · [InteractionSpatialGrid](InteractionSpatialGrid.md). [병렬 가능]
9. **빌드 속도 12 단계**(리플렉션 헤더 팬아웃) [BuildSpeed](BuildSpeed.md). [단독: 다른 작업이 모두 끝난 뒤 혼자, 빌드가 도는 동안은 측정이 무효]
10. **2026-10-23 사용자와 정한 일**
    - Automation 분리 [AutomationSplit](AutomationSplit.md). [병렬 가능: 셰이더 · 층 게이트와 파일이 다르다]
    - 패치 · 델타 배포 [PatchDeltaDelivery](PatchDeltaDelivery.md): 출시는 스팀 기준이라 P6 은 스팀 전용이면 취소하고 P4 · P5 는 스팀 밖과 콘텐츠 핫픽스용으로 내린다. P7(팩 레이아웃 안정성 점검)이 남은 일이다. [단독: P7 측정은 Release 에서 혼자]
11. **병렬 · 비동기 트랙의 남은 단위**(2026-10-25 통합 I 에서 들어온 설계)
    - `hostgpu` 의 AppBin 잠금 풀기 [HostGpuIsolation](HostGpuIsolation.md) H1 → H4. [단독: H1 은 앱을 돌려 쓰기 위치를 스냅샷한다 — 통합 · gpu-verifier]
    - RHI 비동기 업로드 [RhiAsyncUploads](RhiAsyncUploads.md) — B0 골격 번들은 통합 L, U8 · U9 의 프레임 경로 몫(글리프 아틀라스 · 텍스처 구간 갱신)은 통합 M, Vulkan 비동기 전송(U6, 매니페스트 id `rhi-transfer-vk-u6`)은 통합 N 에서 들어갔다. DX12 비동기 전송(U5, 매니페스트 id `rhi-transfer-dx12-u5`)은 코드가 끝났고 GPU 확인은 통합이 돌린다. U7(테스트) · U8 남은 몫(새 텍스처 업로드 세 단계) · U9 남은 몫(렌더 타깃 읽기 티켓) · U11(PSO 상태)을 단위별로, 그 뒤 U10 · U12 ~ U14. [병렬 가능: 단위마다 소유 폴더가 다르다(계획 문서 표), GPU 확인은 통합]
    - 저장 전용 압축 타입 [PackedTypes](PackedTypes.md) P4(G 버퍼 법선 — 설계는 문서에 있고 코드는 GPU 확인과 재쿠킹을 하는 통합이 쓴다) → P5 → P6. [단독: P4 는 셰이더 재베이크]

## 지금 하지 않는 것

조건부 · 보류 · 관찰 항목은 영역별 후보 문서의 `## 조건부` / `## 보류` / `## 관찰` 절에 있고 순서에 올리지 않는다. 조건이 오면 해당 후보 문서에서 골라 온다: [engine](Candidates-engine.md) · [graphics](Candidates-graphics.md) · [gameplay](Candidates-gameplay.md) · [editor](Candidates-editor.md) · [network](Candidates-network.md) · [ui](Candidates-ui.md) · [quality](Candidates-quality.md) · [project](Candidates-project.md) · [audio-ai-localization](Candidates-audio-ai-localization.md).
