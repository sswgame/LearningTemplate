#!/bin/bash
# 묶음 끝 전체 검증: Windows(린트, Debug · Release · 서버 · 배포 빌드, 단위 · ctest)가 모두 통과한 뒤에만 Linux 를 돌린다.
# 인자는 Linux 로 넘긴다(--record). --no-linux 면 Windows 까지만 돈다(Linux 골든이 곧 바뀌는 묶음 사이). 로그는 build/<프리셋>/Testing/Logs/verify.* · build/verify.*.log. 끝 줄 VERIFIED=<sha> 가 나와야 push 한다.
# --show 는 게임 · 에디터 테스트 창을 화면에 띄우고 테스트를 하나씩 돌린다(사람이 지켜볼 때). --dry-run 은 돌릴 명령만 차례대로 찍는다(스크립트를 고친 뒤 확인).
# --from build|cpu|gpu-release|gpu-debug|linux 는 앞 단계를 건너뛴다(linux 는 Windows 가 이미 통과했고 그 뒤 고친 것이 Linux 쪽 스크립트 · 환경뿐일 때). 앞 단계가 통과한 뒤
# 바뀐 것이 그 단계에 영향을 주지 않을 때만 쓴다(실패한 단계부터 다시 돌린다). 끝 줄에 어디서 시작했는지 적는다.
#
# 단계(시간 판정이 CPU 경합에 흔들리는 일과 그렇지 않은 일을 가른다 — 시나리오는 벽시계 시한 · 프레임에 걸린 그림을 판정하므로 GPU 단계는 혼자 돈다):
#   build       lint --all(뒤에서) ‖ Debug → Release → 서버 빌드. 빌드는 한 번에 하나(ninja 가 코어를 다 쓴다)이고, 린트는 빌드와 겹친다.
#   cpu         Debug ctest -L nogpu(단위 · 셰이더 다시 굽기 · 파서 …) ‖ 서버 단위 → launch-args → shipping-check(배포 빌드 · 기동). 시간을 판정하지 않는 CPU 일만 겹친다.
#   gpu-release Release ctest 전체 — 시나리오 그림 · 골든 · 기록 대조의 정본(모든 게임 · 네 백엔드).
#   gpu-debug   Debug ctest -LE nogpu — 체인은 시나리오마다 백엔드 하나(-scenario-backend-split=all)로 Debug 단언만 지난다(Test/Scenario/CMakeLists.txt).
# 예전(2026-11-13 측정 53 분)은 단계가 모두 차례였고, 같은 체인을 두 구성에서 네 백엔드로 다 돌았고, 셰이더 다시 굽기가 두 구성에서 혼자(RUN_SERIAL) 돌았고, Debug 단위를 ctest 밖에서 한 번 더 돌았다.
cd "$(dirname "$0")/../.." || exit 2
source Scripts/common/git.sh
SHA=$(sw_git rev-parse HEAD)
FROM=build
DRY=0
NO_LINUX=0
RECORD=()
# ctest 슬롯 수는 설정 값이다(Config/Dev/Verify.json — 없으면 멈춘다).
CTEST_JOBS=$(py -3 -c "import json,sys; print(json.load(open('Config/Dev/Verify.json', encoding='utf-8'))['_ctestJobs'])") || { echo "Config/Dev/Verify.json 의 _ctestJobs 를 읽지 못했다"; exit 2; }
CTEST_JOBS=${CTEST_JOBS//$'\r'/}
while true; do
  case "$1" in
    --from) FROM=$2; shift 2 ;;
    # --show: 무인 실행의 창 숨김만 끈다(App 이 SW_SHOW_WINDOWS=1 을 본다 — 판정 · 골든은 같다). 보는 사람을 위해 테스트를 하나씩 돌린다.
    --show) export SW_SHOW_WINDOWS=1; CTEST_JOBS=1; shift ;;
    --dry-run) DRY=1; shift ;;
    --no-linux) NO_LINUX=1; shift ;;
    --record) RECORD=(--record); shift ;;
    "") break ;;
    # 인자 순서와 무관하게 모두 여기서 읽는다 — 예전에는 모르는 인자에서 멈춰 `--no-linux --from gpu-release` 의 --from 을 버리고 처음부터 돌았다.
    *) echo "모르는 인자: $1 (--from · --show · --dry-run · --no-linux · --record)"; exit 2 ;;
  esac
done
case "$FROM" in
  build) STAGE=1 ;;
  cpu) STAGE=2 ;;
  gpu-release) STAGE=3 ;;
  gpu-debug) STAGE=4 ;;
  linux) STAGE=5 ;;
  *) echo "--from 은 build · cpu · gpu-release · gpu-debug · linux 중 하나다: $FROM"; exit 2 ;;
esac

windows_failed() {
  echo "WINDOWS_FAILED ($1) - Linux 를 돌리지 않는다 sha=$SHA"
  exit 1
}

# 명령 하나를 돌린다 — 첫 인자는 출력 파일(빈 글이면 화면). --dry-run 이면 찍기만 하고 통과로 친다.
run() {
  local OUT=$1; shift
  if [ $DRY -eq 1 ]; then
    echo "[dry] $*${OUT:+ > $OUT}"
    return 0
  fi
  if [ -n "$OUT" ]; then "$@" > "$OUT" 2>&1; else "$@"; fi
}

build_preset() {
  local P=$1
  local LOGS=build/$P/Testing/Logs
  mkdir -p $LOGS
  echo "== $P build $(date +%T)"
  run $LOGS/verify.configure.log cmake --preset $P || windows_failed "$P configure"
  if ! run $LOGS/verify.build.log cmake --build --preset $P -- -k 0; then
    grep -E "error|FAILED" $LOGS/verify.build.log | head -30
    windows_failed "$P build"
  fi
  [ $DRY -eq 1 ] || echo "build warnings: $(grep -c 'warning:' $LOGS/verify.build.log)"
}

# 격리 표(Config/Rules/Test/Quarantine.json)의 테스트를 ctest -E 로 뺀다. 만료가 지났거나 칸이 빠진 표는 검증 실패다(격리가 말없이 늘어지지 않게).
QUARANTINE_EXCLUDE=$(py -3 Scripts/dev/test_history.py quarantine --ctest-exclude) || { echo "$QUARANTINE_EXCLUDE"; windows_failed "quarantine table"; }
QUARANTINE_EXCLUDE=${QUARANTINE_EXCLUDE//$'\r'/}   # Windows 파이썬의 print 는 CRLF 를 쓴다 — 정규식 끝에 CR 이 붙으면 아무것도 빼지 못한다
[ -z "$QUARANTINE_EXCLUDE" ] || echo "격리로 뺀다: $QUARANTINE_EXCLUDE"

# run_ctest <프리셋> <로그 이름> [ctest 거르기 인자…] — 돌리고 이력에 쌓는다(진 실행도 — 흔들림 · p95 추세는 여러 묶음을 모아야 보인다, test_history.py report).
# 통과면 0, 아니면 1 을 돌려준다(뒤에서 도는 갈래가 부르므로 여기서 끝내지 않는다).
run_ctest() {
  local P=$1 NAME=$2; shift 2
  local EXCLUDE=()
  [ -z "$QUARANTINE_EXCLUDE" ] || EXCLUDE=(-E "$QUARANTINE_EXCLUDE")
  echo "== $P ctest $NAME $(date +%T)"
  (cd build/$P && run Testing/Logs/verify.ctest.$NAME.log ctest -j$CTEST_JOBS --output-on-failure "${EXCLUDE[@]}" "$@")
  local RC=$?
  echo "$P ctest $NAME exit=$RC"
  [ $DRY -eq 1 ] || grep -E "tests passed|tests failed|\*\*\*Failed|Timeout|Not Run" build/$P/Testing/Logs/verify.ctest.$NAME.log | head -30
  run "" py -3 Scripts/dev/test_history.py ingest build/$P/Bin/Saved/Automation --commit "$SHA" || { echo "$P test history ingest 실패"; return 1; }
  return $RC
}

# 겹쳐 도는 갈래 — 뒤에서 띄우고 pid 를 모은다. --dry-run 은 차례로 찍는다(출력이 섞이지 않게).
LANE_PIDS=()
LANE_NAMES=()
start_job() {
  local NAME=$1; shift
  if [ $DRY -eq 1 ]; then echo "[dry] -- 갈래 $NAME (겹쳐 돈다)"; "$@"; return; fi
  "$@" &
  LANE_PIDS+=($!)
  LANE_NAMES+=("$NAME")
}
wait_jobs() {
  local FAILED=""
  for i in "${!LANE_PIDS[@]}"; do
    wait "${LANE_PIDS[$i]}" || FAILED="$FAILED ${LANE_NAMES[$i]}"
  done
  LANE_PIDS=()
  LANE_NAMES=()
  [ -z "$FAILED" ] || windows_failed "${FAILED# }"
}

job_lint() {
  # 커밋 훅은 스테이지된 파일만 본다(Scripts/lint/lint_scope.py) — 낡은 예외 · 기준선 같은 저장소 전체 판정은 여기서 한 번 낸다.
  echo "== lint --all $(date +%T)"
  run build/verify.lint.log py -3 -m Scripts lint --all
  local RC=$?
  [ $DRY -eq 1 ] || tail -n 20 build/verify.lint.log
  echo "lint exit=$RC"
  return $RC
}

job_debug_cpu() {
  run_ctest Ninja-Debug nogpu -L nogpu || return 1
  # App --test 의 케이스 수(보고에 적는다) — 보고 JSON 에서 읽는다(엔진 로그 파일에는 요약 줄이 없다). 보고 줄은 판정을 바꾸지 않는다.
  [ $DRY -eq 1 ] || py -3 -c "import json; d=json.load(open('build/Ninja-Debug/Testing/Reports/UnitTests.json', encoding='utf-8')); print(f\"App --test: {d['runCount']} case run(s), {d['failedCount']} failed\")" || true
  return 0
}

job_server_tools() {
  echo "== server unit $(date +%T)"
  run build/Ninja-Debug-Server/Testing/Logs/verify.unit.log build/Ninja-Debug-Server/Bin/Server.exe --test -unattended
  local RC=$?
  echo "server unit exit=$RC"
  [ $DRY -eq 1 ] || grep -E "case run" build/Ninja-Debug-Server/Testing/Logs/verify.unit.log
  [ $RC -eq 0 ] || return 1

  # 도구 테스트: 실행 인자 GUI(VS Code 확장 Tools/launch-args — 확장이라 JS 다)가 Source 의 매크로 · 인자 표를 읽는다. 엔진 쪽 이름 · 형식이 바뀌면 깨지는데
  # 어디서도 돌지 않아 오래 깨져 있었다(2026-11-08). 확장 도구가 VS Code 안의 Node 로 돌린다(따로 깐 node 가 필요 없다).
  echo "== launch-args tests $(date +%T)"
  run build/verify.launchargs.log py -3 Tools/launch-args/Scripts/ExtensionTool.py test
  RC=$?
  [ $DRY -eq 1 ] || grep -E "^ℹ (pass|fail)" build/verify.launchargs.log
  [ $RC -eq 0 ] || { echo "launch-args tests exit=$RC"; return 1; }

  # 배포 구성: 빌드(쿠킹 포함) · 기동 · 종료. 시간을 판정하지 않으므로 Debug nogpu 와 겹친다.
  echo "== shipping-check $(date +%T)"
  run build/verify.shipping.log py -3 -m Scripts shipping-check
  RC=$?
  [ $DRY -eq 1 ] || tail -n 8 build/verify.shipping.log
  echo "shipping-check exit=$RC"
  return $RC
}

mkdir -p build
if [ $STAGE -le 1 ]; then
  start_job "lint --all" job_lint
  build_preset Ninja-Debug
  build_preset Ninja-Release
  build_preset Ninja-Debug-Server
  wait_jobs
fi

if [ $STAGE -le 2 ]; then
  # 임포트를 먼저 끝낸다 — 낡은 산출물을 Content 에 다시 쓰는 일이라 배포 빌드의 쿠킹이 그 파일을 읽는 동안 돌지 않게(ctest 의 고정 단계 ImportedAssets 는 아래에서 곧바로 끝난다).
  run_ctest Ninja-Debug import -L import || windows_failed "Ninja-Debug import"
  start_job "Debug nogpu" job_debug_cpu
  start_job "server · tools · shipping" job_server_tools
  wait_jobs
fi

if [ $STAGE -le 3 ]; then
  run_ctest Ninja-Release all || windows_failed "Ninja-Release ctest"
fi

if [ $STAGE -le 4 ]; then
  run_ctest Ninja-Debug gpu -LE nogpu || windows_failed "Ninja-Debug ctest gpu"
fi

if [ $NO_LINUX -eq 1 ]; then
  echo "WINDOWS_VERIFIED=$SHA (Linux 를 돌리지 않았다, 시작 단계 $FROM)"
  exit 0
fi
echo "== WINDOWS_OK, linux 시작 $(date +%T)"
PYTHONUTF8=1 run build/verify.linux.log py -3 Scripts/dev/linux_scenarios.py all "${RECORD[@]}"
RC=$?
[ $DRY -eq 1 ] || tail -n 40 build/verify.linux.log
# 새니타이저(ASan + UBSan, Linux-Debug-ASan 단위 테스트) — 묶음마다 한 번(linux-verify 스킬). 체인과 CPU 를 다투지 않게 all 뒤에 차례로 돌고, 지면 끝 줄에 적는다(push 는 WINDOWS_OK 로 이미 정해졌다).
# --record 실행은 건너뛴다: sanitize 도 클론을 HEAD 로 되돌려(reset --hard · clean) 아직 옮기지 않은 기록 골든을 지운다 — goldens 로 옮긴 뒤 따로 돌린다.
SAN_RC=0
case " ${RECORD[*]} " in
  *" --record "*) echo "== sanitize 건너뜀(--record — goldens 로 옮긴 뒤 linux_scenarios.py sanitize 를 따로 돌린다)"; SAN_RC=skip ;;
  *)
    echo "== sanitize address $(date +%T)"
    PYTHONUTF8=1 run build/verify.sanitize.log py -3 Scripts/dev/linux_scenarios.py sanitize address
    SAN_RC=$?
    [ $DRY -eq 1 ] || tail -n 20 build/verify.sanitize.log
    ;;
esac
echo "== done $(date +%T)"
[ $RC -eq 0 ] || echo "LINUX_FAILED sha=$SHA"
[ "$SAN_RC" = 0 ] || [ "$SAN_RC" = skip ] || echo "SANITIZE_FAILED (address) sha=$SHA - build/verify.sanitize.log"
[ $RC -eq 0 ] && { [ "$SAN_RC" = 0 ] || [ "$SAN_RC" = skip ]; } || exit 1
SAN_NOTE=""
[ "$SAN_RC" = skip ] && SAN_NOTE=", sanitize 건너뜀"
echo "VERIFIED=$SHA (시작 단계 $FROM$SAN_NOTE)"
