# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 배치 경계마다 적용 담당이 고쳐 씁니다. 끝난 항목은 지웁니다.

## 지금 상태 (2026-10-10)

- main 에 들어간 것: 3 차(가상 입력 · 자동화 시나리오 · 빙의 · 탑승), 4 차(런타임 UI 전부 + 원시 장치 조회 → 입력 맵 액션), 5 차 a(문서 분리 · 다시 쓰기 · 작업 방식), 5 차 b 의 대부분.
- 5 차 b 는 **컴파일 통과만 확인하고 반영했습니다(Debug 경고 0). 린트 · 시험 · Shipping · hostgpu · 백엔드 실행은 돌리지 않았습니다.**
  새 세션의 첫 일은 검증입니다: `ctest -L lint` · `-L nogpu` · Shipping `-L hostgpu` · 대표 셋(네 백엔드 × Empty · 에디터 · Shooter3D · NileCity). 실패는 5 차 b 의 해당 커밋을 찾아 고칩니다.
- 5 차 b 에서 들어간 것: 에디터 결함 약 20 건(뷰포트 클릭 · 기즈모 · 로딩 화면 · Play 와 되돌리기 · 인스펙터 이름 칸 · Hierarchy · 콘텐츠 브라우저 · 프리팹 · 타일맵 · Global Variables · Dialogue Graph · Sequencer · Quick Open),
  에디터 시나리오 기반, 렌더러 결함 6 건, 규칙 예외 게이트 · 린트, 명령줄 철자 통일, 리소스 교체, 아이콘 폰트(Font Awesome 제거), 현지화 도구, 에디터 보강 계획.
- 6 차(주석을 새 문체로)는 시작하지 않았습니다.

## 5 차 b 마무리(2026-10-10, 검증 안 함)

남은 것을 모두 넣었습니다. **이것도 Debug 컴파일(경고 0)만 확인했고 린트 · 시험 · Shipping · hostgpu · 백엔드 실행은 돌리지 않았습니다.**

- 의존성 기준선: vcpkg 레지스트리 `0699a19d`(openssl 3.6.5 · imgui 1.92.9 · meshoptimizer 1.3 · mimalloc 3.5.3 · vulkan-headers 1.4.363 · tracy 0.14.1), 오버레이 openssl · tracy · RTM 2.3.1,
  `py -3 -m Scripts deps-outdated`. Tracy 뷰어는 0.14.1 을 `Tools/Tracy/` 에 받아야 붙는다. 검증 때 TLS(온라인 시험) · Tracy 연결 · Vulkan 검증 레이어를 본다.
- 옵션 창 자동 크기 + 캔버스 자동 크기 자식을 패널 안으로 — 골든 `options.layout.txt` 를 다시 써야 한다(백로그 1-6 옵션 · 일시정지 (6)).
- UI 그리기: 보이는 자식 범위 이분 탐색 · 그리기 목록 통째 복사 — Release 벤치 측정이 남았다(백로그 1-6 "런타임 UI 그리기 성능").
- 에디터 고정 픽셀 64 곳 배율(D24), 프로토타입 격자 머티리얼 셋(R7). D25 · R6 · R8 · 백로그 두 줄(월드 공간 UI GPU · 글리프 SDF 컴퓨트)은 이미 들어가 있었다.
- 실행으로 판정할 에디터 결함(D21 · D26 · D24 확인)은 백로그 1-4 "패널 점검" 항목에 있다.

## 5 차 b 뒤 추가(2026-10-10, 검증 안 함 — Debug 컴파일 경고 0 만)

- Dev 산출물 자리(`17c41b88b`): 모듈 DLL 은 `Bin/Modules`, PDB 는 `Bin/Symbols`, 시험 실행 파일은 모든 구성에서 `TestBin`(키트를 링크한 시험은 키트 DLL 을 옆에 복사),
  옛 자리 · 꺼진 모듈 산출물은 configure 가 지운다. **검증 때 가장 먼저 볼 것**: `-L nogpu` 전부(시험이 TestBin 에서 Bin 작업 폴더로 Engine.dll 을 찾는지),
  App · 에디터 · 핫 리로드(Ctrl+Alt+F11 — 섀도 사본이 `Bin/Modules` 에 생기는지), SmokeTest(모듈 올리고 내리기), 리눅스 WSL 빌드(RPATH).
  CLAUDE.md 의 시험 실행 예시(`build/Ninja-Debug/Bin/EngineTest.exe`)는 고치지 않았다 — 사용자가 고칠지 정한다(새 자리 `cd build/Ninja-Debug/Bin; ../TestBin/EngineTest.exe`).
- 크래시 보고 인자(`2b64fb7dc`): `-crash-reporter=<폴더>` 가 명령줄 표(CRASH_REPORTER)로 — 부트스트랩이 핸들러 · 리소스 루트 없이 서고 App 헤드리스 분기가 보낸다.
  검증: CrashBundleTest, `App.exe -crash-reporter="<폴더>"` 를 Resource 없는 폴더에서 띄워 종료 코드 0 · 로그 한 줄.
- 반복문 중괄호(`443e38c5a` · `f50bc0d35`): 게이트 CheckLoopBraces + FormatBranchBraces 반복문 패스, 트리 전체 2,605 곳 적용.
- 중복 정리(`41eab8be9` · `6d2ff3838` · `a4751c74b` · `d902b7dbb`): MathUtil::wrapAngle · moveToward · smoothstep 사본, startsWith · trim 손코딩, 시험 test::tickFrames,
  kHexWidth · kSchemaVersionKey. 경계가 달라지는 곳은 커밋 메시지에 있다(워핑 · 카메라 · 사격 기믹의 ±π). 남긴 것: 난수 통합 · 격자 · 레지스트리 · 타이머 · HP(범위 밖),
  runSteps · stepFor(타입별 한 줄), kRecordFormat(레코드마다 따로 올리는 판).

## 다음 세션이 할 일

- 에디터 보강 — 계획은 [에디터 보강 계획](EditorPlus.md)입니다. 단위마다 에디터 시나리오로 확인하고, 끝난 단위는 그 문서에서 지웁니다.
  보강이 끝나면 에디터 문서(`Source/Editor/README.md` 등)를 새 문체로 다시 씁니다.
- 백로그 1 절의 남은 일.
