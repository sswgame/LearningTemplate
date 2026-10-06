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
