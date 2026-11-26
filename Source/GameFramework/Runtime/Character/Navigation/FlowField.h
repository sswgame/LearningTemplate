/**
 * @file FlowField.h
 * @brief 흐름장입니다. 목적지에서 거꾸로 퍼진 거리장과, 셀마다 "어느 쪽으로 가면 가까워지는가" 를 담습니다. 한 번 구하면 몇 마리가 써도 같은 값입니다. 목적지마다 흐름장 하나를 나눠 쓰는 캐시(`FlowFieldCache`)도 둡니다.
 */
#pragma once
#include "Core/Container/ChunkedArray.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Grid/GridTopology.h"

namespace sw
{
    class NavGrid;

    /**
     * @class FlowField
     * @brief 목적지 셀들(하나 이상)에서 다익스트라로 거리를 펼치고, 셀마다 거리가 가장 작은 이웃 방향을 적습니다.
     * @details 유닛 백 마리를 한 곳으로 보낼 때 A* 백 번 대신 흐름장 하나를 씁니다(Supreme Commander · StarCraft II 류 군집 이동). 막힌 목적지는 빼고,
     *          모두 막혔으면 아무 셀도 닿지 못합니다. 격자 리비전을 기억하므로 `isStale` 로 다시 구할지 압니다.
     */
    class SW_GF_API FlowField
    {
    public:
        /** @brief 방향이 없는 셀(목적지 · 닿지 못함)입니다. */
        static constexpr int8 kNoDirection = constant::invalid_index::kInt8;

        FlowField();

        /** @brief 거리장과 방향장을 구합니다. 닿은 셀 수를 돌려줍니다. */
        int32 compute( const NavGrid& grid, const vector<int2>& listGoal );
        /** @brief 목적지 하나로 구합니다. 막혀 있으면 가장 가까운 걸을 수 있는 셀로 옮겨 구합니다. */
        int32 computeToCell( const NavGrid& grid, const int2& goal );

        bool isReachable( const int2& cell ) const;
        /** @brief 목적지까지의 거리(셀 값 가중)입니다. 닿지 못하면 음수입니다. */
        float32 getDistance( const int2& cell ) const;
        /** @brief 셀의 방향 번호(0..7, `kNoDirection`)입니다. */
        int8 getDirectionIndex( const int2& cell ) const;
        /** @brief 셀의 다음 셀입니다. 방향이 없으면 그 셀 그대로입니다. */
        int2 getNextCell( const int2& cell ) const;
        /** @brief 월드 위치의 이동 방향(XZ 단위 벡터)입니다. 목적지이거나 닿지 못하거나 격자 밖이면 0 입니다. */
        float3 sampleDirection( const NavGrid& grid, const float3& worldPosition ) const;
        /** @brief 구한 뒤 격자가 바뀌었거나 다른 격자면 true 입니다. */
        bool isStale( const NavGrid& grid ) const;

        int32 getWidth() const { return _topology._width; }
        int32 getHeight() const { return _topology._height; }

    private:
        using OpenEntry = GridOpenEntry<float32>;

        vector<float32>   _listDistance;
        vector<int8>      _listDirection;
        vector<OpenEntry> _listOpen;
        GridTopology      _topology; ///< 구한 격자의 너비, 높이, 셀 번호
        const NavGrid*    _pGrid;    ///< 구한 격자. 리비전만으로는 다른 격자를 구별하지 못한다
        uint32            _gridRevision;
    };

    class NavGrid;

    /**
     * @class FlowFieldCache
     * @brief 목적지 셀을 키로 흐름장을 빌려 주고(`acquire`) 쓰는 수를 셉니다. 아무도 쓰지 않는 슬롯은 다음 목적지가 다시 씁니다.
     * @details 슬롯은 ChunkedArray 라서 움직이지 않는다 — 빌린 쪽(명령 · 행위자)이 포인터를 쥐어도 된다. 격자가 바뀌면 `refreshStale` 이 쓰는 흐름장만 다시 구한다.
     *          RTS 월드에서 떼었다 — 무리 이동을 쓰는 다른 키트(타워 디펜스 · 군중)도 같은 캐시를 쓴다(Supreme Commander · SC2 의 목적지별 흐름장 공유).
     */
    class SW_GF_API FlowFieldCache
    {
    public:
        /** @brief @p goal 로 가는 흐름장을 빌립니다. 같은 목적지를 쓰는 흐름장이 있으면 그것을, 없으면 빈 슬롯에 새로 구합니다. */
        const FlowField* acquire( const NavGrid& grid, const int2& goal );
        /** @brief 빌린 흐름장을 돌려줍니다. 이 캐시의 것이 아니거나 이미 다 돌려받았으면 아무것도 하지 않습니다. */
        void release( const FlowField* pField );
        /** @brief 누군가 쓰는 흐름장 가운데 @p grid 가 바뀌어 낡은 것을 다시 구합니다. */
        void refreshStale( const NavGrid& grid );
        /** @brief 슬롯을 모두 버립니다. 빌려 간 포인터는 모두 무효가 됩니다. */
        void clear() { _listSlot.clear(); }

    private:
        static constexpr int2 kNoGoal{ -1, -1 }; ///< 아직 아무 목적지도 구하지 않은 슬롯

        struct Slot
        {
            FlowField _field{};
            int2      _goal{ kNoGoal };
            int32     _userCount{ 0 };
        };

        ChunkedArray<Slot> _listSlot;
    };
} // namespace sw
