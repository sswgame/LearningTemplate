#include "pch.h"

#include "GameFramework/Kits/Voxel/VoxelBody.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Kits/Voxel/VoxelWorld.h"

namespace sw
{
    namespace
    {
        struct VoxelBodyInternal
        {
            /** @brief 몸 상자가 경계에 딱 붙어 있어도 겹침으로 치지 않게 줄이는 여유입니다. */
            static constexpr float32 kSkin = 1.0e-3f;

            static float32 getAxis( const float3& value, int32 axis ) { return axis == 0 ? value._x : ( axis == 1 ? value._y : value._z ); }

            static void setAxis( float3& value, int32 axis, float32 component )
            {
                if ( axis == 0 )
                    value._x = component;
                else if ( axis == 1 )
                    value._y = component;
                else
                    value._z = component;
            }

            /** @brief @p current 를 @p target 으로 @p maxDelta 만큼 다가가게 합니다. */
            static float32 approach( float32 current, float32 target, float32 maxDelta )
            {
                if ( current < target )
                    return MathUtil::min( current + maxDelta, target );
                return MathUtil::max( current - maxDelta, target );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    VoxelBody::VoxelBody()
        : _settings{}
        , _position{ 0.0f, 0.0f, 0.0f }
        , _velocity{ 0.0f, 0.0f, 0.0f }
        , _waterBlock{ kVoxelAirBlock }
        , _bOnGround{ SW_FALSE }
        , _bInWater{ SW_FALSE }
    {
    }

    void VoxelBody::setPosition( const float3& position )
    {
        _position  = position;
        _velocity  = float3{ 0.0f, 0.0f, 0.0f };
        _bOnGround = SW_FALSE;
    }

    void VoxelBody::step( const VoxelWorld& world, const float3& wishDirection, bool bJump, bool bSprint, float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        const float32 clampedTime = MathUtil::min( deltaTime, 0.25f );
        const int32   stepCount   = MathUtil::max( 1, static_cast<int32>( MathUtil::ceil( clampedTime / _settings._maxStep ) ) );
        const float32 stepTime    = clampedTime / static_cast<float32>( stepCount );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            integrate( world, wishDirection, bJump, bSprint, stepTime );
    }

    bool VoxelBody::overlapsBlock( const VoxelCoord& coord ) const
    {
        const float32 skin = VoxelBodyInternal::kSkin;
        return _position._x - _settings._halfWidth + skin < static_cast<float32>( coord._x + 1 ) && _position._x + _settings._halfWidth - skin > static_cast<float32>( coord._x ) &&
               _position._y + skin < static_cast<float32>( coord._y + 1 ) && _position._y + _settings._height - skin > static_cast<float32>( coord._y ) &&
               _position._z - _settings._halfWidth + skin < static_cast<float32>( coord._z + 1 ) && _position._z + _settings._halfWidth - skin > static_cast<float32>( coord._z );
    }

    bool VoxelBody::isBlockedAt( const VoxelWorld& world, const float3& position ) const
    {
        const float32 skin = VoxelBodyInternal::kSkin;
        const int32   minX = static_cast<int32>( MathUtil::floor( position._x - _settings._halfWidth + skin ) );
        const int32   maxX = static_cast<int32>( MathUtil::floor( position._x + _settings._halfWidth - skin ) );
        const int32   minY = static_cast<int32>( MathUtil::floor( position._y + skin ) );
        const int32   maxY = static_cast<int32>( MathUtil::floor( position._y + _settings._height - skin ) );
        const int32   minZ = static_cast<int32>( MathUtil::floor( position._z - _settings._halfWidth + skin ) );
        const int32   maxZ = static_cast<int32>( MathUtil::floor( position._z + _settings._halfWidth - skin ) );
        for ( int32 y = minY; y <= maxY; ++y )
        {
            for ( int32 z = minZ; z <= maxZ; ++z )
            {
                for ( int32 x = minX; x <= maxX; ++x )
                {
                    if ( world.isSolid( x, y, z ) )
                        return true;
                }
            }
        }
        return false;
    }

    void VoxelBody::integrate( const VoxelWorld& world, const float3& wishDirection, bool bJump, bool bSprint, float32 deltaTime )
    {
        const int32 waistY = static_cast<int32>( MathUtil::floor( _position._y + _settings._height * 0.4f ) );
        _bInWater          = ( _waterBlock != kVoxelAirBlock &&
                      world.getBlock( static_cast<int32>( MathUtil::floor( _position._x ) ), waistY, static_cast<int32>( MathUtil::floor( _position._z ) ) ) == _waterBlock )
                               ? SW_TRUE
                               : SW_FALSE;

        // 수평 — 원하는 속도로 다가간다(땅에서 빠르게, 공중에서 느리게).
        float3        wish{ wishDirection._x, 0.0f, wishDirection._z };
        const float32 wishLength = wish.getLength();
        if ( wishLength > 1.0f )
            wish = wish * ( 1.0f / wishLength );
        float32 speed = bSprint ? _settings._sprintSpeed : _settings._walkSpeed;
        if ( _bInWater != SW_FALSE )
            speed = _settings._swimSpeed;
        const float32 acceleration = ( _bOnGround != SW_FALSE || _bInWater != SW_FALSE ) ? _settings._groundAcceleration : _settings._airAcceleration;
        _velocity._x               = VoxelBodyInternal::approach( _velocity._x, wish._x * speed, acceleration * deltaTime );
        _velocity._z               = VoxelBodyInternal::approach( _velocity._z, wish._z * speed, acceleration * deltaTime );

        // 수직 — 중력 · 점프 · 헤엄.
        if ( _bInWater != SW_FALSE )
        {
            _velocity._y -= _settings._gravity * 0.2f * deltaTime;
            if ( bJump )
                _velocity._y = VoxelBodyInternal::approach( _velocity._y, _settings._swimSpeed, _settings._gravity * deltaTime );
            _velocity._y = MathUtil::max( _velocity._y, -_settings._swimSpeed );
        }
        else
        {
            if ( bJump && _bOnGround != SW_FALSE )
                _velocity._y = _settings._jumpSpeed;
            _velocity._y = MathUtil::max( _velocity._y - _settings._gravity * deltaTime, -_settings._maxFallSpeed );
        }

        _bOnGround = SW_FALSE;
        if ( moveAxis( world, 1, _velocity._y * deltaTime ) )
        {
            if ( _velocity._y < 0.0f )
                _bOnGround = SW_TRUE;
            _velocity._y = 0.0f;
        }
        if ( moveAxis( world, 0, _velocity._x * deltaTime ) )
            _velocity._x = 0.0f;
        if ( moveAxis( world, 2, _velocity._z * deltaTime ) )
            _velocity._z = 0.0f;

        // 서 있는지 — 움직이지 않아도 발밑을 본다(점프 판정이 한 걸음 늦지 않게).
        if ( _bOnGround == SW_FALSE && _velocity._y <= 0.0f )
        {
            float3 probe = _position;
            probe._y -= 0.01f;
            if ( isBlockedAt( world, probe ) )
                _bOnGround = SW_TRUE;
        }
    }

    bool VoxelBody::moveAxis( const VoxelWorld& world, int32 axis, float32 delta )
    {
        if ( delta == 0.0f )
            return false;
        float3 target = _position;
        VoxelBodyInternal::setAxis( target, axis, VoxelBodyInternal::getAxis( _position, axis ) + delta );
        if ( isBlockedAt( world, target ) == false )
        {
            _position = target;
            return false;
        }
        // 막혔다 — 닿는 칸 경계에 붙인다. 몸의 앞쪽 면이 다음 정수 경계에 오도록.
        const float32 current    = VoxelBodyInternal::getAxis( _position, axis );
        const float32 lowExtent  = ( axis == 1 ) ? 0.0f : _settings._halfWidth;
        const float32 highExtent = ( axis == 1 ) ? _settings._height : _settings._halfWidth;
        float32       snapped    = current;
        if ( delta > 0.0f )
            snapped = MathUtil::floor( current + highExtent + delta ) - highExtent - VoxelBodyInternal::kSkin * 0.5f;
        else
            snapped = MathUtil::floor( current - lowExtent + delta ) + 1.0f + lowExtent + VoxelBodyInternal::kSkin * 0.5f;
        // 붙인 자리가 처음보다 뒤로 가거나 그 자리도 막혔으면 움직이지 않는다.
        const bool bForward = ( delta > 0.0f ) ? ( snapped >= current ) : ( snapped <= current );
        VoxelBodyInternal::setAxis( target, axis, snapped );
        if ( bForward && isBlockedAt( world, target ) == false )
            _position = target;
        return true;
    }
} // namespace sw
