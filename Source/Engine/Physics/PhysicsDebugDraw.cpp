#include "pch.h"

#include "Engine/Physics/PhysicsDebugDraw.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    namespace
    {
        struct PhysicsDebugDrawInternal
        {
            static constexpr uint32 kCircleSegmentCount = 16;

            /** @brief 셰이프 로컬 점을 월드로 옮기는 자세 하나입니다. */
            struct Frame3D
            {
                float3     _position{};
                quaternion _rotation{};

                float3 apply( const float3& local ) const { return _position + float3::transform( local, _rotation ); }
            };

            static Frame3D makeShapeFrame( const PhysicsShapeDesc3D& shape, const float3& bodyPosition, const quaternion& bodyRotation )
            {
                Frame3D frame;
                frame._rotation = bodyRotation * quaternion::makeFromYawPitchRoll( shape._localRotation );
                frame._position = bodyPosition + float3::transform( shape._localPosition, bodyRotation );
                return frame;
            }

            /** @brief @p axisA · @p axisB 가 펼치는 평면의 원(중심 @p center)입니다. */
            static void drawCircle( IPhysicsDebugRenderer& renderer, const Frame3D& frame, const float3& center, const float3& axisA, const float3& axisB,
                                    float32 radius, const float4& color )
            {
                float3 previous = frame.apply( center + axisA * radius );
                for ( uint32 segment = 1; segment <= kCircleSegmentCount; ++segment )
                {
                    const float32 angle   = 2.0f * MathUtil::kPi * static_cast<float32>( segment ) / static_cast<float32>( kCircleSegmentCount );
                    const float3  current = frame.apply( center + axisA * ( ::cosf( angle ) * radius ) + axisB * ( ::sinf( angle ) * radius ) );
                    renderer.drawLine( previous, current, color );
                    previous = current;
                }
            }

            static void drawBox( IPhysicsDebugRenderer& renderer, const Frame3D& frame, const float3& half, const float4& color )
            {
                float3 arrCorner[8];
                for ( uint32 cornerIndex = 0; cornerIndex < 8; ++cornerIndex )
                {
                    const float3 local{ ( cornerIndex & 1u ) != 0 ? half._x : -half._x, ( cornerIndex & 2u ) != 0 ? half._y : -half._y,
                                        ( cornerIndex & 4u ) != 0 ? half._z : -half._z };
                    arrCorner[cornerIndex] = frame.apply( local );
                }
                for ( uint32 cornerIndex = 0; cornerIndex < 8; ++cornerIndex )
                {
                    for ( uint32 bit = 1; bit < 8; bit <<= 1 )
                    {
                        if ( ( cornerIndex & bit ) == 0 )
                            renderer.drawLine( arrCorner[cornerIndex], arrCorner[cornerIndex | bit], color );
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float4 PhysicsDebugDrawUtil::getBodyColor( uint8 bodyType, bool bSleeping, bool bTrigger )
    {
        float4 color{ 0.2f, 0.9f, 0.3f, 1.0f };
        if ( bTrigger )
            color = float4{ 1.0f, 0.85f, 0.1f, 1.0f };
        else if ( bodyType == static_cast<uint8>( PhysicsBodyType::Static ) )
            color = float4{ 0.6f, 0.6f, 0.6f, 1.0f };
        else if ( bodyType == static_cast<uint8>( PhysicsBodyType::Kinematic ) )
            color = float4{ 0.3f, 0.5f, 1.0f, 1.0f };
        if ( bSleeping )
            color = float4{ color._x * 0.5f, color._y * 0.5f, color._z * 0.5f, color._w };
        return color;
    }

    void PhysicsDebugDrawUtil::drawShape3D( IPhysicsDebugRenderer& renderer, const PhysicsShapeDesc3D& shape, const float3& bodyPosition,
                                            const quaternion& bodyRotation, const float4& color )
    {
        const PhysicsDebugDrawInternal::Frame3D frame = PhysicsDebugDrawInternal::makeShapeFrame( shape, bodyPosition, bodyRotation );
        const float3                            axisX{ 1.0f, 0.0f, 0.0f };
        const float3                            axisY{ 0.0f, 1.0f, 0.0f };
        const float3                            axisZ{ 0.0f, 0.0f, 1.0f };
        switch ( shape._type )
        {
            case PhysicsShapeType3D::Box:
            {
                PhysicsDebugDrawInternal::drawBox( renderer, frame, shape._halfExtents, color );
                break;
            }
            case PhysicsShapeType3D::Sphere:
            {
                PhysicsDebugDrawInternal::drawCircle( renderer, frame, float3{}, axisX, axisY, shape._radius, color );
                PhysicsDebugDrawInternal::drawCircle( renderer, frame, float3{}, axisY, axisZ, shape._radius, color );
                PhysicsDebugDrawInternal::drawCircle( renderer, frame, float3{}, axisX, axisZ, shape._radius, color );
                break;
            }
            case PhysicsShapeType3D::Capsule:
            {
                const float3 top{ 0.0f, shape._halfHeight, 0.0f };
                const float3 bottom{ 0.0f, -shape._halfHeight, 0.0f };
                PhysicsDebugDrawInternal::drawCircle( renderer, frame, top, axisX, axisZ, shape._radius, color );
                PhysicsDebugDrawInternal::drawCircle( renderer, frame, bottom, axisX, axisZ, shape._radius, color );
                PhysicsDebugDrawInternal::drawCircle( renderer, frame, top, axisX, axisY, shape._radius, color );
                PhysicsDebugDrawInternal::drawCircle( renderer, frame, bottom, axisX, axisY, shape._radius, color );
                PhysicsDebugDrawInternal::drawCircle( renderer, frame, top, axisZ, axisY, shape._radius, color );
                PhysicsDebugDrawInternal::drawCircle( renderer, frame, bottom, axisZ, axisY, shape._radius, color );
                const float3 arrSide[4] = { axisX, -axisX, axisZ, -axisZ };
                for ( const float3& side : arrSide )
                {
                    renderer.drawLine( frame.apply( top + side * shape._radius ), frame.apply( bottom + side * shape._radius ), color );
                }
                break;
            }
            case PhysicsShapeType3D::ConvexHull:
            {
                // 껍질의 면은 백엔드가 짓는다 — 점 사이의 별 모양(중심에서 각 점)으로 부피만 보인다.
                float3 center{};
                for ( const float3& point : shape._listPoint )
                {
                    center += point;
                }
                if ( shape._listPoint.empty() == false )
                    center = center * ( 1.0f / static_cast<float32>( shape._listPoint.size() ) );
                for ( const float3& point : shape._listPoint )
                {
                    renderer.drawLine( frame.apply( center ), frame.apply( point ), color );
                }
                break;
            }
            case PhysicsShapeType3D::TriangleMesh:
            {
                const size_t pointCount = shape._listPoint.size();
                for ( size_t index = 0; index + 2 < shape._listIndex.size(); index += 3 )
                {
                    const uint32 indexA = shape._listIndex[index];
                    const uint32 indexB = shape._listIndex[index + 1];
                    const uint32 indexC = shape._listIndex[index + 2];
                    if ( indexA >= pointCount || indexB >= pointCount || indexC >= pointCount )
                        continue;
                    const float3 pointA = frame.apply( shape._listPoint[indexA] );
                    const float3 pointB = frame.apply( shape._listPoint[indexB] );
                    const float3 pointC = frame.apply( shape._listPoint[indexC] );
                    renderer.drawLine( pointA, pointB, color );
                    renderer.drawLine( pointB, pointC, color );
                    renderer.drawLine( pointC, pointA, color );
                }
                break;
            }
        }
    }

    void PhysicsDebugDrawUtil::drawShape2D( IPhysicsDebugRenderer& renderer, const PhysicsShapeDesc2D& shape, const float2& bodyPosition, float32 bodyAngle,
                                            const float4& color )
    {
        // 2D 는 Z 축 둘레 회전 하나라 3D 자세로 바꿔 같은 도우미를 쓴다.
        PhysicsDebugDrawInternal::Frame3D frame;
        const quaternion                  bodyRotation = quaternion::makeFromAxisAngle( float3{ 0.0f, 0.0f, 1.0f }, bodyAngle );
        frame._rotation                                = bodyRotation * quaternion::makeFromAxisAngle( float3{ 0.0f, 0.0f, 1.0f }, shape._localAngle );
        frame._position                                = float3{ bodyPosition._x, bodyPosition._y, 0.0f } + float3::transform( float3{ shape._localPosition._x, shape._localPosition._y, 0.0f }, bodyRotation );
        const float3 axisX{ 1.0f, 0.0f, 0.0f };
        const float3 axisY{ 0.0f, 1.0f, 0.0f };
        switch ( shape._type )
        {
            case PhysicsShapeType2D::Box:
            {
                PhysicsDebugDrawInternal::drawBox( renderer, frame, float3{ shape._halfExtents._x, shape._halfExtents._y, 0.0f }, color );
                break;
            }
            case PhysicsShapeType2D::Circle:
            {
                PhysicsDebugDrawInternal::drawCircle( renderer, frame, float3{}, axisX, axisY, shape._radius, color );
                break;
            }
            case PhysicsShapeType2D::Capsule:
            {
                const float3 top{ 0.0f, shape._halfHeight, 0.0f };
                const float3 bottom{ 0.0f, -shape._halfHeight, 0.0f };
                PhysicsDebugDrawInternal::drawCircle( renderer, frame, top, axisX, axisY, shape._radius, color );
                PhysicsDebugDrawInternal::drawCircle( renderer, frame, bottom, axisX, axisY, shape._radius, color );
                renderer.drawLine( frame.apply( top + axisX * shape._radius ), frame.apply( bottom + axisX * shape._radius ), color );
                renderer.drawLine( frame.apply( top - axisX * shape._radius ), frame.apply( bottom - axisX * shape._radius ), color );
                break;
            }
            case PhysicsShapeType2D::Polygon:
            case PhysicsShapeType2D::Chain:
            {
                const size_t pointCount = shape._listPoint.size();
                const bool   bClosed    = shape._type == PhysicsShapeType2D::Polygon || shape._bLoop;
                for ( size_t index = 0; index + 1 < pointCount + ( bClosed ? 1 : 0 ); ++index )
                {
                    const float2& pointA = shape._listPoint[index];
                    const float2& pointB = shape._listPoint[( index + 1 ) % pointCount];
                    renderer.drawLine( frame.apply( float3{ pointA._x, pointA._y, 0.0f } ), frame.apply( float3{ pointB._x, pointB._y, 0.0f } ), color );
                }
                break;
            }
        }
    }
} // namespace sw
