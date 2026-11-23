"""
키트 뼈대를 만든다 — 새 키트 하나를 붙이는 자리(매니페스트 · CMake · 카탈로그 · 컴포넌트 · 에디터 확장의 맵 검사 · 테스트)를 한 번에 놓는다.

  py -3 -m Scripts new-kit Fishing --group Feature/World [--dry-run]

폴더 틀(`docs/agents/FolderLayout.md` 의 키트 줄): `Source/GameFramework/Kits/<묶음>/<이름>/`
  `<이름>Catalog`              데이터 카탈로그(`<팩>/data/<이름 소문자>.xml`) — 카탈로그 창이 등록 줄만으로 열고 검증한다
  Component/`<이름>Component`  카탈로그 항목 하나를 가리키는 컴포넌트(`CatalogKey` 칸)
  `<이름>GameFeature`          게임 기능 — 켜면 카탈로그가 걸리고 받는 이름 `<이름>` 의 오브젝트에 컴포넌트가 붙는다
  Editor/                      에디터 확장 `GF_EditorPlugin_<이름>`(Dev) — 씬이 쓰는 항목이 팩에 있는지 보는 맵 검사
  Tests/                       카탈로그 읽기 테스트
중앙 표 한 곳에 두 줄을 더한다: 카탈로그 키(`engine/settings/datacatalogreferences.xml` 의 `<CatalogKey>` · `<Field>`) — 쿠커와 인스펙터 콤보가 같은 줄을 본다.
온라인 키트는 `new-online-kit` 이다(Runtime · Server 로 나뉜다).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from common.exit_code import ExitCode
from common.paths import get_project_root

KIT_ROOT = "Source/GameFramework/Kits"
REFERENCE_TABLE = "Content/engine/settings/datacatalogreferences.xml"
REFERENCE_TABLE_END = "</DataCatalogReferenceTable>"
ONLINE_GROUP = "Feature/Online"


def make_manifest(name: str, kind: str, description: str, dependencies: list[str], configurations: list[str], targets: list[str]) -> str:
    def make_list(key: str, values: list[str]) -> str:
        rows = ",\n".join(f'        "{value}"' for value in values)
        return f'    "{key}": [\n{rows}\n    ],\n'

    dependency_rows = ",\n".join(f'        {{\n            "_name": "{value}"\n        }}' for value in dependencies)
    return (f'{{\n    "_name": "{name}",\n    "_version": "1.0.0",\n    "_kind": "{kind}",\n    "_description": "{description}",\n'
            f'    "_listDependency": [\n{dependency_rows}\n    ],\n' + make_list("_listConfiguration", configurations) + make_list("_listTarget", targets)
            + '    "_bEnabledByDefault": true\n}\n')


def make_cmake(path: str, brief: str, call: str) -> str:
    line = "# " + "=" * 78
    return f"{line}\n# @file {path}/CMakeLists.txt\n# @brief {brief}\n{line}\n\n{call}\n"


def make_files(name: str, group: str) -> dict[str, str]:
    """키트 폴더 기준 상대 경로 → 글. C++ 은 놓은 뒤 `py -3 -m Scripts format` 이 맞춘다."""
    include = f"GameFramework/Kits/{group}/{name}"
    kit_path = f"{KIT_ROOT}/{group}/{name}"
    lower_camel = name[0].lower() + name[1:]
    kind = name.lower()
    files: dict[str, str] = {}
    files[f"GF_{name}.module.json"] = make_manifest(f"GF_{name}", "Kit", f"{name} 키트 — 카탈로그 · 컴포넌트", ["GameFramework"], ["Dev", "Shipping"], ["Client", "Server"])
    files["CMakeLists.txt"] = make_cmake(kit_path, f"{name} 키트 — 카탈로그 · 컴포넌트", f"sw_add_game_framework_kit(GF_{name})")
    files[f"{name}Catalog.h"] = f'''/**
 * @file {name}Catalog.h
 * @brief {name} 항목 정의를 담습니다. `{kind}.xml` 에서 읽습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{{
    /** @brief {name} 항목 하나입니다. */
    REFLECT()
    struct {name}Def
    {{
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{{}};
        PROPERTY( Required )
        string _name{{}};
    }};
}} // namespace sw

namespace sw
{{
    /**
     * @class {name}Catalog
     * @brief `<{name}Catalog><Entry id="first" name="First"/></{name}Catalog>` 를 읽습니다.
     */
    class SW_KIT_API {name}Catalog : public XMLCatalog<{name}Catalog>
    {{
        friend class XMLCatalog<{name}Catalog>;

    public:
        static constexpr const utf8* kDataKind = "{kind}"; ///< 데이터 종류 이름(팩 데이터 경로 · 게임 기능 `loadCatalog`)
        /** @brief 이 데이터 종류입니다(등록은 .cpp 의 공유 캐시 줄이 로더를 더한다). */
        static constexpr AssetTypeDef kAssetType = AssetTypeDef::makeDataFile( kDataKind, AssetPathRule{{ "game/", {{}}, "/data/{kind}.xml" }}, {{}} );

        {name}Catalog();

        const {name}Def*         findEntry( const CatalogKey<{name}Def>& key ) const {{ return _catalog.find( key ); }}
        const vector<{name}Def>& getEntries() const {{ return _catalog.getAll(); }}

    private:
        static constexpr const utf8* kXMLRootName = "{name}Catalog"; ///< 루트 요소(`XMLCatalog`)
        uint32                       loadRoot( const XMLCatalogRoot& root, string_view sourceName );

    private:
        GameCatalog<{name}Def> _catalog;
    }};

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 `SharedAssetCache<{name}Catalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_KIT_API_EXTERN_TEMPLATE, {name}Catalog );
}} // namespace sw
'''
    files[f"{name}Catalog.cpp"] = f'''#include "pch.h"

#include "{include}/{name}Catalog.h"

#include "Engine/Module/ModuleSharedAssetCache.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{{
    {name}Catalog::{name}Catalog()
        : _catalog{{}}
    {{
    }}

    uint32 {name}Catalog::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {{
        using Element                          = XMLCatalogElement<{name}Catalog>;
        static constexpr Element kArrElement[] = {{
            Element::of<&{name}Catalog::_catalog>( "Entry" ),
        }};
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result.isClean() ? result._loadedCount : 0; // 틀린 항목을 빼고 쓰면 그 항목을 쓰는 씬이 조용히 바뀐다
    }}
}} // namespace sw

namespace sw
{{
    /** @brief `{name}Catalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_MODULE_SHARED_ASSET_CACHE( {name}, {name}Catalog, SW_KIT_API, {name}Catalog::kAssetType.withLoader( AssetTypeLoader::of<{name}Catalog>( MemoryTag::Game ) ) );
}} // namespace sw
'''
    files[f"Component/{name}Component.h"] = f'''/**
 * @file {name}Component.h
 * @brief {name} 카탈로그의 항목 하나를 씬에 둡니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{{
    struct {name}Def;

    /**
     * @class {name}Component
     * @brief 파일 머리말 참고.
     */
    REFLECT( Category = "{name}", DisplayName = "{name}", Tooltip = "Places one {name} catalog entry in the scene" )
    class SW_KIT_API {name}Component : public Component
    {{
    public:
        REFLECT_BODY();

        {name}Component();
        ~{name}Component() override = default;

        const CatalogKey<{name}Def>& getEntry() const {{ return _entry; }}

    protected:
        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;

    private:
        PROPERTY( Category = "{name}", DisplayName = "Entry", Tooltip = "Entry of the pack's {name} catalog", Required )
        CatalogKey<{name}Def> _entry;
    }};
}} // namespace sw
'''
    files[f"Component/{name}Component.cpp"] = f'''#include "pch.h"

#include "{include}/Component/{name}Component.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "{include}/{name}Catalog.h"

namespace sw
{{
    {name}Component::{name}Component()
        : _entry{{}}
    {{
    }}

    void {name}Component::onRegister( GameObjectManager& manager )
    {{
        Component::onRegister( manager );
        manager.getComponentRegistry().add<{name}Component>( this );
    }}

    void {name}Component::onUnregister( GameObjectManager& manager )
    {{
        manager.getComponentRegistry().remove<{name}Component>( this );
        Component::onUnregister( manager );
    }}
}} // namespace sw
'''
    files[f"{name}GameFeature.cpp"] = f'''#include "pch.h"

#include "{include}/Component/{name}Component.h"
#include "{include}/{name}Catalog.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureScope.h"

namespace sw
{{
    namespace
    {{
        /** @brief 카탈로그(팩의 `data/{kind}.xml`)를 걸고 받는 이름 `{name}` 의 오브젝트(`GameFeatureReceiverComponent`)에 컴포넌트를 붙입니다. */
        [[nodiscard]] bool activate{name}( GameFeatureScope& scope )
        {{
            return scope.loadCatalog<{name}Catalog>() && scope.addComponent<{name}Component>( "{name}" );
        }}
    }} // namespace

    SW_GAME_FEATURE( {name}, "{name}", &activate{name} );
}} // namespace sw
'''
    files[f"Tests/Test{name}Catalog.cpp"] = f'''#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "{include}/{name}Catalog.h"

/**
 * @brief [{name}CatalogTest] 항목을 읽어 키로 찾고, 겹친 id 가 있는 파일은 거절한다
 */
SW_TEST_CASE( {name}CatalogTest, ReadsEntriesAndRejectsDuplicates )
{{
    sw::{name}Catalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<{name}Catalog><Entry id="first" name="First"/></{name}Catalog>)", "{name}CatalogTest" ) );
    const sw::{name}Def* pEntry = catalog.findEntry( sw::CatalogKey<sw::{name}Def>( sw::hashed_string( "first" ) ) );
    SW_REQUIRE_TRUE( pEntry != nullptr );
    SW_EXPECT_TRUE( pEntry->_name == "First" );

    SW_EXPECT_LOG( "GameDataXML.DuplicateID" );
    sw::{name}Catalog duplicated;
    SW_EXPECT_FALSE( duplicated.loadFromXMLText( R"(<{name}Catalog><Entry id="first" name="A"/><Entry id="first" name="B"/></{name}Catalog>)", "{name}CatalogTest" ) );
}}
'''
    files[f"Editor/GF_EditorPlugin_{name}.module.json"] = make_manifest(f"GF_EditorPlugin_{name}", "EditorExtension", f"{name} 키트의 에디터 확장 — 맵 검사",
                                                                        ["EditorModule", f"GF_{name}"], ["Dev"], ["Client"])
    files["Editor/CMakeLists.txt"] = make_cmake(f"{kit_path}/Editor", f"{name} 키트의 에디터 확장 — 맵 검사 (Dev 전용, 매니페스트 GF_EditorPlugin_{name}.module.json)",
                                                f"sw_add_editor_extension(GF_EditorPlugin_{name})")
    files[f"Editor/{name}MapCheck.cpp"] = f'''/**
 * @file {name}MapCheck.cpp
 * @brief 맵 검사 "{name} 항목" — 씬의 `{name}Component` 가 가리키는 항목이 활성 팩의 {name} 카탈로그에 있는지 봅니다(쿠커의 키 검사를 저작 중에 앞당긴다).
 */
#include "pch.h"

#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/Scene/EditorMapCheck.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "{include}/Component/{name}Component.h"
#include "{include}/{name}Catalog.h"

namespace sw::editor
{{
    namespace
    {{
        struct {name}MapCheckInternal
        {{
            static constexpr int32 kOrder = 900;

            static bool hasEntry( const vector<shared_ptr<const {name}Catalog>>& listCatalog, const CatalogKey<{name}Def>& key )
            {{
                for ( const shared_ptr<const {name}Catalog>& pCatalog : listCatalog )
                {{
                    if ( pCatalog->findEntry( key ) != nullptr )
                        return true;
                }}
                return false;
            }}

            static void validateEntries( const GameObjectManager& manager, EditorMapCheckReport& outReport )
            {{
                vector<shared_ptr<const {name}Catalog>> listCatalog;
                vector<const AssetTypeDef*>             listKind;
                for ( const string& assetID : outReport.getPackAssets() )
                {{
                    AssetTypeRegistry::collectPathMatches( assetID, listKind );
                    if ( listKind.size() != 1 || string_view{{ listKind[0]->_name }} != string_view{{ {name}Catalog::kAssetType._name }} )
                        continue;
                    const shared_ptr<const {name}Catalog> pCatalog = SharedAssetCache<{name}Catalog>::acquire( assetID ).snapshot();
                    if ( pCatalog != nullptr )
                        listCatalog.push_back( pCatalog ); // 읽지 못하는 카탈로그는 카탈로그 검사가 알린다
                }}
                for ( const {name}Component* pComponent : manager.getComponentRegistry().getAll<{name}Component>() )
                {{
                    const CatalogKey<{name}Def>& key = pComponent->getEntry();
                    if ( key.empty() || hasEntry( listCatalog, key ) )
                        continue; // 빈 필수 칸은 오브젝트 검증이 알린다
                    const GameObject*                        pOwner = pComponent->getOwner();
                    const hashed_string                      label  = pOwner != nullptr ? pOwner->getName() : hashed_string{{}};
                    StringBuilder<constant::buffer::kMax256> message;
                    message.appendFormat( "'%#' is not an entry of the active pack's {name} catalog", key.c_str() );
                    outReport.add( ValidationSeverity::Error, label.view(), message.view() );
                }}
            }}
        }};
    }} // namespace

    SW_EDITOR_MAP_CHECK( {name}Entries, "{lower_camel}.entries", {name}MapCheckInternal::kOrder, &{name}MapCheckInternal::validateEntries );
}} // namespace sw::editor
'''
    return files


def add_reference_rows(text: str, name: str) -> str:
    """카탈로그 키 표 글에 이 키트의 두 줄을 더한다. 이미 있으면 그대로 돌려준다."""
    if f'tag="{name}Def"' in text:
        return text
    if text.count(REFERENCE_TABLE_END) != 1:
        raise RuntimeError(f"{REFERENCE_TABLE_END} anchor not found in {REFERENCE_TABLE}")
    newline = "\r\n" if "\r\n" in text else "\n"
    rows = (f'\t<CatalogKey tag="{name}Def" catalog="{name}Catalog" entry="Entry"/>{newline}'
            f'\t<Field owner="{name}Component" field="_entry" tag="{name}Def"/>{newline}')
    return text.replace(REFERENCE_TABLE_END, rows + REFERENCE_TABLE_END)


def describe_invalid_request(root: Path, name: str, group: str) -> str | None:
    """만들 수 없는 까닭을 돌려준다(만들 수 있으면 None)."""
    if re.fullmatch(r"[A-Z][A-Za-z0-9]+", name) is None:
        return f"'{name}' is not PascalCase"
    if re.fullmatch(r"(Feature|Genre)/[A-Z][A-Za-z0-9]+", group) is None or not (root / KIT_ROOT / group).is_dir():
        return f"'{group}' is not a kit group folder under {KIT_ROOT} (for example Feature/World or Genre/Simulation)"
    if group == ONLINE_GROUP:
        return "online kits are split into Runtime and Server - use new-online-kit"
    list_manifest = sorted((root / KIT_ROOT).rglob(f"GF_{name}.module.json"))
    if list_manifest:
        return f"GF_{name} already exists: {list_manifest[0].relative_to(root).as_posix()}"
    if (root / KIT_ROOT / group / name).exists():
        return f"{KIT_ROOT}/{group}/{name} already exists"
    return None


def main_new_kit(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts new-kit", description="키트 뼈대를 만든다")
    parser.add_argument("name", help="키트 이름(PascalCase, 예: Fishing)")
    parser.add_argument("--group", required=True, help="키트 묶음 폴더(예: Feature/World · Genre/Simulation)")
    parser.add_argument("--dry-run", action="store_true", help="만들 파일만 보인다")
    args = parser.parse_args(argv)
    root = get_project_root()
    reason = describe_invalid_request(root, args.name, args.group)
    if reason is not None:
        print(f"[new-kit] {reason}", file=sys.stderr)
        return ExitCode.CHECK_INVALID
    table_path = root / REFERENCE_TABLE
    table_text = table_path.read_bytes().decode("utf-8")
    new_table_text = add_reference_rows(table_text, args.name)  # 파일을 하나라도 쓰기 전에 표의 자리를 확인한다
    kit_directory = root / KIT_ROOT / args.group / args.name
    for relative, content in make_files(args.name, args.group).items():
        path = kit_directory / relative
        print(f"[new-kit] {'would write' if args.dry_run else 'write'} {path.relative_to(root).as_posix()}")
        if args.dry_run is False:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8", newline="")
    if new_table_text != table_text:
        print(f"[new-kit] {'would add rows to' if args.dry_run else 'added rows to'} {REFERENCE_TABLE}")
        if args.dry_run is False:
            table_path.write_bytes(new_table_text.encode("utf-8"))
    if args.dry_run is False:
        print(f"[new-kit] next: py -3 -m Scripts format, "
              f"then add GF_{args.name} to `_listDependency` of Source/Games/<game>/SWGame.module.json "
              f"and '{args.name}' to gamesettings gameFeatures or a minigame's features")
    return ExitCode.PASSED
