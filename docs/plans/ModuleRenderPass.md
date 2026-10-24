# 모듈이 렌더 패스를 등록하는 창구

> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: G-1 · 진행: 2/3

**남은 확인(통합에서 확인):** 매니페스트 id `module-pass-shader-patch`, `module-pass-scenario`(M2 행). 확인이 끝나면 통합 담당이 M2 를 닫는다.

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
| M2 | 게임 모듈 하나가 실제로 등록하는 시연 패스(파이프라인 XML + 셰이더 + 등록)와 네 백엔드 캡처 | 끝(코드 · 데이터 · 시나리오, 2026-10-24 — 실행 경로는 아직 GPU 에서 돈 적이 없다). 파이프라인 `game/empty/pipeline/modulepasspipeline.xml`(모션 블러와 Present 사이의 `Module` 패스), `EmptyGame` 이 `Empty.ModulePassDemo` 를 등록하고 상수 `g_OutlineColor`(붉은 틴트)를 넘기며, 시나리오 `game/empty/automation/modulepass.scenario.xml` 이 `meanRedMinusBlue` 로 본다. 셰이더 `engine/shaders/modulepassdemo.hlsl` 은 커밋 훅(쿠킹 바이너리 요구)에 걸리고 에셋 검증(파이프라인이 가리키는 셰이더가 있어야 한다)이 셰이더에 묶여, **셰이더 · 파이프라인 · 시나리오 세 파일을 패치 `ShaderPatches/02-ModuleRenderPass-M2.patch` 하나로** 남겼다 — 통합이 적용하고 `App --cook-shaders` 로 굽는다. 만들면서 고친 것: 모듈 상수의 이름은 엔진 패스 상수 칸(PassCB)의 이름이라 뒤 패스가 같은 칸을 읽으므로, `executeModulePass` 가 쓰기 전 값을 떠 두었다가 그린 뒤 되돌린다(없던 칸은 `PassConstantValues::remove` 로 지운다, 테스트 `PassConstantValuesRemoveClearsTheSlot`). 모듈 전용 칸을 PassCB 에 따로 두는 안은 모든 셰이더를 다시 굽게 하므로 하지 않았다 |
| M3 | 모듈이 드로우를 직접 기록해야 하는 요구가 생기면 기록 계약 설계(지금은 하지 않음) | 남음(조건부 — 요구가 생길 때. 지금은 요구하는 모듈이 없어 설계할 입력이 없다) |
