#include "pch.h"

#include "GameFramework/UI/DamageUIComponent.h"

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
                // 표시만 하면 파괴 목록에 들어가지 않아 오브젝트가 풀로 돌아오지 않는다.
                pOwner->destroy();
                return;
            }

            SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
            if ( pSceneComp != nullptr )
            {
                // 위로 떠오른다 — 월드 위다(돌아가거나 커진 부모 아래에서도).
                float3 pos = pSceneComp->getWorldPosition();
                pos._y += _floatSpeed * deltaTime;
                pSceneComp->setWorldPosition( pos );
            }
        }
    }
} // namespace sw
