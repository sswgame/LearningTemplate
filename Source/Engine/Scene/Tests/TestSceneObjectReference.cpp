/**
 * @file TestSceneObjectReference.cpp
 * @brief 씬 파일을 넘는 오브젝트 참조가 씬 파일을 옮긴 뒤에도 이어지는지 봅니다(nogpu).
 */
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Content/AssetRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneObjectReference.h"

using namespace sw;

/** @brief [SceneObjectReferenceTest] 씬을 옮겨도 앞서 만든 참조 · 저장한 글이 같은 오브젝트를 찾는다 — 경로로 견주면 옮긴 씬의 참조가 모두 끊긴다 */
SW_TEST_CASE( SceneObjectReferenceTest, SurvivesMovingTheScene )
{
    const AssetGUID            sceneGUID = AssetGUID::create();
    const string               oldPath   = "game/test/maps/old.scene.xml";
    const string               newPath   = "game/test/maps/sub/new.scene.xml";
    vector<AssetRegistryLayer> listLayer;
    listLayer.push_back( AssetRegistryLayer{ "test", { AssetRegistryEntry{ sceneGUID, oldPath } } } );
    AssetRegistry previous = AssetRegistry::install( std::move( *AssetRegistry::merge( listLayer ) ) );

    Scene scene( "Moved" );
    scene.setSourcePath( oldPath );
    const uint64         objectID = scene.getObjectManager()->createGameObject( hashed_string( "Target" ) )->getObjectID();
    SceneObjectReference reference;
    SW_EXPECT_TRUE( SceneObjectReference::makeReference( scene, objectID, reference ) );
    const string savedText = reference._targetScene.toText();
    SW_EXPECT_EQUAL( AssetRegistry::makeReferenceText( sceneGUID ), savedText );

    SW_EXPECT_TRUE( AssetRegistry::move( oldPath, newPath ) );
    scene.setSourcePath( newPath );
    uint64 resolvedID = 0;
    SW_EXPECT_TRUE( SceneObjectReference::resolveInScene( scene, reference, resolvedID ) == SceneObjectResolveResult::Resolved );
    SW_EXPECT_EQUAL( objectID, resolvedID );
    SceneObjectReference loaded;
    SW_EXPECT_TRUE( loaded._targetScene.assign( savedText ) );
    loaded._fileID = reference._fileID;
    SW_EXPECT_TRUE( SceneObjectReference::resolveInScene( scene, loaded, resolvedID ) == SceneObjectResolveResult::Resolved );

    // 옛 자리에 새로 생긴 씬은 다른 씬이다.
    Scene replacement( "Replacement" );
    replacement.setSourcePath( oldPath );
    SW_EXPECT_TRUE( SceneObjectReference::resolveInScene( replacement, reference, resolvedID ) == SceneObjectResolveResult::SceneNotLoaded );
    (void)AssetRegistry::install( std::move( previous ) );
}
