#include "pch.h"

#include "GameFramework/Base/GameState/GameStateComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Utility/StateArchiveUtil.h"

namespace sw
{
    SW_LOG_CALLER( "GameState" );

    namespace
    {
        struct GameStateComponentInternal
        {
            static constexpr uint32 kSectionCount = 5;

            /** @brief 읽은 구간 하나 — 본문은 읽은 바이트의 보기입니다. */
            struct Section
            {
                Archive _body{};
                uint32  _tag{ 0 };
                uint32  _version{ 0 };
            };

            /** @brief 상태 하나를 그 타입의 표 · 판으로 구간 하나에 씁니다. */
            template <typename TState>
            static void writeStateSection( Archive& outArchive, const TState& state )
            {
                Archive body;
                state.writeState( body );
                StateArchiveUtil::writeSection( outArchive, TState::kStateTag, TState::kStateVersion, body );
            }

            /** @brief 구간 하나를 사본에 읽어 끝까지 맞으면 바꿉니다. 판이 다르거나 깨졌으면 알리고 그대로 둡니다(새 판). */
            template <typename TState>
            [[nodiscard]] static bool applyStateSection( const Section& section, TState& inoutState, const utf8* pName )
            {
                if ( section._version != TState::kStateVersion )
                {
                    SW_LOG_WARNING( "GameState: %# section version %# is not %# - it starts fresh", pName, section._version, TState::kStateVersion );
                    return false;
                }
                Archive    body  = section._body;
                TState     state = inoutState;
                const bool bRead = state.readState( body ) && body.getRemainingBytes() == 0;
                if ( bRead == false )
                {
                    SW_LOG_WARNING( "GameState: %# section is broken - it starts fresh", pName );
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
    GameStateComponent::GameStateComponent()
        : _wallet{}
        , _flags{}
        , _clock{}
        , _questLog{}
        , _reputation{}
        , _listClockEvent{}
        , _pendingStateBytes{}
        , _bInitialized{ SW_FALSE }
        , _bFreshGame{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    GameStateComponent::~GameStateComponent() = default;

    GameStateInitResult GameStateComponent::initialize( const GameStateSettings& settings )
    {
        if ( _bInitialized == SW_TRUE )
            return GameStateInitResult::AlreadyInitialized;
        _wallet.clear();
        _flags.clear();
        _clock.initialize( settings._clock );
        _questLog.initialize( settings._pQuestCatalog );
        _reputation.initialize( settings._pReputationCatalog );
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

    void GameStateComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터와 같은 그룹 — 같은 오브젝트 안에서는 붙은 순서로 돈다(이 컴포넌트가 맨 앞).
        setTickGroup( TickGroup::PrePhysics );
    }

    void GameStateComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        _listClockEvent.clear();
        if ( _bInitialized == SW_FALSE )
            return;
        _clock.update( deltaTime );
        _clock.drainEvents( _listClockEvent );
    }

    void GameStateComponent::writeState( Archive& outArchive ) const
    {
        // 열리기 전(되살린 직후 두 번째 캡처)이면 받은 바이트가 곧 상태다 — 빈 판을 쓰면 세이브를 잃는다.
        if ( _bInitialized == SW_FALSE && _pendingStateBytes.empty() == false )
        {
            outArchive.writeBytes( _pendingStateBytes.data(), _pendingStateBytes.size() );
            return;
        }
        StateArchiveUtil::writeHeader( outArchive, kStateTag, kStateVersion );
        outArchive << GameStateComponentInternal::kSectionCount;
        GameStateComponentInternal::writeStateSection( outArchive, _wallet );
        GameStateComponentInternal::writeStateSection( outArchive, _flags );
        GameStateComponentInternal::writeStateSection( outArchive, _clock );
        GameStateComponentInternal::writeStateSection( outArchive, _questLog );
        GameStateComponentInternal::writeStateSection( outArchive, _reputation );
    }

    void GameStateComponent::restoreState( vector<uint8>&& bytes )
    {
        _pendingStateBytes = std::move( bytes );
        if ( _bInitialized == SW_TRUE && applyPendingState() )
            _bFreshGame = SW_FALSE;
    }

    GameStateRefs GameStateComponent::makeRefs()
    {
        GameStateRefs refs;
        refs._pWallet     = &_wallet;
        refs._pFlags      = &_flags;
        refs._pClock      = &_clock;
        refs._pQuestLog   = &_questLog;
        refs._pReputation = &_reputation;
        return refs;
    }

    GameStateComponent* GameStateComponent::findOnOwner( const Component& component )
    {
        GameObject* pOwner = component.getOwner();
        return pOwner != nullptr ? pOwner->getComponent<GameStateComponent>() : nullptr;
    }

    bool GameStateComponent::applyPendingState()
    {
        Archive    archive( _pendingStateBytes.data(), _pendingStateBytes.size() );
        const bool bRestored = readState( archive );
        _pendingStateBytes.clear();
        if ( bRestored == false )
            SW_LOG_ERROR( "GameState: the saved shared state could not be read - the game state starts fresh" );
        return bRestored;
    }

    bool GameStateComponent::readState( Archive& archive )
    {
        if ( StateArchiveUtil::readHeader( archive, kStateTag, kStateVersion ) == false )
            return false;
        uint32 sectionCount = 0;
        // 구간마다 표 · 판 · 길이(12) 이상
        if ( StateArchiveUtil::readCount( archive, 12, sectionCount ) == false )
            return false;
        // 틀을 끝까지 먼저 읽는다 — 틀이 깨졌으면 아무것도 바꾸지 않는다.
        vector<GameStateComponentInternal::Section> listSection;
        listSection.resize( sectionCount );
        for ( GameStateComponentInternal::Section& section : listSection )
        {
            if ( StateArchiveUtil::readSection( archive, section._tag, section._version, section._body ) == false )
                return false;
        }
        if ( archive.getRemainingBytes() != 0 )
            return false;

        // 구간 하나가 깨지면 applyStateSection 이 알리고 그 구간만 새 판으로 둔다 — 결과는 버린다.
        for ( const GameStateComponentInternal::Section& section : listSection )
        {
            switch ( section._tag )
            {
                case Wallet::kStateTag:
                {
                    (void)GameStateComponentInternal::applyStateSection( section, _wallet, "wallet" );
                    break;
                }
                case GameFlags::kStateTag:
                {
                    (void)GameStateComponentInternal::applyStateSection( section, _flags, "flags" );
                    break;
                }
                case WorldClock::kStateTag:
                {
                    (void)GameStateComponentInternal::applyStateSection( section, _clock, "clock" );
                    break;
                }
                case QuestLog::kStateTag:
                {
                    (void)GameStateComponentInternal::applyStateSection( section, _questLog, "quest log" );
                    break;
                }
                case ReputationState::kStateTag:
                {
                    (void)GameStateComponentInternal::applyStateSection( section, _reputation, "reputation" );
                    break;
                }
                default:
                {
                    SW_LOG_WARNING( "GameState: unknown section %# skipped", section._tag );
                    break;
                }
            }
        }
        _listClockEvent.clear();
        return true;
    }
} // namespace sw
