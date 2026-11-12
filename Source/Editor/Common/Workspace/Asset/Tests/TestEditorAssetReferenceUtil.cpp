// EditorAssetReferenceUtilTest — 에셋을 옮기면 메모리의 씬이 든 참조를 다시 읽지 않고 새 id 로 바꿔 끼운다(감사 P0-4): 컴포넌트의 에셋 경로 칸 · 엔티티의 프리팹 경로.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/Asset/EditorAssetReferenceUtil.h"

#include "Engine/Animation/Notify/AnimationNotifyComponent.h"
#include "Engine/Object/Component/ComponentAssetPathUtil.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/PropertyInfo.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/Serializer/AssetRefBase.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct EditorAssetReferenceUtilTestInternal
    {
        /** @brief @p component 의 칸 @p pName 입니다. 없으면 nullptr 입니다. */
        static const PropertyInfo* findProperty( const Component& component, const utf8* pName )
        {
            const PropertyInfo* pFound = nullptr;
            const hashed_string name( pName );
            component.getTypeInfo()->forEachProperty( [&pFound, &name]( const PropertyInfo& prop )
            {
                if ( prop._name == name )
                    pFound = &prop;
            }, true );
            return pFound;
        }
    };
} // namespace

/** @brief [EditorAssetReferenceUtilTest] 컴포넌트의 에셋 참조 칸(`AssetRef<T>`)이 옛 id 면 새 id 로 바뀌고 바꾼 칸 이름이 돌아온다. 맵에 없는 값은 그대로다 */
SW_TEST_CASE( EditorAssetReferenceUtilTest, ComponentAssetRefFollowsTheMove )
{
    Scene                     scene( "ReferenceRemapProbe" );
    GameObject* const         pObject = scene.getObjectManager()->createGameObject( hashed_string( "ReferenceRemapProbe" ) );
    AnimationNotifyComponent* pNotify = pObject->addComponent<AnimationNotifyComponent>();
    SW_REQUIRE_TRUE( pNotify != nullptr );
    const PropertyInfo* const pPath = EditorAssetReferenceUtilTestInternal::findProperty( *pNotify, "_notifyTablePath" );
    SW_REQUIRE_TRUE( pPath != nullptr );
    SW_REQUIRE_TRUE( pPath->getValuePtr<AssetRefBase>( pNotify )->assign( "scratch/p04/old.notifies.xml" ) );

    unordered_map<string, string> mapUnrelated;
    mapUnrelated.insert_or_assign( string( "scratch/p04/other.notifies.xml" ), string( "scratch/p04/else.notifies.xml" ) );
    vector<hashed_string> listChanged;
    SW_EXPECT_EQUAL( 0u, EditorAssetReferenceUtil::remapComponent( *pNotify, mapUnrelated, listChanged ) );
    SW_EXPECT_TRUE( ComponentAssetPathUtil::readAssetPath( *pPath, pNotify ) == "scratch/p04/old.notifies.xml" );

    unordered_map<string, string> mapMove;
    mapMove.insert_or_assign( string( "scratch/p04/old.notifies.xml" ), string( "scratch/p04/new.notifies.xml" ) );
    SW_EXPECT_EQUAL( 1u, EditorAssetReferenceUtil::remapComponent( *pNotify, mapMove, listChanged ) );
    SW_EXPECT_TRUE( ComponentAssetPathUtil::readAssetPath( *pPath, pNotify ) == "scratch/p04/new.notifies.xml" );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listChanged.size() );
    SW_EXPECT_TRUE( listChanged[0] == hashed_string( "_notifyTablePath" ) );
}

/** @brief [EditorAssetReferenceUtilTest] 엔티티의 프리팹 경로도 새 id 로 바뀐다 — 씬을 다시 읽지 않으므로 오브젝트(되돌리기 · 선택이 가리키는 id)는 그대로다 */
SW_TEST_CASE( EditorAssetReferenceUtilTest, EntityPrefabPathFollowsTheMove )
{
    Scene             scene( "ReferencePrefabProbe" );
    GameObject* const pObject = scene.getObjectManager()->createGameObject( hashed_string( "ReferencePrefabProbe" ) );
    (void)pObject->addComponent<SceneComponent>();
    const uint64 objectID = pObject->getObjectID();
    scene.setEntityPrefabPath( objectID, "scratch/p04/old.prefab.xml" );

    unordered_map<string, string> mapMove;
    mapMove.insert_or_assign( string( "scratch/p04/old.prefab.xml" ), string( "scratch/p04/new.prefab.xml" ) );
    SW_EXPECT_EQUAL( 1u, EditorAssetReferenceUtil::remapScene( scene, mapMove ) );
    SW_EXPECT_TRUE( scene.getEntityPrefabPath( objectID ) == "scratch/p04/new.prefab.xml" );
    SW_EXPECT_TRUE( scene.getObjectManager()->resolveGameObject( pObject->getHandle() ) == pObject );
}
