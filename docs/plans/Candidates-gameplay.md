# 게임플레이 후보

> 상태: 후보 · 선행: 없음 · 영역: gameplay · 옛번호: D4-2,D4-3,D4-4,D4-5,D4-6,D4-7,D4-8,D4-9,D4-10,D4-11,D4-12,D4-13,D4-14,D4-15,D4-16,D4-17,G-4,G-6,G-7 · 진행: 해당없음

GameFramework 키트와 게임, 캐릭터, 카메라, 애니메이션 영역의 착수 전 후보 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다.
이미 계획 문서가 있는 일의 남은 것은 그 문서에 있습니다: [PairedAnimation](PairedAnimation.md) · [EditorCharacterEditor](EditorCharacterEditor.md) · [MotionMatching](MotionMatching.md).

## 기능

### UserSettingsConsumers
제목: 사용자 설정 값을 읽는 쪽 잇기
옛번호: D4-2
왜 필요한가: 옵션 메뉴 백엔드(`Source/Engine/UserSettings/`, 메뉴는 `Engine/UI/Screen/OptionsMenuScreen`)에 값만 있고 읽는 곳이 없는 대상이 남았다.
남은 것:
- 렌더러 몫: `gv_renderScale`(업스케일 패스) · 시야 거리 · 후처리 · 텍스처 · 이펙트 품질 · 모션 블러 · `gv_colorVisionMode` 의 톤맵 쪽(톤맵에 상수 버퍼가 없어 미뤘다 — 함수는 `colorvision.hlsli`, UI 캔버스는 이미 쓴다).
- 머리 흔들림은 데이터(`<HeadBob>` · `FirstPersonCameraComponent::_headBob`)로 켜는데 켠 게임이 없다 — Shooter3D · VoxelCraft 1인칭에 진폭을 넣고 화면으로 확인한다. 카메라 세 값은 `CameraUserSettings` 가 읽는다.
- X11 `setDisplayMode`(EWMH 전체 화면)는 리눅스 실기 미확인.

### GamepadVirtualCursor
제목: 패드 가상 커서의 남은 것
옛번호: D4-3
왜 필요한가: 명령형 게임에서 패드로 마우스 커서를 대신하는 가상 커서가 들어갔지만(입력 맵 `virtualCursor` · 충돌 검사 `reportVirtualCursorConflict`), 줌과 실기 확인이 남았다. Nile · StarSkirmish 는 커서가 왼쪽 스틱, 오른쪽 스틱이 카메라 이동이다.
남은 것:
- 오른쪽 스틱 줌: 1D 축 액션이 스틱의 Y 를 읽는 바인딩이 없고, 확대는 칸 단위라 스틱 값을 매 프레임 넣으면 너무 빠르다(지금은 D 패드 위 · 아래).
- 실제 패드로 확인하기(시나리오 동안 가상 커서는 쉬어 패드 길을 시나리오가 보지 않는다).

### KitCompositionGames
제목: 키트 조립의 두 번째 테스트 게임과 땅 쓰기
옛번호: D4-4
왜 필요한가: 틀 · 키트 전부 · 조립 테스트 둘(`KitCompositionTest`)과 `MeadowVillage` 는 들어갔다(규칙: `Source/GameFramework/Kits/` "키트 여럿을 한 게임에"). 필요해지기 전까지 남긴 몫이다.
착수 조건 / 남은 것: 둘째 조립(RTS + City)의 테스트 게임이 필요해지면 MeadowVillage 모양으로 만든다. 격자 키트를 섞는 게임이 생기면 `SessionStateSettings::_land*` 와 키트의 `bindLand` 를 건다.
함정: SRPG 는 전투 끝에 `releaseLand`. 공원 놀이기구 발자국은 상태 바이트에 싣지 않는다 — 지은 칸은 땅이 든다.

### CameraRendererIntegration
제목: 카메라의 렌더러 쪽 연결과 다중 뷰 검증
옛번호: D4-5
왜 필요한가: 프리셋 · 블렌드 스택 · 디렉터 · 모드 · 다중 뷰 · 시퀀서 카메라 신호 · 렌즈 값 · 돌리 레일, 다중 뷰의 화면 크기 보임 판정과 뷰 수 · 픽셀 예산(`RenderViewCollector` · `RenderViewScheduler` 헤더), 에디터 게임 뷰의 화면 사각형 뷰 시나리오(`screenrectview`)는 들어갔다(`Source/GameFramework/` "카메라" 절). 아래가 남았다.
남은 것:
- 다중 뷰 보임의 가려짐: 렌더러에 가림 컬링 결과가 없다. HZB 가림 컬링(이 문서 아래 "중간" 목록)이 들어오면 모니터가 지난 프레임에 그려졌는지(언리얼 `WasRecentlyRendered`)로 판정을 바꾼다(`RenderViewCollector` 헤더).
- 컷 프레임 신호는 카메라 쪽이 끝났다(디렉터 `consumeCut` → `CameraComponent::markCut` → `RenderViewSettings::_bCut`, 테스트 `CameraModeTest.DirectorComponentMarksTheCameraCutOnce`). 남은 것은 렌더러 쪽뿐이다: `_bCut` 는 TAA 기록만 리셋한다 — 렌더러에 모션 벡터 · 자동 노출 · 모션 블러가 생기면 같은 신호로 0 · 즉시 맞춤 · 끔을 붙인다. 대기 뷰 신호(`CameraManagerComponent::setPendingView`)를 듣는 쪽은 [PreloadAndLod](#preloadandlod).
- 렌더러 쪽: 피사계 심도 패스, 카메라별 후처리 값(`CameraPostProcessValues` — 포즈가 나르기만 한다)을 렌더러가 읽기.

### AnimationLodAndCrowd
제목: 애니메이션 LOD 와 군중 공유
옛번호: D4-6
왜 필요한가: 애니메이션 LOD(`AnimationLOD.h` — 리그 노드도 화면 크기 `min_screen_size` 로 꺼진다)와 군중 공유(`AnimationCrowd.h`)는 들어갔다.
남은 것:
- 메시 LOD 가 생기면 본 LOD 를 메시 LOD 와 묶기([PreloadAndLod](#preloadandlod)).
- 군중의 섞기 묶음(언리얼 Animation Sharing 의 블렌드 액터 — 지금 섞는 유닛은 사본으로 혼자 평가), Shooter3D 군중 켜기.

### FacialAnimation
제목: 얼굴 애니메이션의 남은 것
옛번호: D4-6
왜 필요한가: 얼굴(`FacialAnimationComponent`)은 있으나 립싱크와 얼굴 에셋이 합성 수준이다.
남은 것: 음소 인식 립싱크(지금은 세 대역 모양 분류 — 모음 넷 · 치찰음 정도만 가른다), 실제 얼굴 에셋(KayKit 은 모프가 없다 — 합성 테스트 머리뿐), 스킨 없는 메시의 모프(GPU 모프 풀이 스키닝 컴퓨트 안에서만 가중치를 건다), 실시간 얼굴 입력(Live Link Face 자리).

### PoweredRagdoll
제목: 파워드 래그돌과 헤어 · 천
옛번호: D4-6
왜 필요한가: 관절 모터가 애니메이션 포즈를 쫓는 래그돌(전신 · 부분 래그돌 · 맞음 반응 · 기상 섞기는 `RagdollComponent`).
남은 것: 헤어 카드 · 천(Jolt 소프트 바디 — 옛 D1-4), 가닥 헤어(TressFX)는 나중.

### KayKitMaterials
제목: KayKit 캐릭터 텍스처를 머티리얼로
옛번호: D4-6
왜 필요한가: 원본 GLB 에 든 `knight_texture` 등을 `textures_raw/` 로 옮겨 머티리얼을 만들어야 한다. 지금 캐릭터는 씬 기본 머티리얼(흰색)로 그려진다.
착수 조건 / 남은 것: Shooter3D 팩에는 이미 꺼낸 아틀라스(`textures_raw/kaykit_*.png`) · `materials/kaykit_*.material` 이 있다. 나머지 게임에 같은 일을 한다.

### ShooterStrideWarping
제목: Shooter3D 발 미끄러짐 연결
옛번호: D4-6
왜 필요한가: 앞으로 걷기 · 달리기는 재생 속도 매칭(`LocomotionSpeedUtil`, 범위 0.6 ~ 1.8)으로 맞췄고, 범위를 넘는 몫은 리그 노드 `StrideWarping`(+ `LocomotionSpeedUtil::computeStrideScale`)이 맡는다.
남은 것: Shooter3D 연결(`knight.rig.json` 에 노드를 두고 `ShooterAvatarComponent` 가 프레임마다 `stride_scale` · 이동 방향을 `setNodeControl` 로 넣기)과 화면 확인. 뒷걸음 · 옆걸음은 [ShooterCharacterIntegration](#shootercharacterintegration).

### AnimationAdvancedFeatures
제목: 애니메이션 고급 기능 후보
옛번호: D4-16
왜 필요한가: 전신 IK, 리타게팅 도구, 애니메이션 압축 · 샘플링 최적화. 모션 매칭은 보류라 [MotionMatching](MotionMatching.md) 에 따로 있다.
착수 조건 / 남은 것: 합성 리그 벤치(`도구 - 합성 리그 생성기`)가 비용 곡선을 낸 뒤 병목이 확인되는 것부터 한다.

### PreloadAndLod
제목: 프리로딩 · LOD · 사전 준비
옛번호: D4-7
왜 필요한가: 컷이나 레벨 전환에서 LOD · 텍스처 · 셰이더가 바뀌는 게 보이지 않게 미리 올리고 준비해야 한다(로드맵 규모).
남은 것:
- 프리로드 세트: 미리 올릴 에셋 + 미리 만들 프리팹 · 우선순위, 쿠킹 때 레벨 · 시퀀스 · 샷의 참조를 따라 자동 수집(`collectReferencedPrefabPaths` 가 있다), `requestPreload` 가 진행률 · 완료를 준다, 프레임 예산(IO · 업로드 · PSO · 인스턴스 수), 참조 수 · LRU 로 내림(지금 캐시는 약한 참조라 고정 단계가 필요하다) — 언리얼 AssetManager 번들 · Addressables.
- 프리팹 풀(숨겨 둔 인스턴스를 켜고 돌려받기 — 탄 · 손님 · 유닛), 시퀀서 사전 스폰(프리롤 동안 캐릭터를 숨겨 만들어 첫 포즈 · LOD · 머티리얼을 준비).
- 메시 LOD: 임포트 때 `meshopt_simplify` 로 단계 생성, `.mesh` 판 올림, GPU 컬링이 화면 크기로 고름, 디더 크로스페이드, 컷 · 대기 뷰 강제 LOD, HLOD 는 나중.
- 컷 준비(프리웜): 대기 뷰 신호(`CameraManagerComponent::setPendingView`, 시퀀서 컷 트랙의 프리롤)는 있고 듣는 쪽이 없다. LOD 선택은 대기 뷰 기준 LOD 도 구해 컷 첫 프레임부터 그 LOD(컷을 넘는 LOD 크로스페이드는 끈다), 스트리밍은 대기 뷰에서 보일 메시 · 텍스처 밉을 먼저 요청(언리얼 streaming source · AddViewLocation), PSO 는 대기 뷰 절두체 안 머티리얼의 PSO 를 미리 만들고, 그림자는 대기 뷰 기준 캐스케이드를 미리 맞춘다. 다중 뷰 렌더로 컷 1~2 프레임 전에 대기 뷰를 저해상도로 숨겨 그려 업로드 · PSO 를 강제로 데운다.
착수 조건 / 남은 것: 선행 조건 셋이 아직 없다 — LOD 시스템, 밉 단위 텍스처 스트리밍(`AssetStreamingQueue` 는 에셋 단위 비동기 읽기), PSO 를 미리 만드는 창구(DX12 PSO 생성 히치).

### CharacterAppearanceIntegration
제목: 캐릭터 외형의 통합 · 쿠킹 · 장비 잇기
옛번호: D4-8
왜 필요한가: 형상 쪽(`Source/Engine/Character`, README "외형 조립 순서")과 GPU 모프 풀(`Mesh::setGPUMorphEnabled`), 장비 해석 데이터 · 해석기(`GameFramework/Base/Gameplay/Appearance`)와 외형 컴포넌트(`CharacterAppearanceComponent`)는 들어갔다. 이어 붙이는 일이 남았다. 스켈레톤 에셋(`.skeleton.json`)은 소켓 파일을 처음 만들 근거(임포트가 적은 본 부착 메시 표)뿐이고 편집 창구가 없다(편집 창은 [EditorCharacterEditor](EditorCharacterEditor.md)).
남은 것:
- 통합: `Mesh` · 포즈 ↔ `AppearanceGeometry` · `CharacterBoneArray` 변환, 체형 모프 · 피팅 델타(`FitPartResult::_listVertexDelta`)를 GPU 모프 풀에 싣기(스키닝 앞), 병합 결과(`MeshMerger`)를 인덱스 · 정점 버퍼와 구간 그리기로, 애니메이션 시스템이 본이 움직인 프레임에만 `SocketBindingComponent::updateSocketTransform`, 표면 상태(`CharacterSurfaceState`)를 머티리얼 파라미터 · 마스크 텍스처로.
- 쿠킹: 장비 정점 → 몸 전이(`SurfaceTransferUtil` — 모프 · 스킨 가중치)를 임포트 · 쿠킹 단계에, 아틀라스 굽기(`IMeshMergeHooks` 구현).
- 핫 리로드: 소켓 · 레퍼런스 포즈 · 체형 · 피팅 표 · 부품 피팅 · 표면 채널 파일을 고치면 외형을 다시 조립 — 소비자(외형 컴포넌트)가 `IAssetCache` 로 올린다(로더는 다 있다). 데이터 파일 감시 → `AppearanceDatabase` 다시 읽기.
- 장비 해석: 장비(`Equipment`)를 `CharacterAppearanceState` 로 잇기, 부품 풀(스폰 대신 숨겨 둔 인스턴스), 외형 상태가 바뀌면 같은 인스턴스를 다른 소켓으로, 떨어져 나감 이벤트를 `SocketBindingComponent` ReleasedPhysics 로, 결과 해시로 병합 결과를 캐시해 같은 차림의 NPC 가 나눠 쓰고 다시 조립하는 동안 이전 외형 유지(비동기), `finishLoad` 에 몸 영역 표 이름 넘기기, 시퀀서 트랙 · 스폰 · 프리팹이 프리셋을 이름으로 가리키기 · 프리로드 세트가 프리셋 참조를 따라 모으기, 편집 창의 "지금 모습을 프리셋으로 저장" · 썸네일 렌더 · 규칙 설명(`_listTrace`) 표시.
- 나중: 천 시뮬레이션(Jolt 소프트 바디)이 같은 겹 정보를 충돌체로(Mutable 의 Clip with Mesh · Clip Deform 이 같은 문제를 푼다).
함정: 칸 점유(양손 무기)는 아직 외형만 본다 — 게임플레이에서 보조 손을 막을지는 게임이 정한다. 참고: 언리얼 Mutable(Customizable Object) · Skin Weights 전이, Character Creator 스마트 핏, Daz 오토핏.

### FeatureQueue
제목: 남은 기능 대기열(빠른 순)
옛번호: D4-9
왜 필요한가: 2026-10-04 사용자 지시로 빠른 순으로 둔 기능 대기열. 진행 방식은 [작업 방식](../11_Workflow.md) — 결함 먼저 · 겹치는 파일 없게 나누고, 병합은 준비되는 대로, 전체 검증은 큰 묶음 뒤 한 번.
착수 조건 / 남은 것: 착수하는 것부터 이름 있는 절로 떼어 낸다. **2D · 3D 에 다 쓰이는 기능은 공통 코어로**(사용자 지시). 다른 절에 있는 것(메시 LOD · PSO 미리 만들기 · 밉 스트리밍은 PreloadAndLod, 카메라는 CameraRendererIntegration, 리플레이 · 킬캠은 IntentNetworkWiring, 하늘 · 높이 안개 옛 D2-5, 천 · 머리카락은 PoweredRagdoll, DPI 실물 확인 옛 B-8)은 여기 다시 적지 않는다.
- 중간(M): 대역폭 프로파일러 · 게임플레이 디버거 · 비주얼 로거 · 모듈 패키지 관리(옛 D3-8) · HZB 가림 컬링 · 로딩 흐름의 남은 것(로딩 화면 · 페이드는 `LoadingScreenController` — 진행률(씬 매니저에 진행 질의가 없다 — 스트리밍 큐 바이트로) · 프리로드 세트 연결 · 팁 목록 데이터(`LoadingScreenSettings::_listTip` 은 있으나 gamesettings 칸 · 글 수집 규칙이 없다)) · 에셋 DCC 내보내기 · SSR · 업스케일러 · HDR 출력 · 데칼 · 2D 스켈레탈 · 학습용 몫(장르 시작 템플릿 · 튜토리얼 · API 문서).
- 큼(L): 제약 · 파티클/VFX · 에셋 레지스트리 · DDC · 증분 쿠킹 · 월드 편집 도구 · 전술 AI · 볼류메트릭 안개/빛/구름 · 캐릭터 셰이딩 · 모션 캡처 공정 · 대규모 좌표 · PCG 저작 그래프 · GI/반사 프로브 · 플랫폼 서비스 · 패치/DLC · 모드/UGC.
- 아주 큼(XL): 비주얼 스크립팅 · 월드 파티션/스트리밍/HLOD · 음성 채팅(온라인 구성은 옛 D7-5).

### ShooterCharacterIntegration
제목: Shooter3D 캐릭터 통합의 남은 것
옛번호: D4-10
왜 필요한가: KayKit 기사 플레이어 · 스켈레톤 적 · 외형 프리셋 · 입력 맵 · 탄도선 풀은 들어갔다(`Source/Games/Shooter3D/`). 리그와 게임플레이 쪽이 남았다.
남은 것:
- 리그: 왼손을 총에 붙이는 손 IK(목표는 무기 소켓 `MainHand.SupportHand`), 1인칭에서 머리만 숨기기(리그 노드 `ScaleBone` 은 있다 — 플레이어 몸에 연결하고 1인칭 화면으로 확인; 지금은 몸 전체를 숨긴다), 스프링 본(망토).
- 게임플레이: 래그돌 쓰러짐(지금은 클립), 부위 히트박스(지금은 발에서 키까지 캡슐 하나, 중심 소켓 `Chest`), 물리 질의 히트스캔 · 시야(지금 상자 목록 · 캡슐), 쓰러질 때 무기 떨어뜨리기(`SocketBindingComponent::release( Physics )` — 무기 프리팹에 강체를 더하면 된다), 알림으로 휘두름 피해 · 발소리(지금 휘두름 피해는 `_attackHitTime` 초).
- 그 밖: 외형 부품 풀(스켈레톤마다 프리팹을 세우고 지운다), 뒷걸음 · 옆걸음 발 미끄러짐([ShooterStrideWarping](#shooterstridewarping)).

### AbilitySystemExtensions
제목: 어빌리티 시스템의 다음 조각
옛번호: D4-11
왜 필요한가: 언리얼 GAS 에 있고 여기 없는 조각이다. 에디터의 런타임 상태 패널(`AbilityDebugPanel` — `GF_EditorPlugIn_Base`, `ability_debugger`, 읽기 전용, 플레이 중에만 값이 있다)은 들어갔다.
착수 조건 / 남은 것: 쓰는 게임이 생기면. 이펙트가 주는 어빌리티(장비가 스킬을 준다), 걸린 동안의 태그 조건(`OngoingTagRequirements` — 기절 중 버프 정지), 태그가 붙을 때 발동(`OwnedTagAdded` 트리거), 큐를 데이터로 이어 주는 큐 매니저(큐 태그 → 프리팹 · 사운드), 어트리뷰트를 `SaveGame` 에 싣는 도우미.
함정: 넣을 때마다 `AbilitySystemTest` 에 테스트 하나. 무적 API 는 없다(옛 D3-4).

### InteractionHighlightAndMontage
제목: 상호작용 강조 패스와 몽타주 실행 확인
옛번호: D4-12
왜 필요한가: 상호작용 · 기믹(`GameFramework/Base/Gameplay/Interaction` · `Gimmick` · `Spline` README)의 남은 두 조각이다.
남은 것:
- 렌더러: `InteractableComponent::getHighlightRequest`(Outline · Sense)를 읽는 외곽선 · 감각 모드 패스.
- 실행 확인: 상호작용 단계 몽타주가 애니메이터를 실제로 부르는지(그래프 에셋이 있는 씬). 3D 기믹 변형의 겹침은 `GimmickGenreTest.Gimmick3DTriggerVariantsReceiveOverlapsInA3DScene` 이 본다.

### IntentNetworkWiring
제목: 의도 기록 · 원격 조종자의 남은 연결
옛번호: D4-13
왜 필요한가: 입력 의도를 기록 · 재생하고 원격 폰을 조종하는 길이 서버 쪽에서 아직 이어지지 않았다. 재생 진입점(`IntentPlayback::start` · `startFromFile`)은 있다.
남은 것: `NetClientServer` 의 입력 바이트(`ReplicationClient::sendInput` 이 게임이 준 바이트를 싣는 자리)를 `ControlIntent::write` 로, 서버 폰은 `RemoteControllerComponent` 로 — 폰을 쓰는 넷 테스트 게임이 생기면(지금 0). 의도 기록을 켜고 `.swintent` 를 쓰는 명령줄 · 에디터 단추와 재생 UI · 킬캠 카메라 · 되감기는 아직 없다(호출하는 쪽의 일).

### PhotoModePauseUpdate
제목: 포토 모드 갱신을 씬 틱 밖으로
옛번호: D4-14
왜 필요한가: 에디터 Simulate 중 Pause 는 씬 틱이 멈춰 포토 모드(`Source/GameFramework/Base/Actor/Camera/PhotoModeCamera` · `CameraDirectorComponent`)가 갱신되지 않는다.
착수 조건 / 남은 것: 게임 카메라가 없는 상황이라 고치지 않았다 — 포토 모드 갱신을 씬 틱 밖으로 옮길지는 에디터 쪽 요구가 생기면.
설계 메모: 포토 모드를 켤 때 롤은 이어받고 기울이기 입력을 둔다(`PhotoModeCamera.h` 에 이미 있다). 조작은 `EngineDefaultAssets::_photoModeInputMap` 에서 읽어 게임 모듈에 읽기 전용으로 보인다(`CameraDirectorComponent.h` 에 이미 있다).

### WildWestHorseMount
제목: WildWest 승마의 말 모델
옛번호: D4-15
왜 필요한가: 말 모델이 저장소에 없어 말과 기수가 큐브 · 캡슐 자리 표시다(`Source/Games/WildWest`, 2026-10-16).
남은 것: 네발 말 glTF(걸음새 클립 넷: 걷기 · 속보 · 구보 · 질주, 안장 소켓)를 `Resource/game/wildwest/models_raw/` 에 들이고 `MountMovementComponent` 의 `_gaitParameter` · 좌석 `_socketName` 을 잇는다.
함정: KayKit · Kenney 에 말이 없다.

### GameSceneCc0Meshes
제목: 게임별 씬이 새로 들인 CC0 메시를 부르게 한다
옛번호: D4-17
왜 필요한가: 2026-10-17 `res3` 가 여섯 게임에 CC0 모델 팩(KayKit Dungeon · Space Base, Kenney Nature · Fantasy Town)의 원본을 배치했지만 씬 · 프리팹 · 코드는 아직 자리 표시 큐브를 쓴다.
남은 것: 게임마다 씬이 새 메시를 부르게 바꿔 에셋 검증 `orphans` 경고를 없앤다. 배치 목록은 `ModelImportConfig.json` 과 게임별 `credits.md`.

### MovingGroundFollowUps
제목: 움직이는 바닥 위 캐릭터의 남은 일
옛번호: 없음
왜 필요한가: `moveCharacter` 가 딛은 바닥의 속도(선속도 + 회전)를 실어 주게 됐지만(2026-10-20, 결정은 `IPhysicsCharacterScene.h` 머리말과 `JoltPhysicsSceneQuery.cpp`), 아래 셋은 하지 않았다.
남은 것: (1) 바닥이 돌 때 캐릭터의 방향(yaw)이 따라 돌지 않는다 — 위치만 돈다. (2) 점프로 바닥을 떠난 뒤의 관성이 없다 — 바닥 속도는 딛고 있는 스텝에만 얹히므로, 컴포넌트가 점프 순간의 바닥 속도를 `_airVelocity` 로 이어야 한다. (3) 네트워크 틱 동기: 서버와 클라이언트가 바닥 위치를 같은 틱에 맞춰야 캐릭터 예측이 어긋나지 않는다.
함정: 큰 Kinematic 메시(공중 요새)는 캐릭터 쓸기 충돌 비용이 크다 — 측정 전에 단순 상자로 바닥을 대신할 수 있는지 먼저 본다.

## 조건부

### InteractionSpatialGrid
제목: 상호작용 대상의 공유 공간 격자
옛번호: G-4
왜 필요한가: 지금 하는 쪽은 등록된 대상 목록을 한 번 돈다(대상 16 · 10k 오브젝트에서 +2~6 us).
착수 조건 / 남은 것: 하는 쪽 · 대상이 모두 수천일 때. 대상이 움직일 수 있어 격자를 프레임마다 맞추는 비용이 목록을 도는 비용과 같으므로, 하는 쪽이 여럿일 때만 씬당 하나(`SpatialHashGrid2D`, 셀 = 최대 상호작용 반경)를 틱 앞에 맞춘다.

### TilemapCellDataLayers
제목: 타일맵 칸 데이터를 일반 레이어로
옛번호: G-6
왜 필요한가: 지금 레이어는 0/1(`kArrTileFlagLayerInfo`)이고 워프 · 역할 · 스폰은 전용 원소다.
착수 조건 / 남은 것: 두 번째 장르가 칸마다 다른 값(지형 비용 · 발소리)을 원하면. 값 종류를 정수로 넓히고 에디터 페인트 · 형식 테스트를 같이 바꾼다(Godot TileSet custom data 모양).

### StepEncounterRules
제목: 걸음 조우 판정 둘
옛번호: G-7
왜 필요한가: Overworld `shouldEncounterOnStep` 의 결정적 주기와 ClassicJRPG `JRPGEncounterWalker` 의 확률 + 유예가 따로 있다.
착수 조건 / 남은 것: 오버월드 위에 JRPG · 몬스터 수집 게임이 서면 기반 `World/` 로 하나를 올린다.
