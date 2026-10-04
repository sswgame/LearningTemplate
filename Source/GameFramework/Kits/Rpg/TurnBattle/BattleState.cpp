#include "pch.h"

#include "GameFramework/Kits/Rpg/TurnBattle/BattleState.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include "GameFramework/Data/GameSettings.h"
#include "GameFramework/Framework/GameService.h"
#include "GameFramework/Framework/GameSound.h"

namespace sw
{
    SW_LOG_CALLER( "Battle" );

    BattleState::BattleState()
        : _player{}
        , _foe{}
        , _phaseTimer{ 0.0f }
        , _statusText{}
        , _phase{ BattlePhase::Inactive }
        , _bPlayerWon{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void BattleState::startWildEncounter( const utf8* pSpeciesId )
    {
        const SpeciesCatalog* pCatalog = game::getService<SpeciesCatalog>();
        const PartyMember     lead     = pCatalog != nullptr ? pCatalog->makeStarter() : PartyMember{};
        startWithPartyLead( lead, pSpeciesId );
    }

    void BattleState::startWithPartyLead( const PartyMember& playerLead, const utf8* pFoeSpeciesId )
    {
        _player                        = playerLead;
        const SpeciesCatalog* pCatalog = game::getService<SpeciesCatalog>();
        _foe                           = pCatalog != nullptr ? pCatalog->makeWild( pFoeSpeciesId, playerLead._level ) : PartyMember{};
        _phase                         = BattlePhase::Intro;
        _phaseTimer                    = 0.55f;
        _bPlayerWon                    = SW_FALSE;
        setStatusText( SW_LOCFORMAT( "battle", "wild_appeared", "A wild {name} appeared!", TextArgumentList().addText( "name", _foe._nickname ) ) );
        SW_LOG_TRACE( "%#", _statusText.c_str() );
        const GameSettings* pGameSettings = game::getService<GameSettings>();
        if ( pGameSettings != nullptr )
        {
            const string_view bgm = pGameSettings->getCustomProperty( "battleBgm" );
            if ( bgm.empty() == false )
                (void)GameSound::playMusic( bgm );
        }
    }

    void BattleState::update( float32 deltaTime )
    {
        // **`Ended` 도 갱신한다.** 아래 `Ended` 분기가 `Inactive` 로 돌리는 유일한 자리다 —
        // 여기서 같이 걸러 내면 `Ended` 에 들어가며 건 0.4 초 타이머가 영영 안 끝나, 전투가 스스로
        // 끝나기를 기다리는 쪽은 `endBattle()` 을 따로 부르지 않는 한 영원히 기다린다.
        if ( _phase == BattlePhase::Inactive )
            return;

        _phaseTimer -= deltaTime;
        if ( _phaseTimer > 0.0f )
            return;

        if ( _phase == BattlePhase::Intro )
        {
            _phase = BattlePhase::PlayerChoice;
            setStatusText( SW_LOCTEXT( "battle", "prompt_fight", "Fight: 1/2 moves, Enter=Move0, Esc=Run" ) );
            SW_LOG_TRACE( "%#", _statusText.c_str() );
        }
        else if ( _phase == BattlePhase::ResolvePlayer )
        {
            if ( _foe._hp <= 0 )
            {
                _bPlayerWon = SW_TRUE;
                _player._exp += 10;
                setStatusText( SW_LOCFORMAT( "battle", "foe_fainted", "{name} fainted! You won!", TextArgumentList().addText( "name", _foe._nickname ) ) );
                _phase      = BattlePhase::Ended;
                _phaseTimer = 0.4f;
                SW_LOG_TRACE( "%#", _statusText.c_str() );
                return;
            }
            applyMove( _foe, _player, pickFoeMoveSlot(), false );
            _phase      = BattlePhase::ResolveFoe;
            _phaseTimer = 0.45f;
        }
        else if ( _phase == BattlePhase::ResolveFoe )
        {
            if ( _player._hp <= 0 )
            {
                _bPlayerWon = SW_FALSE;
                setStatusText( SW_LOCFORMAT( "battle", "player_fainted", "{name} fainted...", TextArgumentList().addText( "name", _player._nickname ) ) );
                _phase      = BattlePhase::Ended;
                _phaseTimer = 0.4f;
                SW_LOG_TRACE( "%#", _statusText.c_str() );
                return;
            }
            _phase = BattlePhase::PlayerChoice;
            setStatusText( SW_LOCFORMAT( "battle", "what_will", "What will {name} do?", TextArgumentList().addText( "name", _player._nickname ) ) );
        }
        else if ( _phase == BattlePhase::Ended )
        {
            _phase = BattlePhase::Inactive;
            SW_LOG_TRACE( "Encounter ended." );
        }
    }

    void BattleState::selectFight( int32 moveSlot )
    {
        if ( _phase != BattlePhase::PlayerChoice )
            return;
        applyMove( _player, _foe, moveSlot, true );
        _phase      = BattlePhase::ResolvePlayer;
        _phaseTimer = 0.45f;
    }

    void BattleState::selectRun()
    {
        if ( _phase != BattlePhase::PlayerChoice )
            return;
        setStatusText( SW_LOCTEXT( "battle", "got_away", "Got away safely!" ) );
        SW_LOG_TRACE( "%#", _statusText.c_str() );
        _bPlayerWon = SW_FALSE;
        _phase      = BattlePhase::Ended;
        _phaseTimer = 0.3f;
    }

    void BattleState::setStatusText( string_view text )
    {
        formatstring( _statusText.data(), _statusText.capacity(), "%#", text );
    }

    void BattleState::endBattle()
    {
        _phase = BattlePhase::Inactive;
        _statusText.clear();
    }

    void BattleState::applyMove( PartyMember& attacker, PartyMember& defender, int32 moveSlot, bool playerSide )
    {
        const SpeciesCatalog* pCatalog = game::getService<SpeciesCatalog>();
        const SpeciesDef*     pSpecies = pCatalog != nullptr ? pCatalog->findSpecies( attacker._speciesId.c_str() ) : nullptr;
        const size_t          slot     = static_cast<size_t>( MathUtil::max( moveSlot, 0 ) );
        const MoveDef*        pMove    = ( pCatalog != nullptr && pSpecies != nullptr ) ? pCatalog->findMoveAtSlot( *pSpecies, slot ) : nullptr;

        // 슬롯 수는 데이터가 정하므로 없는 슬롯을 고를 수 있다. PP 가 없는 것과 같이 다룬다.
        if ( slot >= attacker._listPp.size() )
        {
            setStatusText( SW_LOCFORMAT( "battle", "no_pp", "{name} has no PP!", TextArgumentList().addText( "name", attacker._nickname ) ) );
            return;
        }

        // 기술 정의가 없을 수 있다. 종족이나 카탈로그가 빠지면 위에서 pMove 가 nullptr 이 된다.
        // 아래 dmg 계산은 `pMove != nullptr` 을 확인하지만 dmg == 0 분기는 pMove->_name 을 읽는다 —
        // 정상 데이터로는 걸리지 않는 널 역참조다. 쓸 기술이 없으면 PP 를 쓰기 전에 끝낸다. 슬롯이
        // 없을 때와 같은 취급이다.
        if ( pMove == nullptr )
        {
            setStatusText( SW_LOCFORMAT( "battle", "no_move", "{name} has no usable move!", TextArgumentList().addText( "name", attacker._nickname ) ) );
            return;
        }

        int32& pp = attacker._listPp[slot];
        if ( pp <= 0 )
        {
            setStatusText( SW_LOCFORMAT( "battle", "no_pp", "{name} has no PP!", TextArgumentList().addText( "name", attacker._nickname ) ) );
            return;
        }
        --pp;

        const int32 dmg = ( pMove->_power > 0 ) ? ( pMove->_power / 4 + attacker._level / 2 ) : 0;
        if ( dmg > 0 )
        {
            defender._hp = MathUtil::max( defender._hp - dmg, 0 );
            setStatusText( SW_LOCFORMAT( "battle", "used_move_dmg", "{name} used {move}! ({damage} dmg)",
                                         TextArgumentList().addText( "name", attacker._nickname ).addText( "move", pMove->_name ).addInteger( "damage", dmg ) ) );
        }
        else
        {
            setStatusText( SW_LOCFORMAT( "battle", "used_move", "{name} used {move}!", TextArgumentList().addText( "name", attacker._nickname ).addText( "move", pMove->_name ) ) );
        }
        (void)playerSide;
        SW_LOG_TRACE( "%#", _statusText.c_str() );
    }

    int32 pickFoeMoveSlot( int32 hp, int32 hpMax, size_t moveSlotCount )
    {
        if ( moveSlotCount == 0 )
            return 0;

        // **가진 슬롯 안에서 고른다.** 슬롯 수는 데이터가 정하므로 기술이 하나뿐인 종족이 있을 수 있다.
        // 없는 슬롯을 고르면 `applyMove` 가 "no PP" 만 찍고 돌아가서, **적은 절반 이하로
        // 떨어지는 순간부터 한 대도 못 때린다.**
        const int32 lastSlot = static_cast<int32>( moveSlotCount - 1 );
        if ( hp * 2 < hpMax )
            return MathUtil::min( 1, lastSlot );
        return 0;
    }

    int32 BattleState::pickFoeMoveSlot() const
    {
        return sw::pickFoeMoveSlot( _foe._hp, _foe._hpMax, _foe._listPp.size() );
    }
} // namespace sw
