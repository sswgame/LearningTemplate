# 결정 기록

> 질문: 이 제안은 이미 정했거나 기각했는가?
> 독자: 같은 제안을 다시 하려는 사람, 정한 이유를 찾는 사람.
> 결론: 1 ~ 4절이 이 문서에 있고, 5절의 영역별 결정은 `docs/decisions/` 의 영역 파일 열둘이다. 새 결정은 상용 엔진과 비교한 근거 한 줄과 함께 맞는 영역 파일의 끝에 더한다.

다시 볼 조건이 붙은 결정은 그 조건이 오면 다시 엽니다. 아직 남은 일은 [작업 방식](11_Workflow.md) 6절의 네 곳에 있습니다.

## 1. 작업 방향

- **쪼개기보다 공통부 추출.** 중복의 원인은 "매번 다시 만들어야 하는 구조" 이니 그것을 없앤다.
- **추가 · 변경이 쉬운 구조를 먼저 만든다.** 기준은 "하나 더하려면 몇 곳을 고쳐야 하는가" 다. 목록은 하나(표 · X-macro · 폴더 열거)로 둔다. 같은 enum 을 여러 파일이 `switch` 하는 자리는 "백엔드 · 스테이지마다 정말 다른 일을 하는가" 를 먼저 묻는다.
- **한 곳에 넣은 고침은 형제에게도 넣는다.** 가장 잦은 결함 모양이다. 같은 일을 하는 형제를 grep 으로 찾고 원인이 같은 결함이 여럿이면 구조(한 창구 · 단언 · 게이트)로 막는다.
- **실패는 조용하면 안 된다.** 실패할 수 있는 bool 은 `[[nodiscard]]` 이고 일부러 버릴 때는 `(void)호출(); // 이유` — 실패 가능 동사(load · read · apply …)의 결과만 이유가 필수다(`CheckDiscardReason`).
- **상용 엔진의 같은 자리와 견준다.** 따르되 일부러 다르게 한 것은 이유를 적는다(예: `hashed_string` 은 FName 규칙이지만 숫자 꼬리는 없다).
- **성능 주장은 Release 숫자로만 한다.** 이전 · 이후를 같은 조건에서 번갈아 재고 평균이 아니라 p50 · p99 · 최악 프레임을 본다([검증과 측정](08_Verification.md) 2절).
- **주석 · 커밋 메시지는 한국어.** 문서 주석은 "~합니다" 체, 함수 본문 `//` 은 "~다" 체. 직역어 대신 표준 용어(thundering herd · 락 컨보이 · 브로드캐스트 · 조인 · continuation · 오버플로 · 분기)를 쓰고 식별자 · 로그 · assert 문자열 · `NOLINT` 줄은 그대로 둔다. 주석을 고치기 전에 구현을 읽는다. 규칙은 [AGENTS.md](../AGENTS.md)와 `docs/agents/`, [04_CodingGuidelines.md](04_CodingGuidelines.md).
- **자산 처리 용어는 넷이다(상용 엔진 기준).** Cook = 배포 · 실행용 플랫폼 데이터로 바꾸기(`--cook-*`), Import = 원본 → 엔진 형식(`--import-textures`), Generate = 빌드가 만드는 코드 · 헤더, Bake = 미리 계산한 결과(지금은 없다). "굽다" 는 쓰지 않는다. 이름을 바꿀 때 별칭을 두지 않고 데이터를 다시 쓴다.
- **새 기능 로드맵보다 백로그의 결함 · 정리 · 성능 · 테스트 · CI 항목을 먼저 닫습니다**(2026-10-06, 사용자 결정). 온라인 서비스 키트는 함께 진행합니다. 보류로 정한 것(`hashed_string` 숫자 꼬리, CC0 리소스 배치와 MechArena)은 그대로 둡니다.

## 2. 안 하기로 한 것 (다시 제안하지 말 것)

- **PIX 캡처(DX12 전용)는 하지 않았다**(2026-10-10) — RenderDoc in-app API(`-renderdoc`)가 네 백엔드를 덮는다.
- **지금 하지 않는 구조 후보 — 다시 볼 조건과 함께**(2026-10-03 상용 엔진 비교):
  - 트랜스폼 SoA 2 단계(UE 액터도 AoS, 측정 근거가 생기면).
  - 선행 조건 스케줄러(시스템이 서로의 결과에 기대기 시작하면 — UE `AddTickPrerequisite`). 지금은 오브젝트에 붙인 순서와 `_tickAfter` 가 순서를 주고 키트 코드끼리의 의존은 0 곳이다.
  - 에셋 로더 등록제는 재개했다(2026-10-20, 로더 20 종 이상 — UE `UFactory`, [구조 단순화](decisions/5-4_DataConfig.md) AT).
  - 참조 카운트 RHI 핸들(한 리소스를 여럿이 나눠 들기 시작하면 — UE `TRefCountPtr`). Mesh/Material `SlotHandle`(`shared_ptr` 이 수명과 RT 안전을 한 번에 준다). `ResourceUtil` 소유 객체화(UE `FPaths` 도 정적).
  - 링크 단위 분할(증분 링크 시간이 문제가 되면 별도 PR). GameFramework 기반을 `GFS_*` DLL 여럿으로 쪼개는 안도 하지 않는다(2026-10-05, 층은 `CheckGameFrameworkLayers`).
  - `UnitStatsComponent` 를 기반(`Combat/`)으로(2026-10-05 — 쓰는 게임 0, 옮기면 데미지 숫자 · `MonsterDef` · `DamageAppliedEvent` 셋을 끊어야 층이 맞는다. 체력 읽기는 `HealthSourceComponent`). 다시 볼 조건: 다른 키트 · 게임이 피해 입구(`takeDamage` · 무적 · `DamageAppliedEvent`)를 이 키트 없이 쓰려 할 때 — 그때 `DamageAppliedEvent` 를 기반으로, `setStats( MonsterDef )` 를 키트 도우미로, 데미지 숫자를 `HealthListenerComponent` 파생으로 내리고 `generated/GF_ActionCombat/UnitStatsComponent.gen.*` 을 지운 뒤 re-configure 한다. 피해 입구 인터페이스도 같은 조건에서 `Combat/` 에 `takeDamage( 양, 쏜 쪽 )` 하나(언리얼 `AActor::TakeDamage`).
  - 키트 아이템/효과 처리기 등록부(두 키트가 나누는 종류 enum 0 이고 `-Werror=switch` 를 잃는다 — 데이터 효과는 GAS `registerExecutionClass`). `TimedModifierSet`(`Countdown` 슬롯과 `extendTo` · `start` 로 충분 — GAS 밖 키트 둘 이상에 목록형이 생기면 다시). API 통합 남은 판단(다음 훑기).
- **도구 버전을 "최신 자동" 으로 두는 것.** 빌드 재현성이 떨어진다(clang-format 은 버전이 곧 출력).
- **`EditorContext` 의 소유 구조를 더 쪼개는 것.** 조회 · 생명주기를 두 TU 로 갈라 조회만 하는 코드가 ImGui 없이 링크되게 한 것으로 충분하다(`Test/EditorTest`).
- **`EngineServices.h` 와 `RHIBackendType.h` 를 위 티어로 옮기는 것**(2026-10-10). `EngineServices.h` 는 Engine include 0 의 찾기 창구로 180 파일이 써서 옮기면 prelude 예외만 는다(언리얼 `GEngine` 도 Engine 코어). `RHIBackendType` 은 `EngineConfig` 가 드는 값이라 `Graphics/RHI` 로 옮기면 Config → Graphics → Config 순환이다.
- **clang-tidy `bugprone-throwing-static-initialization` 남은 건에 `noexcept` 를 붙이는 것.** 전역 등록자 · 설정 싱글턴은 `string` · `variant` 를 들어 실제로 던질 수 있다 — 분석기를 침묵시키는 대신 정보를 지운다.
- **네트워크에서 하지 않는 것**: NAT 통과, 리플렉션 속성 복제(언리얼 Iris), 복제용 RPC, 외부 네트워크 라이브러리(암호 라이브러리만 예외 — 5-9). N18c 는 측정으로 기각했다(3절).
- **Engine 에 남겨 두는 폴더**(2026-10-10): 에디터는 `GameFramework` 를 include 할 수 없으므로(`CheckEngineLayers`) 에디터 · 서버가 쓰는 폴더는 Engine 에 둔다. 목록은 `CheckEngineLayers` 가 정본.
- **구조 리팩토링 후보를 옮길지 가르는 기준**(2026-10-12). 줄 수가 아니라 앞으로 고칠 곳이 줄어서 옮긴다. ① **반쪽 이전 금지** — 한 클래스 안에 옛 방식과 새 방식이 섞이면 더 헷갈린다. ② **훅 · 인터페이스는 쓰는 곳이 둘 이상일 때**(SOLID 와 부딪히면 SOLID 가 이김). ③ **동작이 같으면 코드를 늘리지 않는다**(한도 없는 대기에 시한 · 경고를 붙이는 식의 상수와 분기 증가). ④ **의미가 조금씩 다른 복붙은 합치지 않는다**(조건이 콜백 · 인자로 새는 공용 함수는 복붙 넷보다 읽기 어렵다). ⑤ **옮기면 공용 구조가 주는 규칙(중복 거절 · 리로드 대체 · 순서)을 지키는 단언을 그 자리의 테스트에 둔다.**
- **SOLID 는 성능을 해치지 않는 선에서 반드시 지킨다**(2026-10-12, 사용자 결정). 성능은 측정으로 해치지 않음을 보이거나 핫 경로가 아님을 코드로 보인다. S: 클래스 하나가 한 가지 이유로만 바뀐다. O: 같은 모양의 분기나 직접 호출이 늘어나는 자리는 레지스트리 · 관찰자 뒤로. L: 구현이 계약을 좁히지 않고 훅의 기본 동작은 중립. I: 한 훅에 무관한 일을 얹지 않는다. D: 위 티어가 구체 클래스를 직접 알지 않는다. 핫 경로에는 가상 호출을 넣지 않고 뽑은 클래스는 값 멤버로 둔다. 위 기준에 걸려 옮기지 않은 것(S1 ~ S22): 서버 `pause`/`resume`/`step` 개발 명령화(S3 — 에디터가 같은 이름을 다른 뜻으로 등록), 패스 종류 표의 실행 함수 칸(S5 — 쿠커가 `FrameRenderer` 에 의존), `ProfilerPanel` 탭 분리(S8 — 상태를 나눠 써 반쪽 이전), 인자 없는 `Scene( name )` · `GameObjectManager()` 생성자(S10), 에디터 셸 프레임 열기 · 닫기(S12 — ImGui 순서와 `DrawSnapshotHandoff` 가 정한 프레임 계약), `ModuleShadowCopy` 인스턴스화(S20 — 섀도 폴더 번호가 프로세스 전역), `Engine::update` 의 백엔드 교체 · 관찰자 루프(S13 — 수명 · 순서가 뜻). S16 의 **동작 변화**: 엔진 서비스가 묶이지 않은 실행은 GPU 계측이 꺼진다(전에는 단언).
- **폴더 · 파일 구조는 기계적으로 합치지 않는다**(2026-10-12 감사, R8). 큰 클래스를 나눈 가족 대부분이 파일마다 120 줄을 넘고 작은 조각은 의존을 분리한 것이라, 합치면 층 규칙과 그래픽스 파일 분할 기준을 해친다. **파일이 40 개를 넘는 평면 폴더는 기능 묶음 하위 폴더로 나누고 한 클래스를 나눈 파일 가족은 가르지 않는다**(2026-10-15, 언리얼 `Private/` · 유니티 `Runtime/` · Godot `scene/` 와 같은 단위): `Editor/Common/Commands`(`Asset` · `Scene` · `Play` · `Dev`), `Engine/Object/GameObject`(`Registry` · `Tick` · `Service` · `Serialization`), `Engine/Renderer/Frame`(`View` · `Packet` · `Resource` · `Capture` · `Timeline`). 오브젝트 모델 코어 규칙은 `GameObject` 트리 전체(`CheckEngineLayers.toml` 의 `coreTree`)로 읽는다.
- **스크립팅 언어 층(Lua · C# · 비주얼 스크립팅)은 두지 않는다**(2026-10-17, 사용자 결정). 게임 로직은 C++ 모듈과 핫리로드로 짠다. 비프로그래머용 층이 필요해지는 게임이 생기면 다시 연다.

## 3. 기각한 것 — 측정값과 함께 (다시 제안하지 말 것)

측정이 근거인 결정은 끝에 `[측정: 기계 · 규모 · 날짜 · 근거 커밋]` 한 줄을 둔다(알 수 없는 값은 `미상`). 조건이 바뀌면(빌드 구성 · 프레임 길이 · 기계) 이 기각은 "무조건 금지"가 아니라 "그 조건에서 이득 없음"이므로 다른 문서가 "하지 말 것"을 말할 때는 이 절의 항목을 링크해 조건째 읽게 한다. 재측정이 필요한 항목은 [후보 문서](plans/Candidates-project.md)의 `DecisionMeasurementColumn` 에 남긴다.

- **`runParallelByCost` 를 모든 병렬 호출처로 넓히기**(2026-10-16): 트랜스폼 계층 · GPU 씬의 고정 임계값은 손익분기에 맞춘 값이라 바꿔 재니 틱 8000 이 86~99 → 105~107 us 로 느려졌다. 항목당 비용이 크고 개수가 달라지는 곳(씬 틱 · 애니메이션 평가)에만 쓴다. [측정: 기계 미상 · Release · 2026-10-16 · e3542d9db]
- **`common` 지연 import(PEP 562)**: `import common` 은 81 → 1.8 ms 지만 생성기 한 번은 110.5 → 110.1 ms — 결국 하위 모듈을 다 올리고 공개 이름이 import 순서에 따라 모듈로 바뀌는 함정이 생긴다. [측정: 12 회 중앙값 · 2026-10-06 · 0df62d4cd]
- **N18c 메시지 버퍼 풀**: 틱당 할당 1133 중 서버 복제 544, "전체 − 서버 복제" 589 < 문턱 1 000. [측정: Release `NetReplicationBenchTest` 16 × 1000 · 2026-10-06 · 38a18ee1c]
- **TaskManager 후보들**(스핀 워커 제한 + 적응 예산, 워커별 노드 자유 목록, 워커 지정 틱 쓰기, 스핀 늘리기 — SMT 형제를 빼앗아 GT 889 → 1305 us, hot pool, 레인 없는 GT 병렬화, `TaskArgs` 인라인 4 칸): 모두 손해 또는 잡음. [측정: 큐브 8000 · 16 스레드 · 2026-09-20 · 4846237f0 — 0.7 ms 프레임 조건이라 프레임 8 ms 이상에서 다시 재기 전까지는 "그 조건에서 이득 없음". "레인 없이" 기각은 High · Normal · Low 큐가 생겨 전제가 없어졌으니 레인 있는 설계로 다시 판단]
- **사용자 결정**: mimalloc · SIMD 수학 · 프리페치는 쓰지 않는다. 사용처가 0 이어도 상용 엔진에 대응이 있는 공개 API 는 남긴다(남긴 기능에는 테스트를 붙인다). SIMD 수학을 미룬 까닭은 성능 근거가 아니라 플랫폼 파편화 우려다(2026-10-17). 다시 볼 조건: 수학이 병목인 핫 루프가 프로파일에 나오거나 지원 플랫폼 목록이 확정될 때(제안: 한 추상 계층 뒤에 SSE2 · NEON · 스칼라 백엔드를 컴파일 때 고르고 스칼라를 항상 빌드해 테스트하며 배치 커널에만 적용).
- **GPUScene · 렌더**: `DrawCandidate` SoA(구조체를 78 % 키워도 불변), 영속 렌더 씬을 종류 축으로, 인스턴스 채우기 병렬화, 배치 정렬, DX12 루트 상수 드로우 ID 주입(ExecuteIndirect 2 배 느림), 클리어 `DontCare`(타일 GPU 로 가면 다시), PSO 병렬 생성(9 %, 드라이버가 직렬), DX12 온라인 블록 잠금의 워커별 목록(RT 프레임의 2~4 %), `GPUInstance` 128 → 96 B(셰이더 · 계약 테스트를 모두 건드림), 추가 뷰 사이 그림자 공유(뷰당 ~50 us) — 이득 없음 또는 잡음. [측정: Release 큐브 8000 · 2026-10-06 이전 · 7b1d868d4 · a55a90f0b]
- **히치**: 펜스 시그널을 Present 앞으로 · 백버퍼 수 · 인라인/즉시 제출은 분포가 그대로였고 어댑터 강제 선택은 악화. 꼬리(p99 2.4~4.2 ms · 최악 12~37 ms)는 DX11 에도 같고 Present 호출 자체는 p99 0.2 ms — GPU 백프레셔라 대기 가능 스왑체인으로 고칠 히치가 없다. [측정: Release 큐브 100 · VSync 끔 · 2026-10-06 · 커밋 미상]
- **틱 · 오브젝트**: 인라인 `TickItem`(192 → 232 B), 적용 단계를 틱에 합치기(+124 B), 쓰기 정렬의 `id % 버킷`(150 → 450 us), 오브젝트 id 표 2 단 디렉터리(1 억 스폰이면 800 MB) — 이득 없거나 메모리만 는다. [측정: 규모 · 날짜 미상 · a55a90f0b]
- **패딩 재배치 나머지**(`RunPaddingReport --preset Ninja-Release --min-saving 8`): GameObject(168 → 160)만 했다. 나머지는 인스턴스가 수백 개 이하이거나 나눠 쓰거나 PROPERTY 순서가 고정이다. 수천 개가 만들어지는 타입이 나오면 그것만 한다. [측정: 69 개 · 2026-10-06 · 529e83933]
- **전역 소형 블록 할당자**: 프레임당 sw 할당 7.7 회(1 us 아래), 기동 할당 8 ms(1 % 아래). 프레임당 할당이 다시 늘거나 큰 맵 로드에서 5 % 를 넘으면 다시 잰다. [측정: Release · 2026-10-06 · 529e83933]
- **파일 시스템을 플랫폼 API 로 다시 짜기**: 존재 확인 · 순회 시간은 같고 할당만 준다(기동 때뿐). 한 경로가 프로파일에서 1 ms 이상이면 그 함수만 `File/Windows` · `File/Linux` 로 옮긴다. [측정: Release 스크래치 2920 파일 · 2026-10-06 · a6cd49bb9]
- **물리**: 파괴 잎 볼록 껍질을 Jolt 잡 시스템 워커로 나눠 짓기 — `FractureBenchTest` p50 20.7 → 24.4 ms 로 느려졌다(Jolt 할당이 엔진 `Memory::allocate` 로 가서 경합). 할당을 풀어 주지 않는 한 다시 하지 말 것. Jolt 백엔드 TU 에 `Engine/Common/EngineParallel.h` 를 넣으면 물리 전체가 깨진다 — 그 TU 에서는 `JoltJobSystem` 을 쓴다. [측정: `FractureBenchTest` 잎 13 개 · 2026-10-06 · 3ba538f4d]
- **도구 · 빌드**: 린트를 파일마다 프로세스로(5.3 → 9.4 s), 린트 파일 공유 캐시(C++ 2500 개를 다 읽어도 0.15–0.7 s), 린트 CTest 를 한 프로세스로(항목별 보고 · TIMEOUT 을 잃음), 팩 코덱 Zstd(−0.1 %) · LZ4(+28 %) — 이미 압축된 자산이라 Zlib 유지(압축 안 된 자산이 들어오면 다시), `flatMapInProcesses` 16 → 8 워커(혼자 돌면 3.3 → 3.7 s — `ctest -j` 겹침에서 재 볼 것). [측정: 날짜 · 커밋 미상 a55a90f0b]
- **구조**: 백엔드 `*RHIResource.h` · `*RHICommandContext.h` 공통 기반(겹침이 전부 override 선언), `ResourceCache<T>`, 모듈 팩토리 골격 공통화, RHI · GF leaf CMakeLists 를 부모 루프로, "자유 `static` 함수 금지" 린트(오탐), 근접 질의를 엔진 `SpatialHashGrid2D` 로(RTS 는 순서 계약을 들어 격자로 바꾸면 동점 · 밀어내기 합이 바뀌고 Mech 는 전수 검사가 더 싸다), 키트 칸 저장소 템플릿 `Grid2D<T>`(저장소 모양이 키트마다 다르다).
- **게임 · 키트**: 복셀 청크 메시 제자리 갱신(바뀐 청크만 다시 짓기 — 5,000 프레임에 2 회), 카트 트랙을 공용 `SplinePath` 로(랩 · 고스트 거리가 수평 길이라 3D 호 길이로 바꾸면 값이 바뀌고 쓰는 게임 0). 늘 상위에 오는 정당한 중복(`RunDuplicateCode`: 백엔드 인터페이스 선언 · 플랫폼 구현 · enum 레이블 나열 · 서비스 로케이터 둘 · 셰이더 반사 D3D11/12 등)은 합치지 않는다.
- **콜드 · 워크트리용 프리셋 `Ninja-Debug-Cold`(PCH 끔 + 유니티)**: 캐시 없이 115.2 s(개발) 대 147.3 s, PCH + 유니티는 145.2 s 로 개발보다 느리다. [측정: PC `SW` · 풀 빌드 · 2026-10-11 · 1fed3f45f]
- **`Engine.dll` 을 층 DLL 로 쪼개 증분 빌드를 줄이는 안**(BuildSpeed 9, Debug): `.cpp` 하나 증분 13.9 s 중 `Engine.dll` 링크는 1.9 s, 컴파일 1.6 s 였고 나머지 약 10 s 는 임포트 라이브러리 수정 시각 때문에 킷 · 게임 DLL 84 개가 다시 링크된 것이었다(`sw_keepImportLibraryIfUnchanged` 로 해결, 5 s). 헤더를 고친 비용은 PCH 에 든 헤더(`GameObject.h` 가 TU 약 2,200 개)가 정하므로 분할로 줄지 않는다. 링크 단위 분할은 다른 이유가 생길 때만 한다. 2026-10-25 재측정에서 링크가 2.3 ~ 5.8 s(증분의 52 ~ 61 %)로 커졌지만 분할 전에 링크 옵션(`-gcodeview-ghash` + `/DEBUG:GHASH`)을 먼저 잰다(ArchitectureSeams 5a). [측정: PC `SW` · Debug · 2026-10-16 04024dda7 · 2026-10-25 eb2a1fa59]
- **킷 밖 묶음의 PCH 공유 · ReflectionParser 강제 include PCH**: 합쳐도 16 스레드에서 2 ~ 3 s(편차 안) — 킷(PCH 60 개 → 1, −20.6 s)만 했다. [측정: 2026-10-11 · 65ee88bb4]
- **롤백에 파괴 상태 싣기**(사용자 결정 2026-10-06): 롤백 키트(격투)에 파괴물이 없고 조각 물리(Jolt)는 되감지 못한다(Chaos GC 도 롤백 없음). 롤백 게임이 파괴물을 쓰면 `saveState` 에 `makeNetworkSnapshot` 을 싣고 `loadState` 가 즉시 적용하는 창구를 더한다.
- **훅의 `CheckHeaderSelfContained` 를 하위 프로세스로 띄우기**: 트리 전체 게이트가 이미 먼저 떠 있어 겹치고 홀로 남는 커밋에서는 기동 0.2 s 만 낸다. [측정: 2026-10-10 · 4304eb03f]
- **`CheckConfigReference` 를 파일 단위로 나누기**: 커밋 훅의 바닥은 트리 전체 게이트 여섯이 각자 `Source/` 를 읽는 값(1 ~ 1.4 s)이라 하나만 0 으로 만들어도 같은 높이로 남는다. 소스 게이트는 `SourceScan`(한 번 걷고 코어 수만큼 프로세스에 나눠 읽음)으로 옮기고 무거운 `CheckNameReferences` · `CheckKitNamespaces` 는 CI 로 넘겼다. [측정: 단독 5 회 중앙값 · 2026-10-11 · 7a5fe1016]
- **비소스 린트 게이트(Python · CMake · 문서 · 텍스트)의 공유 훑기**: 읽기가 값이 아니다 — 열둘의 합 15.2 s 중 걷기 · 읽기는 1 s 미만이라 전부 공유해도 CPU 합 3~4 s 만 준다. 덩어리는 `CheckVerifyPending`(케이스마다 `Test/` 전체 글에 정규식을 돌려 단독 52 s)이니 그 정규식을 먼저 본다. [측정: 단독 순차 1 회 · 2026-10-25 · 233d5ef35]

## 5. 영역별 결정

결정마다 결정한 날 · 근거 · 상용 엔진의 방식을 적고, 코드와 헤더 주석에 반영된 결정은 한두 문장으로만 적습니다(자세한 계약은 그 코드의 헤더 주석). 결정을 바꾸려면 여기 적힌 근거와 상용 엔진의 방식을 먼저 확인하고 같은 커밋에서 그 영역 파일을 고칩니다.

**새 결정은 아래 영역 파일 가운데 맞는 하나의 끝에 한 줄로 더합니다**(병렬 작업자가 서로 다른 영역 파일을 고치면 병합이 충돌하지 않습니다). 영역 번호와 제목은 이 문서가 정하고 본문은 `decisions/5-<번호>_<이름>.md` 에 있습니다. 한 영역은 `py -3 -m Scripts doc 09_Decisions --section 5-4` 로 읽습니다. 맞는 영역이 없으면 가장 가까운 파일에 더하고 새 영역 파일은 만들지 않습니다.

### 5-1. 문서와 저장소

[5-1_DocsRepo.md](decisions/5-1_DocsRepo.md)

### 5-2. 빌드, 스크립트, CI

[5-2_Build.md](decisions/5-2_Build.md)

### 5-3. Core 와 플랫폼

[5-3_CorePlatform.md](decisions/5-3_CorePlatform.md)

### 5-4. 데이터, 직렬화, 설정

[5-4_DataConfig.md](decisions/5-4_DataConfig.md)

### 5-5. 그래픽스와 RHI

[5-5_GraphicsRHI.md](decisions/5-5_GraphicsRHI.md)

### 5-6. 오브젝트, 조종, 입력

[5-6_ObjectsInput.md](decisions/5-6_ObjectsInput.md)

### 5-7. 런타임 UI 와 글자

[5-7_RuntimeUI.md](decisions/5-7_RuntimeUI.md)

### 5-8. 게임프레임워크와 키트

[5-8_GameFramework.md](decisions/5-8_GameFramework.md)

### 5-9. 온라인 서비스, 네트워크, 보안

[5-9_OnlineNet.md](decisions/5-9_OnlineNet.md)

### 5-10. 전용 서버

[5-10_Server.md](decisions/5-10_Server.md)

### 5-11. 에디터와 외부 공개

[5-11_Editor.md](decisions/5-11_Editor.md)

### 5-12. 기능 대기열 결정 (2026-10-17)

[5-12_FeatureQueue.md](decisions/5-12_FeatureQueue.md)
