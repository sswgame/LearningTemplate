/**
 * @file DestructionTestUtil.h
 * @brief 파괴 시험이 함께 쓰는 형상 — 닫힌 상자 · L 자 기둥(오목) 삼각형 목록입니다(엔진 앞면 규약: (b-a)×(c-a) 가 바깥).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Destruction/PolygonTriangulation.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace test
{
    struct DestructionTestUtil
    {
        static sw::RHIVertex makeVertex( const sw::float3& position, const sw::float3& normal )
        {
            sw::RHIVertex vertex{};
            vertex._arrPosition[0] = position._x;
            vertex._arrPosition[1] = position._y;
            vertex._arrPosition[2] = position._z;
            vertex._arrNormal[0]   = normal._x;
            vertex._arrNormal[1]   = normal._y;
            vertex._arrNormal[2]   = normal._z;
            vertex._arrColor[0]    = 1.0f;
            vertex._arrColor[1]    = 1.0f;
            vertex._arrColor[2]    = 1.0f;
            vertex._arrColor[3]    = 1.0f;
            return vertex;
        }

        static void addTriangle( sw::vector<sw::RHIVertex>& inoutList, const sw::float3& a, const sw::float3& b, const sw::float3& c )
        {
            const sw::float3 normal = ( b - a ).cross( c - a ).normalize();
            inoutList.push_back( makeVertex( a, normal ) );
            inoutList.push_back( makeVertex( b, normal ) );
            inoutList.push_back( makeVertex( c, normal ) );
        }

        /** @brief 사각형 a-b-c-d(바깥에서 보아 반시계)를 두 삼각형으로 더합니다. */
        static void addQuad( sw::vector<sw::RHIVertex>& inoutList, const sw::float3& a, const sw::float3& b, const sw::float3& c, const sw::float3& d )
        {
            addTriangle( inoutList, a, b, c );
            addTriangle( inoutList, a, c, d );
        }

        /** @brief 중심 @p center, 반 크기 @p half 의 닫힌 상자입니다. */
        static sw::vector<sw::RHIVertex> makeBox( const sw::float3& half, const sw::float3& center = sw::float3{} )
        {
            const sw::float3          lo = center - half;
            const sw::float3          hi = center + half;
            sw::vector<sw::RHIVertex> list;
            addQuad( list, sw::float3{ hi._x, lo._y, lo._z }, sw::float3{ hi._x, hi._y, lo._z }, sw::float3{ hi._x, hi._y, hi._z }, sw::float3{ hi._x, lo._y, hi._z } ); // +X
            addQuad( list, sw::float3{ lo._x, lo._y, hi._z }, sw::float3{ lo._x, hi._y, hi._z }, sw::float3{ lo._x, hi._y, lo._z }, sw::float3{ lo._x, lo._y, lo._z } ); // -X
            addQuad( list, sw::float3{ lo._x, hi._y, lo._z }, sw::float3{ lo._x, hi._y, hi._z }, sw::float3{ hi._x, hi._y, hi._z }, sw::float3{ hi._x, hi._y, lo._z } ); // +Y
            addQuad( list, sw::float3{ lo._x, lo._y, hi._z }, sw::float3{ lo._x, lo._y, lo._z }, sw::float3{ hi._x, lo._y, lo._z }, sw::float3{ hi._x, lo._y, hi._z } ); // -Y
            addQuad( list, sw::float3{ lo._x, lo._y, hi._z }, sw::float3{ hi._x, lo._y, hi._z }, sw::float3{ hi._x, hi._y, hi._z }, sw::float3{ lo._x, hi._y, hi._z } ); // +Z
            addQuad( list, sw::float3{ hi._x, lo._y, lo._z }, sw::float3{ lo._x, lo._y, lo._z }, sw::float3{ lo._x, hi._y, lo._z }, sw::float3{ hi._x, hi._y, lo._z } ); // -Z
            return list;
        }

        /**
         * @brief XY 평면의 L 자(2 × 2 에서 오른쪽 위 1 × 1 을 뺀 것)를 Z 로 [0, @p depth] 만큼 밀어낸 닫힌 오목 기둥입니다. 부피 3 × depth.
         */
        static sw::vector<sw::RHIVertex> makeLPrism( float32 depth )
        {
            const sw::vector<sw::float2> listBorder = {
                sw::float2{0.0f, 0.0f},
                sw::float2{2.0f, 0.0f},
                sw::float2{2.0f, 1.0f},
                sw::float2{1.0f, 1.0f},
                sw::float2{1.0f, 2.0f},
                sw::float2{0.0f, 2.0f}
            };
            sw::vector<sw::RHIVertex>      list;
            sw::vector<sw::vector<uint32>> listLoop{
                sw::vector<uint32>{ 0, 1, 2, 3, 4, 5 }
            };
            sw::vector<uint32> listIndex;
            (void)sw::PolygonTriangulationUtil::triangulate( listBorder, listLoop, listIndex );
            for ( size_t index = 0; index + 2 < listIndex.size(); index += 3 )
            {
                const sw::float2& a = listBorder[listIndex[index]];
                const sw::float2& b = listBorder[listIndex[index + 1]];
                const sw::float2& c = listBorder[listIndex[index + 2]];
                // 앞(+Z)은 반시계 그대로, 뒤(Z = 0)는 뒤집어 -Z 를 향하게.
                addTriangle( list, sw::float3{ a, depth }, sw::float3{ b, depth }, sw::float3{ c, depth } );
                addTriangle( list, sw::float3{ a, 0.0f }, sw::float3{ c, 0.0f }, sw::float3{ b, 0.0f } );
            }
            for ( size_t index = 0; index < listBorder.size(); ++index )
            {
                const sw::float2& from = listBorder[index];
                const sw::float2& to   = listBorder[( index + 1 ) % listBorder.size()];
                addQuad( list, sw::float3{ from, 0.0f }, sw::float3{ to, 0.0f }, sw::float3{ to, depth }, sw::float3{ from, depth } );
            }
            return list;
        }
    };
} // namespace test
