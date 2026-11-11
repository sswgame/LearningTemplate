/**
 * @file TestAssetReloadCascade.cpp
 * @brief 연쇄 리로드의 계획(`AssetReloadCascade::planDependents`)이 펼쳐 넣는 참조자만 의존 순서로 고르고, 순환은 오류로 멈추는지 봅니다.
 *        부모 머티리얼을 고친 뒤 계획대로 다시 읽으면 로드된 자식이 새 값을 갖는지도 봅니다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/Reference/EditorReferenceIndex.h"
#include "Editor/Common/Asset/Tests/EditorScratchDomain.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/HotReload/AssetReloadCascade.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Graphics/Material/Material.h"

namespace
{
    using sw::editor::AssetReloadCascade;
    using sw::editor::EditorAssetReference;
    using sw::editor::EditorReferenceIndex;
    using sw::editor::EditorReferenceIndexData;

    /** @brief 머티리얼만 펼쳐 넣는 종류로 본다(표의 칸 `AssetReferenceUse::InlinedAtLoad` 와 같은 판정 — 표 자체는 `MaterialRouteInlinesReferences` 가 본다). */
    bool shouldInlineMaterial( sw::string_view assetID )
    {
        return sw::StringUtil::endsWith( assetID, ".material" );
    }

    /** @brief (참조자, 대상) 쌍으로 색인을 짓는다. @p listEdge 는 `_base` 칸이고 @p listPlainEdge 는 경로만 적은 칸이다. 질의는 대상 순서의 이분 탐색이라 정렬해 넣는다. */
    void buildIndex( EditorReferenceIndex& outIndex, std::initializer_list<std::pair<const char*, const char*>> listEdge, std::initializer_list<std::pair<const char*, const char*>> listPlainEdge = {} )
    {
        EditorReferenceIndexData data{};
        for ( const auto& [pReferrer, pTarget] : listEdge )
        {
            data._listReference.push_back( EditorAssetReference{ sw::FileUtil::normalizePath( pReferrer ), sw::FileUtil::normalizePath( pTarget ), 1u, true } );
        }
        for ( const auto& [pReferrer, pTarget] : listPlainEdge )
        {
            data._listReference.push_back( EditorAssetReference{ sw::FileUtil::normalizePath( pReferrer ), sw::FileUtil::normalizePath( pTarget ), 1u, false } );
        }
        sw::VectorUtil::sort( data._listReference, []( const EditorAssetReference& left, const EditorAssetReference& right )
        { return left._targetPath != right._targetPath ? left._targetPath < right._targetPath : left._referrerPath < right._referrerPath; } );
        outIndex.assign( std::move( data ) );
    }

    sw::string makeMaterialText( const char* pName, const sw::string& parentID, const char* pRoughness )
    {
        sw::string text = "<MaterialDef formatVersion=\"0\" name=\"" + sw::string( pName ) + "\"";
        if ( parentID.empty() == false )
            text += " _base=\"" + parentID + "\"";
        else
            text += " shaderPath=\"engine/shaders/forward_lit.hlsl\" alphaMode=\"Opaque\"";
        text += "><_properties>";
        if ( pRoughness != nullptr )
            text += "<item name=\"_roughness\" type=\"Range\" defaultValue=\"" + sw::string( pRoughness ) + "\" min=\"0.0\" max=\"1.0\" />";
        text += "</_properties></MaterialDef>";
        return text;
    }

    sw::string readRoughness( const sw::Material& material )
    {
        const sw::MaterialProperty* pRoughness = material.findProperty( sw::hashed_string( "_roughness" ) );
        return pRoughness != nullptr ? pRoughness->_defaultValue : sw::string( "<none>" );
    }
} // namespace

/** @brief [AssetReloadCascadeTest] 머티리얼 종류는 표에서 "로드할 때 참조를 펼쳐 넣는다" 칸이다 — 연쇄가 이 칸으로 대상을 고른다 */
SW_TEST_CASE( AssetReloadCascadeTest, MaterialRouteInlinesReferences )
{
    using sw::editor::AssetReferenceUse;
    using sw::editor::EditorAssetTypeRegistry;
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findReloadRoute( "game/empty/materials/child.material" )._referenceUse == AssetReferenceUse::InlinedAtLoad );
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findReloadRoute( "game/empty/textures/albedo.png" )._referenceUse == AssetReferenceUse::ResolvedAtUse );
}

/**
 * @brief [AssetReloadCascadeTest] 부모 머티리얼을 고친 뒤 계획대로 다시 읽으면 로드된 자식이 부모의 새 값을 갖는다 — 자식은 부모를 로드할 때 펼쳐 넣어 스스로는 따라가지 않는다
 */
SW_TEST_CASE( AssetReloadCascadeTest, ParentChangeReloadsLoadedChildWithNewValue )
{
    const sw::test::EditorScratchDomain domain;
    const sw::string                    parentID = sw::test::EditorScratchDomain::makeID( "parent.material" );
    const sw::string                    childID  = sw::test::EditorScratchDomain::makeID( "child.material" );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( domain.makeDiskPath( "parent.material" ), makeMaterialText( "Parent", {}, "0.5" ) ) );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( domain.makeDiskPath( "child.material" ), makeMaterialText( "Child", parentID, nullptr ) ) );

    sw::shared_ptr<sw::Material> child = sw::Material::create();
    SW_REQUIRE_TRUE( child->loadFromAsset( sw::AssetID{ childID } ) );
    SW_EXPECT_EQUAL( sw::string( "0.5" ), readRoughness( *child ) );

    EditorReferenceIndex index;
    buildIndex( index, {
                           { childID.c_str(), parentID.c_str() }
    } );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( domain.makeDiskPath( "parent.material" ), makeMaterialText( "Parent", {}, "0.9" ) ) );
    SW_EXPECT_EQUAL( sw::string( "0.5" ), readRoughness( *child ) ); // 다시 읽기 전에는 옛 부모 값이다

    sw::vector<sw::string> listOrder;
    SW_REQUIRE_TRUE( AssetReloadCascade::planDependents( index, parentID, &shouldInlineMaterial, listOrder ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listOrder.size() );
    SW_EXPECT_EQUAL( sw::FileUtil::normalizePath( childID ), listOrder[0] );
    SW_REQUIRE_TRUE( child->loadFromAsset( sw::AssetID{ listOrder[0] } ) );
    SW_EXPECT_EQUAL( sw::string( "0.9" ), readRoughness( *child ) );
}

/**
 * @brief [AssetReloadCascadeTest] 3 단 사슬(조부모 → 부모 → 자식)은 이름 순서와 무관하게 부모가 자식보다 먼저 온다. 경로로만 쓰는 참조자(메시 데이터)는 연쇄하지 않는다
 * @details 자식은 조부모를 직접도 적어 두 고리에서 한 번에 찾힌다 — 넓이 우선 순서나 이름 순서(`a_` < `z_`)로 두면 자식이 부모보다 먼저 와 옛 부모를 펼친다.
 */
SW_TEST_CASE( AssetReloadCascadeTest, ThreeLevelChainReloadsInDependencyOrder )
{
    EditorReferenceIndex index;
    buildIndex( index, {
                           {"game/t/z_parent.material",    "game/t/grand.material"},
                           { "game/t/a_child.material", "game/t/z_parent.material"},
                           { "game/t/a_child.material",    "game/t/grand.material"},
                           {  "game/t/statue.mesh.xml",    "game/t/grand.material"},
    } );
    sw::vector<sw::string> listOrder;
    SW_REQUIRE_TRUE( AssetReloadCascade::planDependents( index, "game/t/grand.material", &shouldInlineMaterial, listOrder ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listOrder.size() );
    SW_EXPECT_EQUAL( sw::string( "game/t/z_parent.material" ), listOrder[0] );
    SW_EXPECT_EQUAL( sw::string( "game/t/a_child.material" ), listOrder[1] );

    // 중간 고리만 바뀌면 그 아래만 다시 읽는다.
    SW_REQUIRE_TRUE( AssetReloadCascade::planDependents( index, "game/t/z_parent.material", &shouldInlineMaterial, listOrder ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listOrder.size() );
    SW_EXPECT_EQUAL( sw::string( "game/t/a_child.material" ), listOrder[0] );
}

/** @brief [AssetReloadCascadeTest] 참조자 사이의 순환은 오류이고 아무것도 다시 읽지 않는다(방문 집합 덕에 계획은 끝난다) */
SW_TEST_CASE( AssetReloadCascadeTest, ReferenceCycleIsAnError )
{
    EditorReferenceIndex index;
    buildIndex( index, {
                           {"game/t/a.material", "game/t/base.material"},
                           {"game/t/a.material",    "game/t/b.material"},
                           {"game/t/b.material",    "game/t/a.material"},
    } );
    sw::vector<sw::string> listOrder{ "stale" };
    SW_EXPECT_LOG( "AssetReloadCascade.ReferenceCycle" );
    SW_EXPECT_FALSE( AssetReloadCascade::planDependents( index, "game/t/base.material", &shouldInlineMaterial, listOrder ) );
    SW_EXPECT_TRUE( listOrder.empty() );
}

/** @brief [AssetReloadCascadeTest] 표에 없는 XML 은 그 경로를 가진 캐시가 다시 읽고 `_base` 로 연쇄한다(부모만 고쳐도 감시가 알린다). JSON 은 상속이 없어 대상이 아니다 */
SW_TEST_CASE( AssetReloadCascadeTest, CatalogRouteInlinesReferences )
{
    using sw::editor::AssetReferenceUse;
    using sw::editor::AssetReloadRoute;
    using sw::editor::EditorAssetTypeRegistry;
    const AssetReloadRoute catalog = EditorAssetTypeRegistry::findReloadRoute( "game/t/data/items.xml" );
    SW_REQUIRE_TRUE( catalog._pCacheKindName != nullptr );
    SW_EXPECT_EQUAL( sw::string( AssetReloadRoute::kAnyCacheHoldingPath ), sw::string( catalog._pCacheKindName ) );
    SW_EXPECT_TRUE( catalog._referenceUse == AssetReferenceUse::InlinedAtLoad );
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findReloadRoute( "game/t/data/x.interactions.xml" )._referenceUse == AssetReferenceUse::InlinedAtLoad );

    const AssetReloadRoute json = EditorAssetTypeRegistry::findReloadRoute( "game/t/data/x.json" );
    SW_EXPECT_TRUE( json._pCacheKindName == nullptr );
    SW_EXPECT_TRUE( json._referenceUse == AssetReferenceUse::ResolvedAtUse );
}

/** @brief [AssetReloadCascadeTest] XML 주석에 적은 `_base="…"` 는 상속 참조가 아니다 - 주석이 닫힌 뒤의 칸은 그대로 상속이다 */
SW_TEST_CASE( AssetReloadCascadeTest, IndexIgnoresBaseInsideXMLComment )
{
    const sw::vector<sw::string>     listKnownID{ "game/t/a.xml", "game/t/b.xml" };
    sw::vector<EditorAssetReference> listReference;
    constexpr sw::string_view        kText = R"(<R><!-- <I _base="game/t/a.xml"/> --><I _base="game/t/b.xml"/></R>)";
    EditorReferenceIndex::extractReferences( "game/t/child.xml", kText, listKnownID, listReference );
    for ( const EditorAssetReference& reference : listReference )
    {
        SW_EXPECT_EQUAL( reference._targetPath == "game/t/b.xml", static_cast<bool>( reference._bInlined ) );
    }
    bool bFoundInherited = false;
    for ( const EditorAssetReference& reference : listReference )
    {
        bFoundInherited = bFoundInherited || reference._targetPath == "game/t/b.xml";
    }
    SW_EXPECT_TRUE( bFoundInherited );
}

/** @brief [AssetReloadCascadeTest] 역색인은 `_base` 칸을 다른 경로 칸과 가른다 - 항목 상속은 `#` 앞 파일이 대상이고, 이름이 비슷한 칸(`my_base`)은 아니다 */
SW_TEST_CASE( AssetReloadCascadeTest, IndexMarksBaseReferences )
{
    const sw::vector<sw::string>     listKnownID{ "game/t/a.xml", "game/t/b.xml", "game/t/c.xml", "game/t/d.xml", "game/t/e.xml" };
    sw::vector<EditorAssetReference> listReference;
    constexpr sw::string_view        kText = R"(<R><I _base="game/t/a.xml#id"/><I _base='game/t/b.xml'/><I item="game/t/c.xml"/><I my_base="game/t/d.xml"/><I _base = "game/t/e.xml"/></R>)";
    EditorReferenceIndex::extractReferences( "game/t/child.xml", kText, listKnownID, listReference );
    SW_REQUIRE_EQUAL( size_t{ 5 }, listReference.size() );
    SW_EXPECT_EQUAL( sw::string( "game/t/a.xml" ), listReference[0]._targetPath );
    SW_EXPECT_TRUE( listReference[0]._bInlined );
    SW_EXPECT_TRUE( listReference[1]._bInlined );
    SW_EXPECT_FALSE( listReference[2]._bInlined );
    SW_EXPECT_FALSE( listReference[3]._bInlined );
    SW_EXPECT_TRUE( listReference[4]._bInlined );
}

/**
 * @brief [AssetReloadCascadeTest] 카탈로그 파일은 `_base` 칸만 따라간다 - 서로의 경로를 적은 두 카탈로그는 순환이 아니고 다시 읽지 않는다. 사슬은 부모부터다
 * @details 다른 파일 항목(`_base="b.xml#id"`)과 파일 상속(`_base="b.xml"`)은 색인에서 같은 칸이다.
 */
SW_TEST_CASE( AssetReloadCascadeTest, CatalogFollowsOnlyBaseReferences )
{
    const auto inlinesCatalog = []( sw::string_view assetID )
    { return sw::StringUtil::endsWith( assetID, ".xml" ); };
    EditorReferenceIndex index;
    buildIndex( index,
                {
                    {"game/t/z_child.xml", "common/data/base.xml"},
                    {"game/t/a_grand.xml",   "game/t/z_child.xml"}
    },
                { { "game/t/quests.xml", "game/t/z_child.xml" },
                  { "game/t/z_child.xml", "game/t/quests.xml" } } );
    sw::vector<sw::string> listOrder;
    SW_REQUIRE_TRUE( AssetReloadCascade::planDependents( index, "common/data/base.xml", inlinesCatalog, listOrder ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listOrder.size() );
    SW_EXPECT_EQUAL( sw::string( "game/t/z_child.xml" ), listOrder[0] );
    SW_EXPECT_EQUAL( sw::string( "game/t/a_grand.xml" ), listOrder[1] );
}

/** @brief [AssetReloadCascadeTest] 카탈로그 파일이 서로를 부모로 삼으면 오류로 알리고 아무것도 다시 읽지 않는다 */
SW_TEST_CASE( AssetReloadCascadeTest, CatalogBaseCycleIsAnError )
{
    const auto inlinesCatalog = []( sw::string_view assetID )
    { return sw::StringUtil::endsWith( assetID, ".xml" ); };
    EditorReferenceIndex index;
    buildIndex( index, {
                           {"game/t/a.xml", "common/data/base.xml"},
                           {"game/t/a.xml",         "game/t/b.xml"},
                           {"game/t/b.xml",         "game/t/a.xml"},
    } );
    sw::vector<sw::string> listOrder;
    SW_EXPECT_LOG( "AssetReloadCascade.ReferenceCycle" );
    SW_EXPECT_FALSE( AssetReloadCascade::planDependents( index, "common/data/base.xml", inlinesCatalog, listOrder ) );
    SW_EXPECT_TRUE( listOrder.empty() );
}
