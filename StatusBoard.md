# 지금 진행 상황

`docs/plans` 문서의 머리 줄과 [로드맵](docs/plans/Roadmap.md)에서 `py -3 -m Scripts status-board --write` 로 만든 생성 파일입니다. 손으로 고치지 않고, 통합 담당이 병합 뒤에만 다시 만듭니다.

<!-- status-board:start -->
| 순위 | 업무(계획 문서) | 상태 | 진행 | 선행 | 영역 | 옛번호 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | [DataDriven](docs/plans/DataDriven.md) — 데이터 주도 구조 정리 계획 (2026-10-16) | 구현중 | 13/14 | 없음 | engine | C-30, C-36 |
| 2 | [Vfx](docs/plans/Vfx.md) — 파티클 · VFX 구현 단위 (`D2-11`) | 측정대기 | 12/12 | 없음 | graphics | D2-11 |
| 3 | [CameraRendererIntegration](docs/plans/CameraRendererIntegration.md) — 카메라의 렌더러 쪽 연결과 다중 뷰 검증 | 측정대기 | 4/4 | 없음 | gameplay | D4-5 |
| 4 | [LongFunctions](docs/plans/LongFunctions.md) — 긴 함수 정리 기준 (2026-10-18) | 구현중 | 해당없음 | 없음 | project | C-18 |
| 5 | [SerializerSilentSkips](docs/plans/SerializerSilentSkips.md) — 직렬화가 조용히 넘기는 곳 (2026-10-21) | 측정대기 | 4/4 | 없음 | engine | C-66 |
| 6 | [StructureSimplification](docs/plans/StructureSimplification.md) — 구조 단순화 계획 (2026-10-20) | 측정대기 | 21/21 | 없음 | engine | 없음 |
| 7 | [ScriptsStructure](docs/plans/ScriptsStructure.md) — Scripts 구조 — import 루트 하나 · 3층 · 층 게이트 (2026-10-21) | 측정대기 | 10/10 | 없음 | project | 없음 |
| 8 | [ModuleRenderPass](docs/plans/ModuleRenderPass.md) — 모듈이 렌더 패스를 등록하는 창구 | 구현중 | 2/3 | 없음 | engine | G-1 |
| 9 | [BuildSpeed](docs/plans/BuildSpeed.md) — 빌드 속도 계획 | 구현대기 | 6/13 | 다른 작업이 모두 끝난 뒤 혼자 | quality | C-2 |
| 10 | [AutomationSplit](docs/plans/AutomationSplit.md) — Automation 분리 | 구현중 | 0/4 | 없음 | engine | 없음 |
| 11 | [HostGpuIsolation](docs/plans/HostGpuIsolation.md) — hostgpu 의 AppBin 잠금 풀기 — 실행별 쓰기 위치 나누기 | 설계 | 0/4 | 없음 | quality | 없음 |
| - | [ClusteredLighting](docs/plans/ClusteredLighting.md) — 클러스터드 라이팅 구현 단위 (`D2-13`) | 구현중 | 5/6 | 없음 | graphics | D2-13 |
| - | [CrossFileObjectReference](docs/plans/CrossFileObjectReference.md) — 씬 · 프리팹 파일을 넘는 오브젝트 참조 | 구현중 | 2/3 | 없음 | engine | G-5 |
| - | [DedicatedServerNoRenderer](docs/plans/DedicatedServerNoRenderer.md) — 전용 서버 타깃에서 렌더러 빼기와 리눅스 확인 | 구현중 | 1/3 | 없음 | network | D7-2 |
| - | [EnvironmentRemainders](docs/plans/EnvironmentRemainders.md) — 환경(지형 · 식생 · 물)의 남은 것 (2026-10-24) | 구현중 | 3/4 | 없음 | graphics | D2-5, D2-5.2, D2-5.5 |
| - | [GpuVerification](docs/plans/GpuVerification.md) — Linux 와 GPU 검증 구조의 남은 일 (2026-10-18) | 구현중 | 1/3 | 없음 | quality | C-41, E-8 |
| - | [LeakDetection](docs/plans/LeakDetection.md) — 누수 탐지 | 구현중 | 0/4 | 없음 | engine | 없음 |
| - | [MsdfGlyphs](docs/plans/MsdfGlyphs.md) — MSDF 글리프 | 구현중 | 4/5 | 없음 | ui | G-10 |
| - | [OnlineKits](docs/plans/OnlineKits.md) — 온라인 키트 마감 계획 (2026-10-21) | 구현중 | 29/34 | 없음 | network | D7-5 |
| - | [PackedTypes](docs/plans/PackedTypes.md) — 저장 전용 압축 타입 | 구현중 | 3/6 | 없음 | graphics | 없음 |
| - | [PreloadAndLod](docs/plans/PreloadAndLod.md) — 프리로딩 · LOD · 사전 준비 | 구현중 | 10/11 | 없음 | gameplay | D4-7 |
| - | [RhiAsyncUploads](docs/plans/RhiAsyncUploads.md) — RHI 비동기 업로드 · 머티리얼 초기화 · PSO 준비 상태 | 구현중 | 8/15 | 없음 | graphics | 없음 |
| - | [RuntimeQualityAdjust](docs/plans/RuntimeQualityAdjust.md) — 부하 검증의 남은 것과 자동 품질 조정 | 구현중 | 1/4 | 없음 | quality | C-29 |
| - | [ServerDebug](docs/plans/ServerDebug.md) — 서버 디버깅 계획 — 에디터에서 서버 · 멀티플레이를 손쉽게 보는 길 | 구현중 | 9/15 | 없음 | network | D7-3 |
| - | [ShooterCharacterIntegration](docs/plans/ShooterCharacterIntegration.md) — Shooter3D 캐릭터 통합의 남은 것 | 구현중 | 6/7 | 없음 | gameplay | D4-10 |
| - | [SilentFallbackAudit](docs/plans/SilentFallbackAudit.md) — 조용한 폴백과 남은 같은 결함 감사 (2026-10-22) | 구현중 | 10/14 | 없음 | engine | 없음 |
| - | [TerrainEditCost](docs/plans/TerrainEditCost.md) — 지형 LOD 교체 · 지형 쓰기 · 식생 다시 계산의 비용 (2026-10-21) | 구현중 | 3/4 | 없음 | graphics | D2-5.6, D2-5.7 |
| - | [ThirdPartyGaps](docs/plans/ThirdPartyGaps.md) — 서드파티 빈자리 | 구현중 | 5/7 | 없음 | project | G-11 |
| - | [WaylandWindowLayer](docs/plans/WaylandWindowLayer.md) — Wayland 창 계층 | 구현중 | 1/3 | 없음 | engine | D1-6 |
| - | [WildWestHorseMount](docs/plans/WildWestHorseMount.md) — WildWest 승마의 말 모델 | 구현중 | 2/3 | 없음 | gameplay | D4-15 |
| - | [AbilitySystemExtensions](docs/plans/AbilitySystemExtensions.md) — 어빌리티 시스템의 다음 조각 | 측정대기 | 5/5 | 없음 | gameplay | D4-11 |
| - | [AnimationAdvancedFeatures](docs/plans/AnimationAdvancedFeatures.md) — 애니메이션 고급 기능 | 측정대기 | 4/4 | 없음 | gameplay | D4-16 |
| - | [AnimationComponentChain](docs/plans/AnimationComponentChain.md) — 애니메이션 평가를 연결 성분 단위로 옮기기 | 측정대기 | 4/4 | 없음 | gameplay | 없음 |
| - | [AnimationLodAndCrowd](docs/plans/AnimationLodAndCrowd.md) — 애니메이션 LOD 와 군중 공유의 남은 것 | 측정대기 | 4/4 | 없음 | gameplay | D4-6 |
| - | [AppStructure](docs/plans/AppStructure.md) — App 구조 정리 (2026-10-22) | 측정대기 | 7/7 | 없음 | engine | 없음 |
| - | [AsyncResourceLoading](docs/plans/AsyncResourceLoading.md) — 리소스 비동기 로드와 디더 등장 계획 (2026-10-20) | 측정대기 | 7/7 | 없음 | engine | 없음 |
| - | [AudioChannelLayout](docs/plans/AudioChannelLayout.md) — 오디오 채널 레이아웃 계획 (2026-10-20) | 측정대기 | 5/5 | 없음 | audio-ai-localization | D5-1 |
| - | [AudioEngineExtensions](docs/plans/AudioEngineExtensions.md) — 오디오 엔진 확장 계획 (2026-10-21) | 측정대기 | 8/8 | AudioChannelLayout | audio-ai-localization | D5-1 |
| - | [CharacterAppearanceIntegration](docs/plans/CharacterAppearanceIntegration.md) — 캐릭터 외형의 통합 · 쿠킹 · 장비 잇기 | 측정대기 | 6/6 | 없음 | gameplay | D4-8 |
| - | [ClothFollowups](docs/plans/ClothFollowups.md) — 천(`ClothComponent`)의 남은 것 | 측정대기 | 3/3 | 없음 | engine | D1-4 |
| - | [ContentBrowserWatchAndScan](docs/plans/ContentBrowserWatchAndScan.md) — 콘텐츠 브라우저 감시 지연과 스캔 비용 | 측정대기 | 2/2 | 없음 | editor | 없음 |
| - | [DataKindRegistry](docs/plans/DataKindRegistry.md) — 엔진 데이터 종류의 레지스트리 이전 | 측정대기 | 5/5 | 없음 | editor | D3-1.2 |
| - | [DesignerDataToolsAudit](docs/plans/DesignerDataToolsAudit.md) — 디자이너용 데이터 도구 점검 | 측정대기 | 6/6 | 없음 | editor | D3-13 |
| - | [DiskPathVsResourceId](docs/plans/DiskPathVsResourceId.md) — 디스크 경로와 리소스 id 의 입구 나누기 (2026-10-21) | 측정대기 | 3/3 | 없음 | engine | C-70 |
| - | [EditorCharacterEditor](docs/plans/EditorCharacterEditor.md) — 캐릭터 편집 창 구현 계획 (D4-8 ⑥) | 측정대기 | 6/6 | 없음 | gameplay | D4-8 |
| - | [EditorFeatureGaps](docs/plans/EditorFeatureGaps.md) — 에디터 기능 후보의 남은 것 | 측정대기 | 10/10 | 없음 | editor | D3-1, D3-3, D3-4 |
| - | [EditorMultiWorld](docs/plans/EditorMultiWorld.md) — 다중 월드 · 에디터 툴 창 구현 계획 (D3-7) | 측정대기 | 6/6 | 없음 | editor | D3-7 |
| - | [EngineCleanups](docs/plans/EngineCleanups.md) — 엔진 정리 묶음 (2026-10-24) | 측정대기 | 7/7 | 없음 | engine | C-49, C-63, C-64, C-47, C-34 |
| - | [EnginePartition](docs/plans/EnginePartition.md) — 엔진 분할 계획 | 측정대기 | 6/6 | 없음 | engine | C-1 |
| - | [FacialAnimation](docs/plans/FacialAnimation.md) — 얼굴 애니메이션의 남은 것 | 측정대기 | 5/5 | 없음 | gameplay | D4-6 |
| - | [GameSceneCc0Meshes](docs/plans/GameSceneCc0Meshes.md) — 게임별 씬이 새로 들인 CC0 메시를 부르게 한다 | 측정대기 | 2/2 | 없음 | gameplay | D4-17 |
| - | [GamepadVirtualCursor](docs/plans/GamepadVirtualCursor.md) — 패드 가상 커서의 남은 것 | 측정대기 | 3/3 | 없음 | gameplay | D4-3 |
| - | [GlyphRasterizationHitch](docs/plans/GlyphRasterizationHitch.md) — 새 글리프 래스터화 히치 | 측정대기 | 3/3 | 없음 | ui | C-9 |
| - | [GraphicsFormatBundle](docs/plans/GraphicsFormatBundle.md) — 그래픽스 형식 묶음 — HDR 장면 색 · 툰 · 변형 탄젠트 · Shaded Wireframe · 2D 조명 · VRM 런타임 · 메시 구간 | 측정대기 | 12/12 | 없음 | graphics | D2-1, D2-6, D2-7, D2-10 |
| - | [GraphicsMeasurements](docs/plans/GraphicsMeasurements.md) — 그래픽스 측정 · 확인 묶음 (2026-10-24) | 측정대기 | 4/4 | 없음 | graphics | D2-9, D2-2, D2-10, C-55 |
| - | [ImportRuleOverlap](docs/plans/ImportRuleOverlap.md) — 임포트 규칙의 겹침 막기 | 측정대기 | 3/3 | 없음 | editor | C-44 |
| - | [IndirectLighting](docs/plans/IndirectLighting.md) — 간접 조명 구현 단위 (`D2-12`) | 측정대기 | 8/8 | ClusteredLighting | graphics | D2-12 |
| - | [IntentNetworkWiring](docs/plans/IntentNetworkWiring.md) — 의도 기록 · 원격 조종자의 남은 연결 | 측정대기 | 4/4 | 없음 | gameplay | D4-13 |
| - | [InteractionHighlightAndMontage](docs/plans/InteractionHighlightAndMontage.md) — 상호작용 강조 패스와 몽타주 실행 확인 | 측정대기 | 4/4 | 없음 | gameplay | D4-12 |
| - | [InteractionSpatialGrid](docs/plans/InteractionSpatialGrid.md) — 상호작용 대상의 공유 공간 격자 | 측정대기 | 3/3 | 없음 | gameplay | G-4 |
| - | [KayKitMaterials](docs/plans/KayKitMaterials.md) — KayKit 캐릭터 텍스처를 머티리얼로 | 측정대기 | 2/2 | 없음 | gameplay | D4-6 |
| - | [KitCompositionGames](docs/plans/KitCompositionGames.md) — 키트 조립의 두 번째 테스트 게임과 땅 쓰기 | 측정대기 | 2/2 | 없음 | gameplay | D4-4 |
| - | [MeleeHitUnify](docs/plans/MeleeHitUnify.md) — 근접 판정 한 길로 | 측정대기 | 3/3 | 없음 | gameplay | 없음 |
| - | [MeshModifierPreviewCost](docs/plans/MeshModifierPreviewCost.md) — 메시 모디파이어 미리보기 비용 | 측정대기 | 3/3 | 없음 | editor | D3-10 |
| - | [MovingGroundFollowUps](docs/plans/MovingGroundFollowUps.md) — 움직이는 바닥 위 캐릭터의 남은 일 | 측정대기 | 4/4 | 없음 | gameplay | 없음 |
| - | [NetChannelSchemaHashHandshake](docs/plans/NetChannelSchemaHashHandshake.md) — 넷 채널 등록 API 로 스키마 해시를 핸드셰이크 버전에 섞기 | 측정대기 | 2/2 | 없음 | network | D7-7 |
| - | [PairedAnimation](docs/plans/PairedAnimation.md) — 페어드 애니메이션(여러 개체가 한 타임라인으로 도는 애니메이션) | 측정대기 | 6/6 | 없음 | gameplay | D4-6 |
| - | [PanelIDLiterals](docs/plans/PanelIDLiterals.md) — 패널을 id 글자로 여는 남은 호출 | 측정대기 | 3/3 | 없음 | editor | 없음 |
| - | [PatchDeltaDelivery](docs/plans/PatchDeltaDelivery.md) — 패치 · 델타 업데이트 배포 구조 | 측정대기 | 7/7 | 없음 | network | G-16 |
| - | [PersistentRecordUnknownFields](docs/plans/PersistentRecordUnknownFields.md) — 영속 레코드의 모르는 필드 보관 | 측정대기 | 2/2 | 없음 | network | F-3 |
| - | [PhysicsExtensions](docs/plans/PhysicsExtensions.md) — 물리 확장 — 부력 · 파워드 래그돌 · 천과 캐릭터 · 결정성 | 측정대기 | 5/5 | 없음 | engine | D1-8 |
| - | [RealtimeGlobalIllumination](docs/plans/RealtimeGlobalIllumination.md) — 실시간 전역 조명과 볼륨 표현 (2026-10-24) | 측정대기 | 7/7 | ClusteredLighting | graphics | D2-14, D2-5.2 |
| - | [ReflectionFormats](docs/plans/ReflectionFormats.md) — 리플렉션 직렬화 형식 계획 (2026-10-17) | 측정대기 | 12/13 | 없음 | engine | 없음 |
| - | [RigEditing](docs/plans/RigEditing.md) — 후처리 리그 편집 저장과 기즈모 | 측정대기 | 3/3 | 없음 | gameplay | D4-6 |
| - | [RollbackPrediction](docs/plans/RollbackPrediction.md) — 클라이언트 예측 · 롤백과 관심 영역 복제 | 측정대기 | 4/4 | 없음 | network | D7-8 |
| - | [ScreenReaderSupport](docs/plans/ScreenReaderSupport.md) — 스크린 리더 | 측정대기 | 6/6 | 없음 | ui | G-9 |
| - | [ShooterStrideWarping](docs/plans/ShooterStrideWarping.md) — Shooter3D 발 미끄러짐 연결 | 측정대기 | 2/2 | 없음 | gameplay | D4-6 |
| - | [SourceScanGates](docs/plans/SourceScanGates.md) — 공유 훑기로 못 옮긴 소스 게이트와 픽서 | 측정대기 | 4/4 | 없음 | quality | C-48 |
| - | [StructuralOptimization](docs/plans/StructuralOptimization.md) — 구조 최적화 측정 표 (A · B) | 측정대기 | 해당없음 | 없음 | engine | 없음 |
| - | [TilemapCellDataLayers](docs/plans/TilemapCellDataLayers.md) — 타일맵 칸 데이터를 일반 레이어로 | 측정대기 | 3/3 | 없음 | gameplay | G-6 |
| - | [UiDrawPerformance](docs/plans/UiDrawPerformance.md) — 런타임 UI 그리기 성능의 남은 것 | 측정대기 | 0/2 | 없음 | ui | D6-6 |
| - | [UiInputRemaining](docs/plans/UiInputRemaining.md) — 런타임 UI 입력의 남은 것 | 측정대기 | 3/3 | 없음 | ui | D6-2 |
| - | [UiNotificationRemaining](docs/plans/UiNotificationRemaining.md) — 알림 · 힌트 · 목표 마커의 남은 것 | 측정대기 | 2/2 | 없음 | ui | D6-5 |
| - | [UiTooltipAndCursor](docs/plans/UiTooltipAndCursor.md) — 런타임 UI 툴팁과 커서 모양 | 측정대기 | 4/4 | 없음 | ui | 없음 |
| - | [UserSettingsConsumers](docs/plans/UserSettingsConsumers.md) — 사용자 설정 값을 읽는 쪽 잇기 | 측정대기 | 4/4 | 없음 | gameplay | D4-2 |
| - | [VfxGpuLights](docs/plans/VfxGpuLights.md) — GPU 로 시뮬레이션하는 방출기의 파티클 라이트 (2026-10-24) | 측정대기 | 5/5 | ClusteredLighting | graphics | 없음 |
| - | [WorldStreaming](docs/plans/WorldStreaming.md) — 월드 스트리밍(D1-7) — 설계와 구현 단위 | 측정대기 | 7/7 | 없음 | engine | D1-7 |
| - | [WorldWidgetInput](docs/plans/WorldWidgetInput.md) — 월드 공간 위젯의 입력 · 가려짐 | 측정대기 | 4/4 | 없음 | ui | G-3 |
| - | [MotionMatching](docs/plans/MotionMatching.md) — 모션 매칭 (보류) | 보류 | 해당없음 | 없음 | gameplay | D4-16 |

후보 문서 10개 · 후보 절 55개
<!-- status-board:end -->
