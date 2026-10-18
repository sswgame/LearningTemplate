# Scripts (파이썬 도구)

CMake는 빌드만 담당하고, 도구 탐색·설정·보조 생성 및 코드 품질 검사는 여기서 처리한다.

폴더는 하는 일로 가른다: `common/`(공용) · `setup/`(환경) · `generate/`(생성) · `lint/`(검사 · 수정) · `dev/`(실험) · `qa/`(App 비교) · `asset/`(에셋 병합). 진입점은 `py -3 -m Scripts <명령>` 이다.
상세는 영역별 문서로 나눴다(`Scripts/docs/`). 절 하나만 읽으려면 `py -3 -m Scripts doc Scripts/docs/<파일> --section <제목>`.

| 문서 | 내용 |
| --- | --- |
| [이름 · 자리 규칙](docs/Layout.md) | 함수 · 파일 이름 규칙, 폴더별 파일 이름 앞머리 표 |
| [파일 목록 — common · setup · generate](docs/TreeCommon.md) | 공용 모듈, 환경 구성, 생성기 한 줄 설명 |
| [파일 목록 — lint](docs/TreeLint.md) | 게이트 · 픽서 · 보고서 · 셀프테스트 한 줄 설명 |
| [파일 목록 — dev · qa · asset](docs/TreeDev.md) | 개발 도구, QA, 에셋 병합 한 줄 설명 |
| [실행 방법 · 에셋 병합 드라이버](docs/Commands.md) | `py -3 -m Scripts` 명령 목록, 개별 스크립트 실행, XML 에셋 git 드라이버 |
| [린트 — 게이트 추가 · 규칙 데이터 · 커밋 훅](docs/Lint.md) | 새 게이트 놓는 법, `lint/rules/` 데이터, 병합 커밋의 훅 범위 |
| [함정 · 계약 — 코드모드 · 포맷 · 린트](docs/TrapsLint.md) | 약어 코드모드, clang-format · clang-tidy, 린트 속도, 커밋 훅 |
| [함정 · 계약 — 빌드 · 쿠킹 · 규칙 데이터 · 문서](docs/TrapsBuild.md) | 쿠킹, 경고 측정, include 정리, `CheckCodeConventions`, 문서 경로 게이트, TOML 부분 집합 |
