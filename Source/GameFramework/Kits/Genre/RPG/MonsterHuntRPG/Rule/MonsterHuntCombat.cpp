#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Rule/MonsterHuntCombat.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Catalog/MonsterHuntCatalog.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Rule/SkillTree.h"
#include "GameFramework/Runtime/Character/Combat/Damage/ElementChart.h"
#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"

namespace sw
{
    MonsterHuntCombat::MonsterHuntCombat()
        : _stamina{}
        , _random{}
        , _pCatalog{ nullptr }
        , _adrenaline{ 0.0f }
    {
    }

    bool MonsterHuntCombat::initialize( const MonsterHuntCatalog* pCatalog, uint32 seed )
    {
        if ( pCatalog == nullptr )
        {
            SW_LOG_ERROR_ID( "MonsterHuntCombat.MonsterHuntCombatRefusedNoCatalog", "MonsterHuntCombat: refused - no catalog (the combat numbers come from the catalog, there is no default set)" );
            return false;
        }
        const MonsterHuntCombatDef& combat = pCatalog->getCombat();
        ResourceGaugeSettings       staminaSettings;
        staminaSettings._max        = combat._stamina;
        staminaSettings._regenRate  = combat._staminaRegen;
        staminaSettings._regenDelay = combat._staminaDelay;
        ResourceGauge stamina;
        if ( stamina.initialize( staminaSettings ) == false )
            return false;
        _pCatalog = pCatalog;
        _stamina  = stamina;
        _random.setSeed( seed );
        _adrenaline = 0.0f;
        return true;
    }

    MonsterHuntCombatResult MonsterHuntCombat::performAction( MonsterHuntAction action )
    {
        const float32 cost = getActionCost( action );
        if ( cost <= 0.0f )
            return MonsterHuntCombatResult::Ok;
        return _stamina.trySpend( cost ) ? MonsterHuntCombatResult::Ok : MonsterHuntCombatResult::NotEnoughStamina;
    }

    MonsterHuntSignCast MonsterHuntCombat::castSign( const hashed_string& signID, bool bAlternate, const SkillTreeState* pSkill, const StatBlock& stats,
                                                     const ElementChart* pChart )
    {
        MonsterHuntSignCast cast;
        cast._signID                    = signID;
        cast._bAlternate                = bAlternate ? SW_TRUE : SW_FALSE;
        const MonsterHuntSignDef* pSign = _pCatalog != nullptr ? _pCatalog->findSign( signID ) : nullptr;
        if ( pSign == nullptr )
        {
            cast._result = MonsterHuntCombatResult::UnknownSign;
            return cast;
        }
        if ( bAlternate && ( pSign->_altSkill.empty() || pSkill == nullptr || pSkill->getRank( pSign->_altSkill ) <= 0 ) )
        {
            cast._result = MonsterHuntCombatResult::AlternateLocked;
            return cast;
        }
        if ( _stamina.trySpend( bAlternate ? pSign->_altCost : pSign->_cost ) == false )
        {
            cast._result = MonsterHuntCombatResult::NotEnoughStamina;
            return cast;
        }
        const float32 intensity = stats.getValue( _pCatalog->getCombat()._intensityStat );
        cast._element           = pSign->_element;
        cast._power             = pSign->_basePower * MathUtil::max( 0.0f, 1.0f + intensity / 100.0f ) * ( bAlternate ? pSign->_altPowerScale : 1.0f );
        if ( pChart != nullptr && pSign->_element.empty() == false )
            cast._status = pChart->rollStatus( pSign->_element, _random );
        return cast;
    }

    void MonsterHuntCombat::registerHitLanded()
    {
        if ( _pCatalog == nullptr )
            return;
        const MonsterHuntCombatDef& combat = _pCatalog->getCombat();
        _adrenaline                        = MathUtil::min( combat._adrenalineMax, _adrenaline + combat._adrenalinePerHit );
    }

    void MonsterHuntCombat::registerHitTaken()
    {
        if ( _pCatalog == nullptr )
            return;
        _adrenaline = MathUtil::max( 0.0f, _adrenaline - _pCatalog->getCombat()._adrenalineLossOnHit );
    }

    int32 MonsterHuntCombat::consumeAdrenalinePoints()
    {
        const int32 points = getAdrenalinePoints();
        _adrenaline        = MathUtil::max( 0.0f, _adrenaline - static_cast<float32>( points ) );
        return points;
    }

    float32 MonsterHuntCombat::computeDamageScale() const
    {
        const float32 bonus = _pCatalog != nullptr ? _pCatalog->getCombat()._adrenalineDamageBonus : 0.0f;
        return 1.0f + static_cast<float32>( getAdrenalinePoints() ) * bonus;
    }

    int32 MonsterHuntCombat::getAdrenalinePoints() const
    {
        // 0.1씩 열 번 더한 값이 0.9999…가 되어 포인트를 놓치지 않도록, 아주 조금 올려서 자른다.
        return static_cast<int32>( MathUtil::floor( _adrenaline + 1.0e-4f ) );
    }

    float32 MonsterHuntCombat::getActionCost( MonsterHuntAction action ) const
    {
        if ( _pCatalog == nullptr )
            return 0.0f;
        const MonsterHuntCombatDef& combat = _pCatalog->getCombat();
        switch ( action )
        {
            case MonsterHuntAction::FastAttack:
                return combat._fastCost;
            case MonsterHuntAction::StrongAttack:
                return combat._strongCost;
            case MonsterHuntAction::Dodge:
                return combat._dodgeCost;
            case MonsterHuntAction::Roll:
                return combat._rollCost;
        }
        return 0.0f;
    }

    void MonsterHuntCombat::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        io.value( _stamina ); // 게이지 설정은 `initialize` 에서 받은 것이므로 값만 읽는다
        io.value( _random );
        io.value( _adrenaline );
        // 상한 밖 · NaN 은 포인트 변환(`static_cast<int32>`)과 피해 배율을 깨뜨린다. NaN 은 비교가 거짓이라 `<` 로는 걸러지지 않는다.
        if ( io.isReading() )
        {
            io.check( 0.0f <= _adrenaline && _adrenaline <= _pCatalog->getCombat()._adrenalineMax );
        }
    }

} // namespace sw
