#include "pch.h"

#include "Games/Empty/BenchMoverComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"

namespace sw
{
    BenchMoverComponent::BenchMoverComponent()
        : _pTarget{ nullptr }
        , _basePosition{ 0.0f, 0.0f, 0.0f }
        , _phase{ 0.0f }
        , _elapsed{ 0.0f }
        , _bWritesTransform{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void BenchMoverComponent::setTarget( SceneComponent* pTarget, const float3& basePosition, float32 phase, bool bWritesTransform )
    {
        _pTarget          = pTarget;
        _basePosition     = basePosition;
        _phase            = phase;
        _bWritesTransform = bWritesTransform ? SW_TRUE : SW_FALSE;
    }

    void BenchMoverComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        _elapsed += deltaTime;
        if ( _bWritesTransform == SW_FALSE || _pTarget == nullptr )
            return;

        // BenchScene::update 의 배치 쓰기와 같은 사인파 — 두 경로의 그림이 같아야 비교가 된다.
        const float32 wave  = MathUtil::sin( _elapsed + _phase );
        const float32 scale = 0.6f + 0.4f * MathUtil::abs( wave );
        _pTarget->setLocalPosition( float3{ _basePosition._x, wave * 0.75f, _basePosition._z } );
        _pTarget->setLocalScale( float3{ scale, scale, scale } );
    }
} // namespace sw
