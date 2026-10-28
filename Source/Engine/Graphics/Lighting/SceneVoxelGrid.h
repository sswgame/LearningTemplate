/**
 * @file SceneVoxelGrid.h
 * @brief 장면의 불투명 정적 메시를 1 비트 점유 복셀로 옮긴 격자입니다.
 * @details 격자는 축 정렬 상자 하나(시작점, 정육면체 칸 크기, 축마다 칸 수)이고 칸 번호는 (z × Y + y) × X + x 입니다. 칸마다 1 비트(켜졌으면 면이 있다)이며 32 칸이
 *          `uint32` 한 단어에 담긴다(번호의 아래 5 비트가 단어 안 위치) — GPU 의 읽기 전용 구조 버퍼(`probeupdate.hlsl`)가 같은 배치를 읽는다.
 *          삼각형은 칸과 겹치는지(삼각형-상자 분리축 판정, Akenine-Möller 2001)로 켠다 — 얇은 면도 빠짐없이 켜는 보수적 복셀화다.
 *          광선은 칸을 따라 걷는다(Amanatides-Woo DDA). 셰이더 `probeupdate.hlsl` 의 `traceVoxelRay` 가 같은 식이다 — 식을 고치면 두 곳을 함께 고친다.
 *          메시 컴포넌트를 모으는 일은 렌더러의 `SceneVoxelizer` 가 하고, 이 형식은 컴포넌트를 모른다(그래서 컴포넌트가 이 형식을 값으로 들 수 있다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/AABB.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 복셀 광선이 처음 닿은 면입니다. */
    struct SceneVoxelHit
    {
        float32 _distance{ 0.0f };           ///< 광선 시작점에서 닿은 곳까지의 거리
        float3  _normal{ 0.0f, 1.0f, 0.0f }; ///< 닿은 칸의 면 노멀(들어온 면의 바깥 방향 — 축 정렬)
    };
} // namespace sw

namespace sw
{
    /** @brief 점유 복셀 격자입니다. 파일 형식은 없다(씬의 메시에서 그때그때 만든다). */
    struct SW_API SceneVoxelGrid
    {
        /// @brief 축마다 칸 수의 상한입니다. 상자가 크면 칸 크기를 키워서 맞춘다(`make`) — 비트 격자는 최대 128³ = 256 KiB 다.
        static constexpr uint32 kMaxCellsPerAxis = 128;

        float3         _origin{};         ///< 가장 낮은 모서리의 월드 위치
        float32        _cellSize{ 1.0f }; ///< 칸 한 변의 길이(m)
        uint32         _dimX{ 0 };
        uint32         _dimY{ 0 };
        uint32         _dimZ{ 0 };
        vector<uint32> _listWord; ///< 점유 비트(칸 번호 / 32 번째 단어의 칸 번호 % 32 번째 비트)

        /**
         * @brief 상자 @p bounds 를 덮는 빈 격자를 만듭니다. 칸 크기는 @p targetCellSize 이되, 축 하나라도 칸 수가 `kMaxCellsPerAxis` 를 넘으면 가장 긴 축이 맞도록 키웁니다.
         * @details 상자가 유효하지 않거나 칸 크기가 0 이하이면 빈(칸 0 개) 격자를 돌려준다.
         */
        [[nodiscard]] static SceneVoxelGrid make( const AABB& bounds, float32 targetCellSize );

        /** @brief 칸이 하나도 없으면 true 입니다. */
        bool isEmpty() const { return _dimX == 0 || _dimY == 0 || _dimZ == 0; }
        /** @brief 칸 수입니다. */
        uint32 getCellCount() const { return _dimX * _dimY * _dimZ; }
        /** @brief 격자가 덮는 상자입니다. */
        AABB getBounds() const;
        /** @brief 칸 (x, y, z) 의 번호입니다. */
        uint32 computeCellIndex( uint32 cellX, uint32 cellY, uint32 cellZ ) const { return ( cellZ * _dimY + cellY ) * _dimX + cellX; }
        /** @brief 칸 (x, y, z) 가 격자 안인지입니다. */
        bool containsCell( uint32 cellX, uint32 cellY, uint32 cellZ ) const { return cellX < _dimX && cellY < _dimY && cellZ < _dimZ; }

        /** @brief 칸 하나를 켭니다. 범위 밖이면 아무것도 하지 않습니다. */
        void setOccupied( uint32 cellX, uint32 cellY, uint32 cellZ );
        /** @brief 칸이 켜졌는지입니다. 범위 밖이면 false 입니다. */
        bool isOccupied( uint32 cellX, uint32 cellY, uint32 cellZ ) const;
        /** @brief 켜진 칸 수입니다. */
        uint32 countOccupied() const;

        /** @brief 삼각형 (@p a, @p b, @p c)(월드 좌표)와 겹치는 모든 칸을 켭니다. 격자 밖의 부분은 무시합니다. */
        void voxelizeTriangle( const float3& a, const float3& b, const float3& c );

        /**
         * @brief 광선이 처음 만나는 켜진 칸을 찾습니다.
         * @param direction 단위 벡터
         * @param maxDistance 이 거리까지만 봅니다
         * @return 만나면 true 이고 @p outHit 에 거리와 면 노멀을 적습니다. 광선이 켜진 칸 안에서 시작하면 거리 0 으로 닿는다.
         */
        [[nodiscard]] bool traceRay( const float3& origin, const float3& direction, float32 maxDistance, SceneVoxelHit& outHit ) const;
    };
} // namespace sw
