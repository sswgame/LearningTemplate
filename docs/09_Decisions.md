# 결정 기록

이 저장소에서 정한 방향과, 하지 않기로 한 것과, 측정으로 기각한 것을 모은 문서입니다. 같은 제안이 다시 나왔을 때 그때의 근거와 측정값을 찾는 곳입니다.
다시 볼 조건이 붙은 결정은 그 조건이 오면 다시 엽니다. 아직 남은 일은 [백로그](06_Backlog.md) 에 있습니다.

## 1. 작업 방향

- **쪼개기보다 공통부 추출.** 긴 함수를 나누면 코드가 옮겨 갈 뿐 총량은 그대로다. 중복은 증상이고 원인은 "매번 다시 만들어야
  하는 구조" 다. 원인을 없앤다.
- **추가 · 변경이 쉬운 구조를 먼저 만든다.** 판단 기준은 "하나 더하려면 몇 곳을 고쳐야 하는가" 다. 복사해야 할 것이 남아 있으면 그
  자리가 다음 리팩터 대상이다. 목록이 여럿이면 한쪽만 늘어난다 — 목록은 하나(표 · X-macro · 폴더 열거)로 둔다.
  여러 파일이 같은 enum 을 `switch` 하는 자리를 셀 때는 "백엔드 · 스테이지마다 정말 다른 일을 하는가" 를 먼저 묻는다(그러면 정당하다).
- **한 곳에 넣은 고침은 형제에게도 넣는다.** 지금까지 가장 잦았던 결함 모양이다(한 함수만 상한을 안 봄 · 셋 중 하나만 폴백 없음).
  고칠 때 같은 일을 하는 형제를 grep 으로 찾는다. 원인이 같은 결함이 여럿이면 그 원인을 구조(한 창구 · 단언 · 게이트)로 막는다.
- **실패는 조용하면 안 된다.** 버린 값 · 못 읽은 칸 · 못 만든 폴더는 그 자리에서 무엇이 왜인지 알린다. 실패할 수 있는 bool 은
  `[[nodiscard]]` 이고, 일부러 버릴 때는 `(void)호출();` 과 이유 한 줄.
- **상용 엔진의 같은 자리와 견준다.** 새 구조를 정할 때 언리얼 · 유니티 · Godot 의 같은 자리를 먼저 보고 그 모양을 따르되, 일부러
  다르게 한 것은 이유를 적는다(예: `hashed_string` 은 FName 규칙이지만 숫자 꼬리는 없다).
- **성능 주장은 Release 숫자로만 한다.** 숫자 없는 최적화는 하지 않는다. 이전 · 이후를 같은 조건에서 번갈아 재고, 평균이 아니라
  p50 · p99 · 최악 프레임을 본다([검증과 측정](08_Verification.md) 2절 참고).
- **주석 · 커밋 메시지는 한국어.** 문서 주석(`/** */` · `///<`)은 "~합니다" 체, 함수 본문 `//` 주석은 "~다" 체로 한 블록 안에서 통일한다.
  직역어 대신 표준 용어(thundering herd · 락 컨보이 · 브로드캐스트 · 조인 · continuation · 오버플로 · 분기)를 쓴다. 식별자 · 로그 ·
  assert 문자열 · `NOLINT` 줄은 그대로 둔다. 주석을 고치기 전에 구현을 읽는다 — 복사해 붙인 설명이 실제 동작과 다른 곳이 많았다.
  규칙은 [AGENTS.md](../AGENTS.md) 와 [04_CodingGuidelines.md](04_CodingGuidelines.md).
- **자산 처리 용어는 넷이다(상용 엔진 기준).** Cook = 배포 · 실행용 플랫폼 데이터로 바꾸기(셰이더 · 씬 · 프리팹 · 팩, `--cook-*`), Import = 원본 → 엔진 형식(텍스처, `--import-textures`), Generate = 빌드가 만드는 코드 · 헤더, Bake = 미리 계산한 결과(라이트맵 · 내비 — 지금은 없다). 한국어 "굽다" 는 쓰지 않는다. 이름을 바꿀 때 별칭(`Alias` · 옛 CLI 철자 · 옛 형식 리더)을 두지 않고 데이터를 다시 쓴다.
- **새 기능 로드맵보다 백로그의 결함 · 정리 · 성능 · 테스트 · CI 항목을 먼저 닫습니다**(2026-10-06, 사용자 결정). 온라인 서비스 키트는 함께 진행합니다. 보류로 정한 것(`hashed_string` 숫자 꼬리, CC0 리소스 배치와 MechArena)은 그대로 둡니다.

## 2. 안 하기로 한 것 (다시 제안하지 말 것)

- **지금 하지 않는 구조 후보 — 다시 볼 조건과 함께**(2026-10-03 상용 엔진 비교로 결정): 트랜스폼 SoA 2 단계(UE 액터도 AoS, 측정 근거가 생기면) ·
  선행 조건 스케줄러(시스템이 서로의 결과에 기대기 시작하면 — UE `AddTickPrerequisite` 모양. 2026-10-06 키트 조립 점검: 키트 디렉터 사이 순서는 한 오브젝트에
  붙인 순서가 주고 — `TickRegistry` 가 한 오브젝트의 항목을 한 워커가 붙은 순서로 돌린다 — 다른 오브젝트 사이는 디렉터의 `_tickAfter`(규칙 서브틱 + 선행 조건),
  키트 코드끼리의 의존은 0 곳이라 스케줄러의 근거가 아직 없다) · 에셋 로더 등록제(종류가 대여섯이 되면 — UE `UFactory`) ·
  참조 카운트 RHI 핸들(한 리소스를 여럿이 나눠 들기 시작하면 — UE `TRefCountPtr`) · Mesh/Material `SlotHandle`(하지 않는다 — `shared_ptr` 이 수명과 RT 안전을 한 번에
  준다) · `ResourceUtil` 소유 객체화(하지 않는다 — UE `FPaths` 도 정적) · 링크 단위 분할(증분 링크 시간이 문제가 되면 별도 PR) — GameFramework 기반(약 58k 줄)을 `GFS_*` DLL 여럿으로 쪼개는 안도 같은 이유로 하지 않는다(2026-10-05 재확인). 기반 폴더의
  층은 폴더 층 게이트(`CheckGameFrameworkLayers`)로 지킨다. · `UnitStatsComponent` 를 기반(`Combat/`)으로(2026-10-05 — 쓰는 게임 · 씬 · 프리팹 0, 옮기면 데미지 숫자(UI 층 4) · `MonsterDef` · `DamageAppliedEvent` 셋을 끊어야 층이 맞는다. 체력 읽기는 `Combat/HealthSourceComponent` 가 맡는다. 다시 볼 조건: 다른 키트 · 게임이 피해 입구(`takeDamage` · 무적 · `DamageAppliedEvent`)를 이 키트 없이 쓰려 할 때 — 그때는 `DamageAppliedEvent` 도 기반으로, `setStats( MonsterDef )` 는 키트의 도우미로, 데미지 숫자는 `HealthListenerComponent` 파생으로 내리고, `CheckGameFrameworkLayers` 자가 시험의 키트 헤더 예를 바꾸고, `generated/GF_ActionCombat/UnitStatsComponent.gen.*` 을 지운 뒤 re-configure) · 피해 입구 인터페이스(19 곳 — 인자 모양이 넷이다. 키트 투사체 · 공격 판정을 다른 체력 모델(어빌리티 시스템 · 게임 컴포넌트)에 쓰는 게임이 생기면 `Combat/` 에 `takeDamage( 양, 쏜 쪽 )` 하나를 `HealthSourceComponent` 옆에 — 언리얼 `AActor::TakeDamage`)
  · 키트 아이템/효과 처리기 등록부(2026-10-05 전수: enum switch 4 곳 · 19 case, 두 키트가 나누는 종류 enum 0 — 키트마다 하는 일이 다르고 `-Werror=switch` 를
  잃는다. 데이터로 효과를 더할 게임은 기반 GAS 의 `registerExecutionClass` — 언리얼 GameplayEffect 자리 — 를 쓴다. 두 키트가 같은 종류 집합을 나누게 되면 다시 본다) ·
  `TimedModifierSet`(시간 제한 수정자는 이름 붙은 `Countdown` 슬롯이고 쌓기는 `extendTo` · `start` 로 이미 정해져 있다, 목록형은 위쳐 물약 한 곳 — 일반형은 GAS 의
  지속 이펙트 · 스택 정책. GAS 밖 키트 둘 이상에 목록형이 생기면 다시 본다) · API 통합 남은 판단(다음 훑기).

- **도구 버전을 "최신 자동" 으로 두는 것.** 네트워크 의존이 생기고 빌드 재현성이 떨어진다. 버전 키 하나로 고정하고 올릴 때만 의도적으로
  올린다. clang-format 은 버전이 곧 출력이라 고정이 아니면 안 된다.
- **`EditorContext` 의 소유 구조를 더 쪼개는 것.** 조회 · 생명주기를 두 TU 로 갈라 조회만 하는 코드가 ImGui 없이 링크되게 해 둔 것으로
  충분하다(그래서 `Test/EditorTest` 가 성립한다). 매니저 소유를 밖으로 빼는 것은 영향이 크고 얻는 것이 없다.
- **clang-tidy 의 `bugprone-throwing-static-initialization` 남은 건에 `noexcept` 를 붙이는 것.** 전역 변수 등록자 · 설정 싱글턴은
  `string` · `variant` 를 들어 실제로 던질 수 있다. 분석기를 침묵시키는 대신 정보를 지우는 거래다.

## 3. 기각한 것 — 측정값과 함께 (다시 제안하지 말 것)

- **`common` 지연 import(PEP 562 `__getattr__`)**(2026-10-06, 12 회 중앙값): `import common` 누적 81.0 → 1.8 ms 이지만 `GenerateCookContract.py` 한 번이 110.5 → 110.1 ms — 생성기는 결국 같은 하위 모듈(GeneratedFile · CookContract · 그 앞의 탐색 순서)을 다 올린다.
  기준(생성기 30 % 이상)에 한참 못 미치고, 하위 모듈 이름과 같은 공개 이름(`BuildTree`)이 하위 모듈 import 순서에 따라 모듈로 바뀌는 함정이 생긴다.
- **N18c 메시지 버퍼 풀**(2026-10-06, Release `NetReplicationBenchTest` 16 × 1000, 3 회): N18b 뒤 틱당 할당 1133 중 서버 복제 544 — "전체 − 서버 복제" 589 < 문턱 1 000.
  (N18b 전 25182 · 서버 복제 16886, ReplicationServer tick p50 5.4 → 3.5 ms.)
- **TaskManager 후보 셋**(Release, 큐브 8000, 16 스레드, 5~8 회 번갈아): 스핀 워커 2 개 제한 + 적응 예산(GT 839→846, RT.ExecutePacket 395→436 us), 워커별 노드
  자유 목록(GT 839→917, RT.Graph 187→237 us), 워커 지정 틱 쓰기 적용(queued 78→80 us, p99 163→212 us). 셋 다 손해 또는 잡음.

- **사용자 결정**: mimalloc · SIMD 수학 · 프리페치는 쓰지 않는다. 사용처가 0 이어도 상용 엔진에 대응이 있는 공개 API 는 남긴다(남긴 기능에는 시험을 붙인다).
- **GpuScene · 렌더**: `DrawCandidate` SoA(구조체를 78 % 키워도 불변, 거의 선형 — 예전의 "28~62 배" 는 GT 가 GPU 를 기다린 착시), 발행 배열 풀(75 vs 76~85 us), 영속 렌더 씬을 **종류** 축으로
  (8 → 1024 에 평평), 인스턴스 채우기 병렬화(모든 크기에서 인라인에 짐), raw 페이로드 워커 쓰기 · `MeshInstanceBatch::updateParallel`, 배치 정렬(잡음), DX12 루트 상수 드로우 ID 주입
  (ExecuteIndirect 2 배 느림 — 인스턴스 슬롯 스트림이 대안), 클리어 `DontCare`(0 us — 타일 GPU 로 가면 다시), 점 샘플러(122 → 121), 머티리얼 CB 워커화(6 us), PSO 병렬 생성(9 %, 드라이버가 직렬),
  `executeCommandLists` 일괄(리스트 수 그대로), 꼬리 재방출 그룹 캐시(98 → 98).
  DX12 온라인 블록 잠금의 워커별 목록(잠금이 드는 씬 배치 기록 전체가 p50 36~65 us = RT 프레임의 2~4 %, 큐브 8000 · 도형 5 · 디퍼드).
  `GpuInstance` 128 → 96 B(모든 인스턴스가 매 프레임 올라가는 최악에서도 증분 상한이 RT ~57 · GT ~65 us, 월드 float3x4 · 블렌드 비트 묶기는 모든 셰이더 · 계약 시험을 건드린다).
  추가 뷰 사이 그림자 공유(2026-10-06, Release 큐브 8000 + 바닥, `-gv_benchViews=4` — 512² 캡처 카메라 넷, 번갈아 4 쌍): 추가 뷰의 그림자 패스를 빼도 `GPU.Frame` 이
  쌍마다 256 · 390 · −47 · 169 us 줄 뿐(뷰당 ~50 us, 추가 뷰 넷이 GPU 프레임을 0.65 → 1.8~2.1 ms 로 늘리는 몫의 ~15 %). 프레임 순서를 바꾸는 구조 변경에 비해 작다.
- **히치**: 펜스 시그널을 Present 앞으로 · 백버퍼 수 · 인라인/즉시 제출 — 분포가 그대로였다. 어댑터 강제 선택은 A/B 로 악화.
  2026-10-06 재측정(Release, 큐브 100 · VSync 끔, 제안서 측정 3 회씩): 꼬리(p99 2.4~4.2 ms · 최악 12~37 ms)는 DX11 에도 같은 크기로 있고(DX11 은 Present 안, DX12 · Vulkan 은
  BeginFrame 펜스) Present 호출 자체는 p99 0.2 ms — GPU 백프레셔다. VSync 켬은 DXGI 가 판당 많아야 한 주기를 놓친다. 대기 가능 스왑체인으로 고칠 히치가 없다.
  Vulkan 스왑체인 이미지를 4 개로(+1) 늘려도 VSync 주기 놓침이 줄지 않았다(12 판 중 6 판 최악 13~27 ms — 3 개는 22 판 중 2 판).
- **틱 · 오브젝트**: 인라인 `TickItem`(GameObject 192 → 232 B), 적용 단계를 틱에 합치기(칸당 +124 B), 회전 사원수 캐시(+28 B), 축별 sin/cos 건너뛰기(18.0 → 18.7 ns), 쓰기 정렬의
  `id % 버킷`(150 → 450 us) · 키를 건에 넣기(64 → 80 B), 오브젝트 id 표 2 단 디렉터리(1 억 스폰이면 800 MB) · 늘 견주기(5.6 → 6.4 ns), 엔티티당 XML 재파싱 구조 변경(2 ms 뿐).
- **패딩 재배치 나머지**(2026-10-06, `RunPaddingReport --preset Ninja-Release --min-saving 8` 69 개): GameObject(168 → 160, 풀 칸 176 → 160)만 했다. 나머지는 인스턴스가
  적거나 나눠 쓴다 — Material · MaterialInstance 16 B(캐시 · `MaterialTintCache` · 스프라이트 공유, 수백 개 이하), Mesh 8 B(`MeshCache` 공유), 컴포넌트(RigidBody 16 · SkeletalMesh 16 ·
  Camera 8 — PROPERTY 순서 고정), 싱글턴(InputManager 64 · FrameProfiler 8). 수천 개가 만들어지는 타입이 나오면 그것만 한다. 위치 초기화 표(EditorAssetTypeInfo ·
  AssetMatchRow · CommandRow)는 모든 행을 같이 바꿔야 한다.
- **전역 소형 블록 할당자**(2026-10-06): Release 프레임당 sw 할당 7.7 회(120 프레임 × 3) — 회당 ~100 ns 로 잡아도 프레임당 1 us 아래. 기동(Release 856~1207 ms)의
  sw 할당은 Debug 기준 상주 블록 8 만 개 — 회당 100 ns 면 8 ms(1 % 아래)라 절반을 줄여도 기동의 0.5 %. 프레임당 할당이 다시 늘거나 큰 맵 로드의 프로파일에서
  할당이 5 % 를 넘으면 다시 잰다.
- **파일 시스템을 플랫폼 API 로 다시 짜기**(2026-10-06, Release 스크래치 · 2920 파일): 존재 확인 p50 10.9 vs 10.3 us · 순회 6.5~9.4 vs 7.6~10.0 ms — 시간은 같고
  할당만 준다(존재 확인 1 → 0, 순회 항목당 3.1 → 1.0). 그 할당은 기동 때만이고 상주가 아니다. `Core/File/Std` 로 감싼 뒤(Debug `FileUtilBenchTest`): 존재 확인 호출당 sw 할당자 밖 2 · sw 4
  (구분자 정규화 · UTF-16 변환 문자열), 순회 파일당 밖 2.3 · sw 6.5 — 감싸기 전 밖 1 · 순회 3.1(스크래치). 줄이려면 `toPath` 를 스택 버퍼 변환으로(sw 0).
  다시 재는 법: App Debug `-gv_memoryReport=1` 의 "cumulative since start" 줄 · `FileUtilBenchTest`. `std::filesystem` 은 `Core/File/Std` 뒤에 감췄으니 한 경로가 프로파일에 보이면 그 함수만
  `File/Windows` · `File/Linux` 로 옮긴다(기준: Release 기동이나 프레임에서 1 ms 이상 · 프레임마다 부르는 경로).
- **물리**: 파괴 잎 볼록 껍질을 워커로 나눠 짓기(Jolt 잡 시스템, 2026-10-06) — `FractureBenchTest` p50 20.7 → 24.4 ms 로 느려졌다. 워커 15 개가 청크(잎 13 개)마다
  ~10 ms(잎당 ~750 us — 직렬 55 us 의 14 배)를 썼다: 껍질 짓기는 할당이 많고 Jolt 할당이 엔진 `Memory::allocate` 로 가서 경합한다. 할당을 풀어 주지 않는 한 다시 하지 말 것.
  주의: Jolt 백엔드 TU(`Physics/Jolt/*.cpp`, PCH 없이 Jolt 정의 · 대상 기능으로 컴파일)에 `Engine/Common/EngineParallel.h` 를 넣자 물리 전체가 깨졌다(질량 · 자세가 쓰레기) —
  그 TU 에서는 엔진 태스크 대신 이미 쓰는 Jolt 잡 시스템(`JoltJobSystem`)을 쓴다.
- **태스크**: 워커 스핀 늘리기(2 → 50 us 면 SMT 형제를 빼앗아 GT 889 → 1305 us), hot pool(이득 없음), 스레드별 목록 머리 패딩(잡음), 공유 풀 위 GT 병렬화를 레인 없이(RT 170 → 261 us),
  워커 깨우기 사슬, `TaskArgs` 인라인 4 칸(기록 122 → 196 us), `PagedArray` 통합의 첫 측정(번갈아 재니 차이 없음 — 측정 착시).
- **도구 · 빌드**: 린트를 파일마다 프로세스로(5.3 → 9.4 s — 덩어리 프로세스는 7.5 → 2.25 s), 짝 헤더 메모이즈(차이 없음), `formatstring` 비템플릿 부분
  `.cpp` 분리(41 → 44 s) · 타입 소거 배열, `ContainerTypeMap` 선형 탐색 개선(파서 시간은 libclang), 팩 코덱 Zstd(−0.1 %) · LZ4(+28 %) — 이미 압축된 자산이라 Zlib 유지(압축 안 된 자산이
  들어오면 다시 잰다), 린트 파일 공유 캐시(C++ 2500 개 28.7 MB 를 다 읽어도 0.15–0.7 s — 게이트는 정규식에 쓴다), 린트 CTest 를 한 프로세스로(기동 0.17 s × 항목 수 —
  항목별 보고 · TIMEOUT 을 잃는다), 린트 증분 해시 캐시(훅 바닥 ~2–4 s 에 교차 파일 규칙 재작성 — 보류), `flatMapInProcesses` 16 → 8 워커(혼자 돌면 3.3 → 3.7 s 로
  지고 CPU −40 % — `ctest -j` 겹침에서 재 볼 것).
- **구조**: 백엔드 `*RHIResource.h` · `*RHICommandContext.h` 공통 기반(겹침이 전부 override 선언), `ResourceCache<T>`(나머지 39 % 가 소유 방식), 모듈 팩토리 골격 공통화(공통 4 줄),
  RHI · GF leaf CMakeLists 를 부모 루프로(디렉터리 스코프), `FindWindowsTools` 파이썬 이전, `EngineTest` 에 `GF_*` 자동 링크, `SW_ASSERT_NULL`, `formatstring` 인자 수 컴파일 검사의 매크로 판
  (C++20 으로 올리면 `consteval` 포맷 타입으로 옮긴다), "자유 `static` 함수 금지" 린트(오탐), "CommandList 가 RecordingState 를 소유" 린트, `CheckCodeConventions` 매개변수 · 지역변수 사슬,
  `Cb` · `Fbo` 풀어 쓰기, 되돌리기 스냅샷 바이너리 통일, `StringBuilder::appendFormat` 잘림(버퍼를 늘려 다시 포맷한다), `EditorViewportClient` 쪼개기(공통 빼기로 간다),
  RTS 버킷 · MechArena · BattleRoyale 근접 질의를 엔진 `SpatialHashGrid2D` 로(RTS 는 ~40 줄이 순서 계약을 들어 핸들 정렬 격자로 바꾸면 자동 목표 동점 · 첫 빈 광물 ·
  밀어내기 합이 바뀐다, BR 은 근접 질의가 없다, Mech 는 조종사 몇 명 전수 검사가 격자보다 싸다), 키트 칸 저장소 템플릿 `Grid2D<T>`(덮는 자리 4 곳에서 4 줄,
  저장소 모양이 키트마다 다르다).
- **게임 · 키트**: 복셀 청크 메시 제자리 갱신 — 다시 짓기는 블록이 바뀐 청크만(Debug VoxelCraft 자동 플레이 5,000 프레임에 2 회, 프레임당 최대 4 개), 새 `Mesh` 가
  렌더 스레드가 든 옛 메시와 겹치지 않는 길.
  카트 트랙을 공용 `SplinePath` 로 — 카트는 XZ 로 달려 랩 · 고스트 거리가 수평 길이다(3D 호 길이로 바꾸면 같은 트랙의 값이 바뀐다), 공유되는 것은 누적 거리 표 하나, 쓰는 게임 0.
- **늘 상위에 오는 정당한 중복**(`RunDuplicateCode`): 백엔드 인터페이스 선언 · 레이스 래퍼 전달 · 플랫폼 구현 · enum 레이블 나열 · 서비스 로케이터 둘(`sw::editor` 는 nullptr, `sw::game` 은
  assert) · `MaterialPacking` 숫자 case(`-Wswitch-enum`) · DX12 상태 조회 · `TypeInfo` 생성자 · RLE · 콜스택 관문 · 셰이더 반사 D3D11/12(확인 중 — 백로그 1-3) · Win32 마우스 case · include 묶음.
- **ReflectionParser 강제 include PCH**(2026-10-06): Debug `ReflectionParserTest` 35 케이스 25.8 초(여섯 조각, 조각당 약 4.3 초 · 한도 30 초), 파서 한 번
  1.1~1.3 초 중 공통 include 0.5 초 — 조각당 약 2 초를 위해 depfile 의존(PCH 안 헤더를 `clang_getInclusions` 가 내는가) 재검증을 떠안지 않는다.
- **롤백에 파괴 상태 싣기**(사용자 결정 2026-10-06): 롤백 키트(격투)에 파괴물이 없고 조각 물리(Jolt)는 되감지 못한다(Chaos GC 도 롤백 없음). 롤백 게임이
  파괴물을 쓰면 `saveState` 에 `makeNetworkSnapshot` 을 싣고(바뀐 사건 수일 때만) `loadState` 가 즉시 적용하는 창구를 더한다.
- **GpuScene 배치 키 중복 계산 제거**(투명 배치 헤드 키 재계산, 인스턴스마다 셰이더 경로 해시): `GT.GpuScene.build` 3670 → 3761 us 로 차이가 없었습니다.

## 4. 옛 이름 → 지금 이름 (`git log` 을 읽을 때)

| 옛 이름 | 지금 이름 |
|---|---|
| `PendingKill` · `markPendingKill` | `PendingDestroy` · `markPendingDestroy` |
| `GameConfig::_gameSettingsFile` · `EngineConfig::_engineDefaultAssets` · `EditorToolDefaults::_configFolder` 외 11 칸 | `path::kGameSettingsFile` · `path::kEngineDefaultAssets` · `config::kDirConfig(Editor)` · `EditorUtil::k…FileName` |
| 렌더 그래프 wave / 틱 wave | level / stage(`GT.Scene.tick.stages`) |
| `precede` · `succeed` | `runBefore` · `runAfter` |
| lane | queue |
| `retire*` · `retireImage` | `deferImageUnload` · `free*` |
| `*Recipe`(`D3D12RHIResourceRecipe` · `VulkanRHISamplerRecipe`) | `*Preset` · `ShaderCookRequest` |
| `poisonLiveReload` | `markGraphBroken` |
| Vulkan band | range(`SW_VK_SLOT_RANGE_SIZE`) |
| `HandleTable` · `ObjectHandle` | `SlotHandleTable` · `SlotHandle` |
| `isVisibleIntended` | `isVisibleRequested` |
| `RHI::applyPendingChange` | `RHI::recreateDevice` |
| `createParentDirectory` | `ensureParentDirectoryExists` |
| `FileUtil::fileExists` · `directoryExists` | `FileUtil::exists`(파일 · 폴더 무엇이든) · `isDirectory`(파일만은 `isRegularFile`) |
| `transformNormal` | `transformVector`(방향 변환) |
| `-gv_editorOpenAllPanels=1` | `-gv_editorOpenPanel=all` |
| `RenderResourceXml` | `Serialization/Format/ReflectedXmlFile` |
| `CameraBlendCurve` · `CameraBlendKey` · `CameraBlendSpec`(GameFramework/Base/Camera) | `BlendCurve` · `BlendCurveKey` · `BlendCurveSpec`(`Engine/Animation/BlendCurve.h`) |
| `Engine/Character/<평면 60 개>` | `Character/{Fit,Socket,Hit,Pose,AnimNotify}/`, 워핑 둘은 `Object/Animation/`(2026-10-05) |
| `Utility/Debug/*` · `Utility/Format/KeyValueFile` | `Utility/Profiling/*`(`DebugOverlayState` · `KeyValueFile` 은 `Utility/`) |
| `Graphics/Renderer/Debug/` | `Graphics/Debug/` |
| 엔진 루트 `LocalizationTools` · `EngineDevCommands.cpp` | `DevTools/` |
| `Input/Events/` · `Input/Utils/` · `Reflection/Rpc/` | 한 단계 위(`Input/` · `Reflection/`) |
| `Test/<실행 파일>/Test*.cpp`(평면) | 소스 폴더를 따르는 하위 폴더(`Test/README.md`) |
| Overworld `PlayerController` · `PlayerControllerSettings` | `OverworldTileMover` · `OverworldTileMoverSettings`(의도를 받는 몸 — 조종자는 `PlayerControllerComponent`) |
| `RtsAiController` · `SrpgAiController` | `RtsAiCommander` · `SrpgAiCommander`(명령형 장르의 AI — 조종자가 아니다) |
| `NavMeshAgentComponent::_bUpdatePosition` | `_driveMode`(`NavAgentDriveMode` — Transform · CharacterController · SteerOnly) |
| `InputReplay::play` · `updatePlayback` | `InputManager::attachVirtualInput`(재생은 가상 입력 원천) |
| `InputSnapshot` · `InputHistoryBuffer` · `InputManager::recordSnapshot` | 삭제 — 행동 층은 `ControlIntent` · `ControlIntentHistory` |
| `Base/Camera/FirstPersonCameraComponent` | `Base/Control/FirstPersonCameraComponent`(시점 = 폰의 조종 회전) |

일부러 둔 용어: stamp · kit · cook · orphan · chord · pin.

## 5. 영역별 결정

결정마다 결정한 날, 근거, 상용 엔진에서 같은 기능을 어떻게 하는지를 적습니다. 이미 코드와 README 에 반영된 결정은 한두 문장으로만 적고, 자세한 계약은 그 영역 README 의 "함정 · 계약" 절에 있습니다.
결정을 바꾸려면 여기 적힌 근거와 상용 엔진의 방식을 먼저 확인하고, 바꾼 결정은 같은 커밋에서 이 절을 고칩니다.

### 5-1. 문서와 저장소

- **저장소에 LICENSE 파일을 두지 않습니다**(2026-10-06). 라이선스를 정하는 것은 사용자의 일이라, README 의 MIT 배지와 develop 배지도 뺐습니다.
- **코딩 규칙의 원본은 `AGENTS.md` 하나입니다**(2026-10-06). [04 코딩 규칙 예시](04_CodingGuidelines.md)는 규칙을 다시 적지 않는 한국어 예시 모음입니다.
  언리얼도 Coding Standard 한 문서에 규칙을 두고 예시는 따로 둡니다. `CLAUDE.md` 는 영어 요약이고, 자주 바뀌는 목록은 링크로만 가리킵니다.
- **모듈 폴더마다 README 를 강제하지 않습니다**(2026-10-06). 있는 README 는 모두 [문서 지도](02_DocumentMap.md)에 있어야 하고, `CheckDocPaths` 가 이것을 확인합니다.
- **문서만 바뀐 커밋에서도 CI 린트 잡은 돌고, 빌드 잡만 건너뜁니다**(2026-10-06). 바뀐 파일은 git 으로 직접 판정하고, 비교할 기준 커밋을 얻지 못하면 안전하게 빌드합니다.
- **백로그는 할 일 목록만 둡니다**(2026-10-07). 끝낸 일의 교훈은 그 영역 README 의 "함정 · 계약" 절에, 결정과 기각한 안은 이 문서에, 검증 방법은 [검증과 측정](08_Verification.md)에 둡니다.
  대형 저장소가 할 일은 이슈 트래커에, 설계 결정은 결정 기록(ADR)에, 지식은 코드 옆 문서에 두는 방식과 같습니다.
- **게임 리소스는 상업적 사용 가능, 출처 표기 의무 없음, 재배포 허용(CC0 · 퍼블릭 도메인 급)만 넣습니다**(사용자 결정). 저장소가 GitHub 에 공개되기 때문입니다.
  CC BY 처럼 표기 의무가 있거나 재배포를 막는 리소스는 내려받기 스크립트로 저장소 밖에 두는 방식으로도 쓰지 않습니다. 후보는 라이선스 원문으로 확인하고, `credits.md` 에는 감사 표기만 합니다.
  쓸 수 있는 예는 Kenney, KayKit, Quaternius, VRoid 의 CC0 샘플입니다. 게임별 CC0 리소스 배치와 MechArena 는 "안정화된 뒤" 로 보류했습니다([백로그](06_Backlog.md) 1-6).
- **문서는 사람이 읽는 순서로, 한국 개발자가 쓰는 말로 씁니다**(2026-10-07, 사용자 결정). 모듈 README 는 무엇이고 왜 있나, 머릿속 그림, 따라 해 보기, 작동 원리, 확장하는 법, 함정과 주의, 더 볼 곳 순서이고 폴더 트리와 클래스 표는 쓰지 않습니다.
  버전, 빌드, 베이크, 테이블, 슬롯, 레지스트리, 엔드포인트 같은 업계 용어를 그대로 쓰고 순우리말 조어를 만들지 않습니다. 지침 전문은 docs/10_WritingDocs.md(문서 쓰기 지침)이고, 코드 주석과 커밋 메시지에도 같은 기준을 씁니다.

### 5-2. 빌드, 스크립트, CI

- **Shipping 의 RHI 백엔드는 쿠킹 테이블의 이름(`DirectX11`, `DirectX12`, `Vulkan`, `OpenGL`)으로만 고릅니다**(2026-10-06). 별칭이나 그 플랫폼에 없는 백엔드를 주면 configure 가 멈춥니다.
  언리얼의 TargetRules 도 플랫폼에 없는 RHI 를 빌드 전에 거부합니다.
- **vcpkg 라이브러리 등록 함수는 `sw_addVcpkgPackage`(못 찾으면 configure 실패)와 `sw_addVcpkgHeaderOnly` 두 가지입니다**(2026-10-06). 언리얼 ThirdParty 모듈의 두 종류(라이브러리, 헤더 전용)와 같습니다.
- **키트 빌드 순서는 모듈 매니페스트 의존 관계의 위상 정렬입니다**(2026-10-06). 폴더 이름 순서로는 공유 키트가 먼저 정의된다는 보장이 없습니다. UBT 도 모듈 의존 그래프로 순서를 정합니다.
- **스크립트 이름과 도움말 규칙**(2026-10-06): 도움말은 한국어로 씁니다. `Scripts/dev/` 의 이름은 동사로 시작하고(`Make*`, `Run*`), "Generate" 는 빌드가 만드는 생성물에만 씁니다.
  `py -3 -m Scripts gate|fix|report|selftest <이름>` 은 폴더를 찾아 부르고, CMake와 커밋 훅과 CI 는 경로로 부릅니다.
- **게이트는 파일을 고치지 않습니다**(2026-10-06). include 순서와 namespace 블록을 고치는 일은 픽서(`FormatIncludeOrder`, `FormatNamespaceBlocks`)가 하고, 픽서의 대상 파일은 `--files` 하나로 고릅니다.
  포맷터는 내용 밖의 바이트(BOM, 줄끝)를 바꾸지 않습니다. clang-format 과 같은 원칙입니다.
- **configure 중의 파이썬 생성기는 한 프로세스로 돌립니다**(2026-10-06). configure 시간의 29 % 가 파이썬 기동이었고, 한 프로세스로 묶자 configure 가 3,486 ms 에서 2,898 ms 로 줄었습니다.
- **커밋 훅의 트리 전체 게이트는 코어 수만큼만 동시에 띄우고, 린트 CTest 는 동시에 4 개씩 돌립니다**(2026-10-06). ninja 와 UBT 의 ParallelExecutor 도 동시 작업 수를 프로세서 수로 묶습니다.
  린트 CTest 는 4 와 8 이 측정 오차 안에서 같고 4 가 CPU 를 절반만 씁니다. CI 린트 잡은 처음부터 실패를 막는 단계입니다.
- **Release 와 Shipping 도 디버그 정보를 만들고, 기본은 줄 정보만(`SW_RELEASE_DEBUG_INFO=lines`)입니다**(2026-10-06). 배포물의 PDB 는 `Symbols/`, 테스트 실행 파일의 PDB 는 `TestBin/` 에 둡니다.
- **vcpkg 포트를 고칠 때는 트리플릿이 아니라 오버레이 포트(`ThirdParty/<pkg>/vcpkg-port/`)를 씁니다**(2026-10-06). 트리플릿 파일의 해시는 모든 포트의 ABI 에 들어가므로, 트리플릿을 고치면 공유 설치 트리 전체를 다시 빌드합니다.
  포트 버전을 올릴 때는 오버레이도 같이 올립니다.
- **서드파티 고지(`THIRD_PARTY_NOTICES.txt`)는 빌드가 모읍니다**(2026-10-06). 대상은 매니페스트가 끌어오는 의존 전부이고 개발 전용 라이브러리도 넣습니다. 고지가 넘치는 것은 해가 없지만 빠지면 라이선스 위반입니다.
- **규칙 예외 감사에서 정한 넷**(2026-10-07, 아직 적용 전 — [백로그](06_Backlog.md) 1-9): ① 두 플랫폼이 같은 명시적 경고 목록을 씁니다(clang-cl 의 `-Wall` 은 `-Weverything` 이라 플랫폼마다 경고가 달랐습니다).
  ② `(void)` 와 이유 주석은 `[[nodiscard]]` 가 붙은 실패 가능 함수에만 요구합니다. ③ 백엔드 명령줄 철자는 `-dx12`, `-dx11`, `-vk`, `-gl` 하나씩만 둡니다.
  ④ 출처를 모르는 리소스는 저장소에서 생성한 것이나 CC0 로 확인한 것으로 바꿉니다.

### 5-3. Core 와 플랫폼

- **파일 존재 술어는 `FileUtil::exists`(무엇이든), `isDirectory`, `isRegularFile` 셋입니다**(2026-10-06). `exists` 는 `std::filesystem::exists` 와 같은 뜻입니다. 경로 전용 타입(`FilePath`)은 만들지 않습니다.
  파일 오류는 그 자리에서 경고하고 `[[nodiscard]] bool` 로 돌려주며, 실패를 알리지 않는 시도는 `FileUtil::tryRemoveFile` 하나입니다. 언리얼 `IFileManager::Delete` 의 `bQuiet` 에 해당합니다.
- **`std::hash<fixed_string>` 은 대소문자를 구분합니다**(2026-10-06). `string` 과 `wstring` 의 해시와 같은 규칙입니다.
- **Win32 API 는 W 버전을 이름으로 부르고, A 버전도 막습니다**(2026-10-06). 전역 `UNICODE` 정의는 대상마다 정의가 빠지면 말없이 A 버전으로 돌아가서 택하지 않았습니다. 언리얼, 유니티, Godot 모두 Windows 경계에서는 W 버전만 씁니다.
  X11 창 제목은 `XStoreName` 과 `_NET_WM_NAME` 을 함께 적습니다. SDL2, GLFW, Godot 과 같은 방식입니다.
- **벽시계는 `WallClock` 하나만 읽습니다**(2026-10-06). 서비스는 현재 시각을 매개변수로 받고, 테스트는 가짜 시각을 넣습니다.

### 5-4. 데이터, 직렬화, 설정

- **엔진 데이터에는 별칭을 두지 않습니다**(2026-10-03). 실제로 출시한 게임 데이터가 없으므로, 이름을 바꾸면 데이터를 다시 씁니다. 옛 `SaveGame` 파일도 읽지 않습니다. 계약은 [Serialization README](../Source/Engine/Serialization/README.md) 에 있습니다.
- **설정 파일에는 기본값과 다른 값만 적습니다**(2026-10-06). 언리얼의 `Default*.ini` 와 같은 규칙이고, `ConfigFileSchemaTest` 가 기본값을 다시 적은 키를 막습니다.
- **설정 값의 출처는 하나씩입니다**(2026-10-06). 시작 씬은 팩의 `gamesettings.xml`, 창 제목은 게임 프리셋의 `GameConfig::_windowTitle`(언리얼 ProjectName), 고정 스텝은 `EngineConfig::_fixedDeltaTime` 입니다.
  물리는 고정 스텝 위에 서브스텝 수(`_subStepCount`)만 둡니다. 유니티의 `Time.fixedDeltaTime` 이 하나인 것과 같습니다. 기본 RHI 도 한 곳에만 적습니다.
- **앱이 다시 쓰는 에디터 상태는 `Saved/Editor/` 에 둡니다**(2026-10-06). `Config/Editor/` 에는 사람이 쓰는 설정만 남깁니다.
- **텍스처와 모델 임포트 설정은 키 테이블입니다**(2026-10-06). 리플렉션 구조체로 만들지 않았고, `PROPERTY( Config )` 메타도 지웠습니다. 엔진 타입에 키트 설정 필드를 두지 않고, 키트 설정은 팩 데이터에 둡니다.
- **게임플레이 튜닝 값은 컴포넌트 `PROPERTY` 나 키트 설정 구조체 필드로 둡니다**(2026-10-06). 언리얼의 UPROPERTY 와 데이터 에셋 방식입니다. 성능 임계값은 측정으로 정한 이름 붙은 상수로 두고, 언리얼처럼 실제로 조절할 것만 전역 변수로 만듭니다.
- **중력의 출처는 물리 설정 하나입니다**(2026-10-06). 코드는 `PhysicsSystem::getConfiguredGravity` 로 읽고, 셰이더(파도)는 머티리얼 상수로 같은 값을 받습니다. 언리얼의 `UWorld::GetGravityZ` 에 해당합니다.
- **4 글자 표식은 리틀 엔디언 FourCC 하나로 통일했습니다**(2026-10-06). 세이브와 쿠킹 바이너리를 다시 만들었습니다.
- **엔진의 각도 필드는 라디안입니다**(2026-10-06). 도 단위 필드를 두지 않고, `PropertyUnitsTest` 가 이 규칙에 예외를 두지 않습니다.

### 5-5. 그래픽스와 RHI

- **런타임 UI 를 위해 RHI 에 R8 텍스처, 영역 업로드, 프리멀티플라이 블렌드, 가위 사각형을 더했습니다**(2026-10-06). 사각형 클리핑은 가위로 합니다. Slate, Dear ImGui, Godot 모두 같은 방식이고, 배치가 끊기지 않습니다.
  둥근 모서리 클리핑만 셰이더에서 합니다.
- **프리멀티플라이 블렌드는 PSO 비트(`_bPremultipliedAlpha`)이고, 머티리얼 블렌드 enum 에는 넣지 않습니다**(2026-10-06). 언리얼도 머티리얼의 `EBlendMode` 와 RHI 블렌드 상태가 따로입니다. 머티리얼이 프리멀티플라이를 원하게 되면 그때 enum 에 더합니다.
- **톤맵을 HDR 씬 컬러로 바꾸는 일은 하지 않았습니다**(2026-10-06). 기본 포워드 파이프라인의 Reinhard 가 흰색을 0.5 로 누르지만, 바꾸면 골든 이미지를 모두 다시 떠야 합니다. 화면이 어두운 게임은 데이터(체력 바 보임 정책 등)로 맞췄습니다.

### 5-6. 오브젝트, 조종, 입력

- **프리팹에서 사라진 컴포넌트의 오버라이드는 경고하고 버립니다**(2026-10-06). 언리얼과 같습니다. 유니티는 남겨 둡니다.
- **"그 타입의 컴포넌트 모두" 는 타입별 레지스트리(`ComponentRegistry`)에서 찾습니다**(2026-10-06). 상호작용 대상을 프레임마다 씬 전체에서 찾던 경로가 Release 10k 오브젝트에서 190 us 를 넘었고, 레지스트리로 2~6 us 가 됐습니다.
  언리얼 서브시스템의 등록 목록과 같은 방식입니다. 공간 해시는 두지 않았습니다([백로그](06_Backlog.md) 1-12).
- **조종 구조**(2026-10-06): 가상 입력은 배타 모드가 기본입니다. 게임플레이 리플레이와 네트워크는 의도(`ControlIntent`)를 싣고, `InputReplay` 는 입력 층의 QA 녹화입니다.
  전략과 경영 장르는 폰 없이 디렉터가 명령 조종자가 됩니다. 시나리오는 늘 고정 프레임 시간으로 돕니다. 탑승 중에도 피격을 받고, 쓰러지면 강제로 내립니다. 탈것의 의도는 운전석 조종자를 따릅니다.
- **1인칭 카메라는 폰의 부품이라 `Base/Control` 에 둡니다**(2026-10-06). 언리얼의 `bUsePawnControlRotation` 과 같은 자리입니다. 자동 플레이는 몸 안의 분기가 아니라 AI 조종자의 빙의입니다.
- **커서 위치 API 는 `InputManager::getMousePositionNormalized` 하나입니다**(2026-10-06). 게임 쪽 코드는 장치를 직접 묻지 않고 입력 맵 액션을 읽습니다. 패드 스틱 시점은 바인딩 배율 모디파이어가 없어 아직 넣지 않았습니다([백로그](06_Backlog.md) 1-6).

### 5-7. 런타임 UI 와 글자

- **저장소에는 CC0 라틴 글꼴만 둡니다**(2026-10-06, 사용자 결정). 한글과 CJK 는 OS 시스템 글꼴로 대체하고, 시스템 글꼴을 쓰는 테스트는 글리프가 있는지만 확인합니다.
- **글리프는 단일 채널 SDF(FreeType 내장)로 그립니다**(2026-10-06). 유니티 TextMeshPro, 언리얼 SDF 글꼴과 같습니다. MSDF 는 [백로그](06_Backlog.md) 1-12 입니다. FreeType 은 `vcpkg.json` 의 엔진 직접 의존입니다.
- **체력 바와 데미지 숫자는 화면 마커 위젯입니다**(2026-10-06). 값은 컴포넌트 코드가 직접 넣습니다. 마커는 오브젝트마다 하나라 뷰모델과 문서를 두면 마커 수만큼 생기기 때문이고, 언리얼의 위젯 컴포넌트 체력 바도 보통 그렇게 합니다.
- **데이터 바인딩은 뷰모델과 필드 통지입니다**(2026-10-06). 언리얼 5 MVVM 과 같은 구조이고, 알림은 필드마다의 단조 번호로 한 프레임에 한 번 반영합니다. 폴링 바인딩은 개발 편의용입니다.
- **UI 문서와 스타일은 모두 엔진 XML 입니다**(2026-10-06). 파서가 하나여서 학습하기 쉽습니다. 스타일이 정한 필드만 위젯 필드를 이기고, 정하지 않은 필드는 위젯의 기본 모습을 씁니다.
- **엔진이 기본 옵션 메뉴와 일시정지 메뉴 문서를 두고, 게임 프리셋이 덮어씁니다**(2026-10-06). 언리얼 CommonUI 와 Lyra 의 방식입니다. 메뉴는 UI 입력 맵의 `UI.Pause` 로 열리고, UI 행동 맵은 `UiSystem` 이 소유하는 별도 맵입니다.
- **게임 정지는 시간 배율과 따로인 정지 요청 수입니다**(2026-10-06). 언리얼의 `SetGamePaused` 와 같습니다. UI, 페이드, 로딩 화면은 실제 경과 시간을 씁니다(유니티 `unscaledDeltaTime`).
- **접근성**(2026-10-06): 글자 크기는 배율이 줄여도 12 UI 단위 밑으로 내려가지 않습니다(Xbox 접근성 지침 101). 색각 보정은 Machado 2009 흉내 행렬과 오차 재분배이고 UI 전체에 겁니다.
- **HUD 문서 경로는 플레이어 오브젝트의 컴포넌트 필드이고, 로딩 화면 문서는 팩의 `gamesettings.xml` 에 둡니다**(2026-10-06). 언리얼의 `AHUD` 가 GameMode 에 붙는 것처럼 씬마다 다른 HUD 를 쓸 수 있습니다.

### 5-8. 게임프레임워크와 키트

- **GameFramework 최상위에는 `Base/`(장르 공통 기반)와 `Kits/`(장르 키트)만 둡니다**(2026-10-06). 게이트가 다른 폴더를 막습니다.
- **모듈 이름은 공유 `GF_<X>`, 서버 전용 `GF_Server_<X>`, 클라이언트 전용 `GF_Client_<X>` 입니다**(2026-10-06). `CheckModuleTargets` 가 접두와 매니페스트의 대상(`_listTarget`)이 맞는지 봅니다.
  서버 키트는 같은 기능의 공유 키트만 include 할 수 있고, 키트끼리는 include 하지 않습니다.
- **키트 조립 규칙**(2026-10-06): 조립 테스트 게임은 `MeadowVillage` 입니다. 플레이어와 세계가 들고 있는 가방은 `Inventory`, "아이템과 개수" 값 목록은 `ItemStackList` 입니다.
  가방의 칸 구조체는 `InventorySlot` 이고, 달력은 `WorldClock` 하나입니다. 키트 하나만 쓰는 게임은 디렉터가 기반 상태를 들고 키트에 빌려 줍니다. 언리얼 Lyra 도 단일 기능 게임은 GameMode 나 GameState 에 직접 상태를 둡니다.
- **온라인 게임의 로컬 지갑은 서버 원장의 읽기 사본입니다**(2026-10-06).

### 5-9. 온라인 서비스, 네트워크, 보안

- **DB 와 캐시**(2026-10-06, 사용자 결정): 영속 저장소는 PostgreSQL(서버)과 SQLite(개발용 단독 서버, 클라이언트 로컬)입니다. 캐시는 RESP 드라이버 하나로 리눅스는 Valkey, 윈도우는 Garnet 을 씁니다.
  Redis 7.4 이상은 RSAL/SSPL 이라 "오픈소스이고 무료" 조건 밖이고, Redis 8 의 AGPLv3 선택도 그 의무 때문에 택하지 않았습니다. 전용 서버는 윈도우와 리눅스 둘 다 1 급입니다. 남은 일은 [백로그](06_Backlog.md) 1-7 "네트워크 서비스 계층" 에 있습니다.
- **암호 라이브러리는 OpenSSL(vcpkg, Apache-2.0)입니다**(2026-10-06). Windows 에서는 지금 트리플릿대로 DLL 로 링크합니다. 비밀번호 해시는 보안 제공자의 Argon2id 이고, 기본값은 OWASP 권장(19 MiB, 반복 2)입니다.
- **인증기 없는 UDP 암호화와 자체 서명 개발용 인증서는 개발 빌드에만 있습니다**(2026-10-06). Shipping 서버는 설정 파일의 인증서와 키 경로가 필수입니다.
- **계정**(2026-10-06): 같은 계정으로 다시 로그인하면 새 로그인이 옛 세션을 밀어냅니다. 게스트를 정식 계정에 연동할 때 이미 쓰인 이름이나 플랫폼 계정은 거절합니다(자동 합치기 없음).
  탈퇴는 30 일 유예 뒤 지우고, 원장과 감사 기록에는 계정 id 만 남깁니다. PC 외부 로그인은 공개 클라이언트와 PKCE(S256)이고, 서명은 RS256 과 ES256 만 받습니다.
- **로컬 저장의 봉인은 장치 키입니다**(2026-10-06). 실수나 가벼운 변조를 막을 뿐이고, 경쟁 데이터의 원본은 서버입니다.
- **경제**(2026-10-06): 거래는 맡김 없이 양쪽이 확정하는 순간 원장 분개 하나로 정산합니다. 경매와 우편 첨부는 맡김 방식입니다. 유상과 무상 재화의 차감 순서는 데이터이고, 기본은 무상 먼저입니다.
  환불 회수에만 음수 잔액(빚)을 허용하고, 빚이 있는 동안 그 재화로 살 수 없습니다(PlayFab 의 Subtract 와 같은 자리). 원장, 우편 넣기, 제재처럼 여러 키트가 한 트랜잭션에 넣는 쓰기는 기반 `Online/` 에 둡니다.
- **우편 만료**(2026-10-06): 운영 우편과 보상 우편은 첨부가 사라지고, 플레이어 우편은 보낸 사람에게 돌아갑니다. 보관 기간은 30 일입니다.
- **응답 형식은 하나입니다**(2026-10-06). 업무 결과는 응답 본문의 첫 값이고, 오류 코드는 공통 `OnlineError` 만 씁니다. gRPC 의 status 와 본문 구분과 같습니다. 키트 클라이언트는 요청마다 완료 델리게이트를 받습니다(언리얼 Online Services).
- **운영 HTTP 엔드포인트는 기본으로 꺼져 있고(`_opsPort` 0), 켜면 `127.0.0.1` 에 바인딩합니다**(2026-10-06). Nakama 의 기본값과 같습니다. 운영 지표와 상태 엔드포인트는 엔진(`Engine/Observability`)에 있어 전용 서버가 GameFramework 없이 씁니다.
- **매칭 권한은 모드마다 캐시 임대(10 초) 하나입니다**(2026-10-06). 순위표 점수는 서버만 제출하고, 표마다 허용한 경우(`_bClientSubmit`)에만 클라이언트가 제출합니다. 길드는 소셜 키트(`GF_Server_Social`) 안에 있습니다.
- **외부 계정이나 비용이 드는 연동은 계약과 가짜 제공자까지만 둡니다**(2026-10-06). APNs, FCM 푸시와 Apple, Google, Steam 영수증 검증이 여기에 해당합니다. 금칙어 목록은 테스트용 낱말만 저장소에 둡니다.
- **부하 테스트 봇(`Tools/OnlineLoadBot`)은 Dev 구성에서만 빌드합니다**(2026-10-06).

### 5-10. 전용 서버

- **전용 서버는 별도 실행 파일 `Server` 입니다**(2026-10-06). App 의 모드가 아닙니다. 기존 Shipping 프리셋은 Client 타깃이고, Dev 기본은 Game 타깃입니다. 언리얼의 `<Game>Server` 타깃과 같습니다.
- **서버 설정은 Shipping 에서도 디스크에서 읽고, 없으면 기동이 실패합니다**(2026-10-06). Windows 서비스 모드도 있습니다.
- **서버 전용 쿠킹은 하지 않고, 패키징할 때 타깃별로 에셋 종류를 뺍니다**(2026-10-06). 처음 빼는 종류는 텍스처, 셰이더 바이너리, 오디오입니다. 메시와 애니메이션은 충돌, 소켓, 히트박스, 루트 모션 때문에 남깁니다.
  패키징과 런타임이 같은 테이블(`Config/Engine/CookContract.json` 의 `target_excluded_asset_kinds`)을 읽습니다. 언리얼이 전용 서버에서 텍스처와 사운드 로드를 건너뛰는 것과 같은 목적입니다.

### 5-11. 에디터와 외부 공개

- **imgui-node-editor 의 vcpkg 수정은 업스트림에 PR 을 내지 않고 오버레이로 유지합니다**(2026-10-06). 외부에 공개하는 일은 사용자가 요청할 때만 합니다.
- **DPI 150 % 는 대체 테스트로 확인하고, 글자 래스터 선명도만 실제 모니터에서 확인합니다**(2026-10-06). 남은 확인은 [백로그](06_Backlog.md) 1-12 에 있습니다.
