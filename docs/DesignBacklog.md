# 설계 백로그

> 질문: 코드에서 확인한 구조 경계 · 확장 후보는 무엇이고, 각각 어떻게 하기로 정했는가?
> 독자: 이 저장소에서 일하는 에이전트와 사용자.
> 결론: 결함 · 성능 병목의 항목 상세는 [CommercialGapAudit](CommercialGapAudit.md), 순서는 [NEXT](NEXT.md) 다. 여기는 설계 후보와 그 판단이고, 판단과 측정의 정본은 맨 아래 "통합 담당 판단" 절이다. 끝난 일은 지운다(기록은 git).

## 열린 설계 후보

### 구조와 시스템

- **층 경계:** 엔진 층은 컴파일 의존 규칙으로 막지만 링크 산출물 경계와는 별개다. 실제 순환 · 독립 배포 요구가 확인된 층만 별도 타깃으로 나눈다.
- **`ModuleSharedAssetCache.h`** 는 여러 장르 · 온라인 카탈로그가 직접 include 한다(그래프 간선 — 결함 증거는 아니다). 사용처가 공유 캐시 계약에 실제로 기대는지 보고 광역 include 는 좁힌다. 모듈 언로드 · 재로드 때 캐시 소유 · 무효화를 테스트한다.
- **쿠킹이 경로를 조립해 읽는 에셋을 놓칠 수 있다** — `-cooked-content` 로 확인하지 않았다. 실제 자리는 감사 K-7, 순서는 NEXT 7 절.
- **RHI 형식 지원:** 형식 · 용도 조합은 실제 자원 · 렌더 상태 조합으로 질의하고 미지원 경로는 백엔드별 시나리오로 본다. lavapipe `R16G16_SNORM` 블렌드 거부 기록은 코드가 블렌드 비트 없으면 덮어쓰기로 바꾸므로 옛 증상일 수 있다 — 재현 뒤 갱신.
- **`AccountTicketConnect` 단위 테스트는 거절 · 종료만 본다.** 루프백 온라인 호스트로 성공 · 만료 · 재시도 실패와 비밀 값이 로그 · 오류에 새지 않는지를 더한다.

### 이름과 용어

- `naming-words` 는 함수 첫 단어 · 타입 접미사 · 금지 이름만 본다. 멤버 · 지역 · 매개변수 형태(`_camelCase` · `out…` · 단수 컨테이너)는 검사 밖이라 규칙을 단계적으로 자동 검사에 잇는다. 대량 일괄 개명은 하지 않는다.
- 일회성 구조 감사가 낸 모호한 파일 이름 24 · 폴더 5 · `Util` 없는 정적 함수 타입 464 · 첫 단어 반복 폴더 29 는 후보이지 위반 수가 아니다. 소유 시스템이 분명하면 이름을 둔다. 바꿀 때는 리플렉션 · 직렬화 · 에셋 경로 참조까지 함께.

### 구조 리팩터링 코드 대조

- 엔진 프레임 API(`Engine::update` 의 `FrameViewports`)는 렌더 구현을 직접 알지 않는다(`IEngineRenderDriver`). 서버 · 쿠커 타깃에서 이 입력이 불필요한 의존을 끄는지 include 그래프로 보고, 위반이 재현될 때만 뷰 계약을 분리한다.
- 기본 씬 수집과 `RenderExtensionRegistry` 는 입력 · 수명 · 스레드 계약이 달라 합치지 않는다. 같은 기능을 두 번 구현하는 사례가 나올 때만 공통 기여 API.
- 온라인은 `OnlineProtocolService` 가 공통 기반이다. 두 번째 서버 추상화(`OnlineSession`)를 먼저 만들지 않는다.
- 유지할 구조: 타입 기반 온라인 프로토콜, InterestReplicationPolicy 증분 가시 목록, 프로브 revision 캐시, 머티리얼 변경 감지, `IEngineRenderDriver` 경계.
- 측정 전에는 결함으로 세지 않는 것: 헤더 전파 · TU 비용, 시작 시간 단계 비중, UI · VFX 배열 복사, 프레임 경로 intern.

### 데이터 기반 값 · 매직 리터럴

- 늘지 않게 하는 구조(파일 단위 래칫 `Scripts/lint/magic_literals.py`, 기준선 `Config/Rules/Lint/MagicLiteralBaseline.json`, 규칙 [Constants.md](agents/Constants.md) "The ratchet" · "Sentinels")는 섰다.
- 남은 것: 기준선이 큰 파일(데모 UI · 게임 모드 컴포넌트)의 튜닝 값을 PROPERTY · 카탈로그로, AI · 내비 · 에디터 패널의 남은 `-1`, `find*Index` 무리를 `sw::optional` 로(`sw::expected` Core 밖 이전과 함께 — [Errors](agents/Errors.md)). 키트 규칙 수치는 감사 K-4, 렌더 정책 값은 RN-16.
- 이미 PROPERTY 인 게임 값(`WildWestGameModeComponent` · `MeadowFarmGameModeComponent`)은 생성자 기본값만 보고 하드코딩으로 세지 않는다. sentinel 은 `-1` 표기가 아니라 도메인 의미와 저장 · 통신 호환성으로 정한다.

### App 프로세스 종료 코드

- **판단: 현 체계를 둔다.** `Config/Engine/ProcessExitCodes.json` 이 숫자의 단일 출처이고 생성기(`Scripts/generate/process_exit_codes.py`)가 C++ 열거형 · CMake 변수를 만든다. 없애면 CTest `SKIP_RETURN_CODE`(77)와 자동화 결과 분류가 숫자 하드코딩으로 돌아간다.
- 후보(낮음): 스키마에 플랫폼 종료 코드 범위 · 예약 코드 · 자동화 결과 매핑 누락 검사와 생성기 테스트.
- `Scripts/common/exit_code.py` 는 스크립트 자체의 반환 계약이라 합치지 않는다.

### 추가 아키텍처 후보

- **엔진 서비스 슬롯은 프로세스 전역 하나다**(`ServiceSlot.h` · `EngineServices.cpp`, 두 번째 엔진은 `AlreadyBound` 로 거절). 동시 Engine(에디터 다중 미리보기 · 인프로세스 서버+클라이언트)이 실제 요구될 때만 컨텍스트별 슬롯 + 두 인스턴스 격리 테스트.
- **서브시스템 의존이 두 선언이다** — 등록 `after`(생성 순서)와 `getSubsystemDependencies()`(초기화 · 종료 순서). 의미가 달라 그냥 합치지 않는다. `scope.get<T>()` 가 `after` 에 빠진 타입을 시작 단계에서 오류로 잡는지, 수명 의존 누락을 진단이 잡는지 테스트한다.

### 셰이더 구조와 디퍼드 · G버퍼

- 디퍼드 파이프라인(`deferredpipeline.xml` — 알베도 · 옥타헤드럴 노멀 · 금속도/거칠기 + 깊이)과 판 번호 있는 G버퍼 명세(`GBufferAttachmentSpec` v1, HLSL `SW_GBUFFER_LAYOUT_VERSION`)는 섰다. 기존 노드로 PBR 표면 조합 · 게임별 `.material` 변형 튜닝은 되지만, 게임 고유 셰이딩 모델 · G버퍼 계약을 아티스트가 단독으로 더하는 것은 토대 밖이다(노드 등록은 엔진 고정).
- **남은 것:** 공통 `PixelContext` · 표면 평가와 패스별 출력(포워드 색 / G버퍼 인코딩) 분리, 표면 모델 · 필수 입력 선언과 미지원 조합의 쿠킹 실패. Lit · Unlit · Emissive · 툰 · 알파 마스크의 패스 분류 목록부터 만든다.
- 툰 · 그래프 Emissive 처럼 G버퍼에 담기지 않는 것은 `MATERIAL_FORWARD_ONLY` 가 안전한 길이다. PBR 계열을 실수로 ForwardOnly 로 두면 디퍼드 이득을 잃고, 모든 속성을 G버퍼에 넣으면 대역폭이 는다 — 속성 추가는 남는 채널 · 정밀도 검사 뒤 판을 올린다.
- 포워드 · 디퍼드는 같은 장면 · 해상도 · 플랫폼에서 GPU 시간 · G버퍼 대역폭 · PSO 수 · 라이트 수 증가로 비교한다. 모바일 · 웹은 측정 전까지 포워드.
- 머티리얼 제작 UX: 게임 재료(피부 · 천 · 금속 · 젖음 · 식생) 시작 그래프, 미리보기에 대표 메시 · 조명 · 경로 선택, 누락 텍스처 · 경로 분류 · 쿠킹 상태 인라인 표시. 실제 제작 시나리오로 병목 단계를 먼저 찾는다.

### Test Framework

- 틀(필터 · 태그 · 픽스처 · 매개변수 · 시한 · 결정적 샤드 · 스냅숏 · 기대 로그 · JSON)은 충분하다 — 별도 프레임워크를 들이지 않는다.
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
| 다른 파일 카탈로그로의 연결(무기 → 탄 · 아이템, 격투 기술 → 프레임) | 둘을 쥐는 쪽이 시작할 때 보고 깨지면 서지 않는다 | `WeaponCatalog::checkProjectiles` · `findUnknownItems`(Shooter3DGame · MechArenaWorld). 쿠커는 못 본다 |
| 원소 자기 칸(모르는 속성 · 범위 밖 · 겹침) | 오류 · 경고와 그 원소 제외 | 각 카탈로그 읽기(`XMLFields`), `ResourceDataSchemaTest` 는 경고도 실패 |

- **남은 것:** `FighterCatalog`(기술 → 프레임)는 그 기술만 빼는 정책이라 계약대로 서지 않게 할지, `ActionPlatformerCatalog` 의 패턴 상태 전이 검사를 표로 옮길 수 있는지 정한다. 상속으로 물려받은 참조 · 다른 팩 참조의 테스트, 잘못된 참조가 쿠킹 · 시작을 통과하는 회귀 테스트를 더한다. 배포 콘텐츠 일괄 검증은 돌리지 않았다.
- `CropCatalog::fillItemCatalog` 의 아이템 자동 생성은 콘텐츠 계약이라 미검증 참조로 세지 않는다.

### ReflectionParser · 런타임 Reflection

- **파서 토큰화(측정 후보):** `SourceTokens::tokenizeCursor` 비용은 계측(`ReflectionParser.TokenizeStats`)만 들었다 — 판정은 S8 표. 10% 를 넘으면 토큰 저장소 재사용, 기본 인자 · 매크로 이름 · 별칭 이벤트 fixture 결과는 보존.
- **유지:** `TypeRegistry::populateLookupCaches` 사전 계산과 `TypeLookupCache` 세대 슬롯 · 조상 테이블. 캐시를 더하거나 등록 · 언로드를 바꾸면 무효화 순서와 모듈 재등록 테스트를 함께.
- **테스트:** 파서 fixture 8 개가 생성 코드 · 오류를 대조한다. 성능 최적화 전에 증분 없음 · 단일 헤더 · 다중 헤더 기준선을 같은 입력으로 반복 측정한다. 흔들리는 시간 문턱은 CI 에 넣지 않는다.
- **FUNCTION 리플렉션:** `CallInEditor` 계약은 섰다(a02f95478d — 주인 편집 하나로 감싸 Undo 한 단계 · dirty). 다음은 (1) 인자 있는 메서드를 기존 타입 위젯 · `ReflectCallResult` 오류 표시로, (2) 실사용 반복 작업부터 액션 목록 · 컨텍스트 메뉴, (3) `callWithText` · `bindEventToFunction` 의 콘솔 · 비주얼 배선은 수요가 생길 때 허용 목록 · 수명 · 핫 리로드 검증과 함께. `bindEventToFunction` 소비자는 지금 테스트뿐이다.

### 비동기 · 병렬 처리

기준(사용자 2026-11-08): 측정 숫자로 순서를 정하지 않고 연쇄 모양으로 고른다 — ① N 에 비례하고 서로 독립 ② 앞단이 뒤를 막는 사슬 ③ N² 를 미리 병렬 · 비동기로. 확인은 목표 규모 부하 시나리오(`ctest -L load`), 숫자는 회귀 감시(언리얼 Mass · 유니티 DOTS 잡과 같은 방향). 도구 고르기는 [CppStyle](agents/CppStyle.md) "Parallel and async — which tool where", 기존 `TaskManager` · `runParallel(ByCost)` 를 쓰고 새 풀을 만들지 않는다.

| 자리 | 판단 |
|---|---|
| 씬 저장(`SceneManager::writeActiveScene`) | 단계 시간 계측은 들었다. 스냅숏(`serialize`)은 게임 스레드, `base` · `write` 는 독립 값(`SceneDocument`)만 읽어 워커로 옮길 수 있다 — 판정은 S8. 성공 전에 source path · dirty 를 완료 처리하지 않고 낡은 스냅숏을 버전으로 가린다 |
| 씬 로드(`loadSceneAsyncJob`) | 파싱 · 인스턴스화는 이미 워커. `instantiate` 가 우세할 때만 순수 전처리(상태 디코드 · 검사)를 병렬로, 생성 · 부착 · 등록은 소유 경계 유지 |
| 씬 쿠킹(`SceneCooker::cookAllScenes`) | 씬 쿠킹 시간 합 · 가장 느린 씬 계측은 들었다. 합이 쿠킹 전체를 지배할 때만 — `CookStore` 공유 통계 · `clearAssetCaches` 때문에 캐시 격리 · 결과 병합이 먼저다(루프만 태스크로 감싸지 않는다) |
| 게임 스레드 에셋 로드 | `AssetLoadProfiler` 로 스톨 상위 에셋 · 단계(IO · decode · upload)를 찾아 지연 로드를 허용하는 경로만 비동기로 |
| 렌더 패스 기록(`RenderGraph::executeParallel`) | 레벨 폭 계측은 들었다 — 폭 문턱 판정은 S8. 배리어 · 레벨 제출 순서는 그래프 소유 |
| AI 이동 사슬 | 비동기 경로 요청 줄 · 군중 프레임 일은 섰다. 남은 것: 한 종류 군중 안 나누기(에이전트 수천이면 지역으로 쪼갬), 측정 `load/navcrowd` |
| 에디터 UI | MVVM → 입력 큐 → 출력 계약(아래 "에디터 UI 스레드 분리 검토") |
| 유지(이미 병렬) | 컴포넌트 틱 fork-join, 변환 계층, 애니메이션 비용 기반, 프레임 시스템 병렬(runOnAnyThread), GPU 업로드 · 식생, 셰이더 쿠킹, PackWriter 항목 압축. 틱 비용 기반 분배는 8 게임 중 1 만 이득이라 기본 정책을 바꾸지 않는다 |

**씬 서비스 · 자체 스레드 · 폴링 판정(2026-11-08):** `MeshLODSystem` · `TextureStreamingSystem` · `SceneAudio` 는 `runParallelByCost`(공유 쓰기는 게임 스레드가 목록 순서로, 오디오 가림 레이캐스트는 물리 한 스레드 계약이라 직렬 16 개 상한). 식생 레이어는 병렬 for. `SceneVFXProvider` · `NetObjectTable` · `SceneNetLink` 는 그대로. 프레임 · 틱 맞춤, GPU 펜스, 소켓 · 망, 프로세스 대기, 감시 · 크래시, 재시도 백오프, 종료 한 번의 완료 거두기의 잠자기는 시간을 재야 하는 자리라 그대로 둔다. `RenderGraph` 레벨 스테이지 · `AudioClip` 디코드 · 스크린샷 쓰기 · `AsyncFileIO` 는 이미 연쇄다.

### 렌더 스레드와 업로드 전용 스레드

- **판단: 전용 업로드 스레드는 넣지 않는다.** 언리얼에서 렌더 스레드 밖의 대표 스레드는 업로드 스레드가 아니라 RHI Thread(플랫폼 중립 명령 → API 실행, 제출 순서 보존)이고 텍스처 스트리밍은 별개 시스템이다.
- 메시 GPU 생성은 `GPUUploadQueue::flush` 가 `runParallel` 로 나누고 GT 가 기다린다(`GT.GPUUpload.flush`) — 보이는 메시는 그 프레임에 있다는 계약 때문이다. 대기를 없애려면 계약(선요청 · 준비 전 제외)을 바꿔야 한다.
- 텍스처는 IO → 디코드(로드 작업) → 렌더 스레드 생성 · 제출(프레임 예산) → 티켓 폴링이 이미 있다. 업로드 계측(`AssetLoadScope` `Texture` · `TextureUpload`, `RT.TextureUpload.pump` · 제출 · 폴링 · 대기 수)은 들었다 — 판정은 S8. 스트리밍 구조 결함은 감사 R-6.
- DX12 · Vulkan 전송은 그래픽스 큐로 제출된다(별도 copy queue 없음). CPU 제출을 옮겨도 GPU 복사 시간은 줄지 않는다 — copy queue 는 중첩 이득 측정이 있을 때만.
- RT 의 RHI 실행이 병목이면(S8: pump 가 RT 의 10% 초과) 업로드 스레드가 아니라 명령 순서 · CPU 소유권을 보존하는 RHI Thread 형 시제품을 검토한다.

### 에디터 UI 스레드 분리 검토

목표(사용자 2026-11-08)는 렌더가 아니라 게임(메인) 스레드와의 분리다 — 게임이 멈춰도 에디터가 반응하고, UI 비용이 게임 프레임을 늦추지 않게(언리얼 Slate · 유니티 · Godot 에디터는 게임 스레드에서 돈다 — 그보다 앞서는 방향). UI → 렌더는 이미 `EditorDrawDataSnapshot` · `DrawSnapshotHandoff` 로 겹친다(유지). 막는 것은 동기 입력 포착(`App::onWindowMessage` → `ImGuiEditor::processEvent`), 라이브 씬 편집(`HierarchyPanel` 이 `GameObjectManager` 를 직접), UI 프레임이 확정하는 뷰 타깃 · 틱 여부다. 측정을 기다리지 않고 선결 구조부터 넣는다:

1. **에디터 MVVM**(계약 정본 [EditorUI.md](agents/EditorUI.md) "MVVM" 절, 남은 View 이전은 NEXT) — ViewModel `gather`(메인 스레드)가 값만 담은 ViewState 를 만들고 View 는 그것만 읽으며, 의도는 명령 큐로 메인 스레드가 프레임 시작에 실행한다. View 가 모델 · 씬 헤더를 include 하면 빌드가 멈춘다. 문서 패널 View 의 남은 강제 범위는 감사 E-16.
2. **입력 큐** — 창과 ImGui 는 UI 스레드가 소유하고 게임 뷰 입력은 프레임 번호가 붙은 큐로(게임 입력 한 프레임 지연, 자동화 가상 입력도 같은 큐).
3. **출력 계약** — 뷰 크기 · Play/Pause/Step 도 명령으로, 한 프레임 지연을 계약으로 적는다. 그 뒤 ImGui 프레임만 UI 스레드로 옮긴다. 모듈 리로드 · RHI 교체 · 종료 전 UI 작업 drain 과 자동화 결정성 테스트를 함께.

### 데이터 구동 구조 후보 판별

기준: 숫자 리터럴이 아니라 **내용 · 정책을 코드가 정하는 모양**을, 내용이 늘어날 연쇄 기준으로 고른다. 형식 · 프로토콜 · 수학 표(RHI 형식 · DDS · DSP 계수 · HTTP/2)는 명세라 코드에 둔다.

- **1 게임 AI(옮김):** `AIControllerComponent` 의 `think` = 감각(`updateBlackboard`) → 트리 한 틱이고 덮어쓸 수 없다. 자동 플레이 · 적 AI 일곱을 `*.bt.xml` 로 옮겼다(언리얼 `RunBehaviorTree` 자리). 코드에 두는 것: `KartAI`(연속 조향 수학), `SRPGCommanderAI`(효용 점수 — 갈래 둘), `ShooterEnemyComponent` · `SkirmishUnitComponent` · `FarmVillagerComponent`(AI 판단이 아닌 몸 · 표시 상태).
- **2 프리셋 · 규칙 표:** `FractureSettings` 철자 표(원본 해시 형식) · `TextureImportConfig` DXGI 번호(명세) · `Autosave` 까닭 우선순위(의미 규칙)는 코드, 나머지는 이미 데이터다.
- **3 문화권:** 복수형 · 서수는 CLDR 규칙 글(`PluralRuleSet`), 숫자 체계 · 통화는 문화권 표 칸. 남은 것: 문화권별 통화 기호(필요할 때 칸 추가).
- **4 에디터 표시:** 입력 오버레이 · 강조 스와치는 테마 토큰. 오버레이 바 표는 바마다 내용 코드가 붙어 코드.
- **5 게임 규칙 수치(남음):** 게임 모드 컴포넌트의 상태 `switch` 와 키트의 `-1` · 실수 상수를 규칙 카탈로그 · PROPERTY 로. 키트 설정을 C++ 기본값으로 만드는 자리와 제안은 감사 K-4 가 정본이다.

### 에디터 기능 · UX

- 단축키 · 명령 팔레트 · Quick Launcher · 다중 선택 인스펙터 · Undo History · 명명 레이아웃 · Map Check · 참조 찾기 · 경로 자동 수정 · 비동기 임포트 · 자동 저장은 이미 있다 — 새로 만들지 않고 Undo 누락 · 취소 불가 편집을 시나리오로 찾아 보강한다(되돌리기 입구 결함은 감사 E-1 · E-2 · E-13).
- **첫 사용(중간):** 빈 상태(새 프로젝트 · 빈 폴더 · 첫 임포트 · Map Check 오류 · 첫 패키징)에 다음 행동을 잇는 것부터. Welcome 화면은 필요가 확인될 때(Godot Project Manager 비교).
- **시각 저작(높음):** 범용 시각 편집 프레임워크를 만들지 않고 데이터 의미로 고른다 — 위치 · 부착은 뷰포트 · 기즈모, 계층 · 범위는 트리 · 오버레이, 시간은 타임라인, 흐름은 노드 그래프, 조합 · 카탈로그는 표 + 미리보기. 공통 재료는 `EditorPreviewPanel` · `EditorPreviewGizmo` · `EditorTransaction` · `EditorNodeGraph`, 같은 결합이 반복될 때만 공용 헬퍼를 뽑는다.
- 경계: 편집 UI 는 저장될 에셋을 바꾸고 미리보기는 그 에셋을 소비한다(미리보기 전용 사본 금지). 선택 · 문서가 바뀌면 끌기를 끝내고 빠른 끌기는 Undo 한 단계, 사라진 본 · 소켓 참조는 조용히 버리지 않고 표시. 에셋 값의 기즈모 조작은 데이터 로컬 공간 · Undo 병합 · 문서 dirty 를 그 모델에 잇는다. 첫 사례(애니메이션 마스크 · 소켓)로 공통 코드가 실제로 있는지 본다 — 완료는 XML 왕복 · 다른 스켈레톤 · 재로드 배치 일치 · `uiaudit` · `uicapture` 화면.
- 타일맵 · 스프라이트 시트의 자동 투명 영역 · 회전 아틀라스는 콘텐츠가 요구할 때 런타임 형식과 함께. 볼륨 안개는 광원 · 범위 표시와 해상도별 `RT.Fog.Volume` 측정이 남았다(`FogComponent` 씬 값이 정본).
- 제외: 코드 에디터 · 마켓플레이스 · 클라우드 협업 · 원격 빌드 · AI 검색 · 멀티 유저 편집(비용 · 운영 책임을 먼저 정한다).

### 키트별 무료 상업용 리소스 (CC0 기준)

- 키트마다 대표 동작을 보이는 최소 샘플만 둔다(범용 에셋을 모든 키트에 복사하지 않는다). 먼저 각 게임 `credits.md` · 공용 출처 기록을 키트별로 이어 중복을 막고, 부족한 것만 고른다.
- **허용 출처:** Kenney(asset pages CC0 — 묶음의 `License.txt` 보관), Poly Haven(CC0), ambientCG(CC0), 이미 쓰는 KayKit · 오디오 팩은 `Content/common/credits.md` · `audio.sources.json` 기록 그대로(같은 제작자라고 새 팩 라이선스를 추정하지 않는다). OpenGameArt 같은 업로드 사이트는 항목별 CC0 확인만. Quaternius QAL(원본 단독 재배포 금지) · CC-BY · NC · ND · 불명확한 것은 들이지 않는다(기존 묶음 여덟의 판단은 아래 "리소스 라이선스").
- **추가 전 기록:** 쓰는 파일과 키트 id, 원본 페이지 · 라이선스 원문, 원본 SHA-256 · 받은 날짜, 변환 경로 · 옵션, 재배포 · 수정 허용. CC0 도 상표 · 초상 · 유명 IP 는 따로 뺀다. 원본은 쓰는 파일만 Git LFS, 구조는 [ResourceStructure](ResourceStructure.md). "무료" 에셋을 자동으로 받는 도구는 만들지 않는다.
- 네트워크 · 온라인 · 저장소 키트는 이미지보다 합성 fixture(입력 · 시드 · 예상 상태, 가짜 사용자 · 상품 데이터)를 쓰고 실제 공급자 로고 · 사용자 자료를 쓰지 않는다.

### GameFramework

- **`GameInstance` 의 조정 책임 집중(중간):** 팩 설정 · 초기화 순서 · 클라이언트 훅 · 넷 세션 · 저장 · 자동 저장 · 오디오 · 프레임 갱신을 한 곳이 조정한다. 기동 실패 되감기 · 프로세스당 하나는 S2 계약(e6933d4ab1). 수명 · 호스트 · 실패 정책이 다른 책임만 나누고, `update` 순서는 계약 테스트로 고정한다. 여러 인스턴스가 요구되면 thread-local 이 아니라 명시적 게임 컨텍스트.
- 유지: 키트 의존 · 모듈 종류는 `*.module.json` 매니페스트가 정본이고 `ModuleManifest.cmake` · `ModuleTargets.cmake` 가 순환 · 링크를 검사한다 — 다른 레지스트리로 복제하지 않는다. 키트 전체의 API 일관성 · 데이터 판 정책은 감사하지 않았다.

### 오픈월드 액션 RPG 키트: MMO 와 싱글플레이 분리

MMO(검은사막류 — 지속 월드 · 경제 · 길드 경쟁)와 싱글 캠페인 액션 어드벤처(붉은사막류)를 한 "대형 오픈월드 키트" 나 게임 이름을 딴 키트로 만들지 않는다. 재사용 기반: `SceneStreamingSubsystem`(관찰자 원점 셀 스트리밍), `GF_NetMMO`(관심 영역 · 우선도), `GF_ActionAdventure`, `GF_OpenWorldWestern`, 온라인 `Social` · `Trade` · `Economy`.

1. 장르 이름을 딴 새 키트는 기존 키트를 조합해 본 뒤 **두 번째 사용처에서 같은 소유 · 저장 · 데이터 계약이 확인될 때만** 나눈다. 공통 후보는 지역 · 관심 지점 진행, 발견 기록, 탐험 지도, 보스 계약, 탈것 인터랙션, 채집 · 제작 진행뿐이다.
2. MMO 조합 후보(`GF_ActionMMO`): 서버 권위 · 영속 프로필 · 구역/샤드 · 파티/길드 · 시장 일관성 · 대규모 전투 복제. 기술 · 강화 · 거래는 클라이언트가 권위로 정하지 않는다. 부하 · 운영 지표 전에 월드 전체 규모를 약속하지 않는다.
3. 싱글플레이 조합 후보(`GF_OpenWorldActionAdventure`): 캠페인 진행 · 지역 상태 · 저장 · 동료 관계 · 보스 · 탈것, MMO 의존 없음. DLC(해양 · 수중 · 주거 · 시설 · 관계 · 추가 이야기)는 기존 세이브를 깨지 않는 선택 데이터 · 시스템 확장 — 팩 마운트보다 넓다(퀘스트 · 지역 상태 승계, 시설 소유, 건축 저장).
4. 말 · 배 · 전투 · 퀘스트는 바로 공유하지 않는다(`WesternHorse` 는 서부극 모델). 공통 탈것 수명 · 입력 · 좌석 인터페이스는 두 번째 사용처로 검증될 때 뽑는다.

수직 슬라이스: MMO 는 `LanternVale`, 싱글은 `Velmora`(확장 팩 · 세이브 승계)가 섰다. 운영형 게임의 누적 업데이트(예: 검은사막 Treasure Codex)는 콘텐츠 목록을 키트에 고정하지 않고 판 붙은 카탈로그 · 선택 팩으로 더한다. 남은 것: 실제 대규모 MMO 부하 목표와 운영 비용.

### 하우징 · 주거 건축 키트

- 경계: `GF_Housing`(부지 소유 · 부품 카탈로그 · 배치 검증 · 미리보기 · 구매 · 회수 · 부지별 저장)은 선택 기능 키트다. 심즈형 욕구 · 가구 효용 · 일정은 생활 시뮬레이션 키트 몫이고, MMO 는 소유 · 최종 판정 · 저장을 서버 권위로. 저장 정본은 부지 id · 안정 배치 id · 부품 id/판 · 변환이다(임시 핸들 금지).
- **남은 것**(`Source/GameFramework/Kits/Feature/World/Housing/`, 시연 Empty `maps/housing.scene.xml`): 첫 판은 칸 벽 · 축 정렬 부지다. 서버 권위 배치(요청 → 서버 판정 → 부지 상태 방송 · 늦은 참가 — 넷 메시지 영역 · 요청 검증 · 복제 테스트 · 둘 실행 시나리오)가 없다.
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

남은 것: 잔상의 GPU · CPU 메모리 측정(`game.empty.afterimage`)과 품질 단계별 상한. 분신은 게임플레이에서 필요할 때 별도 캐릭터 프리팹 · 애니메이션 인스턴스로 — 포즈 재생형인지 AI · 전투 가능한 독립 개체인지 먼저 가른다. 임의 그래프 · 모듈 시스템을 먼저 들이지 않는다.

## 통합 담당 판단(2026-11-08)

코드 규칙(구조로 푼다 · 폴백 없음 · 미사용은 쓰게 · 대기를 쌓지 않음)에 맞춰 정했다. 상용 엔진과 비교해 지금 정할 수 있는 것은 정했고 측정이 필요한 것은 측정을 일감으로 넣었다.

- **계약(S2, 들어감):** 프로세스당 Engine 하나 · GameInstance 하나, 중복은 시작 오류(다중 엔진 요구가 생기면 컨텍스트로). 카탈로그 참조는 데이터 표 한 벌 — 위 "데이터 기반 카탈로그 참조 전수 점검" 의 엄격도 계약. XML 의 같은 종류 id 겹침은 오류(코드 API `add*` 의 교체는 유지).
- **에디터 작업 안전(S3, 들어감):** 소스 관리 연동은 Git 만, 외부 계정 · 유료 공급자 없음. 자동 저장은 세대별 임시본이고 원본을 덮지 않는다.
- **개발 그림은 사진에 들지 않는다(2026-11-09, 들어감):** 캔버스 겹치기 등록(`SW_CANVAS_OVERLAY`)은 층(`CanvasOverlayLayer`)을 밝힌다. 디버그 드로우 게임 뷰 겹치기는 디버그 HUD 와 같은 개발 층 — 사진 캡처 뒤 화면에만 합치고(에디터 게임 뷰는 `rendertarget/dev_overlay`) 스크린샷 · `<ExpectImage>` 골든에 들지 않는다. 캔버스 테스트 그림만 주 출력 층이다. 유니티 기즈모 · 언리얼 `HighResShot` 을 따랐다(Godot 디버그 그리기는 캡처에 든다).
- **하지 않기로 정한 것:** Meadow 밭 크기는 소유 타입 상수, Shooter3D 탄착 표면 목록은 코드 상수. 범용 스크립팅 VM · 네이티브 모드 DLL · 원격 명령 서버는 넣지 않는다.
- **리소스 라이선스(사용자 결정 2026-11-08 — 문제가 되면 다른 리소스):** 저장소의 Quaternius 묶음 여덟은 묶음 페이지가 CC0 이고 QAL 은 소급하지 않아 둔다(근거 `Content/common/credits.md` 머리). 새 리소스는 무료 · 공개 라이선스만(CC0 우선, MIT 처럼 재배포 되는 것은 고지와 함께 — [NEXT](NEXT.md) 리소스 규칙), QAL · CC-BY · 불명확한 것은 다른 CC0 출처로.

**측정과 성능 후보(S8).** 아래 값으로 고치거나, 이득이 없으면 값을 이 표에 적고 닫는다. RHI 스레드 · 에디터 UI 스레드 시제품은 여기서 병목이 나올 때만. "켤 코드" 커밋은 따로라 값이 흔들림 안이면 그 커밋만 되돌린다. 값 칸은 통합 실행이 채운다.

| 후보 | 재는 법(Release, 반복) | 볼 값 | 판정 | 켤 코드 | 값 |
|---|---|---|---|---|---|
| 대기 호출 표 | `App --test --test-filter=OnlineServerCallTableTest.CompletionCostByPending` 5 번, 이 커밋과 그 앞 | `OnlineServerCallTableTest.CompletionCost` P 16 · 256 · 4096 의 ns/완료, 부하 봇 서버 `service_store_pending_peak` | 실제 P 에서 차이가 흔들림보다 크면 둔다 | 호출 번호 색인(`OnlineServerCallTable::_mapCall`) | |
| 반사 검증 중첩 타입 | `--test-filter=ReflectionValidationTest.NestedElementCost` 5 번, 이 커밋과 그 앞 | `ReflectionValidationTest.NestedElementCost` ns/원소 | 차이가 흔들림보다 작으면 되돌린다 | `ReflectionValidationInternal::NestedTypeCache` | |
| 파서 토큰화 | 스탬프를 지우고 변경 없음 · 헤더 하나 · 모듈 전체 3 번씩 | `ReflectionParser.TokenizeStats` 와 `Done in` | 토큰화가 전체의 10% 를 넘으면 저장소 재사용 | 없음 | |
| 씬 저장 비동기 | `-game=AbilityArena` 에디터 `game/abilityarena/automation/editor/scenesave` 3 번 | `Scene.save.serialize` · `.write` · `.base` | write 가 16 ms 를 넘고 serialize 보다 크면 write 를 워커로 | 없음 | |
| 씬 쿠킹 병렬 | `--cook-all` 을 DDC 없이 · 있이 2 번 | `Headless.CookStageTimes` · `SceneCooker.SceneCookTime`(합 · 가장 느린 씬) | scenes 가 쿠킹 벽시계 절반을 넘고 가장 느린 씬이 합의 1/4 아래면 씬 단위 병렬 설계 | 없음(캐시 격리 먼저) | |
| GPU 업로드 · 텍스처 | shooter3d · meadowvillage 첫 진입, DX12 · Vulkan · GL, `-gv_assetLoadReport=1` × `-gv_gpuUploadQueue=0/1` 3 번 | `Texture` · `TextureUpload` 행, `RT.TextureUpload.pump` · `.submitted` · `.polled` · `.waiting`, `GT.GPUUpload.flush` | pump 가 RT 의 10% 를 넘거나 waiting 이 계속 0 이 아니면 RHI Thread 형 시제품 | 없음 | |
| 렌더 그래프 레벨 폭 | `game/empty/automation/benchmark` · Shooter3D, `-gv_renderGraphParallelMinWidth=2 · 3 · 4 · 99`, DX12 · Vulkan 3 번 | RT 프레임 중앙값, `RT.Graph.parallelLevel` · `.parallelLevels` · `.parallelPasses` · `.serialPasses` · `.maxLevelWidth` | 3 · 4 가 2 보다 흔들림 밖으로 빠르면 기본값을 바꾼다(그림은 같아야 한다) | 폭 문턱 gv(기본 2) | |

### 지역 효과의 범용 구조(사용자 2026-11-08 — 용암 · 바다 · 늪 · 유황 · 독가스)

**정한 것(들어감).** 지역 효과는 입력 → 모듈 → 반응이다.

- **입력:** 그린 볼륨(`ZoneVolumeComponent`), 표면 종류 · 물리 재질(`surface:<종류>` — 접촉 `ZoneContactComponent`), 런타임 영역(볼륨 + 수명 · 퍼짐 프리팹). 접촉 넷(겹침 · 디딤 · 부딪힘 · 잠김)이 같은 처리기(`ZoneTracker`)를 지나고 프레임 일은 `ZoneSystem` 하나가 틱 뒤에 돈다.
- **프로필:** `*.zone.xml`(이름 붙은 모듈 목록, `_base` 하나) · 재사용 `*.zonemodule.xml`, 세 층(프리셋 · 변형 · 배치 덮어쓰기 `_listModuleOverride`)을 데이터 상속이 합치고 합친 결과를 로드 때 검사한다(모르는 종류 · 매개변수 · 식 · 동작 · 참조는 로드 실패). 수치 칸은 조건식(`zoneDepth01` · `distanceToCenter` · `immersion` · `timeInside` · `impactSpeed`), 사건은 진입 · 체류 · 이탈 · 조건 전환(`Became` · `Lost`, `delay`), 경우 목록은 처음 맞는 하나, 저항 · 면역은 이펙트 계산이 깎는다.
- **모듈**(서로를 모른다 — 새 효과는 `SW_ZONE_MODULE` 한 줄): `Action` · `PhysicsField`(중력 배율 · 방향 × 세기 · 유체 저항 · 부력 · 마찰, 캐릭터 · 강체 공통) · `Blackboard` · `NavArea` · `SurfaceChannel` · `SurfaceStamp`.
- **반응**은 태그 · 속성만 읽는 컴포넌트다: `VisualSinkReactionComponent` · `JumpEscapeReactionComponent` · `ScreenReactionComponent`(`CameraDirector::setPostProcessOverlay` 페이드).
- 데이터 `Content/game/empty/zones/`, 시나리오 `game.empty.zone*` · `sinkingsand`. 비교: 언리얼 GAS 이펙트 Component · PhysicsVolume, 유니티 Volume Override · Effector, Godot `Area3D` 를 한 프로필의 모듈로 합쳤다(영역마다 컴포넌트를 늘리지 않는다).

### 표면 변형 층(사용자 2026-11-08 — 지나온 길 · 파묻힘 · 빨려드는 구멍)

**정한 것(들어감).** 씬 서비스 `SurfaceDeformSystem` 하나가 카메라 앞 창의 CPU 높이 격자에 도장(`SurfaceStampDef` — Print · Trail · Funnel)을 찍고 바뀐 칸만 텍스처로 보낸다. 렌더 패킷(`SurfaceDeformBinding`)이 엔진 텍스처 칸 `SurfaceDeform` 과 패스 상수 `g_SurfaceDeformParams` 로 모든 씬 지오메트리 패스(그림자 포함)에 걸고, 지면 · 지형 셰이더가 같은 함수(`surface_deform_layer.hlsli`)로 정점을 내린다(언리얼 RVT · 유니티 `SetGlobalTexture` 자리, 2026-11-11). 충돌 · 내비 · 높이장은 그대로다. 도장을 내는 길은 셋이고 모두 데이터다: 표면 종류 발자국(`SurfaceKindDef::_footprint`), 오브젝트 도장(`SurfaceStampComponent` Hold · Trail), 지역 모듈 `SurfaceStamp`. GPU 렌더 타깃이 아니라 CPU 격자인 것은 도장이 프레임에 몇 개뿐이고 값 시나리오가 깊이를 그대로 읽으며 백엔드 코드가 늘지 않아서다 — 수백 개로 늘면 컴퓨트로 옮긴다. 시나리오 `game.empty.surfacedeform` · `surfacefootprint` · `sinkingsand` · `terraindeform`.

### 관성 이동과 탑승 가속감(사용자 2026-11-08 — 썰매처럼 경사에서 빨라지고 느려지고, 타면 가속이 느껴지게)

**정한 것(들어감).**

- 탈것 이동은 둘이다: Jolt 강체 · 바퀴(`WheeledVehicleComponent`, 시뮬레이션 레이싱)와 바디 없이 힘을 적분하는 `KinematicVehicleComponent` · `KinematicVehicleMotor`(아케이드 · 썰매 · 수레 · 호버 · 레일).
- 바깥 힘은 `InertiaSettings::computeAlongAcceleration` 한 벌(경사 중력 − 구름 저항 − 공기 저항 v², 질량으로)을 키네마틱 탈것 · 말(`MountMovementComponent`) · 롤러코스터가 함께 쓰고, 구동은 출력 한계(`_maxPower`, P / (m·v))로 자른다 — 언리얼 Chaos Vehicle 토크 곡선 · 유니티 motorTorque 자리를 바디 없는 모델에서는 일률 한 칸으로.
- 조작감은 모듈(`Grip` · `Drift` · `Boost` · `AirControl` · `Hover` · `Rail`), 튜닝은 `*.vehicle.xml`(`_base` 상속). 탑승 가속감은 좌석(`VehicleSeatComponent::_rideFeel`)이 탈것 가속도로 탑승자 기울임 · 시야각 킥 · 흔들림 · 소리 매개변수를 낸다.
- **배**는 새 물리 없이 선체 강체 + 폰툰 부력 · 키 · 돛 · 방향별 저항의 조합이다(`BoatComponent` · `BoatHelmComponent` · `WeatherWaveComponent`).
- 시나리오 `game.empty.inertia`(골든 `inertia_sled`) · `mountslope` · `boat`. 카트 트랙 전용 시나리오는 두지 않는다(같은 모터 · 표면 종류라 `inertia` 와 겹친다). 비행은 범위 밖.

### 에셋 정체성: 경로 id → 무작위 GUID (사용자 결정 2026-11-11, 1 단계 들어감)

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
- **구조(들어감):** 원장 정책 `ILedgerPolicy::isTransferable`(PlayFab Economy v2 · Steam Inventory 처럼 정의의 속성 — 거래 `isTradable` 과 같은 표, 화폐는 `_bPaid` 가 아니면 이전 가능). 이전 불가 자산은 맡긴 사람별 보유자(`esc/guild/<길드>.<계정>`)에 들고 맡긴 사람은 `social_guild_depositor` 에 적는다(원장이 보유자를 열거하지 못해 해산 확인 · 창고 목록이 이것으로 몫을 찾는다). 꺼내기는 `_depositorID` 로 몫을 적고 본인이거나 허용(`social_guild_storage_grant`, 남은 한도를 같은 트랜잭션에서 줄인다)이 있을 때만 옮긴다. 허용 부여 · 회수 `GuildGrantWithdrawal` · `GuildRevokeWithdrawal` 은 맡긴 사람만, 멱등 키 필수, 한도는 덮어쓴다(WoW 길드 은행 출금 한도와 같다, 맡긴 사람마다 32 개). 맡긴 사람이 떠나거나 내보내지면 몫을 같은 트랜잭션에서 돌려주고(몫의 자산 종류는 원장 다리 상한 16 까지) 기록 · 허용을 지운다. 허용을 받은 멤버가 떠나면 받은 허용도 지우고, 부여 · 회수는 지우지 않는 멱등 기록(`ServiceIdempotency`, 범위 `guild/<계정>`)으로 회수 뒤 늦게 온 같은 부여를 무시한다.
- **남은 것:** 창고 목록의 128 개 잘림(쪽 읽기 없음)은 감사 O-18 에 있다.
- **확인:** `TestGuildService` — `NonTransferableShareNeedsDepositorGrant` · `DepositorShareBlocksDissolution` · `DepositorShareReturnsOnLeaveAndKick` · `ReceivedGrantEndsWithMembership` · `LateGrantReplayAfterRevokeChangesNothing`.

## 도구 · 에디터 · 콘텐츠 파이프라인 설계(2026-11-17)

미뤄 둔 항목을 작업자에게 바로 나눠 줄 수 있게 정했다. 절마다 문제(코드 자리) → 상용 대조 → 결정 · 기각 → 초안 → 이전 순서 → 측정 → 일 목록이다. 일의 크기는 S(파일 서넛) · M(한 묶음)이고, "의존" 이 없는 일은 지금 맡길 수 있다. 새 린트 규칙은 없다 — 막을 것은 형식 · API · 로드 오류로 막는다.

- **확인하다 나온 것:** NEXT 가 남은 일로 적은 A1(Mirror · Montage 등록)과 R1(팩이 리다이렉트 표를 담기)은 main 에 들어가 있다(`git cherry main integ2` 가 비었다) — 아래 두 절은 남은 틈만 적는다.
- **맡기는 순서:** ① 의존 없는 것 — 셰이더 소스 생성기 창구(G1), GameFrameworkDev(H1), DDC 계층(D1), 분수 색인 키(O1 · O2), 프로퍼티 단위 기록(P1), 리다이렉트 표 제외 오류(X1), 퍼징 CMake(F1), 다시 돌기 입구(V1), 측정 케이스(R0 · E1 · T1). ② 그 위 — G2, D2, O3, P2 · P3, F2 · F3. ③ 에셋 GUID 2 단계(`w-guid`) 병합 뒤 — 임포트 예외 칸(M1 · M2 · M3), 에디터 에셋 색인(R1 · R2). ④ 측정이 문턱을 넘을 때만 — 그리지 않는 대기(V3 · V4), 체인 샤드(T2), 색인 디스크 캐시(R3), 패널 준비 옮기기(E2), 원본 떼기(S2 · S3).
- **사용자 결정:** 공유 DDC 를 둘 자리(NAS · 클라우드 버킷)와 쓰기 권한 머신, 원본 전체를 Git LFS 로 넓히기(호스팅 용량 · 비용)와 이력 다시 쓰기(`git lfs migrate` — 되돌리기 어렵다), 원본을 별도 저장소로 떼기, 밤새 도는 퍼징 · 여러 머신 샤드 분배에 쓸 외부 CI 러너.

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

**이전 순서.** ① 창구를 넣고 `ShaderCookDriver` 가 창구만 부른다(컴파일러는 제자리에서 등록 — 동작 같음). ② 폴더를 `DevTools/Material/` 로 옮기고 `sw_add_reflection_step(DevTools MODULE DevTools HEADERS …)`(DevTools 의 첫 리플렉션 단계), `SW_API` → `SW_DEVTOOLS_API`, include 경로, `DevFolders.json` 줄 삭제, `MutableStaticExceptions.json` 경로. ③ `docs/agents/MaterialGraph.md` · `FolderLayout.md` 의 자리 설명.

**판정(측정 없음 — 구조).** Engine 타깃 소스에 `MaterialGraph` 글자 0 건, Shipping 빌드 통과, `--cook-all` 뒤 `git status` 깨끗(생성물 바이트 같음), `matgraph_layer` · `matgraph_parameter` 시나리오와 `MaterialGraphCompileResultTest` 통과.

| 일 | 크기 | 바꿀 파일 | 남길 테스트 | 의존 |
|---|---|---|---|---|
| G1 생성기 창구 | S | `Engine/Graphics/Shader/Compile/ShaderSourceGenerator.h/.cpp`(새), `ShaderCookDriver.cpp`, `MaterialGraphGeneratedPaths.cpp`(등록) | `ShaderSourceGeneratorTest.NoGeneratorFailsTheCook` · `.FailedGeneratorFailsTheCook` | 없음 |
| G2 폴더 이전 | M | `Material/Dev/*` → `DevTools/Material/*`, `DevTools/CMakeLists.txt`, Editor 의 include 자리, `DevFolders.json`, 문서 둘 | 기존 테스트가 새 자리에서 돈다. 통합이 볼 것: 콘텐츠 모듈 검사(`sw_add_content_module_check`)가 DevTools 타입을 호스트 소유로 보는지, 그래프 원본이 팩에 실리지 않는지 | G1 |

### GF 개발 전용 모듈 GameFrameworkDev(설계 2026-11-17, NEXT 5 폴더 자리)

**문제.** `GameFramework/Runtime/Foundation/GameData/Dev/GameSettingsLaunchOverride.cpp` 와 `…/Framework/Save/Dev/SnapshotFaultLaunchHook.cpp` 는 GF 창구(`IGameSettingsOverride` · `ISnapshotFaultHook` — `SW_GF_API`, `StaticRegistrar`)에 정적 등록하는 개발 코드다. DevTools 는 GF 를 볼 수 없어(`TargetLayers.toml` `devtools` 게이트) Runtime 안 `Dev/` 예외 두 줄로 남았다 — 둘째 줄(`d791901155`)이 생긴 것이 자리가 열려 있으면 쌓인다는 증거다.

**대조.** 언리얼은 모듈 `Type` 이 `DeveloperTool` · `UncookedOnly` 면 배포에서 빠진다(GameplayDebugger 가 게임 프레임워크 위의 그런 모듈이다). 유니티는 asmdef 의 `defineConstraints`(`DEVELOPMENT_BUILD || UNITY_EDITOR`)로 어셈블리 단위로 뺀다. Godot 은 모듈을 가르지 않고 `OS.is_debug_build()` 로 분기한다. → 언리얼 꼴(모듈 단위).

**결정.** 공유 모듈 `GameFrameworkDev` 를 `Source/GameFramework/Developer/` 에 둔다(폴더 이름이 `Dev/` 가 아니라 `DevFolders` 검사에 걸리지 않고, 배포에서 빠지는 것은 패턴이 아니라 매니페스트다). **새 매니페스트 칸 · 새 `ModuleKind` 는 없다** — 있는 것으로 된다:
- 개발 전용 표시 = `_listConfiguration: ["Dev"]`(매니페스트 해석 `ModuleManifest.cmake` 가 배포에서 끄고 `sw_skip_inactive_module` 이 짓지 않는다. 런타임은 `ModuleCatalog.cpp:52` 가 거른다).
- 공유 로드 = `_kind: "GameFramework"` + GameFramework 의존 — `GameModuleManager.cpp:517` 이 그 종류를 의존 순서대로 `loadSharedModule` 한다(GameFramework 다음, 키트 · 게임 앞).
- 기각 — `Dev/` 두 줄 유지(위 증거). 기각 — 매니페스트에 개발 전용 칸을 새로 두기(같은 뜻의 칸이 둘이 된다).

매니페스트 `GameFrameworkDev.module.json` 은 `GameFramework.module.json` 과 같은 칸에 `_listDependency` 가 GameFramework 하나, `_listConfiguration` 이 `["Dev"]`, `_listTarget` 이 Client · Server, `_bEnabledByDefault` 가 true 다.

**이전 순서.** ① `Developer/CMakeLists.txt`(`sw_add_module_library(GameFrameworkDev KIND gameframework DEV_TYPE SHARED … NO_REFLECTION LINK_PUBLIC GameFramework)`) + 매니페스트, `GameFramework/CMakeLists.txt` 의 소스 글롭에서 `Developer/` 를 빼고 `add_subdirectory(Developer)`. ② 두 파일을 옮기고(전역 변수 등록자 이름이 `GameFrameworkDev` 가 된다 — `verifyModuleRegistrars` 가 요구) `DevFolders.json` 두 줄과 헤더 주석의 경로를 고친다. ③ `TargetLayers.toml` `gameframework` · `kit` · `game` 게이트에 `GameFramework/Developer` 를 더해 Runtime · 키트 · 게임이 include 하지 못하게 한다.

**판정.** 배포 실행 파일에 `gv_saveRestoreBreakReplace` 글자 없음(`shipping-check`), `savetravel` 시나리오(결함 주입)와 로컬 온라인 실행(`-gv_onlineServerAddress`) 통과.

| 일 | 크기 | 바꿀 파일 | 남길 테스트 | 의존 |
|---|---|---|---|---|
| H1 모듈 만들고 옮기기 | S | 위 ①②③ | `ModuleCatalogTest.DevOnlySharedModuleLoadsAfterItsDependency`(Dev 는 GameFramework 다음, Shipping 은 없음) | 없음 |

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

| 일 | 크기 | 바꿀 파일 | 남길 테스트 | 의존 |
|---|---|---|---|---|
| D1 계층 · 머리 검증 · 설정 | M | `CookRecord.h/.cpp`, `CookStore.cpp`, `Config/Dev/DerivedData.json`(새) | `DerivedDataCacheTest.SharedHitFillsLocal` · `.KeyCollisionIsAMiss` · `.CorruptPayloadIsAMiss` · `.ReadOnlyLayerIsNotWritten` | 없음 |
| D2 임포트를 DDC 로 | M | `Editor/Common/Asset/AssetImportStamp.cpp`, 임포터 3 종(키 입력) | `AssetImportStampTest.StaleSourceIsServedFromCache`(임포터 호출 0) · `.DifferentSettingsMiss` | D1 |
| D3 측정 | 통합 | 위 판정 | — | D2 |

### 원본 저장소 떼기와 `_source`(설계 2026-11-17, NEXT 5 에셋 GUID 3 단계)

**문제.** 원본은 저장소에 있다 — Content 추적 169 MB(골든 ppm 36 · ogg 32 · glb 26 · jpg 20 · hdr 19 · vrm 16 · png 11 MB), LFS 는 출처 폴더 둘(polyhaven · quaternius, 186 파일)뿐이다. `AssetMeta::_source` 는 곁의 파일 이름이다(`AssetDatabase.h:79`).

**대조.** 언리얼은 임포트 결과를 `.uasset` 에 넣고 원본은 저장소 밖을 가리킨다(`AssetImportData` 의 경로 — 원본이 없어도 에셋은 열린다). 유니티 · Godot 은 원본이 프로젝트 안이고 서술(`.meta` · `.import`)이 곁에 있다(Git LFS · Plastic). → 지금은 유니티 꼴이고, 언리얼의 "원본 없이도 돈다" 는 공유 DDC 가 준다.

**결정.**
1. **지금 규모에서는 떼지 않는다.** 문턱: 원본 합계 2 GB 또는 LFS 용량 · 대역 비용이 문제가 될 때. 그 전에 할 것은 원본 확장자 전체를 LFS 로 넓히는 것 하나다(`.gitattributes` 를 출처 폴더 줄에서 `Content/**/*_raw/**` 종류 줄로) — **사용자 결정**(호스팅 용량 · 비용). 이력은 다시 쓰지 않는다(새 커밋부터).
2. **뗄 때의 꼴.** `_source` 는 두 꼴만 받는다: 상대 경로(지금) 또는 `store:<SHA-256>/<파일 이름>`(내용 주소 — Git LFS 의 oid 와 같은 해시라 LFS 서버를 그대로 원본 저장소로 쓸 수 있다). 읽는 창구는 하나(`AssetSourceLocator::resolve` → 로컬 `Saved/SourceStore/<해시>`, 없으면 가져오기 도구)이고, 못 가져오면 임포트 오류다. 주소가 곧 원본 내용 해시라 **원본을 받지 않고도 DDC 키를 구한다** — 원본을 만지지 않는 사람은 공유 DDC 의 산출물만 받는다.
3. 기각 — 서브모듈(원본을 고칠 때마다 두 저장소 커밋, 부분 받기 없음). 기각 — 서술에 절대 경로 · URL(머신마다 다르다).

**일.** S1 `.gitattributes`(S, 사용자 승인 뒤). S2 `AssetSourceLocator` + `AssetMeta::parseXML` 이 두 꼴만 받기 + `asset_identity.py` 같은 규칙(M), S3 `asset-ids --externalize <폴더>`(원본을 올리고 서술을 바꾸고 git 에서 지운다, M) — S2 · S3 는 문턱에 닿을 때 함께 한다(지금 만들면 쓰는 곳이 없다). 남길 테스트: `AssetMetaTest.SourceIsSiblingOrStoreAddress`, `AssetSourceLocatorTest.MissingSourceIsAnImportErrorButCachedOutputLoads`.

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

**이전 순서.** ① 칸 + 엄격 읽기 + 파이썬 `asset-ids`. ② 임포터가 읽고, 규칙 로더가 한 파일 패턴을 거절하고, 지금의 한 파일 규칙을 서술로 옮긴다(옮긴 뒤 `ctest -L import` 의 CheckOnly 가 깨끗해야 한다 — 규칙 이름이 해시에 들면 그 파일만 한 번 다시 임포트된다). ③ 인스펙터 Import Settings 절(값마다 규칙에서 왔는지 예외인지 표시, 고치면 서술에 차이를 쓰고 다시 임포트 — `taste-skill` app-screens, 확인은 `editor/uicapture`).

| 일 | 크기 | 바꿀 파일 | 남길 테스트 | 의존 |
|---|---|---|---|---|
| M1 서술 칸 | S | `Engine/Asset/AssetDatabase.h/.cpp`, `Scripts/lint/asset_identity.py` | `AssetMetaTest.ImportOverrideRoundTrips` · `.UnknownAttributeIsStillAnError`, 파이썬 self-check | `w-guid` 병합(같은 파일을 고친다) |
| M2 임포터 · 규칙 이전 | M | `Editor/Common/Asset/Model/ModelImportConfig.*` · `Texture/TextureImporter.*`, `Config/Editor/*ImportConfig.json`, 해당 `.meta` | `ModelImportConfigTest.MetaOverrideWinsOverRule` · `.UnknownOverrideKeyIsAnError` · `.SingleFilePatternIsRejected` | M1 |
| M3 인스펙터 절 | M | Inspector Model · View | `editor/uicapture` 단계, 예외 고치기 → Ctrl+Z 시나리오 | M2 · P1 |

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

| 일 | 크기 | 바꿀 파일 | 남길 테스트 | 의존 |
|---|---|---|---|---|
| O1 키 함수 | S | `SiblingOrderKey.h/.cpp`(새) | `SiblingOrderKeyTest.BetweenBisects` · `.ExhaustedGapAsksRespace` · `.AppendKeepsStep` | 없음 |
| O2 칸 넓히기 · 이름 | M | `GameObject.h/.cpp`, `ObjectStateSerializer.cpp:905`, 씬 · 프리팹 판 올림, 부르는 곳 3 | `SceneFormatUpgradeTest.SiblingIndexBecomesSiblingOrder`, 옛 씬이 같은 순서로 열린다 | 없음 |
| O3 옮기기 · 기록 | M | `EditorSiblingOrder.cpp`(`renumber` 삭제), `hierarchyorder.scenario.xml` | `EditorSiblingOrderTest.MoveRecordsOneObject` · `.TiesKeepObjectIDOrder`, 시나리오 8000 단계 | O1 · O2 |

### 인스펙터 프로퍼티 단위 기록(설계 2026-11-17, 감사 E-7 · 41)

**문제.** `InspectorPropertyUndo.cpp:23-27` 이 위젯이 활성화되면 `InspectorEditRecorder::beginEdit` 을 부르고, 그것이 선택 전체를 `captureSnapshot` 한다(`InspectorEditRecorder.cpp` `captureSelectionBeforeEdit`). 닫기는 `IsItemDeactivatedAfterEdit` 뿐이라 누르기만 하고 나오면 스냅샷이 세션 끝까지 남는다. 커밋은 대상마다 스냅샷을 한 번 더 찍는다 — 8000 선택에서 숫자 한 칸이 전체 XML 16000 개다.

**대조.** 유니티 `Undo.RecordObject` 는 `PropertyModification`(대상 · `propertyPath` · 값)으로 남고 프리팹 오버라이드와 단위가 같다. Godot `EditorUndoRedoManager.add_do_property` / `add_undo_property`. 언리얼은 `Modify()` 로 객체를 직렬화하되 트랜잭션이 프로퍼티 차이를 낸다. → (대상, 경로, 값).

**결정.** 기록 단위는 (오브젝트 id, 컴포넌트 키, 프로퍼티 경로, 값 글)이다. 컴포넌트 키는 프리팹 오버라이드가 이미 쓰는 `타입#n`(`PrefabOverrides::writesField`), 값 글은 `PropertyGridModel::setPropertyText` 가 쓰는 꼴이다.

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

**판정.** 8000 선택에서 칸을 누르고 나오기 → 기록 0 · 프레임 증가 1 ms 미만. 숫자 칸 끌기 → 구간 열기 프레임 16 ms 미만, 거래 1 MB 미만.

| 일 | 크기 | 바꿀 파일 | 남길 테스트 | 의존 |
|---|---|---|---|---|
| P1 단계 · 편집 구간 | M | `PropertyEditStep.h/.cpp`(새), `PropertyGridModel.cpp`, `InspectorEditRecorder.*`(축소) | `InspectorEditRecorderTest.DeactivateWithoutEditDropsPending` · `.EditRecordsOnePropertyPerTarget` · `.UndoRestoresValueThroughModelPath` · `.OrphanedSpanClosesOnNextGather` | 없음 |
| P2 View 정리 | S | `InspectorPropertyUndo.cpp`, `IInspectorProperty` 구현들의 호출 순서 주석 | 기존 `editor/inspector*` 시나리오 | P1 |
| P3 전수 · 규모 | M | `EditorUndoCoverage.cpp`(프로퍼티 전수), 8000 선택 시나리오 단계 | `editor/undocoverage` 프로퍼티 단계, 아래 측정 절의 값 | P1 |

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

| 일 | 크기 | 바꿀 파일 | 남길 테스트 | 의존 |
|---|---|---|---|---|
| R0 측정 케이스 | S | `Editor/Common/Asset/Reference/Tests/` | `EditorReferenceIndexTest.ScanCostBySize`(ns 를 찍는다 — S8 표의 꼴) | 없음 |
| R1 색인 · 증분 | M | `Editor/Common/Asset/EditorAssetIndex.h/.cpp`(새) | `EditorAssetIndexTest.AddedFileRefreshesIncrementally` · `.RemovedFileDropsItsReferences` · `.MoveKeepsReferrers` | `w-guid` 병합, R0 판정 |
| R2 읽는 쪽 이전 · 옛 색인 삭제 | M | `AssetReferenceTracker.*`, 선택 팝업 · Open · Quick Open · ContentBrowser Model, `EditorReferenceIndex.*` 삭제(`deprecation-and-migration` — 읽는 쪽을 다 옮긴 뒤) | `editor.assetmove` · `contentbrowser` · 참조 찾기 시나리오 | R1 |
| R3 디스크 캐시 | S | `EditorAssetIndex.cpp`(머리에 판 번호 · 참조 칸 표 해시 — 다르면 버린다) | `EditorAssetIndexTest.StaleCacheIsDiscarded` · `.CacheRoundTrips` | R1, R0 판정 |

### 에디터 첫 패널 · Hierarchy 8000 측정 계획(설계 2026-11-17, NEXT 4)

**문제.** 에디터가 뜬 뒤 첫 패널이 약 1 초 멈춘다 — 패널이 그리기 안에서 동기로 준비한다(후보: `ModulesPanel.cpp:32` 가 draw 에서 부르는 `ModulesModel::ensureLoaded`, `EditorViewportOverlays::ensureLoaded`, 참조 색인 첫 스캔, 트리 걷기). 큐브 8000 씬의 계층 · 인스펙터 비용은 잰 값이 없다.

**대조.** 언리얼은 탭을 늦게 만들고(`RegisterNomadTabSpawner`) 무거운 자료는 에셋 레지스트리가 비동기로 채운 뒤 알린다(`OnFilesLoaded`). 유니티는 `EditorWindow.OnEnable` 뒤 `EditorApplication.delayCall`. Godot `EditorFileSystem` 은 스레드 스캔 + 진행 표시. → 패널은 곧바로 그리고 자료는 잡이 채운다. 창구는 이미 있다(`IEditorPanel::onPanelOpened`, `IEditorPanel.h:161`).

**결정.** 재서 문턱을 넘는 패널만 고친다. 고치는 꼴은 하나다: 동기 준비를 `onPanelOpened` 가 띄우는 잡으로 옮기고 Model 이 `isReady()` 를 내며, View 는 준비 전 한 줄만 그린다. 시간 문턱은 시나리오 판정에 넣지 않는다(흔들린다) — `ctest -L load` 처럼 혼자 도는 항목이 값을 남기고 통합이 아래 표를 채운다.

| 후보 | 재는 법(Release, 3 번) | 볼 값 | 판정 | 값 |
|---|---|---|---|---|
| 첫 패널 | `App -EnableEditor -gv_editorPanelTimes=120 -gv_traceSlowTickMs=100`, `Saved/Editor` 를 지운 것 · 둔 것 | 패널별 `EditorPanelTime` 줄의 max, 느린 프레임 트레이스 수와 그 `GT.Frame` 자식 | 첫 그리기 max 가 50 ms 를 넘는 패널마다 준비를 잡으로. 목표: 100 ms 넘는 프레임 0 | |
| Hierarchy 정지 | 큐브 8000 씬(루트 8000 · 20×20×20 트리 두 모양 — 시나리오가 `scratch` 에 만든다), 아무것도 안 함 | Hierarchy 의 `EditorPanelTime` avg, 행 다시 짓기 수 | avg 0.5 ms 미만, 구조 세대가 같은 프레임의 수집 · 정렬 0 회(넘으면 세대로 캐시) | |
| 전체 선택 | 같은 씬에서 Ctrl+A | 그 프레임 ms, 인스펙터 값 행 수와 Inspector 의 `EditorPanelTime` max | 16 ms 미만 | |
| 한 줄 옮기기 | 같은 씬에서 끌어 옮기기 | 프레임 ms, 거래 바이트 | 형제 순서 절의 판정 | |
| 칸 누르기 · 끌기 | 8000 선택, 숫자 칸 | 프레임 ms, 기록 수 · 바이트 | 프로퍼티 기록 절의 판정 | |
| 이름 거르기 | 같은 씬에서 필터에 글자 치기 | 글자마다 프레임 ms | 16 ms 미만(넘으면 거르기를 구조 세대 + 글로 캐시) | |

| 일 | 크기 | 바꿀 파일 | 남길 테스트 | 의존 |
|---|---|---|---|---|
| E1 측정 시나리오 | S | `engine/automation/editor/load/` 에 `startuptime` · `hierarchy8000`(값은 `<Record>`), `Test/Scenario/CMakeLists.txt` 의 load 항목 | 시나리오 자체(판정은 기능만 — 행 수 · 선택 수) | 없음 |
| E2 문턱 넘는 패널 고치기 | 패널마다 S | 그 패널의 Model · `onPanelOpened` | `ModulesModelTest.PanelDrawsBeforeCatalogIsRead` 꼴의 케이스 | E1 의 값 |

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

| 일 | 크기 | 바꿀 파일 | 남길 테스트 | 의존 |
|---|---|---|---|---|
| T1 시간표 내기 | S | `Scripts/dev/test_history.py`(`costs` 하위 명령) | `--self-check` 케이스(p50 · 없는 이름) | 없음 |
| T2 체인 나누기 | M | `Test/Scenario/CMakeLists.txt`, 체인 이름을 푸는 검증 스크립트 셋(`Scripts/dev/` 의 이분 탐색 · Linux 시나리오 · 묶음 검증) | configure 가 나눈 결과를 찍는다(샤드마다 시나리오 수와 예상 초), 샤드 합집합 = 전체 검사 | T1, 검증 시간 절의 V4 뒤 판정 |

### 리다이렉트 표의 팩 담기(설계 2026-11-17, NEXT 5 R1)

**확인.** 들어가 있다 — `PackReachResult.cpp:456` `addRedirectTableRoots` 가 도메인 뿌리의 `redirects.xml` 을 아무도 가리키지 않아도 담고(`PackReachabilityTest`, `TestPackReachResult.cpp:180`), 런타임은 `PackEngineSettingsSubsystem.cpp:44` 가 `AssetRedirect::loadFromSearchDomains` 로 읽는다(틀린 표는 기동을 멈춘다).

**남은 틈.** ① 담는 것은 `_setPackable` 안의 표뿐이다 — `PackConfig.json` 의 제외 규칙이 표를 빼면 조용히 빠지고, 런타임은 "표가 없는 도메인" 을 정상으로 본다. ② 실제 배포 팩으로 본 적이 없다.

**대조.** 언리얼 리다이렉터는 패키지라 가리키는 쪽이 쿠킹에 끌고 오고 `CoreRedirects` 는 ini 라 늘 실린다. 유니티 · Godot 4 는 GUID · `uid` 라 옮기기 표가 없다.

**결정.** 새 구조를 만들지 않는다 — 옮기기용 표는 에셋 GUID 2 단계가 걷고 정체성 대체용 GUID → GUID 표만 남는다(그 표도 같은 뿌리 담기를 탄다: `addRedirectTableRoots` 는 `AssetRedirect::isTable` 만 묻는다). 그때까지 틈 ① 만 쿠킹 오류로 막는다: 디스크에 도메인 표가 있는데 담을 수 있는 집합에 없으면 `PackCooker.RedirectTableExcluded` 로 쿠킹이 멈춘다. ② 는 `shipping-check` 가 팩 목록에서 도메인마다 표를 확인하는 한 줄이다.

| 일 | 크기 | 바꿀 파일 | 남길 테스트 | 의존 |
|---|---|---|---|---|
| X1 제외된 표는 오류 | S | `PackReachResult.cpp`, `shipping-check` 스크립트 | `PackReachabilityTest.ExcludedRedirectTableIsACookError`, 실제 `Config/Engine/PackConfig.json` 규칙으로 도는 `.RealPackRulesKeepRedirectTables` | 없음(`w-guid` 가 표를 걷으면 술어만 따라간다) |

### Mirror · Montage 포즈 노드(설계 2026-11-17, NEXT 5 A1)

**확인.** 들어가 있다 — `AnimationPoseNode.cpp:311-313` 이 `BlendPoses` · `Mirror` · `Montage` 를 `AnimationPoseNodeRegistry` 에 등록하고, 로드(`AnimationGraphAsset::parsePoseNode`) · 생성(`AnimationPoseNodeSet`) · 에디터(`AnimationGraphEdit` 의 `getTypeNames` · `makeDefaultDef` · `isSatisfiedBy`)가 같은 계약(`AnimationPoseNodeType`)을 본다. 테스트는 `AnimationPoseNodeTest` 9 건(`EngineNodesAreRegistered` · `MontageFollowsSectionsAndBranches` · `MirrorSwapsPairedBonesAcrossTheAxis` · `DefaultDefsSatisfyTheirContract` …). **할 일이 없다 — NEXT 의 줄은 통합이 지운다.**

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

| 일 | 크기 | 바꿀 파일 | 남길 테스트 | 의존 |
|---|---|---|---|---|
| F1 프리셋 · 타깃 함수 · 숨김 조건 | S | `CMakePresets.json`, `BuildLayout.cmake`, `cmake/Modules/Build/FuzzTargets.cmake`(새), `Fuzzing.cmake` 주석, `Test/CMakeLists.txt` | `Fuzz.<이름>.Smoke` | 없음 — Windows 에서 지을 수 없어 통합의 `linux-verify` 가 첫 빌드다 |
| F2 범위 · 타깃 셋 · 씨앗 | M | `Test/Fuzz/*`(새) | Smoke 셋, 크래시가 나오면 단위 케이스 | F1 |
| F3 검증에 넣기 | S | Linux 시나리오 스크립트(`fuzz` 하위 명령), `linux-verify` 스킬 | — | F2 |

### 검증 시간: 그리지 않는 대기와 진 단계 다시 돌기(설계 2026-11-17, NEXT 4 검증 시간)

**문제.** shooter3d 체인은 시나리오 28 개에 `<Wait frames>` 140 개 · 5857 프레임이고(slice 1686 · enemydeath 1685 · crowd 545 · bodyviews 512 가 76 %), 백엔드 넷을 차례로 돈다(`Test/Scenario/CMakeLists.txt:115`) — 고정 대기만 2 만 3 천 프레임을 그린다. 큰 대기는 "자동 플레이가 N 프레임 진행한 뒤 사진" 이다(enemydeath: 180 · 300 · 600 · 600 프레임 사이에 사진 5 장). 기다릴 조건이 없는 진행 시간이라 **NEXT 가 적은 "조건 대기로" 는 이 대기에 맞지 않는다** — `Test/README.md` "어디서 시간이 드는가" 가 이미 그렇게 정했다. 체인이 지면 다시 도는 입구도 없다(범인 찾기 `Scripts/dev/scenario_bisect.py` 를 손으로 조립한다).

**대조.** 언리얼은 기능 테스트를 `-nullrhi` 로 돌리고 스크린샷 테스트만 그린다. 유니티는 `-batchmode -nographics` 로 PlayMode 테스트를 돌리고 그림은 Graphics Test Framework 가 따로 본다. Godot 은 `--headless` 와 `--disable-render-loop`(스크립트가 시킬 때만 그린다)가 있다. → 진행과 그리기를 떼고 사진이 필요한 프레임만 그린다.

**결정.**
1. **`<Wait frames="N" render="false"/>`.** 그 프레임은 게임 틱 · 고정 물리 · 에셋 도착 대기를 그대로 돌고 렌더 패킷을 내지 않는다. 그리지 않는 프레임 길은 지금 없다(창 최소화는 `FramePacer` 가 루프를 재울 뿐이다) — 렌더 드라이버 창구(`IEngineRenderDriver`)에 "이 프레임은 내지 않는다" 를 더하고 실행기만 요청한다. 대기의 마지막 프레임들은 그린다(TAA 이력 · 노출 적응 · 그림자 캐시가 사진 전에 자리 잡게) — 그 수는 `Config/Dev/GoldenImage.json` 의 칸이고(없으면 오류) 시나리오가 줄일 수 없다.
2. **"렌더는 게임 상태에 되먹이지 않는다" 를 테스트로 박는다.** 같은 시나리오를 그리며 · 안 그리며 돌려 사진 시점의 `<RecordStateHash>` 가 같아야 한다. 다르면 그 시스템이 가시성 · 렌더 결과를 게임 상태에 쓰는 것이다 — 고치거나 그 시나리오는 그린다.
3. **골든.** 시간 누적 효과가 달라져 문턱 밖으로 나갈 수 있다 — 바꾼 시나리오만 다시 기록하고 dx12 표본을 눈으로 본다(지금 절차). 마지막에 그리는 프레임 수는 "다 그린 사진과의 차이가 골든 문턱 안" 인 가장 작은 값을 재서 정한다(8 · 16 · 32 · 64).
4. **진짜 조건 대기로 바꿀 것**은 바로 뒤에 `<Expect watch>` 가 오는 짧은 대기다. 목록은 도구가 낸다(이분 탐색 스크립트의 새 인자 `--fixed-waits <체인>` — 대기마다 프레임 수와 다음 단계 종류). 시간 이득은 작고 흔들림을 줄이는 것이 목적이다. 부정형 단언은 `was already true` 로그로 가려낸다.
5. **다시 도는 입구.** 이분 탐색 스크립트에 `<체인> --rerun-failed` — 마지막 실행의 단계 보고(`build/<프리셋>/Bin/Saved/Automation` 의 `report[.<백엔드>].<단계>.json`, 통과가 아니거나 보고가 없는 단계)를 읽어 진 단계를 그 백엔드로 혼자 돌리고, 통과하면 "체인에서만 진다" 로 적고 앞 단계 전부를 붙여 다시 돌린 뒤 지면 이분 탐색으로 넘어간다. `--from-stage <단계>` 는 그 단계부터 끝까지만 돈다. 명령은 지금처럼 `Scenario.<체인>` 의 ctest 명령에서 가져온다. Linux 시나리오 스크립트가 같은 함수를 부르고, 묶음 검증 · 시나리오 추적 스킬의 "진 테스트만 다시" 가 이 명령을 가리킨다.
- 기각 — 시나리오 전체 `gv_timeScale`(README: 94 단계 중 17 실패). 기각 — 사진 수 줄이기(판정 범위가 준다). 기각 — 백엔드 함께 돌리기(무거운 장면에서 더 느려 차례로 정한 것이다).

**측정이 먼저다.** 지난 Release 로그의 `[Scenario] … timeline:` 줄에서 shooter3d 의 고정 대기 ms 합과 체인 벽시계를 모은다(새로 재지 않는다). 판정: 고정 대기가 체인의 50 % 이상이면 V3 를 짓고, 그린 프레임 대비 안 그린 프레임 비용이 1/3 이하면 V4 로 간다. 아니면 닫고 `stage reset took` 과 기동을 본다.

| 일 | 크기 | 바꿀 파일 | 남길 테스트 | 의존 |
|---|---|---|---|---|
| V1 다시 돌기 · 대기 목록 | S | `Scripts/dev/` 의 이분 탐색 · Linux 시나리오 스크립트, 스킬 글 둘 | `--self-check` 케이스(보고 없는 단계는 진 것, 진 단계 고르기, 대기 분류) | 없음 |
| V2 timeline 모으기 | 통합 | — | 위 판정 | 없음 |
| V3 그리지 않는 대기 | M | Wait 단계 · `AutomationSession`, `IEngineRenderDriver`, `Config/Dev/GoldenImage.json` | `engine/automation/waitrenderoff`(그리며 · 안 그리며 상태 해시 같음, 안 그린 프레임의 렌더 패킷 0), 설정 칸이 없으면 오류인 단위 케이스 | V2 판정 |
| V4 shooter3d 적용 | M | slice · enemydeath · crowd · bodyviews 시나리오, 골든, `Test/README.md` 의 대기 절 | 체인 통과 + 벽시계 전후 | V3 |
