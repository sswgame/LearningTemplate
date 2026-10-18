#include "pch.h"

#include "GameFramework/Base/Gameplay/SessionState/SessionStateComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/ConfigRangeUtil.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    SW_LOG_CALLER( "SessionState" );

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
                StateArchiveUtil::writeSection( outArchive, TState::kStateTag, TState::kStateVersion, body );
            }

            /** @brief 구간 하나를 사본에 읽고 끝까지 맞으면 반영합니다. 버전이 다르거나 깨졌으면 경고를 남기고 그대로 두며, 이 경우 그 상태는 새 판입니다. */
            template <typename TState>
            [[nodiscard]] static bool applyStateSection( const Section& section, TState& inoutState, const utf8* pName )
            {
                if ( section._version != TState::kStateVersion )
                {
                    SW_LOG_WARNING( "SessionState: %# section version %# is not %# - it starts fresh", pName, section._version, TState::kStateVersion );
                    return false;
                }
                Archive    body  = section._body;
                TState     state = inoutState;
                const bool bRead = state.readState( body ) && body.getRemainingBytes() == 0;
                if ( bRead == false )
                {
                    SW_LOG_WARNING( "SessionState: %# section is broken - it starts fresh", pName );
                    return false;
                }
                inoutState = std::move( state );
                return true;
            }
        };
    } // namespace
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
            SW_LOG_ERROR( "SessionState: refused to open - settings '%#'", rangeError.c_str() );
            return SessionStateInitResult::Refused;
        }
        if ( _clock.initialize( settings._clock ) == false )
            return SessionStateInitResult::Refused;
        _wallet.clear();
        _flags.clear();
        _questLog.initialize( settings._pQuestCatalog );
        _reputation.initialize( settings._pReputationCatalog );
        _inventory.initialize( settings._pItemCatalog, settings._inventorySlotCount );
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

    void SessionStateComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터와 같은 그룹이다. 같은 오브젝트 안에서는 붙은 순서대로 처리하며, 이 컴포넌트가 맨 앞이다.
        setTickGroup( TickGroup::PrePhysics );
    }

    void SessionStateComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        _listClockEvent.clear();
        if ( _bInitialized == SW_FALSE )
            return;
        _clock.update( deltaTime );
        _clock.drainEvents( _listClockEvent );
        // 공유 평판은 공유 날짜가 넘어갈 때 한 번만 식힌다. 참조해서 쓰는 키트는 식히지 않는다. 키트 수만큼 반복해서 식지 않게 하기 위해서다.
        for ( const WorldClockEvent& clockEvent : _listClockEvent )
        {
            if ( clockEvent._kind == WorldClockEvent::Kind::DayChanged )
                _reputation.advanceDay();
        }
        if ( _bWeather == SW_TRUE && _clock.isPaused() == false )
            (void)_weather.update( deltaTime * _clock.getTimeScale() * 86400.0f / _clock.getSettings()._secondsPerDay, _clock.getSeasonName() );
    }

    void SessionStateComponent::writeState( Archive& outArchive ) const
    {
        // 열리기 전(복원한 직후의 두 번째 캡처)이면 받은 바이트가 곧 상태이다. 빈 판을 쓰면 세이브를 잃는다.
        if ( _bInitialized == SW_FALSE && _pendingStateBytes.empty() == false )
        {
            outArchive.writeBytes( _pendingStateBytes.data(), _pendingStateBytes.size() );
            return;
        }
        StateArchiveUtil::writeHeader( outArchive, kStateTag, kStateVersion );
        outArchive << SessionStateComponentInternal::kSectionCount;
        SessionStateComponentInternal::writeStateSection( outArchive, _wallet );
        SessionStateComponentInternal::writeStateSection( outArchive, _flags );
        SessionStateComponentInternal::writeStateSection( outArchive, _clock );
        SessionStateComponentInternal::writeStateSection( outArchive, _questLog );
        SessionStateComponentInternal::writeStateSection( outArchive, _reputation );
        SessionStateComponentInternal::writeStateSection( outArchive, _inventory );
        SessionStateComponentInternal::writeStateSection( outArchive, _weather );
        SessionStateComponentInternal::writeStateSection( outArchive, _land );
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

    SessionStateComponent* SessionStateComponent::findOnOwner( const Component& component )
    {
        GameObject* pOwner = component.getOwner();
        return pOwner != nullptr ? pOwner->getComponent<SessionStateComponent>() : nullptr;
    }

    bool SessionStateComponent::applyPendingState()
    {
        Archive    archive( _pendingStateBytes.data(), _pendingStateBytes.size() );
        const bool bRestored = readState( archive );
        _pendingStateBytes.clear();
        if ( bRestored == false )
            SW_LOG_ERROR( "SessionState: the saved shared state could not be read - the game state starts fresh" );
        return bRestored;
    }

    bool SessionStateComponent::readState( Archive& archive )
    {
        if ( StateArchiveUtil::readHeader( archive, kStateTag, kStateVersion ) == false )
            return false;
        uint32 sectionCount = 0;
        // 구간마다 태그, 버전, 길이(12바이트) 이상
        if ( StateArchiveUtil::readCount( archive, 12, sectionCount ) == false )
            return false;
        // 구조를 끝까지 먼저 읽는다. 구조가 깨졌으면 아무것도 바꾸지 않는다.
        vector<SessionStateComponentInternal::Section> listSection;
        listSection.resize( sectionCount );
        for ( SessionStateComponentInternal::Section& section : listSection )
        {
            if ( StateArchiveUtil::readSection( archive, section._tag, section._version, section._body ) == false )
                return false;
        }
        if ( archive.getRemainingBytes() != 0 )
            return false;

        // 구간 하나가 깨지면 applyStateSection 이 경고를 남기고 그 구간만 새 판으로 둔다. 반환값은 쓰지 않는다.
        for ( const SessionStateComponentInternal::Section& section : listSection )
        {
            switch ( section._tag )
            {
                case Wallet::kStateTag:
                {
                    // 실패는 applyStateSection 이 경고로 남기고 그 상태는 새로 시작한다
                    (void)SessionStateComponentInternal::applyStateSection( section, _wallet, "wallet" );
                    break;
                }
                case GameFlags::kStateTag:
                {
                    (void)SessionStateComponentInternal::applyStateSection( section, _flags, "flags" ); // 실패는 applyStateSection 이 경고로 남기고 그 상태는 새로 시작한다
                    break;
                }
                case WorldClock::kStateTag:
                {
                    (void)SessionStateComponentInternal::applyStateSection( section, _clock, "clock" ); // 실패는 applyStateSection 이 경고로 남기고 그 상태는 새로 시작한다
                    break;
                }
                case QuestLog::kStateTag:
                {
                    // 실패는 applyStateSection 이 경고로 남기고 그 상태는 새로 시작한다
                    (void)SessionStateComponentInternal::applyStateSection( section, _questLog, "quest log" );
                    break;
                }
                case ReputationState::kStateTag:
                {
                    // 실패는 applyStateSection 이 경고로 남기고 그 상태는 새로 시작한다
                    (void)SessionStateComponentInternal::applyStateSection( section, _reputation, "reputation" );
                    break;
                }
                case Inventory::kStateTag:
                {
                    // 실패는 applyStateSection 이 경고로 남기고 그 상태는 새로 시작한다
                    (void)SessionStateComponentInternal::applyStateSection( section, _inventory, "inventory" );
                    break;
                }
                case WeatherSystem::kStateTag:
                {
                    // 실패는 applyStateSection 이 경고로 남기고 그 상태는 새로 시작한다
                    (void)SessionStateComponentInternal::applyStateSection( section, _weather, "weather" );
                    break;
                }
                case LandRegistry::kStateTag:
                {
                    (void)SessionStateComponentInternal::applyStateSection( section, _land, "land" ); // 실패는 applyStateSection 이 경고로 남기고 그 상태는 새로 시작한다
                    break;
                }
                default:
                {
                    SW_LOG_WARNING( "SessionState: unknown section %# skipped", section._tag );
                    break;
                }
            }
        }
        _listClockEvent.clear();
        return true;
    }
} // namespace sw
