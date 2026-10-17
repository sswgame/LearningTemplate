# 백로그: 게임플레이, 캐릭터, 카메라, 애니메이션

GameFramework 키트와 게임, 캐릭터, 카메라, 애니메이션의 남은 일입니다. 상태 태그의 뜻, 일을 고르는 순서, 새 항목을 쓰는 법은 [백로그 안내](README.md)에 있습니다.

- `D4-2` [기능] **사용자 설정(옵션 메뉴 백엔드)의 남은 것** — 백엔드 · 바인딩 API 는 `Source/Engine/UserSettings/`, 메뉴는 `Engine/UI/Screen/OptionsMenuScreen`.
  (2) 값만 있고 읽는 곳이 없는 대상: `gv_renderScale`(업스케일 패스) · 시야 거리 · 후처리 ·
  텍스처 · 이펙트 품질 · 모션 블러 · `gv_colorVisionMode` 의 톤맵 쪽(톤맵에 상수 버퍼가 없어 미뤘다 — 함수는 `colorvision.hlsli`, UI 캔버스는 이미 쓴다) — 모두 렌더러 몫.
  카메라 세 값은 `CameraUserSettings` 가 읽는다. 머리 흔들림은 데이터(`<HeadBob>` · `FirstPersonCameraComponent::_headBob`)로 켜는데 아직 켠 게임이 없다 — Shooter3D · VoxelCraft 1인칭에 진폭을 넣고 화면으로 확인한다.
  (6) X11 `setDisplayMode`(EWMH 전체 화면)는 리눅스 실기 미확인.

- `D4-3` [기능] **패드 가상 커서(명령형 게임)를 실제로 확인하기.** 스틱 시점과 가상 커서(`InputManager::getVirtualCursor`)는 들어갔고, StarSkirmish · NileCity 는 `onInitialize` 에서 켜고
  `onShutdown` 에서 끈다(2026-10-14). 확인할 것: 그 시점에 `InputManager` 서비스가 이미 묶여 있는지(아니면 조용히 안 켜진다), 패드 A 를 누를 때 가상 커서가 마우스 왼쪽 누름도 내서 한 액션의 바인딩 두 개가
  같은 프레임에 눌리는데 디렉터가 바인딩을 따로 읽는 곳이 없는지. 두 맵에는 오른쪽 스틱 바인딩이 없다.

- `D4-4` [기능] **키트 조립 — 남은 것(2026-10-06).** 틀 · 키트 전부 · 조립 테스트 둘(`KitCompositionTest` — Farming + CreatureLife, RTS + CityBuilder)과 `MeadowVillage` 는
  들어갔다(규칙: `Source/GameFramework/Kits/` "키트 여럿을 한 게임에"). 남은 것 — 둘째 조립(RTS + City)의 테스트 게임(필요해지면 MeadowVillage 모양 — 조건부라 남김). 땅을 쓰는 게임은 아직 없다 — 격자 키트를 섞는 게임이 생기면 `SessionStateSettings::_land*` 와 키트의 `bindLand` 를 건다(SRPG 는 전투 끝에
  `releaseLand`, 공원 놀이기구 발자국은 상태 바이트에 싣지 않는다 — 지은 칸은 땅이 든다).

- `D4-5` [기능] **카메라의 남은 것.** 프리셋 · 블렌드 스택 · 디렉터 · 모드 · 다중 뷰 · 시퀀서 카메라 신호 · 렌즈 값 · 돌리 레일은 들어갔다(`Source/GameFramework/` "카메라" 절, `Source/Engine/Renderer/` "다중 뷰").
  - 직교 리그 네 게임(ThemePark · Harvest · Nile · StarSkirmish)을 디렉터 + 프리셋 XML 로 옮긴다 — 디렉터에 `setViewOverride` · `findGroundPoint` 가 생겼으니 게임 디렉터(.h/.cpp 네 개)와 씬 · 입력 데이터만 바꾸면 된다.
  - 다중 뷰의 예산 · 보임 판정이 거칠다 — 보임은 "지정한 오브젝트가 주 카메라 절두체 안" 하나(가려짐 · 화면 크기 안 봄), 예산은 프레임당 뷰 수(`gv_renderViewBudget`)지 시간이 아니다.
    화면 사각형 뷰는 에디터 GameView(ImGui 이미지)에서 검증하지 않았다.
  - 컷 프레임 신호(`RenderViewSettings::_bCut`)는 TAA 기록만 리셋한다 — 렌더러에 모션 벡터 · 자동 노출 · 모션 블러가 생기면 같은 신호로 0 · 즉시 맞춤 · 끔을 붙인다.
    대기 뷰 신호(`CameraManagerComponent::setPendingView`)를 듣는 쪽(컷 준비)은 `D4-7`.
  - 렌더러 쪽: 피사계 심도 패스, 카메라별 후처리 값(`CameraPostProcessValues` — 포즈가 나르기만 한다)을 렌더러가 읽기.
  - 게임이 디렉터 컴포넌트를 `registerStatefulComponent` 로 올려 세이브 · 핫 리로드가 켠 프리셋과 블렌드를 싣게 하는 일. 게임마다 한 줄씩 넣지 말고 `GameInstanceBase` 생성자가
    `TerrainComponent` 처럼 올리면 모든 게임에 걸린다(`Base/Foundation` 몫).

- `D4-6` [기능] **애니메이션(로드맵).** 지금 있는 것은 `Source/Engine/Animation/`(임포트 · 코덱 · 재생 · 상태 기계 · 블렌드 스페이스 · AnimationSystem · GPU 스키닝 · 2D/3D 공용 재생),
  알림 디스패치(구간 알림 · 처리기 레지스트리 · `*.notifies.xml`)는 `Source/Engine/Character/`. 남은 것 —
  ① 그래프에 레이어를 데이터로(지금 레이어는 `addLayer` 코드), 스켈레탈 애니메이터가 블렌드 스페이스 상태를 재생하는 연결(블렌드 스페이스 · 동기 그룹은 그래프 데이터다)
  ② 후처리 리그(`PoseModifierComponent`): 시퀀서 `RigSlot` 트랙(`setSlotWeight` 에 키 찍기)과 리그 디버그 창(`RigDebugPanel` — 노드 가중치 · 켜짐 · 쓰는 본 표시)은 들어갔다. 남은 것: 창에서 고친 가중치 · 켜짐은
    런타임 인스턴스의 복제본에만 들어가 저장되지 않는다(리그 에셋에 쓰는 길), 노드 대상 본 기즈모 편집, 시퀀서가 슬롯 가중치를 읽는 함수가 엔진에 없어 `readTrackValues` 는 컴포넌트가 있으면 1 을 돌려준다,
    `Engine/Sequencer` 가 `Engine/Character` 헤더를 새로 include 한다(레이어 검사 확인)
  ③ 애니메이션 LOD(`AnimationLOD.h` — 리그 노드도 화면 크기 `min_screen_size` 로 꺼진다): 메시 LOD 가 생기면 본 LOD 를 메시 LOD 와 묶기
  ④ 군중 공유(`AnimationCrowd.h`): 섞기 묶음(언리얼 Animation Sharing 의 블렌드 액터 — 지금 섞는 유닛은 사본으로 혼자 평가), Shooter3D 군중 켜기.
  얼굴(`FacialAnimationComponent`): 음소 인식 립싱크(지금은 세 대역 모양 분류 — 모음 넷 · 치찰음 정도만 가른다) · 실제 얼굴 에셋(KayKit 은 모프가 없다 — 합성 테스트 머리뿐) ·
  스킨 없는 메시의 모프(GPU 모프 풀이 스키닝 컴퓨트 안에서만 가중치를 건다) · 실시간 얼굴 입력(Live Link Face 자리)
  ⑤ 파워드 래그돌(관절 모터가 애니메이션 포즈를 쫓음 — 전신 · 부분 래그돌 · 맞음 반응 · 기상 섞기는 `RagdollComponent`), 헤어 카드 · 천(Jolt 소프트 바디 — `D1-4`), 가닥 헤어(TressFX)는 나중
  ⑥ KayKit 텍스처(원본 GLB 에 든 `knight_texture` 등)를 `textures_raw/` 로 옮겨 머티리얼을 만들 것 — 지금 캐릭터는 씬 기본 머티리얼(흰색)로 그려진다. Shooter3D 팩에는 이미 꺼낸 아틀라스(`textures_raw/kaykit_*.png`) · `materials/kaykit_*.material` 이 있다.
  ⑦ Shooter3D 발 미끄러짐 — 앞으로 걷기 · 달리기는 재생 속도 매칭(`LocomotionSpeedUtil`, 범위 0.6 ~ 1.8)으로 맞췄다. 범위를 넘는 뒷걸음 · 옆걸음은 남는다(보폭 워핑은 리그 노드가 필요하다).

- `D4-7` [기능] **프리로딩 · LOD · 사전 준비(로드맵).** ① 프리로드 세트(미리 올릴 에셋 + 미리 만들 프리팹 · 우선순위, 쿠킹 때 레벨 · 시퀀스 · 샷의 참조를 따라 자동 수집 —
  `collectReferencedPrefabPaths` 가 있다), `requestPreload` 가 진행률 · 완료를 준다, 프레임 예산(IO · 업로드 · PSO · 인스턴스 수), 참조 수 · LRU 로 내림(지금 캐시는
  약한 참조라 고정 단계가 필요하다) — 언리얼 AssetManager 번들 · Addressables ② 프리팹 풀(숨겨 둔 인스턴스를 켜고 돌려받기 — 탄 · 손님 · 유닛) · 시퀀서
  사전 스폰(프리롤 동안 캐릭터를 숨겨 만들어 첫 포즈 · LOD · 머티리얼을 준비) ③ 메시 LOD(임포트 때 `meshopt_simplify` 로 단계 생성, `.mesh` 판 올림, GPU 컬링이
  화면 크기로 고름, 디더 크로스페이드, 컷 · 대기 뷰 강제 LOD, HLOD 는 나중) ④ 컷 준비(프리웜) — 컷 순간 LOD · 텍스처 · 셰이더가 바뀌는 게 보이지 않게.
  대기 뷰 신호(`CameraManagerComponent::setPendingView`, 시퀀서 컷 트랙의 프리롤)는 있고 듣는 쪽이 없다: LOD 선택은 대기 뷰 기준 LOD 도 구해 컷 첫 프레임부터 그 LOD(컷을 넘는 LOD 크로스페이드는 끈다),
  스트리밍은 대기 뷰에서 보일 메시 · 텍스처 밉을 먼저 요청(언리얼 streaming source · AddViewLocation), PSO 는 대기 뷰 절두체 안 머티리얼의 PSO 를 미리 만들고, 그림자는 대기 뷰 기준 캐스케이드를 미리 맞춘다.
  다중 뷰 렌더로 컷 1~2 프레임 전에 대기 뷰를 저해상도로 숨겨 그려 업로드 · PSO 를 강제로 데운다. 선행 조건 셋이 아직 없다 — LOD 시스템, 밉 단위 텍스처 스트리밍(`AssetStreamingQueue` 는
  에셋 단위 비동기 읽기), PSO 를 미리 만드는 창구(DX12 PSO 생성 히치).

- `D4-8` [기능] **캐릭터 외형 편집(로드맵).** 지금: 형상 쪽(아래 ①~④ · ⑤ 의 소켓 이름 공간)은 `Source/Engine/Character` 에 있다(README "외형 조립 순서"), GPU 모프 풀(`Mesh::setGPUMorphEnabled`)은
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
  게임이 정한다 ⑥ 캐릭터 편집 창 — 첫 단계는 들어갔다(`GameFramework/PlugIn/Editor/CharacterEditor`, README): 소켓 파일 · 외형 선택 편집 모델(되돌리기 · 검증 · 좌우 대칭 · 저장)과
  패널(소켓 · 스켈레톤 · 외형 탭)과 모델이 들어갔다 — 스켈레톤 본 트리 · 레퍼런스 포즈 숫자 편집(`BonePoseEditModel`, `*.refpose.xml`) · 본 목록 → 소켓 부모 검증 · 좌우 이름 표 데이터 파일 ·
  장비 칸 콤보 · 세트 입히기 · 규칙 설명(`_listTrace`) · 프리셋 파일 저장 · 피팅 요약 표시(`FitCheckSummary`) · 자기 저장 뒤 핫 리로드 무시(`SaveReloadGuard`). 남은 것 —
  (a) 미리보기 씬 위로 옮기기: **`EditorMultiWorld` 단계 1~4 의 `EditorPreviewPanel` 이 main 에 들어온 뒤**(`multiworld2` 작업)에 `EditorPreviewPanel` 상속 · 캐릭터 스폰 · `EditorAssetType` 에 소켓 종류를 더해
  `EditorDocumentPanel` 계약으로 더티 · 저장 · 되돌리기 이관. 그때 같이 할 것: 뷰포트 기즈모 포즈 편집 · 뷰포트 소켓 축 표시 · 제약 리그 미리보기 · 애니메이션 재생 · 썸네일 렌더
  (`UserAppearancePresetStore::setThumbnailPath` 에 경로만 넣으면 된다) · 피팅 계산 연결(`CharacterEditorPanel::setFitResult` 로 푼 결과를 넘긴다 — 슬라이더를 놓을 때 한 번 풀고, 몸 메시 →
  `AppearanceGeometry` 변환은 통합 항목) · 핫 리로드 결과를 모델에 다시 반영(지금은 외부 변경 알림만). (c) 체형 · 얼굴 슬라이더 UI(`CustomizationSchema` 로 슬라이더를 만든다)는 GPU 모프 풀 통합 뒤 —
  꾸미기 값 모델 · 직렬화는 있다. 이 패널의 기본 경로는 shooter3d 데이터다(경로 칸에서 바꾼다).
  계획: docs/plans/EditorCharacterEditor.md

- `D4-9` [기능] **남은 대기열 — 빠른 순(2026-10-04 사용자 지시).** 진행 방식은 [작업 방식](../11_Workflow.md) — 결함 먼저 · 겹치는 파일 없게 나누고, 병합은 준비되는 대로, 전체 검증은 큰 묶음 뒤 한 번.
  **2D · 3D 에 다 쓰이는 기능은 공통 코어로**(사용자 지시). 다른 항목에 있는 것(메시 LOD · PSO 미리 만들기 · 밉 스트리밍 `D4-7`, 카메라 `D4-5`, 리플레이 · 킬캠 `D4-13`, 하늘 · 높이 안개 `D2-5`,
  천 · 머리카락 `D4-6`, DPI 실물 확인 `B-8`)은 여기 다시 적지 않는다.
  - **중간(M)**: 대역폭 프로파일러 · 게임플레이 디버거 · 비주얼 로거 · 모듈 패키지 관리(`D3-8`) · HZB 가림 컬링 ·
    로딩 흐름의 남은 것(로딩 화면 · 페이드는 `LoadingScreenController` — 진행률(씬 매니저에 진행 질의가 없다 — 스트리밍 큐 바이트로) · 프리로드 세트 연결 ·
    팁 목록 데이터(`LoadingScreenSettings::_listTip` 은 있으나 gamesettings 칸 · 글 수집 규칙이 없다)) · 에셋 DCC 내보내기 · SSR · 업스케일러 · HDR 출력 · 데칼 ·
    2D 스켈레탈 · 학습용 몫(장르 시작 템플릿 · 튜토리얼 · API 문서).
  - **큼(L)**: 제약 · 파티클/VFX · 에셋 레지스트리 · DDC · 증분 쿠킹 · 월드 편집 도구 · 전술 AI · 볼류메트릭 안개/빛/구름 · 캐릭터 셰이딩 · 모션 캡처 공정 · 대규모 좌표 ·
    PCG 저작 그래프 · GI/반사 프로브 · 플랫폼 서비스 · 패치/DLC · 모드/UGC.
  - **아주 큼(XL)**: 비주얼 스크립팅 · 월드 파티션/스트리밍/HLOD · 음성 채팅(온라인 구성은 `D7-5`).

- `D4-10` [기능] **Shooter3D 캐릭터 통합의 남은 것.** KayKit 기사 플레이어 · 스켈레톤 적 · 외형 프리셋 · 입력 맵 · 탄도선 풀은 들어갔다(`Source/Games/Shooter3D/`).
  리그: 왼손을 총에 붙이는 손 IK(목표는 무기 소켓 `MainHand.SupportHand`), 1인칭에서 머리만 숨기기(리그 노드 `ScaleBone` 은 있다 — 플레이어 몸에 연결하고 1인칭 화면으로 확인; 지금은 몸 전체를 숨긴다),
  스프링 본(망토). 게임플레이: 래그돌 쓰러짐(지금은 클립), 부위 히트박스(지금은 발에서 키까지 캡슐 하나, 중심 소켓 `Chest`),
  물리 질의 히트스캔 · 시야(지금 상자 목록 · 캡슐), 쓰러질 때 무기 떨어뜨리기(`SocketBindingComponent::release( Physics )` — 무기 프리팹에 강체를 더하면 된다),
  알림으로 휘두름 피해 · 발소리(지금 휘두름 피해는 `_attackHitTime` 초). 그 밖: 외형 부품 풀(스켈레톤마다 프리팹을 세우고 지운다), 1인칭에서 숨긴 몸의 애니메이션
  LOD(숨겨도 평가한다), 뒷걸음 · 옆걸음 발 미끄러짐(`D4-6` ⑦).

- `D4-11` [기능] **어빌리티 시스템의 다음 조각(쓰는 게임이 생기면).** 언리얼 GAS 에 있고 여기 없는 것: 이펙트가 주는 어빌리티(장비가 스킬을 준다), 걸린 동안의 태그 조건
  (`OngoingTagRequirements` — 기절 중 버프 정지), 태그가 붙을 때 발동(`OwnedTagAdded` 트리거), 큐를 데이터로 이어 주는 큐 매니저(큐 태그 → 프리팹 · 사운드),
  어트리뷰트를 `SaveGame` 에 싣는 도우미. 넣을 때마다 `AbilitySystemTest` 에 테스트 하나. 에디터의 런타임 상태 패널은 들어갔다(`AbilityDebugPanel` — `GF_EditorPlugIn_Base`, `ability_debugger`,
  읽기 전용, 플레이 중에만 값이 있다). 무적 API 는 없다(`D3-4`).

- `D4-12` [기능] **상호작용 · 기믹의 남은 것**(`GameFramework/Base/Gameplay/Interaction` · `Gimmick` · `Spline` README).
  - 렌더러: `InteractableComponent::getHighlightRequest`(Outline · Sense)를 읽는 외곽선 · 감각 모드 패스.
  - 실행 확인: 상호작용 단계 몽타주가 애니메이터를 실제로 부르는지(그래프 에셋이 있는 씬). 3D 기믹 변형의 겹침은 `GimmickGenreTest.Gimmick3DTriggerVariantsReceiveOverlapsInA3DScene` 이 본다.

- `D4-13` [기능] **의도 기록 · 원격 조종자의 남은 연결.** `NetClientServer` 의 입력 바이트(`ReplicationClient::sendInput` 이 게임이 준 바이트를 싣는 자리)를
  `ControlIntent::write` 로, 서버 폰은 `RemoteControllerComponent` 로 — 폰을 쓰는 넷 테스트 게임이 생기면(지금 0). 재생 진입점(`IntentPlayback::start` · `startFromFile`)은 있다.
  의도 기록을 켜고 `.swintent` 를 쓰는 명령줄 · 에디터 단추와 재생 UI · 킬캠 카메라 · 되감기는 아직 없다(호출하는 쪽의 일).

- `D4-14` [기능] **포토 모드의 남은 것**(2026-10-14, `Source/GameFramework/Base/Actor/Camera/PhotoModeCamera` · `CameraDirectorComponent`).
  에디터 Simulate 중 Pause 는 씬 틱이 멈춰 포토 모드가 갱신되지 않는다(게임 카메라가 없는 상황이라 고치지 않았다 — 포토 모드 갱신을 씬 틱 밖으로 옮길지는 에디터 쪽 요구가 생기면).

- `D4-15` [기능] **WildWest 승마의 남은 것**(2026-10-16, `Source/Games/WildWest`). 말 모델이 저장소에 없어 말과 기수가 큐브 · 캡슐 자리 표시다 — 네발 말 glTF(걸음새 클립 넷:
  걷기 · 속보 · 구보 · 질주, 안장 소켓)를 `Resource/game/wildwest/models_raw/` 에 들이고 `MountMovementComponent` 의 `_gaitParameter` · 좌석 `_socketName` 을 잇는다(KayKit · Kenney 에 말이 없다).

- `D4-16` [기능] **애니메이션 고급 기능 후보.** 모션 매칭, 전신 IK, 리타게팅 도구, 애니메이션 압축 · 샘플링 최적화.
  모션 매칭은 보류다 — 데이터(이어진 이동 모캡)와 게임 요구가 생기면 다시 본다([모션 매칭](../plans/MotionMatching.md): 조건 · 구조 초안 · 측정 질문).
  선행 구조(포즈 단위 inertialization `PoseInertializer`, 포즈를 내는 그래프 노드 `IAnimationPoseNode`)는 들어갔고, 스켈레탈 애니메이터의 포즈 노드 평가와 레이어 inertialization 연결이 남았다.
  나머지 후보는 합성 리그 벤치(`도구 - 합성 리그 생성기`)가 비용 곡선을 낸 뒤 병목이 확인되는 것부터 한다.

- `D4-17` [기능] **게임별 씬이 새로 들인 CC0 메시를 부르게 한다.** 2026-10-17 `res3` 가 여섯 게임에 CC0 모델 팩(KayKit Dungeon · Space Base, Kenney Nature · Fantasy Town)의 원본을
  배치했지만 씬 · 프리팹 · 코드는 아직 자리 표시 큐브를 쓴다. 게임마다 씬이 새 메시를 부르게 바꿔 에셋 검증 `orphans` 경고를 없앤다. 배치 목록은 `ModelImportConfig.json` 과 게임별 `credits.md`.

- `G-4` [조건부] **상호작용 대상의 공유 공간 격자**(조건: 하는 쪽 · 대상이 모두 수천 — 지금 하는 쪽은 등록된 대상 목록을 한 번 돈다, 대상 16 · 10k 오브젝트에서 +2~6 us).
  대상이 움직일 수 있어 격자를 프레임마다 맞추는 비용이 목록을 도는 비용과 같으므로, 하는 쪽이 여럿일 때만 씬당 하나(`SpatialHashGrid2D`, 셀 = 최대 상호작용 반경)를 틱 앞에 맞춘다.

- `G-6` [조건부] **타일맵 칸 데이터를 일반 레이어로**(두 번째 장르가 칸마다 다른 값 — 지형 비용 · 발소리 — 을 원하면): 지금 레이어는 0/1(`kArrTileFlagLayerInfo`)이고 워프 · 역할 · 스폰은
  전용 원소다. 값 종류를 정수로 넓히고 에디터 페인트 · 형식 테스트를 같이 바꾼다(Godot TileSet custom data 모양).

- `G-7` [조건부] **걸음 조우 판정 둘**(Overworld `shouldEncounterOnStep` 의 결정적 주기 · ClassicJRPG `JRPGEncounterWalker` 의 확률 + 유예) — 오버월드 위에 JRPG · 몬스터 수집 게임이 서면 기반 `World/` 로 하나를 올린다.

- `G-14` [조건부] **Mech · Horror 스냅샷의 열거형을 기반 타입 전체 폭(8 비트) 대신 범위 비트로 쓴다.** `MatchPhase` · `MechPilotState` · `SurvivorState` ·
  `SurvivorActivity` · `PalletState` 에는 `ENUM()` 도 `Count` 열거자도 없어 B 코덱이 8 비트를 쓴다(손 코덱은 2 ~ 3 비트였다). 하려면 다섯 열거형에 `ENUM( Count = Count )` 와
  `Count` 열거자를 더하고, 모두 다루는 `switch`(`HorrorMatchRules.cpp` 의 `SurvivorActivity` 등)와 세이브 읽기의 `<= 마지막 열거자` 검사를 함께 본다.
  `MatchPhase` 는 `Base/Gameplay/Match` 에 있어 여러 키트가 같이 바뀐다. 스냅샷 크기가 문제가 될 때(채널에 싣는 게임이 생길 때) 한다.

---

[◀ 백로그 안내](README.md)
