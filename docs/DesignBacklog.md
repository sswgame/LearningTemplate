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
- **FUNCTION 리플렉션:** `CallInEditor` 계약은 섰다(a02f95478d — 주인 편집 하나로 감싸 Undo 한 단계 · dirty). `callWithText` · `bindEventToFunction` 의 콘솔 · 비주얼 배선은 수요가 생길 때만 허용 목록 · 수명 · 핫 리로드 검증과 함께. `bindEventToFunction` 소비자는 지금 테스트뿐이다.

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
- 데이터 `Content/game/empty/zones/`, 시나리오 `game.empty.zone*` · `sinkingsand`. 비교: 언리얼 GAS 이펙트 Component · PhysicsVolume, 유니티 Volume Override · Effector, Godot `Area3D` 를 한 프로필의 모듈로 합쳤다(영역마다 컴포넌트를 늘리지 않는다).

### 표면 변형 층(사용자 2026-11-08 — 지나온 길 · 파묻힘 · 빨려드는 구멍)

**정한 것.** 씬 서비스 `SurfaceDeformSystem` 하나가 카메라 앞 창의 CPU 높이 격자에 도장(`SurfaceStampDef` — Print · Trail · Funnel)을 찍고 바뀐 칸만 텍스처로 보낸다. 렌더 패킷(`SurfaceDeformBinding`)이 엔진 텍스처 칸 `SurfaceDeform` 과 패스 상수 `g_SurfaceDeformParams` 로 모든 씬 지오메트리 패스(그림자 포함)에 걸고, 지면 · 지형 셰이더가 같은 함수(`surface_deform_layer.hlsli`)로 정점을 내린다(언리얼 RVT · 유니티 `SetGlobalTexture` 자리, 2026-11-11). 충돌 · 내비 · 높이장은 그대로다. 도장을 내는 길은 셋이고 모두 데이터다: 표면 종류 발자국(`SurfaceKindDef::_footprint`), 오브젝트 도장(`SurfaceStampComponent` Hold · Trail), 지역 모듈 `SurfaceStamp`. GPU 렌더 타깃이 아니라 CPU 격자인 것은 도장이 프레임에 몇 개뿐이고 값 시나리오가 깊이를 그대로 읽으며 백엔드 코드가 늘지 않아서다 — 수백 개로 늘면 컴퓨트로 옮긴다. 시나리오 `game.empty.surfacedeform` · `surfacefootprint` · `sinkingsand` · `terraindeform`.

### 관성 이동과 탑승 가속감(사용자 2026-11-08 — 썰매처럼 경사에서 빨라지고 느려지고, 타면 가속이 느껴지게)

**정한 것.**

- 탈것 이동은 둘이다: Jolt 강체 · 바퀴(`WheeledVehicleComponent`, 시뮬레이션 레이싱)와 바디 없이 힘을 적분하는 `KinematicVehicleComponent` · `KinematicVehicleMotor`(아케이드 · 썰매 · 수레 · 호버 · 레일).
- 바깥 힘은 `InertiaSettings::computeAlongAcceleration` 한 벌(경사 중력 − 구름 저항 − 공기 저항 v², 질량으로)을 키네마틱 탈것 · 말(`MountMovementComponent`) · 롤러코스터가 함께 쓰고, 구동은 출력 한계(`_maxPower`, P / (m·v))로 자른다 — 언리얼 Chaos Vehicle 토크 곡선 · 유니티 motorTorque 자리를 바디 없는 모델에서는 일률 한 칸으로.
- 조작감은 모듈(`Grip` · `Drift` · `Boost` · `AirControl` · `Hover` · `Rail`), 튜닝은 `*.vehicle.xml`(`_base` 상속). 탑승 가속감은 좌석(`VehicleSeatComponent::_rideFeel`)이 탈것 가속도로 탑승자 기울임 · 시야각 킥 · 흔들림 · 소리 매개변수를 낸다.
- **배**는 새 물리 없이 선체 강체 + 폰툰 부력 · 키 · 돛 · 방향별 저항의 조합이다(`BoatComponent` · `BoatHelmComponent` · `WeatherWaveComponent`).
- 시나리오 `game.empty.inertia`(골든 `inertia_sled`) · `mountslope` · `boat`. 카트 트랙 전용 시나리오는 두지 않는다(같은 모터 · 표면 종류라 `inertia` 와 겹친다). 비행은 범위 밖.

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
| 머티리얼 다시 묶기 | `game/empty/automation/load/*` · Shooter3D 첫 진입, 새 카운터 `GT.Material.textureResyncs`(프레임마다) | 밉이 오르는 동안 0(구조상 정확히 0 — 아니면 결함) |
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
| 한 쌍의 비용 | `game/empty/automation/benchmark` dx12 · vk · gl, 시계 켬 · 끔 5 번, `RT.Frame` p50 | 차이가 흔들림 안(밖이면 값을 S8 에 적는다 — 시계는 자동 배율이 켜졌을 때만 켜지므로 결정은 그대로다) |
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
| 그림 | 새 시나리오 `game/empty/automation/vfxsort` — 알파 연기 방출기(CPU 하나 · GPU 하나)를 주 시점과 180° 반대편 PiP 로 한 프레임에 본다, 다섯 백엔드 골든 | 두 뷰 모두 먼 입자가 먼저 그려진다. 정렬을 끈 그림과 5 % 넘게 달라야 한다(같으면 장면이 정렬을 드러내지 못한 것) |
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

   통과로 끝맺었는데 실패 줄이 있으면 첫 실패 줄이, 처리하지 않은 로그면 그 첫 줄(과 남은 수)이 까닭이다 — 끝 단계 이름이 까닭이 되는 길이 없다. `Automation.Finished` · `chain stage i/n` · 체인 끝 표 · 부모의 백엔드 요약 · 보고 JSON(`verdict`) · HUD 가 이 값 하나를 찍는다. `chain stage 12/94 Failed (game.empty.vfxsort)` 뒤에 `: UnhandledLog - [Warning] (id=Shadow.AtlasFull) … (+2)` 가 붙는다(앞머리는 그대로 — `linux_scenarios.py` 의 정규식이 읽는다).
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
  - 메시지 방향 강제 — 라우터가 역할별 마스크로 반대 방향 종류를 `Malformed`(XMachina, S).
  - 게임 넷 메시지 본문을 REFLECT 고정 배치로, 본문 판은 스키마 해시(GameServer PacketGenerator, M — NetMessageDef 37 곳).
  - 열거형: 등록기를 항목 표 하나로(생성 바이트 약 0.7 MB↓), 플래그 글은 단일 비트 이름만, 끝 열거자 `Count` 자동 센티널, Core `EnumArray<E,T>` · `makeEnumTable`(개수 모자람을 컴파일 오류로), 열거자 단위 데이터 철자(magic_enum, S~M).
  - 렌더 그래프 접근 종류를 핸들 형식에 — 손 전이 81 곳 제거(Adria, L, HZB 뒤).
  - 컴포넌트 → 카탈로그 id 를 `CatalogKey<Tag>` 형식으로, 쿠킹이 대조(ezEngine, M).
  - 타입별 판 + 패치 `REFLECT( Version = N )` — 그릇마다가 아니라 타입 한 곳(ezEngine GraphPatch, M).
- 화질
  - 스카이 라이트 · 반사 프로브 거르기를 filtered importance sampling(원본 밉을 pdf 로 고름), HDRI 반사를 SH9 대신 거른 텍스처로(VQEngine, S → GPU 찍기 M).
  - TAA: YCoCg 분산 클리핑 · Catmull-Rom 히스토리 · 적응 블렌드 → 그 위에 TAAU(Adria, S → M).
  - 프로브 GI 빛샘: 거리 모먼트 + 체비쇼프 가시성 · 뒷면 가중(Adria DDGI, M — GI 골든 전부).
    - 판단(2026-11-19, 했다): 모먼트는 텍스처 슬롯 대신 프로브 버퍼(t13) 안 SH9 뒤 옥타헤드럴 8×8 묶음(float4 하나에 텍셀 둘)으로 — 반사 프로브와 같은 이유(네 백엔드 같은 길, 바인딩 수 그대로). 광선마다 가장 가까운 텍셀에 기록 비율로 섞고(DDGI 의 텍셀마다 코사인 가중 적분은 스레드 하나 = 프로브 하나라 비싸 뺐다), 읽을 때 쌍선형. 가중 식은 shading_math.hlsli 하나를 C++ 테스트와 셰이더가 함께 읽고, 계수는 EngineConfig `_render._probeVisibility` → 쿠킹 define. 베이크 볼륨은 모먼트가 없어 뒷면 가중 · 바이어스만 받는다. 프로브 재배치(DDGI relocation)는 칸 안에 박힌 프로브를 모먼트로 가리는 것으로 대신한다.
  - 2 단계 HZB 가림(지난 HZB → 새 HZB 로 다시 시험) — SSR 계층 탐색 · 큰 물체 컬링도 같은 피라미드(Adria · VQEngine, L).
  - 자동 노출 히스토그램 + 백분위 컷(Adria, S~M), HDR 디스플레이 출력 `DisplayOutputDesc` 한 값에서 스왑체인 포맷 · 톤맵 변형(VQEngine, M~L), 볼류메트릭 구름 · 비 가림 맵(Adria).
- 도구: 엔진 안 MCP 서버는 들이지 않는다(사용자 결정 2026-11-19 — 지금 필요 없다), 선언형 조작기 · 시각화 속성(PROPERTY Shape), Fileserve(장치가 PC 콘텐츠를 TCP 로 — 개발 전용)(ezEngine).
- 작업 시스템(측정 먼저 — 계기는 됨: Tracy `Wait runParallel` 구간, 카운터 `Task.Join.waits` · `Task.Join.waitMicros` · `Task.Join.parks` · `Task.Idle.parks`, 아래는 그 수를 본 뒤): 묶음 제출 `submitBatch`, `runParallel` 우선순위 레인, guided 청크, 후속 하나 이어 실행, 넘침 보조 큐, 단계 그래프 위상 순서 재사용(Taskflow).
  - 선언형 모양 판단(2026-11-19): 새 어노테이션 키 대신 기존 커스텀 메타 `Meta = "Shape=Sphere|Box|Cone|Arrow, ShapeRange=<float32 프로퍼티>"` 를 프로퍼티마다 적는다 — 파서 · 런타임 메타데이터는 그대로이고 틀린 선언은 EditorTest 가 등록 타입 전수로 막는다(`PropertyShapeTest.EveryRegisteredDeclarationIsWellFormed`). 모양 선 · 손잡이는 선택한 오브젝트에만(ezEngine 조작기 · 유니티 Handles), 쓰기는 인스펙터와 같은 `PropertyEditRequest`, 끌기 하나가 되돌리기 하나. 편집 요청이 Panels 층이라 기능은 `Panels/Scene/` 에 둔다. 적용: 점광 반경 · 스폿 원뿔 · 수중 볼륨 상자 · 잎 영향 구 · 발사 방향 · 2D 빛 반경(`Circle`) · 캐릭터 컨트롤러 캡슐(`Capsule, ShapeOrigin=Base` — 발 기준) · 오디오 존(`ShapeAxes=World` 축 정렬, 상자 · 구는 프로퍼티 `EditCondition = "_shape == …"` 으로 갈린다 — 조건이 거짓인 모양은 그리지 않는다). 배율은 값을 쓰는 쪽의 규칙을 따른다 — 빛 · 오디오 · 부력 · 잎 영향은 미터 그대로라 보지 않고, 캐릭터 캡슐은 `ShapeScale=Physics`(3D: 반지름 max(X, Z) · 반 높이 Y) · `Physics2D`(반지름 X · 반 높이 Y)로 `PhysicsComponent::makeScaledShape` 와 같은 식을 쓴다. 손잡이는 끈 월드 거리를 그 배율로 나눠 저장 값으로 되쓰고, 배율 0 이면 손잡이가 없다. 손잡이 명령 순서는 콜라이더 손잡이와 한 틀(`EditorHandleDragTracker<THandle>`), 축 끌기 식은 `Ray::computeClosestAxisParameter` 하나. 옮기지 않은 것: 물리 셰이프(셰이프 목록 원소) · BoxCollider2D(모든 콜라이더를 늘 그리는 레지스트리 시각화) · NavMeshObstacle(반 크기 하나를 상자 · 원기둥이 함께 쓰고, 중심 오프셋 `_center` 가 월드 행렬로 옮겨지며, 상자는 요만 · 원기둥은 회전 없이 선다 — 원기둥 모양 · 열거 값으로 모양을 고르는 키 · 중심 오프셋 키 · 요만 도는 틀이 함께 필요해 작지 않다).
- 측정 뒤에만: DX12 PipelineLibrary(차가운 시작 PSO 합), 깊이 전용 배치 키(그림자 드로우 수), 서버 MMO 관찰자 거리 기반 틱 주기 · 컴포넌트 TickInterval 버킷(XMachina · ezEngine), 서버 쿠킹의 축약 애니메이션 클립, 온라인 프로토콜 퍼저 타깃 ⑦(퍼징 1 단계 뒤).
