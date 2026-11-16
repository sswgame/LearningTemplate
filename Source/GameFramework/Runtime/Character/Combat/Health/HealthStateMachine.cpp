#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/Health/HealthStateMachine.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"
#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"

namespace sw
{
    namespace
    {
        /** @brief 상태 바이트에 적는 "때린 쪽 없음"입니다(저장 형식이라 번호 칸에 -1 로 남긴다 — 언리얼 `INDEX_NONE`). */
        constexpr int32 kNoInstigator = constant::invalid_index::kInt32;

        /** @brief 이번 걸음 중 지연이 끝난 뒤의 시간입니다(@p elapsed 는 이번 걸음을 더한 뒤의 값). */
        float32 computeRegenTime( float32 elapsed, float32 delay, float32 deltaTime )
        {
            return MathUtil::clamp( elapsed - delay, 0.0f, deltaTime );
        }
    } // namespace
} // namespace sw

namespace sw
{
    HealthStateMachine::HealthStateMachine()
        : _settings{}
        , _eventBuffer{}
        , _health{ 0.0f }
        , _shield{ 0.0f }
        , _downedHealth{ 0.0f }
        , _poise{ 0.0f }
        , _timeSinceDamage{ 0.0f }
        , _timeSincePoiseDamage{ 0.0f }
        , _poiseBreak{}
        , _invulnerable{}
        , _reviveElapsed{ 0.0f }
        , _reviveSpeedScale{ 1.0f }
        , _reviverID{ constant::invalid_index::kInt32 }
        , _lastInstigatorID{}
        , _downCount{ 0 }
        , _state{ LifeState::Alive }
        , _bReviving{ SW_FALSE }
        , _bDamageImmune{ SW_FALSE }
    {
        // 기본 설정은 선언된 범위 안이다(테스트 `VitalityDefaultSettingsAreInRange` 가 지킨다) — 검사하지 않고 바로 쓴다.
        applySettings( HealthSettings{} );
    }

    bool HealthStateMachine::initialize( const HealthSettings& settings )
    {
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "HealthStateMachine.VitalityRefusedSettings", "HealthStateMachine: refused settings - %#", rangeError.c_str() );
            return false;
        }
        applySettings( settings );
        return true;
    }

    void HealthStateMachine::reset()
    {
        applySettings( HealthSettings( _settings ) );
    }

    void HealthStateMachine::applySettings( const HealthSettings& settings )
    {
        _settings = settings;
        _eventBuffer.clear();
        _downCount = 0;
        respawn();
    }

    void HealthStateMachine::respawn()
    {
        _state                = LifeState::Alive;
        _health               = _settings._maxHealth;
        _shield               = _settings._maxShield;
        _downedHealth         = 0.0f;
        _poise                = _settings._poiseMax;
        _timeSinceDamage      = 0.0f;
        _timeSincePoiseDamage = 0.0f;
        _poiseBreak.clear();
        _invulnerable.clear();
        _reviveElapsed    = 0.0f;
        _reviveSpeedScale = 1.0f;
        _reviverID        = constant::invalid_index::kInt32;
        _bReviving        = SW_FALSE;
        _lastInstigatorID = nullopt;
    }

    void HealthStateMachine::setMaxHealth( float32 maxHealth, bool bFill )
    {
        if ( maxHealth <= 0.0f )
            return;
        _settings._maxHealth = maxHealth;
        if ( isAlive() && bFill )
        {
            const float32 healed = maxHealth - _health;
            _health              = maxHealth;
            if ( healed > 0.0f )
                pushEvent( HealthEventType::Healed, healed, nullopt );
            return;
        }
        _health = MathUtil::min( _health, maxHealth );
    }

    HealthDamageResult HealthStateMachine::applyDamage( float32 amount, float32 poiseDamage, optional<int32> instigatorID )
    {
        HealthDamageResult result;
        amount      = MathUtil::max( 0.0f, amount );
        poiseDamage = MathUtil::max( 0.0f, poiseDamage );
        if ( shouldIgnoreDamage() )
        {
            result._bIgnored = SW_TRUE;
            return result;
        }
        _lastInstigatorID = instigatorID;
        _timeSinceDamage  = 0.0f;

        if ( _state == LifeState::Downed )
        {
            // 기절 중 — 실드 · 경직은 없다. 출혈 체력을 깎고, 설정이면 부활을 끊는다.
            result._healthDamage = DepletingValue::take( _downedHealth, amount )._taken;
            if ( result._healthDamage > 0.0f )
                pushEvent( HealthEventType::Damaged, result._healthDamage, instigatorID );
            const bool bReviveInterrupted = amount > 0.0f && isReviving() && _settings._bDamageInterruptsRevive == SW_TRUE;
            if ( bReviveInterrupted )
            {
                pushEvent( HealthEventType::ReviveInterrupted, 0.0f, instigatorID );
                stopRevive();
            }
            if ( _downedHealth <= 0.0f && amount > 0.0f )
            {
                enterDead( instigatorID );
                result._bDied = SW_TRUE;
            }
            return result;
        }

        // 실드가 먼저, 넘친 만큼 체력.
        const bool bHadShield  = _shield > 0.0f;
        result._shieldAbsorbed = DepletingValue::take( _shield, amount )._taken;
        result._healthDamage   = DepletingValue::take( _health, amount - result._shieldAbsorbed )._taken;
        if ( result._shieldAbsorbed + result._healthDamage > 0.0f )
            pushEvent( HealthEventType::Damaged, result._shieldAbsorbed + result._healthDamage, instigatorID );
        if ( bHadShield && _shield <= 0.0f )
        {
            _shield               = 0.0f;
            result._bShieldBroken = SW_TRUE;
            pushEvent( HealthEventType::ShieldBroken, 0.0f, instigatorID );
        }

        // 경직 — 붕괴 중에는 더 쌓지 않는다(붕괴가 끝나면 가득 찬다).
        const bool bPoiseDamageApplies = _settings._poiseMax > 0.0f && poiseDamage > 0.0f && isPoiseBroken() == false;
        if ( bPoiseDamageApplies )
        {
            _timeSincePoiseDamage = 0.0f;
            _poise                = MathUtil::max( 0.0f, _poise - poiseDamage );
            if ( _poise <= 0.0f )
            {
                _poiseBreak.start( MathUtil::max( _settings._poiseBreakDuration, 1.0e-6f ) );
                result._bPoiseBroken = SW_TRUE;
                pushEvent( HealthEventType::PoiseBroken, poiseDamage, instigatorID );
            }
        }

        if ( _health <= 0.0f && amount > 0.0f )
        {
            _health                   = 0.0f;
            const bool bDownLimitLeft = _settings._maxDownCount == 0 || _downCount < _settings._maxDownCount;
            const bool bEnterDowned   = _settings._bDownedEnabled == SW_TRUE && bDownLimitLeft && _settings._downedHealth > 0.0f;
            if ( bEnterDowned )
            {
                enterDowned( instigatorID );
                result._bDowned = SW_TRUE;
            }
            else
            {
                enterDead( instigatorID );
                result._bDied = SW_TRUE;
            }
        }
        return result;
    }

    float32 HealthStateMachine::heal( float32 amount )
    {
        if ( _state != LifeState::Alive || amount <= 0.0f )
            return 0.0f;
        const float32 healed = MathUtil::min( amount, _settings._maxHealth - _health );
        _health += healed;
        if ( healed > 0.0f )
            pushEvent( HealthEventType::Healed, healed, nullopt );
        return healed;
    }

    float32 HealthStateMachine::addShield( float32 amount )
    {
        if ( _state != LifeState::Alive || amount <= 0.0f )
            return 0.0f;
        const float32 added = MathUtil::min( amount, _settings._maxShield - _shield );
        _shield += added;
        return added;
    }

    void HealthStateMachine::update( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f || _state == LifeState::Dead )
            return;
        _invulnerable.update( deltaTime );

        if ( _state == LifeState::Downed )
        {
            if ( isReviving() )
            {
                // 살리는 동안은 출혈이 멈춘다.
                _reviveElapsed += deltaTime * _reviveSpeedScale;
                if ( _reviveElapsed >= _settings._reviveTime )
                    finishRevive();
                return;
            }
            if ( DepletingValue::take( _downedHealth, _settings._bleedoutRate * deltaTime )._bDepleted == SW_TRUE )
                enterDead( _lastInstigatorID );
            return;
        }

        _timeSinceDamage += deltaTime;
        const float32 shieldRegenTime = computeRegenTime( _timeSinceDamage, _settings._shieldRegenDelay, deltaTime );
        _shield                       = MathUtil::min( _settings._maxShield, _shield + _settings._shieldRegenRate * shieldRegenTime );
        const float32 healthRegenTime = computeRegenTime( _timeSinceDamage, _settings._healthRegenDelay, deltaTime );
        _health                       = MathUtil::min( _settings._maxHealth, _health + _settings._healthRegenRate * healthRegenTime );

        if ( _settings._poiseMax <= 0.0f )
            return;
        if ( isPoiseBroken() )
        {
            if ( _poiseBreak.update( deltaTime ) )
            {
                _poise                = _settings._poiseMax;
                _timeSincePoiseDamage = 0.0f;
                pushEvent( HealthEventType::PoiseRecovered, 0.0f, nullopt );
            }
            return;
        }
        _timeSincePoiseDamage += deltaTime;
        const float32 poiseRegenTime = computeRegenTime( _timeSincePoiseDamage, _settings._poiseRegenDelay, deltaTime );
        _poise                       = MathUtil::min( _settings._poiseMax, _poise + _settings._poiseRegenRate * poiseRegenTime );
    }

    bool HealthStateMachine::startRevive( int32 reviverID, float32 speedScale )
    {
        if ( _state != LifeState::Downed )
            return false;
        _reviveSpeedScale = MathUtil::max( 0.0f, speedScale );
        _reviverID        = reviverID;
        if ( isReviving() == false )
        {
            _bReviving = SW_TRUE;
            pushEvent( HealthEventType::ReviveStarted, 0.0f, reviverID );
        }
        if ( _settings._reviveTime <= 0.0f )
            finishRevive();
        return true;
    }

    void HealthStateMachine::stopRevive()
    {
        if ( isReviving() == false )
            return;
        _bReviving        = SW_FALSE;
        _reviverID        = constant::invalid_index::kInt32;
        _reviveSpeedScale = 1.0f;
        if ( _settings._bKeepReviveProgress == SW_FALSE )
            _reviveElapsed = 0.0f;
    }

    void HealthStateMachine::setInvulnerable( float32 seconds )
    {
        _invulnerable.extendTo( seconds );
    }

    void HealthStateMachine::kill( optional<int32> instigatorID )
    {
        if ( _state == LifeState::Dead )
            return;
        enterDead( instigatorID );
    }

    void HealthStateMachine::pollEvents( vector<HealthEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    float32 HealthStateMachine::getHealthRatio() const
    {
        return _health / _settings._maxHealth;
    }

    float32 HealthStateMachine::getReviveProgress() const
    {
        if ( _settings._reviveTime <= 0.0f )
            return _state == LifeState::Downed ? 0.0f : 1.0f;
        return MathUtil::saturate( _reviveElapsed / _settings._reviveTime );
    }

    void HealthStateMachine::enterDowned( optional<int32> instigatorID )
    {
        _state        = LifeState::Downed;
        _health       = 0.0f;
        _shield       = 0.0f;
        _downedHealth = _settings._downedHealth;
        _poiseBreak.clear();
        _reviveElapsed = 0.0f;
        _reviverID     = constant::invalid_index::kInt32;
        _bReviving     = SW_FALSE;
        ++_downCount;
        pushEvent( HealthEventType::Downed, 0.0f, instigatorID );
    }

    void HealthStateMachine::enterDead( optional<int32> instigatorID )
    {
        _state        = LifeState::Dead;
        _health       = 0.0f;
        _shield       = 0.0f;
        _downedHealth = 0.0f;
        _poiseBreak.clear();
        _reviverID = constant::invalid_index::kInt32;
        _bReviving = SW_FALSE;
        pushEvent( HealthEventType::Died, 0.0f, instigatorID );
    }

    void HealthStateMachine::finishRevive()
    {
        const int32 reviverID = _reviverID;
        _state                = LifeState::Alive;
        _health               = _settings._maxHealth * _settings._reviveHealthRatio;
        _shield               = 0.0f;
        _downedHealth         = 0.0f;
        _poise                = _settings._poiseMax;
        _timeSinceDamage      = 0.0f;
        _timeSincePoiseDamage = 0.0f;
        _reviveElapsed        = 0.0f;
        _reviveSpeedScale     = 1.0f;
        _reviverID            = constant::invalid_index::kInt32;
        _bReviving            = SW_FALSE;
        _invulnerable.extendTo( _settings._invulnerableAfterRevive );
        pushEvent( HealthEventType::Revived, _health, reviverID );
    }

    void HealthStateMachine::pushEvent( HealthEventType type, float32 amount, optional<int32> instigatorID )
    {
        HealthEvent event;
        event._type         = type;
        event._amount       = amount;
        event._instigatorID = instigatorID;
        _eventBuffer.push( event );
    }

    void HealthStateMachine::serializeState( StateIO& io )
    {
        if ( io.isReading() )
        {
            HealthStateMachine restored;
            restored._settings      = _settings;
            restored._bDamageImmune = _bDamageImmune; // 개발 스위치는 상태가 아니다
            *this                   = std::move( restored );
        }
        io.value( _health );
        io.value( _shield );
        io.value( _downedHealth );
        io.value( _poise );
        io.value( _timeSinceDamage );
        io.value( _timeSincePoiseDamage );
        io.value( _poiseBreak );
        io.value( _invulnerable );
        io.value( _reviveElapsed );
        io.value( _reviveSpeedScale );
        io.value( _reviverID );
        int32 lastInstigatorID = _lastInstigatorID.value_or( kNoInstigator );
        io.value( lastInstigatorID );
        if ( io.isReading() )
            _lastInstigatorID = lastInstigatorID == kNoInstigator ? nullopt : optional<int32>( lastInstigatorID );
        io.value( _downCount );
        io.enumValue( _state, LifeState::Dead );
        io.flag( _bReviving );
        io.check( 0.0f <= _health && _health <= _settings._maxHealth && 0.0f <= _shield && _shield <= _settings._maxShield && 0.0f <= _downedHealth && _downedHealth <= _settings._downedHealth && 0 <= _downCount );
    }

} // namespace sw
