# 설계 백로그

> 질문: 코드에서 확인한 구조 경계 · 확장 후보는 무엇이고, 각각 어떻게 하기로 정했는가?
> 독자: 이 저장소에서 일하는 에이전트와 사용자.
> 결론: 할 일 · 측정 목록은 [NEXT](NEXT.md) 하나다. 여기는 설계 후보와 그 판단(왜 그렇게 정했는가)만 두고, 판단과 측정의 정본은 "통합 담당 판단" 절이고, 작업자에게 나눠 줄 설계(초안 · 측정 문턱)는 맨 끝 "코어 · 런타임 설계" 절이다. 끝난 일은 지운다(기록은 git).

## 열린 설계 후보

### 구조와 시스템

- **층 경계:** 엔진 층은 컴파일 의존 규칙으로 막지만 링크 산출물 경계와는 별개다. 실제 순환 · 독립 배포 요구가 확인된 층만 별도 타깃으로 나눈다.
- **`ModuleSharedAssetCache.h`** 를 여러 장르 · 온라인 카탈로그가 직접 include 하는 것은 그래프 간선이지 결함 증거가 아니다 — 쓰는 쪽이 공유 캐시 계약에 기대는지 본 뒤에만 좁힌다.
- **RHI 형식 지원:** 형식 · 용도 조합은 실제 자원 · 렌더 상태 조합으로 질의하고 미지원 경로는 백엔드별 시나리오로 본다. lavapipe `R16G16_SNORM` 블렌드 거부 기록은 코드가 블렌드 비트 없으면 덮어쓰기로 바꾸므로 옛 증상일 수 있다.

### 이름과 용어

- `naming-words` 는 함수 첫 단어 · 타입 접미사 · 금지 이름만 본다. 멤버 · 지역 · 매개변수 형태(`_camelCase` · `out…` · 단수 컨테이너)는 검사 밖이라 규칙을 단계적으로 자동 검사에 잇는다. 대량 일괄 개명은 하지 않는다.
- 일회성 구조 감사가 낸 모호한 파일 이름 24 · 폴더 5 · `Util` 없는 정적 함수 타입 464 · 첫 단어 반복 폴더 29 는 후보이지 위반 수가 아니다. 소유 시스템이 분명하면 이름을 둔다. 바꿀 때는 리플렉션 · 직렬화 · 에셋 경로 참조까지 함께.

### 구조 리팩터링 코드 대조

- 엔진 프레임 API(`Engine::update` 의 `FrameViewports`)는 렌더 구현을 직접 알지 않는다(`IEngineRenderDriver`). 서버 · 쿠커 타깃에서 이 입력이 불필요한 의존을 끄는지 include 그래프로 보고, 위반이 재현될 때만 뷰 계약을 분리한다.
- 기본 씬 수집과 `RenderExtensionRegistry` 는 입력 · 수명 · 스레드 계약이 달라 합치지 않는다. 같은 기능을 두 번 구현하는 사례가 나올 때만 공통 기여 API.
- 온라인은 `OnlineProtocolService` 가 공통 기반이다. 두 번째 서버 추상화(`OnlineSession`)를 먼저 만들지 않는다.
- 유지할 구조: 타입 기반 온라인 프로토콜, InterestReplicationPolicy 증분 가시 목록, 프로브 revision 캐시, 머티리얼 변경 감지, `IEngineRenderDriver` 경계.
- 측정 전에는 결함으로 세지 않는 것: 헤더 전파 · TU 비용, 시작 시간 단계 비중, UI · VFX 배열 복사, 프레임 경로 intern.

### 구조 점검에서 닫은 것(SOLID 점검)

모두 끝냈거나 하지 않기로 했다. 번호는 코드 주석이 가리킨다(`Core · Engine 15` 처럼). 근본 원인은 바닥 도우미가 찾기 어려운 자리에 있어 호출부가 사본을 만든 것, 층 검사가 include 글만 봐서 전방 선언 · 함수 포인터로 역류가 숨은 것이었다. 스레드 소유 점검의 기준은 [CppStyle](agents/CppStyle.md) "Thread and GPU resource ownership" 이다.

**Core · Engine**

| # | 문제 | 지금 모양 |
|---|---|---|
| 11 | Core 티어 사이 역류를 빌드가 막지 않았다 | 티어마다 빌드 부분(`Core_tier<N>_objects`)과 위 티어 헤더의 `#error` 껍데기, 티어 이하만 담은 pch |
| 13 | `BindingKind` switch 여럿 | 둔다 — 종류 값은 표 하나(`BindingKinds`)이고 남은 switch 는 `-Werror=switch` 아래라 종류를 늘리면 빠진 자리가 컴파일 오류다 |
| 14 | 큰 파일 | 크기만 보고 나누지 않는다(아래 "큰 클래스"). `MemoryProfiler` 의 누수 창만 `MemoryLeakWindow` · `LeakCurveCriteria` · `LeakWindowReport` 로 뗐다 |
| 15 | 같은 일 둘 | Wayland xdg 리스너는 `WaylandShell` 하나, MemoryTag 이름표는 `SW_MEMORY_TAG_LIST`, float3 성분 곱은 `float3::multiply`, CRC32 는 `HashUtil::computeCRC32` |
| 16 | 이름 | 호스트 창구는 `Engine/Module/HostInterfaces.h`, 콘텐츠 주소는 `Engine/Content`. 관측 넷(Diagnostics · Profiling · Observability · Telemetry)은 역할이 달라 둔다 |

**GameFramework · Editor · App** ("닫음" 은 하지 않기로 한 판단)

| # | 문제 | 판단 |
|---|---|---|
| 7 | App · Server 의 프레임 본문 두 벌 | 끝 — 겹치던 게임 걸음은 `GameModuleManager::updateGameFrame` 하나(언리얼 `FEngineLoop::Tick`). 나머지는 실행 파일마다 다른 일이다 |
| 8 | 파일 감시 넷 | 닫음 — OS 감시기 하나(`IFileWatcher::createNative`)와 거르기 하나(`FileWatchDispatcher`)를 뿌리 폴더마다 둘 뿐이다. 셰이더는 일부러 수동 재컴파일(Ctrl+F8) |
| 12 | 온라인 소도우미 복사 | 닫음 — `encodeID` 는 저장 형식이, `isTransientStatus` 는 제공자마다 뜻이 다르다 |
| 14 | Runtime 에 장르 기믹 | 닫음 — 공용 프리팹 라이브러리(`common/prefabs/gimmicks/`)를 여러 게임이 쓴다. 키트로 쪼개면 결합이 는다 |
| 16 | 큰 패널 | 닫음 — 상태는 Model 이 들고 패널은 그리기만 한다. 탭이 독립된 상태 · 수명을 가질 때만 뗀다 |
| 17 | 큰 인터페이스(`EditorAPI` · `IEditor`) | 닫음 — 호스트 ↔ 에디터 모듈 C ABI 표 하나, 호출자는 `EditorModuleManager` 뿐이다 |
| 18 | 계약 층(ModuleAPI)이 `SW_GF_API` · `SW_KIT_API` 를 알았다 | 끝 — 정의는 `GameFramework/GameFrameworkExports.h`, 계약 층의 게임 서비스 선언은 자기 빌드 정의(`SW_GAMESERVICE_API`) |

- 폴더 뜻: `GameFramework/Runtime` · `Kits/` · `Plugin/` 은 언리얼 플러그인의 `Source/Runtime` 과 같다. `Plugin/Editor/GameplayTools` 는 기반이 아니라 내용 이름이다. 자동 플레이 되돌리기는 조종 영역(`ControlSystem`)에, 소리 그래프는 그래프 문서 템플릿이 아니라 문서 기반에(링크 표가 없다).
- 큰 클래스: 크기만 보고 cpp 를 나누지 않고(같은 클래스는 한 파일) 제 상태와 규칙이 있는 묶음만 뗀다 — RTSWorld → `RTSVision` · `RTSUnitBuckets` · `FlowFieldCache`(유닛 슬롯은 순서 · LIFO 빈 칸이 결정적 결과와 저장 바이트에 들어가 `SlotHandleTable` 로 옮기지 않는다), InputMap → `InputTriggerRules` · `InputBinding.h`, AudioEngine 보이스 슬롯 → `AudioVoicePool`. 잠금 + 목록 큐는 Core `LockedSwapQueue` 하나(손 잠금이 남은 곳은 잠금 안에서 다른 상태도 만진다).
- 에디터 콘솔 명령은 기능의 명령 파일 옆에 둔다. 짝 없는 cpp 는 스스로 등록하는 단위 · 플랫폼 구현 · 모듈 진입점만 둔다.

### 데이터 기반 값 · 매직 리터럴

- 늘지 않게 하는 구조(파일 단위 래칫 `Scripts/lint/magic_literals.py`, 기준선 `Config/Rules/Lint/MagicLiteralBaseline.json`, 규칙 [Constants.md](agents/Constants.md) "The ratchet" · "Sentinels")는 섰다.
- 이미 PROPERTY 인 게임 값(`WildWestGameModeComponent` · `MeadowFarmGameModeComponent`)은 생성자 기본값만 보고 하드코딩으로 세지 않는다. sentinel 은 `-1` 표기가 아니라 도메인 의미와 저장 · 통신 호환성으로 정한다.

### App 프로세스 종료 코드

- **판단: 현 체계를 둔다.** `Config/Engine/ProcessExitCodes.json` 이 숫자의 단일 출처이고 생성기(`Scripts/generate/process_exit_codes.py`)가 C++ 열거형 · CMake 변수를 만든다. 없애면 CTest `SKIP_RETURN_CODE`(77)와 자동화 결과 분류가 숫자 하드코딩으로 돌아간다.
- 후보(낮음): 스키마에 플랫폼 종료 코드 범위 · 예약 코드 · 자동화 결과 매핑 누락 검사와 생성기 테스트.
- `Scripts/common/exit_code.py` 는 스크립트 자체의 반환 계약이라 합치지 않는다.

### 추가 아키텍처 후보

- **엔진 서비스 슬롯은 프로세스 전역 하나다**(`ServiceSlot.h` · `EngineServices.cpp`, 두 번째 엔진은 `AlreadyBound` 로 거절). 동시 Engine(에디터 다중 미리보기 · 인프로세스 서버+클라이언트)이 실제 요구될 때만 컨텍스트별 슬롯 + 두 인스턴스 격리 테스트.
- **서브시스템 의존이 두 선언이다** — 등록 `after`(생성 순서)와 `getSubsystemDependencies()`(초기화 · 종료 순서). 의미가 달라 그냥 합치지 않는다.

### 셰이더 구조와 디퍼드 · G버퍼

- 디퍼드 파이프라인(`deferredpipeline.xml` — 알베도 · 옥타헤드럴 노멀 · 금속도/거칠기 + 깊이)과 판 번호 있는 G버퍼 명세(`GBufferAttachmentSpec` v1, HLSL `SW_GBUFFER_LAYOUT_VERSION`)는 섰다. 기존 노드로 PBR 표면 조합 · 게임별 `.material` 변형 튜닝은 되지만, 게임 고유 셰이딩 모델 · G버퍼 계약을 아티스트가 단독으로 더하는 것은 토대 밖이다(노드 등록은 엔진 고정).
- 툰 · 그래프 Emissive 처럼 G버퍼에 담기지 않는 것은 `MATERIAL_FORWARD_ONLY` 가 안전한 길이다. PBR 계열을 실수로 ForwardOnly 로 두면 디퍼드 이득을 잃고, 모든 속성을 G버퍼에 넣으면 대역폭이 는다 — 속성 추가는 남는 채널 · 정밀도 검사 뒤 판을 올린다.
- **판단(2026-11-17): 박막 · 털 · 스타킹(그물 밑층) · 비단(시엔 + 비등방 · 산란)의 겹친 엽은 포워드 전용으로 둔다.** 겹친 엽 하나가 셰이딩 모델 번호 하나 + 커스텀 데이터 칸(4 채널)을 넘는다 — 박막은 두께 · IOR · 무게, 털은 탄젠트 · 큐티클 기울기 · 색, 비단은 시엔 색 · 거칠기와 비등방 축을 함께 든다. 언리얼 옛 디퍼드는 셰이딩 모델 ID + 커스텀 데이터 칸 방식이고 박막은 없으며 겹친 엽은 Substrate(가변 크기 G버퍼)가 맡는다. 유니티 HDRP 의 Hair · Fabric · StackLit 은 포워드 전용이다. 칸을 늘리면 모든 디퍼드 픽셀의 대역폭이 늘어 소수 머티리얼을 위해 전체가 값을 치른다 — HDRP 와 같이 포워드 전용(`MATERIAL_FORWARD_ONLY`, 디퍼드 파이프라인의 ForwardOnlyOpaque 패스)으로 두고, 홑 엽(시엔 · 비등방 · 산란 · 투과 · 코트)만 번호로 싣는다. 다시 볼 때: 겹친 엽 머티리얼이 화면의 큰 몫을 차지하는 장면에서 ForwardOnly 패스의 GPU 시간이 디퍼드 이득을 넘으면 Substrate 식 가변 칸을 검토한다.
- **판단(2026-11-20): 표면 모델은 머티리얼 데이터가 선언하고, 셰이더 뼈대가 조명 훅과 맞춰 본다.** `.material` 의 필수 칸 `surfaceModel`(`MaterialSurfaceModel` — Lit · Unlit · Emissive · Toon · Hair · CustomLit)이 define `MATERIAL_SURFACE_MODEL_<이름>` 을 내고, material_shader.hlsli 가 셰이더가 고른 조명 훅(DEFAULT_LIGHTING · UNLIT · EMISSIVE · CUSTOM_LIGHTING ± CUSTOM_SHADING)과 다르면 쿠킹을 멈춘다. G버퍼에 칸이 없는 모델(Toon · Hair · CustomLit)은 모델이 `MATERIAL_FORWARD_ONLY` 를 내고 `.material` 에 직접 적으면 읽기 오류다 — 툰 · 머리카락 · 그래프 Hair 마다 있던 G버퍼 `#error` 는 "셰이딩 훅 없는 조명 훅의 G버퍼 변형" 하나로 뼈대에 모였다. 표면 평가 · 패스별 출력 분리는 이미 뼈대 모양이다 — 표면 훅(`computeMaterialSurface` → `MaterialSurface`)은 패스와 무관하고 `PixelContext::storeSurface` 한 곳이 포워드 색 · G버퍼 인코딩 · 왜곡을 가른다. 상용 비교: 언리얼은 머티리얼 에셋의 Shading Model 이 `MATERIAL_SHADINGMODEL_*` 을 내고 `GetMaterialPixelParameters` 가 채운 `FMaterialPixelParameters` 를 패스(BasePass 포워드 · GBuffer 인코딩)가 함께 쓴다 — 같은 모양이고 Unlit 은 GBuffer 에 셰이딩 모델 번호로 간다. 유니티 SRP 는 `SurfaceData` · `BuiltinData` 를 만드는 함수가 하나이고 패스(`GBuffer` · `ForwardOnly`)가 인코딩을 고르며 HDRP 의 Hair · StackLit 은 ForwardOnly 다. Godot 은 셰이더 글의 `render_mode unshaded` · 사용자 `light()` 가 모델을 정한다 — 데이터와 셰이더가 갈라질 수 없지만 디퍼드가 없다. 우리 셰이더는 훅 선택이 이미 셰이더 안에 있어 Godot 처럼 셰이더만 정본으로 둘 수도 있으나, C++(배치 거르기 · 쿠킹 요청)가 셰이더를 컴파일하지 않고 포워드 전용을 알아야 해 언리얼처럼 데이터에 두고 양쪽을 맞춰 보는 쪽을 골랐다. 남은 몫: 기믹 표면(Emissive)은 G버퍼 발광 칸(판 3)이 생긴 뒤에도 `MATERIAL_FORWARD_ONLY` 를 직접 적고 있다 — 빼면 디퍼드 경로로 옮겨 픽셀이 바뀔 수 있어 골든과 함께 본다. 에디터 머티리얼 패널은 셰이더만 바꾸고 표면 모델은 바꾸지 않는다(어긋나면 미리보기 컴파일이 멈춘다).
- 포워드 · 디퍼드는 같은 장면 · 해상도 · 플랫폼에서 GPU 시간 · G버퍼 대역폭 · PSO 수 · 라이트 수 증가로 비교한다. 모바일 · 웹은 측정 전까지 포워드.
- 머티리얼 제작 UX: 게임 재료(피부 · 천 · 금속 · 젖음 · 식생) 시작 그래프, 미리보기에 대표 메시 · 조명 · 경로 선택, 누락 텍스처 · 경로 분류 · 쿠킹 상태 인라인 표시. 실제 제작 시나리오로 병목 단계를 먼저 찾는다.

### Test Framework

- 틀(필터 · 태그 · 픽스처 · 매개변수 · 시한 · 결정적 샤드 · 스냅숏 · 기대 로그 · JSON)은 충분하다 — 별도 프레임워크를 들이지 않는다. 실행기의 입구 · 출구, 새 테스트와 기준 파일의 수명, 대기 · 격리는 맨 끝 "시나리오 · 테스트 프레임워크 개편 판단" 절이 정했다.
- 샤드는 이름 FNV 해시라 긴 테스트가 몰릴 수 있다. JSON 실행 시간을 모아 병목이 실제일 때만 시간 균형형 결정적 배정. 같은 프로세스 병렬은 `TestExecutionContext` 를 케이스별로 가르기 전에는 하지 않는다.
- 게임 시나리오(Shooter3D autoplay · HarvestValley actions · StarSkirmish AI · AbilityArena AI)는 개별 동작을 본다. 장르의 완결 루프(목표 → 보상, 파종 → 판매, 생산 → 승패)가 빈 게임에만 수직 슬라이스 시나리오를 더하고, 자동 플레이는 플레이어와 같은 입력 · 서비스를 쓴다(치트 경로 금지).

### 데이터 기반 카탈로그 참조 전수 점검

카탈로그 간 id 참조는 데이터 표 한 벌(`Content/engine/settings/datacatalogreferences.xml`, `DataCatalogReferences`)을 런타임 `XMLCatalogLoader` 와 쿠커 `PackCooker` 가 함께 읽는다(S2 계약 b2c3841ac4).

**엄격도 계약(2026-11-08 — 카탈로그 공통):**

| 오류 종류 | 정책 | 보는 곳 |
|---|---|---|
| 같은 파일의 id 연결(필수) | 로드 · 쿠킹 실패 | 참조 표(Schedules · Metroidvania 까지 올렸다) |
| 다른 팩 · 코드가 채울 수 있는 연결 | 경고만(`optional="true"`) | 참조 표 |
| 코드 등록 이름(스케줄 활동 · 몸 설정 이름) | 오류, 로드 실패 | 그 카탈로그 코드(`ScheduleCatalog::validateActivityNames`) |
| 다른 파일 카탈로그로의 연결(무기 → 탄 · 아이템, 격투 기술 → 프레임) | 둘을 쥐는 쪽이 시작할 때 보고 깨지면 서지 않는다 | `WeaponCatalog::validateProjectiles` · `findUnknownItems`(Shooter3DGame · MechArenaWorld). 쿠커는 못 본다 |
| 원소 자기 칸(모르는 속성 · 범위 밖 · 겹침) | 오류 · 경고와 그 원소 제외 | 각 카탈로그 읽기(`XMLFields`), `ResourceDataSchemaTest` 는 경고도 실패 |

- `CropCatalog::fillItemCatalog` 의 아이템 자동 생성은 콘텐츠 계약이라 미검증 참조로 세지 않는다.

### ReflectionParser · 런타임 Reflection

- **파서 토큰화(측정 후보):** `SourceTokens::tokenizeCursor` 비용은 계측(`ReflectionParser.TokenizeStats`)만 들었다 — 판정은 S8. 10% 를 넘으면 토큰 저장소 재사용, 기본 인자 · 매크로 이름 · 별칭 이벤트 fixture 결과는 보존.
- **유지:** `TypeRegistry::populateLookupCaches` 사전 계산과 `TypeLookupCache` 세대 슬롯 · 조상 테이블. 캐시를 더하거나 등록 · 언로드를 바꾸면 무효화 순서와 모듈 재등록 테스트를 함께.
- **테스트:** 파서 fixture 8 개가 생성 코드 · 오류를 대조한다. 성능 최적화 전에 증분 없음 · 단일 헤더 · 다중 헤더 기준선을 같은 입력으로 반복 측정한다. 흔들리는 시간 문턱은 CI 에 넣지 않는다.
- **각 형식 `Radians`(정함, 구조 감사 6):** 각은 `Core/Math/Radians.h` 값 형식(암시 변환 없음, `fromDegrees` · `fromRadians`)이고 리플렉션 내장 표의 `packed` 줄이라 XML 글 · 인스펙터 · 콘솔 숫자 · `Min`/`Max` 는 도, 메모리 · 이진은 라디안이다(Godot `radians_as_degrees` 대조 — 언리얼 `FRotator` 는 도로 담지만 계산이 라디안이라 Godot 쪽). 데이터에서 도 · 라디안이 섞이던 칸(무기 퍼짐 · 반동, 리그 관절 제한 · 조준 · 발 정렬 · 포즈 구동, 지글, 짝 애니메이션 요 · 방위각, 킷 카탈로그의 `Units = "rad"` 칸)을 옮겼다. 이어 `Units = rad` 이던 `float32` 칸 약 70 개(카메라 · 빛 · 물리 · 환경 · UI · 게임)도 옮겼고 시퀀서 · UI 애니메이션 보간과 원뿔 모양 손잡이가 Radians 를 받는다. 남은 몫: 벡터 각(`float2` 경사 띠 · `float3` 오일러, `Units = rad` · `deg`)은 회전 형식을 따로 정할 때 옮기고, 도 영역으로 양자화하는 `SkinLayerValue::_rotationDegrees` 는 이름이 단위를 말하는 채로 둔다. 접근자(`getFieldOfViewY` 등)와 런타임 서술자(`PhysicsDesc` · `CameraPose`)는 라디안 float32 다.
- **FUNCTION 리플렉션:** `CallInEditor` 계약은 섰다(a02f95478d — 주인 편집 하나로 감싸 Undo 한 단계 · dirty). `callWithText` · `bindEventToFunction` 의 콘솔 · 비주얼 배선은 수요가 생길 때만 허용 목록 · 수명 · 핫 리로드 검증과 함께. `bindEventToFunction` 소비자는 지금 테스트뿐이다.
- **FUNCTION 인자 · 오류 · 액션 목록(정함, NEXT 5-4):** 인스펙터 호출은 한 길 `ReflectionInvoke::callByName` 이다 — 인자 칸(내장 표 `ReflectBuiltins.xxx` 의 Widget 칸 위젯, 프로퍼티와 같은 위젯)의 값을 `ReflectedValue` 로 넘기고, 결과 갈래(`ReflectCallResult`) · 반환 글 · 성공 수/대상 수를 메서드 목록 위에 보인다. 전에는 `TypeRegistry::invokeMethod` 가 없는 함수 · 틀린 인자를 빈 값으로 삼켰다(지웠다). 실행 취소는 대조해 정했다: 언리얼 Details 의 `CallInEditor` 단추는 호출마다 `FScopedTransaction` 을 열고 선택 객체를 `Modify` 한다, 유니티 `[ContextMenu]` · Odin `[Button]` 은 함수 본문이 `Undo.RecordObject` 를 부른다, Godot `@export_tool_button` 은 본문이 `EditorUndoRedoManager` 를 쓴다. 우리는 언리얼 쪽이다 — 모든 호출을 주인 오브젝트 스냅샷 편집 하나로 감싸 바뀐 것이 있을 때만 되돌리기 한 단계를 쌓는다(플레이 중에는 기록 없음). 그래서 `Meta="Transactional"` 같은 선언은 두지 않는다: 빠뜨릴 수 있는 선언 대신 기본이 기록이고, "선언 없는 변경 호출" 이라는 틈이 없다. 한계 — 주인 오브젝트 밖(다른 오브젝트 · 에셋)을 바꾸는 호출은 그 몫이 기록되지 않는다. 그런 함수가 생기면 대상을 인자(핸들)로 드러내 편집 명령을 타게 한다. 다중 선택은 같은 타입 컴포넌트 전부에 한 거래(되돌리기 한 단계)로 부른다(언리얼 Details 와 같다). 반복 작업용 액션 목록은 메서드 줄의 별(즐겨찾기)이 그 메서드를 목록 맨 위 즐겨찾기 구역으로 올리고 에디터 상태 폴더 `InspectorActionFavorites.txt` 에 저장한다 — 프로퍼티 즐겨찾기와 파일을 나눈다(섞으면 메서드만 즐겨찾은 타입에 빈 프로퍼티 즐겨찾기 구역이 선다). 인자 값은 세션 동안만 남고, 열거형 · 구조체 인자는 위젯이 없어 Invoke 가 꺼진다. 다시 볼 조건: 같은 인자 묶음을 되풀이하는 수요가 보이면 즐겨찾기에 인자 글(`ReflectedValue::toText` ↔ 인자 변환)을 함께 담는다.

### 비동기 · 병렬 처리

기준(사용자 2026-11-08): 측정 숫자로 순서를 정하지 않고 연쇄 모양으로 고른다 — ① N 에 비례하고 서로 독립 ② 앞단이 뒤를 막는 사슬 ③ N² 를 미리 병렬 · 비동기로. 확인은 목표 규모 부하 시나리오(`ctest -L load`), 숫자는 회귀 감시(언리얼 Mass · 유니티 DOTS 잡과 같은 방향). 도구 고르기는 [CppStyle](agents/CppStyle.md) "Parallel and async — which tool where", 기존 `TaskManager` · `runParallel(ByCost)` 를 쓰고 새 풀을 만들지 않는다.

| 자리 | 판단 |
|---|---|
| 씬 저장(`SceneManager::writeActiveScene`) | 단계 시간 계측은 들었다. 스냅숏(`serialize`)은 게임 스레드, `base` · `write` 는 독립 값(`SceneDocument`)만 읽어 워커로 옮길 수 있다 — 판정은 S8. 성공 전에 source path · dirty 를 완료 처리하지 않고 낡은 스냅숏을 버전으로 가린다 |
| 씬 로드(`loadSceneAsyncJob`) | 파싱 · 인스턴스화는 이미 워커. `instantiate` 가 우세할 때만 순수 전처리(상태 디코드 · 검사)를 병렬로, 생성 · 부착 · 등록은 소유 경계 유지 |
| 씬 쿠킹(`SceneCooker::cookAllScenes`) | 씬 쿠킹 시간 합 · 가장 느린 씬 계측은 들었다. 합이 쿠킹 전체를 지배할 때만 — `CookStore` 공유 통계 · `clearAssetCaches` 때문에 캐시 격리 · 결과 병합이 먼저다(루프만 태스크로 감싸지 않는다) |
| 게임 스레드 에셋 로드 | `AssetLoadProfiler` 로 스톨 상위 에셋 · 단계(IO · decode · upload)를 찾아 지연 로드를 허용하는 경로만 비동기로 |
| 렌더 패스 기록(`RenderGraph::executeParallel`) | 레벨 폭 계측은 들었다 — 폭 문턱 판정은 S8. 배리어 · 레벨 제출 순서는 그래프 소유 |
| AI 이동 사슬 | 비동기 경로 요청 줄 · 군중 프레임 일은 섰다. 한 종류 군중은 `load/navcrowd` 가 문턱을 넘을 때만 지역으로 쪼갠다 |
| 에디터 UI | MVVM 까지(스레드 분리는 하지 않음 — 아래 "에디터 UI 스레드 분리 검토") |
| 병렬 틱의 남 읽기(구조 감사 2) | const 매니저는 쓰기만 막는다. 틱 디스패치가 스레드 표지에 병렬 그룹을 걸고, 같은 그룹에서 틱하는 남의 오브젝트 컴포넌트를 `getComponent` · `resolveComponent` 로 찾으면 검사 구성에서 단언(`GameObject::assertParallelTickRead`, 배포 0 비용). 남 읽기는 틱 중에 바뀌지 않는 형식(`Component::kTickReadSafe` — 트랜스폼 · 디렉터)만 받는 `TickContext::readOther` 하나 — 유니티 잡 안전 시스템 `[ReadOnly]`, 언리얼 Mass 읽기 요구 선언 · 틱 그룹 장벽과 같다. 같은 그룹의 선행 조건(스테이지)으로 순서를 잡은 읽기는 그룹 단위 판정이라 걸린다 — 시나리오에서 걸리면 `readOther` · 틱 뒤 작업 · 그룹 옮기기로 고친다 |
| 유지(이미 병렬) | 컴포넌트 틱 fork-join, 변환 계층, 애니메이션 비용 기반, 프레임 시스템 병렬(runOnAnyThread), GPU 업로드 · 식생, 셰이더 쿠킹, PackWriter 항목 압축. 틱 비용 기반 분배는 8 게임 중 1 만 이득이라 기본 정책을 바꾸지 않는다 |

**씬 서비스 · 자체 스레드 · 폴링 판정(2026-11-08):** `MeshLODSystem` · `TextureStreamingSystem` · `SceneAudio` 는 `runParallelByCost`(공유 쓰기는 게임 스레드가 목록 순서로, 오디오 가림 레이캐스트는 물리 한 스레드 계약이라 직렬 16 개 상한). 식생 레이어는 병렬 for. `SceneVFXProvider` · `NetObjectTable` · `SceneNetLink` 는 그대로. 프레임 · 틱 맞춤, GPU 펜스, 소켓 · 망, 프로세스 대기, 감시 · 크래시, 재시도 백오프, 종료 한 번의 완료 거두기의 잠자기는 시간을 재야 하는 자리라 그대로 둔다. `RenderGraph` 레벨 스테이지 · `AudioClip` 디코드 · 스크린샷 쓰기 · `AsyncFileIO` 는 이미 연쇄다.

### 렌더 스레드와 업로드 전용 스레드

- **판단: 전용 업로드 스레드는 넣지 않는다.** 언리얼에서 렌더 스레드 밖의 대표 스레드는 업로드 스레드가 아니라 RHI Thread(플랫폼 중립 명령 → API 실행, 제출 순서 보존)이고 텍스처 스트리밍은 별개 시스템이다.
- 메시 GPU 생성은 `GPUUploadQueue::flush` 가 `runParallel` 로 나누고 GT 가 기다린다(`GT.GPUUpload.flush`) — 보이는 메시는 그 프레임에 있다는 계약 때문이다. 대기를 없애려면 계약(선요청 · 준비 전 제외)을 바꿔야 한다.
- 텍스처는 IO → 디코드(로드 작업) → 렌더 스레드 생성 · 제출(프레임 예산) → 티켓 폴링이 이미 있다. 업로드 계측(`AssetLoadScope` `Texture` · `TextureUpload`, `RT.TextureUpload.pump` · 제출 · 폴링 · 대기 수)은 들었다 — 판정은 S8. 스트리밍 구조 결함(감사 R-6)은 됐다 — 번호 고정 · 밉 범위 대기 줄(w-rhi e7275a2d50 외).
- DX12 · Vulkan 전송은 그래픽스 큐로 제출된다(별도 copy queue 없음). CPU 제출을 옮겨도 GPU 복사 시간은 줄지 않는다 — copy queue 는 중첩 이득 측정이 있을 때만.
- RT 의 RHI 실행이 병목이면(S8: pump 가 RT 의 10% 초과) 업로드 스레드가 아니라 명령 순서 · CPU 소유권을 보존하는 RHI Thread 형 시제품을 검토한다.

### 에디터 UI 스레드 분리 검토

**결정(사용자 2026-11-18): 스레드는 나누지 않고 MVVM 까지 한다.** 언리얼 Slate · 유니티 에디터처럼 UI 는 게임 스레드에서 정해진 순서로 돈다. UI 는 스냅샷만 읽고 명령 · 값 묶음만 쓰며(쓰기 열쇠가 형식으로 막는다), Model 은 ImGui · 창 없이 `App --test` 로 검증한다 — 이 이득은 스레드 없이도 남는다. 창 · 메시지 펌프 이전, 락스텝 UI 스레드, 나란히 돌기(한 프레임 지연)는 하지 않는다: 정본이 아니고, Win32 · X11 · Wayland · IME · 대화상자 · 웹 예외와 새 결함 종류를 들인다. 끊김 중 UI 응답은 정본 방식(비동기 로딩 · 컴파일 · 베이크, 비활성 시 에디터 틱 낮추기, 렌더 스레드)으로 얻는다. 다시 열 조건: 무거운 씬에서 정본 방식으로도 UI 응답이 기준을 넘는다는 측정값. 다른 UI 프레임워크(Avalonia 등)로의 전환은 나중 일이다 — View · 접착층만 다시 쓰도록, 그때 C API 경계와 스냅샷 변경 알림을 먼저 세운다. 아래 단계 기록 중 4 단계 이후(UI 스레드)는 이 결정으로 닫혔다.

목표(사용자 2026-11-08)는 렌더가 아니라 게임(메인) 스레드와의 분리다 — 게임이 멈춰도 에디터가 반응하고, UI 비용이 게임 프레임을 늦추지 않게(언리얼 Slate · 유니티 · Godot 에디터는 게임 스레드에서 돈다 — 그보다 앞서는 방향). UI → 렌더는 이미 `EditorDrawDataSnapshot` · `DrawSnapshotHandoff` 로 겹친다(유지). 막는 것은 동기 입력 포착(`App::onWindowMessage` → `ImGuiEditor::processEvent`), 라이브 씬 편집(`HierarchyPanel` 이 `GameObjectManager` 를 직접), UI 프레임이 확정하는 뷰 타깃 · 틱 여부다. 측정을 기다리지 않고 선결 구조부터 넣는다:

1. **에디터 MVVM**(계약 정본 [EditorUI.md](agents/EditorUI.md) "MVVM" 절. `Panels` View 는 다 옮겼고 빌드가 막는다. 껍데기는 표 `[panelViewBan] views` 의 경로를 티어마다(`EditorTier<N>MVVMView`) 받는다 — 메뉴 바 · 플레이 툴바 · 문서 · 미리보기 패널 바탕과 미리보기 뷰포트의 그리기, `Common/Input` 팝업 셋, `AssetActions`(썸네일 그리기 `EditorAssetThumbnailRenderer` 만 View, 종류 등록 · 열기 · 놓기는 `AssetActions/Model`)가 들었다(모델 쪽은 `Common/GUI/Model` · `Panel/Model`). 확장 화면 도구 둘도 같은 모양이다 — 리그 대상 기즈모(모으기 · 적용 `RigTargetEdit`, 명령 `RigTargetGizmoCommand`)와 MeshModifiers 손잡이(`Model/MeshModifierHandleEdit`, 판정 `MeshModifierHandleViewState::pick`). 미리보기 창 바탕은 막대가 값만 고치고 씬 만들기 · 초점 · 바닥 · 시간 배율은 Model(`preparePreviewFrame` · `applyPreviewBar`)이 건다 — 미리보기 씬은 창이 쥔 씬이다(언리얼 `FPreviewScene` 을 에셋 편집기가 든다). 남은 것: `Common/GUI` 의 나머지(상태 표시 · 도킹 레이아웃 · 테마). 뷰포트 클라이언트(`EditorViewportClientScene` · 카메라 값 `EditorViewportFrame`) · 피커(`EditorViewportPick`, 판정은 그리기가 남긴 화면 배지 목록) · 빌보드(모으기 `BillboardVisualizer` + `drawAll` 배지) · 놓기(`EditorPlacementDrag`) · 콜라이더 손잡이(`EditorColliderHandleEdit`) · 지형 붓(`TerrainBrushEdit`)은 기즈모와 같은 모양으로 갈라 `views` 에 들었다. 기즈모 · 미리보기 기즈모: View 가 스냅샷(`EditorGizmoViewState`)만 읽고 드래그 중간 값을 들며 명령(`EditorGizmoCommand`)을 내고, Model(`EditorGizmoEdit`)이 그리기 뒤 메인 스레드에서 적용 · 기록한다. 시각화는 모으기(`Viewport/Model/Visualizers`, 도형 값 `EditorVisualizerDrawList`)와 그리기(`EditorViewportVisualizer::drawAll`)로 갈랐고 화면 도구는 등록 줄의 `_pTool` 로 남았다. 카메라 · 투영 · 오버레이 · 캔버스 · 입력 오버레이(값 묶음 `ViewportInputOverlayState`) · 툴바 설정(값 `ViewportToolbarState`) · 디버그 HUD(모델 `EditorDebugHUDViewModel`) 그리기는 들었다. Character Editor 본창은 미리보기 값(`PreviewViewState`, 모으기는 `Model/CharacterEditorPanelPreview.cpp`)을 받아 `PANEL_VIEW_SOURCES` 에 들었다. 확장 패널 View 열셋은 `PANEL_VIEW_SOURCES` 로 껍데기를 받고(Housing 은 패널이 없다), 인스펙터 확장 · Add Component 목록은 값 · 명령 API 다(2026-11-18). 확장 시각화는 모으기로 옮겼고 `_pTool` 는 화면 도구(콜라이더 손잡이 · 지형 붓 · 리그 대상 기즈모 · MeshModifiers 손잡이 — 모두 값만 그리고 명령을 낸다)만 쓴다 — 언리얼 컴포넌트 비주얼라이저도 그리기와 입력 처리를 한 등록에 둔다) — ViewModel `gather`(메인 스레드)가 값만 담은 ViewState 를 만들고 View 는 그것만 읽으며, 의도는 명령 큐로 메인 스레드가 프레임 시작에 실행한다. View 가 모델 · 씬 헤더를 include 하면 빌드가 멈춘다. 문서 패널 View 의 남은 강제 범위는 감사 E-16.
2. **입력 큐** — 창과 ImGui 는 UI 스레드가 소유하고 게임 뷰 입력은 프레임 번호가 붙은 큐로(게임 입력 한 프레임 지연, 자동화 가상 입력도 같은 큐). 큐는 `InputManager` 의 원시 이벤트 큐다 — 칸마다 입력 프레임 번호(`postRawEventAt` · `getCollectingInputFrame`), `beginFrame` 은 그 번호까지 넣은 순서대로 적용하고 뒤 번호는 남긴다. 가상 입력은 같은 적용 목록을 지난다(`TestInputFrameQueue`). **판단(2026-11-18): 창 메시지를 날것으로 미뤄 ImGui 가 큐에서 읽게 하지 않는다** — Win32 수정 키(`GetKeyState`) · `WM_INPUT` 핸들 · IME 조합 · 마우스 붙잡기는 받는 스레드에서 메시지 시점에만 맞고, ImGui 보조 뷰포트 창은 메시지를 ImGui 에 바로 넣어 미룬 주 창 사건과 순서가 뒤집힌다. 그래서 번역(ImGui 사건 · `RawInputEvent`)은 창을 가진 스레드에서 받을 때 하고, 경계는 UI → 게임이다(언리얼도 받은 스레드에서 수정 키를 값으로 떠 미룬다). 남은 것(3 과 함께): 창 소유 · 메시지 펌프를 UI 스레드로, `InputManager` 의 번역 쪽 상태(잠금 · 포커스 · 삼킨 버튼 · 서로게이트)를 적용 쪽과 가르기, 라우팅이 읽는 `EditorPlaySession::isSimulating` 을 UI 가 가진 값으로.
3. **출력 계약** — UI 가 엔진 상태를 바꾸는 의도는 `EditorUICommandQueue`(값만 든 `EditorUICommand`)로 보낸다: 뷰 렌더 타깃 크기(`EditorViewSurface::fitTargetSize`)와 Play/Simulate/Pause/Resume/Step/Stop(툴바 모델 · 단축키 Play · Console · Profiler · Animation Rewind). **UI 프레임 N 의 명령은 게임 틱 N+1 시작에 넣은 순서대로 적용된다 — 예전 직접 변경과 지연이 같다.** 호스트 프레임은 게임 모듈 갱신 → UI 프레임 → 씬 틱 순이고 호스트가 UI 프레임 뒤 · 씬 틱과 틱 여부 질의 앞에서 `EditorAPI::applyUICommands`(게임 스레드)를 부른다 — 입력 큐와 같은 프레임 번호 칸(`postAt` · `takeDue`), 언리얼 Slate 가 게임 스레드에 명령을 넘기는 자리. 계약 테스트 `TestEditorUICommandQueue`. 종료(모듈 리로드 · RHI 교체)는 남은 명령을 버린다. 게임 스레드 쪽(콘솔 `play` · `step`, 자동화 단계 초기화, 호스트 `stopSimulation`)은 바로 바꾼다 — 세션 쓰기 함수는 게임 스레드 열쇠(`EditorPlaySessionWriteKey`, 그 입구만 만든다)를 받아 ViewModel 이 바로 부를 수 없다(계약 테스트 `TestEditorPlaySessionWriteKey`). Eject/Possess · Keep Simulation Changes 도 명령이다(`ToggleEject` 는 세션 칸과 패널 포커스를 한 적용이 바꾼다). UI → 게임 값(그린 뷰와 자리 · 미리보기 눈 · 게임 뷰 초점 · UI 쪽 다시 그릴 까닭)은 UI 프레임 끝에 값 묶음 하나로 넘기고(`EditorUIFrameOutput` · `EditorRenderViews::publishUIFrame`) 게임 스레드는 그 사본만 읽는다(계약 테스트 `TestEditorUIFrameOutput`) — 두 방향 모두 값만 건너간다. UI 프레임 안에서 엔진을 바로 고치던 길도 옮겼다: 뷰 · 미리보기 렌더 타깃은 게임 스레드가 가지고(`applyReadyAndPublishTargets` → 다음 UI 프레임이 `EditorTargetSnapshot` 을 받는다, 만들기 · 크기 · 놓기는 `FitViewTarget` · `FitPreviewTarget` · `FreePreviewTarget` 명령), 키보드 주인은 묶음 값(`_bEditorOwnsKeyboard` → `EditorInputActions::applyKeyboardOwner`), 플레이 배치(Maximize On Play · Eject 포커스)는 UI 가 세션 전이를 보고 정하고(`EditorPlayLayoutState`), 게임 스레드 일은 게임 단계(`SW_EDITOR_GAME_STEP` — `applyUICommands` 끝, 세션 열쇠를 받는다)가 한다: 씬 편집 명령(`EditorCommandRegistry::request` 가 넘긴 것), 베이크(시작은 `requestStart` 요청, 편집 막기는 `EditorSceneEditHold` RAII), 에셋 감시 · 리로드. 그 뒤로 옮긴 것: UI 의 플레이 상태 읽기는 스냅샷(`EditorPlaySession::getUIState` — 게임이 `publishState`), 씬 열기 요청은 게임 단계 `sceneSession`, 자동 저장은 게임 단계, 계층 창 · 인스펙터의 오브젝트 · 컴포넌트 쓰기는 `EditorCommandRegistry::requestSceneEdit`(id · 값만 붙잡는 거래 람다). `ImGuiRenderer::registerTexture` · 해제 큐는 이미 잠금이 지켜 게임 스레드도 부른다(계약 주석). UI 프레임 안의 직접 쓰기도 명령 · 스냅샷으로 옮겼다(MVVM 경계 — 스레드는 나누지 않는다, 위 결정). 프로퍼티 칸 편집: 그리드 모델이 행 번호 명령을 `PropertyEditRequest`(깊이 0 프로퍼티 + 필드 · 원소 · 맵 항목 경로, 새 값)로 바꿔 게임 단계에 넘기고, 끄는 동안 프레임마다 적용해 바로 보이며 되돌리기는 마감 요청 하나 — 언리얼 `InteractiveChange` 뒤 한 트랜잭션. 놓을 때 한 번만 넘기면 끄는 동안 뷰포트가 멈춰 보여 그 모양은 버렸다. 선택은 되돌리기 기록 밖이다 — 되돌리기 알림은 id 만 `EditorSelectionUpdateQueue` 에 남기고 UI 가 입힌다, Godot 와 같다. 인스펙터 메서드 호출 · 이벤트 방송도 요청이고, 확장의 쓰기 길은 `InspectorComponentEdit::requestComponentEdit` 하나다 — `findComponent` 가 const 만 돌려준다. 한 호스트 프레임은 준비(Model 이 UI 가 읽을 값을 낸다) → UI 프레임 → 명령 적용 · 게임 단계 → 씬 틱이 한 스레드에서 정해진 순서로 돈다 — UI 가 낸 요청은 같은 프레임의 게임 단계가 실행하고, UI 가 읽는 값 묶음은 UI 바로 앞에서 나므로 늦는 프레임이 없다(계약 테스트 `EditorUIFrameOrderTest`). 옮긴 것: 파일 대화상자 결과는 요청(씬 Save As 는 `EditorWorkspace::requestSaveSceneAs` → 게임 단계 `sceneSession`, 컴포넌트 프리셋은 `requestSceneEdit`), 자동 저장 복원은 `EditorAutosaveCommands::requestRestore`(사본 id) → 게임 단계 `tick`, Input Map Editor 패널은 장치 값 묶음(`InputMapDeviceState`)과 모델 명령만 쓴다, 메뉴바 · 명령 단추의 켜짐은 UI 프레임 직전에 Model 이 낸 값 묶음(`EditorCommandRegistry::publishEnabledState` · `isEnabledInSnapshot` — 언리얼 `FUICommandList` CanExecute 자리), 맥락 메뉴의 켜짐은 Model 이 대상마다 모은 값(`gatherContextMenu` · `EditorContextMenuState`), 미저장 확인 선택은 요청(`EditorWorkspace::requestUnsavedSceneChoice` → 게임 단계 `sceneSession`), 에디터 단축키의 맵 갱신 · 발화 판정은 Model `EditorShortcutMap`(어댑터 `EditorInputActions` 는 ImGui 키 넘기기 · 포커스 · 키보드 주인만), View 는 입력 매니저 헤더를 볼 수 없다(`[panelViewBan]` 의 `^Engine/Input/InputManager[.]h` — 열거는 잎 헤더 `InputGlyphStyle.h` · `InputKeyboardFocus.h`).

### 데이터 구동 구조 후보 판별

기준: 숫자 리터럴이 아니라 **내용 · 정책을 코드가 정하는 모양**을, 내용이 늘어날 연쇄 기준으로 고른다. 형식 · 프로토콜 · 수학 표(RHI 형식 · DDS · DSP 계수 · HTTP/2)는 명세라 코드에 둔다.

- **1 게임 AI(옮김):** `AIControllerComponent` 의 `think` = 감각(`updateBlackboard`) → 트리 한 틱이고 덮어쓸 수 없다. 자동 플레이 · 적 AI 일곱을 `*.bt.xml` 로 옮겼다(언리얼 `RunBehaviorTree` 자리). 코드에 두는 것: `KartAI`(연속 조향 수학), `SRPGCommanderAI`(효용 점수 — 갈래 둘), `ShooterEnemyComponent` · `SkirmishUnitComponent` · `FarmVillagerComponent`(AI 판단이 아닌 몸 · 표시 상태).
- **2 프리셋 · 규칙 표:** `FractureSettings` 철자 표(원본 해시 형식) · `TextureImportConfig` DXGI 번호(명세) · `Autosave` 까닭 우선순위(의미 규칙)는 코드, 나머지는 이미 데이터다.
- **3 문화권:** 복수형 · 서수는 CLDR 규칙 글(`PluralRuleSet`), 숫자 체계 · 통화는 문화권 표 칸. 남은 것: 문화권별 통화 기호(필요할 때 칸 추가).
- **4 에디터 표시:** 입력 오버레이 · 강조 스와치는 테마 토큰. 오버레이 바 표는 바마다 내용 코드가 붙어 코드.

### 에디터 기능 · UX

- 단축키 · 명령 팔레트 · Quick Launcher · 다중 선택 인스펙터 · Undo History · 명명 레이아웃 · Map Check · 참조 찾기 · 경로 자동 수정 · 비동기 임포트 · 자동 저장은 이미 있다 — 새로 만들지 않고 Undo 누락 · 취소 불가 편집을 시나리오로 찾아 보강한다(되돌리기 입구 결함은 감사 E-1 · E-2 · E-13).
- **첫 사용(중간):** 빈 상태(새 프로젝트 · 빈 폴더 · 첫 임포트 · Map Check 오류 · 첫 패키징)에 다음 행동을 잇는 것부터. Welcome 화면은 필요가 확인될 때(Godot Project Manager 비교).
- **시각 저작(높음):** 범용 시각 편집 프레임워크를 만들지 않고 데이터 의미로 고른다 — 위치 · 부착은 뷰포트 · 기즈모, 계층 · 범위는 트리 · 오버레이, 시간은 타임라인, 흐름은 노드 그래프, 조합 · 카탈로그는 표 + 미리보기. 공통 재료는 `EditorPreviewPanel` · `EditorPreviewGizmo` · `EditorTransaction` · `EditorNodeGraph`, 같은 결합이 반복될 때만 공용 헬퍼를 뽑는다.
- 경계: 편집 UI 는 저장될 에셋을 바꾸고 미리보기는 그 에셋을 소비한다(미리보기 전용 사본 금지). 선택 · 문서가 바뀌면 끌기를 끝내고 빠른 끌기는 Undo 한 단계, 사라진 본 · 소켓 참조는 조용히 버리지 않고 표시. 에셋 값의 기즈모 조작은 데이터 로컬 공간 · Undo 병합 · 문서 dirty 를 그 모델에 잇는다. 첫 사례(애니메이션 마스크 · 소켓)로 공통 코드가 실제로 있는지 본다 — 완료는 XML 왕복 · 다른 스켈레톤 · 재로드 배치 일치 · `uiaudit` · `uicapture` 화면.
- 타일맵 · 스프라이트 시트의 자동 투명 영역 · 회전 아틀라스는 콘텐츠가 요구할 때 런타임 형식과 함께. 볼륨 안개는 `FogComponent` 씬 값이 정본이다.
- 제외: 코드 에디터 · 마켓플레이스 · 클라우드 협업 · 원격 빌드 · AI 검색 · 멀티 유저 편집(비용 · 운영 책임을 먼저 정한다).

### 키트별 무료 상업용 리소스 (CC0 기준)

- 키트마다 대표 동작을 보이는 최소 샘플만 둔다(범용 에셋을 모든 키트에 복사하지 않는다). 먼저 각 게임 `credits.md` · 공용 출처 기록을 키트별로 이어 중복을 막고, 부족한 것만 고른다.
- **허용 출처:** Kenney(asset pages CC0 — 묶음의 `License.txt` 보관), Poly Haven(CC0), ambientCG(CC0), 이미 쓰는 KayKit · 오디오 팩은 `Content/common/credits.md` · `audio.sources.json` 기록 그대로(같은 제작자라고 새 팩 라이선스를 추정하지 않는다). OpenGameArt 같은 업로드 사이트는 항목별 CC0 확인만. Quaternius QAL(원본 단독 재배포 금지) · CC-BY · NC · ND · 불명확한 것은 들이지 않는다(기존 묶음 여덟의 판단은 아래 "리소스 라이선스").
- **추가 전 기록:** 쓰는 파일과 키트 id, 원본 페이지 · 라이선스 원문, 원본 SHA-256 · 받은 날짜, 변환 경로 · 옵션, 재배포 · 수정 허용. CC0 도 상표 · 초상 · 유명 IP 는 따로 뺀다. 원본은 쓰는 파일만 Git LFS, 리소스 구조는 [ExistingFacilities](agents/ExistingFacilities.md) "리소스 읽기 입구와 에셋 참조". "무료" 에셋을 자동으로 받는 도구는 만들지 않는다.
- 네트워크 · 온라인 · 저장소 키트는 이미지보다 합성 fixture(입력 · 시드 · 예상 상태, 가짜 사용자 · 상품 데이터)를 쓰고 실제 공급자 로고 · 사용자 자료를 쓰지 않는다.

### GameFramework

- **`GameInstance` 의 조정 책임 집중(중간):** 팩 설정 · 초기화 순서 · 클라이언트 훅 · 넷 세션 · 저장 · 자동 저장 · 오디오 · 프레임 갱신을 한 곳이 조정한다. 기동 실패 되감기 · 프로세스당 하나는 S2 계약(e6933d4ab1). 수명 · 호스트 · 실패 정책이 다른 책임만 나누고, `update` 순서는 계약 테스트로 고정한다. 여러 인스턴스가 요구되면 thread-local 이 아니라 명시적 게임 컨텍스트.
- 유지: 키트 의존 · 모듈 종류는 `*.module.json` 매니페스트가 정본이고 `ModuleManifest.cmake` · `ModuleTargets.cmake` 가 순환 · 링크를 검사한다 — 다른 레지스트리로 복제하지 않는다. 키트 전체의 API 일관성 · 데이터 판 정책은 감사하지 않았다.

### 무기와 발사체 조합(정함)

무기 N 종과 발사체 M 종이 파일 N + M 개로 조합된다(`GameFramework/Runtime/Character/Combat/Projectile/`, 언리얼 Lyra 무기 조각 · 유니티 ScriptableObject 탄 정의와 같은 방향).

| 자리 | 형식 |
|---|---|
| 탄 | `ProjectileDef` · `ProjectileCatalog`(`data/projectiles.xml`): 운동(광선 · 탄도 · 유도) · 탄속 · 중력 · 저항 · 수명 · 관통 · 튕김 · 폭발 · 태그 · `<OnHit when call>`(조건 · 동작 어휘) |
| 맞음 표 | 같은 파일 `<Impact id surface when color size>`, 처음 맞는 줄(`ProjectileCatalog::findImpact`) — 효과는 탄 × 표면이라 탄이 들지 않는다 |
| 무기 | `WeaponDef::_projectileID`(필수) · `_accepts`(탄 태그 식 — 호환은 조합 표가 아니라 태그 조건) · `<Modifier field op value>` |
| 수정자 | `ShotModifierDef`(`ShotField`, `Add` · `Multiply`, 음수는 0 에서 멈춤). 순서는 탄 → 무기 → 수정자 |
| 조합 | `ResolvedShot::resolve` — 장전 · 부착물이 바뀔 때 한 번(`WeaponState::equip` · `loadProjectile`) |
| 시뮬레이션 | `ProjectileSimulation` · `ProjectileState` · `IProjectileWorld` — 탄은 게임 오브젝트가 아니다 |

- 키트는 데이터와 반응만 갖는다 — `IProjectileWorld` 로 맞을 대상을 답하고 `ProjectileEvent` 를 받아 반응한다. 반응 수치는 탄에 베끼지 않고 가리킨다. 키트는 자기 카탈로그를 받는 길(게임 서비스 또는 생성 인자)로 탄 카탈로그를 받는다 — 판이 서비스를 찾지 않아 테스트 · 리플레이 · 서버가 같은 판을 연다. 벽 없는 판은 수명 · 사거리 없는 탄을 받지 않는다.
- 2D 는 평면 제약 3D 다(언리얼 ProjectileMovement 의 평면 제약). 차원마다 관통 · 튕김 · 유도를 두 벌 쓰지 않는다.
- 결정성: `ProjectileDef.cpp` · `ProjectileSimulation.cpp` 는 `sw_mark_deterministic_sources` 에 있고 삼각함수는 `DeterministicMathUtil`, 중력은 값으로 받는다. 상태 바이트는 `ProjectileSimulation::writeState` 한 벌. 걸음 안에서 세계를 바꾸지 않는다(사건은 걸음 뒤).
- 어빌리티 탄: 탄은 시뮬레이션이 돌리고 이펙트 스펙은 탄 id 로 찾는 짐(`ProjectileCargo`)이다 — 런타임 값이라 상태 바이트에 넣지 않는다. 복제가 생기면 시뮬레이션은 서버 권위, 짐은 서버에 둔다(GAS 투사체와 같은 길).
- 확인: `ProjectileTest`, `ActionRoomTest.ShotsResolveFromTheProjectileCatalog` · `KartRaceTest.ItemsFlyByTheirProjectile` · `MechArenaRulesTest`, 시나리오 `game.shooter3d.projectilecombo` · `game.abilityarena.control`.

### 오픈월드 액션 RPG 키트: MMO 와 싱글플레이 분리

MMO(검은사막류 — 지속 월드 · 경제 · 길드 경쟁)와 싱글 캠페인 액션 어드벤처(붉은사막류)를 한 "대형 오픈월드 키트" 나 게임 이름을 딴 키트로 만들지 않는다. 재사용 기반: `SceneStreamingSubsystem`(관찰자 원점 셀 스트리밍), `GF_NetMMO`(관심 영역 · 우선도), `GF_ActionAdventure`, `GF_OpenWorldWestern`, 온라인 `Social` · `Trade` · `Economy`.

1. 장르 이름을 딴 새 키트는 기존 키트를 조합해 본 뒤 **두 번째 사용처에서 같은 소유 · 저장 · 데이터 계약이 확인될 때만** 나눈다. 공통 후보는 지역 · 관심 지점 진행, 발견 기록, 탐험 지도, 보스 계약, 탈것 인터랙션, 채집 · 제작 진행뿐이다.
2. MMO 조합 후보(`GF_ActionMMO`): 서버 권위 · 영속 프로필 · 구역/샤드 · 파티/길드 · 시장 일관성 · 대규모 전투 복제. 기술 · 강화 · 거래는 클라이언트가 권위로 정하지 않는다. 부하 · 운영 지표 전에 월드 전체 규모를 약속하지 않는다.
3. 싱글플레이 조합 후보(`GF_OpenWorldActionAdventure`): 캠페인 진행 · 지역 상태 · 저장 · 동료 관계 · 보스 · 탈것, MMO 의존 없음. DLC(해양 · 수중 · 주거 · 시설 · 관계 · 추가 이야기)는 기존 세이브를 깨지 않는 선택 데이터 · 시스템 확장 — 팩 마운트보다 넓다(퀘스트 · 지역 상태 승계, 시설 소유, 건축 저장).
4. 말 · 배 · 전투 · 퀘스트는 바로 공유하지 않는다(`WesternHorse` 는 서부극 모델). 공통 탈것 수명 · 입력 · 좌석 인터페이스는 두 번째 사용처로 검증될 때 뽑는다.

수직 슬라이스: MMO 는 `LanternVale`, 싱글은 `Velmora`(확장 팩 · 세이브 승계)가 섰다. 운영형 게임의 누적 업데이트(예: 검은사막 Treasure Codex)는 콘텐츠 목록을 키트에 고정하지 않고 판 붙은 카탈로그 · 선택 팩으로 더한다.

### 하우징 · 주거 건축 키트

- 경계: `GF_Housing`(부지 소유 · 부품 카탈로그 · 배치 검증 · 미리보기 · 구매 · 회수 · 부지별 저장)은 선택 기능 키트다. 심즈형 욕구 · 가구 효용 · 일정은 생활 시뮬레이션 키트 몫이고, MMO 는 소유 · 최종 판정 · 저장을 서버 권위로. 저장 정본은 부지 id · 안정 배치 id · 부품 id/판 · 변환이다(임시 핸들 금지).
- 첫 판(`Source/GameFramework/Kits/Feature/World/Housing/`, 시연 Empty `maps/housing.scene.xml`)은 칸 벽 · 축 정렬 부지다.
- 첫 단계에서 넣지 않는 것: 구조 안정성 시뮬레이션, 임의 메시 → 벽 절차 생성, NPC 가구 자동 이용 플래너, 온라인 동시 건축.

### 핫 리로드와 스크립팅 언어 판단

- **판단: 범용 스크립트 VM(Lua 등)은 넣지 않는다** — 모드가 새 로직을 실제로 저작해야 한다는 요구가 확인될 때만 별도 평가한다.
- 핫 리로드(`GameModuleManager` — 개발 빌드에서 게임 모듈 재빌드 · 교체, 상태 직렬화 복원)는 프로그래머의 C++ 반복 도구이고, 스크립팅은 빌드 없이 새 로직을 쓰게 하는 별도 제품 기능이다. 배포는 정적 링크다.
- 제한된 로직은 이미 데이터다: `BehaviorTreeAsset` · `ConditionExpr` · `DialogueGraphAsset` · `AnimationGraphPlayer`. 요구가 생기면 먼저 이 형식에 노드 · 식을 더한다. VM 이 꼭 필요하면 서버 권한 · 결정성 · 시간/메모리 한도 · 허용 API · 버전 · 세이브 호환을 설계하고, 엔진 포인터를 노출하거나 신뢰하지 않는 스크립트를 제한 없이 돌리지 않는다.

### 모드 지원 키트

선택 키트 `GF_ModSupport`(매니페스트 · 검증 · 결정적 순서 · 구성 해시 · 서버 허용 목록, 덮어쓰기 층, 와이어 · 세이브 해시)가 섰다. **쿠킹된 콘텐츠 · 데이터 모드만** 받는다 — 네이티브 DLL · 스크립트 VM 은 범위 밖이다.

1. 엔진 `PackManager` · VFS 는 일반 콘텐츠 팩 기능이고, `ModSupport` 는 그 위의 조정자(매니페스트 · 호환 범위 · 의존 · 충돌 · 우선순위)다 — 팩 기능을 복제하지 않는다.
2. `ContentOnly` — 기존 데이터 스키마로 표현되는 것만, 데이터가 부를 수 있는 동작도 등록된 엔진 액션뿐이다. 마운트 · 캐시 무효화 경계를 함께 둔다.
3. **런타임 토글은 넣지 않는다** — 모드 구성은 게임 · 세션 시작 때 고정한다. 에셋 핸들 · 공유 캐시 · 진행 중 로드의 안전 규칙(언마운트 · 에셋 해제)이 필요해, 실제 요구가 될 때 그것과 함께 더한다.
4. 제작 · 활성화 UI 는 같은 키트의 `Editor/` 나 호스트 도구로, 배포 채널(수동 설치 · 워크숍)은 어댑터로 — 특정 스토어 연동을 엔진에 박지 않는다.
5. 멀티플레이는 서버가 허용 목록을 정하고 콘텐츠 · 판 해시를 맞춘다. 모드 없는 환경의 세이브 · 실행은 영향받지 않는다.

`PackPatchTrust` 는 패치 사슬 서명용이라 모드 제작자 신뢰로 넓히지 않는다(CRC 는 손상 검사, 암호화는 권한 경계가 아니다). 외부 모드를 받으면 서명 · 출처 · 권한 정책과 거부 이유를 정한다.

### 게임 액션 데이터 저작과 에디터

- XML 이 저장 정본이고 전용 에디터는 그 형식을 검증하는 UX 다. `GameActionCall` · `ActionRegistry`(행동 트리 · 대화 · 스케줄이 공유하는 이름 있는 동작 어휘)와 GAS 형 `Ability` 는 실행기 · 복제 · 수명이 달라 합치지 않는다.
- 이미 있다: 반영된 매개변수 구조체로 등록 · XML 호출 · `BehaviorTreePanel` 의 동작 선택과 매개변수 편집. 새 동작의 실행 의미는 C++ 다(예: RTS 액션 7 개는 매개변수 없는 C++ 작업).
- 순서: (1) 등록 메타데이터(설명 · 분류 · 적용 문맥, `PROPERTY` 메타를 범위 · 선택지 · 도움말 정본으로), (2) 에디터의 분류 · 검색 · 인라인 문제 표시 · 동작 교체 때 호환 안 되는 값 처리, (3) 범용 그래프 · 스크립트 전에 실제로 막히는 콘텐츠 사례 수집, (4) 완료는 XML 왕복 · 잘못된 참조 거부 · [EditorUI](agents/EditorUI.md) 화면 검토. 1 · 2 는 사용성 문제가 확인되면 한다.

### VFX 저작 · 캐릭터 잔상 · 분신

잔상은 품질 단계별 상한을 둔다. 분신은 게임플레이에서 필요할 때 별도 캐릭터 프리팹 · 애니메이션 인스턴스로 — 포즈 재생형인지 AI · 전투 가능한 독립 개체인지 먼저 가른다. 임의 그래프 · 모듈 시스템을 먼저 들이지 않는다.

### 체크포인트 · 리스폰 공통 키트(설계 2026-11-20)

**문제.** 사망 → 부활이 장르 키트마다 따로다. 공통 체력(`HealthStateMachine`)은 사망 · 기절 · 동료 부활 · `respawn` · 무적까지만 있고, 그 뒤(어디서 · 언제 · 무엇을 잃고)는 키트마다 다시 쓴다.

| 사본 | 체크포인트 켜기 | 저장하는 것 | 부활 자리 | 대기 | 무적 | 벌칙 |
|---|---|---|---|---|---|---|
| `ActionStageRun`(플랫포머) | 게임이 지나감을 알림(`reachCheckpoint`), 목록 순서 앞으로만 | 체크포인트 순번(int) | 마지막 체크포인트, 없으면 시작점(게임이 자리를 앎) | 없음 | 없음 | 목숨 1 · 확정 안 된 수집품 되돌림 |
| `MetroidvaniaDeathRestState`(소울류) | 쉬는 지점 상호작용(`rest`), 다시 쓸 수 있음 | 되살아날 지점 id | 마지막 쉰 지점 | 없음 | 없음 | 통화를 그 자리 시체로 떨굼(두 번째 죽음에 잃음) · 일반 적 리셋 |
| `ConquestWorld`(횡스크롤 정복) | — | 부활 타이머(Countdown) | 본진 거점(없으면 생길 때까지 기다림) | `commanderRespawn` 초 | 없음 | 없음 |
| `MechArenaWorld`(결정성) | — | `MatchState` 부활 타이머 | 조종사 고정 출격 자리 | `MatchState` | `respawnInvulnerable` | 팀 코스트 게이지(`MatchState`) |
| `TrampolineArena`(파티) | — | `MatchState` 부활 타이머 | 판 둘레 고정 자리 | `MatchState` | 자체 Countdown | 자기 낙하 감점 |
| `KartRace` · `KartTrack` | 바퀴 판정 관문 | 지난 관문 수 | (부활 아님) | — | — | — |
| 게임 `ArenaGameModeComponent`(AbilityArena) | — | 플레이어 부활 타이머 | 고정 | 초 | — | — |

**대조.** 언리얼: `AGameModeBase::RestartPlayer` → `FindPlayerStart` → `ChoosePlayerStart`(`APlayerStart` · 태그) — 자리 고르기와 스폰이 게임 모드 한 곳이고 서버에서만 돈다. Lyra 는 `ULyraPlayerSpawningManagerComponent`(자리 고르기 · 막힌 자리 거름)와 사망 어빌리티 → 리스폰 타이머로 나누고, 다운 · 부활은 GAS 어빌리티다(기억에 의존). 유니티 · Godot 은 엔진 기능이 없고 흔한 꼴이 "체크포인트 관리자 + 코루틴 대기 + 마지막 체크포인트 변환" 이다. 장르 관례: 소울류 화톳불(쉬면 적 리셋 · 죽으면 소지 재화를 그 자리에 떨굼), 플랫포머(앞으로만 가는 체크포인트 · 목숨), 슈터(팀 거점 · 스폰 고르기 · 스폰 보호 무적), MMO(제자리 부활 — 아이템 · 비용 · 횟수, 마을 부활, 경험치 · 내구도 벌칙, 파티원 부활), 배틀로얄(기절 → 동료 부활 시간).

**결정.**
1. **자리는 GameFramework 런타임 `Runtime/Gameplay/Respawn`(`SW_GF_API`, 클라이언트 · 서버 공용 한 벌) — 기능 키트가 아니다.** 장르 키트는 다른 키트를 링크할 수 없다(두 키트가 모두 `SW_KIT_EXPORTS` 라 받은 쪽이 남의 `SW_KIT_API` 클래스를 다시 내보낸다 — 서버 · 클라이언트 짝만 다른 내보내기 매크로로 런타임 키트를 링크한다). 공통 체력 · 판 상태(`HealthStateMachine` · `MatchState`)와 같은 층이고, 언리얼도 `RestartPlayer` 가 엔진 GameFramework 모듈에 있다. 서버 판정이 클라이언트와 같은 순수 로직이라 서버 짝을 따로 두지 않는다(하우징 서버 권위 배치와 같은 꼴).
   - `CheckpointState` — 켠 체크포인트 목록(id · 자리 · 요 · 순번)과 지금 부활 지점. 켜기 입구는 `activate( 정의, 자리, 요 )` 하나이고, 정의(`CheckpointDef`)가 다시 쓸 수 있는지(화톳불)를, 상태 규칙이 앞으로만(플랫포머)을 정한다. 반사 구조체라 주인의 상태 칸(`PROPERTY( RuntimeState )`)으로 세이브 · 스냅숏에 실린다 — 새 세이브 형식을 만들지 않는다. 트리거 진입 · 상호작용 · 스크립트는 모두 이 입구를 부른다(트리거 계약은 `w-trigger` 통합 뒤 잇는다).
   - `RespawnRules` — 정책 목록(차례로 시도: `LastCheckpoint` · `InPlace` · `NearestSpawnPoint` · `TeamBase` · `PlayerChoice`, 항목마다 횟수 · 비용 · 안전 반경 · 고르기 시한), 대기 초, 무적 초, 벌칙(통화 비율 · 고정, 경험치 · 내구도 비율). XML `<Respawn delay invulnerable …><Policy kind …/></Respawn>` 을 장르 카탈로그가 품는다. 정책이 없으면 시작 거절(폴백 없음).
   - `RespawnFlow` — 캐릭터 하나의 사망 → 대기 → 위치 고르기 → 위치 준비(스트리밍 셀) → 스폰(체력 `respawn`) → 무적 → 벌칙. 세계 질의는 `IRespawnWorld`(안전 지점 찾기 — 내비 메시 · 충돌 질의는 게임이 구현, 위치 준비 — 셀 쪽은 `w-cellstate`) 하나다. 정책이 모두 자리를 못 내면 다음 걸음에 다시 본다(정복의 "본진이 생길 때까지").
   - **동료 부활은 정책이 아니다** — 기절(`HealthSettings::_bDownedEnabled`) · 부활 진행은 `HealthStateMachine` 이 이미 갖고, 흐름은 죽음(`Dead`)에서만 시작한다.
   - 키트에 넣지 않는 것(장르 규칙으로 남김): 목숨(플랫포머), 시체 · 적 리셋(소울류), 팀 코스트 게이지(`MatchState`).
2. **멀티플레이는 서버가 정한다.** 클라이언트는 `RespawnRemoteTarget::requestRespawn( 정책 종류, 지점 id 해시 )` 만 보낸다(반사 함수 `FUNCTION( Server, Reliable, Validate, MaxCallsPerSecond )`). 자리 · 비용 · 잔액은 받지 않는다 — 제자리 부활 자리는 서버가 아는 죽은 자리에서 서버의 세계 질의로, 비용은 서버 지갑으로, 횟수는 서버 상태로 판정한다. 열거 밖 값은 검증 짝이 거짓(조작 — 연결을 끊는다), 규칙 거절(살아 있음 · 고르기 단계 아님 · 허용 안 된 정책 · 모르는 지점 · 돈 부족 · 횟수 끝 · 안전 지점 없음)은 결과 값으로 돌려준다. 흐름 상태는 `serializeState` 로 복제 · 세이브에 실린다.
3. **옮김.** `ActionStageRun` · `MetroidvaniaDeathRestState` 는 체크포인트를 `CheckpointState` 로(앞으로만 · 다시 쓰기는 데이터), `ConquestWorld` 지휘관은 `RespawnFlow`(`TeamBase`, 대기 = `commanderRespawn`)로 옮긴다. `MechArenaWorld` 는 결정성 키트라 비트 동일을 지키려 옮기지 않고, `TrampolineArena` 는 대기 · 비용이 이미 공통 `MatchState` 이고 자리가 판 고정이라 옮길 몫이 무적 타이머 하나라 두며, 카트 관문은 부활이 아니다. 아레나형 게임은 `MatchState` 가 "언제 · 돈" 을, `RespawnFlow` 가 "어디" 를 맡는다(흐름 대기 0).
4. **차례.** ① 키트 + 단위 · 서버 단위 테스트 ② 장르 키트 옮김(바뀐 상태 바이트는 해당 테스트의 손 바이트를 같이 고침) ③ Empty 플랫포머 시연이 흐름을 쓰고 시나리오.

**확인.** 단위: `CheckpointStateTest` · `RespawnRulesTest` · `RespawnFlowTest`(정책 고르기 · 제자리 안전 지점 · 체크포인트 저장 왕복 · 대기 · 자리 준비 · 무적 · 벌칙 · 동료 부활), 칸 지형 설 자리 `ActionPlatformerRespawnWorldTest`. 서버: `RespawnServerAuthorityTest`(서버 결정 · 잘못된 요청 거절), 루프백 서버 · 클라이언트 짝 `RespawnRemoteCallTest`(선을 지난 요청 → 서버 판정 → 회신, 꼴 틀린 호출 · 함수 상한). 옮긴 키트 테스트 그대로(바이트 모양이 바뀐 손 바이트 도우미만 고침). 시나리오 `game.playground.kitdemo_respawn`: 제자리, 낭떠러지 제자리 → 안전 지점, 체크포인트 → 사망 → 그 체크포인트, 세이브 · 로드 뒤 체크포인트 유지. 두 프로세스 시나리오는 아바타가 죽는 게임(LanternVale 에 아바타 사망 규칙)이 생길 때 그 게임의 `netpair/` 에 더한다.

## 통합 담당 판단(2026-11-08)

코드 규칙(구조로 푼다 · 폴백 없음 · 미사용은 쓰게 · 대기를 쌓지 않음)에 맞춰 정했다. 상용 엔진과 비교해 지금 정할 수 있는 것은 정했고 측정이 필요한 것은 측정을 일감으로 넣었다.

- **계약(S2):** 프로세스당 Engine 하나 · GameInstance 하나, 중복은 시작 오류(다중 엔진 요구가 생기면 컨텍스트로). 카탈로그 참조는 데이터 표 한 벌 — 위 "데이터 기반 카탈로그 참조 전수 점검" 의 엄격도 계약. XML 의 같은 종류 id 겹침은 오류(코드 API `add*` 의 교체는 유지).
- **에디터 작업 안전(S3):** 소스 관리 연동은 Git 만, 외부 계정 · 유료 공급자 없음. 자동 저장은 세대별 임시본이고 원본을 덮지 않는다.
- **개발 그림은 사진에 들지 않는다(2026-11-09):** 캔버스 겹치기 등록(`SW_CANVAS_OVERLAY`)은 층(`CanvasOverlayLayer`)을 밝힌다. 디버그 드로우 게임 뷰 겹치기는 디버그 HUD 와 같은 개발 층 — 사진 캡처 뒤 화면에만 합치고(에디터 게임 뷰는 `rendertarget/dev_overlay`) 스크린샷 · `<ExpectImage>` 골든에 들지 않는다. 캔버스 테스트 그림만 주 출력 층이다. 유니티 기즈모 · 언리얼 `HighResShot` 을 따랐다(Godot 디버그 그리기는 캡처에 든다).
- **하지 않기로 정한 것:** Meadow 밭 크기는 소유 타입 상수, Shooter3D 탄착 표면 목록은 코드 상수. 범용 스크립팅 VM · 네이티브 모드 DLL · 원격 명령 서버는 넣지 않는다.
- **리소스 라이선스(사용자 결정 2026-11-08 — 문제가 되면 다른 리소스):** 저장소의 Quaternius 묶음 여덟은 묶음 페이지가 CC0 이고 QAL 은 소급하지 않아 둔다(근거 `Content/common/credits.md` 머리). 새 리소스는 무료 · 공개 라이선스만(CC0 우선, MIT 처럼 재배포 되는 것은 고지와 함께 — [NEXT](NEXT.md) 리소스 규칙), QAL · CC-BY · 불명확한 것은 다른 CC0 출처로.

**측정과 성능 후보(S8).** 재는 일은 [NEXT](NEXT.md) 4 절이다. 재서 이득이 없어 닫은 값만 여기에 한 줄씩 남긴다(판단 기록 — 아직 없다). RHI 스레드 시제품은 거기서 병목이 나올 때만 만든다. 에디터 UI 스레드 분리는 하지 않기로 했다(사용자 결정 2026-11-18 — 위 "에디터 UI 스레드 분리 검토" 절).

### 지역 효과의 범용 구조(사용자 2026-11-08 — 용암 · 바다 · 늪 · 유황 · 독가스)

**정한 것.** 지역 효과는 입력 → 모듈 → 반응이다.

- **입력:** 그린 볼륨(`ZoneVolumeComponent`), 표면 종류 · 물리 재질(`surface:<종류>` — 접촉 `ZoneContactComponent`), 런타임 영역(볼륨 + 수명 · 퍼짐 프리팹). 접촉 넷(겹침 · 디딤 · 부딪힘 · 잠김)이 같은 처리기(`ZoneTracker`)를 지나고 프레임 일은 `ZoneSystem` 하나가 틱 뒤에 돈다.
- **프로필:** `*.zone.xml`(이름 붙은 모듈 목록, `_base` 하나) · 재사용 `*.zonemodule.xml`, 세 층(프리셋 · 변형 · 배치 덮어쓰기 `_listModuleOverride`)을 데이터 상속이 합치고 합친 결과를 로드 때 검사한다(모르는 종류 · 매개변수 · 식 · 동작 · 참조는 로드 실패). 수치 칸은 조건식(`zoneDepth01` · `distanceToCenter` · `immersion` · `timeInside` · `impactSpeed`), 사건은 진입 · 체류 · 이탈 · 조건 전환(`Became` · `Lost`, `delay`), 경우 목록은 처음 맞는 하나, 저항 · 면역은 이펙트 계산이 깎는다.
- **모듈**(서로를 모른다 — 새 효과는 `SW_ZONE_MODULE` 한 줄): `Action` · `PhysicsField`(중력 배율 · 방향 × 세기 · 유체 저항 · 부력 · 마찰, 캐릭터 · 강체 공통) · `Blackboard` · `NavArea` · `SurfaceChannel` · `SurfaceStamp`.
- **반응**은 태그 · 속성만 읽는 컴포넌트다: `VisualSinkReactionComponent` · `JumpEscapeReactionComponent` · `ScreenReactionComponent`(`CameraDirector::setPostProcessOverlay` 페이드).
- 데이터 `Content/game/playground/zones/`, 시나리오 `game.playground.zone*` · `sinkingsand`. 비교: 언리얼 GAS 이펙트 Component · PhysicsVolume, 유니티 Volume Override · Effector, Godot `Area3D` 를 한 프로필의 모듈로 합쳤다(영역마다 컴포넌트를 늘리지 않는다).

### 표면 변형 층(사용자 2026-11-08 — 지나온 길 · 파묻힘 · 빨려드는 구멍)

**정한 것.** 씬 서비스 `SurfaceDeformSystem` 하나가 카메라 앞 창의 CPU 높이 격자에 도장(`SurfaceStampDef` — Print · Trail · Funnel)을 찍고 바뀐 칸만 텍스처로 보낸다. 렌더 패킷(`SurfaceDeformBinding`)이 엔진 텍스처 칸 `SurfaceDeform` 과 패스 상수 `g_SurfaceDeformParams` 로 모든 씬 지오메트리 패스(그림자 포함)에 걸고, 지면 · 지형 셰이더가 같은 함수(`surface_deform_layer.hlsli`)로 정점을 내린다(언리얼 RVT · 유니티 `SetGlobalTexture` 자리, 2026-11-11). 충돌 · 내비 · 높이장은 그대로다. 도장을 내는 길은 셋이고 모두 데이터다: 표면 종류 발자국(`SurfaceKindDef::_footprint`), 오브젝트 도장(`SurfaceStampComponent` Hold · Trail), 지역 모듈 `SurfaceStamp`. GPU 렌더 타깃이 아니라 CPU 격자인 것은 도장이 프레임에 몇 개뿐이고 값 시나리오가 깊이를 그대로 읽으며 백엔드 코드가 늘지 않아서다 — 수백 개로 늘면 컴퓨트로 옮긴다. 시나리오 `game.playground.surfacedeform` · `surfacefootprint` · `sinkingsand` · `terraindeform`.

### 관성 이동과 탑승 가속감(사용자 2026-11-08 — 썰매처럼 경사에서 빨라지고 느려지고, 타면 가속이 느껴지게)

**정한 것.**

- 탈것 이동은 둘이다: Jolt 강체 · 바퀴(`WheeledVehicleComponent`, 시뮬레이션 레이싱)와 바디 없이 힘을 적분하는 `KinematicVehicleComponent` · `KinematicVehicleMotor`(아케이드 · 썰매 · 수레 · 호버 · 레일).
- 바깥 힘은 `InertiaSettings::computeAlongAcceleration` 한 벌(경사 중력 − 구름 저항 − 공기 저항 v², 질량으로)을 키네마틱 탈것 · 말(`MountMovementComponent`) · 롤러코스터가 함께 쓰고, 구동은 출력 한계(`_maxPower`, P / (m·v))로 자른다 — 언리얼 Chaos Vehicle 토크 곡선 · 유니티 motorTorque 자리를 바디 없는 모델에서는 일률 한 칸으로.
- 조작감은 모듈(`Grip` · `Drift` · `Boost` · `AirControl` · `Hover` · `Rail`), 튜닝은 `*.vehicle.xml`(`_base` 상속). 탑승 가속감은 좌석(`VehicleSeatComponent::_rideFeel`)이 탈것 가속도로 탑승자 기울임 · 시야각 킥 · 흔들림 · 소리 매개변수를 낸다.
- **배**는 새 물리 없이 선체 강체 + 폰툰 부력 · 키 · 돛 · 방향별 저항의 조합이다(`BoatComponent` · `BoatHelmComponent` · `WeatherWaveComponent`).
- 시나리오 `game.playground.inertia`(골든 `inertia_sled`) · `mountslope` · `boat`. 카트 트랙 전용 시나리오는 두지 않는다(같은 모터 · 표면 종류라 `inertia` 와 겹친다). 비행은 범위 밖.

### 에셋 정체성: 경로 id → 무작위 GUID (사용자 결정 2026-11-11)

**결정.** 에셋 id 를 경로에서 무작위 128 비트 GUID(`AssetGUID` — 소문자 16진 32 자 한 철자, 빈 값 없음)로 바꾼다. `6b909f783f`("에셋 .meta · GUID 를 지우고 참조를 경로 하나로")를 되돌리는 판단이다.

**까닭.** 경로 id 는 옮기면 바뀐다 — 저장소 안 참조는 옮기기가 고치지만 저장소 밖(세이브 · 서버 기록 · 배포된 팩 · 모드)에 적힌 옛 경로를 살리려면 리다이렉트 줄(`AssetRedirect`)이 옮길 때마다 영구히 쌓인다. 에셋 수만~수십만 · 여러 사람 동시 작업 · DLC · 라이브 패치 · 수년 쌓인 세이브 규모에서 그 표는 줄지 않는다. 정체성을 경로와 떼면 옮기기는 색인 한 줄만 바꾼다(유니티 `.meta` guid, Godot 4 `uid://`). 언리얼의 경로 + 리다이렉터를 버린 자리다.

| 정할 것 | 선택 | 까닭 |
|---|---|---|
| id | 무작위 128 비트 GUID, 내용이 바뀌어도 그대로. 내용이 바뀌었는지는 따로 내용 해시(DDC · 쿠킹 키) | 정체성과 내용을 한 값에 두면 고칠 때마다 참조가 끊긴다 |
| 자리 | 우리 형식 글(`.xml` · `.material`)은 파일 맨 앞 처리 지시 `<?asset guid="…"?>`(XML 선언 바로 뒤), 원본(png · ogg · glb …)은 곁의 `<파일>.meta`(`<AssetMeta _guid _source/>`, 리플렉션 엄격 읽기) | 루트 속성(`guid=`)은 버렸다 — 로더 70 여 종이 받는 루트 속성을 따로 적고, 데이터 상속(`_base` 합치기)이 부모 속성을 자식에게 베껴 두 에셋이 한 정체성을 갖는다. 처리 지시는 파서가 문서에 올리지 않아 로더 · 합치기가 보지 않는다(Godot 4 도 uid 를 리소스 칸이 아니라 파일 머리 줄에 둔다). 형식 판 올림으로 GUID 를 넣지 않는다 — 읽을 때 만든 GUID 는 실행마다 달라 정체성이 아니다. 판 올리기 자리는 `asset-ids --fix` 가 파일에 쓰는 것이다 |
| 규칙 | `Config/Dev/AssetIdentity.json`(머리 접미사 · 정체성 없는 파일 패턴 · 임포트 산출물 폴더) — C++ `AssetIdentityRules` 와 파이썬 `asset-ids` 가 같이 읽는다 | 골든(시험 기록 도구가 통째로 다시 쓰는 증거) · credits.md · .clang-format · localization 표(글 수집이 바이트로 쓰고 대조)는 정체성이 없다 |
| 원본 보관 | 서술은 git, 원본은 나중에 LFS · 별도 저장소로(서술의 `_source` 가 원본 자리를 상대 경로로 든다 — 지금은 곁 파일 이름) | 3 단계 |
| 변환 결과 | 커밋 안 하는 캐시(`models/` · `textures/` · `heightfields/` — 정체성은 원본의 것) — 키는 서술 + 원본 내용 해시 | 나중에 공유 DDC |
| 배포 | 팩마다 GUID → 팩 안 경로 색인 `assetindex.xml`(`AssetPackIndex`, GUID 순), 패치 · DLC 는 자기 색인에 줄을 더할 뿐 | `.meta` 는 팩에 싣지 않는다(PackConfig). 유니티 Addressables 카탈로그 · 언리얼 에셋 레지스트리 조각 자리 |
| 관리 | 도구가 만들고 짝을 지킨다 — 에디터 옮기기 · 이름 바꾸기 · 휴지통 · 지우기는 `.meta` 를 함께, 새로 만들기 · 복제 · 가져오기는 새 GUID, 저장 입구(`ResourceComposer::saveText`)와 생성기는 머리를 옮겨 쓴다(`AssetGUIDHeader::carry`). `move-asset` 도 `.meta` 를 함께 옮긴다 | 사람은 서술을 만지지 않는다 |
| 깨지는 경우 | 지우고 다시 만들기(새 GUID) → 에디터 "내용 바꾸기"(`replaceAssetContent`)가 GUID 를 지키고, 커밋 훅이 HEAD 경로의 GUID 가 바뀐 것을 멈춘다. 복사(겹친 GUID) → 커밋 훅이 `git grep` 으로 잡고 `--fix` 가 복사본(HEAD 에서 그 GUID 를 들지 않던 쪽)에 새 GUID. 머리를 잃은 저장 → `--fix` 가 HEAD 의 GUID 를 되살린다. 짝 잃은 `.meta` → HEAD 블롭 해시 · 이름으로 새 자리 원본에 다시 붙이고, 하나로 정해지지 않으면 오류(조용히 새 GUID 를 만들지 않는다). 정체성 대체 · 합치기만 작은 GUID→GUID 표(2 단계, 옮기기용 리다이렉트는 없어진다) | |

**커밋 훅의 범위.** 바뀐 파일과 그 짝만 본다(겹침은 `git grep --cached` — 블롭 전체를 읽지 않아 에셋 수십만에서도 커밋마다 돈다). 저장소 전체는 `py -3 -m Scripts asset-ids` 가 본다(묶음 검증에 넣는다).

**알려진 틈.** `FileUtil::writeTextFile` 로 Content 글을 바로 쓰는 도구(저장 입구를 지나지 않는 쓰기)는 머리를 잃는다 — 커밋 훅이 머리 없음으로 잡고 `--fix` 가 HEAD 의 GUID 를 되살리므로 정체성이 새지는 않지만, "생성물이 최신인가" 를 바이트로 견주는 검사가 있으면 그 검사가 진다(머티리얼 그래프 산출물은 `carry` 로 고쳤다). 임포터가 커밋하는 원본 폴더에 내용 주소 이름으로 꺼내는 파일(`textures_raw/_shared/`)은 지금 `--fix` 가 무작위 GUID 를 준다 — 두 사람이 같은 모델을 따로 임포트하면 같은 파일에 다른 `.meta` 가 생겨 더하기 · 더하기 충돌이 난다. 정했다: 그 저장소 파일의 GUID 는 저장소 경로(도메인 + 내용 키)의 해시 128 비트로 정한다(내용 주소 파일은 경로가 곧 정체성이다) — 2 단계에서 `ImportedAssetStore::writeIfAbsent` 가 `.meta` 를 함께 쓴다.

## 길드 창고의 이전 불가 자산(사용자 결정 2026-11-12, 감사 O-10)

- **결정:** 유료 재화 · 귀속 아이템처럼 계정 사이 이전이 막힌 자산은 길드 창고를 거쳐서도 **맡긴 당사자가 허용한 권한 안에서만** 다른 계정으로 간다. 허용이 없으면 맡긴 사람만 꺼낸다.
- **구조:** 원장 정책 `ILedgerPolicy::isTransferable`(PlayFab Economy v2 · Steam Inventory 처럼 정의의 속성 — 거래 `isTradable` 과 같은 표, 화폐는 `_bPaid` 가 아니면 이전 가능). 이전 불가 자산은 맡긴 사람별 보유자(`esc/guild/<길드>.<계정>`)에 들고 맡긴 사람은 `social_guild_depositor` 에 적는다(원장이 보유자를 열거하지 못해 해산 확인 · 창고 목록이 이것으로 몫을 찾는다). 꺼내기는 `_depositorID` 로 몫을 적고 본인이거나 허용(`social_guild_storage_grant`, 남은 한도를 같은 트랜잭션에서 줄인다)이 있을 때만 옮긴다. 허용 부여 · 회수 `GuildGrantWithdrawal` · `GuildRevokeWithdrawal` 은 맡긴 사람만, 멱등 키 필수, 한도는 덮어쓴다(WoW 길드 은행 출금 한도와 같다, 맡긴 사람마다 32 개). 맡긴 사람이 떠나거나 내보내지면 몫을 같은 트랜잭션에서 돌려주고(몫의 자산 종류는 원장 다리 상한 16 까지) 기록 · 허용을 지운다. 허용을 받은 멤버가 떠나면 받은 허용도 지우고, 부여 · 회수는 지우지 않는 멱등 기록(`ServiceIdempotency`, 범위 `guild/<계정>`)으로 회수 뒤 늦게 온 같은 부여를 무시한다.
- **확인:** `TestGuildService` — `NonTransferableShareNeedsDepositorGrant` · `DepositorShareBlocksDissolution` · `DepositorShareReturnsOnLeaveAndKick` · `ReceivedGrantEndsWithMembership` · `LateGrantReplayAfterRevokeChangesNothing`.

## 렌더 · RHI 설계(2026-11-17)

"측정 · 설계가 먼저" 라 미뤄 둔 다섯 항목과, 맨 끝에 이미 정한 메시 구조다. 절마다 문제 → 상용 대조 → 결정과 기각 → API 초안(C++17) → 이전 순서 → 측정 순이다(일 목록은 [NEXT](NEXT.md) 5 절). 상용 엔진의 내부 이름은 기억 기준이라 작업자가 그 판의 문서로 다시 본다. 새 린트 규칙은 없다 — 막을 것은 형식(순수 가상 · 주인 타입 · 매개변수 타입 · 로드 오류)으로 막았다.

### bindless 번호 고정과 디스크립터 다시 쓰기(설계 2026-11-17, 감사 R-6)

**문제.**

- 텍스처의 SRV 번호가 수명 중 바뀐다. 첫 업로드 끝(`TextureCache.cpp:310` `finishLoad`), 밉 올리기(`:579` — `Texture2D::finishUpgradeResidency` 가 새 텍스처를 새 번호로 `registerBindlessTexture` 하고 `_srv.exchange`, `Texture2D.cpp:356-395`), 렌더 타깃 크기 변경(`:843`), 핫 리로드(`:1324`)가 모두 전역 `_reloadGeneration` 을 올린다.
- 세대가 오르면 모든 `Material::refreshTextureBindings`(`Material.cpp:238`) · `MaterialInstance`(`MaterialInstance.cpp:345`)가 자기 텍스처를 다시 찾고, `MaterialRenderInputs::_textureReloadGeneration` 이 달라져 씬 스냅샷이 머티리얼 사본 전부를 다시 맞추며, `Light2DCookieAtlas.cpp:89` 는 모든 칸을 다시 그린다. 500 장이 프레임당 2 장(`TextureCache::kMaxStreamingUpgradesPerFrame`)씩 오르면 250 프레임 내내다.
- 내리는 길이 없다(`TextureResidency` 가 `Full` · `StreamingBase` 둘뿐). `Config/Engine/MemoryBudget.json` 의 Texture 1024 MB 를 스트리밍이 읽지 않는다.

**상용 대조.** 셋 다 머티리얼이 쥔 정체는 그대로 두고 뒤의 GPU 자원만 바꾼다.

- 언리얼: `FRHITextureReference` — 머티리얼은 참조를 묶고 `RHIUpdateTextureReference` 가 가리키는 텍스처만 바꾼다. bindless 에서는 참조가 `FRHIDescriptorHandle` 을 유지하고, D3D12 `FD3D12BindlessDescriptorManager::UpdateDescriptor` 는 GPU 가 쓰는 중이면 힙의 새 판에 복사해 쓴다.
- 유니티: `Texture2D` 객체가 정체다. Mipmap Streaming(`QualitySettings.streamingMipmapsMemoryBudget` · `Texture2D.requestedMipmapLevel`)이 같은 객체 아래의 밉을 갈아 끼운다.
- Godot 4: `RenderingServer.texture_replace` 가 RID 를 유지한 채 내용을 바꾼다. 2D 텍스처 밉 스트리밍은 없다.

**결정.**

1. **번호는 `Texture2D` 의 수명과 같다.** 캐시 항목을 만들 때 번호를 받고(플레이스홀더인 로드 중 텍스처를 가리킨다), 첫 업로드 · 밉 올리기 · 내리기 · 크기 변경 · 핫 리로드는 `rebindBindlessTexture` 로 같은 번호가 가리키는 텍스처만 바꾼다. 로드 실패는 누락 체커로 rebind 한다. 머티리얼이 로드 중 텍스처를 대신 빌렸다가 바꾸던 일(`_listBorrowedTexturePath` · `resolveArrivedTexture`)은 캐시 한 곳으로 온다.
2. **네이티브 bindless(DX12 · Vulkan)는 디스크립터 테이블을 프레임 링 슬롯마다 한 벌 둔다**(`constant::render::kMaxFrameCountInFlight` = 3). 새 번호 등록은 세 벌에 그 자리에서 쓴다(그 번호는 펜스 뒤에야 다시 나가므로 진행 중 프레임이 읽지 않는다). 산 번호 다시 쓰기는 등록표 값만 바꾸고 세 슬롯에 예약하며, 슬롯의 사본은 링이 그 슬롯에 들어온 직후(앞 사용의 펜스를 기다린 뒤, 기록 전)에만 쓴다 — 진행 중 커맨드가 읽는 디스크립터를 덮는 일이 없다. 새 텍스처는 늦어도 다음 프레임부터 보이고, 옛 텍스처는 지금의 펜스 지연 해제(`releaseHandle` → `destroyTexture`)로 간다. DX12 는 셰이더 가시 힙 안에 bindless 구간(32768 칸)을 세 벌 두고 `bindBindlessRootState` 가 이번 슬롯 구간을 루트 테이블 시작으로 건다(힙은 하나, `kDx12DescriptorHeapSize` 를 두 구간만큼 키운다). Vulkan 은 `_textureSet` 을 슬롯별 세트 셋으로 한다.
3. **에뮬레이션(DX11 · GL · WebGPU)은 등록표 값만 바꾼다.** 번호를 기록 때 자원으로 풀기 때문이다. 번호로 중복 바인딩을 거르는 기록 상태(DX11 `forget*InRecordingStates` 길 · GL · WebGPU 바인드 그룹 키)는 rebind 가 그 번호를 잊게 한다.
4. **전역 세대를 지운다.** `TextureCache::getReloadGeneration` · `Material::refreshTextureBindings` · `MaterialRenderInputs::_textureReloadGeneration` 을 없앤다. 내용을 복사해 가는 쪽(쿠키 아틀라스)은 텍스처별 `Texture2D::getContentSerial()` 을 칸마다 견준다. 장치 교체는 번호가 새로 나오지만 머티리얼이 장치마다 `resolveTextureAssets` 를 다시 하므로 세대가 필요 없다(R6-5 가 그 길을 확인한다).
5. **밉 내리기(디모션)는 같은 rebind 다.** 아래쪽 밉만으로 작은 텍스처를 만들어 rebind 하고 큰 것을 지연 해제한다. 무엇을 올리고 내릴지는 순수 함수 `TextureStreamingPolicy` 가 예산(`MemoryBudgetSubsystem` 의 Texture)과 화면 크기에서 정한다 — 예산 아래면 필요한 밉까지 올리고, 넘으면 화면 크기가 작거나 오래 안 보인 것부터 내린다. 프레임당 제출 수 · 내리기까지의 유휴 프레임은 `Config/Engine/TextureStreaming.json`(없으면 기동 오류).

**기각.**

- 힙 · 세트 하나에 제자리 덮어쓰기: 진행 중 커맨드가 쓰는 디스크립터를 덮는 것은 정의되지 않은 동작이다(`VulkanRHIResourceFactory::scheduleTextureBindlessIndexFree` 주석이 이미 같은 이유로 펜스를 기다린다). 디스크립터 하나(수십 바이트)가 반쯤 쓰인 채 읽힐 수 있다.
- 언리얼식 힙 판 올림(갱신마다 힙 전체 복사): 밉이 오르는 프레임마다 32768 칸 복사다. 링 사본은 바뀐 번호만 쓴다.
- 셰이더 간접 표(슬롯 → 번호 버퍼): 모든 머티리얼 샘플에 종속 읽기가 하나 는다(모바일 성능 하락).
- 예약 · 스파스 텍스처 + 최소 LOD 클램프: 번호는 그대로지만 GL · WebGPU · 모바일 Vulkan 에 없다.
- 텍스처별 세대(번호는 바뀜): 그 텍스처를 쓰는 머티리얼을 찾는 역참조 표가 새로 필요하고 다시 패킹은 남는다.

**API 초안.**

```cpp
// IRHIResourceFactory (RHI ABI v56) — 산 번호 index 가 texture 를 가리키게 한다. 번호와 주인별 번호 목록의 자리만 옮기고 옛 텍스처는 부르는 쪽이 부순다.
// 조건은 등록과 같다(첫 업로드 끝 · Texture2D · requireRegistryMutableNow). 산 번호가 아니면 false.
[[nodiscard]] virtual bool rebindBindlessTexture( RHIDescriptorIndex index, RHITextureHandle texture ) = 0;

// Backend/RHIBindlessRegistry.h — 값과 주인 목록을 함께 옮긴다(find 가 고쳐 쓰는 길을 두지 않는 까닭을 지킨다).
bool rebind( RHIDescriptorIndex index, const TEntry& entry, TEntry* pOutPrevious = nullptr );

// Backend/RHIDescriptorRingMirror.h — 잠금 없음(쓰는 쪽의 _bindlessMutex 아래), RHIConstantBufferMirror 와 같은 자리.
class RHIDescriptorRingMirror
{
public:
    void noteRewrite( RHIDescriptorIndex index ); // 모든 슬롯에 예약
    template <typename FnWrite>
    void flushSlot( uint32 slot, FnWrite&& fnWrite ); // 링이 slot 에 들어온 직후: 예약된 번호마다 fnWrite( index ) — 등록표의 지금 값으로 쓰고 죽은 번호는 건너뛴다
    void clear();

private:
    FixedCountArray<vector<RHIDescriptorIndex>, constant::render::kMaxFrameCountInFlight> _arrPending;
};

// Engine/Graphics/RHI/RHIBindlessTextureSlot.h — 번호 하나의 주인(이동 전용). 번호를 값으로 복사해 따로 해제하는 길이 없다.
class SW_API RHIBindlessTextureSlot final
{
public:
    [[nodiscard]] static sw::expected<RHIBindlessTextureSlot> create( IRHIDevice& device, RHITextureHandle placeholder );
    [[nodiscard]] bool rebind( RHITextureHandle texture );
    RHIDescriptorIndex getIndex() const { return _index; }
    ~RHIBindlessTextureSlot(); // releaseHandle( RHIHandleKind::BindlessTexture )
};

// Texture2D: atomic<RHIDescriptorIndex> _srv → RHIBindlessTextureSlot _slot, uint32 getContentSerial() const(rebind 마다 오른다).
// TextureResidency(둘) → uint8 _residentFirstMip(0 = 전부).
struct TextureStreamingCandidate
{
    uint32 _mipCount{ 0 };
    uint32 _residentFirstMip{ 0 }; // 지금 GPU 에 있는 가장 큰 밉
    uint32 _wantedFirstMip{ 0 };   // 화면 크기에서 구한 필요한 가장 큰 밉
    uint32 _idleFrames{ 0 };       // 화면 크기를 알리지 않은 프레임 수
    uint64 _topMipBytes{ 0 };
};
struct TextureStreamingDecision
{
    uint32 _candidateIndex{ 0 };
    uint32 _targetFirstMip{ 0 };
};
class SW_API TextureStreamingPolicy
{
public:
    static void decide( vector_reference<const TextureStreamingCandidate> listCandidate, uint64 residentBytes, uint64 budgetBytes, const TextureStreamingConfig& config,
                        vector<TextureStreamingDecision>& outListDecision );
};
```

**측정.**

| 무엇 | 시나리오 · 지표 | 판정 |
|---|---|---|
| 머티리얼 다시 묶기 | `game/playground/automation/load/*` · Shooter3D 첫 진입, 새 카운터 `GT.Material.textureResyncs`(프레임마다) | 밉이 오르는 동안 0(구조상 정확히 0 — 아니면 결함) |
| 스트리밍 구간 프레임 | 같은 실행의 `GT.Frame` · `RT.Frame` p95, `RT.TextureStreaming` | ④ 뒤 p95 가 앞보다 흔들림 밖으로 나쁘지 않다 |
| 링 사본 비용 | 새 카운터 `RT.Bindless.mirrorWrites`, 등록 한 번의 시간(`AssetLoadScope` `TextureUpload`) | 쓰기 수 ≤ 3 × (등록 + rebind), 등록 시간 증가 1 µs 아래 |
| 유효성 | dx12 디버그 레이어 · vk 검증 레이어로 shooter3d · load 체인 | 디스크립터 관련 메시지 0 |
| 내리기 | 4K 300 장 합성 씬을 카메라가 지나는 새 시나리오 `load/texturebudget`, `RHIMemoryTracker` 텍스처 바이트 | 지난 뒤 120 프레임 안에 예산 아래, 멈춘 화면 골든은 예산 없는 실행과 1 % 안 |

### 텍스처 스트리밍 밉 범위 대기 줄(설계 2026-11-17, 감사 R-6 · A-12)

**문제.** 대기 줄 원소가 DDS 전체를 쥔다 — `TextureCache::Impl::StreamingUpgrade{ _key, unique_ptr<DDSImageData> _pImage }`(`TextureCache.cpp:112-117`). 첫 로드도 아래쪽 밉만 올리면서 파일 전체를 읽는다(`Texture2D::readImage` → `DDSLoader::loadFromAsset`). 500 장이면 마지막 텍스처의 바이트가 250 프레임 동안 RAM 에 있다. 팩은 항목을 통째로만 읽는다(`PackReader::readFile` · `readFileAsync`, 감사 A-12).

**상용 대조.**

- 언리얼: `FTexture2DStreamIn_IO` 가 필요한 밉만 벌크 데이터에서 오프셋 · 크기로 읽는다(`FBulkDataIORequest`). 작은 밉은 에셋에 인라인, 큰 밉은 스트리밍 벌크로 나뉜다. 대기 줄(`FRenderAssetStreamingManager`)은 "어느 텍스처의 어느 밉" 만 든다.
- 유니티: Mipmap Streaming 이 `.resS` 에서 밉을 오프셋으로 읽고 동시 IO 수를 `QualitySettings.streamingMipmapsMaxFileIORequests` 로 묶는다.
- Godot: `CompressedTexture2D`(`.ctex`)를 통째로 읽는다 — 지금의 우리와 같다.

**결정.**

1. **대기 줄 원소는 `TextureMipRequest{ 경로, 첫 밉, 밉 수 }` 다.** 바이트를 쥘 칸이 형식에 없다. 바이트는 줄에서 꺼낼 때 워커가 범위로 읽고, 읽는 중 · 제출 대기 바이트의 합을 설정 상한(`TextureStreaming.json` `_maxBytesInFlight`)으로 묶는다. 상태는 대기 → 읽는 중 → 제출 대기 → 업로드 중 → rebind.
2. **파일 형식은 DDS 그대로다.** DDS 는 큰 밉부터 이어지므로 밉 범위가 곧 바이트 구간이다. 머리(최대 148 바이트)에서 `TextureMipLayout`(밉별 오프셋 · 크기)을 구하는 순수 함수를 두고 `Texture2D::computeMipDrop` 의 건너뛸 바이트 계산을 이것으로 합친다. 첫 로드는 머리 + 꼬리 구간, 올리기는 위쪽 구간만 읽는다.
3. **범위 읽기 입구는 `IContentFileSource::readRangeAsync` 순수 가상이다** — 구현을 빠뜨린 소스는 컴파일되지 않는다. 디스크는 `AsyncFileIO::readRange`, 메모리 소스는 구간 복사, 팩은 무압축 항목이면 저장 오프셋에 더한 구간이다.
4. **A-12 와의 접점은 이 함수 하나다.** A-12(블록 표 + 블록 단위 압축)가 들어오기 전에는 팩의 압축 항목 범위 읽기를 오류(`Pack.RangeReadOnCompressedEntry`)로 두고, 쿠킹이 `.dds` 를 무압축으로 싣는다(BC · ASTC 는 이미 압축이라 이득이 작다 — 압축된 `.dds` 항목은 쿠킹 오류). A-12 뒤에는 팩 안쪽만 블록 읽기로 바뀌고 텍스처 코드는 그대로다.

**기각.**

- 전체 이미지를 쥔 채 줄 길이만 제한: RAM 은 묶이지만 줄 뒤 텍스처의 첫 화면(아래쪽 밉)이 늦는다.
- 파일을 꼬리 + 위쪽 밉 둘로 나누기(언리얼 인라인 · 벌크): 파일과 `.meta` 짝이 두 배가 되고 범위 읽기로 같은 IO 를 얻는다.
- KTX2 같은 새 컨테이너: 새 의존성과 트랜스코딩이 든다.
- 메모리 맵: 팩 · 웹 · APK 에셋에서 같은 길이 없다.

**API 초안.**

```cpp
// Engine/Asset/Image/TextureMipLayout.h — 파일 없이 테스트한다.
struct SW_API TextureMipLayout
{
    struct Mip
    {
        uint64 _offset{ 0 }; // 파일 처음부터
        uint32 _size{ 0 };
        uint32 _width{ 0 };
        uint32 _height{ 0 };
    };
    static constexpr uint32 kMaxHeaderBytes = 148; // 매직 4 + 머리 124 + DX10 머리 20

    vector<Mip> _listMip; // 0 = 가장 큰 밉
    uint32      _dxgiFormat{ 0 };

    [[nodiscard]] static sw::expected<TextureMipLayout> parse( const uint8* pHeader, size_t headerSize, uint64 fileSize );
    void getRange( uint32 firstMip, uint32 mipCount, uint64& outOffset, uint64& outSize ) const;
};
struct TextureMipRequest
{
    string _key;
    uint32 _firstMip{ 0 };
    uint32 _mipCount{ 0 };
};

// Core/File/ContentFileSource.h
virtual AsyncReadHandle readRangeAsync( AsyncFileIO& io, string_view path, uint64 offset, uint64 size, AsyncIOPriority priority,
                                        const AsyncReadCompleteDelegate& onComplete ) const = 0;
// Texture2D — DDSImageData 전체 대신 구간 바이트를 받는다.
[[nodiscard]] Texture2DUploadStep beginUploadMips( IRHIDevice* pDevice, const TextureMipLayout& layout, uint32 firstMip, vector_reference<const uint8> mipBytes, bool bBlocking );
```

**측정.**

| 무엇 | 시나리오 · 지표 | 판정 |
|---|---|---|
| 줄이 쥔 바이트 | `load/*`, 새 게이지 `TextureStreaming.bytesInFlight` | 늘 `_maxBytesInFlight` 아래, 대기 상태 바이트 0 |
| 첫 화면 | `load/*` × 워커 1 · 4, `Assets.Pending` 이 0 이 되는 시간과 `AssetLoadScope` `Texture` 가 읽은 바이트 | ③ 뒤 읽은 바이트가 2K 이상 텍스처에서 전체의 10 % 아래, 시간은 ② 보다 흔들림 밖으로 느리지 않다(느리면 첫 로드만 전체 읽기로 되돌리고 값을 S8 에) |
| 올리기 지연 | 화면에 든 뒤 전체 밉까지의 프레임 수(p95) | ② 앞보다 10 프레임 넘게 늘지 않는다(늘면 `_maxBytesInFlight` · 프레임당 제출 수를 올린다) |
| 팩 | `-cooked-content` 로 같은 시나리오 | `Pack.RangeReadOnCompressedEntry` 0 |

**R-15 후속 결정(2026-11-17).**

- **R-15 후속 1 GI 복셀화의 원본은 GPU 다** — `SceneVoxelizer.cpp` 는 `MeshCPUView` 대신 GPU 래스터 복셀화(아래 결정)로 읽는다. 정점 풀은 GPU 원본에서 채우고, 스킨 · 모프 휴지 자세는 옮기지 않는다(아래 결정).
  - 업로드 기록 자리 결정(2026-11-17): 프레임 그래프 실행기가 파이프라인과 무관하게 첫 리스트 앞에 고정으로 기록하는 **프레임 업로드 단계** 하나(파이프라인 XML 의 패스가 아니다 — 풀이 안 채워지는 파이프라인이 생길 수 없다). 정점 풀의 GPU 복사 · 상태 전이를 여기서 기록한다. 풀 정점 버퍼는 GPU 전용(`createGPUVertexBuffer`, DX12 default 힙)이고 인덱스는 CPU 인덱스에서 그대로 올린다. 상용 대조: 언리얼 RDG 의 초기 업로드, 유니티 SRP 의 프레임 시작 CommandBuffer.
  - 모프 · 스킨 휴지 자세 결정(2026-11-17): GPU 원본으로 옮기지 않고 **스킨 · 모프 메시는 CPU 정점을 쥔다(Keep)** — 휴지 자세를 Packed 프록시(snorm8 노멀 · half 색)에서 풀면 노멀이 8 비트로 떨어져 CPU float 휴지 자세와 그림이 갈리고(골든), Vulkan 프록시 정점 버퍼에 STORAGE 용도 · DX12 정점 버퍼의 원시 SRV · 새 컴퓨트 PSO 가 든다. 스킨 · 모프 메시는 캐릭터 몫이라 수가 적다 — 놓기를 켤 때(후속 2) `Mesh::hasSkin` · 모프 타깃이 있는 메시는 만드는 쪽이 Keep 으로 정한다. 상용 대조: 언리얼도 스켈레탈 메시는 CPU 스키닝 · 물리 에셋 때문에 CPU 정점을 쥐는 설정이 기본이다.
  - GI 복셀화 결정(2026-11-17): **기하 셰이더 없는 3 축 투영**으로 간다 — 정점 셰이더가 삼각형 노멀의 가장 큰 축을 골라 그 축 직교 투영으로 그리고, 픽셀 셰이더가 깊이를 복셀 z 로 바꿔 3D UAV 에 원자 쓰기한다(삼각형 축은 컴퓨트 한 번으로 미리 구해 정점 흐름에 싣는다 — 정점 셰이더는 이웃 정점을 모른다). 보존 래스터는 DX12 Tier · Vulkan 확장 · GL/WebGPU 미지원으로 다섯 백엔드에 같은 그림을 못 내 기각하고, 대신 복셀 한 칸만큼 삼각형을 넓히지 않는 대가로 얇은 벽이 빠지는 것은 복셀 크기 절반의 바깥 확장(정점에서 노멀 방향)으로 막는다. 기하 셰이더는 WebGPU · Metal 계열에 없어 기각. 상용 대조: 언리얼 Lumen 의 메시 카드 · 유니티 APV 는 래스터 복셀화가 아니지만, NVIDIA VXGI · CryEngine SVOGI 가 같은 3 축 투영을 쓴다.
- **R-15 후속 2 CPU 정점 놓기** — `MeshCPUAccess` 는 `Mesh::create` · `MeshCache::request` 에 기본값 없이(절차 메시 Keep, 요청이 갈리면 Keep, 놓인 뒤 Keep 요청이면 `.mesh` 다시 읽기), 업로드가 모두 끝난 뒤 놓고 장치 교체 때 `.mesh` 를 다시 읽는다. 상용 대조: 언리얼 `UStaticMesh::bAllowCPUAccess`(꺼진 메시는 CPU 쓰기 — 물리 쿠킹 · 내비 · 복셀 GI — 를 에셋 설정 단계에서 막는다). 의존: 후속 1.

### 자동 렌더 배율: GPU 프레임 시간과 뷰 사각형(설계 2026-11-17, 감사 RN-8)

**문제.**

- 조정기 입력이 벽시계다 — `RenderPacketViewSetup::updateAutoRenderScale` 이 `deltaTime * 1000` 을 넣는다(`RenderPacketViewSetup.cpp:92-93`). vsync 60 Hz 면 프레임 시간이 16.67 ms 아래로 내려가지 않아 "여유"(예산 × 0.8 = 13.3 ms 아래)가 서지 않고, 내려간 배율이 돌아오지 않는다.
- GPU 타임스탬프는 프로파일러가 기록 중일 때만 켜진다 — `GPUPassTimeline::readAndReport` 가 `#if SW_PROFILE_COMPILED` 안에서 `setTimestampEnabled( isRecording() )`(`GPUPassTimeline.cpp:94-105`). Shipping 에는 없다.
- 배율 단계마다 `FrameViewResources::updateViewTransients` 가 풀을 비우고 첨부 · TAA · 깊이 히스토리 · 차폐 타일 · 노출 기록을 다시 만든다(`FrameViewResources.cpp:207-250`) — 프레임이 무거워 배율을 내리는 바로 그 프레임에 할당이 몰리고 히스토리를 잃는다.

**상용 대조.**

- 언리얼 Dynamic Resolution: `FDynamicResolutionHeuristicProxy` 가 GPU 프레임 시간(`r.DynamicRes.FrameTimeBudget`)으로 판단하고, 렌더 타깃은 `r.DynamicRes.MaxScreenPercentage` 크기로 한 번 잡아 `FViewInfo::ViewRect` 안에 그린다. Shipping 에서도 돈다.
- 유니티: `FrameTimingManager`(`FrameTiming.gpuFrameTime`) + HDRP `DynamicResolutionHandler`. RTHandle 이 최대 크기로 잡고 `_RTHandleScale` 로 뷰포트 안쪽만 쓴다.
- Godot: `Viewport.scaling_3d_scale` 을 바꾸면 버퍼를 다시 만든다 — 지금의 우리와 같고 자동 조정이 없다.

**결정.**

1. **GPU 프레임 시간은 늘 컴파일되는 `GPUFrameClock` 이 낸다.** 프레임 그래프의 첫 명령과 마지막 패스 끝에 타임스탬프 한 쌍(`GPUTimestampSlotLayout::kFrameBegin` + 새 `GPUTimestampSlotLayout::kFrameEnd`)을 쓰고 끝난 프레임의 차이를 읽는다. 계측 스위치(`setTimestampEnabled`)의 주인이 `GPUPassTimeline` 에서 이 시계로 옮겨 온다 — 켜는 조건은 "프로파일러 기록 중 또는 자동 배율 켜짐" 이고, 읽은 `RHITimestampFrame` 하나를 프로파일러(`GPUPassTimeline::reportFrame`)도 받는다. 쿼리는 한 벌, RHI ABI 는 그대로다.
2. **렌더 → 게임 스레드는 원자값 하나다.** `GPUFrameClock` 이 `{읽은 순번, 마이크로초}` 를 64 비트 원자값에 싣고 게임 스레드가 락 없이 읽는다. 표본은 2 ~ 3 프레임 늦고 겹치거나 건너뛸 수 있어, 조정기는 순번이 바뀐 표본만 세고 배율을 바꾼 뒤 `_settleSamples`(기본 4 = 링 3 + 1)개는 버린다(옛 배율로 그린 프레임의 시간이다).
3. **조정기의 입력 형식을 바꾼다** — `update( float32 frameMs )` → `update( const GPUFrameSample& )`. 벽시계를 넣는 호출이 컴파일되지 않는다. 타임스탬프가 없는 장치(`findTimestampProvider()` 가 nullptr 이거나 슬롯 0)에서 자동 배율을 켜면 오류를 한 번 남기고 끈다(벽시계로 대신하지 않는다).
4. **뷰 풀은 최대 크기로 한 번 만들고 뷰 사각형 안에 그린다.** 풀 크기 = 출력 × 사용자 배율(`gv_renderScale`) × 자동 배율 상한(1). 자동 배율은 `RenderViewRect`(원점 0,0)만 줄인다 — `updateViewTransients` 의 키가 변하지 않아 할당이 0 이고 히스토리가 남는다. 뷰포트 · 시저 · 컴퓨트 디스패치 크기는 사각형에서 구한다.
5. **버퍼 UV 변환은 `PassTexture`(`binding.hlsli:172`) 안에 둔다.** 패스 첨부를 읽는 셰이더 29 개가 이미 이 묶음을 지나므로 `uv * g_ViewRectScale` 과 가장자리 클램프(`g_ViewRectUVMax`, 반 텍셀 안쪽)를 여기 한 곳에 넣는다. 텍셀 번호로 읽는 쪽(`loadEngineTexel` · `getEngineTextureSize`)은 `PassTexture::getViewRectSize()` 를 쓴다. 지난 프레임 첨부(TAA · 깊이 히스토리)는 지난 사각형 배율(`g_PrevViewRectScale`)로 읽어 단계가 바뀐 프레임에도 히스토리를 버리지 않는다.
6. 조정기 값(예산 · 하한 · 단계 · 연속 프레임 수 · `_settleSamples`)은 `Config/Engine/RenderScale.json` 으로 옮긴다(지금은 구조체 기본값과 gv 둘).

**기각.**

- 벽시계에서 vsync 주기를 빼서 추정: 프레임 페이싱 · 컴포지터에 따라 틀리고 GPU 가 한가한지 알 수 없다.
- CPU 의 `RT.Frame` 시간: 제출까지의 CPU 시간이라 GPU 부하와 무관하다.
- 하드웨어 동적 해상도(유니티 `ScalableBufferManager`): 백엔드 · 드라이버마다 지원이 다르다.
- 단계마다 히스토리만 버리고 재할당은 없애기: 무거운 프레임에 앨리어싱이 한 번 번쩍인다 — 지난 사각형 배율 한 칸이면 막는다.
- 프로파일러를 Shipping 에 넣기: 스코프 전체가 따라 들어온다.

**API 초안.**

```cpp
// Renderer/Frame/Timeline/GPUFrameClock.h — Shipping 포함. 렌더 스레드가 쓰고 게임 스레드가 읽는다.
struct GPUFrameSample
{
    uint32  _serial{ 0 }; // 읽은 순번(1 부터, 0 은 "아직 없음")
    float32 _gpuMs{ 0.0f };
};
enum class GPUFrameClockState : uint8
{
    Off,
    Unsupported,
    WarmingUp,
    Ready
};
class SW_API GPUFrameClock
{
public:
    void setWanted( bool bWanted ); // 게임 스레드(원자)
    void writeFrameBegin( IRHICommandList& cmd ); // 렌더 스레드
    void writeFrameEnd( IRHICommandList& cmd );
    /** 프레임마다 한 번. 스위치를 맞추고 끝난 프레임을 읽는다. 읽었으면 그 프레임(프로파일러가 이어 쓴다), 아니면 nullptr. */
    const RHITimestampFrame* readBack( IRHIDevice& device, bool bProfilerRecording );
    [[nodiscard]] GPUFrameClockState getState() const; // 게임 스레드, 락 없음
    [[nodiscard]] GPUFrameSample     getLatest() const;
};

// RenderScaleAdjuster — 같은 _serial 은 세지 않는다. 배율을 바꾼 뒤 _settleSamples 개는 버린다.
float32 update( const GPUFrameSample& sample );

// RenderView / FrameViewTarget — 풀 크기 안의 그리는 구간(원점 0,0).
struct RenderViewRect
{
    uint32 _width{ 0 };
    uint32 _height{ 0 };
};
// 뷰 상수: float2 g_ViewRectScale, g_ViewRectUVMax, g_PrevViewRectScale, uint2 g_ViewRectSize
```

**측정.**

| 무엇 | 시나리오 · 지표 | 판정 |
|---|---|---|
| 배율 복귀 | 단위 `RenderScaleAdjusterTest`(아래), Shooter3D `benchmark` vsync 켬 + 자동 배율, `RenderPacketBuilder.AutoRenderScale` 로그 | 부하가 걷힌 뒤 `_upFrames` × 단계 수 프레임 안에 1.0 |
| 한 쌍의 비용 | `game/playground/automation/benchmark` dx12 · vk · gl, 시계 켬 · 끔 5 번, `RT.Frame` p50 | 차이가 흔들림 안(밖이면 값을 S8 에 적는다 — 시계는 자동 배율이 켜졌을 때만 켜지므로 결정은 그대로다) |
| 단계 프레임 | 개발 변수로 배율을 0.7 ↔ 1.0 으로 바꾸는 새 시나리오 `renderscalestep`, 새 카운터 `RT.Frame.transientAllocations`, 그 프레임의 `RT.Frame` | 할당 0, 프레임 시간이 중앙값의 1.2 배 아래 |
| 그림 | `renderscalestep` 의 0.7 그림을 `gv_renderScale=0.7`(다시 만드는 길)의 그림과 견준다, 다섯 백엔드 | 차이 1 % 아래, 화면 가장자리에 사각형 밖 텍셀이 번지지 않는다 |

### 입자 정렬 모드(설계 2026-11-17, 감사 RN-15)

**문제.** 정렬 키가 방출기마다 하나다 — `GPUSceneBuilder::fillParticleKeys`(`GPUSceneBuilder.cpp:1255-1267`)가 방출기 위치로 깊이를 재고 주석이 "입자 단위 정렬은 하지 않는다" 고 적는다. 방출기 안에서는 CPU 입자가 배열 순서(`drawInstanced( 6, 입자 수 )`), GPU 입자가 풀 순서로 그려져, 알파 블렌드 연기를 반대편에서 보면 앞뒤가 뒤집힌다. 추가 뷰(CCTV · PiP)도 같은 순서를 쓴다.

**상용 대조.**

- 언리얼 나이아가라: 렌더러마다 `SortMode`(`ENiagaraSortMode` — None · ViewDepth · ViewDistance · CustomAscending · CustomDecending)와 `bSortOnlyWhenTranslucent`. GPU 시뮬은 `FGPUSortManager` 가 뷰마다 컴퓨트로 정렬한다.
- 유니티: VFX Graph 출력의 Sort(Auto · Off · On)와 정렬 기준, Shuriken `ParticleSystemRenderer.sortMode`(None · Distance · OldestInFront · YoungestInFront · Depth).
- Godot: `GPUParticles3D.draw_order`(Index · Lifetime · Reverse Lifetime · View Depth).

**결정.**

1. **방출기 정의에 `VFXEmitterDef::_sortMode`** — `None`(기본) · `ViewDepth` · `OldestFirst` · `YoungestFirst`. 뜻 없는 조합은 로드 오류다: 더하기 블렌드(순서 무관) · 띠(태어난 순서가 모양) · 불투명 메시에 `None` 이 아닌 값.
2. **정렬 결과는 순서 목록(입자 번호의 `uint32` 배열)이다.** 입자 데이터는 옮기지 않고 뷰들이 함께 쓴다. 셰이더는 `g_ParticleOrder[ orderBase + SV_InstanceID ]` 로 입자를 고르고, `None` 방출기는 `orderBase` 가 무효 값이라 인스턴스 번호를 그대로 쓴다.
3. **뷰마다 한 구간.** `ViewDepth` 는 (방출기, 입자를 그리는 뷰)마다 구간 하나다 — 메시 투명의 뷰별 순번(`computeViewTransparentOrders`)과 같은 규칙. 나이 순서 둘은 뷰와 무관해 방출기마다 하나다.
4. **CPU 시뮬**은 렌더 스레드 `VFXParticleRenderer::prepareFrame` 이 뷰의 눈 · 정렬 축(`GPUTransparentSorter::getAxis` — 직교 카메라 포함)으로 깊이 키를 만들어 정렬하고 프레임의 순서 버퍼 하나에 이어 붙인다(방출기마다 독립이라 `runParallelByCost`).
5. **GPU 시뮬**은 `instance_sort.hlsl` 의 그룹 공유 메모리 바이토닉 정렬을 `bitonic_sort.hlsli` 로 떼어 함께 쓴다. 새 컴퓨트 `vfx_particle_sort.hlsl` 이 (방출기, 뷰)마다 워크그룹 하나로 산 입자의 깊이 키를 만들어 정렬하고 순서 구간에 쓴다. 한도는 `SW_SORT_MAX_ELEMENTS`(512)다 — GPU 대상이면서 `ViewDepth` 인데 `_maxParticles` 가 512 를 넘으면 로드 오류다(조용히 정렬을 건너뛰지 않는다. CPU 대상은 한도가 없다).
6. 방출기끼리 · 메시 투명 배치와의 순서(`placeParticles`)는 그대로다 — 상용도 프리미티브 단위다.

**기각.**

- 입자 데이터를 뷰마다 정렬해 복사: 뷰 수만큼 입자 버퍼가 늘고 CPU 입자 배열은 게임 스레드와 나눠 쓰는 불변 값이다.
- 순서 무관 투명(weighted blended OIT): 모든 투명의 그림이 바뀌고 타깃이 는다.
- 모드 없이 알파 방출기를 늘 정렬: 작은 불꽃 · 먼지까지 비용을 낸다.
- 지금 GPU 기수 정렬(크기 제한 없음): 패스 · 버퍼가 늘고 512 개 넘게 정렬할 알파 GPU 효과가 아직 없다 — 그런 효과가 생기면 PS-3 의 측정 값으로 연다.

**API 초안.**

```cpp
// VFX/VFXEffectDef.h
enum class VFXSortMode : uint8
{
    None,
    ViewDepth,
    OldestFirst,
    YoungestFirst
};
PROPERTY( Tooltip = "Order particles are drawn in (alpha-blended Billboard only)" )
VFXSortMode _sortMode{ VFXSortMode::None };
static constexpr uint32 kMaxGPUSortedParticles = 512; // vfx_particle_sort.hlsl 의 SW_SORT_MAX_ELEMENTS 와 계약 검사로 묶는다

// Renderer/Scene/GPUSceneSnapshot.h — GPUParticleBatch 에 VFXSortMode _sortMode 를 더한다.
// Renderer/VFX/VFXParticleOrder.h — 순수 계산(렌더러 없이 테스트한다).
struct VFXParticleOrderRange
{
    uint32 _first{ constant::invalid_index::kUint32 };
    uint32 _count{ 0 };
};
class SW_API VFXParticleOrder
{
public:
    /** 먼 것부터(뒤 → 앞) 그리는 순서를 outListOrder 끝에 붙이고 그 구간을 돌려준다. 같은 깊이는 입자 번호 순(안정). */
    static VFXParticleOrderRange appendViewDepth( vector_reference<const GPUParticle> listParticle, const float4x4& world, const float3& eye, const float3& axis,
                                                  vector<uint32>& outListOrder );
    static VFXParticleOrderRange appendByAge( vector_reference<const GPUParticle> listParticle, bool bOldestFirst, vector<uint32>& outListOrder );
};
// VFXParticleRenderer::drawBatches 가 뷰 번호를 받아 그 뷰의 구간을 건다.
```

**측정.**

| 무엇 | 시나리오 · 지표 | 판정 |
|---|---|---|
| 그림 | 새 시나리오 `game/playground/automation/vfxsort` — 알파 연기 방출기(CPU 하나 · GPU 하나)를 주 시점과 180° 반대편 PiP 로 한 프레임에 본다, 다섯 백엔드 골든 | 두 뷰 모두 먼 입자가 먼저 그려진다. 정렬을 끈 그림과 5 % 넘게 달라야 한다(같으면 장면이 정렬을 드러내지 못한 것) |
| CPU 정렬 비용 | 방출기 32 × 입자 256 의 합성 장면, 새 스코프 `RT.VFX.sortParticles` | 프레임당 0.2 ms 아래(넘으면 깊이를 16 비트로 양자화한 기수 정렬로) |
| GPU 정렬 비용 | 같은 장면 GPU 대상, `GPU.VFXSort` | 0.1 ms 아래 |
| `None` 의 비용 | `benchmark` · VFX 표본 장면의 투명 패스 GPU 시간, 이 일 앞뒤 | 흔들림 안(밖이면 `None` 을 간접 읽기 없는 PSO 변형으로) |

### DX12 · Vulkan 기록 경로 락의 남은 자리(설계 2026-11-17, NEXT RHI 5-2, 감사 R-4 · R-12)

**지금 구조.** DX12 기록 경로는 락 없이 읽는다:

- bindless 읽기: 번호별 고정 크기 표 `D3D12RHIDevice::BindlessReadSlot`(원자값 넷)을 `resolveBufferAddress` · `resolveOfflineView`(`D3D12RHICommandContext.cpp:117-139`)가 락 없이 읽는다. 표가 늘지 않아 등록과 겹쳐도 참조가 매달리지 않는다.
- 상태: `_resourceStateMutex` 와 해시 맵이 없어지고 `RHIHandleTable` 레코드(`TextureRecord::_state` · `BufferRecord::_state`)의 `RHITrackedState::transitionTo` 비교 교환이 한 워커에게만 배리어를 맡긴다. 핸들 표(`SlotHandleTable`)는 `PagedArray` 라 락 없는 `get` 이 삽입과 겹쳐도 안전하다. Vulkan 도 `_imageLayoutMutex` 가 없다.
- 불변식: 등록 · 해제 · 생성 · 파괴는 병렬 기록 구간 밖에서만(`RenderGraph::executeParallel` 이 `setParallelRecording`, 어기면 `requireRegistryMutableNow` 가 모든 구성에서 멈춘다).

**남은 자리**(코드에서 확인).

| 자리 | 무엇 | 결정 |
|---|---|---|
| `D3D12RHIResourceFactory::updateConstantBuffer`(`D3D12RHIResourceFactory.cpp:80-92`) — 병렬 기록 중 `ShaderParameterBinder.cpp:179` 가 부른다 | `_bindlessMutex` 읽기 락 + 해시 찾기(`_mapCbMapped` · `_mapCbAlignedSize` · 미러) + **장치 전역 `RHIConstantBufferMirror::_mutex` 배타 락 아래 memcpy**. 미러는 Vulkan 도 쓴다 | 측정 뒤(L-2). 아래 설계 |
| `D3D12RHIDevice::isBindlessIndexLive`(`D3D12RHIDevice.cpp:2146`) | 진단(`BindlessIndexValidator`, Dev)이 드로우마다 읽기 락 | 락 없이 `BindlessReadSlot::_registry` 를 읽는다(L-1) |
| `acquireOnlineBlock`(`_onlineBlockMutex`) · 리스트 풀(`_cmdListPoolMutex`) | 512 디스크립터 · 리스트 하나마다 한 번인 잎 락 | 둔다(드로우 단위가 아니다) |

**상용 대조.** 언리얼은 기록 스레드마다 `FD3D12CommandContext` 가 자기 `FD3D12FastConstantAllocator` 에서 상수 버퍼 구간을 받아(`RHIUpdateUniformBuffer` 도 갱신마다 새 할당) 기록 중 공유 락이 없다. 유니티 SRP Batcher 는 프레임 전에 큰 버퍼 하나에 모아 올린다. Godot 은 프레임의 유니폼 버퍼를 `RenderingDevice` 락 하나 아래에서 쓴다(지금의 우리 쪽).

**결정(L-2 의 측정이 문턱을 넘을 때).** 상수 버퍼의 기록 경로 값을 버퍼 레코드로 옮긴다 — `BufferRecord` 에 매핑 주소 · 슬롯 크기(만든 뒤 불변)와 미러 값(마지막 바이트 · 채울 슬롯 비트)을 두면 찾기가 칸 번호이고 `_bindlessMutex` 가 필요 없다(파괴는 이미 펜스 뒤라 기록 중 Unmap 과 겹치지 않는다). "채울 버퍼 목록" 은 리스트의 기록 상태(`D3D12RecordingState` · Vulkan 대응)에 쌓고 제출 때 한 스레드가 합친다. 버퍼마다 락이 없어도 되는 근거는 "상수 버퍼는 프레임에 한 번만 쓴다"(감사 R-12, 지금은 글 규칙)이므로, 같은 일에서 레코드의 마지막 쓴 프레임 번호를 비교 교환해 두 번째 쓰기를 오류로 드러낸다.

- 기각 — 감사 R-4 의 구간 할당(`RHIBufferSlice`)까지 기다리기: 그 일이 이 경로를 다시 쓰지만 크기가 크고 순서가 뒤다. 문턱을 넘으면 먼저 하고, 넘지 않으면 R-4 에 맡기고 닫는다.
- 기각 — 미러 락을 버퍼 해시로 나눈 줄무늬 락: 해시 찾기와 공유 락의 캐시 줄 경합이 남는다.

**API 초안.**

```cpp
// D3D12RHIDevice::BufferRecord 와 Vulkan 의 버퍼 레코드에 더한다.
struct ConstantBufferRecordState
{
    void*          _pMapped{ nullptr };   // 만든 뒤 불변
    uint32         _alignedSize{ 0 };     // 링 슬롯 하나의 크기(링이 아니면 0)
    atomic<uint32> _lastWriteFrame{ 0 };  // 같은 프레임의 두 번째 쓰기를 드러낸다(R-12)
    atomic<uint8>  _pendingSlotMask{ 0 }; // 마지막 값을 아직 받지 못한 링 슬롯
};
// Backend/RHIConstantBufferMirror.h — 전역 락 없는 꼴. 값 저장소는 레코드가 든다.
void noteWrite( RHIBufferHandle buffer, vector<RHIBufferHandle>& inoutListDirty ); // 기록 상태의 목록에 붙인다(기록 스레드)
void mergeDirty( vector<RHIBufferHandle>& listDirty );                             // 제출 때(한 스레드)
```

**측정.**

| 무엇 | 시나리오 · 지표 | 판정 |
|---|---|---|
| 들어간 락 제거의 값 | NEXT 4 절 S8 후보의 "DX12 기록 경로 락 제거 전후 `bench.json` p50" — 291d69074e 와 그 앞 커밋, `benchmark` · Shooter3D dx12 5 번 | 값을 S8 에 적고 닫는다(나빠졌으면 그 커밋을 본다) |
| 미러 락 경합 | 같은 장면 dx12 · vk, `-gv_renderGraphParallelMinWidth=2`, 새 스코프 `RT.Bind.mirrorLockWait`(`RHIConstantBufferMirror::_mutex` 를 기다린 시간)의 합과 새 카운터 `RT.Bind.constantBufferWrites` | 대기 합이 `RT.Graph.parallelLevel` 합의 2 % 를 넘으면 L-2 를 한다. 아래면 값을 적고 닫는다 |
| L-2 뒤 | 같은 실행의 RT 프레임 중앙값 | 흔들림 밖으로 빨라지지 않으면 되돌린다 |

### 메시 구조: 인덱스만 · 칸마다 스킨 결과 · 고른 단계만 스키닝(정함)

미뤄 둔 항목이 아니라 정해서 코드에 있는 구조다. 남은 측정(인덱스 전환 뒤 골든 대조 · 캐릭터당 GPU)은 [NEXT](NEXT.md) 4 절.

- **인덱스 버퍼:** 모든 메시가 고유 정점 + 인덱스를 가진다(내장 도형 · 스프라이트 · 절차 메시 포함). 색인 없는 이중 모드는 두지 않는다 — 언리얼 · 유니티 · Godot 과 같다. 구간 · LOD 는 인덱스 범위다.
- **스킨 인스턴스 — 메시 사본 대신 칸마다 결과 구간:** 메시(원본)는 한 벌을 나누고, 스킨을 내는 유닛 · 군중 묶음마다 게임 스레드가 받는 오래 가는 스킨 칸(`AnimationSkinSlot`)이 결과 구간을 가진다(언리얼 `FSkeletalMeshObjectGPUSkin` · 유니티 · Godot 의 인스턴스마다 스킨 버퍼).
  - 팔레트 항목과 인스턴스(`InstanceData::_skinSlot`)가 칸을 싣고, 구간 컴포넌트는 주 메시 컴포넌트의 칸을 따른다. 프레임마다 바뀌는 번호(팔레트 순서)는 쓰지 않는다 — 인스턴스를 매 프레임 다시 올리게 된다.
  - 모프 결과 구간은 칸마다, 원본은 스킨 데이터 번호마다 한 번. 칸 → 결과 시작 표(`g_SwSkinResultOffsets`)는 바뀐 프레임에만 올린다. 모프만 있는 메시는 결과가 메시마다라 배치 값(`_morphVertexBase`)이고, 그 메시를 유닛이 그릴 때 쓰는 고정 스킨 원본(`Mesh::acquireRigidSkinSource`)은 원본 내용 번호마다 한 벌이다.
  - 같은 메시 · 구간 · 머티리얼의 캐릭터가 한 배치다(드로우 수가 캐릭터 수와 무관). 변형이 필요한데 칸이 무효인 인스턴스는 컬링이 가시 목록에 넣지 않는다(레스트 포즈로 대신 그리지 않는다).
  - 잰 것(DX12 Release, 마을 사람 100 명): 배치 3,000 → 30, 그림자 6.8 → 3.2 ms, 불투명 8.3 → 5.1 ms, GT 프레임 24.5 → 17.7 ms.
- **구간 메시 LOD — 고른 단계만 스키닝:** 언리얼 `FSkeletalMeshLODRenderData::RenderSections` 처럼 단계마다 LOD 0 구간 순서의 범위(`MeshLOD::_listSectionRange`)를 두고, 고른 단계만 스킨 캐시가 변형한다(낮은 단계에는 모프가 없다).
  - 구간 컴포넌트는 주 메시의 단계를 따른다. 스킨 칸 · 구간 메시는 CPU 가 고르고, GPU 인스턴스 LOD 와 CPU 선택이 한 규칙(`MeshLODSystem::isGPUSelectable`)을 쓴다. 팔레트가 단계를 싣고 그 단계 정점만 스킨 행으로 낸다(`GPUSkinResultOffset` = 결과 시작 · 단계 첫 정점).
  - 단순화는 `CharacterMeshLOD::buildLODs` — 구간마다 `meshopt_simplify`(경계 잠금, 옷과 몸 틈이 벌어지지 않게), 표는 데이터(`engine/character/characterlod.xml`). 외형의 굽기 · 합치기 · LOD 는 `CharacterAppearanceMeshBuild` 로 워커에서(`AssetManager::launchLoadJob` — 로드 단계라 결정적 실행이 기다린다), 캐시와 걸기는 게임 스레드, 짓는 중인 같은 차림은 키로 나눈다.
  - 잰 것(DX12 Release, `load/townsfolk` 200 명): 스키닝 컴퓨트 7.5 ms · GT 프레임 17.3 ms, 칸마다 스킨 정점 평균 16,653(LOD 0 은 28,696).

## 코어 · 런타임 설계(2026-11-17)

"측정 · 설계가 먼저" 라 미뤄 둔 코어 · 런타임 항목의 설계다. 절마다 문제 · 대조 · 결정과 기각 · 초안 · 이전 순서 · 측정 순서로 적었다(일은 [NEXT](NEXT.md) 5 절). 줄 번호는 main `caf89e45df` 의 것이다. 이득이 없어 닫은 측정 값은 위 "측정과 성능 후보(S8)" 에 적는다. C++ 초안은 C++17 이고 이름은 작업자가 [NamingCpp](agents/NamingCpp.md) 로 다시 대조한다. 외부 제품의 동작(Keycloak · IoStore · MSVC STL …)은 기억 기준이라 고치기 전에 그 판의 문서로 대조한다.

| 절 | 측정 없이 바로 하는 것(결함 · 계약) | 측정이 먼저인 것 |
|---|---|---|
| 할당 · 컨테이너 | — | `unordered_map` 예약 · `map` 범위 구축(벤치 문턱), mimalloc 은 사용자 확인도 |
| 병렬 오브젝트 | 없음 | 서비스 파도 · 캐릭터 일괄 이동, 격자 셋은 옮기지 않는다 |
| 리플렉션 | — | 생성 바이트 다음 후보 · 타입 색인 |

### 구독의 제어 블록 — 출처보다 오래 사는 잠금과 해제 중 대기(설계 2026-11-17, 감사 C-20 · 다음 할 일 42)

**문제.** `Subscription::reset`(`Source/Core/Delegate/Subscription.h:208-223`)에 해제 뒤 사용이 넷 있다.

1. `_pSource` 를 잠금 없이 읽고 그 포인터의 `_listLock` 을 잡는다 — 그 사이 출처가 소멸하면 해제된 메모리의 잠금을 잡는다.
2. 잠금을 놓은 뒤 `pSource->releaseSubscription( id )`(:222)를 부른다 — 그 사이 소멸이면 해제된 객체의 가상 호출이다.
3. 출처 이동(`takeSubscriptionsFrom`, :192)과 겹치면 옛 출처의 잠금을 쥔 채 새 출처의 목록을 고친다.
4. `~SubscriptionSource` 가 목록을 비울 때는 파생 소멸자가 이미 끝났다 — 2 의 호출이 소멸된 파생 멤버를 만진다.

머리말은 이 겹침을 "계약 밖" 이라 적지만 형식은 막지 않는다. 여러 스레드가 쓰는 출처(`LockedMulticastDelegate` — 로그 리스너 · 언어 바뀜, `AtomicSlot` — 태스크 프로파일 훅)는 모듈 언로드 · 종료 순서에서 실제로 겹칠 수 있다. `LockedMulticastDelegate.h` 머리말("약 2 초 뒤 경고하고 돌아옵니다")도 코드(`waitForRetiredBroadcasts` — 끝까지 기다리고 10 초마다 알림)와 어긋난다.

**대조.** 언리얼 `FDelegateHandle` 은 번호뿐이라 역참조가 없고(해제를 호출부가 기억 — 우리가 버린 형식), 스레드 정책은 형식이다(`TMulticastDelegate` 대 `TTSMulticastDelegate`, `TSharedPtr<T, ESPMode>` 의 `FReferenceControllerBase` 가 객체보다 오래 사는 제어 블록). 유니티 C# 이벤트는 GC 가 대상을 살려 둬 매달림이 없다(대신 누수). Godot `Callable` 은 `ObjectID`(세대 든 번호)를 전역 `ObjectDB` 에서 풀 때마다 확인한다. Qt `QMetaObject::Connection` · boost::signals2 `connection` 은 연결마다 참조 세는 본문을 둔다.

**결정.** 잠금 · 목록 머리 · 출처 포인터를 출처 밖 **제어 블록**(출처당 하나, 참조 수 = 출처 1 + 살아 있는 구독 수)으로 옮긴다. 구독이 잡는 잠금이 구독이 살려 둔 메모리에 있으므로 1 · 3 은 모양째 사라진다. 2 · 4 는 **해제 중 고정(pin) + 닫기 대기**로 막는다: `reset` 이 잠금 안에서 `_releasingCount` 를 올리고 잠금 밖에서 `releaseSubscription` 을 부른 뒤 내리며, 출처는 `closeSubscriptions()`(파생 소멸자 첫 줄)에서 `_pSource` 를 지우고 수가 0 이 될 때까지 기다린다. 기다림은 끝까지 한다(포기하면 해제 뒤 사용이다). 자기 해제 안에서 출처를 닫는 스레드는 기다리지 않는다(`_s_broadcastDepth` 와 같은 규칙). 등록 API 서명(`issueSubscription` · `releaseSubscription` · `detachSubscription` · `findIssuedID`)은 그대로라 구독을 쓰는 331 파일은 바뀌지 않는다.

- 기각 — 연결마다 제어 블록(signals2 · Qt): 구독마다 할당한다. 출처당 하나로 같은 수명 보장을 얻는다.
- 기각 — 번호 + 전역 표(Godot `ObjectDB`): 숨은 싱글턴이고 풀 때마다 표 조회다.
- 기각 — 계약 + 스레드 확인 단언(감사의 제안): 실행 때만 잡고, 정당한 여러 스레드 출처는 그대로 남는다.
- 기각 — 해저드 포인터 · RCU(folly `hazptr`): 등록 · 해제는 콜드 경로라 기계가 과하다.
- 기각 — 침입형 목록을 없애고 구독 = 블록 + 번호: `SubscriptionList::detach( id )`(`LocalStoreChannel.cpp:189`, 끝난 요청의 `isActive` 를 끈다)가 구독 하나를 무효화할 길을 잃는다.

**초안.**

```cpp
// Source/Core/Delegate/Subscription.h
class SubscriptionControl final // 출처가 첫 구독을 낼 때 한 번 할당한다(구독 없는 델리게이트는 할당 0)
{
    friend class Subscription;
    friend class SubscriptionSource;
    SpinLock            _lock;           // 아래 칸과 구독의 _pPrev · _pNext · _id 를 지킨다
    SubscriptionSource* _pSource;        // 닫히면 nullptr
    Subscription*       _pHead;
    atomic<uint32>      _refCount;       // 0 이 되면 블록을 지운다
    atomic<uint32>      _releasingCount; // 잠금 밖 releaseSubscription 안에 있는 구독 수(0 이 되면 Futex 로 깨운다)
};

class [[nodiscard]] Subscription
{
public:
    bool isActive() const noexcept; // 잠금 안에서 _id != 0(풀렸거나 출처가 무효화하면 0)
private:
    SubscriptionControl* _pControl{ nullptr }; // 이 구독만 쓴다(출처가 남의 구독 칸을 잠금 밖에서 읽고 쓰는 길이 없다)
    Subscription*        _pPrev{ nullptr };
    Subscription*        _pNext{ nullptr };
    uint64               _id{ 0 };             // 크기는 32 B 그대로
};

class SubscriptionSource
{
protected:
    void closeSubscriptions() noexcept;        // 무효화 + _pSource 지움 + 진행 중인 해제 대기. 파생 소멸자 첫 줄
    void detachAllSubscriptions() noexcept;    // 무효화만(출처는 산다 — removeAll). 기다리지 않는다
    virtual ~SubscriptionSource();             // 닫히지 않았으면 SW_FATAL — 빠뜨린 파생은 그 형식을 지우는 첫 테스트에서 멈춘다
private:
    SubscriptionControl* _pControl{ nullptr }; // SpinLock + 머리(16 B) 자리에 포인터 하나
};
```

이동 가능한 출처(`MulticastDelegate` · `DelegateSlot`)는 한 스레드 형식뿐이다(여러 스레드 출처 둘은 이동이 지워져 있다) — 이동은 블록 포인터를 넘기고 잠금 안에서 `_pSource` 만 고친다. `MulticastDelegate` 의 등록 목록 자체는 지금처럼 한 스레드 계약이고 Debug 의 `RaceDetectContext` 가 본다.

| 비용 | 지금 | 뒤 |
|---|---|---|
| 출처 크기 | 16 B | 8 B |
| 첫 구독 | 잠금 1 | 할당 1(32 B) + 잠금 1 + 원자 1 |
| 구독 이동 | 잠금 1 | 같다 |
| 해제 | 잠금 1 + 가상 호출 | 잠금 1 + 원자 3 + 가상 호출 |
| 출처 소멸 | 잠금 1 | 잠금 1 + 원자 2(겹칠 때만 대기) |

**측정.** `SW_TEST_CASE( DelegateTest, SubscribeCost, ._tags = "bench" )` — `MulticastDelegate` 리스너 1 · 8 · 64 의 `add` + `reset` ns/짝, 구독 1 개짜리 출처 1 만 개 만들고 지우기(할당 포함)를 이 커밋과 그 앞에서 5 번씩. 씬은 `load/characters` · `load/housingbulk` 의 `Scene.load` 와 `MemoryProfiler` 할당 수. **문턱:** ns/짝 증가가 30 ns 를 넘거나 씬 로드 차이가 흔들림 밖이면 출처가 생성자로 스레드 정책을 고르게 한다(한 스레드 출처는 원자 연산 · 대기 없음 — 언리얼 `ESPMode::NotThreadSafe` 자리). 흔들림 안이면 정책 하나로 둔다. 결함 수정이라 되돌리지는 않는다. 수명 확인은 `linux-verify` 의 `sanitize`(ASan)와 `sanitize thread`(TSan, 손으로 한 번).

근본 원인은 "약한 역참조가 가리키는 쪽의 잠금을 믿는다" 이다. 같은 모양(주인 주소를 든 해제 델리게이트 — `SubscriptionList::ReleaseFunction`)은 주인이 목록을 맨 끝 멤버로 두는 규칙에 기대므로 C20-2 가 `SubscriptionList` 소멸자에서 닫고 기다리면 같이 닫힌다.

### 비밀번호 해시 연산 줄과 저장소 대기 상한(설계 2026-11-17, 감사 O-3 · 다음 할 일 30)

작업자는 `security-and-hardening` 을 먼저 부른다.

**문제.** 로그인 일 하나(`LoginService::submitCall`, `LoginService.cpp:595` → `LoginStoreRun::runCall`)가 저장소 워커에서 읽기 · Argon2id · 커밋을 모두 한다. 해시 자리는 `LoginStoreLogic.cpp` 의 `registerAccount`(:526), `login`(:590, 매개변수가 바뀌면 :611 에서 한 번 더), `linkCredential`(:958), 없는 이름의 시간 맞추기 `burnPasswordHash`(:1436)다. 해시 하나가 19 MiB · 2 회라 워커(연결) 수만큼의 동시 로그인이 원장 · 우편 · 거래의 일을 뒤에 세운다. `SQLConnectionPool::submit`(`SQLConnectionPool.cpp:138-149`)의 대기열은 상한이 없어 넘치는 요청이 메모리로 쌓인다.

**대조.** 언리얼 · 유니티 · Godot 에는 계정 서버가 없다(EOS · UGS 는 관리형). OWASP Password Storage Cheat Sheet 는 KDF 의 동시 실행 수를 묶으라 하고, Keycloak 24 의 Argon2 제공자는 동시 해시 수를 코어 수로 묶는다(`cpu-cores`). Nakama 는 bcrypt 를 요청 고루틴에서 바로 돌려 묶는 것이 DB 풀뿐이다 — 지금 우리와 같은 모양이다. 대기 상한은 Envoy overload manager · gRPC 의 수락 제어(넘치면 바로 거절, 클라이언트가 재시도)를 따른다.

**결정.**

1. **세 단계.** 읽기(저장소) → 해시(연산) → 커밋(저장소). 로그인 로직은 이미 낙관적 버전 조건(`accountRaw._version`, Conflict 면 다시 읽기)이라 읽기와 커밋 사이가 벌어져도 정합성 규칙이 그대로다. 커밋이 Conflict 면 다시 읽어 솔트 · 매개변수 · 저장된 해시가 같을 때 계산한 해시를 다시 쓰고, 다르면 해시 단계로 돌아간다(`kStoreRetryLimit` 안에서).
2. **해시가 저장소 스레드에서 컴파일되지 않게.** `ILoginCrypto` 에서 `computePasswordHash` 를 떼어 `IPasswordHasher` 로 옮기고 `LoginStoreLogic` 은 그것을 받지 않는다.
3. **연산 줄은 `TaskManager` 위의 상한 있는 줄.** 동시 실행 `_maxConcurrent`, 대기 `_maxQueued`. 자리는 이동 전용 티켓으로만 얻고 `submit` 이 티켓을 요구하므로 수락 검사를 건너뛰는 호출은 쓸 수 없다. 자리가 없으면 읽기 전에 `OnlineError::kBusy`(재시도 가능, 멱등 응답으로 기억하지 않는다).
4. **없는 이름도 같은 길.** 읽기 단계가 설정 매개변수로 가짜 문제를 만들어 같은 줄을 지나고 커밋 단계가 `WrongCredentials` 를 낸다(지금의 `burnPasswordHash` 와 같은 비용 · 같은 시간).
5. **저장소 대기 상한은 앞문 하나.** `OnlineServiceHost` 의 요청 진입(`OnlineServiceHost.cpp:617-628` 도배 제한 다음)에서 `IServiceStore::getPendingCount() >= _maxStorePending` 이면 `kBusy` 로 답한다(Hello · Ping 은 뺀다). 이미 받은 요청의 뒤 단계(커밋)는 거절하지 않는다 — 받은 요청은 끝난다.

- 기각 — 전용 스레드 풀: 스레드 주인이 하나 늘고 `TaskManager` 와 코어를 다툰다(`ThreadCreationKey` 머리말의 까닭). 동시 실행 상한이 프레임 일을 지킨다.
- 기각 — `ServiceStoreTracker::submit` 이 거절을 돌려주기: 키트 서비스마다의 모든 제출이 중간 단계 거절을 다뤄야 하고, 읽은 뒤 커밋이 거절되는 반쯤 한 요청이 생긴다.
- 기각 — 저장소 워커만 늘리기: 연결 수가 해시 수와 묶여 SQLite 쓰기 하나 계약과 부딪힌다.

**상한 사이의 관계.** 해시가 저장소 밖으로 나가면 저장소 일은 밀리초 단위다. 저장소의 최악 대기는 `_maxStorePending + _maxConcurrent + _maxQueued`(뒤 단계 몫) + 유지 일이고, 메모리 상한은 `_maxConcurrent × 19 MiB × 2`(다시 해시)다. `_maxQueued` 는 "기다릴 시간 × 해시 처리율" 로, `_maxStorePending` 은 "워커 수 × 목표 대기 ÷ 평균 일 시간" 으로 정한다 — 값은 `Config/Server/Online/*.online.json` 에 적고 없으면 기동 오류다.

**초안.**

```cpp
// Account/Server/LoginTypes.h — ILoginCrypto 에는 fillRandom · computeKeyedHash 만 남는다
class IPasswordHasher
{
public:
    virtual ~IPasswordHasher() = default;
    [[nodiscard]] virtual bool computePasswordHash( string_view password, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& params,
                                                    uint8* pOut, int32 outSize ) = 0;
};

// Account/Server/Rule/LoginStoreLogic.h — 저장소 스레드. 생성자가 IPasswordHasher 를 받지 않는다
struct PasswordHashChallenge // 읽기 단계의 답: 무엇을 해시할지와 커밋 조건
{
    string                _nameKey;
    ServiceRecord         _accountRaw; // 버전이 커밋 조건. 없는 이름이면 빈 레코드
    NetPasswordHashParams _params;
    uint8                 _arrSalt[LoginConstant::kSaltSize];
    uint8                 _bKnownAccount : 1;
    uint8                 _bRehash       : 1; // 설정 매개변수가 바뀌었다 — 새 솔트 · 새 값도 계산한다
};
LoginServiceResult readLoginChallenge( const LoginCredential& credential, int64 nowMs, PasswordHashChallenge& outChallenge, LoginGrant& outGrant );
LoginServiceResult commitLogin( const PasswordHashChallenge& challenge, const PasswordHashOutcome& outcome, int64 nowMs, LoginGrant& outGrant );

// Account/Server/Service/PasswordHashQueue.h — 서비스 틱 스레드가 주인
class [[nodiscard]] PasswordHashTicket; // 이동 전용. 소멸이 자리를 돌려준다
class PasswordHashQueue
{
public:
    [[nodiscard]] expected<void> initialize( TaskManager& taskManager, IPasswordHasher& hasher, const PasswordHashQueueConfig& settings );
    [[nodiscard]] sw::optional<PasswordHashTicket> tryReserve(); // 진행 + 대기가 상한이면 빈 값 — 호출자가 kBusy
    void  submit( PasswordHashTicket ticket, PasswordHashInput&& input, const CompleteFunction& onComplete );
    int32 pollCompletions();    // 완료를 서비스 틱 스레드에서 부른다
    int32 getOpenCount() const; // LoginService::getPendingCount 에 더한다
    void  shutdown();           // 돌던 해시는 끝까지(waitStage), 대기는 취소 완료로
};
```

`registerAccount` · `linkCredential` 도 같은 줄을 쓴다(읽기 단계가 없으면 해시 → 커밋 두 단계). 비밀번호는 해시가 끝난 워커에서 지운다(지금의 `clear` 관례 그대로).

**측정.** 부하 봇 `login_storm` 을 `_bLightPasswordHash` 없이(새 구성 파일), 해시 처리율의 0.5 · 1 · 2 배 도착률로 돌린다. 도착률 고정은 O-12 의 열린 루프가 있어야 하고, 그 전에는 닫힌 루프로 대기 최고치만 본다. 볼 값: `service_store_pending_peak`, 새 지표 `login_hash_queue_depth` · `login_hash_wait_ms` · `service_requests_shed_total`, 폭주 중 다른 키트(`chat_send` · `leaderboard_top`)의 p99, 프로세스 최고 메모리. **문턱:** 2 배 도착률에서 다른 키트 p99 가 한가할 때의 1.5 배 이내, 저장소 대기 최고치가 상한 식 이내, 최고 메모리가 기준 + `_maxConcurrent × 38 MiB` 이내, 거절은 모두 `kBusy`(시간 초과 · 끊김 0). 고치기 전 값도 같은 표에 남긴다.

근본 원인은 "느린 연산과 연결 점유가 한 일에 묶였다" 이다. 같은 모양 후보는 영수증 검증(이미 `_verifications` 로 저장소 밖)과 저장소 일 안의 서명 · 압축이다 — O3-2 작업자가 저장소 람다 안의 `ILoginCrypto` 밖 암호 호출을 한 번 찾아 보고에 적는다.

### 팩 규모 — 스트림 쓰기 · 블록 표 · 색인 서명 · 해시 캐시(설계 2026-11-17, 감사 A-9 ~ A-12 · 다음 할 일 18)

**문제.**

- **쓰기(A-10).** `PackWriter::addBytes`(`PackWriter.cpp:132-139`)가 모든 항목의 원본을 들고, `makePackBytes` 가 압축 결과와 팩 이미지를 한 벡터로 지은 뒤 쓴다(:507). `CookStore.cpp:90-98` 은 쓴 출력을 다시 읽어 해시한다. 팩은 도메인당 하나다.
- **색인(A-12).** FAT 은 디스크에서 이미 경로 해시 오름차순인데 `PackReader::loadIndexTable`(`PackReader.cpp:464-510`)이 항목마다 경로 `string` 사본을 지어 `unordered_map<uint64, PackFileEntry>` 에 넣고, 폴더 나열용 `unordered_map<string, vector<uint64>>` 를 또 짓는다. 읽기는 항목 통째(`readFile` · `readFileAsync`)뿐이라 밉 범위 읽기(R-6)가 막힌다.
- **서명(A-9).** `PackManager::verifyPatchSignatures`(`PackManager.cpp:685-703`)가 기동마다 사슬의 패치 팩 전체를 메모리로 읽어 SHA-256 을 세고(`PackPatch.cpp:86-99`, 2 GiB 이상 거절), 그 바이트와 뒤에 마운트하는 핸들이 다르다.
- **증분 쿠킹(A-11).** `CookRecord::findChangedInput`(`CookRecord.cpp:154-158`)이 매번 모든 입력을 해시한다. `ContentHashCache`(`ContentHash.cpp:122-158`)는 도장(크기 · 시각)으로 거르지만 프로세스 메모리뿐이다. `ContentHash::computeBytes`(:54-66)는 바이너리에서도 0x0D 를 건너뛰어 그 자리만 다른 두 파일이 같은 해시다(결함).

**대조.** 언리얼 IoStore 는 `.ucas` 에 청크를 이어 쓰고 끝에 `.utoc`(완전 해시 TOC + 64 KiB 압축 블록 표 + 블록 해시 서명)를 쓰며 컨테이너를 크기 · 청크 id 로 나눈다. pak 은 `.sig` 블록 해시 표다. 유니티 AssetBundle 은 LZ4 청크 단위 압축이라 부분 읽기가 되고 Addressables 카탈로그가 묶음 해시를 든다. Godot PCK 는 파일마다 MD5 를 두지만 블록 압축 · 부분 검증은 없다. 증분 판정은 git 색인(도장 → 해시, 쓴 시각과 겹친 도장은 믿지 않는다)과 언리얼 `FFileHashCache` 다.

**결정.**

1. **형식 3 판 — 색인을 끝에.** 구역 순서를 헤더 · 페이로드 · FAT · 블록 표 · (블록 해시 표) · 스트링 풀 하나로 한다(지금의 `Slot` 배치 순서, 리더는 이미 헤더의 오프셋만 믿는다). `PackLayoutMode` 는 페이로드 자리 규칙으로만 남는다. 구현(PK-3): 헤더는 **파일 끝**(96 B, 언리얼 pak `FPakInfo` 자리) — 앞에 두면 마지막에 정해지는 칸 때문에 앞에서부터 쓰며 내용 해시를 셀 수 없다. 색인 구역은 `_indexOffset` 부터 파일 끝까지 한 구간이다.
2. **스트림 쓰기.** 항목을 경로 해시 순으로 하나씩(병렬 압축은 바이트 창 `_writeWindowBytes` 안에서, 쓰기는 순서대로) 읽고 · 압축하고 · 이어 쓴 뒤 색인과 헤더를 쓴다. 쓰는 동안 내용 해시와 색인 SHA-256 을 섞어 돌려주므로 다시 읽지 않는다. 구현(PK-3): 색인 SHA-256 은 PK-5 가 `PackWriteResult::_indexOffset` 구간으로 센다(쓰기가 보안 제공자를 알지 않게).
3. **블록 표.** 모든 항목을 `_blockSize`(PackConfig, 2 의 거듭제곱 64 ~ 256 KiB — 첫 값 128 KiB)로 잘라 블록마다 압축한다. 작은 항목은 블록 하나다. CRC 는 블록마다로 옮긴다. `readRange` 가 걸친 블록만 읽는다.
4. **색인 서명.** 패치 목록은 팩 전체 대신 **색인 구역**(헤더 · FAT · 블록 표 · 블록 해시 표 · 스트링 풀)의 SHA-256 을 서명한다. 리더가 여는 핸들로 색인을 검증한 뒤에만 마운트하고(검증한 핸들이 곧 읽는 핸들), 서명 채널의 팩(`PackFlag::HasBlockHash`)은 읽을 때 블록마다 SHA-256 을 견준다. 기동 비용은 색인 크기에만 비례하고 2 GiB 한도가 사라진다.
5. **정렬 배열 색인.** 리더는 디스크 FAT 을 그대로 든다(정렬이 아니면 마운트 거절, 이진 탐색). 경로는 스트링 풀을 가리키는 `string_view`, 폴더 나열은 (폴더 해시, 항목 번호) 정렬 배열이다.
6. **분할.** PackConfig `_split`(없으면 나누지 않는다, 있으면 `_maxPackBytes` 필수 + 패턴 → 청크 이름 규칙). 경로 해시 순으로 잘라 결정적이고, 이름은 `<도메인>.<청크>.<번호>.pack`. 마운트 도메인은 풀 머리에 있어 리더는 그대로 붙이고, 같은 도메인의 형제 팩 사이 겹침은 오류다(뒤가 이기는 것은 패치뿐).
7. **해시 캐시 영속.** `ContentHashCache` 를 `DerivedData` 에 쓴다(경로 → 크기 · 수정 시각 · 해시). 저장 시각의 시간 창 안에 고쳐진 도장은 싣지 않는다(지금의 `isCompleted` 규칙을 파일로 넓힌 것). 캐시 파일이 없거나 판이 다르면 빈 캐시다 — 캐시라 폴백이 아니다.
8. **내용 해시는 원시 바이트.** CR 건너뛰기를 없앤다. `.gitattributes` 가 `* text=auto eol=lf` 라 추적 파일은 어느 기계에서나 같은 바이트다. 쿠킹 키 판이 올라 한 번 전체 재쿠킹이다.

- 기각 — CR 정규화를 텍스트 확장자에만(감사의 제안): 무엇이 텍스트인지를 `.gitattributes` 와 해시 코드 두 곳이 알게 된다.
- 기각 — 해시를 xxh3 로(사용자 판단 2026-11-17: 들이지 않는다): 지금 Content 전체를 FNV-1a 로 한 번 해시해도 0.2 초 안팎(추정)이고 영속 캐시가 있으면 바뀐 파일만 해시한다. 실행 중 블록 해시 검사(PK-5)가 느리면 의존성 없이 하드웨어 CRC32C(SSE4.2 · ARMv8 CRC)로 `HashUtil::computeCRC32` 를 바꾼다.
- 기각 — 완전 해시 색인(IoStore): 쿠커와 리더가 해시 씨앗을 맞춰야 하고, 30 만 항목 이진 탐색은 19 단계다. 조회 문턱을 넘으면 그때 해시 앞 8 비트 버킷 표를 더한다.
- 기각 — 큰 항목에만 블록 표: 읽기 길이 둘이 된다. 블록 표 한 줄 8 B 라 30 만 항목에 2.4 MB 다.

**초안.**

```cpp
// PackTypes.h — kPackFormatVersion = 3
struct PackHeader // 96 B. 기존 칸 뒤에 더한다
{
    // … _totalDataSize 까지 그대로 …
    uint64 _blockTableOffset{ 0 }; ///< PackBlockEntryOnDisk × _blockCount
    uint64 _blockHashOffset{ 0 };  ///< PackFlag::HasBlockHash 일 때 SHA-256(32 B) × _blockCount, 아니면 0
    uint32 _blockCount{ 0 };
    uint32 _blockSize{ 0 };        ///< 압축 전 블록 크기
    uint8  _reserved[10]{};
};
struct PackFileEntryOnDisk // 32 B 그대로 — _crc32 자리가 _firstBlock 이 된다
{
    uint64 _pathHash;
    uint64 _dataOffset;
    uint32 _compressedSize;
    uint32 _uncompressedSize; ///< 블록 수 = ceil( _uncompressedSize / _blockSize ), 빈 항목은 0
    uint32 _firstBlock;
    uint32 _stringPoolOffset;
};
struct PackBlockEntryOnDisk // 8 B
{
    uint32 _compressedEnd; ///< 항목 _dataOffset 기준 누적 끝 — 블록 i 의 자리가 O(1)
    uint32 _crc32;         ///< 압축 전 블록의 CRC32
};

// PackWriter.h
class PackStreamWriter
{
public:
    [[nodiscard]] expected<void> open( const DataPath& packPath, const PackWriterDesc& desc );
    [[nodiscard]] expected<void> append( string_view relativePath, span<const uint8> bytes ); // 경로 해시 오름차순이 아니면 오류
    [[nodiscard]] expected<PackWriteResult> close(); // 색인 · 헤더를 쓰고 내용 해시 · 색인 SHA-256 · 통계를 돌려준다
};

// PackReader.h
[[nodiscard]] expected<void> open( string_view packFilePath, const PackIndexTrust* pTrust ); // pTrust 가 있으면 색인 서명을 확인한 뒤에만 연다
[[nodiscard]] bool readRange( uint64 pathHash, uint64 offset, uint32 size, vector<uint8>& outBytes ) const;
AsyncReadHandle    readRangeAsync( AsyncFileIO& io, uint64 pathHash, uint64 offset, uint32 size, AsyncIOPriority priority,
                                   const AssetReadCompleteDelegate& onComplete ) const;

// ContentHash.h
[[nodiscard]] expected<void> ContentHashCache::load( const DataPath& cachePath );
[[nodiscard]] expected<void> ContentHashCache::save() const; // 임시 파일에 쓰고 바꿔 끼운다
```

**측정 — 30 만 항목 합성 팩.** `PackScaleTest`(`._tags = "bench"`)가 스크래치 폴더에 합성 팩을 짓는다: `index300k`(30 만 × 256 ~ 1024 B, 세 단계 폴더 · 경로 48 자 안팎), `patch1g`(1 GiB 패치 + 목록), `bulk10g`(2000 × 5 MiB, 절반은 압축되는 바이트 — 디스크 10 GiB 라 손으로만). 고치기 전 값을 먼저 S8 에 적는다.

| 값 | 재는 법 | 문턱 |
|---|---|---|
| `Pack.Scale.mountMs` | `index300k` 마운트 5 번 중앙값(따뜻한 캐시) | 60 ms 이하이고 지금의 절반 이하 |
| `Pack.Scale.indexBytesPerEntry` | 마운트 앞뒤 `MemoryTag` 증가 ÷ 항목 수 | 48 B + 풀 바이트 이하 |
| `Pack.Scale.lookupNs` | `hasFile` 무작위 100 만 번 | 지금 해시 맵의 1.5 배 이내 — 넘으면 버킷 표를 더한다 |
| `Pack.Scale.writePeakBytes` | `bulk10g` 쓰기의 프로세스 최고 메모리 | `_writeWindowBytes` × 3 + 색인 이하(콘텐츠 크기와 무관) |
| `Pack.Scale.patchVerifyMs` | `patch1g` 서명 확인 | 10 ms 이하(색인 크기에만 비례) |
| `Pack.Scale.rangeReadBlocks` | 64 MiB 항목의 256 KiB 범위 | 읽은 블록 3 개 이하 |
| 변경 없는 `--cook-all` 두 번째 | `Headless.CookStageTimes` + 읽은 입력 바이트 | 입력 읽기 0 바이트(도장만), 벽시계 지금의 1/5 이하 |

블록 크기는 `index300k` · Shooter3D 실제 팩에서 64 · 128 · 256 KiB 의 팩 크기와 첫 진입 `Asset.mount` · 로드 시간으로 고른다(크기 차이 2 % 안이면 128 KiB).

근본 원인은 "팩 하나를 한 벡터로 다룬다" 이다. 같은 모양 후보는 DDC 저장(`DerivedDataCache::store` 가 출력 통째)과 `PackPatchBuilder` 의 앞 빌드 팩 읽기다 — PK-3 작업자가 통째 읽는 자리를 세어 보고에 적는다.

쿠킹본 실행(감사 A-13, 2026-11-19 판단): 개발 `--cook-all` 과 배포 CookAssets 는 이미 한 쿠킹 순서(셰이더 · 번역 표 굽기 → 팩)였고, 개발에서 팩에 셰이더 · `.locres` 가 빠진 것은 상대 `--cooked-dir=../Cooked` 의 `..` 가 명령줄에서 풀리지 않아 포함 판정(`tryMakePathUnderRoot`)이 스테이징을 모두 밖으로 보고 조용히 넘긴 탓이다 — 언리얼 `FPaths::ConvertRelativePathToFull` 처럼 받는 자리(`AbsolutePath::tryResolve`)에서 `.` · `..` 를 풀고, 팩 목록의 넘김은 오류로 바꿨다. 이 길은 `Pack.CookedContent`(Release · Empty · DX12)가 묶음마다 돈다.

### 출하 할당기와 컨테이너 복잡도(설계 2026-11-17, 감사 C-6 · C-9 · 다음 할 일 20)

감사의 순서대로 **벤치가 먼저**다. 벤치 숫자가 문턱을 넘은 것만 고치고, 넘지 않으면 값을 S8 에 적고 닫는다.

**C-6 출하 할당기 — 문제.** Shipping 의 `Memory::allocate` · `allocateAligned`(`Source/Core/Memory/Memory.cpp:148-158 · 224-234`)가 `::malloc` · `_aligned_malloc` 그대로다. 스레드 캐시와 크기 등급이 없어 워커가 함께 할당하면 CRT 힙 잠금에서 다툴 수 있다(측정 필요).

**대조.** 언리얼은 `FMallocBinned2/3`(크기 등급 + 스레드 캐시)가 기본이고 mimalloc 을 고를 수 있다. 유니티는 TLSF `DynamicHeapAllocator` + 스레드별 버킷 + 잡용 선형 할당자다. Godot 는 시스템 `malloc` 에 `PagedAllocator` 풀을 얹는다.

**결정.** 두 걸음으로 잰다. ① 의존 없이: 프레임당 할당 수 × CRT 한 번 비용으로 프레임 몫을 추정한다. ② 문턱을 넘으면 mimalloc(MIT)을 `Memory` 한 입구 뒤에 Shipping 만 끼워 재고, 이득이 흔들림 밖이면 둔다. 입구는 변형 파일 하나로 가른다([BuildConfigSwitch](agents/BuildConfigSwitch.md)). **mimalloc(MIT) 도입은 사용자가 확인했다(2026-11-17) — 백엔드 이름이 공개 헤더에 나오지 않게 감싸 나중에 바꿔도 밖이 모르게 한다. 기본값은 ② 의 판정으로 정한다.**

- 기각 — 자체 크기 등급 할당기: mimalloc 이 하는 일을 다시 쓰는 몇 달이다.
- 기각 — jemalloc · tcmalloc: Windows · Android · wasm 지원이 고르지 않다.
- 기각 — 전역 `operator new` 교체 · `mimalloc-redirect`: 서드파티 라이브러리의 힙과 섞인다. `Memory` 한 입구만 바꾸면 서로의 블록을 풀 일이 없다.
- 기각 — 타입별 풀 먼저: NEXT "메모리 풀은 측정 뒤" 그대로다.

```cpp
// Source/Core/Memory/MemoryBackend.h — Memory.cpp 의 Shipping 분기가 부르는 한 벌
struct MemoryBackend
{
    [[nodiscard]] static void* allocate( size_t size ) noexcept;
    [[nodiscard]] static void* allocateAligned( size_t size, size_t alignment ) noexcept;
    [[nodiscard]] static void* reallocate( void* pBlock, size_t newSize ) noexcept;
    static void                free( void* pBlock ) noexcept;
    static void                freeAligned( void* pBlock ) noexcept;
};
// 구현은 CMake SW_MEMORY_BACKEND 가 고르는 변형 파일 하나: MemoryBackend.CRT.cpp | MemoryBackend.Mimalloc.cpp
```

**측정과 문턱.**

| 걸음 | 재는 법 | 문턱 |
|---|---|---|
| ① 할당 수 | Release, Shooter3D `benchmark` · `load/characters` · `load/housingbulk` · 부하 봇 서버에서 `MemoryProfiler` 의 프레임당 할당 수(게임 · 렌더 · 워커) | — |
| ① 한 번 비용 | `MemoryBenchTest.AllocateFreeCost`(bench) — 스레드 1 · 4 · 8 × 16 · 64 · 256 · 4096 B × 같은 스레드 풀기 · 다른 스레드 풀기 · 살아 있는 1000 개 무작위, `MemoryBackend` 를 바로 부른다 | — |
| ① 판정 | 할당 수 × ns | 어느 시나리오든 프레임의 3 % 이상(60 Hz 에서 0.5 ms)이거나, 8 스레드 ns 가 1 스레드의 3 배 이상이면서 워커 할당이 프레임당 1000 번 이상이면 ② 로. 아니면 닫는다 |
| ② 비교 | 같은 벤치와 시나리오를 `SW_MEMORY_BACKEND=mimalloc` 으로 5 번 | 프레임 p50 2 % 이상 또는 씬 로드 5 % 이상 빨라지고 최고 메모리가 10 % 넘게 늘지 않으면 Shipping 기본으로. 아니면 변형 파일 커밋을 되돌린다 |

**C-9 컨테이너 — 문제와 결정.** std 와 이름 · API 가 같은데 복잡도가 다른 넷이다.

| 자리 | 지금 | 벤치(`ContainerBenchTest`, bench) | 문턱 | 고칠 모양 |
|---|---|---|---|---|
| `vector::insert( pos, first, last )` | 고침 — 한 번 키우고 꼬리를 한 번 민다(`TArray::Insert`). `ByteStream.h` · `Archive.cpp` 가 부른다 | 1 MB 를 4 KB 꼬리 앞에 범위 삽입 | `reserve` + `memcpy` 기준의 2 배 이상 | 전방 반복자는 거리 → 한 번 키우기 → 꼬리 한 번 밀기 → 범위 생성(자명 복사는 `memmove`), 입력 반복자는 임시 벡터로 모아 같은 길 |
| `unordered_map` · `unordered_set` 의 `reserve` | 고침(AC-2) — 버킷과 밀집 배열을 함께 예약(`TMap::Reserve`), `UnorderedMapTest.ReservedInsertsDoNotAllocate` | 10 만 개 `reserve` 뒤 삽입의 할당 수 | 0 이 아니면 | 값 배열도 예약 |
| `map` · `set` 범위 구축(정렬 벡터) | 고침(AC-2) — 꼬리에 붙여 안정 정렬 · 겹친 키 거르기 · 한 번 병합하는 O(n log n), 버퍼는 컨테이너 할당자(`SortedVectorBase.h`), `MapTest.RangeBuildKeepsTheFirstOfEqualKeys` | 정렬 안 된 10 만 키 범위 구축 | `stable_sort` 기준의 2 배 이상 | 붙이고 `stable_sort` 뒤 겹친 키는 앞 것만 |
| `deque` | 고침 — 2 의 거듭제곱 링 버퍼(`TDeque`), 주소 안정 자리는 `ChunkedArray`(옛 래퍼는 MSVC STL 이 8 B 넘는 원소마다 블록 하나) | 64 B 원소 1 만 번 `push_back` + `pop_front` 의 할당 수와 ns(Windows · Linux) | 원소당 할당 0.5 번 이상 | 아래 |

**`deque` 는 역할 둘을 형식으로 가른다.** 쓰는 자리 50 여 곳 가운데 대부분은 FIFO 줄(`AsyncFileIO` · `NetConnection` · `SQLConnectionPool` · `DedicatedWorker` · `JoltJobSystem` · `StreamSendQueue` …)이고 몇 곳은 "주소가 움직이지 않는다" 때문에 쓴다(`LoopbackCluster.h` 셋 · `NetTransport.h` 의 `_listEndpoint` · `RTSWorld::_listUnit` · `FlowFieldCache::_listSlot` · 테스트 셋). 링 버퍼는 커질 때 원소를 옮겨 뒤쪽 계약을 깬다.

- `sw::deque` → 2 의 거듭제곱 링 버퍼(언리얼 5 `TDeque` · Rust `VecDeque` 자리). `static_assert( std::is_nothrow_move_constructible_v<T> )` 를 둬 옮길 수 없는 원소(`NetEndpoint`)는 컴파일되지 않는다 — 주소 안정이 필요한 자리가 조용히 링으로 넘어가지 않는다.
- 주소 안정이 필요한 자리 → 지금의 래퍼를 `ChunkedArray<T>`(언리얼 `TChunkedArray` — 붙여도 원소 주소가 그대로)로 이름을 바꿔 옮긴다. 원소가 옮겨지는데도 포인터를 드는 자리는 형식이 못 잡으므로 위 목록을 작업자가 하나씩 읽는다.
- 기각 — `PagedArray` 로: 색인으로 채우는 고정 표라 `emplace_back` · 기본 생성 불가 원소와 맞지 않는다.
- 기각 — `std::deque` 에 블록 크기 할당자: MSVC STL 의 블록 크기는 할당자로 못 바꾼다.

```cpp
// Source/Core/Container/deque.h
template <typename T, typename Allocator = ContainerAllocator<T>>
class deque // 연속 링. 커지면 원소를 옮긴다 — 참조 · 반복자는 push 뒤에 무효다(std::deque 와 다른 점은 이것 하나)
{
    static_assert( std::is_nothrow_move_constructible_v<T>, "sw::deque moves elements when it grows - use ChunkedArray for address-stable elements" );
public:
    void reserve( size_type capacity );
    template <typename... TArgs> reference emplace_back( TArgs&&... args );
    template <typename... TArgs> reference emplace_front( TArgs&&... args );
    void pop_front();
    void pop_back();
    reference operator[]( size_type index ); // ( _head + index ) & _mask
    // insert · erase · 반복자 · 리플렉션 컨테이너 래퍼가 쓰는 멤버는 지금 쓰는 곳의 집합만(작업자가 센다)
};
```

근본 원인은 "std 의 이름을 빌리면서 계약 일부만 지켰다" 이다. 같은 모양 후보는 `list.h` · `set.h` · `unordered_set.h` 의 범위 연산과 `string` 의 범위 `append` 다 — AC-1 작업자가 래퍼마다 범위 연산의 본문을 읽어 보고에 적는다.

### 병렬 오브젝트 남은 몫 — 씬 서비스 파도 · 캐릭터 일괄 이동 · 공간 격자(설계 2026-11-17, NEXT 5-3)

완전 ECS 는 기각된 결정이다. 오브젝트 모델은 그대로 두고, 이득이 흔들림 안이면 "켤 코드" 커밋을 되돌린다.

**(가) 같은 단계 서비스 갱신 — 문제.** `ISceneService::update( float32 )`(`Engine/Object/GameObject/Service/ISceneService.h`)는 게임 스레드에서 의존 순서로 하나씩 돈다. `FlushEnd` 에 열한 개(`SceneNetLink` · `NetObjectTable` · `SceneEnvironmentProvider` · `FoliageTerrainFollower` · `MeshLODSystem` · `Scene2DOccluderProvider` · `SceneAudio` · `SceneVFXProvider` · `SurfaceDeformSystem` · `SurfaceFootprintSystem` · `TextureStreamingSystem`), `DestroyPost` 에 다섯이 있다. `FlushEnd` 의 선언(`_updateAccess`)은 쓰기가 비고 `Structure` · `Transform` 읽기뿐이라 서로 겹치지 않는다(`DestroyPost` 의 넷은 `Structure` 를 써서 겹친다). 그런데 서비스는 생성자로 받은 가변 `GameObjectManager&` 를 멤버로 들고 `engine::getSubsystem<T>()` 로 엔진 서비스를 바로 얻으므로, 워커로 보내도 게임 스레드 전용 호출이 컴파일된다.

**대조.** 언리얼 Mass 프로세서는 질의에 읽기 · 쓰기를 선언하고 `bRequiresGameThreadExecution` 이 아니면 의존 해석기가 병렬로 돌린다(`UWorldSubsystem` 틱은 게임 스레드). 유니티 `ISystem` 은 선언한 컴포넌트 접근으로 잡 의존을 잇고 잡 안에서는 안전 시스템이 메인 스레드 API 를 막는다. Godot 는 서버(Physics · Rendering · Navigation)를 명령 줄 뒤의 자기 스레드에 둔다.

**결정.** 프레임 시스템이 한 것(5-3 2 단계 — 서명이 곧 실행 자리)을 서비스에 그대로 쓴다.

1. **서명으로 가른다.** `ISceneMainService::update( float32 )` 와 `ISceneParallelService::updateOnAnyThread( SceneJobContext& )`. 병렬 서비스는 만드는 함수가 `const GameObjectManager&` 만 넘겨 가변 매니저를 멤버로 둘 수 없고, 쓰기는 `context.deferPostTick` 뿐이다.
2. **게임 스레드 전용 엔진 API 는 열쇠 인자.** `GameThreadKey`(`ThreadCreationKey` 와 같은 friend 목록 — 게임 스레드 루프만 만든다)를 `ISceneMainService` 쪽에만 건네고, 병렬로 옮기려는 서비스가 부르던 게임 스레드 전용 API 에 `const GameThreadKey&` 인자를 더한다. 전수 이전이 아니라 옮기는 서비스가 닿는 API 만이다.
3. **파도 계획은 있는 것으로.** 같은 단계의 병렬 서비스를 `FrameSystemParallelPlan` 에 `_updateAccess` 로 넣어 겹치지 않는 것끼리 한 파도로 돌린다. 의존 순서는 파도 순서로 남는다.
4. **이미 안에서 나눠 도는 서비스는 그대로.** `MeshLODSystem` · `TextureStreamingSystem` · `SceneAudio` 는 `runParallelByCost` 로 워커를 다 쓰므로 파도에 넣어도 얻는 것이 없다(틱 안 중첩 `runParallel` 의 TLS 문제도 피한다). `SceneVFXProvider` · `NetObjectTable` · `SceneNetLink` 도 2026-11-08 판정대로 둔다. 후보는 남은 직렬 서비스다.

- 기각 — 선언(`_updateAccess`)만 믿고 지금 서명으로 워커에 보내기: 선언과 본문의 어긋남을 컴파일러가 못 본다(`_bLaneSafe` 를 버린 까닭).
- 기각 — 서비스마다 자기 스레드(Godot 서버): 씬마다 서비스 열여섯 × 스레드다.

```cpp
// Engine/Object/GameObject/Service/ISceneService.h
class SW_API ISceneMainService : public ISceneService
{
public:
    virtual void update( float32 deltaTime ) = 0; // 게임 스레드. 지금의 update
};
class SW_API ISceneParallelService : public ISceneService
{
public:
    virtual void updateOnAnyThread( SceneJobContext& context ) = 0; // 매니저는 const 로만, 쓰기는 context.deferPostTick
};
// 등록: SW_SCENE_PARALLEL_SERVICE( Type, 단계, 접근 ) — 생성자가 `const GameObjectManager&` 를 받는 형식만 컴파일된다

// Engine/Foundation/GameThreadKey.h
class GameThreadKey
{
    friend class GameObjectManager; // 프레임 단계 본문
    friend class Engine;            // 메인 루프
    GameThreadKey() {}              // NOLINT(modernize-use-equals-default) — 집합체가 되면 `{}` 가 private 를 비켜 간다
};
```

**측정.** 서비스 갱신마다 구간 `GT.Scene.service.<이름>` 을 넣고(`getSubsystemName`), Release 로 `load/characters` · `load/housingbulk` · `load/townsfolk` · Shooter3D `benchmark` 를 3 번씩. **문턱:** 한 단계의 직렬 서비스 합이 어느 시나리오든 0.3 ms 이상이고 서로 겹치지 않는 서비스 둘 이상이 각각 0.1 ms 이상이면 그 둘부터 옮긴다. 아니면 값을 S8 에 적고 닫는다(서명 나누기도 하지 않는다 — 옮길 서비스가 없으면 형식만 늘어난다). 옮긴 뒤에는 `GT.Scene.tick.FlushEnd` 가 흔들림 밖으로 줄어야 하고 상태 해시(`TestSceneStateHash`)가 같아야 한다.

**(나) 캐릭터 컨트롤러 3D · 2D 입력은 `ComponentDataStore` 페이지다** — `PhysicsCommandPage<CharacterCommand3D/2D>`(`Engine/Physics/Scene/PhysicsPoseStore.h:181`)로 옮겼다. 남은 병목은 감사 S-8 이다 — `CharacterControllerComponent.cpp:310-377` 이 스텝마다 게임 스레드에서 한 명씩 움직이고 Jolt `ExtendedUpdate` 가 씬 공용 임시 할당자를 쓴다. 설계: 물리 씬에 `moveCharacters( span<const CharacterMoveRequest>, span<CharacterMoveResult> )` 일괄 입구를 두고 백엔드가 레인마다 임시 할당자(S-2)를 들고 병렬 for 로 돈다(유니티 DOTS Character Controller 잡 자리). 감사 37 번 묶음(S-2 · S-7 · S-8)에서 같이 한다. **측정 · 문턱:** `load/characters`(500 명)의 물리 PreStep 을 워커 1 · 4 · 8 로 — 한 워커에서 1 ms 이상일 때만 하고, 4 워커에서 2.5 배 이상 빨라지지 않으면 되돌린다. **결과(2026-11-19 Release, `load/charactermove` 500 명 `GT.Physics.moveCharacters` 평균):** 워커 1 · 4 · 8 = 23.8 · 11.2 · 8.3 ms — 4 워커 2.12 배로 문턱 미달, 8 워커 2.85 배. 되돌리면 한 명씩 경로(워커 수와 무관하게 1 워커 값 이상)로 돌아가 2 배 이득을 잃으므로 유지한다. 4 워커에서 남는 약 5 ms 직렬 몫(일괄 앞뒤의 요청 모으기 · 결과 반영 · Jolt 공용 상태)이 다음 측정 대상이다.

**(다) 근접 기믹 · 감지 · 넷 관심 격자를 `SceneSpatialGrid` 로 — 셋 다 옮기지 않는다.**

| 자리 | 지금 | 판정 | 다시 볼 조건 |
|---|---|---|---|
| 근접 기믹(`GimmickCircuitComponent::computeProximity`, `GimmickCircuitComponent.cpp:387`) | 노드마다 `TagIndex::findObjectsWithTags` 로 고른 뒤 가장 가까운 거리 | 태그 색인이 이미 후보를 줄이고, 거리 상한이 없어 격자가 가지를 못 친다 | 프레임당 (근접 노드 × 태그 후보)가 어느 게임 씬에서든 1 만을 넘으면 노드에 필수 `range` 칸을 더하고 태그 층을 격자에 올린다 |
| 감지(`AIPerception::sense`) | 후보 목록을 부르는 쪽이 넘긴다 — 쓰는 곳은 AsymmetricHorror 키트 하나, 후보는 플레이어 수 | 순수 함수라 격자와 무관하다 | 씬 컴포넌트에서 자극을 64 개 넘게 모으는 키트가 생기면 그 키트가 `PhysicsCharacter3D` 층을 질의해 후보를 만든다(감각 코드는 그대로) |
| 넷 관심 격자(`Core/Network/Replication/NetInterestGrid`) | 엔티티 id 의 점 격자 + 들어오는 · 나가는 반경, `MMOReplicator` · `InterestReplicationPolicy` 가 움직인 것만 `setPoint` | Core 층이라 씬을 모르고, 원본이 서버 엔티티 표이며(씬 컴포넌트가 아닐 수 있다), 이미 증분이다. 밑 구조는 같은 `SpatialHashGrid2D` 다 | 없음 — 언리얼 `UReplicationGraphNode_GridSpatialization2D` 도 씬 가속 구조와 따로 둔다 |

재는 것은 근접 하나다: `GimmickCircuitComponent` 틱에 (노드 수, 후보 수) 카운터 `Gimmick.ProximityCandidates` 를 두고 게임 12 개 `slice` 에서 최고치를 본다.

근본 원인은 "실행 자리를 선언으로 말하고 서명으로 말하지 않았다" 이다. 같은 모양은 `ISceneService` 의 렌더 기여 훅(`contributeLights` 등 — 게임 스레드 계약이 주석뿐)에 남아 있다. 패킷 채우기를 병렬로 돌릴 때 같은 열쇠 인자를 쓴다.

### 리플렉션 남은 몫 — 생성 바이트 · 문화권 상속 · 방문자 · 타입 색인 · 복제 스키마(설계 2026-11-17, NEXT 5-4)

**W-R3 (a) 고정 배치형 코덱 본문.** 타입마다 내던 쓰기 · 읽기 · 비교 본문은 서술자 표(`Engine/Serialization/Fixed/FixedFieldDesc.h`)와 해석기 하나(`FixedLayout`)다(언리얼 `FRepLayout` 자리). 남은 것은 측정뿐이다: 통합 빌드에서 `.gen.cpp` 합계(전 7.86 MB, 목표 4 MB 미만)와 `build_time_report.py` 생성 비중(목표 8 % 미만). 목표에 못 미치면 다음 후보를 숫자로 고른다 — `CodeEmitBuffer` 가 방출 함수별 바이트를 세어 `ReflectionParser.EmitBytes` 로 남기고(`emitMethodInvoker` · `emitPropertyStatics` · `emitNestedContainerTree` · `emitPropertyMetadata` …), 합계의 25 % 를 넘는 방출 함수만 표로 바꾼다(메타데이터 채우기 함수 → 상수 (키, 값) 줄). 형식이 있어야 하는 호출 본문(`emitMethodInvoker`)은 표로 못 바꾼다.

**W-R3 (e) `CultureInfo` 부모 병합 — 문제.** `Engine/Localization/CultureInfo.cpp` 가 `*.cultures.xml` 을 손으로 읽는다: `parent` 속성으로 부모를 찾아 값 복사로 합치고(:339-411, 순서 정렬 · 순환 검사도 손으로), 자식 요소마다 `findChild` 로 있으면 덮고 없으면 부모 · 코드 기본값을 쓴다. 칸 목록을 구조체와 파서 두 곳이 알고, "부모 없는 문화권은 `<pluralRules>` 필수"(:377) 같은 규칙이 파서 본문에 흩어져 있다.

**대조.** CLDR 은 로캘 데이터를 부모 사슬(`root` → `ko` → `ko_KR`)로 상속하고 ICU 리소스 묶음이 그것을 따른다. 언리얼 `FCulture` 는 ICU 위에서 부모 문화권 목록을 준다. 유니티 Localization 은 `Locale` 의 폴백 메타데이터, Godot `TranslationServer` 는 언어 코드 비교로 폴백한다.

**결정.** 문화권 한 장을 `REFLECT` 구조체로 두고 리플렉션 XML 직렬화로 읽는다. 부모 병합은 [DataInheritance](agents/DataInheritance.md) 의 카탈로그 항목 상속(`_base="#ko"`)으로, 자식 요소(월 이름 · 숫자 기호 · 날짜 패턴 · 폰트 목록)는 리플렉션 컨테이너로 옮긴다. 코드 기본값 폴백은 지우고 사슬의 뿌리에 꼭 있어야 하는 칸은 `PROPERTY( RequiredAtRoot )` 다. 표에 없는 문화권 코드를 줄여 찾는 것(`ko-KR` → `ko`, `CultureTable::makeParentCode`)은 칸 상속이 아니라 로캘 맞추기라 그대로 둔다.

- 기각 — 손 파서에 검사만 더하기: 칸 목록을 두 곳이 아는 모양이 남는다.
- 기각 — ICU 도입: `CultureInfo.h` 머리말의 결정(CLDR 부분 집합만) 그대로다.

이전: 문화권 XML 을 형식 판 올림(`SW_ASSET_FORMAT_UPGRADE` — `parent` → `_base`, 요소 이름 → 칸 이름)으로 다시 쓰고 손 파서를 지운다. 확인: 지금 표의 모든 문화권에 대해 숫자 · 통화 · 날짜 · 복수형 서식 결과가 바꾸기 전과 글자까지 같다.

**`InspectorLayoutCache` 키를 `PropertyHandle` 로 — 하지 않는다.** "핫 리로드 때 비우는지" 를 읽었다: `getOrCreateGroups` 가 `TypeRegistry::getGeneration()` 이 바뀌면 묶음과 `_mapDisplay` 를 통째로 버린다(`InspectorLayoutCache.cpp:13-17`). 세대 안에서는 `const PropertyInfo*` 키가 매달리지 않고, 세대는 모듈을 올리고 내릴 때만 바뀐다. `PropertyHandle` 로 바꾸면 행마다 `PropertyHandle::of` 의 사슬 걷기가 들고 얻는 것은 "무관한 모듈 리로드에 캐시를 지킨다" 뿐이다. 다시 볼 조건: S8 의 인스펙터 값 행 측정에서 `findDisplay` 조회가 그리드 그리기의 5 % 를 넘으면 표시 정보를 묶음 행 곁(같은 색인의 배열)에 둬 포인터 키 맵을 없앤다.

**W-R4 `ReflectVisit` 하나 — 문제.** `getPropertiesWithBase()` 를 도는 루프가 테스트 밖에 52 곳이다(직렬화 · 검증 · 문서 · 인스펙터 모으기). 상태 해시는 컴포넌트 일곱이 `Component::foldStateHash` 를 손으로 써서 새 칸이 빠져도 아무것도 지지 않는다. 잎까지 내려가는 재귀는 `ReflectedValueVisitor`(핸들 잎 옮기기 전용)에만 있다.

**대조.** 언리얼 `TFieldIterator<FProperty>` · `FPropertyValueIterator`(구조체 · 컨테이너 재귀)와 `FArchive` 하나로 직렬화 · CRC(`FArchiveObjectCrc32`)를 겸한다. 유니티 `SerializedProperty.Next( enterChildren )` 와 Unity.Properties `PropertyVisitor`. Godot `get_property_list` + Variant.

**결정.** 방문 순서 · 걸러 내기 · 재귀를 한 곳에 두고 소비자는 방문자만 쓴다. 두 걸음이다.

1. `ReflectVisit` + 상태 해시 기본 구현 + 검증. `Component::foldStateHash` 의 기본 본문이 상태 역할 칸(`serializeState` 대상)을 방문해 접고, 손으로 쓴 재정의는 리플렉션 밖 상태(물리 바디 자세처럼 칸이 아닌 값)를 **더할** 때만 남긴다.
2. 직렬화 루프 아홉 · 문서 · 인스펙터 모으기. 걸음마다 Content 왕복 바이트가 같아야 한다.

- 기각 — 가상 `IPropertyVisitor`: 칸마다 가상 호출이다. 템플릿 방문자로 인라인한다(`ReflectedValueVisitor` 와 같은 모양).
- 기각 — 한 번에 52 곳: 직렬화 바이트가 걸려 있어 소비자별 커밋으로 나눠 되돌릴 수 있게 한다.

```cpp
// Engine/Reflection/ReflectVisit.h
enum class ReflectVisitFlow : uint8 { Continue, SkipChildren, Stop };
struct ReflectVisitField
{
    const TypeInfo&     _owner;    ///< 칸을 선언한 타입
    const PropertyInfo& _property;
    void*               _pValue;   ///< 접근자 · 비트필드를 푼 값 주소(비트필드는 임시 사본 — 쓰기는 commit 으로)
    uint32              _depth;    ///< 0 이 최상위
};
struct ReflectVisitOptions
{
    PropertyRoleMask _roleMask;         ///< 새 형식 — `PropertyRoleUtil` 의 역할(SaveGame · Replicated · 상태 …) 비트 집합. 비면 전부
    uint8            _bWithBase   : 1;  ///< 기반부터
    uint8            _bIntoValues : 1;  ///< 구조체 칸 · 컨테이너 원소로 내려간다
};
struct ReflectVisit
{
    /** @p visitor 는 `ReflectVisitFlow( const ReflectVisitField& )`. 돌려준 값이 Stop 이면 false 입니다. */
    template <typename TVisitor>
    static bool visitProperties( const TypeRegistry& registry, const TypeInfo& type, void* pInstance, const ReflectVisitOptions& options, TVisitor&& visitor );
    template <typename TVisitor>
    static bool visitProperties( const TypeRegistry& registry, const TypeInfo& type, const void* pInstance, const ReflectVisitOptions& options, TVisitor&& visitor );
};
```

**W-R4 뒤 — 상태 직렬화 · 타입 판(구조 감사 1 · 11, 결정).** `serializeState` 의 기본 본문은 `StateIO::reflected`(상태 역할 칸을 `ReflectVisit` 와 같은 순서로, 머리는 타입 판 + 칸 배치 해시)이고 손 본문은 리플렉션 밖 값만 더한다. 판은 그릇이 아니라 타입의 `REFLECT( Version = N )` 한 곳, 옛 판은 `SW_TYPE_VERSION_PATCH` 사슬(ezEngine GraphPatch 의 타입 판 + 패치, 그래프 대신 바이트 본문)이 상태 바이트 · 서비스 레코드에서 같이 올린다. 기각 — 생성기가 타입마다 `io.value` 줄을 내기: 리플렉션 밖 칸 형식(Countdown · 포인터 ↔ id)을 생성기가 알아야 하고 실행 걷기로 충분한 비핫 경로다. 다시 볼 조건: 롤백 프레임 경로가 `reflected` 를 쓰게 되면 타입별 칸 계획 캐시(배치 해시 포함)를 둔다. 남은 손 본문은 158 곳(테스트 형식 빼고 — 옮긴 넷: LevelProgress · StatBlock · ResourceGauge · ScavengerQuota)이고, 칸마다 검사 · 포인터 ↔ id · 리플렉션이 아닌 칸 형식이 섞여 키트별로 옮긴다.

**측정 · 판정.** 동등성은 값이 아니라 가르는 힘으로 본다: `StateHashTest.EveryStateFieldChangesTheHash` 가 상태 칸을 가진 모든 컴포넌트 타입의 모든 칸을 리플렉션으로 하나씩 바꿔 해시가 달라지는지 본다(새 칸이 빠질 수 없다). 손 구현과는 `TestSceneStateHash` 의 같은 씬 N 프레임에서 "두 실행이 같다 ⇔ 두 해시가 같다" 를 견준다. 비용: 오브젝트 8000 씬의 상태 해시 ns, 씬 로드의 인스턴스화 구간 · `Scene.save.serialize` — 소비자별 커밋 앞뒤 차이가 3 % 를 넘으면 그 소비자의 커밋만 되돌린다.

**W-R6 밀집 타입 색인 — 측정 뒤, 지금은 소비자가 없다.** NEXT 는 "5-3 SoA 와 함께" 라 했지만 SoA(`ComponentDataStore<TPage>`)는 페이지 형식이 템플릿 인자라 `TypeInfo` 로 찾지 않는다. 타입으로 찾는 표는 `TickRegistry::_mapTypeDispatch`(`unordered_map<const TypeInfo*, …>`) · `GameObjectStore::_mapComponentPool`(FQN 키 + 뮤텍스) · `ComponentDefaults::_mapResolved` 셋이다. 대조: 유니티 `TypeManager.GetTypeIndex<T>()`, 언리얼 Mass `TStructTypeBitSet` 의 구조체 번호, Godot `ClassDB` 는 `StringName` 해시 표다. 설계는 `TypeInfo::_typeIndex`(등록 때 받는 밀집 번호, 내려간 타입의 번호는 `_contentGeneration` 을 올리고 다시 쓴다)와 그 번호로 찾는 배열 `TypeIndexedArray<T>` 이다. **문턱:** `load/characters` · `load/housingbulk` 에서 위 세 표의 조회 합이 게임 스레드 프레임의 1 % 이상(또는 0.1 ms 이상)일 때만 한다. 아니면 값을 적고 닫는다.

**N1 복제 스키마 — 문제.** `PROPERTY( Replicated )`(`ReflectionMetadata.h:136`, `PropertyRoleUtil::collectReplicatedProperties`)는 소비자가 테스트뿐이다. 키트와 게임은 스냅샷을 따로 손으로 쓴다(`MechArenaSnapshot` · `GimmickSnapshotUtil` · `ShooterNetPawnComponent::writeState` · `readState`) — 같은 칸을 컴포넌트와 스냅샷 두 곳이 안다. 한편 바탕은 있다: 고정 배치형 코덱이 범위 · 양자화 키(`Min` · `Max` · `Step` · `MaxCount`)를 읽고, `NetMessageChannel::seal` 이 등록한 타입의 스키마 해시를 핸드셰이크 와이어 버전에 섞는다.

**대조.** 언리얼 `UPROPERTY( Replicated )` + `FRepLayout`, 양자화는 형식(`FVector_NetQuantize10`), Iris 는 `FReplicationStateDescriptor` 와 프로토콜 식별자 해시를 연결 때 맞춘다. 유니티 Netcode for Entities 는 `[GhostField( Quantization = 100 )]` 에서 직렬화기를 생성하고 고스트 모음 해시가 다르면 연결을 끊는다. Godot `SceneReplicationConfig` 는 프로퍼티 경로 목록이고 양자화 · 스키마 해시가 없다.

**결정.**

1. **복제 스키마 = 서술자 표의 `Replicated` 부분.** 생성기가 타입마다 `Replicated` 칸만 가리키는 두 번째 뿌리 목록과 그 정규 문자열 · 해시를 낸다(표 자체는 하나). `Replicated` 칸의 형식이 인코딩 표 밖이면 **생성 오류**다(전체 코덱은 "이유 등록" 이지만 `Replicated` 는 명시한 요청이다).
2. **`Quantize` 키는 새로 두지 않는다.** 양자화는 이미 있는 `Min` · `Max` · `Step` 이다. 키를 따로 두면 같은 칸의 범위를 두 곳이 안다. 편집 범위와 와이어 범위가 달라야 하는 칸이 실제로 나오면 그때 `NetMin` · `NetMax` 를 더한다.
3. **스키마 해시는 와이어 버전에.** `NetMessageChannel::registerReplicatedType<T>( kind )` 가 (종류, 복제 스키마 해시)를 봉인 목록에 넣는다 — 칸 순서 · 범위가 다른 빌드는 핸드셰이크에서 `VersionMismatch` 다. 데이터(프리팹)로 정해지는 타입은 복제 키트가 호스트를 세울 때 게임의 넷 프리팹 표에서 FQN 순으로 등록한다.
4. **1 단계는 통째 쓰기.** 엔티티 상태 바이트(`NetEntityState`)를 `writeFull` 로 채우고 델타는 지금의 `NetSnapshot` 바이트 델타가 한다. 칸별 변경 비트(`writeDelta`)는 대역폭 측정 뒤다.
5. **첫 소비자는 Shooter3D `ShooterNetPawnComponent`.** 손 스냅샷 구조체는 그대로 쓸 수 있다(전체 코덱).

- 기각 — 양자화를 형식으로(언리얼 `FVector_NetQuantize`): 칸 형식이 와이어 사정으로 갈라지고 인스펙터 · 세이브가 그 형식을 따로 알아야 한다.
- 기각 — 실행 때 프로퍼티 경로 목록(Godot): 스키마 해시가 없어 다른 빌드가 조용히 어긋난다.

```cpp
// Engine/Serialization/Fixed/ReplicatedLayout.h — FixedLayout 의 해석기를 쓴다
struct ReplicatedSchema; // 생성 표: 칸 색인 목록 · 정규 문자열 · 해시
struct SW_API ReplicatedLayout
{
    [[nodiscard]] static const ReplicatedSchema* findSchema( const TypeInfo& type ); // Replicated 칸이 없으면 nullptr
    [[nodiscard]] static uint64 getSchemaHash( const ReplicatedSchema& schema );
    static void writeFull( const ReplicatedSchema& schema, const void* pObject, BitWriter& writer );
    /** @p pOutChanged 가 있으면 값이 바뀐 칸의 비트를 세웁니다 — RepNotify(`PropertyRoleUtil::callRepNotify`)는 부르는 쪽이 읽기 뒤에 부릅니다. */
    [[nodiscard]] static FixedReadResult readFull( const ReplicatedSchema& schema, void* pObject, BitReader& reader, DynamicBitset* pOutChanged );
};
```

**측정.** `net-pair` Shooter3D 에서 폰을 옮기기 전후의 클라이언트당 스냅샷 바이트(손 구조체의 105 % 이내)와 `ReplicatedLayoutTest.EncodeCost`(bench, 엔티티당 ns — 전체 코덱 `FixedLayout::write` 의 1.5 배 이내). 넘으면 해석기의 칸 건너뛰기를 본다.

근본 원인은 "리플렉션 칸을 도는 일을 소비자마다 다시 썼다" 이다. 같은 모양 후보는 `ObjectStateSerializer` 의 상태 쓰기 · 읽기 짝과 `ComponentDefaults` 의 기본값 비교다 — RF-4 가 같이 옮긴다.

### 리플렉션 내장 형식 한 곳 정본(설계 2026-11-20)

**문제.** `int2` 를 내장 형식으로 넣을 때 `ReflectBuiltins.xxx` 한 줄 말고 손 표 8 곳을 채워야 했다 — 파서 고정 배치 수학 표 · 각 이름 · `Interp` 허용 목록, 에디터 `InspectorWidgetFor<T>`, `PropertyRoleUtil::findBlend`, `UIAnimatedProperty`, `StateIOReflected` 잎 종류 표, `Component::foldLeaf`. 빌드가 멈춘 것은 둘뿐이고 나머지는 런타임 FATAL 이거나 조용히 어긋났다(Interp 목록 셋이 달랐다, 상태 해시는 `hashed_string` 을, 상태 직렬화는 `GameObjectHandle` 을 못 실었다).

**대조.** 언리얼은 UHT 가 `UPROPERTY` 의 C++ 형식에서 `FProperty` 종류(`FIntProperty` · `FStructProperty` …)를 정하고, 보간 · 저장 · 복제가 그 종류 하나를 본다. Godot `ClassDB` 도 `Variant::Type` 하나가 에디터 위젯 · 보간(`Animation::interpolate_variant`) · 직렬화를 가른다. 둘 다 "형식 한 곳 → 소비처가 파생" 이다.

**결정.** xxx 줄에 `Widget` · `Layout`(`none | num | f32(멤버…) | i32(멤버…) | angle`) · `Roles`(`Interp` · `State`) 칸을 더하고 소비처는 칸에서 만든다. 엔진은 `ReflectBuiltinTraitTable`(모양 · 역할)과 `getBuiltinComponentCount`, `ReflectedValue.cpp` 가 Layout 을 C++ 선언과 컴파일 때 대조한다(멤버 형식 · `offsetof` · `sizeof`, i32 ↔ `PackedValueTraits::kIntegerComponents`). 파서는 로더가 `BuiltinTypeTraits` 를 세션 표로 만들고 시작할 때 철자 · Interp-모양 · Packed 위젯-글 처리기 짝을 검사한다. `RuntimeState` 칸이 State 없는 내장 형식 · 맵이면 생성 오류다(`DeclarationValidator::isRuntimeStateAllowed`). 손 처리기는 Layout 이 none 인 State 형식(bool · string · hashed_string · GameObjectHandle)만 남고, 빠지면 `static_assert`. UI 애니메이션의 성분 보간은 실수 num · f32(4 성분 이하) · angle 만 받고, 정수 칸은 예전처럼 끝에서 바뀐다(Discrete — float32 성분이 int64 · uint64 를 깨고 반올림 중간값은 뜻이 없다), 쿼터니언도 뺀다(회전은 slerp). 시퀀서 보간은 그 타입 그대로다. 죽은 `PROPERTY( Polymorphic )` 는 지웠다.

**리뷰 뒤(이어서).** State 역할의 범위는 같은 프로세스의 롤백 · 리플레이 · 스냅샷이다. 핸들은 세이브에서 빼지 않는다 — 직렬화가 이미 대상의 고정 지역 id · 키 해시로 적어(감사 A-2) 다른 실행에서도 같은 대상을 찾는다(언리얼이 `FObjectPtr` 을 세이브에서 경로로 바꾸는 것과 같은 자리). 통합 때 한때 둔 Local 역할(세이브에서 핸들 빼기)은 이 전제를 놓쳐 약 38 칸의 세이브 참조를 잃게 해 걷었다(`ObjectStateSerializerTest.HandleToMissingComponentSurvivesBinarySaveRoundTrip` 가 잡음). 손 처리기 목록은 `ReflectBuiltinTraitTable::hasStateHandHandler` 하나이고 State 줄마다 `isStateCarried` 를 컴파일이 본다. 래퍼 모양 답은 CRTP 기반이 가상 함수를 `final` 로 두고 정적 값을 돌려준다.

**컨테이너(이어서).** CONTAINER 줄에 Roles(`State` · `Fixed`) 칸을 더했다. 래퍼가 `kInPlaceElementWrite` · `kFixedSize` 를 형식의 정적 값으로 들고 `ContainerWrapperOf<W>::matchesTable` 이 표와 대조한다 — 생성 코드가 컨테이너 칸마다 `static_assert` 를 낸다(언리얼 `FArrayProperty` · `FSetProperty` 가 형식에서 정해지는 것과 같은 자리). 집합 · 맵은 State 가 아니라 RuntimeState 로 쓸 수 없다(집합 읽기가 정렬 키를 제자리에서 고치고, unordered_set 해시는 실행마다 차례가 달랐다). 런타임 판정은 `PropertyInfo::findStateSequence` 하나다. xxx 의 "숫자 줄이 이어져 있다" 차례 규칙은 지웠다 — num 줄이 Number 갈래인지 컴파일이 본다.

**확인.** 파서 픽스처 `RuntimeStateUnsupportedType`, 도구 입력 `ReflectionParser.ToolInputs.broken-builtins-{interp,widget,layout}`, 단위 `ReflectionPropertyRoleTest.InterpRoleRowsAreInterpolatable` · `BuiltinLayoutGivesComponentCountAndSize`, `StateIOReflectedTest.BuiltinLayoutPropertiesRoundTrip` · `Int2BytesMatchTheHandWriter`, `StateHashTest.LayoutAndHandHandlerPropertiesFold` · `EveryStateRowCanBeChanged`, `ReflectionPropertyRoleTest.StateSequenceIsTheOrderedFlatSequence`, `UIBindingValueKindTest.EveryRowKindComesFromItsOwnClass`, `UIAnimationPlayerTest.IntegerPropertiesStayDiscrete`, `ReflectionPropertyRoleTest.StateRowsHaveOneHandHandlerList` · `ProcessLocalHandlesAreNotSaved`, 픽스처 `SaveGameOnLocalHandle`, 도구 입력 `broken-builtins-container`.
### 트리거 Enter · Stay · Exit 계약(설계 2026-11-20)

**문제.** 겹침 영역이 여러 벌이고 경계 상황의 약속이 적혀 있지 않았다. 전수:
1. **물리 트리거 · 센서**(Jolt `mIsSensor` · Box2D 센서 · 2D 연속 콜라이더의 쓸기): 백엔드 알림 → `PhysicsContactTracker`(셰이프 쌍 수 → 바디 쌍 시작 · 유지 · 끝) → `ScenePhysics` 가 `FlushPostPhysics` 단계에 게임 스레드에서 `Component::onOverlapBegin/End` 로 나눈다. 받는 쪽: `GimmickSensorComponent`(인원 · 무게), `ActivatorVolumeComponent`(→ `TutorialHint` · `AutosaveTrigger` · `SequenceTrigger`), `ScareTrigger` · `LaunchPad` · `BoostPad` · `ItemBox`. 유지는 opt-in(`registerContactStayListener`).
2. **지역 볼륨 · 표면**(`ZoneVolumeComponent` · `ZoneContactComponent`): 매 프레임 겹침 질의(공간 격자 후보 → 모양 안 점 검사) → `ZoneTracker`(이번 프레임 집합과 지난 집합의 차) → 진입 · 체류 · 이탈. `ZoneSystem` 이 틱 뒤 게임 스레드에서.
3. **거리 판정**: `ActivatorVolumeComponent` 의 `_radius`(물리 몸 없는 활성자), 들어오는 순간만.
4. 이벤트가 아닌 가중 볼륨(`PostProcessVolume` · `AudioReverbZone` · `BuoyancyVolume`)과 복제 관심 격자(`NetInterestGrid`)는 Enter/Exit 계약 밖이다.
전용 서버(`Ninja-Release-Server`)는 `Engine`(물리 포함) · `GameFramework`(Zone · Gimmick, `Client/` 아님)를 링크하므로 둘 다 서버에서 돈다. 권한 게이트는 없다 — 시뮬레이션하는 월드마다 같은 계약으로 낸다.

결함: (a) 이벤트가 **바디 쌍** 단위라 몸이 여럿인 오브젝트(래그돌 뼈 · 탈것 · 캐릭터 안쪽 바디)는 시작을 여러 번 받고, 센서는 첫 끝에서 아직 안에 있는 대상을 뺐다(`GimmickSensor`), 발판은 여러 번 쐈다. (b) 같은 프레임의 끝과 시작이 쌍 순서로 섞였다. (c) 꺼진 지역 볼륨 · 표면은 시스템이 건너뛰기만 해 안의 대상이 다시 켤 때까지 나가지 않았다(유니티 함정과 같은 꼴). 파괴 대기인 대상은 실제로 지워질 때까지 후보로 남았다(꺼진 대상은 격자 질의가 이미 뺀다). (d) 거리 판정은 활성자가 사라지면 "안" 표시가 남았다.

**대조.** 언리얼: 컴포넌트 쌍마다 `BeginOverlap` 한 번, 생성 · 등록 시 `UpdateOverlaps` 로 이미 겹친 것에 시작, 파괴 · 콜리전 끔 · 등록 해제에 `EndOverlap`, 갱신은 끝을 먼저 낸다. 유니티: 다음 물리 단계에서 `OnTriggerEnter`, 비활성 · 파괴에 `OnTriggerExit` 가 오지 않는 것이 알려진 함정. Godot: `body_entered/exited`, `monitoring` 을 끄면 나간다. → 언리얼 꼴(생성 시 시작, 사라짐 · 꺼짐에 끝, 끝 먼저)을 따르되, 받는 훅이 오브젝트 전체(`Component::onOverlap*` 은 오브젝트의 켜진 컴포넌트 모두)라 단위는 **오브젝트 쌍**이다.

**결정.**
1. 물리 겹침은 `PhysicsOverlapTracker`(Engine/Physics/Scene) 하나가 낸다 — 열쇠는 (두 오브젝트 id, 각 쪽 트리거 여부, 2D/3D). 바디 쌍 시작 · 끝을 세어 0→1 에 시작, 1→0 에 끝. 트리거 여부를 열쇠에 두는 까닭: 상대의 감지 범위(트리거)와 몸을 받는 쪽이 가린다(`OverlapInfo::_bOtherTrigger`) — 하나로 합치면 범위가 먼저 들어온 뒤 몸이 들어와도 시작이 안 온다. `ScenePhysics` 의 겹침 나눔은 이 추적기의 결과만 쓴다(바디 쌍 이벤트를 직접 나누지 않는다 — 형식으로 한 길).
2. 프레임 단위로 정리한다: 프레임 시작과 끝의 수로 **끝 먼저 → 시작 → 같은 프레임에 들고 난 것(시작 + 끝)**. 프레임 안에서 나갔다 다시 든 것(끝 0 거침)은 아무것도 내지 않는다(트리거 켜고 끄기 · 바디 다시 짓기가 헛 이탈 · 진입을 내지 않게).
3. 유지(Stay)는 오브젝트 쌍마다 물리 프레임 한 번, 진입 프레임 포함, 올린 듣는 쪽만(`_pOnOverlapStay`). 막는 접촉의 유지는 지금처럼 바디 쌍 · 스텝마다.
4. 경계 상황: 트리거 안에서 생성 → 다음 물리 스텝에 시작 한 번(백엔드가 겹친 채 추가된 바디의 접촉을 알린다). 파괴 · 비활성(바디를 놓는다 — `isSimulated`) · 로그아웃(폰 파괴) → 끝 한 번(`removeBodies`). 로그인(폰 생성) → 서버에서 시작 한 번 — 클라이언트는 이벤트를 복제받지 않고 자기 월드가 같은 계약으로 낸다(지금 넷 구조 그대로). 트리거 생성 · 파괴 · 켜고 끄기 · 모양 · 크기 · 이동 → 같은 규칙(바디 추가 · 제거 · 셰이프 교체). 순간 이동은 지나친 길에 이벤트를 내지 않는다(언리얼 `Teleport` · 유니티와 같다 — 2D 연속 콜라이더만 쓸기로 시작 + 끝). 트리거끼리는 `_bOtherTrigger` 가 참인 겹침으로 온다. 처리 안의 파괴 · 생성은 다음 프레임 이벤트가 되고 이번 목록은 사본을 돈다.
5. 지역(`ZoneTracker`)은 이미 오브젝트 단위 · 이탈 먼저다. 두 벌을 한 추적기로 합치지 않는다 — 입력이 다르다(사건 수 세기 대 프레임 집합 차이) · 지역은 대상마다 모듈 기억을 든다. 계약만 같게 맞춘다: 꺼진 볼륨 · 표면은 안의 대상을 모두 내보내고(다시 켜면 다음 갱신에서 진입), 꺼지거나 파괴 대기인 물리 개체는 후보가 아니다(나간다).
6. 스트리밍 셀이 내려가면 그 오브젝트는 파괴 길을 지나므로 파괴 규칙대로 남은 트리거에 끝이 간다(트리거가 내려가면 안의 대상에 끝). 스트리밍 코드는 바꾸지 않는다.
7. 막는 접촉(`onCollisionBegin/End` · 접촉 유지)은 바디 쌍 단위로 둔다 — 겹침은 "안에 있다" 는 상태라 오브젝트 쌍으로 세지만, 접촉은 부딪힌 바디마다의 충격이다(파괴 조각마다 피해 `FractureComponentBase` · 같은 오브젝트 조각끼리의 밀어냄 거름이 `_selfBody` 를 본다). 언리얼 `OnComponentHit`(컴포넌트마다) · 유니티 `OnCollisionEnter`(콜라이더 쌍)와 같다. 접촉을 세는 소비자는 종류로 모은다(`ZoneContactComponent` 는 표면 종류마다 최대 충격 하나 — 몸이 여럿이어도 한 기록). 테스트 `TriggerOverlapContractTest.CollisionStaysPerBodyPair3D`.

**확인.** 단위 `PhysicsOverlapTrackerTest.*`(물리 없음 — 여러 바디 한 번, 끝 먼저, 들고 남, 다시 짓기, 트리거 열쇠 분리, 유지), `TriggerOverlapContractTest.*`(Jolt · Box2D 씬 — 안에서 생성 · 파괴 · 비활성 · 트리거 끄고 켜기 · 순간 이동 · 몸 둘), `ZoneTest.*`(꺼진 볼륨 이탈 · 안에서 생성 첫 프레임 진입 · 파괴 이탈 기록) — 서버 단위(`Server --test`)에도 같은 케이스가 돈다. 시나리오 `triggercontract`(감시 값 `Physics.OverlapCount`).

**소비자 — 지역 진입 컷씬(2026-11-20).** `SequenceTriggerComponent` 는 이 계약의 오브젝트 쌍 진입(`onOverlapBegin`)만 받는다(유지 · 이탈 안 씀 — 진입이 이탈 뒤에만 오므로 다시 재생의 재무장이 따로 없다). 한 번만 · 다시 재생은 바탕의 `_bOnce`(블루프린트 `DoOnce` 자리 — 언리얼 Level Sequence Actor 에는 트리거가 없다), 발동 수는 상태 칸이라 세이브 · 재접속 뒤에도 다시 틀지 않는다. 재생 중 재진입은 다시 틀지 않고(언리얼 `Play` 와 같다), 플레이어가 데이터 결함으로 틀지 못하면 발동이 아니다. 시나리오 `game.playground.cutscenetrigger`(안에서 태어남 · 유지 · 재진입 · 폰 재생성).

**지역 진입 컷씬 2 차 — 진행 플래그 · 나타남 규칙 · 셀 경계(2026-11-20).** 대조: 언리얼은 트리거 박스 `BeginOverlap` → Level Sequence `Play` 블루프린트에 `DoOnce`(액터 상태라 세이브에 없다 — 게임이 `USaveGame` 에 "봤음" bool 을 따로 적는다), 안에서 스폰한 폰을 거르려면 `GetGameTimeSinceCreation` 을 본다. 유니티는 `OnTriggerEnter` → `PlayableDirector.Play` 스크립트에 저장 데이터 칸. → 둘 다 게임마다 손으로 적는 일을 볼륨 바탕(`ActivatorVolumeComponent`)의 데이터 칸 둘로 둔다.
1. **진행 플래그 `_progressFlag`.** 적으면 본 기록은 판 상태(`SessionStateComponent`)의 `GameFlags` 다 — 세이브 슬롯에 실리고, 같은 이름의 볼륨끼리 나누며(입구 둘에 컷씬 하나), 대화 · 퀘스트 조건식 · 감시 값 `Session.Flag` 가 같은 이름으로 읽는다. 발동 때 1 을 더한다(다시 재생 볼륨은 횟수). 플래그를 적었는데 열린 판 상태가 없으면 오류(`ActivatorVolume.ProgressFlagNoSession`)로 일하지 않는다 — 볼륨 발동 수로 대신하지 않는다. 빈 이름이면 예전대로 볼륨의 발동 수다. 튜토리얼 · 자동 저장 볼륨도 같은 칸을 얻는다.
2. **나타남 규칙 `_spawnInside`(Activate · Ignore).** 계약의 "안에서 생성 = 진입" 이 기본(Activate). Ignore 는 이미 안에 나타난 활성자를 세지 않고 나갔다 다시 들어올 때 센다. 나타남 = 활성자 오브젝트가 막 생겼거나(오브젝트 id 가 창 전에 적은 다음 id 이상 — `ObjectIDAllocator::peekNextObjectID`, 언리얼 생성 시각 자리) 볼륨이 막 생겼거나 켜졌다(8 프레임 창). 불러오기는 저장된 id 를 되살리지만 볼륨도 다시 서므로 볼륨 쪽 창이 덮는다. 이미 있던 활성자를 끄고 켠 것은 나타남이 아니다(걸어 들어온 것과 같다). 겹침 · 거리 두 길이 같은 판정을 쓴다. 기각 — `GameObject` 에 생성 프레임 칸(풀 블록 크기 단언이 지키는 형식, 수천 개에 8 B) · 물리가 겹침에 "막 더한 바디" 표시(물리 백엔드 둘을 고쳐야 한다 — 창이 모자라는 구성이 생기면 그때 간다, 코드 주석의 shortcut).
3. **재생 중 밀려남 · 셀 경계.** 트리거는 진입만 받으므로 밖으로 밀려나도, 트리거의 셀이 내려가 파괴 길로 이탈해도 재생은 이어진다(시퀀스 오브젝트는 늘 올라 있는 자리에). 셀이 다시 오르면 발동 수는 떠 둔 상태로, 본 기록은 플래그로 돌아온다. 기수가 선 자리에 셀이 올라오면 안에서 생긴 트리거라 진입이다(Activate) — 막으려면 Ignore.
4. **멀티플레이(2026-11-20).** 대조: 언리얼은 트리거 블루프린트가 `HasAuthority` 뒤에서 `Multicast`(전원) 또는 그 플레이어의 `Client` RPC 로 시퀀스를 틀고 진행은 `GameState` · `PlayerState` 에, 유니티 Netcode 는 `ClientRpcParams` 로 대상을 고른다. → 판정 · 대상 · 기록 위치를 데이터 칸 하나로 둔다.
   - **판정은 서버만.** 바탕의 `hasActivationAuthority` 가 넷 링크 역할을 본다(판정 자리는 아래 "판정 자리 · 지역 나타남") — 클라이언트 월드는 진입을 받아도 틀지 않고 플래그도 찾지 않는다(로그 없음).
   - **대상 · 기록 `_audience`(Everyone · Activator).** Everyone 은 판 상태 플래그 · 연결된 모두, Activator 는 그 플레이어의 상태(`PlayerStateComponent` — 조종자 오브젝트, 언리얼 `APlayerState`, 상태 스냅샷에 실린다) · 활성자 폰을 가진 연결 하나. 한 번은 진행 플래그가 있으면 플래그로만 정한다(플레이어마다 따로 — 볼륨 발동 수로 막지 않는다).
   - **정본 창구 둘.** 서버의 소유 연결 `PawnComponent::findInputConnection`(입력 피어 = 연결 id + 1, 쓰는 곳 `ControllerComponent::setInputConnection` — 이미 있던 `_inputPeer` 칸이 쓰는 곳 없이 남아 있던 것을 정본으로), 로컬 조종 `ControlSystem::isLocallyControlled`(플레이어 조종자가 쥐고 입력 피어 0 — 언리얼 `IsLocallyControlled`, Godot `is_multiplayer_authority`). 원격 조종자는 `RemoteControllerComponent::spawnForConnection( manager, 이름, 연결 id, 폰 )` 한 길로만 세운다 — 연결 id 가 필수 인자이고 `setInputConnection` 은 보호 멤버라 연결 없는 원격 조종자를 코드로 세울 수 없다(데이터로는 놓지 못한다 — `REFLECT( NotPlaceable )`, 코드가 `addComponent` 로 바로 붙인 것은 시작에 오류 `RemoteController.NoConnection`). 조종 연결이 바뀌면 `transferToConnection`(Shooter3D `possessRemote` · 조종 연결 교체가 쓴다).
   - **키트 `GF_NetSequence`(영역 0x90).** 서버가 트리거 등록 칸에서 발동 수가 오른 트리거를 찾아 시작(재생 번호 · 디렉터 · 활성자 번호 · 대상 · 서버의 지금 프레임)을 대상에게, 디렉터가 멈추면 끝을 보낸다. 늦은 참가는 재생 중인 전원 재생을 지금 프레임부터(`SequencePlayerComponent::playFromFrame` — 지난 이벤트는 쏘지 않는다). 클라이언트가 보내는 것은 건너뛰기 요청(재생 번호)뿐이고, 서버는 살아 있는 Activator 재생 · 요청 연결 = 소유 연결일 때만 서버 재생을 끝낸다(위조 · 늦은 요청은 셈만 — 로그 폭주 방지). 클라이언트는 Activator 시작의 활성자가 자기 로컬 폰이 아니면 거절한다.
   - shortcut: 디렉터 하나를 플레이어들이 나눠 쓰므로 Activator 재생이 도는 동안 다른 플레이어의 진입은 틀지 않는다(트리거의 재생 중 재진입 규칙) — 동시 개인 컷씬이 필요하면 플레이어마다 디렉터를 세운다. 서버의 입력 막기는 서버 권위 이동 쪽 일이다.
   **확인.** 단위 `LocalControlTest.*`(로컬 · 소유 연결 · 음수 거절 · 플레이어 상태 찾기와 저장 왕복), 하니스 `SequenceNetSimTest.*`(전원 · 클라이언트 판정 없음 · 늦은 참가 프레임, 그 플레이어만 · 플레이어별 한 번, 건너뛰기 위조 거절 · 소유 요청 수락 — 에디터 확장 `GF_EditorPlugin_NetSequence`), 에디터 시나리오 `editor.netsequence`(본보기 게임 `Sample: cutscene`, 감시 값 `Editor.NetSimValue`).
**판정 자리 · 지역 나타남(2026-11-20).** 문제: 바탕 `hasActivationAuthority` 의 기본이 "참" 이라 시퀀스 트리거 말고는 넷 월드마다 각자 판정했다(자동 저장은 클라이언트도 저장 요청), 새 파생도 그 결함을 그대로 얻었다. 대조: 언리얼은 오버랩이 양쪽에서 나고 게임 로직은 `HasAuthority()` 뒤 서버만, 로컬 화면 일은 `IsLocallyControlled` 로 거른다. → 바탕 생성자가 `ActivatorAuthority`(Server · Local)를 필수 인자로 받는다 — 기본 생성자가 없어 파생이 고르지 않으면 컴파일되지 않는다(배치 데이터 칸이 아니다 — 볼륨 형식의 성질). Server 는 넷 링크가 클라이언트인 월드에서 판정하지 않는다.
- 시퀀스 트리거 Server(서버가 틀고 넷 키트가 보낸다), 자동 저장 Server(저장은 권위 있는 판 상태 — 클라이언트 복제본을 저장하지 않는다, 언리얼 `USaveGame` 은 서버 · 호스트), 튜토리얼 힌트 Local(그 월드 로컬 플레이어의 화면 일 — 서버는 알림 창구가 없다). 힌트는 넷 월드에서 로컬 조종이 아닌 폰(원격 플레이어 복제 폰 · 리슨 서버의 원격 조종 폰)을 거른다(`ControlSystem::isLocallyControlled`).
- 지역 볼륨도 나타남 칸 `_spawnInside` 를 얻는다(기본 Activate — 늪 · 독가스처럼 머무는 효과는 안에서 생겨도 걸려야 한다, 언리얼 `APhysicsVolume` 과 같다). Ignore 는 같은 판정(`ActivatorSpawnWindow` — 활성자 볼륨에서 뽑아 둘이 같이 쓴다)으로 나타난 대상을 나갈 때까지 대상으로 세지 않는다(진입 · 체류 없음). `ZoneTracker::touch` 가 거르므로 표면 접촉(`ZoneContactComponent`, 기본 Activate)도 같은 길이다. 오버월드 `ZoneTracker`(타일 맵 활성 존)는 진입 사건이 없어 대상 밖이다.
- 지역 효과의 판정 자리(2026-11-20): 모듈 처리기가 `ZoneModuleProcessor::getAuthority`(순수 가상 — 새 종류가 고르지 않으면 컴파일되지 않는다)로 고르고, 겹침 · 대상 기록은 모든 월드가 같이 하되 처리기 사건만 `ZoneTracker` 가 `ActivatorAuthorityRule::hasAuthority`(활성자 볼륨과 같은 규칙 한 벌)으로 거른다. Server — `Action`(이펙트 · 피해 · 스킬 · 즉사 — 복제로 받는다) · `Blackboard`(AI) · `NavArea`(AI 길 찾기). Local — `PhysicsField`(이동 시뮬레이션, 언리얼 `APhysicsVolume` 은 서버와 조종 클라이언트가 함께 읽는다) · `SurfaceChannel` · `SurfaceStamp`(보이는 것만). 표면 접촉(`ZoneContactComponent`)도 같은 길이다.
- 원격 조종자 막기(2026-11-20): `REFLECT( NotPlaceable )`(언리얼 `UCLASS(NotPlaceable)`) — 이름 생성 함수를 내지 않아 "Add Component" 메뉴에 없고, 씬 · 프리팹 읽기는 아는 타입을 만들지 못하면 오류 `ObjectStateSerializer.ComponentNotPlaceable`(원문은 `MissingComponent` 에 남긴다). `ControllerComponent::setInputPeer` 는 비공개 — 연결은 `spawnForConnection` · `transferToConnection` 으로만 정한다.
**확인(판정 자리 · 지역 나타남).** 단위 `ZoneTest.ServerModulesRunOnlyOnTheServerWorld`, `RemoteControllerTest.DataCannotPlaceIt`, `ControlSystemTest.InputPeerFollowsThePossession`, `ActivatorVolumeTest.AutosaveJudgesOnlyOnTheServer`, `TutorialHintVolumeTest.ClientWorldShowsHintsOnlyForItsLocalPlayer`, `ZoneTest.SpawnInsideIgnoreWaitsForTheWalkIn` · `SpawnInsideIgnoreSkipsABodyBornInside`. 테스트 장치 `Tests/SceneNetLinkTestBinding.h`(월드에 서버 · 클라이언트 넷 링크를 올린다).

**확인.** 단위 `SequenceTriggerTest.ProgressFlagIsTheOnceRecord`(같은 플래그 둘째 입구 · 플래그 지우고 되살리기) · `ProgressFlagWithoutSessionDoesNothing` · `SpawnInsideActivatePlaysOnTheFirstLook` · `SpawnInsideIgnoreWaitsForTheWalkIn` · `SpawnInsideIgnoreSkipsAnActivatorBornInside`. 시나리오 `game.wildwest.arrivalonce`(켜짐 = 나타남 무시 · 걸어 들어와 한 번 · 밀려나도 이어짐 · 건너뛰기 뒤 조작 · 다시 안 함 · 저장 · 불러오기 뒤 플래그와 다시 안 함), `game.wildwest.cellcutscene`(선 자리에 셀이 올라와 진입 · 재생 중 셀 내림에도 정상 끝 · 다시 올라와도 안 함). 와일드웨스트 `TownArrival` 은 `_progressFlag wildwest.arrivalSeen` · `_spawnInside Ignore` 다.

### 충돌 채널 · 응답 · 프리셋(설계 2026-11-20)

**문제.** 충돌 거름이 이름 붙은 레이어(`physicssettings.xml` 의 `PhysicsLayerDef` 7 개)와 32 × 32 "부딪힘 / 안 부딪힘" 대칭 행렬 하나였다 — 응답이 둘뿐(막지 않고 겹침만 내는 쌍을 적을 수 없다), 질의 채널이 없어(카메라 · 시야 질의가 레이어 이름 목록을 저마다 들었다) 프리셋(오브젝트 종류 + 응답 묶음)이 없었다. 2D 콜라이더(`BoxCollider2DComponent`)만 레이어를 이름이 아닌 번호(`_colliderType`)로 들었다.

**대조.** 언리얼: 채널(Object Channel · Trace Channel, 합쳐 32) · 응답 셋(`ECollisionResponse` Ignore · Overlap · Block) · 두 물체 응답 중 약한 쪽 · 프로필(Collision Preset — 오브젝트 종류 + 채널별 응답 + Query/Physics 켜기) · `LineTraceByChannel`. 유니티: 레이어 32 + 충돌 행렬(켜기/끄기) + 트리거 · `LayerMask` 질의. Godot: `collision_layer` / `collision_mask` 비트. → 언리얼 꼴로 간다. Jolt 의 `ObjectLayer` 는 "바디가 걸러지는 칸" 이라 칸을 **프로필 번호**로 쓰면 백엔드 구조(레이어 쌍 거름 · Box2D category/mask)를 그대로 둔 채 프로필 쌍 응답 표를 내릴 수 있다.

**결정.**
1. 데이터(`PhysicsSettings`): `_listChannel`(`CollisionChannelDef` — 이름 · 종류 Object/Trace · 기본 응답, 32 이하)과 `_listProfile`(`CollisionProfileDef` — 이름 · 오브젝트 채널 · 채널별 응답 목록 · `_bQueryEnabled` · `_bPhysicsEnabled`, 32 이하, 차례가 바디의 레이어 번호). 프로필이 적지 않은 채널은 그 채널의 기본 응답. 이름이 없거나 겹치거나, 프로필의 오브젝트 채널이 오브젝트 채널이 아니거나, 응답이 모르는 채널이면 읽기 오류. 정책 값은 이 설정 에셋(XML, 카탈로그 편집기가 반사 그리드로 고친다)에 둔다 — Config JSON 이 아니라 이미 있는 물리 설정 자리.
2. 응답 표(`CollisionLayers` — 프로필 쌍 32 × 32, 세 값): 두 프로필의 응답은 서로의 오브젝트 채널에 준 응답 중 약한 쪽(무시 < 겹침 < 막음), 물리가 꺼진 프로필은 모두 무시. 백엔드는 무시가 아닌 쌍만 짝짓는다. 겹침 쌍: Jolt 는 그 쌍의 접촉을 센서로 돌리고(`ContactSettings::mIsSensor`) 이벤트를 겹침으로 알린다 → 트리거 계약 추적기(`PhysicsOverlapTracker`)가 진입 · 이탈을 낸다. Box2D 는 막는 셰이프끼리 겹침이면 사용자 거름(`b2World_SetCustomFilterCallback`)이 접촉을 뺀다 — Box2D 는 막는 셰이프끼리 겹침 이벤트를 못 내므로(센서만) 2D 에서 겹침 이벤트는 한쪽이 트리거일 때만이다(유니티 2D 와 같다). 마스크에서 겹침 쌍을 빼지 않는다 — 트리거가 그 상대를 봐야 한다.
3. 트리거 표시(`_bTrigger`)는 남긴다 — 바디 단위 "센서로 만든다" 이고, 응답은 쌍 단위 "막을지" 다. 트리거 프로필은 상대에 겹침을 준다(데이터).
4. 질의: `PhysicsSettings::makeTraceLayerMask( 채널 )`(그 채널을 막는, 질의 켜진 프로필), `ScenePhysics::makeTraceFilter( 채널 )` → `PhysicsQueryFilter`. 모르는 채널 · 오브젝트 채널이면 오류 + nullopt(모두 맞는 필터로 대신하지 않는다). Jolt · Box2D 질의는 이미 레이어 마스크로 거르므로 그대로 내려간다. 전용 서버도 같은 설정 · 같은 코드다.
5. 이전: 프로필 이름 · 차례를 옛 레이어(Default · Static · Character · Ragdoll · Debris · Trigger · Projectile)와 같게 두고, 응답을 옛 행렬 그대로 적었다(테스트가 옛 표와 견준다) — 이름으로 적은 씬 · 프리팹(`_layer`)은 그대로 같은 동작이다. 일부러 바꾼 칸은 둘이다: Projectile 은 Pawn 에 막히고 Trigger 와 겹친다(옛 표는 무시 — Projectile 프로필을 쓰는 콘텐츠가 없다). 오브젝트 채널은 WorldStatic · WorldDynamic · Pawn · Ragdoll · Debris · Trigger · Projectile, 트레이스 채널은 Visibility(기본 막음) · Camera(기본 무시, 세상만 막음). 2D 콜라이더의 번호 칸 `_colliderType` 은 이름 칸 `_layer` 로 바꾸고 콘텐츠 20 파일을 옮겼다(모두 0 → Default). 인스펙터 표시 이름은 "Collision Profile".
6. 에디터: 응답 격자는 "Collision Responses" 창(`CollisionResponsePanel` — 프로필 × 채널 칸을 눌러 막음 → 겹침 → 무시, 아래에 프로필 쌍 결과 표, 문서 원문 단계로 되돌리기, 주석 · 자산 줄을 지키는 원문 편집 `CatalogXMLText`, 기본과 같은 값은 줄을 빼 적힌 줄은 늘 기본과 다른 값). 채널 · 프로필 더하기 · 지우기는 카탈로그 편집기다. 이름 칸은 `PROPERTY( Meta = "NameSource=CollisionProfile" )` · `"NameSource=CollisionChannel"` 메타로 인스펙터가 고르기 목록을 그린다(`InspectorNameSource` — 언리얼 `GetOptions` 자리, 목록에 없는 지금 값은 끝 항목으로 남겨 보인다).

**확인.** 단위 `CollisionProfileTest.*`(응답 표 대칭 · 약한 쪽, 프로필 해석 · 트레이스 마스크, 나쁜 데이터 오류, 배포 설정이 옛 표와 같음, Jolt · Box2D 막음 · 겹침 · 무시, 트레이스 채널 광선 거르기) — 서버 단위에도 돈다. 시나리오 `game/playground/automation/collisionprofiles`(투사체가 트리거는 겹치며 지나가고 Pawn 에 막힌다, 감시 값 `Physics.TraceDistance` 로 Camera 채널이 Pawn 을 무시하고 Visibility 는 맞는다).
### 스트리밍 월드 길찾기(설계 2026-11-20)

**문제.** 내비 타일은 셀과 같은 원점 · 반경으로 오르내린다(`SceneNavigation::updateTileStreaming`). 그런데 경로 · 군중은 끼운 타일만 안다 — 목적지 쪽 타일이 아직 오르지 않았으면 `requestMoveTarget` 이 목적지를 못 찾아 `Failed` 이고(오픈월드에서 먼 마을로 가는 NPC 가 출발조차 못 한다), 서 있던 타일이 내렸다 다시 오르면 Detour 군중 에이전트는 `INVALID` 상태로 남아 스스로 되살아나지 않는다. Detour 는 오프 메시 링크의 끝을 시작 타일과 여덟 이웃에서만 찾아(`connectExtOffMeshLinks`) 그보다 긴 다리 · 문 링크는 조용히 이어지지 않는다.

**대조.**
- 언리얼: World Partition 은 내비 데이터를 셀 청크(`UNavigationDataChunk` · World Partitioned Navmesh)로 굽고 셀과 함께 올린다. 먼 경로는 Recast 클러스터 링크(`WITH_NAVMESH_CLUSTER_LINKS` — 타일 안 이어진 덩어리 + 덩어리 사이 링크)의 계층 경로(`FindHierarchicalPath`)다. 런타임 생성 모드 Static · Dynamic Modifiers Only · Dynamic 은 굽기 비용을 정하는 값이다. NavLink Proxy(Smart Link)는 타일 경계를 넘는 링크이고, Detour Crowd 는 경로 통로를 다가가며 고친다. Mass AI 는 먼 개체를 낮은 LOD 로 레인 위에서 시간만 진행한다.
- 유니티 AI Navigation: NavMeshSurface 여러 개(각자 데이터) · NavMeshLink 로 잇고, 대형 월드는 구역마다 표면을 싣고 내린다. 계층 경로는 없다 — 게임이 경유지를 둔다.
- Recast/Detour 1.6: 타일 단위 `addTile` · `removeTile` 이 이웃 경계 이음과 오프 메시 링크 끝을 다시 잇는다(이웃 한 칸까지).
→ 우리 쿠킹본의 타일 구간이 이미 언리얼의 내비 데이터 청크다(셀보다 잘게 — 9.6 m 타일, 셀과 같은 원점 · 반경). 빠진 것은 계층 경로와 내려간 구역 위 이동이다.

**우리 빈 곳(코드 확인).**
- (가) 먼 경로 — 없었다. `NavMeshAssetEntry` 에 타일 바이트만 있고 타일 사이 연결 요약이 없다.
- (나) 경계 이음 — 타일 경계 · 이웃 한 칸 링크는 Detour 가 잇고 끊는다(맞다). 두 칸 넘는 링크는 조용히 빠졌다. 내렸다 오른 타일 위 군중 에이전트는 되살아나지 않았다.
- (다) 인스턴스 레벨 — 씬 인스턴스는 읽을 때 평평하게 펼친다(`SceneDocument::expandSceneInstances`) — 기하가 주인 씬 하나로 모여 한 번 굽는다. 합칠 내비 데이터가 따로 없다(언리얼 Level Instance 도 주인 월드의 내비 시스템이 굽는다). 인스턴스 안의 표면은 지금처럼 처음 것만 쓴다.
- (라) 내려간 셀 위 NPC — 정할 것이 없었다(군중이 `INVALID`).
- (마) 서버 — 내비게이션은 씬 서비스라 호스트를 가리지 않고 돈다. 서버의 원점은 관찰자(`MMOReplicator::feedStreamingSources`)라 관찰자에서 먼 NPC 는 (가) · (라) 와 같은 처지다.
- (바) 군중 · 회피 — 이웃 찾기는 타일과 무관한 근접 격자라 경계를 넘어도 같다. 내려간 타일로 들어가면 (나) 의 되살림이 필요하다.
- (사) 동적 장애물 · 다시 굽기 — 타일 단위 재베이크(`NavTileRebaker`, 동시 상한 `_maxConcurrentTileBakeCount`)와 장애물 캐시(`NavObstacleMode::TileCache`)가 언리얼 Dynamic Modifiers Only · Dynamic 자리다. 스트리밍은 입력 해시가 쿠킹과 다른 타일만 다시 굽는다. 바꿀 것 없다.

**결정.**
1. **대략 그래프(`NavClusterGraph`).** 노드는 타일 안에서 이어진 폴리곤 덩어리(클러스터 — 다리 위 · 아래처럼 겹친 층은 따로), 간선은 타일 경계 · 오프 메시 링크의 Detour 링크(방향 있음), 비용은 중심 사이 거리. 쿠킹이 모든 타일을 끼운 뒤 `INavMeshTileStore::collectClusterGraph` 로 짓고 쿠킹본 항목에 싣는다(형식 판 4). DDC 키는 쿠커 코드 지문(`Config/Dev/CookCodeInputs.json` 의 `Source/Engine/Navigation`)이 이미 덮는다 — 새 쿠킹 작업을 두지 않는다. 런타임 베이크(Dev · 스트리밍 안 하는 씬)는 모든 타일이 늘 끼워져 있어 그래프가 필요 없다.
2. **계층 경로(`NavStreamRoute`).** 시작 · 목적지 클러스터 → A* → 시작부터 타일이 이어 오른 마지막 클러스터(앞선 자리). 다 올라 있으면 세밀 경로 하나, 아니면 앞선 타일의 다음 클러스터 쪽 가장자리 점을 군중의 중간 목표로 건다. 내비메시 판 번호가 바뀌거나(타일이 오르내림) 중간 목표에 닿으면 다시 계획한다. 동기 질의는 `SceneNavigation::findStreamingPath`(세밀 경로 + 대략 점, `Partial`). 영역 비용 · 막힌 링크는 대략 그래프가 보지 않는다 — 세밀 경로가 보고 다가가며 다시 계획한다.
3. **내려간 구역 위 이동 = 가상 이동(정지가 아니다).** 서 있는 타일이 내려갔거나 앞선 가장자리에 닿았는데 그 너머가 내려가 있으면, 대략 점(클러스터 중심들 · 목적지)을 따라 최대 속도로 시간을 진행한다(충돌 · 회피 없음 — Mass 의 낮은 LOD 자리). 지나는 자리의 타일이 오르고 그 너머가 이어지면 군중으로 순간이동해 세밀 이동으로 돌아온다. 정지를 고르지 않은 까닭: 관찰자가 따라가지 않는 NPC(먼 마을로 가는 상인 · 서버의 관찰자 밖 NPC)가 영영 닿지 않는다. 오브젝트는 `Transform` 이면 자리, `CharacterController` 면 컨트롤러 자리를 옮긴다(내려간 셀에는 땅 충돌이 없다). `SteerOnly` 는 폰 이동이 자리를 정하므로 가장자리에서 기다린다. 셀 소속 재배정(내려간 셀의 NPC 를 지울지 · 옮길지)은 `w-cellstate` 의 몫이고, 내비 쪽은 에이전트가 살아 있는 동안의 이동만 정한다.
4. **되살림.** 계획할 때 군중 에이전트가 내비메시 밖(`INVALID`)이면 지금 자리로 순간이동해 다시 붙인다.
5. **긴 링크.** 끝이 이웃 타일 밖인 오프 메시 링크는 굽기에서 오류(`RecastNavMesh.OffMeshLinkSpansTiles`)로 멈추고 넣지 않는다 — Detour 가 이을 수 없는 모양을 조용히 넣지 않는다.
6. **디버그.** `gv_navDebugDraw` 비트 32(대략 그래프 — 클러스터 색이 그 타일의 로드 단계, 에이전트의 중간 목표 · 대략 점), HUD 섹션 `navstream`(npc 아래 — 종류마다 타일 로드 · 클러스터 · 먼 경로 · 다시 계획 수, 대상 에이전트의 상태), 감시 값 `Nav.RouteReplanCount` · `Nav.StreamLoadedTiles` · `Nav.VirtualAgents`, 개발 명령 `nav.moveto`.

**확인.** 단위 `NavClusterGraphTest`(덩어리 · 링크 간선 · A* · 왕복 · 깨진 바이트), `NavTileStreamingTest`(먼 목표 출발 → 오를 때마다 이어 도착 · 내려간 구역 가상 이동 · 되살림 · 관찰자 둘(서버 구성) · 계층 경로 질의), 시나리오 `game.wildwest.navstream`(prairie — 먼 셀 목표 · 다리 링크 · 군중 셋이 셀 경계를 넘음). 시나리오의 판정은 소스 콘텐츠(셀 없음)와 쿠킹본(`-cooked-content`, 셀 스트리밍) 모두에서 같게 적는다.

**측정.** 계획은 이벤트(목적지 · 타일 오르내림 · 중간 목표 도착)에만 돈다 — 프레임마다 도는 비용은 가상 이동 에이전트의 점 진행뿐이다. `NavClusterGraphTest.RouteCostBench`(64 × 64 타일 격자 A*)로 계획 한 번의 비용을 잰다. 대략 그래프가 없는 내비메시(지금의 모든 씬)는 분기 하나 말고 바뀐 길이 없다.
### 런타임 UI 공통 장치(설계 2026-11-20)

**문제.** 런타임 UI 는 화면 스택(`UISystem` — 레이어 · 모달 · 포커스 기억 · `UI.Back` · 게임 정지 요청), 방향 이웃(`WidgetNavigation` · `UINavigationSolver`), 장치별 글리프(`UIActionGlyphSource`), 일시정지 · 옵션 메뉴, 안전 영역 · 글자 배율 · 의사 현지화를 이미 갖췄다. 빠진 것은 공통 확인 대화 상자(설정 전용 창뿐), 메인 메뉴와 "시작" 명령(`requestEntranceScene` 을 부르는 곳이 없었다), 수량 창, 칸 메뉴 액션, 그리고 이 흐름을 값으로 보는 시나리오였다.

**대조.** 언리얼 CommonUI: 활성화 가능 위젯 스택 · 입력 라우팅 · `UCommonGameDialog`(Lyra `ULyraConfirmationScreen`) · 액션 바(보조 액션). 유니티 UI Toolkit · Godot Control: 포커스 이웃(`focus_neighbor_*`) · `ConfirmationDialog` · `SpinBox`. 우리 스택은 CommonUI 와 같은 자리라 새 스택을 만들지 않고 화면 종류만 더한다.

**결정.**
1. 엔진 화면 셋: `ConfirmDialogScreen`(답은 한 번 — 중복 누름 · 닫기 애니메이션 중 누름 버림, 뒤로 가기 = 취소), `QuantityDialogScreen`(슬라이더 1 ~ 최대), `MainMenuScreen`(시작 · 옵션 · 끝내기 — 끝내기는 확인 대화 상자 뒤). 문서는 엔진 기본 에셋 필수 칸(`_uiConfirmDialog` · `_uiQuantityDialog` · `_uiMainMenu`) — 코드에 경로가 없다.
2. 게임 흐름의 일은 전역 명령 표(`StartGame` · `QuitGame` · `ReturnToTitle`)로 — 화면은 흐름을 모른다. 메인 메뉴는 타이틀 씬의 `TitleMenuComponent` 가 연다.
3. 보조 액션 `UI.ContextMenu`(패드 X · 메뉴 키) — 탭 액션처럼 위젯 다음 화면(`onUnhandledAction`)이 받는다. 마우스 오른쪽 · 길게 누르기(입력 맵 hold 문턱)는 위젯이 직접 받는다.
4. 시나리오 감시 값: `UI.FocusedWidget` · `UI.FocusedText` · `UI.FocusedComposition` · `UI.ActionGlyph` · `Input.GlyphStyle` · `UI.TextOverflowCount`(글 위젯의 원하는 크기 > 배치 크기) · `Time.Paused`, IME 조합은 `<Text composition="true">`.

**확인.** 단위 `ConfirmDialogTest` · `GameFlowTest`. 시나리오 `game.playground.frontend` · `pausemenu` · `textinput` · `inventory` · `shopoffline`.

**남은 것.** 화면 전환 애니메이션은 문서 애니메이션(`Open` · `Close`)으로 화면마다 둔다(스택 공통 전환은 두지 않는다 — CommonUI 도 위젯마다 전환을 단다).

### 상점 흐름(설계 2026-11-20)

**문제.** 오프라인 가게 규칙(`ShopManager` · `Wallet` · `Inventory`)과 온라인 경제 키트(서버 구매 · 영수증)는 있었지만 게임 쪽 상점 화면 · 구매 흐름이 없었고, 클라이언트는 서버 답을 대조 없이 받았다.

**대조.** 상용 상점: 목록 → 상세 → 구매 확인 → 결과(성공 · 잔액 부족 · 서버 오류) → 재화 표시 갱신. 언리얼 Lyra 에는 상점이 없다. 유니티 Gaming Services Economy 클라이언트: 가상 구매 목록 → `MakeVirtualPurchaseAsync` → 결과 · 잔액 갱신, 서버가 정본.

**결정.**
1. 화면 하나 · 공급자 형식 하나(합성): `ShopScreen` 은 `IShopPurchaseProvider`(목록 · 잔액 · 갱신 번호 · 대기 여부 · 비동기 구매, 답은 정확히 한 번)만 안다. 오프라인 `LocalShopPurchaseProvider`(가게 · 지갑 · 캐릭터 가방), 온라인 `EconomyShopPurchaseProvider`(경제 키트).
2. 합계는 `ShopOffer::computeTotal` 한 곳(수량 범위 · 음수 단가 · int64 곱 넘침). 온라인은 화면이 넘긴 줄 대신 카탈로그 정의로 다시 셈한다.
3. 서버 답 대조: 바뀐 잔액이 이 상품 자산만 · 원장 범위 안 · 값 × 수와 지급 × 수만큼 움직였는지. 어긋나면 `Unverified` + `GetWallet` 스냅샷으로 다시 맞춘다. 실패해도 클라이언트는 재화를 되돌리지 않는다.
4. 중복 누름은 두 겹: 화면(`_bBusy`) · 공급자(`Busy`). 재시도는 같은 멱등 키만 — 영수증 id 가 그 키다.
5. 인벤토리 화면과 같은 가방: 산 것은 캐릭터 가방(`CharacterEquipmentComponent`)에 들고, 칸 · 무게가 모자라면 가게가 `NoRoom` 으로 거절한다. 가방 · 장비 조작은 `InventoryCommand` 하나(서버가 받은 요청을 같은 `validate` 로 거른다 — 복제 수량 · 음수 · 범위 밖 거절).

**확인.** 단위 `ShopPurchaseTest` · `EconomyShopProviderTest`(UnitTests.OnlineKits) · `InventoryCommandTest`. 시나리오 `game.playground.shopoffline`(사기 · 잔액 부족 · 무게 가득 · 세이브 왕복) · `game.shooter3d.onlineshop`(성공 · 서버 한도 거절 · 잔액 부족 · 서버 오류).

**남은 것.** 인벤토리 명령의 네트워크 전송(클라이언트 요청 → 서버 적용 → 복제)은 인벤토리 복제 키트가 생길 때 — 요청 모양과 검사는 이미 `InventoryCommand` 다. 결제 상품(`<Product>`)은 `StorePurchaseFlow` 길이라 이 화면에 싣지 않는다.

## 도구 · 에디터 · 콘텐츠 파이프라인 설계(2026-11-17)

미뤄 둔 항목을 작업자에게 바로 나눠 줄 수 있게 정했다. 절마다 문제(코드 자리) → 상용 대조 → 결정 · 기각 → 초안 → 이전 순서 → 측정이다(일과 사용자 결정 대기는 [NEXT](NEXT.md)). 새 린트 규칙은 없다 — 막을 것은 형식 · API · 로드 오류로 막는다.

### 머티리얼 그래프 컴파일러를 DevTools 로(설계 2026-11-17, NEXT 5 폴더 자리)

**문제.** `Source/Engine/Graphics/Material/Dev/`(소스 9 + 테스트 1)는 배포가 빼는 개발 코드인데 Engine 안에 있다. 묶는 것은 둘이다: REFLECT 타입 넷(`MaterialGraphLink` · `MaterialGraphValue` · `MaterialGraphNodeDef` · `MaterialGraphDef`)이 Engine 리플렉션 단계를 타고, `ShaderCookDriver.cpp:437` 이 `MaterialGraphGeneratedPaths::refreshAll` 을 직접 부른다(Engine → 개발 코드). 그 밖의 사용처는 Editor 뿐이고(AssetActions · ContentBrowser · MaterialGraph 패널) Editor 는 DevTools 를 본다(`TargetLayers.toml` `editor` 게이트).

**NEXT 가 적은 막힘은 이 이전에 걸리지 않는다.** `DataInheritance.h:232,243` 의 `SW_REGISTRAR_MODULE == "Engine"` 단언은 `SW_DATA_INHERITANCE_SCHEMA` · `_MERGE` 를 쓰는 손 형식에만 든다. 머티리얼 그래프는 리플렉션 형식이라 그 매크로를 쓰지 않고, 팩 쿠커는 문서 표의 줄(`datacataloginheritance.xml:42` `<Document root="MaterialGraphDef">`)로 펼친다(`ResourceComposer.cpp` `flattenByRootName` — 리플렉션을 읽지 않는다).

**대조.** 유니티 Shader Graph 는 에디터 어셈블리(`com.unity.shadergraph`)이고 `ScriptedImporter` 로 임포트 파이프라인에 스스로 붙는다. 언리얼 머티리얼 번역기(`FHLSLMaterialTranslator`)는 Engine 의 `WITH_EDITOR` 구역이고 셰이더 컴파일 · DDC 만 Developer 모듈이다. Godot `VisualShader` 는 런타임 리소스다. 이 엔진은 `WITH_EDITOR` 구역을 두지 않으므로 유니티 꼴(엔진이 창구를 내고 개발 모듈이 등록)이다.

**결정.** 엔진에 "쿠킹 전 셰이더 소스 생성기" 창구 하나를 두고 `DevTools/Material/` 이 정적 등록한다. 개발 구성에서 등록이 0 이면 쿠킹 실패다(낡은 생성물로 굽지 않는다).
- 기각 — Engine `Dev/` 유지: 표의 까닭("REFLECT + 쿠커가 부른다")이 둘 다 풀린다.
- 기각 — Editor 로: 헤드리스 쿠킹(`HeadlessSubsystem`)이 에디터 모듈 없이 돈다.
- 기각 — `DevTools/Client/Material/`: 부르는 쪽은 클라이언트뿐이지만 서버 구성에서 `SW_ASSET_TYPE( MaterialGraph )` 등록이 사라져 대상마다 에셋 형식 표가 갈린다. 지금과 같은 범위(서버도 짓는다)로 옮긴다.

```cpp
// Engine/Graphics/Shader/Compile/ShaderSourceGenerator.h
class SW_API IShaderSourceGenerator
{
public:
    virtual ~IShaderSourceGenerator() = default;
    virtual const utf8* getName() const = 0;
    /** @p contentRoot 아래 생성물을 소스 트리에 맞춘다(바이트가 같으면 쓰지 않는다). 하나라도 실패하면 false — 쿠킹은 실패로 센다. */
    [[nodiscard]] virtual bool refreshAll( const AssetPaths& assetPaths, string_view contentRoot, uint32& outWrittenCount ) = 0;

    struct Registration final : StaticRegistrar<IShaderSourceGenerator*> { explicit Registration( IShaderSourceGenerator& generator ); };
    /** 등록된 생성기를 이름 순으로 모두 돈다. 개발 구성에서 0 개면 `ShaderCooker.NoSourceGenerator` 오류다. */
    [[nodiscard]] static bool refreshAllRegistered( const AssetPaths& assetPaths, string_view contentRoot, uint32& outWrittenCount );
};
```

**단언 넓히기(`registerHostLibraryTypes` 범위).** 단언이 지키는 것은 "팩 쿠킹 시점(게임 모듈을 올리기 전)에 이미 올라온 이미지의 등록" 이다. 술어를 `constexpr bool DataInheritance::isCookVisibleModule( string_view )`(Engine · DevTools 만 참 — App · Server · ModuleManager 는 형식을 두는 자리가 아니다)로 바꾸면 된다. **지금은 하지 않는다** — DevTools 에 손 형식이 하나도 없어 쓰는 곳이 없다. DevTools 가 `SW_DATA_INHERITANCE_SCHEMA` 를 처음 쓰는 커밋에서 같이 바꾼다.

**판정(측정 없음 — 구조).** Engine 타깃 소스에 `MaterialGraph` 글자 0 건, Shipping 빌드 통과, `--cook-all` 뒤 `git status` 깨끗(생성물 바이트 같음), `matgraph_layer` · `matgraph_parameter` 시나리오와 `MaterialGraphCompileResultTest` 통과.

### GF 개발 전용 모듈 GameFrameworkDev(설계 2026-11-17, NEXT 5 폴더 자리)

**문제.** `GameFramework/Runtime/Foundation/GameData/Dev/GameSettingsLaunchOverride.cpp` 와 `…/Framework/Save/Dev/SnapshotFaultLaunchHook.cpp` 는 GF 창구(`IGameSettingsOverride` · `ISnapshotFaultHook` — `SW_GF_API`, `StaticRegistrar`)에 정적 등록하는 개발 코드다. DevTools 는 GF 를 볼 수 없어(`TargetLayers.toml` `devtools` 게이트) Runtime 안 `Dev/` 예외 두 줄로 남았다 — 둘째 줄(`d791901155`)이 생긴 것이 자리가 열려 있으면 쌓인다는 증거다.

**대조.** 언리얼은 모듈 `Type` 이 `DeveloperTool` · `UncookedOnly` 면 배포에서 빠진다(GameplayDebugger 가 게임 프레임워크 위의 그런 모듈이다). 유니티는 asmdef 의 `defineConstraints`(`DEVELOPMENT_BUILD || UNITY_EDITOR`)로 어셈블리 단위로 뺀다. Godot 은 모듈을 가르지 않고 `OS.is_debug_build()` 로 분기한다. → 언리얼 꼴(모듈 단위).

**결정.** 공유 모듈 `GameFrameworkDev` 를 `Source/GameFramework/Developer/` 에 둔다(폴더 이름이 `Dev/` 가 아니라 `DevFolders` 검사에 걸리지 않고, 배포에서 빠지는 것은 패턴이 아니라 매니페스트다). **새 매니페스트 칸 · 새 `ModuleKind` 는 없다** — 있는 것으로 된다:
- 개발 전용 표시 = `_listConfiguration: ["Dev"]`(매니페스트 해석 `ModuleManifest.cmake` 가 배포에서 끄고 `sw_skip_inactive_module` 이 짓지 않는다. 런타임은 `ModuleCatalog.cpp:52` 가 거른다).
- 공유 로드 = `_kind: "GameFramework"` + GameFramework 의존 — `GameModuleManager.cpp:517` 이 그 종류를 의존 순서대로 `loadSharedModule` 한다(GameFramework 다음, 키트 · 게임 앞).
- 기각 — `Dev/` 두 줄 유지(위 증거). 기각 — 매니페스트에 개발 전용 칸을 새로 두기(같은 뜻의 칸이 둘이 된다).

매니페스트 `GameFrameworkDev.module.json` 은 `GameFramework.module.json` 과 같은 칸에 `_listDependency` 가 GameFramework 하나, `_listConfiguration` 이 `["Dev"]`, `_listTarget` 이 Client · Server, `_bEnabledByDefault` 가 true 다.

**판정.** 배포 실행 파일에 `gv_saveRestoreBreakReplace` 글자 없음(`shipping-check`), `savetravel` 시나리오(결함 주입)와 로컬 온라인 실행(`-gv_onlineServerAddress`) 통과.

### Fileserve(설계 2026-11-20)

**문제.** Android 장치는 팩을 APK 안에서만 읽어(`BundledContentFileSource`) 다시 구울 때마다 APK 를 다시 묶고 설치한다.

**대조.** ezEngine Fileserve 는 장치가 PC 의 데이터 폴더를 TCP 로 받아 장치 캐시에 둔다. 언리얼은 쿡한 데이터를 네트워크 파일 시스템(`FNetworkPlatformFile`) · Zen 스트리밍으로 받는다. 둘 다 자체 TCP 프로토콜이다. → 장치가 받아 거울에 두고 디스크로 읽는 꼴은 같게 하고, 프로토콜은 읽기 전용 HTTP GET 셋(`/stat` · `/list` · `/file` 구간)으로 한다 — PC 쪽이 표준 라이브러리 서버 하나로 끝나고(`py -3 -m Scripts fileserve`), 장치 쪽은 엔진의 `IHTTPClient` 계약만 본다.

**결정.**
1. 장치 쪽 `FileServeContentSource`(Engine/Net/FileServe): `IContentFileSource` 구현. 받은 파일은 앱 전용 폴더의 거울, 곁 도장(크기 + PC 수정 시각 ns)이 같으면 다시 받지 않는다. 1 MiB 구간(장치 HTTP 본문 상한)으로 받아 원자 교체. 서버 응답은 믿지 않는다(목록 이름의 구분자 · `..` 거절, 숫자 칸 검사).
2. PC 쪽 `Scripts/net/fileserve.py`: 기본 바인드 루프백(USB 는 `adb reverse`), 경로는 상대 · `..` · 역슬래시 · 드라이브 · NUL 없음 + 실제 경로가 루트 아래, 읽기만, 오류 본문은 짧은 글.
3. **남은 몫 — 장치 전송.** App · DevTools 는 GameFramework(실제 HTTP 클라이언트 `BlockingHTTPClient`)를 링크하지 않고, 엔진은 일부러 네트워크 클라이언트를 싣지 않는다. 그런데 콘텐츠 소스는 엔진이 첫 팩을 읽기 전(`android_main` — 게임 모듈보다 앞)에 걸어야 한다.
   → 엔진 층에 개발 전용 평문 HTTP/1.1 GET 클라이언트(`PlatformSocketUtil` 위, TLS 없음, 응답 본문 상한, 시한)를 두고 배포 구성에서는 빼며, `AndroidMain` 이 `-fileserve=<주소>` 일 때 그것으로 `FileServeContentSource` 를 건다(거울 = 내부 저장소 `FileServe/`). GameFramework 의 HTTP 파서를 Engine 으로 내리는 길은 크기 L 이라 이 기능만으로는 하지 않는다.
   → 됨(2026-11-20): `Engine/Net/Dev/PlainHTTPClient`(GET · `Connection: close` · Content-Length · chunked, 1xx · 리다이렉트 · 압축 없음, TCP 는 `PlatformSocketUtil::connectTCPSocket` 등), 한도 · 시한은 `EngineConfig::_fileServe`(구운 엔진 설정에서 읽는다 — 정적 구성은 디스크 Config 가 없다), 인자 `FILESERVE`(`CommandLineManager::findValueBeforeParse`). 남은 것은 실기기 측정(아래).

**측정.** 재는 법: Android-Release 에서 팩 하나를 다시 구운 뒤 `adb reverse` + `-fileserve` 로 기동까지 걸린 시간 대 `android-apk` 다시 묶기 + 설치. 판정: 다시 받는 몫이 APK 길의 1/2 아래가 아니면 구간 크기 · 동시 받기를 본다.

### HDR 디스플레이 출력(설계 2026-11-20)

**문제.** 출력은 SDR 하나다 — 톤맵은 Reinhard(`tonemap.hlsl` 의 `x / (x + 1)`) 한 곡선, 백버퍼는 RGBA8 UNORM(`constant::render::kBackBufferFormat`, `RHIBackBufferFormat` 은 RGBA8 · BGRA8 둘), 색 공간은 Vulkan 이 sRGB 비선형만 고른다(`VulkanRHISwapChain.cpp`). HDR 모니터에서도 0 ~ 1 로 잘린 SDR 그림이다. UI(Canvas)는 Present 뒤에 같은 백버퍼에 Load 로 그린다(`RenderGraphPassDesc.h` Canvas 설명).

**대조.** 언리얼은 `r.HDR.EnableHDROutput` · `r.HDR.Display.OutputDevice`(sRGB · ST2084(PQ) 1000/2000 nits · scRGB) · `r.HDR.Display.ColorGamut` 로 고르고, 스왑체인을 HDR10(R10G10B10A2 + BT.2020 PQ) 또는 scRGB(R16G16B16A16F + 선형 Rec.709)로 만들며, UI 는 따로 그려 마지막 패스에서 종이 흰색(`r.HDR.UI.Level`)으로 합성한다(`r.HDR.UI.CompositeMode`). Godot 4.5 는 Vulkan · D3D12 에서 HDR 출력(최대 휘도 · 기준 흰색 설정)을 들였다. 유니티는 `HDROutputSettings`(모니터 능력 묻기 · 종이 흰색 · 톤맵 곡선이 출력 휘도를 안다). → 셋 다 "출력 장치 하나를 고르는 값 + 능력 묻기 + 출력 끝 인코딩 + UI 따로 합성" 이다. 같은 꼴로 간다.

**결정.**
1. **값 하나.** `DisplayOutputDesc { DisplayOutputKind _kind; float32 _paperWhiteNits; float32 _maxLuminanceNits; }` — `DisplayOutputKind { SDR, HDR10, ScRGB }`. 기본은 EngineConfig `_render._displayOutput`(SDR — 배포 기본, 골든 그대로), 사람은 사용자 설정(옵션 메뉴 "HDR", 종이 흰색 · 최대 휘도)에서 고른다. 실행 중 바꾸면 스왑체인을 다시 만든다(창 크기 바뀜과 같은 길).
2. **능력.** `RHICapabilities` 에 `_displayOutputSupport`(비트 — 백엔드 · 모니터가 받는 종류)와 모니터가 알린 최대 휘도. DX12 · DX11: `IDXGIOutput6::GetDesc1`(색 공간 · 최대 휘도) + `IDXGISwapChain3::CheckColorSpaceSupport`. Vulkan: `VK_EXT_swapchain_colorspace` 의 표면 포맷 목록(`VK_COLOR_SPACE_HDR10_ST2084_EXT` · `VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT`). GL · WebGPU · Android 는 SDR 만(능력 표가 정한 길 — 실패의 대체가 아니다). 고른 값이 능력 밖이면 정보 로그 한 줄 + SDR(`DisplayOutput::resolve( 요청, 능력 )` 순수 함수 하나 — 단위 테스트).
3. **RHI.** `RHIFormat::R10G10B10A2_UNORM` 을 더하고(백엔드 포맷 표 넷), `RHISwapChainDesc` 에 `RHIColorSpace _colorSpace { SRGBNonLinear, HDR10ST2084, ExtendedSRGBLinear }`. 포맷은 색 공간에서 정한다(SDR → 지금 값, HDR10 → R10G10B10A2, scRGB → R16G16B16A16_FLOAT) — 둘을 따로 받지 않아 맞지 않는 짝을 만들 수 없다. DX: `SetColorSpace1` + `SetHDRMetaData`(HDR10). Vulkan: 맞는 표면 포맷 · `vkSetHdrMetadataEXT`(있으면).
4. **톤맵 · 인코딩.** 곡선은 출력 휘도를 안다: SDR 은 지금 Reinhard 그대로(SDR 골든 불변), HDR 은 같은 곡선의 어깨를 `최대 휘도 / 종이 흰색` 으로 늘린 판. 인코딩은 출력 끝 한 곳: SDR 그대로, HDR10 은 Rec.709 → BT.2020 행렬 + PQ(ST 2084) OETF(nits = 값 × 종이 흰색), scRGB 는 선형 × (종이 흰색 / 80). 셰이더 퍼뮤테이션 `SW_DISPLAY_OUTPUT`(0 · 1 · 2) — Tonemap · 후처리 체인(Present) · FSR RCAS 출력 패스가 같은 `display_encode.hlsli` 를 쓴다(`present_output.hlsli` 처럼 출력 한 벌). CPU 거울 `ColorEncoding::encodePQ` · `decodePQ`(Core Math)로 단위 테스트(ST 2084 표 값: 100 nits → 0.5081, 1000 nits → 0.7518).
5. **UI 합성.** HDR 이면 Present 는 백버퍼 대신 장면 출력 RT(R16G16B16A16F, 디스플레이 기준 선형)에, Canvas 는 따로 SDR UI RT(RGBA8, 미리 곱한 알파)에 그리고, 새 패스 `RenderPassKind::DisplayEncode` 가 둘을 섞으며(UI 는 종이 흰색) 인코딩해 백버퍼에 쓴다. SDR 은 이 패스가 없다(파이프라인 XML 이 출력 종류에 따라 고른다 — 그래프 · 골든 그대로). 화면 회전(`FrameScreenRotation`)은 Android 라 SDR 만이다.
6. **확인.** 단위: `DisplayOutput::resolve`(능력 밖 → SDR + 로그), PQ 인코딩 표 값, 색 공간 → 포맷 짝. 시나리오: `engine/automation/render/hdroutput` — `-gv_displayOutput=HDR10` 을 HDR 이 없는 CI 에서 돌리면 SDR 로 떨어지는 로그 · 그림이 SDR 골든과 같다. HDR 모니터 있는 기계: 되읽은 백버퍼를 `decodePQ` 로 풀어 장면 RT 와 견주는 검사(`gpu.inspect` 한 픽셀 — 손 확인 대신 자동).
7. **차례.** ① 값 · `resolve` · CPU 인코딩 함수 + 단위 → ② RHI 포맷 · 색 공간 · 능력(DX12 먼저, Vulkan, DX11) → ③ 셰이더 인코딩 · 퍼뮤테이션 → ④ UI 합성 패스와 파이프라인 XML → ⑤ 옵션 메뉴. ① 은 빌드 없이 쓸 수 있고 ②~④ 는 단계마다 네 백엔드 SDR 골든이 그대로인지 본다.

**측정.** 없다(정확성). HDR 퍼뮤테이션이 SDR 프레임 시간을 바꾸지 않는지만 `bench.json` p50 으로 한 번.

**구현에서 바꾼 것(2026-11-20, ②~⑤).** ① 기본값은 사용자 설정 스키마 `display.hdr`(off) · `-gv_displayOutput` 이 맡고 EngineConfig `_render._displayOutput` 칸은 두지 않았다 — 원천을 둘로 만들지 않는다. ② 장면 패스는 SDR 과 같은 [0,1] 출력 신호(1 = 최대 휘도)를 내고 톤 곡선은 `x / (x + 어깨)`(어깨 = `(최대 휘도 / 종이 흰색)^(1/2.2)`, SDR 1)를 PassCB `g_DisplayOutput` 으로 받는다 — Tonemap · 후처리 체인에 퍼뮤테이션을 두지 않아 PSO 가 늘지 않고 SDR 셰이더 결과가 그대로다. 인코딩(퍼뮤테이션 `SW_DISPLAY_OUTPUT`)은 끝의 패스 하나라 RCAS 는 바꿀 것이 없다. ③ UI 합성 · 인코딩은 파이프라인 XML 패스 대신 회전 출력(`FrameScreenRotation`)과 같은 그래프 밖 출력(`FrameDisplayEncode`)으로 했다 — 화면에 쓰는 곳이 넷이 넘어(Present · Canvas · 캡처 → 화면 · 개발 겹치기 · 렌더 스레드 UI) XML 패스로는 다 모을 수 없고, 모두 `getScreenTarget` 한 자리를 본다. 패스는 `DisplayEncodeHDR10` · `DisplayEncodeScRGB` · `DisplayCompositeUI`(고정 포맷, 모니터가 HDR 을 받을 때만 PSO). 에뮬 백엔드(DX11)의 엔진 텍스처 칸을 늘리지 않으려고 UI 는 인코딩 앞 합성 패스로 장면 RT 위에 섞는다. ④ Vulkan 은 HDR 인 동안 다른 창(에디터 도구 창)을 새로 열지 않는다(주 창 렌더 패스 포맷을 따른다 — 코드의 shortcut 주석).

### 메모리 사용량 디버그 · 내보내기(설계 2026-11-20)

**문제.** 메모리를 볼 곳이 흩어져 있다 — 태그 표는 로그(`memory.report`) · HUD · Profiler 메모리 탭, 할당 사이트 상위 N 은 `-gv_profileAllocSites` 의 파일, 캐시 계수(`CacheStats`)는 `FrameProfileSession` 파일에만 있다. 에셋별 · 형식별 크기, GPU 자원별 VRAM, 두 시점 비교, 표 내보내기는 없다. "어디서 메모리가 늘었나" 를 숫자로 답할 수 없다.

**대조.** 언리얼은 LLM 태그 + Memory Insights(할당 콜스택 · 두 시점 비교), `memreport -full`(`obj list` · `ListTextures` · `rhi.DumpMemory` 의 글 보고), 유니티 Memory Profiler 는 스냅숏을 찍어 표(All Of Memory · Unity Objects — 거르기 · 정렬)로 보고 두 스냅숏을 비교한다. Godot 디버거의 Video RAM 탭은 자원(경로 · 형식 · 포맷 · 크기) 목록을 CSV 로 내보낸다. → 셋 다 "한 시점의 스냅숏 하나 + 표 여럿 + 두 스냅숏 비교 + 파일로 내보내기" 다. 같은 꼴로 간다.

**결정.**
1. **스냅숏은 일반 표 여섯.** `MemorySnapshot`(DevTools/Profiling/Memory) = 시트 여섯(Tags · AllocationSites · Assets · GPUResources · Types · Caches)이고, 시트마다 칸 서술(이름 · 종류 Text/Bytes/Count · 키 칸 · 크기 칸 · 거르기 칸)이 표 하나(`MemorySheetSchema`)에 있다. 비교 · CSV · 패널 · 원격 글이 시트별 코드를 갖지 않는다 — 시트를 더할 때 서술 한 줄과 수집 함수 하나만 는다.
2. **캐시 질의.** `AssetCacheBase::collectMemoryEntries`(순수 가상 — 새 캐시가 빠뜨리고 지을 수 없다, `getCachedCount` 와 같은 진단 계약이라 배포에도 둔다). 줄은 경로 · CPU · GPU 바이트 · 쥔 쪽 수, 종류는 캐시 이름. 약한 표는 `WeakInternTable::forEachLive`(잠금 안에서는 포인터 복사만 — 마지막 사본을 놓는 소멸자가 같은 잠금을 잡는다). 크기는 텍스처(밉 논리 크기 + CPU 사본) · 메시(정점 · 인덱스)만 정확하고 나머지는 `sizeof` — 컨테이너 몫은 태그 표가 센다.
3. **GPU 자원 공통 면은 `RHIMemoryTracker` 하나.** 네 백엔드가 이미 키별로 올리므로 줄에 모양(`recordTexture( 핸들, 서술, 바이트 )`)과 디버그 이름(`setLabel` — 백엔드 `setTextureDebugName` 한 줄)을 붙이고, `collectResources` 가 잠금 아래에서 복사한다. 해제는 백엔드가 펜스 뒤 콜백에서 내리므로 복사본이 곧 "GPU 가 놓은 뒤" 의 목록이다 — 렌더 스레드에 따로 펜스 복사를 두지 않는다. 렌더링 디버그(RT 목록 · HUD VRAM 줄)도 이 면만 읽는다.
4. **형식별.** 올라온 모든 씬(`SceneManager::collectAllScenes` — PIE · 미리보기 포함)의 오브젝트 저장소를 돌아 GameObject 형식 · 컴포넌트 형식(리플렉션 이름)마다 수 × `TypeInfo::_size`.
5. **할당 사이트.** 세부 추적(`memory.tracking`)이 켜져 있을 때 `getTopCallStacks` 상위 N(설정) — 기호 풀기는 수집 때 한다(원격 스냅숏은 주소가 아니라 글이어야 한다).
6. **배포에서 빠짐.** 수집 · 비교 · CSV · 명령은 DevTools(배포 호스트가 링크하지 않는다) · 에디터다. 엔진에 남는 것은 위 2 · 3 의 질의뿐이다.
7. **패널 "Memory"**(MVVM — Model 은 ImGui 없이 단위 테스트): 요약(CPU · GPU 합 · 태그 막대), 시트 탭(정렬 · 이름 거르기 · 거르기 칸), 스냅숏 찍기 · 둘 고르기 · 비교(늘어난 것 순 — 새로 생김 · 늘어남 · 줄어듦 · 사라짐), 에셋 줄 → 콘텐츠 브라우저 · 인스펙터. 원천은 이 프로세스(편집 · PIE 같은 프로세스)와 원격(개발 명령 `memory.snapshot` 의 답이 스냅숏 글 — 디버그 채널 `EditorRemoteInspector`).
8. **내보내기.** CSV(UTF-8 BOM · 쉼표 · RFC 4180 따옴표 · 첫 줄 머리말 · 바이트 칸은 원값 + `(MB)` 칸 따로). 표 하나 또는 스냅숏 전체(시트마다 파일 + Summary). 폴더는 `<Saved>/<_exportFolder>/<yyyyMMdd-HHmmss>`(같은 초는 `-2` — 덮지 않는다), 설정 `Config/Dev/MemorySnapshot.json`(없으면 오류). 쓰기는 워커(에디터 `EditorBackgroundTask`, 명령 · 인자는 `TaskManager`). 개발 명령 `memory.export [폴더]`, 실행 인자 `-memory-export=<폴더>`(루프가 끝날 때 한 번 — 언리얼 `-csvExitOnCompletion` 자리), `SavedRetention` 규칙 한 줄.

**확인.** 단위: 수집(가짜 캐시 · 추적기에 올린 가짜 자원), 비교(늘어남 · 새로 생김 · 사라짐), CSV(BOM · 이스케이프 · 한글 경로 · 숫자 칸), 스냅숏 글 왕복, ViewModel(정렬 · 거르기 · 비교). 시나리오 `editor/memorypanel`: 패널 열기 → 찍기 두 번 → 비교 → 내보내기 → `memory.exportinfo` 가 읽은 머리말 · 행 수.

### 헤더 팬인 다이어트(설계 2026-11-20, 감사 8)

**문제.** 공통 헤더 하나를 고치면 전체 재빌드(50 분대 — 4 절 "검증 시간")다. 숫자(`py -3 -m Scripts header-fanin`, 2026-11-20, TU 3685 · pch 가 끄는 헤더 65): 점수(헤더를 포함하는 TU 수 × 그 헤더가 pch 밖에서 끌어오는 헤더 수) 합 867 490. 위 넷은 오브젝트 · 리플렉션 핵심이다 —
`GameObjectManager.inl`(317 TU × 40), `TypeRegistry.h`(524 × 23), `ReflectionTypes.h`(666 × 17), `GameObject.h`(594 × 19), `TypeInfo.h`(677 × 16), `Component.h`(703 × 15), `SceneComponent.h`(417 × 22), `GameObjectStore.h`(317 × 24). `ReflectionMacros.h` 는 2664 TU(72 %)지만 pch 밖 3 개라 싸다.

**대조.** 언리얼은 IWYU 모드(`bEnforceIWYU`, 모듈마다 `CoreMinimal.h` 만 기본) · 전방 선언 · `*Fwd.h`(`UObject/ObjectMacros.h` 와 `ObjectPtr` 의 전방 선언 헤더)로 팬인을 줄이고, 크로미움은 `analyze_includes.py` 로 "추가 컴파일 크기" 를 잰다(위 점수와 같은 생각). → 재는 숫자 하나 + 전방 선언 헤더 + 템플릿 몸체 분리(이미 `GameObjectManager.inl` 이 한 꼴)다.

**결정.**
1. **숫자가 정본.** 커밋마다 `header-fanin --json` 전후를 견주고, 점수 합이 줄지 않는 변경은 하지 않는다(줄 수가 아니라 점수). 빌드 시간은 `build_time_report.py` 로 묶음 끝에 한 번.
2. **전방 선언 헤더.** `Engine/Object/ObjectFwd.h`(Component · GameObject · GameObjectManager · SceneComponent · 핸들) · `Engine/Reflection/ReflectionFwd.h`(TypeInfo · PropertyInfo · EnumInfo · FunctionInfo · TypeRegistry) — 포인터 · 참조만 쓰는 헤더는 이것만 포함한다.
3. **차례(점수 큰 순 · 헤더끼리 겹치지 않게).** ① `Component.h` — `TickContext.h` · `AnimationNotifyPhase.h` · `PhysicsHandle.h` 를 쓰는 함수 선언이 값을 받는지 보고 전방 선언으로. ② `GameObject.h` — `Component.h` 전체 대신 전방 선언(컴포넌트 목록은 포인터), `TickRegistry.h` · `ReflectionCast.h` 는 템플릿 몸체가 쓰는 자리만 `.inl` 로. ③ `TypeRegistry.h` — `TaskTypes.h` · `EngineMinimal.h` · `ReflectionCast.h` 를 cpp 로. ④ `ReflectionTypes.h` → `TypeInfo.h` 묶음은 리플렉션 생성 코드(`.gen.cpp`)가 읽으므로 생성기 템플릿과 함께. ⑤ `SceneComponent.h` · `GameObjectStore.h`.
4. **막기(형식).** 다이어트한 헤더는 `Config/Rules/Lint` 의 헤더 포함 상한 표(헤더마다 pch 밖 포함 수 상한 — 지금 값)를 더해 늘면 진다 — 린트가 아니라 측정 숫자의 문턱이다(다시 늘어나는 것을 막는 형식이 C++17 에 없다).
5. **확인.** 단계마다 Release · Shipping · ASan · 서버 증분 빌드(포함을 지우면 다른 TU 가 그 헤더를 간접으로 받던 것이 깨진다 — 컴파일러만 판정한다), clang `-Werror=undefined-func-template` 가 `.inl` 빠짐을 잡는다.

**측정.** 재는 법: 단계 전후 `header-fanin` 점수 합과 위 넷의 점수, `Component.h` 를 한 줄 고친 뒤 증분 빌드 시간. 판정: 단계마다 점수 합 5 % 이상 · 그 헤더 점수 30 % 이상 줄지 않으면 되돌린다.

### 렌더 그래프 접근 핸들 ② ~ ④ — 데이터 · 실행기 · 패스 몸통(설계 2026-11-20)

①(19829a1506)은 그래프 안이다 — 접근 핸들 · `importResource` · 접근에서 전이 추론 · 테스트. 남은 것은 프레임 파이프라인이 그것을 쓰게 하는 일이다(손 전이 89 곳 — `FrameGraphExecutor` 16 · `VFXGPUSimulation` 13 · `FramePassExecutorPasses` 12 · `FramePassExecutorDraw` 10 · `FrameAutoExposure` 6 · `HZBOcclusion` 6 · 나머지).

**결정.**
1. **데이터(파이프라인 XML) — 새 문법은 최소.** `RenderGraphResourceDesc`(`_listResource` 원소 — 이미 외부 · 영속 자원 이름과 수명)에 `RenderGraphViewableKind _viewable`(기본 Buffer)을 더한다. 패스의 `_listInput` · `_listOutput` 은 그대로 두고 접근은 규칙으로 정한다: 첨부 이름 입력 = TextureSRV, 출력 = RenderTarget(깊이 첨부는 DepthRead · DepthWrite — `_depthAttachment` 와 읽기 여부), `_listResource` 버퍼 입력 = BufferSRV, 출력 = BufferUAV. 규칙 밖 접근(간접 인자 · 복사)만 패스의 `_listIndirectArgs` · `_listCopySource` · `_listCopyDest`(이름 목록)로 적는다. 로드 검증이 한 패스의 같은 이름 두 접근을 거절한다(①의 `Renderer.GraphDuplicateAccess` 와 같은 규칙).
2. **실행기 — 버퍼 표.** `FrameBufferTable`(이름 → RHI 버퍼 핸들 · 처음 접근): 버퍼 주인(GPUScene 컬링 인자 · HZB · 라이트 그리드 · 입자)이 프레임 시작에 `registerBuffer( 이름, 핸들, 접근 )` 하고 그래프에 `importResource` 한다. 레벨 콜백(`FramePassExecutor::onGraphLevelPrologue`)이 텍스처 전이와 함께 버퍼 전이를 `transitionBuffers` 한 번으로, `_bUAVBarrier` 는 `uavBarrier` 로 낸다(①의 shortcut 주석 자리). 큐: 비동기 패스의 전이는 컴퓨트 리스트에(`_queue`).
3. **패스 몸통 — 핸들로만.** `RenderGraphPassContext::getBuffer( RenderGraphBufferUAV )` · `getTexture( RenderGraphTextureSRV )` 가 선언한 접근에서만 RHI 핸들을 준다(선언 안 한 이름은 오류 로그 + 무효 핸들 — 손으로 묶을 길이 없다). 한 패스 안 연속 디스패치(HZB 밉 사슬 · VFX 방출기 · 컬링 → 정렬)는 `context.uavBarrier( RenderGraphBufferUAV )` — SRV 핸들로는 부를 수 없다.
4. **④ 형식으로 막기.** 패스 몸통이 받는 명령 리스트를 전이 함수가 없는 면(`RenderPassCommandList` — 그리기 · 디스패치 · 바인딩만)으로 바꾸고 `transitionBuffer` · `prepareTexture*` 는 그래프 실행기 · 프레임 끝(스왑체인 · 되읽기)만 쓰는 `IRHICommandList` 에 남긴다 — 손 전이가 돌아오면 컴파일이 멈춘다.
5. **차례(작은 것 → 큰 것, 단계마다 빌드 · 골든).** ② HZB(6 곳, `hzb` 골든과 가림 판정 시나리오) → VFX GPU 시뮬레이션(13) → 자동 노출(6) → `FrameGraphExecutor`(16) → `FramePassExecutorPasses` · `Draw`(22) → 나머지. ③ 뷰 패스. ④ 명령 리스트 면.
6. **확인.** 단계마다: 네 백엔드 골든 그대로, DX12 · Vulkan 검증 층 오류 · 경고 없음(preflight 체인의 `-rhi-validation`), 프레임 전이 수(`RT.Graph.transitions` — 실행기가 센다)가 늘지 않음. 단위: `FrameBufferTable` 등록 · 중복 이름 거절, 선언 안 한 핸들 요청 거절.

**측정.** 프레임 GPU 시간 · 전이 수가 이전 단계와 같거나 적다(늘면 그 단계에서 고친다 — 같은 상태 반복은 ①이 이미 걸러 낸다).

**② 첫 단계에서 정한 것(2026-11-20).** 표는 이름 하나에 **뷰 칸**(`registerBuffer( 이름, 칸, 핸들, 처음 접근 )` — 주 시점 0, 추가 뷰 번호 + 1)을 둔다 — 프레임 패스 하나가 모든 뷰를 돌아 HZB 가 뷰마다 버퍼다(언리얼은 뷰마다 RDG 자원). 주인은 이번 프레임에 쓰는 칸만 등록한다(가림 판정 기본 끔 — 등록하면 쓰지 않는 버퍼에 전이가 난다). HZB 는 프레임 패스 `HZBBuild` 만 옮겼고, 뷰 그래프 안의 두 짓기(2 단계 가림 · 반사 앞)는 `HZBOcclusion::recordInViewPass` 에 손 전이로 남아 ③에서 지운다. 텍스처 UAV → UAV 배리어는 만드는 접근이 없어(뷰 패스는 첨부, 프레임 텍스처 출력은 렌더 타깃) RHI 에 더하지 않았다. 뷰 · 확장 패스의 깊이 첨부는 `DepthWrite` 로 나눴다(RHI 상태는 형식이 고른다 — 깊이 읽기 전용 바인딩 `DepthRead` 는 D3D11 읽기 전용 DSV 확인 뒤).

**② VFX 단계에서 정한 것(2026-11-20).** (1) **등록 접근 = 프레임 시작이자 끝.** 그래프가 다른 접근에 둔 등록은 실행기가 그래프 끝(뷰 그래프 앞, 그래픽스 리스트)에 등록 접근으로 되돌린다(`FrameBufferTable::forEachChangedAccess` · `FramePassExecutor::returnFrameResourcesToInitialAccess` — 언리얼 외부 자원의 끝 접근 `SetBufferAccessFinal`). 끝 상태를 따로 적는 API 를 두지 않아 잊을 수 없다. 뷰 그래프 · 프레임 끝 되읽기가 상태를 바꾸면 쓰고 나서 등록 접근으로 되돌린다(메시 되읽기는 인자를 간접 인자로). HZB 가림 타일은 이제 끝에 셰이더 읽기로 돌아간다(전이 하나 — 등록 접근과 실제 시작 상태가 같아졌다). (2) **패스 몸통은 전이하지 않으므로 읽는 상태가 다른 디스패치는 패스를 나눈다.** 입자는 `ParticleSimulation`(UAV 쓰기) · `ParticleSort` · `ParticleMeshInstance`(셰이더 읽기) 셋 — `VFXParticleSort` · `VFXMeshInstance` 가 프레임 패스 종류가 됐다(언리얼 Niagara 정렬도 따로 RDG 패스). (3) **쓰는 패스가 다른 방출기는 이름을 나눈다.** 그래프는 이름마다 상태 하나라, 밀린 단계가 없어 시뮬레이션하지 않는 방출기를 같은 이름에 두면 UAV 로 오가 전이가 는다 — 시뮬레이션하는 방출기 `ParticleState` · `Particles` · `ParticleArgs`(칸 = 디스패치 번호), 읽기만 하는 방출기 `IdleParticles` · `IdleParticleArgs`(칸 = 읽기 번호), 순서 버퍼 `ParticleOrder`. 라이트 버퍼 · 인스턴스 버퍼는 입자가 쓰는 프레임에만 등록한다(인스턴스 버퍼는 `FrameGraphExecutor` 단계에서 GPUScene 이 늘 등록하도록 넓힌다 — 지금은 인스턴스 애니메이션이 손 전이를 남겨 그래프 추적과 실제 상태가 잠깐 다르다. RHI 가 실제 상태를 추적하므로 맞지만 `RT.Graph.transitions` 에 중복이 섞인다). (4) 깊이 히스토리 RW 전이는 생산자(`copySceneDepth`)가 둔다 — 입자 몸통에서 지웠다.

**② 자동 노출은 ③ 첫 단계로 옮긴다(2026-11-20).** 손 전이 6 곳(`FrameAutoExposure::recordAutoExposure`)은 프레임 패스가 아니라 **뷰 그래프 패스 몸통**(톤맵하는 `Tonemap` · 후처리 체인 `Present` 가 그리기 앞에 부른다)이다. 뷰 그래프에는 아직 자원 표가 없고(`onGraphPassExecute` 는 `_pResourceTable` 을 싣지 않는다, 레벨 콜백은 프레임 표 `_frameBufferTable` 만 본다), 측정 쓰기와 톤맵 읽기가 한 패스 안이라 ② 모양(몸통은 전이하지 않는다)으로 옮길 수 없다. ③ 에서 할 모양: (1) 뷰 그래프 실행마다 뷰 표(칸 = 뷰)에 뷰 소유 텍스처(노출 기록 두 칸)를 등록하고 `importResource`, 레벨 콜백은 그 그래프의 표를 본다. (2) 측정을 뷰 패스 `AutoExposure`(종류 `AutoExposure` · `AutoExposureHistogram` 이 이미 있다)로 나눠 입력 `SourceColor` · `AutoExposureHistory`, 출력 `AutoExposure`(히스토그램은 UAV, 풀스크린은 렌더 타깃 — 구현이 접근을 고른다)를 선언하고 톤맵은 `AutoExposure` 를 입력으로 읽는다(언리얼 EyeAdaptation 도 톤맵 앞 따로 RDG 패스). 원본 `prepareTextureForShaderRead` 는 톤맵 입력 선언이 이미 낸다 — 그때 함께 지운다.

**② FrameGraphExecutor 단계에서 정한 것(2026-11-20).** 인스턴스 애니메이션 · 모프 · 스킨 · 라이트 그리드 · 컬링(되돌리기 → 컬링 → 정렬)의 손 전이 13 곳을 옮겼다. (1) **쓰는 패스가 도는 프레임에만 등록한다**(`InstanceBuffer` 는 인스턴스 애니메이션 `shouldAnimateInstances` · 메시 입자, `DeformVertices` 는 모프 · 스킨이 디스패치할 수 있을 때, `LightGrid` 는 GPU 생산자 프레임). 쓰지 않는 프레임은 등록 접근(셰이더 읽기)에서 쉬어 읽는 패스가 그 상태를 그대로 본다 — 쓰는 패스가 선언돼 있어도 전이가 나지 않는다. (2) **컬링 일감은 프레임 시작에 모은다**(`collectCullJobs` — 등록과 디스패치가 같은 목록). 준비되지 않은 추가 뷰를 쉬게 하는 일도 그때라 HZB 등록보다 앞이다. (3) 컬링 버퍼는 등록 접근이 달라 이름을 나눴다 — `CullArgs`(간접 인자) · `CullVisibleInstances`(셰이더 읽기) · `CullLODState` · `OccludedOrders`(UAV), 칸 = 컬링 슬롯. 컬링 뒤 "그리는 쪽 상태로" 전이는 그래프 끝 되돌림(`forEachChangedAccess`)이 맡는다. (4) 남긴 것: 깊이 히스토리 복사의 텍스처 전이 3 곳 — 읽는 쪽(HZB · 입자)이 RW 텍스처로 읽는데 데이터 규칙의 입력은 셰이더 읽기라 선언할 접근이 없다(텍스처 UAV 입력 목록 `_listUnorderedAccess` 를 더할 때 옮긴다). 뷰 그래프 안의 2 단계 가림 뒤 단계 · 반사 앞 HZB 는 ③.

**② 나머지 프레임 패스(2026-11-20).** 볼륨 안개(`VolumetricFogVolume`) · 실시간 프로브(`ProbeIrradiance` · 새 `ProbeHistory`) · 화면 마커(`CanvasMarkers`, 인스턴스 버퍼 읽기 포함)의 손 전이 7 곳을 옮겼다 — 주인 함수는 리스트만 받고, 등록과 선언 확인은 `FrameGraphExecutor` 가 한다(도는 프레임에만 등록). 이로써 프레임 패스 몸통에 남은 손 전이는 깊이 히스토리 텍스처 3 곳뿐이다. `FramePassExecutorPasses` · `Draw`(22)는 뷰 그래프 패스 몸통(디퍼드 타일 분류 · 컴퓨트 후처리 · 그림자 층)과 그래프 밖 굽기(아틀라스)라 자동 노출과 같이 ③ 의 뷰 표가 먼저다 — ② 는 여기서 닫는다.

### 자원 구간 할당(설계 2026-11-20, 감사 R-4)

**문제.** 머티리얼 인스턴스마다 상수 버퍼 하나 · bindless 디스크립터 하나(`MaterialRenderProxy::sync`)이고, 버퍼마다 백엔드 할당이 따로다 — DX12 는 업로드 커밋 자원(256 B 정렬 × 비행 중 프레임 수, `D3D12RHIResourceFactory::createConstantBuffer`), Vulkan 은 자원마다 `vkAllocateMemory`(`VulkanRHIDevice::allocateDeviceMemory` 9 곳). 인스턴스 5000 이면 할당 수가 Vulkan `maxMemoryAllocationCount`(흔히 4096)에 닿는다 — 지금은 한도 가드(`RHIConfig::_allocationCountReserve`)가 오류로 거절할 뿐이다.

**대조.** 언리얼은 상수를 `FD3D12FastConstantAllocator`(프레임 링) · 풀 할당기(`FD3D12PoolAllocator`)에서 구간으로 떼고, Vulkan 은 `FVulkanResourceHeap` 페이지에서 나눈다. 유니티 SRP Batcher 는 머티리얼 속성을 큰 영속 GPU 버퍼 하나에 두고 오프셋으로 읽는다. Godot 은 VMA. → 두 층이다: (가) 백엔드 메모리를 큰 덩이에서 나누기, (나) 작은 상수를 페이지 구간으로 모으기.

**결정.**
1. **(나) 먼저 — 셰이더 계약을 바꾸지 않는 구간.** `RHIBufferSlice { RHIBufferHandle _page; uint32 _offset; uint32 _size; }` 와 백엔드 무관한 `ConstantBufferPageAllocator`(페이지 = `createConstantBuffer( 64 KiB )`, 크기 등급 256 · 512 · 1024 · 2048 · 4096 B 의 빈 칸 목록, 등급 밖은 전용 버퍼). bindless 는 구간마다 디스크립터 하나(`registerBindlessBufferSRV( RHIBufferSlice )` — DX12 CBV/SRV 의 오프셋 · Vulkan 디스크립터의 offset · range · GL `glBindBufferRange` · WebGPU 바인딩 오프셋(256 B 정렬 — 등급이 이미 맞춘다)). 셰이더는 그대로 머티리얼 디스크립터 하나를 읽는다. 비행 중 프레임 복제는 페이지가 갖는다(지금 버퍼마다 하던 것 — 갱신은 그 프레임 칸에).
2. **(가) 다음 — 백엔드 메모리.** Vulkan VMA · DX12 D3D12MA(둘 다 MIT, GPUOpen)로 자원 메모리를 덩이에서 나눈다(배치 텍스처 `RT.Frame.transientPlacements` 의 힙도 같은 할당기로). **새 의존성이라 라이선스 확인은 사용자 결정 줄**(NEXT "사용자에게 받을 것") — 확인 전에는 (나) 만으로 할당 수가 등급 수 × 페이지 수로 준다.
3. **차례.** ① `RHIBufferSlice` · 페이지 할당기(CPU 쪽 빈 칸 목록은 백엔드 없이 단위 테스트 — 등급 고르기 · 해제 뒤 재사용 · 페이지 비면 반납) ② 백엔드 넷의 구간 디스크립터 ③ `MaterialRenderProxy` 가 구간을 쓴다(레이아웃이 커지면 등급을 옮긴다 — 지금의 "커지면 다시 만든다" 자리) ④ VMA · D3D12MA(확인 뒤).
4. **확인.** 단위: 할당기. 시나리오: 머티리얼 골든 그대로(네 백엔드), Vulkan 검증 층 경고 없음. 측정 아래.
5. **② · ③ 구현에서 정한 것(2026-11-20).** 페이지는 `createConstantBufferPage` 로 만든다 — DX11 은 동적 버퍼의 WRITE_DISCARD 가 페이지 전체를 버려 기본 용도 버퍼 + `UpdateSubresource1` · `*SetConstantBuffers1`(D3D11.1 능력 없으면 오류)이고, 넷은 `createConstantBuffer` 와 같다. 페이지 RHI 버퍼 · 구간 디스크립터는 디바이스가 하나씩 갖는 `ConstantBufferPagePool`(`IRHIDevice::getConstantBufferPagePool`)이 들고, 등급 밖(4096 B 초과)은 그 풀이 전용 버퍼 하나를 준다. 프레임 칸 채우기(`RHIConstantBufferMirror`)의 열쇠는 (버퍼, 오프셋)이다. 지표는 `RT.RHI.liveBuffers`(DX12 커밋 버퍼 수) · `RT.RHI.constantBufferPages` · `RT.Vulkan.allocationCount`.

**측정.** 재는 법: 머티리얼 인스턴스 5000 씬(`load/materialinstances` — 없으면 만든다)에서 상수 버퍼 바이트 · 백엔드 할당 수(Vulkan `RT.Vulkan.allocationCount`, DX12 커밋 자원 수) · 프레임 시간 p50, 전후. 판정: 할당 수가 1/10 아래로 줄고 프레임 시간이 흔들림 안이면 둔다.

### 렌더링 런타임 디버그(설계 2026-11-20)

**문제.** 렌더 타깃 창(`RenderTargetPanel` — 주 시점 첨부 목록 · 채널 · 깊이 선형화 · 픽셀 값), `GPUPixelInspector`, 프로파일러의 패스 GPU 시간, 보기 모드 16 개, 개발 명령 `gpu.show` · `gpu.timings` · `gpu.inspect` · `render.graph` · `lod.explain` · `renderdoc.*` 가 있다. 빈 것: 그 지점까지 그린 그림을 드로우 하나씩 넘기며 보는 도구가 없고(렌더독을 띄워야 한다), 렌더 그래프는 글(`render.graph`)로만 보이며, 그래프 밖 구조 버퍼(HZB · 가림 타일 — `FrameBufferTable`)와 HDR 출력의 장면 RT · UI RT 는 볼 곳이 없고, 가림 컬링 · 텍스처 스트리밍은 켜고 끄기(`gv_occlusionCulling` · `gv_textureStreaming`)만 있다. 렌더 타깃 창은 밉 0 만 읽고 HDR 값은 0 ~ 1 로 잘라 보인다.

**대조.** 언리얼: Buffer Visualization 보기 모드 · `vis <RT> [mip]`(RDG 자원을 화면에), `ProfileGPU`(패스 트리와 시간), RDG Insights(패스 · 자원 · 수명 · 전이 · 비동기 큐 타임라인), Shader Complexity, 프레임 단위 드로우는 RenderDoc 연동에 맡긴다. 유니티: Frame Debugger(이벤트 목록 = 패스 → 드로우, 고른 이벤트까지만 그려 게임 뷰와 RT 를 그 시점으로, 그 드로우의 셰이더 · 키워드 · 속성 값 · 바인딩), Rendering Debugger 창(디스플레이 통계 · 머티리얼 · 조명 · 렌더 타깃 겹쳐 보기, 런타임 겹치기 화면도 같은 항목). Godot: 뷰포트 Debug Draw 모드(Overdraw · Unshaded · Wireframe · 버퍼 보기), 디버거의 비디오 메모리 패널(텍스처마다 VRAM)과 Monitors(드로우 · 프리미티브 · VRAM). → 셋 모두 (가) 이벤트를 넘기며 "여기까지" 그림, (나) 자원 목록과 그 내용 보기, (다) 그래프 · 수명 그림, (라) 화면 위 통계 줄을 갖는다. 우리는 (나) 의 텍스처 반쪽만 있다.

**결정.**
1. **차례(영향 큰 순).** ① 렌더 타깃 창 보강(밉 고르기 · HDR 노출 · HDR 장면 RT · UI RT 목록) → ② 프레임 디버거 → ③ 렌더 그래프 그림 → ④ 가림 · 스트리밍 보기와 HUD 렌더 범주 → ⑤ 버퍼 보기. ①②는 "이 픽셀이 왜 이런가" 의 첫 질문이고 ③~⑤는 그 뒤의 "왜 이 패스 · 왜 이 버퍼" 다.
2. **엔진 쪽 우편함 하나 — `RenderDebugRegistry`.** `RenderTargetRegistry` 와 같은 서비스 서브시스템(UI 가 요청하고 렌더 스레드가 프레임 경계에서 처리해 값으로 공개, 세대 번호로 다시 읽을지 안다): 그래프 스냅숏 요청 · 공개, 프레임 디버거 설정 · 드로우 기록, 버퍼 읽기 요청 · 결과, 프레임 통계(드로우 · 삼각형 상한). 렌더러에는 호스트(`FrameRendererHost`)로만 건다 — 화면 밖 렌더러는 기록하지 않는다. 렌더러의 훅은 셋이다: 패킷 처리 앞뒤(`FrameRenderer::executePacket` — 프레임 시작 · 그래프 스냅숏), 드로우 직전(`FramePassExecutor::drawBatchGroup` — 허용 · 기록), 프레임 그래프 끝(`recordFrameGraph` — 버퍼 되읽기). 모두 `!SW_SHIPPING` 이고 꺼져 있으면 원자 값 하나를 읽는다.
3. **프레임 디버거 — 유니티 꼴 "여기까지".** 멈춤 지점 = (패스, 그 패스 안 드로우 번호). 그 패스의 뒤 드로우와 실행 순서상 뒤 패스의 씬 드로우를 건너뛴다. 전체 화면 · 컴퓨트 패스는 그대로 돈다 — 조명 · 후처리가 "N 번째까지 그린 G버퍼" 로 계산되어 화면과 렌더 타깃 창이 모두 그 시점을 보인다(유니티는 뒤 이벤트를 모두 멈추지만 우리 디퍼드 경로는 조명이 따로라 이쪽이 더 많이 보인다). 주 시점만 — 렌더 타깃 창이 보는 풀과 같다. 기록은 드로우마다 PSO · 인덱스 수 · 묶음(멀티 드로우) 수 · 삼각형 상한, 그리고 PSO 의 바인딩 레이아웃(리플렉션 이름 · 레지스터 · 구조 보폭 · CB 멤버)이다. 병렬 기록이라 드로우 번호는 패스 안에서만 센다(패스 하나는 한 스레드가 기록한다). 게임 시간은 멈추지 않는다 — 멈춘 그림이 필요하면 에디터 일시정지를 쓴다.
4. **렌더 그래프 그림 — 실행 결과의 값.** `RenderGraphSnapshot::build( graph )`(순수 함수 — 단위 테스트)가 실행 순서 패스(레벨 · 큐 · 비동기 묶음 · 전이 수 — 같은 추론을 빈 문맥으로 다시 돈다), 자원(종류 · 수명 · 읽는 패스 · 쓰는 패스)을 값으로 낸다. 에디터는 `EditorNodeGraph` 로 레벨 = 열, 패스 = 노드, 생산 → 소비 = 링크로 그리고 노드에 전이 수 · GPU 시간(`GPU.<패스>` 프로파일러 구간)을 겹친다. 뷰 그래프와 프레임 그래프 둘 다.
5. **가림 · 스트리밍.** 가림 타일은 이미 CPU 사본이 있다(`FrameRenderer::getOcclusionSnapshot` — 몇 프레임 늦은 16 px 밉). 창은 그 격자를 깊이 회색으로 그리고(가린 타일 = 1 미만) 살아 있는 프리미티브 경계 구를 GPU 컬링과 같은 식(`OcclusionTileSnapshot::isSphereOccluded`)에 넣어 가린 수를 센다. 스트리밍은 이미 있는 프로파일러 수(`RT.TextureStreaming.residentBytes` · `poolBytes` · `bytesInFlight` · `RT.TextureUpload.waiting`)를 읽는다 — 새 계측이 없다. 둘 다 HUD 범주(`occlusion` · `streaming`)로도 보인다. 새 보기 모드는 만들지 않는다(셰이더 퍼뮤테이션이 늘고 같은 정보가 창 · HUD 에 있다 — 필요해지면 언리얼 `Visualize Occlusion` 처럼 그때 더한다).
6. **버퍼 보기.** `FrameBufferTable` 등록은 크기를 함께 적는다(구조 버퍼 판 `registerBuffer( 이름, 칸, const RHIStructuredBuffer&, 접근 )` — 크기를 모르는 핸들은 읽지 않는다). 창이 이름 · 칸을 고르면 프레임 그래프 끝에서 `AsyncBufferReadback` 으로 읽어 공개한다. 풀이는 `BufferViewLayout`(원소 보폭 + 칸 목록 — 셰이더 리플렉션 `ShaderBindingSlot::_listElementMember` 에서 만들거나 float · uint · int 한 종류 보폭) 순수 함수로 표 행을 만든다(단위 테스트).
7. **HUD.** 이미 있는 `draw` 범주에 삼각형 상한 · 전이 · GPU · VRAM(추적 바이트)을 더하고, `occlusion` · `streaming` · `framedebug` 범주를 `SW_DEBUG_HUD_SECTION` 으로 등록한다. 게임 중에는 개발 명령 `framedebug <패스> <드로우> | off`(유니티 Frame Debugger 의 이벤트 고르기)과 `gpu.show`(언리얼 `vis`)가 같은 일을 한다.

**확인.** 단위: `RenderGraphSnapshot`(패스 수 · 레벨 · 전이 수 · 수명), `BufferViewLayout`(리플렉션 칸 → 행 글, 보폭 밖 자르기), 렌더 타깃 노출 · 밉 크기, 프레임 디버거 허용 규칙(멈춤 패스 앞 · 안 · 뒤), 가림 타일 통계, 그래프 그림 배치(노드 수 = 패스 수, 열 = 레벨). 에디터 시나리오 `editor.renderdebugger`: 창 열기 → 그래프 캡처 노드 수 = 패스 수 → 프레임 디버거 켜고 세 번 넘김(감시 값 멈춤 드로우 번호) → 버퍼 탭 목록 → 보기 모드 전환. 골든 사진은 새로 만들지 않는다(통합이 기록).

**측정.** 꺼진 상태의 비용은 드로우마다 원자 값 하나다 — 통합에서 `bench.json` p50 이 흔들림 안인지 한 번 본다.

**구현에서 바꾼 것(2026-11-20).** ① 렌더 타깃 창의 "패스 경계 시점" 은 고른 타깃을 쓰는 패스 끝을 멈춤 지점(패스, 모든 드로우)으로 거는 콤보다(`RenderTargetViewModel::setStopAfterPass`) — 프레임 디버거와 같은 우편함이라 렌더 디버거 창에서 고른 드로우도 같은 줄에 보인다. 지금 풀의 첨부는 모두 밉 하나라 밉 고르기는 밉이 여럿인 타깃에만 보인다(HZB 밉은 구조 버퍼라 버퍼 탭의 행과 가림 탭의 16 px 타일 격자로 본다). ② 버퍼 칸 해석은 사람이 고르는 4 바이트 칸(float · uint · int · hex) × 등록 보폭이다 — 그래프 밖 버퍼의 원소 형식은 표에 없고(크기만), 그 버퍼를 읽는 컴퓨트 PSO 의 리플렉션(`ShaderBindingSlot::_listElementMember`)을 버퍼 이름에 잇는 표가 아직 없다. 드로우 바인딩 글에는 리플렉션 멤버가 그대로 나온다. ③ 텍스처 스트리밍은 텍스처마다의 표 대신 `TextureCache::updateStreaming` 이 이미 계산하던 요청 밉 · 올라온 밉을 합한 카운터 셋(`RT.TextureStreaming.textures` · `belowWanted` · `missingMips`)이다. ④ 삼각형 수는 GPU 컬링 전 상한(배치 인덱스 / 3 × 가시 칸)이다 — 간접 드로우의 실제 수는 GPU 에만 있다. ⑤ 드로우 기록은 `drawBatchGroup`(씬 메시 묶음)만이다 — 입자 · 전체 화면 · 컴퓨트는 기록하지도 자르지도 않는다.

### Android IME 조합(설계 2026-11-20, 감사 P-4)

**문제.** Android 는 미확정(조합 중) 글을 받지 못한다. 앱은 NativeActivity 라 Java `InputConnection` 이 없고, 글은 키 이벤트의 `getUnicodeChar`(`AndroidWindow.cpp`)로만 들어온다 — 한글 · 일본어 · 중국어 입력기는 조합을 `InputConnection` 으로 보내므로 아무것도 오지 않는다. 받는 쪽은 이미 있다: `CallbackInputWindow::queueTextInput( WindowTextInputKind::Composition | Commit, 글 )`(웹이 쓰는 길).

**대조.** 언리얼 5.3+ 와 유니티 2023 은 GameActivity + GameTextInput(AGDK — 편집기 글 전체 · 선택 · 조합 구간을 JNI 로 넘긴다)을 쓴다. Godot 은 자체 Java 편집 뷰(`GodotEditText`)다. 둘 다 Java `InputConnection` 의 호출을 엔진의 조합 · 확정으로 옮긴다.

**결정.**
1. **옮기는 규칙은 C++ 한 곳.** `AndroidInputConnectionState`(Engine/Window/Android, 플랫폼 헤더 없는 순수 코드): `setComposingText( 글 )` → 조합(글), `finishComposingText()` → 조합 중이면 확정(그 글) + 조합(빈 글), `commitText( 글 )` → 조합(빈 글, 조합 중이었으면) + 확정(글), `deleteSurroundingText( 앞, 뒤 )` → 백스페이스 · Delete 키 그 수만큼(상한 64 — 명세대로 조합 구간은 빼고 지우므로 조합은 그대로). 결과는 (종류, 글) · 키 목록이라 데스크톱 단위 테스트가 Android 없이 본다. 편집기 글은 엔진이 갖고(UI 글 칸) Java 쪽은 빈 편집기로 둔다 — `getTextBeforeCursor` 는 빈 글(언리얼 GameTextInput 도 매 확정 뒤 상태를 비운다).
2. **Java 는 얇게.** `SWInputView`(View + `onCreateInputConnection` 이 `BaseInputConnection` 파생을 돌려준다)를 NativeActivity 의 내용 뷰 위에 둔다 — 호출마다 JNI 로 C++ 에 넘기고 C++ 는 ALooper 파이프로 게임 스레드에 옮겨 `queueTextInput` 한다(창 스레드 계약 그대로). 소프트 키보드는 `requestTextInput` 이 그 뷰에 포커스 + `InputMethodManager.showSoftInput`. GameActivity 로 바꾸기는 하지 않는다(액티비티 수명 · 글루 전체가 바뀐다 — 크기 L, 얻는 것은 이 기능 하나).
3. **묶기.** `android-apk` 에 Java 컴파일 · d8(dex) 단계를 더한다(Tools/JDK 의 javac, Android 빌드 도구의 d8) — 지금 APK 는 dex 가 없다. 매니페스트의 `android:hasCode="true"`.
4. **차례.** ① `AndroidInputConnectionState` + 단위 테스트(빌드 없이 쓸 수 있다) ② Java 뷰 · JNI · 파이프 ③ `android-apk` 의 dex 단계 ④ 실기기.
5. **확인.** 단위: `WindowTextInputTest` 에 Android 조합 케이스(한글 "ㅎ" → "하" → "한" 조합 뒤 확정, 일본어 변환 확정, 조합 중 지우기). 실기기: 한국어 키보드로 UI 글 칸에 "한글" 입력, 조합 중 밑줄 표시.

**측정.** 없다(정확성).

### 게임 중 카메라 프리셋 전환(설계 2026-11-20)

**문제.** 프리셋 정의 · 블렌드 표 · 블렌드 스택(블렌드 중 다시 켜기) · 순환 키는 있었지만, 조준 · 탈것 · 실내 같은 상황에 따라 프리셋을 바꾸는 규칙이 없어 게임 코드가 `activatePreset` 을 직접 불러야 했고, 순환으로 고른 시점은 다시 기동하면 사라졌다. 게임 중 전환을 값으로 확인하는 시나리오도 없었다.

**대조.** 언리얼 Lyra 는 `ULyraCameraModeStack` 에 능력이 카메라 모드를 밀어 넣고(조준) 위 층이 블렌드 가중치로 섞이며, `APlayerCameraManager::SetViewTargetWithBlend` 가 뷰 타깃 사이를 섞는다. Cinemachine 은 가상 카메라 우선순위 · Custom Blends 표 · State-Driven Camera(상태 → 가상 카메라)다. 우리 것: 정의 · 적용 · 블렌드 표(Custom Blends 순서) · 중첩 블렌드 스택 · 입력 순환은 있었다. 빠진 것은 상태 기반 전환과 선택 저장.

**결정.** 카탈로그 `<Context name priority action><Map from to/></Context>` — 켜진 문맥 중 지금 **선택 프리셋**의 줄이 있는 가장 높은 우선순위가 보일 프리셋이다(같으면 먼저 적은 것). 문맥은 게임 코드가 `CameraDirectorComponent::setContext` 로, 또는 데이터 `action` 을 누르는 동안 켠다. 보일 프리셋이 바뀔 때만 켜서 코드가 직접 켠 프리셋(킬캠 · `camera.preset`)과 다투지 않고, 덮어쓴 시점(탑승) · 포토 모드 동안은 풀지 않는다. 블렌드는 기존 블렌드 표를 그대로 쓴다. 순환은 문맥 프리셋을 건너뛰고, 고른 시점은 사용자 설정 `gameplay.cameraPreset`(`gv_cameraPreset`)에 쓴다 — 순환 액션이 있는 디렉터(플레이어 시점)만 다음 시작에 그 시점에서 시작한다(세이브가 아니라 사용자 설정: 어느 세이브를 불러도 사람이 고른 시점). 탈것은 지금처럼 `setViewOverride`(좌석이 시점을 준다)로 두고, 탈것마다 프리셋을 고르는 게임은 문맥 `Vehicle` 을 쓴다. 컷씬 카메라 블렌드(시퀀서)는 손대지 않았다.

**확인.** 단위 `CameraContextTest`(우선순위 · 선택 프리셋 줄 · `*` · 없는 프리셋 오류). 시나리오 `game.shooter3d.camerapresets`: 순환 블렌드 중간 프레임의 시야각 · 대상 거리가 두 값 사이, 끝에서 thirdperson 값, Aim 문맥 블렌드, 돌아가는 도중 다시 조준(튀지 않음), 장면을 다시 열면 고른 시점 유지 — 감시 값 `CameraDirector.*`.

### 자막(설계 2026-11-20)

**문제.** 자막 서비스(`UISubtitleService`)와 음성 이벤트의 자막 키는 있었지만 동시 줄 수 · 최소 표시 시간 · 읽기 속도 · 크기 배율이 코드 상수였고, 음성이 낸 줄은 음성 길이와 상관없이 내려갔으며, `ISubtitleSink` 는 올리기만 있어 시퀀서 자막 구간을 나가거나 대화를 건너뛸 때 줄을 내릴 수 없었다. 언어 설정의 빈 값("게임 기본")은 핸들러가 건너뛰어 돌아오지 않았다. 게임에서 확인하는 시나리오가 없었다.

**대조.** 언리얼 `FSubtitleManager`(`USubtitleManager`)는 활성 사운드가 시작할 때 큐를 넣고 소리 길이만큼 보이며 `KillSubtitles` 로 내린다. Dialogue Wave 가 화자 · 자막을 갖는다. 유니티 · Godot 은 내장 자막이 없어 게임이 UI 로 만든다(타임라인 신호 · 오디오 길이). Xbox Accessibility Guidelines 104: 자막 켜기, 크기 고르기, 배경 불투명도, 화자 이름 표시, 한 번에 2 ~ 3 줄, 읽을 시간 보장, 화면 아래 가운데.

**결정.** 표시 규칙은 `EngineConfig::_subtitles`(`SubtitleConfig` — `_maxVisibleLineCount` 2 · `_minDisplaySeconds` 2 · `_readingSecondsPerCharacter` 0.06 · `_arrSizeScale[3]` 0.85 · 1 · 1.3), 없거나 0 이면 기동 오류. 자막 문서에 그 줄 수만큼 `Line<n>` 이 없으면 오류. 보이는 시간은 max( 읽기 시간, 음성 길이 ) — 음성이 낸 줄은 그 음성이 나는 동안 남는다(`refreshVoiceState`). `ISubtitleSink` 는 `postSubtitle` 이 줄 번호를 돌려주고 `removeSubtitle` · `clearSubtitles` 를 갖는다 — 시퀀서 자막 트랙(다른 갈래)이 구간을 나갈 때 쓴다. 사용자 설정은 기존 `accessibility.subtitles` · `subtitleSize` · `subtitleBackground` 그대로(옵션 메뉴에 이미 있다). 화자 색은 스타일 변수(`subtitles.uistyle.xml` 의 `speaker`)로 둔다. 긴 언어는 문서의 최대 폭(1000)에서 접힌다. 언어 빈 값은 기동 때 고른 언어로 돌아간다(`LocalizationManager::getStartupLanguage`).

**확인.** 단위 `UIScreenServicesTest`(설정 검사 · 줄 번호로 내리기 · 차례). 시나리오 `game.playground.subtitles`: 개발 명령 `audio.post` 로 대사를 내면 뜨고 화자가 맞고, 둘이 쌓이고, 크기 설정이 글 크기에 곱해지고, 읽기 시간 뒤 내려가고, 끄면 화면이 열리지 않고, qps-ploc 에서 접히고 넘치지 않는다(감시 값 `UI.Subtitle*`, 사진 한 장 — 골든은 통합이 첫 실행에서 기록).
### 시퀀서 재생 · 컷씬 · 카메라(설계 2026-11-20)

**문제.** 재생기(`SequencePlayer`)는 정방향 1 배속 · 반복 켜기/끄기(`bool`)뿐이다. 시간은 `float32` 초를 더해 쌓아(누적 오차로 긴 시퀀스 · 반복에서 프레임이 어긋날 수 있다) 프레임으로 잘랐고, 역재생 · 재생 속도 · 왕복 · 재생 범위 · 끝으로 건너뛰기 · 끝 알림이 없다. 컷씬 중 플레이어 입력 · HUD 를 그대로 두고, 건너뛰기 입력이 없다. 컷과 컷 사이는 프리셋 블렌드로만 섞인다.

**대조(외부 API 는 context7 도구가 이 세션에 없어 기억으로 적었다 — UE 5.x `ULevelSequencePlayer` / `UMovieSceneSequencePlayer`, Unity 2022+ `PlayableDirector`).**
- 언리얼: `SetPlayRate`(음수 = 역방향), `PlayReverse`, `PlayLooping(Count)`, `FMovieSceneSequencePlaybackSettings`(`LoopCount` · `bPingPongPlayback` · `bDisableMovementInput` · `bDisableLookAtInput` · `bHidePlayer` · `bHideHud` · `bPauseAtEnd`), `SetFrameRange`(재생 범위), `JumpToMarkedFrame` · `GoToEndAndStop`(점프 — 지난 이벤트를 쏘지 않고 끝 상태를 적용, `OnFinished`), 시간은 `FFrameTime`(정수 프레임 + 서브프레임, 틱 해상도 24000). 이벤트 트랙은 방향마다 쏠지(`bFireEventsWhenForwards` · `bFireEventsWhenBackwards`, 기본 둘 다 참), 한 갱신에 여러 바퀴를 넘으면 지난 바퀴 끝 구간과 새 바퀴 앞 구간만 평가(통째로 지난 바퀴는 평가하지 않는다). Camera Cut 섹션은 `bCanBlend` + 섹션 Ease In 으로 컷 사이 블렌드, 시퀀스 Blend In/Out 은 게임 카메라와. CineCamera 는 Focal Length 키, Rig Rail · Crane 은 별도 액터.
- 유니티: `PlayableDirector.time`(double 초) · `extrapolationMode`/`wrapMode`(None · Hold · Loop — 왕복 없음) · `playableGraph.GetRootPlayable(0).SetSpeed`(속도, 음수 비공식) · `stopped` 이벤트. Signal 은 정방향만(역재생 미지원), Cinemachine Track 클립 겹침이 블렌드. 입력 막기 · HUD 는 게임 코드 몫.

**결정.**
1. **시간은 정수 틱.** `int64` 위치, 프레임당 `kSequenceTicksPerFrame = 960`(2 · 3 · 4 · 5 · 6 · 8 로 나뉨). 갱신마다 `dt × fps × |속도| × 960` 을 반올림해 더한다 — 위치가 정수라 반복 · 긴 재생에서 프레임이 어긋나지 않는다(언리얼 `FFrameTime` 과 같은 생각, 해상도만 작다).
2. **속도 · 방향.** `setPlayRate(rate)` — 부호가 방향, 0 · 유한하지 않은 값은 오류 로그 + 거절(`[[nodiscard]] bool`). `playReverse()` = 속도를 음수로 두고 범위 끝에서 시작. 컴포넌트 PROPERTY `_playRate`(데이터가 0 이면 시작에서 오류로 틀지 않는다).
3. **반복 방식** `SequenceLoopMode { None, Loop, PingPong }` 이 `bool` 반복을 대신한다(씬 데이터 `_bLoop` → `_loopMode` 이전). 왕복은 방향 표지를 뒤집는다(속도 값은 그대로 — 언리얼 `bPingPongPlayback` 과 같다).
4. **재생 범위** `setPlaybackRange(start, end)` — 에셋 `[_frameMin, _frameMax]` 안, `start < end` 가 아니면 오류 + 거절. 에셋을 바꾸면 전체 범위로 돌아간다(언리얼 `SetFrameRange`).
5. **끝으로 건너뛰기** `skipToEnd()` — 진행 방향의 끝 프레임으로 **점프**(지난 이벤트를 쏘지 않는다 — 언리얼 `GoToEndAndStop` 과 같다), 그 프레임 상태를 적용하고 정상 끝(`Finished`)으로 끝낸다. 덮던 컷이 있으면 시네마틱 끝(Blend Out)이 나가 게임 카메라로 돌아온다.
6. **끝 알림** `registerFinished` — 정상 끝 · 건너뛰기에서 한 번(멈춤 `stop` 은 아니다 — 언리얼 `OnFinished` · `OnStop` 나눔).
7. **발화 규칙(이벤트 · 카메라 신호).** 갱신 하나는 구간(`SequencePlaybackSegment { from, to }`) 많아야 둘이다. 정방향 구간은 `from < 시작 <= to`, 역방향은 `to <= 시작 < from` 인 항목을 쏜다 — 역재생도 쏜다(언리얼 기본). 반복 되감기: (이전, 끝] + (시작 − 1, 지금] (역방향은 대칭). 왕복 꺾임: (이전, 끝] + (끝, 지금] 역방향 — 꺾이는 끝 프레임은 한 번. 한 갱신에 여러 바퀴를 넘으면 첫 바퀴 끝 구간과 마지막 바퀴 앞 구간만(언리얼과 같다). 점프(`seekToFrame` · `playFromFrame` · `skipToEnd`)는 쏘지 않는다. 같은 문턱을 한 번 지나면 한 번 — 단위 테스트가 정방향 · 역방향 · 큰 dt · 반복 경계 · 왕복 꺾임을 센다.
8. **구간(애니메이션 · 소리 · 자막 · 스폰)은 상태다.** 지금 프레임이 구간 안이면 들어가 있고 밖이면 나가 있다 — 들어가기 · 나가기는 바뀔 때 한 번. 한 갱신에 통째로 지나친 구간은 들어가지 않는다(언리얼 섹션 평가와 같다 — 이벤트는 "지난 것", 구간은 "지금 있는 것"). 자막 길이는 구간 길이 ÷ |속도|.
9. **카메라.** 트랜스폼 키 트랙(위치 · 회전) · 렌즈 키 트랙(화각 · 초점 · 조리개)은 이미 컷 카메라를 몬다(`LensTrackKeysTheCameraAndRestores`). 빈 것: 컷 사이 블렌드 — 카메라 컷 항목의 구간 섞임 `_easeInFrames` · 곡선(앞 컷 화면에서 이 컷으로 섞는 길이, 0 은 끊음 — 언리얼 Camera Cut 섹션 Ease In, Cinemachine 클립 겹침, 결정 12). 컷 카메라에 디렉터가 없으면 끊는다(지금 규칙). 시퀀스 Blend In/Out 은 그대로. 화각은 감시 값 `Object.Property`(`CameraComponent._fovY`)로 읽어 새 감시 값을 두지 않는다.
10. **컷씬 옵션(플레이어 컴포넌트 PROPERTY).** `_bDisablePlayerInput`(언리얼 `bDisableMovementInput` + `bDisableLookAtInput`) — 입력 관리자의 셈하는 막기 요청(`InputManager::addPlayerInputBlockRequest`), 플레이어 컨트롤러는 막힌 동안 의도 0 · 시선 없음. `_bHideHud`(`bHideHud`) — 레이어 숨김 창구(`IUILayerVisibility`)의 HUD 요청. `_bSkippable` — 엔진 컷씬 입력 맵(`EngineDefaultAssets::_cutsceneInputMap` = `engine/input/cutscene.input.xml`, 액션 `Cutscene.Skip`)을 재생 동안 읽어 누르면 `skipToEnd`. 포토 모드 입력 맵과 같은 꼴(게임마다 맵에 액션을 더하지 않는다, 키는 데이터). 맵이 없거나 액션이 없으면 오류 로그 + 건너뛰기 없음(폴백 없음). 셋 다 재생 시작에 걸고 끝내기(`endPlayback`)가 같은 수로 푼다. 움직임 정책은 열거 `SequencePlayerMovement { Free, Blocked, Confined }` 로 넓혔다 — `Confined` 는 대상 · 중심 · 반지름 칸(비면 오류로 틀지 않음)으로 대상을 바닥 평면 원 안에 두고 넘으면 경계로 되돌린다(원 포함 검사로 둔다 — 겹침 계약은 들고 남만 알려 되돌릴 경계 점을 주지 않는다. 언리얼 · 유니티는 이 칸이 없고 게임 코드가 막는다). 빨리감기(`Cutscene.FastForward` 를 누르는 동안 EngineConfig `_sequencer._fastForwardRate` 배) — 이벤트는 모두 쏘고, 자막은 벽시계 길이, 소리는 음 높이를 두고 구간 끝에서 멈춘다(늘이기 없음, 언리얼과 같다). 일시정지는 게임 시간(`GameTimeScale`)이 원천이다 — 재생 갱신 시간이 0 이면 위치가 서고 컷씬 입력도 쉰다(일시정지 메뉴 화면은 런타임 UI 갈래 몫). 정상 끝은 위치를 끝에 둔다(Stop 만 되감는다).
11. **자막 지우기는 창구가 모자란다.** `ISubtitleSink` 는 `postSubtitle` 뿐이라 역재생 · 건너뛰기 · 멈춤에서 남은 줄을 지울 수 없다 — 줄 id 를 돌려주는 `post` 와 `clearSubtitle(id)` 가 필요하다(표시 장치 `UISubtitleService` 는 다른 갈래 몫이라 이번에 고치지 않았다).
12. **이징 · 보간.** 키 보간은 `FloatCurve` 의 Constant · Linear · Cubic(들어오는 · 나가는 접선, 자동 · 사용자)에 `Eased` 하나를 더했다 — 다음 키까지 `BlendCurve` 표(카메라 · UI · 애니메이션 블렌드와 같은 곡선)로 섞는다(언리얼 `ERichCurveInterpMode` + 섹션 Easing, 이징 표를 시퀀서에 따로 두지 않는다). `BlendCurve` 는 기반 층(`Engine/Curve`)으로 옮겼다(커브 층이 애니메이션 층을 읽을 수 없다). 자동 접선은 넘치지 않게 바꿨다(극값 키는 평평, 기울기는 양옆 구간의 3 배 이하 — 언리얼 Smart Auto · 유니티 Clamped Auto). 넘치는 이징(Back · Elastic · Bounce)은 언리얼 내장 이징에도 없고 블렌드 가중치 계약("단조 · 0→0 · 1→1")을 깨므로 들이지 않는다. 가중 접선(언리얼 Weighted Tangent)은 키 칸 둘(길이)이 더 들어 다음으로 미룬다. 구간 섞임은 항목 칸 `_easeInFrames` · `_easeOutFrames` · 곡선 둘 — 애니메이션 세기와 카메라 컷 사이 섞임이 쓴다(역방향으로 들어오면 꼬리). 자막 · 소리는 창구(`ISubtitleSink` · `ISoundPlayer`)에 세기 칸이 없어 아직 섞지 않는다. 시퀀스 파일에 키가 든 것이 지금 없어(검색) 옛 파일 업그레이드는 필요 없다 — 리플렉션이 모든 칸을 쓰므로 새 칸(`_easing`)만 기본값으로 읽힌다.
13. **이번에 하지 않음.** CineCamera Rig Rail · Crane(레일 위 위치 0..1 키 트랙 + 크레인 팔 길이 · 각 키 트랙 — 리그 컴포넌트가 카메라를 자식으로 몬다), 표식 프레임(`JumpToMarkedFrame` — 에셋에 표식 표가 먼저), 반복 횟수(`LoopCount`).

**확인.** 단위: `TestSequencePlayback`(틱 · 속도 · 범위 · 왕복 · 건너뛰기 · 발화 수). 시나리오: `game.playground.cinematicplayback`(역재생 컷 순서 · 속도 2 배 · 0.5 배 끝 프레임 · 왕복 · 범위 · 건너뛰기 뒤 게임 카메라), `game.playground.cinematiccamera`(키 사이 위치 · 화각 보간), `game.wildwest.arrivalskip`(입력 막힘 · HUD 숨김 · 건너뛰기 · 조작 복귀), `game.playground.cutscenetrigger`(물리 트리거 진입으로 한 번 · 다시 재생, 막기 · 범위 제한 · 빨리감기 · 재생 중 일시정지 메뉴와 옵션 — UI 맵이라 컷씬 레이어가 막지 않는다 — · 건너뛰기, Empty 는 이 때문에 `_bUIPauseMenu` 를 켰다), `editor.sequencerplayback`(재생 · 일시정지 · 역재생 · 스크럽).

### 공유 DDC(설계 2026-11-17, NEXT 5 에셋 GUID 3 단계)

**문제.** `DerivedDataCache`(`Engine/Asset/Cook/CookRecord.h:59`)는 로컬 폴더 하나(`Saved/DDC/<키 앞 두 글자>/<키>`)이고 키는 `uint64`(FNV-1a)다. 쿠킹(씬 · 팩 · 내비)만 쓴다. 임포트(텍스처 · 모델 · 하이트필드)는 폴더별 `import.stamp` 로 판정해 무시되는 산출물 폴더에 바로 쓴다 — 새 체크아웃 · 워크트리마다 전부 다시 임포트한다(`Import.*` 시한 3600 초).

**대조.** 언리얼 DDC 는 계층이다(Local → Shared 파일 시스템 → Cloud/Zen, `DerivedDataBackendGraph`, 공유 경로는 환경 변수 `UE-SharedDataCachePath`), 키는 버킷 + 160 비트 해시이고 공유 히트는 로컬에 채운다. 유니티 Accelerator 는 캐시 서버(키 = GUID + 임포터 판 + 설정 + 원본 해시)다. Godot 은 `.godot/imported` 로컬뿐이다. → 언리얼의 파일 시스템 계층.

**결정.**
1. **계층은 같은 클래스의 폴더 둘이다.** `DerivedDataCache` 가 층 목록(`_rootPath` · `_bWritable`)을 받아 앞에서부터 찾고, 뒤 층에서 찾으면 앞의 쓰기 가능 층에 채운다. 가상 인터페이스는 폴더가 아닌 백엔드(HTTP · 버킷)가 생길 때 낸다 — 호스팅이 정해지지 않았다.
2. **키 폭은 그대로 두고 충돌을 미스로 만든다.** 공유 층은 팀 · 수년 누적(10⁸ 항목)이면 64 비트 생일 충돌이 3×10⁻⁴ 쯤이고, 충돌은 조용히 틀린 산출물이다. 항목 머리에 키 서술(쿠커 이름 · 코드 지문 · 대상 · 설정 해시 · 입력 id 와 내용 해시)과 출력 해시를 적고, 찾을 때 서술이 글자 그대로 같고 본문 해시가 맞을 때만 히트다(다르면 미스 + 경고). 128 비트 해시로 넓히기는 기각 — 검증된 해시 의존을 새로 들이고 기록 형식이 전부 바뀐다.
3. **공유 층은 신뢰 경계다.** 다른 머신이 쓴 바이트(셰이더 바이트코드 포함)이므로 본문 해시 검사는 필수이고, 쓰기는 설정이 허락한 머신만 한다(파일 시스템 권한은 운영 몫).
4. **설정.** `Config/Dev/DerivedData.json` 의 층 목록 — 로컬 층(경로 `Saved/DDC`, 쓰기 가능)과 공유 층(경로는 환경 변수 `SW_SHARED_DDC`, 읽기 전용이 기본). 공유 경로는 머신마다 달라 환경 변수로 받고, 변수가 없으면 그 층은 없는 것이다(로그 `DDC.SharedLayerOff` 한 줄 — 층은 선택 항목이지 필수 값이 아니다). 설정 파일이 없으면 오류다.
5. **임포트를 DDC 에 태운다.** `AssetImportStampUtil` 이 어긋난 원본을 임포트하기 전에 DDC 를 찾고, 미스면 임포트한 뒤 넣는다 — 한 자리(임포터 3 종은 그대로). 키 = 임포터 이름 + 코드 지문(`CookCodeFingerprint`) + 대상 + 설정 글(폴더 규칙 + 서술의 예외) + 원본 내용 해시. **GUID 와 경로는 키에 넣지 않는다**(같은 원본 · 같은 설정이면 복제본 · 옮긴 파일도 같은 산출물 — 유니티는 GUID 를 넣어 복제본을 다시 임포트한다). 산출물 글에 자기 경로가 박히는 종류(모델의 `_shared/` · 머티리얼 경로)만 리소스 id 를 키에 넣는다. 출력이 여러 파일인 임포트는 (이름, 바이트)를 이름 순으로 이은 한 덩어리다. 산출물 폴더와 `import.stamp` 는 그대로다(느슨한 트리 읽기 · "폴더가 최신인가" 판정).

**측정.** 재는 법: 새 워크트리에서 `ctest -L import` 를 로컬 DDC 빈 것 · 찬 것 · 공유만 찬 것으로 한 번씩. 판정: 로컬 히트가 전체 임포트의 10 % 이하 시간이면 둔다. 종류별로 공유 히트 시간이 임포트 시간의 1/2 을 넘으면 그 종류는 공유에 쓰지 않는다(작은 텍스처는 왕복이 더 느릴 수 있다).

### 원본 저장소 떼기와 `_source`(설계 2026-11-17, NEXT 5 에셋 GUID 3 단계)

**문제.** 원본은 저장소에 있다 — Content 추적 169 MB(골든 ppm 36 · ogg 32 · glb 26 · jpg 20 · hdr 19 · vrm 16 · png 11 MB), LFS 는 출처 폴더 둘(polyhaven · quaternius, 186 파일)뿐이다. `AssetMeta::_source` 는 곁의 파일 이름이다(`AssetDatabase.h:79`).

**대조.** 언리얼은 임포트 결과를 `.uasset` 에 넣고 원본은 저장소 밖을 가리킨다(`AssetImportData` 의 경로 — 원본이 없어도 에셋은 열린다). 유니티 · Godot 은 원본이 프로젝트 안이고 서술(`.meta` · `.import`)이 곁에 있다(Git LFS · Plastic). → 지금은 유니티 꼴이고, 언리얼의 "원본 없이도 돈다" 는 공유 DDC 가 준다.

**결정.**
1. **지금 규모에서는 떼지 않는다.** 문턱: 원본 합계 2 GB 또는 LFS 용량 · 대역 비용이 문제가 될 때. 그 전에 할 것은 원본 확장자 전체를 LFS 로 넓히는 것 하나다(`.gitattributes` 를 출처 폴더 줄에서 `Content/**/*_raw/**` 종류 줄로) — **사용자 결정**(호스팅 용량 · 비용). 이력은 다시 쓰지 않는다(새 커밋부터).
2. **뗄 때의 꼴.** `_source` 는 두 꼴만 받는다: 상대 경로(지금) 또는 `store:<SHA-256>/<파일 이름>`(내용 주소 — Git LFS 의 oid 와 같은 해시라 LFS 서버를 그대로 원본 저장소로 쓸 수 있다). 읽는 창구는 하나(`AssetSourceLocator::resolve` → 로컬 `Saved/SourceStore/<해시>`, 없으면 가져오기 도구)이고, 못 가져오면 임포트 오류다. 주소가 곧 원본 내용 해시라 **원본을 받지 않고도 DDC 키를 구한다** — 원본을 만지지 않는 사람은 공유 DDC 의 산출물만 받는다.
3. 기각 — 서브모듈(원본을 고칠 때마다 두 저장소 커밋, 부분 받기 없음). 기각 — 서술에 절대 경로 · URL(머신마다 다르다).

**판정.** S2 · S3 는 문턱에 닿을 때 함께 한다(지금 만들면 쓰는 곳이 없다). S1 은 사용자 결정이다.

### 에셋별 임포트 예외 칸(설계 2026-11-17, NEXT 5 에셋 GUID 3 단계)

**문제.** 한 파일의 예외가 폴더 규칙 파일에 경로 패턴으로 박혀 있다 — `Config/Editor/ModelImportConfig.json` 의 `KayKit_SkeletonMinionLOD`(한 파일을 가리키는 `includePatterns` + 앞 규칙의 `excludePatterns` 에 같은 파일), `*/wildwest/models_raw/horse.*`, `brick_wall*` · `crate_medium*` · `barrel*`. 파일을 옮기거나 이름을 바꾸면 패턴이 조용히 빗나가 기본 규칙으로 임포트된다 — 경로가 정체성이던 때의 잔재다.

**대조.** 유니티 `.meta` 의 임포터 블록(에셋별 값, 폴더 기본은 Preset), Godot `.import` 의 `[params]`, 언리얼 `UAssetImportData`(에셋 안). 셋 다 예외는 에셋 서술에 있다.

**결정.** `AssetMeta` 에 **차이만** 적는다 — 기본값은 폴더 규칙이 준다(데이터 상속과 같은 생각: 규칙이 부모, 서술이 차이).

```cpp
REFLECT() struct SW_API AssetImportOverride { REFLECT_BODY();
    PROPERTY( Required, InheritKey ) string _key;    // 규칙 파일과 같은 칸 이름("animationCodec" · "generateMips")
    PROPERTY( Required )             string _value; };
// AssetMeta 에 한 칸: PROPERTY() vector<AssetImportOverride> _listImportOverride;   (비면 쓰지 않는다)
```
- Engine 은 임포터 설정 형식(Editor)을 모르므로 칸은 글 쌍이고, **검증은 임포터가 한다**: 규칙 사슬(`inherits`)을 푼 뒤 서술의 예외를 마지막 층으로 같은 파서에 넣는다 — 모르는 칸 · 틀린 값은 임포트 오류다. 우선순위는 프리셋 < 규칙 < 서술.
- 규칙 로더는 와일드카드(`*`)가 없는 `includePatterns` 를 로드 오류로 거절한다 — 한 파일 예외가 규칙 파일로 돌아오지 못한다.
- 해석된 최종 설정 글이 DDC 키와 `computeSourceHash` 에 든다(예외를 고치면 그 에셋만 다시 임포트).
- 기각 — 서술에 전체 값(유니티 꼴): 규칙을 고쳐도 이미 있는 에셋에 닿지 않는다. 기각 — 타입 있는 칸(Engine 이 Editor 형식을 알아야 한다).

**M3 에서 정한 것(2026-11-17).** 인스펙터는 오브젝트를 고르지 않았고 고른 에셋이 서술을 가진 원본(`models_raw/` · `textures_raw/`)이면 Import Settings 를 보인다(`InspectorContent::ImportSettings`, 모델 `InspectorImportSettings`). 줄은 규칙 구조체의 리플렉션 칸(규칙 파일 키 이름)마다 해석한 값이고, 예외 칸은 서술의 JSON 값 글 그대로 진한 이름 + Revert 단추, 규칙 칸은 흐린 이름이다(강조색 없음 — app-screens). 고친 값은 임포터와 같은 `resolveRule` 로 먼저 해석해 통과할 때만 서술에 쓴다(폴백 없음). 서술 쓰기는 작업 공간 스택의 데이터 단계(서술 글 전 · 후)이고 씬 dirty 는 아니며, 쓰기 · 되돌리기 모두 `AssetHotReload::applySavedAsset` 으로 그 원본을 다시 임포트한다. 언리얼은 `UAssetImportData` 를 Details 에 보이고 Reimport 를 따로 누르게 하나, 유니티처럼 고치면 바로 적용한다(차이 한 칸이라 비용이 작다). 확인: `editor/importsettings`(고치기 → 되돌리기 → 바이트 같음, Revert) · `editor/uicapture` 의 `ui_import_settings.png`.

### 형제 순서: 분수 색인 키(설계 2026-11-17, 감사 E-6 · 41)

**문제.** `EditorSiblingOrder.cpp:40-52` `renumber` 가 형제 번호를 0 부터 다시 매기며 바뀐 형제마다 전체 XML 스냅샷 → `commitModify` 한다. 루트 8000 에서 한 줄을 옮기면 직렬화 16000 회, 씬 파일 8000 줄이 바뀐다. 번호는 `GameObject::_siblingIndex`(`uint16`, 65535 에서 막힘)이고 에디터 계층 창만 읽는다("틱 · 렌더 순서에는 쓰지 않는다", `GameObject.h:188`).

**대조.** 유니티는 조밀한 `m_RootOrder` 의 병합 충돌 때문에 2022.2 에 루트 순서를 `SceneRoots` 목록으로 옮겼다. Godot 은 `.tscn` 의 노드 순서가 곧 순서다. 언리얼 아웃라이너는 순서를 저장하지 않는다(이름 정렬 + 폴더). 협업 도구(Figma · Linear)는 분수 색인(fractional indexing) — 옮긴 항목 하나의 키만 바꾼다.

**결정.** 오브젝트마다 사이에 틈을 둔 정렬 키 하나(분수 색인)를 든다. 한 줄 옮기기 = 그 오브젝트의 키 하나 = 씬 파일 속성 한 줄.
- 기각 — 부모가 든 자식 id 목록(감사 제안, 유니티 `SceneRoots`): ① 씬 · 프리팹 변형(`_base`)은 칸 단위 차이라 "한 오브젝트의 키" 는 차이로 적히지만 목록 순서는 적히지 않는다. ② 에디터만 읽는 값에 부모마다 힙 목록이 런타임 오브젝트에 든다. ③ 두 사람이 다른 오브젝트를 옮기면 키는 다른 줄이라 병합되고 목록은 같은 구간이라 충돌한다.
- 기각 — 파일 순서(Godot): 변형이 순서를 덮을 길이 없고 씬 쓰기 순서를 전부 바꾼다.

```cpp
// Editor/Common/Commands/Scene/SiblingOrderKey.h — 순수 함수(씬 · ImGui 를 모른다)
struct SiblingOrderKey
{
    static constexpr uint32 kAppendStep = 1u << 12;   ///< 끝에 더할 때의 간격(형제 100 만까지 다시 펴지 않는다)
    /** 앞 · 뒤 키 사이의 키. 끝이면 그쪽을 비운다. 틈이 없으면 없음 — 부르는 쪽이 그 부모의 형제만 `respace` 한다. */
    [[nodiscard]] static optional<uint32> between( optional<uint32> previous, optional<uint32> next );
    /** @p count 개를 고르게 편 키. 틈이 다 찼을 때만 쓴다. */
    static void respace( uint32 count, vector<uint32>& outListKey );
};
```
- 칸은 `uint32 _siblingOrder`(이름을 바꾼다 — 값이 자리가 아니라 정렬 키다. `getSiblingIndex` 를 자리로 쓰는 코드를 컴파일에서 끊는다). 같은 키는 지금처럼 오브젝트 id 순 — 옛 파일의 조밀한 번호와 두 사람이 같은 틈에 넣은 키가 그대로 유효해서 값은 옮길 것이 없고, 씬 · 프리팹 형식 판 올림(`SW_ASSET_FORMAT_UPGRADE`)은 속성 이름만 바꾼다.
- 기록은 스냅샷이 아니라 데이터 단계다: `SW_UNDO_STEP( applySiblingOrder, objectID, key )` 의 전 · 후 한 쌍. 다시 펼 때도 (id, 키) N 쌍이 한 거래다.
- `uint16` → `uint32` 로 오브젝트가 커질 수 있다(지금은 꼬리 패딩). 통합이 `sizeof( GameObject )` 를 전후로 적는다 — 8 바이트까지는 받아들인다(20 만 오브젝트에 1.6 MB).

**판정.** `editor/hierarchyorder` 에 루트 8000 단계: 한 줄 옮기기 뒤 `Editor.UndoCount` +1, 그 거래의 바이트 1 KB 미만, 그 프레임 16 ms 미만(Release), 저장한 씬의 바뀐 줄 1.

### 인스펙터 프로퍼티 단위 기록(설계 2026-11-17, 감사 E-7 · 41)

**문제.** `InspectorPropertyUndo.cpp:23-27` 이 위젯이 활성화되면 `InspectorEditRecorder::beginEdit` 을 부르고, 그것이 선택 전체를 `captureSnapshot` 한다(`InspectorEditRecorder.cpp` `captureSelectionBeforeEdit`). 닫기는 `IsItemDeactivatedAfterEdit` 뿐이라 누르기만 하고 나오면 스냅샷이 세션 끝까지 남는다. 커밋은 대상마다 스냅샷을 한 번 더 찍는다 — 8000 선택에서 숫자 한 칸이 전체 XML 16000 개다.

**대조.** 유니티 `Undo.RecordObject` 는 `PropertyModification`(대상 · `propertyPath` · 값)으로 남고 프리팹 오버라이드와 단위가 같다. Godot `EditorUndoRedoManager.add_do_property` / `add_undo_property`. 언리얼은 `Modify()` 로 객체를 직렬화하되 트랜잭션이 프로퍼티 차이를 낸다. → (대상, 경로, 값).

**결정.** 기록 단위는 (오브젝트 id, 컴포넌트 키, 프로퍼티 경로, 값 글)이다. 컴포넌트 키는 프리팹 오버라이드가 이미 쓰는 `타입#n`(`PrefabOverrides::writesField`), 값 글은 `PropertyGridViewModel::setPropertyText` 가 쓰는 꼴이다.

```cpp
// Editor/Panels/Inspector/Model/PropertyEditStep.h
struct PropertyEditTarget { ObjectID _objectID; string _componentKey; string _propertyPath; };
/** 되돌리기 · 다시 하기가 함께 쓰는 데이터 단계 — 모델의 쓰기 길(값 쓰기 + `onPropertyChanged`)을 그대로 탄다. */
void applyPropertyText( UndoStepContext& context, const PropertyEditTarget& target, const string& valueText );
// 기록: SW_UNDO_STEP( applyPropertyText, target, beforeText ) / ( …, afterText ) 한 쌍, 다중 선택은 한 거래
```
- **찍는 때는 위젯 활성화가 아니라 모델의 첫 쓰기 직전이다.** 쓰기 길은 `setRowValue` · `setRowNumber` · `setPropertyText` 셋뿐이라("명령 한 길") 거기서 열린 편집 구간이 없으면 대상마다 그 프로퍼티의 값 글만 읽어 구간을 연다. 누르기만 한 위젯은 아무것도 만들지 않는다.
- 구간은 위젯이 풀릴 때(`IsItemDeactivated` — 편집 여부와 무관) 닫는다: 전 · 후가 같으면 버리고 다르면 한 거래로 올린다. 위젯이 사라져 닫기를 못 받은 구간은 모델의 다음 `gather` 가 닫는다 — 남는 길이 없다.
- `InspectorEditRecorder` 의 스냅샷 API(`beginEdit` · `finishEdit` · `recordImmediateEdit`)는 지운다 — View 가 스냅샷을 찍을 창구가 없어진다. 미리보기 창 선택도 같은 단계이고 스택은 `UndoStepContext` 가 고른다.
- `타입#n` 은 컴포넌트를 지우면 밀리지만, 스택 안에서는 추가 · 삭제도 단계라 순서대로 되감으면 맞는다. 영속 지역 id(감사 A-2)가 들어오면 키만 바꾼다.
- 스냅샷으로 남는 것: 구조 편집(생성 · 삭제 · 컴포넌트 추가 · 삭제). 한 쓰기가 다른 칸을 고치는 타입(`onPropertyChanged`)이 되돌리기에서 어긋나는지는 추측하지 않고 `editor/undocoverage` 의 전수 단계(프로퍼티마다 편집 → 되돌리기 → 상태 글 해시가 편집 전과 같다)가 가린다 — 지는 프로퍼티만 거래에 오브젝트 스냅샷을 함께 싣는다.
- 기각 — 감사의 임시안(`IsItemDeactivated` 에서 pending 지우기만): 누수는 막지만 선택 전체 직렬화가 남는다.

**P1 에서 정한 것(2026-11-17).**
- 확장 영역(`IInspectorComponent` — 트랜스폼 · 카메라 …, 컴포넌트에 바로 써 모델을 지나지 않는다): 그룹 위젯이 활성화될 때 **그 컴포넌트 하나의 프로퍼티 전부**의 값 글을 찍고, 풀릴 때 바뀐 프로퍼티만 같은 단계로 남긴다. 유니티 `Undo.RecordObject`(대상 하나를 찍고 차이를 `PropertyModification` 으로) · 언리얼 `Modify` + 트랜잭션 차이와 같다. 기각 — 오브젝트 스냅샷 유지(단위가 둘로 갈린다), 확장마다 모델 쓰기 길로 옮기기(회전 도 표시처럼 프로퍼티와 1:1 이 아닌 입력이 있다 — Godot 도 커스텀 에디터는 `emit_changed` 로 프로퍼티 단위만 받는다).
- 구간 표(`PropertyEditSpanSet`)는 그리드 모델이 하나씩 든다 — 인스펙터 창이 둘이어도 서로의 구간을 닫지 않는다. 고아 구간은 "다음 `gather`" 가 아니라 그리드의 프레임 끝(`EditorPropertyGrid::endFrame`)이 닫는다: 한 그리드가 한 프레임에 카드마다 모으므로 모으기로는 다른 카드의 활성 위젯과 가릴 수 없다.
- 구간 하나 = 데이터 단계 한 쌍이고 대상 목록을 인자로 든다(8000 선택이 명령 8000 개가 아니다). 단계 인자에 씬 키를 더했다(미리보기 창 — `ObjectSnapshot` 과 같은 규칙), 스택은 `EditorTransaction::recordObjectCommand` 가 오브젝트의 씬으로 고른다.
- P2 · P3: 구간 키는 위젯이 아니라 **행**(깊이 0 프로퍼티의 행 키)이다 — 쓰기가 그 행의 구간을 열거나 잇고, 그리드가 행을 그리는 사이 활성 위젯이 그려졌으면(ImGui `EndGroup` 과 같은 `ActiveIdIsAlive` 판정) 잇고, 활성이 아닌 프레임의 마감(놓은 프레임의 쓰기까지 담아 그 프레임에 오른다)이 닫는다. 그리기 확장은 되돌리기를 모르고 쓰기 명령만 부른다(추적 호출 9 곳을 지웠다). 위젯 하나 기준이던 P1 은 벡터 축 셋(`drawVec3Control`)처럼 위젯 여럿인 행에서 X · Y 축을 끌면 프레임마다 한 단계가 되었다(그 전 스냅샷 길은 아예 기록하지 않았다). 언리얼 프로퍼티 체인 편집 구간(`NotifyPreChange` ~ `NotifyPostChange`)과 같은 선. 되돌리기에서 빼는 타입(`atomic<bool>` — 다른 스레드가 쓴다)은 모델이 가린다.
- P3 규모: 단계 인자는 (씬 키, 프로퍼티 경로 하나, 대상 목록 — 오브젝트 id · 컴포넌트 지역 id 16 바이트, 값 글 표 + 대상마다 번호)다. 다중 선택은 대개 값이 하나라 8000 선택의 거래가 약 0.3 MB 다(대상마다 글 키 · 경로를 들면 1.3 MB 로 판정을 넘었다). 확인은 `editor/inspectorundoscale`(8000 선택: 누르기만 → 기록 0, 끌기 → 기록 1 · 1 MB 이하, Ctrl+Z 한 번에 씬 해시)와 `editor/undocoverage` 의 프로퍼티 전수(`editor.undoCoverageProperties` — 숫자 · 체크 · 벡터 칸을 인스펙터 모델로 고치고 되돌려 해시를 맞댄다). 전수가 `PropertyMismatch` 로 이름을 내는 프로퍼티만 거래에 오브젝트 스냅샷을 함께 싣는다 — 지금은 그런 칸을 모른다(통합 검증에서 나오면 그때 단다).
- 컨테이너 편집(원소 더하기 · 지우기 · 맵 항목)도 같은 구간을 탄다 — 예전 스냅샷은 주 선택만 찍어 다중 선택의 나머지가 되돌아가지 않았다. 메서드 호출(`invokeMethod`)만 오브젝트 스냅샷으로 남는다(무엇을 바꿀지 모른다).

**판정.** 8000 선택에서 칸을 누르고 나오기 → 기록 0 · 프레임 증가 1 ms 미만. 숫자 칸 끌기 → 구간 열기 프레임 16 ms 미만, 거래 1 MB 미만.

### 에디터 에셋 색인(설계 2026-11-17, 감사 E-8 · 41)

**문제.** `AssetReferenceTracker` 는 `Content/` 변경 번호가 바뀌면 전체를 다시 스캔하고, `EditorReferenceIndex::refreshFile` 은 UI 스레드에서 전체 `removeIf` + 정렬을 하며, 색인은 디스크에 없어 기동마다 전체다. 참조는 "경로처럼 보이는 글" 을 찾는 텍스트 검색이다. 선택 팝업 · Open 은 트리를 동기로 걷는다(`EditorContentScan`).

**2 단계가 바꾸는 것.** `w-guid` 가 런타임 `AssetRegistry`(GUID ↔ 리소스 id, 층 병합 · `record` · `move`)를 넣고 참조를 `guid:<32자>` 로 바꾼다 — 참조 스캔이 정확한 토큰 찾기가 된다. 언리얼 "AssetRegistry" 의 자리는 여기서 둘로 갈린다: 정체성은 런타임 `AssetRegistry`, 목록 · 종류 · 참조 그래프는 에디터 색인. 그래서 이름은 `EditorAssetIndex` 다(같은 이름을 둘에 쓰지 않는다).

**대조.** 언리얼 `IAssetRegistry`(`FAssetData` + `FDependsNode` 양방향 그래프, 디스크 캐시, 백그라운드 `FAssetDataGatherer` + 디렉터리 감시 증분, 선택기는 `FARFilter` 질의). 유니티 `AssetDatabase`(`Library/` 의 DB, 시각 · 해시 증분, `FindAssets("t:…")`). Godot `EditorFileSystem`(`filesystem_cache`, 스레드 스캔).

**결정.** `EditorAssetIndex` 하나가 `EditorReferenceIndex` + `AssetReferenceTracker` 의 스캔 · 색인을 대신하고, 선택 팝업 · Open · Quick Open · 콘텐츠 브라우저 목록 · 참조 찾기 · 핫 리로드 연쇄가 이것만 읽는다.

```cpp
struct EditorAssetEntry
{
    AssetGUID _guid;                 ///< 정체성 없는 파일(골든 등)은 색인에 없다
    uint32    _pathID;               ///< 경로 풀의 번호(같은 폴더 접두어를 나눠 쓴다)
    uint32    _typeID;               ///< `AssetTypeRegistry` 번호
    uint64    _fileStamp;            ///< 크기 · 쓰기 시각 — 같으면 파일을 열지 않는다
    uint32    _firstReference;       ///< 나가는 참조 구간
    uint32    _referenceCount;
};
class EditorAssetIndex
{
public:
    [[nodiscard]] const EditorAssetEntry* find( const AssetGUID& guid ) const;
    void collectReferrers( const AssetGUID& guid, vector<AssetGUID>& outList ) const;    // 역방향 표
    void collectReferences( const AssetGUID& guid, vector<AssetGUID>& outList ) const;
    void collectByType( uint32 typeID, vector<uint32>& outListEntry ) const;             // 선택 팝업
    void applyChanges( const vector<EditorAssetChange>& listChange );                    // 추가 · 삭제 · 바뀜 — 그 파일의 줄만
    [[nodiscard]] expected<void> loadCache( const AbsolutePath& path );                  // `Saved/Editor/AssetIndex.bin`
    [[nodiscard]] expected<void> saveCache( const AbsolutePath& path ) const;
};
```
- 갱신 입구는 하나다: 파일 감시 → 워커가 바뀐 파일만 읽어 변경 목록을 만들고 → UI 스레드가 프레임 경계에서 `applyChanges`. 같은 적용이 런타임 `AssetRegistry::record` · `move` 도 부른다 — 두 색인이 갈리지 않는다.
- 참조는 `AssetReferenceFields`(2 단계가 GUID 를 알게 고친다)의 칸에서 `guid:` 토큰만 뽑는다. 경로 글 추측은 없앤다.
- 기각 — SQLite · LMDB(10 만 줄 × 32 바이트 ≈ 3 MB 는 이진 한 덩어리로 충분하고 의존이 는다). 기각 — 런타임 `AssetRegistry` 에 종류 · 참조를 넣기(에디터만 읽는 값이 Engine 과 팩 색인에 든다).

**측정이 먼저다.** 합성 트리(테스트가 임시 폴더에 작은 XML 1 만 · 10 만 개, 참조 평균 4)로 지금 값을 잰다: 전체 스캔(`ReferenceIndexReady`) ms, 파일 하나 바뀜의 UI 스레드 ms. 판정: 파일 하나 바뀜이 1 ms 를 넘으면 R1 · R2, 10 만 트리 기동 스캔이 2 초를 넘으면 R3(디스크 캐시). 목표: 바뀜 적용 1 ms 미만, 종류 거르기 5 ms 미만, 캐시가 찬 기동 500 ms 미만(워커 — UI 는 막히지 않는다).

### 에디터 첫 패널 · Hierarchy 8000 측정 계획(설계 2026-11-17, NEXT 4)

**문제.** 에디터가 뜬 뒤 첫 패널이 약 1 초 멈춘다 — 패널이 그리기 안에서 동기로 준비한다(후보: `ModulesPanel.cpp:32` 가 draw 에서 부르는 `ModulesViewModel::ensureLoaded`, `EditorViewportOverlays::ensureLoaded`, 참조 색인 첫 스캔, 트리 걷기). 큐브 8000 씬의 계층 · 인스펙터 비용은 잰 값이 없다.

**대조.** 언리얼은 탭을 늦게 만들고(`RegisterNomadTabSpawner`) 무거운 자료는 에셋 레지스트리가 비동기로 채운 뒤 알린다(`OnFilesLoaded`). 유니티는 `EditorWindow.OnEnable` 뒤 `EditorApplication.delayCall`. Godot `EditorFileSystem` 은 스레드 스캔 + 진행 표시. → 패널은 곧바로 그리고 자료는 잡이 채운다. 창구는 이미 있다(`IEditorPanel::onPanelOpened`, `IEditorPanel.h:161`).

**결정.** 재서 문턱을 넘는 패널만 고친다. 고치는 꼴은 하나다: 동기 준비를 `onPanelOpened` 가 띄우는 잡으로 옮기고 Model 이 `isReady()` 를 내며, View 는 준비 전 한 줄만 그린다. 시간 문턱은 시나리오 판정에 넣지 않는다(흔들린다) — `ctest -L load` 처럼 혼자 도는 항목이 값을 남기고 통합이 아래 표를 채운다.

**측정.** 재는 법(첫 패널 · Hierarchy 정지 · 전체 선택 · 이름 거르기)과 문턱은 [NEXT](NEXT.md) 4 절이다.

### 샤드 시간 균형(설계 2026-11-17, NEXT 5 T3)

**문제.** 한 PC 의 `ctest -j6`(`Config/Dev/Verify.json`) 벽시계는 max(가장 긴 항목, 합 ÷ 슬롯)이다. 시나리오 체인은 팩마다 항목 하나라 가장 긴 체인(shooter3d — 백엔드를 차례로 돈다, Linux 872 초)이 하한이다. 단위 테스트 샤드(`SW_UNIT_TEST_SHARDS`, `TestRunner::getShardOf` 이름 해시)는 1 이다. 시간 자료는 이미 쌓인다(`Scripts/dev/test_history.py` — 단계별 `wallMilliseconds`).

**대조.** 언리얼 Horde 와 크로미움 Swarming 은 지난 실행 시간으로 샤드를 나눈다. Bazel `shard_count` 와 GoogleTest `GTEST_TOTAL_SHARDS` 는 순번 · 해시다. 유니티 UTR 은 시간 자료로 파티션을 나눈다. Godot 은 샤드가 없다.

**결정(러너 없이 할 수 있는 몫).**
1. **CTest 의 시작 순서는 할 일이 없다.** ctest 가 지난 실행 시간(`CTestCostData.txt`)으로 긴 항목부터 시작한다.
2. **가장 긴 체인을 시간으로 나눈다 — 한 항목이 전체 벽시계의 40 % 를 넘을 때만.** 체인 등록 함수가 `SHARDS n` 을 받아, 커밋된 시간표 `Config/Dev/ScenarioCosts.json`(시나리오 → 초, 없는 것은 중앙값)을 읽고 긴 것부터 가장 가벼운 샤드에 넣는다. 표는 `test_history.py costs --write` 가 갱신하고 **사람이 커밋할 때만 바뀐다** — 실행마다 다시 나누면 앞 단계가 가리던 결함이 실행마다 드러났다 숨는다.
3. **단위 테스트는 건드리지 않는다.** 합계가 60 초를 넘을 때 `--test-shard` 가 같은 꼴의 시간표로 나눈다(표에 없는 케이스는 지금의 해시).
4. 여러 머신에 나누는 것은 외부 러너가 있어야 한다(**사용자 결정**) — 위 시간표가 그대로 그 입력이다.
- 주의: shooter3d 는 무거운 장면이 함께 돌면 느려져 차례로 돈다(`sequential_scenario_packs`). 둘로 나눠 나란히 돌리면 같은 문제가 반쯤 돌아온다 — 그래서 측정으로 정한다. 그리지 않는 대기(아래 검증 시간 절)가 이 체인을 먼저 줄이므로 그 뒤에도 40 % 를 넘을 때만 한다.

**판정.** 나눈 뒤 Release ctest 벽시계가 10 % 이상 줄지 않으면 되돌린다.

### 리다이렉트 표의 팩 담기(설계 2026-11-17, NEXT 5 R1)

**구조.** `PackReachResult.cpp:456` `addRedirectTableRoots` 가 도메인 뿌리의 `redirects.xml` 을 아무도 가리키지 않아도 담고(`PackReachabilityTest`, `TestPackReachResult.cpp:180`), 런타임은 `PackEngineSettingsSubsystem.cpp:44` 가 `AssetRedirect::loadFromSearchDomains` 로 읽는다(틀린 표는 기동을 멈춘다).

**남은 틈.** 실제 배포 팩으로 본 적이 없다(제외 규칙이 표를 빼면 쿠킹 오류 `PackCooker.RedirectTableExcluded`).

**대조.** 언리얼 리다이렉터는 패키지라 가리키는 쪽이 쿠킹에 끌고 오고 `CoreRedirects` 는 ini 라 늘 실린다. 유니티 · Godot 4 는 GUID · `uid` 라 옮기기 표가 없다.

**결정.** 새 구조를 만들지 않는다 — 옮기기용 표는 에셋 GUID 2 단계가 걷고 정체성 대체용 GUID → GUID 표만 남는다(그 표도 같은 뿌리 담기를 탄다: `addRedirectTableRoots` 는 `AssetRedirect::isTable` 만 묻는다). 그때까지 틈 ① 만 쿠킹 오류로 막는다: 디스크에 도메인 표가 있는데 담을 수 있는 집합에 없으면 `PackCooker.RedirectTableExcluded` 로 쿠킹이 멈춘다. ② 는 `shipping-check` 가 팩 목록에서 도메인마다 표를 확인하는 한 줄이다.

### Mirror · Montage 포즈 노드(설계 2026-11-17, NEXT 5 A1)

**구조.** `AnimationPoseNode.cpp:311-313` 이 `BlendPoses` · `Mirror` · `Montage` 를 `AnimationPoseNodeRegistry` 에 등록하고, 로드(`AnimationGraphAsset::parsePoseNode`) · 생성(`AnimationPoseNodeSet`) · 에디터(`AnimationGraphEdit` 의 `getTypeNames` · `makeDefaultDef` · `isSatisfiedBy`)가 같은 계약(`AnimationPoseNodeType`)을 본다. 테스트는 `AnimationPoseNodeTest` 9 건(`EngineNodesAreRegistered` · `MontageFollowsSectionsAndBranches` · `MirrorSwapsPairedBonesAcrossTheAxis` · `DefaultDefsSatisfyTheirContract` …).

**대조로 본 남은 차이(지금 일감이 아니다).** 언리얼 `UAnimInstance::Montage_Play` 는 그래프 밖에서 게임 코드가 한 번 트는 재생이다(슬롯 노드 `FAnimNode_Slot`, 블렌드 인 · 아웃, `Montage_JumpToSection`). 유니티는 Playables 로 코드가 섞는다. 여기 `Montage` 는 그래프 상태의 포즈 노드이고 분기는 섹션 끝에서 읽는 파라미터라 "지금 이 섹션으로" 가 없다. 첫 요구는 NEXT 4 의 Shooter3D 소총 자세(쥔 자세 클립 + 상체 층)일 가능성이 높다 — 그 일에서 슬롯 노드를 등록표에 한 줄로 더한다(등록표가 그 확장점이다).

### 퍼징 구성과 퍼저 타깃(설계 2026-11-17, NEXT 4 퍼징)

**문제.** `SW_ENABLE_FUZZING`(`BuildOptions.cmake:101`)은 어느 프리셋에도 없고 퍼저 타깃이 하나도 없다(`LLVMFuzzerTestOneInput` 0 건) — 계측 플래그만 있고 지어 본 적이 없다. 타깃을 더하면 `BuildLayout.cmake:83` 의 `LINKER:--exclude-libs,ALL` 이 정적 libFuzzer 의 `__sanitizer_cov_*` 콜백을 실행 파일의 동적 심볼 표에서 가려, 엔진 .so 가 ASan 런타임의 빈 구현에 묶인다 — 돌기는 하는데 커버리지가 0 인 조용한 결함이다(`Fuzzing.cmake` 주석).

**대조.** 크로미움은 `fuzzer_test()` 템플릿으로 타깃마다 실행 파일 + 씨앗 말뭉치를 두고 ClusterFuzz 가 돌리며, 퍼저는 공유 라이브러리 빌드를 피해 정적으로 링크한다. 언리얼 UBT 에는 `bEnableLibFuzzer` 가 있다(기억에 의존). Godot 은 libFuzzer 타깃 대신 외부 API 퍼저를 쓴다(기억에 의존). 유니티는 공개된 것이 없다.

**결정.**
1. **프리셋 `Linux-Fuzz`**(`Linux-Debug-ASan` 을 이어받고 `SW_ENABLE_FUZZING=ON`)와 빌드 프리셋.
2. **`sw_add_fuzz_target(<이름> SOURCES … LINK …)`** — 실행 파일 `Fuzz_<이름>`, `-fsanitize=fuzzer`, 대상 속성 `SW_FUZZ_TARGET`. 심볼 숨김 줄을 `$<$<NOT:$<BOOL:$<TARGET_PROPERTY:SW_FUZZ_TARGET>>>:LINKER:--exclude-libs,ALL>` 로 바꿔 퍼저 실행 파일만 정적 아카이브 심볼을 내보낸다(실행 파일이 심볼 찾기 순서의 맨 앞이라 ASan 의 약한 기본 구현을 이긴다). 기각 — 엔진을 정적으로 링크(크로미움 꼴): 정적 구성은 Shipping 뿐이라 구성 하나를 더 유지해야 한다.
3. **조용한 결함을 테스트로 만든다.** 타깃마다 CTest `Fuzz.<이름>.Smoke`(라벨 `fuzz`) = `Fuzz_<이름> -runs=2000 <씨앗 폴더>` + `PASS_REGULAR_EXPRESSION "Loaded ([2-9]|[1-9][0-9]+) modules"` — 커버리지가 실행 파일에만 닿으면 진다.
4. **엔진 범위.** `Test/Fuzz/FuzzEngineScope.cpp` 의 `LLVMFuzzerInitialize` 가 단위 테스트 실행기(`App --test`)와 같은 최소 범위를 한 번 세운다. 타깃 파일은 `LLVMFuzzerTestOneInput` 하나다.
5. **타깃은 신뢰 경계의 파서만**(`security-and-hardening`): ① `XMLDocument::parse`(모드 · 세이브 글) ② 팩 머리 · 색인(`PackReader` — 받은 패치 · DLC · 모드) ③ 게임 세션 수신 패킷 디코드, 그 뒤 ④ `JSONDocument` ⑤ WebSocket 프레임 · 핸드셰이크 ⑥ 세이브 머리. 첫 셋은 이미지가 다르다(Core · Engine · GameFramework) — 숨김 수정이 세 .so 에 다 닿는지 함께 본다.
6. **말뭉치.** 씨앗은 `Test/Fuzz/Seeds/<이름>/`(작은 것 몇 개), 자라는 말뭉치는 `build/<프리셋>/Saved/Fuzz/<이름>/`(커밋하지 않는다). 찾은 크래시는 바이트를 `Tests/Snapshots/` 에 넣고 단위 테스트 케이스로 남긴다 — 퍼저 없는 구성(Windows 포함)에서 회귀를 본다.
7. **돌리기.** `linux-verify` 의 sanitize 단계 끝에 타깃마다 `-max_total_time=30`(셋이면 1.5 분). 밤새 도는 실행은 러너가 있어야 한다(**사용자 결정**) — 없을 때는 Linux 시나리오 스크립트(`Scripts/dev/`)의 새 하위 명령 `fuzz <이름> --minutes N`.

**측정.** 타깃별 exec/s(100 미만이면 입력마다 범위를 다시 세우는 것 — 고친다), 30 초 뒤 `cov` 가 씨앗 재생 직후보다 늘었는지(안 늘면 타깃이 죽은 것), sanitize 단계 합 5 분 이내.

### 검증 시간: 그리지 않는 대기와 진 단계 다시 돌기(설계 2026-11-17, NEXT 4 검증 시간)

**문제.** shooter3d 체인은 시나리오 28 개에 `<Wait frames>` 140 개 · 5857 프레임이고(slice 1686 · enemydeath 1685 · crowd 545 · bodyviews 512 가 76 %), 백엔드 넷을 차례로 돈다(`Test/Scenario/CMakeLists.txt:115`) — 고정 대기만 2 만 3 천 프레임을 그린다. 큰 대기는 "자동 플레이가 N 프레임 진행한 뒤 사진" 이다(enemydeath: 180 · 300 · 600 · 600 프레임 사이에 사진 5 장). 기다릴 조건이 없는 진행 시간이라 **NEXT 가 적은 "조건 대기로" 는 이 대기에 맞지 않는다** — `Test/README.md` "어디서 시간이 드는가" 가 이미 그렇게 정했다. 체인이 지면 다시 도는 입구도 없다(범인 찾기 `Scripts/dev/scenario_bisect.py` 를 손으로 조립한다).

**대조.** 언리얼은 기능 테스트를 `-nullrhi` 로 돌리고 스크린샷 테스트만 그린다. 유니티는 `-batchmode -nographics` 로 PlayMode 테스트를 돌리고 그림은 Graphics Test Framework 가 따로 본다. Godot 은 `--headless` 와 `--disable-render-loop`(스크립트가 시킬 때만 그린다)가 있다. → 진행과 그리기를 떼고 사진이 필요한 프레임만 그린다.

**결정.**
1. **`<Wait frames="N" render="false"/>`.** 그 프레임은 게임 틱 · 고정 물리 · 에셋 도착 대기를 그대로 돌고 렌더 패킷을 내지 않는다. 그리지 않는 프레임 길은 지금 없다(창 최소화는 `FramePacer` 가 루프를 재울 뿐이다) — 렌더 드라이버 창구(`IEngineRenderDriver`)에 "이 프레임은 내지 않는다" 를 더하고 실행기만 요청한다. 대기의 마지막 프레임들은 그린다(TAA 이력 · 노출 적응 · 그림자 캐시가 사진 전에 자리 잡게) — 그 수는 `Config/Dev/GoldenImage.json` 의 칸이고(없으면 오류) 시나리오가 줄일 수 없다.
2. **"렌더는 게임 상태에 되먹이지 않는다" 를 테스트로 박는다.** 같은 시나리오를 그리며 · 안 그리며 돌려 사진 시점의 `<RecordStateHash>` 가 같아야 한다. 다르면 그 시스템이 가시성 · 렌더 결과를 게임 상태에 쓰는 것이다 — 고치거나 그 시나리오는 그린다.
3. **골든.** 시간 누적 효과가 달라져 문턱 밖으로 나갈 수 있다 — 바꾼 시나리오만 다시 기록하고 dx12 표본을 눈으로 본다(지금 절차). 마지막에 그리는 프레임 수는 "다 그린 사진과의 차이가 골든 문턱 안" 인 가장 작은 값을 재서 정한다(8 · 16 · 32 · 64).
4. **진짜 조건 대기로 바꿀 것**은 바로 뒤에 `<Expect watch>` 가 오는 짧은 대기다. 목록은 도구가 낸다(이분 탐색 스크립트의 새 인자 `--fixed-waits <체인>` — 대기마다 프레임 수와 다음 단계 종류. `<Wait>` 를 없앤 뒤 `--advances` 로 바꿔 `<AdvanceSimulation>` 을 센다). 시간 이득은 작고 흔들림을 줄이는 것이 목적이다. 부정형 단언은 `was already true` 로그로 가려낸다.
5. **다시 도는 입구.** 이분 탐색 스크립트에 `<체인> --rerun-failed` — 마지막 실행의 단계 보고(`build/<프리셋>/Bin/Saved/Automation` 의 `report[.<백엔드>].<단계>.json`, 통과가 아니거나 보고가 없는 단계)를 읽어 진 단계를 그 백엔드로 혼자 돌리고, 통과하면 "체인에서만 진다" 로 적고 앞 단계 전부를 붙여 다시 돌린 뒤 지면 이분 탐색으로 넘어간다. `--from-stage <단계>` 는 그 단계부터 끝까지만 돈다. 명령은 지금처럼 `Scenario.<체인>` 의 ctest 명령에서 가져온다. Linux 시나리오 스크립트가 같은 함수를 부르고, 묶음 검증 · 시나리오 추적 스킬의 "진 테스트만 다시" 가 이 명령을 가리킨다.
- 기각 — 시나리오 전체 `gv_timeScale`(README: 94 단계 중 17 실패). 기각 — 사진 수 줄이기(판정 범위가 준다). 기각 — 백엔드 함께 돌리기(무거운 장면에서 더 느려 차례로 정한 것이다).

**측정이 먼저다.** 지난 Release 로그의 `[Scenario] … timeline:` 줄에서 shooter3d 의 고정 대기 ms 합과 체인 벽시계를 모은다(새로 재지 않는다). 판정: 고정 대기가 체인의 50 % 이상이면 V3 를 짓고, 그린 프레임 대비 안 그린 프레임 비용이 1/3 이하면 V4 로 간다. 아니면 닫고 `stage reset took` 과 기동을 본다.

### 플레이 중 리로드 확인 장치(설계 2026-11-20)

**문제.** 리로드 다섯 길(게임 모듈 · 에디터 모듈 · 에셋 · 설정 · 셰이더)을 플레이 중에 일으키고 결과를 판정할 장치가 없었다. 에셋 캐시 리로드 함수가 `void` 라 깨진 파일도 `Reloaded` 로 적혔고(`reload.status` 가 OK), 머티리얼 · 텍스처 · 프리팹은 새 파일을 읽기 전에 옛 판을 놓아 깨진 저장이 마젠타 · 체커 · 스폰 실패로 번졌다.

**대조.** 언리얼 리임포트는 실패하면 옛 에셋을 둔다. Live Coding 은 빌드가 지면 옛 이미지로 돈다. 자동화는 정리 단계(`AddCleanup`)로 바꾼 파일을 되돌린다. 유니티 Test Framework 도 `TearDown` 에서 되돌린다. → 같은 계약: 실패면 이전 판, 결과는 한 형식, 바꾼 파일은 실행기의 끝 한 곳에서 되돌린다.

**결정.**
1. 형식: `AssetCacheReloadFunction` 이 `ReloadResult` 를 돌려주고 `reloadAssetCache<T>` 가 멤버 결과를 `return` 한다 — `void` 를 돌려주는 캐시는 등록이 컴파일되지 않는다. 결과를 적는 곳은 `AssetHotReload::reloadCaches` 한 곳이다.
2. 집계: `ReloadTracker` 에 길 `Module` 과 길마다 결과 수 · 실패 수 · 마지막 시간(감시 값 `Reload.Count.<길>` · `Reload.FailedCount` · `Reload.StaleCount` · `Reload.LastMs.<길>`). 모듈 결과는 `LiveReloadManager`(빌드 실패 · 연쇄 교체)가 적는다.
3. 단계: `<RebuildModule name variant>`(빌드 트리의 판 머리를 덮고 `ModuleCompiler` 로 다시 짓기 — 추적 소스는 고치지 않는다, 판 표 `Config/Dev/ModuleRebuildVariants.json`), `<OverwriteConfig configFile from>`(쓰기 게이트가 연 `Config/Editor` 만), `<RestoreOverwrites/>`. 바꾼 파일은 `AutomationStepContext::restoreFileAtEnd` 에 맡기고 실행기가 `endRun` · 소멸자에서 되돌린다. 셰이더 · 에셋은 `<Scratch>` 사본을 `asset.overwriteScratch` 로 덮는다(원본은 게이트가 막는다).
4. 시나리오: `game/playground/automation/desktop/editor/playreload`(편집기 · 혼자 · RUN_SERIAL — 빌드 트리 · 모듈 DLL · `Config/Editor` 를 바꾸므로 백엔드 넷 체인에 넣지 않는다), `shaderreload`(재컴파일기가 Debug 클라이언트만이라 `<SkipUnless Shader.LiveRecompile>`). 허브 게임(Playground)으로 옮겨도 같은 단계 · 감시 값을 쓴다.

**PIE 를 리로드 너머로 잇는다(판단 2026-11-20).** 월드 · 씬 오브젝트 · 게임 인스턴스와 그 연결은 처음부터 호스트(엔진 `SceneManager` · App `GameModuleManager`) 것이고, 에디터 모듈이 쥔 것은 플레이 세션(`PlaySessionData` — 상태 · Step · 시작 위치 · 플래그 · 플레이 전 편집 씬의 롤백 스냅샷 · 남길 변경)뿐이다. 그래서 호스트로 통째 옮기지 않고 리로드 동안만 맡긴다: 작은 값은 `IModuleStateStore` 에 바이트로(`EditorPlaySession::carryOut` · `carryIn`, 키 `editor.playSession`), 롤백 스냅샷은 엔진 형 `ScenePlaySnapshot` 으로 묶어 씬 관리자가 맡는다(`SceneManager::holdPlaySnapshot` — 조립 기록 `Scene::AssemblySnapshot` 은 바이트로 옮기기에 크고, 엔진은 리로드하지 않아 형의 모양이 옛 이미지와 함께 바뀌지 않는다). 호스트 훅 `onBeforeSuspendModules( reason )` 은 리로드 · 백엔드 교체에 세션을 잇고(월드를 끄지 않는다 — 끄면 `endPlay` 가 돌아 상태 해시가 바뀐다) 종료에만 Stop 한다. 게임 모듈 리로드도 같은 길이라 PIE 가 멈추지 않고 게임 상태는 원래대로 `serializeState` 로 넘어간다. 편집 막기 수 · 거절 기록은 맡기지 않는다(쥔 작업이 옛 인스턴스와 사라진다). 되돌리기 기록의 보관분(플레이 동안 맡긴 편집 내역)은 이미 `EditorUndoHistory` 가 맡긴다. 대조: 언리얼 Live Coding 은 이미지를 바꾸지 않고 함수를 고쳐 PIE 가 그대로 이어지고, 유니티의 플레이 중 도메인 리로드는 상태를 직렬화해 되살린다 — 우리는 DLL 을 내리므로 유니티 모양(맡겼다 되찾기)으로 언리얼과 같은 결과(PIE 유지)를 낸다.

**남은 위험.** 메시는 임포트 산출물이라 추적 사본이 없어 시나리오 무대에 없다. 다른 갈래가 새로 더한 캐시의 `reload` 는 통합 때 `ReloadResult` 로 맞춰야 컴파일된다.

### 에디터 마우스 상호작용 공통 규칙(설계 2026-11-20)

**전수(패널 62 · 공통 위젯).** 있던 공통 층: 끌기 형식 하나(`EditorDragPayload` `{종류, id}` · 받는 쪽 `EditorPayloadKindSet`), 맥락 메뉴 R1 ~ R9(`EditorInputGUI::drawContextMenu` — 명령 등록부, `beginItemContextMenu` — 행 메뉴), 단축키는 입력 맵(`editor.input.xml` 패널 레이어), 프로퍼티 행 메뉴(Reset · Copy · Paste · Copy Name)와 노란 되돌리기 단추, 에셋 칸 끌어 놓기 · 고르기 · Browse.

| 패널 · 위젯 | 끌어 놓기 | 우클릭 | 휠클릭 | 그 밖 |
| --- | --- | --- | --- | --- |
| 인스펙터(프로퍼티 그리드) | 에셋 → 에셋 칸(받는 종류 검사는 놓은 뒤) · **오브젝트 → 참조 · 핸들 칸 없음** | 행: Reset · Copy · Paste(형식 검사 없음) · Copy Name, 컴포넌트 카드: 명령 메뉴. **에셋 칸의 찾기 · 참조 없음** | **없음** | **칸 위 Ctrl+C/V 없음** |
| 계층 | 오브젝트 → 행(앞 · 뒤 · 안 표시) · 에셋 → 행 · 빈 곳 | 오브젝트 · 컴포넌트 · 빈 곳 명령 메뉴 | — | F2 · Ctrl+D · Del · Ctrl+C/V · Ctrl+A · 화살표, Ctrl/Shift 다중 선택. **누르는 순간 선택해 다른 패널로 끌면 그 패널 대상이 바뀐다** |
| 콘텐츠 브라우저 | 에셋 → 폴더(이동 · 참조 고침 확인 창) · 폴더 트리 | 에셋 명령 메뉴 | — | 더블클릭 열기 · F2 · Ctrl+D · Del · Alt+←/→. **OS 파일 끌어 놓기 없음** |
| 씬 뷰 | 에셋 → 캔버스(배치 고스트) | 오브젝트 · 빈 곳 명령 메뉴(R7) | 화면 이동 | F · W/E/R · 책갈피 |
| 시퀀서 | 에셋 → 트랙(확장자 거르개 — **맞지 않아도 강조**) | — | — | (w-sequencer 갈래) |
| 노드 그래프 6(머티리얼 · 애니 · 오디오 · 대화 · 행동 트리 · 기믹) | 핀 끌기 → 노드 검색 | 빈 곳: 노드 넣기 | — | Ctrl+C/V/D · Del(`EditorGraphEditCommand`). **에셋 → 노드 없음** |
| UI 미리보기 | 위젯 타입 → 디자이너 | — | 화면 이동 | 더블클릭 |
| VFX 미리보기 | 프리팹 에셋 → 미리보기(**확장자 맞지 않아도 강조**) | — | — | — |
| 머티리얼 · 전역 변수 | — | — | **없음**(되돌리기 단추만) | — |
| 콘솔 · 카탈로그 · 맵 점검 · 타일 맵 | — | 행 메뉴 | 타일 맵: 화면 이동 | 콘솔 Ctrl+C, 맵 점검 더블클릭 |
| 나머지 40(설정 · 디버거 · 프로파일러 · 키트 확장 패널) | 다룰 끌 것 없음 | 표 행 메뉴 없음(읽기 전용 표) | — | — |

**대조.** 언리얼: Details 행 우클릭 Copy · Paste · Reset to Default, 노란 화살표, 행 위 Ctrl+C/V, Content Browser · Outliner 끌기(놓을 수 없으면 금지 아이콘), 액터를 액터 칸에 끌어 놓기. 유니티: Inspector 우클릭 Reset · Copy/Paste(타입 있는 클립보드 — 맞지 않으면 회색), Hierarchy 에서 Inspector 칸으로 끌기(끌기가 시작되면 선택을 바꾸지 않는다), Project 창 OS 파일 끌기. Godot: Inspector 되돌리기 아이콘, 노드를 칸에 끌기. 휠클릭으로 기본값은 셋 다 없다 — 사용자 규칙이다.

**규칙(정함).**
- R10 휠클릭 = 기본값으로: 프로퍼티 칸(이름 · 값)에서 가운데 단추를 끌기 문턱 안에서 떼면 기본값으로 되돌린다. 기본값과 다른 칸에서만(되돌리기 단추가 보이는 칸 — 상용의 화살표 자리), 되돌리기는 한 단계(편집 구간 — 전 · 후가 같으면 버린다). 단추는 `EditorInputGUI::kResetToDefaultButton` 하나, 패널은 `EditorWidgets::beginResetToDefaultGroup` · `endResetToDefaultGroup` 로 감싼다. 마우스 몸짓은 입력 맵이 보지 못해(에디터 창 위 마우스는 ImGui 가 먹는다) 맥락 메뉴의 오른쪽 단추처럼 코드 상수다.
- R11 끌어 놓기: 받는 쪽은 종류(`EditorPayloadKindSet`)로, 그다음 내용(확장자 · 에셋 종류 · 대상 타입)으로 **놓을 자리를 열기 전에** 거른다(`EditorInputGUI::peekPayload`) — 받지 않을 것은 강조하지 않는다. 받은 자리가 없으면 끄는 쪽 미리보기가 흐려지고 커서가 금지 모양이 된다(`beginDragSource` 한 곳). 놓기는 모델 명령 하나 = 되돌리기 한 단계.
- R12 끌 수 있는 목록 행은 누를 때가 아니라 끌기 문턱 안에서 뗄 때 고른다(`wasItemClickedWithoutDrag`) — 다른 패널의 칸으로 끄는 동안 그 패널의 대상이 바뀌지 않는다(유니티 Hierarchy · 언리얼 Outliner).
- R13 프로퍼티 칸: 행 메뉴의 Paste 는 클립보드 글이 그 칸 타입으로 읽힐 때만 켜진다(`PropertyGridViewModel::canPasteText`). 칸 위에서 Ctrl+C · Ctrl+V 는 행 메뉴와 같은 명령이다(입력 맵 `Editor.Global` 의 `property.copyValue` · `property.pasteValue` — 그리드를 둔 모든 패널에서, 그 창이 포커스일 때만 읽어 계층 · 콘솔의 Ctrl+C/V 와 겹치지 않는다). 에셋 칸 메뉴에 Browse to Asset · Find References(명령 등록부 — `EditorInputGUI::drawCommandMenuItem`).
- 항목 단축키: 계층은 F2 · Ctrl+D · Del · Ctrl+C/V 가 이미 있고, 콘텐츠 브라우저에 Ctrl+C = Copy Relative Path(언리얼 콘텐츠 브라우저의 Ctrl+C 가 에셋 참조 글을 복사하는 것)를 더한다. 노드 그래프는 `EditorGraphEditCommand` 가 같은 키를 맡는다.
- 오브젝트 → 칸: 계층 행을 `SceneObjectReference` · 타입 핸들 칸에 놓으면 그 오브젝트(핸들은 그 오브젝트의 첫 대상 타입 컴포넌트)가 들어간다.

**닫은 것(2 차).** OS 파일 → 콘텐츠 브라우저: 창 사건 `WindowFileDropEvent`(원시 입력 큐가 아니다 — 길이가 정해지지 않은 경로이고 리플레이 대상이 아니다)와 능력 표 `WindowFileDropSupport`(Win32 `WM_DROPFILES` 놓기만 — OLE `IDropTarget` 은 STA 가 필요한데 메인 스레드가 오디오 때문에 MTA, X11 XDND · Wayland `wl_data_device` 는 끄는 동안도, 웹 · Android 는 받지 않음) → `EditorFileDrop` → `ContentBrowserFileDropPlan`(경로를 믿지 않는다) → 기존 임포트 길. 에셋 → 노드 그래프: `EditorGraphAssetNodeRule` 표와 `EditorNodeGraph::acceptAssetDrop`, 머티리얼 그래프에 텍스처 → TextureSample 첫 등록. 카탈로그 칸 휠클릭 = 상속값으로(`CatalogDocument::canRevertToInherited` — 데이터 테이블은 상속 칸이 없다). 에셋 페이로드 id 는 리소스 id 한 형식(`EditorWidgets::registerAssetPayload`). 시퀀서 · 카메라 프리셋 · 충돌 프리셋 패널은 그 갈래가 공통 층을 붙인다.

## 시나리오 · 테스트 프레임워크 개편 판단(2026-11-17)

통합 묶음 5(작업자 갈래 약 30 개를 빌드 없이 합침)에서 겪은 실패 열 가지가 형식 · 실행기 동작으로 다시 생기지 않게 하는 설계다. 코드 근거는 f615fb04f4 다. 할 일 줄은 [NEXT](NEXT.md) 5 절 ST-1 ~ ST-6 이다.

**판단.** 틀(`SW_TEST_CASE` · 단언 매크로 · 시나리오 XML 형식 · 단계 등록)은 바꾸지 않는다. 실패 열 가지 가운데 선언 · 단언의 모양이 원인인 것은 없고, 케이스 선언 약 6490 개(921 파일)와 시나리오 443 개를 옮겨 얻는 것이 없다. 바꾸는 것은 넷이다 — ① 사진 앞 정착과 대기의 형식, ② 판정 값과 실행기의 입구 · 출구, ③ 새 테스트와 기준 파일(골든 · 스냅숏)의 수명, ④ 프로세스 · 단계 격리의 주인. 위 "Test Framework" 절의 "별도 프레임워크를 들이지 않는다" 는 그대로다.

### 실패 열 가지와 막는 자리

| # | 실패 | 근본 모양 | 막는 구조 | 막는 시점 | 남는 틈 |
| --- | --- | --- | --- | --- | --- |
| 1 | `Render.Stable` 이 TAA 없는 Forward 뷰에서 계속 0, Empty 체인 74 분 | 합성 0/1 감시 값이 "이 구성에는 해당 없음" 과 "아직" 을 같은 0 으로 냈다. 대기는 시한까지 기다린 뒤 실패만 적고 다음 단계로 갔다(`shouldHoldForCondition` → `recordFailure`) | 정착 조건의 세 값 형식(D1), 정지 시한 · 포기는 시나리오의 끝 · 반복 실패 차단(D2) | 컴파일(`bool` 조건을 등록할 수 없다) · 로드(`Render.Stable` 은 모르는 감시 값) · 실행(정지 시한) | 0/1 프레임 감시 값을 기다리는 `<WaitUntil>` 은 진척을 볼 수 없어 시한까지 기다린다 — 상한은 K × 시한(D2) |
| 2 | 렌더 감시 값이 공용 자동화 파일에 있어 전용 서버 빌드가 깨짐 | 층 표의 포괄 키 `Automation = 11` — `Engine/Automation` 뿌리 파일은 모든 층을 include 할 수 있어 기능의 감시 값 · 차단 조건이 주인 폴더 밖에 쌓였다 | 포괄 키를 지우고 주인 폴더에서 등록(D3) | configure(층이 없는 파일) · 컴파일(위 층 `#error` 껍데기) | 층 11 의 조립 파일(`Engine.cpp` …)이 렌더러를 include 하는 것은 서버 빌드(`preflight` build 단계)가 본다 |
| 3 | 빌드 없이 쓴 테스트: 로그 선언 누락 · 틀린 기대값 · 박아 둔 형식 폭 · 사라진 로그 대기 · 기록된 적 없는 스냅숏 · 없는 골든 | 한 번도 돌지 않은 테스트와 기준 파일이 검증된 것과 구별 없이 전체 검증에 섞였다 | `changed` 단계 · 기준 파일 없음 판정 · 실패 종류(D4 · D6) | 실행 — 빌드 직후, 갈래 이름과 함께 | 작업자 시점에는 막지 못한다(빌드 금지가 규칙이다). 손으로 쓴 기대값 · 형식 폭은 형식이 막지 못한다 |
| 4 | vfxsort 의 PiP `ExpectImagesDiffer` 전제가 틀림 | 3 과 같다 — 새 시나리오의 단언이 처음 도는 곳이 전체 체인이었다 | `changed` 단계가 새 시나리오를 혼자 · 백엔드마다 먼저 돌리고, 모든 백엔드가 같은 단언으로 지면 "전제" 로 묶는다(D4) | 실행 — 빌드 직후 | 그림의 전제는 돌려야 안다 |
| 5 | 체인 위치에 따라 골든이 1.2 ~ 1.8 % 갈림(TAA 지터 순번) | 프레임을 넘는 뷰 상태가 단계보다 오래 살았고, 단계 사이 되돌리기는 사고가 날 때마다 늘리는 목록이다 | 뷰 대상을 통째로 버리기, 혼자 기록 · 체인 검증(D5) | 실행 — 골든을 기록한 그 묶음 | 판정에 드러나지 않는 누출, 특정 앞 단계에서만 드러나는 누출 |
| 6 | scratch 사본 판정이 한 층만 봐 남의 프로세스 파일을 읽음 | 프로세스 격리를 id 꼬리(`_p<pid>`)로 하고, 같은 리소스 트리에 놓인 남의 사본을 읽는 쪽이 이름을 풀어 걸렀다 | 실행 뿌리와 scratch 도메인 마운트(D7) | 구조 — 남의 사본이 이 프로세스의 리소스 트리에 없다 | 드라이버 캐시 · 창 포커스 같은 OS 전역 상태 |
| 7 | 백엔드 넷이 기동 직후 함께 멈춰 정지 시한에 죽음(재현 안 됨) | 자식 넷이 저마다 옛 산출물을 거두고, 부모는 죽일 때 증거를 남기지 않는다 | 거두기는 부모만, 죽이기 전 덤프(D7) | 실행 | 원인은 추정이다 — 다음에는 덤프가 가른다 |
| 8 | 거르개에 콜론을 써 `NO TESTS` | 거르개가 어떤 글자든 받아 틀린 문법과 "맞는 케이스 없음" 이 같은 길이다. 끝 줄이 기계가 읽는 꼴이 아니다 | 문법 검사 · 패턴마다 맞는 케이스 · 끝 줄(D6) | 인자를 읽을 때(실행 전) | 없음 |
| 9 | 세 갈래가 같은 멤버 · 감시 값 · 설정 칸을 각자 더해 충돌 | 기능마다 칸을 더하는 닫힌 집합체(`RenderStabilitySample` · `EngineConfig::_renderStability` · 실행기 멤버)가 공용 편집 자리였다 | 열린 등록(D1), 중복 등록은 시작 거절 · 병합 전 새 이름 겹침 보고(D3) | 병합 전(`api-diff`) · 기동(로드 오류) | 같은 일을 두 갈래에 맡기는 것은 배정이 막는다 |
| 10 | 체인 로그 끝줄이 `Failed X at frame 5: <Pass/>` | 결과와 까닭이 따로 든 두 변수 — 통과로 끝맺은 뒤(`finish( Passed, "<Pass/>" )`) 실패로 바뀌면 까닭은 끝 단계 이름으로 남는다 | 판정 값 하나(D6) | 컴파일(까닭 없는 실패를 만들 수 없다) | 없음 |

### 상용 엔진 대비

| 자리 | 언리얼 | 유니티 | Godot | 여기(이 설계 뒤) |
| --- | --- | --- | --- | --- |
| 사진 앞 정착 | 스크린샷 기능 테스트가 흔들리는 기능(TAA · 자동 노출 · 모션 블러)을 끄고(`bDisableNoisyRenderingFeatures`) 지연 뒤 찍는다 | Graphics Test Framework 의 `WaitFrames`(고정 프레임) | 없음(스크립트가 프레임을 센다) | 끄지 않고 찍는다 — 정착 조건을 만든 기능이 등록하고 사진 단계가 기다린다(D1) |
| 대기의 두 시계 | Gauntlet `MaxDuration` + 하트비트(`TimeoutBetweenActivity` — 기억에 의존), 기능 테스트 `TimeLimit` | `[Timeout]` 하나(넘으면 테스트 끝) | GUT `wait_until( 조건, 상한 )` — 넘으면 false 를 주고 테스트는 이어진다 | 시한 + 정지 시한, 포기는 끝(D2) |
| 기준 그림이 없을 때 | 스크린샷 비교 도구에 "New" 로 떠 승인을 기다린다(실패와 다른 상태) | 실패하고 실제 그림을 `ActualImages` 에 둔다 | 없음 | 실패 종류 `MissingBaseline`, `changed` 단계가 없는 것만 기록해 승인 목록으로(D4) |
| 기대 로그 | `AddExpectedError` — 안 나오면 실패, 선언 밖 Error 는 실패, Warning 은 기본 통과 | `LogAssert.Expect` — 같은 규칙, Warning 은 통과 | GUT 는 선택 | Warning 도 실패(그대로 둔다 — DDC · 그림자 경고가 실제 결함이었다) |
| 빈 실행 | Low-Level Tests(Catch2)는 맞는 테스트가 없으면 0 이 아닌 코드, 안 맞는 패턴 경고 옵션 | 맞는 테스트가 없어도 0(기억에 의존) | — | 문법 오류 · 안 맞는 패턴 · 0 개 모두 실패(D6). GoogleTest 는 0 개가 통과다 — 콜론 문법과 함께 들여오지 않는다 |
| 테스트 사이 격리 | Gauntlet 은 테스트마다 프로세스, PIE 는 새 GameInstance, 뷰 이력은 `FSceneViewState` 하나 | 없음(TearDown 이 맡는다) | 없음(고아 노드 수만 센다) | 단계마다 게임 · 뷰 대상을 새로 만들고, 골든은 혼자 기록해 체인으로 검증(D5) |
| 프로세스 산출물 | Gauntlet 이 역할마다 산출물 폴더를 주고 시한 때 로그 · 덤프를 모은다 | 프로젝트 `Library` 잠금이 한 편집기만 받는다 | `user://` 는 프로젝트마다 | 부모가 실행 뿌리를 주고 죽이기 전에 덤프(D7) |
| 실패 한 줄 | Gauntlet `ExitReason` + 요약 머리 | NUnit XML | — | 판정 값의 종류 + 첫 까닭이 모든 끝 줄에(D6) |

### 결정

**D1. 사진 앞 정착은 사진 단계가 맡고, 정착 조건은 그 상태를 만든 기능이 세 값 형식으로 등록한다.**
지금은 시나리오가 사진마다 `<WaitUntil watch="Render.Stable" equals="1" timeoutWallSeconds="30"/>` 를 적는다(170 줄 · 75 파일). `Render.Stable` 은 `RenderStabilitySample` 의 칸들을 `EngineConfig::_renderStability` 문턱과 견준 0/1 이라, 칸을 채우는 패스가 파이프라인에 없으면 계속 0 이다. 한편 `<Screenshot>` 은 이미 에셋 일과 차단 조건을 기다린다(`holdScreenshot`) — 정착을 기다리는 곳이 둘이다.

```cpp
class AutomationReadiness // 기본 생성자가 없다 — 셋 중 하나를 골라야 한다
{
public:
    [[nodiscard]] static AutomationReadiness ready();
    [[nodiscard]] static AutomationReadiness notApplicable();                              // 이 구성에는 이 일이 없다(파이프라인에 TAA 패스가 없다)
    [[nodiscard]] static AutomationReadiness waiting( uint64 progress, const utf8* pWhat ); // progress 는 일이 나아가면 바뀌는 수(쌓인 정지 프레임 · 남은 일 수)
};
using AutomationReadinessFunction = AutomationReadiness ( * )();
```

1. `SW_AUTOMATION_CAPTURE_BLOCKER` · `FINISH_BLOCKER` · `START_BLOCKER` · `CLOCK_HOLDER` 가 `bool()` 대신 `AutomationReadinessFunction` 을 받는다(12 자리 — 옛 함수는 컴파일되지 않는다). `_pDescribe` 와 `FINISH_BLOCKER_DESCRIBED` 는 지운다(`pWhat` 이 대신한다).
2. TAA 이력 · 노출 적응 · 실시간 GI 누적은 각각 그 이력을 든 코드 옆(`Renderer/…`)에서 캡처 조건으로 등록한다. `Render.Stable` 감시 값, `RenderStabilityUtil::isStable`, `EngineConfig::_renderStability`(배포 엔진 설정에 든 테스트 전용 문턱)는 지운다. 낱값 감시 값(`Render.StillFrames` …)은 진단용으로 남긴다.
3. 문턱은 `Config/Dev/ScenarioRun.json` 의 `_conditions.<조건 이름>` 이고 조건을 등록한 기능이 제 구조체로 읽는다. 등록된 조건에 칸이 없거나 칸에 조건이 없으면 로드 오류다(공용 구조체에 칸을 더하지 않는다 — 실패 9).
4. `<Screenshot stability="render|assets|none">`(옛 `waitForStability` 를 대신한다). `render`(기본) 는 에셋 + 캡처 조건 전부, `assets` 는 에셋 일만(움직이는 장면), `none` 은 그 프레임 그대로다. 움직이는 장면에 `render` 를 쓰면 TAA 조건이 정지 시한(D2)에 걸려 "카메라 · 물체가 움직인다" 로 바로 진다.
- 기각 — 감시 값 564 개의 함수 꼴(`bool f( …, float64& )`)을 세 값으로 바꾸기: 실패 1 의 원인은 합성 판정 하나였고, 낱값은 "값 없음" 과 0 이 이미 갈린다. 564 자리를 옮겨도 지은이가 0 을 내는 것은 막지 못한다.
- 기각 — 언리얼처럼 흔들리는 기능을 끄고 찍기: 골든이 TAA · 노출을 켠 그림을 보지 못한다.

**D2. 대기는 시계가 둘이고(시한 · 정지 시한), 포기는 그 시나리오의 끝이며, 같은 실패가 되풀이되면 체인을 멈춘다.**
에디터의 `<EditorWaitStable/>` 는 이미 이 모양이다(진행 중인 일에는 시한이 없고, 할 일이 없는데 120 프레임 넘게 움직이면 진다) — 나머지 대기에 넓힌다.
1. **정지 시한.** `waiting( progress, … )` 의 `progress` 가 `_stallFrames`(ScenarioRun.json) 동안 그대로면 그 단계는 `WaitStalled` 로 진다. 붙잡기 네 종류(캡처 · 끝 · 시작 · 시계)가 한 구현을 쓴다. 시한(`kMaxAssetStabilityFrames` 등)은 나아가고 있는 일의 상한으로만 남는다.
2. **벽시계 감시 값.** `SW_AUTOMATION_WALL_WATCH` 가 활동 수 함수(`uint64()` — 따르는 바깥 일이 나아가면 느는 수: 읽은 바이트 · 끝난 컴파일 · 자식 출력 길이)를 필수 인자로 받는다(24 자리 — 10 곳이 활동 수를 내고, 사건 하나를 기다리는 14 곳은 `nullptr` 과 까닭 주석으로 시한만 본다). `timeoutWallSeconds` 대기는 활동 수가 `_stallWallSeconds` 동안 그대로면 `WaitStalled` 다. 프로세스 층의 정지 시한(`kProcessStallSeconds` — 로그가 안 자라면 죽인다)과 같은 생각을 단계에 둔다.
3. **프레임 감시 값의 `<WaitUntil>`** 은 시한만 본다 — 0/1 값은 뒤집히기 전까지 진척이 보이지 않는다. 무인 실행은 프레임이 실시간보다 빨라 비용이 작다.
4. **포기는 끝이다.** `<WaitUntil>` · 사진 정착 · 입력 대상 대기가 시한 · 정지 시한에 걸리면 `AutomationStepEnd::Fail` 로 시나리오를 끝낸다. 지금은 실패를 적고 다음 단계로 가서 뒤의 대기가 모두 다시 시한까지 기다린다(실패 1 — 사진마다 수천 프레임).
5. **반복 실패 차단.** 한 체인에서 같은 실패 열쇠(종류 + 단계 종류 + 감시 값 · 조건 이름)가 `_repeatedFailureLimit`(기본 3) 번 나오면 체인을 `RepeatedFailure` 로 끝내고 남은 단계는 돌지 않은 것으로 적는다. 다시 돌 때는 `--from-stage` 로 잇는다.
- 상한: 실패 1 과 같은 일이 다시 나도 백엔드마다 3 × 정지 시한이다.
- 기각 — `<WaitUntil>` 모두에 진척 요구: 깃발 감시 값(애니메이션 끝 · 씬 전환)은 낼 진척이 없다.

**D3. 감시 값 · 조건 · 되돌리기는 주인 폴더에서 등록하고, 겹친 등록은 시작을 거절한다.**
1. `Engine/Automation` 뿌리의 기능 파일을 옮긴다: `Client/AutomationRenderWatches.cpp` → `Renderer/Frame/View/`(`RenderViewWatches.cpp` 옆), `AutomationInputWatches.cpp` → `Input/`, `AutomationCaptureUtil.cpp` 의 foliage 조건 → `Environment/Foliage/`(같은 모양의 숨은 자리다 — 층 11 파일이 층 8 헤더를 읽는다), 에셋 · 스크린샷 조건은 그 큐의 폴더로.
2. `EngineLayers.toml` 의 `Automation = 11` 을 지우고 남는 기계 파일(`AutomationRuntime.h` · `AutomationResult.h`)만 파일 키로 적는다. 뿌리에 새 파일을 더하면 configure 가 멈춘다(표에 없는 파일).
3. 정적 등록이 거절되면(`Registration.DuplicateName`) 수를 세고, `App --test` · `-scenario` 는 0 이 아니면 로드 오류로 시작하지 않는다 — 두 등록 자리(파일 · 줄)를 함께 찍는다. 지금은 오류 로그 한 줄이고 뒤의 등록이 조용히 빠진다.
4. `py -3 -m Scripts api-diff --base <시작 커밋> <갈래…>` 가 두 갈래 이상이 새로 더한 같은 이름(함수 · 멤버 · 등록 이름 · Config 키)을 따로 낸다 — 병합 전에 한 곳에서 본다.
- 기각 — 서버가 짓지 않는 헤더를 클라이언트 빌드에서 `#error` 로 가리기: 층마다 부분을 둘로 갈라야 해(PCH 두 벌) 빌드 시간이 든다. 서버 빌드는 build 단계에서 이미 돈다.

**D4. `origin/main` 뒤에 들어온 테스트 · 시나리오 · 기준 파일은 `changed` 단계에서 먼저 돈다.**
push 는 검증한 커밋만 하므로 `origin/main` 에 있는 것은 돈 적이 있고, 그 뒤에 더해지거나 바뀐 것은 돈 적이 없다. 이 구별을 상태 파일 없이 git 으로 낸다.
1. `preflight.sh` 에 build 와 cpu 사이 단계 `changed`(`--from changed`): `git diff --name-status origin/main...HEAD` 에서 (a) 바뀐 소스 · 테스트 파일의 기능 폴더 → 그 폴더를 싣는 ctest 항목(`UnitTests` · `UnitTests.<게임>` · `UnitTests.OnlineKits`)을 `--test-source=<폴더…>` 로 좁혀, (b) 더해지거나 바뀐 `*.scenario.xml` 을 하나씩 제 프로세스로 · 체인 백엔드마다 돌린다. 고르는 코드는 이력 스크립트(`test_history.py`)의 새 하위 명령이다.
2. 출력은 표 하나다: 항목 · 새것/바뀐 것 · 실패 종류(D6) · 까닭 한 줄 · 그 파일을 더한 갈래(병합 커밋 제목). 새 시나리오가 모든 백엔드에서 같은 단언으로 지면 "전제" 로, 한 백엔드만 지면 "백엔드" 로 묶는다. 지면 cpu · gpu-release 를 시작하지 않는다 — 끝 줄 `CHANGED_FAILED` · `CHANGED_OK=<sha>`.
3. **기준 파일 없음은 실패 종류 `MissingBaseline` 이다.** `changed` 단계만 `-scenario-record-missing-golden` · `--record-missing-snapshots` 로 **없는 것만** 쓴다(다른 것 · 진 것은 덮지 않는다 — `-scenario-update-golden` 은 진 골든도 써서 바뀐 시나리오의 회귀를 가린다). 쓴 파일이 있으면 단계는 목록을 내고 멈춘다. 통합 담당이 그림 · 글을 보고 커밋한 뒤 `--from changed` 로 다시 돌려 기록 없이 통과해야 한다. 커밋된 트리는 늘 완전하다는 불변식(`ScenarioGoldensTest.EveryChainBackendHasGoldens`)은 그대로다.
4. **스냅숏의 뜻은 "지금 코드의 출력을 사람이 본 것" 하나다.** 돌려 봐야 아는 수(전이 수 · 생성 글)는 추측한 리터럴 대신 `SW_EXPECT_SNAPSHOT` 으로 적어 첫 실행이 기록하게 한다(지금 쓰는 곳 3 · 파일 2 라 옮길 것이 없다). "옛 구현과 같다" 는 스냅숏으로 쓰지 않는다 — 옛 구현을 `Tests/` 로 옮겨 한 케이스에서 둘을 견주고 다음 묶음에서 지운다(CultureInfo 는 옛 코드로 돌아가 빌드해야 했다).
5. **낡은 기대 로그.** `SW_EXPECT_LOG( "X" )` · `<ExpectLog id="X">` · `<AllowLog id="X">` 의 id 를 내는 `SW_LOG_*_ID( "X"` 가 소스에 없으면 위반 — 기능 참조 린트(`feature-refs`)의 이름 종류에 로그 id 를 더한다. 린트인 까닭: id 는 테스트와 다른 TU 의 호출 자리를 잇는 글자라 형식이 잇지 못하고, 작업자는 빌드하지 않으므로 커밋 때 볼 수 있는 것은 글 대조뿐이다. 이 설계가 린트에 기대는 자리는 이것 하나다.
- 기각 — 선언 밖 Warning 을 통과로(언리얼 · 유니티 기본): 이번 그림자 · 글리프 · DDC 경고는 테스트가 새 경로를 밟는다는 사실이었다. 대신 실패 줄이 붙일 줄을 그대로 낸다(D6).
- 기각 — 이력 DB 로 "새 테스트" 가르기: `Saved/` 의 sqlite 는 기계마다 다르다. git 은 어디서나 같다.

**D5. 단계 격리는 되돌릴 목록이 아니라 버리고 새로 만드는 객체로, 누출 찾기는 혼자 기록 · 체인 검증으로 한다.**
지금 단계 시작(`AutomationSession::startStage`)은 빈 씬 · 전역 변수 · 창 크기 · `SW_AUTOMATION_STAGE_RESET` 5 개 · 사본 · 번역 키를 차례로 되돌리고 게임을 다시 세운다. 줄마다 사고의 흔적이고, 뷰 대상(`FrameViewTarget` — TAA · 깊이 이력, 지터 순번, 노출 기록, 가림 타일)은 이 목록에 없었다.
1. 단계 시작이 렌더 드라이버 창구(`IEngineRenderDriver`)로 "뷰 대상을 모두 버린다" 를 한 번 부른다. 프레임을 넘는 뷰 상태는 뷰 대상의 멤버로만 둔다(`TemporalJitterSequence` 포함) — 새 이력 멤버를 더해도 목록을 고치지 않는다. 이력을 버릴 때 지터를 되돌리는 지금 줄은 지운다. 언리얼 `FSceneViewState` 와 같은 자리다.
2. 렌더러 범위에서 프레임을 넘어 쌓이는 그 밖의 상태는 씬 객체가 들거나(씬과 함께 사라진다) `SW_AUTOMATION_STAGE_RESET` 을 등록한다 — 이 목록은 남는다.
3. **골든과 `<Record>` 는 혼자 돈 실행(새 프로세스)에서만 기록하고 체인으로 검증한다.** `changed` 단계가 그렇게 돈다(D4). 판정에 닿는 누출은 골든을 기록한 그 묶음에서 그 시나리오의 실패로 드러난다 — 지금은 체인 자리에서 기록해 체인 순서가 바뀔 때(갈래 30 개가 시나리오를 더할 때) 남의 골든이 진다.
- 기각 — 단계마다 프로세스(Gauntlet): 시나리오 443 × 백엔드 4 의 기동 비용. 기각 — 체인 순서 섞기: 앞 단계가 가리던 결함이 실행마다 드러났다 숨는다(시간표를 사람이 커밋하게 한 까닭과 같다).

**D6. 입구는 틀린 문법을 거절하고, 출구는 판정 값 하나에서 한 줄을 찍는다.**
1. **거르개.** 패턴에 올 수 있는 글자(이름 글자 · `.` · `*` · `?` · `/` · `[태그]` · 앞의 `-`)와 쉼표 밖의 글자가 있으면 인자 오류다("separate patterns with a comma"). 더하기 패턴마다 등록된 케이스 하나는 맞아야 한다(샤드 · 폴더로 거르기 전에 본다) — `A.*,Typo.*` 가 A 만 돌고 통과하지 않는다.
2. **끝 줄.** 단위 실행의 마지막 표준 출력 줄은 늘 `TEST_RESULT=<PASSED|FAILED|NO_TESTS|TIMEOUT> run=… failed=… skipped=…`, 시나리오 부모는 `SCENARIO_RESULT=<…> stages=… failed=… notRun=…` 다(`WINDOWS_OK=` · `LINUX_OK=` 와 같은 꼴). 출력을 잘라 본 쪽도 0 개 실행을 통과로 읽지 않는다.
3. **판정 값.** `AutomationRunner` 의 `_result` + `_finishReason` 을 값 하나로 바꾼다.

```cpp
enum class AutomationFailureKind : uint8 { Assertion, WaitTimeout, WaitStalled, UnhandledLog, GoldenMismatch, MissingBaseline, LoadError, Hang, RepeatedFailure };
class AutomationVerdict // 만드는 길은 아래 셋뿐이다
{
public:
    [[nodiscard]] static AutomationVerdict passed();
    [[nodiscard]] static AutomationVerdict skipped( string_view reason, bool bByBuild );
    [[nodiscard]] static AutomationVerdict failed( AutomationFailureKind kind, string_view reason, string_view stepLocation );
};
```

   통과로 끝맺었는데 실패 줄이 있으면 첫 실패 줄이, 처리하지 않은 로그면 그 첫 줄(과 남은 수)이 까닭이다 — 끝 단계 이름이 까닭이 되는 길이 없다. `Automation.Finished` · `chain stage i/n` · 체인 끝 표 · 부모의 백엔드 요약 · 보고 JSON(`verdict`) · HUD 가 이 값 하나를 찍는다. `chain stage 12/94 Failed (game.playground.vfxsort)` 뒤에 `: UnhandledLog - [Warning] (id=Shadow.AtlasFull) … (+2)` 가 붙는다(앞머리는 그대로 — `linux_scenarios.py` 의 정규식이 읽는다).
4. **부모 요약.** 백엔드별 실행 끝에 (단계 · 종류 · 까닭)이 같은 줄을 백엔드로 접은 표를 낸다 — `vfxsort: GoldenMismatch dx11,dx12,vk,gl` 과 `…: vk` 가 한눈에 갈린다. `preflight.sh` 가 단계 끝마다 이 표를 찍는다.
5. **단위 테스트 실패 종류.** 보고 판 4 에 케이스의 `failureKind`(Assertion · NoAssertion · UndeclaredLog · ExpectedLogMissing · SnapshotMissing · SnapshotDiffers · Timeout · Crashed)와 종류별 수. `UndeclaredLog` 줄은 붙일 글을 그대로 낸다(`add SW_EXPECT_LOG( "Shadow.AtlasFull" )` + 케이스 파일 · 줄).

**D7. 프로세스가 쓰는 곳은 부모가 준 실행 뿌리 하나이고, 지우는 것은 주인뿐이며, 죽이기 전에 덤프를 뜬다.**
지금은 scratch 사본(`scratch/<이름>_p<pid>`) · 사용자 데이터(`run_p<pid>`) · 에디터 상태(`editor_p<pid>`) · 울타리 표지(`contentfence_p<pid>`)가 저마다 pid 로 이름을 짓고, 자식마다 죽은 형제의 폴더를 거두며, `Content/scratch/` 를 훑는 길(에디터 콘텐츠 훑기 · 파일 감시)이 `isOtherProcessInstance` 로 남의 사본을 걸러야 한다.
1. `ProcessRoots` 에 실행 뿌리 `_run` 을 둔다. 백엔드별 부모(`AutomationPerBackend`)가 `Saved/Automation/Runs/<실행 id>/<백엔드>/` 를 만들어 `-run-root=` 로 넘기고, 혼자 뜬 실행은 스스로 만든다. 사용자 데이터 · 에디터 상태 · 테스트 쓰기 폴더는 그 아래다.
2. **scratch 도메인은 실행 뿌리에 마운트한다**(`AssetPaths::mountDiskDomain( "scratch", <실행 뿌리>/scratch )` — 단위 테스트가 이미 쓰는 길). 사본 id 는 시나리오가 적은 그대로다. 지우는 것: `makeInstanceID` · `isOtherProcessInstance` · `removeOtherProcessInstances` · `findInstanceProcessID` · `AutomationScenario::bindScratchInstances` · 형제 거두기 · 울타리 표지의 pid 꼬리. 남의 사본은 이 프로세스의 리소스 트리 밖이라 거를 것이 없다.
3. **거두기는 부모만.** 부모가 자식을 띄우기 전에 끝난 실행의 뿌리를 지운다(최근 몇 개를 남길지는 ScenarioRun.json). 자식은 제 뿌리 밖을 지우지 않는다.
4. **죽이기 전 덤프.** 정지 시한 · 프로세스 시한으로 자식을 죽일 때 부모가 먼저 자식의 모든 스레드 스택을 실행 뿌리에 쓴다(Windows 는 자식 핸들로 미니덤프, Linux 는 중단 신호로 크래시 처리기) — 경로가 요약 줄에 붙는다. 행 감시(`AutomationHangWatch`)는 지금 단계 시작에서 켜져 기동 중 멈춤을 보지 못한다. 언리얼 `FThreadHeartBeat` · Gauntlet 의 시한 덤프와 같은 자리다.
- 기각 — 정지 시한에 걸린 자식을 다시 띄우기: 흔들림을 가린다.

### 이전 순서와 규모

모두 강제 이전이다 — 옛 꼴은 컴파일 · 로드에서 멈추고, 옮기는 일은 형식을 바꾼 쪽이 한다. 수는 `git ls-files` · `git grep`(f615fb04f4)이다.

| 단계 | 내용 | 손대는 곳 | 의존 |
| --- | --- | --- | --- |
| ST-1 출입구 | D6 전부 + D3-3(겹친 등록 시작 거절) | `Core/Testing/TestRunner` · 실행기 판정(`finish` 호출 38 자리, `recordFailure` 113 자리는 그대로) · 세션 · 체인 · 백엔드별 부모 · 보고 · HUD · 스크립트 3(`preflight.sh` · `linux_scenarios.py` · `scenario_bisect.py`) · 에디터 Test Runner 패널 — C++ 약 15 파일 | 없음 |
| ST-2 `changed` 단계 | D4 | `preflight.sh` · `test_history.py` · 골든 판정(`AutomationEvaluator` · `AutomationGoldenUtil`) · `TestRunner` 스냅숏 · `feature_references.py` · 스킬 2(`preflight` · `scenario-debug`) · `Test/README.md` | ST-1(실패 종류) |
| ST-3 정착 · 대기 | D1 · D2 | 차단 조건 12 자리 · 벽시계 감시 값 25 자리(16 파일) · 실행기 대기 3 파일 · 렌더러 정착 3 ~ 4 파일 · `EngineConfig`(칸 삭제) · 시나리오 코드모드: `Render.Stable` 170 줄(75 파일) 삭제, `<Screenshot>` 268 개(110 파일) 가운데 `waitForStability` 29 개 → `stability="none"`, 앞에 `Render.Stable` 이 없던 약 70 개 → `stability="assets"`(동작 보존) | ST-1 |
| ST-4 주인 폴더 | D3-1 · 2 · 4 | 파일 옮기기 3(`py -3 -m Scripts move-source`) · `EngineLayers.toml` · `api_diff.py` | 없음(ST-3 과 같은 파일 — 뒤에) |
| ST-5 실행 뿌리 | D7 | `ProcessRoots` · `AutomationScratch`(+ Workspace) · `AutomationUserDataRoot` · `AutomationPerBackend` · `EditorPaths` · `EditorContentScan` · `AssetHotReload` · 테스트 — 약 14 파일. `<Scratch>` 61 개(36 파일)의 XML 은 그대로 | 없음 |
| ST-6 격리 | D5 | 세션 · 렌더 드라이버 창구 · 뷰 대상 3 파일 | ST-2(혼자 기록) |

ST-1 · ST-5 는 파일이 겹치지 않아 함께 맡길 수 있다. ST-2 · ST-3 은 ST-1 뒤, ST-4 는 ST-3 뒤, ST-6 은 ST-2 뒤다. 코드모드는 이전 스크립트(`Scripts/dev/migrate_*.py` 와 같은 자리, 자체 검사 포함)로 한다. 지운 이름(`isOtherProcessInstance` · `makeInstanceID` · `_finishReason` …)은 `_bannedNames` 에 올린다.

### 합격 문턱

- **ST-1:** 단위 — 콜론 · 세미콜론 · 공백이 든 거르개가 인자 오류, 안 맞는 패턴이 실패, 통과로 끝맺고 실패 줄이 있는 판정의 까닭이 첫 실패 줄, 겹친 등록을 낸 자식 역할이 로드 오류. 체인 로그에 `: <Pass/>` 로 끝나는 실패 줄 0. 세 스크립트의 `--self-check` 통과.
- **ST-2:** 심은 묶음(틀린 기대값 케이스 · 골든 없는 시나리오 · 스냅숏 없는 케이스 · 선언 안 한 Warning 하나씩)을 `changed` 단계가 종류 · 갈래와 함께 넷 다 내고 cpu 를 시작하지 않는다. 증분 빌드 뒤 5 분 이내. 기록한 골든으로 기록 없는 체인이 통과한다.
- **ST-3:** 단위 — 진척 없는 `waiting` 이 `_stallFrames` 뒤 `WaitStalled`, `notApplicable` 은 막지 않는다. Forward 파이프라인 사진 시나리오가 TAA 조건을 기다린 프레임 0. 멈춘 조건을 심은 체인이 같은 실패 3 번 뒤 `RepeatedFailure` 로 끝나고 벽시계가 3 × 정지 시한 + 기동 이내. 코드모드 뒤 Release 체인이 기록 없이 통과(다시 뜬 골든 0 — 넘으면 그 시나리오의 대기 순서를 본다). Empty 체인 벽시계가 전보다 늘지 않는다(`timeline` 줄). `Render.Stable` · `waitForStability` · `_renderStability` 참조 0.
- **ST-4:** 전용 서버 Debug 빌드 통과. `Source/Engine/Automation/` 뿌리에 `.cpp` 0. 층 생성 스크립트 테스트에 "뿌리에 더한 파일은 configure 오류" 사례.
- **ST-5:** pid 꼬리 도우미 호출 0. `<Scratch>` 시나리오 36 개가 네 백엔드 함께 3 회 통과하고 실행 뒤 `Content/scratch/` 에 남는 폴더 0. 멈춘 자식 역할을 죽인 부모가 덤프를 남기고 그 안에 게임 스레드 스택이 있다.
- **ST-6:** 같은 시나리오를 혼자 · 체인 끝에서 돌린 `goldenSamples` 차이가 같은 자리 두 번의 차이 이내. `stage reset took` 증가 5 ms 이내.

### 남는 위험

- 3 번 실패(빌드 없이 쓴 테스트)는 통합에서 처음 드러난다는 점은 그대로다 — 줄인 것은 드러나는 때(전체 검증 중간 → 빌드 직후)와 분류 비용이다. `changed` 단계는 바뀐 폴더의 테스트만 골라, 다른 폴더의 테스트가 밟는 새 Warning 은 전체 실행에서 처음 나온다.
- 반복 실패 차단은 뒤 단계의 다른 실패를 가린다 — 돌지 않은 단계를 표에 남기고 `--from-stage` 로 잇는다.
- scratch 마운트: 리소스 뿌리에 id 를 직접 붙여 디스크 경로를 만드는 에디터 코드가 있으면 사본을 찾지 못한다 — ST-5 의 첫 일은 그 자리를 `AssetPaths` 로 모으는 것이다.
- 같은 모양이 남은 곳: `EngineConfig` 의 다른 개발 전용 칸. 모듈 섀도 복사본은 실행 뿌리 아래 `ModuleShadow/<pid>` 로 옮겼다(`ShadowCopyName` — 이름으로 거두는 코드 없음).
- 시나리오 산출물 `{out}`(`Saved/Automation/<시나리오>/<pid>`)과 체인 보고 `…/per-backend-<pid>` 는 실행 뿌리로 옮기지 않는다: 사람과 도구(`scenario_bisect.py` · `linux_scenarios.py` · `test_history.py ingest`)가 시나리오 이름으로 찾는 자리이고, 보존이 시나리오마다 최근 3 개(`SavedRetention` 의 `Automation` 규칙 `_bPerChild`)라 실행 전체 최근 3 개인 `Runs` 보다 오래 남는다(실행 뿌리를 쓰는 단독 실행마다 `Runs` 에 폴더가 생겨 체인 산출물이 금방 밀린다). pid 꼬리는 자리 잡기(`reserveUniquePath` — 겹치면 번호를 붙인다)일 뿐 형제 거두기가 없어 ST-5 가 없앤 결함 모양이 아니다. 언리얼도 자동화 보고는 `Saved/Automation/Reports` 에 실행과 따로 둔다.

## 외부 엔진 · 라이브러리 검토(2026-11-19)

ezEngine(MIT) · Adria(MIT) · VQEngine(GPL-3.0 — 설계만) · Taskflow(MIT, v4 는 C++20) · magic_enum(MIT) · GameServer(사용자 저장소, 라이선스 없음) · XMachinaServer(라이선스 없음 — 설계만)를 읽고 우리 것과 견줬다. 코드 이식은 없다 — 모양만 옮긴다. 의존 추가(taskflow · magic_enum · FidelityFX SDK)는 들이지 않는다(C++20 요구 · 런타임 레지스트리와 이름 출처가 둘 · 템플릿 비용).
우리가 이미 앞선 것: 틱 배치 계약, 렌더 그래프 바이트 별칭 · 레벨 병렬 기록 · 비동기 컴퓨트, 데이터 구동 파이프라인 · 쿠킹, 열거자 개명 리다이렉트, 작업 시스템 깨우기 · 대기 중 돕기 · 결정적 리듀스, 서버 프레이밍 상한 · 송신 물금 · 인증 · 도배 제한 · SQL 재연결.

옮길 것(가치 순, 크기 S/M/L, 측정 조건은 끝 괄호):
- 형식으로 막는 결함 모양
  - 병렬 본문 출력 칸 `runParallelInto<T>( span<T>, body( i, T& ) )` · 결정적 `runParallelReduce`(Taskflow transform · 감사 4번, S).
    - 됨(2026-11-19): TaskManager · `engine::` 에 Into(임계 · ByCost) · Reduce(고정 블록 번호 순 접기 → 블록 순 합치기 — 워커 수 무관 같은 비트). 칸 하나 = 항목 하나인 호출부 14 곳을 옮겼다. 칸이 여럿이거나 구간째 일괄 처리하는 본문은 `runParallelRangeInto( count, threshold, body( start, end, span<T0>… ), span<T0>… )`(출력마다 제 구간 조각, 빈 출력 = 없는 스트림)로 옮겼다(GPUSceneBuilder 수집, 메시 모디파이어 변환 · 변형, 물리 포즈 묶음 읽기). 같은 길이 span 을 묶는 칸별 본문 대신 구간 본문을 고른 까닭: 백엔드 묶음 호출 · SoA 스트림은 구간이 일의 모양이고, 칸별 본문은 그 안의 루프로 쓰면 된다. 남은 손 포인터는 형식으로 담을 수 없는 모양이라 둔다 — 셰이더 쿠킹 사슬 · GPUSceneBuilder 트랜스폼 복사(칸 표를 거친 흩뿌리기, 단사는 실행 데이터), GPUSceneBuilder 갱신(청크 칸 + 칸 없는 투명 꼬리 블록 하나 — 칸 수와 항목 수가 다르다), PathModifierUtil 정합(덩어리별 표시).
  - 메시지 방향 강제 — 라우터가 역할별 마스크로 반대 방향 종류를 `Malformed`(XMachina, S). 했다: `INetMessageHandler::getMessageDirectionMask`(순수 가상, 방향 없는 종류는 등록 거절), `poll` 이 반대 방향 연결을 끊는다. 표 메시지가 라우터 판 알림 종류(0x0F)를 쓰면 컴파일 오류 — `net.traffic` 이 그 자리라 상대 판을 빈 목록으로 덮던 결함을 함께 고쳤다.
  - 게임 넷 메시지 본문을 REFLECT 고정 배치로, 본문 판은 스키마 해시(GameServer PacketGenerator, M — NetMessageDef 37 곳). 했다: `NetMessageBodyDef`(판 = 스키마 해시) · 표가 본문을 다 읽고 넘김(`NetMessageDelivery`) — 24 종(턴 중계 7 · 락스텝 3 · 엔진 세션 3 · 파괴 3 · 하우징 3 · 원격 호출 거절 · 스냅샷 확인 · 페어드 끝 · NetSim 샘플 2 — 에디터 확장도 반사 단계를 거친다). 손 본문 13 종은 `BitReader::readEnum` · `readCount` · `readEnd` 로 막고, 표 받기가 넘친 손 본문을 Malformed 로 바꾼다. 손 본문으로 남긴 13 종은 값마다 인코딩을 고르거나 예산으로 개수를 정하는 모양이라 고정 배치가 대역을 늘린다: 스냅샷 · 입력(NetClientServer), MMO 셋, 파괴 이벤트 · 자세, 하우징 편집 · 델타, 원격 호출, 롤백 입력, 페어드 상태(롤백 소유 상태 공개 형식), `net.traffic`(0 바이트 부하). 서술자가 있는 메시지는 서술자 채널로만 나간다(`NetMessageWriter::sendMessage` 계열).
  - 열거형: 등록기를 항목 표 하나로(생성 바이트 약 0.7 MB↓), 플래그 글은 단일 비트 이름만, 끝 열거자 `Count` 자동 센티널, Core `EnumArray<E,T>` · `makeEnumTable`(개수 모자람을 컴파일 오류로), 열거자 단위 데이터 철자(magic_enum, S~M).
    - 했다(w-enum): 등록기는 `NamedValue<int64>` 항목 표 + `EnumInfo::create<E>( EnumInfoDesc )` 한 줄(맵 · 인덱스 목록은 런타임이 짓고, 값은 컴파일러 식이라 대조 assert 없음). 표식 열거자는 이름 표에 남겨 왕복을 지키고 `_listEnumerator`(선언 순서, Count 제외)만 반복에서 뺀다. 플래그 글은 단일 비트 이름만 쓰되 단일 비트 이름이 없는 비트만 조합 이름으로 적어 값을 잃지 않고, 읽기는 조합 이름(`_channels="All"` 같은 손 데이터)도 받는다. 끝 `Count` 규칙은 `CursorReader::resolveCountEnumerator` 하나를 등록기 · 고정 배치 코덱이 함께 쓴다. 열거자 메타는 `ENUMERATOR( DisplayName · Tooltip · Hidden · Meta )`(언리얼 UMETA 자리, 개발 전용 메타 단위로).
    - `EnumArray<E, T>` PROPERTY 는 리플렉션에서 열거자 → 값 고정 맵(`EnumArrayWrapper`, 텍스트 키 = 열거자 이름 — 고정 배열 꼴은 열거자를 끼우면 저장 값이 밀린다), 고정 배치형에서는 본문이 `T[N]` 과 같은 바이트(스키마 해시는 칸 철자를 섞어 바뀐다 — B 는 같은 빌드끼리라 변환 불필요). 언리얼은 C 배열 + UENUM 크기라 그 밀림을 막지 못한다.
  - 렌더 그래프 접근 종류를 핸들 형식에 — 손 전이 81 곳 제거(Adria, L, HZB 뒤 — 설계는 아래 "렌더 그래프 접근 핸들" 단락, 코드는 다음 갈래).
  - 컴포넌트 → 카탈로그 id 를 `CatalogKey<Tag>` 형식으로, 쿠킹이 대조(ezEngine, M). **했다(2026-11-19)** — 판단: 항목 종류 → 카탈로그 루트 · 원소와 칸 목록은 코드가 아니라 참조 표(`<CatalogKey>` · `<Field>`)에 둔다. 쿠커는 게임 모듈 리플렉션을 못 보기 때문이고(문서 표 · `DocumentTableMatchesReflection` 과 같은 모양), 파서가 칸의 항목 종류를 메타 `_catalogKeyTag` 에 실어 표와 코드를 테스트가 대조한다. 대조는 "굽는 모든 도메인의 그 종류 카탈로그 어딘가에 있다" 이고(키는 파일을 모른다 — 언리얼 `FDataTableRowHandle` 은 표 포인터를 함께 든다), 쓰는 쪽이 고른 한 벌은 시작할 때 `GameCatalog::require` 가 본다. `ActionPlatformer` 패턴 전이는 같은 패턴 안(inEntry) 줄로, `FighterCatalog` 기술 → 프레임은 다른 파일 줄(`file` · `defaultAttribute`)로 옮겨 그 원소만 버리던 읽기가 실패한다. 이어서 외형 프리셋 칸을 옮기고, 종류가 둘인 칸은 종류마다 칸 · 형식으로 나눴다(`WesternDeedComponent` 의 범죄 · 행동 · 사냥감 · 음식 · 옷, `MeadowTownObjectView` · `MeadowTownCreatureView`). 구조체 칸 하나는 `<Struct owner field type>`, 카탈로그 정의 사이의 키(무기 → 탄 · 아이템 등)는 정의 형식 ↔ 원소 연결 `<Element type catalog path>` 로 쿠커가 잡는다. 남은 한계: 쿠커는 원소 이름(가장 아래 형식)으로 찾고 상속을 따라가지 않는다 — 키 칸을 든 형식을 물려받으면 표 대조 테스트가 멈춘다. 같은 파일 안 정의끼리의 참조(복셀 블록 `_dropID` · 하우징 `_replacedID` 등)는 `<Reference>` 줄이 맡을 자리라 옮기지 않았다.
  - 타입별 판 + 패치 `REFLECT( Version = N )` — 그릇마다가 아니라 타입 한 곳(ezEngine GraphPatch, M).
- 화질
  - 스카이 라이트 · 반사 프로브 거르기를 filtered importance sampling(원본 밉을 pdf 로 고름), HDRI 반사를 SH9 대신 거른 텍스처로(VQEngine, S → GPU 찍기 M).
    - 됨(2026-11-19): 두 곳이 `ReflectionProbeAsset::filterRoughnessLevels( OctahedralRadianceMap, 표본 수 )` 하나를 쓴다 — 원본은 옥타헤드럴 상자 밉 사슬(밉 수는 Config `_skyLight._filterMipCount` · `_reflectionProbeFilterMipCount`). 밉 치우침은 GPU Gems 의 +1 대신 −1(쌍선형이 텍셀 둘을 덮는다 — 기준 적분과 견준 numpy 원형: +1 은 거친 단계 평균 오차 10 ~ 35 %, −1 은 1 ~ 5 %). HDRI 는 GPU 찍기 대신 스크립트가 원본을 128² `.radiancemap` 으로 접고(CPU 는 BC6H 를 읽지 않는다) 스카이 라이트가 회전 · 세기를 곱해 거른다 — 확산은 같은 원본의 SH9 투영. GPU 찍기(구름 · 해 원반 포함)는 하늘이 프레임마다 바뀌는 장면이 생길 때(M).
  - TAA: YCoCg 분산 클리핑 · Catmull-Rom 히스토리 · 적응 블렌드 → 그 위에 TAAU(Adria, S → M) — 옮겼다. 식은 셰이더 · C++ 가 같은 파일(temporal_math.hlsli · `TemporalMath.h`)을 읽는다. TAAU 는 첨부마다 해상도 공간(`RenderPassAttachment::_resolutionSpace` 렌더 · 출력, 언리얼 1 차 · 2 차 화면 배율)을 선언하고 풀이 두 크기를 갖는다 — 원본을 렌더 → 출력으로 넘기는 것은 TAA 만(검증), 두 공간의 사각형 배율은 같아 깊이 · 속도를 UV 로 읽는다. 켜는 값은 EngineConfig `_render._temporalUpscaleRenderScale`(기본 1, 품질 low 0.5 · medium 0.75), 지터 바퀴는 8 / 배율².
  - 프로브 GI 빛샘: 거리 모먼트 + 체비쇼프 가시성 · 뒷면 가중(Adria DDGI, M — GI 골든 전부).
    - 판단(2026-11-19, 했다): 모먼트는 텍스처 슬롯 대신 프로브 버퍼(t13) 안 SH9 뒤 옥타헤드럴 8×8 묶음(float4 하나에 텍셀 둘)으로 — 반사 프로브와 같은 이유(네 백엔드 같은 길, 바인딩 수 그대로). 광선마다 가장 가까운 텍셀에 기록 비율로 섞고(DDGI 의 텍셀마다 코사인 가중 적분은 스레드 하나 = 프로브 하나라 비싸 뺐다), 읽을 때 쌍선형. 가중 식은 shading_math.hlsli 하나를 C++ 테스트와 셰이더가 함께 읽고, 계수는 EngineConfig `_render._probeVisibility` → 쿠킹 define. 베이크 볼륨은 모먼트가 없어 뒷면 가중 · 바이어스만 받는다. 프로브 재배치(DDGI relocation)는 칸 안에 박힌 프로브를 모먼트로 가리는 것으로 대신한다.
  - 2 단계 HZB 가림(지난 HZB → 새 HZB 로 다시 시험) — SSR 계층 탐색 · 큰 물체 컬링도 같은 피라미드(Adria · VQEngine, L). **했다(2026-11-19)** — 피라미드는 뷰마다 구조 버퍼 하나(float2 최소 · 최대, `HZBLayout` · hzb.hlsli — 에뮬 백엔드 컴퓨트가 밉마다 UAV 를 못 걸어 텍스처 밉 사슬 대신), 프레임 처음(지난 깊이 — 앞 단계) · 깊이 프리패스 뒤(뒤 단계) · SSR 앞(hi-z trace)에 다시 짓는다. 뒤 단계는 Adria 처럼 따로 간접 인자 · 그리기를 두지 않고 앞 단계가 적은 차례 표시(u3)만 다시 판정해 **같은 간접 인자에 더한다** — 패스마다 그리기를 둘로 나누지 않는 대신 뒤 단계 인스턴스가 그 프레임 깊이 프리패스에 없다(DBuffer 데칼이 드러나는 첫 프레임 한 번 모른다). 켜고 끄기 · 문턱은 `Config/Engine/OcclusionCulling.json`, 간접 멀티 드로우가 없는 WebGPU 는 끈다. 투명 정렬 배치는 앞 단계만 본다(정렬 뒤에 붙지 않게).
  - 자동 노출 히스토그램 + 백분위 컷(Adria, S~M — 옮겼다: 네이티브 bindless 는 컴퓨트 한 그룹 `AutoExposureHistogram`, 에뮬 백엔드는 같은 식의 1x1 풀스크린, 컷은 EngineConfig `_render._autoExposure*`), HDR 디스플레이 출력 `DisplayOutputDesc` 한 값에서 스왑체인 포맷 · 톤맵 변형(VQEngine, M~L), 볼류메트릭 구름 · 비 가림 맵(Adria).
- 도구: 엔진 안 MCP 서버는 들이지 않는다(사용자 결정 2026-11-19 — 지금 필요 없다), 선언형 조작기 · 시각화 속성(PROPERTY Shape), Fileserve(장치가 PC 콘텐츠를 TCP 로 — 개발 전용)(ezEngine).
- 작업 시스템(측정 먼저 — 계기는 됨: Tracy `Wait runParallel` 구간, 카운터 `Task.Join.waits` · `Task.Join.waitMicros` · `Task.Join.parks` · `Task.Idle.parks`, 아래는 그 수를 본 뒤): 묶음 제출 `submitBatch`, `runParallel` 우선순위 레인, guided 청크, 후속 하나 이어 실행, 넘침 보조 큐, 단계 그래프 위상 순서 재사용(Taskflow).
  - 선언형 모양 판단(2026-11-19): 새 어노테이션 키 대신 기존 커스텀 메타 `Meta = "Shape=Sphere|Box|Cone|Arrow, ShapeRange=<float32 프로퍼티>"` 를 프로퍼티마다 적는다 — 파서 · 런타임 메타데이터는 그대로이고 틀린 선언은 EditorTest 가 등록 타입 전수로 막는다(`PropertyShapeTest.EveryRegisteredDeclarationIsWellFormed`). 모양 선 · 손잡이는 선택한 오브젝트에만(ezEngine 조작기 · 유니티 Handles), 쓰기는 인스펙터와 같은 `PropertyEditRequest`, 끌기 하나가 되돌리기 하나. 편집 요청이 Panels 층이라 기능은 `Panels/Scene/` 에 둔다. 적용: 점광 반경 · 스폿 원뿔 · 수중 볼륨 상자 · 잎 영향 구 · 발사 방향 · 2D 빛 반경(`Circle`) · 캐릭터 컨트롤러 캡슐(`Capsule, ShapeOrigin=Base` — 발 기준) · 오디오 존(`ShapeAxes=World` 축 정렬, 상자 · 구는 프로퍼티 `EditCondition = "_shape == …"` 으로 갈린다 — 조건이 거짓인 모양은 그리지 않는다). 배율은 값을 쓰는 쪽의 규칙을 따른다 — 빛 · 오디오 · 부력 · 잎 영향은 미터 그대로라 보지 않고, 캐릭터 캡슐은 `ShapeScale=Physics`(3D: 반지름 max(X, Z) · 반 높이 Y) · `Physics2D`(반지름 X · 반 높이 Y)로 `PhysicsComponent::makeScaledShape` 와 같은 식을 쓴다. 손잡이는 끈 월드 거리를 그 배율로 나눠 저장 값으로 되쓰고, 배율 0 이면 손잡이가 없다. 손잡이 명령 순서는 콜라이더 손잡이와 한 틀(`EditorHandleDragTracker<THandle>`), 축 끌기 식은 `Ray::computeClosestAxisParameter` 하나. 옮기지 않은 것: 물리 셰이프(셰이프 목록 원소) · BoxCollider2D(모든 콜라이더를 늘 그리는 레지스트리 시각화). NavMeshObstacle 은 옮겼다(2026-11-20 — `Shape=Box|Cylinder, ShapeSelector=_shape, ShapeCenter=_center, ShapeAxes=Yaw, ShapeScale=Physics`).
- 측정 뒤에만: DX12 PipelineLibrary(차가운 시작 PSO 합), 깊이 전용 배치 키(그림자 드로우 수), 서버 MMO 관찰자 거리 기반 틱 주기 · 컴포넌트 TickInterval 버킷(XMachina · ezEngine), 서버 쿠킹의 축약 애니메이션 클립, 온라인 프로토콜 퍼저 타깃 ⑦(퍼징 1 단계 뒤).

**렌더 그래프 접근 핸들(설계 2026-11-19, 코드는 다음 갈래).** 지금 그래프 자원은 이름(`hashed_string`)이고 접근은 읽기 · 쓰기 둘뿐이라, 레벨 프롤로그가 첨부 텍스처만 이름으로 전이하고 버퍼 · UAV · 간접 인자 · 같은 버퍼의 연속 컴퓨트 쓰기는 패스 몸통이 손으로 낸다(렌더러 `transitionBuffer` · `prepareTexture*` · `uavBarrier` 약 100 곳 — 이번 HZB 가 6 곳을 더했다). 빠뜨리면 DX12 · Vulkan 에서만 지난 내용을 읽는다. 모양: Adria `RGTextureReadOnlyId` · `RGBufferReadWriteId`, 언리얼 RDG `FRDGTextureSRVRef` · `FRDGBufferUAVRef` 처럼 **접근 종류가 형식인 핸들**(`RGTextureSRV` · `RGTextureUAV` · `RGRenderTarget` · `RGDepthTarget<읽기 | 쓰기>` · `RGBufferSRV` · `RGBufferUAV` · `RGIndirectArgs` · `RGCopySource` · `RGCopyDest`)을 패스 선언이 받고, 패스 몸통은 그 핸들로만 디스크립터를 얻는다(`RGPassContext::getUAV( RGBufferUAV )` — 선언하지 않은 자원을 묶을 길이 없다). 그래프가 접근에서 RHI 상태를 정해(SRV → 셰이더 읽기, 비동기 큐는 큐가 받는 상태로 접는다, UAV → UAV, 간접 인자 → IndirectArgument) 레벨 프롤로그에서 텍스처와 버퍼를 한 묶음으로 낸다. 그래프 밖에서 사는 버퍼(GPUScene 컬링 인자 · HZB · 라이트 그리드 · 입자)는 프레임마다 `importBuffer( 이름, RHIStructuredBuffer&, 처음 상태 )` 로 들이고 끝 상태를 내보낸다(언리얼 RegisterExternalBuffer). 한 패스 안의 연속 디스패치(HZB 밉 사슬 · 컬링 → 정렬)는 `RGPassContext::uavBarrier( RGBufferUAV )` 하나로 남긴다 — SRV 핸들로는 부를 수 없다. 이전 차례(deprecation-and-migration): ① 핸들 · 버퍼 들이기를 이름 옆에 더하고 그래프 단위 테스트(UAV 쓰기 뒤 읽기에 전이 하나, 같은 레벨 읽기 여럿에 전이 0, 비동기 큐 상태)를 먼저, ② 프레임 패스(FrameGraphExecutor 27 · VFX 13 · HZB 6 곳)를 옮기고, ③ 뷰 패스, ④ 패스 몸통이 받는 명령 리스트 면에서 `transitionBuffer` · `prepareTexture*` 를 빼 그래프 실행기만 부르게 한다(형식으로 막아 손 전이가 돌아오지 못하게 — 린트가 아니다). 크기 L, 측정은 전이 수 · 프레임 GPU 시간 그대로(늘면 고친다).

## 엔진 전수 조사 판단(2026-11-20)

> 질문: 에디터 · 키트 · 성능 · 구조 전수 조사(13 갈래)에서 무엇을 어떤 차례로 고치는가?
> 결론: 결함에 가까운 것을 먼저, 구조 장치(형식으로 막기)를 다음, 측정이 문턱을 넘는 성능 후보는 그 뒤. 진행 중인 갈래(렌더 그래프 접근 핸들 · 자원 구간 할당 · HDR · Android · 리플렉션 내장 형식 표 · 시퀀서 · 카메라 프리셋 · 자막 · 런타임 UI · 상점) 위에 얹는다. 사용자 결정: 편의 매크로(`BLOCK` · `SW_DELEGATE_*` · `SW_DECLARE_DELEGATE`)는 남긴다 · 컨테이너 std 상속은 불필요하면 자체 구현 · 부르는 곳 없는 공개 API 는 지우지 않고 적용(중복 · 잔재 · 아무도 안 읽는 진단만 지움).

**1. 결함에 가까운 것(측정 없이 고친다).**
- `FileUtil::trimTrailingSlashes`(공개)와 내부 `trimTrailingSeparators` 의 규칙이 다르다(`C:/` → `C:`) — 쓰는 곳 68. 내부 규칙 하나로.
- 렌더러 지연 재사용을 프레임 수로 짐작하는 4 곳(`kMaxFrameCountInFlight` · `kRetireLag`) — `IRHIDevice` 계약은 펜스. 정확 크기 재생성(개수가 늘면 매 프레임) · 라이트 버퍼 두 번 올리기.
- 다시 읽기 함수가 `void` 라 실패도 `Reloaded` 로 기록(`AssetCacheReloadFunction`) → `ReloadResult`.
- 라이브 셰이더: 일부 실패면 `clearCache` 로 캐시 · PSO 전부 버림(이전 판 유지 없음), 셰이더 하나에 패스 자원 전부 동기 재생성, DDC 키의 define 이 받은 순서(헛 미스 · 두 벌), 도구 지문에 WGSL 꾸러미 · Tint · FXC 판 빠짐, `compileHLSL` 이 DDC 히트 전에 매번 가림 검사로 폴더를 재귀로 훑음.
- 목록 구분자가 자리마다 다름(`",; "` · `",;| "` · `" ,\t\n\r"`) — `XMLReadUtil::kListSeparators` 하나로.
- 머티리얼: 인스턴스 `enableKeyword` · `setMultiCompile` 이 아무 이름이나 받고 쿠커는 `.material` 만 훑어(`ShaderCookRequestCollector.cpp:441`) 인스턴스가 켠 조합을 굽지 않는다 — 배포에서 대체 해시로 그린다. 인스턴스의 Bool · 키워드 값 덮어쓰기가 define 을 바꾸지 않는다(`refreshCachedDefines`). `_base` 부모를 고쳐도 자식이 옛 값을 쥐는지(확인 필요). → 정적 변경은 부모가 선언한 런타임 스위치 색인으로만(`StaticSwitchParam` 형식), 값 API 는 정적 종류 거절.
- JNI 수정 UTF-8 은 `w-android` 가 고침. 실행 인자 `-fileserve` 의 `ArgumentList.xxx` 안 `#if` 는 아래 2 의 쓰는 구성 칸(`AndroidDev`)으로 바꿨다(`w-cmdargs`).

**2. 구조 장치(형식으로 막기).**
- 등록: 손 `*Registrar` 약 20 · 기능 헤더마다 배포 이중 정의(`SW_AUTOMATION_*` 7 번 재정의)를 `SW_STATIC_REGISTRATION` 틀 하나 + 배포 정책 인자(`Dev` · `Automation` · `Always`)로. Action · Condition 레지스트리 → `NamedEntryRegistry<T>`.
- 리플렉션: 내장 형식 한 곳 정본(`w-reflbuiltin`). 다음 — `tryParseEnum<E>` · 배포에도 남는 열거자 데이터 철자(`ENUMERATOR(Text=)`) · 반사 동등 비교 · 해시 · 기본값 · 기반 형식으로 하위 형식 생성 · `FUNCTION(Exec)` 콘솔. `StateIO::value(T&)` 를 반사 구조체에 닫아 칸 나열 손 코드(103 파일 · 723 줄)를 `io.reflected` 로. `.cpp` 안 `REFLECT`(최초 커밋부터 헤더만 입력)는 손 코드 이득은 작고 헤더로 새는 반사 스키마(약 227)를 줄이는 값 — Qt moc 꼴(`#include "Foo.gen.inl"`)로, 익명 이름공간 FQN 충돌 키를 먼저 정한다.
- 매크로: 쓰는 곳 0 · `SW_CONCAT` 사본 정리, 에셋 캐시 6 · 엔진 서비스 6 매크로를 하나씩으로. `SourceLocation`(`Core/Foundation/SourceLocation.h`, `__builtin_FILE/LINE/FUNCTION`) — Error · Warning 로그 · `SW_FATAL` 은 함수로(`%#` 컴파일 검사 길 먼저), Info 로그 · 단언 · 테스트 검사 · 프로파일은 매크로 몸만 얇게. `-fmacro-prefix-map` 추가.
- 실행 인자(됨 — `w-cmdargs`): `ArgumentList.xxx` 에 "쓰는 구성" 칸(`CommandLineArgumentUsage` — `All` · `Dev` · `Test` · `Server` · `AndroidDev`) — 정책(`CommandLineBuildPolicy`)이 받지 않는 줄은 자리만 두고 철자를 등록하지 않아 주면 알 수 없는 인자 오류로 기동이 멈춘다, 열거값은 모든 구성에 남김(읽으면 "주지 않음"). `Server` 는 클라이언트 전용 타깃이 받지 않는다(`build::kWithServerCode`). 배포 App · Server 가 스스로 굽는 `-cook-all` 길(`-cooked-dir` · `-pack-dir` · `-game` · `-pack-for-shipping` · `-pack-build`)은 `All` — 쿠킹이 `DevTools/Cook` 으로 옮겨 가면 `Dev` 로. `shipping-check` 는 Dev · Test · AndroidDev 철자 전부를 배포 App 에 주어 모두 거절되는지 본다(예전 `-scenario` 한 줄 확인을 대신). 대조: 언리얼은 `UE_BUILD_SHIPPING` 에서 개발 명령줄 처리와 `#if !UE_BUILD_SHIPPING` 콘솔 변수를 컴파일에서 빼 조용히 무시되고, 유니티는 Development Build 에서만 듣는 플레이어 인자를 배포 플레이어가 무시한다 — 여기는 빼되 무시하지 않고 거절한다(오타 · 잘못된 빌드에 개발 인자를 준 실행이 조용히 다르게 돌지 않게). 남은 틈: 서버가 클라이언트 전용 헤드리스 작업(`-cook-shaders` · `-import-*`)을 실행 시점에 거절하는 표(`HeadlessSubsystem::rejectClientOnlyTask`)는 `Client` 칸으로 같은 구조가 된다.
- Dev → DevTools: 빈 `Core/Diagnostics/Dev` · 낡은 `FolderLayout.md` 줄 정리, 게임 `Dev/*Autoplay.cpp` 10 개를 `SW_GAME_AUTOPLAY` 배포 무효로 접기, 엔진 개발 명령은 DevTools 로 · GF 치트는 `GameFrameworkDev` 로. 쿠킹 코드(`runCookTask` · `PackCooker` · `SceneCooker` · `ShaderCookDriver`)도 배포에 남음 → `DevTools/Cook`.
- 파일: 자기 `.cpp` 만 포함하는 60 줄 미만 헤더 65 개 접기, 한 곳만 쓰는 도우미 헤더 36 개 합치기(`header-fanin` 합이 늘면 안 함). 헤더끼리 합치기는 하지 않는다(늘 함께 포함되는 쌍은 1 쌍뿐). 테스트 위치는 그대로(같은 대상 안 컴파일 — 내부 접근 · 층 규칙 · 모듈별 등록, 크로미움 곁 배치와 같은 판단).
- 렌더러 규격(접근 핸들 · R-4 뒤): 그래프 일시 버퍼(`graph.createBuffer`) → 뷰 히스토리, `RHITextureSlot`(텍스처 손 생성 · 뷰 등록 8 곳 — HDR 뒤), 컴퓨트 패스 도우미(디스패치 21 · 그룹 크기는 셰이더 `[numthreads]` 리플렉션), 풀 버퍼(펜스 재사용), 되읽기 단일 면. RHI 자원 생성은 패스키로 막는다.
- 쿠킹: 셰이더 컴파일을 `CookJob` 으로 감싸 증분 판정 세 벌(`.cookrec` · `cook.stamp`+`.deps` · 메모리 지문)을 하나로, 셰이더 DDC 를 `_derivedData` 층으로(구성마다 따로인 것 · 공유층 · 정리 밖). `DerivedDataCache` 생성자 비공개 + `open(bucket)`.
- 파일 감시: OS 감시자 생성은 `FileWatchService` 만(열쇠 형식) — 루트마다 하나 공유, 프레임 안 경로별 합치기, 안정화 창. 다시 읽기 알림 · 텍스처 다시 읽기를 프레임 단위 일괄로.
- 머티리얼: 파라미터 핸들(`MaterialParameterHandle{index, offset, kind, layoutGen}` — 지금 매번 `findProperty` 선형 · 사본마다 부모 바이트 전체 복사 · 전체 업로드) → 바뀐 칸만 덧칠 · 범위 업로드. 전역 값(식생이 층마다 매 틱 바람 · 시간 · 카메라를 `setVector` → 매 프레임 사본 재생성)과 `MaterialParameterCollection`(머티리얼마다 글로 다시 패킹)을 전역 CB 로(`MaterialGlobals` 형식). 인스턴스 모델 셋(`MaterialInstanceDef` 에셋 Content 0 개 · `_base` 자식 · 실행 중)을 MIC=`_base` · MID=`create` 둘로. `_permutations` 를 이름 단위 합치기. 형식 표(`MaterialPropertyKind` 25 · 패킹 분기 132)를 리플렉션 내장 형식 특성에 잇기. 색 하나 바꾸려 만든 인스턴스는 `InstanceCustomData` 로.
- 컨테이너: `string` 자체 구현(부모 형식 누출 함정), `list` 는 LRU 전용 형식으로 바꾸고 지움. 레이스 가드는 먼저 인라인 + 스레드 id 캐시(탐지 범위 그대로), 별도 깃발로 떼기는 측정 뒤(뗄 때 그 깃발 체인 하나를 preflight 에).
- 디버그 허브: `DebugHUDRegistry`(글줄 HUD 범주 · 선택 대상)를 범주 단위로 넓혀 그리기 · 감시 값 묶음 · `VisualLogger`(생산자 0 — 비어 있음) 기록 · 에디터 패널을 함께. `gv_*DebugDraw` 깃발을 범주 켜짐으로, 에디터 패널이 `getTarget` 공유, 사람용 감시 값 표. 먼저 붙일 것: 내비 · AI, 물리, 렌더링, 네트워크, UI · 입력. 진단 getter 118 개는 살림(실패 · 적체 · 큐 크기 → 범주 줄)과 지움(테스트용 중복 · 에디터 내부 캐시 · 일회성 깃발)으로 가르되 감시 값 · 로그 참조를 다시 센다(표본 7 중 2 가 쓰이고 있었다).

**3. 에디터 · 키트.** 빈 곳(영향 순): VFX 편집기(미리보기만), 폴리지 칠하기, 스플라인 점 손잡이, 물리 에셋 시각 편집, 패키징 플랫폼 선택, 로컬라이제이션 패널. 키트: "꽂으면 도는" 컴포넌트 층(데모 래퍼를 키트 `Component/` 로 — Lyra Game Feature 자리), 테마파크 배치 패널을 표본으로 카탈로그 편집 · 미리보기 일반화 + Map Check 키트 검증 등록점, 새 게임 · 장르 키트 생성기, 키트 README. 서버만 있고 쓰는 게임이 없는 키트(Social · Trade · LiveOps · ServerDirectory · NetTurnRelay)는 지우지 않고 붙인다. 정리 검토: GF 본체 `Gimmick/Genre/*` 를 키트로, 호러 4 종 공통(은신 · 소음 · 조명) 합치기.

**4. 성능(측정 뒤 — S8 줄과 같은 규칙).** 동기 대기(히치)가 처리량보다 먼저: 프레임 단계 안 동기 로드 경고 · 렌더 스레드 디스크 IO 단언을 구조로 먼저 넣고, VAT 굽기 · Vulkan 파이프라인 캐시 저장 · 파괴 셰이프 · 프레임 경로 동기 에셋 로드 5 곳을 비동기로. 문자열: 텍스처 스트리밍의 머티리얼 경로 복사 · 글꼴 체인 키 · 프리팹 스폰 경로 정규화 · 캐릭터마다 이름 intern — 유틸에 버퍼 받는 판, 핫 경로 API 는 `string_view` · `hashed_string` 만. 리플렉션 로드 경로 칸 형식 해석을 `PropertyInfo` 캐시로. 컨테이너: Swiss table 꼴 색인 · 정수 해시 곱셈 · `m[sv]` 임시 문자열. 숫자 확인: NEXT "생성 코드 1.65 MB" 대 지금 빌드 트리 9.33 MB — 측정 범위부터 맞춘다.

### 스트리밍 셀 경계 정합 · 저장(설계 2026-11-20)

**문제.** 셀 소속이 쿠킹 때 고정된다 — `Scene::unloadCell` 은 셀이 지은 id 목록만 지운다. 그래서 (1) 셀 A 의 NPC 가 B 로 걸어간 뒤 A 가 내리면 NPC 도 같이 지워지고 A 가 다시 오를 때 A 자리에 돌아온다, (2) 런타임 스폰은 어느 셀에도 들지 않아 영영 내리지 않는다, (3) 바닥(셀의 정적 바디)이 내려가도 그 위 늘 올라 있는 동적 물체가 남아 떨어진다, (4) 내릴 때 떠 둔 상태(`captureCellNodes`)는 목적 `Authoring` 이라 `RuntimeState` 칸(활성 볼륨 발동 수 · AI 블랙보드)이 빠지고, 메모리에만 있어 세이브에 실리지 않는다. 게다가 세이브 복원이 매니저를 비우면(`GameObjectManager::clear` → `onSceneCleared`) 스트리밍이 셀을 통째로 버려 그 뒤로 아무 셀도 오르지 않았고, 다른 실행의 세이브로 새 id 를 받은 오브젝트는 씬의 파일 id 표에서 빠져 셀이 늘 올라 있는 오브젝트를 파일 id 로 찾지 못했다.

**대조.** 언리얼 World Partition: 액터의 셀은 에디터 · 쿠킹 때 자리로 정해지고(`bIsSpatiallyLoaded` 를 끄면 늘 올라 있음) 런타임에 셀을 옮기지 않는다 — 걸어 나간 액터는 원래 셀과 함께 내린다. 런타임 스폰 액터는 스트리밍 밖(퍼시스턴트 레벨)이라 내리지 않는다. 내린 셀의 상태는 엔진이 지키지 않는다 — 게임이 `SaveGame` 칸 · World Partition 세이브 플러그인으로 직접 싣는다. 유니티 Additive 씬: 새 오브젝트는 활성 씬에 들고, 다른 씬으로 옮기는 것은 게임이 `SceneManager.MoveGameObjectToScene` 으로 명시한다(씬을 내리면 그 씬의 것이 모두 지워진다). Godot 은 셀 스트리밍이 없다(게임이 노드를 붙이고 뗀다). → 엔진 셋 모두 "옮기기 · 세이브는 게임 몫" 이다. 우리는 NPC · 물리가 셀을 넘는 열린 월드를 키트가 바로 쓰므로 그 몫을 엔진에 둔다 — 규칙은 하나(자리가 곧 소속)로 쿠킹과 같게 한다.

**결정.**
1. **소속은 내릴 때 자리로 다시 정한다**(`SceneStreamingSubsystem` — 유니티 `MoveGameObjectToScene` 을 엔진이 내릴 때 부르는 꼴). 셀을 내리는 프레임에 그 셀이 든 루트 오브젝트마다 지금 자리(primary 씬 컴포넌트, 쿠커와 같은 식)의 셀을 다시 본다. 같으면 그 셀로 떠 두고, 다르면 같은 레이어 집합의 그 셀 조각으로 옮긴다 — 올라 있으면(또는 반경 안이라 지금 만든 조각이면) id 만 옮겨 살려 두고, 내려가 있으면 그 오브젝트를 떠서 그 조각의 떠 둔 상태에 합친다(쿠킹 내용만 든 조각은 "들어온 엔티티" 목록에 두었다가 다음 짓기에서 같은 배치로 짓는다). 조각이 없으면 만든다(조각은 `unique_ptr` 로 들어 짓는 중인 조각의 주소가 흔들리지 않는다). 셀을 넘는 참조는 약한 참조라 갈라지면 빈 핸들로 이어진다(배치 규칙 그대로).
2. **런타임 스폰은 자리의 셀에 든다**(유니티의 "새 오브젝트는 활성 씬에" 와 같은 쪽, 언리얼과는 반대). 늘 올라 있음 표시가 없고(`GameObject::_bAlwaysLoaded` — 놓은 오브젝트와 같은 데이터 칸) 자리가 있는 루트 오브젝트 중 어느 셀에도 들지 않은 것은 셀을 내리는 프레임에 반경 밖이면 그 자리 셀에 들어 함께 떠 둔다(그때 파일 id 를 준다 — 새 id 는 쿠킹본 머리의 셀 최대 파일 id 위에서 고른다, `SceneDocument::_maxCellFileID` · 쿠킹본 판 10). 레이어 묶음 오브젝트 · 파생 오브젝트(`isTransient`)는 뺀다.
   **복제 스폰은 넣지 않는다**: 네트워크 생성 번호를 받은 오브젝트(`NetObjectTable::registerSpawned` — MMO 엔티티 · 투사체처럼 서버가 번호를 매겨 클라이언트와 잇는 것)는 셀과 함께 지우면 복제가 끊긴다. 존재는 서버가, 클라이언트 보임은 복제기의 관심 영역(관찰자 반경, `MMOReplicator`)이 정한다(언리얼: 복제 액터는 스트리밍 밖, 관련성은 Replication Graph). 또 원점이 하나도 없으면(관찰자 없는 서버) 스폰을 넣지 않는다 — 셀은 원점이 덮지 않는 자리에서만 스폰을 내린다. 플레이어 · 카메라처럼 늘 있어야 하는 스폰은 프리팹에 표시를 켠다 — 언리얼처럼 기본을 끄면 놓은 것과 스폰한 것의 규칙이 둘이 된다.
3. **바닥이 없으면 물리를 쥔다.** 자리 셀에 문서의 조각(켜진 레이어)이 있는데 올라 있지 않으면(읽는 중 · 짓는 중 포함) 바닥이 없다(`SceneStreamingSubsystem::isFloorMissing`). 스트리밍(층 5)은 물리(층 6)를 모르므로 물리가 묻는다 — `ScenePhysics::updateStreamingHolds` 가 프레임마다 바닥이 필요한 컴포넌트(`PhysicsComponent::needsStreamingFloor` — 동적 강체 · 캐릭터 컨트롤러)를 쥐어(`isStreamingHeld`) 시뮬레이션 밖으로 빼 바디를 놓고, 셀이 오르면 풀어 지금 트랜스폼에서 다시 만든다(속도는 0 에서 다시). 조각이 없는 셀(늘 올라 있는 지형만)은 쥐지 않는다. 셀이 내려간 후보에만 몸 아래 광선(`PhysicsSettings::_streamingFloorProbeDistance` — 데이터 필수, 땅 광선 층, 제 몸 제외)을 쏴 받쳐 줄 충돌(늘 올라 있는 지형 · 정적 바디)이 닿으면 쥐지 않는다 — 순간이동으로 남겨진 캐릭터가 땅 위에서 서지 않는다. 상용 엔진에는 기본 장치가 없다(로딩 반경을 물리보다 넓게 잡는 운영 규칙) — 반경 밖으로 굴러 나간 물체가 사라지지 않게 하는 최소 장치다.
4. **떠 둔 상태는 목적 `Snapshot`** 이다 — `RuntimeState` 를 싣고 `Transient` 는 뺀다. 엔티티마다 읽을 목적을 든다(`SceneObjectNode::_statePurpose`, 런타임 칸 — 씬 파일 · 쿠킹본에는 쓰지 않는다). 쿠킹 내용은 `Authoring`. 프리팹 엔티티는 원형과의 차이(런타임 칸 포함)로, 그 밖은 전체 상태로 적는다.
5. **세이브에 싣는다**(봉투 판 7). 씬 오브젝트 섹션은 셀이 든 오브젝트(와 그 자식)를 빼고, 넷째 섹션에 씬 몫 — 늘 올라 있는 오브젝트의 파일 id 표(찍은 id → 파일 id)와 조각마다 쿠킹 내용에서 달라진 상태(올라 있으면 지금 떠서, 내려가 있으면 떠 둔 것, 들어온 엔티티)를 싣는다. 복원은 매니저를 비우면 스트리밍이 조각을 쿠킹 내용으로 되돌리되 월드는 놓지 않고(`onSceneCleared`), 오브젝트를 다시 만든 뒤 씬이 파일 id 표를 새 id 로 다시 잇고(`Scene::rebindRestoredFileIDs`) 조각에 세이브의 상태를 얹는다(`SceneStreamingSubsystem::applySaveState`) — 다음 갱신이 원점 둘레를 그 상태로 짓는다.
6. **다시 오를 때 차례.** 셀 짓기는 엔티티를 모두 숨긴 채 만들고 → 배치를 이어(부착 · 핸들) → 한 번에 드러내며 등록한다(물리 바디 · 내비 · AI 는 등록 · 시작 뒤에 서로를 찾는다). 들어온 엔티티도 같은 배치다 — 따로 지으면 먼저 선 쪽이 아직 없는 쪽을 찾는다.
7. **개발 실행의 셀.** 셀은 쿠킹본에만 있어 소스 콘텐츠 시나리오는 스트리밍을 밟지 못했다. 언리얼 PIE 가 에디터 데이터로 World Partition 을 스트리밍하듯, 개발 변수 `gv_sceneStreamSourceCells=1` 이면 씬 로드가 소스 문서를 메모리에서 셀로 나눈다(`SceneCooker::splitStreamingCells`) — 기본은 꺼짐(지금 시나리오 · 골든 그대로).
8. **데이터 레이어 묶음도 같은 장치다**(`SceneDataLayerSubsystem`): 레이어를 끌 때 묶음 오브젝트를 Snapshot 으로 떠 두고(`_listHeldNode` — 문서 `_listNode` 는 저작 저장이 그대로 쓴다) 다시 켜면 그것으로 짓는다. 세이브 복원의 매니저 비우기는 묶음 · 켜진 레이어를 지키고(씬을 내리는 길은 `endScene`), 세이브 몫에 켜진 레이어 · 묶음 상태를 싣고 씬 섹션은 묶음 오브젝트를 뺀다(`isGroupObject`). 엔티티+목적 목록 형식은 `SceneDocument::writeSavedNodes` 하나.
9. 셀이 내려갈 때 트리거 Exit 규칙은 트리거 · 충돌 채널 갈래(`w-trigger`)가 정한다 — 여기서는 오브젝트를 지울 뿐이다.

**남는 위험.** (1) 바닥 광선은 바디 자리에서 아래 한 줄이다 — 비탈 · 가장자리에 걸친 큰 바디는 받쳐 줄 것을 못 보고 쥘 수 있다(사례가 나오면 셰이프 스윕으로). (2) 복제 번호가 없는 서버 스폰(게임이 번호를 매기지 않은 NPC)은 관찰자 반경 밖에서 셀과 함께 내린다 — 늘 있어야 하면 `_bAlwaysLoaded` 를 켜거나 복제 번호를 매긴다. (3) 상태 스냅숏의 컴포넌트 상태 섹션(`ComponentStateStore` — PROPERTY 밖 상태)은 셀 오브젝트의 것을 되돌려 줄 곳이 복원 순간에 없다 — 셀 안에 PROPERTY 밖 상태를 가진 컴포넌트(지형 편집)는 아직 세이브를 넘지 못한다. (4) 쿠킹본 판이 9 → 10 이라 다시 쿠킹하기 전의 쿠킹본은 읽지 않는다(기존 규칙 — 오류로 알린다).

**확인.** 단위(`SceneStreamingTest`): 걸어 나간 오브젝트가 올라 있는 셀로 옮겨 살아남음 · 내려간 셀로 합쳐져 그 셀과 함께 다시 오름, 런타임 스폰이 자리 셀에 들어 함께 내리고 오름, 바닥 셀이 없으면 쥐고 오르면 풂, 런타임 칸이 내렸다 올려도 남음, 세이브 섹션 왕복(내려간 셀의 바뀐 상태 · 올라 있는 셀). `StreamingFloorHoldTest`: 내려간 셀 위 동적 바디는 쥐여 그 높이, 오르면 풀려 떨어짐. 시나리오 `game.wildwest.cellstate`(prairie 를 물려받은 무대, 16 m 셀 · 32 m 반경): NPC 가 A→B 로 간 뒤 A 를 내려도 하나 · 기믹(문 자리 · 상자 발동 수 — `RuntimeState`)이 내렸다 올라도 남음 · 세이브 · 로드 뒤 복원 · 바닥 셀이 내려간 동안 물체 높이 그대로.

**런타임 하위 레벨 · Packed Level · 원점 이동(설계 2026-11-20, 남은 갈래 (가)).** 대조: 언리얼은 `ULevelStreamingDynamic::LoadLevelInstance`(레벨을 변환과 함께 더함 — 이름이 패키지 이름을 정해 서버 · 클라이언트가 맞춘다) · Level Instance / Packed Level Actor(정적 메시를 ISM 으로 구움, 에디터는 원본 레벨 편집) · UE4 World Origin Rebasing(`SetNewWorldOrigin` 이 액터 · 물리 · 내비를 옮김, 서버는 옮기지 않고 복제 좌표를 `RebaseOntoLocalOrigin` 으로 바꿈 — UE5 World Partition 은 이것을 버리고 LWC), 유니티는 Additive 씬(`LoadSceneAsync(Additive)`) · floating origin 스크립트, Godot 은 노드를 붙이고 떼고 큰 월드는 배정밀도 빌드다. 결정: (1) 하위 레벨 = 실행 중에 놓은 씬 인스턴스(`SceneSubLevelSubsystem`) — 월드는 하나, 앵커(이름에서 정한 넓은 파일 id · 늘 올라 있음) 아래로 펼치고 셀과 같은 시간 나눈 짓기, 셀에 들지 않는다. 세이브는 데이터 레이어 묶음과 같은 꼴(씬 섹션에서 빼고 하위 레벨마다 Snapshot 상태), 봉투 판 8. 더하기 · 빼기는 프로세스마다 — 같은 이름이면 id · 넷 씬 번호가 같아 서버 · 클라이언트가 각자 더해도 맞는다. 셀 · 레이어로 다시 나뉘는 씬은 하위 레벨로 받지 않는다. (2) Packed Level 은 내부 씬의 `SceneSettings::_packing="StaticMeshes"` — 쿠커(`SceneCooker::packStaticMeshes`)가 앵커의 직접 멤버 중 메시 컴포넌트 하나 · `_bStatic` · 사용자 값 없음 · 가리켜지지 않음을 (주인, 레이어, 메시, 머티리얼, 구간)마다 `InstancedMeshComponent`(ISM) 하나로 바꾼다. 씬(5)은 메시(6)를 몰라 타입 표 이름으로 읽고 쓰며 없으면 쿠킹 오류. 그리기만 묶는다(충돌 있는 엔티티는 그대로). (3) 원점 이동은 UE4 모델을 클라이언트만 — `Scene` 이 원점(double3)을 들고 문턱(`_originRebaseDistance`, 0 이면 끔)의 배수로 XZ 만 옮긴다. 서버(바깥 원점) · 세이브 · 쿠킹본 · 셀 격자 · 하위 레벨 자리는 절대 좌표, 엔티티마다 적힌 원점(`SceneObjectNode::_worldOrigin`)으로 지을 때 맞춘다. 물리는 루트 순간이동을 따르고 오디오 · 렌더는 매 프레임 오브젝트를 읽는다. 내비메시가 있으면 옮기지 않는다(`ISceneService::canShiftWorldOrigin` — Detour 타일 원점을 옮길 API 가 없다). 남는 위험: 내비 · 군중이 원점 기준 질의를 받기 전까지 길찾기 월드는 원점을 옮기지 못한다, 월드 공간 파티클 · 공간 격자(`SceneSpatialGrid`) · 디버그 그리기의 원점 원 · MMO 클라이언트가 받은 절대 자리를 오브젝트에 놓는 게임 코드는 원점을 빼야 한다(지금은 손으로), 원점 이동 한 번에 모든 바디를 옮겨 큰 씬은 그 프레임이 무겁다, 하위 레벨 문서 읽기는 게임 스레드 동기다.

### 멀티플레이 스트리밍 소스 · 관련성(설계 2026-11-20)

> 질문: 스트리밍 월드에서 서버는 어느 셀을 올리고, 관련성은 셀과 어떻게 맞물리며, 클라이언트는 아직 오르지 않은 셀의 복제 엔티티를 어떻게 다루는가?
> 결론: 서버 셀 = 관찰자 소스(위치 · 반경 · 우선순위) 반경의 합집합, 관련성 격자 = 스트리밍 격자. 클라이언트는 셀이 다 오를 때까지 복제 엔티티를 보류하고, 셀에 놓인 엔티티는 쿠킹 파일 id 로 맞춰 스폰하지 않는다. 클라이언트가 셀 상태를 서버에 알리지 않는다.

**대조.**
- 언리얼 Replication Graph: Spatial2D 노드(격자 칸마다 액터 목록 — 연결은 둘레 칸만 모은다) · AlwaysRelevant 노드(전역 목록) · 연결별 노드(AlwaysRelevant_ForConnection — 자기 폰 · 파티). 우리: 칸 = `NetInterestGrid`(칸 = `StreamingCellGrid` 셀), 전역 = `_setAlwaysRelevant`, 연결별 = `IInterestPolicy::isAlwaysRelevant( connection, … )` — 이미 있다.
- 언리얼 World Partition 서버 스트리밍: 플레이어 컨트롤러마다 스트리밍 소스(`FWorldPartitionStreamingSource` — 위치 · 모양 반경 · `EStreamingSourcePriority` 0 이 가장 먼저), 서버는 모든 소스의 합집합을 올린다. 우리: `StreamingSource{ 위치, 로딩 반경, 우선순위 }`, 우선순위는 언리얼과 같은 다섯 단계 · 같은 차례(Highest · High · Normal · Low · Lowest). 읽기 차례는 덮는 소스 중 가장 급한 우선순위 → 가까운 거리, 내리기는 모든 소스에서 (그 소스 반경 + 여유) 밖.
- 정적 액터 NetGUID: 맵에서 읽은 액터(bNetStartup)는 경로 이름으로 서버 · 클라이언트가 같은 객체를 가리킨다 — 클라이언트는 새로 스폰하지 않고 이미 읽은 액터에 묶고, 관련성으로 채널이 닫혀도 지우지 않는다. 못 풀면 스폰하지 않고 경고한다. 우리: `MMOEntity::_staticID` = 쿠킹된 씬의 엔티티 파일 id(같은 쿠킹본이라 양쪽이 같다).
- 레벨을 아직 안 올린 클라이언트: 언리얼은 클라이언트가 `ServerUpdateLevelVisibility` 로 보이는 레벨을 알리고, 서버는 그 레벨의 액터 복제를 미룬다(액터가 레벨 객체에 속해 레벨 없이 스폰할 수 없기 때문).

**결정.**
1. 소스(`Engine/Scene/StreamingCellGrid.h` 의 `StreamingSource` · `StreamingSourcePriority`): `SceneStreamingSubsystem::setExternalSources( span<const StreamingSource> )` 는 위치가 유한하지 않거나 반경이 유한한 0 이상이 아니면 오류로 거절한다. 위치만 받는 입구는 두지 않는다(부를 때의 반경을 박아 두면 월드가 바뀌어도 낡은 반경이 남는다) — 카메라 원점만 격자 로딩 반경 · Normal 이다. 셀 번호는 ±2^30 에서 포화하고, 범위 셀 수가 받은 셀 수보다 많은 큰 반경은 받은 셀을 훑는다(믿을 수 없는 좌표 · 반경이 서버를 멈추지 않게). `getActiveSources` 가 소스를 돌려주고 내비 타일 스트리밍도 소스 반경을 쓴다 — 셀과 타일이 다른 반경으로 오르지 않게.
2. 서버 관찰자: `MMOReplicator::setObserver` 의 소스 반경은 격자 로딩 반경, 우선순위 Normal. `setObserverStreaming( 연결, 반경, 우선순위 )` 로 바꾸되 반경 < 나가는 반경이거나 Config 상한(`MMOReplicatorConfig::_maxStreamingRadius`, 0 이면 바꾸지 못함)을 넘으면 거절한다 — 보이는 엔티티가 서버에 오르지 않은 셀에 있을 수 없다(`initialize` 의 같은 규칙을 관찰자마다). 서버의 오른 셀 = 관찰자 반경의 합집합.
3. 클라이언트 보류(`MMOClientView` + `INetClientWorld`, 씬 구현 `MMOSceneClientWorld`): 새로 보임의 자리 셀이 다 오르지 않았거나(`SceneStreamingSubsystem::isCellStreamingCompleted` — 그 좌표의 문서 조각(`_bFromDocument`) 가운데 켜진 레이어 조각이 모두 Loaded · Failed, 조각 없는 셀 · 옮겨 온 오브젝트용 조각만 있는 셀은 끝난 것 — 물리의 `isFloorMissing` 과 같은 질의 하나) 정적 엔티티의 셀 오브젝트가 아직 없으면 보류한다. 보류 중 갱신은 틱 규칙대로 들고, 사라짐이면 사건 없이 버린다. `update( 초 )` 가 다 오른 것을 놓고, 제한 시간(`MMOClientViewConfig::_cellHoldTimeoutSeconds` — 기본값 없음, 0 이면 `setWorld` 가 거절)이 지나면 경고한 뒤 동적은 놓고(영원히 숨기지 않는다 — 서버 나가는 반경이 클라이언트 로딩 반경보다 큰 설정 결함의 신호) 정적은 경고만 하고 사라짐이 올 때까지 보류한다(중복 스폰보다 안 보임이 낫다, 언리얼과 같다). 보류 · 보임은 한 표의 단계(`ClientEntity::_state`)라 한 id 가 둘에 동시에 있을 수 없고, 이미 아는 id 의 새로 보임은 상태만 새로 한다. 한 셀 오브젝트에 두 번째 묶기는 받지 않는다. 월드를 주지 않은 보기(LanternVale — 스트리밍 씬이 아니다)는 동적을 지금처럼 곧바로 놓고, 월드를 떼면(`detachWorld`) 보류한 동적이 보인다.
4. 정적 엔티티 사건은 `Bound` · `Unbound`(셀 오브젝트 id 를 싣는다)로 `Entered` · `Left` 와 종류부터 다르다 — 게임이 `Entered` 에만 스폰하면 정적 엔티티를 스폰할 길이 없다(형식으로 막음). 월드 없이 정적 id 가 오면 오류를 남기고 월드가 걸리거나 사라짐이 올 때까지 보류한다.
5. 클라이언트 → 서버 셀 상태 보고는 두지 않는다. 우리 엔티티는 셀 오브젝트에 속하지 않아 셀 없이도 상태를 들 수 있고(보류), 보고를 두면 서버가 믿을 수 없는 입력(가짜 "안 올랐음" 으로 복제를 피하기 · 셀 목록 폭주)을 검증해야 하는 공격면만 생긴다. 대가: 보류 중에도 갱신 대역폭이 든다 — 보류는 텔레포트 직후 몇 틱이다.

**남은 위험.** 우선순위는 읽기 차례만 정한다(언리얼처럼 소스마다 목표 상태 Loaded · Activated 를 두지 않는다). 부모(붙은 오브젝트)를 기다리는 보류는 없다 — MMO 엔티티에 부모 칸이 없다. 두 프로세스 짝 시나리오는 스트리밍 씬을 쓰는 게임이 생길 때(LanternVale 은 격자만) — 그때까지 같은 경로를 루프백 끝단 두 클라이언트 · 서버 · 클라이언트 씬 셋으로 단위 테스트가 돈다(`NetMMOStreamingTest`).

### 씬 스트리밍 (라) HLOD · 우선순위 공유 · 로드 큐 디버그(설계 2026-11-20)

> 질문: 셀이 내려간 먼 영역을 무엇으로 그리고, 셀 로드와 텍스처 · 메시 스트리밍은 어떤 차례를 함께 쓰며, 로드 큐는 어디서 보는가?
> 결론: HLOD 는 셀 조각마다 정적 메시를 인스턴스 메시로 모은 대리(언리얼 HLOD 레이어 `Instancing`)를 쿠커가 굽고, 조각이 올라 있지 않을 때만 켠다. 우선순위는 스트리밍 소스 우선순위 하나(`StreamingSourcePriority`)를 셀 읽기 · 텍스처 밉 줄 · 두 쪽 IO 큐가 함께 쓴다. 로드 큐는 감시 값과 개발 명령 `world.streamqueue` 로 본다.

**대조.**
- 언리얼 World Partition HLOD: HLOD 레이어 종류 `Instancing`(원본 메시를 인스턴스로 — 굽기 없음) · `MeshMerge`(합친 메시 + 머티리얼 굽기) · `MeshSimplify` · `MeshApproximate`(임포스터에 가까운 근사). 런타임 셀마다 HLOD 액터 하나가 있고 그 셀이 오르면(Activated) 숨긴다. HLOD 액터는 데이터 레이어를 따른다. 텍스처 스트리머는 화면 크기(wanted mip)로 차례를 정하고 World Partition 은 스트리밍 소스 우선순위로 셀 차례를 정한다.
- 유니티: HLOD 는 패키지(`com.unity.hlod`)로 셀이 아닌 공간 분할 노드마다 합친 메시를 굽는다. Mipmap Streaming 은 카메라 거리 · 화면 크기로 차례를 정한다.
- Godot: HLOD 가 없다(visibility range 로 손으로 갈음).

**결정.**
1. **대리 = 인스턴스 메시(Instancing).** 쿠커가 셀을 나눌 때(`SceneCooker::splitStreamingCells` — 쿠킹과 소스 셀 나누기 `gv_sceneStreamSourceCells` 가 같은 길) 조각마다 정적(`_bStatic`) · 보이는 `MeshComponent` 를 (메시, 머티리얼, 구간)마다 `InstancedMeshComponent` 하나로 모아 대리 엔티티 하나(원점의 씬 컴포넌트 아래, 인스턴스 자리는 월드 자리, `_bAlwaysLoaded`, 조각의 레이어)를 기본 목록에 더하고 조각에 그 파일 id 를 적는다(`StreamingCell::_hlodFileID`, 쿠킹본 판 11). 이름은 `HLOD_<x>_<z>[_레이어]`. 합친 메시 · 임포스터는 머티리얼 굽기(텍스처 아틀라스)가 필요해 지금 들지 않는다 — 인스턴스 대리는 Packed Level 의 인스턴스 메시를 그대로 써 새 굽기가 없고, 드로우는 (메시, 머티리얼)마다 하나다. 그리기 수가 문제가 되면 `MeshMerge` 를 다음 단계로 더한다(같은 `_hlodFileID` 자리).
2. **켜고 끄기.** `SceneStreamingSubsystem` 이 갱신 끝(짓기 뒤)에 조각마다 대리를 조각이 `Loaded` 가 아닐 때만 켠다(`GameObject::setActive`, 바뀐 것만). 내린 조각 · 읽는 중 · 짓는 중 · 실패한 조각은 대리가 그린다 — 짓기가 여러 프레임에 걸쳐도 구멍이 나지 않는다. 대리는 거리로 따로 전환하지 않는다 — 셀 반경이 곧 전환 거리다(언리얼도 셀 Activated 가 HLOD 를 숨긴다). 대리는 충돌 · 내비 입력이 없다(그리기만).
3. **우선순위 공유.** 형식은 `Engine/Asset/StreamingSourcePriority.h`(텍스처 캐시가 씬보다 아래 층이라 둘이 함께 보는 에셋 층에 둔다). 셀: 읽기 차례 = 덮는 원점 중 가장 급한 우선순위 → 거리(전과 같다), 셀 내용 읽기의 IO 우선순위 = `toAsyncIOPriority( 그 우선순위 )`. 텍스처: `TextureStreamingSystem` 이 메시마다 화면 크기와 함께 그 자리의 우선순위(`SceneStreamingSubsystem::computePriorityAt` — 그 자리 셀의 셀 우선순위, 같은 함수)를 `TextureCache::recordStreamingDemand` 로 알리고, 위쪽 밉 줄은 내리기 → 우선순위 → 화면 크기 차례이며 밉 구간 읽기의 IO 우선순위도 같은 변환이다. 원점 반경 밖 메시(대리가 그리는 먼 영역)는 `Lowest` 라 뒤에 선다. 스트리밍하지 않는 씬은 모두 `Normal` 이라 전과 같다. 메시: 메시 LOD 스트리밍은 없다 — 메시 읽기는 셀을 지을 때 컴포넌트가 걸므로 셀 짓기 차례(같은 키)를 따른다.
4. **로드 큐 디버그.** 감시 값 `Scene.PendingCellCount`(원점 반경 안인데 아직 오르지 않은 조각 — 갱신의 정렬 키를 그대로 읽는다) · `Scene.HLODCount` · `Scene.HLODShownCount` · `Texture.StreamingQueueCount`(기다림 + 읽는 중 + 업로드 중), 개발 명령 `world.streamqueue`(셀 줄: 좌표 · 레이어 · 우선순위 · 거리 · 단계, 텍스처 줄: 키 · 우선순위 · 밉, 앞 16 개). 에디터 패널은 두지 않는다 — Streaming Cells 창은 쿠킹 전 나누기 미리보기이고 큐는 실행 중 값이라 개발 명령 · HUD 자리다.

**확인.** 단위 `SceneStreamingHLODTest`(조각이 오르면 대리 꺼짐 · 내리면 켜짐 · 다 짓기 전에는 켜진 채 · `computePriorityAt` 가 셀 우선순위와 같음 · 로드 큐 차례 · 단계), `StreamingHLODCookTest`(정적 메시 조각만 대리 · 움직이는/숨긴/늘 올라 있는 메시 제외 · 인스턴스 월드 자리 · 스케일), `TextureStreamingDemandTest`(요구 합치기 · IO 우선순위 접기). 시나리오 `game.playground.streaminghlod`(소스 셀 나누기, 카메라가 셀 사이를 오가며 대리 수 · 큐가 빔을 감시 값으로 — 골든 없음).

**남은 위험.** 대리 인스턴스는 원본의 가장 높은 LOD 그대로다(언리얼 Instancing 은 최소 LOD 를 고른다 — 인스턴스 메시에 LOD 고정 칸이 생기면 쓴다). 중첩 행렬의 비균일 스케일 + 회전(전단)은 TRS 로 나눌 때 잃는다. 대리는 XML 상태로 쿠킹본에 실린다(바이너리 상태 쿠킹 뒤에 만든다). 데이터 레이어 조각마다 대리가 따로라 같은 좌표의 여러 레이어 조각은 대리도 여럿이다. 쿠킹 판 11 이라 다시 쿠킹하기 전의 쿠킹본은 읽지 않는다(기존 규칙).

### 허브 게임 · 미니게임 Experience(사용자 결정 2026-11-20)

**결정(사용자).** `Source/Games/*` 의 게임들은 키트를 시험하려고 만든 것이라 폐기하고 게임 하나로 합친다. 플레이어가 허브 월드에 입장해 장소 · 사물과 상호작용으로 장르 미니게임을 실행한다(심즈의 집 · 동네에서 활동을 고르는 꼴). 허브는 여러 키트를 함께 쓰고(스트리밍 오픈월드 · 길찾기 · 하우징 · 농사 · 생물 · 상점 · 채팅 · 소셜 · 트리거 → 컷씬 · 리스폰 · 카메라 프리셋 · 자막 · 런타임 UI), 장르 키트는 미니게임 Experience 로 들어간다.

**대조.** 언리얼 Lyra — 게임 하나 + `ULyraExperienceDefinition`(게임 모드 · 폰 데이터 · 액션 셋) + Game Feature 플러그인(ShooterCore · TopDownArena)을 켜고 끈다, 프런트엔드에서 Experience 를 고른다. 이 결정은 거기에 "허브 안에서 고르기" 를 더한 것이다.

**이름.** 허브 게임은 `Empty`(빈 시작 게임이라는 뜻 — 하는 일과 어긋남)에서 `Playground` 로 바꾼다(여러 장르를 들어가 해 보는 장소 · 기능 시험 공간의 흔한 낱말, 사용자가 다른 이름을 고르면 그것으로). 이름 바꾸기는 이전의 첫 단계 — `Empty` 를 만지는 진행 중 갈래(리스폰 · 시퀀서 · 런타임 UI · 카메라 · 자막)를 합친 뒤 `py -3 -m Scripts move-source` · `move-asset` · `rename-symbol` 로 소스 · 콘텐츠 · 설정 · 시나리오 경로 · CTest 이름을 한 번에, 옛 이름은 `_bannedNames` 에.

**원칙 — 엔진 기능은 모두 허브에(사용자 결정 2026-11-20).** 키트뿐 아니라 엔진 · GameFramework 기능은 빠짐없이 허브 장소나 미니게임 어딘가에서 쓰이고 시나리오로 확인된다(예: 정거장 텔레포트 장치, 숲 풀베기 · 나무 베기, 대장간 장비 교체, 진흙길 발자국 · 지형 표면 효과 · 웅덩이, 광장 날씨 전환 · 낮밤, 격투 미니게임 히트 반응 · 피 · 신체 절단). 배치표는 엔진 기능 전수 조사(같은 날)로 채우고, 없는 기능은 "만들 것" 으로 배치한다. 폐기할 게임에서만 쓰이던 기능은 그 게임을 지우기 전에 허브로 옮긴다.

**엔진 기능 전수(2026-11-20, 시나리오 467 · 기능 폴더 전체) — 허브 이전 확인 목록.**
- 없는 기능(만들 것 → 붙일 장치): 텔레포트 장치 · 포털(Interactable · ZoneVolume · `SceneComponent::teleport` · savetravel), 풀베기(FoliageComponent 인스턴스 제거 API 새로), 나무 베기(Fracture · RigidBody · LootTable), 피 데칼 · 지속 출혈(DecalComponent · Dismemberment `_bloodChannel` · VFXEffectPool), 번개 · 비 · 눈 파티클 · 쌓이는 눈 · 웅덩이 고임 · 마름 · 밟으면 튐(WeatherSystem · 지형 `_wetness` · `_puddleScale` · SurfaceDeform), 실내 차폐(DecalRainWash 지붕 광선 · AudioRoomGraph), 날씨 · 낮밤 → 오디오 · 조명, 바람 → 천, 3D 등반 · 맨틀 · 수영 · 로프 · 사다리(MotionWarping · NavMeshLink), 상자 열기 · 줍기(LootTable 은 쓰는 곳 0), 이모트 · 미니맵 · 낚시.
- 시험이 없는 기능: 단위 · 시나리오 둘 다 없음 — SmartObject · 소프트바디 · HDR 출력, 시나리오 없음 — 3D 파괴 · 표정 · 립싱크 · AI 인지 · AI · 스폰 디렉터 · 락온 · 턴 순서 · 인벤토리 · 제작 · 상점 · 날씨 전환 · 데칼 비 씻김 · 카메라 흔들림 · 사진 모드 · 스플라인 강, 단위 없음 — GrabberComponent · 튜토리얼 힌트 · 안개 · 무기 교체.
- 폐기할 게임에서만 쓰는 사용처(지우기 전에 허브로): Shooter3D — 장비 교체 · 외형 골든 5 · rainyard(발자국 · 표면 · 젖음 유일 사용처) · 래그돌 · IK · 보폭 워핑 · 머리카락 · 망토 · 군중 · 활 · 킬캠 · 업적 · AI · 스폰 디렉터 · 공간 오디오 · 온라인 매치 · 리바인드 · 장치 분실 · 재생성. WildWest — 말 타기 · AI 인지 · 날씨 · 자동 저장 · 도착 컷씬 · 운반. HarvestValley — 낮밤 · NPC 일과 · 날씨 · 튜토리얼 힌트. MeadowVillage · Velmora — 퀘스트 · 평판 · 넷 하우징 · 시퀀스 운반. PixelTown — 대화 · 타일맵. FrontierTown · NileCity — 월드 시계 · 결정성 · 패드 커서 · 일시정지. ThemeParkTycoon — 소품 흩뿌리기 · 오디오 이벤트 · 그림자 골든. StarSkirmish · VoxelCraft · AbilityArena · LanternVale — 매치 상태 · AI 스커미시 · 뷰 동기 · 복셀 · 어빌리티 디버거 · 씬 저장 · 넷 페어드 애니메이션.

**플레이 중 리로드 시나리오(사용자 요청).** 허브 · 미니게임을 플레이하는 도중에 게임 모듈 핫 리로드 · 에디터 모듈 리로드(PIE) · 에셋 리로드(텍스처 · 머티리얼 · 메시 · 프리팹 · 카탈로그) · 설정 리로드 · 셰이더 다시 컴파일(실패 하나 섞기)을 일으켜, 플레이 상태 보존 · 화면 안 깨짐 · 실패는 이전 판 유지 + 실패로 기록(`AssetCacheReloadFunction` void 결함 — 전수 조사 판단 1 절) · 소요 시간 감시 값을 확인한다. 키트가 많이 살아 있는 허브 지점에서 돈다.

**결정(전수 조사 2026-11-20 — 게임 14 개, Empty 골든 613 · Shooter3D 510 · ThemePark 25 · WildWest 14 · Meadow 8).**
- Experience 데이터 `Content/game/playground/experiences/<이름>.experience.xml`: `name` · `map` · `entry`(`SubLevel` | `SceneSwitch`) · `gameMode`(컴포넌트 형식 · 설정) · `kits[]` · `inputContexts[]` · `ui[]` · `catalogs[]`(지금 `PlaygroundGame::onInitialize` 에 박힌 필수 데이터를 옮김) · `net`(`None` | `ClientServer` | `Lockstep` | `Rollback` | `MMO`) · `results[]`(허브로 돌려줄 감시 값 → 경제 · 진행 매핑) · `returnTo`. 맵이 기본 Experience 를 가리킨다(Lyra World Settings 자리). 실행 전역 변수 `-gv_firstExperience=<이름>` 은 `gv_firstScene` 의 상위(처음 둔 실행 인자 `-experience=` 는 2 단계에서 바꿈 — 아래 2 단계 판단).
- 키트 켜기: 모두 링크하고 Experience 가 등록 · 서비스 바인딩만 켜고 끈다(정적 구성 · 결정성 · 핫 리로드와 맞음). 런타임 모듈 로드 · 언로드는 기각(정적 구성 불가, 미니게임마다 언로드 절차).
- **Experience 범위 서비스(구조로 먼저):** 키트 23 개 이상이 한 인스턴스에 등록되면 전역 레지스트리 · 게임 서비스 · GV 가 Experience 사이로 샌다(지금은 체인 단계마다 인스턴스를 다시 세워 감춰짐). 들어갈 때 등록하고 나올 때 해제하는 범위 형식을 두고, 나온 뒤 남은 등록 · 입력 문맥 · 전역 값을 검사(시나리오 판정)한다. 락스텝 · 롤백 미니게임은 허브 틱 · 난수를 섞지 않는다. 넷 구성 해시는 Experience 마다.
- 진입: 허브 사물 상호작용 · 트리거가 `experience.enter <이름>`. 하위 레벨 더하기(허브가 보임) — 농사 · 도시 · 테마파크 · 식당 · 카드 · 리듬, 씬 전환 — 격투 · 카트 · RTS · 복셀 · 2D · 플랫포머 · 메트로배니아 · 배틀로얄 · 호러. 씬 전환 전 허브를 `GameStateSnapshot` 으로 찍고 돌아올 때 복원, 하위 레벨은 허브 틱만 멈춤.
- 서버: 허브는 전용 서버 하나(NetClientServer · NetMMO · 온라인 키트 — `Config/Server/Online/Playground.json`), 매치형 미니게임은 매칭이 Experience 이름으로 세션을 가르고(락스텝 · 롤백은 P2P 또는 `NetTurnRelay`) 끝나면 결과를 허브 서버에 보고.
- 허브가 쓰는 키트: 스트리밍 셀 · 길찾기 · 군중 · Housing · Farming · CreatureLife · Economy 상점 · Chat · Social · Trade · Mailbox · Leaderboard · 트리거 → 컷씬 · 리스폰 · 카메라 프리셋 · 자막 · 런타임 UI · 탈것 · ModSupport(DLC 지역).
- **남길 것:** 다중 게임 팩 경로(`SW_GAMES` · `Bin/Games/<게임>` · `-game=` · 게임별 `_cookRules`)는 게임이 하나면 사용처가 사라진다 — 시험용 작은 둘째 팩 하나를 남긴다.

**이전 차례.** ① `Empty` → `Playground` 이름 바꾸기 + Experience 로더 · `-experience=` + Experience 범위 서비스, KitDemo 20 개를 Experience 로 — `kit_*` 시나리오 20 개 그대로 통과. ② 허브 맵(스트리밍 셀 · 하위 레벨 — `w-cellstate` · `w-netstream` · 씬 스트리밍 (가) 이 기반). ③ 게임 흡수 · 삭제(작은 것부터): PixelTown(2D → 오락실 RPG 미니게임) → WildWest(대형 스트리밍 → 허브 맵) → FrontierTown → NileCity → HarvestValley → MeadowVillage(NetSimulation → 허브 하우징) → ThemeParkTycoon → VoxelCraft(복셀 → 광산 미니게임) → AbilityArena → Velmora(DLC 세이브 승계 → 허브 세이브 · ModSupport) → StarSkirmish(락스텝 → RTS 매치) → LanternVale(MMO 복제 · 셀 · 계정 저장 → 허브 서버) → Shooter3D(서버 권위 · 온라인 키트 → 허브 서버 · 사격장). 단계마다 그 게임의 사용처가 옮겨졌고 시나리오 · 골든이 통과한 뒤에 폴더를 지운다. 골든은 팩 이름이 경로에 들어 이름 재기록만(그림 재기록 없음), CTest 체인은 Experience 별로 나눠 지금의 시한 · 병렬성 유지. 비용: 게임 소스 약 2.7 만 줄 · 콘텐츠 약 1470 · 시나리오 104(Empty 제외) · CMake · `Config/Game` 13 · `CMakePresets.json` `SW_GAMES` 3 곳 · Linux 체인 목록. 먼저 합칠 갈래: `w-sequencer` · `w-runtimeui` · `w-r4` · `w-camsub`(게임 콘텐츠를 만짐), `w-respawn` · `w-trigger` · `w-navworld` · `w-cellstate` · `w-netstream`(허브 기반) — 그 전에는 이름 바꾸기 · WildWest · Shooter3D 흡수를 시작하지 않는다.

**이전 실행표(조사 2026-11-20, main `8b09a82f50`).** 공통: 소스 `Source/Games/<게임>/`(매니페스트 `SWGame.module.json` 의 `GF_*` 가 쓰는 키트), 콘텐츠 `Content/game/<팩>/`, 게임 프리셋 `Config/Game/<게임>.json`(`_packRoot`), CTest 등록은 목록 없이 폴더를 훑는 `Test/Scenario/CMakeLists.txt` 한 곳 — `automation/*.scenario.xml` → `Scenario.<팩>`, `showcase/` → `.showcase`, `desktop/` → `.desktop.<이름>`, `sequence/*.1|.2` → `.sequence.<쌍>`, `editor/` → `.editor`, `load/` → `.load.<이름>`(라벨 load). `debugger/` · `resourcecondition/` · `netpair/` 는 손으로 · 스크립트로 돈다. 골든은 `<시나리오 폴더>/golden/<Scenario name>.<이름>.<백엔드>[.linux].ppm`(`AutomationGoldenUtil::makeGoldenID`) — 시나리오 `name="game.<팩>.<이름>"` 이 파일 이름에 들어간다. `-game=<게임>` 은 각 `<게임>Game.cpp` 머리 주석 · 일부 시나리오 주석에만 있고 실제 인자는 CTest 가 붙인다. 깃발 열은 그 게임만 정의한 `SW_TEST_GLOBAL_VARIABLE` · `SW_DEV_COMMAND`, 감시는 감시 값을 등록하는 파일 수.

| 게임(팩) | 소스 줄 · 콘텐츠 파일 | 키트(`GF_*`) | CTest 항목(시나리오 수) | 골든 | 서버 짝 · 스크립트 | 그 게임만의 깃발 · 명령 · 감시 | 쿠킹 · 패키지 · 빌드 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Empty(`empty`) | 9467 · 1204 | 24: NetDestruction · NetPairedAnimation · ActionCombat · Housing · ModSupport + 장르 19(KitDemo) | `Scenario.empty`(154 + 엔진 시나리오 — 활성 게임), `.editor`(4), `.load.*`(13), 수동 `debugger/`(2) · `resourcecondition/` | 629 `automation/golden/` | `Config/Server/Empty.json`, `net-pair`(`Scripts/net/net_pair_run.py:34`) · `destruction_net_pair.py:31` | `gv_stressTest*` 44 · `gv_uiDemo`, `DemoInventory` · `DemoShop`, 감시 23 파일(`EmptyNav.*` 6 · `EmptyLoad.CrowdCharacters`), 렌더 확장 패스 `Empty.DrawDemo` · `Empty.TintDemo` | 활성 게임 기본값 `cmake/Modules/Config/BuildOptions.cmake:177`, `pack.py --game` 기본 · android-apk · web-package · web-check 예시, `Test/Pack/RunCookedContent.cmake:20`, `gv_firstScene` 149 시나리오 |
| Shooter3D(`shooter3d`) | 7110 · 667 | Account · Chat · Economy · Leaderboard · Mailbox · Matchmaking · NetClientServer · NetSimulation | `Scenario.shooter3d`(32, SLOTS 1 — `sequential_scenario_packs`), `.showcase`(8), `.desktop.*`(4), `.sequence.*`(1 쌍), `.editor`(2) | 510 `automation/golden/` · `showcase/golden/` | `Config/Server/Online/Shooter3D{,.online,.external,.external.online}.json` · `Providers/Shooter3D.{push,receipts}.json`, `online-local`(기본) · `telemetry-local`(기본) · OnlineLoadBot `--store-config` | `gv_shooterAutoPlay` · `AutoMelee` · `MotionTrace`, Cheat 3 · `ShooterOnline*` 8 · `ShooterProjectile`, 감시 3 파일 | `CMakePresets.json` `SW_GAMES=Shooter3D` 3 곳(Debug · ASan cpu · Server), `gv_firstScene` 11 |
| WildWest(`wildwest`) | 618 · 55 | OpenWorldWestern | `Scenario.wildwest`(9) | 14 | `telemetry-local --game WildWest` 예시 | `gv_wildWestAutoPlay`, 감시 1 | — |
| ThemeParkTycoon(`themepark`) | 1252 · 84 | ThemePark | `Scenario.themepark`(3), `.editor`(2) | 25 | — | `gv_parkAutoBuild`, 감시 1 | 팩 이름이 게임 이름과 다름(`pack.py:87`) |
| MeadowVillage(`meadowvillage`) | 1142 · 46 | Farming · CreatureLife · Housing · NetSimulation | `Scenario.meadowvillage`(5), `.editor`(1) | 8 | — | `gv_meadowAutoPlay` | — |
| HarvestValley(`harvestvalley`) | 2370 · 67 | Farming | `Scenario.harvestvalley`(7), `.editor`(1) | 0 | — | `gv_farmAutoPlay`, 감시 1 | `_cookRules`(`_listVillagerPreset`) |
| NileCity(`nilecity`) | 2102 · 69 | CityBuilder | `Scenario.nilecity`(5) | 0 | — | `gv_nileAutoPlay`, 감시 1 | `_cookRules` · `_bAutomationOnly` · `_listAlwaysCook` |
| FrontierTown(`frontiertown`) | 743 · 11 | CityBuilder · RealTimeStrategy | `Scenario.frontiertown`(3) | 0 | — | `gv_frontierAutoPlay` | — |
| StarSkirmish(`starskirmish`) | 2615 · 102 | NetLockstep · RealTimeStrategy | `Scenario.starskirmish`(4), `.load.*`(1) | 0 | `lockstep-pair`(`Scripts/net/lockstep_pair_run.py:33`) | `gv_skirmishAutoPlay`, `SkirmishLockstepStatus` · `SkirmishOrder`, 감시 1 | `_cookRules` · `_bAutomationOnly`, `Content/game/starskirmish/redirects.xml`(유일한 팩 리다이렉트 표) |
| LanternVale(`lanternvale`) | 3045 · 7 | NetClientServer · NetMMO · Account | `Scenario.lanternvale`(2), 수동 `netpair/` | 0 | `net-pair mmo`(`net_pair_run.py:191` — 계정 저장 · 재접속) | `Vale*` 명령 7, 감시 1 | — |
| Velmora(`velmora`) | 1420 · 17 | ActionAdventure · ActionCombat · ModSupport | `Scenario.velmora`(1), `.sequence.*`(2 쌍: carry · release) | 0 | — | 감시 1, DLC 모드 `Test/Mods/velmora_tidecaves/` | — |
| VoxelCraft(`voxelcraft`) | 1749 · 25 | Voxel | `Scenario.voxelcraft`(3) | 0 | — | `gv_voxelAutoPlay`, 감시 1 | — |
| AbilityArena(`abilityarena`) | 2228 · 48 | (없음 — GameFramework 만) | `Scenario.abilityarena`(3), `.editor`(2) | 0 | — | `gv_arenaAutoPlay`, 감시 1 | — |
| PixelTown(`pixeltown`) | 629 · 12 | Overworld | `Scenario.pixeltown`(2) | 0 | — | 감시 1 | — |

게임 이름이 박힌 공용 자리(모든 단계가 같이 고친다): `Scripts/dev/linux_scenarios.py:56` `ALL_CHAINS`(Linux 체인 목록), `Config/Dev/AssetReferenceFields.generated.json` 의 `owner: game:<게임>` 11 곳(생성물 — 다시 생성), `Config/Rules/Lint/MagicLiteralBaseline.json` · `CommonShapeExceptions.json` · `NamingWords.json` 의 `Source/Games/<게임>/` 경로, `Test/README.md`, `Test/Scenario/RunStateDirPair.cmake`(Velmora · Shooter3D 쌍).

**`Empty` → `Playground` 이름 바꾸기 자리(전수 2026-11-20).** 합계 — 옮길 폴더 2(`Source/Games/Empty/` 73 파일 · `Content/game/empty/` 1204 파일) + 파일 이름 6 + 골든 파일 이름 629 + 안쪽 글 390 파일 3002 줄 + 바깥 글 164 파일 320 줄.
- 폴더 · 파일 이름: `Source/Games/Empty/` → `Playground/`(CMake 타깃 `SWGame_Empty` → `SWGame_Playground` 는 폴더 이름에서 나옴, `Scripts/dev/hot_reload_debug_check.py:20,21,81`), `Content/game/empty/` → `game/playground/`, `Config/Game/Empty.json`(`_packRoot` 도), `Config/Server/Empty.json`, `EmptyGame.cpp`, `Client/Dev/EmptyClientPart.cpp`, `Client/Dev/EmptyRenderDemo.{h,cpp}`.
- C++ 식별자(`rename-symbol`): `EmptyGame`(17) · `EmptyClientPart` · `EmptyClientPartFactory` · `EmptyRenderDemo`(11) · `EmptyDrawDemoProxy` · `EmptyRenderExtension`, 감시 변수 `emptyCrowdCharacters` · `emptyNavTestPathLength` · `emptyNavTestResultCount` · `emptyNavTestCancelLeaks` · `emptyNavWanderArrivals` · `emptyNavWanderProbeResults` · `emptyNavWanderCancelLeaks`.
- 문자열 속 게임 이름(`rename-symbol` 이 기본으로 건너뜀 → `--include-strings` 또는 손): 감시 값 이름 `EmptyNav.*` 6 · `EmptyLoad.CrowdCharacters`(C++ 5 파일 + 시나리오 3: `navasync` · `load/charactermove` · `load/navcrowd`) → `PlaygroundNav.*` · `PlaygroundLoad.*`, 렌더 확장 패스 `Empty.DrawDemo` · `Empty.TintDemo`(+ `Content/engine/shaders/extension_{draw,pass}_demo.hlsl` 주석), 시나리오 이름 `name="game.empty.<이름>"` 170 파일 236 줄 + 바깥 16 파일(Source 9 — 테스트 주석 · `AutomationRunner.cpp:638`, Scripts 3, Test 1, docs 3), CTest 이름 `Scenario.empty*` → `Scenario.playground*`(`linux_scenarios.py:56` 의 `empty` · `preflight.sh:60` 테스트 로그 이름 · 문서 속 `ctest -R` 줄).
- 경로 `game/empty/`: 팩 안 315 파일 2569 줄(경로 칸은 `move-asset` 이 고침, 칸 밖 주석 · 실행 줄은 끝에 찍히는 남은 목록), 팩 밖 158 파일 258 줄 — `Content/engine/automation/editor` 38 · `Content/engine/vfx` 9 · `Content/common/data/appearance` 3, C++ 문자열(Source 89 파일 152 줄 — `Engine/Content/Pack` · `Asset/Cook` 테스트 · `Games/Empty/KitDemo` 20 등)은 경로 칸이 아니라 `move-asset` 이 고치지 않는다.
- 게임 이름 값 `Empty`: `-game=Empty`(`Test/Pack/RunCookedContent.cmake:20`, 시나리오 주석 `surfacelibrary` · `scanprops`), `cmake/Modules/Config/BuildOptions.cmake:177` 기본값, `Scripts/package/pack.py:26,315` · `android_apk.py:27-29` · `web_package.py:19-20` · `web_check.py:21-22`, 테스트 `Scripts/package/tests/test_android_apk.py`(5) · `test_web_package.py`(3), `Scripts/net/net_pair_run.py:34` · `destruction_net_pair.py:31`, 테스트 자료 `TestProjectSettingsJSONDocument.cpp:191,194` · `OnlineServerConfig.h:4` · `TestOnlineServerConfig.cpp:114-115`(`Config/Server/Empty.online.json` 예시), 주석 `GameInstance.h:52`, `AssetReferenceFields.generated.json:1026`(다시 생성), `MagicLiteralBaseline.json:974-982`(`move-source` 가 경로를 고침).
- 일반 낱말 — 바꾸지 않는다(`rename-symbol Empty=Playground` 를 낱말 그대로 돌리면 이것들이 깨진다): 원소 규칙 `<Material id="Empty"/>`(`default.elements.xml` · `AdventureElementGrid.cpp:116` · `ElementRuleCatalog.h`), 기믹 볼륨 출력 `"Empty"`(`GimmickNodeRegistry.cpp:725`), `InputMapViewModel.cpp:54`, 테스트 자료 `TestCameraDirector.cpp:186` · `TestSequenceTrigger.cpp:52` · `TestRetargetProfileEdit.cpp:101` · `TestModulesViewModel.cpp:190`, `kEmptyGamepadSlot` · `createEmptyActiveScene` · `kEmptySceneName`, 편집기 시나리오 `emptyscene`(+ 골든 5), 모델 `stall_empty` · `stall_cart_empty`, 텍스처 `progress_empty`, `Test/Fuzz/Seeds/PackReader/empty.bin`.
- 에셋 정체성: `.meta` · `<?asset guid?>` 의 GUID 는 옮겨도 그대로라 GUID 참조는 안 깨진다. 깨지는 것은 경로 참조뿐 — `move-asset` 이 경로 칸(`Config/Dev/AssetReferenceFields.json`)만 고치고 끝에 작업 트리 전체 끊긴 참조 검사를 돌린다. 임포트 산출물은 커밋하지 않으므로 옮긴 뒤 다시 임포트.
- 도구: `py -3 -m Scripts move-source Games/Empty=Games/Playground`(include · CMake · 문서 · 설정 경로), `py -3 -m Scripts move-source --base . Config/Game/Empty.json=Config/Game/Playground.json Config/Server/Empty.json=Config/Server/Playground.json`, `py -3 -m Scripts move-asset game/empty game/playground`(meta 함께, 경로 칸 · 테스트 CMake), `py -3 -m Scripts rename-symbol EmptyGame=PlaygroundGame EmptyClientPart=PlaygroundClientPart EmptyClientPartFactory=PlaygroundClientPartFactory EmptyRenderDemo=PlaygroundRenderDemo EmptyDrawDemoProxy=PlaygroundDrawDemoProxy EmptyRenderExtension=PlaygroundRenderExtension emptyCrowdCharacters=playgroundCrowdCharacters …`(감시 변수 6 더). 모두 `--dry-run` 먼저.
- **도구 틈(이름 바꾸기 전에 도구부터 고친다 — 원칙 1).** ① `move-asset` 은 골든 파일 이름(`game.empty.*` 629)과 `<Scenario name>` 을 바꾸지 않는다 — 팩 이름이 이름 안에 들어 있는 자리라 팩 단위 옮기기(`game/<옛>` → `game/<새>`)일 때 `game.<옛>.` 접두를 함께 바꾸게 넓힌다(게임 흡수마다 다시 쓴다). ② `move-asset` 은 옛 id 도메인 표(`Content/game/empty/redirects.xml`)에 옮긴 파일마다 리다이렉트를 무조건 쓴다 — 팩 통째 이름 바꾸기면 옛 팩 폴더가 표 하나로 되살아난다. 세이브 · 서버에 `game/empty/…` 가 남은 배포본이 없으므로 팩 이름 바꾸기에서는 파일마다 표를 쓰지 않고 도메인 한 줄로 둔다(언리얼은 플러그인 이름 바꾸기에 `[CoreRedirects]` PackageRedirects 한 줄 — 파일마다 리다이렉터가 아니다). ③ C++ 문자열 속 `game/empty/` 158 줄은 경로 칸이 아니어서 어느 도구도 고치지 않는다 — 같은 확장에서 `game/<옛>/` 접두 문자열을 함께(정확한 접두라 일반 낱말과 안 겹친다).
- `_bannedNames` 에 올릴 옛 이름: `EmptyGame` · `EmptyClientPart` · `EmptyClientPartFactory` · `EmptyRenderDemo` · `EmptyDrawDemoProxy` · `EmptyRenderExtension` · `emptyCrowdCharacters` · `emptyNavTestPathLength` · `emptyNavTestResultCount` · `emptyNavTestCancelLeaks` · `emptyNavWanderArrivals` · `emptyNavWanderProbeResults` · `emptyNavWanderCancelLeaks`(`Empty` 자체는 일반 낱말이라 올리지 않는다). 문자열 이름(`EmptyNav.*` · `EmptyLoad.*` · `Empty.TintDemo` · `game/empty` · `game.empty.` · `-game=Empty`)은 린트 표가 아니라 경로 · 데이터라 끊긴 참조 검사 · 시나리오 판정이 잡는다.
- 빌드 캐시: 이미 있는 빌드 폴더는 `SW_ACTIVE_GAME=Empty` 를 들고 있어 configure 가 멈춘다(`Source/Games/CMakeLists.txt` 의 FATAL_ERROR) — 통합 때 메인 작업 폴더의 각 프리셋을 `-DSW_ACTIVE_GAME=Playground` 로 다시 구성(캐시 기본값 바꾸기만으로는 안 바뀐다).

**단계 · 갈래 · 통합 시나리오.** 한 단계 = 한 갈래, 커밋은 단계 안에서 나눈다. 단계마다 통합 담당이 Release · Shipping · ASan 증분 빌드 + 아래 시나리오 먼저, 묶음 끝 preflight.

| 단계 · 갈래 | 내용(커밋 단위) | 통합 때 돌릴 것 |
| --- | --- | --- |
| 0 `w-assetmovepack` | 도구 틈 ①–③(`move-asset` 팩 단위 옮기기 — 골든 이름 · `<Scenario name>` · 문자열 접두, 리다이렉트 표 규칙) + Python 테스트 | `py -3 -m Scripts lint`, 스크립트 단위 테스트, 작은 팩(PixelTown)에 `--dry-run` |
| 1 `w-playground` | (가) `move-source` 소스 · 설정 + `rename-symbol` 식별자 (나) `move-asset game/empty game/playground`(골든 이름 · 시나리오 이름 · 문자열 접두) (다) 게임 이름 값(CMake 기본값 · 스크립트 · 테스트 · Linux 체인 목록 · 생성물 다시 생성) (라) `_bannedNames` | `Scenario.playground`(엔진 시나리오 포함 전체 — 그림은 안 바뀌고 골든 이름만), `Scenario.playground.editor`, `Scenario.editor`(편집기 시나리오 38 이 `game/empty` 를 씀), `Scenario.playground.load.*` 목록 확인(`ctest -N`), `Pack.*`(RunCookedContent), `UnitTests`, `net-pair` · `destruction-net-pair`, Linux 는 `.linux` 골든 이름까지 바꿨으니 모아 돌릴 때 |
| 2 `w-kitexperience` | KitDemo 21(`kit_*` 17 · `kitdemo_*` 4)을 `experiences/*.experience.xml` + `gv_firstExperience` 로, `PlaygroundGame::onInitialize` 의 필수 데이터(몬스터 · 카탈로그)를 Experience `catalogs[]` 로, Experience 범위 서비스 해제 검사 | 그 21 시나리오 단독 목록(`-scenario=a,b,…`), 이어서 `Scenario.playground` |
| 3 허브 맵 | `w-cellstate` · `w-netstream` · 씬 스트리밍 (가) 위에 허브 맵 · 하위 레벨 진입 | 새 허브 시나리오 + `Scenario.playground` |
| 4 `w-absorb-pixeltown` | 오락실 RPG Experience(Overworld, 씬 전환) → 폴더 · 프리셋 · Linux 체인 줄 삭제 | 옮긴 2 시나리오 + `Scenario.playground` |
| 5 `w-absorb-wildwest` | 허브 맵 바탕(OpenWorldWestern · 말 · 날씨 · 도착 컷씬), 골든 14 이름 재기록 | 옮긴 9 + 날씨 · 자동 저장 시나리오 |
| 6 `w-absorb-citybuilder` | FrontierTown → 도시 건설 Experience(CityBuilder · RTS), NileCity 는 **둘째 팩으로 남김**(`SW_GAMES` · `-game=` · `_cookRules` · `_bAutomationOnly` 를 함께 쓰는 작은 팩 — 위 "남길 것") | 옮긴 3 + `Scenario.nilecity` |
| 7 `w-absorb-harvestvalley` | 허브 농장 지역(Farming · 낮밤 · NPC 일과 · 튜토리얼 힌트), `_cookRules` `_listVillagerPreset` 를 Playground 프리셋으로 | 옮긴 7 + `.editor` 1 |
| 8 `w-absorb-meadowvillage` | 허브 하우징 · 생물(NetSimulation · 넷 하우징), 골든 8 | 옮긴 5 + `.editor` 1 |
| 9 `w-absorb-themepark` | 테마파크 Experience(하위 레벨), 골든 25 | 옮긴 3 + `.editor` 2 |
| 10 `w-absorb-voxelcraft` | 광산 Experience(Voxel, 씬 전환) | 옮긴 3 |
| 11 `w-absorb-abilityarena` | 아레나 Experience(어빌리티 디버거 · 씬 저장 편집기 시나리오) | 옮긴 3 + `.editor` 2 |
| 12 `w-absorb-velmora` | 액션 어드벤처 Experience + DLC 세이브 승계 → 허브 세이브 · ModSupport(`Test/Mods/velmora_tidecaves` 의 `-game=` 바꿈) | 옮긴 1 + `sequence` 2 쌍 → `Scenario.playground.sequence.*` |
| 13 `w-absorb-starskirmish` | RTS 매치 Experience(`net: Lockstep`), `lockstep-pair` 의 `_GAME`, 팩 리다이렉트 표 처리 | 옮긴 4 + `load` 1 + `lockstep-pair` |
| 14 `w-absorb-lanternvale` | 허브 서버(NetMMO · 계정 저장), `net-pair mmo` 의 `_MMO_GAME` · 로그 정규식 | 옮긴 2 + `net-pair mmo` |
| 15 `w-absorb-shooter3d` | 사격장 · 온라인 매치 Experience, `Config/Server/Online/Shooter3D*` → `Playground*`(비밀 환경 변수 이름 `SW_SHOOTER3D_*` 도), `online-local` · `telemetry-local` 기본값, OnlineLoadBot 주석, 프리셋 `SW_GAMES=Shooter3D` 3 곳 → 온라인 키트가 활성 게임에 들었으니 `NileCity`, `sequential_scenario_packs`, 골든 510 이름 재기록 | 옮긴 32 + showcase 8 + desktop 4 + sequence 1 + editor 2, 서버 Release `Server --test`, `online-local run` |

**2 단계 판단(`w-kitexperience`, 2026-11-20).**
- 진입 값: 실행 인자 `-experience=`(+ `LaunchSelection` 서비스 칸)를 실행 전역 변수 `gv_firstExperience` 로 바꿨다. 시나리오 체인은 단계마다 게임을 다시 세우며 `<Launch>` 전역 변수만 되돌리고 다시 거는데, 실행 인자는 기동 때 한 번 읽혀 둘째 단계부터 바꿀 길이 없었다(Lyra 도 `?Experience=` URL 옵션 — 맵 · 게임 모드와 같은 실행 값). `gv_firstScene` 처럼 배포에도 있다. 옛 이름 `LaunchSelection` 은 `_bannedNames`.
- 켜는 때: 맵이 선 뒤(로드 완료 알림)에 켜던 것을 맵 요청 전으로 옮겼다 — 씬 적용(`beginPlay`)이 완료 알림보다 앞이라 시연 컴포넌트의 `startGame` 이 키트 카탈로그를 못 봤다. 맵이 안 서면 끈다.
- 키트 등록: 장르 키트 20 개가 `SW_EXPERIENCE_KIT` 로 등록한다. 걸 것이 있는 것은 ActionCombat(몬스터 정의 `MonsterCatalog` — `PlaygroundGame::onInitialize` 에서 옮김) 하나이고, 나머지는 `activateKitWithoutServices`(시연 컴포넌트가 규칙 객체를 쥐고 데이터를 경로 칸으로 읽는다 — 서비스가 생기면 그 자리를 바꾼다). Experience 의 `<GameMode>` 는 쓰지 않았다 — 맵에 시연 컴포넌트가 이미 있다.
- 해제 검사: `ExperienceScope` 가 켜기 전 · 켠 뒤 · 끈 뒤의 게임 범위 칸 · 입력 레이어 스택 · 전역 값을 찍어, 켜기가 바꾸고 끈 뒤에도 남은 것을 `ExperienceScope.Leaked*` 오류로 알린다(범위 밖 직접 등록 — 형식이 못 막는 틈). 플레이 중 바뀐 것은 Experience 의 등록이 아니라 보지 않는다. 단위 테스트 `ExperienceTest.LeakCheck*`(음성 대조 포함).
- 남긴 것: `PlaygroundGame::onInitialize` 의 어빌리티 카탈로그 · 클래스는 지역 효과 시연(`zoneshowcase` · `surfaceinteraction` — KitDemo 아님)의 것이라 허브 기본 Experience(3 단계, 맵이 기본 Experience 를 가리킴)와 함께 옮긴다.

**3 단계 판단(`w-hubmap`, 2026-11-20).**
- 허브 Experience 는 게임 단위 바탕: gamesettings `hubExperience`(허브 `experiences/hub`)를 `onInitialize` 뒤 · 첫 씬 전에 켜 인스턴스 내내 두고, 미니게임은 그 위에 하나만 켠다. 2 단계가 적은 "맵이 기본 Experience 를 가리킴"(Lyra World Settings) 대신 Lyra 의 기본 Experience 폴백 자리다 — 허브 밖 첫 씬(지역 효과 시연)도 그 카탈로그를 읽고, 미니게임의 해제 검사는 허브가 켜진 채로 찍어 허브 몫을 남은 것으로 세지 않는다. 맵마다 기본 Experience 칸은 맵 둘이 다른 바탕을 요구할 때 더한다.
- 어빌리티 카탈로그 · 클래스: `PlaygroundGame::onInitialize` 에서 허브의 키트 `PlaygroundAbilities`(게임 모듈 등록 — Lyra 게임 전용 Game Feature 자리, `catalogs="abilities"`)로 옮겼다. 클래스 레지스트리는 `ExperienceScope::emplaceService`(범위가 만들고 끌 때 지운다 — Game Feature `AddComponents` 꼴)로 쥔다. 범위 카탈로그(`ExperienceScope::loadCatalog` — 몬스터 정의도 같은 길)는 `SharedAssetRef` 를 쥐고 관리자 `update`(게임 프레임 앞)가 다시 읽힌 판으로 칸을 바꿔 건다 — 게임 인스턴스 필수 카탈로그(`followScopedService`)와 같은 길, 스냅숏 사본은 두지 않는다. 게임 모듈 키트는 인스턴스보다 먼저 붙는다(`GameModuleManager::bindGameAPI` → `registerModuleTypes` → 등록자 → `createGameInstance`) — 없으면 `validate` 가 모르는 키트로 지고 초기화가 멈춘다.
- 진입 · 복귀: 허브 사물 `ExperienceTriggerComponent`(활성자 볼륨, 서버 판정)가 `ExperienceManager::requestEnter` 로 맡기고 `GameInstance::update` 앞머리(틱 밖 — 자동 저장 자리)가 들어간다. `experience.leave` · 허브 이름 진입이 돌아가기다. `SceneSwitch` 는 허브 씬 안에서 들어갈 때만 허브를 `GameStateSnapshot`(목적 `Snapshot`)으로 찍고, 돌아올 때 허브 맵을 연 뒤 세이브 불러오기와 같은 길로 복원한다. 허브 밖에서 바로 들어간 미니게임(`gv_firstExperience` — KitDemo 시나리오)은 허브 맵을 새로 연다. 복원은 게임 인스턴스 상태까지 되돌리므로 미니게임 결과(`results[]`)는 복원 뒤에 얹어야 한다(결과 매핑 단계에서).
- `SubLevel`: 허브 씬 안에서만 들어가고(밖이면 `ExperienceManager.SubLevelNeedsHub` 오류), 하위 레벨 이름 = Experience 이름, 자리 = 원점(맵을 허브 좌표로 놓는다 — 언리얼 레벨 인스턴스를 원점에). 허브 틱 멈춤은 기각 — 언리얼 레벨 인스턴스 · 유니티 Additive 씬도 바탕 레벨을 멈추지 않고, 미니게임 입력은 `inputContexts` 레이어가 위에서 가린다. 허브 씬이 다른 길(불러오기 · 다른 씬)로 바뀌면 하위 레벨은 씬과 함께 사라지지만 미니게임 Experience 는 켜진 채다(다음 진입 · 돌아가기가 끈다) — 씬 바뀜 알림으로 끄는 일은 허브 밖 씬 전환이 생길 때.
- 허브 맵 `maps/hub`: 스트리밍 씬(셀 10 m, 장식 상자만 셀 — 쿠킹본에서 나뉨), 입구 둘(카드 탁자 → `experiences/cardtable` SubLevel · `maps/hub_cardtable`, 오락실 → `kitdemo_actioncombat` SceneSwitch), 상태 표지 `Marker`. 입구는 `_spawnInside="Ignore"` — 돌아와 복원된 자리가 입구 안이라 다시 들어가지 않게.
- 감시 값: `Experience.Active`(미니게임 — 허브만 켜졌으면 `none`, 2 단계 시나리오 그대로) · `Experience.Hub` · `Experience.LeakCount`(끈 범위가 남긴 것의 합) · `Experience.HubRestoreCount`. 시나리오 `automation/hub`, 단위 테스트 `ExperienceTest.LayeredScopesReleaseOnlyTheirOwn`.
- 남은 것(같은 절 다음 단계): 허브가 쓰는 키트(스트리밍 오픈월드 · 하우징 · 농사 · 상점 …)를 허브 맵에 배치(흡수 단계마다), 전용 서버 허브(NetMMO), 플레이 중 리로드 시나리오를 허브 지점에서.

**겹치는 진행 중 갈래(main 대비 diff, 2026-11-20).** 이름 바꾸기(단계 1)는 아래가 합쳐진 뒤에 시작한다.
- `w-reload` — `Content/game/empty/automation/desktop/editor/{playreload,shaderreload}` 새 파일 24 · `Source/Games/Empty/{CMakeLists.txt,HotReloadTestComponent.*}` · `Test/Scenario/CMakeLists.txt` · `AutomationRunner.cpp`(`game.empty.` 주석) · `Test/README.md`. 가장 크게 겹친다 — 반드시 먼저 합친다.
- `w-cutscene` — `Content/game/empty/{automation/cutscenetrigger.scenario.xml,data/cutscenegate.seq.xml,data/cutscenelanding.seq.xml,data/gamesettings.xml,maps/cutscenetrigger.scene.xml}`.
- `w-auditfix` — `Source/Games/Empty/Client/Dev/StressTestScene.cpp`, `NamingWords.json`.
- `w-edinput` — `ContentBrowserPanel.{h,cpp}` · `ContentBrowserViewModel.h` · `ImGuiEditor.cpp` · `Content/engine/automation/editor/scenereference.scenario.xml`(`game/empty` 경로가 든 파일), `NamingWords.json`.
- `w-memdebug` — `ContentBrowserPanel.h`, `NamingWords.json`.
- `w-fenceretire` — `GPULightBuffer.cpp`(`game/empty` 주석 한 줄).
- `w-experience` — 겹치는 파일 없음. 다만 Experience 데이터를 `Content/game/empty/experiences/` 에 더하기 시작하면 단계 1 과 겹치므로 그 갈래는 틀(`GameInstance.cpp` · `GameSettings` · `GameSelection`)만 합치고, 데이터는 단계 2 에서 `game/playground/` 에 둔다.
- `w-stream` · `w-cmdargs` — 이름 바꾸기 자리와 겹치지 않음(`docs/DesignBacklog.md` 는 다른 절).
- `NamingWords.json` `_bannedNames` 는 세 갈래가 더하므로 단계 1 (라) 는 맨 마지막 커밋으로 둬 충돌을 한 줄 병합으로 줄인다. `docs/NEXT.md` 에도 `Empty` · `game/empty` 줄이 있다 — `move-source` · `rename-symbol` 이 문서도 고치므로 단계 1 은 NEXT 를 만지는 갈래가 없을 때.
