# 모듈이 렌더러를 확장하는 창구

> 상태: 구현중 · 선행: 없음 · 영역: engine · 진행: 4/4

게임 · 키트 모듈이 렌더 스레드 확장 객체로 렌더 그래프에 패스를 넣고, 씬 프록시로 메시를 낸다. 언리얼의 `ISceneViewExtension` + `FRDGBuilder::AddPass` + `FPrimitiveSceneProxy` 와 같은 선이다(유니티 SRP `ScriptableRendererFeature` · 고도 `CompositorEffect` 도 렌더 스레드 훅 + 선언 + 실행 콜백으로 같은 방향이다).

## 진행

| 단위 | 내용 | 상태 |
|---|---|---|
| M1 | 모듈 렌더 패스 창구. 처음에는 XML `_type="Module"` 패스 + 이름 · 상수 레지스트리였고, M3 의 확장 훅으로 **대체**했다(옛 형식 리더 · 어댑터 없음) | 끝(대체됨) |
| M2 | 시연 패스(`Empty.ModulePassDemo`)를 M3 의 새 API 로 옮김(`Empty.TintDemo`) | 코드 끝, 컴파일 · 실행 확인 전 |
| M3 | 확장 훅(`IModuleRenderExtension`) + 패스 빌더(`ModulePassBuilder`) + 렌더 스레드 flush | 코드 끝, 컴파일 · 실행 확인 전 |
| M4 | 프록시 기하(`IModuleSceneProxy` → `ModuleDrawElement` → 렌더러가 그림), 시연 `Empty.DrawDemo` | 코드 끝, 컴파일 · 실행 확인 전 |

남은 구현은 없다. 컴파일을 하면 시연 파이프라인(`game/empty/pipeline/modulepasspipeline.xml`)으로 두 패스가 도는지 한 번 확인한다.

## 코드 읽기 결과 — 있던 것 · 없던 것

언리얼 RDG 에 맞출 때 새로 만들지 않고 기존 `RenderGraph` 를 확장했다. 읽고 확인한 결과다.

- **있던 것**: `RenderGraph::addPass( 이름, 읽기 목록, 쓰기 목록, 실행 )`(코드로 패스를 더하는 API 가 이미 있다), 읽기 · 쓰기 선언만 보고 위상 정렬(레벨)과 배리어를 추론하는 `compile` · `appendPassBarriers`, 레벨 앞에서 배리어를 `prepareTextureForShaderRead` · `prepareTextureForRenderTarget` 으로 내는 `onGraphLevelPrologue`, 패스마다 독립 컨텍스트로 병렬 기록하는 `executeParallel`. 즉 **선언 → 전이 · 배리어 자동화는 이미 있고**, 이름으로 찾는 트랜지언트 첨부에 대해 동작한다.
- **없던 것**: (a) 그래프는 파이프라인 로드 때 XML 패스로 한 번 묶여(`bindPassCallbacks`) 프레임마다 패스를 넣는 길이 없었다. (b) 트랜지언트 첨부는 그래프가 아니라 뷰 풀이 XML `_listAttachment` 로만 만들었다 — 임시 리소스 요청이 없었다. (c) 렌더 스레드에서 모듈 코드를 부르는 훅 · flush 가 없었다.
- **더한 것**: (a) 프레임마다 확장이 선언한 패스를 모아 선언 해시가 바뀔 때만 그래프를 다시 묶는다(`collectModuleFrame` → `bindPassCallbacks` 가 모듈 노드를 XML 패스 사이 정해진 자리에 넣는다). (b) 빌더의 `createTexture` 가 뷰 풀에 임시 텍스처를 만든다(`allocateModuleTextures`). (c) `ModuleRenderPassRegistry::RenderScope` + 등록 해제의 flush.

## 설계 결정

- **확장 훅 = 렌더 스레드의 `IModuleRenderExtension::setupPasses`.** 언리얼 `ISceneViewExtension::SetupViewFamily` 자리다. 프레임당 한 번, 기록 전에 불린다(뷰마다 아니다 — 그래프가 뷰마다 같은 패스를 실행하므로 뷰 정보는 실행 콜백이 받는다. 언리얼은 뷰마다 훅이 있고 뷰 목록이 프레임마다 다르기 때문에 뷰 수만큼 부르는 차이가 있다).
- **훅 위치는 XML 패스에 붙인 고정 지점 셋이다**(`ModuleHookStage`: 첫 패스 앞 · 후처리 효과 앞 · 출력 패스 앞). 이 엔진의 패스 목록은 XML 이 정하므로 언리얼처럼 후처리 패스 열거자마다 구독 지점을 두지 않는다.
- **패스 추가 = `ModulePassBuilder::addPass( 이름 · 읽기 · 쓰기 · 깊이 · 프로그램, 실행 콜백 )`.** 읽기 · 쓰기 선언이 그래프의 의존성이고 배리어는 엔진이 넣는다(언리얼 RDG 의 패스 파라미터와 같다). 같은 그래프에 들어가므로 스케줄러 · 배리어 추론이 XML 패스와 같다. 같은 자리의 패스끼리는 선언 순서가 쓰기 사슬이다. 읽기와 쓰기에 같은 첨부를 적으면 오류다(읽으며 쓰는 첨부는 만들 수 없다 — 블렌드로 위에 그리는 패스는 쓰기만 선언한다).
- **임시 리소스는 빌더가 요청하고 엔진이 만든다**(`createTexture` → 뷰 풀, 이름 "Module.<이름>"). 언리얼은 프레임 안에서 수명이 겹치지 않는 임시끼리 메모리를 공유하지만 여기는 풀 규칙을 따라 프레임 내내 보관한다(`TransientAttachmentPool` 머리 주석과 같은 한계). 선언하지 않게 된 임시 텍스처는 풀이 놓을 때(해상도 변경 · 파이프라인 교체)까지 남는다.
- **실행 콜백은 커맨드 리스트를 받지 않는다.** `ModulePassContext` 가 선언한 첨부 · 프로그램 위에서 검사한 그리기(`drawFullscreen` · `drawProxyElements`)만 연다. 언리얼은 `FRHICommandList` 를 통째로 주지만, 그러면 선언하지 않은 자원을 만져 그래프가 넣은 배리어가 틀어지고 병렬 기록 안전을 모듈이 지켜야 한다. 임의 기록이 필요해지면 그때 연산을 더한다.
- **프로그램 셰이더는 파이프라인 XML 의 `_listModuleShader` 로 선언한다.** 모듈이 런타임에 고르는 셰이더라도 쿠커가 미리 구워야 Shipping 이 PSO 를 만든다. 쿠커가 이 목록의 VS · PS 를 기본 진입점 · define 없이 굽고, 목록에 없는 셰이더를 요청한 패스는 오류로 막힌다. 진입점 · 퍼뮤테이션을 모듈이 고르는 길은 닫혀 있다(굽는 것이 한 벌이라서).
- **PSO 는 셋업에서 만든다**(`findOrCreateModuleProgramPSO`, 키 = 셰이더 · 고정 상태 · 타깃 포맷). 타깃 포맷은 첫 쓰기 첨부의 선언이, 깊이 테스트는 `_depthAttachment` 선언 여부가 정한다(어긋난 PSO 로 깊이를 붙이면 렌더 패스가 비호환). 실패는 기록해 되풀이하지 않고, 그 패스는 그래프에 넣지 않는다 — 다른 셰이더로 대신 그리지 않는다.
- **오류는 키마다 한 번 남기고 그리지 않는다.** 잘못된 선언(이름 없음 · 겹침 · 모르는 첨부 · 목록에 없는 셰이더 · PSO 실패), 메시를 얻지 못한 요소가 같다.
- **기하 = 컴포넌트와 렌더 스레드 프록시를 가른다**(`IModuleSceneProxy`, 언리얼 `FPrimitiveSceneProxy`). 게임 스레드는 값을 캡처한 갱신 함수를 올리고(`ModuleSceneProxyRegistration::update`) 렌더 스레드가 다음 프레임 시작에 프록시에 적용한다(`ENQUEUE_RENDER_COMMAND` 자리). 프록시는 `collectDrawElements`(언리얼 `GetDynamicMeshElements`)로 `ModuleDrawElement`(메시 ID · 월드 행렬 · 인스턴스 수 · 요소 상수)를 내고, **그리는 것은 렌더러다.** 요소는 GPU 씬 빌더에 넣지 않고 모듈 패스의 전용 드로우(요소마다 패스 상수 슬롯 · 직접 드로우)로 그린다 — 씬 배치 · 컬링 · 정렬에 들지 않는다(그림자를 드리우지 않고 절두체 컬링을 받지 않는다). 필요해지면 그 경로로 합친다.
- **메시는 렌더러가 쥔다.** 요소의 메시 ID 는 `MeshComponent::_meshID` 와 같은 철자(내장 도형 이름 또는 `.mesh` 경로)이고, 셋업에서 읽고 GPU 에 올린 `shared_ptr<Mesh>` 를 렌더러 표가 쥔다(메시 캐시가 약한 참조라서). 기록 중에는 표를 읽기만 한다.
- **핫 리로드 = 등록 해제가 flush 한다.** 렌더 스레드는 프레임의 모듈 구간(확장 훅 → 기록 → 정리) 동안 `RenderScope` 로 `_runMutex` 를 쥔다. `unregisterExtension` · `removeProxy` 는 그 잠금을 먼저 잡아 구간이 끝나기를 기다린 뒤 목록을 바꾸므로(언리얼 Live Coding 이 모듈을 내리기 전 `FlushRenderingCommands` 하는 것과 같다) 풀린 뒤에는 렌더 스레드가 그 모듈 코드를 다시 부르지 않는다. 구간이 끝날 때 엔진이 모듈이 넣은 콜백 · 요소를 모두 놓는다(람다의 소멸 코드가 모듈에 있으므로). 프록시는 레지스트리가 소유하고 등록 객체의 소멸자(모듈 안)에서 소멸한다. **모듈 구간 안(훅 · 콜백)에서 등록 해제를 부르면 교착한다.**
- **미리보기 씬 렌더러는 확장을 받지 않는다.** 확장은 주 렌더러의 파이프라인에 붙는다.
- **상수 이름은 엔진 PassCB 칸의 이름이라 그린 뒤 되돌린다**(`ModuleConstantScope`: 쓰기 전 값을 떠 두었다 되돌리고, 없던 칸은 지운다). 모듈 전용 칸을 PassCB 에 두는 안은 모든 셰이더를 다시 굽게 해서 기각했다.
