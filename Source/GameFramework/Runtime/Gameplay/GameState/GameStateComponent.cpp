#include "pch.h"

#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatUpgrade.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"
#include "GameFramework/Runtime/Gameplay/GameState/StateSectionTableUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"
#include "GameFramework/Runtime/Gameplay/Progression/Reputation.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"

namespace sw
{
    namespace
    {
        struct GameStateComponentInternal
        {
            static constexpr uint32 kSectionCount = 4;

            /** @brief 판 1 의 플레이어 구간(지갑 · 일지 · 평판 · 가방)이면 true 입니다 — 판 2 부터 플레이어 상태 컴포넌트(`WalletComponent` 등)가 싣는다. */
            static bool isPlayerSection( uint32 tag ) { return tag == Wallet::kStateTag || tag == QuestLog::kStateTag || tag == ReputationState::kStateTag || tag == Inventory::kStateTag; }

            /**
             * @brief 판 1 → 2: 플레이어 구간을 버리고 월드 구간(플래그 · 시계 · 날씨 · 땅)만 옮깁니다. 구간 본문은 그대로입니다.
             * @details 플레이어 구간은 다른 컴포넌트(플레이어 상태 컴포넌트)의 상태라 이 컴포넌트의 바이트에서 옮겨 줄 수 없다 — 버린 것을 경고로 알린다.
             */
            [[nodiscard]] static bool upgradeStateFrom1( Archive& inOldBody, Archive& outNewBody )
            {
                uint32 sectionCount = 0;
                inOldBody >> sectionCount;
                vector<StateSectionTableUtil::Section> listKept;
                uint32                                 droppedCount = 0;
                for ( uint32 sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex )
                {
                    StateSectionTableUtil::Section section;
                    if ( StateIO::readSection( inOldBody, section._tag, section._version, section._body ) == false )
                        return false;
                    if ( isPlayerSection( section._tag ) )
                        ++droppedCount;
                    else
                        listKept.push_back( section );
                }
                if ( droppedCount > 0 )
                    SW_LOG_WARNING_ID( "GameState.PlayerSectionsDropped", "GameState: an old save (version 1) held %# player section(s) (wallet, quest log, reputation, inventory) - they are not carried to the player components",
                                       droppedCount );
                outNewBody << static_cast<uint32>( listKept.size() );
                for ( const StateSectionTableUtil::Section& section : listKept )
                    StateIO::writeSection( outNewBody, section._tag, section._version, section._body );
                return inOldBody.isOk();
            }
        };

        struct GameStateDevCommandsInternal
        {
            /** @brief 활성 씬에서 열린 월드 상태(씬에 하나)입니다. 없으면 답을 채우고 nullptr 입니다. */
            static GameStateComponent* findGameState( string& outReply )
            {
                SceneManager* const      pSceneManager = game::findSubsystem<SceneManager>();
                const GameObjectManager* pManager      = pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
                if ( pManager == nullptr )
                {
                    outReply = "cheat: no active scene";
                    return nullptr;
                }
                GameStateComponent* const pFound = GameStateComponent::findOpen( pManager );
                if ( pFound == nullptr )
                    outReply = "cheat: no initialized GameStateComponent in the active scene";
                return pFound;
            }

            /** @brief `cheat.weather <weatherID> [duration]` — 판 상태의 날씨를 곧바로 바꿉니다(전환 없이). 기간을 빼면 정의의 범위에서 고른다. 표면 젖음 · 강수 시나리오가 쓴다. */
            static bool runWeather( const vector<string>& listArgument, string& outReply )
            {
                float32    duration                 = 0.0f;
                const bool bInvalidWeatherArguments = listArgument.empty() || listArgument.size() > 2 || ( listArgument.size() == 2 && ( StringUtil::parseFloat( listArgument[1], duration ) == false || duration < 0.0f ) );
                if ( bInvalidWeatherArguments )
                    return false;
                GameStateComponent* pState = findGameState( outReply );
                if ( pState == nullptr )
                    return true;
                WeatherSystem&      weather   = pState->getWeather();
                const hashed_string weatherID = hashed_string( listArgument[0] );
                weather.forceWeather( weatherID, duration, true );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                if ( weather.getCurrent() == weatherID )
                    replyBuilder.appendFormat( "cheat.weather %#", listArgument[0] );
                else
                    replyBuilder.appendFormat( "cheat.weather: the session state has no weather '%#' (no weather catalog or unknown id)", listArgument[0] );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_STATE_FORMAT_UPGRADE( sessionState1To2, "GTST", 1, 2, &GameStateComponentInternal::upgradeStateFrom1 );

    // 지갑 · 가방 명령(`cheat.money` · `cheat.give`)은 플레이어 상태 개발 도구(`PlayerStateDevTools.cpp`)에 있다.
    SW_DEV_COMMAND( CheatWeather, "cheat.weather", "cheat.weather <weatherID> [duration]", "Switch the session state's weather now, without the transition (duration in game seconds; none picks from the definition)",
                    &GameStateDevCommandsInternal::runWeather );
} // namespace sw

namespace sw
{
    GameStateComponent::GameStateComponent()
        : _flags{}
        , _clock{}
        , _pWeatherCatalog{}
        , _weather{}
        , _weatherMaterial{}
        , _land{}
        , _listClockEvent{}
        , _pendingStateBytes{}
        , _bInitialized{ SW_FALSE }
        , _bFreshGame{ SW_FALSE }
        , _bWeather{ SW_FALSE }
        , _bOpenAtBeginPlay{ SW_FALSE }
        , _bRefusedInScene{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    GameStateComponent::~GameStateComponent() = default;

    GameStateInitResult GameStateComponent::initialize( const GameStateSettings& settings )
    {
        if ( _bInitialized == SW_TRUE )
            return GameStateInitResult::AlreadyInitialized;
        // 값을 범위 안으로 맞춰 열지 않는다. 시계 설정 안까지 보며, 상태를 바꾸기 전에 본다.
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "GameState.RefusedOpenSettings", "GameState: refused to open - settings '%#'", rangeError.c_str() );
            return GameStateInitResult::Refused;
        }
        // 등록에서 거절된 둘째 월드 상태는 열지 않는다(`onRegister`).
        if ( _bRefusedInScene == SW_TRUE )
            return GameStateInitResult::Refused;
        if ( _clock.initialize( settings._clock ) == false )
            return GameStateInitResult::Refused;
        _flags.clear();
        _pWeatherCatalog = settings._pWeatherCatalog;
        _weather.initialize( _pWeatherCatalog.get(), settings._weatherSeed, _clock.getSeasonName() );
        _bWeather = settings._pWeatherCatalog != nullptr ? SW_TRUE : SW_FALSE;
        _weatherMaterial.open( _pWeatherCatalog.get() ); // 강수를 칸으로 적은 카탈로그면 지면 머티리얼이 날씨를 따른다(없으면 열지 않는다)
        _land.initialize( settings._landWidth, settings._landHeight, settings._landCellSize, settings._landOrigin );
        _listClockEvent.clear();
        _bInitialized = SW_TRUE;
        _bFreshGame   = SW_TRUE;
        if ( _pendingStateBytes.empty() )
            return GameStateInitResult::Fresh;
        if ( applyPendingState() == false )
            return GameStateInitResult::Fresh;
        _bFreshGame = SW_FALSE;
        return GameStateInitResult::Restored;
    }

    void GameStateComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        // 월드 상태는 씬에 하나다 — 둘째를 들이면 시계 · 날씨를 읽는 쪽(`findOpen`)이 등록 순서에 따라 다른 판을 본다(하위 레벨 미니게임이 바탕 씬에 판 상태를 둔 경우).
        // 열 때가 아니라 등록에서 거절한다: 둘째는 레지스트리에 오르지 않고 `initialize` 도 거절한다(언리얼은 월드에 AGameState 하나를 GameMode 가 만든다).
        for ( const GameStateComponent* pRegistered : manager.getComponentRegistry().getAll<GameStateComponent>() )
        {
            const GameObject* const pOwner = getOwner();
            const GameObject* const pFirst = pRegistered->getOwner();
            SW_LOG_ERROR_ID( "GameState.SecondInScene", "GameState: refused to register on '%#' - '%#' already holds the world state of this scene (one per scene)",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?", pFirst != nullptr ? pFirst->getName().c_str() : "?" );
            _bRefusedInScene = SW_TRUE;
            return;
        }
        _bRefusedInScene = SW_FALSE;
        manager.getComponentRegistry().add<GameStateComponent>( this );
    }

    void GameStateComponent::onUnregister( GameObjectManager& manager )
    {
        if ( _bRefusedInScene == SW_FALSE )
            manager.getComponentRegistry().remove<GameStateComponent>( this );
        _bRefusedInScene = SW_FALSE;
        Component::onUnregister( manager );
    }

    void GameStateComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터와 같은 그룹이다. 같은 오브젝트 안에서는 붙은 순서대로 처리하며, 이 컴포넌트가 맨 앞이다.
        setTickGroup( TickGroup::PrePhysics );
        // 디렉터 없는 씬(시연 · 시험)만 데이터로 연다 — 디렉터가 있으면 그 설정으로 디렉터가 연다.
        if ( _bOpenAtBeginPlay == SW_TRUE && initialize( GameStateSettings{} ) == GameStateInitResult::Refused )
            SW_LOG_ERROR_ID( "GameState.OpenAtBeginPlayRefused", "GameState: could not open with default settings at begin play" );
    }

    void GameStateComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _listClockEvent.clear();
        if ( _bInitialized == SW_FALSE )
            return;
        _clock.update( context.getDeltaTime() );
        _clock.pollEvents( _listClockEvent ); // 날이 넘어가면 평판 컴포넌트가 이 알림을 읽고 평판을 식힌다(`ReputationComponent::onTick`)
        if ( _bWeather == SW_TRUE && _clock.isPaused() == false )
            (void)_weather.update( context.getDeltaTime() * _clock.getTimeScale() * 86400.0f / _clock.getSettings()._secondsPerDay, _clock.getSeasonName() );
        // 묶음은 게임 스레드만 고친다 — 쓰기는 틱 뒤에 한다.
        if ( _weatherMaterial.isOpen() )
            context.deferPostTickOnComponent( *this, []( GameStateComponent& state )
            { state._weatherMaterial.apply( state._weather ); } );
    }

    void GameStateComponent::onEndPlay()
    {
        _weatherMaterial.close();
        Component::onEndPlay();
    }

    void GameStateComponent::serializeState( StateIO& io )
    {
        // 구간 표는 여기 한 곳이다. 쓰기는 이 순서로 싣고, 읽기는 표로 찾아 넘긴다.
        const auto forEachSection = [this]( auto&& visit )
        {
            visit( _flags, "flags" );
            visit( _clock, "clock" );
            visit( _weather, "weather" );
            visit( _land, "land" );
        };
        if ( io.isWriting() )
        {
            Archive& archive = io.getArchive();
            // 열리기 전(복원한 직후의 두 번째 캡처)이면 받은 바이트가 곧 상태이다. 빈 판을 쓰면 세이브를 잃는다.
            if ( _bInitialized == SW_FALSE && _pendingStateBytes.empty() == false )
            {
                archive.writeBytes( _pendingStateBytes.data(), _pendingStateBytes.size() );
                return;
            }
            StateSectionTableUtil::write( archive, kStateTag, kStateVersion, GameStateComponentInternal::kSectionCount, forEachSection );
            return;
        }
        if ( StateSectionTableUtil::read( io, kStateTag, kStateVersion, GameStateComponentInternal::kSectionCount, forEachSection, "the session state" ) )
            _listClockEvent.clear();
    }

    void GameStateComponent::restoreState( vector<uint8>&& bytes )
    {
        _pendingStateBytes = std::move( bytes );
        if ( _bInitialized == SW_TRUE && applyPendingState() )
            _bFreshGame = SW_FALSE;
    }

    GameStateComponent* GameStateComponent::openOnOwner( const Component& component, const GameStateSettings& settings, GameStateInitResult& outResult )
    {
        outResult                  = GameStateInitResult::Refused;
        GameStateComponent* pState = findOnOwner( component );
        if ( pState == nullptr )
        {
            SW_LOG_ERROR_ID( "GameState.MissingOnOwner", "no GameStateComponent in front of the game mode on this object - the game cannot start" );
            return nullptr;
        }
        outResult = pState->initialize( settings );
        return outResult != GameStateInitResult::Refused ? pState : nullptr; // 거절한 까닭은 `initialize` 가 남겼다
    }

    GameStateComponent* GameStateComponent::openOnOwner( const Component& component, const GameStateSettings& settings )
    {
        GameStateInitResult result = GameStateInitResult::Refused;
        return openOnOwner( component, settings, result );
    }

    GameStateComponent* GameStateComponent::findOnOwner( const Component& component )
    {
        GameObject* pOwner = component.getOwner();
        return pOwner != nullptr ? pOwner->getComponent<GameStateComponent>() : nullptr;
    }

    GameStateComponent* GameStateComponent::findOpen( const GameObjectManager* pManager )
    {
        if ( pManager == nullptr )
            return nullptr;
        for ( GameStateComponent* pState : pManager->getComponentRegistry().getAll<GameStateComponent>() )
        {
            if ( pState->isInitialized() )
                return pState; // 씬에 하나다(`onRegister` 가 둘째를 거절한다)
        }
        return nullptr;
    }

    bool GameStateComponent::applyPendingState()
    {
        Archive    archive( _pendingStateBytes.data(), _pendingStateBytes.size() );
        const bool bRestored = readState( archive );
        _pendingStateBytes.clear();
        if ( bRestored == false )
            SW_LOG_ERROR_ID( "GameState.SavedSharedState", "GameState: the saved shared state could not be read - the game state starts fresh" );
        return bRestored;
    }

    GameFlags* GameStateComponent::GameFlagsOwnerPort::findOpenFlags()
    {
        return getPortOwner<GameStateComponent>( *this, offsetof( GameStateComponent, _gameFlagsOwnerPort ) ).findOpenFlags();
    }
} // namespace sw
