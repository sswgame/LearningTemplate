#include "pch.h"

#include "GameFramework/UI/HealthBarComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    namespace
    {
        struct HealthBarComponentInternal
        {
            /** @brief 이보다 짧은 조각은 숨깁니다(길이 0 인 사각형을 그리지 않습니다). 바 길이에 대한 비율입니다. */
            static constexpr float32 kMinSegmentRatio = 1.0e-4f;

            /** @brief @p current 를 @p target 쪽으로 옮깁니다. 속도가 0 이하면 바로 갑니다(지수 접근 — 프레임 속도에 덜 민감합니다). */
            static float32 approach( float32 current, float32 target, float32 speed, float32 deltaTime )
            {
                if ( speed <= 0.0f )
                    return target;
                return MathUtil::lerp( current, target, MathUtil::saturate( speed * deltaTime ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    HealthBarComponent::HealthBarComponent()
        : _hpRatio{ 0.0f }
        , _remainRatio{ 0.0f }
        , _targetRatio{ 0.0f }
        , _lerpSpeed{ 5.0f }
        , _offsetPos{ 0.0f, 0.0f }
        , _barSize{ 1.0f, 0.12f }
        , _fillColor{ 0.25f, 0.85f, 0.3f, 1.0f }
        , _trailColor{ 0.95f, 0.8f, 0.25f, 1.0f }
        , _backgroundColor{ 0.08f, 0.08f, 0.1f, 0.8f }
        , _sortingLayer{ "WorldUI" }
        , _bVisible{ false }
        , _spriteBatch{}
    {
    }

    void HealthBarComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostUpdate );

        _hpRatio     = MathUtil::saturate( _hpRatio );
        _remainRatio = _hpRatio;
        _targetRatio = _hpRatio;

        GameObject* pOwner = getOwner();
        if ( pOwner != nullptr )
        {
            GameObjectManager* pManager = pOwner->getManager();
            // 바탕만 있는 단색 조각이라 텍스처가 없다(머티리얼의 흰색 × 조각 색).
            _spriteBatch.setSorting( _sortingLayer, 0 );
            if ( pManager != nullptr && _spriteBatch.initialize( *pManager, {}, kEntryCount ) == false )
                SW_LOG_WARNING( "HP bar sprites could not be created" );
        }
        layoutSprites();
    }

    void HealthBarComponent::onEndPlay()
    {
        _spriteBatch.shutdown();
        Component::onEndPlay();
    }

    void HealthBarComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );

        // 채움: 줄 때는 맞은 순간 줄고, 늘 때는 차오른다. 흔적: 채움보다 길면 채움까지 줄고, 짧아질 일은 없다(채움을 따라간다).
        if ( _targetRatio < _hpRatio )
            _hpRatio = _targetRatio;
        else
            _hpRatio = HealthBarComponentInternal::approach( _hpRatio, _targetRatio, _lerpSpeed, deltaTime );
        if ( _remainRatio < _hpRatio )
            _remainRatio = _hpRatio;
        else
            _remainRatio = HealthBarComponentInternal::approach( _remainRatio, _hpRatio, _lerpSpeed, deltaTime );

        scheduleLayout();
    }

    void HealthBarComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        if ( hasBegunPlay() )
            layoutSprites();
    }

    void HealthBarComponent::onOwnerActiveInHierarchyChanged()
    {
        if ( hasBegunPlay() )
            layoutSprites();
    }

    void HealthBarComponent::setTargetRatio( float32 ratio )
    {
        _targetRatio = MathUtil::saturate( ratio );
    }

    void HealthBarComponent::resetRatio( float32 ratio )
    {
        _targetRatio = MathUtil::saturate( ratio );
        _hpRatio     = _targetRatio;
        _remainRatio = _targetRatio;
        if ( hasBegunPlay() )
            scheduleLayout();
    }

    void HealthBarComponent::setVisible( bool bVisible )
    {
        _bVisible = bVisible;
        if ( hasBegunPlay() )
            scheduleLayout();
    }

    void HealthBarComponent::scheduleLayout()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 핸들로 다시 찾는다 — 그 사이 지워졌으면(구조 변경 큐가 먼저 돈다) 아무것도 하지 않는다.
        const ComponentHandle handle = getHandle();
        pManager->executeOrDeferPostTick( [pManager, handle]()
        {
            Component* pComponent = pManager->resolveComponent( handle );
            if ( pComponent != nullptr )
                static_cast<HealthBarComponent*>( pComponent )->layoutSprites();
        } );
    }

    void HealthBarComponent::layoutSprites()
    {
        if ( _spriteBatch.isInitialized() == false )
            return;
        const GameObject*     pOwner  = getOwner();
        const SceneComponent* pAnchor = ( pOwner != nullptr ) ? pOwner->getPrimarySceneComponent() : nullptr;
        const bool            bShown  = _bVisible && isActive() && pAnchor != nullptr;
        _spriteBatch.setVisible( bShown );
        if ( bShown == false )
            return;

        const float3  ownerPosition = pAnchor->getWorldPosition();
        const float32 width         = _barSize._x;
        const float32 height        = _barSize._y;
        const float32 left          = ownerPosition._x + _offsetPos._x - width * 0.5f;
        const float32 centerY       = ownerPosition._y + _offsetPos._y;
        const float32 fillEnd       = MathUtil::saturate( _hpRatio );
        const float32 trailEnd      = MathUtil::max( fillEnd, MathUtil::saturate( _remainRatio ) );
        const float4  fullUvRect{ 0.0f, 0.0f, 1.0f, 1.0f };

        struct Segment
        {
            uint32  _entry;
            float32 _start;
            float32 _end;
            float4  _color;
        };
        const Segment arrSegment[kEntryCount] = {
            {      kFillEntry,     0.0f,  fillEnd,       _fillColor},
            {     kTrailEntry,  fillEnd, trailEnd,      _trailColor},
            {kBackgroundEntry, trailEnd,     1.0f, _backgroundColor},
        };
        for ( const Segment& segment : arrSegment )
        {
            const float32 length = segment._end - segment._start;
            if ( length < HealthBarComponentInternal::kMinSegmentRatio )
            {
                _spriteBatch.setEntryVisible( segment._entry, false );
                continue;
            }
            const float3 center{ left + ( segment._start + length * 0.5f ) * width, centerY, ownerPosition._z };
            _spriteBatch.setEntry( segment._entry, SpriteInstanceBatch::makeQuadWorld( center, length * width, height ), fullUvRect, segment._color );
        }
    }
} // namespace sw
