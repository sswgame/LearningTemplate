# 모듈이 렌더 패스를 등록하는 창구

> 상태: 구현중 · 선행: 없음 · 영역: engine · 진행: 2/3

게임 · 키트 모듈이 렌더 패스를 넣을 수 있게 하되, 경계를 넘는 것은 데이터(파이프라인 XML · 이름 · float4 상수)뿐으로 한다. 커맨드 리스트를 밖으로 내면 렌더 스레드 안전 계약까지 내보내야 하기 때문이다.

## 진행

| 단위 | 내용 | 상태 |
|---|---|---|
| M1 | `Module` 패스 종류 · 검증 · 패스별 PSO · 실행 · `ModuleRenderPassRegistry` | 끝 |
| M2 | 게임 모듈이 실제로 등록하는 시연 패스(`EmptyGame` 의 `Empty.ModulePassDemo`, 파이프라인 `modulepasspipeline.xml`, 셰이더 `modulepassdemo.hlsl`) | 코드 끝, 컴파일 · 실행 확인 전 |
| M3 | 모듈이 드로우를 직접 기록해야 할 때의 기록 계약 | 남음(요구하는 모듈이 생길 때) |

첫 일: M2 를 네 백엔드로 돌려 `meanRedMinusBlue` 문턱(0.02)을 첫 측정값으로 확정한다. 이후 남은 구현은 없다.

## 설계 결정

- **파이프라인 XML 에 `_type="Module"` 패스를 둔다.** `_modulePass`(등록 이름)와 `_shaderPath` 가 필수다. 다른 타입에 `_modulePass` 를 적거나 한 파이프라인에 같은 이름이 두 번 나오면 검증 오류다.
- **실행은 일반 풀스크린 패스와 같다.** 선언한 입력을 역할로 바인딩하고 첫 출력에 그린다. SourceColor · SceneDepth · GBufferNormal · AmbientOcclusion 은 모두 선택이다.
- **PSO 는 모듈 이름으로 둔다.** 셰이더가 패스마다 다르기 때문이다(`RenderPSOCache::setModulePassPSO`). 쿠커가 파이프라인 패스를 훑으므로 셰이더는 이미 구워진다.
- **등록은 수명 객체 `ModuleRenderPassRegistration`** 이 한다(소멸자가 등록을 푼다). 패스 상수(이름 · float4, 최대 16)는 렌더러가 기록 워커에서 락 안에 **값으로 복사**하며 모듈 코드를 부르지 않는다. 그래서 렌더 스레드 상태를 건드릴 길이 없고, 모듈 DLL 을 핫 리로드로 내려도 vtable 이 남지 않는다.
- **등록 없는 이름 · PSO 실패는 오류를 이름마다 한 번 남기고 그리지 않는다.** 다른 셰이더로 대신 그리지 않으며, 그리지 않은 패스는 첨부를 지웠다고 적지 않는다.
- **모듈 상수 이름은 엔진 PassCB 칸의 이름이라 뒤 패스가 같은 칸을 읽는다.** 그래서 `executeModulePass` 가 쓰기 전 값을 떠 두었다 그린 뒤 되돌린다(없던 칸은 `PassConstantValues::remove`). 모듈 전용 칸을 PassCB 에 두는 안은 모든 셰이더를 다시 굽게 해서 기각했다.
