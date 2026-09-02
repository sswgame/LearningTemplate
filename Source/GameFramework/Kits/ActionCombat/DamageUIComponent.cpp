#include "pch.h"

#include "GameFramework/Kits/ActionCombat/DamageUIComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
    DamageUIComponent::DamageUIComponent()
        : _damageValue{ 0 }
        , _lifeTime{ 0.0f }
        , _currentLife{ 0.0f }
        , _floatSpeed{ 0.0f }
        , _alpha{ 0.0f }
    {
    }

    void DamageUIComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostUpdate );

        GameObject* pOwner = getOwner();
        if ( pOwner != nullptr )
            pOwner->addTag( "UI"_tag );

        _currentLife = 0.0f;
        _alpha       = 1.0f;
    }

    void DamageUIComponent::onEndPlay()
    {
        Component::onEndPlay();
    }

    void DamageUIComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );

        _currentLife += deltaTime;
        if ( _lifeTime > 0.0f )
        {
            _alpha             = MathUtil::saturate( 1.0f - ( _currentLife / _lifeTime ) );
            GameObject* pOwner = getOwner();
            if ( pOwner == nullptr )
                return;

            if ( _currentLife >= _lifeTime )
            {
                pOwner->markPendingKill();
                return;
            }

            SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
            if ( pSceneComp != nullptr )
            {
                float3 pos = pSceneComp->getLocalPosition();
                pos._y += _floatSpeed * deltaTime;
                pSceneComp->setLocalPosition( pos );
            }
        }
    }
} // namespace sw
