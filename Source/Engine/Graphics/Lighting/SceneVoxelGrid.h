/**
 * @file SceneVoxelGrid.h
 * @brief 장면의 불투명 정적 메시를 1 비트 점유 복셀로 옮기는 격자의 모양(시작점 · 칸 크기 · 칸 수)입니다.
 * @details 격자는 축 정렬 상자 하나(시작점, 정육면체 칸 크기, 축마다 칸 수)이고 칸 번호는 (z × Y + y) × X + x 입니다. 칸마다 1 비트(켜졌으면 면이 있다)이며 32 칸이
 *          `uint32` 한 단어에 담긴다(번호의 아래 5 비트가 단어 안 위치). 칸의 내용은 GPU 버퍼에만 있다 — 칠하기는 컴퓨트 `scene_voxelize.hlsl`, 읽기는 `probe_update.hlsl` 의
 *          `traceVoxelRay` 다. 두 셰이더의 식을 CPU 에서 보는 기준 구현은 테스트 헤더 `SceneVoxelReference.h` 다(식을 고치면 함께 고친다).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/AABB.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 점유 복셀 격자의 모양입니다. 파일 형식은 없다(씬의 메시에서 그때그때 만든다). */
    struct SW_API SceneVoxelGrid
    {
        /// @brief 축마다 칸 수의 상한입니다. 상자가 크면 칸 크기를 키워서 맞춘다(`make`) — 비트 격자는 최대 128³ = 256 KiB 다.
        static constexpr uint32 kMaxCellsPerAxis = 128;
        /// @brief 점유 단어 하나가 담는 칸 수입니다(scene_voxelize.hlsl · probe_update.hlsl 의 `>> 5`).
        static constexpr uint32 kCellsPerWord = 32;

        float3  _origin{};         ///< 가장 낮은 모서리의 월드 위치
        float32 _cellSize{ 1.0f }; ///< 칸 한 변의 길이(m)
        uint32  _dimX{ 0 };
        uint32  _dimY{ 0 };
        uint32  _dimZ{ 0 };

        /** @brief 격자 칸의 직육면체 구간입니다(축 x · y · z 마다 처음 · 끝 칸, 끝 포함). */
        struct CellRange
        {
            uint32 _arrFirst[3]{};
            uint32 _arrLast[3]{};

            /** @brief 구간의 칸 수입니다. */
            uint32 getCellCount() const
            {
                return ( _arrLast[0] - _arrFirst[0] + 1u ) * ( _arrLast[1] - _arrFirst[1] + 1u ) * ( _arrLast[2] - _arrFirst[2] + 1u );
            }
        };

        /**
         * @brief 상자 @p bounds 를 덮는 격자를 만듭니다. 칸 크기는 @p targetCellSize 이되, 축 하나라도 칸 수가 `kMaxCellsPerAxis` 를 넘으면 가장 긴 축이 맞도록 키웁니다.
         * @details 상자가 유효하지 않거나 칸 크기가 0 이하이면 빈(칸 0 개) 격자를 돌려준다.
         */
        [[nodiscard]] static SceneVoxelGrid make( const AABB& bounds, float32 targetCellSize );

        /** @brief 격자 전체 구간입니다. 빈 격자에는 부르지 않는다. */
        CellRange getFullRange() const;
        /** @brief 월드 상자 @p box 가 덮는 칸 구간을 @p outRange 에 적습니다. 상자가 격자 밖이거나 유효하지 않으면 false 입니다. */
        [[nodiscard]] bool findCellRange( const AABB& box, CellRange& outRange ) const;
        /** @brief 칸 구간 @p range 가 덮는 월드 상자입니다. */
        AABB computeRangeBounds( const CellRange& range ) const;
        /** @brief 칸이 하나도 없으면 true 입니다. */
        bool isEmpty() const { return _dimX == 0 || _dimY == 0 || _dimZ == 0; }
        /** @brief 칸 수입니다. */
        uint32 getCellCount() const { return _dimX * _dimY * _dimZ; }
        /** @brief 점유 비트가 차지하는 단어 수입니다. */
        uint32 getWordCount() const { return ( getCellCount() + kCellsPerWord - 1u ) / kCellsPerWord; }
        /** @brief 격자가 덮는 상자입니다. */
        AABB getBounds() const;
        /** @brief 칸 (x, y, z) 의 번호입니다. */
        uint32 computeCellIndex( uint32 cellX, uint32 cellY, uint32 cellZ ) const { return ( cellZ * _dimY + cellY ) * _dimX + cellX; }
        /** @brief 칸 (x, y, z) 가 격자 안인지입니다. */
        bool containsCell( uint32 cellX, uint32 cellY, uint32 cellZ ) const { return cellX < _dimX && cellY < _dimY && cellZ < _dimZ; }
        /** @brief 모양(시작점 · 칸 크기 · 칸 수)이 같으면 true 입니다. */
        bool hasSameShape( const SceneVoxelGrid& other ) const
        {
            return _origin == other._origin && MathUtil::nearEqual( _cellSize, other._cellSize ) && _dimX == other._dimX && _dimY == other._dimY && _dimZ == other._dimZ;
        }
    };
} // namespace sw
