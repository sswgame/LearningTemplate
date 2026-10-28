# 그래픽스 후보

> 상태: 후보 · 선행: 없음 · 영역: graphics · 진행: 해당없음

그래픽스 · RHI · 셰이더 영역에서 아직 안 한 일의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다.

## 한눈에 보기

| 절 | 한 줄 설명 | 착수 조건 |
|---|---|---|
| ShaderPrefixAndFormat | 셰이더 sw 접두 제거 · clang-format 적용의 남은 소스 커밋과 쿠킹 | 바로 착수 가능 |
| VfxMultiGroupCompaction | GPU 방출기 다중 그룹 압축 | 측정 없이 착수 가능 |
| SpanAllocatorVatSkinPools | 스킨 원본 · VAT 풀의 구간 할당기 | 측정 없이 착수 가능 |

## 정리

### ShaderPrefixAndFormat
제목: 셰이더 이름의 sw 접두를 없애고 clang-format 을 셰이더에도 건다
왜 필요한가: 셰이더 60 개에 clang-format 을 걸고 구조체 필드 261 개를 `_camelCase` 로 바꾸는 일은 끝났다. 셰이더 소스 일부는 커밋 훅이 쿠킹 바이너리를 요구해 커밋하지 못했다.
착수 조건: 없음.
남은 첫 일: `probeupdate.hlsl`(맨 `[unroll]` 열 곳을 `SW_UNROLL` 로 바꾼 것)과 후처리 우회 작업의 다섯 진입점 변경을 넣고 `App --cook-shaders` 로 네 백엔드를 굽는다. 그 전에는 전체 `CheckShaderConventions` 가 `probeupdate.hlsl` 에서 진다. 이어 자원을 가리키는 매크로 별칭 셋을 함수로 바꿀 길을 본다 — `SW_MATERIAL( index )`(원소 형식이 셰이더마다 달라 함수 반환형을 못 적는다), `SW_SAMPLER_STATE`(백엔드마다 배열 · 개별 변수), `SW_APPEARANCE_*`(필드 유무가 퍼뮤테이션). 에디터 머티리얼 패널은 `displayName` 이 없는 프로퍼티에 `_color` 처럼 밑줄 이름을 그대로 보인다. 셰이더를 고친 뒤 `FormatModified.py` 로 포맷한다.
함정: 루트 `.clang-format` 이 아니라 `Resource/.clang-format` 을 쓴다(`AlignFunctionDeclarations: false` 가 C++ 정렬을 바꾼다). `SW_ROOT_CONSTANTS_BEGIN/END` 정의는 여러 줄로 갈린다(매크로 구조 변경은 폐기). `CheckShaderConventions` 가 문장 머리의 맨 `[unroll]` · `[loop]` · `[branch]` · `[flatten]` 을 막는다.
결정(사용자, 2026-10-23):
- 셰이더에도 `.clang-format` 을 적용한다. 폐기한 안: `#endif` 주석 자동 달기, 매크로 구조 변경, 매크로 헤더만 `clang-format off`. 포맷한 뒤 다시 돌려 멱등을 확인한다.
- `sw` 접두(FXC 에 네임스페이스가 없어 붙였던 것)는 **구조체로 묶어** 없앤다: 구조체를 첫 인자로 받던 함수는 멤버 함수(`context.computeLit( albedo )`), 구조체 없이 쓰거나 만드는 함수는 `static` 멤버 함수(`PixelContext::make( … )`, `View::getViewProjection()`). 멤버 · static 멤버 함수와 안에서 전역 cbuffer 읽기는 FXC `ps_5_0` · DXC `ps_6_6` · SPIR-V 모두 컴파일된다(실행 결과와 OpenGL 경로는 못 봄). `public` / `private` / `const` 멤버 함수는 안 되고 접근을 막는 린트는 두지 않는다. `SW_` 매크로 접두와 `g_` 전역은 그대로 둔다.
- 구조체 필드도 C++ 처럼 `_camelCase`(거울 필드는 이름이 같아져 변환 규칙이 사라진다).
- DX11 은 당분간 지원한다 — FXC 경로가 남으므로 `SW_ROOT` 같은 백엔드 차이 매크로는 지금 못 없앤다. 함수 안 `#if` 는 정의로 몰아 호출 쪽 본문에서 없앤다. `IndentPPDirectives: BeforeHash` 는 바꾸지 않는다(`None` 은 C++ 400 개 중 61 개를 바꾸고 `Leave` 는 20.1.8 이 못 읽으며 `PPIndentWidth: 2` 는 3중첩에서 `#` 줄이 코드와 겹친다).

## 조건부

### VfxMultiGroupCompaction
제목: GPU 방출기 다중 그룹 압축
왜 필요한가: GPU 시뮬레이션은 방출기 하나를 그룹 하나(256 스레드)가 돌아 풀이 수만 개면 느리다. 풀이 크면 디스패치 셋으로 나눈다: (1) 그룹마다 생성 · 적분 · 생존 표시와 개수, (2) 개수의 접두사 합(그룹 하나), (3) 그룹마다 자기 시작 위치로 안정 압축. 단계마다 셋이라 여러 단계를 한 디스패치에 못 넣는다.
착수 조건: 측정 없이 착수 가능.
남은 첫 일: 세 디스패치 경로를 만들고, 풀 크기로 두 경로를 고를지 정한다(같은 결과여야 한다).

### SpanAllocatorVatSkinPools
제목: 스킨 원본 · VAT 풀의 구간 할당기
왜 필요한가: 스킨 원본 전체 다시 올리기(`RT.Deform.rebuildSkinSources`, 집합이 바뀐 프레임에만 찍힘)와 VAT 테이블 풀(`RT.Deform.rebuildVertexAnimationPool`)은 `GPUMeshVertexPool` · `GPUMeshMorphPool` 과 같은 모양의 `SpanAllocator` 구간 갱신이 아니다. 구간은 이미 들어갔다.
착수 조건: 측정 없이 착수 가능.
남은 첫 일: 두 풀을 `SpanAllocator` 구간 갱신으로 옮기는 단위를 계획 문서로 올린다.
