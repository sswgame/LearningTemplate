# 설계 백로그

> 질문: 코드에서 확인한 구조 경계 · 확장 후보는 무엇이고, 각각 어떻게 하기로 정했는가?
> 독자: 이 저장소에서 일하는 에이전트와 사용자.
> 결론: 결함 · 성능 병목의 항목 상세는 [CommercialGapAudit](CommercialGapAudit.md), 순서는 [NEXT](NEXT.md) 다. 여기는 설계 후보와 그 판단이고, 판단과 측정의 정본은 "통합 담당 판단" 절이고, 작업자에게 나눠 줄 설계(초안 · 측정 문턱 · 일 목록)는 맨 끝 "코어 · 런타임 설계" 절이다. 끝난 일은 지운다(기록은 git).

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

## 렌더 · RHI 설계(2026-11-17)

"측정 · 설계가 먼저" 라 미뤄 둔 다섯 항목이다. 절마다 문제 → 상용 대조 → 결정과 기각 → API 초안(C++17) → 이전 순서 → 측정 → 일 목록 순이다. 상용 엔진의 내부 이름은 기억 기준이라 작업자가 그 판의 문서로 다시 본다. 일 번호(R6-1 …)는 작업자 한 명이 워크트리 하나에서 끝내는 크기이고 "의존: 없음" 은 바로 맡길 수 있다. 새 린트 규칙은 없다 — 막을 것은 형식(순수 가상 · 주인 타입 · 매개변수 타입 · 로드 오류)으로 막았다.

**맡기는 순서.** 바로: R6-1 · R6-2 · MR-1 · RS-1 · RS-3 · PS-1 · L-1(파일이 겹치지 않는다 — R6-1 과 L-1 만 DX12 · Vulkan 팩터리 파일의 서로 다른 함수를 만진다). 그 뒤: R6-3 · R6-4(R6-1 뒤) → R6-5 → MR-2 → MR-3 · R6-6, RS-2(RS-1 뒤), RS-4(RS-3 뒤), PS-2 → PS-3(PS-1 뒤). 측정이 먼저인 것: L-2(상수 버퍼 미러 락), R6-6 의 UV 밀도 쿠킹, PS-3 의 512 개 한도 풀기.

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

**이전 순서.** ① 등록표 `rebind` + 링 미러 + 에뮬레이션 세 백엔드 · 가짜 장치의 `rebindBindlessTexture`(ABI v56) → ② DX12 · Vulkan 링 사본 → ③ `RHIBindlessTextureSlot` 과 `Texture2D` · `TextureCache` 가 rebind 로(이때 세대는 아직 올린다 — 번호가 안 바뀌므로 머티리얼은 다시 찾아도 같은 값을 얻는다) → ④ 세대와 그 읽는 쪽 삭제(`deprecation-and-migration` — 읽는 쪽 넷을 먼저 옮긴 뒤 지운다) → ⑤ 정책 · 내리기. ④ 까지가 감사 R-6 의 "모든 머티리얼 다시 묶기" 를 없애고 ⑤ 는 밉 범위 대기 줄(다음 절) 뒤다.

**측정.**

| 무엇 | 시나리오 · 지표 | 판정 |
|---|---|---|
| 머티리얼 다시 묶기 | `game/empty/automation/load/*` · Shooter3D 첫 진입, 새 카운터 `GT.Material.textureResyncs`(프레임마다) | 밉이 오르는 동안 0(구조상 정확히 0 — 아니면 결함) |
| 스트리밍 구간 프레임 | 같은 실행의 `GT.Frame` · `RT.Frame` p95, `RT.TextureStreaming` | ④ 뒤 p95 가 앞보다 흔들림 밖으로 나쁘지 않다 |
| 링 사본 비용 | 새 카운터 `RT.Bindless.mirrorWrites`, 등록 한 번의 시간(`AssetLoadScope` `TextureUpload`) | 쓰기 수 ≤ 3 × (등록 + rebind), 등록 시간 증가 1 µs 아래 |
| 유효성 | dx12 디버그 레이어 · vk 검증 레이어로 shooter3d · load 체인 | 디스크립터 관련 메시지 0 |
| 내리기 | 4K 300 장 합성 씬을 카메라가 지나는 새 시나리오 `load/texturebudget`, `RHIMemoryTracker` 텍스처 바이트 | 지난 뒤 120 프레임 안에 예산 아래, 멈춘 화면 골든은 예산 없는 실행과 1 % 안 |

**일 목록.**

- **R6-1 등록표 rebind + 에뮬레이션 백엔드** — `Backend/RHIBindlessRegistry.h`, `Backend/RHIDescriptorRingMirror.h`(새 파일), `IRHIResourceFactory.h`, `RHIModuleABI.h`, DX11 · GL · WebGPU `*RHIResourceFactory.cpp`, 가짜 장치. DX12 · Vulkan 은 이 일에서 오류(`RHI.RebindNotImplemented`)를 남기고 false 를 돌려주는 본문만 두고(반쯤 동작하는 길을 만들지 않는다) R6-3 · R6-4 가 채운다. 테스트: `RHIBindlessRegistryTest.RebindKeepsIndexAndMovesOwner` · `DestroyOfPreviousOwnerKeepsReboundIndex`, `RHIDescriptorRingMirrorTest.RewriteReachesEverySlotOnce` · `ReleasedIndexIsSkipped`. 의존: 없음.
- **R6-2 `TextureStreamingPolicy`(순수 계산)와 설정** — `Graphics/Texture/TextureStreamingPolicy.{h,cpp}`, `Config/Engine/TextureStreaming.json`. 테스트: `TextureStreamingPolicyTest.OverBudgetDemotesSmallestOnScreenFirst` · `UnderBudgetPromotesToWantedMip` · `NoOscillationAtBudgetEdge`. 의존: 없음.
- **R6-3 DX12 링 사본** — `D3D12RHIDevice.{h,cpp}`(구간 세 벌 · `bindBindlessRootState` · 프레임 시작 `flushSlot`), `D3D12RHIResourceFactory.cpp`, `ShaderBindingSlots.h`(힙 크기). `refreshConstantBufferViews`(`D3D12RHIDevice.cpp:1890`)가 프레임마다 산 번호의 CBV 를 제자리에 다시 쓰는데, 그 뷰가 셰이더 가시 힙의 것이면 같은 결함이라 이번 슬롯 구간에만 쓰게 한다. 테스트: 장치 테스트 `D3D12BindlessRingTest.RebindIsVisibleNextFrame`, dx12 디버그 레이어 체인. 의존: R6-1.
- **R6-4 Vulkan 링 세트** — `VulkanRHIDevice.{h,cpp}`(세트 셋 · `writeBindlessTextureSlot` 이 슬롯을 받는다), `VulkanRHIResourceFactory.cpp`, `VulkanRHICommandContext.cpp`(리스트를 열 때 이번 슬롯 세트). `UPDATE_UNUSED_WHILE_PENDING` 비트는 그 판 문서로 확인해 붙인다. 테스트: 검증 레이어 체인(lavapipe 포함). 의존: R6-1.
- **R6-5 엔진을 rebind 로, 세대 삭제** — `RHIBindlessTextureSlot`(새 파일), `Texture2D.{h,cpp}`, `TextureCache.{h,cpp}`(플레이스홀더 둘을 `bindDevice` 가 동기로 올린다 — 없으면 기동 오류), `Material.{h,cpp}` · `MaterialInstance.{h,cpp}` · `MaterialRenderVersion.{h,cpp}` · `Light2DCookieAtlas.{h,cpp}`. 테스트: `TextureCacheStreamingTest.PromotionKeepsSRV` · `FailedLoadPointsAtMissingChecker`, `MaterialTextureBindingTest.PromotionDoesNotRepack`, 시나리오 `light2dcookie` · `materialswitch` · `load/*` 다섯 백엔드. 의존: R6-3 · R6-4.
- **R6-6 내리기** — `TextureCache.cpp`(정책 호출 · 내리기 제출), `TextureStreamingSystem.cpp`(화면 크기를 필요한 밉으로 — 대기가 없어도 알린다). 테스트: `TextureCacheStreamingTest.DemotionKeepsSRV`, 시나리오 `load/texturebudget`. 쿠킹 때 UV 밀도로 필요한 밉을 구하는 일은 이 측정에서 과하게 올린 텍스처가 예산의 10 % 를 넘을 때만 한다. 의존: R6-5 · MR-2 · R6-2.

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

**이전 순서.** ① `TextureMipLayout` + `readRangeAsync`(읽는 쪽 없이 들어간다) → ② 올리기 줄을 `TextureMipRequest` 로 바꾸고 `StreamingUpgrade::_pImage` 삭제(첫 로드는 아직 전체 읽기 — 이때 줄의 RAM 이 0 이 된다) → ③ 첫 로드를 머리 + 꼬리로, 쿠킹 규칙(`.dds` 무압축). 동기 로드(도구 · 미리보기, `AssetLoadStart::OnCallingThread`)는 전체 읽기를 유지한다.

**측정.**

| 무엇 | 시나리오 · 지표 | 판정 |
|---|---|---|
| 줄이 쥔 바이트 | `load/*`, 새 게이지 `TextureStreaming.bytesInFlight` | 늘 `_maxBytesInFlight` 아래, 대기 상태 바이트 0 |
| 첫 화면 | `load/*` × 워커 1 · 4, `Assets.Pending` 이 0 이 되는 시간과 `AssetLoadScope` `Texture` 가 읽은 바이트 | ③ 뒤 읽은 바이트가 2K 이상 텍스처에서 전체의 10 % 아래, 시간은 ② 보다 흔들림 밖으로 느리지 않다(느리면 첫 로드만 전체 읽기로 되돌리고 값을 S8 표에) |
| 올리기 지연 | 화면에 든 뒤 전체 밉까지의 프레임 수(p95) | ② 앞보다 10 프레임 넘게 늘지 않는다(늘면 `_maxBytesInFlight` · 프레임당 제출 수를 올린다) |
| 팩 | `-cooked-content` 로 같은 시나리오 | `Pack.RangeReadOnCompressedEntry` 0 |

**일 목록.**

- **MR-1 밉 배치와 범위 읽기 입구** — `Asset/Image/TextureMipLayout.{h,cpp}`(새 파일), `DDSLoader.{h,cpp}`(머리 풀이를 공유), `Core/File/ContentFileSource.{h,cpp}` · `BundledContentFileSource.h` · 팩 소스, `PackReader.{h,cpp}`(무압축 구간 · 압축 항목 오류). 테스트: `TextureMipLayoutTest.OffsetsMatchWholeFileDecode`(BC1 · BC7 · RGBA8 · ASTC) · `TruncatedFileIsError`, `ContentFileSourceTest.RangeMatchesWholeRead`(소스마다), `PackReaderTest.RangeReadOnCompressedEntryIsError`. 의존: 없음.
- **MR-2 올리기 줄을 밉 요청으로** — `TextureCache.cpp`(`StreamingUpgrade` → `TextureMipRequest`, 읽는 중 목록, 바이트 상한), `Texture2D.{h,cpp}`(`beginUploadMips`, `computeMipDrop` 을 배치로). 테스트: `TextureCacheStreamingTest.QueueHoldsNoImageBytes` · `BytesInFlightStaysUnderCap` · `ReleasedWhileReadingIsDropped`. 의존: MR-1(R6-5 와 같은 파일이라 그 뒤에 맡긴다).
- **MR-3 첫 로드를 머리 + 꼬리로, 쿠킹 규칙** — `TextureLoadOperation`(`TextureCache.cpp`), `PackConfig.h` · 쿠킹 검사. 테스트: `TextureCacheStreamingTest.FirstLoadReadsTailOnly`, 쿠킹 사례 `PackCooker.CompressedTextureEntry`. 의존: MR-2.

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

1. **GPU 프레임 시간은 늘 컴파일되는 `GPUFrameClock` 이 낸다.** 프레임 그래프의 첫 명령과 마지막 패스 끝에 타임스탬프 한 쌍(`kGPUTimestampSlotFrameBegin` + 새 `kGPUTimestampSlotFrameEnd`)을 쓰고 끝난 프레임의 차이를 읽는다. 계측 스위치(`setTimestampEnabled`)의 주인이 `GPUPassTimeline` 에서 이 시계로 옮겨 온다 — 켜는 조건은 "프로파일러 기록 중 또는 자동 배율 켜짐" 이고, 읽은 `RHITimestampFrame` 하나를 프로파일러(`GPUPassTimeline::reportFrame`)도 받는다. 쿼리는 한 벌, RHI ABI 는 그대로다.
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

**이전 순서.** ① `GPUFrameClock` + 조정기 입력 형식(이것만으로 "배율이 돌아오지 않는다" 가 고쳐진다 — 재할당은 아직 있다) → ② 뷰 상수 넷과 `PassTexture` 변환(배율 1 에서는 값이 1 이라 그림이 같다 — 골든으로 확인) → ③ 풀을 최대 크기로, 뷰포트 · 시저 · 디스패치를 사각형으로, 업스케일(`FrameUpscale`)의 입력 크기를 사각형으로. `gv_renderScale`(사용자 설정)을 바꾸면 지금처럼 다시 만든다.

**측정.**

| 무엇 | 시나리오 · 지표 | 판정 |
|---|---|---|
| 배율 복귀 | 단위 `RenderScaleAdjusterTest`(아래), Shooter3D `benchmark` vsync 켬 + 자동 배율, `RenderPacketBuilder.AutoRenderScale` 로그 | 부하가 걷힌 뒤 `_upFrames` × 단계 수 프레임 안에 1.0 |
| 한 쌍의 비용 | `game/empty/automation/benchmark` dx12 · vk · gl, 시계 켬 · 끔 5 번, `RT.Frame` p50 | 차이가 흔들림 안(밖이면 값을 S8 표에 적는다 — 시계는 자동 배율이 켜졌을 때만 켜지므로 결정은 그대로다) |
| 단계 프레임 | 개발 변수로 배율을 0.7 ↔ 1.0 으로 바꾸는 새 시나리오 `renderscalestep`, 새 카운터 `RT.Frame.transientAllocations`, 그 프레임의 `RT.Frame` | 할당 0, 프레임 시간이 중앙값의 1.2 배 아래 |
| 그림 | `renderscalestep` 의 0.7 그림을 `gv_renderScale=0.7`(다시 만드는 길)의 그림과 견준다, 다섯 백엔드 | 차이 1 % 아래, 화면 가장자리에 사각형 밖 텍셀이 번지지 않는다 |

**일 목록.**

- **RS-1 `GPUFrameClock`** — `Renderer/Frame/Timeline/GPUFrameClock.{h,cpp}`(새 파일), `GPUPassTimeline.{h,cpp}`(스위치 · 읽기를 넘긴다), `FrameRendererUtil.h`(끝 슬롯), `FramePassExecutor.cpp:227`(시작 슬롯 쓰기를 시계로), `FrameRenderer.{h,cpp}`. Shipping 구성에서 백엔드의 타임스탬프 제공자와 `writeTimestamp` 가 컴파일되는지 먼저 확인하고, 프로파일 매크로로 가려 있으면 푼다. 테스트: `GPUFrameClockTest.SampleIsEndMinusBegin` · `UnrecordedSlotGivesNoSample` · `DeviceWithoutTimestampsIsUnsupported` · `SerialAdvancesOncePerReadBack`. 의존: 없음.
- **RS-2 조정기 입력과 설정** — `RenderScaleAdjuster.{h,cpp}`, `Config/Engine/RenderScale.json`, `RenderPacketViewSetup.{h,cpp}`. 테스트: `RenderScaleAdjusterTest.ScaleReturnsWhenGPUHasHeadroomAtFixedVsyncInterval`(감사의 "16.67 ms 고정에서 배율 복귀") · `SameSerialCountsOnce` · `SamplesAfterAStepAreDiscarded` · `UnsupportedClockTurnsAutoScaleOff`. 의존: RS-1(`GPUFrameSample` · `getLatest`).
- **RS-3 뷰 사각형 상수와 `PassTexture` 변환** — `binding.hlsli` · `binding_slots.hlsli`, 뷰 상수 구조(`ShaderBindingSlots.h` 계약), 텍셀 번호로 읽는 셰이더, TAA · 깊이 히스토리 셰이더. 테스트: 셰이더 계약 검사, 기존 골든 전부(배율 1 에서 그대로). 의존: 없음.
- **RS-4 최대 크기 풀과 사각형 그리기** — `FrameViewResources.{h,cpp}`, `FrameViewTarget.h`, `RenderView.{h,cpp}` · `RenderViewCollector.cpp`(`_resolutionScale` 을 풀 배율과 사각형 배율로 가른다), `FramePassExecutor*.cpp`(뷰포트 · 시저), 컴퓨트 패스의 디스패치 크기, `FrameUpscale.cpp`. 테스트: `FrameViewResourcesTest.AutoScaleStepAllocatesNothing` · `HistorySurvivesAutoScaleStep`, 시나리오 `renderscalestep`. 의존: RS-3.

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

**이전 순서.** ① 정의 칸 · 로드 검사 · 스냅샷 전달(그림은 그대로) → ② CPU 순서 목록과 셰이더 간접 읽기 → ③ GPU 정렬 컴퓨트. 기본값이 `None` 이라 기존 효과와 골든은 바뀌지 않고, 연기류 효과만 데이터에서 `ViewDepth` 를 켠다.

**측정.**

| 무엇 | 시나리오 · 지표 | 판정 |
|---|---|---|
| 그림 | 새 시나리오 `game/empty/automation/vfxsort` — 알파 연기 방출기(CPU 하나 · GPU 하나)를 주 시점과 180° 반대편 PiP 로 한 프레임에 본다, 다섯 백엔드 골든 | 두 뷰 모두 먼 입자가 먼저 그려진다. 정렬을 끈 그림과 5 % 넘게 달라야 한다(같으면 장면이 정렬을 드러내지 못한 것) |
| CPU 정렬 비용 | 방출기 32 × 입자 256 의 합성 장면, 새 스코프 `RT.VFX.sortParticles` | 프레임당 0.2 ms 아래(넘으면 깊이를 16 비트로 양자화한 기수 정렬로) |
| GPU 정렬 비용 | 같은 장면 GPU 대상, `GPU.VFXSort` | 0.1 ms 아래 |
| `None` 의 비용 | `benchmark` · VFX 표본 장면의 투명 패스 GPU 시간, 이 일 앞뒤 | 흔들림 안(밖이면 `None` 을 간접 읽기 없는 PSO 변형으로) |

**일 목록.**

- **PS-1 정의 · 검사 · 순서 계산** — `VFX/VFXEffectDef.{h,cpp}`(칸 · 로드 검사), `Renderer/Scene/GPUSceneSnapshot.h` · `GPUSceneBuilder.cpp`(스냅샷에 싣기), `Renderer/VFX/VFXParticleOrder.{h,cpp}`(새 파일). 테스트: `VFXEffectDefTest.AdditiveWithSortModeIsLoadError` · `RibbonWithSortModeIsLoadError` · `GPUViewDepthOverSortLimitIsLoadError`, `VFXParticleOrderTest.ViewDepthDrawsFarthestFirst` · `OppositeEyeReversesOrder` · `EqualDepthKeepsIndexOrder` · `AgeOrdersIgnoreEye`. 의존: 없음.
- **PS-2 CPU 방출기 그리기** — `VFXParticleRenderer.{h,cpp}`(순서 버퍼 · 뷰별 구간), `FramePassExecutor` 의 `drawParticleSlots`(뷰 번호), `particle.hlsl` · `binding_slots.hlsli`(순서 버퍼 슬롯). 테스트: 셰이더 계약 검사, 시나리오 `vfxsort`(CPU 쪽 골든). 의존: PS-1.
- **PS-3 GPU 방출기 정렬** — `bitonic_sort.hlsli`(새 파일, `instance_sort.hlsl` 이 include), `vfx_particle_sort.hlsl`(새 파일), `VFXGPUSimulation.{h,cpp}`(순서 버퍼 · 뷰별 디스패치 · 전이 묶음), 파이프라인 XML 의 컴퓨트 단계, `ShaderBindingSlots.h`. 테스트: 계약 검사(`kMaxGPUSortedParticles` 와 셰이더 상수), `instance_sort` 를 쓰는 기존 골든 유지, `vfxsort`(GPU 쪽 골든). 의존: PS-2.

### DX12 · Vulkan 기록 경로 락의 남은 자리(설계 2026-11-17, NEXT RHI 5-2, 감사 R-4 · R-12)

**상태.** NEXT 5 절 RHI 5-2 가 남은 일로 적은 "DX12 기록 경로 락(`resolveBufferAddress` · `resolveOfflineView` 의 `shared_mutex`, `_resourceStateMutex`)" 은 **이미 들어갔다**(291d69074e, 2026-11-10 — NEXT 의 줄이 병합에서 되살아난 것으로 보인다. 통합이 지운다).

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
| 들어간 락 제거의 값 | S8 표의 "DX12 기록 경로 락 제거 전후 `bench.json` p50" — 291d69074e 와 그 앞 커밋, `benchmark` · Shooter3D dx12 5 번 | 값을 S8 표에 적고 닫는다(나빠졌으면 그 커밋을 본다) |
| 미러 락 경합 | 같은 장면 dx12 · vk, `-gv_renderGraphParallelMinWidth=2`, 새 스코프 `RT.Bind.mirrorLockWait`(`RHIConstantBufferMirror::_mutex` 를 기다린 시간)의 합과 새 카운터 `RT.Bind.constantBufferWrites` | 대기 합이 `RT.Graph.parallelLevel` 합의 2 % 를 넘으면 L-2 를 한다. 아래면 값을 적고 닫는다 |
| L-2 뒤 | 같은 실행의 RT 프레임 중앙값 | 흔들림 밖으로 빨라지지 않으면 되돌린다 |

**일 목록.**

- **L-1 진단 읽기를 락 없이 + 측정 준비** — `D3D12RHIDevice.cpp`(`isBindlessIndexLive`), `RHIConstantBufferMirror.h`(락 대기 스코프), `D3D12RHIResourceFactory.cpp` · `VulkanRHIResourceFactory.cpp`(카운터). 테스트: 가짜 장치 없이 도는 `BindlessReadSlotTest.RetiredIndexIsNotLive`(읽기 표만), 기존 `RHIBindlessRegistryTest` 유지. 의존: 없음. 통합이 위 두 측정을 돌린다.
- **L-2 상수 버퍼 기록 값을 레코드로**(문턱을 넘을 때만) — `D3D12RHIDevice.{h,cpp}` · `D3D12RHIResourceFactory.cpp` · `D3D12RHICommandContext.cpp`, Vulkan 대응 셋, `Backend/RHIConstantBufferMirror.{h,cpp}`. 테스트: 기존 `RHIConstantBufferMirrorTest.FillsStaleSlotsOnceAndForgets` 유지 · `SecondWriteInAFrameIsReported`, lightculling · shadow_* · postprocess_* 시나리오 dx12 · vk. 의존: L-1 의 측정, R6-3 · R6-4 와 같은 파일이라 그 뒤.

## 코어 · 런타임 설계(2026-11-17)

"측정 · 설계가 먼저" 라 미뤄 둔 코어 · 런타임 항목의 설계다. 절마다 문제 · 대조 · 결정과 기각 · 초안 · 이전 순서 · 측정 · 일 목록 순서로 적었고, 일 하나가 작업자 하나의 몫이다(코드 → 린트 → 커밋, 빌드 · 측정은 통합). 줄 번호는 main `caf89e45df` 의 것이다. 측정 값은 위 "측정과 성능 후보(S8)" 표에 줄을 더해 적는다. C++ 초안은 C++17 이고 이름은 작업자가 [NamingCpp](agents/NamingCpp.md) 로 다시 대조한다. 외부 제품의 동작(Keycloak · IoStore · MSVC STL …)은 기억 기준이라 고치기 전에 그 판의 문서로 대조한다.

| 절 | 측정 없이 바로 하는 것(결함 · 계약) | 측정이 먼저인 것 |
|---|---|---|
| 구독 제어 블록 | 전부(C20-1 → 2 → 3) | 스레드 정책 나누기(벤치 문턱을 넘을 때만) |
| 해시 연산 줄 | 세 단계 · 인터페이스 나누기 · 대기 상한(O3-1 ~ 4) | 상한 값 맞추기(부하 봇 열린 루프 O-12 뒤) |
| 팩 규모 | 내용 해시 CR 결함 · 영속 캐시 · 스트림 쓰기 · 색인 서명 | 정렬 배열 색인의 조회 비용, 블록 크기 |
| 할당 · 컨테이너 | 벤치 넷(먼저 지는 것을 본다) | 고치기 전부, mimalloc 은 사용자 확인도 |
| 병렬 오브젝트 | 없음 | 서비스 파도 · 캐릭터 일괄 이동, 격자 셋은 옮기지 않는다 |
| 리플렉션 | 문화권 상속 · 방문자 1 단계 · 복제 스키마 | 생성 바이트 다음 후보 · 인스펙터 캐시 · 타입 색인 |

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

| 일 | 바꿀 파일 | 남길 테스트 | 의존 |
|---|---|---|---|
| C20-1 테스트 먼저 | `Core/Delegate/Tests/TestDelegate.cpp` | `SourceDestroyedOnAnotherThreadWhileSubscriptionsReset`(스레드 둘, 1 만 번 — ASan 에서 지는 것을 본다) · `SourceCloseWaitsForReleaseInProgress` · `SourceClosedInsideItsOwnReleaseDoesNotWait` · `SourceLeftOpenAtDestructionIsFatal` · bench `SubscribeCost` | 없음 |
| C20-2 제어 블록 | `Subscription.h`, 파생 여덟(`Delegate.h` · `DelegateSlot.h` · `AtomicSlot.h` · `LockedMulticastDelegate.h` · `SubscriptionList.h` 둘 · `Core/Registry/RegistrationList.h` 의 `NameRegistry` · `Save/SaveLock.*`)의 소멸자에 `closeSubscriptions()` | 기존 `DelegateTest.*` 그대로 통과 | C20-1 |
| C20-3 머리말 | `LockedMulticastDelegate.h`("약 2 초" 를 코드대로), `Subscription.h` 스레드 주석 | — | C20-2 |

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

**이전 순서.** 테스트(가짜 `IPasswordHasher` 가 부른 스레드를 적는다) → 인터페이스 나누기와 세 단계 → 줄과 설정 → 앞문 상한 → 부하 봇 구성.

**측정.** 부하 봇 `login_storm` 을 `_bLightPasswordHash` 없이(새 구성 파일), 해시 처리율의 0.5 · 1 · 2 배 도착률로 돌린다. 도착률 고정은 O-12 의 열린 루프가 있어야 하고, 그 전에는 닫힌 루프로 대기 최고치만 본다. 볼 값: `service_store_pending_peak`, 새 지표 `login_hash_queue_depth` · `login_hash_wait_ms` · `service_requests_shed_total`, 폭주 중 다른 키트(`chat_send` · `leaderboard_top`)의 p99, 프로세스 최고 메모리. **문턱:** 2 배 도착률에서 다른 키트 p99 가 한가할 때의 1.5 배 이내, 저장소 대기 최고치가 상한 식 이내, 최고 메모리가 기준 + `_maxConcurrent × 38 MiB` 이내, 거절은 모두 `kBusy`(시간 초과 · 끊김 0). 고치기 전 값도 같은 표에 남긴다.

| 일 | 바꿀 파일 | 남길 테스트 | 의존 |
|---|---|---|---|
| O3-1 테스트 먼저 | `Account/Server/Tests/TestAccountServer.cpp` | `LoginHashNeverRunsOnAStoreWorker` · `UnknownNameStillPaysOneHash` · `ConflictAfterHashReusesTheVerifiedHash` · `LoginIsBusyWhenTheHashQueueIsFull` | 없음 |
| O3-2 인터페이스 · 세 단계 | `LoginTypes.h` · `NetSecurityLoginCrypto.*` · `Rule/LoginStoreLogic.*` · `Service/LoginService.*` | 위 넷 중 앞 셋, 기존 `AccountServer*` 테스트 | O3-1 |
| O3-3 연산 줄 | 새 `Service/PasswordHashQueue.*`, `AccountKitConfig.h`, `AccountOnlineServerKit.cpp`, `Config/Server/Online/*.online.json` | `PasswordHashQueueTest.TicketBoundsInFlightAndQueued` · `ShutdownFinishesRunningAndCancelsQueued` · `MissingLimitIsAStartupError` | O3-2 |
| O3-4 앞문 상한 | `Runtime/Online/Service/Server/OnlineServiceHost.*`(`OnlineServiceHostConfig::_maxStorePending`), `ServiceMetrics.*` | `OnlineServiceHostTest.ShedsNewRequestsWhenStorePendingIsAtLimit` · `AdmittedRequestStillCompletes` | 없음(O3-2 와 병렬) |
| O3-5 부하 구성 | `Config/Server/Online/` 실제 해시 구성, `Source/Programs/OnlineLoadBot` 시나리오 | 통합 실행(위 문턱) | O3-3 · O3-4, 도착률은 O-12 |

근본 원인은 "느린 연산과 연결 점유가 한 일에 묶였다" 이다. 같은 모양 후보는 영수증 검증(이미 `_verifications` 로 저장소 밖)과 저장소 일 안의 서명 · 압축이다 — O3-2 작업자가 저장소 람다 안의 `ILoginCrypto` 밖 암호 호출을 한 번 찾아 보고에 적는다.

### 팩 규모 — 스트림 쓰기 · 블록 표 · 색인 서명 · 해시 캐시(설계 2026-11-17, 감사 A-9 ~ A-12 · 다음 할 일 18)

**문제.**

- **쓰기(A-10).** `PackWriter::addBytes`(`PackWriter.cpp:132-139`)가 모든 항목의 원본을 들고, `makePackBytes` 가 압축 결과와 팩 이미지를 한 벡터로 지은 뒤 쓴다(:507). `CookStore.cpp:90-98` 은 쓴 출력을 다시 읽어 해시한다. 팩은 도메인당 하나다.
- **색인(A-12).** FAT 은 디스크에서 이미 경로 해시 오름차순인데 `PackReader::loadIndexTable`(`PackReader.cpp:464-510`)이 항목마다 경로 `string` 사본을 지어 `unordered_map<uint64, PackFileEntry>` 에 넣고, 폴더 나열용 `unordered_map<string, vector<uint64>>` 를 또 짓는다. 읽기는 항목 통째(`readFile` · `readFileAsync`)뿐이라 밉 범위 읽기(R-6)가 막힌다.
- **서명(A-9).** `PackManager::verifyPatchSignatures`(`PackManager.cpp:685-703`)가 기동마다 사슬의 패치 팩 전체를 메모리로 읽어 SHA-256 을 세고(`PackPatch.cpp:86-99`, 2 GiB 이상 거절), 그 바이트와 뒤에 마운트하는 핸들이 다르다.
- **증분 쿠킹(A-11).** `CookRecord::findChangedInput`(`CookRecord.cpp:154-158`)이 매번 모든 입력을 해시한다. `ContentHashCache`(`ContentHash.cpp:122-158`)는 도장(크기 · 시각)으로 거르지만 프로세스 메모리뿐이다. `ContentHash::computeBytes`(:54-66)는 바이너리에서도 0x0D 를 건너뛰어 그 자리만 다른 두 파일이 같은 해시다(결함).

**대조.** 언리얼 IoStore 는 `.ucas` 에 청크를 이어 쓰고 끝에 `.utoc`(완전 해시 TOC + 64 KiB 압축 블록 표 + 블록 해시 서명)를 쓰며 컨테이너를 크기 · 청크 id 로 나눈다. pak 은 `.sig` 블록 해시 표다. 유니티 AssetBundle 은 LZ4 청크 단위 압축이라 부분 읽기가 되고 Addressables 카탈로그가 묶음 해시를 든다. Godot PCK 는 파일마다 MD5 를 두지만 블록 압축 · 부분 검증은 없다. 증분 판정은 git 색인(도장 → 해시, 쓴 시각과 겹친 도장은 믿지 않는다)과 언리얼 `FFileHashCache` 다.

**결정.**

1. **형식 3 판 — 색인을 끝에.** 구역 순서를 헤더 · 페이로드 · FAT · 블록 표 · (블록 해시 표) · 스트링 풀 하나로 한다(지금의 `Slot` 배치 순서, 리더는 이미 헤더의 오프셋만 믿는다). `PackLayoutMode` 는 페이로드 자리 규칙으로만 남는다.
2. **스트림 쓰기.** 항목을 경로 해시 순으로 하나씩(병렬 압축은 바이트 창 `_writeWindowBytes` 안에서, 쓰기는 순서대로) 읽고 · 압축하고 · 이어 쓴 뒤 색인과 헤더를 쓴다. 쓰는 동안 내용 해시와 색인 SHA-256 을 섞어 돌려주므로 다시 읽지 않는다.
3. **블록 표.** 모든 항목을 `_blockSize`(PackConfig, 2 의 거듭제곱 64 ~ 256 KiB — 첫 값 128 KiB)로 잘라 블록마다 압축한다. 작은 항목은 블록 하나다. CRC 는 블록마다로 옮긴다. `readRange` 가 걸친 블록만 읽는다.
4. **색인 서명.** 패치 목록은 팩 전체 대신 **색인 구역**(헤더 · FAT · 블록 표 · 블록 해시 표 · 스트링 풀)의 SHA-256 을 서명한다. 리더가 여는 핸들로 색인을 검증한 뒤에만 마운트하고(검증한 핸들이 곧 읽는 핸들), 서명 채널의 팩(`PackFlag::HasBlockHash`)은 읽을 때 블록마다 SHA-256 을 견준다. 기동 비용은 색인 크기에만 비례하고 2 GiB 한도가 사라진다.
5. **정렬 배열 색인.** 리더는 디스크 FAT 을 그대로 든다(정렬이 아니면 마운트 거절, 이진 탐색). 경로는 스트링 풀을 가리키는 `string_view`, 폴더 나열은 (폴더 해시, 항목 번호) 정렬 배열이다.
6. **분할.** PackConfig `_split`(없으면 나누지 않는다, 있으면 `_maxPackBytes` 필수 + 패턴 → 청크 이름 규칙). 경로 해시 순으로 잘라 결정적이고, 이름은 `<도메인>.<청크>.<번호>.pack`. 마운트 도메인은 풀 머리에 있어 리더는 그대로 붙이고, 같은 도메인의 형제 팩 사이 겹침은 오류다(뒤가 이기는 것은 패치뿐).
7. **해시 캐시 영속.** `ContentHashCache` 를 `DerivedData` 에 쓴다(경로 → 크기 · 수정 시각 · 해시). 저장 시각의 시간 창 안에 고쳐진 도장은 싣지 않는다(지금의 `isCompleted` 규칙을 파일로 넓힌 것). 캐시 파일이 없거나 판이 다르면 빈 캐시다 — 캐시라 폴백이 아니다.
8. **내용 해시는 원시 바이트.** CR 건너뛰기를 없앤다. `.gitattributes` 가 `* text=auto eol=lf` 라 추적 파일은 어느 기계에서나 같은 바이트다. 쿠킹 키 판이 올라 한 번 전체 재쿠킹이다.

- 기각 — CR 정규화를 텍스트 확장자에만(감사의 제안): 무엇이 텍스트인지를 `.gitattributes` 와 해시 코드 두 곳이 알게 된다.
- 기각 — 해시를 xxh3 로 지금: 영속 캐시가 있으면 바뀐 파일만 해시한다. 차가운 쿠킹에서 해시가 10 % 를 넘을 때만(새 의존성 BSD-2 — 사용자 확인).
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

**이전 순서.** 벤치 · 합성 팩 → 해시(독립) → 형식 3 판 + 스트림 쓰기 → 리더 → 서명 → 분할. 옛 팩은 리더가 거절하고 쿠커가 다시 쓴다(지금의 판 올림 규칙, 배포된 팩은 아직 없다). 패치 목록의 `_packSHA256Hex` 는 `_indexSHA256Hex` 로 바뀐다(목록 판도 올린다).

**측정 — 30 만 항목 합성 팩.** `PackScaleTest`(`._tags = "bench"`)가 스크래치 폴더에 합성 팩을 짓는다: `index300k`(30 만 × 256 ~ 1024 B, 세 단계 폴더 · 경로 48 자 안팎), `patch1g`(1 GiB 패치 + 목록), `bulk10g`(2000 × 5 MiB, 절반은 압축되는 바이트 — 디스크 10 GiB 라 손으로만). 고치기 전 값을 먼저 S8 표에 적는다.

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

| 일 | 바꿀 파일 | 남길 테스트 | 의존 |
|---|---|---|---|
| PK-1 벤치 · 합성 팩 | 새 `Content/Pack/Tests/TestPackScale.cpp` | `PackScaleTest.*`(bench) | 없음 |
| PK-2 내용 해시 | `Content/ContentHash.*`, `Asset/Cook/CookRecord.cpp`(키 판) | `ContentHashTest.BinaryWithMovedCRHashesDifferently`(먼저 진다) · `ContentHashCacheTest.SecondProcessReadsNoBytes` · `StampInsideTheWriteWindowIsHashedAgain` · `CacheFromAnotherVersionIsDropped` | 없음 |
| PK-3 형식 3 판 · 스트림 쓰기 | `PackTypes.h` · `PackWriter.*` · `PackCompressionUtil.*` · `Asset/Cook/PackCooker.cpp` · `CookStore.cpp` · `PackConfig.h` · `Config/Engine/PackConfig.json` | `PackWriterTest.StreamedPackReadsBackEveryEntry` · `OutOfOrderAppendIsAnError` · `WriteResultHashEqualsHashOfTheFile` | PK-1 |
| PK-4 리더 | `PackReader.*` · `PackManager.cpp` · `PackFileEntry` 를 쓰는 곳 | `PackReaderTest.UnsortedIndexIsRefused` · `RangeReadEqualsTheSameSliceOfAWholeRead` · `CorruptBlockIsRefused` · 기존 `PackReaderTest.*` | PK-3 |
| PK-5 색인 서명 | `PackPatch.*` · `PackPatchBuilder.*` · `PackManager.cpp` · `PackPatchTrustSource.*` | `PackPatchTest.TamperedIndexFailsAtMount` · `TamperedPayloadBlockFailsAtRead` · `PatchOverTwoGiBIsAccepted`(성긴 파일) | PK-4 |
| PK-6 분할 | `PackConfig.h` · `PackCooker.cpp` · `PackManager.cpp` | `PackCookerTest.SplitIsDeterministicAndBounded` · `PackManagerTest.SiblingPacksOverlapIsAnError` | PK-3 |

근본 원인은 "팩 하나를 한 벡터로 다룬다" 이다. 같은 모양 후보는 DDC 저장(`DerivedDataCache::store` 가 출력 통째)과 `PackPatchBuilder` 의 앞 빌드 팩 읽기다 — PK-3 작업자가 통째 읽는 자리를 세어 보고에 적는다.

### 출하 할당기와 컨테이너 복잡도(설계 2026-11-17, 감사 C-6 · C-9 · 다음 할 일 20)

감사의 순서대로 **벤치가 먼저**다. 벤치 숫자가 문턱을 넘은 것만 고치고, 넘지 않으면 값을 S8 표에 적고 닫는다.

**C-6 출하 할당기 — 문제.** Shipping 의 `Memory::allocate` · `allocateAligned`(`Source/Core/Memory/Memory.cpp:148-158 · 224-234`)가 `::malloc` · `_aligned_malloc` 그대로다. 스레드 캐시와 크기 등급이 없어 워커가 함께 할당하면 CRT 힙 잠금에서 다툴 수 있다(측정 필요).

**대조.** 언리얼은 `FMallocBinned2/3`(크기 등급 + 스레드 캐시)가 기본이고 mimalloc 을 고를 수 있다. 유니티는 TLSF `DynamicHeapAllocator` + 스레드별 버킷 + 잡용 선형 할당자다. Godot 는 시스템 `malloc` 에 `PagedAllocator` 풀을 얹는다.

**결정.** 두 걸음으로 잰다. ① 의존 없이: 프레임당 할당 수 × CRT 한 번 비용으로 프레임 몫을 추정한다. ② 문턱을 넘으면 mimalloc(MIT)을 `Memory` 한 입구 뒤에 Shipping 만 끼워 재고, 이득이 흔들림 밖이면 둔다. 입구는 변형 파일 하나로 가른다([BuildConfigSwitch](agents/BuildConfigSwitch.md)). **mimalloc 도입은 새 의존성이라 사용자 확인 뒤다.**

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
| `vector::insert( pos, first, last )`(`vector.h:983`) | 원소마다 한 칸 삽입 — O(개수 × 꼬리), 여러 번 재할당. `ByteStream.h:54` · `Archive.cpp:112` 가 부른다 | 1 MB 를 4 KB 꼬리 앞에 범위 삽입 | `reserve` + `memcpy` 기준의 2 배 이상 | 전방 반복자는 거리 → 한 번 키우기 → 꼬리 한 번 밀기 → 범위 생성(자명 복사는 `memmove`), 입력 반복자는 임시 벡터로 모아 같은 길 |
| `unordered_map::reserve`(`unordered_map.h:645`) | 버킷만(`rehash`) | 10 만 개 `reserve` 뒤 삽입의 할당 수 | 0 이 아니면 | 값 배열도 예약 |
| `map` 범위 구축(`map.h` 정렬 벡터) | 원소마다 정렬 삽입 — O(n²) | 정렬 안 된 10 만 키 범위 구축 | `stable_sort` 기준의 2 배 이상 | 붙이고 `stable_sort` 뒤 겹친 키는 앞 것만 |
| `deque`(`deque.h:14`, `std::deque` 래퍼) | MSVC STL 은 8 B 넘는 원소마다 블록 하나 | 64 B 원소 1 만 번 `push_back` + `pop_front` 의 할당 수와 ns(Windows · Linux) | 원소당 할당 0.5 번 이상 | 아래 |

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

| 일 | 바꿀 파일 | 남길 테스트 | 의존 |
|---|---|---|---|
| AC-1 벤치 넷 + 할당 벤치 | 새 `Core/Container/Tests/TestContainerBench.cpp` · `Core/Memory/Tests/TestMemoryBench.cpp` | `ContainerBenchTest.*` · `MemoryBenchTest.AllocateFreeCost`(bench) | 없음 |
| AC-2 범위 삽입 · `reserve` · `map` 구축(문턱을 넘은 것만) | `vector.h` · `unordered_map.h` · `map.h` | `VectorTest.RangeInsertMovesTheTailOnce`(옮김 수를 세는 원소) · `UnorderedMapTest.ReservedInsertsDoNotAllocate` · `MapTest.RangeBuildKeepsTheFirstOfEqualKeys` | AC-1 값 |
| AC-3 `deque` 링 + `ChunkedArray` | `deque.h` · 새 `ChunkedArray.h` · 위 주소 안정 자리 · `Engine/Reflection/ReflectionContainers.h` | `DequeTest.WrapsAndGrowsKeepingOrder` · `EraseAndInsertMatchStdDeque`(무작위 연산 대조) · 기존 `TestDataStructure` | AC-1 값 |
| AC-4 `MemoryBackend` 변형 파일(① 이 넘을 때만, mimalloc 은 사용자 확인 뒤) | `Memory.cpp` · 새 `MemoryBackend.*` · `Source/Core/CMakeLists.txt` · vcpkg 목록 | `MemoryBackendTest.AlignedBlocksRoundTrip` | AC-1 값 · 사용자 확인 |

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

**측정.** 서비스 갱신마다 구간 `GT.Scene.service.<이름>` 을 넣고(`getSubsystemName`), Release 로 `load/characters` · `load/housingbulk` · `load/townsfolk` · Shooter3D `benchmark` 를 3 번씩. **문턱:** 한 단계의 직렬 서비스 합이 어느 시나리오든 0.3 ms 이상이고 서로 겹치지 않는 서비스 둘 이상이 각각 0.1 ms 이상이면 그 둘부터 옮긴다. 아니면 값을 S8 표에 적고 닫는다(서명 나누기도 하지 않는다 — 옮길 서비스가 없으면 형식만 늘어난다). 옮긴 뒤에는 `GT.Scene.tick.FlushEnd` 가 흔들림 밖으로 줄어야 하고 상태 해시(`TestSceneStateHash`)가 같아야 한다.

**(나) 캐릭터 컨트롤러 3D · 2D 입력을 `ComponentDataStore` 페이지로 — 이미 들어갔다.** `4e4920e67a` 가 입력을 `PhysicsCommandPage<CharacterCommand3D/2D>`(`Engine/Physics/Scene/PhysicsPoseStore.h:181`)로 옮겼다. NEXT 의 그 줄은 통합이 지운다. 남은 병목은 감사 S-8 이다 — `CharacterControllerComponent.cpp:310-377` 이 스텝마다 게임 스레드에서 한 명씩 움직이고 Jolt `ExtendedUpdate` 가 씬 공용 임시 할당자를 쓴다. 설계: 물리 씬에 `moveCharacters( span<const CharacterMoveRequest>, span<CharacterMoveResult> )` 일괄 입구를 두고 백엔드가 레인마다 임시 할당자(S-2)를 들고 병렬 for 로 돈다(유니티 DOTS Character Controller 잡 자리). 감사 37 번 묶음(S-2 · S-7 · S-8)에서 같이 한다. **측정 · 문턱:** `load/characters`(500 명)의 물리 PreStep 을 워커 1 · 4 · 8 로 — 한 워커에서 1 ms 이상일 때만 하고, 4 워커에서 2.5 배 이상 빨라지지 않으면 되돌린다.

**(다) 근접 기믹 · 감지 · 넷 관심 격자를 `SceneSpatialGrid` 로 — 셋 다 옮기지 않는다.**

| 자리 | 지금 | 판정 | 다시 볼 조건 |
|---|---|---|---|
| 근접 기믹(`GimmickCircuitComponent::computeProximity`, `GimmickCircuitComponent.cpp:387`) | 노드마다 `TagIndex::findObjectsWithTags` 로 고른 뒤 가장 가까운 거리 | 태그 색인이 이미 후보를 줄이고, 거리 상한이 없어 격자가 가지를 못 친다 | 프레임당 (근접 노드 × 태그 후보)가 어느 게임 씬에서든 1 만을 넘으면 노드에 필수 `range` 칸을 더하고 태그 층을 격자에 올린다 |
| 감지(`AIPerception::sense`) | 후보 목록을 부르는 쪽이 넘긴다 — 쓰는 곳은 AsymmetricHorror 키트 하나, 후보는 플레이어 수 | 순수 함수라 격자와 무관하다 | 씬 컴포넌트에서 자극을 64 개 넘게 모으는 키트가 생기면 그 키트가 `PhysicsCharacter3D` 층을 질의해 후보를 만든다(감각 코드는 그대로) |
| 넷 관심 격자(`Core/Network/Replication/NetInterestGrid`) | 엔티티 id 의 점 격자 + 들어오는 · 나가는 반경, `MMOReplicator` · `InterestReplicationPolicy` 가 움직인 것만 `setPoint` | Core 층이라 씬을 모르고, 원본이 서버 엔티티 표이며(씬 컴포넌트가 아닐 수 있다), 이미 증분이다. 밑 구조는 같은 `SpatialHashGrid2D` 다 | 없음 — 언리얼 `UReplicationGraphNode_GridSpatialization2D` 도 씬 가속 구조와 따로 둔다 |

재는 것은 근접 하나다: `GimmickCircuitComponent` 틱에 (노드 수, 후보 수) 카운터 `Gimmick.ProximityCandidates` 를 두고 게임 12 개 `slice` 에서 최고치를 본다.

| 일 | 바꿀 파일 | 남길 테스트 | 의존 |
|---|---|---|---|
| PO-1 서비스 구간 · 근접 카운터 | `GameObjectManager.cpp`(서비스 갱신 루프) · `GimmickCircuitComponent.cpp` | 통합 측정(위 문턱) | 없음 |
| PO-2 서명 나누기 + 파도(문턱을 넘을 때만) | `ISceneService.h` · `GameObjectManager.*` · `FrameSystemParallelPlan` · 새 `GameThreadKey.h` · 옮기는 서비스 둘 | `SceneFrameStepTest.ParallelServicesOfAStepRunInOneWave` · `OverlappingAccessRunsInDependencyOrder` · 기존 `EveryStepUpdatesItsRegisteredServicesOnce` | PO-1 값 |
| PO-3 캐릭터 일괄 이동(감사 37 과 같이, 문턱을 넘을 때만) | `Engine/Physics/**`(물리 씬 인터페이스 · Jolt 구현 · `CharacterControllerComponent.cpp`) | `PhysicsCharacterTest.BatchMoveEqualsOneByOne`(같은 입력의 자세가 같다) | S-2 |

근본 원인은 "실행 자리를 선언으로 말하고 서명으로 말하지 않았다" 이다. 같은 모양은 `ISceneService` 의 렌더 기여 훅(`contributeLights` 등 — 게임 스레드 계약이 주석뿐)에 남아 있다. 패킷 채우기를 병렬로 돌릴 때 같은 열쇠 인자를 쓴다.

### 리플렉션 남은 몫 — 생성 바이트 · 문화권 상속 · 방문자 · 타입 색인 · 복제 스키마(설계 2026-11-17, NEXT 5-4)

**W-R3 (a) 고정 배치형 코덱 본문 — 이미 들어갔다.** `4b701a6227` 이 타입마다 내던 쓰기 · 읽기 · 비교 본문을 서술자 표(`Engine/Serialization/Fixed/FixedFieldDesc.h`)와 해석기 하나(`FixedLayout`)로 바꿨다(언리얼 `FRepLayout` 자리). 남은 것은 측정뿐이다: 통합 빌드에서 `.gen.cpp` 합계(전 7.86 MB, 목표 4 MB 미만)와 `build_time_report.py` 생성 비중(목표 8 % 미만). 목표에 못 미치면 다음 후보를 숫자로 고른다 — `CodeEmitBuffer` 가 방출 함수별 바이트를 세어 `ReflectionParser.EmitBytes` 로 남기고(`emitMethodInvoker` · `emitPropertyStatics` · `emitNestedContainerTree` · `emitPropertyMetadata` …), 합계의 25 % 를 넘는 방출 함수만 표로 바꾼다(메타데이터 채우기 함수 → 상수 (키, 값) 줄). 형식이 있어야 하는 호출 본문(`emitMethodInvoker`)은 표로 못 바꾼다.

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

| 일 | 바꿀 파일 | 남길 테스트 | 의존 |
|---|---|---|---|
| RF-1 방출 바이트 보고 | `Programs/ReflectionParser/CodeEmit.*` · `ReflectionPipeline.cpp` | 파서 fixture 결과 그대로, 로그 `ReflectionParser.EmitBytes` | 없음 |
| RF-2 문화권 상속 | `Engine/Localization/CultureInfo.*` · `LocalizationDocuments.cpp` · `Content/**/*.cultures.xml` · 편집기 `LocalizationTools.cpp` | `CultureInfoTest.ChildOverridesOnlyWhatItWrites` · `RootWithoutPluralRulesIsAnError` · `ParentCycleIsAnError` · `FormatsMatchTheHandParserForEveryShippedCulture`(먼저 기록) | 없음 |
| RF-3 `ReflectVisit` 1 걸음 | 새 `Engine/Reflection/ReflectVisit.h` · `Component.*` · `StateHashBuilder.h` · `ReflectionValidation.cpp` · `foldStateHash` 재정의 일곱 | `StateHashTest.EveryStateFieldChangesTheHash`(먼저 진다) · `ReflectVisitTest.VisitsBaseFirstAndNestedValues` · `StopEndsTheWalk` | 없음 |
| RF-4 `ReflectVisit` 2 걸음(소비자마다 커밋) | `Engine/Serialization/**` 루프 아홉 · `ReflectionDocWriter.cpp` · 인스펙터 `InspectorPropertyLayout.cpp` | Content 왕복 바이트 같음(기존 직렬화 테스트) · `--check-text` | RF-3 |
| RF-5 복제 스키마 생성 | `Programs/ReflectionParser/FixedCodecGenerator.*` · `Engine/Serialization/Fixed/*` · 새 `ReplicatedLayout.*` | 파서 사례 `ReplicatedOnUnencodableTypeIsAnError` · `ReplicatedLayoutTest.OnlyReplicatedFieldsAreWritten` · `FieldOrderChangesTheSchemaHash` · `NonReplicatedFieldKeepsTheHash` · `QuantizedFloatRoundTripsWithinHalfAStep` · bench `EncodeCost` | 없음 |
| RF-6 와이어 버전 · 첫 소비자 | `Engine/Net/NetMessageChannel.*` · `Kits/Feature/Network/NetClientServer/*` · `Games/Shooter3D/ShooterNetPawnComponent.*` | `NetMessageChannelTest.ReplicatedSchemaMismatchIsRefusedAtHandshake` · `net-pair` Shooter3D 시나리오 | RF-5 |
| RF-7 타입 색인(문턱을 넘을 때만) | `Engine/Reflection/TypeInfo.h` · `TypeRegistry.*` · 위 세 표 | `ReflectionTypeRegistryTest.TypeIndexIsReusedWithANewGeneration` | 통합 측정 |

근본 원인은 "리플렉션 칸을 도는 일을 소비자마다 다시 썼다" 이다. 같은 모양 후보는 `ObjectStateSerializer` 의 상태 쓰기 · 읽기 짝과 `ComponentDefaults` 의 기본값 비교다 — RF-4 가 같이 옮긴다.
