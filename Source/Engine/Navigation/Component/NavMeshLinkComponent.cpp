#include "pch.h"

#include "Engine/Navigation/Component/NavMeshLinkComponent.h"

#include "Core/Common/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Navigation/Component/SceneNavigation.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Object/Component/ComponentNameValidation.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    SW_LOG_CALLER( "SceneNavigation" );

    namespace
    {
        struct NavMeshLinkComponentInternal
        {
            /** @brief 링크 번호를 정할 때 위치를 자르는 단위(센티미터)입니다. 부동소수 잡음이 번호를 바꾸지 않게 합니다. */
            static constexpr float32 kIDQuantumPerMeter = 100.0f;

            static uint64 quantize( float32 value )
            {
                return static_cast<uint64>( static_cast<int64>( MathUtil::round( value * kIDQuantumPerMeter ) ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    NavMeshLinkComponent::NavMeshLinkComponent()
        : _startOffset{ 0.0f, 0.0f, 0.0f }
        , _endOffset{ 0.0f, 0.0f, 2.0f }
        , _width{ 0.5f }
        , _areaName{}
        , _linkType{}
        , _bBidirectional{ true }
        , _bLinkEnabled{ true }
        , _reportedBounds{ AABB::empty() }
        , _navIndex{ SceneNavigation::kNotRegistered }
        , _bEnabledDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void NavMeshLinkComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _navRegistration = manager.getSubsystem<SceneNavigation>().registerLink( this );
        // 플레이 중에 생긴 링크는 그 위치의 타일을 다시 베이크해야 포함된다(내비메시가 아직 없으면 알림은 버려진다).
        invalidateLinkArea();
    }

    void NavMeshLinkComponent::onUnregister( GameObjectManager& manager )
    {
        SceneNavigation& navigation = manager.getSubsystem<SceneNavigation>();
        if ( _reportedBounds.isValid() )
            navigation.invalidateArea( _reportedBounds, true );
        _navRegistration.reset();
        Component::onUnregister( manager );
    }

    void NavMeshLinkComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        _bEnabledDirty = SW_TRUE;
        invalidateLinkArea();
    }

    void NavMeshLinkComponent::invalidateLinkArea()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        SceneNavigation& navigation = pManager->getSubsystem<SceneNavigation>();
        if ( _reportedBounds.isValid() )
            navigation.invalidateArea( _reportedBounds, true );
        float3 start{};
        float3 end{};
        if ( computeWorldEndpoints( start, end ) == false )
            return;
        NavOffMeshLink link;
        link._start     = start;
        link._end       = end;
        link._radius    = _width;
        _reportedBounds = link.computeBounds();
        navigation.invalidateArea( _reportedBounds, true );
    }

    bool NavMeshLinkComponent::computeWorldEndpoints( float3& outStart, float3& outEnd ) const
    {
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
            return false;
        const float4x4 world = pScene->getWorldMatrix();
        outStart             = float3::transform( _startOffset, world );
        outEnd               = float3::transform( _endOffset, world );
        return true;
    }

    uint32 NavMeshLinkComponent::computeLinkID( const float3& start, const float3& end, const hashed_string& linkType )
    {
        using Internal = NavMeshLinkComponentInternal;
        uint64 hash    = HashUtil::kFNVOffset64;
        for ( const float32 value : { start._x, start._y, start._z, end._x, end._y, end._z } )
        {
            hash = HashUtil::combine( hash, Internal::quantize( value ) );
        }
        hash              = HashUtil::combine( hash, static_cast<uint64>( linkType.getHash() ) );
        const uint32 fold = static_cast<uint32>( hash ^ ( hash >> 32 ) );
        // 0 은 번호 없는 링크다.
        return fold != 0 ? fold : 1u;
    }

    uint32 NavMeshLinkComponent::computeCurrentLinkID() const
    {
        float3 start{};
        float3 end{};
        if ( computeWorldEndpoints( start, end ) == false )
            return 0;
        return computeLinkID( start, end, _linkType );
    }

    bool NavMeshLinkComponent::makeLink( const NavMeshSettings& settings, uint8 fallbackArea, NavOffMeshLink& outLink ) const
    {
        outLink = NavOffMeshLink{};
        if ( computeWorldEndpoints( outLink._start, outLink._end ) == false )
            return false;
        outLink._radius         = _width;
        outLink._userID         = computeLinkID( outLink._start, outLink._end, _linkType );
        outLink._bBidirectional = _bBidirectional ? SW_TRUE : SW_FALSE;
        outLink._area           = fallbackArea;
        if ( _areaName.empty() == false && settings.findAreaIndex( _areaName, outLink._area ) == false )
        {
            const GameObject* pOwner = getOwner();
            SW_LOG_ERROR( "NavMesh link on '%#' names unknown area '%#' - baked as the object's area", pOwner != nullptr ? pOwner->getName().c_str() : "",
                          _areaName.c_str() );
            outLink._area = fallbackArea;
        }
        return true;
    }

    void NavMeshLinkComponent::setStartOffset( const float3& offset )
    {
        _startOffset = offset;
        invalidateLinkArea();
    }

    void NavMeshLinkComponent::setEndOffset( const float3& offset )
    {
        _endOffset = offset;
        invalidateLinkArea();
    }

    void NavMeshLinkComponent::setWidth( float32 width )
    {
        _width = width;
        invalidateLinkArea();
    }

    void NavMeshLinkComponent::setBidirectional( bool bBidirectional )
    {
        _bBidirectional = bBidirectional;
        invalidateLinkArea();
    }

    void NavMeshLinkComponent::setAreaName( const hashed_string& areaName )
    {
        _areaName = areaName;
        invalidateLinkArea();
    }

    void NavMeshLinkComponent::setLinkType( const hashed_string& linkType )
    {
        // 종류 이름은 번호에 포함된다 — 다시 베이크해야 새 번호로 알린다. 막힌 상태도 새 번호로 다시 적용한다.
        _linkType      = linkType;
        _bEnabledDirty = SW_TRUE;
        invalidateLinkArea();
    }

    void NavMeshLinkComponent::setLinkEnabled( bool bEnabled )
    {
        if ( _bLinkEnabled == bEnabled )
            return;
        _bLinkEnabled  = bEnabled;
        _bEnabledDirty = SW_TRUE;
    }
} // namespace sw

namespace sw
{
    void NavMeshLinkComponent::validateAreaName( ValidationContext& context ) const
    {
        ComponentNameValidation::validateNavArea( context, "_areaName", _areaName );
    }
} // namespace sw
