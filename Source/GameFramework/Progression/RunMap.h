/**
 * @file RunMap.h
 * @brief 로그라이트 한 판의 갈림길 지도 — 층마다 칸(전투 · 정예 · 상점 · 휴식 · 사건 · 보물 · 보스), 엇갈리지 않는 길, 씨앗으로 같은 지도, 지금 자리와 갈 수 있는 칸입니다.
 * @details 슬레이 더 스파이어 류 지도입니다. 칸 종류는 데이터(이름 · 가중치 · 나올 수 있는 층 · 이어서 나오지 않기)라 장르마다 바꿉니다 — 메탈슬러그 택틱스의 작전 지도도 같은 모양입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameRandom;

    /** @brief 칸 종류의 규칙입니다. */
    struct RunNodeRule
    {
        hashed_string _kind{};
        float32       _weight{ 1.0f };
        int32         _minFloor{ 0 };
        int32         _maxFloor{ 1000 };
        uint8         _bNoRepeat{ SW_FALSE }; ///< 바로 앞 칸과 같은 종류가 되지 않는다(휴식 · 상점 연달아 금지)
    };
} // namespace sw

namespace sw
{
    /** @brief 지도 설정입니다. */
    struct RunMapSettings
    {
        vector<RunNodeRule>   _listRule{};
        vector<hashed_string> _listForcedFloor{}; ///< 층 번호 → 그 층 모두 이 종류(비면 규칙대로) — 첫 층 전투, 마지막 보스
        int32                 _floorCount{ 15 };
        int32                 _columnCount{ 7 };
        int32                 _pathCount{ 6 }; ///< 아래에서 위로 그을 길 수(길이 지나간 칸만 남는다)
    };
} // namespace sw

namespace sw
{
    /** @brief 칸 하나입니다. */
    struct RunNode
    {
        vector<int32> _listNext{}; ///< 위층에서 이어진 칸 번호
        hashed_string _kind{};
        int32         _floor{ 0 };
        int32         _column{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class RunMap
     * @brief 길은 아래층 칸에서 위층의 같은 · 옆 칸으로 그어지고, 이미 그은 길과 엇갈리면 다른 쪽으로 꺾습니다. 마지막 층은 한 칸(보스)으로 모입니다.
     */
    class SW_GF_API RunMap
    {
    public:
        static constexpr int32 kStart = -1; ///< 아직 첫 층을 고르지 않았다

        /** @brief `<RunMap floors="15" columns="7" paths="6"><Node kind="Battle" weight="5" minFloor="0"/>...<Floor index="0" kind="Battle"/></RunMap>` */
        [[nodiscard]] static bool loadSettings( string_view xmlText, string_view sourceName, RunMapSettings& outSettings );

        void generate( const RunMapSettings& settings, uint32 seed );
        /** @brief 지금 자리에서 갈 수 있는 칸입니다(시작이면 첫 층 모두). */
        void collectChoices( vector<int32>& outListNode ) const;
        /** @brief 칸으로 갑니다. 갈 수 없는 칸이면 false 입니다. */
        [[nodiscard]] bool moveTo( int32 nodeIndex );

        const vector<RunNode>& getNodes() const { return _listNode; }
        const RunNode*         findNode( int32 nodeIndex ) const;
        int32                  getCurrent() const { return _current; }
        int32                  getFloorCount() const { return _floorCount; }
        /** @brief 끝 칸(보스)에 닿았는가입니다. */
        bool isFinished() const;

    private:
        int32         findOrAddNode( int32 floor, int32 column );
        bool          crossesExisting( int32 floor, int32 fromColumn, int32 toColumn ) const;
        hashed_string pickKind( const RunMapSettings& settings, int32 floor, const hashed_string& previousKind, GameRandom& random ) const;

        vector<RunNode> _listNode;
        vector<int32>   _listCell; ///< 층 × 열 → 칸 번호(없으면 −1)
        int32           _floorCount{ 0 };
        int32           _columnCount{ 0 };
        int32           _current{ kStart };
    };
} // namespace sw
