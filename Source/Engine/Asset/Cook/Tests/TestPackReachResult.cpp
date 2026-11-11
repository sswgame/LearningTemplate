#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/Cook/AssetCompanion.h"
#include "Engine/Asset/Cook/PackReachResult.h"
#include "Engine/Asset/Reference/AssetReferenceFields.h"
#include "Engine/Content/Pack/AssetCookRoot.h"
#include "Engine/Content/Pack/PackConfig.h"

using namespace sw;

// PackReachabilityTest — 쓰는 것만 담는 쿠킹의 고르기: 뿌리(항상 담기 · 필수 데이터)에서 참조 칸 · 부모(`_base`) · 곁 파일을 따라가고, 빠진 참조는 오류다.
// 칸 목록은 저장소의 표(Config/Dev/AssetReferenceFields*.json)를 그대로 읽고, 파일은 메모리에 둔다(nogpu).

namespace
{
    struct PackReachabilityTestUtil
    {
        /** @brief 메모리의 글 에셋입니다. 글이 없는 파일(텍스처 · 메시)은 목록에만 있다. */
        class MemorySource final : public IPackReachSource
        {
        public:
            unordered_map<string, string> _mapText;

            bool readText( const string& assetID, string& outText ) const override
            {
                const auto itText = _mapText.find( assetID );
                if ( itText == _mapText.end() )
                    return false;
                outText = itText->second;
                return true;
            }
        };

        static bool loadRepositoryFields( AssetReferenceFields& outParams ) { return outParams.load( ProcessRoots::get()._project.str() ); }

        /** @brief 맵 폴더를 항상 담고, 필수 데이터로 외형 폴더 · 아이템 파일을 아는 규칙입니다. */
        static PackCookRules makeRules()
        {
            PackCookRules       rules;
            PackAlwaysCookRule& maps = rules._listAlwaysCook.emplace_back();
            maps._name               = "Maps";
            maps._listPattern.push_back( "game/*/maps/**" );
            rules._gameSettingsPath          = "data/gamesettings.xml";
            PackRequiredDataPath& items      = rules._listRequiredData.emplace_back();
            items._kind                      = "items";
            items._path                      = "data/items.xml";
            PackRequiredDataPath& appearance = rules._listRequiredData.emplace_back();
            appearance._kind                 = "appearance";
            appearance._path                 = "data/appearance";
            rules._listUnfollowedField.push_back( "assetRoots" );
            return rules;
        }

        /** @brief 글 에셋 하나를 담을 수 있는 파일로 더합니다. */
        static void addText( PackReachFiles& ioFiles, MemorySource& ioSource, const string& assetID, string text )
        {
            ioFiles._setPackable.insert( assetID );
            ioFiles._setExisting.insert( assetID );
            ioSource._mapText.try_emplace( assetID, std::move( text ) );
        }

        /** @brief 글이 없는 파일 하나를 담을 수 있는 파일로 더합니다. */
        static void addBinary( PackReachFiles& ioFiles, const string& assetID )
        {
            ioFiles._setPackable.insert( assetID );
            ioFiles._setExisting.insert( assetID );
        }

        /** @brief 게임 `game/x` 하나와 그 설정(필수 데이터 @p requiredData 줄)을 둡니다. */
        static void addGame( PackReachFiles& ioFiles, MemorySource& ioSource, string_view requiredData )
        {
            ioFiles._listGameDomain.push_back( "game/x" );
            addText( ioFiles, ioSource, "game/x/data/gamesettings.xml", string( "<GameSettings><startMap>game/x/maps/a.scene.xml</startMap>" ) + string( requiredData ) + "</GameSettings>" );
        }
    };
    using Util = PackReachabilityTestUtil;

    struct PackEntryCatalogTestUtil
    {
        /** @brief 외형 카탈로그 둘(프리셋 · 외형)과 그 칸을 아는 규칙입니다. */
        static PackCookRules makeRules()
        {
            PackCookRules     rules   = Util::makeRules();
            PackEntryCatalog& presets = rules._listEntryCatalog.emplace_back();
            presets._kind             = "preset";
            presets._path             = "data/appearance/presets.xml";
            presets._entryElement     = "CharacterAppearance";
            presets._idAttribute      = "id";
            PackEntryCatalog& visuals = rules._listEntryCatalog.emplace_back();
            visuals._kind             = "visual";
            visuals._path             = "data/appearance/itemvisuals.xml";
            visuals._entryElement     = "ItemVisual";
            visuals._idAttribute      = "id";
            addField( rules, "_presetID", "preset" );
            addField( rules, "body", "visual" );
            return rules;
        }

        static void addField( PackCookRules& ioRules, const utf8* pField, const utf8* pKind )
        {
            PackEntryIDField& field = ioRules._listEntryIDField.emplace_back();
            field._field            = pField;
            field._kind             = pKind;
        }

        /** @brief 쓰는 프리셋 Farmer(외형 body_farmer → 부모 body_base) · 안 쓰는 Knight 를 둡니다. @p requiredData 는 게임 설정의 줄입니다. */
        static void addCatalogs( PackReachFiles& ioFiles, Util::MemorySource& ioSource, string_view requiredData, const utf8* pExtraMapText )
        {
            Util::addGame( ioFiles, ioSource, requiredData );
            Util::addText( ioFiles, ioSource, "game/x/maps/a.scene.xml", pExtraMapText );
            Util::addText( ioFiles, ioSource, "common/data/appearance/presets.xml",
                           R"(<CharacterAppearanceCatalog><CharacterAppearance id="Farmer" body="body_farmer"/><CharacterAppearance id="Knight" body="body_knight"/></CharacterAppearanceCatalog>)" );
            Util::addText( ioFiles, ioSource, "common/data/appearance/itemvisuals.xml", R"(<ItemVisualCatalog>
  <ItemVisual id="body_base"><Part mesh="common/models/base.mesh"/></ItemVisual>
  <ItemVisual id="body_farmer" _base="body_base"><Part mesh="common/models/farmer.mesh"/></ItemVisual>
  <ItemVisual id="body_knight"><Part mesh="common/models/knight.mesh"/></ItemVisual>
</ItemVisualCatalog>)" );
            for ( const utf8* pMesh : { "common/models/base.mesh", "common/models/farmer.mesh", "common/models/knight.mesh" } )
            {
                Util::addBinary( ioFiles, pMesh );
            }
        }
    };
    using EntryUtil = PackEntryCatalogTestUtil;
} // namespace

/** @brief [PackReachabilityTest] 뿌리(맵)에서 메시 · 머티리얼 · 부모(`_base`) · 텍스처까지 따라가고, 아무도 가리키지 않는 파일은 담지 않는다. 메시 곁의 파쇄도 담긴다 */
SW_TEST_CASE( PackReachabilityTest, FollowsReferencesFromTheRoots )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( Util::loadRepositoryFields( params ) );
    PackReachFiles     files;
    Util::MemorySource source;
    Util::addGame( files, source, "" );
    Util::addText( files, source, "game/x/maps/a.scene.xml", R"(<Scene><Entity mesh="common/models/a.mesh" material="common/materials/a.material"/></Scene>)" );
    Util::addText( files, source, "common/materials/a.material",
                   R"(<MaterialDef _base="engine/materials/base.material"><_properties><item assetPath="common/textures/a.dds" name="_albedoMap"/></_properties></MaterialDef>)" );
    Util::addText( files, source, "engine/materials/base.material", R"(<MaterialDef><_properties><item assetPath="engine/textures/white.dds" name="_albedoMap"/></_properties></MaterialDef>)" );
    Util::addBinary( files, "common/models/a.mesh" );
    Util::addBinary( files, "common/models/a.fracture" );
    Util::addBinary( files, "common/textures/a.dds" );
    Util::addBinary( files, "engine/textures/white.dds" );
    Util::addBinary( files, "common/textures/unused.dds" );
    Util::addBinary( files, "common/models/unused.mesh" );

    PackReachResult result;
    SW_REQUIRE_TRUE( PackReachResult::collect( params, Util::makeRules(), files, source, result ) );
    for ( const utf8* pIncluded : { "game/x/data/gamesettings.xml", "game/x/maps/a.scene.xml", "common/models/a.mesh", "common/materials/a.material", "engine/materials/base.material",
                                    "common/textures/a.dds", "engine/textures/white.dds" } )
    {
        SW_EXPECT_TRUE( result._setIncluded.contains( string( pIncluded ) ) );
    }
    SW_EXPECT_TRUE( AssetCompanionRegistry::hasRule( "MeshFracture" ) );
    SW_EXPECT_TRUE( result._setIncluded.contains( string( "common/models/a.fracture" ) ) );
    SW_EXPECT_FALSE( result._setIncluded.contains( string( "common/textures/unused.dds" ) ) );
    SW_EXPECT_FALSE( result._setIncluded.contains( string( "common/models/unused.mesh" ) ) );
    SW_EXPECT_EQUAL( 1u, result._rootCount ); // 맵 하나 — 게임 설정은 필수 데이터의 출발점이라 담지만 뿌리 수는 그 줄(여기는 없다)로 센다
}

/** @brief [PackReachabilityTest] 닿는 참조가 어디에도 없는 파일을 가리키면 조용히 빼지 않고 쿠킹 오류다 */
SW_TEST_CASE( PackReachabilityTest, MissingReferenceStopsTheCook )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( Util::loadRepositoryFields( params ) );
    PackReachFiles     files;
    Util::MemorySource source;
    Util::addGame( files, source, "" );
    Util::addText( files, source, "game/x/maps/a.scene.xml", R"(<Scene><Entity mesh="common/models/gone.mesh"/></Scene>)" );

    SW_EXPECT_LOG( "PackReachability.MissingReference" );
    PackReachResult result;
    SW_EXPECT_FALSE( PackReachResult::collect( params, Util::makeRules(), files, source, result ) );
}

/** @brief [PackReachabilityTest] 제외 규칙이 뺀 원본(셰이더 원본 — 쿠킹본은 스테이징)을 가리키는 참조는 빠진 것이 아니고, 담지도 않는다 */
SW_TEST_CASE( PackReachabilityTest, ExcludedSourceIsNotMissing )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( Util::loadRepositoryFields( params ) );
    PackReachFiles     files;
    Util::MemorySource source;
    Util::addGame( files, source, "" );
    Util::addText( files, source, "game/x/maps/a.scene.xml", R"(<Scene><Entity material="game/x/materials/a.material"/></Scene>)" );
    Util::addText( files, source, "game/x/materials/a.material", R"(<MaterialDef shaderPath="engine/shaders/lit.hlsl"/>)" );
    files._setExisting.insert( "engine/shaders/lit.hlsl" );

    PackReachResult result;
    SW_REQUIRE_TRUE( PackReachResult::collect( params, Util::makeRules(), files, source, result ) );
    SW_EXPECT_TRUE( result._setIncluded.contains( string( "game/x/materials/a.material" ) ) );
    SW_EXPECT_FALSE( result._setIncluded.contains( string( "engine/shaders/lit.hlsl" ) ) );
}

/** @brief [PackReachabilityTest] 항상 담기 묶음은 참조가 없어도 담고 그 참조도 따라간다. 묶음 밖에서 아무도 가리키지 않는 같은 폴더의 파일은 담지 않는다 */
SW_TEST_CASE( PackReachabilityTest, AlwaysCookRulesAreRoots )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( Util::loadRepositoryFields( params ) );
    PackReachFiles     files;
    Util::MemorySource source;
    Util::addText( files, source, "engine/data/defaults.xml", R"(<Defaults><Item material="common/materials/default.material"/></Defaults>)" );
    Util::addText( files, source, "common/materials/default.material", R"(<MaterialDef/>)" );
    Util::addText( files, source, "common/materials/other.material", R"(<MaterialDef/>)" );

    PackCookRules       rules  = Util::makeRules();
    PackAlwaysCookRule& engine = rules._listAlwaysCook.emplace_back();
    engine._name               = "EngineContent";
    engine._listPattern.push_back( "engine/**" );

    PackReachResult result;
    SW_REQUIRE_TRUE( PackReachResult::collect( params, rules, files, source, result ) );
    SW_EXPECT_TRUE( result._setIncluded.contains( string( "engine/data/defaults.xml" ) ) );
    SW_EXPECT_TRUE( result._setIncluded.contains( string( "common/materials/default.material" ) ) );
    SW_EXPECT_FALSE( result._setIncluded.contains( string( "common/materials/other.material" ) ) );
    SW_EXPECT_EQUAL( 1u, result._rootCount );
}

/** @brief [PackReachabilityTest] 게임 설정의 `<RequiredData kind pack>` 이 그 팩의 자리(폴더면 그 아래 전부)를 뿌리로 더하고, 적지 않은 종류는 담지 않는다 */
SW_TEST_CASE( PackReachabilityTest, RequiredDataAddsTheNamedKindOnly )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( Util::loadRepositoryFields( params ) );
    PackReachFiles     files;
    Util::MemorySource source;
    Util::addGame( files, source, R"(<RequiredData kind="appearance" pack="common"/>)" );
    Util::addText( files, source, "game/x/maps/a.scene.xml", "<Scene/>" );
    Util::addText( files, source, "common/data/appearance/itemvisuals.xml", R"(<Visuals><Part mesh="common/models/knight.mesh"/></Visuals>)" );
    Util::addText( files, source, "common/data/appearance/slots.xml", "<Slots/>" );
    Util::addText( files, source, "common/data/items.xml", "<Items/>" );
    Util::addBinary( files, "common/models/knight.mesh" );

    PackReachResult result;
    SW_REQUIRE_TRUE( PackReachResult::collect( params, Util::makeRules(), files, source, result ) );
    SW_EXPECT_TRUE( result._setIncluded.contains( string( "common/data/appearance/itemvisuals.xml" ) ) );
    SW_EXPECT_TRUE( result._setIncluded.contains( string( "common/data/appearance/slots.xml" ) ) );
    SW_EXPECT_TRUE( result._setIncluded.contains( string( "common/models/knight.mesh" ) ) );
    SW_EXPECT_FALSE( result._setIncluded.contains( string( "common/data/items.xml" ) ) );
}

/** @brief [PackReachabilityTest] 표에 없는 필수 데이터 종류는 자리를 모르므로 쿠킹 오류다 */
SW_TEST_CASE( PackReachabilityTest, UnknownRequiredDataKindIsAnError )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( Util::loadRepositoryFields( params ) );
    PackReachFiles     files;
    Util::MemorySource source;
    Util::addGame( files, source, R"(<RequiredData kind="quests" pack="common"/>)" );
    Util::addText( files, source, "game/x/maps/a.scene.xml", "<Scene/>" );

    SW_EXPECT_LOG( "PackReachability.RequiredDataKindUnknown" );
    PackReachResult result;
    SW_EXPECT_FALSE( PackReachResult::collect( params, Util::makeRules(), files, source, result ) );
}

/** @brief [PackReachabilityTest] 따라가지 않는 칸(현지화 수집 뿌리 `assetRoots`)이 가리키는 폴더는 담지 않는다 — 런타임이 읽지 않는 도구 입력이다 */
SW_TEST_CASE( PackReachabilityTest, UnfollowedFieldsAreNotFollowed )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( Util::loadRepositoryFields( params ) );
    PackReachFiles     files;
    Util::MemorySource source;
    Util::addGame( files, source, "" );
    Util::addText( files, source, "game/x/maps/a.scene.xml", "<Scene/>" );
    Util::addText( files, source, "game/x/data/x.locproject.xml", "<LocalizationProject><assetRoots><item>common/props</item></assetRoots></LocalizationProject>" );
    Util::addBinary( files, "common/props/crate.mesh" );

    PackCookRules       rules = Util::makeRules();
    PackAlwaysCookRule& data  = rules._listAlwaysCook.emplace_back();
    data._name                = "GameData";
    data._listPattern.push_back( "game/*/data/**" );

    PackReachResult result;
    SW_REQUIRE_TRUE( PackReachResult::collect( params, rules, files, source, result ) );
    SW_EXPECT_TRUE( result._setIncluded.contains( string( "game/x/data/x.locproject.xml" ) ) );
    SW_EXPECT_FALSE( result._setIncluded.contains( string( "common/props/crate.mesh" ) ) );
}

/** @brief [PackReachabilityTest] 코드가 상수로 읽는 에셋(`SW_ASSET_COOK_ROOT`)은 뿌리이고 그 참조도 따라간다. 어디에도 없으면 빠진 참조와 같은 오류다. 엔진 기본 에셋 표가 등록돼 있다 */
SW_TEST_CASE( PackReachabilityTest, CodeRootsAreRoots )
{
    SW_EXPECT_TRUE( AssetCookRootRegistry::hasRoot( "engine/data/enginedefaultassets.xml" ) );
    SW_EXPECT_TRUE( AssetCookRootRegistry::hasRoot( "engine/physics/physicssettings.xml" ) );

    AssetReferenceFields params;
    SW_REQUIRE_TRUE( Util::loadRepositoryFields( params ) );
    PackReachFiles     files;
    Util::MemorySource source;
    Util::addText( files, source, "engine/data/defaults.xml", R"(<Defaults _defaultMaterial="engine/materials/default.material"/>)" );
    Util::addText( files, source, "engine/materials/default.material", "<MaterialDef/>" );
    Util::addText( files, source, "engine/materials/unused.material", "<MaterialDef/>" );
    files._listCodeRoot.push_back( "engine/data/defaults.xml" );

    PackReachResult result;
    SW_REQUIRE_TRUE( PackReachResult::collect( params, Util::makeRules(), files, source, result ) );
    SW_EXPECT_TRUE( result._setIncluded.contains( string( "engine/materials/default.material" ) ) );
    SW_EXPECT_FALSE( result._setIncluded.contains( string( "engine/materials/unused.material" ) ) );

    files._listCodeRoot.push_back( "engine/data/gone.xml" );
    SW_EXPECT_LOG( "PackReachability.MissingReference" );
    SW_EXPECT_FALSE( PackReachResult::collect( params, Util::makeRules(), files, source, result ) );
}

/** @brief [PackReachabilityTest] 자동화 전용 묶음(시나리오)은 실행기가 있는 구성에서만 뿌리다 — 자동화 없는 배포는 시나리오와 그것만 쓰는 에셋을 담지 않는다 */
SW_TEST_CASE( PackReachabilityTest, AutomationRulesNeedTheAutomationRuntime )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( Util::loadRepositoryFields( params ) );
    PackReachFiles     files;
    Util::MemorySource source;
    Util::addText( files, source, "game/x/automation/a.scenario.xml", R"(<Scenario><Load path="game/x/maps/test.scene.xml"/></Scenario>)" );
    Util::addText( files, source, "game/x/maps/test.scene.xml", "<Scene/>" );

    PackCookRules rules = Util::makeRules();
    rules._listAlwaysCook.clear(); // 맵 묶음을 빼 시나리오만 그 맵에 닿게 한다
    PackAlwaysCookRule& scenarios = rules._listAlwaysCook.emplace_back();
    scenarios._name               = "Scenarios";
    scenarios._listPattern.push_back( "game/*/automation/**" );
    scenarios._bAutomationOnly = SW_TRUE;

    PackReachResult result;
    files._automation = PackAutomationContent::Excluded;
    SW_REQUIRE_TRUE( PackReachResult::collect( params, rules, files, source, result ) );
    SW_EXPECT_FALSE( result._setIncluded.contains( string( "game/x/automation/a.scenario.xml" ) ) );
    SW_EXPECT_FALSE( result._setIncluded.contains( string( "game/x/maps/test.scene.xml" ) ) );

    files._automation = PackAutomationContent::Included;
    SW_REQUIRE_TRUE( PackReachResult::collect( params, rules, files, source, result ) );
    SW_EXPECT_TRUE( result._setIncluded.contains( string( "game/x/automation/a.scenario.xml" ) ) );
    SW_EXPECT_TRUE( result._setIncluded.contains( string( "game/x/maps/test.scene.xml" ) ) );
}

/** @brief [PackReachabilityTest] 필수 데이터로 닿은 카탈로그는 불린 항목(씬의 프리셋 → 외형 → `_base` 부모)의 에셋만 담고, 안 불린 항목의 에셋은 뺀다. 파일 자체는 담긴다 */
SW_TEST_CASE( PackReachabilityTest, EntryCatalogFollowsNamedEntriesOnly )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( Util::loadRepositoryFields( params ) );
    PackReachFiles     files;
    Util::MemorySource source;
    EntryUtil::addCatalogs( files, source, R"(<RequiredData kind="appearance" pack="common"/>)", R"(<Scene><Component _presetID="Farmer"/></Scene>)" );

    PackReachResult result;
    SW_REQUIRE_TRUE( PackReachResult::collect( params, EntryUtil::makeRules(), files, source, result ) );
    SW_EXPECT_TRUE( result._setIncluded.contains( string( "common/data/appearance/itemvisuals.xml" ) ) );
    SW_EXPECT_TRUE( result._setIncluded.contains( string( "common/models/farmer.mesh" ) ) );
    SW_EXPECT_TRUE( result._setIncluded.contains( string( "common/models/base.mesh" ) ) );
    SW_EXPECT_FALSE( result._setIncluded.contains( string( "common/models/knight.mesh" ) ) );
}

/** @brief [PackReachabilityTest] 폴더 참조로 닿은 카탈로그(표 전체를 보여 주는 화면)는 모든 항목의 에셋을 담는다 */
SW_TEST_CASE( PackReachabilityTest, EntryCatalogReachedByFolderIsWhole )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( Util::loadRepositoryFields( params ) );
    PackReachFiles     files;
    Util::MemorySource source;
    EntryUtil::addCatalogs( files, source, "", R"(<Scene><Editor appearance="common/data/appearance"/></Scene>)" );

    PackReachResult result;
    SW_REQUIRE_TRUE( PackReachResult::collect( params, EntryUtil::makeRules(), files, source, result ) );
    SW_EXPECT_TRUE( result._setIncluded.contains( string( "common/models/knight.mesh" ) ) );
    SW_EXPECT_TRUE( result._setIncluded.contains( string( "common/models/farmer.mesh" ) ) );
}

/** @brief [PackReachabilityTest] `파일#id` 참조는 그 항목만 부른다 */
SW_TEST_CASE( PackReachabilityTest, EntryCatalogFileHashIDNamesOneEntry )
{
    AssetReferenceFields params;
    SW_REQUIRE_TRUE( Util::loadRepositoryFields( params ) );
    PackReachFiles     files;
    Util::MemorySource source;
    EntryUtil::addCatalogs( files, source, "", R"(<Scene><Item _base="common/data/appearance/itemvisuals.xml#body_knight"/></Scene>)" );

    PackReachResult result;
    SW_REQUIRE_TRUE( PackReachResult::collect( params, EntryUtil::makeRules(), files, source, result ) );
    SW_EXPECT_TRUE( result._setIncluded.contains( string( "common/models/knight.mesh" ) ) );
    SW_EXPECT_FALSE( result._setIncluded.contains( string( "common/models/farmer.mesh" ) ) );
}

/** @brief [PackReachabilityTest] 스테이징 산출물은 원본이 쓰일 때만 담는다 — 같은 줄기의 원본, 셰이더는 가장 긴 줄기. 원본을 가릴 수 없는 것(스탬프 · 원본 없는 산출물)은 담는다 */
SW_TEST_CASE( PackReachabilityTest, StagedOutputsFollowTheirSource )
{
    PackReachResult reach;
    reach._setIncluded.insert( "game/x/maps/used.scene.xml" );
    reach._setReferencedExcluded.insert( "engine/shaders/post_bloom.hlsl" );

    vector<PackDomainSources> listSources;
    PackDomainSources&        gameSources = listSources.emplace_back();
    gameSources._domain                   = "game/x";
    gameSources._listExistingID.push_back( "game/x/maps/used.scene.xml" );
    gameSources._listExistingID.push_back( "game/x/maps/unused.scene.xml" );
    PackDomainSources& engineSources = listSources.emplace_back();
    engineSources._domain            = "engine";
    engineSources._listExistingID.push_back( "engine/shaders/post.hlsl" );
    engineSources._listExistingID.push_back( "engine/shaders/post_bloom.hlsl" );
    engineSources._listExistingID.push_back( "engine/shaders/matgraph/fog.hlsl" );

    PackStagedSelection staged;
    staged.build( listSources, reach );
    SW_EXPECT_TRUE( staged.isSelected( "game/x", "maps/used.scene.bin" ) );
    SW_EXPECT_TRUE( staged.isSelected( "game/x", "maps/used.preload.json" ) );
    SW_EXPECT_FALSE( staged.isSelected( "game/x", "maps/unused.scene.bin" ) );
    SW_EXPECT_TRUE( staged.isSelected( "game/x", "registry/generated.bin" ) ); // 원본이 없다
    SW_EXPECT_TRUE( staged.isSelected( "engine", "shaders/bin/dx12/post_bloom_ps.dxil" ) );
    SW_EXPECT_TRUE( staged.isSelected( "engine", "shaders/bin/dx12/post_bloom_ps_0076c9bf.dxil" ) );
    SW_EXPECT_FALSE( staged.isSelected( "engine", "shaders/bin/dx12/post_vs.dxil" ) );
    SW_EXPECT_FALSE( staged.isSelected( "engine", "shaders/bin/dx12/fog_ps.dxil" ) );
    SW_EXPECT_TRUE( staged.isSelected( "engine", "shaders/bin/dx12/cook.stamp" ) );
    SW_EXPECT_TRUE( staged.isSelected( "engine", "shaders/bin/dx12/reflection.manifest" ) );
}
