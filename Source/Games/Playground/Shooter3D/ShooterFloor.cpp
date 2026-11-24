#include "pch.h"

#include "Games/Playground/Shooter3D/ShooterFloor.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    float3 ShooterFloorStep::toAbsolute( const float3& position, float32 yaw, const float3& local )
    {
        // 요 0 이 +Z, 요 90° 가 +X 다(`ControlIntent::computeWorldMove` 와 같다).
        const float32 sinYaw = MathUtil::sin( yaw );
        const float32 cosYaw = MathUtil::cos( yaw );
        return position + float3{ local._x * cosYaw + local._z * sinYaw, local._y, -local._x * sinYaw + local._z * cosYaw };
    }

    float3 ShooterFloorStep::toLocal( const float3& position, float32 yaw, const float3& absolute )
    {
        return toAbsolute( float3{}, -yaw, absolute - position );
    }

    bool ShooterFloorStep::isOver( const float3& position, float32 yaw, const float3& feet ) const
    {
        const float3 local = toLocal( position, yaw, feet );
        return _halfSize._x > 0.0f && MathUtil::abs( local._x ) <= _halfSize._x && MathUtil::abs( local._z ) <= _halfSize._z;
    }

    bool ShooterFloorStep::isStandingAfter( const float3& feet ) const
    {
        return isOver( _toPosition, _toYaw, feet ) && MathUtil::abs( feet._y - ( _toPosition._y + _halfSize._y ) ) <= kStandingTolerance;
    }

    float3 ShooterFloorStep::carry( const float3& feet ) const
    {
        return toAbsolute( _toPosition, _toYaw, toLocal( _fromPosition, _fromYaw, feet ) );
    }

    float3 ShooterFloorStep::carryStanding( const vector<ShooterFloorStep>& listFloor, const float3& feet, bool bOnGround )
    {
        if ( bOnGround == false )
            return feet;
        for ( const ShooterFloorStep& floor : listFloor )
        {
            const bool bStanding = floor.isOver( floor._fromPosition, floor._fromYaw, feet ) && MathUtil::abs( feet._y - ( floor._fromPosition._y + floor._halfSize._y ) ) <= kStandingTolerance;
            if ( bStanding )
                return floor.carry( feet );
        }
        return feet;
    }

    float32 ShooterFloorStep::computeGround( const vector<ShooterFloorStep>& listFloor, const float3& feet, float32 startHeight )
    {
        float32 ground = 0.0f;
        for ( const ShooterFloorStep& floor : listFloor )
        {
            const float32 top = floor._toPosition._y + floor._halfSize._y;
            if ( startHeight >= top - kStepHeight && floor.isOver( floor._toPosition, floor._toYaw, feet ) )
                ground = MathUtil::max( ground, top );
        }
        return ground;
    }

    uint8 ShooterFloorStep::findNumber( const vector<ShooterFloorStep>& listFloor, const float3& feet )
    {
        for ( size_t floorIndex = 0; floorIndex < listFloor.size(); ++floorIndex )
        {
            if ( listFloor[floorIndex].isStandingAfter( feet ) )
                return static_cast<uint8>( floorIndex + 1u );
        }
        return 0;
    }
} // namespace sw
