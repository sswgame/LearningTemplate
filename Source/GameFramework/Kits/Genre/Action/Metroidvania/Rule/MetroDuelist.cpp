#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Rule/MetroDuelist.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Input/TimingJudge.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/Catalog/MetroidvaniaCatalog.h"

namespace sw
{
    namespace
    {
        struct MetroDuelistInternal
        {
            static constexpr float32 kNeverPressed    = -1.0e9f; ///< 패리를 누른 적 없음(어느 창에도 닿지 않는 과거)
            static constexpr float32 kGuardPoiseRatio = 0.5f;    ///< 막으면 강인도 피해는 반
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

    void MetroDuelist::initialize( const MetroidvaniaCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        if ( pCatalog != nullptr )
        {
            _vitality.initialize( pCatalog->getRules()._health );
            _stamina.initialize( pCatalog->getRules()._stamina );
        }
        _time           = 0.0f;
        _parryPressTime = MetroDuelistInternal::kNeverPressed;
        _riposteWindow.clear();
        _damageTakenScale = 1.0f;
        _bGuarding        = SW_FALSE;
    }

    void MetroDuelist::update( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        _time += deltaTime;
        _riposteWindow.tick( deltaTime );
        _vitality.update( deltaTime );
        _stamina.update( deltaTime );
    }

    bool MetroDuelist::tryAttack() { return _pCatalog != nullptr && _vitality.isAlive() && _stamina.trySpend( _pCatalog->getRules()._attackStaminaCost ); }

    bool MetroDuelist::tryDodge() { return _pCatalog != nullptr && _vitality.isAlive() && _stamina.trySpend( _pCatalog->getRules()._dodgeStaminaCost ); }

    void MetroDuelist::pressParry() { _parryPressTime = _time; }

    MetroDefenseOutcome MetroDuelist::receiveAttack( float32 damage, float32 poiseDamage, int32 attackerId )
    {
        MetroDefenseOutcome outcome;
        if ( _pCatalog == nullptr || _vitality.isAlive() == false )
        {
            outcome._result = MetroDefenseResult::Ignored;
            return outcome;
        }
        const MetroRules& rules = _pCatalog->getRules();

        // 패리 — 누른 시각이 공격이 닿는 순간의 창 안이면 피해 없이 반격 창을 연다. 누름은 한 번만 쓴다.
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
                // 가드 붕괴 — 남은 스태미나를 모두 잃고 그대로 맞는다.
                outcome._result = MetroDefenseResult::GuardBroken;
                (void)_stamina.reduce( _stamina.getValue() );
            }
        }
        const VitalityDamageResult damageResult = _vitality.applyDamage( healthDamage, poise, attackerId );
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
        // 사본에 읽고 끝까지 맞으면 바꾼다 — 카탈로그 · 체력 · 지구력 설정은 사본이 그대로 든다.
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
