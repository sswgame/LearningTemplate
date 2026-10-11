# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 배치 경계마다 적용 담당이 고쳐 씁니다. 끝난 항목은 지웁니다(무엇을 했는지는 `git log`, 교훈은 영역 README · 결정 기록에 있다).

## 지금 상태 (2026-10-11)

- 구조 이동(Core 층 · GameFramework · Engine 폴더 · DevTools 해체 · Network 이동 · ModuleHost 분리), 도구(약어 · 이름 린트 · 빌드 속도 측정), 에디터 보강, 이름 정리(큰 이름 · 파일 분리 · 바이트 접미사 · 난수 통합)가 끝났다 — 계획 문서는 지우고 기각한 제안은 결정 기록 5-4 로 옮겼다.
- 전체 검증(2026-10-11, PC `SW`)은 린트 63/63 · Debug `-L nogpu` · `-L hostgpu` · Shipping · Server · 게임별 프리셋 여덟 · 대표 실행 네 백엔드가 통과했다. 이후 변경은 린트 + Debug `-L nogpu` + Shipping `-L nogpu` 로 확인했다.
- **CI**: 새 push 마다 진행 중인 실행이 취소돼 결과를 못 볼 수 있다 — push 는 묶어서 하고 실행이 끝나기를 기다린 뒤 다음을 올린다. 리눅스 구성은 zlib `ZLIB::ZLIBSTATIC` 대응(`6f6a441c9`)으로 통과했고, 리눅스 빌드는 `EpollStreamTransport` 의 `unique_ptr<EpollLoop[]>` 를 고쳤다(`37df145`) —
  이 뒤 결과를 아직 못 봤다. 개명 뒤 한 번도 컴파일되지 않은 리눅스 전용 파일(`IOUring` · `LinuxAsyncFileIOBackend` · `PosixThreadCrashStack`)에서 오류가 더 나올 수 있다.
  Windows STL containers 잡은 sw 할당자 · 기록 해시에 기대는 시험 셋(`MemoryTagTest` 둘 · `DestructionDamageTest.EventLogHash…`)을 건너뛰게 했다. 같은 시험이 기본 컨테이너 구성에서 가끔 지는지는 지켜본다(Windows Debug 잡의 `ActionAdventure` · `ActionCombat` · `ActionPlatformer` 실패는 한 번 있었고 다음 실행에서 사라졌다).
- **못 한 것**: 리눅스(CI 로), 실제 RenderDoc 캡처(이 PC 에 없다), 단언 대화상자 세 단추(모달), Tracy 0.14.1 뷰어 연결, 배율 1.5 · 2 에디터, Dialogue Graph 첫 열기, 리눅스 RPATH.
- 최근에 들어간 것: 이름 정리(`ResourceUtil` → `ResourcePaths` + `ResourceIO`, `EditorUtil` 경로 → `EditorPaths`, `Misc` → `None`, `CameraShakeRequest.h`, `_sizeBytes`),
  트레이스 내보내기 X1 · X2(`ProfilerTraceExport`, `profile.trace save | record`, 에디터 Save Trace, Shipping 제외 매크로 `SW_PROFILER_TRACE_ENABLED`). 마지막 커밋 `d5cc62f1e` 는 아직 push 하지 않았다.

## 다음 세션이 할 일 (순서대로)

작업 방식은 [작업 흐름](../11_Workflow.md). 도우미 여럿은 워크트리(`py -3 -m Scripts worktree-make <이름>`)에서만 일하고 push 하지 않으며, 적용 담당 하나가 `git merge --ff-only wt/<이름>` 으로 `main` 에 넣고 push 한다.
도우미가 동시에 빌드할 때는 `D:\Projects\Personal\LT-wt\BUILD-LOCK-README.txt` 의 슬롯 잠금을 쓴다. 검증은 큰 묶음 끝에 한 번, 사이에는 Debug 풀 컴파일 + 린트 게이트만. WSL(리눅스)은 CI 로 본다.

1. **CI 확인과 리눅스 빌드 오류 잡기.** 위 "CI" 항.
2. **트레이스 내보내기 X5 · X4.** 계획 [트레이스 내보내기](TraceExport.md) — 자동 첨부, 여러 프로세스 합치기(X1 ~ X3 은 끝났다). X5 는 Shipping 서버에서 어떻게 할지 다시 정한다(내보내기 코드는 Shipping 에서 빠진다).
3. **빌드 속도 2 ~ 6 단계.** 계획 [빌드 속도](BuildSpeed.md). 기준선은 `docs/08_Verification.md` "빌드 속도". 단계마다 같은 표로 전후를 남기고, 효과 없으면 되돌려 `docs/09_Decisions.md` 3절에 숫자와 함께 적는다. PCH 공유(3 단계)는 타깃마다 다른 정의
   (`SW_LOG_TAG` · `SW_EXPORTS`/`SW_IMPORTS` · 타깃별 `FlagOps.gen.h` 강제 포함)가 걸림돌이다 — 먼저 컴파일 정의 차이 목록을 뽑는다. 7 ~ 9 는 필요할 때만(사용자 결정).
4. **중복 정리 — 끝.** 결정적 난수 · 격자 사본 여섯 · 타이머(남은 7곳은 엔진 · 에디터라 그대로)는 끝났고, 키트 체력 직접 관리의 `Vitality` 이전은 하지 않기로 했다(결정 기록 5-8). 남은 격자 손셈(`Habitat` · `Overworld` 타일맵 · 게임 코드)은 파일 스키마가 얽혀 키트가 `getTopology()` 를 열 때 같이.
5. **엔진 분할.** 계획 [엔진 분할](EnginePartition.md) 0 · 1 ~ 5 단계(작은 고리, `Object` 코어 / 기능 가르기, `OBJECT` 라이브러리 → DLL 승격, 월드를 값으로, 모듈 창구).
6. **서버 디버깅.** 계획 [서버 디버깅](ServerDebug.md) — 이름 정리가 끝났으니 시작할 수 있다.
7. **6 차(주석을 새 문체로).**

### 알아 둘 함정

- **vcpkg 가 갑자기 포트 전체를 다시 지으면**(20분 넘게): `VCPKG_BINARY_SOURCES` 가 `cmake --preset` 환경에만 있어 ninja 재구성에서 빠졌었다(`d649e08f4` 가 `Vcpkg.cmake` 에서 정한다). 그래도 재발하면 `Tools/vcpkg/packages/*/share/*/vcpkg_abi_info.txt` 의 `triplet_abi` 가 흔들리는지 본다. 설치 도중에는 건드리지 않는다.
- **`main` 체크아웃에서는 빌드 · 편집하지 않는다**(도우미가 워크트리에서 일한다). 시험 실행 파일은 `AllTests` 를 지어야 최신이다(옛 exe 로 돌리면 모듈 ABI 불일치로 진다 — 지금 ABI v4).
- **새 소스 파일을 더한 뒤**에는 `cmake --preset Ninja-Debug` 로 다시 구성한다 — `EditorTest` 는 소스 목록(`Test/EditorTest/CMakeLists.txt`)을 손으로 들고 있다.
- **샤드를 동시에 돌리면**(`ctest -j3`) 시간 제한이 가짜로 진다 — 의심되면 하나만 다시 돌린다.
- **임시 파일**은 scratchpad 에서 에이전트 이름을 앞에 붙인다. 파이썬 조각은 heredoc 대신 파일로 쓴다(bash heredoc 은 따옴표 · 역슬래시를 망가뜨린다).
- **측정**은 다른 빌드가 도는 동안 오염된다. 측정 PC 이름(CPU)을 표에 적는다(이 PC 는 Ryzen 7 6800H).
- 사용 한도(`rate_limit`)로 도우미가 끊기면 워크트리 작업은 그대로 남는다 — 한도가 풀린 뒤 `SendMessage` 로 이어서 시킨다.
- 빌드 폴더 함정: 옛 대소문자 이름의 CMake 폴더(`Test/EditorUiTest`)가 남아 있으면 `-Wnonportable-include-path` 경고가 난다 — 그 폴더를 지우고 다시 구성한다.

### 에디터 작은 단위(E5 · G2 · G3 · H1)의 남은 것

남은 것: 리눅스 X11 · dlopen 갈래(CI), 실제 RenderDoc 캡처, 단언 대화상자 세 단추(모달), 스크린샷 토스트 "Show in Explorer" 단추(알림 관리자에 동작 단추가 없다),
`screenshotbutton` 시나리오가 `Bin/Saved/Screenshots` 에 남기는 PNG, Engine README 개발 명령 줄의 경로 확인.
RenderDoc 은 RHI 단계 맨 앞에서 올린다. `ThirdParty/renderdoc` 에 원문 헤더를 넣었고 그 폴더의 `.clang-format` 은 `DisableFormat` 이다.

## 검증할 것

- 리눅스 RPATH: CI 결과로 확인(사용자 결정 2026-10-10 — 로컬 WSL 빌드는 하지 않는다).
