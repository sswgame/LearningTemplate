# 설계 백로그

> 질문: 아직 안 한 설계 계획과 열린 판정은 무엇이고, 앞으로의 일을 막는 결정은 무엇인가?
> 독자: 이 저장소에서 일하는 에이전트와 사용자.
> 결론: 남은 계획 · 열린 판정만, 끝난 것은 지운다(기록은 git). 할 일 · 측정 목록은 [NEXT](NEXT.md) 하나다.

## 열린 설계 후보

### 구조와 시스템

- 층 경계: 실제 순환 · 독립 배포 요구가 확인된 층만 별도 링크 타깃으로 나눈다.
- `ModuleSharedAssetCache.h` 의 여러 include 는 결함 증거가 아니다 — 쓰는 쪽이 공유 캐시 계약에 기대는지 본 뒤에만 좁힌다.
- RHI 형식 지원: 형식 · 용도 조합은 실제 자원 · 렌더 상태로 질의하고, 미지원 경로는 백엔드별 시나리오로 본다(lavapipe `R16G16_SNORM` 블렌드 거부는 옛 증상일 수 있다 — 재현될 때만 본다).

### 이름과 용어

- `naming-words` 밖의 멤버 · 지역 · 매개변수 형태(`_camelCase` · `out…` · 단수 컨테이너)는 단계적으로 자동 검사에 잇는다. 대량 일괄 개명은 하지 않는다.
- 구조 감사의 모호한 이름 후보(파일 24 · 폴더 5 · `Util` 없는 정적 함수 타입 464 · 첫 단어 반복 폴더 29)는 위반 수가 아니다. 바꿀 때는 리플렉션 · 직렬화 · 에셋 경로 참조까지 함께.

### 구조 리팩터링 코드 대조

- `FrameViewports` 가 서버 · 쿠커 타깃에 불필요한 의존을 끄는지 include 그래프로 보고, 위반이 재현될 때만 뷰 계약을 나눈다.
- 기본 씬 수집과 `RenderExtensionRegistry` 는 합치지 않는다 — 같은 기능을 두 번 구현하는 사례가 나올 때만 공통 기여 API.
- 온라인 두 번째 서버 추상화(`OnlineSession`)를 먼저 만들지 않는다 — `OnlineProtocolService` 가 공통 기반.
- 유지: 타입 기반 온라인 프로토콜, InterestReplicationPolicy 증분 가시 목록, 프로브 revision 캐시, 머티리얼 변경 감지, `IEngineRenderDriver` 경계.
- 측정 전에는 결함으로 세지 않는다: 헤더 전파 · TU 비용, 시작 시간 단계 비중, UI · VFX 배열 복사, 프레임 경로 intern.

### 구조 점검에서 닫은 것(SOLID 점검)

번호는 코드 주석이 가리킨다(`Core · Engine 15` 처럼). 모두 끝냈거나 하지 않기로 했다 — 다시 열지 않는다.

- Core · Engine: 13 `BindingKind` switch 는 둔다(`-Werror=switch`), 14 크기만 보고 파일을 나누지 않는다, 16 관측 넷(Diagnostics · Profiling · Observability · Telemetry)은 역할이 달라 둔다.
- GameFramework · Editor · App(닫음): 8 파일 감시 넷, 12 온라인 소도우미 복사, 14 Runtime 의 장르 기믹(공용 프리팹 라이브러리), 16 큰 패널(탭이 독립 상태 · 수명을 가질 때만 뗀다), 17 큰 인터페이스(`EditorAPI` · `IEditor`).
- 큰 클래스는 제 상태와 규칙이 있는 묶음만 뗀다(같은 클래스는 한 파일). RTS 유닛 슬롯은 `SlotHandleTable` 로 옮기지 않는다 — 순서 · LIFO 빈 칸이 결정적 결과와 저장 바이트에 든다.
- 에디터 콘솔 명령은 기능의 명령 파일 옆에. 짝 없는 cpp 는 스스로 등록하는 단위 · 플랫폼 구현 · 모듈 진입점만.

### 데이터 기반 값 · 매직 리터럴

- 이미 PROPERTY 인 게임 값의 생성자 기본값은 하드코딩으로 세지 않는다. sentinel 은 `-1` 표기가 아니라 도메인 의미와 저장 · 통신 호환성으로 정한다(규칙 [Constants](agents/Constants.md)).

### App 프로세스 종료 코드

- 현 체계를 둔다(`Config/Engine/ProcessExitCodes.json` 단일 출처 + 생성기). `Scripts/common/exit_code.py` 와 합치지 않는다.
- 후보(낮음): 스키마에 플랫폼 종료 코드 범위 · 예약 코드 · 자동화 결과 매핑 누락 검사와 생성기 테스트.

### 추가 아키텍처 후보

- 엔진 서비스 슬롯은 프로세스 전역 하나다. 동시 Engine(다중 미리보기 · 인프로세스 서버+클라이언트)이 실제로 요구될 때만 컨텍스트별 슬롯 + 두 인스턴스 격리 테스트.
- 서브시스템 의존 두 선언(등록 `after` · `getSubsystemDependencies()`)은 뜻이 달라 합치지 않는다.

### 셰이더 구조와 디퍼드 · G버퍼

- G버퍼 속성 추가는 남는 채널 · 정밀도 검사 뒤 판(`SW_GBUFFER_LAYOUT_VERSION`)을 올린다.
- 겹친 엽(박막 · 털 · 스타킹 · 비단)은 포워드 전용(`MATERIAL_FORWARD_ONLY`)으로 둔다 — 칸을 늘리면 모든 디퍼드 픽셀이 대역폭을 치른다. 다시 볼 조건: 겹친 엽 머티리얼이 화면 큰 몫인 장면에서 ForwardOnly 패스 GPU 시간이 디퍼드 이득을 넘을 때(Substrate 식 가변 칸 검토).
- 표면 모델은 `.material` 의 `surfaceModel` 이 선언하고 셰이더 조명 훅과 맞춰 본다(정함).
- 남은 몫: 기믹 표면(Emissive — `gimmicksurface*.material`)이 G버퍼 발광 칸(판 3)이 생긴 뒤에도 `MATERIAL_FORWARD_ONLY` 를 직접 적는다 — 빼면 픽셀이 바뀔 수 있어 골든과 함께 옮긴다.
- 포워드 · 디퍼드 비교는 같은 장면 · 해상도 · 플랫폼에서 GPU 시간 · G버퍼 대역폭 · PSO 수 · 라이트 수로. 모바일 · 웹은 측정 전까지 포워드.
- 머티리얼 제작 UX 후보: 게임 재료 시작 그래프, 미리보기 대표 메시 · 조명 선택, 누락 텍스처 · 쿠킹 상태 인라인 표시 — 실제 제작 시나리오로 병목부터 찾는다.

### Test Framework

- 별도 테스트 프레임워크를 들이지 않는다.
- 샤드 시간 균형은 JSON 실행 시간에서 병목이 실제일 때만. 같은 프로세스 병렬은 `TestExecutionContext` 를 케이스별로 가르기 전에는 하지 않는다.
- 완결 루프가 빈 장르 게임에만 수직 슬라이스 시나리오를 더한다. 자동 플레이는 플레이어와 같은 입력 · 서비스를 쓴다(치트 경로 금지).

### 데이터 기반 카탈로그 참조 전수 점검

엄격도 계약(지킨다): 같은 파일 필수 id 연결은 로드 · 쿠킹 실패, 다른 팩 · 코드가 채울 연결은 `optional="true"` 경고, 코드 등록 이름 오류는 로드 실패, 다른 파일 카탈로그 연결은 둘을 쥐는 쪽이 시작 때 검사, 원소 자기 칸 오류는 그 원소 제외. 참조 표는 `Content/engine/settings/datacatalogreferences.xml` 한 벌.

### ReflectionParser · 런타임 Reflection

- 파서 토큰화(측정 후보, S8): `ReflectionParser.TokenizeStats` 가 10% 를 넘으면 토큰 저장소 재사용(기본 인자 · 매크로 이름 · 별칭 fixture 결과 보존).
- 캐시를 더하거나 등록 · 언로드를 바꾸면 무효화 순서와 모듈 재등록 테스트를 함께. 흔들리는 시간 문턱은 CI 에 넣지 않는다.
- 각 형식 `Radians`(구조 감사 6) 남은 몫: 벡터 각(`float2` 경사 띠 · `float3` 오일러)은 회전 형식을 정할 때 옮긴다. `SkinLayerValue::_rotationDegrees` 는 그대로.
- `callWithText` · `bindEventToFunction` 콘솔 · 비주얼 배선은 수요가 생길 때만(허용 목록 · 수명 · 핫 리로드 검증과 함께).
- FUNCTION 호출(NEXT 5-4): 주인 오브젝트 밖을 바꾸는 함수가 생기면 대상을 인자(핸들)로 드러내 편집 명령을 타게 한다. 같은 인자 묶음 반복 수요가 보이면 즐겨찾기에 인자 글을 함께 담는다. `Meta="Transactional"` 같은 선언은 두지 않는다.

### 비동기 · 병렬 처리

기준(사용자 2026-11-08): 연쇄 모양으로 고른다 — ① N 비례 독립 ② 앞단이 뒤를 막는 사슬 ③ N². 확인은 `ctest -L load`. 도구는 [CppStyle](agents/CppStyle.md) "Parallel and async", 새 풀을 만들지 않는다.

| 자리 | 남은 판단 |
|---|---|
| 씬 저장 | `base` · `write` 워커 이전은 S8 판정. 성공 전에 source path · dirty 를 완료 처리하지 않는다 |
| 씬 로드 | `instantiate` 가 우세할 때만 순수 전처리 병렬, 생성 · 부착 · 등록은 소유 경계 유지 |
| 씬 쿠킹 | 합이 쿠킹 전체를 지배할 때만 — 캐시 격리 · 결과 병합이 먼저(루프만 태스크로 감싸지 않는다) |
| 게임 스레드 에셋 로드 | `AssetLoadProfiler` 스톨 상위만 비동기로 |
| 렌더 패스 기록 | 레벨 폭 문턱은 S8 |
| AI 군중 | `load/navcrowd` 가 문턱을 넘을 때만 지역 분할 |
| 병렬 틱 남 읽기(구조 감사 2) | 단언에 걸리면 `readOther` · 틱 뒤 작업 · 그룹 옮기기로 고친다 |
| 틱 비용 기반 분배 | 기본 정책을 바꾸지 않는다(8 게임 중 1 만 이득) |

### 렌더 스레드와 업로드 전용 스레드

- 전용 업로드 스레드는 넣지 않는다. 메시 GPU 생성 대기(`GT.GPUUpload.flush`)를 없애려면 "보이는 메시는 그 프레임에 있다" 계약부터 바꾼다.
- copy queue 는 중첩 이득 측정이 있을 때만.
- RT 의 RHI 실행이 병목이면(S8: pump 가 RT 의 10% 초과) RHI Thread 형 시제품을 검토한다.

### 에디터 UI 스레드 분리 검토

- 결정(사용자 2026-11-18): 스레드는 나누지 않고 MVVM 까지 한다. 창 · 메시지 펌프 이전, 락스텝 UI 스레드, 한 프레임 지연 나란히 돌기는 하지 않는다. 다시 열 조건: 무거운 씬에서 정본 방식(비동기 로딩 · 컴파일 · 베이크, 에디터 틱 낮추기)으로도 UI 응답이 기준을 넘는다는 측정값.
- 창 메시지를 날것으로 미뤄 ImGui 가 큐에서 읽게 하지 않는다 — 번역은 창을 가진 스레드에서.
- MVVM 남은 몫: `Common/GUI` 의 나머지(상태 표시 · 도킹 레이아웃 · 테마)를 `[panelViewBan] views` 로. 문서 패널 View 강제 범위는 감사 E-16. 계약 정본 [EditorUI](agents/EditorUI.md) "MVVM".
- 다른 UI 프레임워크 전환은 나중 — 그때 C API 경계와 스냅샷 변경 알림을 먼저 세운다.

### 데이터 구동 구조 후보 판별

- 형식 · 프로토콜 · 수학 표는 명세라 코드에 둔다. 코드에 두는 AI: `KartAI` · `SRPGCommanderAI` · 몸 · 표시 상태 컴포넌트.
- 남은 것: 문화권별 통화 기호(필요할 때 칸 추가).

### 에디터 기능 · UX

- 있는 기능을 새로 만들지 않고 Undo 누락 · 취소 불가 편집을 시나리오로 찾아 보강한다(감사 E-1 · E-2 · E-13).
- 첫 사용(중간): 빈 상태(새 프로젝트 · 빈 폴더 · 첫 임포트 · Map Check 오류 · 첫 패키징)에 다음 행동을 잇는다. Welcome 화면은 필요가 확인될 때.
- 시각 저작(높음): 범용 시각 편집 프레임워크를 만들지 않는다 — 데이터 의미로 도구를 고르고, 같은 결합이 반복될 때만 공용 헬퍼. 첫 사례(애니메이션 마스크 · 소켓)로 공통 코드가 있는지 본다 — 완료는 XML 왕복 · 다른 스켈레톤 · 재로드 배치 일치 · `uiaudit` · `uicapture`.
- 규칙: 미리보기 전용 사본 금지, 선택 · 문서가 바뀌면 끌기를 끝내고 빠른 끌기는 Undo 한 단계, 사라진 본 · 소켓 참조는 조용히 버리지 않고 표시.
- 타일맵 자동 투명 영역 · 회전 아틀라스는 콘텐츠가 요구할 때.
- 제외: 코드 에디터 · 마켓플레이스 · 클라우드 협업 · 원격 빌드 · AI 검색 · 멀티 유저 편집.

### 키트별 무료 상업용 리소스 (CC0 기준)

- 키트마다 대표 동작을 보이는 최소 샘플만. 먼저 각 게임 `credits.md` 를 키트별로 이어 중복을 막는다.
- 허용: Kenney · Poly Haven · ambientCG(CC0), 기존 KayKit · 오디오 팩은 기록 그대로, 업로드 사이트는 항목별 CC0 확인. 금지: Quaternius QAL · CC-BY · NC · ND · 불명확한 것, 상표 · 초상 · 유명 IP.
- 추가 전 기록: 쓰는 파일 · 키트 id, 원본 페이지 · 라이선스 원문, SHA-256 · 받은 날짜, 변환 경로. 원본은 쓰는 파일만 Git LFS. 자동 다운로드 도구는 만들지 않는다.
- 네트워크 · 온라인 · 저장소 키트는 합성 fixture 를 쓴다(실제 공급자 로고 · 사용자 자료 금지).

### GameFramework

- `GameInstance` 조정 책임(중간): 수명 · 호스트 · 실패 정책이 다른 책임만 나누고 `update` 순서는 계약 테스트로 고정한다. 여러 인스턴스가 요구되면 명시적 게임 컨텍스트(thread-local 금지).
- 키트 의존 · 모듈 종류는 `*.module.json` 이 정본 — 다른 레지스트리로 복제하지 않는다.

### 무기와 발사체 조합(정함)

- 효과는 탄 × 표면 맞음 표(`ProjectileCatalog::findImpact`)가 정한다 — 탄에 반응 수치를 베끼지 않는다. 무기 호환은 탄 태그 식(`_accepts`).
- 2D 는 평면 제약 3D — 차원마다 관통 · 튕김 · 유도를 두 벌 쓰지 않는다. 걸음 안에서 세계를 바꾸지 않는다(사건은 걸음 뒤).
- 복제가 생기면 시뮬레이션은 서버 권위, 짐(`ProjectileCargo`)은 서버에.

### 오픈월드 액션 RPG 키트: MMO 와 싱글플레이 분리

- 게임 이름 · "대형 오픈월드" 키트를 만들지 않는다. 장르 키트는 두 번째 사용처에서 같은 소유 · 저장 · 데이터 계약이 확인될 때만 나눈다(공통 후보: 지역 · 관심 지점 진행, 발견 기록, 탐험 지도, 보스 계약, 탈것 인터랙션, 채집 · 제작 진행).
- `GF_ActionMMO` 후보: 서버 권위 · 영속 프로필 · 구역/샤드 · 파티/길드 · 시장 일관성 · 대규모 전투 복제. 부하 · 운영 지표 전에 월드 규모를 약속하지 않는다.
- `GF_OpenWorldActionAdventure` 후보: 캠페인 · 지역 상태 · 동료 · 보스 · 탈것, MMO 의존 없음. DLC 는 기존 세이브를 깨지 않는 선택 확장.
- 말 · 배 · 전투 · 퀘스트는 바로 공유하지 않는다. 운영형 누적 업데이트는 판 붙은 카탈로그 · 선택 팩으로.

### 하우징 · 주거 건축 키트

- 저장 정본은 부지 id · 안정 배치 id · 부품 id/판 · 변환(임시 핸들 금지). MMO 는 소유 · 판정 · 저장을 서버 권위로.
- 넣지 않는 것: 구조 안정성 시뮬레이션, 임의 메시 → 벽 생성, NPC 가구 자동 이용, 온라인 동시 건축.

### 핫 리로드와 스크립팅 언어 판단

- 범용 스크립트 VM(Lua 등)은 넣지 않는다 — 모드가 새 로직을 저작해야 한다는 요구가 확인될 때만 재평가. 먼저 `BehaviorTreeAsset` · `ConditionExpr` · `DialogueGraphAsset` · `AnimationGraphPlayer` 에 노드 · 식을 더한다.

### 모드 지원 키트

- 쿠킹된 콘텐츠 · 데이터 모드만 받는다(네이티브 DLL · 스크립트 VM 범위 밖). 팩 기능을 복제하지 않는다.
- 런타임 토글은 넣지 않는다 — 요구가 되면 언마운트 · 에셋 해제 안전 규칙과 함께.
- 특정 스토어 연동을 엔진에 박지 않는다(어댑터로). `PackPatchTrust` 를 모드 제작자 신뢰로 넓히지 않는다 — 외부 모드를 받으면 서명 · 출처 · 권한 정책을 먼저 정한다.

### 게임 액션 데이터 저작과 에디터

- `GameActionCall` · `ActionRegistry` 와 GAS 형 `Ability` 는 합치지 않는다.
- 순서(사용성 문제가 확인되면): (1) 등록 메타데이터(설명 · 분류 · 문맥), (2) 에디터 분류 · 검색 · 인라인 문제 표시 · 동작 교체 시 호환 안 되는 값 처리, (3) 범용 그래프 전에 막히는 콘텐츠 사례 수집, (4) 완료는 XML 왕복 · 잘못된 참조 거부 · 화면 검토.

### VFX 저작 · 캐릭터 잔상 · 분신

- 잔상은 품질 단계별 상한. 분신은 필요할 때 별도 프리팹 · 애니메이션 인스턴스로(포즈 재생형인지 독립 개체인지 먼저 가른다). 임의 그래프 · 모듈 시스템을 먼저 들이지 않는다.

### 체크포인트 · 리스폰 공통 키트(설계 2026-11-20)

구현은 섰다(`Runtime/Gameplay/Respawn`). 지킬 것 · 남은 것:

- `MechArenaWorld`(비트 동일) · `TrampolineArena` · 카트 관문은 옮기지 않는다. 목숨 · 시체 · 적 리셋 · 팀 코스트는 장르 규칙으로 남긴다. 동료 부활은 정책이 아니다(`HealthStateMachine`).
- 클라이언트는 정책 종류 · 지점 id 해시만 보낸다 — 자리 · 비용 · 잔액은 서버가 판정.
- 남은 몫: 두 프로세스 시나리오는 아바타가 죽는 게임(LanternVale 아바타 사망 규칙)이 생길 때 그 게임 `netpair/` 에 더한다.

## 통합 담당 판단(2026-11-08)

- 계약(S2): 프로세스당 Engine · GameInstance 하나(중복은 시작 오류). XML 의 같은 종류 id 겹침은 오류(코드 `add*` 교체는 유지).
- 에디터 작업 안전(S3): 소스 관리 연동은 Git 만, 외부 계정 · 유료 공급자 없음. 자동 저장은 원본을 덮지 않는다.
- 개발 그림(디버그 드로우 · HUD)은 스크린샷 · `<ExpectImage>` 골든에 들지 않는다 — 캔버스 겹치기 등록은 층(`CanvasOverlayLayer`)을 밝힌다.
- 하지 않는다: 범용 스크립팅 VM · 네이티브 모드 DLL · 원격 명령 서버. Meadow 밭 크기 · Shooter3D 탄착 표면 목록은 코드 상수로 둔다.
- 리소스 라이선스(사용자 결정 2026-11-08): 저장소의 Quaternius 묶음 여덟은 둔다. 새 리소스는 CC0 우선, 재배포 되는 MIT 류는 고지와 함께, QAL · CC-BY · 불명확은 다른 CC0 출처로.
- 측정(S8)은 [NEXT](NEXT.md) 4 절. 재서 이득이 없어 닫은 값만 여기에 한 줄씩 남긴다.

### 지역 효과의 범용 구조(사용자 2026-11-08 — 용암 · 바다 · 늪 · 유황 · 독가스)

- 새 효과는 모듈 한 줄(`SW_ZONE_MODULE`)로 더한다 — 영역마다 컴포넌트를 늘리지 않는다. 모듈은 서로를 모르고, 반응 컴포넌트는 태그 · 속성만 읽는다.

### 표면 변형 층(사용자 2026-11-08 — 지나온 길 · 파묻힘 · 빨려드는 구멍)

- CPU 높이 격자를 유지한다. 도장이 프레임당 수백 개로 늘면 컴퓨트로 옮긴다. 충돌 · 내비 · 높이장은 바꾸지 않는다.

### 관성 이동과 탑승 가속감(사용자 2026-11-08 — 썰매처럼 경사에서 빨라지고 느려지고, 타면 가속이 느껴지게)

- 바깥 힘은 `InertiaSettings::computeAlongAcceleration` 한 벌을 함께 쓴다(탈것마다 따로 쓰지 않는다). 배는 새 물리 없이 조합한다. 카트 트랙 전용 시나리오는 두지 않는다. 비행은 범위 밖.

### 에셋 정체성: 경로 id → 무작위 GUID (사용자 결정 2026-11-11)

- 정체성은 GUID, 내용 변경은 내용 해시 — 한 값에 두지 않는다. 형식 판 올림으로 GUID 를 넣지 않는다(`asset-ids --fix` 가 쓴다). 사람은 `.meta` · 머리를 만지지 않는다(도구가 짝을 지킨다).
- 남은 몫: 원본 보관(LFS · 별도 저장소, `_source`)과 공유 DDC 는 3 단계(아래 "도구 · 에디터" 절). 정체성 대체 · 합치기용 GUID→GUID 표는 2 단계.
- 알려진 틈: 저장 입구를 지나지 않는 `FileUtil::writeTextFile` 쓰기는 머리를 잃는다 — 생성물 바이트 비교 검사가 있는 도구는 `AssetGUIDHeader::carry` 를 쓴다.

## 길드 창고의 이전 불가 자산(사용자 결정 2026-11-12, 감사 O-10)

- 이전 불가 자산(유료 재화 · 귀속 아이템)은 길드 창고를 거쳐도 맡긴 당사자가 허용한 권한 안에서만 다른 계정으로 간다. 허용이 없으면 맡긴 사람만 꺼낸다.

## 렌더 · RHI 설계(2026-11-17)

절마다 계획 · 측정 문턱만. 상용 내부 이름은 기억 기준이라 작업자가 그 판의 문서로 다시 본다. 새 린트 규칙 없이 형식으로 막는다.

### bindless 번호 고정과 디스크립터 다시 쓰기(설계 2026-11-17, 감사 R-6)

구현됐다(`rebindBindlessTexture` · `RHIDescriptorRingMirror` · `TextureStreamingPolicy`). 하지 않는다: 힙 · 세트 하나 제자리 덮어쓰기, 갱신마다 힙 전체 복사, 셰이더 간접 표, 스파스 텍스처, 텍스처별 세대.

남은 측정(NEXT 4 절):

| 무엇 | 판정 |
|---|---|
| `GT.Material.textureResyncs` | 밉이 오르는 동안 0 |
| `RT.Bindless.mirrorWrites` | ≤ 3 × (등록 + rebind), 등록 시간 증가 1 µs 아래 |
| dx12 · vk 검증 레이어로 shooter3d · load 체인 | 디스크립터 메시지 0 |
| `load/texturebudget`(4K 300 장) | 지난 뒤 120 프레임 안에 예산 아래, 골든은 예산 없는 실행과 1 % 안 |

### 텍스처 스트리밍 밉 범위 대기 줄(설계 2026-11-17, 감사 R-6 · A-12)

구현됐다(`TextureMipLayout` · `readRangeAsync` · `_maxBytesInFlight`, 팩 블록 읽기). 하지 않는다: 파일 꼬리 · 벌크 분리, KTX2, 메모리 맵.

남은 측정:

| 무엇 | 판정 |
|---|---|
| `TextureStreaming.bytesInFlight` | 늘 `_maxBytesInFlight` 아래 |
| 첫 화면 읽은 바이트(2K 이상) | 전체의 10 % 아래, 시간은 흔들림 밖으로 느리지 않다(느리면 첫 로드만 전체 읽기로) |
| 화면에 든 뒤 전체 밉까지 p95 | 10 프레임 넘게 늘지 않는다(늘면 `_maxBytesInFlight` · 프레임당 제출 수를 올린다) |

**R-15 후속(2026-11-17) — 남은 일.**

- **R-15 후속 1 GI 복셀화의 원본을 GPU 로.** 지금 `SceneVoxelizer` 는 CPU 정점(`Mesh::getVertices`)을 칠한다.
  1. 프레임 그래프 실행기가 첫 리스트 앞에 고정 기록하는 **프레임 업로드 단계** 하나(파이프라인 XML 패스가 아니다)에서 정점 풀의 GPU 복사 · 상태 전이를 기록한다. 풀 정점 버퍼는 GPU 전용(`createGPUVertexBuffer`), 인덱스는 CPU 인덱스 그대로.
  2. 복셀화는 기하 셰이더 없는 3 축 투영 — 삼각형 축은 컴퓨트로 미리 구해 정점 흐름에 싣고, 픽셀 셰이더가 3D UAV 에 원자 쓰기. 얇은 벽은 복셀 절반 바깥 확장으로 막는다. 보존 래스터 · 기하 셰이더는 쓰지 않는다(다섯 백엔드 불일치).
  3. 스킨 · 모프 메시는 CPU 정점을 쥔다(Keep) — Packed 프록시에서 휴지 자세를 풀지 않는다.
- **R-15 후속 2 CPU 정점 놓기**(의존: 후속 1). 지금 모든 메시가 `MeshCPUAccess::Keep`. `Mesh::create` · `MeshCache::request` 에 기본값 없이 받게 하고(절차 메시 · 스킨 · 모프 Keep, 요청이 갈리면 Keep, 놓인 뒤 Keep 요청이면 `.mesh` 다시 읽기), 업로드가 끝난 뒤 놓고, 장치 교체 때 `.mesh` 를 다시 읽는다.

### 자동 렌더 배율: GPU 프레임 시간과 뷰 사각형(설계 2026-11-17, 감사 RN-8)

구현됐다(`GPUFrameClock` · `RenderScaleAdjuster::update( const GPUFrameSample& )` · `PassTexture` 사각형 변환 · `RenderScale.json`). 벽시계 · CPU `RT.Frame` 으로 대신하지 않는다 — 타임스탬프 없는 장치는 오류 한 번 뒤 끈다.

남은 측정:

| 무엇 | 판정 |
|---|---|
| Shooter3D `benchmark` vsync + 자동 배율 | 부하가 걷힌 뒤 `_upFrames` × 단계 수 프레임 안에 1.0 |
| 시계 켬 · 끔 `RT.Frame` p50(dx12 · vk · gl) | 차이가 흔들림 안(밖이면 S8 에 적는다) |
| `renderscalestep`(0.7 ↔ 1.0) | `RT.Frame.transientAllocations` 0, 프레임 시간이 중앙값 1.2 배 아래 |
| 0.7 그림 대 `gv_renderScale=0.7`, 다섯 백엔드 | 차이 1 % 아래, 가장자리 번짐 없음 |

### 입자 정렬 모드(설계 2026-11-17, 감사 RN-15)

구현됐다(`VFXSortMode` · `VFXParticleOrder` · `vfx_particle_sort.hlsl`). 지킬 것: 뜻 없는 조합(더하기 블렌드 · 띠 · 불투명 메시 + 정렬)과 GPU `ViewDepth` 에서 `_maxParticles` > 512 는 로드 오류(조용히 건너뛰지 않는다). 512 개 넘게 정렬할 알파 GPU 효과가 생기면 기수 정렬을 연다.

남은 측정:

| 무엇 | 판정 |
|---|---|
| `vfxsort` 시나리오 다섯 백엔드 골든 | 두 뷰 모두 먼 입자가 먼저, 정렬 끈 그림과 5 % 넘게 다르다 |
| `RT.VFX.sortParticles`(방출기 32 × 256) | 0.2 ms 아래(넘으면 16 비트 기수 정렬) |
| `GPU.VFXSort` | 0.1 ms 아래 |
| `None` 투명 패스 GPU 시간 앞뒤 | 흔들림 안(밖이면 간접 읽기 없는 PSO 변형) |

### DX12 · Vulkan 기록 경로 락의 남은 자리(설계 2026-11-17, NEXT RHI 5-2, 감사 R-4 · R-12)

L-1(진단 락 제거)은 됐다. 잎 락(`_onlineBlockMutex` · `_cmdListPoolMutex`)은 둔다.

**L-2(측정 문턱을 넘을 때만).**

1. 측정: dx12 · vk, `-gv_renderGraphParallelMinWidth=2`, `RT.Bind.mirrorLockWait` 합과 `RT.Bind.constantBufferWrites`. 대기 합이 `RT.Graph.parallelLevel` 합의 2 % 를 넘으면 L-2 를 한다. 아래면 값을 S8 에 적고 닫는다(R-4 구간 할당에 맡긴다).
2. 할 일: 상수 버퍼의 매핑 주소 · 슬롯 크기 · 미러 값을 버퍼 레코드(`ConstantBufferRecordState` — `_pMapped` · `_alignedSize` · `_lastWriteFrame` · `_pendingSlotMask`)로 옮겨 `_bindlessMutex` 와 미러 전역 락을 없앤다. 채울 버퍼 목록은 리스트 기록 상태에 쌓고 제출 때 한 스레드가 합친다(`noteWrite` · `mergeDirty`). 같은 프레임 두 번째 쓰기는 `_lastWriteFrame` 비교 교환으로 오류(R-12).
3. 뒤: RT 프레임 중앙값이 흔들림 밖으로 빨라지지 않으면 되돌린다. 줄무늬 락은 쓰지 않는다.
- 별개 측정: DX12 기록 경로 락 제거 전후 `bench.json` p50(291d69074e 와 그 앞) — S8 에 적고 닫는다.

### 메시 구조: 인덱스만 · 칸마다 스킨 결과 · 고른 단계만 스키닝(정함)

- 모든 메시는 인덱스를 가진다 — 색인 없는 이중 모드를 두지 않는다.
- 스킨 결과는 칸(`AnimationSkinSlot`)마다 — 메시 사본을 만들지 않는다. 프레임마다 바뀌는 번호(팔레트 순서)를 인스턴스에 싣지 않는다. 변형이 필요한데 칸이 무효인 인스턴스는 그리지 않는다(레스트 포즈로 대신하지 않는다).
- GPU 인스턴스 LOD 와 CPU 선택은 한 규칙(`MeshLODSystem::isGPUSelectable`).
- 남은 측정(인덱스 전환 뒤 골든 대조 · 캐릭터당 GPU)은 [NEXT](NEXT.md) 4 절.

## 코어 · 런타임 설계

일은 [NEXT](NEXT.md) 5 절. 측정이 문턱을 넘지 않으면 값만 남기고 닫는다. 외부 제품 동작은 고치기 전에 그 판의 문서로 대조한다.

### 구독의 제어 블록(감사 C-20 · 다음 할 일 42)

- 남은 것: `DelegateTest.SubscribeCost`(bench) 판정. ns/짝 증가가 30 ns 를 넘거나 `load/characters` · `load/housingbulk` 의 `Scene.load` 차이가 흔들림 밖이면 출처가 생성자로 스레드 정책을 고르게 한다(한 스레드 출처는 원자 · 대기 없음). 결함 수정이라 되돌리지 않는다.
- 같은 모양: `SubscriptionList::ReleaseFunction` — `SubscriptionList` 소멸자에서 닫고 기다린다(C20-2).
- 하지 않는다: 연결마다 제어 블록(구독마다 할당) · 전역 번호 표(숨은 싱글턴) · 해저드 포인터(콜드 경로에 과하다).

### 비밀번호 해시 연산 줄과 저장소 대기 상한(감사 O-3 · 다음 할 일 30)

작업자는 `security-and-hardening` 을 먼저 부른다.

- 남은 것(O3-5): `login_storm` 을 가벼운 해시 없이, 해시 처리율의 0.5 · 1 · 2 배 도착률로 돌린다(도착률 고정은 O-12 열린 루프 뒤, 그 전에는 닫힌 루프로 대기 최고치만). 문턱: 2 배에서 다른 키트 p99 가 한가할 때의 1.5 배 이내, 최고 메모리 기준 + `_maxConcurrent × 38 MiB` 이내, 거절은 모두 `kBusy`. 고치기 전 값도 같은 표에.
- O3-2: 저장소 람다 안의 `ILoginCrypto` 밖 암호 호출(서명 · 압축)을 찾아 보고한다.
- 하지 않는다: 해시 전용 스레드 풀(`TaskManager` 와 코어 다툼) · 저장소 워커만 늘리기(SQLite 쓰기 하나 계약).

### 팩 규모(감사 A-9 ~ A-12 · 다음 할 일 18)

- 남은 것: `PackScaleTest` 값으로 블록 크기 확정(64 · 128 · 256 KiB, 크기 차이 2 % 안이면 128 KiB). `lookupNs` 가 해시 맵의 1.5 배를 넘으면 해시 앞 8 비트 버킷 표를 더한다.
- 같은 모양: `DerivedDataCache::store` 의 출력 통째 · `PackPatchBuilder` 의 앞 빌드 팩 읽기 — 통째 읽는 자리를 센다.
- 하지 않는다: xxh3(사용자 판단 2026-11-17 — 느리면 하드웨어 CRC32C) · 완전 해시 색인 · CR 정규화를 확장자별로.

### 출하 할당기와 컨테이너 복잡도(감사 C-6 · C-9 · 다음 할 일 20)

- 남은 것(C-6): ① `MemoryProfiler` 프레임당 할당 수 × `MemoryBenchTest.AllocateFreeCost` ns 가 프레임의 3 % 이상이거나 8 스레드 ns 가 1 스레드의 3 배 이상(워커 할당 프레임당 1000 번 이상)이면 ② `SW_MEMORY_BACKEND=mimalloc` 비교. 프레임 p50 2 % 또는 씬 로드 5 % 이상 빨라지고 최고 메모리 증가 10 % 이하면 Shipping 기본으로.
- AC-1: `list.h` · `set.h` · `unordered_set.h` 범위 연산과 `string` 범위 `append` 의 본문 복잡도를 래퍼마다 확인한다.
- 하지 않는다: 자체 크기 등급 할당기 · jemalloc · tcmalloc · 전역 `operator new` 교체 · 타입별 풀 먼저.

### 병렬 오브젝트 남은 몫(NEXT 5-3)

완전 ECS 는 하지 않는다. 이득이 흔들림 안이면 "켤 코드" 커밋을 되돌린다.

- PO-1: 근접 카운터 `Gimmick.ProximityCandidates`(노드 수, 후보 수)를 `GimmickCircuitComponent` 틱에 두고 게임 12 개 `slice` 최고치를 본다. 서비스 구간 `GT.Scene.service.<이름>` 으로 `load/characters` · `load/housingbulk` · `load/townsfolk` · Shooter3D `benchmark` 3 번씩.
- PO-2(PO-1 값 뒤): 한 단계 직렬 서비스 합 0.3 ms 이상이고 서로 겹치지 않는 둘 이상이 각 0.1 ms 이상일 때만 서명을 나눈다 — `ISceneMainService::update` / `ISceneParallelService::updateOnAnyThread( SceneJobContext& )`(매니저는 const, 쓰기는 `deferPostTick`), 게임 스레드 전용 API 는 `const GameThreadKey&` 인자, 파도는 `FrameSystemParallelPlan`. 문턱 미달이면 서명도 나누지 않는다. 옮긴 뒤 `GT.Scene.tick.FlushEnd` 감소 + `TestSceneStateHash` 동일.
- 이미 안에서 나눠 도는 서비스(`MeshLODSystem` · `TextureStreamingSystem` · `SceneAudio` · `SceneVFXProvider` · `NetObjectTable` · `SceneNetLink`)는 파도에 넣지 않는다.
- PO-3 캐릭터 일괄 이동은 유지(4 워커 2.12 배). 다음: 4 워커에서 남는 약 5 ms 직렬 몫(요청 모으기 · 결과 반영 · Jolt 공용 상태)을 구간으로 나눠 잰다.
- 근접 기믹 · 감지 · 넷 관심 격자는 `SceneSpatialGrid` 로 옮기지 않는다. 다시 볼 조건: (근접 노드 × 태그 후보)가 프레임당 1 만 초과 → 노드에 필수 `range` 칸 + 태그 층 격자. 자극 64 개 넘게 모으는 키트 → 그 키트가 `PhysicsCharacter3D` 층 질의.
- 같은 모양: `ISceneService` 렌더 기여 훅(`contributeLights` 등)의 게임 스레드 계약이 주석뿐 — 병렬로 돌릴 때 같은 열쇠 인자를 쓴다.

### 리플렉션 남은 몫(NEXT 5-4)

- W-R3 (a): 통합 빌드 `.gen.cpp` 합계(목표 4 MB 미만)와 `build_time_report.py` 생성 비중(목표 8 % 미만)을 잰다. 못 미치면 `ReflectionParser.EmitBytes` 에서 합계 25 % 넘는 방출 함수만 표로 바꾼다(`emitMethodInvoker` 는 못 바꾼다).
- `InspectorLayoutCache` 키를 `PropertyHandle` 로 바꾸지 않는다. 다시 볼 조건: `findDisplay` 조회가 그리드 그리기의 5 % 초과 → 표시 정보를 묶음 행 곁 배열로.
- W-R4 뒤: 손으로 쓴 `serializeState` 본문(약 158 곳)을 키트별로 `StateIO::reflected` 로 옮긴다. 손 본문은 리플렉션 밖 값만 더한다. 소비자별 커밋 앞뒤 비용 차이가 3 % 를 넘으면 그 커밋만 되돌린다. 롤백 프레임 경로가 `reflected` 를 쓰게 되면 타입별 칸 계획 캐시를 둔다.
- W-R6 밀집 타입 색인(RF-7): `TickRegistry::_mapTypeDispatch` · `GameObjectStore::_mapComponentPool` · `ComponentDefaults::_mapResolved` 조회 합이 게임 스레드 프레임의 1 %(또는 0.1 ms) 이상일 때만 `TypeInfo::_typeIndex` + `TypeIndexedArray<T>` 를 둔다.
- N1: 칸별 변경 비트(`writeDelta`)는 대역폭 측정 뒤. 편집 범위와 와이어 범위가 달라야 하는 칸이 나오면 그때 `NetMin` · `NetMax`. `Quantize` 키 · 양자화 형식은 두지 않는다.
- RF-4: `ObjectStateSerializer` 상태 쓰기 · 읽기 짝과 `ComponentDefaults` 기본값 비교를 `ReflectVisit` 로 옮긴다.

### 리플렉션 내장 형식 한 곳 정본

- 내장 형식은 `ReflectBuiltins.xxx` 줄(`Widget` · `Layout` · `Roles`) 한 곳에 적고 소비처는 그 칸에서 만든다. 손 처리기는 Layout 이 none 인 State 형식만(`ReflectBuiltinTraitTable::hasStateHandHandler`).
- 세이브에서 핸들을 빼지 않는다(대상의 고정 지역 id 로 적힌다).
- 집합 · 맵은 RuntimeState 로 쓰지 않는다.

### 트리거 Enter · Stay · Exit 계약

- 규칙: 겹침은 오브젝트 쌍(`PhysicsOverlapTracker`), 막는 접촉은 바디 쌍. 한 프레임에 끝 먼저 → 시작. 셀 · 하위 레벨 오름은 "안에서 생성" 진입, 내림은 파괴 규칙.
- 활성자 볼륨 파생은 `TriggerAuthority`(Server · Local)를, 지역 모듈 처리기는 `getAuthority` 를 반드시 고른다.
- 남은 위험: 원점 이동 뒤 AI 인지의 마지막 본 자리가 옛 원점 기준이다.
- shortcut: Activator 컷씬 재생 중 다른 플레이어 진입은 틀지 않는다 — 동시 개인 컷씬이 필요하면 플레이어마다 디렉터를 세운다. 나타남 판정 창(8 프레임)이 모자라는 구성이 나오면 물리가 "막 더한 바디" 를 표시한다.

### 런타임 UI 공통 장치

- 화면 전환 애니메이션은 화면마다 문서 애니메이션(`Open` · `Close`)으로 둔다. 스택 공통 전환은 두지 않는다.

### 상점 흐름

- 남은 것: 인벤토리 명령의 네트워크 전송은 인벤토리 복제 키트가 생길 때(요청 모양 · 검사는 `InventoryCommand`). 결제 상품(`<Product>`)은 `StorePurchaseFlow` 길이라 상점 화면에 싣지 않는다.

## 도구 · 에디터 · 콘텐츠 파이프라인 설계(2026-11-17)

새 린트 규칙은 두지 않는다 — 형식 · API · 로드 오류로 막는다.

### 머티리얼 그래프 컴파일러를 DevTools 로(설계 2026-11-17, NEXT 5 폴더 자리)

- 남은 몫: DevTools 가 처음 `SW_DATA_INHERITANCE_SCHEMA` 를 쓰는 커밋에서 `DataInheritance.h` 의 `SW_REGISTRAR_MODULE == "Engine"` 단언을 `DataInheritance::isCookVisibleModule( string_view )`(Engine · DevTools 만 참)로 바꾼다. 그 전에는 하지 않는다(쓰는 곳 없음).

### Fileserve(설계 2026-11-20)

- 남은 몫: 실기기 측정. Android-Release 에서 팩 하나를 다시 구운 뒤 `adb reverse` + `-fileserve` 기동 시간 대 `android-apk` 다시 묶기 + 설치. 다시 받는 몫이 APK 길의 1/2 아래가 아니면 구간 크기 · 동시 받기를 본다.
- GameFramework 의 HTTP 파서를 Engine 으로 내리지 않는다(크기 L, 이 기능만으로는 이득 없음).

### HDR 디스플레이 출력(설계 2026-11-20)

- 남은 몫: HDR 퍼뮤테이션이 SDR 프레임 시간을 바꾸지 않는지 `bench.json` p50 으로 한 번 본다.
- Vulkan 은 HDR 인 동안 다른 창을 새로 열지 않는다(코드의 shortcut 주석) — 에디터 도구 창을 HDR 에서 열어야 할 때 고친다.

### 헤더 팬인 다이어트(설계 2026-11-20, 감사 8)

- 규칙: 커밋마다 `py -3 -m Scripts header-fanin --json` 전후를 견주고 점수 합이 줄지 않는 변경은 하지 않는다. 다이어트한 헤더는 `Config/Rules/Lint/HeaderIncludeLimits.json` 에 상한을 더한다.
- 남은 차례: ③ `TypeRegistry.h` — `TaskTypes.h` · `EngineMinimal.h` · `ReflectionCast.h` 를 cpp 로. ④ `ReflectionTypes.h` → `TypeInfo.h` 묶음은 리플렉션 생성기 템플릿(`.gen.cpp`)과 함께.
- 판정: 단계마다 점수 합 5 % 이상 · 그 헤더 점수 30 % 이상 줄지 않으면 되돌린다. 단계마다 Release · Shipping · ASan · 서버 증분 빌드.

### 렌더 그래프 접근 핸들 ② ~ ④ — 데이터 · 실행기 · 패스 몸통(설계 2026-11-20)

②(프레임 패스)는 닫았다. 남은 것:
- **③ 뷰 패스.** 뷰 그래프 실행마다 뷰 표(칸 = 뷰)에 뷰 소유 자원을 등록하고 `importResource`, 레벨 콜백이 그 그래프의 표를 본다. 차례: 자동 노출(측정을 뷰 패스 `AutoExposure` 로 나누고 톤맵이 입력으로 읽는다 — 원본 `prepareTextureForShaderRead` 함께 삭제) → `HZBOcclusion::recordInViewPass` 의 두 짓기 → `FramePassExecutorPasses` · `Draw`(디퍼드 타일 분류 · 컴퓨트 후처리 · 그림자 층 · 아틀라스).
- 깊이 히스토리 복사의 텍스처 전이 3 곳은 텍스처 UAV 입력 목록 `_listUnorderedAccess` 를 더할 때 옮긴다.
- **④ 형식으로 막기.** 패스 몸통이 받는 명령 리스트를 전이 함수 없는 면(`RenderPassCommandList`)으로 바꾸고 `transitionBuffer` · `prepareTexture*` 는 그래프 실행기 · 프레임 끝만 쓰는 `IRHICommandList` 에 남긴다.
- 판정: 단계마다 네 백엔드 골든 그대로, DX12 · Vulkan 검증 층 경고 없음, `RT.Graph.transitions` 와 프레임 GPU 시간이 늘지 않음.

### 자원 구간 할당(설계 2026-11-20, 감사 R-4)

- 남은 몫 ④: Vulkan VMA · DX12 D3D12MA(MIT)로 백엔드 메모리를 덩이에서 나눈다 — 새 의존성이라 **사용자 결정**(NEXT "사용자에게 받을 것"). 확인 전에는 상수 버퍼 페이지(①~③, 끝남)만으로 간다.
- 측정: 머티리얼 인스턴스 5000 씬(`load/materialinstances` — 없으면 만든다)에서 할당 수(`RT.Vulkan.allocationCount` · DX12 커밋 자원) · 프레임 시간 p50 전후. 할당 수가 1/10 아래로 줄고 프레임 시간이 흔들림 안이면 둔다.

### 렌더링 런타임 디버그(설계 2026-11-20)

- 남은 몫: 그래프 밖 버퍼를 읽는 컴퓨트 PSO 의 리플렉션(`ShaderBindingSlot::_listElementMember`)을 버퍼 이름에 잇는 표 — 지금 버퍼 칸 해석은 사람이 고르는 4 바이트 칸이다.
- 새 보기 모드(가림 · 스트리밍)는 만들지 않는다 — 창 · HUD 에 같은 정보가 있다.
- 측정: 꺼진 상태 비용(드로우마다 원자 값 하나)이 `bench.json` p50 흔들림 안인지 통합에서 한 번.

### Android IME 조합(설계 2026-11-20, 감사 P-4)

- 남은 몫: 실기기 확인 — 한국어 키보드로 UI 글 칸에 "한글" 입력, 조합 중 밑줄.
- GameActivity 로 바꾸지 않는다(액티비티 수명 · 글루 전체가 바뀐다 — 크기 L).

### 시퀀서 재생 · 컷씬 · 카메라(설계 2026-11-20)

- 하지 않은 것(요구가 오면): CineCamera Rig Rail · Crane, 표식 프레임(`JumpToMarkedFrame` — 에셋 표식 표가 먼저), 반복 횟수(`LoopCount`), 가중 접선, 자막 · 소리의 구간 섞임(창구에 세기 칸이 먼저).
- 넘치는 이징(Back · Elastic · Bounce)은 들이지 않는다 — 블렌드 가중치 계약(단조 · 0→0 · 1→1)을 깬다.

### 원본 저장소 떼기와 `_source`(설계 2026-11-17, NEXT 5 에셋 GUID 3 단계)

- S1(**사용자 결정**): 원본 확장자 전체를 LFS 로(`.gitattributes` 를 `Content/**/*_raw/**` 종류 줄로) — 호스팅 용량 · 비용. 이력은 다시 쓰지 않는다.
- S2 · S3 은 원본 합계 2 GB 또는 LFS 비용이 문제가 될 때 한다: `_source` 는 상대 경로 또는 `store:<SHA-256>/<파일 이름>` 두 꼴만, 읽는 창구는 `AssetSourceLocator::resolve` 하나, 못 가져오면 임포트 오류.
- 서브모듈 · 서술에 절대 경로 · URL 은 쓰지 않는다.

### 에디터 첫 패널 · Hierarchy 8000 측정 계획(설계 2026-11-17, NEXT 4)

- 재서 문턱을 넘는 패널만 고친다. 고치는 꼴: 동기 준비를 `onPanelOpened` 가 띄우는 잡으로, Model 이 `isReady()`, View 는 준비 전 한 줄. 시간 문턱은 시나리오 판정에 넣지 않는다. 재는 법 · 문턱은 [NEXT](NEXT.md) 4 절.

### 샤드 시간 균형(설계 2026-11-17, NEXT 5 T3)

- 가장 긴 체인이 전체 벽시계의 40 % 를 넘을 때만 체인을 `SHARDS n` 으로 나눈다 — 시간표 `Config/Dev/ScenarioCosts.json`(`test_history.py costs --write`, 사람이 커밋할 때만 바뀐다).
- 단위 테스트 샤드는 합계가 60 초를 넘을 때만.
- 여러 머신 분산은 외부 러너가 필요하다(**사용자 결정**).
- 판정: 나눈 뒤 Release ctest 벽시계가 10 % 이상 줄지 않으면 되돌린다.

### 리다이렉트 표의 팩 담기(설계 2026-11-17, NEXT 5 R1)

- 남은 몫 ②: `shipping-check` 가 팩 목록에서 도메인마다 `redirects.xml` 이 담겼는지 확인하는 한 줄.

### Mirror · Montage 포즈 노드(설계 2026-11-17, NEXT 5 A1)

- "지금 이 섹션으로" 재생(언리얼 슬롯 노드)이 필요해지면(첫 후보: Shooter3D 소총 자세) `AnimationPoseNodeRegistry` 에 슬롯 노드를 한 줄로 더한다. 그 전에는 하지 않는다.

### 퍼징 구성과 퍼저 타깃(설계 2026-11-17, NEXT 4 퍼징)

- 남은 타깃(신뢰 경계 파서만): 게임 세션 수신 패킷 디코드, `JSONDocument`, 세이브 머리. 크래시 입력은 `Tests/Snapshots/` + 단위 테스트 케이스.
- 밤샘 실행은 러너가 필요하다(**사용자 결정**).
- 측정: 타깃별 exec/s 100 미만이면 입력마다 범위를 다시 세우는 것 — 고친다. 30 초 뒤 `cov` 가 씨앗 재생 직후보다 늘지 않으면 타깃이 죽은 것.

### 검증 시간: 그리지 않는 대기와 진 단계 다시 돌기(설계 2026-11-17, NEXT 4 검증 시간)

- 남은 몫: 짧은 고정 대기(바로 뒤에 `<Expect watch>`)를 조건 대기로 바꿀 목록을 이분 탐색 스크립트가 내게 한다(`--advances` — `<AdvanceSimulation>` 을 센다). 목적은 흔들림 줄이기.
- 시나리오 전체 `gv_timeScale` · 사진 수 줄이기 · 백엔드 함께 돌리기는 하지 않는다.

### 플레이 중 리로드 확인 장치(설계 2026-11-20)

- 규칙: 새 에셋 캐시의 `reload` 는 `ReloadResult` 를 돌려준다(`void` 는 등록이 컴파일되지 않는다). 실패면 옛 판을 둔다.
- 남은 틈: 메시는 임포트 산출물이라 추적 사본이 없어 시나리오에 없다.

### 에디터 마우스 상호작용 공통 규칙(설계 2026-11-20)

규칙 R10 ~ R13 은 [EditorUI](agents/EditorUI.md). 남은 몫: 시퀀서 · 카메라 프리셋 · 충돌 프리셋 패널이 공통 층(R10 ~ R13)을 붙인다.

## 시나리오 · 테스트 프레임워크 개편 판단(2026-11-17)

- 틀(`SW_TEST_CASE` · 단언 매크로 · 시나리오 XML 형식 · 단계 등록)은 바꾸지 않는다. 별도 테스트 프레임워크를 들이지 않는다.
- 선언 밖 Warning 도 실패다(그대로 둔다).
- 골든 · `<Record>` 는 혼자 돈 실행에서만 기록하고 체인으로 검증한다. 기준 파일이 없으면 `MissingBaseline` — `changed` 단계만 없는 것을 쓴다.
- 단계마다 프로세스 · 체인 순서 섞기 · 정지 시한에 걸린 자식 다시 띄우기는 하지 않는다.
- 시나리오 산출물 `{out}` · 체인 보고는 실행 뿌리로 옮기지 않는다(도구가 시나리오 이름으로 찾는다).
- 남는 틈: `changed` 단계는 바뀐 폴더의 테스트만 골라, 다른 폴더 테스트가 밟는 새 Warning 은 전체 실행에서 처음 나온다. `EngineConfig` 에 남은 다른 개발 전용 칸은 같은 모양이다 — 보이면 `Config/Dev` 로 옮긴다.

## 외부 엔진 · 라이브러리 검토(2026-11-19)

- 의존(taskflow · magic_enum · FidelityFX SDK)은 들이지 않는다 — C++20 요구 · 이름 출처가 둘 · 템플릿 비용. 코드 이식 없이 모양만 옮긴다.
- 엔진 안 MCP 서버는 들이지 않는다(사용자 결정 2026-11-19).
- 남은 화질: 볼류메트릭 구름 · 비 가림 맵(Adria). 스카이 라이트 GPU 찍기(구름 · 해 원반)는 하늘이 프레임마다 바뀌는 장면이 생길 때(M).
- 렌더 그래프 접근 핸들 남은 몫은 "렌더 그래프 접근 핸들 ② ~ ④" 절이 정본이다.
- 작업 시스템(측정 먼저 — 계기 `Wait runParallel` · `Task.Join.*` · `Task.Idle.parks`): 묶음 제출 `submitBatch`, `runParallel` 우선순위 레인, guided 청크, 후속 하나 이어 실행, 단계 그래프 위상 순서 재사용.
- 측정 뒤에만: DX12 PipelineLibrary(차가운 시작 PSO 합), 깊이 전용 배치 키(그림자 드로우 수), 서버 MMO 관찰자 거리 기반 틱 주기 · 컴포넌트 TickInterval 버킷, 서버 쿠킹의 축약 애니메이션 클립, 온라인 프로토콜 퍼저 타깃 ⑦(퍼징 1 단계 뒤).
- 남은 손 포인터 병렬 본문(셰이더 쿠킹 사슬 · GPUSceneBuilder 트랜스폼 복사 · 갱신 · PathModifierUtil 정합)은 형식으로 담을 수 없는 모양이라 둔다.
- 손 본문 넷 메시지 13 종은 고정 배치로 옮기지 않는다 — 값마다 인코딩 · 예산 개수라 대역이 는다.
- 카탈로그 키 남은 한계: 쿠커가 상속을 따라가지 않는다(키 칸을 든 형식을 물려받으면 표 대조 테스트가 멈춘다), 같은 파일 안 정의끼리의 참조(`_dropID` · `_replacedID`)는 `<Reference>` 줄로 옮길 몫.

## 엔진 전수 조사 판단(2026-11-20)

차례: 결함에 가까운 것 → 구조 장치(형식으로 막기) → 측정이 문턱을 넘는 성능. 사용자 결정: 편의 매크로(`BLOCK` · `SW_DELEGATE_*` · `SW_DECLARE_DELEGATE`)는 남긴다 · 컨테이너 std 상속은 불필요하면 자체 구현 · 부르는 곳 없는 공개 API 는 지우지 않고 적용.

**1. 결함에 가까운 것(측정 없이).**
- 머티리얼: `_base` 부모를 고쳐도 자식이 옛 값을 쥐는지 확인한다. 정적 변경은 부모가 선언한 런타임 스위치 색인으로만, 값 API 는 정적 종류를 거절한다.

**2. 구조 장치.**
- 등록: Action · Condition 레지스트리 → `NamedEntryRegistry<T>`.
- 리플렉션: `tryParseEnum<E>` · 배포에도 남는 열거자 데이터 철자(`ENUMERATOR(Text=)`) · 반사 동등 비교 · 해시 · 기본값 · 기반 형식으로 하위 형식 생성 · `FUNCTION(Exec)` 콘솔. `.cpp` 안 `REFLECT` 는 Qt moc 꼴(`#include "Foo.gen.inl"`)로, 익명 이름공간 FQN 충돌 키를 먼저 정한다.
- 매크로: `SW_CONCAT` 사본 정리, 에셋 캐시 6 · 엔진 서비스 6 매크로를 하나씩으로. `SourceLocation`(`__builtin_FILE/LINE/FUNCTION`) — Error · Warning 로그 · `SW_FATAL` 은 함수로(`%#` 컴파일 검사 길 먼저), 나머지는 매크로 몸만 얇게. `-fmacro-prefix-map` 추가.
- 실행 인자 남은 틈: `HeadlessSubsystem::rejectClientOnlyTask` 표를 `CommandLineArgumentUsage` 의 `Client` 칸으로.
- Dev → DevTools: 엔진 개발 명령은 DevTools 로, 쿠킹 코드(`runCookTask` · `PackCooker` · `SceneCooker` · `ShaderCookDriver`)는 `DevTools/Cook` 으로(그때 `-cook-all` 인자 줄은 `Dev`).
- 파일: 자기 `.cpp` 만 포함하는 60 줄 미만 헤더 65 개 접기, 한 곳만 쓰는 도우미 헤더 36 개 합치기(`header-fanin` 합이 늘면 안 함). 헤더끼리 합치기 · 테스트 위치 옮기기는 하지 않는다.
- 렌더러 규격(접근 핸들 · R-4 뒤): 그래프 일시 버퍼(`graph.createBuffer`) → 뷰 히스토리, `RHITextureSlot`(텍스처 손 생성 · 뷰 등록 8 곳), 컴퓨트 패스 도우미(그룹 크기는 `[numthreads]` 리플렉션), 풀 버퍼(펜스 재사용), 되읽기 단일 면. RHI 자원 생성은 패스키로 막는다.
- 쿠킹: 셰이더 컴파일 증분 판정 세 벌(`.cookrec` · `cook.stamp`+`.deps` · 메모리 지문)을 `CookJob` 하나로. `DerivedDataCache` 생성자 비공개 + `open(bucket)`.
- 파일 감시: OS 감시자 생성은 `FileWatchService` 만(열쇠 형식) — 루트마다 하나 공유, 프레임 안 경로별 합치기, 안정화 창. 다시 읽기 알림 · 텍스처 다시 읽기는 프레임 단위 일괄.
- 머티리얼: 파라미터 핸들 `MaterialParameterHandle{index, offset, kind, layoutGen}` → 바뀐 칸만 덧칠 · 범위 업로드. 전역 값 · `MaterialParameterCollection` 을 전역 CB(`MaterialGlobals`)로. 인스턴스 모델을 MIC=`_base` · MID=`create` 둘로. `_permutations` 이름 단위 합치기. `MaterialPropertyKind` 를 리플렉션 내장 형식 특성에 잇기.
- 컨테이너: `list` 는 LRU 전용 형식으로 바꾸고 지운다, `string` 자체 구현. 레이스 가드 별도 깃발 떼기는 측정 뒤(뗄 때 그 깃발 체인 하나를 preflight 에).
- 디버그 허브: `DebugHUDRegistry` 를 범주 단위로 넓혀 그리기 · 감시 값 · `VisualLogger`(생산자 0) · 에디터 패널을 함께. `gv_*DebugDraw` 를 범주 켜짐으로. 먼저: 내비 · AI, 물리, 렌더링, 네트워크, UI · 입력. 진단 getter 118 개는 감시 값 · 로그 참조를 다시 센 뒤 살림 · 지움으로 가른다.

**3. 에디터 · 키트(영향 순).** VFX 편집기, 폴리지 칠하기, 스플라인 점 손잡이, 물리 에셋 시각 편집, 패키징 플랫폼 선택, 로컬라이제이션 패널. 키트: "꽂으면 도는" 컴포넌트 층(데모 래퍼를 키트 `Component/` 로), 카탈로그 편집 · 미리보기 일반화 + Map Check 키트 검증 등록점, 새 게임 · 장르 키트 생성기. 서버만 있는 키트(Social · Trade · LiveOps · ServerDirectory · NetTurnRelay)는 지우지 않고 붙인다. 검토: GF `Gimmick/Genre/*` 를 키트로, 호러 4 종 공통(은신 · 소음 · 조명) 합치기.

**4. 성능(측정 뒤).** 동기 대기(히치) 먼저: 프레임 단계 안 동기 로드 경고 · 렌더 스레드 디스크 IO 단언을 구조로 넣고, VAT 굽기 · Vulkan 파이프라인 캐시 저장 · 파괴 셰이프 · 프레임 경로 동기 에셋 로드 5 곳을 비동기로. 핫 경로 API 는 `string_view` · `hashed_string` 만. 리플렉션 로드 경로 칸 형식 해석을 `PropertyInfo` 캐시로. 생성 코드 크기는 측정 범위부터 맞춘다(NEXT 1.65 MB 대 빌드 트리 9.33 MB).

### 스트리밍 셀 경계 정합 · 저장(설계 2026-11-20)

남은 몫(사례가 나오면):
- 바닥 광선이 비탈 · 가장자리의 큰 바디를 잘못 쥐면 셰이프 스윕으로.
- 셀 안에 PROPERTY 밖 상태를 가진 컴포넌트(`ComponentStateStore` — 지형 편집)는 아직 세이브를 넘지 못한다.
- 늘 있어야 하는 서버 스폰은 `_bAlwaysLoaded` 를 켜거나 복제 번호를 매긴다(규칙).
- 원점 이동: 내비 · 군중이 원점 기준 질의를 받기 전까지 길찾기 월드는 원점을 옮기지 못한다. 월드 공간 파티클 · 디버그 그리기 · `AIPerceivedTarget::_lastKnownPosition` · MMO 절대 자리를 놓는 게임 코드는 원점을 손으로 뺀다. 하위 레벨 문서 읽기는 게임 스레드 동기다.

### 멀티플레이 스트리밍 소스 · 관련성(설계 2026-11-20)

- 클라이언트 → 서버 셀 상태 보고는 두지 않는다 — 믿을 수 없는 입력의 공격면만 는다.
- 두 프로세스 짝 시나리오는 스트리밍 씬을 쓰는 게임이 생길 때(그때까지 `NetMMOStreamingTest`).

### 씬 스트리밍 (라) HLOD · 우선순위 공유 · 로드 큐 디버그(설계 2026-11-20)

- 그리기 수가 문제가 되면 `MeshMerge` 대리를 같은 `_hlodFileID` 자리에 더한다.
- 인스턴스 메시에 LOD 고정 칸이 생기면 대리가 최소 LOD 를 쓴다.
- 로드 큐는 에디터 패널을 두지 않는다 — 개발 명령 · HUD 자리.

### 게임 기능 · 미니게임(옛 제목 "허브 게임 · 미니게임 Experience", 사용자 결정 2026-11-20 · 재편 2026-11-21)

**원칙(사용자).** `Source/Games/*` 의 시험용 게임은 폐기하고 Playground 하나로 합친다. 엔진 · GameFramework 기능은 빠짐없이 Playground 장소나 미니게임 어딘가에서 쓰이고 시나리오로 확인된다. 폐기할 게임에서만 쓰이던 기능은 그 게임을 지우기 전에 옮긴다. 키트는 모두 링크하고 게임 기능이 등록 · 서비스 바인딩만 켜고 끈다(런타임 모듈 로드 · 언로드는 기각).

**없는 기능(만들 것).** 텔레포트 장치 · 포털, 풀베기(FoliageComponent 인스턴스 제거 API), 나무 베기(Fracture · RigidBody · LootTable), 피 데칼 · 지속 출혈, 번개 · 비 · 눈 파티클 · 쌓이는 눈 · 웅덩이 고임 · 마름 · 밟으면 튐, 실내 차폐(지붕 광선 · AudioRoomGraph), 날씨 · 낮밤 → 오디오 · 조명, 바람 → 천, 3D 등반 · 맨틀 · 수영 · 로프 · 사다리, 상자 열기 · 줍기(LootTable 쓰는 곳 0), 이모트 · 미니맵 · 낚시.

**시험이 없는 기능.** 단위 · 시나리오 둘 다 없음 — SmartObject · 소프트바디 · HDR 출력. 시나리오 없음 — 3D 파괴 · 표정 · 립싱크 · AI 인지 · 스폰 디렉터 · 락온 · 턴 순서 · 인벤토리 · 제작 · 상점 · 날씨 전환 · 데칼 비 씻김 · 카메라 흔들림 · 사진 모드 · 스플라인 강. 단위 없음 — GrabberComponent · 튜토리얼 힌트 · 안개 · 무기 교체.

**남은 몫.**
- 시연 컴포넌트 19 개가 카탈로그를 경로 칸으로 직접 읽는다(`FightingDemoComponent.cpp:91` 등) → `GameFeatureScope::loadCatalog<T>()` 로 받게 옮기면 그 키트가 다시 등록하고 미니게임 `catalogs` 검사 안에 든다.
- 미니게임 결과(`MinigameResult`) → 경제 · 진행 매핑(복원 뒤에 얹는다).
- 허브가 쓰는 키트(스트리밍 오픈월드 · 하우징 · 농사 · 상점 …)를 허브 맵에 배치(흡수 단계마다), 전용 서버 허브(NetMMO), 매치형 미니게임은 매칭이 미니게임 이름으로 세션을 가르고 결과를 허브 서버에 보고.
- 남길 것: 시험용 작은 둘째 팩 하나(NileCity — `SW_GAMES` · `-game=` · `_cookRules` · `_bAutomationOnly` 를 지킨다).

**흡수 차례(한 단계 = 한 갈래, 통합 때 Release · Shipping · ASan 증분 빌드 + 아래 시나리오 먼저).** 단계마다 사용처를 옮기고 시나리오 · 골든이 통과한 뒤 폴더를 지운다. 골든은 이름 재기록만, CTest 체인은 미니게임별로 나눠 시한 · 병렬성 유지.

| 단계 · 갈래 | 내용 | 통합 때 돌릴 것 |
| --- | --- | --- |
| 5 `w-absorb-wildwest` | 허브 맵 바탕(OpenWorldWestern · 말 · 날씨 · 도착 컷씬), 골든 14 | 옮긴 9 + 날씨 · 자동 저장 |
| 6 `w-absorb-citybuilder` | FrontierTown → 도시 건설 미니게임(CityBuilder · RTS), NileCity 는 둘째 팩으로 남김 | 옮긴 3 + `Scenario.nilecity` |
| 7 `w-absorb-harvestvalley` | 허브 농장(Farming · 낮밤 · NPC 일과 · 튜토리얼 힌트), `_listVillagerPreset` 를 Playground 로 | 옮긴 7 + `.editor` 1 |
| 8 `w-absorb-meadowvillage` | 허브 하우징 · 생물(NetSimulation · 넷 하우징), 골든 8 | 옮긴 5 + `.editor` 1 |
| 9 `w-absorb-themepark` | 테마파크(하위 레벨), 골든 25 | 옮긴 3 + `.editor` 2 |
| 10 `w-absorb-voxelcraft` | 광산(Voxel, 씬 전환) | 옮긴 3 |
| 11 `w-absorb-abilityarena` | 아레나(어빌리티 디버거 · 씬 저장 편집기 시나리오) | 옮긴 3 + `.editor` 2 |
| 12 `w-absorb-velmora` | 액션 어드벤처 + DLC 세이브 승계 → 허브 세이브 · ModSupport(`Test/Mods/velmora_tidecaves` 의 `-game=`) | 옮긴 1 + sequence 2 쌍 |
| 13 `w-absorb-starskirmish` | RTS 매치(`net: Lockstep`), `lockstep-pair` 의 `_GAME`, 팩 리다이렉트 표 | 옮긴 4 + load 1 + `lockstep-pair` |
| 14 `w-absorb-lanternvale` | 허브 서버(NetMMO · 계정 저장), `net-pair mmo` 의 `_MMO_GAME` · 로그 정규식 | 옮긴 2 + `net-pair mmo` |
| 15 `w-absorb-shooter3d` | 사격장 · 온라인 매치, `Config/Server/Online/Shooter3D*` → `Playground*`(비밀 환경 변수 `SW_SHOOTER3D_*` 도), `online-local` · `telemetry-local` 기본값, 프리셋 `SW_GAMES=Shooter3D` 3 곳 → `NileCity`, `sequential_scenario_packs`, 골든 510 | 옮긴 32 + showcase 8 + desktop 4 + sequence 1 + editor 2, 서버 `Server --test`, `online-local run` |

게임 이름이 박힌 공용 자리(단계마다 같이 고친다): `Scripts/dev/linux_scenarios.py` `ALL_CHAINS`, `Config/Dev/AssetReferenceFields.generated.json` 의 `owner: game:<게임>`(다시 생성), `Config/Rules/Lint/*` 의 `Source/Games/<게임>/` 경로, `Test/README.md`, `Test/Scenario/RunStateDirPair.cmake`(Velmora · Shooter3D 쌍). 팩 흡수는 `move-asset game/<옛> game/<새>/<하위>` · `move-source` · `rename-symbol`(모두 `--dry-run` 먼저), 옛 C++ 이름은 `_bannedNames`.

## 자체 컨테이너 최적화 검토(2026-11-20)

재는 법: Release `App --test --test-run-disabled --test-filter=ContainerBenchTest.*` 5 번 중앙값, 바꾸기 전 커밋과 같은 케이스로 견준다. 범위 검사가 값에 들어가니 배포 수치는 Shipping 으로 한 번 더.

| 자리 | 개선안 | 벤치 | 문턱 |
| --- | --- | --- | --- |
| `vector` 키우기(비트 복사 아닌 형식) | 형식 표식(`is_trivially_relocatable`)을 둔 형식만 `memcpy` — 표식은 형식으로만 연다 | `VectorGrowString` | 예약한 경우의 1.5 배 이상이면 연다 |
| `vector::insert( pos, count, value )` · `erase` 밀기 | 비트 복사 형식은 `Memory::move` 한 번 | `VectorShiftBitwise` | `memmove` 기준의 2 배 이상이면 바꾼다 |
| `vector` 성장 배수 | 2 → 1.5 배 | `VectorGrowString` | 메모리 최대치와 함께 잰 뒤에만 |
| `unordered_map` · `unordered_set` | 열린 주소 + 제어 바이트 그룹 탐사 | `UnorderedMapFind` | 찾기 원소당 20 ns 이상이면 설계를 연다 |
| `flat_map` · `flat_set` 저장소 | `std::vector` → `sw::vector` | `MapRangeBuild` | 다음 정리 때 — `std::vector` 반복자 가정 호출부를 먼저 센다 |
| `ChunkedArray` | 블록 크기를 정하는 자체 구현(MSVC 16 B 블록) | `DequeChurn` | 쓰는 곳이 늘 때 |

하지 않는다: `array( initializer_list )` 두 번 쓰기(측정할 값이 없다), `string` SSO 바꾸기. `RingBuffer` · `EnumArray` · `span` · `PagedArray` · `DynamicBitset` · `IndexFreeList` · `SlotHandleTable` 은 이미 정본 꼴.

## 리플렉션 적용 후보(2026-11-20)

- 중첩 형식 private · protected 감추기: 등록 변수를 `Registrar` 정적 멤버로 옮기는 생성기 변경이 필요하다 — 그때까지 public 중첩으로만.
- `ScreenshotStabilityMode`: 데이터 철자(`stability="assets"` 113 · `"none"` 33, 61 파일)를 일괄 다시 쓸지 정한 뒤 열거형 칸으로.
- `RigCopyTransformNode::Mode`: 노드 클래스를 헤더로 옮길 때 함께.
- DevTools 손 이름 표(`ReloadSource` · `MemoryDiffStatus`)의 "DevTools 는 리플렉션 코드젠을 하지 않는다" 주석은 낡았다 — `ENUM()` 으로 옮길지는 팬인 점수(8 · 11)와 견줘 정한다. 같은 주석이 `AutomationRunnerParams.h` 에도 있다.
- 손으로 둔다: `DialogBackend`, `ShaderBindingValidatorInternal::RegisterClass` · `GlNamespace`, `TextChoiceKind`, `RichTextParser::TagKind`, `StageReportResult`, `WaterBodyComponent::MaterialNames` · `FoliageComponent::ViewMaterialNames`.

## 잠금 종류 감사(2026-11-20)

규칙: `SpinLock` 은 교착 검출기 · 쥔 잠금 수를 거치지 않으니 **안에서 다른 `sw::` 잠금을 잡지 않는 잎 잠금**만 바꾼다. 락프리 구조는 있는 것만 쓰고 새로 만들지 않는다. 고정 용량 큐는 무한 목록 자리에 바로 쓰지 않는다. 표에 없는 잠금은 유지.

재는 법: Release `App --test --test-run-disabled --test-filter=LockKindBenchTest.*` 5 번 중앙값(`TestLockKindBench.cpp`, 스레드 2 · 4 · 8).

| 벤치 | 자리 | 판정 문턱 |
| --- | --- | --- |
| `RenderPSOCacheLookup` | `RenderPSOCache` 두 잠금 | 1/1000 · 1/100 쓰기에서 4 · 8 스레드 `shared_mutex` 가 20 % 이상 빠르고 2 스레드에서 5 % 넘게 느리지 않으면 `shared_mutex`. `SpinLock` 이 둘보다 20 % 이상 빠르면 그쪽(잎일 때만) |
| `CodecRegistryLookup` | `CompressionCodecRegistry` | 같은 문턱(쓰기 없음) |
| `ShaderBindingLayoutLookup` | `ShaderBindingLayoutCache` | 같은 문턱(1/100). 1/10 에서 30 % 넘게 느리면 바꾸지 않는다 |
| `LockedSwapQueuePush` | `LockedSwapQueue` | 4 · 8 생산자에서 `SpinLock` 이 10 % 이상 빠르고 2 생산자에서 느리지 않으면 기본 잠금 인자를 `SpinLock` 으로 |
| `JoltJobQueue` | Jolt 잡 큐(적용함) | 어느 스레드 수에서든 `ConcurrentQueue` 가 10 % 넘게 느리면 `SpinLock` + `deque` 로 되돌린다 |

벤치 없이 측정 뒤 정할 후보: `AudioClip::_mutex` · `PrimitiveRegistry::_mutex`(프로파일러 잠금 대기 먼저) → `shared_mutex`, `TransientAttachmentPool::_clearedMutex` · `ConstantBufferPagePool::_mutex` · D3D12 · Vulkan `_cmdListPoolMutex` · `_liveCmdListMutex` → `SpinLock`, `_transferJobMutex`(`RHIReleaseQueue::_spinLock` 과의 순서를 교착 검출로 먼저), `RHIMemoryTracker::_arrStat`(맵과 한 잠금 — 기록 빈도를 재고). 바꾸지 않는다: `Box2DWorkerPool` 원자 색인(ABA), `TickRegistry` 더러움 목록 고정 큐(무한 목록), `OcclusionTileMailbox` 원자 `shared_ptr`(C++17 은 내부 잠금), `CameraRegistry::_pCutCamera`.

## bindless 범위 · GPU 구동 그리기 감사(2026-11-20)

규칙: 구조 버퍼는 원소 수 상수(`g_Sw<Name>Count`)와 함께 넘기고 읽기 함수 한 곳에서 범위 밖이면 0 을 돌려준다(`if` 로 막는다 — 삼항은 양쪽을 평가). 셰이더 `GetDimensions` 로 원소 수를 읽는 안은 기각(GLSL 변환 · DX12 루트 SRV 에서 뜻이 갈린다). 스트림 아웃은 RHI 에 더하지 않는다. 지오메트리 · 테셀레이션은 `RHICapabilities` 가 참인 곳만, WebGPU 대체와 함께.

남은 몫:
- stride 검사 범위: 인스턴스 · 배치 · 모프 · 머티리얼 버퍼도 `FrameResourceRegistry::registerBuffer( name, const RHIStructuredBuffer& )` 로 등록해 `validateStride` 를 받게 한다.
- 개수 버퍼 압축(측정 뒤): 컬링 뒤 compute 가 instanceCount > 0 인자만 묶음 구간 앞으로 모으고 묶음마다 개수를 써 DX12 · Vulkan · GL4.6 은 count 버퍼 간접 그리기. DX11 · WebGPU 는 그대로. 문턱: `RT.Draw.gpuBatchCount` 대비 보이는 배치가 절반 아래이고 빈 간접 명령이 GPU 프레임에 드러날 때. 머티리얼 bin 마다 간접 하나는 머티리얼 상수를 bindless 구조 버퍼로 옮긴 뒤(자원 구간 할당 (나) 다음)에만.
- 지난 월드 행렬: CPU 통째 업로드 → GPU 복사(이번 인스턴스 버퍼 → 지난 버퍼). 문턱: `RT.GPUScene.motionHistory` 가 인스턴스 수만에서 드러날 때.
- 투명 정렬: 큰 배치(`kGPUSortMaxElements` 초과)는 다단 bitonic — 큰 투명 배치가 있을 때만.
- CPU 입자 방출기는 기능 차이로 남는다 — `RT.Draw.particleCPUDraws` 와 갱신 시간으로 본다.
- 지형 · 풀 전용 경로가 생기면 compute 배치 + 간접(테셀레이션보다 compute).

## 영역별 두 검토자 토론 판단(2026-11-21)

판정 근거: 형식으로 막는가 · 성능 하락이 없는가 · 같은 사실을 한 곳에 두는가. 아래는 남은 차례만이다.

- **에디터**: ③ 체인 단계의 뷰 누수.
- **Renderer**: ② 직렬 execute 의 `_pCmdList` 누락 · 패스 순번 payload · 꺼짐 판정을 맨 앞으로 · 등록 조건 한 자리 ③ 고르기 마스크별 컴파일 계획 캐시(상한 · 계측) + Clear 를 계획에서 + 별칭 계획 값 비교 ④ 주 시점 = 뷰 목록 0 번 ⑤ 엔진 상수 칸 번호 계획 → `RT.Bind.passCbAcquires` 를 재고 FrameCB · ViewCB(C++ · HLSL 대조는 리플렉션 오프셋) — M3(칸 고갈이면 그리지 않음)은 ⑤ 전에 잰다 ⑥ (배치, 패스 종류, 뷰 모드) → PSO 표(잠금 없음) + PSO 키 값 비교 ⑦ 확장 지점 등록 때 선언 → 이름 정리 → DX11 은 컬링 없음을 기동 때 알리고 진단 스위치는 배포에서 막는다. GPUSceneBuilder 의 카메라 의존은 `GT.GPUScene.build.*` 를 잰 뒤.
- **RHI**: ③ 공개 해제 길은 `releaseHandle` 하나 + 이동 전용 소유 형식 + 강타입 핸들(bindless 번호 → 핸들, `RHIHandleKind` 가 불어나지 않게 같은 묶음) + 닫힌 리스트 토큰 + 손실 보고 하나. 웹 `waitIdle` 은 웹 시나리오로 호출 스택을 본 뒤. Vulkan 서브할당은 `RT.Vulkan.allocationCount` 가 허브 월드에서 한도 절반을 넘을 때. 병렬 구간 임시 멈춤은 넣지 않는다.

### 구조 · 성능 발전 방향(같은 날, 반론 B 제안 → 검토자 A 답 → 통합 담당 판정)

판정 근거: 평소 경로에 성능 하락이 없는 쪽, 같은 결함을 형식으로 막는 쪽, 재는 자리부터 두는 쪽. 남은 것만 적는다.

- **Core**: 명시 SIMD 형식(`Float4x`) 확대는 Engine 구간 측정이 2 % 를 넘을 때. 측정 뒤: 열린 주소법 해시 표, 기본 힙 백엔드, 큐 칸 캐시 줄 정렬(벤치는 있다). 성능 판정은 배포와 같은 자료 배치에서. 안 함: 파이버 잡 시스템, 범용 할당기 자체 구현, 이름 풀 잠금 없는 조회.
- **Engine**: ⑤ 틱 주기 등급(허브 부하에서 컴포넌트 틱이 10 % 를 넘으면 — 감시 값은 있다) ⑥ Android 코어 등급 워커(실기기 측정 뒤) ⑦ `InputUser`(로컬 멀티 미니게임이 생기면). XML 데이터 이진 쿠킹은 GameFramework 카탈로그 판정과 한 측정으로. 안 함: 물리 step 겹침, 컴포넌트 청크 풀, 모션 매칭, 롤백 넷코드.
- **GameFramework**: ⑥ 칸 바뀜 비트는 `net-pair` 측정 뒤(엔티티 상한 255 B) ⑧ 미니게임 미리 읽기. 카탈로그 이진 꼴은 Android · 웹 카탈로그 읽기 합이 50 ms 를 넘을 때. 안 함: 전면 ECS, Replication Graph 통째 도입.
- **리플렉션**: ③ 측정만 먼저 — 생성 코드 상수 표화 · 컨테이너 래퍼 얇은 템플릿(`VectorWrapper` 인스턴스 1,702) · 원격 호출 서버 측 비용 ④ 쿠킹본 고정 배치 읽기는 ① 뒤에 재고, 수단은 `BinarySerializer` 의 태그 없는 방식(스키마 해시가 같으면 선언 순서). 안 함: 이름 해시 상수화, 잠금 없는 레지스트리 스냅숏, 컴파일 타임 프로퍼티 순회, C++26 정적 리플렉션.
- **에디터 · DevTools · App**: ② 차이 저장은 기록 16 MB 또는 편집당 1 ms 초과 때 ③ 임포트 우선순위는 임포터 스레드 안전성 확인 뒤 ⑥ 패널 지연 생성 · 교체 때 상태 직렬화 측정. 맞지 않음: 패널 단위 다시 그리기, 함수 단위 핫 패치.
- **CMake**: 기준선 뒤 판정 — 고치는 파일을 유니티 묶음에서 빼기, configure 시간, 주석만 바뀐 헤더의 ABI 스탬프 재링크, split DWARF. 맞지 않음: 컴파일 캐시, C++20 모듈, 분산 빌드, 프리셋 생성기.
- **Scripts**: ⑥ 린트 규칙별 · 훅 호출 시간은 찍는다 — 훅이 예산(`Config/Dev/Verify.json` `_hookBudgetMilliseconds`)을 넘으면 `py -3 -I -S` 와 PostToolUse 매처 좁히기를 본다 ⑦ 시나리오 체인의 멈춘 단계 뒤 이어 돌기 · 나눠 주기 배정을 기록에서. Server 배포 구성은 묶음마다 유지(사용자 규칙), 백엔드 매트릭스 나누기는 검증 실행만으로 다시 센 숫자 뒤. 맞지 않음: 작업자 문법 검사, sccache · 원격 캐시, 흔들리는 테스트 자동 재시도.
