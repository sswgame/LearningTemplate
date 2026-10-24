# 그래픽스 후보

> 상태: 후보 · 선행: 없음 · 영역: graphics · 옛번호: E-3, E-13 · 진행: 해당없음

이 문서는 그래픽스 · RHI · 셰이더 영역에서 착수 전인 후보의 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다.

## 관찰

### VulkanVsyncFrameMiss
제목: Vulkan VSync 에서 프레임이 주기를 놓침
옛번호: E-3
왜 필요한가: Vulkan(FIFO) + VSync 에서 가끔 한 프레임이 여러 주기를 놓친다(Release 큐브 100, 165 Hz). 조사 결과는 [검증과 측정](../08_Verification.md) 2절 "Vulkan VSync 프레임 놓침", 스왑체인 이미지 4 개는 결정 기록 3절에서 기각했다.
착수 조건 / 남은 것: 다시 보이면 `-gv_tracy=1` 로 acquire · 펜스 · present 를 시간축으로 보고, 전체 화면(독점) · NVIDIA "Vulkan/OpenGL 프레젠트 방식" 설정을 바꿔 가른다.
왜 지금 못 하는가: 가끔만 일어나고 모니터 주사율 · 드라이버 설정에 달려 있어 실기에서 다시 보여야 한다(재현 시나리오가 없다). 풀리는 조건: 실기 재현.

### DeviceLossHangRepro
제목: 실제 TDR 뒤 행을 재현하지 못함
옛번호: E-13, A-2
왜 필요한가: `rhi.removeDevice` 로 DX12(장치 제거) · Vulkan(손실 상태로 둠) 모두 프레임을 계속 돌고 종료 코드 0 으로 끝난다(2026-10-15 Vulkan 런타임 3/3 · 에디터 2/2, DX12 1/1). 남은 것은 드라이버 안의 대기(첫 `VK_ERROR_DEVICE_LOST` 를 돌려주기 전)뿐이다.
착수 조건 / 남은 것: 다시 보이면 `py -3 -m Scripts stacks <pid>` 로 매달린 스레드의 스택을 남긴다.
왜 지금 못 하는가: 진짜 TDR 은 GPU 를 2 초 넘게 붙잡는 컴퓨트로 일으킬 수 있으나 화면이 깨지고 쿠킹한 셰이더가 하나 더 필요해, 실기에서 사용자가 승인한 때만 시험한다. 풀리는 조건: 실기 TDR 재현 또는 그 승인.

### ShaderPrefixAndFormat
제목: 셰이더 이름의 sw 접두를 없애고 clang-format 을 셰이더에도 건다
옛번호: 없음
왜 필요한가: (1) 공유 헤더 함수 · 타입의 `sw` / `Sw` 접두는 FXC(DX11 전용 — `ShaderCompiler.cpp`) 에 네임스페이스가 없어서 붙였다. 이름이 겹치면 셰이더 컴파일 오류로 멈추므로(오버로드만 조용히 공존) 접두의 이득이 작다. `SW_` 매크로 접두와 `g_` 전역은 매크로가 조용히 덮이고 C++ 이 이름으로 묶으므로 그대로 둔다. DX11 백엔드를 접으면 `sw::` 네임스페이스도 쓸 수 있다(결정 필요). (2) 셰이더 포맷은 1단계로 끝났다(아래 "1단계 끝" 줄).
착수 조건 / 남은 것: 2단계 — `sw` 접두 제거 · 구조체 멤버 / `static` 멤버 함수로 묶기 · 구조체 필드 `_camelCase` · 함수 안 `#if` 를 정의로 몰기. 접두 제거는 `CheckShaderConventions.py` 와 [NamingScripts](../agents/NamingScripts.md) HLSL 절을 같은 커밋에서 고치고 C++ 이 이름으로 묶는 함수가 없는지 확인한다. 셰이더를 고친 뒤 `FormatModified.py` 로 포맷한다(커밋 훅이 staged 셰이더를 clang-format 으로 검사한다).
결정(사용자, 2026-10-23): 앞으로 셰이더에도 `.clang-format` 을 적용한다. 시험으로 찾은 설정(20.1.8) — 기존 `.clang-format` 에 `MacroBlockBegin: "^SW_ROOT_CONSTANTS_BEGIN$"` · `MacroBlockEnd: "^SW_ROOT_CONSTANTS_END$"`(`SW_MATERIAL_*` 는 넣지 않는다, 안의 `{ }` 가 이중 들여쓰기된다), `AlignConsecutiveDeclarations: {Enabled: true, AlignFunctionDeclarations: false}`, `StatementMacros: [SW_NUMTHREADS]` + 셰이더의 `[numthreads(…)]` 13곳(12 파일)을 `SW_NUMTHREADS(…)` 매크로로(`common.hlsli` 에 정의). 안 되는 것: `SW_ROOT_CONSTANTS_BEGIN` 한 줄 매크로가 여러 줄로 갈리고 `SW_DECLARE_*` 의 `\` 가 길게 정렬된다(cbuffer 의 `{` 를 함수 중괄호로 보기 때문 — `AfterFunction: false` 면 해결되나 모든 함수가 붙는다). 폐기한 안: `#endif` 주석 자동 달기, 매크로 구조 변경, 매크로 헤더만 `clang-format off`. 포맷한 뒤 포맷터를 다시 돌려 같은 결과인지(멱등) 확인한다.
1단계 끝(2026-10-23): 셰이더 60 개에 clang-format 을 건다. 설정은 `Resource/.clang-format`(루트를 물려받고 위 넷에 `AttributeMacros: [SW_GL_BINDING]` — `SW_DECLARE_*` 본문이 줄바꿈되지 않아 `\` 정렬 문제가 사라진다 — 와 `BreakConstructorInitializers: BeforeColon` — 반환값 시맨틱 `) : SV_TARGET` 이 다음 줄로 가지 않는다 — 을 더함). 루트에 두지 않은 까닭은 `AlignFunctionDeclarations: false` 가 C++ 정렬을 바꾸기 때문이고, C++ 4767 개는 한 줄도 바뀌지 않았다. `FormatClangFormat` · `FormatModified` · 커밋 훅이 셰이더를 대상에 넣었고(C++ 픽서는 셰이더에 걸지 않는다), 두 번째 포맷은 바뀌는 파일이 0 이다. 쿠킹 바이너리 9003 개는 포맷 전후 바이트가 같다(`cook.stamp` 만 바뀜). 남은 한계: `SW_ROOT_CONSTANTS_BEGIN/END` 정의는 여러 줄로 갈린다(설정으로 푸는 길이 없고 매크로 구조 변경은 폐기한 안이다). `[unroll]` · `[loop]` · `[branch]` 는 다음 문장과 한 줄로 붙고(18 곳), 중괄호 없는 `[branch] if … else` 는 `else` 가 앞줄에 붙어 `fullscreenblit.hlsl` 에 중괄호를 뒀다 — 언리얼 `UNROLL` · `BRANCH` 같은 문장 매크로(`StatementMacros`)로 바꾸면 풀린다(2단계에서 정한다).
결정(사용자, 2026-10-23): 구조체 필드도 C++ 처럼 `_camelCase` 로 한다(C++ 순수 데이터 구조체도 `_` 를 쓰므로 일관성, 거울 필드는 이름이 같아져 "앞의 `_` 를 뗀다" 변환 규칙이 사라진다). 문자열로 묶인 필드(머티리얼 구조체 — `.material` 과 리플렉션)는 에셋 · C++ 바인딩을 같은 커밋에서 바꾼다. `sw` 접두 제거와 한 묶음으로, `CheckShaderConventions.py` 와 [NamingScripts](../agents/NamingScripts.md) HLSL 절을 같이 고친다. 필드 수는 아직 세지 않았다.
결정(사용자, 2026-10-23): `sw` 접두 대신 **구조체로 묶는다**. 구조체를 첫 인자로 받던 함수는 그 구조체의 멤버 함수(`context.computeLit( albedo )`), 구조체 없이 쓰거나 만드는 함수는 `static` 멤버 함수(`PixelContext::make( … )`, `View::getViewProjection()`). 별칭 매크로(`SW_MATERIAL_VIEW_PROJECTION` 류)는 함수로 바꾼다. 시험(스크래치 최소 셰이더): 멤버 · static 멤버 함수와 안에서 전역 cbuffer 읽기는 FXC `ps_5_0` · DXC `ps_6_6` · SPIR-V 모두 컴파일된다(실행 결과와 OpenGL 경로는 못 봄). `public` / `private` 은 예약어라 되지 않고 `const` 멤버 함수도 안 된다 — 접근을 막는 린트는 두지 않는다(어차피 struct 라는 걸 안다, 사용자 결정). 구조체에 리소스(텍스처 · 샘플러)를 넣는 것은 시험하지 않았다.
결정(사용자, 2026-10-23): DX11 은 당분간 지원한다(접는 안은 묻지 않는다) — FXC 경로가 남으므로 `SW_ROOT` 같은 백엔드 차이 매크로는 지금 못 없앤다. 함수 안 `#if` 는 정의로 몰아 호출 쪽 본문에서 없앤다(`surface.applyAlphaCutoff()` · `surface.store( context )` 꼴 — 설계 예시이고 컴파일하지 않았다). `IndentPPDirectives: BeforeHash` 는 바꾸지 않는다(C++ · 셰이더 같은 모양 유지; `None` 은 C++ 400 개 중 61 개를 바꾸고 `Leave` 는 고정판 20.1.8 이 못 읽으며 `PPIndentWidth: 2` 는 3중첩에서 `#` 줄이 코드와 겹친다). `#endif` 주석 자동 달기는 폐기했다.

## 조건부
