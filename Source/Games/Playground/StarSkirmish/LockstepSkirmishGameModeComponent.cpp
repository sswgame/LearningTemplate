#include "pch.h"

#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimeSeconds.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Component/SkirmishGameModeComponent.h"

#include "Games/Playground/StarSkirmish/SkirmishLockstepLink.h"

namespace sw
{
    /**
     * @class LockstepSkirmishGameModeComponent
     * @brief 넷 세션이 열렸으면 서버(리슨 호스트)가 0 번, 붙은 클라이언트가 1 번을 맡아 같은 판을 같은 걸음으로 돕니다. 자동 플레이면 두 AI 가 양쪽 프로세스에서 같은 규칙으로 돕니다.
     */
    REFLECT( Category = "RealTimeStrategy", DisplayName = "Lockstep Skirmish Game Mode", Tooltip = "Runs the skirmish match; with an open net session two players share only their commands (lockstep)" )
    class LockstepSkirmishGameModeComponent : public SkirmishGameModeComponent
    {
    public:
        REFLECT_BODY();

        LockstepSkirmishGameModeComponent();
        virtual ~LockstepSkirmishGameModeComponent() override;

        /** @brief 락스텝을 닫고 판을 끝냅니다. */
        void onEndPlay() override;

        /** @brief 이 화면이 맡은 플레이어입니다. 혼자 하면 0 번, 2 인 대전이면 서버 0 번 · 클라이언트 1 번입니다(세션이 닫혀도 그 판에서는 그대로다). */
        int32 getLocalPlayer() const override { return _lockstep.getLocalPlayer(); }
        /** @brief 2 인 대전이면 다음 락스텝 입력에 싣고 -1(아직 적용 전), 아니면 바탕 그대로입니다. */
        int32 submitCommand( RTSCommand&& command ) override;
        /** @brief 2 인 대전이면 거절하고 알립니다 — 두 프로세스가 같은 걸음이어야 한다. */
        bool                        submitTimeInput( bool bPauseTriggered, bool bFasterTriggered, bool bSlowerTriggered ) override;
        const SkirmishLockstepLink& getLockstep() const { return _lockstep; }

    protected:
        [[nodiscard]] optional<uint8> openPlayers( bool bAutoPlay ) override;
        void                          stepMatch( GameSeconds frameTime ) override;

    private:
        SW_TRANSIENT SkirmishLockstepLink _lockstep; ///< 2 인 대전(넷 세션이 열렸을 때)의 락스텝. 판 상태가 아니라 세이브에 싣지 않는다
    };

    namespace
    {
        struct LockstepSkirmishGameModeComponentInternal
        {
            // 두 프로세스 시나리오(`netpair/lockstep_*`)가 판정하는 감시 값. 넷 대전이 아니면 값이 없다.
            [[nodiscard]] static bool readPlayer( const LockstepSkirmishGameModeComponent& gameMode, float64& outValue )
            {
                outValue = static_cast<float64>( gameMode.getLockstep().getLocalPlayer() );
                return gameMode.getLockstep().isActive();
            }
            [[nodiscard]] static bool readTick( const LockstepSkirmishGameModeComponent& gameMode, float64& outValue )
            {
                outValue = static_cast<float64>( gameMode.getLockstep().getSession().getCurrentTick() );
                return gameMode.getLockstep().isActive();
            }
            [[nodiscard]] static bool readDesynced( const LockstepSkirmishGameModeComponent& gameMode, float64& outValue )
            {
                outValue = gameMode.getLockstep().getSession().isDesynced() ? 1.0 : 0.0;
                return gameMode.getLockstep().isActive();
            }
            [[nodiscard]] static bool readClientCommands( const LockstepSkirmishGameModeComponent& gameMode, float64& outValue )
            {
                outValue = static_cast<float64>( gameMode.getLockstep().getAppliedCommandCount( 1 ) );
                return gameMode.getLockstep().isActive();
            }
            [[nodiscard]] static bool readLocalArmy( const LockstepSkirmishGameModeComponent& gameMode, float64& outValue )
            {
                outValue = static_cast<float64>( gameMode.getMatch().makeSummary( gameMode.getLocalPlayer() )._army );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_COMPONENT_WATCH( skirmishLockstepPlayer, "Skirmish.LockstepPlayer", "Player this process runs in the network match (0 host, 1 client; no value outside one)", LockstepSkirmishGameModeComponent,
                                   &LockstepSkirmishGameModeComponentInternal::readPlayer );
    SW_AUTOMATION_COMPONENT_WATCH( skirmishLockstepTick, "Skirmish.LockstepTick", "Lockstep tick the network match reached", LockstepSkirmishGameModeComponent, &LockstepSkirmishGameModeComponentInternal::readTick );
    SW_AUTOMATION_COMPONENT_WATCH( skirmishLockstepDesynced, "Skirmish.LockstepDesynced", "1 when the checksums of the two processes differed", LockstepSkirmishGameModeComponent,
                                   &LockstepSkirmishGameModeComponentInternal::readDesynced );
    SW_AUTOMATION_COMPONENT_WATCH( skirmishLockstepClientCommands, "Skirmish.LockstepClientCommands", "Commands of player 1 (the joining client) applied by this process", LockstepSkirmishGameModeComponent,
                                   &LockstepSkirmishGameModeComponentInternal::readClientCommands );
    SW_AUTOMATION_COMPONENT_WATCH( skirmishLocalArmy, "Skirmish.LocalArmy", "Army units of this screen's player", LockstepSkirmishGameModeComponent, &LockstepSkirmishGameModeComponentInternal::readLocalArmy );

    LockstepSkirmishGameModeComponent::LockstepSkirmishGameModeComponent()
        : _lockstep{}
    {
    }

    LockstepSkirmishGameModeComponent::~LockstepSkirmishGameModeComponent() = default;

    void LockstepSkirmishGameModeComponent::onEndPlay()
    {
        _lockstep.close();
        SkirmishGameModeComponent::onEndPlay();
    }

    optional<uint8> LockstepSkirmishGameModeComponent::openPlayers( bool bAutoPlay )
    {
        if ( _lockstep.open() == false )
            return nullopt; // 까닭은 락스텝이 남겼다
        if ( _lockstep.isActive() && getMap()._listPlayerStart.size() != static_cast<size_t>( SkirmishLockstepLink::kPlayerCount ) )
        {
            SW_LOG_ERROR_ID( "LockstepSkirmish.MapPlayerCount", "[Skirmish] a network match needs a map with %# player starts, this map has %#", SkirmishLockstepLink::kPlayerCount, getMap()._listPlayerStart.size() );
            return nullopt;
        }
        if ( bAutoPlay == false && _lockstep.isActive() )
            return static_cast<uint8>( SkirmishMatch::kHumanPlayer0 | SkirmishMatch::kHumanPlayer1 );
        return SkirmishGameModeComponent::openPlayers( bAutoPlay );
    }

    void LockstepSkirmishGameModeComponent::stepMatch( GameSeconds frameTime )
    {
        const float32 deltaTime = frameTime.count();
        if ( _lockstep.releaseLostSession() )
            SW_LOG_INFO_ID( "LockstepSkirmish.PeerLost", "[Skirmish] the other player left the match - it goes on here without the other player" );
        if ( _lockstep.isActive() )
            (void)_lockstep.advance( deltaTime, getWorld().getSettings()._fixedStep, getMutableMatch() ); // 두 프로세스가 같은 걸음으로 돈다 - 속도 · 일시정지는 쓰지 않는다
        else
            SkirmishGameModeComponent::stepMatch( frameTime );
    }

    int32 LockstepSkirmishGameModeComponent::submitCommand( RTSCommand&& command )
    {
        if ( _lockstep.isActive() == false )
            return SkirmishGameModeComponent::submitCommand( std::move( command ) );
        return _lockstep.queueCommand( std::move( command ) ) ? -1 : 0;
    }

    bool LockstepSkirmishGameModeComponent::submitTimeInput( bool bPauseTriggered, bool bFasterTriggered, bool bSlowerTriggered )
    {
        if ( _lockstep.isActive() == false )
            return SkirmishGameModeComponent::submitTimeInput( bPauseTriggered, bFasterTriggered, bSlowerTriggered );
        if ( bPauseTriggered || bFasterTriggered || bSlowerTriggered )
            SW_LOG_INFO_ID( "LockstepSkirmish.TimingInNetMatch", "[Skirmish] speed and pause are off in a network match - both players run the same steps" );
        return false;
    }
} // namespace sw

#include "LockstepSkirmishGameModeComponent.cpp.gen.inl"
