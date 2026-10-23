# 로그 · assert 문자열의 한글 줄을 영어로

> 상태: 끝 · 선행: 없음 · 영역: project · 옛번호: 없음 · 진행: 8/8

AGENTS.md 는 "로그 · assert 문자열은 영어" 라고 정하지만 `SW_LOG_*` · `SW_ASSERT*` 호출 약 270 곳(2026-10-24 측정, 로그 줄 약 390 개)에 한글이 있었다. 폴더 단위 예외로 받아 영어로 옮겼고(2026-10-24), 예외 표는 모두 지웠다.

**근본 원인**: 이 규칙을 지키는 게이트가 없어 한글 로그가 계속 들어왔다. 게이트를 두고(L0) 기존 줄은 폴더 단위 예외로 받아, 폴더를 영어로 옮기면 그 예외가 낡은 줄로 실패해 지우게 했다. 지금은 `Scripts/lint/rules/CheckLogLanguage.toml` 이 없고 게이트에 예외 경로도 없다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| L0 | 게이트 `Scripts/lint/gate/CheckLogLanguage.py`. `SW_LOG_ERROR` · `WARNING` · `INFO` · `TRACE` · `ASSERT` 와 `SW_ASSERT*` 인자의 한글을 `;` 까지 한 덩어리로 본다(여러 줄 호출 포함) | 끝남 |
| L1 | Core 열 폴더(`CommandLine` · `Container` · `Diagnostics` · `Event` · `File` · `GlobalVariable` · `LogSink` · `String` · `Task`) | 끝남(2026-10-24) |
| L2 | Engine/Graphics (RHI 백엔드 · Texture · Material · Shader Reflection) | 끝남(2026-10-24) |
| L3 | Engine/Renderer (`RenderPipelineAsset.cpp` 28 줄 등) | 끝남(2026-10-24) |
| L4 | Engine 나머지(Compression · Resource · Profiling · Serialization · Input · Scene · TileMap · Bootstrap) | 끝남(2026-10-24) |
| L5 | Editor(`Common` · `Panels` · `ImGuiEditor.cpp`) | 끝남(2026-10-24) |
| L6 | App · ModuleHost · Games(`Empty/BenchScene.cpp`) · GameFramework | 끝남(2026-10-24) |
| L7 | Test(AppTest · EngineTest · EditorTest · ReflectionTest) | 끝남(2026-10-24) |

## 2. 한 일과 남은 틈

- 로그 문구를 매칭하는 곳은 하나였다. `RHIMemoryLedger` 의 `[Profile]` 보고가 호출 밖의 한글 상수("모름", "(이 프로세스)")를 로그에 실었고 `TestRHISupport.cpp` 가 그 글을 단언했다. 상수와 단언을 함께 영어로 고쳤다. 외부 스크립트가 읽는 `[Profile] alloc/frame` 줄(`Scripts/qa/Load.py`)의 형식은 그대로다.
- 테스트의 한글 XML 자료(번역 표 두 개)가 `SW_ASSERT_TRUE` 인자에 있어 게이트에 걸렸다. 변수로 빼 호출 밖에 두었다. 한글 자료는 로그 문자열이 아니다.
- 남은 틈: 호출 인자 밖에서 만든 한글(변수나 상수에 담아 넘기는 글)은 게이트가 보지 못한다. 위의 `RHIMemoryLedger` 가 그 경우였다. 로그로 나가는 글은 호출 안에 리터럴로 적는다(게이트 머리 주석에 적었다).
