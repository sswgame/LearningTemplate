#include "pch.h"

#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Panels/Inspector/InspectorPropertyLayout.h"

#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionTypes.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 묶음 전체에서 이 이름의 프로퍼티가 몇 번 나오는지 셉니다. */
    uint32 countProperty( const vector<InspectorPropertyGroup>& listGroup, const utf8* pName )
    {
        const hashed_string wanted( pName );
        uint32              count = 0;
        for ( const InspectorPropertyGroup& group : listGroup )
        {
            for ( const PropertyInfo* pProp : group._listProperty )
            {
                if ( pProp->_name == wanted )
                    ++count;
            }
        }
        return count;
    }

    /** @brief 카테고리 묶음의 자리입니다. 없으면 묶음 수입니다. */
    size_t findGroupIndex( const vector<InspectorPropertyGroup>& listGroup, const utf8* pCategory )
    {
        for ( size_t index = 0; index < listGroup.size(); ++index )
        {
            if ( listGroup[index]._category == pCategory )
                return index;
        }
        return listGroup.size();
    }
} // namespace

/**
 * @brief [InspectorPropertyLayoutTest] 파생 컴포넌트도 상속받은 프로퍼티를 한 번씩 보인다 — 기반의 카테고리가 먼저 온다
 * @details 인스펙터가 타입의 **자기** 프로퍼티만 모아, 스프라이트에는 트랜스폼(SceneComponent) · 메시 칸(MeshComponent)이 없었다. 카테고리는
 *          알파벳 순이었다. 언리얼 Details · 유니티 기본 인스펙터처럼 상속분까지, 기반부터 첫 등장 순서로 묶는다.
 */
SW_TEST_CASE( InspectorPropertyLayoutTest, DerivedComponentShowsInheritedPropertiesOnce )
{
    const TypeInfo* pSprite = SpriteComponent::StaticType();
    SW_ASSERT_NOT_NULL( pSprite );
    vector<InspectorPropertyGroup> listGroup;
    InspectorPropertyLayout::collectPropertyGroups( *pSprite, {}, EditorListFilter{ "" }, listGroup );

    SW_EXPECT_EQUAL( 1u, countProperty( listGroup, "_localPosition" ) ); // SceneComponent
    SW_EXPECT_EQUAL( 1u, countProperty( listGroup, "_meshId" ) );        // MeshComponent
    SW_EXPECT_EQUAL( 1u, countProperty( listGroup, "_meshName" ) );      // SpriteComponent
    const size_t transformAt = findGroupIndex( listGroup, "Transform" );
    const size_t renderingAt = findGroupIndex( listGroup, "Rendering" );
    SW_ASSERT_TRUE( transformAt < listGroup.size() );
    SW_ASSERT_TRUE( renderingAt < listGroup.size() );
    SW_EXPECT_TRUE( transformAt < renderingAt );
}

/**
 * @brief [InspectorPropertyLayoutTest] 인스펙터 확장이 직접 그린 프로퍼티만 빠진다 — 나머지는 그대로 보인다
 * @details 확장이 본문을 그리면 반사 프로퍼티를 통째로 감춰, 카메라의 Priority · Role 과 메시의 Bounds Radius · Blend Mode 를 고칠 수 없었다.
 *          언리얼 `IDetailCustomization::HideProperty` 처럼 확장이 그린 것만 뺀다.
 */
SW_TEST_CASE( InspectorPropertyLayoutTest, PropertiesDrawnByAnExtensionAreLeftOut )
{
    const TypeInfo* pCamera = CameraComponent::StaticType();
    SW_ASSERT_NOT_NULL( pCamera );
    vector<InspectorPropertyGroup> listGroup;
    InspectorPropertyLayout::collectPropertyGroups( *pCamera, { hashed_string( "_fovY" ), hashed_string( "_nearZ" ) }, EditorListFilter{ "" }, listGroup );

    SW_EXPECT_EQUAL( 0u, countProperty( listGroup, "_fovY" ) );
    SW_EXPECT_EQUAL( 0u, countProperty( listGroup, "_nearZ" ) );
    SW_EXPECT_EQUAL( 1u, countProperty( listGroup, "_farZ" ) );
    SW_EXPECT_EQUAL( 1u, countProperty( listGroup, "_priority" ) );
    SW_EXPECT_EQUAL( 1u, countProperty( listGroup, "_role" ) );
    SW_EXPECT_EQUAL( 1u, countProperty( listGroup, "_localPosition" ) );
}

/**
 * @brief [InspectorPropertyLayoutTest] 타입 사슬은 기반 → 파생 순서다 — 확장도 이 순서로 찾고 그린다
 * @details 확장을 정확한 타입 이름으로만 찾아, 게임이 만든 SceneComponent 파생에는 트랜스폼 칸이 없었다.
 */
SW_TEST_CASE( InspectorPropertyLayoutTest, TypeChainRunsFromBaseToDerived )
{
    vector<const TypeInfo*> listType;
    InspectorPropertyLayout::collectTypeChain( *SpriteComponent::StaticType(), listType );
    SW_ASSERT_TRUE( listType.size() >= 3 );
    SW_EXPECT_TRUE( listType.back() == SpriteComponent::StaticType() );
    size_t sceneAt = listType.size();
    size_t meshAt  = listType.size();
    for ( size_t index = 0; index < listType.size(); ++index )
    {
        if ( listType[index] == SceneComponent::StaticType() )
            sceneAt = index;
        if ( listType[index] == MeshComponent::StaticType() )
            meshAt = index;
    }
    SW_EXPECT_TRUE( sceneAt < meshAt );
    SW_EXPECT_TRUE( meshAt < listType.size() - 1 );
}
