/**
 * @file LockstepSkirmishWatches.cpp
 * @brief 락스텝 교전 게임 모드(`LockstepSkirmishGameModeComponent`)의 자동화 감시 값(`Skirmish.*`)입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"

#include "Games/Playground/StarSkirmish/LockstepSkirmishGameModeComponent.h"

namespace sw
{
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
} // namespace sw
