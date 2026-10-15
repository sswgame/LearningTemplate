/**
 * @file CitySimulation.h
 * @brief 도시 한 판을 진행합니다. 땅, 도로, 건물 배치, 노동 배분, 순회 일꾼(서비스, 상인, 수레, 세리), 집 진화, 이민, 달마다 소비와 세금과 임금, 해마다 범람을 다룹니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Utility/Grid/GridTopology.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Foundation/Utility/Time/FixedStepTimer.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/Base/World/Land/LandRegistry.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CityCatalog.h"

namespace sw
{
    struct SessionStateRefs;

    class Archive;
    class Wallet;

    /** @brief 도시 규칙의 수치입니다. 시간 단위는 게임 초입니다. */
    struct CitySettings
    {
        float32       _walkerSpeed{ 2.5f };         ///< 셀 / 초
        float32       _workerRatio{ 0.4f };         ///< 인구 중 일하는 사람의 비율
        float32       _serviceDuration{ 30.0f };    ///< 서비스를 받은 뒤 그 효과가 남아 있는 시간(초)
        float32       _immigrationInterval{ 1.5f }; ///< 빈 집 하나에 한 사람이 들어오는 간격
        float32       _evolveDelay{ 3.0f };         ///< 조건이 이만큼 이어져야 집 단계가 오르내린다
        float32       _wagePerWorkerPerMonth{ 0.5f };
        float32       _fixedStep{ 0.25f };
        int32         _serviceReach{ 2 };       ///< 일꾼이 지나는 셀에서 이 거리 안의 집에 서비스를 준다
        int32         _goodsPerFourPeople{ 1 }; ///< 한 달에 네 사람이 먹는 물자 수
        uint32        _randomSeed{ 3100u };     ///< 범람과 일꾼의 갈림길 선택에 쓰는 난수 시드
        hashed_string _currency{ "Deben" };     ///< 빌린 지갑에서 쓰는 통화
    };
} // namespace sw

namespace sw
{
    /** @brief 건설과 도로 놓기의 결과입니다. */
    ENUM()
    enum class CityPlaceResult : uint8
    {
        Ok = 0,
        OutOfBounds,
        Occupied,
        BadTerrain, ///< 물이나 바위이거나, 그 건물이 요구하는 땅이 아니다
        NotEnoughMoney,
        UnknownBuilding
    };

    /** @brief 일꾼의 종류입니다. */
    enum class CityWalkerKind : uint8
    {
        Service = 0, ///< 서비스 일꾼과 세리이다. 도로를 돌아다니다 돌아간다
        Trader,      ///< 시장 상인이다. 지나는 집에 물자를 판다
        Cart         ///< 생산한 물자를 창고로 나른다
    };

    /** @brief 타일 하나입니다. */
    struct CityTile
    {
        int32       _buildingIndex{ -1 };
        int32       _roadComponent{ -1 }; ///< 이어진 도로끼리 같은 번호를 가진다(도로가 아니면 -1)
        int16       _desirability{ 0 };
        CityTerrain _terrain{ CityTerrain::Grass };
        uint8       _bRoad{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 지은 건물 하나입니다(집 포함). */
    struct CityBuilding
    {
        ItemStackList          _stock{};
        const CityBuildingDef* _pDef{ nullptr };
        int2                   _origin{};
        int2                   _accessTile{ -1, -1 }; ///< 붙어 있는 도로 타일이다(없으면 -1)
        float32                _efficiency{ 0.0f };   ///< 배정된 일꾼 수 / 필요한 일꾼 수이다(일꾼이 필요 없는 건물은 길만 있으면 1)
        float32                _walkerTimer{ 0.0f };
        float32                _productionProgress{ 0.0f };
        float32                _arrServiceTime[static_cast<int32>( CityService::Count )]{};
        float32                _evolveTimer{ 0.0f };
        float32                _devolveTimer{ 0.0f };
        float32                _immigrationTimer{ 0.0f };
        int32                  _assignedWorkers{ 0 };
        int32                  _activeWalkerCount{ 0 };
        int32                  _level{ 0 }; ///< 집 단계
        int32                  _population{ 0 };
        uint8                  _bAlive{ SW_TRUE };

        bool hasRoadAccess() const { return _accessTile._x >= 0; }
        bool isHouse() const { return _pDef != nullptr && _pDef->_kind == CityBuildingKind::House; }
    };
} // namespace sw

namespace sw
{
    /** @brief 일꾼 하나입니다. 화면에 그리는 위치는 `_tile` 에서 `_nextTile` 사이의 `_progress` 지점입니다. */
    struct CityWalker
    {
        vector<int2>   _listPath{};    ///< 정해 둔 경로이다(수레와 돌아가는 일꾼이 쓴다)
        vector<int2>   _listVisited{}; ///< 돌아다니며 지난 타일이다(되도록 새 길로 가게 하려는 용도)
        hashed_string  _cargoGood{};
        int2           _tile{};
        int2           _previousTile{ -1, -1 };
        int2           _nextTile{};
        float32        _progress{ 0.0f };
        int32          _homeBuilding{ -1 };
        int32          _targetBuilding{ -1 };
        int32          _stepsLeft{ 0 };
        int32          _cargoAmount{ 0 };
        CityWalkerKind _kind{ CityWalkerKind::Service };
        CityService    _service{ CityService::Count };
        uint8          _bReturning{ SW_FALSE };
        uint8          _bAlive{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /** @brief 게임에 알리는 이벤트(로그와 알림)입니다. */
    struct CityEvent
    {
        enum class Kind : uint8
        {
            MonthEnded = 0,
            HouseEvolved,
            HouseDevolved,
            Flood,
            GoodsDelivered
        };
        int32 _value{ 0 }; ///< 달 끝은 번 돈, 진화는 새 단계, 범람은 비옥함(%)이다
        int32 _building{ -1 };
        Kind  _kind{ Kind::MonthEnded };
    };
} // namespace sw

namespace sw
{
    /**
     * @class CitySimulation
     * @brief 파라오와 시저 3 의 규칙을 줄인 도시입니다.
     * @details 고정 걸음(`_fixedStep`)마다 아래 순서로 처리한다.
     *          1. 노동: 인구 x `_workerRatio` 를 물, 식량 사슬, 나머지 서비스 순으로 건물에 나눈다. 효율은 받은 일꾼 수 / 필요한 일꾼 수이다.
     *          2. 건물: 반경 서비스(우물)는 둘레 집에 바로 주고, 순회 서비스와 시장은 간격마다 일꾼을 내보낸다. 생산자는 효율 x 비옥함만큼 만들어 수레로
     *             같은 도로망의 창고에 보내고, 시장은 같은 도로망의 창고에서 물자를 사 온다.
     *          3. 일꾼: 도로를 따라 걷는다. 순회 일꾼은 안 가 본 길을 먼저 고르고, 걸음을 다 쓰면 집으로 돌아간다. 지나는 셀 둘레의 집에 서비스와 물자를 준다.
     *          4. 집: 다음 단계의 서비스, 물자, 매력도 조건이 이어지면 단계가 오르고, 지금 단계의 조건을 잃으면 내려간다. 빈 자리가 있고 일자리가 남으면 사람이 들어온다.
     *
     *          달 결산(`settleMonth`)에서는 물자를 먹고(네 사람에 하나), 세리가 다녀간 집이 세금을 내고, 일꾼 임금을 낸다. 디렉터가 공유 시계의 달 넘김에 맞춰 `settleMonth` 를 호출한다.
     *          해가 바뀌면 범람이 범람원의 비옥함을 정한다.
     *          카탈로그는 빌려 쓴다. 시뮬레이션보다 오래 살아야 하고 바뀌지 않아야 한다.
     */
    class SW_KIT_API CitySimulation
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "CITY" );
        static constexpr uint32 kStateVersion = 1;

        CitySimulation();

        /**
         * @brief 빈 도시를 엽니다. 돈은 빌린 지갑(@p refs 의 지갑, 통화는 `CitySettings::_currency`)에서 나가고 들어옵니다.
         * @details 지갑이 없으면 알리고, 건설과 도로 놓기는 모두 돈 부족으로 거절된다. 달 결산에서 돈이 모자라면 지갑이 빚(`Wallet::charge`)을 진다.
         */
        void initialize( const CityCatalog* pCatalog, int32 width, int32 height, const CitySettings& settings, const SessionStateRefs& refs );
        void setTerrain( int32 x, int32 y, CityTerrain terrain );
        void fillTerrain( int32 minX, int32 minY, int32 maxX, int32 maxY, CityTerrain terrain );
        /**
         * @brief 공유 땅을 빌립니다. 도시 셀 (0, 0) 이 땅 셀 @p origin 에 해당합니다. 이후 도로와 건물은 놓기 전에 땅을 얻고(건물은 막힘, 도로는 막히지 않음) 허물면 반납합니다.
         * @details 지형(`setTerrain`)은 땅 테이블과 무관하다. 이미 놓인 도로와 건물은 땅을 얻지 않으므로 건설하기 전에 호출해야 한다. @p pLand 가 nullptr 이면 빌린 땅을 반납한다.
         */
        void bindLand( LandRegistry* pLand, const int2& origin );

        /**
         * @brief 한 달을 결산합니다. 물자를 먹고, 세리가 다녀간 집이 세금을 내고, 일꾼 임금을 냅니다. 돈이 모자라면 지갑이 빚을 집니다. @p bNewYear 가 true 이면 범람이 다음 해의 비옥함을 정합니다.
         * @details 달력은 공유 시계가 가지고 있다. 디렉터가 시계 알림(달 넘김, 해 넘김)을 받아 이 함수를 호출하며, 시뮬레이션은 시간을 세지 않는다.
         */
        void settleMonth( bool bNewYear );

        /** @brief 도로 한 셀을 깝니다. 공유 땅이 남의 것이면 `Occupied` 를 반환합니다. */
        CityPlaceResult placeRoad( int32 x, int32 y );
        /** @brief 두 지점 사이에 ㄱ 자 도로를 깝니다(먼저 X, 다음 Y). 깐 셀 수를 반환합니다. */
        int32 placeRoadLine( const int2& from, const int2& to );
        /** @brief 건물을 @p x, @p y(왼쪽 아래 셀)에 짓습니다. 건물 영역의 공유 땅이 한 셀이라도 남의 것이면 `Occupied` 를 반환합니다. */
        CityPlaceResult placeBuilding( const hashed_string& buildingID, int32 x, int32 y );
        /** @brief 그 셀의 건물이나 도로를 허뭅니다. 허물었으면 true 를 반환합니다. */
        bool demolish( int32 x, int32 y );

        /** @brief 시간을 진행합니다. 내부에서는 고정 걸음으로 나눠 처리합니다. */
        void update( float32 deltaTime );
        /** @brief 쌓인 이벤트를 꺼냅니다. */
        void drainEvents( vector<CityEvent>& outListEvent );

        const CityTile*             findTile( int32 x, int32 y ) const;
        const vector<CityBuilding>& getBuildings() const { return _listBuilding; }
        const vector<CityWalker>&   getWalkers() const { return _listWalker; }
        const CityBuilding*         findBuildingAt( int32 x, int32 y ) const;
        /** @brief 일꾼을 그릴 위치를 셀 단위 실수로 반환합니다. 셀 가운데가 .5 입니다. */
        static float2 computeWalkerPosition( const CityWalker& walker );

        int32                getWidth() const { return _topology._width; }
        int32                getHeight() const { return _topology._height; }
        const hashed_string& getCurrency() const { return _settings._currency; }
        int32                getPopulation() const;
        int32                getWorkforce() const { return _workforce; }
        int32                getEmployed() const { return _employed; }
        float32              getFloodFertility() const { return _floodFertility; }
        /** @brief 종교와 오락을 받은 집 사람의 비율(0..1)을 반환합니다. 파라오의 문화 평가에 해당합니다. */
        float32 computeCultureCoverage() const;
        /** @brief 집 단계의 인구 가중 평균을 반환합니다(번영 평가). */
        float32 computeAverageHouseLevel() const;
        /** @brief 그 집이 그 서비스를 지금 받고 있는지 반환합니다. */
        bool    isHouseServed( const CityBuilding& house, CityService service ) const;
        float32 getTime() const { return _time; }

        /**
         * @brief 타일, 건물, 일꾼, 돈, 달력, 난수를 씁니다. 핫 리로드와 세이브에 쓰며 설정과 카탈로그는 쓰지 않습니다. 읽는 쪽에서 같은 설정과 카탈로그로 `initialize` 해야 합니다.
         * @details 건물과 물자는 카탈로그 id 로 적는다(포인터는 실행마다 다르기 때문이다).
         */
        void writeState( Archive& outArchive ) const;
        /**
         * @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. `initialize` 를 먼저 호출해야 하며 크기와 카탈로그가 같아야 합니다.
         * @return 크기가 다르거나, 카탈로그에 없는 건물이 있거나, 데이터가 깨졌으면 false 이며 상태는 그대로 둔다.
         */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        void stepFixed( float32 deltaTime );
        void assignLabor();
        void updateBuildings( float32 deltaTime );
        void updateWalkers( float32 deltaTime );
        void updateHouses( float32 deltaTime );
        void recomputeRoadComponents();
        void recomputeDesirability();
        void refreshAccess( CityBuilding& building ) const;

        void spawnRoamer( int32 buildingIndex, CityWalkerKind kind );
        void spawnCart( int32 buildingIndex );
        void stepWalker( CityWalker& walker );
        void serveAround( CityWalker& walker );
        void startReturn( CityWalker& walker );
        void removeWalker( CityWalker& walker );
        /** @brief 도로 타일 사이의 가장 짧은 경로(너비 우선)를 구합니다. 갈 수 없으면 false 를 반환합니다. */
        bool  findRoadPath( const int2& from, const int2& to, vector<int2>& outListPath ) const;
        int32 findStorageFor( const hashed_string& goodID, int32 roadComponent, const int2& from, int32 amount ) const;
        int32 computeStorageFree( const CityBuilding& storage ) const;
        bool  meetsHouseLevel( const CityBuilding& house, int32 level ) const;
        bool  isRoad( int32 x, int32 y ) const;
        int32 getRoadComponent( const int2& tile ) const;

        vector<CityTile>          _listTile;
        vector<CityBuilding>      _listBuilding;
        vector<CityWalker>        _listWalker;
        EventBuffer<CityEvent>    _eventBuffer;
        mutable GridSearchScratch _roadSearch; ///< 일꾼 길 찾기(`findRoadPath`)가 호출마다 재사용하는 스크래치이다
        const CityCatalog*        _pCatalog;
        Wallet*                   _pWallet; ///< 빌린 지갑
        CitySettings              _settings;
        FixedStepTimer            _stepTimer;
        GameRandom                _random;
        float32                   _time;
        float32                   _floodFertility;
        float32                   _wageDebt;
        GridTopology              _topology;
        LandBinding               _land; ///< 빌린 공유 땅(없으면 단독 동작)
        int32                     _monthIncome;
        int32                     _workforce;
        int32                     _employed;
        uint8                     _bRoadsDirty;
        uint8                     _bDesirabilityDirty;
    };
} // namespace sw
