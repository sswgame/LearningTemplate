# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 배치 경계마다 적용 담당이 고쳐 씁니다. 끝난 항목은 지웁니다(한 일은 `git log`, 교훈은 영역 README · 결정 기록 · 검증 문서에 있다).
작업 방식은 [작업 방식](../11_Workflow.md), 남은 일 전체는 [백로그](../06_Backlog.md)입니다.

## 지금 상태

- 6 차 주석 작업과 구조 리팩토링이 main 에 들어갔다: 엔진 루프에서 자동화 · 텔레메트리 · RHI 교체 프로브를 프레임 관찰자 훅 뒤로(R1 · R11), 에디터 그리기 인계 상태 기계 `DrawSnapshotHandoff`(R2),
  레지스트리 통일(R3 · R9 · R10), 리플렉션 로더 `FacialRig` · `LipSync`(R4), 게임프레임워크 공용 헬퍼(R13). 에디터 모듈 리로드가 Vulkan · OpenGL 에서 죽던 것을 고쳤다(`abandonPendingDraw` 는 인스턴스를 없앨 때만).
- 에디터 체인은 네 백엔드가 통과한다. Vulkan 만 시나리오 밖 `[Error]` 3 줄이 남는다 — 백로그 1-4 ①(창 크기를 바꾼 뒤 옛 렌더타깃 읽기).
- 판정 기준은 StructureRefactor 의 "옮길지 판정하는 기준"과 "SOLID 는 반드시 지킨다"다(반쪽 이전 금지, 훅은 쓰는 곳이 둘 이상일 때, 충돌하면 SOLID 가 앞선다).
- 마지막 전체 검증(PC `SW`)은 린트, Debug `-L nogpu` · `-L hostgpu`, Shipping, Server, 게임별 프리셋 여덟, 네 백엔드 대표 실행이었다. 그 뒤 변경은 린트와 Debug · Shipping `-L nogpu` 로 확인했다.

## 다음 할 일 (순서대로)

0. **구조 리팩토링 — 계획 [StructureRefactor](StructureRefactor.md).** R4 남은 파일 · R7(체인 후속)을 병렬로, 그다음 R5(`EngineLoop` 분해, 측정 먼저) · R6.
   SOLID 단위 S1 ~ S22 는 같은 문서의 표와 판정 기준(옮길지 판정하는 기준, SOLID 는 반드시 지킨다)을 따른다. 단위마다 파일 소유자가 하나다.
1. **CI 확인.** 확인할 테스트 목록은 백로그 1-10 "다음 CI 실행으로 확인할 것". push 뒤 결과를 기다리지 않는다([작업 방식](../11_Workflow.md) 4절).
2. **빌드 속도 5 · 6 단계** — [BuildSpeed](BuildSpeed.md).
3. **엔진 분할** — [EnginePartition](EnginePartition.md) 0 단계(기준 측정)와 2 ~ 5 단계.
4. **서버 디버깅** — [ServerDebug](ServerDebug.md), 다음은 S4(회선 조절).
5. **6 차 주석 작업의 남은 구역** — 백로그 1-9.

## 검증할 것

- 6 차 병합 뒤 돌리지 않은 것: Shipping `-L hostgpu`, 게임 프리셋(`Ninja-Debug-<Game>`)과 네 백엔드 대표 실행(지연 로드 제거가 핫 리로드와 모듈 로드를 바꿨다),
  Debug `-L hostgpu` 의 통과 여부(실행은 했으나 결과를 읽지 못했다 — 다시 돌려 `tests passed` 를 본다).
- 다른 PC 에서 vcpkg 포트 `tracy`(port-version 2)를 받으면 `cmake --preset` 이 Tracy 를 다시 빌드한다.
