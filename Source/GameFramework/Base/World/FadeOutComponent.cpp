#include "pch.h"

#include "GameFramework/Base/World/FadeOutComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/Utility/LifeSpanUtil.h"

namespace sw
{
    FadeOutComponent::FadeOutComponent()
        : _duration{ 0.0f }
        , _currentTimer{ 0.0f }
        , _currentAlpha{ 0.0f }
        , _listBaseAlpha{}
    {
    }

    void FadeOutComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostPhysics );

        GameObject* pOwner = getOwner();

        // 흐른 시간이 있으면 페이드 중에 다시 읽은 상태다 — 타이머 · 알파 · 기준을 그대로 이어 간다. 스프라이트 알파는 이미 흐려져 있어
        // 기준으로 다시 잡으면 안 된다.
        const bool bResumingFade = _currentTimer > 0.0f;
        if ( bResumingFade == false )
        {
            _currentTimer = 0.0f;
            _currentAlpha = 1.0f;
            _listBaseAlpha.clear();
            // 기준은 시작할 때의 알파다 — 반투명으로 만든 이펙트가 흐려지기 시작할 때 불투명으로 튀지 않게.
            if ( pOwner != nullptr )
            {
                pOwner->forEachComponentOfType<SpriteComponent>( [this]( SpriteComponent* pSprite )
                { _listBaseAlpha.push_back( pSprite->getTint()._w ); } );
            }
        }
        applyAlphaToSprites();
    }

    void FadeOutComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );

        const bool bExpired = LifeSpanUtil::advance( _currentTimer, _duration, deltaTime );
        if ( _duration <= 0.0f )
            return;
        _currentAlpha = LifeSpanUtil::computeFade( _currentTimer, _duration );
        if ( bExpired )
        {
            // **표시만 해서는 사라지지 않는다.** `markPendingDestroy()` 은 무덤 표시일 뿐이라 파괴 목록에 들어가지 않는다. 오브젝트는 틱 · 조회에서
            // 빠지지만 풀로 돌아가지 않고 `_listGameObject` 에 영원히 남아, 수명이 다한 것이 쌓일수록 프레임마다 훑는 양이 늘어난다.
            GameObject* pOwner = getOwner();
            if ( pOwner != nullptr )
                pOwner->destroy();
        }
        // 같은 오브젝트의 스프라이트라 같은 워커가 쓴다(오브젝트 단위 틱). 색은 GPU 인스턴스로 가고 배치는 그대로다.
        applyAlphaToSprites();
    }

    float32 FadeOutComponent::getDuration() const
    {
        return _duration;
    }

    float32 FadeOutComponent::getCurrentTimer() const
    {
        return _currentTimer;
    }

    float32 FadeOutComponent::getCurrentAlpha() const
    {
        return _currentAlpha;
    }

    void FadeOutComponent::setCurrentAlpha( float32 alpha )
    {
        _currentAlpha = alpha;
        applyAlphaToSprites();
    }

    void FadeOutComponent::applyAlphaToSprites()
    {
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        const float32 fade        = MathUtil::saturate( _currentAlpha );
        size_t        spriteIndex = 0;
        pOwner->forEachComponentOfType<SpriteComponent>( [this, fade, &spriteIndex]( SpriteComponent* pSprite )
        {
            const float32 baseAlpha = ( spriteIndex < _listBaseAlpha.size() ) ? _listBaseAlpha[spriteIndex] : 1.0f;
            ++spriteIndex;
            float4 tint = pSprite->getTint();
            tint._w     = baseAlpha * fade;
            pSprite->setTint( tint );
        } );
    }
} // namespace sw
