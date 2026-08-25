# Scripts/setup (환경 구성 스크립트)

LLVM(컴파일러), Ninja(빌드 도구), vcpkg(패키지 매니저) 등 엔진을 빌드하기 위해 필요한 **외부 도구(툴체인)들을 탐색하고 자동으로 다운로드/설치**하는 스크립트 모음입니다.

## ⚠️ 실행 시점
- 이 스크립트들은 주로 **CMake 구성(Configure)이 시작되기 직전에 실행**됩니다.
- 최상위에서 `python Scripts/setup/SetupEnvironment.py`를 실행하면 이 안의 스크립트들이 연쇄적으로 동작하며, 개발자의 PC에 부족한 툴을 스스로 찾아내고 세팅을 마친 뒤 `Config/Environment/toolchain_config.json`에 그 경로를 기록해 둡니다.
