#include "pch.h"

#include "GameFramework/Kits/Rpg/WitcherRpg/WitcherCombat.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Combat/ElementChart.h"
#include "GameFramework/Data/StatBlock.h"
#include "GameFramework/Kits/Rpg/WitcherRpg/WitcherCatalog.h"
#include "GameFramework/Progression/SkillTree.h"

namespace sw
{
    const utf8* toString( WitcherCombatResult result )
    {
        switch ( result )
        {
            case WitcherCombatResult::Ok:
                return "Ok";
            case WitcherCombatResult::UnknownSign:
                return "UnknownSign";
            case WitcherCombatResult::NotEnoughStamina:
                return "NotEnoughStamina";
            case WitcherCombatResult::AlternateLocked:
                return "AlternateLocked";
        }
        return "Unknown";
    }

    WitcherCombat::WitcherCombat()
        : _stamina{}
        , _random{}
        , _pCatalog{ nullptr }
        , _adrenaline{ 0.0f }
    {
    }

    void WitcherCombat::initialize( const WitcherCatalog* pCatalog, uint32 seed )
    {
        _pCatalog = pCatalog;
        const WitcherCombatSettings  fallback{};
        const WitcherCombatSettings& combat = pCatalog != nullptr ? pCatalog->getCombat() : fallback;
        ResourceGaugeSettings        stamina;
        stamina._max        = combat._stamina;
        stamina._regenRate  = combat._staminaRegen;
        stamina._regenDelay = combat._staminaDelay;
        _stamina.initialize( stamina );
        _random.setSeed( seed );
        _adrenaline = 0.0f;
    }

    WitcherCombatResult WitcherCombat::performAction( WitcherAction action )
    {
        const float32 cost = getActionCost( action );
        if ( cost <= 0.0f )
            return WitcherCombatResult::Ok;
        return _stamina.trySpend( cost ) ? WitcherCombatResult::Ok : WitcherCombatResult::NotEnoughStamina;
    }

    WitcherSignCast WitcherCombat::castSign( const hashed_string& signId, bool bAlternate, const SkillTreeState* pSkill, const StatBlock& stats,
                                             const ElementChart* pChart )
    {
        WitcherSignCast cast;
        cast._signId                = signId;
        cast._bAlternate            = bAlternate ? SW_TRUE : SW_FALSE;
        const WitcherSignDef* pSign = _pCatalog != nullptr ? _pCatalog->findSign( signId ) : nullptr;
        if ( pSign == nullptr )
        {
            cast._result = WitcherCombatResult::UnknownSign;
            return cast;
        }
        if ( bAlternate && ( pSign->_altSkill.empty() || pSkill == nullptr || pSkill->getRank( pSign->_altSkill ) <= 0 ) )
        {
            cast._result = WitcherCombatResult::AlternateLocked;
            return cast;
        }
        if ( _stamina.trySpend( bAlternate ? pSign->_altCost : pSign->_cost ) == false )
        {
            cast._result = WitcherCombatResult::NotEnoughStamina;
            return cast;
        }
        const float32 intensity = stats.getValue( _pCatalog->getCombat()._intensityStat, 0.0f );
        cast._element           = pSign->_element;
        cast._power             = pSign->_basePower * MathUtil::max( 0.0f, 1.0f + intensity / 100.0f ) * ( bAlternate ? pSign->_altPowerScale : 1.0f );
        if ( pChart != nullptr && pSign->_element.empty() == false )
            cast._status = pChart->rollStatus( pSign->_element, _random );
        return cast;
    }

    void WitcherCombat::registerHitLanded()
    {
        if ( _pCatalog == nullptr )
            return;
        const WitcherCombatSettings& combat = _pCatalog->getCombat();
        _adrenaline                         = MathUtil::min( combat._adrenalineMax, _adrenaline + combat._adrenalinePerHit );
    }

    void WitcherCombat::registerHitTaken()
    {
        if ( _pCatalog == nullptr )
            return;
        _adrenaline = MathUtil::max( 0.0f, _adrenaline - _pCatalog->getCombat()._adrenalineLossOnHit );
    }

    int32 WitcherCombat::consumeAdrenalinePoints()
    {
        const int32 points = getAdrenalinePoints();
        _adrenaline        = MathUtil::max( 0.0f, _adrenaline - static_cast<float32>( points ) );
        return points;
    }

    float32 WitcherCombat::computeDamageScale() const
    {
        const float32 bonus = _pCatalog != nullptr ? _pCatalog->getCombat()._adrenalineDamageBonus : 0.0f;
        return 1.0f + static_cast<float32>( getAdrenalinePoints() ) * bonus;
    }

    int32 WitcherCombat::getAdrenalinePoints() const
    {
        // 0.1 씩 열 번 더한 값이 0.9999… 가 되어 포인트를 놓치지 않게 아주 조금 올려 자른다.
        return static_cast<int32>( MathUtil::floor( _adrenaline + 1.0e-4f ) );
    }

    float32 WitcherCombat::getActionCost( WitcherAction action ) const
    {
        if ( _pCatalog == nullptr )
            return 0.0f;
        const WitcherCombatSettings& combat = _pCatalog->getCombat();
        switch ( action )
        {
            case WitcherAction::FastAttack:
                return combat._fastCost;
            case WitcherAction::StrongAttack:
                return combat._strongCost;
            case WitcherAction::Dodge:
                return combat._dodgeCost;
            case WitcherAction::Roll:
                return combat._rollCost;
        }
        return 0.0f;
    }
} // namespace sw
