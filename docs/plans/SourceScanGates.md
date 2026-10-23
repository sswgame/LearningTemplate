# 공유 훑기로 못 옮긴 소스 게이트와 픽서

> 상태: 구현대기 · 선행: 없음 · 영역: quality · 옛번호: C-48 · 진행: 0/4

파일마다 보는 소스 게이트 28 개는 `SourceScan` · `RunSourceGates`(CTest · CI 의 `CheckSourceFiles`)로 옮겼다(2026-10-17). 추가 줄 · 기준선은 `LintGate.BaselineSourceGate` · `SourceScan.LineScope` 로 옮겼다(2026-10-18).
이 문서는 못 옮긴 게이트와 픽서(`FormatBranchBraces` 등 — 파일마다 따로 읽고 주석을 가리는 같은 틀)를 나눈다. 목표는 공유 드라이버가 **판정 단위(줄 끝 · 인코딩 · 파일 집합)를 형식으로** 갖게 해서, 게이트마다 같은 읽기 · 가리기를 다시 짓지 않게 하는 것이다.

## 1. 사실

- `SourceFile`(`Scripts/lint/SourceScan.py`)은 읽은 글의 줄 끝을 `\n` 하나로 맞추고(`checkTargetInternal`) UTF-8 로만 읽는다(디코드 오류는 `decodeError`). 줄 끝 · BOM 을 따로 다뤄야 하는 게이트는 이 형식에 담을 칸이 없다.
- 못 옮긴 이유별 게이트:
  - 줄 끝 · `utf-8-sig` 를 따로 다루고 픽서와 판정이 나뉨: `CheckNamespaceBlocks`(+ `FormatNamespaceBlocks`, 공유 함수 `splitNamespaceBlocks`), `CheckIncludeOrder`(+ `FormatIncludeOrder`).
  - C++ 와 CMake 두 파일 집합: `CheckThirdPartyIsolation`, `CheckSuppressionReasons`.
  - 키트 폴더별 규칙: `CheckKitNamespaces`.
- 픽서(`Scripts/lint/fixer/Format*.py`)는 판정과 같은 함수를 부르지만 파일 읽기 · 쓰기 · 주석 가리기는 각자 한다.

## 2. 해법

- `SourceFile` 에 원본 글의 줄 끝 · BOM 을 형식으로 둔다(`rawText` · `lineEnding` · `hasBom`, 처음 묻는 쪽이 한 번 만든다). 정규화한 `text` 는 지금처럼 나눠 쓴다.
- 게이트의 파일 집합을 `sourceScanRoots` 하나가 아니라 집합별 `SourceScanSet`(뿌리 · 확장자 · 제외)의 목록으로 선언하게 하고, `checkSourceFile` 이 어느 집합에서 온 파일인지 `SourceFile.setName` 으로 안다.
- 픽서는 같은 드라이버를 쓴다: 훑기가 `FileFindings` 에 "고친 글"(`fixedText`)을 담을 수 있게 해서, 판정과 고치기가 한 함수의 두 모드가 된다.

## 3. 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| S1 | `SourceFile` 에 `rawText` · `lineEnding` · `hasBom` 을 더하고 `CheckNamespaceBlocks` · `CheckIncludeOrder` 의 판정을 옮긴다 | `Scripts/lint/` | 없음 | 두 게이트의 selftest + `py -3 -m Scripts lint-suite` + 전체 훑기 결과가 옮기기 전과 같은 위반 목록 | 필요합니다. `RunSourceGates` 의 게이트별 초 전후 |
| S2 | 파일 집합 목록(`SourceScanSet`)을 형식으로 두고 `CheckThirdPartyIsolation` · `CheckSuppressionReasons` 를 옮긴다 | `Scripts/lint/` | S1 | 두 게이트의 selftest + 전체 훑기 결과 동일 | 필요 없음 |
| S3 | 픽서가 드라이버를 쓰게 한다(`fixedText`): `FormatNamespaceBlocks` · `FormatIncludeOrder` 부터 | `Scripts/lint/fixer/`, `Scripts/lint/SourceScan.py` | S1 | 픽서 selftest + 같은 입력의 고친 글이 옮기기 전과 바이트 동일 | 필요 없음 |
| S4 | `CheckKitNamespaces` 를 옮긴다(키트 폴더 규칙을 집합 선언으로) | `Scripts/lint/gate/` | S2 | selftest + 전체 훑기 동일 | 필요 없음 |

## 4. 사용자 결정

없습니다.
