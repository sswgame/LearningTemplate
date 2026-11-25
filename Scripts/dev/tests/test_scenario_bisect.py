"""
`scenario_runs.py` 회귀 검사 — 이분 탐색 · 보고 판정 · 골든 여유, 진 단계 다시 돌기(--rerun-failed)가 고르는 백엔드 보고 폴더.

실행: python -X utf8 Scripts/dev/tests/test_scenario_bisect.py (CTest `Scripts.ScenarioBisect`). 임시 폴더만 쓴다.
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.git import run_git_or_raise  # noqa: E402
from dev.run_history import ingest, open_database  # noqa: E402
from dev.scenario_runs import (  # noqa: E402
    collect_expected_stages, count_golden_coverage, describe_code_change, describe_result, describe_stage_failures, find_first_bad_step, find_latest_per_backend_folder, list_advance_steps, minimize_stages, read_backend_chain_order, retry_failed_stages, select_failed_stages, stage_failed_in_reports, summarize_golden_margins,
)


def write_reports(folder: Path, names: list[str], mtime: float) -> None:
    folder.mkdir(parents=True)
    for index, name in enumerate(names):
        (folder / f'dx12.{index}.json').write_text(json.dumps({'name': name, 'result': 'Passed'}), encoding='utf-8')
    os.utime(folder, (mtime, mtime))


class FindLatestPerBackendFolderTest(unittest.TestCase):
    def test_a_later_run_of_another_chain_sharing_a_stage_is_not_chosen(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            now = time.time()
            write_reports(root / 'kitmodulereload' / 'per-backend-1', ['editor.bugreport', 'kitmodulereload'], now - 100)
            write_reports(root / 'kitmodulereload' / 'per-backend-2', ['game.playground.boat', 'kitmodulereload'], now)
            chosen = find_latest_per_backend_folder(root, {'editor.bugreport', 'kitmodulereload'}, root / 'no-chain.log')
            self.assertEqual(chosen, root / 'kitmodulereload' / 'per-backend-1')

    def test_no_matching_folder_is_none(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            write_reports(root / 'x' / 'per-backend-1', ['game.playground.boat'], time.time())
            self.assertIsNone(find_latest_per_backend_folder(root, {'editor.bugreport'}, root / 'no-chain.log'))


class RetryFailedStagesTest(unittest.TestCase):
    def test_code_change_is_seen_from_commit_and_files(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)

            def git(*list_argument: str) -> None:
                run_git_or_raise(['-c', 'user.name=t', '-c', 'user.email=t@localhost', '-c', 'core.autocrlf=false', *list_argument], cwd=root)

            def commit(text: str, moment: str) -> None:
                (root / 'a.txt').write_text(text, encoding='utf-8')
                git('add', '.')
                with mock.patch.dict(os.environ, {'GIT_AUTHOR_DATE': moment, 'GIT_COMMITTER_DATE': moment}):
                    git('commit', '-qm', text)

            git('init', '-q', '-b', 'main')
            commit('one', '2026-01-01T00:00:00+0000')
            report = {'name': 's', 'backend': 'vk', 'result': 'Failed', 'startedAtUnixMs': 1767229200000}   # 2026-01-01T01:00:00Z
            self.assertIsNone(describe_code_change([report], root))
            connection = open_database(root / 'history.sqlite')
            ingest(connection, [report], 'f' * 40, 'verify')
            self.assertIn('ffffffffff', describe_code_change([report], root, connection), '이력이 붙인 커밋이 지금과 다르다')
            connection.close()
            commit('two', '2026-01-01T02:00:00+0000')
            self.assertIn('지금은', describe_code_change([report], root), '보고 뒤에 커밋했다')
            late = {**report, 'startedAtUnixMs': 1767240000000}   # 커밋 둘 뒤(2026-01-01T04:00:00Z)
            self.assertIsNone(describe_code_change([late], root))
            (root / 'a.txt').write_text('three', encoding='utf-8')
            self.assertIn('a.txt', describe_code_change([late], root), '보고 뒤에 고친 파일')

    def test_stage_passed_alone_after_change_is_not_searched(self) -> None:
        for change, want_runs, want_code in (('보고는 a, 지금은 b', 1, 0), (None, 2, 0)):
            list_run: list[int] = []

            def run(stage_paths: list[str], stage: str, backend: str) -> bool:
                list_run.append(len(stage_paths))
                return False   # 늘 통과

            with mock.patch('dev.scenario_runs.read_scenario_name', side_effect=lambda path: path):
                self.assertEqual(retry_failed_stages(['x', 'y'], [('vk', 'y')], run, change), want_code)
            self.assertEqual(len(list_run), want_runs, (change, list_run))


class ScenarioRunsTest(unittest.TestCase):
    def test_bisect_reports_and_golden_margins(self) -> None:
        coverage, key_count, unknown = count_golden_coverage(['a/golden/s.n.dx12.ppm', 'a/golden/s.n.vk.ppm', 'a/golden/s.n.vk.linux.ppm', 'a/golden/s.m.dx12.ppm', 'a/golden/s.m.old.ppm'],
                                                          {'DirectX12': 'dx12', 'Vulkan': 'vk', 'WebGPU': 'wgpu'})
        assert coverage == {('Windows', 'DirectX12'): 2, ('Windows', 'Vulkan'): 1, ('Linux', 'Vulkan'): 1}, coverage
        assert key_count == 2 and unknown == ['a/golden/s.m.old.ppm'], (key_count, unknown)
        reports = [
            {'goldenSamples': [{'golden': 'a', 'differingFraction': 0.002, 'meanDistance': 0.0001, 'largestDeepRegionPixels': 4, 'maxDifferingFraction': 0.01, 'maxMeanDistance': 0.005, 'maxDeepRegionPixels': 16}]},
            {'goldenSamples': [{'golden': 'a', 'differingFraction': 0.001, 'meanDistance': 0.0001, 'largestDeepRegionPixels': 8, 'maxDifferingFraction': 0.01, 'maxMeanDistance': 0.005, 'maxDeepRegionPixels': 16},
                               {'golden': 'b', 'differingFraction': 0.0, 'meanDistance': 0.0, 'largestDeepRegionPixels': 0, 'maxDifferingFraction': 0.01, 'maxMeanDistance': 0.005, 'maxDeepRegionPixels': 16}]},
            {'records': []},
        ]
        rows = summarize_golden_margins(reports)
        assert [row['golden'] for row in rows] == ['a', 'b'], rows
        assert rows[0]['runs'] == 2 and rows[0]['largestDeepRegionPixels'] == 8 and rows[0]['differingFraction'] == 0.002, rows[0]
        assert rows[0]['margin'] == 2.0, rows[0]   # 덩어리 16 / 8 이 가장 작은 여유
        assert rows[1]['margin'] == float('inf'), rows[1]

        stages = [f's{i}' for i in range(10)]
        for first_bad in range(10):
            calls: list[list[str]] = []

            def fails(prefix: list[str], first_bad: int = first_bad) -> bool:
                calls.append(prefix)
                return stages[first_bad] in prefix
            assert find_first_bad_step(stages, fails) == first_bad, first_bad
            assert len(calls) <= 1 + 4, len(calls)   # log2(10) 올림 + 처음 한 번
        assert find_first_bad_step(stages, lambda prefix: False) is None
        sample_log = '\n'.join(['x loaded from engine/a.scenario.xml (3 step(s))', 'y loaded from engine/b.scenario.xml', 'z loaded from engine/a.scenario.xml'])
        assert read_backend_chain_order(sample_log) == ['engine/a.scenario.xml', 'engine/b.scenario.xml']
        assert minimize_stages(stages, lambda prefix: 's0' in prefix and 's4' in prefix and 's7' in prefix) == ['s0', 's4', 's7']
        assert stage_failed_in_reports([{'name': 'a', 'result': 'Passed'}], 'a') is False
        assert stage_failed_in_reports([{'name': 'a', 'result': 'Failed'}], 'a') is True
        assert stage_failed_in_reports([{'name': 'b', 'result': 'Passed'}], 'a') is True, '보고 없는 단계는 짐'
        # 판정 한 줄 — 실행기의 판정 값(종류 · 까닭)을 그대로 잇는다. 끝 단계 이름(`<Pass/>`)이 까닭이 되는 일이 없다.
        failed_report = {'name': 'a', 'result': 'Failed', 'reason': 'frame 5 <Expect> #3: got 2', 'failure': {'kind': 'Assertion', 'reason': 'frame 5 <Expect> #3: got 2', 'step': ''}}
        assert describe_result(failed_report) == 'Assertion - frame 5 <Expect> #3: got 2', describe_result(failed_report)
        assert describe_result({'name': 'a', 'result': 'TimedOut', 'reason': 'hang', 'failure': {'kind': 'Hang', 'reason': 'hang', 'step': 's'}}) == 'Hang - hang'
        assert describe_result({'name': 'a', 'result': 'Failed', 'reason': 'old'}) == 'Failed - old', '판정 칸이 없는 옛 보고'
        assert describe_stage_failures([failed_report, dict(failed_report)], 'a') == 'Assertion - frame 5 <Expect> #3: got 2', '같은 판정은 한 번'
        assert describe_stage_failures([{'name': 'b', 'result': 'Passed'}], 'a').startswith('보고 없음')
        # 진 단계 고르기: 보고 없는 단계(크래시 · 행)도 진 것이고, 백엔드마다 따로 본다.
        list_report = [('dx12', {'name': 'a', 'result': 'Passed'}), ('dx12', {'name': 'b', 'result': 'Failed'}), ('vk', {'name': 'a', 'result': 'Skipped'})]
        assert select_failed_stages({'dx12': ['a', 'b', 'c'], 'vk': ['a', 'b']}, list_report) == [('dx12', 'b'), ('dx12', 'c'), ('vk', 'b')]
        assert select_failed_stages({'dx12': ['a']}, list_report) == []
        # 체인 보고 폴더 고르기 · 백엔드 목록: 체인 로그보다 늦은 혼자 실행은 빼고, 백엔드는 보고 파일 이름에서(보고 폴더에 로그가 없다).
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            chain_folder = root / 'a' / 'per-backend-1'
            solo_folder = root / 'a' / 'per-backend-2'
            other_folder = root / 'b' / 'per-backend-3'
            for folder, files in ((chain_folder, {'dx12.1.json': 'a', 'vk.1.json': 'a', 'vk.2.json': 'b'}), (solo_folder, {'dx12.json': 'a'}), (other_folder, {'dx12.json': 'b'})):
                folder.mkdir(parents=True)
                for name, stage in files.items():
                    (folder / name).write_text(json.dumps({'name': stage, 'result': 'Passed'}), encoding='utf-8')
            chain_log = root / 'Scenario.c.log'
            chain_log.write_text('', encoding='utf-8')
            now = time.time()
            os.utime(chain_folder, (now - 100, now - 100))
            os.utime(chain_log, (now - 90, now - 90))
            os.utime(solo_folder, (now, now))
            assert find_latest_per_backend_folder(root, {'a', 'b'}, chain_log) == chain_folder
            assert collect_expected_stages(chain_folder, root, 'c', ['a', 'b'], False) == {'dx12': ['a', 'b'], 'vk': ['a', 'b']}
        # 진행 분류: 뒤가 Expect watch 면 condition, 사진이면 photo, 그 밖 other. render="true" 를 따로 적는다. 주석 안의 진행 · 조건 대기(WaitUntil)는 세지 않는다.
        advances = list_advance_steps('<Scenario name="x"><!-- <AdvanceSimulation frames="9"/> --><AdvanceSimulation frames="2"/><Expect watch="A" equals="1"/>'
                                 '<AdvanceSimulation frames="600"/><Screenshot name="s"/><WaitUntil watch="B" atLeast="1"/><AdvanceSimulation frames="5" render="true"/><Click x="1"/>'
                                 '<AdvanceSimulation frames="7"/></Scenario>')
        assert [(row['frames'], row['render'], row['next'], row['kind']) for row in advances] == [
            (2, False, 'Expect', 'condition'), (600, False, 'Screenshot', 'photo'), (5, True, 'Click', 'other'), (7, False, '-', 'other')], advances


if __name__ == '__main__':
    unittest.main()
