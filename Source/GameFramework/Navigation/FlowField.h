/**
 * @file FlowField.h
 * @brief 흐름장 — 목적지에서 거꾸로 퍼진 거리장과 칸마다 "어느 쪽으로 가면 가까워지는가" 입니다. 한 번 구하면 몇 마리가 써도 같은 값입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class NavGrid;

    /**
     * @class FlowField
     * @brief 목적지 칸들(하나 이상)에서 다익스트라로 거리를 펼치고, 칸마다 거리가 가장 작은 이웃 방향을 적습니다.
     * @details 유닛 백 마리를 한 곳으로 보낼 때 A* 백 번 대신 흐름장 하나를 씁니다(Supreme Commander · StarCraft II 류 군집 이동). 막힌 목적지는 빼고,
     *          모두 막혔으면 아무 칸도 닿지 못합니다. 격자 리비전을 기억하므로 `isStale` 로 다시 구할지 압니다.
     */
    class SW_GF_API FlowField
    {
    public:
        /** @brief 방향이 없는 칸(목적지 · 닿지 못함)입니다. */
        static constexpr int8 kNoDirection = -1;

        FlowField();

        /** @brief 거리장과 방향장을 구합니다. 닿은 칸 수를 돌려줍니다. */
        int32 compute( const NavGrid& grid, const vector<int2>& listGoal );
        /** @brief 목적지 하나 — 막혀 있으면 가장 가까운 걸을 칸으로 옮겨 구합니다. */
        int32 computeToCell( const NavGrid& grid, const int2& goal );

        bool isReachable( const int2& cell ) const;
        /** @brief 목적지까지의 거리(칸 값 가중)입니다. 닿지 못하면 음수입니다. */
        float32 getDistance( const int2& cell ) const;
        /** @brief 칸의 방향 번호(0..7, `kNoDirection`)입니다. */
        int8 getDirectionIndex( const int2& cell ) const;
        /** @brief 칸의 다음 칸입니다. 방향이 없으면 그 칸 그대로입니다. */
        int2 getNextCell( const int2& cell ) const;
        /** @brief 월드 자리의 이동 방향(XZ 단위 벡터)입니다. 목적지 · 닿지 못함 · 밖이면 0 입니다. */
        float3 sampleDirection( const NavGrid& grid, const float3& worldPosition ) const;
        /** @brief 구한 뒤 격자가 바뀌었거나 다른 격자면 true 입니다. */
        bool isStale( const NavGrid& grid ) const;

        int32 getWidth() const { return _width; }
        int32 getHeight() const { return _height; }

    private:
        struct OpenEntry
        {
            float32 _distance;
            int32   _index;
        };

        vector<float32>   _listDistance;
        vector<int8>      _listDirection;
        vector<OpenEntry> _listOpen;
        int32             _width;
        int32             _height;
        const NavGrid*    _pGrid; ///< 구한 격자 — 리비전만으로는 다른 격자를 가리지 못한다
        uint32            _gridRevision;
    };
} // namespace sw
