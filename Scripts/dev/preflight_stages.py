"""
Scripts/dev/preflight_stages.py

묶음 검증의 단계 — 클래스 하나가 단계 하나이고 **적은 차례가 도는 차례다**. 단계를 더하고 빼는 일은 클래스 하나를 더하고 빼는 일이다(목록 · 번호가 따로 없다).
차례 · 겹침 · 기록 · 실패 처리는 `preflight_run.py`. 시간 판정이 CPU 경합에 흔들리는 일과 그렇지 않은 일을 가른다 — 시나리오는 벽시계 시한 · 프레임에 걸린 그림을
판정하므로 GPU 단계는 혼자 돈다.
"""

from __future__ import annotations

import inspect
import json
from dataclasses import dataclass

from dev.preflight_run import SCRIPTS, BackgroundStage, BuildInput, Job, PreflightOptions, PreflightShell, Stage, StageFailed, list_tree_changes
from common.git import run_git_or_raise

MAX_TREE_LINES = 20
STL_TEST_GLOBS = ("Source/Core/Container/Tests/*.cpp", "Source/Core/String/Tests/*.cpp")


# ------------------------------------------------------------------------------
# 입력
# ------------------------------------------------------------------------------
@dataclass(frozen=True)
class PresetBuild(BuildInput):
    """증분 빌드한 프리셋 — 짓지 않으면 고친 커밋 대신 옛 바이너리가 돌아 고친 테스트가 다시 진다(2026-11-13 --from cpu 의 Release 단위). 바뀐 것이 없으면 몇 초다."""

    preset: str

    def prepare(self, shell: PreflightShell) -> None:
        shell.build(self.preset)


@dataclass(frozen=True)
class ImportedAssets(BuildInput):
    """임포트 산출물 — 낡은 산출물을 Content 에 다시 쓰는 일이라 배포 빌드의 쿠킹이 그 파일을 읽는 동안 돌지 않게 먼저 끝낸다(ctest 의 고정 단계 ImportedAssets 는 그 뒤 곧바로
    끝난다). 최적화 App 으로 임포트한다 — Debug 의 BC7 · BC6H 압축기는 큰 텍스처 하나에 몇 분이다(AssetImportEntry.TextureImportDebugBuild). 산출물은 구성과 무관하게 Content 하나다."""

    def prepare(self, shell: PreflightShell) -> None:
        if not shell.run_tests(RELEASE.preset, "import", "-L", "import"):
            raise StageFailed(f"{RELEASE.preset} import")


RELEASE = PresetBuild("Ninja-Release")
SANITIZER = PresetBuild("Ninja-Release-ASan")
SERVER = PresetBuild("Ninja-Release-Server")
IMPORTED = ImportedAssets()


# ------------------------------------------------------------------------------
# 갈래
# ------------------------------------------------------------------------------
class LintJob(Job):
    """커밋 훅은 스테이지된 파일만 본다(Scripts/lint/lint_scope.py) — 낡은 예외 · 기준선 같은 저장소 전체 판정은 여기서 한 번 낸다."""

    name = "lint --all"

    def run(self, shell: PreflightShell) -> bool:
        return shell.run_logged("lint --all", [*SCRIPTS, "lint", "--all"], "build/verify.lint.log", 20, exit_name="lint") == 0


def run_shipping_check(shell: PreflightShell, *arguments: str) -> bool:
    """배포 구성의 기동 · 종료(shipping_check.py) — `--skip-build` 면 지어 둔 산출물만 띄운다."""
    return shell.run_logged("shipping-check", [*SCRIPTS, "shipping-check", *arguments], "build/verify.shipping.log", 8, exit_name="shipping-check") == 0


class ShippingBuildJob(Job):
    """배포 프리셋 둘(shipping-check 가 띄우는 것과 같은 목록 — 플레이어 Client 와 전용 Server) — 쿠킹이 Content 를 읽으므로 임포트 뒤에 띄운다."""

    name = "shipping build"
    # 배포 팩은 호스트 개발 빌드가 굽는다(CookAssets 가 Ninja-Release · Ninja-Release-Server 를 먼저 짓는다 — HostTools.cmake). 같은 폴더를 다른 갈래가 동시에 짓지 않게
    # 서버 개발 빌드도 이 갈래가 서버 배포 앞에서 짓는다(Ninja-Release 는 이 갈래보다 먼저 끝났다).
    presets = ("Ninja-Shipping", SERVER.preset, "Ninja-Shipping-Server")

    def run(self, shell: PreflightShell) -> bool:
        for preset in self.presets:
            shell.build(preset)
        # 기동 · 종료 확인을 빌드 갈래 끝에서 바로 — 살아 있음 판정이라 다른 빌드와 겹쳐도 되고, 실패가 cpu 단계 끝(2026-11-21 시작 74 분 뒤)이 아니라 배포 빌드 직후에 나온다.
        return run_shipping_check(shell, "--skip-build")


class StdContainerJob(Job):
    """컨테이너 저장을 std 로 바꾼 구성이 컴파일되는가(구문만) — 방금 지은 Release 의 컴파일 DB · 생성 헤더를 쓴다. 컨테이너 테스트가 모든 공개 API 를 인스턴스화한다."""

    name = "std container syntax"

    def run(self, shell: PreflightShell) -> bool:
        list_test = [path.relative_to(shell.root).as_posix() for pattern in STL_TEST_GLOBS for path in sorted(shell.root.glob(pattern))]
        command = [*SCRIPTS, "syntax", "--changed", "origin/main", "--direct", "--define", "SW_ENABLE_STL_CONTAINER", "--files", *list_test]
        return shell.run_logged(self.name, command, "build/verify.stl.log", 20, exit_name=self.name) == 0


class SanitizerCpuJob(Job):
    """ASan ctest -L nogpu — 단위 · 에디터 단위 · 셰이더 다시 굽기 · 파서."""

    name = "ASan nogpu"

    def run(self, shell: PreflightShell) -> bool:
        if not shell.run_tests(SANITIZER.preset, "nogpu", "-L", "nogpu"):
            return False
        if not shell.options.dry_run:
            self._report_case_counts(shell)
        return True

    @staticmethod
    def _report_case_counts(shell: PreflightShell) -> None:
        """App --test 의 케이스 수(보고에 적는다) — 보고 JSON 에서 읽는다(엔진 로그 파일에는 요약 줄이 없다). 판정을 바꾸지 않는다. 단위 테스트는 몫(`UnitTests.<i>`,
        SW_UNIT_TEST_SHARDS)으로 나눠 돌므로 몫 보고를 모두 더한다(게임 항목 `UnitTests.<게임>` 은 뺀다)."""
        list_path = [path for path in (shell.root / f"build/{SANITIZER.preset}/Testing/Reports").glob("UnitTests*.json")
                     if path.name == "UnitTests.json" or path.name[len("UnitTests."):-len(".json")].isdigit()]
        try:
            list_report = [json.loads(path.read_text(encoding="utf-8")) for path in list_path]
            run, failed = (sum(report[key] for report in list_report) for key in ("runCount", "failedCount"))
        except (OSError, ValueError, KeyError) as error:
            shell.write_line(f"App --test: 보고를 읽지 못했다({error})")
            return
        skipped = sum(report.get("skippedCount", 0) for report in list_report)
        shell.write_line(f"App --test: {run} case run(s), {failed} failed, {skipped} skipped ({len(list_report)} report(s))")


class ServerToolsJob(Job):
    name = "server · tools · shipping"

    def run(self, shell: PreflightShell) -> bool:
        log = f"build/{SERVER.preset}/Testing/Logs/verify.unit.log"
        shell.announce("server unit")
        code = shell.run([f"build/{SERVER.preset}/Bin/Server.exe", "--test", "-unattended"], log)
        shell.write_line(f"server unit exit={code}")
        shell.show_matches(log, "case run|TEST_RESULT=")
        if code != 0:
            return False

        # 도구 테스트: 실행 인자 GUI(VS Code 확장 Tools/launch-args — 확장이라 JS 다)가 Source 의 매크로 · 인자 표를 읽는다. 엔진 쪽 이름 · 형식이 바뀌면 깨지는데
        # 어디서도 돌지 않아 오래 깨져 있었다(2026-11-08). 확장 도구가 VS Code 안의 Node 로 돌린다(따로 깐 node 가 필요 없다).
        # 주석 접기 확장(Tools/comment-fold)도 같은 도구로 함께 돈다.
        shell.announce("extension tests")
        code = shell.run(["py", "-3", "Tools/Scripts/ExtensionTool.py", "test", "Tools/launch-args", "Tools/comment-fold"], "build/verify.extensions.log")
        shell.show_matches("build/verify.extensions.log", r"^(\[unit\]|ℹ (pass|fail))")
        if code != 0:
            shell.write_line(f"extension tests exit={code}")
            return False

        # 배포 빌드 · 기동은 build 단계의 ShippingBuildJob 이 했다. --from 으로 build 를 건너뛰었으면 여기서 짓고 띄운다.
        return shell.options.start == BuildStage.name or run_shipping_check(shell)


class ShaderWarmJob(Job):
    """지난 Release 체인의 셰이더 요청 기록(체인 자식마다 하나 — IntegrationTests/Scenario/CMakeLists.txt 의 `-shader-request-log`)만 컴파일해 그 빌드 폴더의 셰이더 DDC 를 네 백엔드 모두 데운다
    (언리얼 Shader Pipeline Cache 의 미리 컴파일 자리). 셰이더 · 공용 hlsli 를 고친 묶음도 GPU 단계가 캐시 미스 컴파일을 떠안지 않는다(2026-11-19: dx11 empty 체인 19 분 중
    FXC 559 개 — 캐시가 차면 74 초). 전체 굽기는 5 만 개를 넘어 넣지 않는다."""

    name = "shader warm"
    folder = f"build/{RELEASE.preset}/Testing/ShaderRequests"

    def run(self, shell: PreflightShell) -> bool:
        logs = f"build/{RELEASE.preset}/Testing/Logs"
        shell.announce(self.name)
        if not shell.options.dry_run and not any((shell.root / self.folder).glob("*.shaderrequests")):
            shell.write_line(f"shader warm: {self.folder} 에 요청 기록이 없다(첫 실행) - 할 일이 없다")
            return True
        code = shell.run([f"build/{RELEASE.preset}/Bin/App.exe", "--cook-shaders", f"-shader-request-dir={self.folder}", "-unattended", f"-log-file={logs}/ShaderWarm.log"],
                         f"{logs}/verify.shaderwarm.log")
        shell.show_matches(f"{logs}/ShaderWarm.log", r"ShaderRequestLog\.(Warmed|NothingToWarm|ReadFailed)")
        shell.write_line(f"shader warm exit={code}")
        return code == 0


# ------------------------------------------------------------------------------
# 단계 — 적은 차례로 돈다
# ------------------------------------------------------------------------------
class BuildStage(Stage):
    """lint --all(뒤에서) ‖ Release 빌드 → 임포트 → (ASan 빌드 ‖ 배포 Client → 서버(Release) → 배포 Server 빌드 · 쿠킹 → shipping-check ‖ std 컨테이너 구문).
    배포 빌드는 증분 빌드의 링크 꼬리에 비는 코어를 쓴다(2026-11-13: CPU 단계 11.5 분 중 9 분이 배포 빌드였다). 건너뛰면 뒤 단계가 쓸 입력만 증분으로 마련한다 — 배포 프리셋은
    --from 의 cpu 단계 shipping-check 가 직접 짓는다."""

    name = "build"
    last_start = "build"
    record = "build"
    standing_jobs = (LintJob(),)   # 문서 · 스킬도 읽으므로 기록으로 건너뛰는 build 에서도 돈다
    steps = (RELEASE, IMPORTED, ShippingBuildJob(), StdContainerJob(), SANITIZER)
    prepares = (RELEASE, SANITIZER, SERVER, IMPORTED)


class TidyStage(BackgroundStage):
    """바뀐 파일 clang-tidy(origin/main 기준, 통과한 TU 는 포함 닫힘 해시로 캐시 — Saved/TidyCache). 방금 지은 ASan 의 컴파일 DB 를 쓴다(유니티를 끈 구성이라 원본 TU 가 DB 에 있다).
    ASan 빌드 직후 떠 서버 · 배포 빌드, changed, cpu 와 겹치고(2026-11-21: 746 TU 2055 초를 build 단계 끝에서 기다렸다) GPU 단계가 혼자 돌게 그 앞에서 합류한다.
    낮은 우선순위라 겹친 테스트가 CPU 를 먼저 받는다. --from changed|cpu|gpu-release 로도 건너뛰지 않는다 — 진 뒤 --from 으로 다시 돌려도 WINDOWS_OK 가 tidy 없이 나오지 않는다."""

    name = "tidy"
    last_start = "gpu-release"
    record = "tidy"
    inputs = (SANITIZER,)
    title = "clang-tidy"
    log = f"build/{SANITIZER.preset}/Testing/Logs/verify.tidy.log"
    joined_before = "gpu-release"
    failed_before = "flaky"

    def run(self, shell: PreflightShell) -> None:
        if shell.run_logged("tidy", [*SCRIPTS, "tidy", "--changed", "origin/main"], self.log, 25, exit_name="tidy") != 0:
            raise StageFailed(f"clang-tidy - {self.log}")


class ChangedStage(Stage):
    """origin/main 뒤에 들어온 · 바뀐 테스트 · 시나리오와 바뀐 에셋 · 감시 값이 닿는 시나리오 표본(scenario_reach.py)만 Release 로 먼저. 없는 골든 · 스냅숏만 쓰고(MissingBaseline) 쓴 것이
    있으면 멈춘다. 지면 cpu · gpu-release 를 시작하지 않는다."""

    name = "changed"
    last_start = "changed"
    record = "changed"
    inputs = (RELEASE, IMPORTED)   # ctest 고정 단계 없이 App 을 바로 띄운다
    log = "build/verify.changed.log"

    def run(self, shell: PreflightShell) -> None:
        shell.announce("changed")
        code = shell.run([*SCRIPTS, "test-history", "changed", "--preset", RELEASE.preset], self.log)
        shell.show_matches(self.log, r"^(?!\[Changed\] run )")
        if code != 0:
            raise StageFailed("changed")

    def make_failure(self, reason: str, sha: str, suffix: str) -> str:
        return f"CHANGED_FAILED sha={sha} - {self.log} (cpu · gpu-release 를 시작하지 않는다)"


class CpuStage(Stage):
    """시간을 판정하지 않는 CPU 일만 겹친다."""

    name = "cpu"
    last_start = "cpu"
    record = "cpu"
    inputs = (RELEASE, SANITIZER, SERVER, IMPORTED)
    steps = (SanitizerCpuJob(), ServerToolsJob(), ShaderWarmJob())


class GpuReleaseStage(Stage):
    """Release ctest 전체(부하 측정 라벨 load 만 뺀다) — 시나리오 그림 · 골든 · 기록 대조의 정본과 여러 프로세스 네트워크(`Net.*`). 다음 묶음의 shader warm 이 읽을 요청 기록을 남긴다."""

    name = "gpu-release"
    last_start = "gpu-release"
    record = "gpu-release"
    inputs = (RELEASE,)

    def run(self, shell: PreflightShell) -> None:
        if not shell.run_tests(RELEASE.preset, "all", "-LE", "^load$"):
            raise StageFailed(f"{RELEASE.preset} ctest")


class FlakyStage(Stage):
    """흔들림 판정 — 최근 검증 커밋에서 같은 커밋에 통과 · 실패가 함께 있는 (테스트, 백엔드)가 격리 표 밖에서 문턱(Verify.json)을 넘으면 진다. 끝 줄 FLAKY=."""

    name = "flaky"
    last_start = "linux"

    def run(self, shell: PreflightShell) -> None:
        if shell.run_logged("flaky", [*SCRIPTS, "test-history", "flaky"], "build/verify.flaky.log", 20) != 0:
            raise StageFailed("흔들림이 문턱을 넘었다 - build/verify.flaky.log")


class TreeStage(Stage):
    """HEAD 가 시작 때 그대로이고 트리가 깨끗한가 — 빌드 · 테스트가 커밋되는 파일을 고치면(예: 리플렉션 생성의 AssetReferenceFields.generated.json) 여기서 진다."""

    name = "tree"
    last_start = "linux"

    def run(self, shell: PreflightShell) -> None:
        if shell.options.dry_run:
            return
        head = run_git_or_raise(["rev-parse", "HEAD"], cwd=shell.root).strip()
        if head != shell.sha:
            raise StageFailed(f"HEAD 가 검증 중에 바뀌었다 {shell.sha} -> {head}")
        list_change = list_tree_changes(shell.root)
        if list_change:
            shell.write_line("\n".join(list_change[:MAX_TREE_LINES]))
            raise StageFailed("검증 중에 추적 파일이 바뀌었다(빌드 · 테스트가 소스 트리를 고쳤다)")


class LoadStage(Stage):
    """부하 측정 시나리오(16 개 약 6 분) — 묶음마다가 아니라 Linux 와 함께 모아서 돈다. 측정은 조절 값 · 퇴행 확인만이라 WINDOWS_OK 를 막지 않는다(VERIFIED 는 막는다).
    바뀐 부하 시나리오는 changed 가 그대로 돈다."""

    name = "load"
    last_start = "linux"
    runs_with = "linux"
    blocks_push = False

    def run(self, shell: PreflightShell) -> None:
        if not shell.run_tests(RELEASE.preset, "load", "-L", "^load$"):
            raise StageFailed("load")

    def make_failure(self, reason: str, sha: str, suffix: str) -> str:
        return f"LOAD_FAILED sha={sha} - build/{RELEASE.preset}/Testing/Logs/verify.ctest.load.log"


class LinuxStage(Stage):
    """Linux 빌드 · 단위 · 체인(`linux-scenarios all`) — 묶음마다가 아니라 모아서 돈다(--no-linux)."""

    name = "linux"
    last_start = "linux"
    record = "linux"
    blocks_push = False

    def run(self, shell: PreflightShell) -> None:
        shell.announce("linux 시작")
        code = shell.run([*SCRIPTS, "linux-scenarios", "all", *(["--record"] if shell.options.record else [])], "build/verify.linux.log", utf8=True)
        shell.show_tail("build/verify.linux.log", 40)
        if code != 0:
            raise StageFailed("linux")

    def make_failure(self, reason: str, sha: str, suffix: str) -> str:
        return f"LINUX_FAILED sha={sha}"


class SanitizeStage(Stage):
    """새니타이저(ASan + UBSan, Linux-Debug-ASan 단위 테스트) — 묶음마다 한 번(linux-verify 스킬). 체인과 CPU 를 다투지 않게 all 뒤에 차례로 돈다."""

    name = "sanitize"
    last_start = "linux"
    record = "linux"
    blocks_push = False

    def make_skip_note(self, options: PreflightOptions) -> str | None:
        # sanitize 도 클론을 HEAD 로 되돌려(reset --hard · clean) 아직 옮기지 않은 기록 골든을 지운다.
        return "--record — goldens 로 옮긴 뒤 linux_scenarios.py sanitize 를 따로 돌린다" if options.record else None

    def run(self, shell: PreflightShell) -> None:
        shell.announce("sanitize address")
        code = shell.run([*SCRIPTS, "linux-scenarios", "sanitize", "address"], "build/verify.sanitize.log", utf8=True)
        shell.show_tail("build/verify.sanitize.log", 20)
        if code != 0:
            raise StageFailed("address")

    def make_failure(self, reason: str, sha: str, suffix: str) -> str:
        return f"SANITIZE_FAILED ({reason}) sha={sha} - build/verify.sanitize.log"


def list_stages() -> list[Stage]:
    """이 파일에 적은 차례 그대로의 단계."""
    return [value() for value in globals().values() if inspect.isclass(value) and issubclass(value, Stage) and value.__module__ == __name__]
