# 모듈이 렌더 패스를 등록하는 창구

> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: G-1 · 진행: 1/3

모듈(게임 · 키트)이 렌더 패스를 넣으려면 `FramePassContext` · 커맨드 리스트 · 트랜지언트 풀을 모듈 경계 밖으로 내야 했고, 그것은 RT 안전 계약까지 내보내는 일이었습니다. 2026-10-22 사용자 결정으로 착수 조건(패스를 등록하려는 모듈)을 갈음했습니다.

## 1. 정한 것 — 경계를 넘는 것은 데이터뿐

- 파이프라인 XML 에 `_type="Module"` 패스를 둡니다. `_modulePass`(등록 이름)와 `_shaderPath` 가 필수이고(검증), 다른 타입에 `_modulePass` 를 적거나 한 파이프라인에 같은 이름이 두 번 나오면 검증 오류입니다.
- 실행은 일반 풀스크린 패스와 같습니다 — 선언한 입력을 역할로 바인딩하고 첫 출력에 그립니다. 입력 계약은 SourceColor · SceneDepth · GBufferNormal · AmbientOcclusion 모두 선택입니다.
- PSO 는 셰이더가 패스마다 달라 종류가 아니라 모듈 이름으로 둡니다(`RenderPSOCache::setModulePassPSO`, 셋업의 `createModulePassPSOs`). 쿠커는 파이프라인 패스를 훑으므로 그 셰이더를 이미 굽습니다.
- 모듈은 `ModuleRenderPassRegistration`(수명 객체 — 소멸자가 등록을 풂)으로 이름을 등록하고 패스 상수(이름 · float4, 최대 16)를 씁니다. 렌더러는 병렬 기록 워커에서 락 안에서 상수를 **값으로 복사**할 뿐 모듈 코드를 부르지 않습니다.
  - RT 안전: 모듈이 렌더 스레드 상태를 건드릴 길이 형식으로 없습니다.
  - 핫 리로드: 모듈 DLL 이 내려가도 렌더 스레드에 그 모듈의 함수 · vtable 이 남지 않습니다.
- 등록되지 않은 이름 · PSO 를 만들지 못한 패스는 오류를 이름마다 한 번 남기고 그리지 않습니다(다른 셰이더로 대신 그리지 않음). 그리지 않은 패스는 첨부를 "지웠다" 고 적지 않습니다.
- 모듈이 직접 드로우를 기록하는 길(커맨드 리스트 내보내기)은 열지 않았습니다. 필요해지면 그때 기록 시점 · 리소스 수명 계약을 따로 정합니다(아래 M3).

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| M1 | `RenderPassType::Module` · 검증 · 패스별 PSO · 실행 · `ModuleRenderPassRegistry` · 테스트 `RenderPassTest.ModulePassRegistrationIsScopedAndCopiesConstants` | 끝(2026-10-23 Debug 빌드 경고 0, `RenderPassTest` · `SceneTest` 81/81, GPU 를 뺀 렌더 관련 스위트 145/145) |
| M2 | 게임 모듈 하나가 실제로 등록하는 시연 패스(파이프라인 XML + 셰이더 + 등록)와 네 백엔드 캡처. 실행 경로(`executeModulePass` · `createModulePassPSOs`)는 아직 GPU 에서 돈 적이 없다 | 남음(GPU 확인 대기) |
| M3 | 모듈이 드로우를 직접 기록해야 하는 요구가 생기면 기록 계약 설계(지금은 하지 않음) | 남음 |
