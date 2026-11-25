#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Client/CityBuildInputComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Component/CityBuilderGameModeComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Control/InputActionCheckUtil.h"
#include "GameFramework/Runtime/Foundation/Presentation/GameSound.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

namespace sw
{
    namespace
    {
        /** @brief 고른 도구를 모든 게임의 상태 스냅샷에 올립니다. */
        const StatefulComponentRegistrar<CityBuildInputComponent> s_stateRegistration{ "CityBuildInputComponent" };

        struct CityBuildInputComponentInternal
        {
            // 사운드 이벤트 이름입니다. 클립, 범위, 동시 재생 상한은 게임의 사운드 이벤트 라이브러리가 정합니다.
            static constexpr const utf8* kSoundSelect  = "CitySelect";
            static constexpr const utf8* kSoundBuilt   = "CityBuilt";
            static constexpr const utf8* kSoundBlocked = "CityBlocked";
            static constexpr const utf8* kSoundEvolved = "CityEvolved";

            /** @brief 감시 값 `City.CursorCellX` · `City.CursorCellZ` 입니다. 커서가 땅 밖이면 값이 없습니다. */
            [[nodiscard]] static bool readCursorCellX( const CityBuildInputComponent& buildInput, float64& outValue )
            {
                int2  tile{};
                int32 size = 1;
                if ( buildInput.findCursor( tile, size ) == false )
                    return false;
                outValue = static_cast<float64>( tile._x );
                return true;
            }
            [[nodiscard]] static bool readCursorCellZ( const CityBuildInputComponent& buildInput, float64& outValue )
            {
                int2  tile{};
                int32 size = 1;
                if ( buildInput.findCursor( tile, size ) == false )
                    return false;
                outValue = static_cast<float64>( tile._y );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( cityTool, "City.Tool", "Selected build tool (0 road, then the catalog buildings in order)", CityBuildInputComponent, &CityBuildInputComponent::getSelectedTool );
    SW_AUTOMATION_COMPONENT_WATCH( cityCursorCellX, "City.CursorCellX", "Ground tile X under the pointer; absent while the pointer is off the ground", CityBuildInputComponent,
                                   &CityBuildInputComponentInternal::readCursorCellX );
    SW_AUTOMATION_COMPONENT_WATCH( cityCursorCellZ, "City.CursorCellZ", "Ground tile Z (city row) under the pointer; absent while the pointer is off the ground", CityBuildInputComponent,
                                   &CityBuildInputComponentInternal::readCursorCellZ );
} // namespace sw

namespace sw
{
    CityBuildInputComponent::CityBuildInputComponent()
        : _cameraRig{}
        , _placeAction{}
        , _demolishAction{}
        , _nextToolAction{}
        , _prevToolAction{}
        , _roadToolAction{}
        , _autoPlanAction{}
        , _pauseAction{}
        , _fasterAction{}
        , _slowerAction{}
        , _statusAction{}
        , _director{}
        , _cursorTile{}
        , _cursorSize{ 1 }
        , _selectedTool{ 0 }
        , _bInputLive{ SW_FALSE }
        , _bActionsValid{ SW_FALSE }
        , _bCursorValid{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    CityBuildInputComponent::~CityBuildInputComponent() = default;

    void CityBuildInputComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        const GameObject* const pOwner = getOwner();
        _director                      = TComponentHandle<CityBuilderGameModeComponent>( pOwner != nullptr ? pOwner->getComponent<CityBuilderGameModeComponent>() : nullptr );
        if ( _director.isValid() == false )
            SW_LOG_ERROR_ID( "City.BuildInputWithoutGameMode", "City build input on '%#': no city game mode in front of it on the same object - it reads no input",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
    }

    void CityBuildInputComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 입력 액션은 입력 맵에 있어야 한다. 입력이 없는 실행(헤드리스)은 보지 않는다.
        const InputManager* const pInput = game::findSubsystem<InputManager>();
        const bool                bValid = pInput != nullptr && InputActionCheckUtil::hasAllActions( pInput->getInputMap(),
                                                                                                     { &_placeAction, &_demolishAction, &_nextToolAction, &_prevToolAction, &_roadToolAction, &_autoPlanAction,
                                                                                                       &_pauseAction, &_fasterAction, &_slowerAction, &_statusAction },
                                                                                                     "CityBuildInputComponent" );
        _bActionsValid                   = bValid ? SW_TRUE : SW_FALSE;
    }

    void CityBuildInputComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime().count() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( CityBuildInputComponent& buildInput, GameObjectManager& manager )
        { buildInput.updateInput( manager ); } );
    }

    void CityBuildInputComponent::serializeState( StateIO& io )
    {
        io.header( kStateTag, kStateVersion );
        int32 selectedTool = _selectedTool;
        io.value( selectedTool );
        io.end();
        if ( io.isReading() && io.isOk() )
            _selectedTool = selectedTool; // 카탈로그 밖 번호는 다음 입력이 도로로 되돌린다
    }

    void CityBuildInputComponent::restoreState( vector<uint8>&& bytes )
    {
        Archive archive( bytes.data(), bytes.size() );
        if ( readState( archive ) == false )
            SW_LOG_WARNING_ID( "City.SavedToolRefused", "[City] the saved build tool does not match this build - the tool is kept as it is" );
    }

    bool CityBuildInputComponent::findCursor( int2& outTile, int32& outSize ) const
    {
        if ( _bCursorValid == SW_FALSE )
            return false;
        outTile = _cursorTile;
        outSize = _cursorSize;
        return true;
    }

    void CityBuildInputComponent::updateInput( GameObjectManager& manager )
    {
        CityBuilderGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        playStepEvents( *pDirector );
        const bool                bReadInput = _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE;
        const InputManager* const pInput     = bReadInput ? game::findSubsystem<InputManager>() : nullptr;
        if ( pInput == nullptr )
            return;
        // 카메라 이동과 확대는 리그가 읽는다. 키는 입력 맵이 정한다. 일시정지 메뉴 같은 UI 가 먹은 입력은 보이지 않는다.
        const GameInputView inputMap( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
        updateTool( *pDirector, inputMap );
        (void)pDirector->submitTimeInput( inputMap.wasActionTriggered( _pauseAction ), inputMap.wasActionTriggered( _fasterAction ), inputMap.wasActionTriggered( _slowerAction ) ); // 받지 않는 디렉터가 있다
        if ( inputMap.wasActionTriggered( _autoPlanAction ) )
            (void)pDirector->submitAutoPlanToggle(); // 자동 계획이 없는 디렉터는 거절한다
        if ( inputMap.wasActionTriggered( _statusAction ) )
            pDirector->logStatus();

        updateCursor( manager, *pDirector, *pInput );
        if ( _bCursorValid == SW_FALSE )
            return;
        // 커서 타일은 포인터 위치로 정하고, 누르는 것은 액션이 정한다. 도로 도구는 누르는 동안 끌어서 잇는다.
        const bool bRoadTool = findSelectedBuilding( *pDirector ) == nullptr;
        if ( inputMap.wasActionPressed( _placeAction ) || ( bRoadTool && inputMap.isActionDown( _placeAction ) ) )
            placeSelected( *pDirector );
        if ( inputMap.wasActionTriggered( _demolishAction ) && pDirector->submitDemolish( _cursorTile ) )
            SW_LOG_INFO_ID( "City.Demolished", "[City] demolished (%#, %#)", _cursorTile._x, _cursorTile._y );
    }

    void CityBuildInputComponent::playStepEvents( const CityBuilderGameModeComponent& director )
    {
        for ( const CityEvent& event : director.getStepEvents() )
        {
            if ( event._kind != CityEvent::Kind::HouseEvolved )
                continue;
            (void)GameSound::postEvent( hashed_string( CityBuildInputComponentInternal::kSoundEvolved ) );
            return; // 한 걸음에 여럿이 올라도 소리는 한 번
        }
    }

    void CityBuildInputComponent::updateTool( const CityBuilderGameModeComponent& director, const GameInputView& inputMap )
    {
        const int32 toolCount = static_cast<int32>( director.findCatalog()->getBuildings().size() ) + 1; // 열린 판은 카탈로그를 쥐고 있다. 0 은 도로다
        if ( _selectedTool < 0 || _selectedTool >= toolCount )
            _selectedTool = 0;
        const bool bNextTool = inputMap.wasActionTriggered( _nextToolAction );
        const bool bPrevTool = inputMap.wasActionTriggered( _prevToolAction );
        const bool bRoadTool = inputMap.wasActionTriggered( _roadToolAction );
        if ( bNextTool )
            _selectedTool = ( _selectedTool + 1 ) % toolCount;
        if ( bPrevTool )
            _selectedTool = ( _selectedTool + toolCount - 1 ) % toolCount;
        if ( bRoadTool )
            _selectedTool = 0;
        const bool bToolChanged = bNextTool || bPrevTool || bRoadTool;
        if ( bToolChanged == false )
            return;
        [[maybe_unused]] const CityBuildingDef* const pDef = findSelectedBuilding( director );
        SW_LOG_INFO_ID( "City.ToolSelected", "[City] tool: %# ($%#)", pDef != nullptr ? pDef->_name.c_str() : "Road", pDef != nullptr ? pDef->_cost : director.findCatalog()->getRoadCost() );
        (void)GameSound::postEvent( hashed_string( CityBuildInputComponentInternal::kSoundSelect ) );
    }

    void CityBuildInputComponent::updateCursor( const GameObjectManager& manager, const CityBuilderGameModeComponent& director, const InputManager& input )
    {
        _bCursorValid                                = SW_FALSE;
        const GameObject* const              pObject = manager.resolveGameObject( _cameraRig );
        const CameraDirectorComponent* const pRig    = pObject != nullptr ? pObject->getComponent<CameraDirectorComponent>() : nullptr;
        float3                               point{};
        if ( pRig == nullptr || pRig->findGroundPoint( input.getMousePositionNormalized(), 0.0f, point ) == false )
            return;
        point         = manager.getWorldOrigin().toAbsolute( point ); // 도시는 절대 자리다
        _cursorTile   = int2{ static_cast<int32>( MathUtil::floor( point._x ) ), static_cast<int32>( MathUtil::floor( point._z ) ) };
        _bCursorValid = director.getCity().findTile( _cursorTile._x, _cursorTile._y ) != nullptr ? SW_TRUE : SW_FALSE;

        const CityBuildingDef* const pDef = findSelectedBuilding( director );
        _cursorSize                       = pDef != nullptr ? pDef->_size : 1;
    }

    void CityBuildInputComponent::placeSelected( CityBuilderGameModeComponent& director )
    {
        const CityBuildingDef* const pDef = findSelectedBuilding( director );
        if ( pDef == nullptr )
        {
            // 도로는 끌어서 깐다. 이미 도로인 타일은 조용히 넘어간다.
            const CityPlaceResult result = director.submitPlaceRoad( _cursorTile );
            if ( result == CityPlaceResult::NotEnoughMoney || result == CityPlaceResult::BadTerrain )
                SW_LOG_INFO_ID( "City.CannotLayRoad", "[City] cannot lay road at (%#, %#): %#", _cursorTile._x, _cursorTile._y, getEnumName( result ) );
            return;
        }
        const CityPlaceResult result = director.submitPlaceBuilding( pDef->_id, _cursorTile );
        if ( result == CityPlaceResult::Ok )
        {
            [[maybe_unused]] const Wallet* const pCityWallet = director.findCityWallet();
            SW_LOG_INFO_ID( "City.Built", "[City] built %# at (%#, %#) - $%# left", pDef->_name.c_str(), _cursorTile._x, _cursorTile._y,
                            pCityWallet != nullptr ? pCityWallet->getBalance( director.getCity().getCurrency() ) : 0 );
            (void)GameSound::postEvent( hashed_string( CityBuildInputComponentInternal::kSoundBuilt ) );
            return;
        }
        SW_LOG_INFO_ID( "City.CannotBuild", "[City] cannot build %# at (%#, %#): %#", pDef->_name.c_str(), _cursorTile._x, _cursorTile._y, getEnumName( result ) );
        (void)GameSound::postEvent( hashed_string( CityBuildInputComponentInternal::kSoundBlocked ) );
    }

    const CityBuildingDef* CityBuildInputComponent::findSelectedBuilding( const CityBuilderGameModeComponent& director ) const
    {
        const vector<CityBuildingDef>& listBuilding = director.findCatalog()->getBuildings(); // 열린 판은 카탈로그를 쥐고 있다
        const bool                     bBuilding    = 0 < _selectedTool && _selectedTool <= static_cast<int32>( listBuilding.size() );
        return bBuilding ? &listBuilding[static_cast<size_t>( _selectedTool - 1 )] : nullptr;
    }
} // namespace sw
