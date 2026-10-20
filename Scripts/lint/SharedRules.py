"""
게이트 여럿이 같이 읽는 C++ 규칙 표 — 한 게이트의 비공개 표를 다른 게이트가 import 하면 린트 명령끼리 의존한다(층 2 끼리,
`docs/plans/ScriptsStructure.md`). 그래서 둘 이상이 읽는 표는 여기(린트 틀 층) 둔다.

- `kListLibraryRule`: 감싼 서드파티 라이브러리 표 — `gate/CheckThirdPartyIsolation.py`(헤더 · 링크 자리)와 `gate/CheckProductNames.py`(제품 이름 자리).
- `readFallibleVerbs`: 실패할 수 있는 동사(`rules/CheckFallibleNodiscard.toml`) — `gate/CheckFallibleNodiscard.py` 와 `gate/CheckDiscardReason.py`.
"""

from __future__ import annotations

from dataclasses import dataclass

from common.RuleData import kKindTextList
from lint.LintGate import LintGate

#: `rules/CheckFallibleNodiscard.toml` 에서 예외 표 말고 읽는 것.
kFallibleVerbSchema: dict[str, str] = {"fallible_verb": kKindTextList}


def readFallibleVerbs() -> tuple[str, ...]:
    """실패할 수 있는 동사. 표가 없거나 틀리면 `RuleDataError` 로 멈춘다."""
    return LintGate.readRules("CheckFallibleNodiscard", kFallibleVerbSchema, requiredKeys=("fallible_verb",))["fallible_verb"]


@dataclass(frozen=True)
class LibraryRule:
    """감싼 라이브러리 하나 — 헤더 접두어 · 그 헤더를 include 해도 되는 폴더들 · CMake 타깃 이름 · 그 타깃을 링크해도 되는 CMake 파일.

    `listCmakeTarget` 이 비면 링크 검사를 하지 않는다(엔진 · 에디터가 다시 내보내는 라이브러리 — 링크 주인을 따질 뜻이 없다).
    """

    name: str
    listIncludePrefix: tuple[str, ...]
    listAllowedRoot: tuple[str, ...]
    listCmakeTarget: tuple[str, ...]
    cmakeLinkOwner: str = "Source/Engine/CMakeLists.txt"


kListLibraryRule: tuple[LibraryRule, ...] = (
    LibraryRule("Jolt", ("Jolt/",), ("Source/Engine/Physics/Jolt/",), ("joltphysics", "Jolt::Jolt")),
    LibraryRule("Box2D", ("box2d/",), ("Source/Engine/Physics/Box2D/",), ("box2d", "box2d::box2d")),
    LibraryRule("ACL", ("acl/", "rtm/"), ("Source/Engine/Animation/Codec/ACL/",), ("acl",)),
    LibraryRule("Tracy", ("tracy/", "client/Tracy", "common/Tracy"), ("Source/Engine/Profiling/Tracy/",), ("tracy", "Tracy::TracyClient")),
    LibraryRule(
        "Recast",
        ("recastnavigation/", "Recast", "Detour", "DebugDraw.h"),
        ("Source/Engine/Navigation/Recast/",),
        ("recastnavigation", "RecastNavigation::Recast", "RecastNavigation::Detour", "RecastNavigation::DetourCrowd",
         "RecastNavigation::DetourTileCache", "RecastNavigation::DebugUtils"),
    ),
    LibraryRule("OpenSSL", ("openssl/",), ("Source/GameFramework/Base/Online/Security/OpenSSL/",), ("openssl", "OpenSSL::SSL", "OpenSSL::Crypto"),
                "Source/GameFramework/CMakeLists.txt"),
    LibraryRule("FreeType", ("ft2build.h", "freetype/"), ("Source/Engine/Text/FreeType/",), ("freetype", "Freetype::Freetype")),
    LibraryRule("RenderDoc", ("renderdoc_app.h",), ("Source/Engine/Renderer/Capture/",), ("renderdoc",)),
    LibraryRule("SQLite", ("sqlite3.h", "sqlite3ext.h"), ("Source/GameFramework/Kits/Feature/Storage/SQLStore/Shared/Driver/SQLite/",),
                ("unofficial::sqlite3::sqlite3", "SQLite::SQLite3"), "Source/GameFramework/Kits/Feature/Storage/SQLStore/Shared/CMakeLists.txt"),
    LibraryRule("PostgreSQL", ("libpq-fe.h", "libpq/", "libpq-events.h", "postgres_ext.h"), ("Source/GameFramework/Kits/Feature/Storage/SQLStore/Server/Driver/Postgres/",),
                ("PostgreSQL::PostgreSQL",), "Source/GameFramework/Kits/Feature/Storage/SQLStore/Server/CMakeLists.txt"),

    # 엔진 안의 형식 · 압축 · 셰이더 · 그래픽 API — 감싼 클래스 한 자리(XMLDocument · JSONDocument · 코덱 · ShaderCompiler · RHI 백엔드)에서만.
    LibraryRule("pugixml", ("pugixml.hpp", "pugiconfig.hpp"), ("Source/Engine/Serialization/XML/",), ()),
    LibraryRule("nlohmann-json", ("nlohmann/",), ("Source/Engine/Serialization/JSON/", "Tools/ReflectionParser/"), ()),   # 파서는 Core 만 링크한다(Engine 순환 방지)
    LibraryRule("lz4", ("lz4.h", "lz4hc.h", "lz4frame.h"), ("Source/Engine/Compression/",), ()),
    LibraryRule("zstd", ("zstd.h", "zdict.h", "zstd_errors.h"), ("Source/Engine/Compression/",), ()),
    LibraryRule("zlib", ("zlib.h", "zconf.h"), ("Source/Engine/Compression/",), ()),
    LibraryRule("DXC", ("dxcapi.h", "dxc/"), ("Source/Engine/Graphics/Shader/Compile/", "Source/Engine/Graphics/Shader/Reflection/"), ()),
    LibraryRule("glad", ("glad/",), ("Source/Engine/Graphics/RHI/GL/",), ()),
    LibraryRule("Vulkan", ("vulkan/",), ("Source/Engine/Graphics/RHI/Vulkan/", "Source/Editor/Common/Backend/Render/"), ()),
    LibraryRule("stb", ("stb_",), ("Source/Editor/Common/Asset/", "Source/Engine/Audio/AudioVorbisDecode.cpp"), ()),
    # 에디터 쪽 — 가져오기(에셋 파이프라인)와 UI.
    LibraryRule("cgltf", ("cgltf",), ("Source/Editor/Common/Asset/",), ()),
    LibraryRule("meshoptimizer", ("meshoptimizer.h",), ("Source/Editor/Common/Asset/",), ()),
    # 테스트는 엔진 DDS 읽기를 독립된 디코더로 대조한다(같은 코드로 같은 코드를 테스트하지 않으려고).
    LibraryRule("DirectXTex", ("DirectXTex",), ("Source/Editor/Common/Asset/", "Test/EditorTest/Common/Asset/"), ()),
    LibraryRule("ImGui", ("imgui", "implot", "ImGuizmo", "imgui_node_editor", "ImGuiNotify"), ("Source/Editor/", "Test/EditorUITest/"), ()),
)
