#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Client/ParkViewComponent.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/OrientationUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/GameObject/WorldOrigin.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Client/CoasterCarComponent.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/Client/FlatRideComponent.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/Client/ParkGuestComponent.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/Component/ParkGameModeComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/GameSound.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/StatefulComponentRegistrar.h"

namespace sw
{
    namespace
    {
        /** @brief 상태를 싣기 전에 세운 모습을 걷습니다 — 씬 섹션에 실리면 복원한 뒤 다시 세운 것과 겹친다. */
        const StatefulComponentRegistrar<ParkViewComponent> s_viewRegistration{ "ParkViewComponent" };

        struct ParkViewComponentInternal
        {
            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 라이브러리(`*.audioevents.xml`)가 정한다.
            static constexpr const utf8* kEventSelect = "ParkSelect";

            static constexpr float32 kHappyThreshold    = 0.6f;
            static constexpr float32 kContentThreshold  = 0.35f;
            static constexpr float32 kPieceOverlap      = 1.05f; ///< 레일 · 승강장 조각이 이웃과 겹치는 비율(틈이 보이지 않게)
            static constexpr float32 kStationWidth      = 2.0f;
            static constexpr float32 kStationSideOffset = 2.2f; ///< 트랙 가운데에서 승강장까지(m)
            static constexpr float32 kSupportMinUp      = 0.6f; ///< 트랙의 위가 이보다 기울면(뒤집힌 구간) 기둥을 세우지 않는다
            static constexpr float32 kSupportMinHeight  = 1.5f; ///< 레일 밑이 이보다 낮으면 기둥을 세우지 않는다(m)
            static constexpr float32 kPathWidth         = 2.4f;
            static constexpr float32 kPathThickness     = 0.3f;
            static constexpr float32 kMinPathLength     = 0.5f; ///< 이보다 짧은 길은 깔지 않는다(m)

            /** @brief 오브젝트의 메시를 시뮬레이션 자리(절대) · 회전으로 둡니다. 메시가 없으면 nullptr 입니다. */
            static MeshComponent* placeMesh( GameObject* pObject, const float3& position, const float3& rotation )
            {
                MeshComponent* pMesh = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
                if ( pMesh == nullptr )
                    return nullptr;
                pMesh->setLocalPosition( pObject->getManager()->getWorldOrigin().toLocal( position ) );
                pMesh->setLocalRotation( rotation );
                return pMesh;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ParkViewComponent::ParkViewComponent()
        : _guestPrefab{}
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
        , _nextRideAction{}
        , _toggleOpenAction{}
        , _priceDownAction{}
        , _priceUpAction{}
        , _feeDownAction{}
        , _feeUpAction{}
        , _buildAction{}
        , _rideAction{}
        , _thoughtsAction{}
        , _statusAction{}
        , _priceStep{ 1 }
        , _entryFeeStep{ 5 }
        , _carCount{ 4 }
        , _modelScale{ 2.0f }
        , _rideEyeHeight{ 1.6f }
        , _rideFieldOfView{ Radians::fromDegrees( 85.0f ) }
        , _rideFarPlane{ 600.0f }
        , _railSpacing{ 2.0f }
        , _supportSpacing{ 8.0f }
        , _railPieceLength{ 4.0f }
        , _railTopDepth{ 0.7f }
        , _railBottomDepth{ 1.0f }
        , _selectedPlacement{ 0 }
        , _director{}
        , _listViewObject{}
        , _listRideShown{}
        , _tintCache{}
        , _arrGuestLook{}
        , _shownRevision{ 0 }
        , _ridingCoaster{ constant::invalid_index::kInt32 }
        , _bWasRiding{ SW_FALSE }
        , _bInputLive{ SW_FALSE }
        , _bActionsValid{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    ParkViewComponent::~ParkViewComponent() = default;

    void ParkViewComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                  pOwner    = getOwner();
        const ParkGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<ParkGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "ParkView.WithoutGameMode", "[Park] park view on '%#': no ParkGameModeComponent in front of it on the same object - the park is not shown",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<ParkGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<ParkGameModeComponent>( pDirector );
    }

    void ParkViewComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터와 같은 그룹 — 카메라 디렉터(PostPhysics 틱 뒤)가 이번 프레임의 탑승 시점을 읽는다.
        setTickGroup( TickGroup::PrePhysics );
        _bActionsValid = SW_TRUE;
        for ( const hashed_string* pAction : { &_nextRideAction, &_toggleOpenAction, &_priceDownAction, &_priceUpAction, &_feeDownAction, &_feeUpAction, &_buildAction, &_rideAction, &_thoughtsAction,
                                               &_statusAction } )
        {
            if ( pAction->empty() )
                _bActionsValid = SW_FALSE;
        }
        if ( _bActionsValid == SW_FALSE )
            SW_LOG_ERROR_ID( "ParkView.ActionMissing", "[Park] park view: every input action must be named - the view reads no input" );
    }

    void ParkViewComponent::onEndPlay()
    {
        despawnViews();
        Component::onEndPlay();
    }

    void ParkViewComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령 · 스폰은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( ParkViewComponent& view, GameObjectManager& manager )
        { view.updateView( manager ); } );
    }

    void ParkViewComponent::despawnViews()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            for ( const GameObjectHandle& handle : _listViewObject )
            {
                GameObject* const pObject = pManager->resolveGameObject( handle );
                if ( pObject != nullptr )
                    pManager->destroyGameObject( pObject );
            }
        }
        _listViewObject.clear();
        _listRideShown.clear();
        _shownRevision = 0;
    }

    const shared_ptr<MaterialInstance>& ParkViewComponent::getGuestLook( int32 bucket ) const
    {
        return _arrGuestLook[MathUtil::clamp( bucket, 0, 2 )];
    }

    int32 ParkViewComponent::computeHappinessBucket( float32 happiness )
    {
        if ( happiness >= ParkViewComponentInternal::kHappyThreshold )
            return 0;
        return happiness >= ParkViewComponentInternal::kContentThreshold ? 1 : 2;
    }

    void ParkViewComponent::updateView( GameObjectManager& manager )
    {
        ParkGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        const bool                bReadInput = _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE;
        const InputManager* const pInput     = bReadInput ? game::findSubsystem<InputManager>() : nullptr;
        if ( pInput != nullptr )
        {
            // 일시정지 메뉴 같은 UI 가 먹은 입력은 보이지 않는다.
            const GameInputView input( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
            updateInput( input, *pDirector );
        }
        syncViews( manager, *pDirector );
        updateCameraOverride( manager, *pDirector );
    }

    // ------------------------------------------------------------------------------
    // 입력 → 디렉터 명령
    // ------------------------------------------------------------------------------
    void ParkViewComponent::updateInput( const GameInputView& input, ParkGameModeComponent& director )
    {
        // 명령이 거절되면(돈 부족 · 짓지 않은 놀이기구) 디렉터가 로그 · 소리로 알린다 — 여기서 따로 할 일이 없다.
        if ( input.wasActionTriggered( _nextRideAction ) )
            selectNextRide( director );
        const vector<ParkGameModeComponent::RidePlacement>& listPlacement = director.getPlacements();
        if ( _selectedPlacement < 0 || _selectedPlacement >= static_cast<int32>( listPlacement.size() ) )
            _selectedPlacement = 0; // 배치 데이터가 줄었다(복원한 값) — 열린 공원에는 배치가 하나 이상 있다
        const int32 rideIndex = listPlacement[static_cast<size_t>( _selectedPlacement )]._rideIndex;
        if ( input.wasActionTriggered( _toggleOpenAction ) )
            (void)director.submitToggleRideOpen( rideIndex );
        if ( input.wasActionTriggered( _priceUpAction ) )
            (void)director.submitRidePriceChange( rideIndex, _priceStep );
        if ( input.wasActionTriggered( _priceDownAction ) )
            (void)director.submitRidePriceChange( rideIndex, -_priceStep );
        if ( input.wasActionTriggered( _feeUpAction ) )
            (void)director.submitEntryFeeChange( _entryFeeStep );
        if ( input.wasActionTriggered( _feeDownAction ) )
            (void)director.submitEntryFeeChange( -_entryFeeStep );
        if ( input.wasActionTriggered( _buildAction ) )
            (void)( rideIndex < 0 ? director.submitBuild( _selectedPlacement ) : director.submitBuildCheapest() );
        if ( input.wasActionTriggered( _rideAction ) )
            toggleCoasterRide( director );
        if ( input.wasActionTriggered( _thoughtsAction ) )
            logThoughts( director );
        if ( input.wasActionTriggered( _statusAction ) )
            director.logStatus();
    }

    void ParkViewComponent::selectNextRide( const ParkGameModeComponent& director )
    {
        const vector<ParkGameModeComponent::RidePlacement>& listPlacement = director.getPlacements();
        _selectedPlacement                                                = ( _selectedPlacement + 1 ) % static_cast<int32>( listPlacement.size() );
        (void)GameSound::postEvent( hashed_string( ParkViewComponentInternal::kEventSelect ) ); // 소리 번호는 쓰지 않는다
        [[maybe_unused]] const ParkGameModeComponent::RidePlacement& placement = listPlacement[static_cast<size_t>( _selectedPlacement )];
        SW_LOG_INFO_ID( "ParkView.Selected", "[Park] selected %# (%#)", placement._ride._name.c_str(), placement._rideIndex >= 0 ? "built" : "not built - build it first" );
    }

    void ParkViewComponent::toggleCoasterRide( const ParkGameModeComponent& director )
    {
        if ( _ridingCoaster >= 0 )
            _ridingCoaster = constant::invalid_index::kInt32;
        else if ( director.findCoasterTrain( 0 ) != nullptr )
        {
            const optional<int32> selectedCoaster = director.findCoasterIndex( _selectedPlacement );
            _ridingCoaster                        = selectedCoaster.has_value() ? *selectedCoaster : 0;
        }
        SW_LOG_INFO_ID( "ParkView.CoasterViewToggled", "[Park] %#", _ridingCoaster >= 0 ? "riding the coaster - the ride action again to get off" : "back to the park view" );
    }

    void ParkViewComponent::logThoughts( const ParkGameModeComponent& director )
    {
        constexpr ParkGuestThought kArrThought[] = { ParkGuestThought::GreatRide, ParkGuestThought::TooIntense, ParkGuestThought::TooTame,
                                                     ParkGuestThought::TooExpensive, ParkGuestThought::QueueTooLong, ParkGuestThought::Sick,
                                                     ParkGuestThought::Tired, ParkGuestThought::OutOfCash, ParkGuestThought::NothingToRide };
        for ( const ParkGuestThought thought : kArrThought )
        {
            const uint32 count = director.getSimulation().countGuestsThinking( thought );
            if ( count > 0 )
                SW_LOG_INFO_ID( "ParkView.GuestsThink", "[Park] %# guests think: %#", count, getEnumName( thought ) );
        }
    }

    // ------------------------------------------------------------------------------
    // 모습(틱 뒤 · 게임 스레드)
    // ------------------------------------------------------------------------------
    void ParkViewComponent::syncViews( GameObjectManager& manager, const ParkGameModeComponent& director )
    {
        const vector<ParkGameModeComponent::RidePlacement>& listPlacement = director.getPlacements();
        if ( _shownRevision != director.getParkRevision() )
        {
            // 처음이거나 걷은 뒤, 또는 공원이 복원됐다 — 손님 풀과 지은 것을 모두 다시 세운다. 탑승 중이던 열차는 없는 것으로 친다.
            if ( _shownRevision != 0 )
                _ridingCoaster = constant::invalid_index::kInt32;
            despawnViews();
            _shownRevision = director.getParkRevision();
            _listRideShown.assign( listPlacement.size(), SW_FALSE );
            spawnGuestPool( manager, director );
        }
        for ( size_t placementIndex = 0; placementIndex < listPlacement.size(); ++placementIndex )
        {
            if ( listPlacement[placementIndex]._rideIndex < 0 || _listRideShown[placementIndex] == SW_TRUE )
                continue;
            _listRideShown[placementIndex] = SW_TRUE;
            spawnRideView( manager, director, static_cast<int32>( placementIndex ) );
        }
    }

    void ParkViewComponent::spawnGuestPool( GameObjectManager& manager, const ParkGameModeComponent& director )
    {
        // 최대 손님 수만큼 미리 스폰하고 숨긴다(오브젝트를 매 프레임 만들고 지우지 않는다). 손님 하나는 뷰 컴포넌트 하나가 시뮬레이션의 같은 번호를 따라간다.
        const GameObjectHandle   gameMode = getOwner()->getHandle();
        const ThemeParkSettings& settings = director.getSimulation().getSettings();
        for ( int32 guestIndex = 0; guestIndex < settings._maxGuests; ++guestIndex )
        {
            GameObject*    pGuest = spawnView( manager, _guestPrefab, "ParkGuest" );
            MeshComponent* pMesh  = ParkViewComponentInternal::placeMesh( pGuest, settings._gatePosition, float3{ 0.0f, 0.0f, 0.0f } );
            if ( pMesh == nullptr )
                continue;
            if ( _arrGuestLook[0] == nullptr )
            {
                const float4 arrColor[3] = { _happyColor, _contentColor, _unhappyColor };
                for ( int32 bucket = 0; bucket < 3; ++bucket )
                {
                    _arrGuestLook[bucket] = _tintCache.acquire( pMesh->getMaterial(), arrColor[bucket] );
                }
            }
            if ( _arrGuestLook[0] != nullptr )
                pMesh->setMaterialInstance( _arrGuestLook[0] );
            pMesh->setVisible( false );
            ParkGuestComponent* pView = pGuest->getComponent<ParkGuestComponent>();
            if ( pView != nullptr )
                pView->assignGuest( gameMode, guestIndex );
        }
    }

    void ParkViewComponent::spawnRideView( GameObjectManager& manager, const ParkGameModeComponent& director, int32 placementIndex )
    {
        using Internal                                        = ParkViewComponentInternal;
        const ParkGameModeComponent::RidePlacement& placement = director.getPlacements()[static_cast<size_t>( placementIndex )];
        const float3                                parkRotation{ 0.0f, director.getParkYaw(), 0.0f };
        spawnPath( manager, director.getSimulation().getSettings()._gatePosition, placement._ride._entrance );
        (void)Internal::placeMesh( spawnView( manager, _rideEntrancePrefab, "RideEntrance" ), placement._ride._entrance, parkRotation ); // 자리만 잡는다
        if ( placement._layoutID.empty() )
        {
            GameObject*    pObject = spawnView( manager, _flatRidePrefab, "FlatRide" );
            MeshComponent* pMesh   = Internal::placeMesh( pObject, placement._position + float3{ 0.0f, placement._size._y * 0.5f, 0.0f }, parkRotation );
            if ( pMesh == nullptr )
                return;
            pMesh->setMeshID( placement._shape );
            pMesh->setLocalScale( placement._size );
            _tintCache.apply( *pMesh, placement._color );
            FlatRideComponent* pView = pObject->getComponent<FlatRideComponent>();
            if ( pView != nullptr )
                pView->assignRide( getOwner()->getHandle(), placement._rideIndex, placement._spin, director.getParkYaw() );
            return;
        }
        const optional<int32>     coasterIndex = director.findCoasterIndex( placementIndex );
        const CoasterTrain* const pTrain       = coasterIndex.has_value() ? director.findCoasterTrain( *coasterIndex ) : nullptr;
        if ( pTrain != nullptr )
            spawnCoasterView( manager, *pTrain, *coasterIndex, placement._position._y );
    }

    void ParkViewComponent::spawnCoasterView( GameObjectManager& manager, const CoasterTrain& train, int32 coasterIndex, float32 groundHeight )
    {
        using Internal            = ParkViewComponentInternal;
        const CoasterTrack& track = *train.getTrack();

        // 레일은 2m 마다 트랙 좌표계로 돌린 직선 레일 조각이다(조각 시작점이 원점). 기둥은 8m 마다 레일 밑에서 땅까지 세운다(뒤집힌 구간은 없다).
        // 스테이션 구간은 레일 옆에 승강장을 깐다.
        const float32 pieceLength = _railSpacing * Internal::kPieceOverlap;
        const float3  railScale{ _modelScale, _modelScale, pieceLength / _railPieceLength };
        const float3  stationScale{ Internal::kStationWidth, Internal::kStationWidth, pieceLength };
        float32       nextSupport = 0.0f;
        for ( float32 distance = 0.0f; distance < track.getLength(); distance += _railSpacing )
        {
            const CoasterTrackFrame frame    = track.sample( distance );
            const float3            rotation = OrientationUtil::computeEulerFromForwardUp( frame._forward, frame._up );
            const float3            railLift = frame._up * ( _modelScale * _railTopDepth );
            MeshComponent*          pRail    = Internal::placeMesh( spawnView( manager, _railPrefab, "CoasterRail" ), frame._position + railLift, rotation );
            if ( pRail != nullptr )
                pRail->setLocalScale( railScale );
            if ( ( frame._flags & CoasterSegmentFlag::kStation ) != 0 )
            {
                MeshComponent* pStation =
                    Internal::placeMesh( spawnView( manager, _stationPrefab, "CoasterStation" ), frame._position - frame._right * Internal::kStationSideOffset - railLift, rotation );
                if ( pStation != nullptr )
                    pStation->setLocalScale( stationScale );
            }
            const float32 railBottom = frame._position._y - _modelScale * ( _railBottomDepth - _railTopDepth ) - groundHeight;
            const bool    bSupported = distance >= nextSupport && frame._up._y > Internal::kSupportMinUp && railBottom > Internal::kSupportMinHeight;
            if ( bSupported )
            {
                nextSupport             = distance + _supportSpacing;
                MeshComponent* pSupport = Internal::placeMesh( spawnView( manager, _supportPrefab, "CoasterSupport" ),
                                                               float3{ frame._position._x, groundHeight, frame._position._z }, float3{ 0.0f, 0.0f, 0.0f } );
                if ( pSupport != nullptr )
                    pSupport->setLocalScale( float3{ _modelScale, railBottom, _modelScale } );
            }
        }
        const GameObjectHandle gameMode = getOwner()->getHandle();
        for ( uint32 carIndex = 0; carIndex < _carCount; ++carIndex )
        {
            GameObject* pCar = spawnView( manager, carIndex == 0 ? _carFrontPrefab : _carPrefab, "CoasterCar" );
            (void)Internal::placeMesh( pCar, train.getFrame()._position, float3{ 0.0f, 0.0f, 0.0f } ); // 기울기는 차량 뷰가 맞춘다
            CoasterCarComponent* pView = pCar != nullptr ? pCar->getComponent<CoasterCarComponent>() : nullptr;
            if ( pView != nullptr )
                pView->assignCar( gameMode, coasterIndex, static_cast<int32>( carIndex ) );
        }
    }

    void ParkViewComponent::spawnPath( GameObjectManager& manager, const float3& from, const float3& to )
    {
        using Internal       = ParkViewComponentInternal;
        const float3  delta  = float3{ to._x - from._x, 0.0f, to._z - from._z };
        const float32 length = delta.getLength();
        if ( length < Internal::kMinPathLength )
            return;
        const float32  yaw   = MathUtil::atan2( delta._x, delta._z );
        MeshComponent* pPath = Internal::placeMesh( spawnView( manager, _pathPrefab, "ParkPath" ), float3{ ( from._x + to._x ) * 0.5f, from._y, ( from._z + to._z ) * 0.5f }, float3{ 0.0f, yaw, 0.0f } );
        if ( pPath != nullptr )
            pPath->setLocalScale( float3{ Internal::kPathWidth, Internal::kPathThickness, length + Internal::kPathWidth } );
    }

    GameObject* ParkViewComponent::spawnView( GameObjectManager& manager, const AssetRef<PrefabAsset>& prefab, const utf8* pName )
    {
        AssetManager* const pAssetManager = game::findSubsystem<AssetManager>();
        if ( pAssetManager == nullptr || prefab.empty() )
            return nullptr;
        GameObject* const pObject = pAssetManager->getAssetCache<PrefabCache>().spawn( &manager, prefab.path(), pName );
        if ( pObject == nullptr )
            return nullptr;
        pObject->setAlwaysLoaded( true ); // 하위 레벨로 얹은 공원의 모습이다 — 스트리밍하는 바탕 씬의 셀이 삼키지 않는다(하위 레벨 앵커와 같다)
        _listViewObject.push_back( pObject->getHandle() );
        return pObject;
    }

    void ParkViewComponent::updateCameraOverride( GameObjectManager& manager, const ParkGameModeComponent& director )
    {
        // 카메라 디렉터는 PostPhysics 틱 뒤에 포즈를 쓴다. 이 그룹(PrePhysics)에서 넣은 시점을 같은 프레임에 읽는다. 카메라 오브젝트에는 다른 쓰기가 없다.
        const CoasterTrain* const pTrain = director.findCoasterTrain( _ridingCoaster );
        if ( pTrain == nullptr && _bWasRiding == SW_FALSE )
            return;
        GameObject* const              pRigObject = manager.resolveGameObject( _cameraRig );
        CameraDirectorComponent* const pRig       = pRigObject != nullptr ? pRigObject->getComponent<CameraDirectorComponent>() : nullptr;
        _bWasRiding                               = pTrain != nullptr ? SW_TRUE : SW_FALSE;
        if ( pRig == nullptr )
            return;
        if ( pTrain == nullptr )
        {
            pRig->clearViewOverride();
            return;
        }
        // 맨 앞 차량에 탄 시점이다. 롤까지 따라간다(루프에서 하늘이 아래로 온다).
        const CoasterTrackFrame frame = pTrain->getFrame();
        pRig->setViewOverride( manager.getWorldOrigin().toLocal( frame._position + frame._up * _rideEyeHeight ), OrientationUtil::computeEulerFromForwardUp( frame._forward, frame._up ), _rideFieldOfView.radians(), _rideFarPlane );
    }
} // namespace sw
