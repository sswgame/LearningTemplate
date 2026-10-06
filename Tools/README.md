# Tools (호스트 빌드 도구)

본격적인 엔진 컴파일 전에 미리 실행되어 빌드를 돕거나, 외부 패키지 매니저(vcpkg) 등이 보관되는 장소입니다.

## 주요 구성요소
- **ReflectionParser**: libclang으로 `REFLECT` / `PROPERTY` 등을 스캔해 `*.gen.cpp`를 생성합니다. 초심자용 흐름·CLI·템플릿 설명은 [ReflectionParser/README.md](ReflectionParser/README.md). 엔진 본체를 빌드하려면 이 도구가 먼저 준비되어야 합니다.
- **LLVM · Ninja · Sccache · vcpkg**: `Scripts/setup/` 의 셋업 스크립트(`SetupEnvironment.py` · `SetupLlvm.py` · `SetupVcpkg.py`)가 받아 두는 툴체인 · 패키지 매니저 자리입니다.
  clang-format 도 `LLVM/bin` 의 고정 판을 씁니다(PATH 의 다른 판이 아니라).
- **_cache**: 위 도구를 받을 때의 다운로드 캐시입니다.
- **DCC/Blender**: Blender 내보내기 애드온 — 고른 것을 엔진 규약으로 glTF(`models_raw/`) · 소켓 초안(`*.sockets.xml`)으로 내보내고 `App --import-models` 를 띄운다. [DCC/Blender/README.md](DCC/Blender/README.md).
- **launch-args**: VS Code 확장 — 소스에서 읽은 전역 변수(`-gv_*`) · 커맨드라인 인자를 사이드바에서 골라 CMake Tools 디버그 · 실행에 넘긴다(프로필로 어느 CMake 프로젝트에나). [launch-args/README.md](launch-args/README.md).
- **OnlineLoadBot**: 온라인 부하 시험 봇(개발 도구 — 개발 구성 · Game 타깃에서만 짓고 배포물에 없다). 봇 하나가 연결 하나에 키트 클라이언트 그대로
  (계정 · 서버 디렉터리 · 채팅 · 친구 · 순위 · 매칭 · 라이브 운영)를 들고, 전송 하나 · 끝점 하나에 연결 수천을 연다(`OnlineServiceClient` 공유 끝점 모드).
  - 실행: `OnlineLoadBot --scenario=Tools/OnlineLoadBot/Scenarios/<이름>.json (--server=<ip:port> | --local-server[=<port>]) [--bots=N] [--report=<json>]
    [--max-error-percent=N] [--light-hash]`. `--local-server` 는 같은 프로세스에 서버 조립(`LoadBotLocalServer` — 메모리 저장소 · 캐시 · 버스 위의 키트 일곱)을 띄우고
    루프백 TCP 로 붙는다(전용 서버가 온라인 서비스를 조립하기 전까지의 길). `--light-hash` 는 가벼운 Argon2id(로그인 해시 비용을 재려면 빼라).
  - 시나리오는 데이터: 봇 수 · 늘리는 시간 · 길이 · 씨앗 · 지역 · 단계(동작 19 가지, `repeat` 의 자식 단계). 키는 `_camelCase` 이고 대소문자를 가린다 —
    모르는 동작 · 칸 · 값은 읽기 오류다. 글의 `{bot}` 은 봇 번호, `{seq}` 는 그 봇의 보낸 순번(같은 글을 되풀이하면 도배 막이에 걸린다).
    `chat_and_match` · `login_storm`(비밀번호 로그인 — 해시 비용) · `quick_check`(봇 300, 수 초).
  - 결과: 동작별 지연 p50 · p95 · p99 · 최대(표본을 모두 들고 정렬 — 정확한 백분위), 오류 키별 수(`code<공통 오류>` · `<동작>.result<업무 결과>`), 알림 종류별 수,
    연결 열림 · 실패 · 닫힘, 경기 수. 표는 로그로, `--report` 면 JSON. 오류율이 상한(기본 1 %)을 넘으면 종료 코드 1, 인자 · 시나리오 오류는 2. 서버 쪽 지표는
    운영 끝점(`/metrics`)을 같이 본다(봇은 손님 쪽만).
  - 리눅스 봇 머신은 열린 파일 상한(`ulimit -n`)을 봇 수보다 크게 둘 것(기본 1024 면 그 위에서 연결이 실패한다).
  - 시험: `EngineTest --test_filter=OnlineLoadBotTest.*` — 봇 200 이 같은 서버 조립에 루프백으로 붙어 가짜 시계로 끝까지 돌고 지표가 정확히 맞으며 같은 씨앗이면 표가 같다.
- 받아 오는 폴더는 `.gitignore` 로 빠져 있고, 이 폴더에서 커밋되는 소스는 `ReflectionParser` · `DCC` · `OnlineLoadBot` · `launch-args` 와 `CMakeLists.txt` 뿐입니다.
