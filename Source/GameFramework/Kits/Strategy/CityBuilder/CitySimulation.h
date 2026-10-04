/**
 * @file CitySimulation.h
 * @brief 도시 한 판 — 땅 · 도로 · 건물 배치, 노동 배분, 순회 일꾼(서비스 · 상인 · 수레 · 세리), 집 진화, 이민, 달마다 소비 · 세금 · 임금, 해마다 범람입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Inventory/ItemBag.h"
#include "GameFramework/Kits/Strategy/CityBuilder/CityCatalog.h"
#include "GameFramework/Utility/EventBuffer.h"
#include "GameFramework/Utility/FixedStepTimer.h"
#include "GameFramework/Utility/GameRandom.h"

namespace sw
{
    class Archive;

    /** @brief 도시 규칙의 수치입니다. 시간은 게임 초입니다. */
    struct CitySettings
    {
        float32 _secondsPerMonth{ 20.0f };
        float32 _walkerSpeed{ 2.5f };         ///< 칸 / 초
        float32 _workerRatio{ 0.4f };         ///< 인구 중 일하는 몫
        float32 _serviceDuration{ 30.0f };    ///< 서비스를 받은 뒤 그 효과가 남는 초
        float32 _immigrationInterval{ 1.5f }; ///< 빈 집 하나에 한 사람이 들어오는 간격
        float32 _evolveDelay{ 3.0f };         ///< 조건이 이만큼 이어져야 단계가 오르내린다
        float32 _wagePerWorkerPerMonth{ 0.5f };
        float32 _fixedStep{ 0.25f };
        int32   _serviceReach{ 2 };       ///< 일꾼이 지나는 칸에서 이 칸 안의 집에 준다
        int32   _goodsPerFourPeople{ 1 }; ///< 달마다 네 사람이 먹는 물자 수
        uint32  _randomSeed{ 3100u };     ///< 범람 · 일꾼의 갈림길
    };
} // namespace sw

namespace sw
{
    /** @brief 짓기 · 도로 결과입니다. */
    enum class CityPlaceResult : uint8
    {
        Ok = 0,
        OutOfBounds,
        Occupied,
        BadTerrain, ///< 물 · 바위, 또는 그 건물이 요구하는 땅이 아니다
        NotEnoughMoney,
        UnknownBuilding
    };

    SW_GF_API const utf8* toString( CityPlaceResult result );

    /** @brief 일꾼 종류입니다. */
    enum class CityWalkerKind : uint8
    {
        Service = 0, ///< 서비스 · 세리 — 도로를 돌아다니다 돌아간다
        Trader,      ///< 시장 상인 — 지나는 집에 물자를 판다
        Cart         ///< 생산물을 창고로
    };

    /** @brief 땅 한 칸입니다. */
    struct CityTile
    {
        int32       _buildingIndex{ -1 };
        int32       _roadComponent{ -1 }; ///< 이어진 도로끼리 같은 번호(도로가 아니면 −1)
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
        ItemBag                _stock{};
        const CityBuildingDef* _pDef{ nullptr };
        int2                   _origin{};
        int2                   _accessTile{ -1, -1 }; ///< 붙어 있는 도로 칸(없으면 −1)
        float32                _efficiency{ 0.0f };   ///< 배정된 일꾼 / 필요한 일꾼(일꾼이 필요 없으면 길만 있으면 1)
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
    /** @brief 일꾼 하나입니다. 그릴 자리는 `_tile` → `_nextTile` 사이 `_progress` 입니다. */
    struct CityWalker
    {
        vector<int2>   _listPath{};    ///< 정한 길(수레 · 돌아가기)
        vector<int2>   _listVisited{}; ///< 돌아다니며 지난 칸(되도록 새 길로)
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
    /** @brief 게임에 알리는 일(로그 · 알림)입니다. */
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
        int32 _value{ 0 }; ///< 달 끝: 번 돈 · 진화: 새 단계 · 범람: 비옥함 %
        int32 _building{ -1 };
        Kind  _kind{ Kind::MonthEnded };
    };
} // namespace sw

namespace sw
{
    /**
     * @class CitySimulation
     * @brief 파라오 · 시저 3 의 규칙을 줄인 도시입니다.
     * @details 한 걸음(`_fixedStep`)마다:
     *          1. 노동 — 인구 × `_workerRatio` 를 물 → 식량 사슬 → 나머지 서비스 순으로 건물에 나눈다. 효율 = 받은 / 필요한.
     *          2. 건물 — 반경 서비스(우물)는 둘레 집에 바로, 순회 서비스 · 시장은 간격마다 일꾼을 내보내고, 생산자는 효율 × 비옥함만큼 만들어 수레로
     *             같은 도로망의 창고에 보내며, 시장은 같은 도로망의 창고에서 사 온다.
     *          3. 일꾼 — 도로를 따라 걷는다. 순회는 안 가 본 길을 먼저 고르고 걸음을 다 쓰면 집으로 돌아간다. 지나는 칸 둘레의 집에 서비스 · 물자를 준다.
     *          4. 집 — 다음 단계의 서비스 · 물자 · 매력도가 이어지면 오르고, 지금 단계를 잃으면 내려간다. 빈 자리가 있고 일자리가 남으면 사람이 들어온다.
     *          달이 끝나면 물자를 먹고(네 사람에 하나), 세리가 다녀간 집이 세금을 내고, 일꾼 임금을 낸다. 해가 바뀌면 범람이 범람원의 비옥함을 정한다.
     *          카탈로그는 빌려 씁니다(시뮬레이션보다 오래 · 바뀌지 않게).
     */
    class SW_GF_API CitySimulation
    {
    public:
        static constexpr int32 kMonthsPerYear = 12;

        CitySimulation();

        void initialize( const CityCatalog* pCatalog, int32 width, int32 height, const CitySettings& settings, int32 startingMoney );
        void setTerrain( int32 x, int32 y, CityTerrain terrain );
        void fillTerrain( int32 minX, int32 minY, int32 maxX, int32 maxY, CityTerrain terrain );

        CityPlaceResult placeRoad( int32 x, int32 y );
        /** @brief 두 칸 사이에 ㄱ 자 도로를 깝니다(먼저 X, 다음 Y). 깐 칸 수입니다. */
        int32 placeRoadLine( const int2& from, const int2& to );
        /** @brief 건물을 @p x, @p y(왼쪽 아래 칸)에 짓습니다. */
        CityPlaceResult placeBuilding( const hashed_string& buildingId, int32 x, int32 y );
        /** @brief 그 칸의 건물 · 도로를 허뭅니다. 허물었으면 true 입니다. */
        bool demolish( int32 x, int32 y );

        /** @brief 시간을 넘깁니다(고정 걸음). */
        void update( float32 deltaTime );
        /** @brief 쌓인 알림을 꺼냅니다. */
        void drainEvents( vector<CityEvent>& outListEvent );

        const CityTile*             findTile( int32 x, int32 y ) const;
        const vector<CityBuilding>& getBuildings() const { return _listBuilding; }
        const vector<CityWalker>&   getWalkers() const { return _listWalker; }
        const CityBuilding*         findBuildingAt( int32 x, int32 y ) const;
        /** @brief 일꾼의 그릴 자리(칸 단위 실수 — 칸 가운데가 .5)입니다. */
        static float2 computeWalkerPosition( const CityWalker& walker );

        int32   getWidth() const { return _width; }
        int32   getHeight() const { return _height; }
        int32   getMoney() const { return _money; }
        int32   getPopulation() const;
        int32   getWorkforce() const { return _workforce; }
        int32   getEmployed() const { return _employed; }
        int32   getMonth() const { return _month; }
        int32   getYear() const { return _year; }
        float32 getFloodFertility() const { return _floodFertility; }
        /** @brief 종교 · 오락을 받은 집 사람의 몫(0..1)입니다(파라오의 문화 평가). */
        float32 computeCultureCoverage() const;
        /** @brief 집 단계의 사람 가중 평균입니다(번영 평가). */
        float32 computeAverageHouseLevel() const;
        /** @brief 이 칸의 집이 그 서비스를 지금 받고 있는가입니다. */
        bool    isHouseServed( const CityBuilding& house, CityService service ) const;
        float32 getTime() const { return _time; }

        /**
         * @brief 칸 · 건물 · 일꾼 · 돈 · 달력 · 난수를 씁니다(핫 리로드 · 세이브). 설정 · 카탈로그는 쓰지 않습니다 — 읽는 쪽이 같은 것으로 `initialize` 합니다.
         * @details 건물 · 물자는 카탈로그 id 로 적습니다(포인터는 실행마다 다르다).
         */
        void writeState( Archive& outArchive ) const;
        /**
         * @brief `writeState` 의 바이트로 바꿉니다. `initialize` 한 뒤에 부릅니다(같은 크기 · 카탈로그).
         * @return 크기가 다르거나, 카탈로그에 없는 건물이 있거나, 깨졌으면 false 이고 그대로입니다.
         */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        void stepFixed( float32 deltaTime );
        void assignLabor();
        void updateBuildings( float32 deltaTime );
        void updateWalkers( float32 deltaTime );
        void updateHouses( float32 deltaTime );
        void endMonth();
        void recomputeRoadComponents();
        void recomputeDesirability();
        void refreshAccess( CityBuilding& building ) const;

        void spawnRoamer( int32 buildingIndex, CityWalkerKind kind );
        void spawnCart( int32 buildingIndex );
        void stepWalker( CityWalker& walker );
        void serveAround( CityWalker& walker );
        void startReturn( CityWalker& walker );
        void removeWalker( CityWalker& walker );
        /** @brief 도로 칸 사이의 가장 짧은 길(너비 우선)입니다. 못 가면 false 입니다. */
        bool  findRoadPath( const int2& from, const int2& to, vector<int2>& outListPath ) const;
        int32 findStorageFor( const hashed_string& goodId, int32 roadComponent, const int2& from, int32 amount ) const;
        int32 computeStorageFree( const CityBuilding& storage ) const;
        bool  meetsHouseLevel( const CityBuilding& house, int32 level ) const;
        bool  isRoad( int32 x, int32 y ) const;
        int32 getRoadComponent( const int2& tile ) const;

        vector<CityTile>       _listTile;
        vector<CityBuilding>   _listBuilding;
        vector<CityWalker>     _listWalker;
        EventBuffer<CityEvent> _eventBuffer;
        const CityCatalog*     _pCatalog;
        CitySettings           _settings;
        FixedStepTimer         _stepTimer;
        GameRandom             _random;
        float32                _time;
        float32                _monthTimer;
        float32                _floodFertility;
        float32                _wageDebt;
        int32                  _width;
        int32                  _height;
        int32                  _money;
        int32                  _monthIncome;
        int32                  _workforce;
        int32                  _employed;
        int32                  _month;
        int32                  _year;
        uint8                  _bRoadsDirty;
        uint8                  _bDesirabilityDirty;
    };
} // namespace sw
