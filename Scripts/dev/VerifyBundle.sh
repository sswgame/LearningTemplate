#!/bin/bash
# 묶음 끝 전체 검증: Windows(Debug · Release 빌드 · 단위 · ctest, 서버 빌드 · 단위, 배포 구성)가 모두 통과한 뒤에만 Linux 를 돌린다.
# 인자는 Linux 로 넘긴다(--record). --no-linux 면 Windows 까지만 돈다(Linux 골든이 곧 바뀌는 묶음 사이). 로그는 build/<프리셋>/Testing/Logs/verify.* · build/verify.linux.log. 끝 줄 VERIFIED=<sha> 가 나와야 push 한다.
# --show 는 게임 · 에디터 테스트 창을 화면에 띄우고 테스트를 하나씩 돌린다(사람이 지켜볼 때). --from debug|release|server|shipping|linux 는 앞 단계를 건너뛴다(linux 는 Windows 가 이미 통과했고 그 뒤 고친 것이 Linux 쪽 스크립트 · 환경뿐일 때). 앞 단계가 통과한 뒤 바뀐 것이 그 단계에 영향을 주지 않을 때만 쓴다(실패한 단계부터 다시 돌린다). 끝 줄에 어디서 시작했는지 적는다.
cd "$(dirname "$0")/../.." || exit 2
SHA=$(git rev-parse HEAD)
FROM=debug
CTEST_JOBS=6
while true; do
  case "$1" in
    --from) FROM=$2; shift 2 ;;
    # --show: 무인 실행의 창 숨김만 끈다(App 이 SW_SHOW_WINDOWS=1 을 본다 — 판정 · 골든은 같다). 보는 사람을 위해 테스트를 하나씩 돌린다.
    --show) export SW_SHOW_WINDOWS=1; CTEST_JOBS=1; shift ;;
    *) break ;;
  esac
done
case "$FROM" in
  debug) STAGE=1 ;;
  release) STAGE=2 ;;
  server) STAGE=3 ;;
  shipping) STAGE=4 ;;
  linux) STAGE=5 ;;
  *) echo "--from 은 debug · release · server · shipping · linux 중 하나다: $FROM"; exit 2 ;;
esac

windows_failed() {
  echo "WINDOWS_FAILED ($1) - Linux 를 돌리지 않는다 sha=$SHA"
  exit 1
}

build_preset() {
  local P=$1
  mkdir -p build/$P/Testing/Logs
  echo "== $P build $(date +%T)"
  cmake --preset $P > build/$P/Testing/Logs/verify.configure.log 2>&1 || windows_failed "$P configure"
  if ! cmake --build --preset $P -- -k 0 > build/$P/Testing/Logs/verify.build.log 2>&1; then
    grep -E "error|FAILED" build/$P/Testing/Logs/verify.build.log | head -30
    windows_failed "$P build"
  fi
  echo "build warnings: $(grep -c 'warning:' build/$P/Testing/Logs/verify.build.log)"
}

# 격리 표(Config/Rules/Test/Quarantine.json)의 테스트를 ctest -E 로 뺀다. 만료가 지났거나 칸이 빠진 표는 검증 실패다(격리가 말없이 늘어지지 않게).
QUARANTINE_EXCLUDE=$(py -3 Scripts/dev/TestHistory.py quarantine --ctest-exclude) || { echo "$QUARANTINE_EXCLUDE"; windows_failed "quarantine table"; }
QUARANTINE_EXCLUDE=${QUARANTINE_EXCLUDE//$'\r'/}   # Windows 파이썬의 print 는 CRLF 를 쓴다 — 정규식 끝에 CR 이 붙으면 아무것도 빼지 못한다
[ -z "$QUARANTINE_EXCLUDE" ] || echo "격리로 뺀다: $QUARANTINE_EXCLUDE"

run_ctest() {
  local P=$1
  local EXCLUDE=()
  [ -z "$QUARANTINE_EXCLUDE" ] || EXCLUDE=(-E "$QUARANTINE_EXCLUDE")
  echo "== $P ctest $(date +%T)"
  (cd build/$P && ctest -j$CTEST_JOBS --output-on-failure "${EXCLUDE[@]}" > Testing/Logs/verify.ctest.log 2>&1)
  local RC=$?
  echo "ctest exit=$RC"
  grep -E "tests passed|tests failed|\*\*\*Failed|Timeout|Not Run" build/$P/Testing/Logs/verify.ctest.log | head -30
  # 이긴 실행 · 진 실행 모두 이력에 쌓는다(흔들림 지수 · p95 추세는 여러 묶음을 모아야 보인다 — TestHistory.py report).
  py -3 Scripts/dev/TestHistory.py ingest build/$P/Bin/Saved/Automation --commit "$SHA" || windows_failed "$P test history ingest"
  [ $RC -eq 0 ] || windows_failed "$P ctest"
}

if [ $STAGE -le 1 ]; then
  build_preset Ninja-Debug
  echo "== unit $(date +%T)"
  build/Ninja-Debug/Bin/App.exe --test > build/Ninja-Debug/Testing/Logs/verify.unit.log 2>&1
  RC=$?
  echo "unit exit=$RC"
  tail -3 build/Ninja-Debug/Testing/Logs/verify.unit.log
  [ $RC -eq 0 ] || windows_failed "Debug unit"
  run_ctest Ninja-Debug
fi

if [ $STAGE -le 2 ]; then
  build_preset Ninja-Release
  run_ctest Ninja-Release
fi

if [ $STAGE -le 3 ]; then
  build_preset Ninja-Debug-Server
  build/Ninja-Debug-Server/Bin/Server.exe --test -unattended > build/Ninja-Debug-Server/Testing/Logs/verify.unit.log 2>&1
  RC=$?
  echo "server unit exit=$RC"
  grep -E "case run" build/Ninja-Debug-Server/Testing/Logs/verify.unit.log
  [ $RC -eq 0 ] || windows_failed "server unit"
fi

if [ $STAGE -le 4 ]; then
  # 도구 테스트: 실행 인자 GUI(VS Code 확장 Tools/launch-args — 확장이라 JS 다)가 Source 의 매크로 · 인자 표를 읽는다. 엔진 쪽 이름 · 형식이 바뀌면 깨지는데
  # 어디서도 돌지 않아 오래 깨져 있었다(2026-11-08). 확장 도구가 VS Code 안의 Node 로 돌린다(따로 깐 node 가 필요 없다).
  echo "== launch-args tests $(date +%T)"
  py -3 Tools/launch-args/Scripts/ExtensionTool.py test > build/verify.launchargs.log 2>&1
  RC=$?
  grep -E "^ℹ (pass|fail)" build/verify.launchargs.log
  [ $RC -eq 0 ] || windows_failed "launch-args tests"

  echo "== shipping-check $(date +%T)"
  py -3 -m Scripts shipping-check 2>&1 | tail -n 8
  RC=${PIPESTATUS[0]}
  echo "shipping-check exit=$RC"
  [ $RC -eq 0 ] || windows_failed "shipping-check"
fi

if [ "$1" = "--no-linux" ]; then
  echo "WINDOWS_VERIFIED=$SHA (Linux 를 돌리지 않았다, 시작 단계 $FROM)"
  exit 0
fi
echo "== WINDOWS_OK, linux 시작 $(date +%T)"
PYTHONUTF8=1 py -3 Scripts/dev/LinuxScenarios.py all "$@" > build/verify.linux.log 2>&1
RC=$?
tail -n 40 build/verify.linux.log
echo "== done $(date +%T)"
[ $RC -eq 0 ] || { echo "LINUX_FAILED sha=$SHA"; exit 1; }
echo "VERIFIED=$SHA (시작 단계 $FROM)"
