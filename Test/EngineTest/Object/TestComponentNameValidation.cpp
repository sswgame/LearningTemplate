#include "pch.h"

#include "Engine/Destruction/FractureComponent.h"
#include "Engine/Environment/Terrain/TerrainColliderComponent.h"
#include "Engine/Object/Component/2D/PixelPerfectCameraComponent.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/2D/TileMapRendererComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshLinkComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshModifierComponent.h"
#include "Engine/Object/Component/Physics/CharacterController2DComponent.h"
#include "Engine/Object/Component/Physics/CharacterControllerComponent.h"
#include "Engine/Object/Component/Physics/RigidBody2DComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Resource/ResourcePaths.h"

#include "TestFramework/TestFramework.h"

// 컴포넌트가 이름으로 가리키는 설정 표(물리 레이어 · 재질, 정렬 레이어, 내비 영역)를 검증 함수가 대 보는지 — 생성자 기본값까지.

using namespace sw;

namespace
{
    struct ComponentNameValidationTestInternal
    {
        /** @brief 컴포넌트 하나를 검증해 결과 글을 한 줄로 모읍니다. */
        template <typename TComponent>
        static uint32 validate( const TComponent* pComponent, string& outJoined )
        {
            outJoined.clear();
            if ( pComponent == nullptr )
            {
                outJoined = "the component could not be added";
                return invalid_index::kUint32;
            }
            ValidationContext context;
            const uint32      count = ReflectionValidation::validateObject( *TComponent::StaticType(), pComponent, context );
            for ( const ValidationIssue& issue : context.getIssues() )
            {
                outJoined += issue._message;
                outJoined += "\n";
            }
            return count;
        }

        /** @brief 새 오브젝트에 컴포넌트 하나만 붙입니다(물리 컴포넌트끼리 한 오브젝트에 겹치지 않게). */
        template <typename TComponent>
        static TComponent* addAlone( GameObjectManager& manager )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( "Probe" ) );
            return pObject != nullptr ? pObject->addComponent<TComponent>() : nullptr;
        }
    };
} // namespace

/**
 * @brief [ComponentNameValidationTest] 이름 칸이 있는 컴포넌트의 생성자 기본값은 모두 엔진 설정 표에 있다
 * @details 데이터에 적힌 값은 에셋 검증 규칙 `component_reference` 가 보지만 생성자 기본값은 데이터에 없다 — 표의 이름을 바꾸면 여기서 진다.
 */
SW_TEST_CASE( ComponentNameValidationTest, ConstructorDefaultsNameKnownTableEntries )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    using Internal = ComponentNameValidationTestInternal;
    GameObjectManager manager;
    string            joined;
    SW_EXPECT_TRUE_MSG( Internal::validate( Internal::addAlone<RigidBodyComponent>( manager ), joined ) == 0u, joined.c_str() );
    SW_EXPECT_TRUE_MSG( Internal::validate( Internal::addAlone<CharacterControllerComponent>( manager ), joined ) == 0u, joined.c_str() );
    SW_EXPECT_TRUE_MSG( Internal::validate( Internal::addAlone<TerrainColliderComponent>( manager ), joined ) == 0u, joined.c_str() );
    SW_EXPECT_TRUE_MSG( Internal::validate( Internal::addAlone<FractureComponent>( manager ), joined ) == 0u, joined.c_str() );
    SW_EXPECT_TRUE_MSG( Internal::validate( Internal::addAlone<NavMeshLinkComponent>( manager ), joined ) == 0u, joined.c_str() );
    SW_EXPECT_TRUE_MSG( Internal::validate( Internal::addAlone<NavMeshModifierComponent>( manager ), joined ) == 0u, joined.c_str() );
    SW_EXPECT_TRUE_MSG( Internal::validate( Internal::addAlone<RigidBody2DComponent>( manager ), joined ) == 0u, joined.c_str() );
    SW_EXPECT_TRUE_MSG( Internal::validate( Internal::addAlone<CharacterController2DComponent>( manager ), joined ) == 0u, joined.c_str() );
    SW_EXPECT_TRUE_MSG( Internal::validate( Internal::addAlone<SpriteComponent>( manager ), joined ) == 0u, joined.c_str() );
    SW_EXPECT_TRUE_MSG( Internal::validate( Internal::addAlone<TileMapRendererComponent>( manager ), joined ) == 0u, joined.c_str() );
    SW_EXPECT_TRUE_MSG( Internal::validate( Internal::addAlone<PixelPerfectCameraComponent>( manager ), joined ) == 0u, joined.c_str() );
}

/**
 * @brief [ComponentNameValidationTest] 표에 없는 이름은 칸 이름과 함께 오류 하나씩이고, 있는 이름 · 빈 이름은 조용하다
 * @details 런타임은 모르는 이름을 레이어 0 · 첫 재질 · Default 정렬 레이어 · 평지 영역으로 대신하고 로그만 남긴다. 변이: 검증 함수의 표 조회를 빼면 진다.
 */
SW_TEST_CASE( ComponentNameValidationTest, UnknownNamesAreErrorsNamingTheField )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    using Internal = ComponentNameValidationTestInternal;
    GameObjectManager manager;
    string            joined;

    RigidBodyComponent* pBody = Internal::addAlone<RigidBodyComponent>( manager );
    SW_ASSERT_NOT_NULL( pBody );
    pBody->setLayer( hashed_string( "Static" ) );
    pBody->setMaterial( hashed_string( "Wood" ) );
    SW_EXPECT_TRUE_MSG( Internal::validate( pBody, joined ) == 0u, joined.c_str() );
    {
        SW_TEST_DEFENSIVE_SCOPE( "an unknown name is an error naming the field" ); // 거절 · 실패 로그가 이 구간이 보려는 동작이다
        pBody->setLayer( hashed_string( "Statik" ) );
        pBody->setMaterial( hashed_string( "Plastic" ) );
        SW_EXPECT_TRUE_MSG( Internal::validate( pBody, joined ) == 2u, joined.c_str() );
    }
    SW_EXPECT_TRUE_MSG( joined.find( "_layer names 'Statik'" ) != string::npos, joined.c_str() );
    SW_EXPECT_TRUE_MSG( joined.find( "_material names 'Plastic'" ) != string::npos, joined.c_str() );

    NavMeshModifierComponent* pModifier = Internal::addAlone<NavMeshModifierComponent>( manager );
    SW_ASSERT_NOT_NULL( pModifier );
    pModifier->setAreaName( hashed_string( "Mud" ) );
    SW_EXPECT_TRUE_MSG( Internal::validate( pModifier, joined ) == 0u, joined.c_str() );
    {
        SW_TEST_DEFENSIVE_SCOPE( "an unknown area name is an error naming the field" ); // 거절 · 실패 로그가 이 구간이 보려는 동작이다
        pModifier->setAreaName( hashed_string( "Mudd" ) );
        SW_EXPECT_TRUE_MSG( Internal::validate( pModifier, joined ) == 1u, joined.c_str() );
    }

    SpriteComponent* pSprite = Internal::addAlone<SpriteComponent>( manager );
    SW_ASSERT_NOT_NULL( pSprite );
    pSprite->setSortingLayer( hashed_string( "Overlay" ) );
    SW_EXPECT_TRUE_MSG( Internal::validate( pSprite, joined ) == 0u, joined.c_str() );
    {
        SW_TEST_DEFENSIVE_SCOPE( "an unknown sorting layer is an error naming the field" ); // 거절 · 실패 로그가 이 구간이 보려는 동작이다
        pSprite->setSortingLayer( hashed_string( "Ovelray" ) );
        SW_EXPECT_TRUE_MSG( Internal::validate( pSprite, joined ) == 1u, joined.c_str() );
    }
    SW_EXPECT_TRUE_MSG( joined.find( "_sortingLayer names 'Ovelray'" ) != string::npos, joined.c_str() );
}
