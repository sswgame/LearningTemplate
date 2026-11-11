/**
 * @file AreaGraph.h
 * @brief 방과 지역의 그래프를 정의합니다.
 * @details 방문과 발견(지도 공개), 조건으로 잠긴 문, 길 찾기, 탐색률(메트로배니아의 %), 지금 막힌 문(다음 목표 힌트)을 다룬다.
 *          메트로배니아의 지도, 젤다 던전의 열쇠 문, 루이지 맨션과 생존 공포 게임의 저택 방, 리썰 컴퍼니 시설의 구역이 같은 구조이다.
 *          잠금 조건은 `GameFlags` 조건식이라서 열쇠, 능력, 퀘스트 진행을 같은 방식으로 적는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Behavior/ConditionExpr.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    class GameFlags;
    class XMLNode;

    /** @brief 방 하나입니다. 위치와 크기는 지도 그리기용이며 그래프 계산에는 쓰지 않습니다. */
    REFLECT()
    struct SW_GF_API AreaDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY()
        hashed_string _name{}; ///< 적지 않으면 id (`addArea`)
        PROPERTY()
        hashed_string _region{}; ///< 지역(층, 구역). 지역별 완료율의 단위다
        PROPERTY()
        float32 _x{ 0.0f };
        PROPERTY()
        float32 _y{ 0.0f };
        PROPERTY( Name = "w", Min = 0.0 )
        float32 _width{ 1.0f };
        PROPERTY( Name = "h", Min = 0.0 )
        float32 _height{ 1.0f };
    };

    /** @brief 방 사이 연결 한 줄(`<Link from to kind oneWay requires/>`)의 칸입니다. 조건(`requires`)은 조건 글이라 그래프가 푼다. */
    REFLECT()
    struct SW_GF_API AreaLinkParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _from{};
        PROPERTY( Required )
        hashed_string _to{};
        PROPERTY()
        hashed_string _kind{};
        PROPERTY()
        bool _bOneWay{ false }; ///< 적지 않으면 양방향이다
        PROPERTY()
        string _requires{};
    };
} // namespace sw

namespace sw
{
    /** @brief 방 사이의 연결입니다. 문, 통로, 일방통행 낭떠러지가 있습니다. */
    struct AreaLink
    {
        hashed_string _from{};
        hashed_string _to{};
        hashed_string _kind{};                        ///< `Door`, `Passage`, `Vent` … 게임이 정한다
        ConditionExpr _requires{};                    ///< 읽을 때 컴파일한 플래그 조건식. 비면 늘 열려 있다
        int32         _fromIndex{ 0 };                ///< `_from` 의 방 번호. 두 방이 모두 표에 있는 연결만 남아 없음이 없다(`addLink`)
        int32         _toIndex{ 0 };                  ///< `_to` 의 방 번호
        uint8         _bOneWay{ SW_FALSE };           ///< `_from` → `_to` 로만 지난다
        uint8         _bInvalidCondition{ SW_FALSE }; ///< 조건식이 문법에 맞지 않았다. 늘 잠기며 읽을 때 한 번만 경고한다
    };
} // namespace sw

namespace sw
{
    /** @brief 지역 하나의 탐색 현황입니다. */
    struct AreaRegionProgress
    {
        hashed_string _region{};
        int32         _visitedCount{ 0 };
        int32         _totalCount{ 0 };

        float32 computeRatio() const { return _totalCount > 0 ? static_cast<float32>( _visitedCount ) / static_cast<float32>( _totalCount ) : 0.0f; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class AreaGraph
     * @brief `<AreaGraph>` 를 읽고 탐색 상태를 보관합니다.
     * @code
     *     <AreaGraph><Area id="hall" name="Hall" x="0" y="0" w="10" h="8" region="Mansion1F"/>
     *         <Link from="hall" to="kitchen" requires="hasKey_red" oneWay="false" kind="Door"/></AreaGraph>
     * @endcode
     * @details 방에 들어가면 방문과 발견이 되고, 그 방에서 나가는 연결의 반대편 방도 발견된다. 문이 잠겨 있어도 문 너머가 지도에 보인다.
     *          길 찾기는 너비 우선이라 지나는 문 수가 가장 적은 길을 찾는다. 같은 길이 여럿이면 연결을 읽은 순서로 정해지므로 늘 같은 답이 나온다.
     */
    class SW_GF_API AreaGraph : public XMLCatalog<AreaGraph>
    {
        friend class XMLCatalog<AreaGraph>;

    public:
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). 타입 있는 참조 `AssetRef<AreaGraph>` 가 접미사로 가른다. */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".areas.xml", AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `AreaGraph.cpp` 의 줄이 로더를 더한다). */
        static constexpr AssetTypeDef kAssetType{ "AreaGraph", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        AreaGraph();

        /** @brief 방문과 발견을 모두 지웁니다(새 게임). */
        void resetState();
        /** @brief 방과 연결을 모두 지웁니다. 코드로 그래프를 만들 때 처음에 부릅니다. */
        void clear();
        /**
         * @brief 방을 더합니다. 절차 생성이나 다른 카탈로그 안에 적은 방에 씁니다. 같은 id 가 있으면 false 이며, 탐색 상태는 새 방만 비어 있게 늘립니다.
         */
        [[nodiscard]] bool addArea( const AreaDef& area );
        /**
         * @brief 두 방을 잇습니다. 모르는 방이거나 같은 방이면 false 입니다.
         * @details 조건식이 문법에 맞지 않으면 늘 잠긴 채로 더하고 true 를 반환하며, @p outbInvalidCondition 로 알린다.
         * @param condition `GameFlags` 조건식이다. 비면 늘 열려 있다.
         */
        [[nodiscard]] bool addLink( const hashed_string& from, const hashed_string& to, const hashed_string& kind, string_view condition, bool bOneWay,
                                    bool& outbInvalidCondition );
        /** @brief 방에 들어갑니다. 방문과 발견이 되고 이웃 방도 발견됩니다. 처음 방문이면 true 이고, 없는 방은 false 입니다. */
        bool enterArea( const hashed_string& areaID );
        /** @brief 방을 지도에 드러냅니다. 지도 아이템과 힌트가 씁니다. 새로 드러났으면 true 입니다. */
        bool discoverArea( const hashed_string& areaID );
        /** @brief 지역의 방을 모두 드러냅니다(층 지도). 새로 드러난 수를 반환합니다. */
        int32 discoverRegion( const hashed_string& region );

        /** @brief @p fromID 에서 @p toID 로 바로 이어진 연결을 지금 지날 수 있으면 true 입니다. 방향과 잠금을 봅니다. */
        bool canTraverse( const hashed_string& fromID, const hashed_string& toID, const GameFlags& flags ) const;
        /**
         * @brief 지금 지날 수 있는 연결만으로 가는 가장 짧은 길(지나는 연결 수)을 찾습니다.
         * @param outListArea 출발과 도착을 포함한 방 순서이다. 길이 없으면 비운다.
         */
        [[nodiscard]] bool findPath( const hashed_string& fromID, const hashed_string& toID, const GameFlags& flags, vector<hashed_string>& outListArea ) const;
        /** @brief 방문한 방을 모든 방으로 나눈 비율(0..1)입니다. */
        float32 computeExplorationRatio() const;
        /** @brief 지역의 방문 비율(0..1)입니다. 없는 지역은 0 입니다. */
        float32 computeRegionRatio( const hashed_string& region ) const;
        /** @brief 지역마다의 현황입니다. 지역이 처음 나온 순서입니다. */
        void collectRegionProgress( vector<AreaRegionProgress>& outListProgress ) const;
        /**
         * @brief 지금 막힌 문입니다. 방문한 방에서 아직 방문하지 않은 방으로 가는데 조건 때문에 지날 수 없는 연결이며, 다음 목표의 힌트로 씁니다.
         * @details 포인터는 다음 읽기까지 유효합니다.
         */
        void collectLockedFrontier( const GameFlags& flags, vector<const AreaLink*>& outListLink ) const;

        /** @brief 세이브용으로 방문한 방 id 를 이름 순으로 반환합니다. */
        void fillVisitedAreas( vector<hashed_string>& outListArea ) const;
        /** @brief 세이브용으로 발견한 방 id 를 이름 순으로 반환합니다. */
        void fillDiscoveredAreas( vector<hashed_string>& outListArea ) const;
        /** @brief 세이브에서 복원합니다. 방문한 방은 발견도 됩니다. 모르는 id 는 건너뜁니다. */
        void restoreState( const vector<hashed_string>& listVisited, const vector<hashed_string>& listDiscovered );

        bool                    isVisited( const hashed_string& areaID ) const;
        bool                    isDiscovered( const hashed_string& areaID ) const;
        const AreaDef*          findArea( const hashed_string& areaID ) const { return _catalog.find( areaID ); }
        const vector<AreaDef>&  getAreas() const { return _catalog.getAll(); }
        const vector<AreaLink>& getLinks() const { return _listLink; }
        int32                   getVisitedCount() const { return _visitedCount; }

    private:
        static constexpr const utf8* kXMLRootName = "AreaGraph"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        [[nodiscard]] bool           loadArea( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadLink( const XMLNode& node, string_view sourceName );
        /** @brief 연결을 @p fromIndex 쪽에서 지나면 닿는 방을 반환합니다. 그 방향으로 지날 수 없으면 비어 있으며, 잠금은 보지 않습니다. */
        [[nodiscard]] optional<int32> findOtherSide( const AreaLink& link, int32 fromIndex ) const;
        bool                          isUnlocked( const AreaLink& link, const GameFlags& flags ) const;
        void                          markVisited( int32 areaIndex );
        void                          fillMarkedAreas( const vector<uint8>& listMark, vector<hashed_string>& outListArea ) const;

    private:
        GameCatalog<AreaDef>  _catalog;
        vector<AreaLink>      _listLink;
        vector<vector<int32>> _listAdjacency;  ///< 방마다 닿은 연결의 인덱스(읽은 순서)
        vector<uint8>         _listVisited;    ///< 방마다 SW_TRUE/SW_FALSE
        vector<uint8>         _listDiscovered; ///< 방마다
        int32                 _visitedCount;
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<AreaGraph>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_GF_API_EXTERN_TEMPLATE, AreaGraph );
} // namespace sw
