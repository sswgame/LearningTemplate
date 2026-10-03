/**
 * @file AreaGraph.h
 * @brief 방 · 지역 그래프 — 방문 · 발견(지도 공개), 조건으로 잠긴 문, 길 찾기, 탐색률(메트로배니아 %), 지금 막힌 문(다음 목표 힌트)입니다.
 * @details 메트로배니아의 지도, 젤다 던전의 열쇠 문, 루이지 맨션 · 생존 공포의 저택 방, 리썰 컴퍼니 시설의 구역이 같은 구조입니다.
 *          잠금 조건은 `GameFlags` 조건식이라 열쇠 · 능력 · 퀘스트 진행을 같은 방식으로 적습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameFlags;
    class XmlNode;

    /** @brief 방 하나입니다. 위치 · 크기는 지도 그리기용(그래프 계산은 쓰지 않는다)입니다. */
    struct AreaDef
    {
        hashed_string _id{};
        hashed_string _name{};
        hashed_string _region{}; ///< 지역(층 · 구역) — 지역별 완료율의 단위
        float32       _x{ 0.0f };
        float32       _y{ 0.0f };
        float32       _width{ 1.0f };
        float32       _height{ 1.0f };
    };

    /** @brief 방 사이 연결(문 · 통로 · 일방통행 낭떠러지)입니다. */
    struct AreaLink
    {
        hashed_string _from{};
        hashed_string _to{};
        hashed_string _kind{};     ///< `Door` · `Passage` · `Vent` … 게임이 정한다
        string        _requires{}; ///< `GameFlags` 조건식 — 비면 늘 열려 있다
        int32         _fromIndex{ -1 };
        int32         _toIndex{ -1 };
        uint8         _bOneWay{ SW_FALSE };           ///< `_from` → `_to` 로만 지난다
        uint8         _bInvalidCondition{ SW_FALSE }; ///< 조건식이 문법에 맞지 않았다 — 늘 잠김(읽을 때 한 번만 경고)
    };

    /** @brief 지역 하나의 탐색 현황입니다. */
    struct AreaRegionProgress
    {
        hashed_string _region{};
        int32         _visitedCount{ 0 };
        int32         _totalCount{ 0 };

        float32 computeRatio() const { return _totalCount > 0 ? static_cast<float32>( _visitedCount ) / static_cast<float32>( _totalCount ) : 0.0f; }
    };

    /**
     * @class AreaGraph
     * @brief `<AreaGraph><Area id="hall" name="Hall" x="0" y="0" w="10" h="8" region="Mansion1F"/>
     *        <Link from="hall" to="kitchen" requires="hasKey_red" oneWay="false" kind="Door"/></AreaGraph>` 를 읽고 탐색 상태를 들고 있습니다.
     * @details 방에 들어가면 방문 · 발견이 되고, 그 방에서 나가는 연결의 반대편 방도 발견됩니다(문 너머가 지도에 보인다 — 잠겨 있어도).
     *          길 찾기는 너비 우선이라 지나는 문 수가 가장 적은 길이며, 같은 길이면 연결을 읽은 순서로 정해져 늘 같은 답입니다.
     */
    class SW_GF_API AreaGraph
    {
    public:
        AreaGraph();

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );

        /** @brief 방문 · 발견을 모두 지웁니다(새 게임). */
        void resetState();
        /** @brief 방과 연결을 모두 지웁니다(코드로 그래프를 지을 때 처음에). */
        void clear();
        /**
         * @brief 방을 더합니다(절차 생성 · 다른 카탈로그 안에 적은 방). 같은 id 가 있으면 false 입니다. 탐색 상태는 새 방만 비어 있게 늘립니다.
         */
        [[nodiscard]] bool addArea( const AreaDef& area );
        /**
         * @brief 두 방을 잇습니다. 모르는 방 · 같은 방이면 false, 조건식이 문법에 맞지 않으면 늘 잠긴 채로 더하고 true 입니다(@p outbInvalidCondition 로 알린다).
         * @param condition `GameFlags` 조건식 — 비면 늘 열려 있다
         */
        [[nodiscard]] bool addLink( const hashed_string& from, const hashed_string& to, const hashed_string& kind, string_view condition, bool bOneWay,
                                    bool& outbInvalidCondition );
        /**
         * @brief `<Area>` · `<Link>` 를 가진 노드를 읽어 **더합니다**(지우지 않는다). 다른 키트의 XML 안에 그래프를 함께 적을 때 씁니다. 더한 방 수입니다.
         */
        uint32 loadFromNode( const XmlNode& node, string_view sourceName );
        /** @brief 방에 들어갑니다 — 방문 · 발견, 이웃 발견. 처음 방문이면 true 입니다(없는 방은 false). */
        bool enterArea( const hashed_string& areaId );
        /** @brief 방을 지도에 드러냅니다(지도 아이템 · 힌트). 새로 드러났으면 true 입니다. */
        bool discoverArea( const hashed_string& areaId );
        /** @brief 지역의 방을 모두 드러냅니다(층 지도). 새로 드러난 수입니다. */
        int32 discoverRegion( const hashed_string& region );

        /** @brief @p fromId 에서 @p toId 로 바로 이어진 연결을 지금 지날 수 있는가입니다(방향 · 잠금). */
        bool canTraverse( const hashed_string& fromId, const hashed_string& toId, const GameFlags& flags ) const;
        /**
         * @brief 지금 지날 수 있는 연결만으로 가는 가장 짧은 길(지나는 연결 수)을 찾습니다.
         * @param outListArea 출발과 도착을 포함한 방 순서. 길이 없으면 비운다.
         */
        [[nodiscard]] bool findPath( const hashed_string& fromId, const hashed_string& toId, const GameFlags& flags, vector<hashed_string>& outListArea ) const;
        /** @brief 방문한 방 / 모든 방(0..1)입니다. */
        float32 computeExplorationRatio() const;
        /** @brief 지역의 방문 비율(0..1)입니다. 없는 지역은 0 입니다. */
        float32 computeRegionRatio( const hashed_string& region ) const;
        /** @brief 지역마다의 현황입니다(지역이 처음 나온 순서). */
        void collectRegionProgress( vector<AreaRegionProgress>& outListProgress ) const;
        /**
         * @brief 지금 막힌 문 — 방문한 방에서 아직 방문하지 않은 방으로 가는데 조건 때문에 지날 수 없는 연결입니다(다음 목표 힌트).
         * @details 포인터는 다음 읽기까지 유효합니다.
         */
        void collectLockedFrontier( const GameFlags& flags, vector<const AreaLink*>& outListLink ) const;

        /** @brief 세이브용 — 방문한 방 id(이름 순)입니다. */
        void fillVisitedAreas( vector<hashed_string>& outListArea ) const;
        /** @brief 세이브용 — 발견한 방 id(이름 순)입니다. */
        void fillDiscoveredAreas( vector<hashed_string>& outListArea ) const;
        /** @brief 세이브에서 되살립니다. 방문한 방은 발견도 됩니다. 모르는 id 는 건너뜁니다. */
        void restoreState( const vector<hashed_string>& listVisited, const vector<hashed_string>& listDiscovered );

        bool                    isVisited( const hashed_string& areaId ) const;
        bool                    isDiscovered( const hashed_string& areaId ) const;
        const AreaDef*          findArea( const hashed_string& areaId ) const { return _catalog.find( areaId ); }
        const vector<AreaDef>&  getAreas() const { return _catalog.getAll(); }
        const vector<AreaLink>& getLinks() const { return _listLink; }
        int32                   getVisitedCount() const { return _visitedCount; }

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName );
        /** @brief 연결을 @p fromIndex 쪽에서 지나면 닿는 방입니다. 그 방향으로 지날 수 없으면 −1 입니다(잠금은 보지 않는다). */
        int32 findOtherSide( const AreaLink& link, int32 fromIndex ) const;
        bool  isUnlocked( const AreaLink& link, const GameFlags& flags ) const;
        void  markVisited( int32 areaIndex );
        void  fillMarkedAreas( const vector<uint8>& listMark, vector<hashed_string>& outListArea ) const;

        GameCatalog<AreaDef>  _catalog;
        vector<AreaLink>      _listLink;
        vector<vector<int32>> _listAdjacency;  ///< 방마다 닿은 연결 자리(읽은 순서)
        vector<uint8>         _listVisited;    ///< 방마다 SW_TRUE/SW_FALSE
        vector<uint8>         _listDiscovered; ///< 방마다
        int32                 _visitedCount;
    };
} // namespace sw
