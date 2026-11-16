#include "pch.h"

#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/GameplayDebugTarget.h"
#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"

namespace sw
{
    namespace
    {
        struct SessionStateComponentInternal
        {
            static constexpr uint32 kSectionCount = 8;

            /** @brief 읽은 구간 하나입니다. 본문은 읽은 바이트를 가리키는 뷰입니다. */
            struct Section
            {
                Archive _body{};
                uint32  _tag{ 0 };
                uint32  _version{ 0 };
            };

            /** @brief 상태 하나를 그 타입의 태그와 버전으로 구간 하나에 씁니다. */
            template <typename TState>
            static void writeStateSection( Archive& outArchive, const TState& state )
            {
                Archive body;
                state.writeState( body );
                StateIO::writeSection( outArchive, TState::kStateTag, TState::kStateVersion, body );
            }

            /**
             * @brief 구간 하나를 사본에 읽고 끝까지 맞으면 반영합니다. 옛 판은 판 올림 사슬(`SW_STATE_FORMAT_UPGRADE`)로 올려 읽습니다(감사 A-6).
             *        올릴 단계가 없거나 깨졌으면 경고를 남기고 그대로 둔 채 false 입니다(부르는 쪽이 로드 전체를 실패시킨다).
             */
            template <typename TState>
            [[nodiscard]] static bool applyStateSection( const Section& section, TState& inoutState, const utf8* pName )
            {
                Archive body = section._body;
                // 머리로 판을 올리는 형식(`kStateHeaderlessVersion` — 세계 시계)은 안쪽 머리가 올린다 — 구간 판으로 또 올리면 두 번 오른다.
                constexpr bool bVersionedByHeader = StateIOInternal::HasHeaderlessVersion<TState>::value;
                if ( bVersionedByHeader == false && StateIO::upgradeSectionBody( TState::kStateTag, section._version, TState::kStateVersion, body ) == false )
                {
                    SW_LOG_WARNING_ID( "SessionState.SessionStateSectionVersionNot", "SessionState: %# section version %# is not %# and has no upgrade chain to it", pName, section._version,
                                       TState::kStateVersion );
                    return false;
                }
                TState     state = inoutState;
                const bool bRead = state.readState( body ) && body.getRemainingBytes() == 0;
                if ( bRead == false )
                {
                    SW_LOG_WARNING_ID( "SessionState.SessionStateSectionBrokenStarts", "SessionState: %# section is broken", pName );
                    return false;
                }
                inoutState = std::move( state );
                return true;
            }
        };

        struct SessionStateDevCommandsInternal
        {
            /** @brief 활성 씬에서 열린 첫 판 상태입니다. 없으면 답을 채우고 nullptr 입니다. */
            static SessionStateComponent* findSessionState( string& outReply )
            {
                SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                GameObjectManager*  pManager      = pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
                if ( pManager == nullptr )
                {
                    outReply = "cheat: no active scene";
                    return nullptr;
                }
                SessionStateComponent* pFound = nullptr;
                for ( SessionStateComponent* pComponent : pManager->getComponentRegistry().getAll<SessionStateComponent>() )
                {
                    if ( pComponent->isInitialized() )
                    {
                        pFound = pComponent;
                        break;
                    }
                }
                if ( pFound == nullptr )
                    outReply = "cheat: no initialized SessionStateComponent in the active scene";
                return pFound;
            }

            /** @brief `cheat.give <itemID> [count]` — 판 상태의 가방에 아이템을 넣습니다(슬롯 · 무게가 모자라면 일부만). */
            static bool runGive( const vector<string>& listArgument, string& outReply )
            {
                int32      count                  = 1;
                const bool bInvalidCountArguments = listArgument.empty() || listArgument.size() > 2 || ( listArgument.size() == 2 && StringUtil::parseInt( listArgument[1], count ) == false ) || count < 1;
                if ( bInvalidCountArguments )
                    return false;
                SessionStateComponent* pState = findSessionState( outReply );
                if ( pState == nullptr )
                    return true;
                Inventory& inventory = pState->getInventory();
                if ( inventory.getSlotCount() == 0 )
                {
                    outReply = "cheat.give: the session state has no inventory (slot count 0)";
                    return true;
                }
                const int32                              addedCount = inventory.addItem( hashed_string( listArgument[0] ), count );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "cheat.give added %# of %# '%#'", addedCount, count, listArgument[0] );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `cheat.money <amount> [currency]` — 지갑에 돈을 더합니다(양수만). 통화를 적지 않으면 기본 통화입니다. */
            static bool runMoney( const vector<string>& listArgument, string& outReply )
            {
                int64      amount                  = 0;
                const bool bInvalidAmountArguments = listArgument.empty() || listArgument.size() > 2 || StringUtil::parseInt64( listArgument[0], amount ) == false || amount <= 0;
                if ( bInvalidAmountArguments )
                    return false;
                SessionStateComponent* pState = findSessionState( outReply );
                if ( pState == nullptr )
                    return true;
                const hashed_string currency = listArgument.size() == 2 ? hashed_string( listArgument[1] ) : Wallet::getDefaultCurrency();
                Wallet&             wallet   = pState->getWallet();
                wallet.add( currency, amount );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "cheat.money balance %#%#", wallet.getBalance( currency ), ( listArgument.size() == 2 ? " " + listArgument[1] : string( " (default currency)" ) ) );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `cheat.weather <weatherID> [duration]` — 판 상태의 날씨를 곧바로 바꿉니다(전환 없이). 기간을 빼면 정의의 범위에서 고른다. 표면 젖음 · 강수 시나리오가 쓴다. */
            static bool runWeather( const vector<string>& listArgument, string& outReply )
            {
                float32    duration                 = 0.0f;
                const bool bInvalidWeatherArguments = listArgument.empty() || listArgument.size() > 2 || ( listArgument.size() == 2 && ( StringUtil::parseFloat( listArgument[1], duration ) == false || duration < 0.0f ) );
                if ( bInvalidWeatherArguments )
                    return false;
                SessionStateComponent* pState = findSessionState( outReply );
                if ( pState == nullptr )
                    return true;
                WeatherSystem&      weather   = pState->getWeather();
                const hashed_string weatherID = hashed_string( listArgument[0] );
                weather.forceWeather( weatherID, duration, true );
                outReply = weather.getCurrent() == weatherID ? "cheat.weather " + listArgument[0] : "cheat.weather: the session state has no weather '" + listArgument[0] + "' (no weather catalog or unknown id)";
                return true;
            }
        };
    } // namespace

    // 등록 줄은 기능 파일로 옮길 때(165e16647c) 빠졌다 — 함수만 남아 명령이 없었다.
    SW_DEV_COMMAND( CheatGive, "cheat.give", "cheat.give <itemID> [count]", "Add items to the session state's inventory (partly when slots or weight run out)",
                    &SessionStateDevCommandsInternal::runGive );
    SW_DEV_COMMAND( CheatMoney, "cheat.money", "cheat.money <amount> [currency]", "Add money to the session state's wallet (default currency when none is given)",
                    &SessionStateDevCommandsInternal::runMoney );
    SW_DEV_COMMAND( CheatWeather, "cheat.weather", "cheat.weather <weatherID> [duration]", "Switch the session state's weather now, without the transition (duration in game seconds; none picks from the definition)",
                    &SessionStateDevCommandsInternal::runWeather );
} // namespace sw

namespace sw
{
    SessionStateComponent::SessionStateComponent()
        : _wallet{}
        , _flags{}
        , _clock{}
        , _questLog{}
        , _reputation{}
        , _inventory{}
        , _weather{}
        , _land{}
        , _listClockEvent{}
        , _pendingStateBytes{}
        , _bInitialized{ SW_FALSE }
        , _bFreshGame{ SW_FALSE }
        , _bWeather{ SW_FALSE }
        , _bOpenAtBeginPlay{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    SessionStateComponent::~SessionStateComponent() = default;

    SessionStateInitResult SessionStateComponent::initialize( const SessionStateSettings& settings )
    {
        if ( _bInitialized == SW_TRUE )
            return SessionStateInitResult::AlreadyInitialized;
        // 값을 범위 안으로 맞춰 열지 않는다. 시계 설정 안까지 보며, 상태를 바꾸기 전에 본다.
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "SessionState.SessionStateRefusedOpenSettings", "SessionState: refused to open - settings '%#'", rangeError.c_str() );
            return SessionStateInitResult::Refused;
        }
        if ( _clock.initialize( settings._clock ) == false )
            return SessionStateInitResult::Refused;
        _wallet.clear();
        _flags.clear();
        _questLog.initialize( settings._pQuestCatalog );
        _reputation.initialize( settings._pReputationCatalog );
        if ( _inventory.initialize( settings._pItemCatalog, settings._inventorySlotCount ) == false )
            return SessionStateInitResult::Refused;
        _weather.initialize( settings._pWeatherCatalog, settings._weatherSeed, _clock.getSeasonName() );
        _bWeather = settings._pWeatherCatalog != nullptr ? SW_TRUE : SW_FALSE;
        _land.initialize( settings._landWidth, settings._landHeight, settings._landCellSize, settings._landOrigin );
        _listClockEvent.clear();
        _bInitialized = SW_TRUE;
        _bFreshGame   = SW_TRUE;
        if ( _pendingStateBytes.empty() )
            return SessionStateInitResult::Fresh;
        if ( applyPendingState() == false )
            return SessionStateInitResult::Fresh;
        _bFreshGame = SW_FALSE;
        return SessionStateInitResult::Restored;
    }

    void SessionStateComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<SessionStateComponent>( this );
    }

    void SessionStateComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<SessionStateComponent>( this );
        Component::onUnregister( manager );
    }

    void SessionStateComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터와 같은 그룹이다. 같은 오브젝트 안에서는 붙은 순서대로 처리하며, 이 컴포넌트가 맨 앞이다.
        setTickGroup( TickGroup::PrePhysics );
        // 디렉터 없는 씬(시연 · 시험)만 데이터로 연다 — 디렉터가 있으면 그 설정으로 디렉터가 연다.
        if ( _bOpenAtBeginPlay == SW_TRUE && initialize( SessionStateSettings{} ) == SessionStateInitResult::Refused )
            SW_LOG_ERROR_ID( "SessionState.OpenAtBeginPlayRefused", "SessionState: could not open with default settings at begin play" );
    }

    void SessionStateComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _listClockEvent.clear();
        if ( _bInitialized == SW_FALSE )
            return;
        _clock.update( context.getDeltaTime() );
        _clock.pollEvents( _listClockEvent );
        // 공유 평판은 공유 날짜가 넘어갈 때 한 번만 식힌다. 참조해서 쓰는 키트는 식히지 않는다. 키트 수만큼 반복해서 식지 않게 하기 위해서다.
        for ( const WorldClockEvent& clockEvent : _listClockEvent )
        {
            if ( clockEvent._kind == WorldClockEvent::Kind::DayChanged )
                _reputation.advanceDay();
        }
        if ( _bWeather == SW_TRUE && _clock.isPaused() == false )
            (void)_weather.update( context.getDeltaTime() * _clock.getTimeScale() * 86400.0f / _clock.getSettings()._secondsPerDay, _clock.getSeasonName() );
    }

    void SessionStateComponent::serializeState( StateIO& io )
    {
        // 구간 표는 여기 한 곳이다. 쓰기는 이 순서로 싣고, 읽기는 표로 찾아 넘긴다.
        const auto forEachSection = [this]( auto&& visit )
        {
            visit( _wallet, "wallet" );
            visit( _flags, "flags" );
            visit( _clock, "clock" );
            visit( _questLog, "quest log" );
            visit( _reputation, "reputation" );
            visit( _inventory, "inventory" );
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
            StateIO::writeHeader( archive, kStateTag, kStateVersion );
            archive << SessionStateComponentInternal::kSectionCount;
            forEachSection( [&archive]( const auto& state, const utf8* )
            { SessionStateComponentInternal::writeStateSection( archive, state ); } );
            return;
        }
        io.header( kStateTag, kStateVersion );
        Archive& archive      = io.getArchive(); // 머리 뒤에 받는다 — 옛 판이면 올린 바이트다
        uint32   sectionCount = 0;
        // 구간마다 태그, 버전, 길이(12바이트) 이상
        io.count( 12, sectionCount );
        if ( io.isOk() == false )
            return;
        // 구조를 끝까지 먼저 읽는다. 구조가 깨졌으면 아무것도 바꾸지 않는다.
        vector<SessionStateComponentInternal::Section> listSection;
        listSection.resize( sectionCount );
        for ( SessionStateComponentInternal::Section& section : listSection )
        {
            if ( io.check( StateIO::readSection( archive, section._tag, section._version, section._body ) ) == false )
                return;
        }
        if ( io.check( archive.getRemainingBytes() == 0 ) == false )
            return;
        // 구간 하나라도 읽지 못하면 로드 전체가 실패다(감사 P0-5) — 예전에는 그 구간만 경고하고 새 판으로 두어, 가방 슬롯 수 같은 설정을 바꾼 패치 뒤
        // 인벤토리 · 땅 · 달력이 통째로 사라졌고 이어진 저장이 그것을 굳혔다. 읽기 전 상태를 같은 구간 형식으로 떠 두었다가 실패하면 되돌린다(전부 아니면 없음).
        Archive backup;
        forEachSection( [&backup]( const auto& state, const utf8* )
        { SessionStateComponentInternal::writeStateSection( backup, state ); } );
        bool bAllApplied = true;
        for ( const SessionStateComponentInternal::Section& section : listSection )
        {
            bool bKnown = false;
            forEachSection( [&section, &bKnown, &bAllApplied]( auto& state, const utf8* pName )
            {
                if ( section._tag == std::decay_t<decltype( state )>::kStateTag )
                {
                    if ( SessionStateComponentInternal::applyStateSection( section, state, pName ) == false )
                        bAllApplied = false;
                    bKnown = true;
                }
            } );
            if ( bKnown == false )
            {
                SW_LOG_WARNING_ID( "SessionState.SessionStateUnknownSectionSkipped", "SessionState: unknown section %# skipped", section._tag );
            }
        }
        if ( bAllApplied == false )
        {
            // 되돌린다 — 방금 떠 둔 구간이라 반드시 읽힌다.
            for ( uint32 sectionIndex = 0; sectionIndex < SessionStateComponentInternal::kSectionCount && backup.getRemainingBytes() > 0; ++sectionIndex )
            {
                SessionStateComponentInternal::Section section;
                if ( StateIO::readSection( backup, section._tag, section._version, section._body ) == false )
                    break;
                forEachSection( [&section]( auto& state, const utf8* pName )
                {
                    if ( section._tag == std::decay_t<decltype( state )>::kStateTag )
                        (void)SessionStateComponentInternal::applyStateSection( section, state, pName ); // 같은 판으로 떠 둔 것이다
                } );
            }
            SW_LOG_ERROR_ID( "SessionState.LoadRejected", "SessionState: a section of the save could not be read - the save is not loaded (the session state is unchanged)" );
            io.fail();
            return;
        }
        _listClockEvent.clear();
    }

    void SessionStateComponent::restoreState( vector<uint8>&& bytes )
    {
        _pendingStateBytes = std::move( bytes );
        if ( _bInitialized == SW_TRUE && applyPendingState() )
            _bFreshGame = SW_FALSE;
    }

    SessionStateRefs SessionStateComponent::makeRefs()
    {
        SessionStateRefs refs;
        refs._pWallet     = &_wallet;
        refs._pFlags      = &_flags;
        refs._pClock      = &_clock;
        refs._pQuestLog   = &_questLog;
        refs._pReputation = &_reputation;
        refs._pInventory  = _inventory.getSlotCount() > 0 ? &_inventory : nullptr;
        refs._pWeather    = _bWeather == SW_TRUE ? &_weather : nullptr;
        refs._pLand       = _land.isInitialized() ? &_land : nullptr;
        return refs;
    }

    SessionStateComponent* SessionStateComponent::openOnOwner( const Component& component, const SessionStateSettings& settings, SessionStateInitResult& outResult )
    {
        outResult                     = SessionStateInitResult::Refused;
        SessionStateComponent* pState = findOnOwner( component );
        if ( pState == nullptr )
        {
            SW_LOG_ERROR_ID( "SessionState.MissingOnOwner", "no SessionStateComponent in front of the game mode on this object - the game cannot start" );
            return nullptr;
        }
        outResult = pState->initialize( settings );
        return outResult != SessionStateInitResult::Refused ? pState : nullptr; // 거절한 까닭은 `initialize` 가 남겼다
    }

    SessionStateComponent* SessionStateComponent::openOnOwner( const Component& component, const SessionStateSettings& settings )
    {
        SessionStateInitResult result = SessionStateInitResult::Refused;
        return openOnOwner( component, settings, result );
    }

    SessionStateComponent* SessionStateComponent::findOnOwner( const Component& component )
    {
        GameObject* pOwner = component.getOwner();
        return pOwner != nullptr ? pOwner->getComponent<SessionStateComponent>() : nullptr;
    }

    void SessionStateComponent::collectOpen( const GameObjectManager* pManager, vector<SessionStateComponent*>& outListState )
    {
        outListState.clear();
        if ( pManager == nullptr )
            return;
        for ( SessionStateComponent* pState : pManager->getComponentRegistry().getAll<SessionStateComponent>() )
        {
            if ( pState->isInitialized() )
                outListState.push_back( pState );
        }
    }

    const SessionStateComponent* SessionStateComponent::findFirstOpen( const GameObjectManager* pManager )
    {
        if ( pManager == nullptr )
            return nullptr;
        for ( const SessionStateComponent* pState : pManager->getComponentRegistry().getAll<SessionStateComponent>() )
        {
            if ( pState->isInitialized() )
                return pState;
        }
        return nullptr;
    }

    bool SessionStateComponent::applyPendingState()
    {
        Archive    archive( _pendingStateBytes.data(), _pendingStateBytes.size() );
        const bool bRestored = readState( archive );
        _pendingStateBytes.clear();
        if ( bRestored == false )
            SW_LOG_ERROR_ID( "SessionState.SessionStateSavedSharedState", "SessionState: the saved shared state could not be read - the game state starts fresh" );
        return bRestored;
    }

} // namespace sw

namespace sw
{
    void SessionStateComponent::writeProgressDebugLines( DebugHUDSectionWriter& writer, uint32 maxLines ) const
    {
        uint32     lineCount = 0;
        uint32     skipped   = 0;
        const auto add       = [&]( const string& label, const string& value )
        {
            if ( lineCount >= maxLines )
            {
                ++skipped;
                return;
            }
            writer.addLine( label, value );
            ++lineCount;
        };
        vector<GameFlagEntry> listFlag;
        _flags.fillEntries( listFlag );
        for ( const GameFlagEntry& flag : listFlag )
        {
            add( string( "flag " ) + flag._name.c_str(), to_string( flag._value ) );
        }
        for ( const WalletBalance& balance : _wallet.getBalances() )
        {
            add( string( "wallet " ) + balance._currency.c_str(), to_string( balance._amount ) );
        }
        for ( int32 slot = 0; slot < _inventory.getSlotCount(); ++slot )
        {
            const InventorySlot& content = _inventory.getSlot( slot );
            if ( content.isEmpty() == false )
                add( "slot " + to_string( slot ), string( content._itemID.c_str() ) + " x" + to_string( content._count ) );
        }
        if ( skipped > 0 )
            writer.addLine( "...", to_string( skipped ) + " more" );
    }
} // namespace sw

namespace sw
{
    namespace
    {
        /** @brief 판 상태를 읽는 게임플레이 디버그 섹션(퀘스트 · 진행 조건)입니다. 대상은 `debug.target`, 없으면 첫 열린 판 상태입니다. */
        struct SessionStateDebugSectionsInternal
        {
            static const SessionStateComponent* findTarget( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* const pManager = GameplayDebugTarget::findActiveObjectManager();
                const SessionStateComponent*   pState   = pManager != nullptr ? GameplayDebugTarget::findTargetComponent<SessionStateComponent>( writer.getSceneIterationKey(), *pManager ) : nullptr;
                if ( pState == nullptr || pState->isInitialized() == false )
                {
                    writer.addLine( "target", "none (no open game state in the active scene)" );
                    return nullptr;
                }
                writer.addLine( "target", pState->getOwner() != nullptr ? string( pState->getOwner()->getName().c_str() ) : string( "?" ) );
                return pState;
            }

            /** @brief 진행 중 퀘스트의 단계 · 목표 달성도, 받지 않은 퀘스트의 잠금 사유입니다(레벨 조건은 판정하지 않고 표시만 — 판 상태는 레벨을 모른다). */
            static void writeQuest( DebugHUDSectionWriter& writer )
            {
                if ( const SessionStateComponent* pState = findTarget( writer ) )
                    pState->getQuestLog().writeDebugLines( writer, MathUtil::kMaxInt32, GameplayDebugTarget::kMaxLines );
            }

            static void writeProgress( DebugHUDSectionWriter& writer )
            {
                if ( const SessionStateComponent* pState = findTarget( writer ) )
                    pState->writeProgressDebugLines( writer, GameplayDebugTarget::kMaxLines );
            }
        };
    } // namespace

    SW_DEBUG_HUD_SECTION( quest, "quest", "Quests", 200, DebugHUDSectionFlag::kNone, &SessionStateDebugSectionsInternal::writeQuest );
    SW_DEBUG_HUD_SECTION( progress, "progress", "Progress (flags, wallet, bag)", 210, DebugHUDSectionFlag::kNone, &SessionStateDebugSectionsInternal::writeProgress );
} // namespace sw
