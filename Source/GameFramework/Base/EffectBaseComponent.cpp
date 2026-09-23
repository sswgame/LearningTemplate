#include "pch.h"

#include "GameFramework/Base/EffectBaseComponent.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
    EffectBaseComponent::EffectBaseComponent()
        : _duration{ 0.0f }
        , _currentTimer{ 0.0f }
        , _currentAlpha{ 0.0f }
    {
    }

    void EffectBaseComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostPhysics );

        GameObject* pOwner = getOwner();
        if ( pOwner != nullptr )
            pOwner->addTag( "VFX"_tag );

        _currentTimer = 0.0f;
        _currentAlpha = 1.0f;
    }

    void EffectBaseComponent::onEndPlay()
    {
        Component::onEndPlay();
    }

    void EffectBaseComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );

        _currentTimer += deltaTime;
        if ( _duration > 0.0f )
        {
            _currentAlpha = 1.0f - ( _currentTimer / _duration );
            if ( _currentAlpha < 0.0f )
            {
                _currentAlpha = 0.0f;
                // **표시만 해서는 사라지지 않는다.** `markPendingKill()` 은 무덤 표시일 뿐이라
                // 파괴 목록에 들어가지 않는다. 오브젝트는 틱 · 조회에서 빠지지만 풀로 돌아가지
                // 않고 `_listGameObject` 에 영원히 남아, 수명이 다한 것이 쌓일수록 프레임마다
                // 훑는 양이 늘어난다. 지우려면 `destroy()` 여야 한다.
                GameObject* pOwner = getOwner();
                if ( pOwner != nullptr )
                    pOwner->destroy();
            }
        }
    }

    float32 EffectBaseComponent::getDuration() const
    {
        return _duration;
    }

    float32 EffectBaseComponent::getCurrentTimer() const
    {
        return _currentTimer;
    }

    float32 EffectBaseComponent::getCurrentAlpha() const
    {
        return _currentAlpha;
    }

    void EffectBaseComponent::setCurrentAlpha( float32 alpha )
    {
        _currentAlpha = alpha;
    }
} // namespace sw
