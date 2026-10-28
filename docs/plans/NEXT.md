# 다음 세션 인계

<!-- next-format: one-page -->

> 질문: 지금 어디까지 왔고, 다음 세션이 처음에 할 일은 무엇인가?
> 독자: 새로 시작하는 세션(에이전트와 사람).
> 결론: 통합 V(작업자 브랜치 14 개)와 뒤이은 설정 소유 변경 · 테스트 수정이 main 에 들어가 push 됐다(ca6208524). 기능별 테스트 · 시나리오(`Test/` · `Resource/**/automation/`)는 새 프레임워크를 만들려고 전부 지웠다 — 새 설계는 사용자와 다음 세션이 정한다(옛 테스트 · 시나리오에 기댄 확인 항목과 남은 일은 이 문서에서 걷었다). 끝난 통합의 일지는 `git log` 가 정본이라 여기 두지 않는다.

이 문서는 지금 상태 한 장만 둔다.

## 1. 지금 상태 — 통합 V (2026-10-28, main 에 push 완료)

1 차(net-load · gfx-baseline · cand-sweep · old-defects · scratch-gate · vtools · keep-assets)와 2 차(wpath · svc-cancel · parse-core · ui-wrap · py-compat · rhi-leftovers · pair-scene)를 병합해 main 에 fast-forward 로 push 했다. CI 수정 커밋 01e3e4e06(Vulkan 확장 개수 · Python 3.12 `read_text` · 없는 sysfs 카드 조사)과 사용자가 지운 `prof.out` 도 들어갔다. push 직전 증거는 세 구성의 빌드 통과와 `full-build` 뿐이고 아래 "못 돌린 검증"은 안 했다.

워크트리는 모두 지웠다(주 작업 트리만 남는다). 병합 안 된 `wt/integ-merge` 의 커밋 12 개를 보존했던 태그는 내용을 확인한 뒤 지웠다 — 5 패치 가운데 `d3338bc16`(모듈이 정의한 설정을 `ConfigManager` 에 등록하지 않고 서비스가 `readConfigFileInto` 로 직접 소유)만 main 에 없어 9d2c3d83f 로 가져왔고 `unloadConfig` 는 지웠다. 나머지는 main 에 같은 수정이 있거나(`releaseConfig` = `unloadConfig`) 낡은 문서다. `ResourceID` 호출처 수정(`f505cecc3`)은 15 개 파일을 하나씩 대조하지 않았고 main 이 세 구성에서 빌드되는 것으로 갈음했다.




**병합 뒤 확인 목록**
- 종료 때 서비스 경고가 나는지(`ServerRuntimeOpensTheSQLiteStore…` 의 방어 범위 표시를 지웠다 — 코드 경로만 근거).
- `py -3 -m Scripts paired-net-pair --preset Ninja-Release` 첫 실행 — `ClipLengthMismatch` · 헤드리스 서버의 SkeletalMesh · 파괴 러너가 새로 보는 `[Error]` 판정의 오탐. 클라이언트의 `ParticipantNotReady` 는 보관 뒤 300 프레임까지 재시도한다.

## 2. 남은 일

| 무엇 | 첫 일 한 줄 | 계획 문서 |
|---|---|---|
| 1. 렌더 기준 재측정 | DoF · 모션 블러 건너뛰기는 이미 코드에 있다(`kSkipsWithoutPostProcess` 등). 통합 빌드로 `perf-frame` · `load-baseline-suite` · `load-auto-render-scale` 를 `quality` 를 적어 다시 뜨고 Empty `GPU.Frame` 468 → 1089 us 와 GPU 메모리 56.8 → 176.9 MB 가 줄었는지 숫자로 본다(숨기기 위한 재기록 금지) | [Candidates-graphics](Candidates-graphics.md) |
| 2. 누수 탐지 L2~L4 | L2 는 Release 측정, L3 는 실행 테스트, L4 는 L2 결과로 값을 정한다 — L2 부터 | [LeakDetection](LeakDetection.md) |
| 4. 야간 Linux Load 실패 | 4d3249e 야간은 `availableExtCount` 컴파일 오류로 졌고 01e3e4e06 이 고쳤다. 그 전 실행의 connect × Mid `solo:2x1` 거절은 `parseCompactMode` 가 이유를 돌려주게 했으니 다음 야간 문구로 원인을 본다 | `Candidates-*.md` |
| 7. 사용자 몫 | Steam/Epic SDK · H-3 운영 문서는 보류. 컴파일 셰이더 git 보관은 유지로 확정(2026-10-28). 말 모델은 Quaternius CC0 로 들어갔다(화면 확인만 남음) | [WildWestHorseMount](WildWestHorseMount.md) |

`D:/w` 에 작업자 워크트리와 산출물이 남아 있다. 통합이 main 에 들어간 뒤 `Scripts/dev` 정리 스크립트로 지운다(워크트리 기준 4 개).

## 3. 남은 후보 (이번 통합에서 드러남, 아직 안 한 것)

- 쓰기 게이트는 `Resource` 루트 아래만 지킨다 — `docs/` · `Config/` 같은 루트 밖 추적 파일을 자동화가 쓰는 길과 `fopen` · `ofstream` 직접 쓰기는 못 막는다. `FileUtil::writeTextFile` 은 빈 글도 쓴다(저장 입구마다 거절).
- `RemoteConfig` 밖 서비스는 `ServiceStoreCompletion` 으로 취소를 가르지만 저장소 스레드의 `run` 안 로그는 린트 밖이다(`ServiceRetentionJob` 한 곳은 손으로).
- 이유가 사라지는 파서 조사를 못 한 곳: `Source/Server` · `Source/App` · `Source/ModuleHost` · `Tools`(`ReflectionParser` 등), 상태 아카이브 읽기 40 여 곳, `(void)load…` 40 여 곳(안쪽 로그가 이유를 남긴다고 주석).
- `ArrowButton` · 체크박스 앞 `SameLine` 은 게이트가 보게 넓혔으나 `InspectorPanel` 의 "Active" · `TileMapPanel` 의 "Erase" 는 `{ }` 가 끼어 게이트도 못 본다. 콤보 · 입력 칸 앞은 폭을 따로 정해 안 봤다.
- `uavBarrier` · `copyBufferToReadback` 선행 상태의 백엔드별 매핑 대조(표는 `transitionBuffer` 만 있다). Vulkan 프레임 밖 펜스 대기는 `RhiAsyncUploads` 계획이 맡는다(M1 측정에서 `RT.Upload.waitIdle` 가 보이면).
- `PairedAnimationSystem::startMatchingSession` · `InteractorComponent::startPairedAnimation` 의 `ParticipantNotReady` 처리. DX11 굶음 방지 두 스레드 GPU 케이스(방법만 `Candidates-graphics.md` 에 있다).
- crowd 기록의 뒤쪽(+240 · +300 프레임) 두 기록도 `WaitUntil` 이 붙잡은 길이에 따라 상태가 갈릴 수 있어 같은 종류의 흔들림이 남았다.
- `CheckPythonMinimumVersion` 의 새 기능 표는 표에 있는 이름만 잡는다. 저장소 기준 최소 버전은 3.10(`Constants.py`) 이고 CI 의 리눅스 러너는 3.12 쯤이다.

사용자 결정(2026-10-27 · 28): 장르 키트 21 개(232 파일) 유지, 컴파일 셰이더 git 보관 유지, 평문 UDP 정책 병합 승인.

**사용자 영향(통합 V)**: 자동화 실행 중 `Resource` 원본 쓰기는 오류(`<Scratch>` 선언 또는 `scratch/` 아래만) · `asset.import` 의 임시 폴더가 `scratch/import` · 컴퓨트 디스패치는 PSO 없으면 멈춤 · 배포본이 `SW_USER_DATA_ROOT` 로 사용자 데이터 위치를 돌릴 수 있음 · 무인 골든 · 기준은 `quality` 를 기록하고 다르면 거절 · `Ninja-Release` 가 모든 게임을 지음.
