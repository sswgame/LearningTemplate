#!/bin/bash
# 묶음 끝 전체 검증: Windows(Debug · Release 빌드 · 단위 · ctest, 서버 빌드 · 단위, 배포 구성)가 모두 통과한 뒤에만 Linux 를 돌린다.
# 인자는 Linux 로 넘긴다(--record). --no-linux 면 Windows 까지만 돈다(Linux 골든이 곧 바뀌는 묶음 사이). 로그는 build/<프리셋>/Testing/Logs/verify.* · build/verify.linux.log. 끝 줄 VERIFIED=<sha> 가 나와야 push 한다.
cd "$(dirname "$0")/../.." || exit 2
SHA=$(git rev-parse HEAD)

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

run_ctest() {
  local P=$1
  echo "== $P ctest $(date +%T)"
  (cd build/$P && ctest -j6 --output-on-failure > Testing/Logs/verify.ctest.log 2>&1)
  local RC=$?
  echo "ctest exit=$RC"
  grep -E "tests passed|tests failed|\*\*\*Failed|Timeout|Not Run" build/$P/Testing/Logs/verify.ctest.log | head -30
  [ $RC -eq 0 ] || windows_failed "$P ctest"
}

build_preset Ninja-Debug
echo "== unit $(date +%T)"
build/Ninja-Debug/Bin/App.exe --test > build/Ninja-Debug/Testing/Logs/verify.unit.log 2>&1
RC=$?
echo "unit exit=$RC"
tail -3 build/Ninja-Debug/Testing/Logs/verify.unit.log
[ $RC -eq 0 ] || windows_failed "Debug unit"
run_ctest Ninja-Debug

build_preset Ninja-Release
run_ctest Ninja-Release

build_preset Ninja-Debug-Server
build/Ninja-Debug-Server/Bin/Server.exe --test -unattended > build/Ninja-Debug-Server/Testing/Logs/verify.unit.log 2>&1
RC=$?
echo "server unit exit=$RC"
grep -E "case run" build/Ninja-Debug-Server/Testing/Logs/verify.unit.log
[ $RC -eq 0 ] || windows_failed "server unit"

echo "== shipping-check $(date +%T)"
py -3 -m Scripts shipping-check 2>&1 | tail -n 8
RC=${PIPESTATUS[0]}
echo "shipping-check exit=$RC"
[ $RC -eq 0 ] || windows_failed "shipping-check"

if [ "$1" = "--no-linux" ]; then
  echo "WINDOWS_VERIFIED=$SHA (Linux 를 돌리지 않았다)"
  exit 0
fi
echo "== WINDOWS_OK, linux 시작 $(date +%T)"
PYTHONUTF8=1 py -3 Scripts/dev/LinuxScenarios.py all "$@" > build/verify.linux.log 2>&1
RC=$?
tail -n 40 build/verify.linux.log
echo "== done $(date +%T)"
[ $RC -eq 0 ] || { echo "LINUX_FAILED sha=$SHA"; exit 1; }
echo "VERIFIED=$SHA"
