# Comment Fold

C · C++ · HLSL 파일(`.h` · `.hpp` · `.inl` · `.cpp` · `.c` · `.cc` · `.hlsl` · `.hlsli`)의 주석을 토글 한 번으로 모두 접고 모두 펼치는 VS Code 확장입니다.

VS Code 내장 `Fold All Block Comments`(`editor.foldAllBlockComments`)는 블록 주석만 접고, 이어진 `//` 줄이나 `///` 문서 주석 묶음은 접지 못하며, 토글 상태도 없습니다. 이 확장은 그 몫을 채웁니다(Visual Studio "Toggle All Outlining" 처럼 한 단추로 오갑니다).

## 쓰는 법

| 무엇 | 어디 |
| --- | --- |
| 명령 `Comment Fold: Toggle Comments` | 명령 팔레트 · 편집기 제목 막대(접기 아이콘) |
| 단축키 | `Ctrl+K Ctrl+Shift+/` (macOS `Cmd+K Cmd+Shift+/`) |
| 상태 표시줄 단추 | 오른쪽 `Comments` — 아이콘이 지금 상태(접힘 `$(fold)` · 펼침 `$(unfold)`), 누르면 토글 |

지금 편집기의 주석이 펼쳐져 있으면 모두 접고, 접혀 있으면 모두 펼칩니다. 상태는 문서마다 기억합니다.
코드 블록의 접힘 상태는 건드리지 않습니다 — 주석 범위의 시작 줄에만 VS Code 의 `editor.fold` / `editor.unfold` 를 겁니다.

접기 범위 제공자를 등록하면 VS Code 는 그 언어의 들여쓰기 접기를 끕니다. 그래서 C/C++(`ms-vscode.cpptools`, `C_Cpp.codeFolding` 켜짐) · clangd 확장이 없는 편집기와 HLSL 파일에서는 VS Code 와 같은 규칙의 들여쓰기 코드 범위(`#pragma region` 표지 포함)도 함께 냅니다.

## 접는 단위

- `/* … */` · `/** … */` 블록 주석 하나.
- 이어진 `//` 줄 묶음. 빈 줄 · 코드 · 블록 주석에서 끊기고, 문서 주석(`///` · `//!`)과 보통 주석(`//`)이 바뀌는 곳에서도 끊깁니다.
- 줄 끝 주석(앞에 코드가 있는 주석)은 접지 않습니다.
- 끝 줄에 코드가 이어지면(`*/ int x;`) 그 줄은 숨기지 않습니다.

문자열 · 문자 리터럴 · 원시 문자열(`R"(...)"`) 안의 `//` · `/*` 는 주석이 아닙니다. 작은 토크나이저가 자릿수 구분자(`1'000`)와 줄 잇기(`\`)까지 가릅니다.
토크나이즈는 문서 판이 바뀔 때만 하고 결과를 캐시합니다(2 만 줄 수 ms).

## 설정

| 설정 | 기본값 | 뜻 |
| --- | --- | --- |
| `commentFold.foldOnOpen` | `false` | 이 창에서 처음 여는 파일의 주석을 접은 채로 엽니다 |
| `commentFold.minimumLineCount` | `2` | 이 줄 수 이상인 주석만 접습니다(최소 2) |
| `commentFold.excludeFileHeader` | `false` | 첫 코드 줄 앞의 주석(`#pragma once` 는 코드로 치지 않음)과 `@file` · `\file` 태그가 든 주석을 접지 않습니다 |

셋 다 언어별로 덮어쓸 수 있습니다(`"[hlsl]": { "commentFold.foldOnOpen": true }`).

## 설치

저장소 루트에서:

```bash
py -3 Tools/Scripts/ExtensionTool.py package Tools/comment-fold   # 단위 테스트 뒤 Tools/comment-fold/dist/comment-fold-<판>.vsix
code --install-extension Tools/comment-fold/dist/comment-fold-1.0.0.vsix
```

`py -3 Tools/Scripts/ExtensionTool.py install Tools/comment-fold` 는 위 둘을 한 번에 합니다. Node.js · npm 은 필요 없습니다(VS Code 안의 Node 로 테스트를 돌리고, VSIX 는 파이썬이 씁니다). 런타임 의존은 없습니다.

## 테스트

```bash
py -3 Tools/Scripts/ExtensionTool.py test Tools/comment-fold                 # 토크나이저 · 범위 고르기 단위 테스트
py -3 Tools/Scripts/ExtensionTool.py test --integration Tools/comment-fold   # + 격리된 VS Code 에서 토글 · foldOnOpen · 단축키 충돌
```

## 알려진 한계

- `editor.foldingStrategy` 가 `indentation` 이면 VS Code 가 접기 제공자를 쓰지 않아 접히지 않습니다(기본 `auto`).
- 다른 확장(C/C++ · clangd)의 접기 범위가 이 확장의 주석 범위와 엇갈리면 VS Code 가 한쪽을 버립니다. 같은 줄에서 시작하는 남의 주석 범위가 이기면 그 범위가 접힙니다.
- 파일 하나의 접기 범위가 `editor.foldingMaximumRegions`(기본 5000)를 넘으면 넘친 범위는 접히지 않습니다.
- VS Code 의 `Unfold All` 처럼 밖에서 펼치면 기억한 상태(접힘)와 어긋납니다 — 그다음 한 번은 펼치기(바뀌는 것 없음)로 상태만 맞춥니다.
- `#include <a//b>` 같은 헤더 이름 안의 `//` 는 주석으로 봅니다.
- clangd 확장이 깔려 있으면 코드 범위는 clangd 에 맡깁니다. clangd 가 범위를 내지 못하는 문서(제목 없는 문서, 서버가 뜨지 않은 때)는 주석만 접힙니다.
- HLSL 의 다른 확장이 코드 범위를 내면 이 확장의 들여쓰기 범위와 겹칠 수 있습니다(같은 줄에서 시작하면 VS Code 가 하나만 남깁니다).
