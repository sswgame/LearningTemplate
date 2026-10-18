# Scripts 파일 목록 — dev · qa · asset

[목차](../README.md)에서 나눈 문서. 규칙은 [이름 · 자리 규칙](Layout.md). 아래 트리는 `Scripts/` 전체 트리의 끝 조각이다.

```
  ├── dev/                            # [개발 실험] 사람이 가끔 손으로 돌린다 — 빌드 · CI 가 부르지 않는다
  │     ├── RunBackendSmoke.py        # 네 백엔드로 같은 씬을 그려 SceneColor 를 비교
  │     ├── CiFailureReport.py        # CI 실패(테스트 · 구성 · 크래시 스택)를 GitHub 주석으로 — ci.yml 이 부른다
  │     ├── ConfigureSnapshot.py      # CMake 구성 결과 스냅숏 · 비교(리팩터 전후) · 구성 시간 요약
  │     ├── CompareDestructionHashLogs.py # 서버 · 클라이언트 로그의 파괴 구조 해시(`[DestructionHash]`)를 (번호, 사건 수)로 맞대기 — 컴파일러 · 플랫폼을 넘어 같은지
  │     ├── MakeStressScene.py        # 로드 경로를 재기 위한 큰 씬(사람이 테스트 데이터를 만든다 — `Make*`, 빌드가 만드는 것은 generate/)
  │     ├── MakeTerrainShowcase.py    # 지형 쇼케이스의 절차 생성 원본(heightfields_raw · textures_raw)
  │     ├── MakeNoiseTexture.py       # 엔진 잡음 텍스처 원본(engine/textures_raw/perlin.png — 이음매 없는 Perlin fBm)
  │     ├── MakeSyntheticRig.py       # 애니메이션 비용 측정용 합성 리그(.glb + .rig.json, 본 수 · 위상 · 모프 · 클립, 대표는 game/empty/rigs_raw)
  │     ├── MakeBrandImages.py        # 에디터 스플래시 원본과 앱 아이콘(app.ico)을 같은 SW 모노그램에서 그린다
  │     ├── MakePackage.py            # 배포 패키지(Shipping 빌드 → 쿠킹 → 스테이징 → 검사, `[package]` 줄) — 에디터 Packaging 창이 같은 것을 부른다
  │     ├── MakeWorktree.py           # 작업 단위용 git 워크트리 + main 과 나눠 쓰는 도구 · vcpkg 폴더 링크(docs/11_Workflow.md)
  │     ├── RemoveWorktree.py         # 워크트리 지우기 — 나눠 쓰는 링크를 먼저 끊는다, 다른 세션이 쓰는 중이면 건너뛴다
  │     ├── ListWorktrees.py          # 워크트리마다 브랜치 · 주인(살아 있는지) — `py -3 -m Scripts worktree-list`
  │     ├── RunSharedLock.py          # 나눠 쓰는 자원 잠금(vcpkg · 빌드 칸 · GPU) — `run` 은 잡고 명령을 돌린 뒤 놓는다
  │     ├── RunFullBuild.py           # 검증 빌드(all AllTests, 빌드 칸) · 트리 증거 — main push 의 pre-push 훅이 증거를 요구한다
  │     ├── ListCiJobs.py             # GitHub Actions 실행 · 잡 · 실패 주석을 공개 API 로(로그인 없이)
  │     ├── ListPlans.py              # 계획 문서(docs/plans)를 머리 줄의 상태 · 영역으로 골라 보인다(`py -3 -m Scripts plans`, --file 은 머리 줄과 절 제목, --find 는 옛 번호 · 이름의 위치, --check 는 로드맵 링크, --section 은 --file 문서의 한 절 본문)
  │     ├── RunEditFile.py            # 파일의 한 곳을 정확히 한 번 바꾼다(`py -3 -m Scripts edit-file`, 줄 끝 · BOM 보존, NUL 거부, --changes 로 여러 곳을 원자적으로)
  │     ├── RunDoc.py                 # 큰 마크다운 문서의 목차(제목 · 행 번호 · 절 크기)나 한 절만 보인다(`py -3 -m Scripts doc`)
  │     ├── RunAgentLog.py            # docs/12_Agents.md '토큰 측정값' 표에 한 줄을 더한다(`py -3 -m Scripts agent-log`)
  │     ├── RunStatusBoard.py         # README 의 '지금 진행 상황' 표를 계획 문서 · 로드맵 · 후보 문서에서 만든다(`py -3 -m Scripts status-board`, --write · --check)
  │     ├── ListOutdatedDeps.py       # vcpkg 의존성의 지금 판 · 레지스트리 최신 판(오버레이 포함, docs/09 5-2)
  │     ├── MakeEditorTheme.py        # 에디터 테마 토큰(tokens.json) ↔ Figma 변수 · DTCG: import-figma · export-figma · check (`py -3 -m Scripts editor-theme`)
  │     ├── RunTests.py               # 스위트 · 케이스 이름으로 테스트 실행 — 테스트까지 지은 뒤(`all` · `AllTests`) 그 케이스가 사는 실행 파일을 `Bin` 에서
  │     ├── RunBuildBaseline.py       # 빌드 시간 기준선(풀 · 헤더 수정 · .cpp 수정 · 워크트리 콜드, 각 3 회 중앙값, 머리에 PC · CPU)
  │     ├── SampleStacks.py           # 살아 있는 프로세스의 스레드 스택을 여러 번 떠 함수별로(DbgHelp) — 프로파일러가 닿지 않는 곳
  │     └── StoreSymbols.py           # 빌드의 PDB · .debug 를 심볼 저장소 배치(GUID+age · .build-id)로
  │
  ├── qa/                             # [QA] 빌드된 App 을 돌려 기준과 비교하거나(hostgpu · soak · perf CTest 와 사람이 부른다) 에셋을 규칙으로 훑는다
  │     ├── ValidateAssets.py         # 에셋 검증 표(파일 · 규칙 · 심각도 · 메시지) — 게이트와 같은 코드, 경고까지 · JSON 출력
  │     ├── GoldenImages.py           # 테스트 게임 자동 플레이 × 네 백엔드 캡처를 Test/QA/Golden 기준과 지표로 비교(`--record` 로 기준을 뜬다)
  │     ├── Soak.py                   # 자동 플레이 장시간 실행 — 메모리 · 핸들 증가 기울기, 프레임 p50 · p99
  │     └── PerfRegression.py         # Release 프레임 p50 · p99 를 이 기계의 기준(Test/QA/Perf)과 비교
  │
  ├── asset/                          # [에셋 도구] 사람과 git 이 부르는 에셋 비교 · 병합
  │     └── AssetMerge.py             # XML 에셋 의미 diff · 3-way merge, git 병합 · 비교 드라이버([Commands.md](Commands.md) "XML 에셋 병합 · 비교 드라이버")
  │
  └── __main__.py                     # ★ 통합 CLI 오케스트레이터 (`py -3 -m Scripts <cmd>`)
```
