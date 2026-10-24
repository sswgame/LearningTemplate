# 다음 세션 인계

> 질문: 지금 어디까지 왔고, 다음 세션이 처음에 할 일은 무엇인가?
> 독자: 새로 시작하는 세션(에이전트와 사람).
> 결론: 통합 E 가 main 에 들어갔다(미푸시). 다음 세션은 GPU 확인(Shipping · hostgpu · 네 백엔드)을 돌리고 초록이면 push 한다.

이 문서는 지금 상태만 둡니다. 한 일은 `git log` 가 원본이고, 설계는 계획 문서, 순서는 [Roadmap](Roadmap.md)에 있습니다.

| 알고 싶은 것 | 어디 |
|---|---|
| 남은 일 한 건 한 건 | 후보 문서 `Candidates-<영역>.md` — `py -3 -m Scripts plans --area <영역>` |
| 설계와 구현 단위 | 계획 문서 — `py -3 -m Scripts plans`, `--file <이름>` |
| 앞으로 할 일의 순서 | [Roadmap](Roadmap.md) |
| 결정과 근거 | [결정 기록](../09_Decisions.md) |
| 작업 방식 · 검증 | [작업 방식](../11_Workflow.md), [검증과 측정](../08_Verification.md) |
| 에이전트에게 맡길 때 · 토큰 | [에이전트와 토큰](../12_Agents.md), [작업자 규칙](WorkerRules.md) |
| 환경 · 도구 함정 | [함정](../14_Pitfalls.md) |

## 0000. 2026-10-24 통합 E — main 에 들어감, GPU 확인 대기

### 상태

- **main 에 들어감(미푸시)**: 통합 워크트리 `integE` 가 `wt/integD`(앞 통합이 모은 lambda* · kitshelpers · servicelist · verifyleak · scnpixel · singletonget · thumbheadless · plansweep · leakwin · ownership · cand* · measurebatch · judgeapply · planproject · plannet · leftovers · docsweep · planengine · plangameplay · planui · plananim · plangfxq)와 `wt/plangfx2` 를 합쳤고, main 을 fast-forward 했다. **push 는 하지 않았다** — 로컬 main 이 `origin/main` 보다 앞서 있다.
- **검증(Debug)**: `full-build` 경고 0 · 오류 0(증거 남음), `lint-suite` 46/46, `ctest -L nogpu|lint|race` 통과(AppTest_Race 는 GPU 로 돈다 — PassCB 가 768 을 넘은 것을 여기서 잡아 고쳤다). **GPU 확인 대기** — Shipping 빌드 · `-L hostgpu` · 네 백엔드 × 대표 셋(Empty · 에디터 · Shooter3D · NileCity)은 돌리지 않았다.
- **통합이 고친 것**(커밋 메시지가 정본): 리플렉션 생성 둘, 컴파일 · 링크(프로파일 매크로가 엔진 전용 헤더 없이 쓰이게 `FrameProfiler::registerEngineScopeSite`, `make_unique<T>.createWith` 로 friend private 생성자, `sw::basic_string::substr` 형식), 셰이더 패치 다섯 적용 · 네 백엔드 쿠킹(옛 퍼뮤테이션 바이너리 376 개 삭제), 린트 다섯 갈래, nogpu 실패(자식 프로세스 띄우기의 상속 핸들 배열 수명, 뗀 뒤 새 누름까지 먹던 UI 소비, 고도 0 하늘의 땅 판정, 테스트 기대값).
- **병합하지 않은 것**: `shaderfmt3`(다른 작업자가 쓰는 중). 이 통합이 `probeupdate.hlsl` · `sky.hlsl` · `binding.hlsli` 를 바꿨으므로 그 브랜치는 main 을 받아 다시 맞춘다. `plangfx2` 워크트리의 커밋하지 않은 변경 39 개(지형 지오모프 · 레이어 · 배치 규칙, 패치 `05-EnvironmentRemainders-E3E4-Terrain.patch`)는 병합하지 않았다 — 작업자가 커밋해야 한다. `lambdaser` · `singletonget` 의 커밋하지 않은 변경은 공유 stash(`stash@{0}` 망가진 정규식, `stash@{1}` 중복 줄)에 있다.

### 다음 세션이 처음에 할 일

1. **GPU 확인**: Shipping 빌드 → `-L hostgpu` → `verify-batch --owner <이름>` 의 Shipping 항목(새 시나리오 fog · sky · vfxgpulights · modulepass · realtimegi, `Empty` 렌더 테스트 씬 + Shooter3D × 네 백엔드). 실패하면 그 단위를 고치거나 되돌린다. 특히 이번에 처음 GPU 에서 도는 것: 볼륨 안개 컴퓨트, 대기 산란 하늘, 프로브 갱신 컴퓨트(`probeupdate.hlsl` — FXC 때문에 루프 모양을 바꿨다), GPU 파티클 라이트, 모듈 패스.
2. **push**: GPU 확인이 초록이면 main 을 push 하고 `ci-jobs --summary`.
3. **정리**: 병합이 끝난 워크트리 `worktree-remove`(통합 세션이 지우지 못한 것 — 주인 프로세스가 살아 있는 앞 세션 소유), 공유 stash, 셰이더 주석의 옛 이름 `MaterialShaderHooks`.
4. **결정 대기**: 로거 구조(`SW_LOW_LEVEL_REPORT` 냐 Logger 를 Container · Memory 에서 떼느냐) — design-reviewer 로 정하고 `09_Decisions.md` 에 적는다.

### 남은 것

끝난 일은 `git log` 에 있고, 여기에는 안 끝난 결함 · 확인만 한 줄씩 둔다.

- **같은 모양이 남았을 곳(이번 통합에서 본 것)**: ① 셰이더 쿠킹이 새 퍼뮤테이션 해시로 다시 구울 때 옛 바이너리를 지우지 않아 `--verify-shaders` 가 진다(통합이 손으로 지웠다) — `ShaderCooker` 가 매니페스트에 없는 바이너리를 스스로 지우게 할 후보. ② `App --cook-shaders` 가 쿠킹 실패가 있어도 종료 코드 0 이다(폴백). ③ 정의되지 않은 매크로를 `#if` 로 보는 include(AuthorityGhost)는 `-Wundef` 가 잡는다 — 켤지 정한다. ④ 로그 문장을 맞추는 테스트(`countContaining("…")`)는 로그 문구를 바꾸면 진다 — 로그 id 로 맞출 후보. ⑤ `TestResourceDataSchema` 의 종류 표는 엔진 등록표(`SW_RESOURCE_DATA_KIND`)의 사본이다(게임 파이프라인 XML 을 몰랐다).
- **싱글턴 · 람다**: `ResourceDataKindRegistry` 단일체(후보 `SingletonGetToServices`), 싱글턴 `get()` 게이트가 `instance()` · `shared()` 이름을 못 봄.
- **누수 탐지**: 설계 검토의 2 ~ 4단계([LeakDetection](LeakDetection.md)).
- **레지스트리 · 파서**: 이름을 손으로 거절하는 레지스트리(`LocalStoreFactory` · `SQLDriverRegistry` · `LoginService` · `ModuleRenderPassRegistry` · `EditorAssetTypeActions`)는 `RegistrationList` 후보, 손 불리언 파서는 `StringUtil::tryParseBool` 후보, `ElementGrid.cpp` 의 `cellBytes` 손 계산.
- **셰이더 확인**: 툰이 프로브(간접 조명)를 받지 않는 것이 의도인지.
- **층 게이트**: GameFramework 표에 "표 = 계산값" 검사를 붙일지, Bootstrap 13(계산값 9)이 의도인지.
- **확인 항목**: 클라이언트 근접 피해 소실(meleeunify) · 활의 가는 줄과 투사체 방향 · `PoseModifierComponent::setSocketTable` 호출처 없음([ShooterCharacterIntegration](ShooterCharacterIntegration.md)).
- **CI**: 야간 `Load (Linux | Release, nightly)` 실패의 근본 원인을 모른다(후보 `DiskPathVsResourceId`).
- **시나리오**: 게임 시나리오가 `Resource/` 에 쓰는지 점검하지 않았다. 클릭한 순간의 방향으로 사격이 나가는지는 시나리오로 보지 못했다.
