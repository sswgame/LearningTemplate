# 그래픽스 후보

> 상태: 후보 · 선행: 없음 · 영역: graphics · 옛번호: E-3, E-13 · 진행: 해당없음

이 문서는 그래픽스 · RHI · 셰이더 영역에서 착수 전인 후보의 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다.

## 관찰

### PostEffectBypass
제목: 꺼진 후처리 효과가 전 화면 패스와 타깃 비용을 낸다
옛번호: 없음
왜 필요한가: 렌즈 없는 카메라(조리개 0)와 서 있는 카메라에서도 피사계 심도 · 모션 블러가 전 화면을 한 번씩 읽고 써서(셰이더가 원본을 고르거나 원본을 복사) Empty `GPU.Frame` 이 468 → 1089 us 가 됐고, 두 출력 타깃(RGBA16F)이 늘 메모리를 차지했다(GPU 메모리 56.8 → 176.9 MB, `renderbench` Native 1600×900 기준선 커밋 `14c775c55`). 블룸 · 외곽선 · SSAO 도 꺼진 뷰(후처리 끔 · 품질 미달)에서 원본을 복사하거나 셰이더로 옮기거나 타깃을 지웠다.
끝난 것(wt/renderbase): 꺼질 수 있는 효과(`kSkipsWithoutPostProcess`)는 빠지는 방법이 정확히 하나다 — 블룸 · 외곽선 · 피사계 심도 · 모션 블러는 출력 이름이 원본에 이어지고(`kBypassesToSource`, `TransientAttachmentPool::forward`), 원본이 없는 SSAO 는 출력이 없어(`kOmittedWhenOff`, `markAbsent`) 읽는 쪽이 AO 역할을 걸지 않고 셰이더 기본값 1 을 쓴다. 셋업(`FrameRenderer::applyViewPassSelection`, 뷰마다 프레임에 한 번)이 패스를 뒤에서부터 보며 고르고, 읽는 패스가 모두 빠진 풀스크린 패스(블룸이 꺼진 뷰의 BloomDownsample)도 뺀다. 일반 풀스크린 패스만 쓰는 첨부는 풀을 만들 때 만들지 않고 그 패스가 처음 돌 때 만든다. C++ 의 원본 복사 · 셰이더 원본 고르기 · 지우기 길은 지웠다. 디퍼드 OutlineColor 를 RGBA8 → RGBA16F 로 맞췄다 — 외곽선은 톤맵 앞 HDR 색에 그리므로 RGBA8 은 필요 없었고 1 이 넘는 색을 TAA · 톤맵 앞에서 잘랐다. 모션 블러의 "뷰-투영이 지난번과 같으면 끔" 은 셰이더가 카메라 움직임만 쓰기 때문이다(`motionblur.hlsl` 이 깊이와 `g_PrevViewProj` 만 읽고, 렌더러에 물체 속도 버퍼 · 속도 패스가 없다). 로드 검증 3-6(출력 · 원본의 형식 · 나눗수, 출력을 쓰는 패스 하나, 뒤 패스가 원본을 다시 쓰지 않음)과 3-7(SSAO 출력은 선택 입력으로만 읽힘)을 더했다. 주 시점 첨부마다의 크기를 `[RenderTargets]` Info 줄로 남긴다.
재측정(wt/rgprune, 2026-10-27, 코드 읽기): 감사 SB R1 이 말한 패스스루(`FrameRendererPassExecute.cpp` 의 블릿 · `SW_PASS_FLAG_SKIP_POST` 길, 상수의 조리개 0 분기)는 renderbase 가 다 지웠다. 그래프를 뷰 상태 키로 다시 컴파일해 캐시하는 길은 만들지 않았다 — 출력 이름 잇기가 같은 결과(빠진 패스 · 원본을 읽는 소비자)를 컴파일 없이 내고, 결정 5-5 가 기각했다. 남은 틈은 뷰 상태가 바뀌는 프레임의 TAA 기록이었다: 후처리를 끈 프레임의 TAA 는 기록을 쓰지 않는데 다시 켠 첫 프레임이 끄기 전 기록을 섞었다. `beginViewHistory` 가 `_bTaaHistoryReset`(컷이거나 지난 프레임이 기록을 쓰지 않음)을 같은 자리에서 정하고 `executeTaaPass` 가 그것을 본다. 피사계 심도 · 모션 블러는 TAA 뒤라 켜고 꺼도 기록과 무관하다. 테스트: `RenderPassGPUTest.DepthOfFieldToggleForwardsAndKeepsTaaHistory`(조리개 0 → 1.4 → 0 에서 출력이 TaaColor 를 가리키다 자기 타깃에 흐린 그림을 그리고 다시 가리킴, TaaColor 는 그대로) · `PostProcessReenableResetsTaaHistory`(껐다 켠 프레임의 TaaColor = 원본). 측정 기대치(통합이 볼 것): Empty `GPU.Frame` 은 468 us 근처, `GPUPassTimeline` 의 DepthOfField · MotionBlur 칸은 렌즈 없는 서 있는 카메라에서 빈다(패스가 기록되지 않는다), `gpu_memory_mb` 는 두 RGBA16F 타깃(1600×900 에서 23 MB)만큼 준다.
메모리 추정(실행 안 함): 1600×900 RGBA16F 하나가 11.5 MB 라 포워드에서 두 타깃을 빼면 23 MB 가 준다. 같은 기간에 장면 색이 RGBA8 → RGBA16F(+5.8 MB), 깊이 히스토리 R32(+5.8 MB)가 더해졌다. 디퍼드는 OutlineColor 가 +5.8 MB 늘고 꺼진 뷰에서 블룸 · 외곽선 · SSAO · BloomDownsample 타깃이 빠진다. 나머지 약 85 MB 는 렌더 타깃 목록 밖(버퍼 · 프로브 · 볼륨 안개 등)일 수 있어 `[Profile] GPU memory by kind` 의 종류별 줄과 `[RenderTargets]` 줄을 함께 보고 가른다.
남은 것(통합): 다섯 셰이더 진입점의 `SW_PASS_FLAG_SKIP_POST` 분기가 이제 부르지 않는 길이다 — `depthoffield.hlsl`(`bSkipPost` · `g_CameraLens.w` 고르기를 지우고 `colorSum / weightSum` 을 낸다), `motionblur.hlsl`(`bSkipPost` 를 지우고 `bStill` 만 남긴다), `postbloom.hlsl` 두 진입점, `postoutline.hlsl`. 셰이더를 고친 커밋은 훅이 쿠킹한 바이너리를 함께 요구하고 작업자는 빌드 · 실행을 못 해 넣지 못했다. 통합이 지우고 `App --cook-shaders` 로 굽고 `ShaderCookRequestTest` 가 알리는 낡은 바이너리를 지운다(`#if` 키워드를 바꾸지 않아 퍼뮤테이션 목록은 같다). `postchain` · `tonemap` 의 분기는 Present 쪽이라 남는다. 빌드 · `RenderPassTest` · `TransientAttachmentPoolTest` · `RenderPassGPUTest` · 모션 블러 시나리오(`game/empty/automation/motionblur.scenario.xml`, 움직인 프레임이 여전히 달라야 한다) 네 백엔드. 디퍼드 골든은 OutlineColor HDR 화로 밝은 곳이 달라지므로 바뀐 그림을 보고 다시 뜬다. 그다음 기준을 새로 뜬다 — 숨기기 위한 재기록이 아니라 이 변경을 넣은 뒤의 값이다. `perf-frame` 은 `py -3 -m Scripts perf --app <app> --record`, `load-baseline-suite` 는 `py -3 -m Scripts load suite baseline --runs 3 --record`, `load-auto-render-scale` 은 LowEnd `renderbench` 를 `--app-arg=-gv_autoRenderScale=1` 로 `--record`. 뜨기 전에 `GPU.Frame` 이 468 us 근처로 돌아왔는지와 `gpu_memory_mb` 를 종류별로 나눈 표를 결과 문서에 남긴다(돌아오지 않은 몫은 원인을 적고 뜬다).
같은 결함 후보: TAA 는 후처리를 끈 뷰(CCTV)에서도 원본을 TaaColor 로 복사한다(형식이 같으면 블릿, 다르면 기록 슬롯에 원본을 건 TAA 셰이더 — 전 화면 한 번). TAA 출력도 원본에 이으면 없어지지만 TAA 는 효과 표(`kSkipsWithoutPostProcess`)가 아니고 셋업이 TaaColor 형식으로 기록을 만들어 검증 3-6 을 TAA 에도 걸어야 한다. 한 번 만든 효과 타깃은 효과를 다시 꺼도 풀을 다시 만들 때까지 남는다(에디터에서 조리개를 열었다 닫으면 11.5 MB). 포워드 Present 의 합친 체인(`postchain`)은 후처리를 끈 뷰에서 셰이더가 블룸 · 외곽선 · 톤맵을 건너뛰지만 한 패스라 빠질 것이 없다.

### VulkanVsyncFrameMiss
제목: Vulkan VSync 에서 프레임이 주기를 놓침
옛번호: E-3
왜 필요한가: Vulkan(FIFO) + VSync 에서 가끔 한 프레임이 여러 주기를 놓친다(Release 큐브 100, 165 Hz). 조사 결과는 [검증과 측정](../08_Verification.md) 2절 "Vulkan VSync 프레임 놓침", 스왑체인 이미지 4 개는 결정 기록 3절에서 기각했다.
착수 조건 / 남은 것: 다시 보이면 `-gv_tracy=1` 로 acquire · 펜스 · present 를 시간축으로 보고 전체 화면(독점) · NVIDIA "Vulkan/OpenGL 프레젠트 방식" 설정을 바꿔 가른다.
왜 지금 못 하는가: 가끔만 일어나고 모니터 주사율 · 드라이버 설정에 달려 있어 재현 시나리오가 없다. 풀리는 조건: 실기 재현.

### DeviceLossHangRepro
제목: 실제 TDR 뒤 행을 재현하지 못함
옛번호: E-13, A-2
왜 필요한가: `rhi.removeDevice` 로 DX12(장치 제거) · Vulkan(손실 상태로 둠) 모두 프레임을 계속 돌고 종료 코드 0 으로 끝난다. 남은 것은 드라이버 안의 대기(첫 `VK_ERROR_DEVICE_LOST` 를 돌려주기 전)뿐이다.
착수 조건 / 남은 것: 다시 보이면 `py -3 -m Scripts stacks <pid>` 로 매달린 스레드의 스택을 남긴다.
왜 지금 못 하는가: 진짜 TDR 은 GPU 를 2 초 넘게 붙잡는 컴퓨트로 일으킬 수 있으나 화면이 깨지고 쿠킹한 셰이더가 하나 더 필요해 실기에서 사용자가 승인한 때만 시험한다. 풀리는 조건: 실기 TDR 재현 또는 그 승인.

### ShaderPrefixAndFormat
제목: 셰이더 이름의 sw 접두를 없애고 clang-format 을 셰이더에도 건다
옛번호: 없음
끝난 것: (1) 셰이더 60 개에 clang-format 을 건다(설정 `Resource/.clang-format` — 루트를 물려받고 `MacroBlockBegin/End`(`SW_ROOT_CONSTANTS_*`), `AlignConsecutiveDeclarations`, `StatementMacros: [SW_NUMTHREADS]`, `AttributeMacros: [SW_GL_BINDING]`, `BreakConstructorInitializers: BeforeColon`). 루트에 두지 않은 까닭은 `AlignFunctionDeclarations: false` 가 C++ 정렬을 바꾸기 때문. `FormatClangFormat` · `FormatModified` · 커밋 훅이 셰이더를 대상에 넣었고 두 번째 포맷은 바뀌는 파일이 0 이다. 남은 한계: `SW_ROOT_CONSTANTS_BEGIN/END` 정의가 여러 줄로 갈리고(매크로 구조 변경은 폐기), `[unroll]` · `[loop]` · `[branch]` 는 다음 문장과 붙는다(문장 매크로 `UNROLL` · `BRANCH` 로 바꾸면 풀린다). (2) 구조체 필드 261 개를 `_camelCase` 로 바꿨다(셰이더 46 개, 머티리얼 구조체 필드 68 이름은 `.material` 75 개와 C++ 문자열 · `customization.xml` · 테스트를 같은 커밋에서). 함수 본문 안의 `#if` 를 정의로 몰았다(남은 본문 `#if` 는 백엔드 매크로 `DX11` · `DX12` · `__spirv__` · `SW_NATIVE_BINDLESS` 뿐이고 게이트가 그 밖의 조건을 막는다). `CheckShaderConventions.py` 와 [NamingScripts](../agents/NamingScripts.md) HLSL 절을 같은 커밋에서 고쳤다. 셰이더 컴파일 · 쿠킹은 네 백엔드로 했다.
남은 것(통합 검증): 새로 빌드한 Release App 으로 쿠킹이 같은 결과인지(필드 · 프로퍼티 이름이 바뀌어 리플렉션이 달라진다), 게임별 골든(`Scripts golden`), `MaterialTest` · `ShaderBindingValidatorTest` · `RenderPassGPUTest`. 에디터 머티리얼 패널은 `displayName` 이 없는 프로퍼티에 `_color` 처럼 밑줄 이름을 그대로 보인다. 자원을 가리키는 매크로 별칭 셋은 아직 매크로다 — `SW_MATERIAL( index )`(원소 형식이 셰이더마다 달라 함수 반환형을 못 적는다), `SW_SAMPLER_STATE`(백엔드마다 배열 · 개별 변수), `SW_APPEARANCE_*`(필드 유무가 퍼뮤테이션). 셰이더를 고친 뒤 `FormatModified.py` 로 포맷한다.
결정(사용자, 2026-10-23):
- 셰이더에도 `.clang-format` 을 적용한다. 폐기한 안: `#endif` 주석 자동 달기, 매크로 구조 변경, 매크로 헤더만 `clang-format off`. 포맷한 뒤 다시 돌려 멱등을 확인한다.
- `sw` 접두(FXC 에 네임스페이스가 없어 붙였던 것)는 **구조체로 묶어** 없앤다: 구조체를 첫 인자로 받던 함수는 멤버 함수(`context.computeLit( albedo )`), 구조체 없이 쓰거나 만드는 함수는 `static` 멤버 함수(`PixelContext::make( … )`, `View::getViewProjection()`). 멤버 · static 멤버 함수와 안에서 전역 cbuffer 읽기는 FXC `ps_5_0` · DXC `ps_6_6` · SPIR-V 모두 컴파일된다(실행 결과와 OpenGL 경로는 못 봄). `public` / `private` / `const` 멤버 함수는 안 되고 접근을 막는 린트는 두지 않는다. `SW_` 매크로 접두와 `g_` 전역은 그대로 둔다.
- 구조체 필드도 C++ 처럼 `_camelCase`(거울 필드는 이름이 같아져 변환 규칙이 사라진다).
- DX11 은 당분간 지원한다 — FXC 경로가 남으므로 `SW_ROOT` 같은 백엔드 차이 매크로는 지금 못 없앤다. 함수 안 `#if` 는 정의로 몰아 호출 쪽 본문에서 없앤다. `IndentPPDirectives: BeforeHash` 는 바꾸지 않는다(`None` 은 C++ 400 개 중 61 개를 바꾸고 `Leave` 는 20.1.8 이 못 읽으며 `PPIndentWidth: 2` 는 3중첩에서 `#` 줄이 코드와 겹친다).

## 조건부

### R8TextureSampling
제목: R8 텍스처를 네 백엔드에서 샘플링
옛번호: G-12
왜 필요한가: `App --import-textures` 가 1 채널 `R8_UNORM` DDS 를 내고 로더가 읽지만(결정 기록 5절 "텍스처 임포트의 형식 이름은") 실제 샘플링은 네 백엔드에서 본 적이 없습니다.
착수 조건 / 남은 것: 규칙 표의 `Mask_Textures`(`*_mask.*`)에 맞는 원본이 생기면 그 게임의 시나리오가 봅니다.
왜 지금 못 하는가: 맞는 원본이 아직 없습니다. 확인 대기: 매니페스트 id `r8-texture-sampling`.
