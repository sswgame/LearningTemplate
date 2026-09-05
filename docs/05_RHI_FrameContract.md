# RHI 프레임/렌더타깃 계약 — 현황 분석과 재설계

> 목적: `RenderThread::executeFrameBody` 의 프레임·오프스크린 순서가 지금은 **백엔드마다 다르게
> 암묵적으로 의존하는 계약**이라 손댈 수 없다. 그 계약을 명시적으로 만들어 Vulkan 병렬 커맨드 기록과
> 오프스크린 GPU 스톨 제거를 가능하게 하는 것이 이 문서의 목표다.
>
> 배경: 2026-09-06 세션에서 이 순서를 바꾸는 시도를 두 번 했고 두 번 다 되돌렸다. 원인은 코드가 아니라
> **계약이 문서화되어 있지 않다는 것**이었다. 실패 기록은 마지막 절에 남긴다.

## 1. 지금의 흐름

`RenderThread::executeFrameBody` (RenderThread.cpp)

```
[오프스크린 경로 — 에디터 게임뷰]              [백버퍼 경로 — 에디터 없이 실행]
beginOffscreenPass(gameRT, clear)              beginFrame(clear)
FrameRenderer::executePacket()                 FrameRenderer::executePacket()
endOffscreenPass(gameRT)
beginFrame(clear)   ← 두 번째 호출
presentHook()  (에디터 UI)                     presentHook()
endFrame()                                     endFrame()
```

## 2. 각 백엔드가 실제로 하는 일

### `beginFrame` — **두 가지 책임이 섞여 있다**

| 백엔드 | ① 프레임 수명주기 | ② 백버퍼 바인딩 |
|---|---|---|
| DX12 | `waitForRingSlot`, 얼로케이터·리스트 `Reset`(미기록 시), `SetDescriptorHeaps` | 백버퍼 → `RENDER_TARGET` 배리어, `OMSetRenderTargets`, `Clear`, 뷰포트/시저 |
| Vulkan | 펜스 대기, `vkAcquireNextImageKHR`, 커맨드 버퍼 reset+begin, 뷰포트/시저 | `vkCmdBeginRenderPass`(스왑체인 RP, `loadOp=CLEAR`) |
| DX11 | 없음 | `OMSetRenderTargets`(백버퍼 RTV) + clear |
| GL | 없음 | `glBindFramebuffer(0)` + clear |

### `beginOffscreenPass` / `endOffscreenPass`

| 백엔드 | begin | end |
|---|---|---|
| DX12 | `ensureRecording()`(**기록을 스스로 시작**), RT→`RENDER_TARGET` 전환, `OMSetRenderTargets(rt)`, `Clear` | RT→`PIXEL_SHADER_RESOURCE` 전환 |
| Vulkan | 오프스크린 **펜스 대기+리셋**, **전용 커맨드 버퍼** reset+begin, RT→`COLOR_ATTACHMENT_OPTIMAL`, 라우팅 플래그 | 렌더패스 종료, RT→`SHADER_READ`, `vkEndCommandBuffer` + **자체 `vkQueueSubmit` + `vkWaitForFences`(블로킹)** |
| DX11 | RT 바인딩 등 상태 설정 | 상태 정리 |
| GL | FBO 바인딩 등 상태 설정 | 상태 정리 |

**Vulkan 만 별도 제출 + 블로킹 대기를 한다.** 이것이 매 프레임 GPU 스톨이자 병렬 기록의 구조적 걸림돌이다.

## 3. 지금 성립하고 있는 암묵 규칙

문서에 없지만 코드가 의존하는 규칙들:

- **R1. `beginFrame` 의 수명주기 부분은 멱등이어야 한다.** 오프스크린 경로에서 두 번째로 불릴 때,
  DX12 는 `_bRecording != 0` 이라 `Reset` 을 건너뛴다. 이 멱등성이 깨지면 오프스크린 기록이 날아간다.
- **R2. 백버퍼를 (재)바인딩하는 유일한 수단이 `beginFrame` 이다.** 오프스크린 경로의 두 번째
  `beginFrame` 호출은 사실 "프레임 시작"이 아니라 **"UI 를 그리기 위해 백버퍼를 다시 바인딩+클리어"**
  가 목적이다. RHI 에 그 뜻을 표현하는 다른 수단이 없어서 `beginFrame` 을 재사용하고 있다.
- **R3. `beginOffscreenPass` 는 기록이 시작 안 됐으면 시작해도 된다.** (DX12 `ensureRecording`,
  Vulkan 은 자기 버퍼를 연다.)
- **R4. 오프스크린 작업과 UI 작업이 같은 제출에 들어가는지는 백엔드마다 다르다.** Vulkan 만 분리
  제출이고 나머지는 같은 스트림이다.

R2 가 이번 실패의 핵심이었다. `beginFrame` 을 앞으로 옮기자 **오프스크린 이후 백버퍼를 재바인딩하는
주체가 사라져** DX12 에서 리소스 상태 오류 758건과 DEVICE_HUNG 이 터졌다.

## 4. 재설계 — 두 책임을 분리한다

핵심 통찰: **렌더타깃 바인딩을 표현하는 수단은 이미 있다.** `IRHICommandList::beginRenderPass(
RHIRenderPassBeginInfo )` 는 컬러 타깃 배열·로드 op·클리어 값을 받고, 타깃 핸들 `0` 은 이미 백버퍼로
해석된다(D3D12RHICommandContext 의 렌더패스 처리 참고). 실제로 `FrameRenderer` 의 Present 패스는
**이미** 이 API 로 게임 RT(`_outputRenderTarget`)를 타깃으로 렌더패스를 연다.

즉 `beginOffscreenPass`/`endOffscreenPass` 와 "두 번째 `beginFrame`" 은 모두 이 API 로 표현 가능한
레거시 스캐폴드다. 셰이더 읽기 전환도 `IRHICommandList::prepareTextureForShaderRead` 가 이미 있다.

### 목표 계약

| 연산 | 의미 | 호출 횟수 |
|---|---|---|
| `IRHISwapChain::beginFrame()` | **수명주기 전용** — 스왑체인 이미지 획득, 기록 시작 | 프레임당 정확히 1회, 맨 앞 |
| `IRHICommandList::beginRenderPass(info)` | **렌더타깃 바인딩 전용** — 타깃·로드 op·클리어 | 필요한 만큼 |
| `IRHICommandList::prepareTextureForShaderRead(tex)` | 샘플링용 레이아웃/상태 전환 | 필요한 만큼 |
| `IRHISwapChain::endFrame()` | 제출 + 프레젠트 | 프레임당 1회 |

`beginOffscreenPass` / `endOffscreenPass` 는 **삭제한다.**

### 목표 흐름

```
swapChain->beginFrame()                        # 수명주기만
FrameRenderer::executePacket()                 # 그래프가 자기 리스트에 기록,
                                               # Present 패스가 gameRT(또는 백버퍼)를 타깃으로 렌더패스
if (offscreen) cmd->prepareTextureForShaderRead(gameRT)
presentHook()                                  # UI: beginRenderPass(백버퍼, Load) … endRenderPass
swapChain->endFrame()
```

**부수 효과(이득)**: Vulkan 의 오프스크린 별도 제출 + 펜스 블로킹이 사라진다. 순서는 큐 순서와
`prepareTextureForShaderRead` 배리어로 보장되므로 정확성은 유지되면서 매 프레임 GPU 스톨이 없어진다.
스트림이 하나로 줄어 Vulkan 병렬 커맨드 기록도 단순한 단일 스트림 설계로 가능해진다.

## 5. 이행 단계 (각 단계는 독립적으로 검증 가능해야 한다)

| 단계 | 내용 | 위험도 |
|---|---|---|
| **S1** | 백버퍼 바인딩을 명시화. 에디터 UI 경로(`presentHook`)가 두 번째 `beginFrame` 에 의존하지 않고 `beginRenderPass(백버퍼, Load)` 를 직접 열도록 변경. 나머지는 그대로 둔다 | **높음** — R2 를 바꾸는 지점 |
| **S2** | `beginFrame` 에서 백버퍼 바인딩/클리어 제거(수명주기만 남김). 클리어는 첫 백버퍼 `beginRenderPass` 의 `loadOp=Clear` 로 이동 | 높음 |
| **S3** | `beginOffscreenPass`/`endOffscreenPass` 호출부를 `beginRenderPass` + `prepareTextureForShaderRead` 로 교체하고 인터페이스와 4개 백엔드 구현에서 삭제. **Vulkan 블로킹 제출도 여기서 사라진다** | 중간 |
| **S4** | Vulkan 리스트가 자기 `VkCommandBuffer`/커맨드 풀 소유, 단일 스트림 세그먼트 제출, `_bParallelCommandRecording=1` | 중간 |

S1·S2 가 가장 위험하다. 백엔드마다 `beginFrame` 이 하는 일이 다르므로(위 표) **한 단계에 한 백엔드씩**
바꾸고 매번 검증하는 편이 안전하다.

## 6. 검증 프로토콜 — **테스트만으로는 절대 부족하다**

2026-09-06 세션에서 두 번 다 **`RenderPassTest` 22개가 validation layer 켠 채로 전부 통과했는데
실기에서 즉시 깨졌다.** 테스트는 에디터/ImGui 경로를 타지 않기 때문이다.

각 단계마다 4개 백엔드를 실제로 띄워 확인할 것:

```powershell
# 백엔드별로 7초 띄우고 로그의 오류를 센다
.\App.exe -EnableEditor            # DX12(기본)
.\App.exe -EnableEditor -vulkan
.\App.exe -EnableEditor -dx11
.\App.exe -EnableEditor -gl
```

- **정상 기준선 = `[Error]` 3건** (전부 `Config/*.json` 없음 안내). 그 이상이면 회귀다.
- Vulkan 은 `VUID` / `spec states` 문자열이 0건이어야 한다.
- stderr 에 `CRASH` 가 없어야 한다.

## 7. 실패 기록 (같은 실수 반복 방지)

**1차** — Vulkan 리스트에 secondary 커맨드 버퍼를 주고 `vkCmdExecuteCommands` 로 끼우려 했다.
`vkCmdBeginRenderPass` 는 **primary 전용**이라(`VUID-vkCmdBeginRenderPass-bufferlevel`) 패스마다 자기
렌더패스를 여는 이 엔진 구조에서는 성립하지 않는다. → primary + 프레임 버퍼 세그먼트 분할로 선회.

**2차** — 세그먼트 분할이 프레임 버퍼 하나만 가정했는데, Vulkan 은 오프스크린이 **별도 스트림**이라
게임뷰 렌더 중에는 엉뚱한 버퍼에 `vkCmdEndRenderPass` 를 날렸다. 이어서 오프스크린을 프레임 버퍼에
합류시키려 `executeFrameBody` 순서를 바꿨더니 **R2 위반으로 DX12 가 깨졌다**(오류 881건).

두 번 다 원인은 "코드를 잘못 옮긴 것"이 아니라 **계약을 모르고 옮긴 것**이었다. 그래서 이 문서가 있다.
