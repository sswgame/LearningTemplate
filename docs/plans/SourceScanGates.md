# 공유 훑기로 못 옮긴 소스 게이트와 픽서

> 상태: 측정대기 · 선행: 없음 · 영역: quality · 옛번호: C-48 · 진행: 4/4

파일마다 보는 소스 게이트 28 개는 `SourceScan` · `RunSourceGates`(CTest · CI 의 `CheckSourceFiles`)로 옮겼다(2026-10-17). 추가 줄 · 기준선은 `LintGate.BaselineSourceGate` · `SourceScan.LineScope` 로 옮겼다(2026-10-18).
이 문서는 못 옮긴 게이트와 픽서(`FormatBranchBraces` 등 — 파일마다 따로 읽고 주석을 가리는 같은 틀)를 나눈다. 목표는 공유 드라이버가 **판정 단위(줄 끝 · 인코딩 · 파일 집합)를 형식으로** 갖게 해서, 게이트마다 같은 읽기 · 가리기를 다시 짓지 않게 하는 것이다.

## 1. 사실

구현했다(S1~S4). 남은 것은 아래 3절의 측정과 이 문서의 해법을 쓰는 다음 게이트 · 픽서다.

- `SourceFile` 은 원본 글(`rawText`)과 줄 끝(`lineEnding`) · BOM(`hasBom`) · BOM 을 뗀 글(`bodyText`) · 원본 형식으로 되돌리는 `restore()` · 걸린 집합 이름(`setName`)을 든다. `text` 는 지금처럼 줄 끝을 맞춘 글이다.
- 게이트의 파일 집합은 `LintGate.sourceScanSets`(`SourceScanSet` 목록 — 뿌리 · 확장자/파일 이름 · 제외 폴더)다. 한 집합이면 `sourceScanRoots` · `sourceSuffixes` 가 클래스를 만들 때 집합 한 칸으로 바뀐다. 한 파일이 집합 둘에 걸리면 선언 순서의 첫 집합이 갖는다.
- `FileFindings.fixedText` 에 원본 형식으로 되돌린 완성 글을 담는다. 게이트(`checkSourceFile`)와 픽서(`SourceScan.fixedTextOf`)가 같은 판정 함수(`NamespaceBlockRules.inspectSource` · `IncludeOrderRules.inspectSource`)를 부른다.
- 옮긴 게이트: `CheckNamespaceBlocks` · `CheckIncludeOrder`(S1, `rawText` · `bodyText`), `CheckThirdPartyIsolation` · `CheckSuppressionReasons`(S2, 집합 `cpp`/`cmake`), `CheckKitNamespaces`(S4, 집합 `kit`/`game`/`framework`/`other`). 옮기기 전 `fixText` 와 옮긴 뒤 `fixText` 의 결과는 트리의 파일 변형(원본 · CRLF · BOM · include 뒤섞기) 3152 건에서 바이트 동일했다.
- 아직 각자 읽고 쓰는 픽서: `FormatBranchBraces` · `FormatForwardDeclarations` · `FormatAcronymSpelling` · `FormatCmakeIndent`(판정 함수가 게이트와 따로 있거나 게이트가 없다). 같은 틀(`inspectSource` + `fixedTextOf`)로 옮길 후보다.

## 2. 해법

- `SourceFile` 에 원본 글의 줄 끝 · BOM 을 형식으로 둔다(`rawText` · `lineEnding` · `hasBom`, 처음 묻는 쪽이 한 번 만든다). 정규화한 `text` 는 지금처럼 나눠 쓴다.
- 게이트의 파일 집합을 `sourceScanRoots` 하나가 아니라 집합별 `SourceScanSet`(뿌리 · 확장자 · 제외)의 목록으로 선언하게 하고, `checkSourceFile` 이 어느 집합에서 온 파일인지 `SourceFile.setName` 으로 안다.
- 픽서는 같은 드라이버를 쓴다: 훑기가 `FileFindings` 에 "고친 글"(`fixedText`)을 담을 수 있게 해서, 판정과 고치기가 한 함수의 두 모드가 된다.

## 3. 남은 확인

| 내용 | 측정 |
|---|---|
| 통합이 `RunSourceGates` 의 게이트별 초를 옮기기 전(main)과 한 번 비교한다. 이 작업자는 옮긴 뒤 한 번만 쟀다: 게이트 41 개 38.8 s, `CheckNamespaceBlocks` 8.64 s(다른 작업자와 동시 실행이라 잡음이 크다) | 필요합니다 |

## 4. 사용자 결정

없습니다.
