# 그래픽스 후보

> 상태: 후보 · 선행: 없음 · 영역: graphics · 옛번호: E-3, E-13 · 진행: 해당없음

이 문서는 그래픽스 · RHI · 셰이더 영역에서 착수 전인 후보의 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다.

## 관찰

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
