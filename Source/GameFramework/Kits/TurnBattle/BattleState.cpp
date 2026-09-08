#include "pch.h"

#include "GameFramework/Kits/TurnBattle/BattleState.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Audio/IAudioSystem.h"

#include "GameFramework/Base/GameService.h"
#include "GameFramework/Data/GameData.h"
#include "GameFramework/Data/GameStrings.h"

namespace sw
{
    SW_LOG_CALLER( "Battle" );

    BattleState::BattleState()
        : _player{}
        , _foe{}
        , _phaseTimer{ 0.0f }
        , _statusText{}
        , _phase{ BattlePhase::Inactive }
        , _pendingCmd{ BattleCommand::None }
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
        _pendingCmd                    = BattleCommand::None;
        _bPlayerWon                    = SW_FALSE;
        formatstring( _statusText.data(), _statusText.capacity(), GameStrings::get( "battle.wild_appeared", "A wild %# appeared!" ),
                      _foe._nickname.c_str() );
        SW_LOG_TRACE( "%#", _statusText.c_str() );
        const GameData* pGameData = game::getService<GameData>();
        if ( pGameData != nullptr )
        {
            const string_view bgm = pGameData->getCustomProperty( "dungeonBgm" );
            if ( bgm.empty() == false )
            {
                IAudioSystem* pAudio = game::getService<IAudioSystem>();
                if ( pAudio != nullptr )
                    pAudio->playMusic( bgm );
            }
        }
    }

    void BattleState::update( float32 deltaTime )
    {
        if ( _phase == BattlePhase::Inactive || _phase == BattlePhase::Ended )
            return;

        _phaseTimer -= deltaTime;
        if ( _phaseTimer > 0.0f )
            return;

        if ( _phase == BattlePhase::Intro )
        {
            _phase = BattlePhase::PlayerChoice;
            formatstring( _statusText.data(), _statusText.capacity(), "%#", GameStrings::get( "battle.prompt_fight", "Fight: 1/2 moves, Enter=Move0, Esc=Run" ) );
            SW_LOG_TRACE( "%#", _statusText.c_str() );
        }
        else if ( _phase == BattlePhase::ResolvePlayer )
        {
            if ( _foe._hp <= 0 )
            {
                _bPlayerWon = SW_TRUE;
                _player._exp += 10;
                formatstring( _statusText.data(), _statusText.capacity(), GameStrings::get( "battle.foe_fainted", "%# fainted! You won!" ),
                              _foe._nickname.c_str() );
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
                formatstring( _statusText.data(), _statusText.capacity(), GameStrings::get( "battle.player_fainted", "%# fainted..." ),
                              _player._nickname.c_str() );
                _phase      = BattlePhase::Ended;
                _phaseTimer = 0.4f;
                SW_LOG_TRACE( "%#", _statusText.c_str() );
                return;
            }
            _phase = BattlePhase::PlayerChoice;
            formatstring( _statusText.data(), _statusText.capacity(), GameStrings::get( "battle.what_will", "What will %# do?" ),
                          _player._nickname.c_str() );
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
        formatstring( _statusText.data(), _statusText.capacity(), "%#", GameStrings::get( "battle.got_away", "Got away safely!" ) );
        SW_LOG_TRACE( "%#", _statusText.c_str() );
        _bPlayerWon = SW_FALSE;
        _phase      = BattlePhase::Ended;
        _phaseTimer = 0.3f;
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

        // 슬롯 수는 데이터가 정하므로 없는 슬롯을 고를 수 있다 — PP 가 없는 것과 같이 다룬다.
        if ( slot >= attacker._listPp.size() )
        {
            formatstring( _statusText.data(), _statusText.capacity(), GameStrings::get( "battle.no_pp", "%# has no PP!" ),
                          attacker._nickname.c_str() );
            return;
        }

        int32& pp = attacker._listPp[slot];
        if ( pp <= 0 )
        {
            formatstring( _statusText.data(), _statusText.capacity(), GameStrings::get( "battle.no_pp", "%# has no PP!" ),
                          attacker._nickname.c_str() );
            return;
        }
        --pp;

        const int32 dmg = ( pMove != nullptr && pMove->_power > 0 ) ? ( pMove->_power / 4 + attacker._level / 2 ) : 0;
        if ( dmg > 0 )
        {
            defender._hp = MathUtil::max( defender._hp - dmg, 0 );
            formatstring( _statusText.data(), _statusText.capacity(), GameStrings::get( "battle.used_move_dmg", "%# used %#! (%# dmg)" ),
                          attacker._nickname.c_str(), pMove->_name.c_str(), dmg );
        }
        else
        {
            formatstring( _statusText.data(), _statusText.capacity(), GameStrings::get( "battle.used_move", "%# used %#!" ),
                          attacker._nickname.c_str(), pMove->_name.c_str() );
        }
        (void)playerSide;
        SW_LOG_TRACE( "%#", _statusText.c_str() );
    }

    int32 BattleState::pickFoeMoveSlot() const
    {
        if ( _foe._hp * 2 < _foe._hpMax )
            return 1;
        return 0;
    }
} // namespace sw
