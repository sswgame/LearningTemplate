r"""
Scripts/generate/shipping_host_defaults.py

커밋된 엔진 설정과 활성 게임의 프리셋(Config/Game/<게임>.json)을 읽어 Shipping 빌드용 C++ 헤더 둘로 생성합니다.
엔진 설정은 ShippingHostDefaults.h, 게임 프리셋(팩 루트 하나)은 ShippingGameDefaults.h. 엔진 설정 헤더는 플랫폼 덮어쓰기
(Config/Platform/<플랫폼>/EngineConfig.json)도 함께 굽고, 빌드 플랫폼(SW_PLATFORM_NAME)의 것을 컴파일 때 고른다 — configure 는 project() 전이라 플랫폼을 모른다.
렌더 품질 단계 표(Config/Engine/RenderQuality.json)는 통째로 굽는다 — 단계는 실행할 때 고른다(`gv_renderQuality`, `RenderQualityOverride`).
합치기는 런타임 한 곳(`PlatformConfigOverride::apply`)이 한다 — 게임 헤더는 Shipping 만 include 하므로 Dev 에서 활성 게임을 바꿔도
엔진 TU 가 다시 컴파일되지 않습니다(Dev 는 실행할 때 게임을 고른다, `GameSelection`).

사용법:
  python Scripts/generate/shipping_host_defaults.py <engine_header_path> <game_header_path> <game_preset_path>
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common.project_constants import DIR_RUNTIME_PLATFORM_CONFIG, FILE_RUNTIME_ENGINE_CONFIG, FILE_RUNTIME_RENDER_QUALITY  # noqa: E402
from generate.generator_output import GeneratorError, run_generator, write_generated_file  # noqa: E402
from common.paths import get_project_root  # noqa: E402

TAG = "ShippingHostDefaults"

# 플랫폼 이름(SW_PLATFORM_NAME — cmake/Modules/Build/BuildLayout.cmake). 이 밖의 폴더는 어느 빌드도 읽지 않는 덮어쓰기라 오류다(철자 틀림이 조용히 무시되지 않게).
SET_PLATFORM_NAME = frozenset({"Windows", "Linux", "Android", "Web"})


def _escape_raw_string(text: str) -> str:
    """JSON 문자열을 C++ raw string literal로 이스케이프합니다."""
    delimiter = "swcfg"
    while f"){delimiter}\"" in text:
        delimiter += "x"
    return f'R"{delimiter}({text}){delimiter}"'


def collect_platform_overrides(project_root: Path) -> dict[str, str]:
    """플랫폼 이름 → 그 플랫폼의 엔진 설정 덮어쓰기 JSON 글. 폴더 이름이 플랫폼이 아니거나 글이 JSON 객체가 아니면 오류입니다."""
    override_root = project_root / DIR_RUNTIME_PLATFORM_CONFIG
    if not override_root.is_dir():
        return {}
    file_name = Path(FILE_RUNTIME_ENGINE_CONFIG).name
    overrides: dict[str, str] = {}
    for folder in sorted(override_root.iterdir()):
        if not folder.is_dir():
            raise GeneratorError(f"{folder} is not a platform folder - {DIR_RUNTIME_PLATFORM_CONFIG}/ holds only <platform>/{file_name}")
        if folder.name not in SET_PLATFORM_NAME:
            raise GeneratorError(f"{folder} is not a platform name (one of {sorted(SET_PLATFORM_NAME)}) - no build would read it")
        override_path = folder / file_name
        if not override_path.is_file():
            raise GeneratorError(f"{folder} has no {file_name} - remove the empty platform folder")
        try:
            text = override_path.read_text(encoding="utf-8").strip()
            data = json.loads(text)
        except (OSError, ValueError) as exception:
            raise GeneratorError(f"Failed to parse platform override {override_path}: {exception}") from exception
        if not isinstance(data, dict):
            raise GeneratorError(f"Platform override {override_path} must be a JSON object")
        overrides[folder.name] = text
    return overrides


def load_render_quality(project_root: Path) -> str:
    """렌더 품질 단계 표 글. 없거나, 단계 이름 → 객체의 JSON 객체가 아니면 오류입니다(필수 — 기동이 고른 단계를 이 표에서 찾는다)."""
    path = project_root / FILE_RUNTIME_RENDER_QUALITY
    try:
        text = path.read_text(encoding="utf-8").strip()
        data = json.loads(text)
    except (OSError, ValueError) as exception:
        raise GeneratorError(f"Failed to read render quality tiers {path}: {exception}") from exception
    if not isinstance(data, dict) or not data or not all(isinstance(tier, dict) for tier in data.values()):
        raise GeneratorError(f"Render quality tiers {path} must be a JSON object of tier name -> `_render` override object")
    return text


def _platform_override_lines(overrides: dict[str, str]) -> str:
    """빌드 플랫폼의 덮어쓰기 글(없으면 nullptr)을 컴파일 때 고르는 C++ 줄."""
    choice = "nullptr"
    for platform in sorted(overrides, reverse=True):
        choice = f"detail::isSamePlatformName( SW_PLATFORM_NAME, \"{platform}\" ) ? {_escape_raw_string(overrides[platform])}\n\t\t: {choice}"
    return f"""	namespace detail
	{{
		constexpr bool isSamePlatformName( const char* pLeft, const char* pRight )
		{{
			while ( *pLeft != '\\0' && *pLeft == *pRight )
			{{
				++pLeft;
				++pRight;
			}}
			return *pLeft == *pRight;
		}}
	}} // namespace detail

	/** 빌드 플랫폼의 엔진 설정 덮어쓰기(Config/Platform/<플랫폼>/EngineConfig.json)입니다. 없으면 nullptr 입니다. */
	inline constexpr const char* kEngineConfigPlatformOverrideJSON = {choice};
"""


def generate_shipping_host_defaults(output_path: Path, game_output_path: Path, game_preset_path: str) -> None:
    """
    엔진 설정과 게임 프리셋을 읽어 C++ 헤더 둘(ShippingHostDefaults.h · ShippingGameDefaults.h)을 생성합니다.
    """
    project_root = get_project_root()
    engine_path = project_root / FILE_RUNTIME_ENGINE_CONFIG
    game_path = project_root / game_preset_path

    if not engine_path.is_file():
        raise GeneratorError(f"Engine config file not found: {engine_path}")
    if not game_path.is_file():
        raise GeneratorError(f"Game config file not found: {game_path}")

    try:
        engine_json = engine_path.read_text(encoding="utf-8").strip()
        json.loads(engine_json)  # 유효성 검증
    except (OSError, ValueError) as exception:
        raise GeneratorError(f"Failed to parse engine config {engine_path}: {exception}") from exception

    try:
        game_data = json.loads(game_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exception:
        raise GeneratorError(f"Failed to parse game config {game_path}: {exception}") from exception
    # 프리셋은 팩 루트와 쿠킹만 읽는 규칙(`GameConfig::_cookRules`)뿐이다 — 나머지 설정은 팩의 gamesettings.xml 이 가진다.
    if not isinstance(game_data, dict) or "_packRoot" not in game_data or set(game_data) - {"_packRoot", "_cookRules"}:
        raise GeneratorError(f"Game preset {game_path} must contain `_packRoot` and optionally `_cookRules` (other settings belong in the pack's data/gamesettings.xml)")

    platform_lines = _platform_override_lines(collect_platform_overrides(project_root))
    render_quality_json = load_render_quality(project_root)
    content = f"""#pragma once
// AUTO-GENERATED by shipping_host_defaults.py — do not edit.
// SSOT: committed {FILE_RUNTIME_ENGINE_CONFIG} · {DIR_RUNTIME_PLATFORM_CONFIG}/<platform>/ · {FILE_RUNTIME_RENDER_QUALITY}

namespace sw::shipping_host
{{
	inline constexpr const char* kEngineConfigJSON = {_escape_raw_string(engine_json)};

	/** 렌더 품질 단계 표({FILE_RUNTIME_RENDER_QUALITY})입니다. 단계는 실행할 때 고른다. */
	inline constexpr const char* kRenderQualityJSON = {_escape_raw_string(render_quality_json)};

{platform_lines}}}
"""
    game_content = f"""#pragma once
// AUTO-GENERATED by shipping_host_defaults.py — do not edit.
// SSOT: committed {game_preset_path}

namespace sw::shipping_host
{{
	inline constexpr const char* kGameConfigJSON = {_escape_raw_string(json.dumps({"_packRoot": game_data["_packRoot"]}))};
}}
"""

    write_generated_file(output_path, content, tag=TAG, report_unchanged=False)
    write_generated_file(game_output_path, game_content, tag=TAG, report_unchanged=False)


def main(argv: Sequence[str] | None = None) -> int:
    def _add_arguments(parser: argparse.ArgumentParser) -> None:
        parser.add_argument("output", type=Path, help="엔진 설정 헤더(ShippingHostDefaults.h)")
        parser.add_argument("game_output", type=Path, help="게임 프리셋 헤더(ShippingGameDefaults.h)")
        parser.add_argument("game_preset", help="게임 프리셋(저장소 기준 — Config/Game/<게임>.json)")

    return run_generator(argv, tag=TAG, description="엔진 설정 + 게임 프리셋 → Shipping 기본값 C++ 헤더",
                        add_arguments=_add_arguments,
                        generate=lambda args: generate_shipping_host_defaults(args.output, args.game_output, args.game_preset))


if __name__ == "__main__":
    sys.exit(main())
