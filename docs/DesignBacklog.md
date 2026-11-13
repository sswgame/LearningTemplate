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
| 유지(이미 병렬) | 컴포넌트 틱 fork-join, 변환 계층, 애니메이션 비용 기반, 프레임 시스템 lane, GPU 업로드 · 식생, 셰이더 쿠킹, PackWriter 항목 압축. 틱 비용 기반 분배는 8 게임 중 1 만 이득이라 기본 정책을 바꾸지 않는다 |

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

## 길드 창고의 이전 불가 자산(사용자 결정 2026-11-12, 감사 O-10)

- **결정:** 유료 재화 · 귀속 아이템처럼 계정 사이 이전이 막힌 자산은 길드 창고를 거쳐서도 **맡긴 당사자가 허용한 권한 안에서만** 다른 계정으로 간다. 허용이 없으면 맡긴 사람만 꺼낸다.
- **구조(들어감):** 원장 정책 `ILedgerPolicy::isTransferable`(PlayFab Economy v2 · Steam Inventory 처럼 정의의 속성 — 거래 `isTradable` 과 같은 표, 화폐는 `_bPaid` 가 아니면 이전 가능). 이전 불가 자산은 맡긴 사람별 보유자(`esc/guild/<길드>.<계정>`)에 들고 맡긴 사람은 `social_guild_depositor` 에 적는다(원장이 보유자를 열거하지 못해 해산 확인 · 창고 목록이 이것으로 몫을 찾는다). 꺼내기는 `_depositorID` 로 몫을 적고 본인이거나 허용(`social_guild_storage_grant`, 남은 한도를 같은 트랜잭션에서 줄인다)이 있을 때만 옮긴다. 허용 부여 · 회수 `GuildGrantWithdrawal` · `GuildRevokeWithdrawal` 은 맡긴 사람만, 멱등 키 필수, 한도는 덮어쓴다(WoW 길드 은행 출금 한도와 같다, 맡긴 사람마다 32 개). 맡긴 사람이 떠나거나 내보내지면 몫을 같은 트랜잭션에서 돌려주고(몫의 자산 종류는 원장 다리 상한 16 까지) 기록 · 허용을 지운다. 허용을 받은 멤버가 떠나면 받은 허용도 지우고, 부여 · 회수는 지우지 않는 멱등 기록(`ServiceIdempotency`, 범위 `guild/<계정>`)으로 회수 뒤 늦게 온 같은 부여를 무시한다.
- **남은 것:** 창고 목록의 128 개 잘림(쪽 읽기 없음)은 감사 O-18 에 있다.
- **확인:** `TestGuildService` — `NonTransferableShareNeedsDepositorGrant` · `DepositorShareBlocksDissolution` · `DepositorShareReturnsOnLeaveAndKick` · `ReceivedGrantEndsWithMembership` · `LateGrantReplayAfterRevokeChangesNothing`.
