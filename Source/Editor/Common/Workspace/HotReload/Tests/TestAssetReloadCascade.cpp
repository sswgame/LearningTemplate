/**
 * @file TestAssetReloadCascade.cpp
 * @brief 연쇄 리로드의 계획(`AssetReloadCascade::planDependents`)이 펼쳐 넣는 참조자만 의존 순서로 고르고, 순환은 오류로 멈추는지 봅니다.
 *        부모 머티리얼을 고친 뒤 계획대로 다시 읽으면 로드된 자식이 새 값을 갖는지도 봅니다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/Reference/EditorReferenceIndex.h"
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
    bool inlinesMaterial( sw::string_view assetID )
    {
        return sw::StringUtil::endsWith( assetID, ".material" );
    }

    /** @brief (참조자, 대상) 쌍으로 색인을 짓는다. 질의는 대상 순서의 이분 탐색이라 정렬해 넣는다. */
    void buildIndex( EditorReferenceIndex& outIndex, std::initializer_list<std::pair<const char*, const char*>> listEdge )
    {
        EditorReferenceIndexData data{};
        for ( const auto& [pReferrer, pTarget] : listEdge )
        {
            data._listReference.push_back( EditorAssetReference{ sw::FileUtil::normalizePath( pReferrer ), sw::FileUtil::normalizePath( pTarget ), 1u } );
        }
        sw::VectorUtil::sort( data._listReference, []( const EditorAssetReference& left, const EditorAssetReference& right )
        { return left._targetPath != right._targetPath ? left._targetPath < right._targetPath : left._referrerPath < right._referrerPath; } );
        outIndex.assign( std::move( data ) );
    }

    /**
     * @brief 케이스 폴더를 도메인 `cascadetest` 로 붙이고 사라질 때 뗍니다. 머티리얼의 부모 읽기(`XMLDocument::loadResource`)가 리소스 id 로 찾는다.
     * @details 엔진 쪽 픽스처(`ScopedScratchDomain`)는 `EngineServices.h` 를 봐 에디터가 include 하지 못한다 — 에디터 창구(`editor::getSubsystem`)로 같은 일을 한다.
     */
    class ScopedCascadeDomain
    {
    public:
        static constexpr const utf8* kDomain = "cascadetest";

        ScopedCascadeDomain()
            : _folder{ sw::FileUtil::normalizeSeparators( sw::test::makeScratchPath( kDomain ) ) }
            , _bMounted{ SW_FALSE }
        {
            _bMounted = sw::FileUtil::ensureDirectoryExists( _folder ) && sw::editor::getSubsystem<sw::AssetPaths>().mountDiskDomain( kDomain, _folder );
            SW_EXPECT_TRUE( _bMounted );
        }
        ~ScopedCascadeDomain()
        {
            if ( _bMounted )
                (void)sw::editor::getSubsystem<sw::AssetPaths>().unmountDiskDomain( kDomain );
        }
        ScopedCascadeDomain( const ScopedCascadeDomain& )            = delete;
        ScopedCascadeDomain& operator=( const ScopedCascadeDomain& ) = delete;

        [[nodiscard]] sw::string        makeDiskPath( sw::string_view relativePath ) const { return sw::FileUtil::joinPath( _folder, relativePath ); }
        [[nodiscard]] static sw::string makeID( sw::string_view relativePath ) { return sw::string( kDomain ) + "/" + sw::string( relativePath ); }

    private:
        sw::string _folder;
        uint8      _bMounted : 1;
    };

    sw::string makeMaterialText( const char* pName, const sw::string& parentID, const char* pRoughness )
    {
        sw::string text = "<MaterialDesc formatVersion=\"0\" name=\"" + sw::string( pName ) + "\"";
        if ( parentID.empty() == false )
            text += " _base=\"" + parentID + "\"";
        else
            text += " shaderPath=\"engine/shaders/forward_lit.hlsl\" alphaMode=\"Opaque\"";
        text += "><_properties>";
        if ( pRoughness != nullptr )
            text += "<item name=\"_roughness\" type=\"Range\" shaderType=\"Float\" defaultValue=\"" + sw::string( pRoughness ) + "\" min=\"0.0\" max=\"1.0\" />";
        text += "</_properties></MaterialDesc>";
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
    const ScopedCascadeDomain domain;
    const sw::string          parentID = ScopedCascadeDomain::makeID( "parent.material" );
    const sw::string          childID  = ScopedCascadeDomain::makeID( "child.material" );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( domain.makeDiskPath( "parent.material" ), makeMaterialText( "Parent", {}, "0.5" ) ) );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( domain.makeDiskPath( "child.material" ), makeMaterialText( "Child", parentID, nullptr ) ) );

    sw::shared_ptr<sw::Material> child = sw::Material::create();
    SW_REQUIRE_TRUE( child->loadFromFile( sw::AssetID{ childID } ) );
    SW_EXPECT_EQUAL( sw::string( "0.5" ), readRoughness( *child ) );

    EditorReferenceIndex index;
    buildIndex( index, {
                           { childID.c_str(), parentID.c_str() }
    } );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( domain.makeDiskPath( "parent.material" ), makeMaterialText( "Parent", {}, "0.9" ) ) );
    SW_EXPECT_EQUAL( sw::string( "0.5" ), readRoughness( *child ) ); // 다시 읽기 전에는 옛 부모 값이다

    sw::vector<sw::string> listOrder;
    SW_REQUIRE_TRUE( AssetReloadCascade::planDependents( index, parentID, &inlinesMaterial, listOrder ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listOrder.size() );
    SW_EXPECT_EQUAL( sw::FileUtil::normalizePath( childID ), listOrder[0] );
    SW_REQUIRE_TRUE( child->loadFromFile( sw::AssetID{ listOrder[0] } ) );
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
    SW_REQUIRE_TRUE( AssetReloadCascade::planDependents( index, "game/t/grand.material", &inlinesMaterial, listOrder ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listOrder.size() );
    SW_EXPECT_EQUAL( sw::string( "game/t/z_parent.material" ), listOrder[0] );
    SW_EXPECT_EQUAL( sw::string( "game/t/a_child.material" ), listOrder[1] );

    // 중간 고리만 바뀌면 그 아래만 다시 읽는다.
    SW_REQUIRE_TRUE( AssetReloadCascade::planDependents( index, "game/t/z_parent.material", &inlinesMaterial, listOrder ) );
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
    SW_EXPECT_FALSE( AssetReloadCascade::planDependents( index, "game/t/base.material", &inlinesMaterial, listOrder ) );
    SW_EXPECT_TRUE( listOrder.empty() );
}
