// BuiltinAssetTypesTest: 코어 애셋 종류의 등록 줄: 경로 → 종류 판정, 끌어 놓기 · 미리보기 동작, 다시 읽는 길, 새 에셋 틀이 자기 종류로 돌아오는지(썸네일 그리기는 뷰라 뺀다, nogpu).
#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Asset/AssetDatabase.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct BuiltinAssetTypesTestUtil
    {
        static string findID( string_view path ) { return string( EditorAssetTypeRegistry::findKind( path ).getID() ); }
    };
} // namespace

/** @brief [BuiltinAssetTypesTest] 경로의 접미사 · 자리가 종류 하나로 풀린다 — 원본과 산출물은 같은 종류이고 쿠킹 산출물 · 모르는 확장자는 그 종류가 아니다 */
SW_TEST_CASE( BuiltinAssetTypesTest, PathsResolveToOneKind )
{
    using Util = BuiltinAssetTypesTestUtil;
    SW_EXPECT_EQUAL( string( "Material" ), Util::findID( "game/a/rock.material" ) );
    SW_EXPECT_EQUAL( string( "Prefab" ), Util::findID( "common/prefabs/props/bow.prefab.xml" ) );
    SW_EXPECT_EQUAL( string( "Scene" ), Util::findID( "game/playground/maps/hub.scene.xml" ) );
    SW_EXPECT_EQUAL( string( "Shader" ), Util::findID( "engine/shaders/pbr_lit.hlsl" ) );
    SW_EXPECT_EQUAL( string( "Shader" ), Util::findID( "engine/shaders/common.hlsli" ) );
    SW_EXPECT_EQUAL( string( "Audio" ), Util::findID( "common/sounds/hit.ogg" ) );
    SW_EXPECT_EQUAL( Util::findID( "common/models/rock.mesh" ), Util::findID( "common/models_raw/rock.glb" ) );
    SW_EXPECT_EQUAL( Util::findID( "common/textures/rock.dds" ), Util::findID( "common/textures_raw/rock.png" ) );
    SW_EXPECT_FALSE( Util::findID( "common/textures/rock.dds" ).empty() );
    SW_EXPECT_FALSE( Util::findID( "game/a/rock.matgraph.xml" ) == Util::findID( "game/a/rock.material" ) );
    SW_EXPECT_FALSE( Util::findID( "common/prefabs/bow.prefab.bin" ) == string( "Prefab" ) );
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::findKind( "common/a.no_such_extension" ).isUnknown() );
}

/** @brief [BuiltinAssetTypesTest] 프리팹 · 메시 · 텍스처는 뷰포트에 놓이고 씬 · 머티리얼은 놓이지 않으며, 텍스처 미리보기는 임포트한 DDS 다 */
SW_TEST_CASE( BuiltinAssetTypesTest, ActionsFollowTheKind )
{
    for ( const string_view path : { string_view{ "common/prefabs/bow.prefab.xml" }, string_view{ "common/models/rock.mesh" }, string_view{ "common/textures/rock.dds" } } )
    {
        const IEditorAssetTypeActions* pActions = EditorAssetTypeRegistry::findActionsForPath( path );
        SW_REQUIRE_NOT_NULL( pActions );
        SW_EXPECT_TRUE( pActions->canPlaceInViewport() );
    }
    for ( const string_view path : { string_view{ "game/playground/maps/hub.scene.xml" }, string_view{ "game/a/rock.material" } } )
    {
        const IEditorAssetTypeActions* pActions = EditorAssetTypeRegistry::findActionsForPath( path );
        SW_REQUIRE_NOT_NULL( pActions );
        SW_EXPECT_FALSE( pActions->canPlaceInViewport() );
    }
    const IEditorAssetTypeActions* pTexture = EditorAssetTypeRegistry::findActionsForPath( "common/textures_raw/rock.png" );
    SW_REQUIRE_NOT_NULL( pTexture );
    SW_EXPECT_TRUE( pTexture->hasImagePreview() );
    SW_EXPECT_EQUAL( string( "common/textures/rock.dds" ), pTexture->findImagePreviewPath( "common/textures/rock.dds" ) );
    SW_EXPECT_TRUE( StringUtil::endsWith( pTexture->findImagePreviewPath( "common/textures_raw/rock.png" ), ".dds" ) );
}

/** @brief [BuiltinAssetTypesTest] 머티리얼은 제 캐시가 다시 읽고 참조하는 쪽도 다시 읽으며, 원본 이미지 · 모델은 임포터가 먼저 워커에서 돈다 */
SW_TEST_CASE( BuiltinAssetTypesTest, ReloadRoutesNameTheCacheAndImporter )
{
    const AssetReloadRoute material = EditorAssetTypeRegistry::findReloadRoute( "game/a/rock.material" );
    SW_REQUIRE_NOT_NULL( material._pCacheKindName );
    SW_EXPECT_EQUAL( string( "Material" ), string( material._pCacheKindName ) );
    SW_EXPECT_TRUE( material._pfnImportSource == nullptr );
    SW_EXPECT_TRUE( material._referenceUse == AssetReferenceUse::InlinedAtLoad );
    for ( const string_view path : { string_view{ "common/textures_raw/rock.png" }, string_view{ "common/models_raw/rock.glb" } } )
    {
        const AssetReloadRoute source = EditorAssetTypeRegistry::findReloadRoute( path );
        SW_EXPECT_TRUE( source._pfnImportSource == nullptr );
        SW_EXPECT_TRUE( source._pfnImportBatch != nullptr );
    }
}

/**
 * @brief [BuiltinAssetTypesTest] 임포트하는 원본의 확장자는 정체성 규칙(`Config/Dev/AssetIdentity.json`) 하나에서 온다 — 규칙의 모든 원본 확장자가 임포터를 가진 종류로 풀리고, 규칙 밖의 확장자는 아니다
 */
SW_TEST_CASE( BuiltinAssetTypesTest, ImportSourcesFollowTheIdentityRules )
{
    const AssetIdentityRules& rules = AssetIdentityRules::getProjectRules();
    for ( const AssetImportedFolder& imported : rules._listImportedFolder )
    {
        for ( const string& extension : imported._listSourceExtension )
        {
            StringBuilder<constant::buffer::kMax256> path;
            path.appendFormat( "common/%#/rock%#", imported._sourceFolder, extension );
            const AssetReloadRoute route = EditorAssetTypeRegistry::findReloadRoute( path.view() );
            SW_EXPECT_TRUE_MSG( route._pfnImportBatch != nullptr, path.c_str() );
            SW_EXPECT_TRUE( rules.isImportSource( imported._sourceFolder, path.view() ) );
        }
        SW_EXPECT_FALSE( rules.isImportSource( imported._sourceFolder, "common/rock.stamp" ) );
    }
    // 스프라이트 시트는 텍스처의 원본(HDR 빼고)과 임포트한 텍스처다.
    const EditorAssetType sprite{ SpriteClipAsset::kAssetType };
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::matches( sprite, "common/textures_raw/hero.png" ) );
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::matches( sprite, "common/textures_raw/hero.bmp" ) );
    SW_EXPECT_TRUE( EditorAssetTypeRegistry::matches( sprite, "common/textures/hero.dds" ) );
    SW_EXPECT_FALSE( EditorAssetTypeRegistry::matches( sprite, "common/textures_raw/sky.hdr" ) );
}

/** @brief [BuiltinAssetTypesTest] 새 에셋 틀은 글과 베낄 원본 가운데 하나만 채우고, 만든 파일 이름이 그 틀의 종류로 풀린다 */
SW_TEST_CASE( BuiltinAssetTypesTest, NewAssetTemplatesComeBackToTheirKind )
{
    vector<const EditorNewAssetTemplate*> listTemplate;
    EditorAssetTypeRegistry::collectNewAssetTemplates( listTemplate );
    SW_EXPECT_TRUE( listTemplate.size() >= 4 ); // 씬 · 프리팹 · 머티리얼 · 머티리얼 그래프
    for ( const EditorNewAssetTemplate* pTemplate : listTemplate )
    {
        SW_REQUIRE_NOT_NULL( pTemplate );
        SW_EXPECT_TRUE( ( pTemplate->_pText != nullptr ) != ( pTemplate->_pfnSourcePath != nullptr ) );
        StringBuilder<constant::buffer::kMax256> path;
        path.appendFormat( "game/playground/new/%#%#", pTemplate->_pStem, pTemplate->_pSuffix );
        const EditorAssetTypeRegistration* pKind = EditorAssetTypeRegistry::findKindInfoForPath( path.view() );
        SW_REQUIRE_NOT_NULL( pKind );
        SW_EXPECT_TRUE( pKind->_pNewAssetTemplate == pTemplate );
    }
}
