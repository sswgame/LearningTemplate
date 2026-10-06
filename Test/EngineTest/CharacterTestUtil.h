/**
 * @file CharacterTestUtil.h
 * @brief 캐릭터 형상 시험의 합성 형상 — Y 축 원기둥(소매 · 팔 · 팔찌) · 상자 · 본 사슬입니다.
 */
#pragma once
#include "Core/Math/MathUtil.h"

#include "Engine/Character/Fit/CharacterGeometry.h"

namespace test
{
    /** @brief 합성 형상을 만드는 도우미입니다. 삼각형은 모두 바깥(축 · 중심에서 먼 쪽)을 보게 감습니다. */
    struct CharacterTestUtil
    {
        /** @brief 원기둥 칸 하나의 설정입니다. */
        struct CylinderDesc
        {
            float32 _radius{ 0.05f };
            float32 _yMin{ 0.0f };
            float32 _yMax{ 1.0f };
            uint32  _segmentCount{ 24 };
            uint32  _ringCount{ 50 };
            bool    _bCapped{ false };
            bool    _bInwardNormals{ false }; ///< 두꺼운 고리의 안벽처럼 축을 보게
        };

        /** @brief 삼각형 하나를 @p outward 쪽을 보게 붙인다. */
        static void addOrientedTriangle( sw::AppearanceGeometry& inoutGeometry, uint32 indexA, uint32 indexB, uint32 indexC, const sw::float3& outward )
        {
            const sw::float3& a      = inoutGeometry._listPosition[indexA];
            const sw::float3  normal = ( inoutGeometry._listPosition[indexB] - a ).cross( inoutGeometry._listPosition[indexC] - a );
            inoutGeometry._listIndex.push_back( indexA );
            if ( normal.dot( outward ) >= 0.0f )
            {
                inoutGeometry._listIndex.push_back( indexB );
                inoutGeometry._listIndex.push_back( indexC );
            }
            else
            {
                inoutGeometry._listIndex.push_back( indexC );
                inoutGeometry._listIndex.push_back( indexB );
            }
        }

        /** @brief Y 축 원기둥입니다(이음매 열은 같은 자리의 정점 둘 — UV 가 0 과 1). 캡이면 위 · 아래를 중심 부채꼴로 막습니다. */
        static sw::AppearanceGeometry makeCylinder( const CylinderDesc& desc )
        {
            sw::AppearanceGeometry geometry;
            const uint32           columnCount = desc._segmentCount + 1;
            for ( uint32 ring = 0; ring <= desc._ringCount; ++ring )
            {
                const float32 v = static_cast<float32>( ring ) / static_cast<float32>( desc._ringCount );
                const float32 y = sw::MathUtil::lerp( desc._yMin, desc._yMax, v );
                for ( uint32 column = 0; column < columnCount; ++column )
                {
                    const float32 u     = static_cast<float32>( column ) / static_cast<float32>( desc._segmentCount );
                    const float32 angle = u * sw::MathUtil::kPi * 2.0f;
                    const float32 x     = column == desc._segmentCount ? desc._radius : desc._radius * sw::MathUtil::cos( angle );
                    const float32 z     = column == desc._segmentCount ? 0.0f : desc._radius * sw::MathUtil::sin( angle );
                    geometry._listPosition.push_back( sw::float3( x, y, z ) );
                    geometry._listUv.push_back( sw::float2( u, v ) );
                }
            }
            for ( uint32 ring = 0; ring < desc._ringCount; ++ring )
            {
                for ( uint32 column = 0; column < desc._segmentCount; ++column )
                {
                    const uint32     i00     = ring * columnCount + column;
                    const uint32     i01     = i00 + 1;
                    const uint32     i10     = i00 + columnCount;
                    const uint32     i11     = i10 + 1;
                    const sw::float3 center  = ( geometry._listPosition[i00] + geometry._listPosition[i11] ) * 0.5f;
                    sw::float3       outward = sw::float3( center._x, 0.0f, center._z );
                    if ( desc._bInwardNormals )
                        outward = -outward;
                    addOrientedTriangle( geometry, i00, i01, i11, outward );
                    addOrientedTriangle( geometry, i00, i11, i10, outward );
                }
            }
            if ( desc._bCapped )
            {
                for ( uint32 side = 0; side < 2; ++side )
                {
                    const float32 y           = side == 0 ? desc._yMin : desc._yMax;
                    const uint32  ringStart   = side == 0 ? 0 : desc._ringCount * columnCount;
                    const uint32  centerIndex = geometry.getVertexCount();
                    geometry._listPosition.push_back( sw::float3( 0.0f, y, 0.0f ) );
                    geometry._listUv.push_back( sw::float2( 0.5f, side == 0 ? 0.0f : 1.0f ) );
                    const sw::float3 outward = side == 0 ? sw::float3( 0.0f, -1.0f, 0.0f ) : sw::float3( 0.0f, 1.0f, 0.0f );
                    for ( uint32 column = 0; column < desc._segmentCount; ++column )
                    {
                        addOrientedTriangle( geometry, centerIndex, ringStart + column, ringStart + column + 1, outward );
                    }
                }
            }
            geometry.computeVertexNormals();
            return geometry;
        }

        /** @brief 축 정렬 닫힌 상자입니다(면마다 네 정점). */
        static sw::AppearanceGeometry makeBox( const sw::float3& center, const sw::float3& halfExtent )
        {
            sw::AppearanceGeometry geometry;
            const sw::float3       arrAxis[3] = { sw::float3::UnitX, sw::float3::UnitY, sw::float3::UnitZ };
            for ( uint32 axis = 0; axis < 3; ++axis )
            {
                for ( int32 sign = -1; sign <= 1; sign += 2 )
                {
                    const sw::float3 normal  = arrAxis[axis] * static_cast<float32>( sign );
                    const sw::float3 tangent = arrAxis[( axis + 1 ) % 3];
                    const sw::float3 other   = arrAxis[( axis + 2 ) % 3];
                    const uint32     base    = geometry.getVertexCount();
                    for ( uint32 corner = 0; corner < 4; ++corner )
                    {
                        const float32    tangentSign = ( corner == 1 || corner == 2 ) ? 1.0f : -1.0f;
                        const float32    otherSign   = ( corner >= 2 ) ? 1.0f : -1.0f;
                        const sw::float3 local( normal._x * halfExtent._x + tangent._x * tangentSign * halfExtent._x + other._x * otherSign * halfExtent._x,
                                                normal._y * halfExtent._y + tangent._y * tangentSign * halfExtent._y + other._y * otherSign * halfExtent._y,
                                                normal._z * halfExtent._z + tangent._z * tangentSign * halfExtent._z + other._z * otherSign * halfExtent._z );
                        geometry._listPosition.push_back( center + local );
                        geometry._listUv.push_back( sw::float2( tangentSign * 0.5f + 0.5f, otherSign * 0.5f + 0.5f ) );
                    }
                    addOrientedTriangle( geometry, base, base + 1, base + 2, normal );
                    addOrientedTriangle( geometry, base, base + 2, base + 3, normal );
                }
            }
            geometry.computeVertexNormals();
            return geometry;
        }

        /** @brief 정점마다 본 하나에 가중치 1 로 묶습니다. */
        static void skinToBone( sw::AppearanceGeometry& inoutGeometry, uint16 bone )
        {
            sw::SkinInfluence influence;
            influence._arrJoint[0]  = bone;
            influence._arrWeight[0] = 1.0f;
            inoutGeometry._listSkin.assign( inoutGeometry.getVertexCount(), influence );
        }

        /** @brief y 가 @p splitY 이상인 정점은 @p upperBone, 아래는 @p lowerBone 에 묶습니다. */
        static void skinBySplit( sw::AppearanceGeometry& inoutGeometry, float32 splitY, uint16 lowerBone, uint16 upperBone )
        {
            inoutGeometry._listSkin.resize( inoutGeometry.getVertexCount() );
            for ( uint32 vertex = 0; vertex < inoutGeometry.getVertexCount(); ++vertex )
            {
                sw::SkinInfluence influence;
                influence._arrJoint[0]          = inoutGeometry._listPosition[vertex]._y >= splitY ? upperBone : lowerBone;
                influence._arrWeight[0]         = 1.0f;
                inoutGeometry._listSkin[vertex] = influence;
            }
        }

        /** @brief Y 를 따라 선 팔 사슬 — root(0) → upperarm(0.0) → lowerarm(0.5) → hand(1.0). */
        static sw::CharacterBoneArray makeArmBones()
        {
            sw::CharacterBoneArray bones;
            const int32            root     = bones.addBone( sw::hashed_string( "root" ), -1, sw::float4x4::Identity );
            const int32            upperarm = bones.addBone( sw::hashed_string( "upperarm" ), root, sw::float4x4::Identity );
            const int32            lowerarm = bones.addBone( sw::hashed_string( "lowerarm" ), upperarm, sw::float4x4::createTranslation( 0.0f, 0.5f, 0.0f ) );
            (void)bones.addBone( sw::hashed_string( "hand" ), lowerarm, sw::float4x4::createTranslation( 0.0f, 0.5f, 0.0f ) );
            return bones;
        }

        /** @brief 정점이 Y 축에서 떨어진 거리입니다. */
        static float32 computeRadius( const sw::float3& position ) { return sw::MathUtil::sqrt( position._x * position._x + position._z * position._z ); }
    };
} // namespace test
