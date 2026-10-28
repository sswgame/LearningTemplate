/**
 * @file RunMap.h
 * @brief 로그라이트 한 판의 갈림길 지도입니다. 층마다 노드(전투, 정예, 상점, 휴식, 사건, 보물, 보스)가 있고, 길은 서로 엇갈리지 않으며, 같은 시드로 같은 지도가 나오고, 현재 위치와 갈 수 있는 노드를 알려 줍니다.
 * @details 슬레이 더 스파이어 같은 지도이다. 노드 종류는 데이터(이름, 가중치, 나올 수 있는 층, 연속 금지 여부)이므로 장르마다 바꿀 수 있다. 메탈슬러그 택틱스의 작전 지도도 같은 구조이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class GameRandom;

    /** @brief 노드 종류의 규칙입니다. */
    REFLECT()
    struct RunNodeRule
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _kind{};
        PROPERTY( Min = 0.0 )
        float32 _weight{ 1.0f };
        PROPERTY()
        int32 _minFloor{ 0 };
        PROPERTY( Min = "_minFloor" )
        int32 _maxFloor{ 1000 };
        PROPERTY()
        bool _bNoRepeat{ false }; ///< 바로 앞 노드와 같은 종류가 되지 않는다. 휴식이나 상점이 연달아 나오지 않게 한다
    };
} // namespace sw

namespace sw
{
    /** @brief `<Floor index="-1" kind="Boss"/>` — 층 하나를 종류 하나로 고정합니다. 음수 번호는 끝에서 셉니다(-1 이 마지막 층). */
    REFLECT()
    struct RunForcedFloorDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _kind{};
        PROPERTY( Required )
        int32 _index{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 지도 설정입니다. */
    REFLECT()
    struct SW_KIT_API RunMapSettings
    {
        REFLECT_BODY();

        vector<RunNodeRule>   _listRule{};
        vector<hashed_string> _listForcedFloor{}; ///< 층 번호별로 그 층 전체를 이 종류로 고정한다. 비어 있으면 규칙대로 정한다. 첫 층은 전투, 마지막 층은 보스에 쓴다
        PROPERTY( Name = "floors", Min = 2 )
        int32 _floorCount{ 15 };
        PROPERTY( Name = "columns", Min = 1 )
        int32 _columnCount{ 7 };
        PROPERTY( Name = "paths", Min = 1 )
        int32 _pathCount{ 6 }; ///< 아래에서 위로 그을 길의 수. 길이 지나간 노드만 남는다
    };
} // namespace sw

namespace sw
{
    /** @brief 노드 하나입니다. */
    struct RunNode
    {
        vector<int32> _listNext{}; ///< 위층에서 이어진 노드 번호
        hashed_string _kind{};
        int32         _floor{ 0 };
        int32         _column{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class RunMap
     * @brief 길은 아래층 노드에서 위층의 같은 열이나 옆 열의 노드로 그어지고, 이미 그은 길과 엇갈리면 다른 쪽으로 꺾습니다. 마지막 층은 노드 하나(보스)로 모입니다.
     */
    class SW_KIT_API RunMap
    {
    public:
        static constexpr int32 kStart = -1; ///< 아직 첫 층을 고르지 않았다

        /** @brief `<RunMap floors="15" columns="7" paths="6"><Node kind="Battle" weight="5" minFloor="0"/>...<Floor index="0" kind="Battle"/></RunMap>` */
        [[nodiscard]] static bool loadSettings( string_view xmlText, string_view sourceName, RunMapSettings& outSettings );

        /** @brief 지도를 새로 긋습니다. 설정이 `PROPERTY( Min )` 범위 밖이면 오류를 남기고 false 이며 지도를 바꾸지 않습니다(값을 조용히 맞추지 않는다). */
        [[nodiscard]] bool generate( const RunMapSettings& settings, uint32 seed );
        /** @brief 현재 위치에서 갈 수 있는 노드입니다. 시작 상태이면 첫 층 전체입니다. */
        void collectChoices( vector<int32>& outListNode ) const;
        /** @brief 노드로 이동합니다. 갈 수 없는 노드이면 false 입니다. */
        [[nodiscard]] bool moveTo( int32 nodeIndex );

        const vector<RunNode>& getNodes() const { return _listNode; }
        const RunNode*         findNode( int32 nodeIndex ) const;
        int32                  getCurrent() const { return _current; }
        int32                  getFloorCount() const { return _floorCount; }
        /** @brief 끝 노드(보스)에 도착했으면 true 입니다. */
        bool isFinished() const;

        /** @brief 노드(층, 열, 종류, 이어진 노드), 노드 인덱스 테이블, 층 수, 열 수, 현재 위치를 씁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 상태를 복원합니다. 데이터가 깨졌으면 false 를 반환하고 상태는 바뀌지 않습니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        int32         getOrCreateNode( int32 floor, int32 column );
        bool          crossesExisting( int32 floor, int32 fromColumn, int32 toColumn ) const;
        hashed_string pickKind( const RunMapSettings& settings, int32 floor, const hashed_string& previousKind, GameRandom& random ) const;

        vector<RunNode> _listNode;
        vector<int32>   _listCell; ///< 층과 열에서 노드 번호로의 대응(없으면 −1)
        int32           _floorCount{ 0 };
        int32           _columnCount{ 0 };
        int32           _current{ kStart };
    };
} // namespace sw
