# 공유 훑기로 못 옮긴 소스 게이트와 픽서

> 상태: 측정대기 · 선행: 없음 · 영역: quality · 옛번호: C-48 · 진행: 4/4

파일마다 보는 소스 게이트 28 개는 `SourceScan` · `RunSourceGates`(CTest · CI 의 `CheckSourceFiles`)로 옮겼고(2026-10-17) 추가 줄 · 기준선은 `LintGate.BaselineSourceGate` · `SourceScan.LineScope` 로 옮겼다(2026-10-18). 이 문서는 못 옮긴 게이트와 픽서(`FormatBranchBraces` 등 — 파일마다 따로 읽고 주석을 가리는 같은 틀)를 나눴다. 목표는 공유 드라이버가 **판정 단위(줄 끝 · 인코딩 · 파일 집합)를 형식으로** 갖게 해서 게이트마다 같은 읽기 · 가리기를 다시 짓지 않게 하는 것이다.

## 1. 사실 (S1 ~ S6 구현함, 남은 것은 3절의 측정)

새 파일별 게이트 · 픽서는 이 문서의 해법을 쓴다.

- `SourceFile` 은 원본 글(`rawText`)과 줄 끝(`lineEnding`) · BOM(`hasBom`) · BOM 을 뗀 글(`bodyText`) · 원본 형식으로 되돌리는 `restore()` · 걸린 집합 이름(`setName`)을 든다. `text` 는 줄 끝을 맞춘 글이다.
- 게이트의 파일 집합은 `LintGate.sourceScanSets`(`SourceScanSet` 목록 — 뿌리 · 확장자/파일 이름 · 제외 폴더 · 이름으로 고르는 파일 `relativeFiles` · `--files` 와 무관하게 전체를 읽는 `bAlwaysWhole`)다. 한 집합이면 `sourceScanRoots` · `sourceSuffixes` 가 집합 한 칸으로 바뀐다. 한 파일이 집합 둘에 걸리면 선언 순서의 첫 집합이 갖는다. 요약 문구만 바꾸는 게이트는 `describeSourceScan` 을 재정의한다.
- `FileFindings.fixedText` 에 원본 형식으로 되돌린 완성 글을 담는다. 게이트(`checkSourceFile`)와 픽서(`SourceScan.fixedTextOf`)가 같은 판정 함수(`NamespaceBlockRules.inspectSource` · `IncludeOrderRules.inspectSource`)를 부른다 — 판정과 고치기가 한 함수의 두 모드다.
- 옮긴 게이트: `CheckNamespaceBlocks` · `CheckIncludeOrder`(S1), `CheckThirdPartyIsolation` · `CheckSuppressionReasons`(S2, 집합 `cpp`/`cmake`), `CheckKitNamespaces`(S4), `scan()` 에서 직접 걷던 `CheckScriptCommonHelpers` · `CheckScriptLayout` · `CheckGlobalVariableKinds` · `CheckModuleTargets` · `CheckCmakeConventions` · `CheckNoDelayLoad` · `CheckPythonConventions` · `CheckPythonMinimumVersion` · `CheckShaderConventions` · `CheckDataFileReferences` · `CheckScriptEntryPoints` · `CheckScriptLayers`(S6, 마지막 둘은 `bAlwaysWhole` + `selectedRelatives` 로 읽기는 전체 보고는 고른 파일). 소스 게이트는 41 개에서 53 개가 됐다.
- 같은 틀로 옮긴 픽서 넷(S5): `FormatBranchBraces`(`BranchBraceRules` + 반복문 패스는 `LoopBraceRules.inspectSource`) · `FormatForwardDeclarations`(`ForwardDeclarationRules`) · `FormatAcronymSpelling`(`AcronymSpellingRules`) · `FormatCmakeIndent`(`CmakeIndentRules`). `FixPass(inspect=…)` 가 판정 함수를 변환으로 쓰고 줄 끝 · BOM 은 `SourceFile.restore` 가 지킨다. 짝 게이트가 있는 둘은 같은 함수를 부른다(`CheckLoopBraces` ↔ `LoopBraceRules.inspectSource`, `CheckAcronymSpelling` ↔ 코드모드는 `AcronymRegistry.findNameSites`). 규칙 둘이 쓰던 리터럴 가리기(`BranchBraceRules.maskLiteralsAndCommentsInternal`)는 따옴표를 남기는 모양이라 공유 가림(`SourceFile.masked`)과 다르다 — 바꾸면 인자를 줄바꿈한 문자열 줄이 빈 줄로 읽혀 문장 수가 달라진다.
- 옮기기 전후 결과가 같음은 파일 변형(원본 · CRLF · BOM 등) 수천 ~ 십만 건과 열두 게이트의 전체 · `--files` 훑기 출력 비교로 확인했다.
- 옮기지 않은 것과 까닭: `CheckNameReferences`(파일 하나가 정의 · 사용 · 게임별 모음 세 역할을 하고 XML 데이터를 경로로 읽는다), `CheckDataDrivenStructure`(파일 집합이 규칙 데이터와 `git ls-files` 에서 온다), `CheckCodeConventions`(자체 프로세스 분할 · 서른 규칙 · 카테고리 보고 · 저장소 밖 `--files`), `CheckResourceCasing` · `CheckTextureFolders` · `CheckExecutableBits`(파일 이름 · 모드만 본다 — 드라이버가 리소스 바이너리까지 읽게 된다), `CheckTextFilesAreText`(원본 바이트가 필요), `CheckDocPaths`(문서 사이 색인), `CheckHeaderSelfContained`(헤더마다 컴파일), `CheckRenderOwnership`(고정 파일 둘 — 없으면 오류로 알려야 한다), `CheckTestSuites` · `CheckSourceGlob` · `CheckCmakeReadme` · `CheckResourceCredits` 등 폴더 목록 · 표를 대 보는 게이트.

## 2. 해법

- `SourceFile` 에 원본 글의 줄 끝 · BOM 을 형식으로 둔다(`rawText` · `lineEnding` · `hasBom`, 처음 묻는 쪽이 한 번 만든다). 정규화한 `text` 는 나눠 쓴다.
- 게이트의 파일 집합을 `sourceScanRoots` 하나가 아니라 집합별 `SourceScanSet`(뿌리 · 확장자 · 제외)의 목록으로 선언하게 하고 `checkSourceFile` 이 어느 집합에서 온 파일인지 `SourceFile.setName` 으로 안다.
- 픽서는 같은 드라이버를 쓴다: 훑기가 `FileFindings` 에 "고친 글"(`fixedText`)을 담을 수 있게 해서 판정과 고치기가 한 함수의 두 모드가 된다.

## 3. 남은 확인

통합이 `RunSourceGates` 의 게이트별 초를 옮기기 전(main)과 한 번 비교한다(필요합니다). 작업자가 옮긴 뒤 한 번 잰 값: 게이트 53 개 `RunSourceGates` 17.6 s. 혼자 실행 초(옮기기 전 → 후, 한 번씩이라 잡음 큼): `CheckModuleTargets` 10.0 → 1.2, `CheckShaderConventions` 5.1 → 1.5, `CheckScriptEntryPoints` 3.7 → 0.8, `CheckPythonMinimumVersion` 3.6 → 1.9, `CheckPythonConventions` 3.5 → 1.4.

## 4. 사용자 결정

없습니다.
