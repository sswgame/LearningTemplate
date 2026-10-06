# RHI 프레임/렌더타깃 계약

이 문서는 렌더 스레드가 한 프레임을 그리는 순서를 계약으로 적어 둡니다. 순서의 기준 코드는 `RenderThread::executeFrameBody`(`RenderThread.cpp`)입니다.
프레임 순서를 바꾸기 전에 1절의 계약과 2절의 함정을 읽고, 바꾼 뒤에는 3절의 방법으로 검증합니다.
이 순서는 네 백엔드(DirectX 11, DirectX 12, Vulkan, OpenGL)가 모두 지켜야 하고, 한 백엔드에서만 맞는 가정은 다른 백엔드에서 크래시나 GPU 행으로 드러납니다.

## 1. 계약

에디터가 있을 때와 없을 때, 두 경로가 같은 순서를 지납니다. 에디터가 있으면 렌더 그래프는 게임 뷰용 렌더 타깃(오프스크린)에 그리고, 에디터가 없으면 백버퍼에 직접 그립니다.

```
pDevice->beginFrame( clearColor )                      # 수명 주기만: 펜스 대기, 스왑체인 이미지 획득, 기록 시작
if (offscreen) cmd->beginRenderPass( gameRT, Clear )   # 에디터 게임 뷰: 그래프가 게임 RT에 그린다
FrameRenderer::executePacket()                         # 그래프가 자기 커맨드 리스트에 기록
if (offscreen) cmd->prepareTextureForShaderRead( gameRT )  # 열린 렌더 패스도 여기서 닫힌다
cmd->beginRenderPass( 백버퍼(핸들 0), offscreen ? Clear : Load )  # 백버퍼 경로는 그래프가 그린 것을 보존
presentHook()                                          # 에디터 UI
pDevice->endFrame( vsync )                             # 제출과 프레젠트
```

| 연산 | 하는 일 | 호출 횟수 |
|---|---|---|
| `IRHIDevice::beginFrame( clearColor )` | 수명 주기 전용. 렌더 타깃은 바인딩하지 않습니다 | 프레임당 정확히 1회, 맨 앞 |
| `IRHICommandList::beginRenderPass( info )` | 렌더 타깃 바인딩 전용. 타깃 핸들 `0` 은 백버퍼 | 필요한 만큼 |
| `IRHICommandList::prepareTextureForShaderRead( tex )` | 샘플링할 수 있는 상태로 전환 | 필요한 만큼 |
| `IRHIDevice::endFrame( vsync )` | 제출과 프레젠트 | 프레임당 1회 |

`beginFrame` 은 펜스를 기다려 GPU가 너무 뒤처지지 않게 하고(백프레셔), 스왑체인 이미지를 얻고, 기록을 시작합니다.
`beginRenderPass` 는 타깃, 로드 동작, 클리어 값을 정합니다.

오프스크린 전용 시작이나 끝 연산은 없습니다. 오프스크린 렌더링도 같은 스트림에 기록하고 같은 제출로 보냅니다. 순서는 큐의 순서와 배리어가 보장합니다.

커맨드를 여러 스레드에서 병렬로 기록하는 기능은 DX12와 Vulkan에서 켜져 있습니다. 커맨드 리스트마다 전용 얼로케이터나 커맨드 풀을 씁니다.
DX11은 드라이버가 `DriverCommandLists=0` 을 보고하면 실행 중에 이 기능을 끕니다. OpenGL은 구조상 병렬 기록을 하지 않습니다.

스왑체인은 백엔드마다 구체 클래스(`D3D11RHISwapChain`, `D3D12RHISwapChain`, `VulkanRHISwapChain`)이고 디바이스가 소유합니다.
공통 가상 인터페이스는 없고, OpenGL에는 스왑체인 객체가 없습니다.

## 2. 함정

**백버퍼 바인딩은 `beginRenderPass( 핸들 0 )` 으로만 합니다.** `beginFrame` 은 타깃을 바인딩하지 않습니다.
그러니 "`beginFrame` 뒤에 기록한 것은 백버퍼로 간다"처럼 호출 위치에 기대는 코드를 두지 않습니다.
백버퍼 패스를 빠뜨리면 UI가 엉뚱한 타깃과 상태로 그려지고, DX12는 리소스 상태 오류와 DEVICE_HUNG으로 멈춥니다.

**프레임 수명 주기 상태는 `beginFrame` 과 `endFrame` 만 바꿉니다.** Vulkan의 `_bFrameStarted` 가 그 상태입니다.
다른 곳에서 이 값을 끄면 `endFrame` 이 일찍 반환해 프레임 제출이 통째로 사라집니다. 그러면 acquire 세마포어가 신호된 채 남아 다음 프레임이 깨집니다.

**렌더 패스는 플래그가 아니라 실제 `vkCmdEndRenderPass` 로 닫습니다.** 플래그만 끄면 다음 `beginRenderPass` 가 아직 열린 패스 위에 겹칩니다.

**Vulkan 커맨드 리스트는 primary 커맨드 버퍼입니다.** `vkCmdBeginRenderPass` 는 primary 커맨드 버퍼에서만 부를 수 있습니다(`VUID-vkCmdBeginRenderPass-bufferlevel`).
이 엔진은 패스마다 자기 렌더 패스를 여는 구조라서, secondary 커맨드 버퍼와 `vkCmdExecuteCommands` 조합을 쓸 수 없습니다.
그래서 커맨드 리스트는 프레임 스트림을 세그먼트로 잘라 그 사이에 끼워 넣습니다(`VulkanRHICommandList.h`).

**프레임을 넘어 재사용하는 커맨드 리스트는 GPU를 기다리지 않고 얼로케이터를 `Reset` 하면 안 됩니다.** `FrameRenderer::_frameCmd` 가 이런 리스트입니다.
직전 프레임의 커맨드를 GPU가 아직 읽고 있어서 DEVICE_HUNG이 납니다.
DX12의 `beginCommandList` 는 두 번째 기록부터 펜스를 통과한 풀에서 리스트와 얼로케이터 쌍을 새로 꺼내 씁니다. 에디터 경로는 프레임이 느려서 이 결함이 드러나지 않습니다.

**블로킹 대기를 없앨 때는 그 대기가 무엇을 가리고 있었는지 먼저 찾습니다.** 블로킹 제출 아래에서만 맞는 코드가 있습니다.
프레임마다 같은 디스크립터 셋 슬롯을 다시 쓰는 코드, 프레임버퍼나 렌더 패스를 즉시 `vkDestroy` 하는 코드가 그렇습니다. 실행 중인 직전 프레임이 그 객체를 참조하고 있기 때문입니다.
실행 중인 프레임이 참조할 수 있는 디스크립터 셋은 덮어쓰지 않고, 파괴는 해제 큐로 미룹니다.

**백버퍼에 직접 그리는 PSO는 백버퍼 포맷(`getBackBufferFormat()`)을 따라야 합니다.** 파이프라인 리소스의 포맷으로 만들면 Vulkan에서 렌더 패스 호환성 오류가 납니다.
`-gv_rhiBackBufferFormat=1`(B8G8R8A8)로 실행해 확인합니다.

## 3. 검증 방법 — 렌더 패스 테스트만으로는 부족합니다

렌더 패스 GPU 테스트(`RenderPassGpuTest`)는 검증 레이어를 켜고 돌지만 에디터와 ImGui 경로를 지나지 않습니다.
그래서 프레임 순서를 바꿨다면 네 백엔드를 에디터가 있을 때와 없을 때로 나눠 8가지 조합을 실제로 실행해 봅니다.
`AppSmokeTest`(AppTest, `hostgpu` 라벨)가 이 8가지 조합을 실행하고, 종료 코드가 0인지와 로그에 `[Error]` 가 없는지를 검사합니다.
그래서 `ctest -L hostgpu` 가 기본 검증입니다. 직접 눈으로 볼 때는 아래처럼 실행합니다.

```powershell
# 백엔드 4개 × 에디터 유무 2가지 = 8가지 조합을 각각 8초 실행하고 로그의 오류를 센다
.\App.exe -EnableEditor            # 에디터 있음: 그래프가 게임 RT(오프스크린)에 그린다
.\App.exe -EnableEditor -vk
.\App.exe -EnableEditor -dx11
.\App.exe -EnableEditor -gl
.\App.exe                          # 에디터 없음: 그래프가 백버퍼에 직접 그린다
.\App.exe -vk
.\App.exe -dx11
.\App.exe -gl
```

정상이면 다음을 모두 만족합니다.

- 로그에 `[Error]` 가 하나도 없습니다. 하나라도 있으면 회귀입니다.
- Vulkan은 `VUID` 와 `spec states` 문자열이 하나도 없습니다.
- stderr에 `CRASH` 가 없습니다.
- 결과가 실행마다 다를 수 있으므로, 새로 고친 조합은 3회 이상 반복해서 재현율을 봅니다.

**에디터가 있을 때와 없을 때를 반드시 둘 다 봅니다.** 두 경로는 그래프가 그리는 대상이 달라서 사실상 다른 코드 경로입니다.
한쪽만 보면 다른 쪽의 결함을 놓칩니다. 특히 에디터 경로는 프레임이 느려 GPU가 늘 따라잡기 때문에 동기화 결함이 잘 숨습니다.
백엔드마다 `beginFrame` 이 하는 일이 다르므로, 프레임 순서는 한 번에 한 백엔드씩 바꾸고 매번 이 절의 방법으로 검증합니다.
