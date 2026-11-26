#include "pch.h"

#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatUpgrade.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/GameState/StateSectionTableUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"
#include "GameFramework/Runtime/Gameplay/Progression/Reputation.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"

namespace sw
{
    namespace
    {
        /** @brief 월드 상태(플래그 · 시계 · 날씨 · 땅)를 모든 게임의 상태 스냅샷에 싣습니다 — 공유 상태가 게임의 디렉터보다 먼저 돌아온다. */
        const StatefulComponentRegistrar<GameStateComponent> s_gameStateRegistration{ "GameStateComponent" };

        struct GameStateComponentInternal
        {
            static constexpr uint32 kSectionCount = 4;
        };

        struct GameStateWeatherWatchesInternal
        {
            /** @brief 열린 판 상태입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static const GameStateComponent* findState( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameStateComponent* pState = GameStateComponent::findOpen( query._pManager );
                if ( pState == nullptr )
                    outValue.setText( "no initialized GameStateComponent" );
                return pState;
            }

            /** @brief 감시 값 `Weather.SoundPlaying` 입니다. 이벤트 key 의 날씨 소리를 내는 중이면 1 입니다. */
            [[nodiscard]] static bool readSoundPlaying( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameStateComponent* pState = findState( query, outValue );
                if ( pState == nullptr )
                    return false;
                outValue.setNumber( pState->getWeatherScene().isSoundPlaying( hashed_string( query._key ) ) ? 1.0 : 0.0 );
                return true;
            }

            /** @brief 감시 값 `Weather.SkyOpenness` 입니다. 듣는 자리의 하늘 열림(0 실내 .. 1 트임)입니다. */
            [[nodiscard]] static bool readSkyOpenness( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameStateComponent* pState = findState( query, outValue );
                if ( pState == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pState->getWeatherScene().getSkyOpenness() ) );
                return true;
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

    SW_AUTOMATION_QUERY_WATCH( weatherSoundPlaying, "Weather.SoundPlaying", "1 while the weather sound event named key plays", &GameStateWeatherWatchesInternal::readSoundPlaying, false, true );
    SW_AUTOMATION_QUERY_WATCH( weatherSkyOpenness, "Weather.SkyOpenness", "Share of the sky open above the listener while a weather sound plays (0 indoors, 1 open)", &GameStateWeatherWatchesInternal::readSkyOpenness,
                               false, false );
    // 지갑 · 가방 명령(`cheat.money` · `cheat.give`)은 플레이어 상태 개발 도구(`PlayerStateDevTools.cpp`)에 있다.
    SW_DEV_COMMAND( CheatWeather, "cheat.weather", "cheat.weather <weatherID> [duration]", "Switch the session state's weather now, without the transition (duration in game seconds; none picks from the definition)",
                    &GameStateDevCommandsInternal::runWeather );
} // namespace sw

namespace sw
{
    GameStateComponent::GameStateComponent()
        : _flags{}
        , _clock{}
        , _weatherCatalog{}
        , _weatherSeed{ DeterministicRandom32::kDefaultSeed }
        , _pWeatherCatalog{}
        , _weather{}
        , _weatherMaterial{}
        , _weatherScene{}
        , _land{}
        , _listClockEvent{}
        , _restore{}
        , _bWeather{ SW_FALSE }
        , _bOpenAtBeginPlay{ SW_FALSE }
        , _bRefusedInScene{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    GameStateComponent::~GameStateComponent() = default;

    GameStateInitResult GameStateComponent::initialize( const GameStateSettings& settings )
    {
        if ( _restore.isOpen() )
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
        _weatherCatalog  = SharedAssetRef<WeatherCatalog>( settings._pWeatherCatalog );
        _weatherSeed     = settings._weatherSeed;
        _pWeatherCatalog = settings._pWeatherCatalog;
        _weather.initialize( _pWeatherCatalog.get(), settings._weatherSeed, _clock.getSeasonName() );
        _bWeather = settings._pWeatherCatalog != nullptr ? SW_TRUE : SW_FALSE;
        _weatherMaterial.open( _pWeatherCatalog.get() ); // 강수를 칸으로 적은 카탈로그면 지면 머티리얼이 날씨를 따른다(없으면 열지 않는다)
        _weatherScene.open( _pWeatherCatalog.get() );
        _land.initialize( settings._landWidth, settings._landHeight, settings._landCellSize, AbsolutePosition{ settings._landOrigin } );
        _listClockEvent.clear();
        return _restore.open( *this );
    }

    void GameStateComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        // 월드 상태는 씬에 하나다 — 둘째를 들이면 시계 · 날씨를 읽는 쪽(`findOpen`)이 등록 순서에 따라 다른 판을 본다(하위 레벨 미니게임이 바탕 씬에 판 상태를 둔 경우).
        // 열 때가 아니라 등록에서 거절한다: 둘째는 `initialize` 를 거절해 열리지 않는다(언리얼은 월드에 AGameState 하나를 GameMode 가 만든다). 거절한 것은 첫째가 아니다.
        for ( const GameStateComponent* pRegistered : manager.getComponentRegistry().getAll<GameStateComponent>() )
        {
            if ( pRegistered == this || pRegistered->_bRefusedInScene == SW_TRUE )
                continue;
            const GameObject* const pOwner = getOwner();
            const GameObject* const pFirst = pRegistered->getOwner();
            SW_LOG_ERROR_ID( "GameState.SecondInScene", "GameState: refused to register on '%#' - '%#' already holds the world state of this scene (one per scene)",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?", pFirst != nullptr ? pFirst->getName().c_str() : "?" );
            _bRefusedInScene = SW_TRUE;
            return;
        }
        _bRefusedInScene = SW_FALSE;
    }

    void GameStateComponent::onUnregister( GameObjectManager& manager )
    {
        _bRefusedInScene = SW_FALSE;
        Component::onUnregister( manager );
    }

    void GameStateComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터 없는 씬(시연 · 시험)만 데이터로 연다 — 디렉터가 있으면 그 설정으로 디렉터가 연다.
        if ( _bOpenAtBeginPlay == SW_TRUE && initialize( GameStateSettings{} ) == GameStateInitResult::Refused )
            SW_LOG_ERROR_ID( "GameState.OpenAtBeginPlayRefused", "GameState: could not open with default settings at begin play" );
    }

    void GameStateComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _listClockEvent.clear();
        if ( _restore.isOpen() == false )
            return;
        _clock.update( context.getDeltaTime().count() );
        _clock.pollEvents( _listClockEvent ); // 날이 넘어가면 평판 컴포넌트가 이 알림을 읽고 평판을 식힌다(`ReputationComponent::onTick`)
        if ( _bWeather == SW_TRUE && _clock.isPaused() == false )
            (void)_weather.update( context.getDeltaTime().count() * _clock.getTimeScale() * 86400.0f / _clock.getSettings()._secondsPerDay, _clock.getSeasonName() );
        if ( _weatherCatalog.get() != _pWeatherCatalog.get() )
            context.deferPostTickOnComponent( *this, []( GameStateComponent& state )
            { state.rebindWeatherCatalog(); } );
        // 묶음 · 다른 컴포넌트의 배율 · 소리는 게임 스레드만 고친다 — 쓰기는 틱 뒤에 한다(씬에 하나인 컴포넌트라 큐에 일 하나다).
        if ( _weatherMaterial.isOpen() || _weatherScene.isOpen() )
            context.deferPostTickOnComponent( *this, []( GameStateComponent& state, GameObjectManager& manager )
            {
                state._weatherMaterial.apply( state._weather );
                state._weatherScene.apply( manager, state._weather );
            } );
    }

    void GameStateComponent::rebindWeatherCatalog()
    {
        shared_ptr<const WeatherCatalog> pLatest = _weatherCatalog.snapshot();
        if ( pLatest == nullptr || pLatest == _pWeatherCatalog )
            return;
        const shared_ptr<const WeatherCatalog> pPrevious = std::move( _pWeatherCatalog ); // 다시 묶을 때까지 앞 판을 쥔다 — 날씨가 그 정의를 가리킨다
        _pWeatherCatalog                                 = std::move( pLatest );
        _weatherMaterial.close();
        _weatherScene.close( getOwner() != nullptr ? getOwner()->getMutableManager() : nullptr );
        _restore.rebind( *this, [this]()
        { _weather.initialize( _pWeatherCatalog.get(), _weatherSeed, _clock.getSeasonName() ); } );
        _weatherMaterial.open( _pWeatherCatalog.get() );
        _weatherScene.open( _pWeatherCatalog.get() );
    }

    void GameStateComponent::onEndPlay()
    {
        _weatherMaterial.close();
        _weatherScene.close( getOwner() != nullptr ? getOwner()->getMutableManager() : nullptr );
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
        const bool bRead = _restore.serializeSections( io, kStateFormat, GameStateComponentInternal::kSectionCount, forEachSection, "the session state" );
        if ( io.isReading() && bRead )
            _listClockEvent.clear();
    }

    void GameStateComponent::restoreState( vector<uint8>&& bytes )
    {
        _restore.receive( *this, std::move( bytes ) );
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

    float32 GameStateComponent::readWeather( const GameObjectManager& manager, const hashed_string& valueName )
    {
        const GameStateComponent* pState = findOpen( &manager );
        return pState != nullptr ? pState->getWeather().computeValue( valueName ) : 0.0f;
    }

    GameFlags* GameStateComponent::GameFlagsOwnerPort::findOpenFlags()
    {
        return getPortOwner<GameStateComponent>( *this, offsetof( GameStateComponent, _gameFlagsOwnerPort ) ).findOpenFlags();
    }
} // namespace sw
