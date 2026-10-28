#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Rule/MetroDuelist.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Character/Input/TimingJudge.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/Catalog/MetroidvaniaCatalog.h"

namespace sw
{
    namespace
    {
        struct MetroDuelistInternal
        {
            static constexpr float32 kNeverPressed    = -1.0e9f; ///< 패리를 누른 적이 없을 때의 값(어느 창에도 닿지 않는 먼 과거)
            static constexpr float32 kGuardPoiseRatio = 0.5f;    ///< 막으면 강인도 피해가 절반이 된다
        };
    } // namespace
} // namespace sw

namespace sw
{
    MetroDuelist::MetroDuelist()
        : _pCatalog{ nullptr }
        , _vitality{}
        , _stamina{}
        , _time{ 0.0f }
        , _parryPressTime{ MetroDuelistInternal::kNeverPressed }
        , _riposteWindow{}
        , _damageTakenScale{ 1.0f }
        , _bGuarding{ SW_FALSE }
    {
    }

    bool MetroDuelist::initialize( const MetroidvaniaCatalog* pCatalog )
    {
        if ( pCatalog == nullptr )
        {
            SW_LOG_ERROR( "MetroDuelist: refused - no catalog" );
            return false;
        }
        // 따로 만들어 둘 다 받아들여질 때만 바꾼다 — 하나라도 거절되면 상태는 그대로다.
        Vitality      vitality;
        ResourceGauge stamina;
        if ( vitality.initialize( pCatalog->getRules()._health ) == false || stamina.initialize( pCatalog->getRules()._stamina ) == false )
            return false;
        _pCatalog         = pCatalog;
        _vitality         = vitality;
        _stamina          = stamina;
        _time             = 0.0f;
        _parryPressTime   = MetroDuelistInternal::kNeverPressed;
        _damageTakenScale = 1.0f;
        _bGuarding        = SW_FALSE;
        _riposteWindow.clear();
        return true;
    }

    void MetroDuelist::update( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        _time += deltaTime;
        _riposteWindow.update( deltaTime );
        _vitality.update( deltaTime );
        _stamina.update( deltaTime );
    }

    bool MetroDuelist::tryAttack()
    {
        return _pCatalog != nullptr && _vitality.isAlive() && _stamina.trySpend( _pCatalog->getRules()._attackStaminaCost );
    }

    bool MetroDuelist::tryDodge()
    {
        return _pCatalog != nullptr && _vitality.isAlive() && _stamina.trySpend( _pCatalog->getRules()._dodgeStaminaCost );
    }

    void MetroDuelist::pressParry()
    {
        _parryPressTime = _time;
    }

    MetroDefenseOutcome MetroDuelist::receiveAttack( float32 damage, float32 poiseDamage, int32 attackerID )
    {
        MetroDefenseOutcome outcome;
        // 죽었거나 무적(시간 · 면역)이면 맞은 것이 아니다 — 가드 스태미나를 쓰거나 패리 창을 열거나 맞음 연출을 낼 결과를 주지 않는다.
        const bool bNothingHappens = _pCatalog == nullptr || _vitality.isAlive() == false || _vitality.ignoresDamage();
        if ( bNothingHappens )
        {
            outcome._result = MetroDefenseResult::Ignored;
            return outcome;
        }
        const MetroRules& rules = _pCatalog->getRules();

        // 패리다. 누른 시각이 공격이 닿는 순간의 창 안이면 피해 없이 반격 창을 연다. 누름은 한 번만 쓴다.
        const TimingResult parry = rules._parryJudge.judge( _time, _parryPressTime );
        if ( parry.isHit() )
        {
            _parryPressTime = MetroDuelistInternal::kNeverPressed;
            _riposteWindow.start( rules._riposteTime );
            outcome._result     = MetroDefenseResult::Parried;
            outcome._parryGrade = parry._pWindow->_grade;
            return outcome;
        }

        float32 healthDamage = damage * _damageTakenScale;
        float32 poise        = poiseDamage;
        outcome._result      = MetroDefenseResult::Hit;
        if ( _bGuarding == SW_TRUE )
        {
            if ( _stamina.trySpend( damage * rules._guardStaminaPerDamage ) )
            {
                outcome._result = MetroDefenseResult::Blocked;
                healthDamage    = healthDamage * rules._guardChipRatio;
                poise           = poise * MetroDuelistInternal::kGuardPoiseRatio;
            }
            else
            {
                // 가드 붕괴다. 남은 스태미나를 모두 잃고 그대로 맞는다.
                outcome._result = MetroDefenseResult::GuardBroken;
                (void)_stamina.reduce( _stamina.getValue() );
            }
        }
        const VitalityDamageResult damageResult = _vitality.applyDamage( healthDamage, poise, attackerID );
        outcome._healthDamage                   = damageResult._healthDamage;
        outcome._bStaggered                     = damageResult._bPoiseBroken;
        return outcome;
    }

    float32 MetroDuelist::computeAttackDamage( float32 baseDamage, const MetroDuelist& target )
    {
        if ( _pCatalog == nullptr )
            return baseDamage;
        const float32 multiplier = _pCatalog->getRules()._riposteMultiplier;
        if ( _riposteWindow.isActive() )
        {
            _riposteWindow.clear();
            return baseDamage * multiplier;
        }
        if ( target.getVitality().isPoiseBroken() )
            return baseDamage * multiplier;
        return baseDamage;
    }

    void MetroDuelist::writeState( Archive& outArchive ) const
    {
        _vitality.writeState( outArchive );
        _stamina.writeState( outArchive );
        outArchive << _time;
        outArchive << _parryPressTime;
        StateArchiveUtil::writeCountdown( outArchive, _riposteWindow );
        outArchive << _damageTakenScale;
        outArchive << _bGuarding;
    }

    bool MetroDuelist::readState( Archive& archive )
    {
        // 사본에 읽고 끝까지 맞으면 바꾼다. 카탈로그, 체력, 지구력 설정은 사본이 그대로 가지고 있다.
        MetroDuelist restored  = *this;
        const bool   bBodyRead = restored._vitality.readState( archive ) && restored._stamina.readState( archive );
        archive >> restored._time;
        archive >> restored._parryPressTime;
        const bool bTimerRead = StateArchiveUtil::readCountdown( archive, restored._riposteWindow );
        archive >> restored._damageTakenScale;
        archive >> restored._bGuarding;
        const bool bValid =
            bBodyRead && bTimerRead && archive.isOk() && 0.0f <= restored._time && 0.0f < restored._damageTakenScale && restored._bGuarding <= SW_TRUE;
        if ( bValid == false )
            return false;
        *this = std::move( restored );
        return true;
    }
} // namespace sw
