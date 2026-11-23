#!/bin/bash
# 묶음 끝 전체 검증: Windows(린트, Release · ASan · 서버 · 배포 빌드, 단위 · ctest)가 모두 통과한 뒤에만 Linux 를 돌린다.
# 인자는 Linux 로 넘긴다(--record). --no-linux 면 Windows 까지만 돈다(Linux 골든이 곧 바뀌는 묶음 사이). 로그는 build/<프리셋>/Testing/Logs/verify.* · build/verify.*.log.
# 시작에 작업 트리가 깨끗해야 하고(검증한 것이 커밋 그 자체), 끝에 HEAD 가 그대로이고 추적 파일이 바뀌지 않았어야 한다. Windows 가 모두 통과하면 build/verify.ok 에 sha 를 적고
# 끝 줄 WINDOWS_OK=<sha> 를 찍는다 — git pre-push 훅(Scripts/dev/verify_record.py)이 main 으로 가는 sha 가 그것인지 본다. Linux 까지 돌면 VERIFIED=<sha> 가 마지막 줄이다.
# --show 는 게임 · 에디터 테스트 창을 화면에 띄우고 테스트를 하나씩 돌린다(사람이 지켜볼 때). --dry-run 은 돌릴 명령만 차례대로 찍는다(스크립트를 고친 뒤 확인).
# --self-check 는 판정 요약(print_results)의 자체 검사만 하고 끝난다(빌드 · 테스트 없이). 끝 줄 SELF_CHECK_OK. --help 는 이 머리말을 찍는다(단계 목록의 정본 — 스킬은 여기를 가리킨다).
# --from build|changed|cpu|gpu-release|linux 는 앞 단계를 건너뛴다(linux 는 Windows 가 이미 통과했고 그 뒤 고친 것이 Linux 쪽 스크립트 · 환경뿐일 때). 앞 단계가 통과한 뒤
# 바뀐 것이 그 단계에 영향을 주지 않을 때만 쓴다(실패한 단계부터 다시 돌린다). 끝 줄에 어디서 시작했는지 적는다.
# 단계 기록: 단계가 통과하면 그 sha 를 build/verify.stages.json 에 적고(Scripts/dev/verify_record.py), 다음 실행에서 그 sha 뒤 바뀐 파일이 모두 입력이 아닌 경로
# (Config/Dev/Verify.json 의 _nonInputPaths — 문서 · 스킬)면 그 단계를 건너뛴다. 거꾸로 적는 목록이라 빠뜨리면 더 돌 뿐이다. lint --all 은 문서 · 스킬도 읽어 build 범위면 늘 돈다.
# 이력(Scripts/dev/test_history.py, Saved/TestHistory/history.sqlite): ctest 보고는 단계 시작 표지 뒤의 것만 이 sha · 검증 실행(verify)으로 쌓고, 단계마다 걸린 시간을 쌓아
# 지난 p95 보다 느리면 경고한다. Windows 끝에 흔들림 판정(test_history.py flaky — 끝 줄 FLAKY=)이 격리 표 밖 흔들림이 문턱을 넘으면 진다.
#
# 단계(시간 판정이 CPU 경합에 흔들리는 일과 그렇지 않은 일을 가른다 — 시나리오는 벽시계 시한 · 프레임에 걸린 그림을 판정하므로 GPU 단계는 혼자 돈다):
#   build       lint --all(뒤에서) ‖ Release 빌드 → 임포트 → (ASan 빌드 → 서버 빌드 ‖ 배포 프리셋 둘 빌드 · 쿠킹 → shipping-check 기동 · 종료). 배포 빌드는 증분 빌드의 링크 꼬리에 비는 코어를 쓴다(2026-11-13: CPU 단계 11.5 분 중 9 분이 배포 빌드였다).
#               ASan 빌드가 끝나면 tidy 를 뒤에서 띄운다(아래 tidy).
#               Debug 는 짓지 않는다 — 단언 · 검사는 SW_DO_CHECKS 로 Release 에 있고, CPU 단위는 ASan(Release + clang-cl ASan)이 돈다(사용자 결정 2026-11-18).
#   changed     origin/main 뒤에 들어온 · 바뀐 테스트 · 시나리오만 Release 로 먼저(test_history.py changed). 돈 적 없는 테스트 · 기준 파일의 실패를
#               전체 실행 중간이 아니라 빌드 직후에 갈래 이름과 함께 낸다. 없는 골든 · 스냅숏만 쓰고(MissingBaseline) 쓴 것이 있으면 멈춘다 — 보고 커밋한 뒤 --from changed.
#               지면 cpu · gpu-release 를 시작하지 않는다. 끝 줄 CHANGED_OK=<sha> · CHANGED_FAILED.
#   cpu         ASan ctest -L nogpu(단위 · 셰이더 다시 굽기 · 파서 …) ‖ 서버(Release) 단위 → launch-args(--from 으로 build 를 건너뛰었으면 → shipping-check 빌드 · 기동) ‖ shader warm.
#               시간을 판정하지 않는 CPU 일만 겹친다.
#               shader warm: 지난 Release 체인이 남긴 셰이더 요청 기록(build/Ninja-Release/Testing/ShaderRequests/*.shaderrequests — 체인 자식마다 하나)만 Release App 으로
#               컴파일해 그 빌드 폴더의 셰이더 DDC 를 네 백엔드 모두 채운다(`App --cook-shaders -shader-request-dir=`). 셰이더 · 공용 hlsli 를 고친 묶음도 GPU 단계가 캐시 미스 컴파일을
#               떠안지 않는다(2026-11-19: dx11 empty 체인 19 분 중 FXC 559 개 — 캐시가 차면 74 초). 기록이 없으면(첫 실행) 할 일이 없다고 찍고 끝난다. 전체 굽기는 5 만 개를 넘어 넣지 않는다.
#   gpu-release Release ctest 전체 — 시나리오 그림 · 골든 · 기록 대조의 정본(모든 게임 · 네 백엔드). 체인이 다음 묶음의 shader warm 이 읽을 요청 기록을 남긴다.
#               체인(부하 측정 제외)은 그래픽 API 검증 층(-rhi-validation) · 교착 탐지(-deadlock-detection) · bindless 죽은 번호 검사를 켠다 — 검증 층 오류 · 경고는 로그 게이트에서 진다.
#   tidy        바뀐 파일 clang-tidy(Scripts/lint/clang_tidy.py --changed origin/main — ASan 컴파일 DB, 통과한 TU 는 포함 닫힘 해시로 캐시). 단계가 아니라 갈래다:
#               ASan 빌드 직후 뒤에서 띄워 서버 · 배포 빌드, changed, cpu 와 겹쳐 돌고(2026-11-21: 746 TU 2055 초를 build 단계 끝에서 기다렸다),
#               gpu-release 앞에서 합류한다 — GPU 단계는 혼자 돈다. clang-tidy 는 낮은 우선순위라 겹친 테스트가 CPU 를 먼저 받는다(테스트는 부하와 무관해야 한다).
#               지면 gpu-release 는 그대로 돌고 끝(WINDOWS_OK 앞)에서 진다. 단계 기록은 이름 tidy 로 따로 두고 --from changed|cpu|gpu-release 로도
#               건너뛰지 않는다(기록이 이 sha 를 덮을 때만 건너뛴다) — tidy 가 진 뒤 --from 으로 다시 돌려도 WINDOWS_OK 가 tidy 없이 나오지 않는다.
#   ctest 는 격리 표(Config/Rules/Test/Quarantine.json)를 -E 로 빼고 돈다(만료가 지났으면 처음에 진다). 슬롯 수는 Config/Dev/Verify.json 의 _ctestJobs.
#   (없앤 gpu-debug: Debug 로 같은 체인을 한 번 더 돌던 단계 14.6 분 — 단언 · 검사가 SW_DO_CHECKS 로 Release 에도 들어 Release 체인이 지난다. Core/Foundation/Macros.h 4-1)
# 예전(2026-11-13 측정 53 분)은 단계가 모두 차례였고, 같은 체인을 두 구성에서 네 백엔드로 다 돌았고, 셰이더 다시 굽기가 두 구성에서 혼자(RUN_SERIAL) 돌았고, Debug 단위를 ctest 밖에서 한 번 더 돌았다.
cd "$(dirname "$0")/../.." || exit 2
source Scripts/common/git.sh
SHA=$(sw_git rev-parse HEAD)
FROM=build
DRY=0
NO_LINUX=0
SELF_CHECK=0
RECORD=()
# clang-tidy 갈래의 pid · 진 결과(start_tidy · join_tidy) — windows_failed 가 처음부터 읽는다.
TIDY_PID=""
TIDY_FAILED=0
TIDY_STARTED=$SECONDS
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
    --self-check) SELF_CHECK=1; shift ;;
    --help) sed -n '2,/^cd /p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; exit 0 ;;
    "") break ;;
    # 인자 순서와 무관하게 모두 여기서 읽는다 — 예전에는 모르는 인자에서 멈춰 `--no-linux --from gpu-release` 의 --from 을 버리고 처음부터 돌았다.
    *) echo "모르는 인자: $1 (--from · --show · --dry-run · --no-linux · --record · --self-check · --help)"; exit 2 ;;
  esac
done

# 판정 요약 — 실행기가 판정 값 하나에서 찍은 끝 줄만 모은다: 단위 실행의 TEST_RESULT=, 시나리오 부모의 SCENARIO_RESULT=,
# 진 단계를 (단계 · 종류 · 까닭)으로 접어 백엔드를 붙인 per-backend failure: 줄. 통과 · 건너뜀 끝 줄은 뺀다. 로그 문구를 해석하지 않는다. 인자는 읽을 파일들이다.
print_results() {
  grep -h -o -E "(TEST_RESULT=|SCENARIO_RESULT=|per-backend failure: ).*" "$@" 2>/dev/null |
    tr -d '\r' | grep -v -E "^(TEST_RESULT=PASSED|SCENARIO_RESULT=(PASSED|SKIPPED)) " | sort -u | head -40
}

# 자체 검사: 통과 줄은 빠지고, 진 끝 줄과 접은 줄은 남는다.
self_check() {
  local DIR
  DIR=$(mktemp -d) || return 1
  printf '%s\n' "[2026-11-18 10:00:00] [Info] [Scenario] per-backend failure: game.vfxsort: GoldenMismatch dx11,dx12,vk,gl - differs" \
    "[2026-11-18 10:00:01] [Info] [Scenario] SCENARIO_RESULT=FAILED stages=8 failed=4 notRun=0" > "$DIR/Scenario.playground.log"
  printf '%s\r\n' "TEST_RESULT=PASSED run=10 failed=0 skipped=1" "SCENARIO_RESULT=PASSED stages=2 failed=0 notRun=0" > "$DIR/verify.ctest.all.log"
  local OUT
  OUT=$(print_results "$DIR"/*.log)
  rm -rf "$DIR"
  [ "$(printf '%s\n' "$OUT" | wc -l)" -eq 2 ] || { echo "self-check: 두 줄이어야 한다: $OUT"; return 1; }
  printf '%s\n' "$OUT" | grep -q "^SCENARIO_RESULT=FAILED stages=8 failed=4 notRun=0$" || { echo "self-check: 진 끝 줄이 없다: $OUT"; return 1; }
  printf '%s\n' "$OUT" | grep -q "^per-backend failure: game.vfxsort: GoldenMismatch dx11,dx12,vk,gl - differs$" || { echo "self-check: 접은 줄이 없다: $OUT"; return 1; }
  echo "SELF_CHECK_OK"
}
if [ $SELF_CHECK -eq 1 ]; then
  self_check
  exit $?
fi
# 빌드 · 구성 · ctest 는 메인 작업 폴더 하나에서만(공용 vcpkg · 디스크, 사용자 지시 2026-11-12) — 연결된 워크트리는 .git 이 파일이다. --dry-run 은 명령만 찍어 워크트리에서도 된다.
if [ -f .git ] && [ $DRY -eq 0 ]; then
  echo "워크트리($(pwd))에서 묶음 검증을 시작하지 않는다 — 메인 작업 폴더에서 돌린다(--dry-run 은 된다)"
  exit 2
fi
case "$FROM" in
  build) STAGE=1 ;;
  changed) STAGE=2 ;;
  cpu) STAGE=3 ;;
  gpu-release) STAGE=4 ;;
  linux) STAGE=5 ;;
  *) echo "--from 은 build · changed · cpu · gpu-release · linux 중 하나다: $FROM"; exit 2 ;;
esac

# 검증한 것이 커밋 SHA 그 자체여야 한다 — 추적 파일의 고친 것 · 무시되지 않은 새 파일이 있으면 SHA 가 아닌 트리를 검증한다. 끝에는 HEAD 가 그대로이고
# 트리가 여전히 깨끗한지 본다(빌드 · 테스트가 커밋되는 파일을 고치면 — 예: 리플렉션 생성의 AssetReferenceFields.generated.json — 거기서 진다).
list_tree_changes() { sw_git status --porcelain --untracked-files=normal | tr -d '\r'; }
TREE_CHANGES=$(list_tree_changes)
if [ -n "$TREE_CHANGES" ]; then
  if [ $DRY -eq 1 ]; then
    echo "[dry] 작업 트리가 깨끗하지 않다 — 실제 실행은 여기서 멈춘다"
  else
    echo "작업 트리가 깨끗하지 않다 — 커밋한 뒤 돌린다(검증한 SHA 와 트리가 달라진다):"
    printf '%s\n' "$TREE_CHANGES" | head -20
    exit 2
  fi
fi

# 단계 시간(test_history.py stage) — begin_stage 가 시작 시각을 잡고, 통과 · 건너뜀 · 실패 때 한 줄씩 쌓는다. 갈래(서브셸)에서는 쌓지 않는다(부모가 쌓는다).
CURRENT_STAGE=""
STAGE_STARTED=$SECONDS
begin_stage() { CURRENT_STAGE=$1; STAGE_STARTED=$SECONDS; }
record_stage_time() {   # <이름> <passed|failed|skipped> [시작 시각 — 없으면 begin_stage 의 것]
  [ $DRY -eq 1 ] && return 0
  [ "$BASHPID" = "$$" ] || return 0
  py -3 -m Scripts test-history stage "$1" --commit "$SHA" --result "$2" --seconds $((SECONDS - ${3:-$STAGE_STARTED})) || echo "단계 시간을 쌓지 못했다($1)"
}

windows_failed() {
  [ -n "$CURRENT_STAGE" ] && record_stage_time "$CURRENT_STAGE" failed
  echo "WINDOWS_FAILED ($1)$([ $TIDY_FAILED -eq 0 ] || echo ' + clang-tidy') - Linux 를 돌리지 않는다 sha=$SHA"
  wait_background
  exit 1
}

# 겹쳐 돌던 갈래(배포 빌드 · 쿠킹 · clang-tidy 등)를 끝까지 기다린다 — 두고 나가면 다음 실행과 같은 빌드 폴더를 다퉈 구성이
# "failed recompaction: Permission denied" 로 진다(2026-11-17). clang-tidy 는 끝까지 돌면 통과한 TU 가 캐시에 남아 다음 실행이 빠르다.
# 갈래 안(서브셸)에서 부르면 남의 pid 라 wait 가 바로 돌아온다.
wait_background() {
  for PID in "${LANE_PIDS[@]}" $TIDY_PID; do wait "$PID" 2>/dev/null; done
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
  [ $DRY -eq 1 ] || mkdir -p $LOGS   # --dry-run 은 빌드 폴더를 만들지 않는다(워크트리에서 명령 순서만 볼 때)
  echo "== $P build $(date +%T)"
  # 재압축이 바깥 프로세스(백신 검사)에 막혀 지면 — 이 문구일 때만 한 번 더 구성한다. 구성 시작이 기록을 제자리에 돌린다(cmake/Modules/Build/NinjaBuildLogs.cmake).
  run $LOGS/verify.configure.log cmake --preset $P ||
    { grep -aq "failed recompaction: Permission denied" $LOGS/verify.configure.log && run $LOGS/verify.configure.log cmake --preset $P; } ||
    windows_failed "$P configure"
  if ! run $LOGS/verify.build.log cmake --build --preset $P -- -k 0; then
    grep -E "error|FAILED" $LOGS/verify.build.log | head -30
    windows_failed "$P build"
  fi
  [ $DRY -eq 1 ] || echo "build warnings: $(grep -c 'warning:' $LOGS/verify.build.log)"
}

# 격리 표(Config/Rules/Test/Quarantine.json)의 테스트를 ctest -E 로 뺀다. 만료가 지났거나 칸이 빠진 표는 검증 실패다(격리가 말없이 늘어지지 않게).
QUARANTINE_EXCLUDE=$(py -3 -m Scripts test-history quarantine --ctest-exclude) || { echo "$QUARANTINE_EXCLUDE"; windows_failed "quarantine table"; }
QUARANTINE_EXCLUDE=${QUARANTINE_EXCLUDE//$'\r'/}   # Windows 파이썬의 print 는 CRLF 를 쓴다 — 정규식 끝에 CR 이 붙으면 아무것도 빼지 못한다
[ -z "$QUARANTINE_EXCLUDE" ] || echo "격리로 뺀다: $QUARANTINE_EXCLUDE"

# run_ctest <프리셋> <로그 이름> [ctest 거르기 인자…] — 돌리고 이력에 쌓는다(진 실행도 — 흔들림 · p95 추세는 여러 묶음을 모아야 보인다, test_history.py report).
# 통과면 0, 아니면 1 을 돌려준다(뒤에서 도는 갈래가 부르므로 여기서 끝내지 않는다).
run_ctest() {
  local P=$1 NAME=$2; shift 2
  local EXCLUDE=()
  [ -z "$QUARANTINE_EXCLUDE" ] || EXCLUDE=(-E "$QUARANTINE_EXCLUDE")
  echo "== $P ctest $NAME $(date +%T)"
  [ $DRY -eq 1 ] || { mkdir -p build/$P/Testing/Logs && touch build/$P/Testing/Logs/verify.ctest.$NAME.log.start; }   # 이번 실행이 쓴 로그를 가르는 시각 표지(print_results)
  ({ [ $DRY -eq 1 ] || cd build/$P; } && run Testing/Logs/verify.ctest.$NAME.log ctest -j$CTEST_JOBS --output-on-failure "${EXCLUDE[@]}" "$@")
  local RC=$?
  echo "$P ctest $NAME exit=$RC"
  [ $DRY -eq 1 ] || grep -E "tests passed|tests failed|\*\*\*Failed|Timeout|Not Run" build/$P/Testing/Logs/verify.ctest.$NAME.log | head -30
  # 진 실행의 판정 요약(끝 줄 · 백엔드로 접은 진 단계) — 단계 끝마다 찍는다. 이번 실행이 쓴 로그만 본다(앞 실행이 남긴 로그를 섞지 않게 ctest 로그보다 새 것).
  [ $DRY -eq 1 ] || [ $RC -eq 0 ] || print_results build/$P/Testing/Logs/verify.ctest.$NAME.log $(find build/$P/Testing/Logs -maxdepth 1 -name "Scenario.*.log" -newer build/$P/Testing/Logs/verify.ctest.$NAME.log.start 2>/dev/null)
  # 표지(.log.start) 앞에 시작한 보고는 손 실행 · 다시 돌리기가 남긴 것이라 sha 없이 manual 로 들어간다(흔들림 판정은 verify 만 본다).
  run "" py -3 -m Scripts test-history ingest build/$P/Bin/Saved/Automation --commit "$SHA" --kind verify --since build/$P/Testing/Logs/verify.ctest.$NAME.log.start ||
    { echo "$P test history ingest 실패"; return 1; }
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

# 새로 들어온 · 바뀐 테스트 · 시나리오만 먼저. 표와 끝 줄을 찍고, 지면 뒤 단계를 시작하지 않는다 — 끝 줄은 CHANGED_FAILED 다.
run_changed() {
  echo "== changed $(date +%T)"
  run build/verify.changed.log py -3 -m Scripts test-history changed --preset Ninja-Release
  local RC=$?
  [ $DRY -eq 1 ] || grep -v "^\[Changed\] run " build/verify.changed.log | tr -d '\r'
  [ $RC -eq 0 ] && return 0
  echo "CHANGED_FAILED sha=$SHA - build/verify.changed.log (cpu · gpu-release 를 시작하지 않는다)"
  wait_background
  exit 1
}

job_tidy() {
  # 정적 분석: 이 묶음이 바꾼 파일(origin/main 기준)만 — 방금 지은 ASan 의 컴파일 DB 를 쓴다(유니티를 끈 구성이라 원본 TU 가 DB 에 있다, clang_tidy.py 기본 프리셋). 오류로 오르는 검사는 .clang-tidy 의 WarningsAsErrors.
  # 빌드 폴더에는 이 로그만 쓴다(결과 캐시는 Saved/TidyCache).
  echo "== tidy $(date +%T)"
  run build/Ninja-Release-ASan/Testing/Logs/verify.tidy.log py -3 -m Scripts tidy --changed origin/main
  local RC=$?
  [ $DRY -eq 1 ] || tail -n 25 build/Ninja-Release-ASan/Testing/Logs/verify.tidy.log
  echo "tidy exit=$RC"
  return $RC
}

# clang-tidy 갈래(머리말 tidy) — ASan 빌드 뒤 띄우고 gpu-release 앞에서 join_tidy 로 합류한다. 진 결과는 TIDY_FAILED 로 들고 있다가 끝에서 진다.
start_tidy() {
  TIDY_STARTED=$SECONDS
  if [ $DRY -eq 1 ]; then echo "[dry] -- 갈래 clang-tidy (뒤에서 — changed · cpu 와 겹치고 gpu-release 앞에서 합류)"; job_tidy; return; fi
  job_tidy &
  TIDY_PID=$!
}
join_tidy() {
  [ $RUN_TIDY -eq 1 ] || return 0
  echo "== clang-tidy 합류 $(date +%T)"
  local RC=0
  if [ -n "$TIDY_PID" ]; then wait "$TIDY_PID"; RC=$?; TIDY_PID=""; fi
  if [ $RC -eq 0 ]; then
    record_stage_time tidy passed $TIDY_STARTED
    [ $DRY -eq 1 ] || py -3 -m Scripts verify-record pass tidy "$SHA" || windows_failed "tidy 통과 기록"
  else
    record_stage_time tidy failed $TIDY_STARTED
    TIDY_FAILED=1
    echo "clang-tidy 가 졌다(build/Ninja-Release-ASan/Testing/Logs/verify.tidy.log) - gpu-release 는 돌고 끝에서 진다"
  fi
}

job_sanitizer_cpu() {
  run_ctest Ninja-Release-ASan nogpu -L nogpu || return 1
  # App --test 의 케이스 수(보고에 적는다) — 보고 JSON 에서 읽는다(엔진 로그 파일에는 요약 줄이 없다). 보고 줄은 판정을 바꾸지 않는다.
  # 단위 테스트는 몫(`UnitTests.<i>`, SW_UNIT_TEST_SHARDS)으로 나눠 돌므로 몫 보고를 모두 더한다(게임 항목 `UnitTests.<게임>` 은 뺀다).
  [ $DRY -eq 1 ] || py -3 -c "import glob, json; ds=[json.load(open(p, encoding='utf-8')) for p in glob.glob('build/Ninja-Release-ASan/Testing/Reports/UnitTests*.json') if p.split('UnitTests')[-1] in ('.json',) or p.split('UnitTests.')[-1][:-5].isdigit()]; print(f\"App --test: {sum(d['runCount'] for d in ds)} case run(s), {sum(d['failedCount'] for d in ds)} failed, {sum(d.get('skippedCount', 0) for d in ds)} skipped ({len(ds)} report(s))\")" || true
  return 0
}

# 배포 프리셋 둘(shipping-check 가 띄우는 것과 같은 목록 — 플레이어 Client 와 전용 Server)을 짓는다 — 쿠킹이 Content 를 읽으므로 임포트 뒤에 띄운다.
job_shipping_build() {
  local P
  for P in Ninja-Shipping Ninja-Shipping-Server; do
    build_preset $P || return 1
  done
  # 기동 · 종료 확인을 빌드 갈래 끝에서 바로 — 살아 있음 판정이라 다른 빌드와 겹쳐도 되고, 실패가 cpu 단계 끝(2026-11-21 시작 74 분 뒤)이 아니라 배포 빌드 직후에 나온다.
  run_shipping_check --skip-build
}

# 배포 구성: 기동 · 종료(shipping_check.py). 인자는 그대로 넘긴다(--skip-build 면 지어 둔 산출물만 띄운다).
run_shipping_check() {
  echo "== shipping-check $(date +%T)"
  run build/verify.shipping.log py -3 -m Scripts shipping-check "$@"
  local RC=$?
  [ $DRY -eq 1 ] || tail -n 8 build/verify.shipping.log
  echo "shipping-check exit=$RC"
  return $RC
}

job_server_tools() {
  echo "== server unit $(date +%T)"
  run build/Ninja-Release-Server/Testing/Logs/verify.unit.log build/Ninja-Release-Server/Bin/Server.exe --test -unattended
  local RC=$?
  echo "server unit exit=$RC"
  [ $DRY -eq 1 ] || grep -E "case run|TEST_RESULT=" build/Ninja-Release-Server/Testing/Logs/verify.unit.log
  [ $RC -eq 0 ] || return 1

  # 도구 테스트: 실행 인자 GUI(VS Code 확장 Tools/launch-args — 확장이라 JS 다)가 Source 의 매크로 · 인자 표를 읽는다. 엔진 쪽 이름 · 형식이 바뀌면 깨지는데
  # 어디서도 돌지 않아 오래 깨져 있었다(2026-11-08). 확장 도구가 VS Code 안의 Node 로 돌린다(따로 깐 node 가 필요 없다).
  # 주석 접기 확장(Tools/comment-fold)도 같은 도구로 함께 돈다.
  echo "== extension tests $(date +%T)"
  run build/verify.extensions.log py -3 Tools/Scripts/ExtensionTool.py test Tools/launch-args Tools/comment-fold
  RC=$?
  [ $DRY -eq 1 ] || grep -E "^(\[unit\]|ℹ (pass|fail))" build/verify.extensions.log
  [ $RC -eq 0 ] || { echo "extension tests exit=$RC"; return 1; }

  # 배포 빌드 · 기동은 build 단계의 job_shipping_build 가 했다. --from 으로 build 를 건너뛰었으면 여기서 짓고 띄운다.
  [ $STAGE -le 1 ] && return 0
  run_shipping_check
}

# 지난 Release 체인의 셰이더 요청만 컴파일해 셰이더 DDC 를 데운다(언리얼 Shader Pipeline Cache 의 미리 컴파일 자리) — 기록은 Test/Scenario/CMakeLists.txt 의 `-shader-request-log`.
job_shader_warm() {
  local DIR=build/Ninja-Release/Testing/ShaderRequests
  echo "== shader warm $(date +%T)"
  if [ $DRY -eq 0 ] && ! compgen -G "$DIR/*.shaderrequests" > /dev/null; then
    echo "shader warm: $DIR 에 요청 기록이 없다(첫 실행) - 할 일이 없다"
    return 0
  fi
  run build/Ninja-Release/Testing/Logs/verify.shaderwarm.log build/Ninja-Release/Bin/App.exe --cook-shaders -shader-request-dir=$DIR -unattended -log-file=build/Ninja-Release/Testing/Logs/ShaderWarm.log
  local RC=$?
  [ $DRY -eq 1 ] || grep -a -E "ShaderRequestLog\.(Warmed|NothingToWarm|ReadFailed)" build/Ninja-Release/Testing/Logs/ShaderWarm.log | tr -d '\r'
  echo "shader warm exit=$RC"
  return $RC
}

# 단계 기록(verify_record.py) — 건너뛸 단계면 기록을 이 sha 로 옮기고 1 을, 돌 단계면 0 을 돌려준다. --dry-run 도 판정은 찍는다(기록은 남기지 않는다).
should_run_stage() {   # <단계 번호> <이름>
  [ $STAGE -le "$1" ] || return 1
  if py -3 -m Scripts verify-record skip "$2" "$SHA"; then
    STAGE_STARTED=$SECONDS
    stage_passed "$2" skipped
    return 1
  fi
  return 0
}
stage_passed() {   # <이름> [skipped]
  record_stage_time "$1" "${2:-passed}"
  CURRENT_STAGE=""
  [ $DRY -eq 1 ] || py -3 -m Scripts verify-record pass "$1" "$SHA" || windows_failed "$1 통과 기록"
}

# Windows 끝 검사 — HEAD 가 시작 때 그대로이고 트리가 깨끗한가.
check_tree_at_end() {
  [ $DRY -eq 1 ] && return 0
  local NOW CHANGES
  NOW=$(sw_git rev-parse HEAD)
  [ "$NOW" = "$SHA" ] || windows_failed "HEAD 가 검증 중에 바뀌었다 $SHA -> $NOW"
  CHANGES=$(list_tree_changes)
  [ -z "$CHANGES" ] || { printf '%s\n' "$CHANGES" | head -20; windows_failed "검증 중에 추적 파일이 바뀌었다(빌드 · 테스트가 소스 트리를 고쳤다)"; }
}

[ $DRY -eq 1 ] || mkdir -p build
RUN_BUILD=0; RUN_TIDY=0; RUN_CHANGED=0; RUN_CPU=0; RUN_GPU=0; RUN_LINUX=0
should_run_stage 1 build && RUN_BUILD=1
should_run_stage 4 tidy && RUN_TIDY=1   # --from 으로는 건너뛰지 않는다(머리말 tidy) — Windows 를 통째로 건너뛰는 --from linux 만 뺀다
should_run_stage 2 changed && RUN_CHANGED=1
should_run_stage 3 cpu && RUN_CPU=1
should_run_stage 4 gpu-release && RUN_GPU=1
[ $NO_LINUX -eq 1 ] || { should_run_stage 5 linux && RUN_LINUX=1; }

[ $STAGE -le 1 ] && start_job "lint --all" job_lint
if [ $RUN_BUILD -eq 1 ]; then
  begin_stage build
  build_preset Ninja-Release
  # 임포트를 먼저 끝낸다 — 낡은 산출물을 Content 에 다시 쓰는 일이라 배포 빌드의 쿠킹이 그 파일을 읽는 동안 돌지 않게(ctest 의 고정 단계 ImportedAssets 는 아래에서 곧바로 끝난다).
  # 최적화 App 으로 임포트한다 — Debug 의 BC7 · BC6H 압축기는 큰 텍스처 하나에 몇 분이다(AssetImportEntry.TextureImportDebugBuild). 산출물은 구성과 무관하게 Content 하나다.
  run_ctest Ninja-Release import -L import || windows_failed "Ninja-Release import"
  start_job "shipping build" job_shipping_build
  build_preset Ninja-Release-ASan
  [ $RUN_TIDY -eq 0 ] || start_tidy
  build_preset Ninja-Release-Server
  wait_jobs
  stage_passed build
else
  # build 를 건너뛰어도(--from · 단계 기록) 뒤 단계가 돌릴 프리셋은 증분 빌드한다 — 짓지 않으면 고친 커밋 대신 옛 바이너리가 돌아 고친 테스트가 다시 진다
  # (2026-11-13 --from cpu 의 Release 단위). 바뀐 것이 없으면 몇 초다. 배포 프리셋은 --from 의 cpu 단계 shipping-check 가 직접 짓는다.
  [ $((RUN_CHANGED + RUN_CPU + RUN_GPU)) -gt 0 ] && build_preset Ninja-Release
  # tidy 도 ASan 컴파일 DB · 생성 헤더를 읽는다 — cpu 를 건너뛰어도 tidy 가 돌면 ASan 을 증분 빌드한다.
  [ $((RUN_CPU + RUN_TIDY)) -gt 0 ] && build_preset Ninja-Release-ASan
  [ $RUN_TIDY -eq 0 ] || start_tidy
  [ $RUN_CPU -eq 1 ] && build_preset Ninja-Release-Server
  # 임포트는 build 단계가 했다 — 거기서 돌지 않았으면 첫 테스트 단계 앞에서 한다(changed 는 ctest 고정 단계 없이 App 을 바로 띄운다).
  [ $((RUN_CHANGED + RUN_CPU)) -gt 0 ] && { run_ctest Ninja-Release import -L import || windows_failed "Ninja-Release import"; }
  wait_jobs   # lint --all
fi

if [ $RUN_CHANGED -eq 1 ]; then
  begin_stage changed
  run_changed
  stage_passed changed
fi

if [ $RUN_CPU -eq 1 ]; then
  begin_stage cpu
  start_job "ASan nogpu" job_sanitizer_cpu
  start_job "server · tools · shipping" job_server_tools
  start_job "shader warm" job_shader_warm
  wait_jobs
  stage_passed cpu
fi

# GPU 단계는 시간 · 그림을 판정한다 — clang-tidy 와 겹치지 않게 여기서 합류한다.
join_tidy

if [ $RUN_GPU -eq 1 ]; then
  begin_stage gpu-release
  run_ctest Ninja-Release all || windows_failed "Ninja-Release ctest"
  stage_passed gpu-release
fi
[ $TIDY_FAILED -eq 0 ] || windows_failed "clang-tidy - build/Ninja-Release-ASan/Testing/Logs/verify.tidy.log"

# 흔들림 판정 — 최근 검증 커밋에서 같은 커밋에 통과 · 실패가 함께 있는 (테스트, 백엔드)가 격리 표 밖에서 문턱(Verify.json)을 넘으면 진다. 끝 줄 FLAKY=.
echo "== flaky $(date +%T)"
run build/verify.flaky.log py -3 -m Scripts test-history flaky
FLAKY_RC=$?
[ $DRY -eq 1 ] || tail -n 20 build/verify.flaky.log
[ $FLAKY_RC -eq 0 ] || windows_failed "흔들림이 문턱을 넘었다 - build/verify.flaky.log"

check_tree_at_end
if [ $DRY -eq 1 ]; then
  echo "[dry] 통과 기록(build/verify.ok)과 WINDOWS_OK 줄은 실제 실행만 남긴다"
else
  py -3 -m Scripts verify-record ok "$SHA" || windows_failed "build/verify.ok 쓰기"
  echo "WINDOWS_OK=$SHA (시작 단계 $FROM)"
fi
if [ $NO_LINUX -eq 1 ]; then
  echo "Linux 를 돌리지 않았다(--no-linux)"
  exit 0
fi
if [ $RUN_LINUX -eq 0 ]; then
  [ $DRY -eq 1 ] || echo "VERIFIED=$SHA (시작 단계 $FROM, linux 는 단계 기록으로 건너뜀)"
  exit 0
fi
echo "== linux 시작 $(date +%T)"
begin_stage linux
PYTHONUTF8=1 run build/verify.linux.log py -3 -m Scripts linux-scenarios all "${RECORD[@]}"
RC=$?
[ $DRY -eq 1 ] || tail -n 40 build/verify.linux.log
# 새니타이저(ASan + UBSan, Linux-Debug-ASan 단위 테스트) — 묶음마다 한 번(linux-verify 스킬). 체인과 CPU 를 다투지 않게 all 뒤에 차례로 돌고, 지면 끝 줄에 적는다(push 는 WINDOWS_OK 로 이미 정해졌다).
# --record 실행은 건너뛴다: sanitize 도 클론을 HEAD 로 되돌려(reset --hard · clean) 아직 옮기지 않은 기록 골든을 지운다 — goldens 로 옮긴 뒤 따로 돌린다.
SAN_RC=0
case " ${RECORD[*]} " in
  *" --record "*) echo "== sanitize 건너뜀(--record — goldens 로 옮긴 뒤 linux_scenarios.py sanitize 를 따로 돌린다)"; SAN_RC=skip ;;
  *)
    echo "== sanitize address $(date +%T)"
    PYTHONUTF8=1 run build/verify.sanitize.log py -3 -m Scripts linux-scenarios sanitize address
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
[ "$SAN_RC" = skip ] || stage_passed linux   # --record 실행은 골든을 쓰는 실행이라 통과 기록을 남기지 않는다
[ $DRY -eq 1 ] || echo "VERIFIED=$SHA (시작 단계 $FROM$SAN_NOTE)"
