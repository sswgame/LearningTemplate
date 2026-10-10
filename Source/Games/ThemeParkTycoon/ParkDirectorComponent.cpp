#include "pch.h"

#include "Games/ThemeParkTycoon/ParkDirectorComponent.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/GameAutoplay.h"

#include "GameFramework/Base/Actor/Camera/OrthoCameraRigComponent.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/MaterialTintCache.h"
#include "GameFramework/Base/Foundation/Utility/Math/OrientationUtil.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

#include "Games/ThemeParkTycoon/CoasterCarComponent.h"
#include "Games/ThemeParkTycoon/FlatRideComponent.h"
#include "Games/ThemeParkTycoon/ParkGuestComponent.h"

namespace sw
{
    SW_LOG_CALLER( "ParkDirector" );

    namespace
    {
        struct ParkDirectorComponentInternal
        {
            static constexpr float32 kRailSpacing    = 2.0f; ///< 레일 조각 간격(m)
            static constexpr uint32  kStateTag       = FourCcUtil::make( "PARK" );
            static constexpr uint32  kStateVersion   = 2;
            static constexpr float32 kSupportSpacing = 8.0f; ///< 기둥 간격(m)
            /**
             * @brief Kenney Coaster Kit 모델의 배율입니다. 키트는 열차 폭 0.7 이고 이 공원의 차 폭은 1.4 다(차 · 레일 프리팹의 크기와 같다).
             * @details 레일 조각은 시작점에서 +Z 로 4 만큼 뻗고, 레일 윗면이 원점보다 0.7 아래다(열차 바닥이 원점). 그래서 레일은 트랙 점에서
             *          `_modelScale × 0.7` 만큼 올려 윗면을 트랙 점에 맞추고, 차는 바닥을 트랙 점에 둔다. 팔레트 텍스처라 늘려도 색이 번지지 않는다.
             */
            static constexpr float32 kRailPieceLength = 4.0f;
            static constexpr float32 kRailTopDepth    = 0.7f;
            static constexpr float32 kRailBottomDepth = 1.0f;

            static constexpr const utf8* kSoundError  = "game/themepark/sounds/error_001.ogg";
            static constexpr const utf8* kSoundBuilt  = "game/themepark/sounds/confirmation_001.ogg";
            static constexpr const utf8* kSoundSelect = "game/themepark/sounds/select_001.ogg";

            /** @brief 오브젝트의 메시를 자리 · 회전 · 크기로 둡니다. 메시가 없으면 nullptr 입니다. */
            static MeshComponent* placeMesh( GameObject* pObject, const float3& position, const float3& rotation )
            {
                MeshComponent* pMesh = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
                if ( pMesh == nullptr )
                    return nullptr;
                pMesh->setLocalPosition( position );
                pMesh->setLocalRotation( rotation );
                return pMesh;
            }
        };

        /** @brief 시나리오 탐침 — 첫 공원 디렉터의 손님 수(그림자 장면이 비어 있지 않다는 전제)입니다. */
        struct ParkDirectorProbeInternal
        {
            [[nodiscard]] static bool readGuestCount( const GameObjectManager* pManager, float64& outValue )
            {
                const ParkDirectorComponent* pFound = nullptr;
                if ( pManager == nullptr )
                    return false;
                pManager->forEachComponentOfType<ParkDirectorComponent>( [&pFound]( ParkDirectorComponent* pDirector )
                {
                    if ( pFound == nullptr )
                        pFound = pDirector;
                } );
                if ( pFound == nullptr )
                    return false;
                outValue = static_cast<float64>( pFound->getSimulation().getGuestCount() );
                return true;
            }
        };
    } // namespace

    /** @brief `-gv_parkAutoBuild=1` — 디렉터의 자동 짓기를 켭니다(씬의 `_bAutoBuild` 가 꺼져 있어도). 배포본 실행으로 입력 없이 공원이 크는 확인. */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_parkAutoBuild, 0, "ThemeParkTycoon: 돈이 모이면 자동으로 짓기 (1=켜기)" );
    SW_GAME_AUTOPLAY( gv_parkAutoBuild, "ThemeParkTycoon", "Build the next ride whenever cash allows" );
} // namespace sw

namespace sw
{
    ParkDirectorComponent::ParkDirectorComponent()
        : _coasterDataPath{ "game/themepark/data/coasters.xml" }
        , _parkDataPath{ "game/themepark/data/rides.xml" }
        , _guestPrefab{}
        , _carFrontPrefab{}
        , _carPrefab{}
        , _railPrefab{}
        , _supportPrefab{}
        , _stationPrefab{}
        , _rideEntrancePrefab{}
        , _pathPrefab{}
        , _flatRidePrefab{}
        , _happyColor{ 0.3f, 0.85f, 0.35f, 1.0f }
        , _contentColor{ 0.95f, 0.85f, 0.25f, 1.0f }
        , _unhappyColor{ 0.9f, 0.25f, 0.2f, 1.0f }
        , _cameraRig{}
        , _gate{}
        , _autoBuildInterval{ 20.0f }
        , _statusLogInterval{ 10.0f }
        , _carCount{ 4 }
        , _modelScale{ 2.0f }
        , _rideEyeHeight{ 1.6f }
        , _rideFieldOfView{ 85.0f * MathUtil::kDegreeToRadian }
        , _rideFarPlane{ 600.0f }
        , _simulation{}
        , _wallet{}
        , _layoutCatalog{}
        , _settings{}
        , _listPlacement{}
        , _listCoaster{}
        , _listPendingRideView{}
        , _tintCache{}
        , _arrGuestLook{}
        , _statusTimer{ 0.0f }
        , _autoBuildTimer{ 0.0f }
        , _startingCash{ 12000 }
        , _selectedPlacement{ 0 }
        , _ridingCoaster{ -1 }
        , _bWasRiding{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ParkDirectorComponent::~ParkDirectorComponent() = default;

    void ParkDirectorComponent::onGameStarted()
    {
        SW_LOG_INFO( "[Park] park is open - WASD pan, Q/E rotate, wheel zoom, Tab select ride, O open/close, [ ] ride price, - = entry fee, "
                     "B build next, V ride the coaster, G guest thoughts" );
        logStatus( 0.0f, true );
    }

    void ParkDirectorComponent::tickGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;

        _simulation.update( deltaTime );
        const InputManager* pInput = game::getService<InputManager>();
        if ( pInput != nullptr )
            updateInput( *pInput );
        if ( isAutoPlayOn() )
        {
            _autoBuildTimer += deltaTime;
            if ( _autoBuildTimer >= _autoBuildInterval )
            {
                _autoBuildTimer = 0.0f;
                (void)buildCheapestRemaining();
            }
        }
        updateRides( deltaTime );
        updateCameraOverride();
        logStatus( deltaTime, false );
    }

    void ParkDirectorComponent::onViewsDespawned()
    {
        _listPendingRideView.clear();
    }

    const shared_ptr<MaterialInstance>& ParkDirectorComponent::getGuestLook( int32 bucket ) const
    {
        return _arrGuestLook[MathUtil::clamp( bucket, 0, 2 )];
    }

    const CoasterTrain* ParkDirectorComponent::findCoasterTrain( int32 coasterIndex ) const
    {
        if ( coasterIndex < 0 || coasterIndex >= static_cast<int32>( _listCoaster.size() ) )
            return nullptr;
        // 뷰가 워커에서 부른다 — 컨테이너 첨자 대신 포인터로 읽는다.
        const CoasterRuntime* pCoaster = _listCoaster.data()[coasterIndex].get();
        return pCoaster != nullptr ? &pCoaster->_train : nullptr;
    }

    int32 ParkDirectorComponent::computeHappinessBucket( float32 happiness )
    {
        if ( happiness >= 0.6f )
            return 0;
        return happiness >= 0.35f ? 1 : 2;
    }

    // ------------------------------------------------------------------------------
    // 데이터 · 짓기
    // ------------------------------------------------------------------------------
    bool ParkDirectorComponent::startGame()
    {
        ParkLayout layout;
        if ( _layoutCatalog.loadFromResource( _coasterDataPath ) == false || layout.loadFromResource( _parkDataPath, _layoutCatalog ) == false )
        {
            SW_LOG_WARNING( "[Park] park data could not be loaded - the park cannot open" );
            return false;
        }
        _settings._gatePosition = layout.getGatePosition();
        _settings._entryFee     = layout.getEntryFee();
        _startingCash           = layout.getStartingCash();
        // 씬에 놓인 정문이 있으면 그 자리가 정문이다 — 에디터에서 옮기면 손님 길이 따라온다.
        GameObjectManager*    pManager   = getObjectManager();
        const GameObject*     pGate      = pManager != nullptr ? pManager->resolveGameObject( _gate ) : nullptr;
        const SceneComponent* pGateScene = pGate != nullptr ? pGate->getPrimarySceneComponent() : nullptr;
        if ( pGateScene != nullptr )
            _settings._gatePosition = pGateScene->getWorldPosition();
        _listPlacement.clear();
        _listPlacement.reserve( layout.getPlacements().size() );
        for ( const ParkRidePlacement& placement : layout.getPlacements() )
        {
            RidePlacement ride{};
            static_cast<ParkRidePlacement&>( ride ) = placement;
            _listPlacement.push_back( ride );
        }
        if ( _listPlacement.empty() )
        {
            SW_LOG_WARNING( "[Park] %# places no rides - the park cannot open", _parkDataPath.c_str() );
            return false;
        }
        _wallet.clear();
        _wallet.add( _settings._currency, _startingCash );
        _simulation.initialize( _settings, makeRefs() );
        _listCoaster.clear();

        // 처음 공원 — 가장 싼 평지 놀이기구 하나와 가장 싼 코스터 하나.
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
        return true;
    }

    bool ParkDirectorComponent::buildPlacement( int32 placementIndex )
    {
        if ( placementIndex < 0 || placementIndex >= static_cast<int32>( _listPlacement.size() ) )
            return false;
        RidePlacement& placement = _listPlacement[static_cast<size_t>( placementIndex )];
        if ( placement._rideIndex >= 0 )
            return false;
        if ( _wallet.canAfford( _settings._currency, placement._buildCost ) == false )
        {
            SW_LOG_INFO( "[Park] not enough cash for %# ($%# needed, $%# in the bank)", placement._ride._name.c_str(), placement._buildCost, _wallet.getBalance( _settings._currency ) );
            getSoundQueue().queueClip( ParkDirectorComponentInternal::kSoundError );
            return false;
        }

        ParkRide                   ride = placement._ride;
        unique_ptr<CoasterRuntime> pCoaster;
        if ( placement._layoutID.empty() == false )
        {
            // 코스터 — 짓고 시험 운행으로 평가를 받는다. 못 돌면(언덕을 못 넘는다) 짓지 않는다.
            const CoasterLayoutDef* pLayout = _layoutCatalog.findLayout( placement._layoutID );
            CoasterRideStats        stats{};
            pCoaster = createCoaster( placementIndex, stats );
            if ( pLayout == nullptr || pCoaster == nullptr )
                return false;
            if ( stats._bCompleted == SW_FALSE )
            {
                SW_LOG_WARNING( "[Park] %# failed its test run (stalled) - not built", pLayout->_name.c_str() );
                return false;
            }
            ride = ThemeParkSimulation::makeRideFromCoaster( placement._layoutID, pLayout->_name, stats, placement._ride._capacity, placement._loadTime );
            // 줄 입구 — 스테이션 가운데 옆(바깥쪽).
            const CoasterTrackFrame stationFrame = pCoaster->_pTrack->sample( 6.0f );
            ride._entrance                       = stationFrame._position - stationFrame._right * 5.0f;
            ride._entrance._y                    = 0.0f;
            ride._runningCostPerMinute           = 8 + static_cast<int32>( stats._length / 60.0f );
            SW_LOG_INFO( "[Park] test run of %#: %# m, %# s lap, top speed %# m/s, %# inversions, airtime %# s -> excitement %# · intensity %# · nausea %#",
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
        SW_LOG_INFO( "[Park] built %# for $%# - ticket $%#, $%# left", ride._name.c_str(), placement._buildCost, ride._price, _wallet.getBalance( _settings._currency ) );
        getSoundQueue().queueClip( ParkDirectorComponentInternal::kSoundBuilt );
        _listPendingRideView.push_back( placementIndex );
        return true;
    }

    unique_ptr<ParkDirectorComponent::CoasterRuntime> ParkDirectorComponent::createCoaster( int32 placementIndex, CoasterRideStats& outStats ) const
    {
        if ( placementIndex < 0 || placementIndex >= static_cast<int32>( _listPlacement.size() ) )
            return nullptr;
        const RidePlacement&    placement = _listPlacement[static_cast<size_t>( placementIndex )];
        const CoasterLayoutDef* pLayout   = _layoutCatalog.findLayout( placement._layoutID );
        if ( pLayout == nullptr )
            return nullptr;
        CoasterTrackBuilder builder;
        builder.reset( placement._position + float3{ 0.0f, pLayout->_startHeight, 0.0f }, placement._heading );
        builder.appendPieces( pLayout->_listPiece );
        unique_ptr<CoasterRuntime> pCoaster = make_unique<CoasterRuntime>();
        pCoaster->_pTrack                   = make_unique<CoasterTrack>( builder.makeTrack( true ) );
        pCoaster->_placementIndex           = placementIndex;
        outStats                            = CoasterRideAnalyzer::analyze( *pCoaster->_pTrack, CoasterPhysicsParams{} );
        pCoaster->_train.initialize( pCoaster->_pTrack.get(), CoasterPhysicsParams{}, 0.0f );
        return pCoaster;
    }

    // ------------------------------------------------------------------------------
    // 상태 쓰기 · 되살리기(핫 리로드 · 세이브)
    // ------------------------------------------------------------------------------
    void ParkDirectorComponent::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, ParkDirectorComponentInternal::kStateTag, ParkDirectorComponentInternal::kStateVersion );
        _simulation.writeState( outArchive );
        outArchive << static_cast<uint32>( _listPlacement.size() );
        for ( const RidePlacement& placement : _listPlacement )
        {
            outArchive << placement._rideIndex;
        }
        outArchive << static_cast<uint32>( _listCoaster.size() );
        for ( const unique_ptr<CoasterRuntime>& pCoaster : _listCoaster )
        {
            outArchive << pCoaster->_placementIndex;
            outArchive << pCoaster->_train.getDistance();
            outArchive << pCoaster->_train.getSpeed();
        }
        outArchive << _statusTimer;
        outArchive << _autoBuildTimer;
        outArchive << _selectedPlacement;
        _wallet.writeState( outArchive );
    }

    bool ParkDirectorComponent::readState( Archive& archive )
    {
        if ( StateArchiveUtil::readHeader( archive, ParkDirectorComponentInternal::kStateTag, ParkDirectorComponentInternal::kStateVersion ) == false )
            return false;
        ThemeParkSimulation simulation;
        simulation.initialize( _settings, makeRefs() );
        uint32 placementCount = 0;
        if ( simulation.readState( archive ) == false || StateArchiveUtil::readCount( archive, sizeof( int32 ), placementCount ) == false )
            return false;
        if ( placementCount != static_cast<uint32>( _listPlacement.size() ) )
            return false; // 배치 데이터가 바뀌었다 — 지은 것을 맞출 수 없다
        const int32   rideCount = static_cast<int32>( simulation.getRides().size() );
        vector<int32> listRideIndex( placementCount );
        for ( int32& rideIndex : listRideIndex )
        {
            archive >> rideIndex;
            if ( rideIndex < -1 || rideIndex >= rideCount )
                archive.setError();
        }
        uint32 coasterCount = 0;
        if ( archive.isError() || StateArchiveUtil::readCount( archive, sizeof( int32 ) + sizeof( float32 ) * 2, coasterCount ) == false )
            return false;
        vector<unique_ptr<CoasterRuntime>> listCoaster;
        for ( uint32 coasterIndex = 0; coasterIndex < coasterCount; ++coasterIndex )
        {
            int32   placementIndex = -1;
            float32 distance       = 0.0f;
            float32 speed          = 0.0f;
            archive >> placementIndex;
            archive >> distance;
            archive >> speed;
            CoasterRideStats           stats{};
            unique_ptr<CoasterRuntime> pCoaster = archive.isOk() ? createCoaster( placementIndex, stats ) : nullptr;
            if ( pCoaster == nullptr )
                return false;
            pCoaster->_train.setDistance( distance );
            pCoaster->_train.setSpeed( speed );
            listCoaster.push_back( std::move( pCoaster ) );
        }
        float32 statusTimer       = 0.0f;
        float32 autoBuildTimer    = 0.0f;
        int32   selectedPlacement = 0;
        archive >> statusTimer;
        archive >> autoBuildTimer;
        archive >> selectedPlacement;
        Wallet wallet;
        if ( archive.isError() || wallet.readState( archive ) == false || archive.getRemainingBytes() != 0 )
            return false;

        _simulation = std::move( simulation );
        _wallet     = std::move( wallet );
        for ( uint32 placementIndex = 0; placementIndex < placementCount; ++placementIndex )
        {
            RidePlacement& placement = _listPlacement[placementIndex];
            placement._rideIndex     = listRideIndex[placementIndex];
            if ( placement._rideIndex >= 0 )
                placement._ride = _simulation.getRides()[static_cast<size_t>( placement._rideIndex )];
        }
        _listCoaster       = std::move( listCoaster );
        _statusTimer       = statusTimer;
        _autoBuildTimer    = autoBuildTimer;
        _selectedPlacement = MathUtil::clamp( selectedPlacement, 0, static_cast<int32>( placementCount ) - 1 );
        _ridingCoaster     = -1;
        return true;
    }

    void ParkDirectorComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored )
            SW_LOG_INFO( "[Park] park state restored - %# guests, $%#, %#s open", _simulation.getGuestCount(), _wallet.getBalance( _settings._currency ),
                         static_cast<int32>( _simulation.getElapsedTime() ) );
        else
            SW_LOG_WARNING( "[Park] the saved park state does not match this build - opening a new park" );
    }

    GameStateRefs ParkDirectorComponent::makeRefs()
    {
        GameStateRefs refs;
        refs._pWallet = &_wallet;
        return refs;
    }

    bool ParkDirectorComponent::buildCheapestRemaining()
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

    // ------------------------------------------------------------------------------
    // 스폰(틱 뒤 · 게임 스레드)
    // ------------------------------------------------------------------------------
    void ParkDirectorComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews )
        {
            // 처음(또는 걷은 뒤) — 손님 풀과 지은 것 모두. 쌓인 개별 요청은 여기에 들어 있다.
            spawnGuestPool( manager );
            for ( int32 placementIndex = 0; placementIndex < static_cast<int32>( _listPlacement.size() ); ++placementIndex )
            {
                if ( _listPlacement[static_cast<size_t>( placementIndex )]._rideIndex >= 0 )
                    spawnRideView( manager, placementIndex );
            }
        }
        else
        {
            for ( const int32 placementIndex : _listPendingRideView )
            {
                spawnRideView( manager, placementIndex );
            }
        }
        _listPendingRideView.clear();
    }

    void ParkDirectorComponent::spawnGuestPool( GameObjectManager& manager )
    {
        // 최대 손님 수만큼 미리 세우고 숨긴다(오브젝트를 매 프레임 만들고 지우지 않는다). 손님 하나는 뷰 컴포넌트 하나가 시뮬레이션의 같은 번호를 따라간다.
        const GameObjectHandle director = getOwner()->getHandle();
        for ( int32 guestIndex = 0; guestIndex < _settings._maxGuests; ++guestIndex )
        {
            GameObject*    pGuest = spawnPrefab( manager, _guestPrefab, "ParkGuest" );
            MeshComponent* pMesh  = ParkDirectorComponentInternal::placeMesh( pGuest, _settings._gatePosition, float3{ 0.0f, 0.0f, 0.0f } );
            if ( pMesh == nullptr )
                continue;
            if ( _arrGuestLook[0] == nullptr && pMesh->getMaterial() != nullptr )
            {
                const float4 arrColor[3] = { _happyColor, _contentColor, _unhappyColor };
                for ( int32 bucket = 0; bucket < 3; ++bucket )
                {
                    _arrGuestLook[bucket] = MaterialInstance::create( pMesh->getMaterial() );
                    if ( _arrGuestLook[bucket] != nullptr )
                        _arrGuestLook[bucket]->setVectorParameter( hashed_string( kMaterialColorParameter ), arrColor[bucket] );
                }
            }
            if ( _arrGuestLook[0] != nullptr )
                pMesh->setMaterialInstance( _arrGuestLook[0] );
            pMesh->setVisible( false );
            ParkGuestComponent* pView = pGuest->getComponent<ParkGuestComponent>();
            if ( pView != nullptr )
                pView->assignGuest( director, guestIndex );
        }
    }

    void ParkDirectorComponent::spawnRideView( GameObjectManager& manager, int32 placementIndex )
    {
        using Internal                 = ParkDirectorComponentInternal;
        const RidePlacement& placement = _listPlacement[static_cast<size_t>( placementIndex )];
        spawnPath( manager, _settings._gatePosition, placement._ride._entrance );
        (void)Internal::placeMesh( spawnPrefab( manager, _rideEntrancePrefab, "RideEntrance" ), placement._ride._entrance, float3{ 0.0f, 0.0f, 0.0f } );
        if ( placement._layoutID.empty() )
        {
            GameObject*    pObject = spawnPrefab( manager, _flatRidePrefab, "FlatRide" );
            MeshComponent* pMesh   = Internal::placeMesh( pObject, placement._position + float3{ 0.0f, placement._size._y * 0.5f, 0.0f }, float3{ 0.0f, 0.0f, 0.0f } );
            if ( pMesh == nullptr )
                return;
            pMesh->setMeshID( placement._shape );
            pMesh->setLocalScale( placement._size );
            _tintCache.apply( *pMesh, placement._color );
            FlatRideComponent* pView = pObject->getComponent<FlatRideComponent>();
            if ( pView != nullptr )
                pView->assignRide( getOwner()->getHandle(), placement._rideIndex, placement._spin );
            return;
        }
        for ( int32 coasterIndex = 0; coasterIndex < static_cast<int32>( _listCoaster.size() ); ++coasterIndex )
        {
            if ( _listCoaster[static_cast<size_t>( coasterIndex )]->_placementIndex == placementIndex )
                spawnCoasterView( manager, coasterIndex );
        }
    }

    void ParkDirectorComponent::spawnCoasterView( GameObjectManager& manager, int32 coasterIndex )
    {
        using Internal                = ParkDirectorComponentInternal;
        const CoasterRuntime& coaster = *_listCoaster[static_cast<size_t>( coasterIndex )];
        const CoasterTrack&   track   = *coaster._pTrack;

        // 레일 — 2 m 마다 트랙 좌표계로 돌린 직선 레일 조각(조각 시작점이 원점). 기둥 — 8 m 마다 레일 밑에서 땅까지(뒤집힌 구간은 없다).
        // 스테이션 구간은 레일 옆에 승강장을 깐다.
        const float3 railScale{ _modelScale, _modelScale, Internal::kRailSpacing * 1.05f / Internal::kRailPieceLength };
        const float3 stationScale{ 2.0f, 2.0f, Internal::kRailSpacing * 1.05f };
        float32      nextSupport = 0.0f;
        for ( float32 distance = 0.0f; distance < track.getLength(); distance += Internal::kRailSpacing )
        {
            const CoasterTrackFrame frame    = track.sample( distance );
            const float3            rotation = OrientationUtil::computeEulerFromForwardUp( frame._forward, frame._up );
            const float3            railLift = frame._up * ( _modelScale * Internal::kRailTopDepth );
            MeshComponent*          pRail    = Internal::placeMesh( spawnPrefab( manager, _railPrefab, "CoasterRail" ), frame._position + railLift, rotation );
            if ( pRail != nullptr )
                pRail->setLocalScale( railScale );
            if ( ( frame._flags & CoasterSegmentFlag::kStation ) != 0 )
            {
                MeshComponent* pStation =
                    Internal::placeMesh( spawnPrefab( manager, _stationPrefab, "CoasterStation" ), frame._position - frame._right * 2.2f - railLift, rotation );
                if ( pStation != nullptr )
                    pStation->setLocalScale( stationScale );
            }
            const float32 railBottom = frame._position._y - _modelScale * ( Internal::kRailBottomDepth - Internal::kRailTopDepth );
            const bool    bSupported = distance >= nextSupport && frame._up._y > 0.6f && railBottom > 1.5f;
            if ( bSupported )
            {
                nextSupport             = distance + Internal::kSupportSpacing;
                MeshComponent* pSupport = Internal::placeMesh( spawnPrefab( manager, _supportPrefab, "CoasterSupport" ),
                                                               float3{ frame._position._x, 0.0f, frame._position._z }, float3{ 0.0f, 0.0f, 0.0f } );
                if ( pSupport != nullptr )
                    pSupport->setLocalScale( float3{ _modelScale, railBottom, _modelScale } );
            }
        }
        const GameObjectHandle director = getOwner()->getHandle();
        for ( uint32 carIndex = 0; carIndex < _carCount; ++carIndex )
        {
            GameObject* pCar = spawnPrefab( manager, carIndex == 0 ? _carFrontPrefab : _carPrefab, "CoasterCar" );
            (void)Internal::placeMesh( pCar, coaster._train.getFrame()._position, float3{ 0.0f, 0.0f, 0.0f } );
            CoasterCarComponent* pView = pCar != nullptr ? pCar->getComponent<CoasterCarComponent>() : nullptr;
            if ( pView != nullptr )
                pView->assignCar( director, coasterIndex, static_cast<int32>( carIndex ) );
        }
    }

    void ParkDirectorComponent::spawnPath( GameObjectManager& manager, const float3& from, const float3& to )
    {
        const float3  delta  = float3{ to._x - from._x, 0.0f, to._z - from._z };
        const float32 length = delta.getLength();
        if ( length < 0.5f )
            return;
        const float32  yaw   = MathUtil::atan2( delta._x, delta._z );
        MeshComponent* pPath = ParkDirectorComponentInternal::placeMesh( spawnPrefab( manager, _pathPrefab, "ParkPath" ),
                                                                         float3{ ( from._x + to._x ) * 0.5f, 0.0f, ( from._z + to._z ) * 0.5f }, float3{ 0.0f, yaw, 0.0f } );
        if ( pPath != nullptr )
            pPath->setLocalScale( float3{ 2.4f, 0.3f, length + 2.4f } );
    }

    // ------------------------------------------------------------------------------
    // 갱신(PrePhysics — 워커)
    // ------------------------------------------------------------------------------
    void ParkDirectorComponent::updateInput( const InputManager& input )
    {
        // 키는 입력 맵(`data/park.input.xml`)이 정한다.
        const InputMap& inputMap = input.getInputMap();
        if ( inputMap.wasActionTriggered( hashed_string( "Park.NextRide" ) ) && _listPlacement.empty() == false )
        {
            _selectedPlacement = ( _selectedPlacement + 1 ) % static_cast<int32>( _listPlacement.size() );
            getSoundQueue().queueClip( ParkDirectorComponentInternal::kSoundSelect );
            [[maybe_unused]] const RidePlacement& placement = _listPlacement[static_cast<size_t>( _selectedPlacement )];
            SW_LOG_INFO( "[Park] selected %# (%#)", placement._ride._name.c_str(), placement._rideIndex >= 0 ? "built" : "not built - B to build" );
        }
        const int32 rideIndex = findSelectedRideIndex();
        if ( rideIndex >= 0 )
        {
            const ParkRide& ride = _simulation.getRides()[static_cast<size_t>( rideIndex )];
            if ( inputMap.wasActionTriggered( hashed_string( "Park.ToggleOpen" ) ) )
            {
                _simulation.setRideOpen( rideIndex, ride._bOpen == SW_FALSE );
                SW_LOG_INFO( "[Park] %# is now %#", ride._name.c_str(), ride._bOpen != SW_FALSE ? "open" : "closed" );
            }
            const bool bPriceUp = inputMap.wasActionTriggered( hashed_string( "Park.PriceUp" ) );
            if ( bPriceUp || inputMap.wasActionTriggered( hashed_string( "Park.PriceDown" ) ) )
            {
                _simulation.setRidePrice( rideIndex, ride._price + ( bPriceUp ? 1 : -1 ) );
                SW_LOG_INFO( "[Park] %# ticket $%# (worth about $%#)", ride._name.c_str(), ride._price, static_cast<int32>( ThemeParkSimulation::computeRideValue( ride ) ) );
            }
        }
        const bool bFeeUp = inputMap.wasActionTriggered( hashed_string( "Park.FeeUp" ) );
        if ( bFeeUp || inputMap.wasActionTriggered( hashed_string( "Park.FeeDown" ) ) )
        {
            _simulation.setEntryFee( _simulation.getSettings()._entryFee + ( bFeeUp ? 5 : -5 ) );
            SW_LOG_INFO( "[Park] entry fee $%#", _simulation.getSettings()._entryFee );
        }
        if ( inputMap.wasActionTriggered( hashed_string( "Park.Build" ) ) )
        {
            if ( _listPlacement[static_cast<size_t>( _selectedPlacement )]._rideIndex < 0 )
                (void)buildPlacement( _selectedPlacement );
            else
                (void)buildCheapestRemaining();
        }
        if ( inputMap.wasActionTriggered( hashed_string( "Park.Ride" ) ) )
        {
            // 타고 있으면 내린다. 아니면 고른 코스터(고른 것이 코스터가 아니면 첫 코스터)에 탄다.
            if ( _ridingCoaster >= 0 )
                _ridingCoaster = -1;
            else if ( _listCoaster.empty() == false )
            {
                _ridingCoaster = 0;
                for ( int32 coasterIndex = 0; coasterIndex < static_cast<int32>( _listCoaster.size() ); ++coasterIndex )
                {
                    if ( _listCoaster[static_cast<size_t>( coasterIndex )]->_placementIndex == _selectedPlacement )
                        _ridingCoaster = coasterIndex;
                }
            }
            SW_LOG_INFO( "[Park] %#", _ridingCoaster >= 0 ? "riding the coaster - V again to get off" : "back to the park view" );
        }
        if ( inputMap.wasActionTriggered( hashed_string( "Park.Thoughts" ) ) )
            logThoughts();
        if ( inputMap.wasActionTriggered( hashed_string( "Park.Status" ) ) )
            logStatus( 0.0f, true );
    }

    void ParkDirectorComponent::updateRides( float32 deltaTime )
    {
        // 열차만 나아간다. 차 · 평지 놀이기구의 모습은 뷰가 PostUpdate 에서 이 상태를 읽어 맞춘다.
        for ( unique_ptr<CoasterRuntime>& pCoaster : _listCoaster )
        {
            pCoaster->_train.step( deltaTime );
        }
    }

    void ParkDirectorComponent::updateCameraOverride()
    {
        // 리그는 PostUpdate 에서 돈다 — 이 그룹(PrePhysics)에서 넣은 시점을 같은 프레임에 읽는다. 리그의 오브젝트에는 다른 쓰기가 없다.
        const bool bRiding = 0 <= _ridingCoaster && _ridingCoaster < static_cast<int32>( _listCoaster.size() );
        if ( bRiding == false && _bWasRiding == SW_FALSE )
            return;
        GameObjectManager*       pManager   = getObjectManager();
        const GameObject*        pRigObject = pManager != nullptr ? pManager->resolveGameObject( _cameraRig ) : nullptr;
        OrthoCameraRigComponent* pRig       = pRigObject != nullptr ? pRigObject->getComponent<OrthoCameraRigComponent>() : nullptr;
        _bWasRiding                         = bRiding ? SW_TRUE : SW_FALSE;
        if ( pRig == nullptr )
            return;
        if ( bRiding == false )
        {
            pRig->clearViewOverride();
            return;
        }
        // 맨 앞 차량에 탄 시점 — 롤까지 따라간다(루프에서 하늘이 아래로 온다).
        const CoasterTrackFrame frame = _listCoaster[static_cast<size_t>( _ridingCoaster )]->_train.getFrame();
        pRig->setViewOverride( frame._position + frame._up * _rideEyeHeight,
                               OrientationUtil::computeEulerFromForwardUp( frame._forward, frame._up ), _rideFieldOfView,
                               _rideFarPlane );
    }

    void ParkDirectorComponent::logStatus( float32 deltaTime, bool bForce )
    {
        _statusTimer += deltaTime;
        if ( bForce == false && _statusTimer < _statusLogInterval )
            return;
        _statusTimer = 0.0f;
        SW_LOG_INFO( "[Park] %# min · cash $%# · guests %# (visited %#) · happiness %#%% · rating %# · rides %# · entry $%#",
                     static_cast<int32>( _simulation.getElapsedTime() / 60.0f ), _wallet.getBalance( _settings._currency ), _simulation.getGuestCount(),
                     _simulation.getTotalVisitorCount(), static_cast<int32>( _simulation.getAverageHappiness() * 100.0f ), _simulation.getParkRating(),
                     static_cast<uint32>( _simulation.getRides().size() ), _simulation.getSettings()._entryFee );
        for ( [[maybe_unused]] const ParkRide& ride : _simulation.getRides() )
        {
            SW_LOG_INFO( "[Park]   %# - %# · queue %# · riders %# · total %# · income $%# · ticket $%#", ride._name.c_str(), ride._bOpen != SW_FALSE ? "open" : "closed",
                         static_cast<uint32>( ride._listQueue.size() ), static_cast<uint32>( ride._listRider.size() ), ride._totalRiders, ride._totalIncome, ride._price );
        }
    }

    void ParkDirectorComponent::logThoughts() const
    {
        constexpr ParkGuestThought kArrThought[] = { ParkGuestThought::GreatRide, ParkGuestThought::TooIntense, ParkGuestThought::TooTame,
                                                     ParkGuestThought::TooExpensive, ParkGuestThought::QueueTooLong, ParkGuestThought::Sick,
                                                     ParkGuestThought::Tired, ParkGuestThought::OutOfCash, ParkGuestThought::NothingToRide };
        for ( const ParkGuestThought thought : kArrThought )
        {
            const uint32 count = _simulation.countGuestsThinking( thought );
            if ( count > 0 )
                SW_LOG_INFO( "[Park] %# guests think: %#", count, toString( thought ) );
        }
    }

    int32 ParkDirectorComponent::findSelectedRideIndex() const
    {
        if ( _selectedPlacement < 0 || _selectedPlacement >= static_cast<int32>( _listPlacement.size() ) )
            return -1;
        return _listPlacement[static_cast<size_t>( _selectedPlacement )]._rideIndex;
    }
} // namespace sw

namespace sw
{
    SW_AUTOMATION_PROBE( parkGuestCount, "ThemePark.GuestCount", "Guests in the park of the first park director", &ParkDirectorProbeInternal::readGuestCount );
} // namespace sw
