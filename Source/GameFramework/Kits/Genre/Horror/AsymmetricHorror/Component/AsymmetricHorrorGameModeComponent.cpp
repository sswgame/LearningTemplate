#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/Component/AsymmetricHorrorGameModeComponent.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"

namespace sw
{
    namespace
    {
        struct AsymmetricHorrorGameModeComponentInternal
        {
            static constexpr FormatDef kStateFormat{ "AHGM" };
            static constexpr int32     kPlayer       = 0;  ///< 늘 사람이 잡는 생존자(카메라 · 감시 값의 주인)
            static constexpr int32     kMaxSurvivors = 32; ///< 사람 비트(`_humanMask`)에 들어가는 수

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 데이터(audioevents.xml)가 정한다.
            static constexpr const utf8* kEventHit           = "AsymmetricHorrorHit";
            static constexpr const utf8* kEventDowned        = "AsymmetricHorrorDowned";
            static constexpr const utf8* kEventHooked        = "AsymmetricHorrorHooked";
            static constexpr const utf8* kEventSacrificed    = "AsymmetricHorrorSacrificed";
            static constexpr const utf8* kEventRescued       = "AsymmetricHorrorRescued";
            static constexpr const utf8* kEventGenerator     = "AsymmetricHorrorGenerator";
            static constexpr const utf8* kEventPowered       = "AsymmetricHorrorPowered";
            static constexpr const utf8* kEventGateOpened    = "AsymmetricHorrorGateOpened";
            static constexpr const utf8* kEventEscaped       = "AsymmetricHorrorEscaped";
            static constexpr const utf8* kEventPallet        = "AsymmetricHorrorPallet";
            static constexpr const utf8* kEventVaulted       = "AsymmetricHorrorVaulted";
            static constexpr const utf8* kEventAbility       = "AsymmetricHorrorAbility";
            static constexpr const utf8* kEventSkillCheck    = "AsymmetricHorrorSkillCheck";
            static constexpr const utf8* kEventCheckPassed   = "AsymmetricHorrorSkillCheckPassed";
            static constexpr const utf8* kEventCheckFailed   = "AsymmetricHorrorSkillCheckFailed";
            static constexpr const utf8* kEventWon           = "AsymmetricHorrorWon";
            static constexpr const utf8* kEventLost          = "AsymmetricHorrorLost";
            static constexpr const utf8* kEventMissed        = "AsymmetricHorrorMissed";
            static constexpr const utf8* kEventPickedUp      = "AsymmetricHorrorPickedUp";
            static constexpr const utf8* kEventHookStage     = "AsymmetricHorrorHookStage";
            static constexpr const utf8* kEventHealed        = "AsymmetricHorrorHealed";
            static constexpr const utf8* kEventKicked        = "AsymmetricHorrorKicked";
            static constexpr const utf8* kEventHatchClosed   = "AsymmetricHorrorHatchClosed";
            static constexpr const utf8* kEventCollapse      = "AsymmetricHorrorCollapse";
            static constexpr const utf8* kEventWindowBlocked = "AsymmetricHorrorWindowBlocked";

            /** @brief 판의 알림 @p kind 에 낼 소리입니다. 소리가 없는 알림이면 nullptr 입니다(판 끝은 결과가 정한다). */
            static const utf8* findSound( AsymmetricHorrorEvent::Kind kind )
            {
                switch ( kind )
                {
                    case AsymmetricHorrorEvent::Kind::Hit:
                    {
                        return kEventHit;
                    }
                    case AsymmetricHorrorEvent::Kind::Downed:
                    {
                        return kEventDowned;
                    }
                    case AsymmetricHorrorEvent::Kind::Hooked:
                    {
                        return kEventHooked;
                    }
                    case AsymmetricHorrorEvent::Kind::Sacrificed:
                    {
                        return kEventSacrificed;
                    }
                    case AsymmetricHorrorEvent::Kind::Rescued:
                    case AsymmetricHorrorEvent::Kind::WiggledFree:
                    {
                        return kEventRescued;
                    }
                    case AsymmetricHorrorEvent::Kind::GeneratorCompleted:
                    {
                        return kEventGenerator;
                    }
                    case AsymmetricHorrorEvent::Kind::GatesPowered:
                    {
                        return kEventPowered;
                    }
                    case AsymmetricHorrorEvent::Kind::GateOpened:
                    case AsymmetricHorrorEvent::Kind::HatchOpened:
                    {
                        return kEventGateOpened;
                    }
                    case AsymmetricHorrorEvent::Kind::Escaped:
                    {
                        return kEventEscaped;
                    }
                    case AsymmetricHorrorEvent::Kind::PalletDropped:
                    case AsymmetricHorrorEvent::Kind::PalletBroken:
                    case AsymmetricHorrorEvent::Kind::KillerStunned:
                    {
                        return kEventPallet;
                    }
                    case AsymmetricHorrorEvent::Kind::Vaulted:
                    {
                        return kEventVaulted;
                    }
                    case AsymmetricHorrorEvent::Kind::KillerAbilityUsed:
                    {
                        return kEventAbility;
                    }
                    case AsymmetricHorrorEvent::Kind::SkillCheckStarted:
                    {
                        return kEventSkillCheck;
                    }
                    case AsymmetricHorrorEvent::Kind::Missed:
                    {
                        return kEventMissed;
                    }
                    case AsymmetricHorrorEvent::Kind::PickedUp:
                    {
                        return kEventPickedUp;
                    }
                    case AsymmetricHorrorEvent::Kind::HookStageAdvanced:
                    {
                        return kEventHookStage;
                    }
                    case AsymmetricHorrorEvent::Kind::Healed:
                    {
                        return kEventHealed;
                    }
                    case AsymmetricHorrorEvent::Kind::GeneratorKicked:
                    {
                        return kEventKicked;
                    }
                    case AsymmetricHorrorEvent::Kind::HatchClosed:
                    {
                        return kEventHatchClosed;
                    }
                    case AsymmetricHorrorEvent::Kind::CollapseStarted:
                    {
                        return kEventCollapse;
                    }
                    case AsymmetricHorrorEvent::Kind::WindowBlocked:
                    {
                        return kEventWindowBlocked;
                    }
                    case AsymmetricHorrorEvent::Kind::LockerGrab:       // 뒤따르는 `PickedUp` 이 낸다
                    case AsymmetricHorrorEvent::Kind::SkillCheckResult: // 성공인지가 정한다(`settle`)
                    case AsymmetricHorrorEvent::Kind::Noise:            // 살인마에게만 들리는 알림이다 — 사람은 생존자다
                    {
                        break;
                    }
                }
                return nullptr;
            }

            static const utf8* findResultName( AsymmetricHorrorResult result )
            {
                if ( result == AsymmetricHorrorResult::Win )
                    return MatchGameModeComponent::kWinResult;
                return result == AsymmetricHorrorResult::Draw ? MatchGameModeComponent::kDrawResult : MatchGameModeComponent::kLoseResult;
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( asymmetricHorrorResult, "AsymmetricHorror.Result", "Match result for the survivors (0 ongoing, 1 win, 2 draw, 3 lose)", AsymmetricHorrorGameModeComponent,
                                &AsymmetricHorrorGameModeComponent::getResult );
    SW_AUTOMATION_GETTER_WATCH( asymmetricHorrorGenerators, "AsymmetricHorror.Generators", "Generators repaired", AsymmetricHorrorGameModeComponent,
                                &AsymmetricHorrorGameModeComponent::getCompletedGenerators );
    SW_AUTOMATION_GETTER_WATCH( asymmetricHorrorPowered, "AsymmetricHorror.GatesPowered", "1 when the exit gates have power", AsymmetricHorrorGameModeComponent,
                                &AsymmetricHorrorGameModeComponent::areGatesPowered );
    SW_AUTOMATION_GETTER_WATCH( asymmetricHorrorOpenGates, "AsymmetricHorror.OpenGates", "Exit gates that stand open", AsymmetricHorrorGameModeComponent,
                                &AsymmetricHorrorGameModeComponent::countOpenGates );
    SW_AUTOMATION_GETTER_WATCH( asymmetricHorrorEscaped, "AsymmetricHorror.Escaped", "Survivors who escaped", AsymmetricHorrorGameModeComponent, &AsymmetricHorrorGameModeComponent::countEscaped );
    SW_AUTOMATION_GETTER_WATCH( asymmetricHorrorSacrificed, "AsymmetricHorror.Sacrificed", "Survivors who were sacrificed", AsymmetricHorrorGameModeComponent,
                                &AsymmetricHorrorGameModeComponent::countSacrificed );
    SW_AUTOMATION_GETTER_WATCH( asymmetricHorrorPlayerX, "AsymmetricHorror.PlayerX", "Absolute X of survivor 0 (m)", AsymmetricHorrorGameModeComponent, &AsymmetricHorrorGameModeComponent::getPlayerX );
    SW_AUTOMATION_GETTER_WATCH( asymmetricHorrorActivity, "AsymmetricHorror.PlayerActivity", "What survivor 0 does (0 nothing, 1 repair, 2 heal, 3 open a gate, 4 vault, 5 in a locker)",
                                AsymmetricHorrorGameModeComponent, &AsymmetricHorrorGameModeComponent::getPlayerActivity );

    AsymmetricHorrorGameModeComponent::AsymmetricHorrorGameModeComponent()
        : _killer{}
        , _fixedStep{ 1.0f / 30.0f }
        , _seed{ 7 }
        , _killerReleaseSeconds{ 0.0f }
        , _stage{}
        , _botSettings{}
        , _escapeReward{ 0 }
        , _pCatalog{}
        , _match{}
        , _bot{}
        , _listCommand{}
        , _listEvent{}
        , _killerRelease{}
        , _humanMask{ 0 }
        , _result{ AsymmetricHorrorResult::Ongoing }
    {
    }

    AsymmetricHorrorGameModeComponent::~AsymmetricHorrorGameModeComponent() = default;

    AsymmetricHorrorMatchSettings AsymmetricHorrorGameModeComponent::makeSettings() const
    {
        AsymmetricHorrorMatchSettings settings;
        settings._killerID  = _killer.key();
        settings._fixedStep = _fixedStep;
        settings._seed      = _seed;
        return settings;
    }

    bool AsymmetricHorrorGameModeComponent::startGame()
    {
        if ( acquireBoundCatalog( _pCatalog ) == false )
            return false;
        if ( _stage._listSurvivorStart.size() > static_cast<size_t>( AsymmetricHorrorGameModeComponentInternal::kMaxSurvivors ) )
        {
            SW_LOG_ERROR_ID( "AsymmetricHorror.TooManySurvivors", "Asymmetric horror game mode: %# survivors but a match holds at most %# - the match does not open", _stage._listSurvivorStart.size(),
                             AsymmetricHorrorGameModeComponentInternal::kMaxSurvivors );
            return false;
        }
        if ( _match.initialize( makeSettings(), _pCatalog.get() ) == false )
        {
            SW_LOG_ERROR_ID( "AsymmetricHorror.MatchRefused", "Asymmetric horror game mode: the match refused killer '%#' or the fixed step %# - the match does not open", _killer.c_str(), _fixedStep );
            return false;
        }
        if ( _stage.populate( _match ) == false )
            return false;
        _match.start();
        _bot.initialize( _botSettings );
        _listCommand.assign( _stage._listSurvivorStart.size(), AsymmetricHorrorCommand{} );
        _killerRelease.start( _killerReleaseSeconds );
        return true;
    }

    void AsymmetricHorrorGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "AsymmetricHorror.Started", "[AsymmetricHorror] %# survivors against '%#', %# generators to the gates", _match.getSurvivorCount(), _killer.c_str(), countRemainingGenerators() );
    }

    void AsymmetricHorrorGameModeComponent::onStateRestored( bool bRestored )
    {
        _bot.initialize( _botSettings ); // 살인마의 기억은 저장하지 않는다 — 다시 감지한다
        if ( bRestored == false )
            SW_LOG_WARNING_ID( "AsymmetricHorror.StateRefused", "[AsymmetricHorror] the saved match does not fit this build - the current match goes on" );
    }

    bool AsymmetricHorrorGameModeComponent::submitSurvivorInput( int32 survivor, const AsymmetricHorrorCommand& command )
    {
        const bool bKnown = 0 <= survivor && survivor < static_cast<int32>( _listCommand.size() );
        if ( isAcceptingInput() == false || bKnown == false )
            return false;
        if ( survivor != AsymmetricHorrorGameModeComponentInternal::kPlayer && command.isIdle() == false )
            _humanMask |= 1u << static_cast<uint32>( survivor );
        AsymmetricHorrorCommand& pending = _listCommand[static_cast<size_t>( survivor )];
        pending._move                    = command._move;
        pending._bUseHeld                = command._bUseHeld;
        if ( command._bUseTriggered == SW_TRUE )
            pending._bUseTriggered = SW_TRUE;
        return true;
    }

    bool AsymmetricHorrorGameModeComponent::isHuman( int32 survivor ) const
    {
        if ( isAutoPlayOn() )
            return false;
        return survivor == AsymmetricHorrorGameModeComponentInternal::kPlayer || ( _humanMask & ( 1u << static_cast<uint32>( survivor ) ) ) != 0;
    }

    void AsymmetricHorrorGameModeComponent::interact( int32 survivor )
    {
        const AsymmetricHorrorSurvivor& self      = *_match.findSurvivor( survivor );
        const InteractionProgress*      pProgress = _match.findActivityProgress( survivor );
        if ( pProgress != nullptr && _match.respondSkillCheck( survivor, pProgress->getTime() ) )
            return;
        if ( self._activity == SurvivorActivity::InLocker )
        {
            _match.exitLocker( survivor );
            return;
        }
        if ( _match.escape( survivor ) )
            return;
        for ( int32 other = 0; other < _match.getSurvivorCount(); ++other )
        {
            if ( _match.rescue( survivor, other ) )
                return;
        }
        for ( int32 gate = 0; gate < _match.getGateCount(); ++gate )
        {
            if ( _match.startOpenGate( survivor, gate ) )
                return;
        }
        for ( int32 other = 0; other < _match.getSurvivorCount(); ++other )
        {
            if ( _match.startHeal( survivor, other ) )
                return;
        }
        for ( int32 generator = 0; generator < _match.getGeneratorCount(); ++generator )
        {
            if ( _match.startRepair( survivor, generator ) )
                return;
        }
        for ( size_t pallet = 0; pallet < _stage._listPallet.size(); ++pallet )
        {
            if ( _match.dropPallet( survivor, static_cast<int32>( pallet ) ) )
                return;
        }
        for ( size_t window = 0; window < _stage._listWindow.size(); ++window )
        {
            if ( _match.vault( survivor, static_cast<int32>( window ) ) )
                return;
        }
        for ( size_t locker = 0; locker < _stage._listLocker.size(); ++locker )
        {
            if ( _match.enterLocker( survivor, static_cast<int32>( locker ) ) )
                return;
        }
    }

    void AsymmetricHorrorGameModeComponent::applyCommand( int32 survivor, const AsymmetricHorrorCommand& command, float32 deltaTime )
    {
        const AsymmetricHorrorSurvivor* const pSelf = _match.findSurvivor( survivor );
        if ( pSelf == nullptr || pSelf->isStanding() == false )
            return;
        _match.setStruggling( survivor, command._bUseHeld == SW_TRUE );
        _match.setWiggling( survivor, command._bUseHeld == SW_TRUE );
        _match.moveSurvivor( survivor, _stage.limitMove( pSelf->_position, float3{ command._move._x, 0.0f, command._move._y } ), deltaTime );
        if ( command._bUseTriggered == SW_TRUE )
            interact( survivor );
    }

    void AsymmetricHorrorGameModeComponent::updateMatch( float32 deltaTime )
    {
        for ( int32 survivor = 0; survivor < _match.getSurvivorCount(); ++survivor )
        {
            AsymmetricHorrorCommand& pending = _listCommand[static_cast<size_t>( survivor )];
            applyCommand( survivor, isHuman( survivor ) ? pending : _bot.decideSurvivor( _match, _stage, survivor ), deltaTime );
            pending._bUseTriggered = SW_FALSE; // 쓴 누름이다(봇이 움직인 걸음의 누름은 버린다)
        }
        (void)_killerRelease.update( deltaTime ); // 풀려난 뒤의 첫 걸음을 따로 알리지 않는다
        if ( _killerRelease.isActive() == false )
            _bot.stepKiller( _match, _stage, deltaTime );
        _match.update( deltaTime );
        settle();
    }

    void AsymmetricHorrorGameModeComponent::settle()
    {
        _listEvent.clear();
        _match.pollEvents( _listEvent );
        for ( const AsymmetricHorrorEvent& event : _listEvent )
        {
            using Internal      = AsymmetricHorrorGameModeComponentInternal;
            const bool  bResult = event._kind == AsymmetricHorrorEvent::Kind::SkillCheckResult;
            const bool  bCheck  = event._kind == AsymmetricHorrorEvent::Kind::SkillCheckStarted || bResult;
            const bool  bOthers = bCheck && event._actor != Internal::kPlayer; // 남의 스킬 체크는 알리지 않는다
            const utf8* pSound  = bOthers ? nullptr : Internal::findSound( event._kind );
            if ( bResult && bOthers == false )
                pSound = event._value > 0.0f ? Internal::kEventCheckPassed : Internal::kEventCheckFailed;
            if ( pSound != nullptr )
                getSoundQueue().queueEvent( pSound );
            if ( event._kind == AsymmetricHorrorEvent::Kind::GeneratorCompleted )
                SW_LOG_INFO_ID( "AsymmetricHorror.GeneratorCompleted", "[AsymmetricHorror] a generator runs (%# done)", _match.getCompletedGeneratorCount() );
            else if ( event._kind == AsymmetricHorrorEvent::Kind::Hooked )
                SW_LOG_INFO_ID( "AsymmetricHorror.Hooked", "[AsymmetricHorror] a survivor is on a hook (stage %#)", event._value );
            else if ( event._kind == AsymmetricHorrorEvent::Kind::KillerAbilityUsed )
                SW_LOG_INFO_ID( "AsymmetricHorror.KillerAbility", "[AsymmetricHorror] '%#' uses its ability for %# s", _killer.c_str(), event._value );
            else if ( event._kind == AsymmetricHorrorEvent::Kind::Escaped )
                SW_LOG_INFO_ID( "AsymmetricHorror.Escaped", "[AsymmetricHorror] a survivor escaped through the %#", event._value > 0.0f ? "hatch" : "gate" );
        }
        if ( _match.isEnded() == false )
            return;
        const int32 winner = _match.getMatch().getWinningTeam();
        if ( winner == _match.getSurvivorTeam() )
            _result = AsymmetricHorrorResult::Win;
        else
            _result = winner < 0 ? AsymmetricHorrorResult::Draw : AsymmetricHorrorResult::Lose;
        endMatch( hashed_string( AsymmetricHorrorGameModeComponentInternal::findResultName( _result ) ) );
        getSoundQueue().queueEvent( _result == AsymmetricHorrorResult::Win ? AsymmetricHorrorGameModeComponentInternal::kEventWon : AsymmetricHorrorGameModeComponentInternal::kEventLost );
        SW_LOG_INFO_ID( "AsymmetricHorror.Ended", "[AsymmetricHorror] %# - %# escaped, %# sacrificed", AsymmetricHorrorGameModeComponentInternal::findResultName( _result ), countEscaped(),
                        countSacrificed() );
    }

    void AsymmetricHorrorGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        addMoneyResult( outListValue, static_cast<int64>( countEscaped() ) * _escapeReward );
    }

    bool AsymmetricHorrorGameModeComponent::readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const
    {
        outFocusObject                          = GameObjectHandle{};
        const AsymmetricHorrorSurvivor* pPlayer = _match.findSurvivor( AsymmetricHorrorGameModeComponentInternal::kPlayer );
        outFocusPosition                        = pPlayer != nullptr ? pPlayer->_position : float3{};
        if ( const GameObjectManager* const pManager = getObjectManager() )
            outFocusPosition = pManager->getWorldOrigin().toLocal( toAbsolute( outFocusPosition ) );
        return isStarted();
    }

    int32 AsymmetricHorrorGameModeComponent::countRemainingGenerators() const
    {
        if ( _pCatalog == nullptr || _match.areGatesPowered() )
            return 0;
        return _pCatalog->getRules()._generatorsRequired - _match.getCompletedGeneratorCount();
    }

    int32 AsymmetricHorrorGameModeComponent::countOpenGates() const
    {
        int32 count = 0;
        for ( int32 gate = 0; gate < _match.getGateCount(); ++gate )
        {
            count += _match.findGate( gate )->_progress.isCompleted() ? 1 : 0;
        }
        return count;
    }

    int32 AsymmetricHorrorGameModeComponent::countState( SurvivorState state ) const
    {
        int32 count = 0;
        for ( int32 survivor = 0; survivor < _match.getSurvivorCount(); ++survivor )
        {
            count += _match.findSurvivor( survivor )->_state == state ? 1 : 0;
        }
        return count;
    }

    float32 AsymmetricHorrorGameModeComponent::getPlayerX() const
    {
        const AsymmetricHorrorSurvivor* pPlayer = _match.findSurvivor( AsymmetricHorrorGameModeComponentInternal::kPlayer );
        return pPlayer != nullptr ? pPlayer->_position._x : 0.0f;
    }

    int32 AsymmetricHorrorGameModeComponent::getPlayerActivity() const
    {
        const AsymmetricHorrorSurvivor* pPlayer = _match.findSurvivor( AsymmetricHorrorGameModeComponentInternal::kPlayer );
        return pPlayer != nullptr ? static_cast<int32>( pPlayer->_activity ) : 0;
    }

    void AsymmetricHorrorGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 디렉터는 세울 오브젝트가 없다 — 모습은 클라이언트 모듈(`AsymmetricHorrorAvatarComponent`)이 읽어 그린다.
        (void)manager;
        (void)bRespawnViews;
    }

    void AsymmetricHorrorGameModeComponent::serializeGameState( StateIO& io )
    {
        using Internal = AsymmetricHorrorGameModeComponentInternal;
        io.header( Internal::kStateFormat );
        if ( io.isOk() == false )
            return;
        // 판은 같은 카탈로그 · 설정으로 `initialize` 한 뒤에 읽는다(키트 계약). 무대와 생존자는 바이트가 정한다.
        AsymmetricHorrorMatch restoredMatch;
        if ( io.isReading() )
        {
            if ( _pCatalog == nullptr || restoredMatch.initialize( makeSettings(), _pCatalog.get() ) == false )
            {
                SW_LOG_ERROR_ID( "AsymmetricHorror.StateNeedsCatalog", "Asymmetric horror game mode: state read before the rules catalog is bound - refused" );
                io.fail();
                return;
            }
        }
        Countdown killerRelease = _killerRelease;
        io.value( io.isWriting() ? _match : restoredMatch ); // 사람 비트 · 결과는 상태 칸이다(`GameModeComponent::serializeState`)
        io.value( killerRelease );
        io.check( io.isWriting() || restoredMatch.getSurvivorCount() == static_cast<int32>( _listCommand.size() ) );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _match         = std::move( restoredMatch );
        _killerRelease = killerRelease;
    }
} // namespace sw
