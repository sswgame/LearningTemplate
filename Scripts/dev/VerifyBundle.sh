#!/bin/bash
# 묶음 끝 전체 검증(Windows + Linux 동시): Debug · Release 빌드 · 단위 · ctest, 서버 빌드 · 단위, 배포 구성. 로그는 build/<프리셋>/Testing/Logs/verify.*
cd "$(dirname "$0")/../.." || exit 2
# Linux 는 함께 돈다(Xvfb 라 Windows GPU 테스트와 겹쳐도 된다). 먼저 끝나도 결과는 맨 끝에 낸다.
SHA=$(git rev-parse HEAD)
mkdir -p build
PYTHONUTF8=1 py -3 Scripts/dev/LinuxScenarios.py all > build/verify.linux.log 2>&1 &
LINUX_PID=$!
for P in Ninja-Debug Ninja-Release; do
  mkdir -p build/$P/Testing/Logs
  echo "== $P build $(date +%T)"
  cmake --preset $P > build/$P/Testing/Logs/verify.configure.log 2>&1 || { echo "CONFIGURE FAIL $P"; exit 1; }
  cmake --build --preset $P -- -k 0 > build/$P/Testing/Logs/verify.build.log 2>&1 || { echo "BUILD FAIL $P"; grep -E "error|FAILED" build/$P/Testing/Logs/verify.build.log | head -30; wait $LINUX_PID; tail -n 20 build/verify.linux.log; exit 1; }
  echo "build warnings: $(grep -c 'warning:' build/$P/Testing/Logs/verify.build.log)"
  if [ $P = Ninja-Debug ]; then
    echo "== unit $(date +%T)"
    build/$P/Bin/App.exe --test > build/$P/Testing/Logs/verify.unit.log 2>&1
    echo "unit exit=$?"
    tail -3 build/$P/Testing/Logs/verify.unit.log
  fi
  echo "== ctest $(date +%T)"
  (cd build/$P && ctest -j6 --output-on-failure > Testing/Logs/verify.ctest.log 2>&1)
  echo "ctest exit=$?"
  grep -E "tests passed|tests failed|\*\*\*Failed|Timeout|Not Run" build/$P/Testing/Logs/verify.ctest.log | head -30
done
P=Ninja-Debug-Server
mkdir -p build/$P/Testing/Logs
echo "== $P build $(date +%T)"
cmake --preset $P > build/$P/Testing/Logs/verify.configure.log 2>&1 || { echo "CONFIGURE FAIL $P"; exit 1; }
cmake --build --preset $P -- -k 0 > build/$P/Testing/Logs/verify.build.log 2>&1 || { echo "BUILD FAIL $P"; grep -E "error|FAILED" build/$P/Testing/Logs/verify.build.log | head -30; exit 1; }
echo "build warnings: $(grep -c 'warning:' build/$P/Testing/Logs/verify.build.log)"
build/$P/Bin/Server.exe --test -unattended > build/$P/Testing/Logs/verify.unit.log 2>&1
echo "server unit exit=$?"
grep -E "case run" build/$P/Testing/Logs/verify.unit.log
echo "== shipping-check $(date +%T)"
py -3 -m Scripts shipping-check 2>&1 | tail -n 8
echo "shipping-check exit=${PIPESTATUS[0]}"
echo "== linux 기다림 $(date +%T)"
wait $LINUX_PID
echo "linux exit=$?"
tail -n 40 build/verify.linux.log
echo "== done $(date +%T) sha=$SHA"
