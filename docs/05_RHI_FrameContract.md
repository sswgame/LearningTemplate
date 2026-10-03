# RHI 프레임/렌더타깃 계약

> 목적: `RenderThread::executeFrameBody` 의 프레임 · 렌더타깃 순서를 **명시적 계약**으로 적는다.
> 프레임 순서를 건드리기 전에 1절(계약)과 2절(함정)을 읽고, 바꾼 뒤에는 3절대로 검증한다.

## 1. 계약

`RenderThread::executeFrameBody` (RenderThread.cpp) — 두 경로가 같은 순서를 지난다.

```
pDevice->beginFrame( clearColor )                      # 수명주기만 — 펜스 대기(GPU 백프레셔), 이미지 획득, 기록 시작
if (offscreen) cmd->beginRenderPass( gameRT, Clear )   # 에디터 게임뷰: 그래프가 게임 RT 에 그린다
FrameRenderer::executePacket()                         # 그래프가 자기 리스트에 기록
if (offscreen) cmd->prepareTextureForShaderRead( gameRT )  # 열린 렌더 패스도 여기서 닫힌다
cmd->beginRenderPass( 백버퍼(핸들 0), offscreen ? Clear : Load )  # 백버퍼 경로는 그래프가 그린 것을 보존
presentHook()                                          # 에디터 UI
pDevice->endFrame( vsync )                             # 제출 + 프레젠트
```

| 연산 | 의미 | 호출 횟수 |
|---|---|---|
| `IRHIDevice::beginFrame( clearColor )` | **수명주기 전용** — 스왑체인 이미지 획득, 기록 시작. 렌더타깃을 바인딩하지 않는다 | 프레임당 정확히 1회, 맨 앞 |
| `IRHICommandList::beginRenderPass( info )` | **렌더타깃 바인딩 전용** — 타깃 · 로드 op · 클리어. 타깃 핸들 `0` 은 백버퍼 | 필요한 만큼 |
| `IRHICommandList::prepareTextureForShaderRead( tex )` | 샘플링용 레이아웃/상태 전환 | 필요한 만큼 |
| `IRHIDevice::endFrame( vsync )` | 제출 + 프레젠트 | 프레임당 1회 |

- 오프스크린 전용 시작/끝 연산은 없다. 오프스크린도 같은 스트림 · 같은 제출이고, 순서는 큐 순서와 배리어가 보장한다.
- 병렬 커맨드 기록: DX12 · Vulkan 활성(리스트마다 전용 얼로케이터/커맨드 풀). DX11 은 드라이버가 `DriverCommandLists=0` 을 보고하면 런타임 판정으로 꺼지고, GL 은 구조상 제외.
- 스왑체인은 백엔드마다 구체 클래스(`D3D11RHISwapChain` · `D3D12RHISwapChain` · `VulkanRHISwapChain`)이고 디바이스가 소유한다 — 가상 인터페이스는 없고 GL 에는 스왑체인 객체가 없다.

## 2. 함정

- **백버퍼 바인딩은 `beginRenderPass( 핸들 0 )` 로만 한다.** `beginFrame` 은 타깃을 바인딩하지 않으므로, "`beginFrame` 뒤에
  기록된 것은 백버퍼로 간다" 는 식으로 호출 위치에 기대는 코드를 두지 말 것. 백버퍼 패스를 빼먹으면 UI 가 엉뚱한 타깃 · 상태로
  그려지고 DX12 는 리소스 상태 오류와 DEVICE_HUNG 으로 무너진다.
- **프레임 수명주기 상태(Vulkan `_bFrameStarted`)는 `beginFrame` / `endFrame` 만 바꾼다.** 다른 곳에서 내리면 `endFrame` 이
  조기 반환해 프레임 제출이 통째로 사라지고, acquire 세마포어가 신호된 채 남아 다음 프레임이 깨진다.
- **렌더 패스를 닫는 것은 플래그가 아니라 실제 `vkCmdEndRenderPass` 다.** 플래그만 내리면 다음 `beginRenderPass` 가 열린 패스 위에 쌓인다.
- **Vulkan 리스트는 primary 커맨드 버퍼다.** `vkCmdBeginRenderPass` 는 primary 전용(`VUID-vkCmdBeginRenderPass-bufferlevel`)이라
  패스마다 자기 렌더 패스를 여는 이 구조에서는 secondary + `vkCmdExecuteCommands` 가 성립하지 않는다. 그래서 리스트는 프레임
  스트림을 세그먼트로 잘라 그 사이에 끼운다(`VulkanRHICommandList.h`).
- **프레임을 넘어 재사용되는 커맨드 리스트**(`FrameRenderer::_frameCmd` 등)는 같은 얼로케이터를 GPU 대기 없이 `Reset` 하면 안 된다 —
  직전 프레임 커맨드를 GPU 가 아직 읽는 중이라 DEVICE_HUNG 이 난다. DX12 `beginCommandList` 는 두 번째 기록부터 리스트 · 얼로케이터
  쌍을 펜스를 통과한 풀의 것으로 갈아 낀다. 에디터 경로는 프레임이 느려 이 결함을 가린다.
- **블로킹 대기를 없앨 때는 그 대기가 무엇을 가리고 있었는지 먼저 찾는다.** 프레임마다 디스크립터 셋을 새 슬롯으로 다시 쓰는 코드나
  프레임버퍼 · 렌더 패스를 즉시 `vkDestroy` 하는 코드는 블로킹 제출 아래에서만 맞는다 — 실행 중인 직전 프레임이 그 객체를 참조한다.
  실행 중인 프레임이 참조할 수 있는 디스크립터 셋은 덮어쓰지 않고, 파괴는 해제 큐로 미룬다.
- **백버퍼에 직접 그리는 PSO 는 백버퍼 포맷(`getBackBufferFormat()`)을 따라야 한다.** 파이프라인 리소스 포맷으로 만들면 Vulkan 에서
  렌더 패스 비호환이 된다. `-gv_rhiBackBufferFormat=1`(B8G8R8A8)로 띄워 확인한다.

## 3. 검증 프로토콜 — **렌더 패스 시험만으로는 부족하다**

렌더 패스 GPU 시험(`RenderPassGpuTest`)은 검증 레이어를 켜고도 에디터/ImGui 경로를 타지 않는다. 프레임 순서를 바꿨으면
네 백엔드 × 에디터 유무 8조합을 실제로 띄워 본다. `AppSmokeTest`(AppTest, `hostgpu`)가 이 8조합을 띄워 종료 코드 0 · 로그 `[Error]` 0건을
단언하므로 `ctest -L hostgpu` 가 기본 그물이고, 손으로 볼 때는 아래처럼 띄운다:

```powershell
# 백엔드 4개 × 에디터 유무 2가지 = 8조합을 각각 8초 띄우고 로그의 오류를 센다
.\App.exe -EnableEditor            # 에디터 O: 그래프가 게임 RT(오프스크린)에 그린다
.\App.exe -EnableEditor -vulkan
.\App.exe -EnableEditor -dx11
.\App.exe -EnableEditor -gl
.\App.exe                          # 에디터 X: 그래프가 **백버퍼에 직접** 그린다 — 경로가 다르다
.\App.exe -vulkan
.\App.exe -dx11
.\App.exe -gl
```

- **정상 기준선 = `[Error]` 0건.** 하나라도 있으면 회귀다.
- Vulkan 은 `VUID` / `spec states` 문자열이 0건이어야 한다.
- stderr 에 `CRASH` 가 없어야 한다.
- 비결정적이므로 새로 고친 조합은 **3회 이상** 반복해 재현율을 본다.

**에디터 유무를 반드시 둘 다 볼 것.** 두 경로는 "그래프가 어디에 그리는가"가 다르므로 사실상 다른 코드 경로이고,
한쪽만 보면 다른 쪽의 결함이 가려진다 — 에디터 경로는 프레임이 느려 GPU 가 늘 따라잡으므로 동기화 결함이 특히 잘 숨는다.
백엔드마다 `beginFrame` 이 하는 일이 다르므로 프레임 순서는 **한 번에 한 백엔드씩** 바꾸고 매번 이 절대로 검증한다.
