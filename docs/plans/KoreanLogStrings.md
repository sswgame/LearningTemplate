# 로그 · assert 문자열의 한글 줄을 영어로

> 상태: 구현중 · 선행: 없음 · 영역: project · 옛번호: 없음 · 진행: 1/8

AGENTS.md 는 "로그 · assert 문자열은 영어" 라고 정하지만 `SW_LOG_*` · `SW_ASSERT*` 호출 약 270 곳(2026-10-24 측정, 로그 줄 약 390 개)에 한글이 있다. 주석 문체 작업(CommentRewriteRemaining)과 같은 파일을 건드리므로 폴더 단위로, 그 폴더의 주석 작업과 같은 작업자가 이어서 한다.

**근본 원인**: 이 규칙을 지키는 게이트가 없어 한글 로그가 계속 들어왔다. 게이트부터 두고(L0) 기존 줄은 폴더 단위 예외로 받아, 폴더를 영어로 옮기면 그 예외가 낡은 줄로 실패해 지우게 한다(예외가 늘지 못하고 줄기만 한다).

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| L0 | 게이트 `Scripts/lint/gate/CheckLogLanguage.py` + `rules/CheckLogLanguage.toml`(폴더 예외 30 줄, 이유에 단위 이름). `SW_LOG_ERROR` · `WARNING` · `INFO` · `TRACE` · `ASSERT` 와 `SW_ASSERT*` 인자의 한글을 `;` 까지 한 덩어리로 본다(여러 줄 호출 포함) | 끝남(통합에서 selftest · lint-suite 대기) |
| L1 | Core 열 폴더(`CommandLine` · `Container` · `Diagnostics` · `Event` · `File` · `GlobalVariable` · `LogSink` · `String` · `Task`) | 대기 |
| L2 | Engine/Graphics (RHI 백엔드 · Texture · Material · Shader Reflection 약 60 곳) | 대기 |
| L3 | Engine/Renderer (`RenderPipelineAsset.cpp` 28 줄 등 약 46 곳) | 대기 |
| L4 | Engine 나머지(Compression · Resource · Profiling · Serialization · Input · Scene · TileMap · Bootstrap) | 대기 |
| L5 | Editor(`Common` · `Panels` · `ImGuiEditor.cpp`) | 대기 |
| L6 | App · ModuleHost(`LiveReloadManager.cpp` 14 줄) · Games(`Empty/BenchScene.cpp` 21 줄) · GameFramework | 대기 |
| L7 | Test(AppTest · EngineTest · EditorTest · ReflectionTest — 테스트가 로그 문구를 매칭하는지 먼저 본다) | 대기 |

## 2. 하는 법

- 단위마다 폴더의 한글 줄을 영어로 바꾸고, 그 줄을 `CheckLogLanguage.toml` 에서 지운다. 지우지 않으면 낡은 예외로 실패한다.
- 로그 문구를 매칭하는 곳(시나리오 `ExpectLog` · 테스트의 로그 캡처 · 외부 스크립트)이 있으면 같이 고친다. 바꾸기 전에 문구를 `grep` 으로 찾는다.
- 동작은 바꾸지 않는다(문자열만). 컴파일되는지는 통합 빌드에서 본다.
- 폴더 예외가 굵어서(예: `Source/Games/*`) 그 폴더 안의 새 한글 줄은 단위가 끝나기 전까지 게이트가 못 잡는다. 단위를 끝낼 때 예외 줄을 지워야 게이트가 완전해진다.
