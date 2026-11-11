#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Rule/MetroidvaniaDuelist.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Catalog/MetroidvaniaCatalog.h"
#include "GameFramework/Runtime/Character/Input/TimingJudge.h"

namespace sw
{
    namespace
    {
        struct MetroidvaniaDuelistInternal
        {
            static constexpr float32 kNeverPressed    = -1.0e9f; ///< 패리를 누른 적이 없을 때의 값(어느 창에도 닿지 않는 먼 과거)
            static constexpr float32 kGuardPoiseRatio = 0.5f;    ///< 막으면 강인도 피해가 절반이 된다
        };
    } // namespace
} // namespace sw

namespace sw
{
    MetroidvaniaDuelist::MetroidvaniaDuelist()
        : _pCatalog{ nullptr }
        , _healthMachine{}
        , _stamina{}
        , _time{ 0.0f }
        , _parryPressTime{ MetroidvaniaDuelistInternal::kNeverPressed }
        , _riposteWindow{}
        , _damageTakenScale{ 1.0f }
        , _bGuarding{ SW_FALSE }
    {
    }

    bool MetroidvaniaDuelist::initialize( const MetroidvaniaCatalog* pCatalog )
    {
        if ( pCatalog == nullptr )
        {
            SW_LOG_ERROR_ID( "MetroidvaniaDuelist.MetroidvaniaDuelistRefusedNoCatalog", "MetroidvaniaDuelist: refused - no catalog" );
            return false;
        }
        // 따로 만들어 둘 다 받아들여질 때만 바꾼다 — 하나라도 거절되면 상태는 그대로다.
        HealthStateMachine healthMachine;
        ResourceGauge      stamina;
        if ( healthMachine.initialize( pCatalog->getRules()._health ) == false || stamina.initialize( pCatalog->getRules()._stamina ) == false )
            return false;
        _pCatalog         = pCatalog;
        _healthMachine    = healthMachine;
        _stamina          = stamina;
        _time             = 0.0f;
        _parryPressTime   = MetroidvaniaDuelistInternal::kNeverPressed;
        _damageTakenScale = 1.0f;
        _bGuarding        = SW_FALSE;
        _riposteWindow.clear();
        return true;
    }

    void MetroidvaniaDuelist::update( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        _time += deltaTime;
        _riposteWindow.update( deltaTime );
        _healthMachine.update( deltaTime );
        _stamina.update( deltaTime );
    }

    bool MetroidvaniaDuelist::tryAttack()
    {
        return _pCatalog != nullptr && _healthMachine.isAlive() && _stamina.trySpend( _pCatalog->getRules()._attackStaminaCost );
    }

    bool MetroidvaniaDuelist::tryDodge()
    {
        return _pCatalog != nullptr && _healthMachine.isAlive() && _stamina.trySpend( _pCatalog->getRules()._dodgeStaminaCost );
    }

    void MetroidvaniaDuelist::pressParry()
    {
        _parryPressTime = _time;
    }

    MetroidvaniaDefenseOutcome MetroidvaniaDuelist::receiveAttack( float32 damage, float32 poiseDamage, int32 attackerID )
    {
        MetroidvaniaDefenseOutcome result;
        // 죽었거나 무적(시간 · 면역)이면 맞은 것이 아니다 — 가드 스태미나를 쓰거나 패리 창을 열거나 맞음 연출을 낼 결과를 주지 않는다.
        const bool bNothingHappens = _pCatalog == nullptr || _healthMachine.isAlive() == false || _healthMachine.shouldIgnoreDamage();
        if ( bNothingHappens )
        {
            result._result = MetroidvaniaDefenseResult::Ignored;
            return result;
        }
        const MetroidvaniaRules& rules = _pCatalog->getRules();
        // 음수 피해는 막기 비용이 음수라 가드가 깨지고, NaN 피해는 막기 비용으로 스태미나를 NaN 으로 만든다. 없는 피해로 친다.
        damage      = damage > 0.0f ? damage : 0.0f;
        poiseDamage = poiseDamage > 0.0f ? poiseDamage : 0.0f;

        // 패리다. 누른 시각이 공격이 닿는 순간의 창 안이면 피해 없이 반격 창을 연다. 누름은 한 번만 쓴다.
        const TimingResult parry = rules._parryJudge.judge( _time, _parryPressTime );
        if ( parry.isHit() )
        {
            _parryPressTime = MetroidvaniaDuelistInternal::kNeverPressed;
            _riposteWindow.start( rules._riposteTime );
            result._result     = MetroidvaniaDefenseResult::Parried;
            result._parryGrade = parry._pWindow->_grade;
            return result;
        }

        float32 healthDamage = damage * _damageTakenScale;
        float32 poise        = poiseDamage;
        result._result       = MetroidvaniaDefenseResult::Hit;
        if ( _bGuarding == SW_TRUE )
        {
            if ( _stamina.trySpend( damage * rules._guardStaminaPerDamage ) )
            {
                result._result = MetroidvaniaDefenseResult::Blocked;
                healthDamage   = healthDamage * rules._guardChipRatio;
                poise          = poise * MetroidvaniaDuelistInternal::kGuardPoiseRatio;
            }
            else
            {
                // 가드 붕괴다. 남은 스태미나를 모두 잃고 그대로 맞는다.
                result._result = MetroidvaniaDefenseResult::GuardBroken;
                (void)_stamina.reduce( _stamina.getValue() );
            }
        }
        const HealthDamageResult damageResult = _healthMachine.applyDamage( healthDamage, poise, attackerID );
        result._healthDamage                  = damageResult._healthDamage;
        result._bStaggered                    = damageResult._bPoiseBroken;
        return result;
    }

    float32 MetroidvaniaDuelist::computeAttackDamage( float32 baseDamage, const MetroidvaniaDuelist& target )
    {
        if ( _pCatalog == nullptr )
            return baseDamage;
        const float32 multiplier = _pCatalog->getRules()._riposteMultiplier;
        if ( _riposteWindow.isActive() )
        {
            _riposteWindow.clear();
            return baseDamage * multiplier;
        }
        if ( target.getHealthMachine().isPoiseBroken() )
            return baseDamage * multiplier;
        return baseDamage;
    }

    void MetroidvaniaDuelist::serializeState( StateIO& io )
    {
        io.value( _healthMachine );
        io.value( _stamina );
        io.value( _time );
        io.value( _parryPressTime );
        io.value( _riposteWindow );
        io.value( _damageTakenScale );
        io.flag( _bGuarding );
        io.check( 0.0f <= _time && 0.0f < _damageTakenScale );
    }

} // namespace sw
