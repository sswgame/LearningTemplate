// FadeOutTest — 흐려지다 사라지는 이펙트: 스프라이트의 처음 알파를 기준으로 흐려지고, 다시 읽은 상태는 이어 가며, 길이가 0 이면 남는다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Visual/2D/SpriteComponent.h"

#include "GameFramework/Runtime/World/Lifetime/FadeOutComponent.h"

using namespace sw;

namespace
{
    struct FadeOutTestRig
    {
        GameObjectManager _manager{};
        GameObject*       _pObject{ nullptr };
        SpriteComponent*  _pSprite{ nullptr };
        FadeOutComponent* _pFade{ nullptr };

        explicit FadeOutTestRig( float32 duration )
        {
            _pObject = _manager.createGameObject( hashed_string( "Effect" ) );
            _pSprite = _pObject->addComponent<SpriteComponent>();
            _pSprite->setTint( float4{ 1.0f, 1.0f, 1.0f, 0.5f } );
            _pFade = _pObject->addComponent<FadeOutComponent>();
            write( "_duration", duration );
        }

        /** @brief 데이터가 정하는 칸(비공개 PROPERTY)을 씬 파일을 읽은 것처럼 씁니다. */
        template <typename T>
        void write( const utf8* pName, const T& value )
        {
            const PropertyInfo* pProperty = FadeOutComponent::StaticType()->findProperty( hashed_string( pName ) );
            SW_REQUIRE_NOT_NULL( pProperty );
            *static_cast<T*>( pProperty->getRawPtr( static_cast<void*>( _pFade ) ) ) = value;
        }

        void tick( float32 deltaTime )
        {
            TickContext context{ *_pObject, deltaTime };
            _pFade->onTick( context );
        }
    };
} // namespace

/** @brief [FadeOutTest] 반투명 스프라이트는 그 알파에서 0 으로 흐려지고, 길이에 닿는 걸음에 오브젝트가 사라진다 */
SW_TEST_CASE( FadeOutTest, FadesFromTheAuthoredAlphaThenDestroys )
{
    FadeOutTestRig         rig( 1.0f );
    const GameObjectHandle handle = rig._pObject->getHandle();
    rig._pFade->onBeginPlay();
    SW_EXPECT_NEAR( 0.5f, rig._pSprite->getTint()._w, 1.0e-5f );
    rig.tick( 0.5f );
    SW_EXPECT_NEAR( 0.25f, rig._pSprite->getTint()._w, 1.0e-5f );
    SW_EXPECT_TRUE( rig._manager.resolveGameObject( handle ) != nullptr );
    rig.tick( 0.5f );
    SW_EXPECT_TRUE( rig._manager.resolveGameObject( handle ) == nullptr );
}

/** @brief [FadeOutTest] 흐려지던 상태를 다시 읽으면 흐른 시간 · 기준 알파를 잇는다 — 이미 흐려진 알파를 기준으로 다시 잡지 않는다 */
SW_TEST_CASE( FadeOutTest, ResumedFadeKeepsItsBase )
{
    FadeOutTestRig rig( 1.0f );
    rig._pSprite->setTint( float4{ 1.0f, 1.0f, 1.0f, 0.25f } ); // 저장할 때 이미 흐려진 알파
    rig.write( "_currentTimer", 0.5f );
    rig.write( "_currentAlpha", 0.5f );
    rig.write( "_listBaseAlpha", vector<float32>{ 0.5f } );
    rig._pFade->onBeginPlay();
    SW_EXPECT_NEAR( 0.25f, rig._pSprite->getTint()._w, 1.0e-5f );
    rig.tick( 0.25f );
    SW_EXPECT_NEAR( 0.125f, rig._pSprite->getTint()._w, 1.0e-5f );
}

/** @brief [FadeOutTest] 길이 0 은 사라지지 않는다. 흐림을 바깥에서 정하면 바로 스프라이트에 닿고 0..1 로 자른다 */
SW_TEST_CASE( FadeOutTest, ZeroDurationStays )
{
    FadeOutTestRig         rig( 0.0f );
    const GameObjectHandle handle = rig._pObject->getHandle();
    rig._pFade->onBeginPlay();
    rig.tick( 100.0f );
    SW_EXPECT_TRUE( rig._manager.resolveGameObject( handle ) != nullptr );
    SW_EXPECT_NEAR( 0.5f, rig._pSprite->getTint()._w, 1.0e-5f );
    rig._pFade->setCurrentAlpha( 4.0f );
    SW_EXPECT_NEAR( 0.5f, rig._pSprite->getTint()._w, 1.0e-5f );
    rig._pFade->setCurrentAlpha( 0.5f );
    SW_EXPECT_NEAR( 0.25f, rig._pSprite->getTint()._w, 1.0e-5f );
}
