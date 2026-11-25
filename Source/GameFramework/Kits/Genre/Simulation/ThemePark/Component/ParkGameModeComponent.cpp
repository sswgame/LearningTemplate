#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Component/ParkGameModeComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/OrientationUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Character/Navigation/NavGridObstacle.h"

namespace sw
{
    namespace
    {
        struct ParkGameModeComponentInternal
        {
            static constexpr FormatDef kStateFormat{ "PARK" };

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 라이브러리(`*.audioevents.xml`)가 정한다.
            static constexpr const utf8* kEventBlocked = "ParkBlocked";
            static constexpr const utf8* kEventBuilt   = "ParkBuilt";

            static constexpr float32 kWalkGridMargin         = 24.0f; ///< 걷는 격자가 정문 · 놀이기구 자리 밖으로 더 덮는 거리(m) — 코스터 줄 입구와 둘러보는 자리가 든다
            static constexpr float32 kStationCenterDistance  = 6.0f;  ///< 줄 입구를 잡는 스테이션 가운데의 트랙 거리(m)
            static constexpr float32 kEntranceSideDistance   = 5.0f;  ///< 스테이션에서 줄 입구까지 바깥쪽 거리(m)
            static constexpr int32   kCoasterBaseRunningCost = 8;     ///< 코스터의 분당 운영비 바탕
            static constexpr float32 kTrackPerRunningCost    = 60.0f; ///< 트랙 이만큼(m)마다 분당 운영비가 1 오른다

            /** @brief 시나리오 프로브입니다. 첫 공원 디렉터의 손님 수를 반환하며, 그림자 장면이 비어 있지 않다는 전제를 확인합니다. */
            [[nodiscard]] static bool readGuestCount( const ParkGameModeComponent& gameMode, float64& outValue )
            {
                outValue = static_cast<float64>( gameMode.getSimulation().getGuestCount() );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ParkGameModeComponent::ParkGameModeComponent()
        : _parkDataPath{}
        , _gate{}
        , _listWalkBlocker{}
        , _walkCellSize{ 1.0f }
        , _walkClearance{ 1.2f }
        , _autoBuildInterval{ 20.0f }
        , _statusLogInterval{ 10.0f }
        , _walkGrid{}
        , _simulation{}
        , _wallet{}
        , _startingCash{ 0 }
        , _pLayoutCatalog{}
        , _settings{}
        , _listPlacement{}
        , _listCoaster{}
        , _autoBuildTimer{ 0.0f }
        , _parkYaw{ 0.0f }
        , _parkRevision{ 0 }
    {
    }

    ParkGameModeComponent::~ParkGameModeComponent() = default;

    void ParkGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "ParkGameMode.ParkOpen", "[Park] park is open" );
        logStatus();
    }

    void ParkGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        addMoneyResult( outListValue, _wallet.getBalance( _settings._currency ) - _startingCash );
    }

    void ParkGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;

        _simulation.update( deltaTime );
        if ( isAutoPlayOn() )
        {
            _autoBuildTimer += deltaTime;
            if ( _autoBuildTimer >= _autoBuildInterval )
            {
                _autoBuildTimer = 0.0f;
                (void)buildCheapestRemaining(); // 돈이 모자라면 다음 차례에 다시 본다
            }
        }
        // 열차만 나아간다. 차 · 평지 놀이기구의 모습은 클라이언트 뷰가 이 상태를 읽어 맞춘다.
        for ( unique_ptr<CoasterRuntime>& pCoaster : _listCoaster )
        {
            pCoaster->_train.step( deltaTime );
        }
    }

    void ParkGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 모습은 클라이언트 모듈(`ParkViewComponent`)이 세운다.
        (void)manager;
        (void)bRespawnViews;
    }

    const CoasterTrain* ParkGameModeComponent::findCoasterTrain( int32 coasterIndex ) const
    {
        if ( coasterIndex < 0 || coasterIndex >= static_cast<int32>( _listCoaster.size() ) )
            return nullptr;
        // 뷰가 워커에서 부른다. 컨테이너 인덱스 대신 포인터로 읽는다.
        const CoasterRuntime* pCoaster = _listCoaster.data()[coasterIndex].get();
        return pCoaster != nullptr ? &pCoaster->_train : nullptr;
    }

    optional<int32> ParkGameModeComponent::findCoasterIndex( int32 placementIndex ) const
    {
        for ( size_t coasterIndex = 0; coasterIndex < _listCoaster.size(); ++coasterIndex )
        {
            if ( _listCoaster[coasterIndex]->_placementIndex == placementIndex )
                return static_cast<int32>( coasterIndex );
        }
        return nullopt;
    }

    // ------------------------------------------------------------------------------
    // 명령(클라이언트 모듈)
    // ------------------------------------------------------------------------------
    bool ParkGameModeComponent::submitBuild( int32 placementIndex )
    {
        return isStarted() && buildPlacement( placementIndex );
    }

    bool ParkGameModeComponent::submitBuildCheapest()
    {
        return isStarted() && buildCheapestRemaining();
    }

    bool ParkGameModeComponent::submitToggleRideOpen( int32 rideIndex )
    {
        if ( hasRide( rideIndex ) == false )
            return false;
        const ParkRide& ride = _simulation.getRides()[static_cast<size_t>( rideIndex )];
        _simulation.setRideOpen( rideIndex, ride._bOpen == SW_FALSE );
        SW_LOG_INFO_ID( "ParkGameMode.Now", "[Park] %# is now %#", ride._name.c_str(), ride._bOpen != SW_FALSE ? "open" : "closed" );
        return true;
    }

    bool ParkGameModeComponent::submitRidePriceChange( int32 rideIndex, int32 delta )
    {
        if ( hasRide( rideIndex ) == false )
            return false;
        const ParkRide& ride = _simulation.getRides()[static_cast<size_t>( rideIndex )];
        _simulation.setRidePrice( rideIndex, ride._price + delta );
        SW_LOG_INFO_ID( "ParkGameMode.TicketWorthAbout", "[Park] %# ticket $%# (worth about $%#)", ride._name.c_str(), ride._price, static_cast<int32>( ThemeParkSimulation::computeRideValue( ride ) ) );
        return true;
    }

    bool ParkGameModeComponent::submitEntryFeeChange( int32 delta )
    {
        if ( isStarted() == false )
            return false;
        _simulation.setEntryFee( _simulation.getSettings()._entryFee + delta );
        SW_LOG_INFO_ID( "ParkGameMode.EntryFee", "[Park] entry fee $%#", _simulation.getSettings()._entryFee );
        return true;
    }

    bool ParkGameModeComponent::hasRide( int32 rideIndex ) const
    {
        return isStarted() && 0 <= rideIndex && rideIndex < static_cast<int32>( _simulation.getRides().size() );
    }

    // ------------------------------------------------------------------------------
    // 데이터와 건설
    // ------------------------------------------------------------------------------
    bool ParkGameModeComponent::placeLayout( const ParkLayout& layout )
    {
        const GameObject* const     pOwner  = getOwner();
        const SceneComponent* const pOrigin = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pOrigin == nullptr )
        {
            SW_LOG_ERROR_ID( "ParkGameMode.OriginMissing", "[Park] '%#' has no scene component - the park has no origin and cannot open", pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            return false;
        }
        // 놀이공원 시뮬레이션(손님 · 트랙)은 절대 자리에서 돈다 — 월드 원점이 옮겨져도 시뮬레이션 자리는 그대로이고 모습만 로컬로 옮긴다.
        const WorldOrigin& worldOrigin = pOwner->getManager()->getWorldOrigin();
        const float4x4     world       = worldOrigin.toAbsolute( pOrigin->getWorldMatrix() );
        _parkYaw                       = OrientationUtil::computeYawFromWorld( world );
        _settings._gatePosition        = float3::transform( layout.getGatePosition(), world );
        // 씬에 놓인 정문이 있으면 그 위치가 정문이다. 에디터에서 옮기면 손님 경로가 따라온다.
        const GameObject* const     pGate      = findGameObject( _gate );
        const SceneComponent* const pGateScene = pGate != nullptr ? pGate->getPrimarySceneComponent() : nullptr;
        if ( pGateScene != nullptr )
            _settings._gatePosition = worldOrigin.toAbsolute( pGateScene->getWorldPosition() );
        _listPlacement.clear();
        _listPlacement.reserve( layout.getPlacements().size() );
        for ( const ParkRidePlacement& placement : layout.getPlacements() )
        {
            RidePlacement ride{};
            static_cast<ParkRidePlacement&>( ride ) = placement;
            ride._position                          = float3::transform( placement._position, world );
            ride._ride._entrance                    = float3::transform( placement._ride._entrance, world );
            ride._heading += MathUtil::toDegree( _parkYaw );
            _listPlacement.push_back( ride );
        }
        if ( _listPlacement.empty() == false )
            return true;
        SW_LOG_ERROR_ID( "ParkGameMode.PlacesNoRidesPark", "[Park] %# places no rides - the park cannot open", _parkDataPath.c_str() );
        return false;
    }

    bool ParkGameModeComponent::rebuildWalkGrid()
    {
        using Internal = ParkGameModeComponentInternal;
        float3 minimum = _settings._gatePosition;
        float3 maximum = _settings._gatePosition;
        for ( const RidePlacement& placement : _listPlacement )
        {
            minimum = float3::min( minimum, float3::min( placement._position, placement._ride._entrance ) );
            maximum = float3::max( maximum, float3::max( placement._position, placement._ride._entrance ) );
        }
        const float32 margin = Internal::kWalkGridMargin + _settings._wanderRadius;
        const float3  origin{ minimum._x - margin, _settings._gatePosition._y, minimum._z - margin };
        const int32   width  = static_cast<int32>( MathUtil::ceil( ( maximum._x + margin - origin._x ) / _walkCellSize ) );
        const int32   height = static_cast<int32>( MathUtil::ceil( ( maximum._z + margin - origin._z ) / _walkCellSize ) );
        if ( _walkGrid.initialize( width, height, _walkCellSize, origin ) == false )
            return false; // 까닭은 격자가 남겼다
        for ( const RidePlacement& placement : _listPlacement )
        {
            if ( placement._layoutID.empty() == false )
                continue; // 코스터 레일은 머리 위를 지난다 — 막는 것은 평지 놀이기구의 땅이다(아직 짓지 않은 땅도)
            const float32 half  = MathUtil::max( placement._size._x, placement._size._z ) * 0.5f + _walkClearance;
            const int2    first = _walkGrid.computeCell( placement._position - float3{ half, 0.0f, half } );
            const int2    last  = _walkGrid.computeCell( placement._position + float3{ half, 0.0f, half } );
            _walkGrid.setAreaCost( first._x, first._y, last._x, last._y, kNavBlockedCost );
        }
        const GameObjectManager* const pManager = getObjectManager();
        for ( const GameObjectHandle blocker : _listWalkBlocker )
        {
            const GameObject* const         pObject = findGameObject( blocker );
            const RigidBodyComponent* const pBody   = pObject != nullptr ? pObject->getComponent<RigidBodyComponent>() : nullptr;
            if ( pManager == nullptr || pBody == nullptr || pBody->getBodyType() != PhysicsBodyType::Static )
            {
                SW_LOG_ERROR_ID( "ParkGameMode.WalkBlockerNoBody", "[Park] walk blocker '%#' has no static rigid body - its size is the body's shape, the park cannot open", pObject != nullptr ? pObject->getName().c_str() : "?" );
                return false;
            }
            (void)NavGridObstacle::blockBody( _walkGrid, pManager->getWorldOrigin(), *pBody, _walkClearance ); // 막은 칸 수는 쓰지 않는다
        }
        return true;
    }

    void ParkGameModeComponent::findGuestRoute( const float3& from, const float3& to, vector<float3>& outListPoint ) const
    {
        GridPathfinder pathfinder; // 놀이기구를 세울 때 한 번 부른다
        ThemeParkSimulation::findWalkRoute( _walkGrid, pathfinder, from, to, outListPoint );
    }

    bool ParkGameModeComponent::startGame()
    {
        // 배치는 공유 캐시의 한 벌이다. 코스터 레이아웃은 배치가 같은 팩에서 받아 쥔 그 한 벌을 쓴다 — 게임 모드가 코스터 경로를 따로 적지 않는다(기준 하나).
        // 배치 · 규칙은 열 때 값으로 옮겨 쥔다 — 연 공원은 다시 읽힌 배치를 따라가지 않는다(다음 판이 쓴다).
        shared_ptr<const ParkLayout> pLayout;
        if ( acquireRequiredSnapshot( _parkDataPath, pLayout ) == false )
            return false;
        const ParkLayout& layout = *pLayout;
        _pLayoutCatalog          = layout.getCoasterLayouts().snapshot();
        _settings                = layout.getRules(); // 공원 규칙은 배치 데이터의 <Rules> 다(감사 K-4)
        _settings._entryFee      = layout.getEntryFee();
        if ( placeLayout( layout ) == false || rebuildWalkGrid() == false )
            return false;
        _wallet.clear();
        _startingCash = layout.getStartingCash();
        _wallet.add( _settings._currency, _startingCash );
        if ( _simulation.initialize( _settings, _wallet, _walkGrid ) == false )
            return false; // 까닭은 시뮬레이션이 남겼다 — 틀린 규칙으로 공원을 열지 않는다
        _listCoaster.clear();

        // 처음 공원에는 가장 싼 평지 놀이기구 하나와 가장 싼 코스터 하나를 둔다.
        int32 cheapestFlat    = -1;
        int32 cheapestCoaster = -1;
        for ( int32 placementIndex = 0; placementIndex < static_cast<int32>( _listPlacement.size() ); ++placementIndex )
        {
            const RidePlacement& placement = _listPlacement[static_cast<size_t>( placementIndex )];
            int32&               cheapest  = placement._layoutID.empty() ? cheapestFlat : cheapestCoaster;
            if ( cheapest < 0 || placement._buildCost < _listPlacement[static_cast<size_t>( cheapest )]._buildCost )
                cheapest = placementIndex;
        }
        if ( cheapestFlat >= 0 )
            (void)buildPlacement( cheapestFlat );
        if ( cheapestCoaster >= 0 )
            (void)buildPlacement( cheapestCoaster );
        ++_parkRevision;
        startStatusLog( _statusLogInterval );
        return true;
    }

    bool ParkGameModeComponent::buildPlacement( int32 placementIndex )
    {
        if ( placementIndex < 0 || placementIndex >= static_cast<int32>( _listPlacement.size() ) )
            return false;
        RidePlacement& placement = _listPlacement[static_cast<size_t>( placementIndex )];
        if ( placement._rideIndex >= 0 )
            return false;
        if ( _wallet.canAfford( _settings._currency, placement._buildCost ) == false )
        {
            SW_LOG_INFO_ID( "ParkGameMode.NotEnoughCashNeeded", "[Park] not enough cash for %# ($%# needed, $%# in the bank)", placement._ride._name.c_str(), placement._buildCost, _wallet.getBalance( _settings._currency ) );
            getSoundQueue().queueEvent( ParkGameModeComponentInternal::kEventBlocked );
            return false;
        }

        ParkRide                   ride = placement._ride;
        unique_ptr<CoasterRuntime> pCoaster;
        if ( placement._layoutID.empty() == false )
        {
            // 코스터는 건설하고 테스트 운행으로 평가를 받는다. 못 돌면(언덕을 못 넘는다) 건설하지 않는다.
            const CoasterLayoutDef* pLayout = _pLayoutCatalog->find( placement._layoutID );
            CoasterRideStats        stats{};
            pCoaster = createCoaster( placementIndex, stats );
            if ( pLayout == nullptr || pCoaster == nullptr )
                return false;
            if ( stats._bCompleted == SW_FALSE )
            {
                SW_LOG_WARNING_ID( "ParkGameMode.FailedTestRunStalled", "[Park] %# failed its test run (stalled) - not built", pLayout->_name.c_str() );
                return false;
            }
            ride = ThemeParkSimulation::makeRideFromCoaster( placement._layoutID, pLayout->_name, stats, placement._ride._capacity, placement._loadTime );
            // 줄 입구는 스테이션 중앙 옆(바깥쪽)이다.
            const CoasterTrackFrame stationFrame = pCoaster->_pTrack->sample( ParkGameModeComponentInternal::kStationCenterDistance );
            ride._entrance                       = stationFrame._position - stationFrame._right * ParkGameModeComponentInternal::kEntranceSideDistance;
            ride._entrance._y                    = placement._position._y;
            ride._runningCostPerMinute           = ParkGameModeComponentInternal::kCoasterBaseRunningCost + static_cast<int32>( stats._length / ParkGameModeComponentInternal::kTrackPerRunningCost );
            SW_LOG_INFO_ID( "ParkGameMode.TestRunMLap", "[Park] test run of %#: %# m, %# s lap, top speed %# m/s, %# inversions, airtime %# s -> excitement %# · intensity %# · nausea %#",
                            pLayout->_name.c_str(), static_cast<int32>( stats._length ), static_cast<int32>( stats._lapTime ), static_cast<int32>( stats._maxSpeed ),
                            stats._inversionCount, stats._airtime, stats._excitement, stats._intensity, stats._nausea );
        }

        const int32 rideIndex = _simulation.buildRide( ride, placement._buildCost );
        if ( rideIndex < 0 )
            return false;
        placement._rideIndex = rideIndex;
        placement._ride      = ride;
        if ( pCoaster != nullptr )
            _listCoaster.push_back( std::move( pCoaster ) );
        SW_LOG_INFO_ID( "ParkGameMode.BuiltTicketLeft", "[Park] built %# for $%# - ticket $%#, $%# left", ride._name.c_str(), placement._buildCost, ride._price, _wallet.getBalance( _settings._currency ) );
        getSoundQueue().queueEvent( ParkGameModeComponentInternal::kEventBuilt );
        return true;
    }

    unique_ptr<ParkGameModeComponent::CoasterRuntime> ParkGameModeComponent::createCoaster( int32 placementIndex, CoasterRideStats& outStats ) const
    {
        if ( placementIndex < 0 || placementIndex >= static_cast<int32>( _listPlacement.size() ) )
            return nullptr;
        const RidePlacement&    placement = _listPlacement[static_cast<size_t>( placementIndex )];
        const CoasterLayoutDef* pLayout   = _pLayoutCatalog->find( placement._layoutID );
        if ( pLayout == nullptr )
            return nullptr;
        CoasterTrackBuilder builder;
        builder.reset( placement._position + float3{ 0.0f, pLayout->_startHeight, 0.0f }, placement._heading );
        builder.appendPieces( pLayout->_listPiece );
        unique_ptr<CoasterRuntime> pCoaster = make_unique<CoasterRuntime>();
        pCoaster->_pTrack                   = make_unique<CoasterTrack>( builder.makeTrack( true ) );
        pCoaster->_placementIndex           = placementIndex;
        const CoasterPhysicsParams& physics = _pLayoutCatalog->getPhysics(); // 규칙 자산(`coasters.xml` 의 `<Physics>` · `<Rating>`)
        outStats                            = CoasterRideStats::analyze( *pCoaster->_pTrack, physics );
        if ( pCoaster->_train.initialize( pCoaster->_pTrack.get(), physics, 0.0f ) == false )
            return nullptr;
        return pCoaster;
    }

    // ------------------------------------------------------------------------------
    // 상태 쓰기 · 복원하기(핫 리로드 · 세이브)
    // ------------------------------------------------------------------------------
    void ParkGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( ParkGameModeComponentInternal::kStateFormat );
        if ( io.isOk() == false )
            return;
        ThemeParkSimulation restoredSimulation;
        if ( io.isReading() && io.check( restoredSimulation.initialize( _settings, _wallet, _walkGrid ) ) == false )
            return;
        ThemeParkSimulation& simulation = io.isWriting() ? _simulation : restoredSimulation;
        io.value( simulation );
        uint32 placementCount = static_cast<uint32>( _listPlacement.size() );
        io.count( sizeof( int32 ), placementCount );
        // 배치 데이터가 바뀌었으면 건설한 것을 맞출 수 없다.
        if ( io.check( placementCount == _listPlacement.size() ) == false )
            return;
        const int32   rideCount = static_cast<int32>( simulation.getRides().size() );
        vector<int32> listRideIndex( placementCount );
        for ( uint32 index = 0; index < placementCount; ++index )
        {
            listRideIndex[index] = _listPlacement[index]._rideIndex;
            io.value( listRideIndex[index] );
            io.check( -1 <= listRideIndex[index] && listRideIndex[index] < rideCount );
        }
        uint32 coasterCount = static_cast<uint32>( _listCoaster.size() );
        io.count( sizeof( int32 ) + sizeof( float32 ) * 2, coasterCount );
        if ( io.isOk() == false )
            return;
        vector<unique_ptr<CoasterRuntime>> listCoaster;
        for ( uint32 coasterIndex = 0; coasterIndex < coasterCount; ++coasterIndex )
        {
            int32   placementIndex = io.isWriting() ? _listCoaster[coasterIndex]->_placementIndex : -1;
            float32 distance       = io.isWriting() ? _listCoaster[coasterIndex]->_train.getDistance() : 0.0f;
            float32 speed          = io.isWriting() ? _listCoaster[coasterIndex]->_train.getSpeed() : 0.0f;
            io.value( placementIndex );
            io.value( distance );
            io.value( speed );
            if ( io.isWriting() )
                continue;
            CoasterRideStats           stats{};
            unique_ptr<CoasterRuntime> pCoaster = io.isOk() ? createCoaster( placementIndex, stats ) : nullptr;
            if ( io.check( pCoaster != nullptr ) == false )
                return;
            pCoaster->_train.setDistance( distance );
            pCoaster->_train.setSpeed( speed );
            listCoaster.push_back( std::move( pCoaster ) );
        }
        Wallet restoredWallet; // 두 시계는 상태 칸이다(`GameModeComponent::serializeState`)
        io.value( io.isWriting() ? _wallet : restoredWallet );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _simulation = std::move( restoredSimulation );
        _wallet     = std::move( restoredWallet );
        for ( uint32 placementIndex = 0; placementIndex < placementCount; ++placementIndex )
        {
            RidePlacement& placement = _listPlacement[placementIndex];
            placement._rideIndex     = listRideIndex[placementIndex];
            if ( placement._rideIndex >= 0 )
                placement._ride = _simulation.getRides()[static_cast<size_t>( placement._rideIndex )];
        }
        _listCoaster = std::move( listCoaster );
        ++_parkRevision;
    }

    void ParkGameModeComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored )
            SW_LOG_INFO_ID( "ParkGameMode.ParkStateRestoredGuests", "[Park] park state restored - %# guests, $%#, %#s open", _simulation.getGuestCount(), _wallet.getBalance( _settings._currency ),
                            static_cast<int32>( _simulation.getElapsedTime() ) );
        else
            SW_LOG_WARNING_ID( "ParkGameMode.SavedParkStateDoes", "[Park] the saved park state does not match this build - opening a new park" );
    }

    bool ParkGameModeComponent::buildCheapestRemaining()
    {
        int32 cheapest = -1;
        for ( int32 placementIndex = 0; placementIndex < static_cast<int32>( _listPlacement.size() ); ++placementIndex )
        {
            const RidePlacement& placement = _listPlacement[static_cast<size_t>( placementIndex )];
            if ( placement._rideIndex < 0 && ( cheapest < 0 || placement._buildCost < _listPlacement[static_cast<size_t>( cheapest )]._buildCost ) )
                cheapest = placementIndex;
        }
        if ( cheapest < 0 )
            return false;
        if ( _wallet.canAfford( _settings._currency, _listPlacement[static_cast<size_t>( cheapest )]._buildCost ) == false )
            return false;
        return buildPlacement( cheapest );
    }

    void ParkGameModeComponent::logStatus() const
    {
        SW_LOG_INFO_ID( "ParkGameMode.MinCashGuestsVisited", "[Park] %# min · cash $%# · guests %# (visited %#) · happiness %#%% · rating %# · rides %# · entry $%#",
                        static_cast<int32>( _simulation.getElapsedTime() / 60.0f ), _wallet.getBalance( _settings._currency ), _simulation.getGuestCount(),
                        _simulation.getTotalVisitorCount(), static_cast<int32>( _simulation.getAverageHappiness() * 100.0f ), _simulation.getParkRating(),
                        static_cast<uint32>( _simulation.getRides().size() ), _simulation.getSettings()._entryFee );
        for ( [[maybe_unused]] const ParkRide& ride : _simulation.getRides() )
        {
            SW_LOG_INFO_ID( "ParkGameMode.QueueRidersTotalIncome", "[Park]   %# - %# · queue %# · riders %# · total %# · income $%# · ticket $%#", ride._name.c_str(), ride._bOpen != SW_FALSE ? "open" : "closed",
                            static_cast<uint32>( ride._listQueue.size() ), static_cast<uint32>( ride._listRider.size() ), ride._totalRiders, ride._totalIncome, ride._price );
        }
    }
} // namespace sw

namespace sw
{
    SW_AUTOMATION_COMPONENT_WATCH( parkGuestCount, "ThemePark.GuestCount", "Guests in the park of the first park game mode", ParkGameModeComponent, &ParkGameModeComponentInternal::readGuestCount );
} // namespace sw
