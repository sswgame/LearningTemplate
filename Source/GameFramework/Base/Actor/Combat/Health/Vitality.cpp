#include "pch.h"

#include "GameFramework/Base/Actor/Combat/Health/Vitality.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    namespace
    {
        struct VitalityInternal
        {
            /** @brief 이번 걸음 중 지연이 끝난 뒤의 시간입니다(@p elapsed 는 이번 걸음을 더한 뒤의 값). */
            static float32 computeRegenTime( float32 elapsed, float32 delay, float32 deltaTime ) { return MathUtil::clamp( elapsed - delay, 0.0f, deltaTime ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    Vitality::Vitality()
        : _settings{}
        , _eventBuffer{}
        , _health{ 0.0f }
        , _shield{ 0.0f }
        , _downedHealth{ 0.0f }
        , _poise{ 0.0f }
        , _sinceDamage{ 0.0f }
        , _sincePoiseDamage{ 0.0f }
        , _poiseBreak{}
        , _invulnerable{}
        , _reviveElapsed{ 0.0f }
        , _reviveSpeedScale{ 1.0f }
        , _reviverId{ -1 }
        , _lastInstigatorId{ -1 }
        , _downCount{ 0 }
        , _state{ VitalityState::Alive }
        , _bReviving{ SW_FALSE }
    {
        initialize( _settings );
    }

    Vitality::Vitality( const VitalitySettings& settings )
        : Vitality()
    {
        initialize( settings );
    }

    void Vitality::initialize( const VitalitySettings& settings )
    {
        _settings                     = settings;
        _settings._maxHealth          = MathUtil::max( 1.0f, _settings._maxHealth );
        _settings._maxShield          = MathUtil::max( 0.0f, _settings._maxShield );
        _settings._downedHealth       = MathUtil::max( 0.0f, _settings._downedHealth );
        _settings._reviveTime         = MathUtil::max( 0.0f, _settings._reviveTime );
        _settings._reviveHealthRatio  = MathUtil::clamp( _settings._reviveHealthRatio, 0.01f, 1.0f );
        _settings._poiseMax           = MathUtil::max( 0.0f, _settings._poiseMax );
        _settings._poiseBreakDuration = MathUtil::max( 0.0f, _settings._poiseBreakDuration );
        _settings._maxDownCount       = MathUtil::max( 0, _settings._maxDownCount );
        _eventBuffer.clear();
        _downCount = 0;
        respawn();
    }

    void Vitality::respawn()
    {
        _state            = VitalityState::Alive;
        _health           = _settings._maxHealth;
        _shield           = _settings._maxShield;
        _downedHealth     = 0.0f;
        _poise            = _settings._poiseMax;
        _sinceDamage      = 0.0f;
        _sincePoiseDamage = 0.0f;
        _poiseBreak.clear();
        _invulnerable.clear();
        _reviveElapsed    = 0.0f;
        _reviveSpeedScale = 1.0f;
        _reviverId        = -1;
        _bReviving        = SW_FALSE;
        _lastInstigatorId = -1;
    }

    void Vitality::setMaxHealth( float32 maxHealth, bool bFill )
    {
        if ( maxHealth <= 0.0f )
            return;
        _settings._maxHealth = maxHealth;
        if ( isAlive() && bFill )
        {
            const float32 healed = maxHealth - _health;
            _health              = maxHealth;
            if ( healed > 0.0f )
                pushEvent( VitalityEventType::Healed, healed, -1 );
            return;
        }
        _health = MathUtil::min( _health, maxHealth );
    }

    VitalityDamageResult Vitality::applyDamage( float32 amount, float32 poiseDamage, int32 instigatorId )
    {
        VitalityDamageResult result;
        amount      = MathUtil::max( 0.0f, amount );
        poiseDamage = MathUtil::max( 0.0f, poiseDamage );
        if ( _state == VitalityState::Dead || isInvulnerable() )
        {
            result._bIgnored = SW_TRUE;
            return result;
        }
        _lastInstigatorId = instigatorId;
        _sinceDamage      = 0.0f;

        if ( _state == VitalityState::Downed )
        {
            // 기절 중 — 실드 · 경직은 없다. 출혈 체력을 깎고, 설정이면 부활을 끊는다.
            result._healthDamage = MathUtil::min( amount, _downedHealth );
            _downedHealth -= result._healthDamage;
            if ( result._healthDamage > 0.0f )
                pushEvent( VitalityEventType::Damaged, result._healthDamage, instigatorId );
            if ( amount > 0.0f && isReviving() && _settings._bDamageInterruptsRevive == SW_TRUE )
            {
                pushEvent( VitalityEventType::ReviveInterrupted, 0.0f, instigatorId );
                stopRevive();
            }
            if ( _downedHealth <= 0.0f && amount > 0.0f )
            {
                enterDead( instigatorId );
                result._bDied = SW_TRUE;
            }
            return result;
        }

        // 실드가 먼저, 넘친 만큼 체력.
        const bool bHadShield  = _shield > 0.0f;
        result._shieldAbsorbed = MathUtil::min( amount, _shield );
        _shield -= result._shieldAbsorbed;
        result._healthDamage = MathUtil::min( amount - result._shieldAbsorbed, _health );
        _health -= result._healthDamage;
        if ( result._shieldAbsorbed + result._healthDamage > 0.0f )
            pushEvent( VitalityEventType::Damaged, result._shieldAbsorbed + result._healthDamage, instigatorId );
        if ( bHadShield && _shield <= 0.0f )
        {
            _shield               = 0.0f;
            result._bShieldBroken = SW_TRUE;
            pushEvent( VitalityEventType::ShieldBroken, 0.0f, instigatorId );
        }

        // 경직 — 붕괴 중에는 더 쌓지 않는다(붕괴가 끝나면 가득 찬다).
        if ( _settings._poiseMax > 0.0f && poiseDamage > 0.0f && isPoiseBroken() == false )
        {
            _sincePoiseDamage = 0.0f;
            _poise            = MathUtil::max( 0.0f, _poise - poiseDamage );
            if ( _poise <= 0.0f )
            {
                _poiseBreak.start( MathUtil::max( _settings._poiseBreakDuration, 1.0e-6f ) );
                result._bPoiseBroken = SW_TRUE;
                pushEvent( VitalityEventType::PoiseBroken, poiseDamage, instigatorId );
            }
        }

        if ( _health <= 0.0f && amount > 0.0f )
        {
            _health                   = 0.0f;
            const bool bDownLimitLeft = _settings._maxDownCount == 0 || _downCount < _settings._maxDownCount;
            if ( _settings._bDownedEnabled == SW_TRUE && bDownLimitLeft && _settings._downedHealth > 0.0f )
            {
                enterDowned( instigatorId );
                result._bDowned = SW_TRUE;
            }
            else
            {
                enterDead( instigatorId );
                result._bDied = SW_TRUE;
            }
        }
        return result;
    }

    float32 Vitality::heal( float32 amount )
    {
        if ( _state != VitalityState::Alive || amount <= 0.0f )
            return 0.0f;
        const float32 healed = MathUtil::min( amount, _settings._maxHealth - _health );
        _health += healed;
        if ( healed > 0.0f )
            pushEvent( VitalityEventType::Healed, healed, -1 );
        return healed;
    }

    float32 Vitality::addShield( float32 amount )
    {
        if ( _state != VitalityState::Alive || amount <= 0.0f )
            return 0.0f;
        const float32 added = MathUtil::min( amount, _settings._maxShield - _shield );
        _shield += added;
        return added;
    }

    void Vitality::update( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f || _state == VitalityState::Dead )
            return;
        _invulnerable.tick( deltaTime );

        if ( _state == VitalityState::Downed )
        {
            if ( isReviving() )
            {
                // 살리는 동안은 출혈이 멈춘다.
                _reviveElapsed += deltaTime * _reviveSpeedScale;
                if ( _reviveElapsed >= _settings._reviveTime )
                    finishRevive();
                return;
            }
            _downedHealth = MathUtil::max( 0.0f, _downedHealth - _settings._bleedoutRate * deltaTime );
            if ( _downedHealth <= 0.0f )
                enterDead( _lastInstigatorId );
            return;
        }

        _sinceDamage += deltaTime;
        const float32 shieldRegenTime = VitalityInternal::computeRegenTime( _sinceDamage, _settings._shieldRegenDelay, deltaTime );
        _shield                       = MathUtil::min( _settings._maxShield, _shield + MathUtil::max( 0.0f, _settings._shieldRegenRate ) * shieldRegenTime );
        const float32 healthRegenTime = VitalityInternal::computeRegenTime( _sinceDamage, _settings._healthRegenDelay, deltaTime );
        _health                       = MathUtil::min( _settings._maxHealth, _health + MathUtil::max( 0.0f, _settings._healthRegenRate ) * healthRegenTime );

        if ( _settings._poiseMax <= 0.0f )
            return;
        if ( isPoiseBroken() )
        {
            if ( _poiseBreak.tick( deltaTime ) )
            {
                _poise            = _settings._poiseMax;
                _sincePoiseDamage = 0.0f;
                pushEvent( VitalityEventType::PoiseRecovered, 0.0f, -1 );
            }
            return;
        }
        _sincePoiseDamage += deltaTime;
        const float32 poiseRegenTime = VitalityInternal::computeRegenTime( _sincePoiseDamage, _settings._poiseRegenDelay, deltaTime );
        _poise                       = MathUtil::min( _settings._poiseMax, _poise + MathUtil::max( 0.0f, _settings._poiseRegenRate ) * poiseRegenTime );
    }

    bool Vitality::startRevive( int32 reviverId, float32 speedScale )
    {
        if ( _state != VitalityState::Downed )
            return false;
        _reviveSpeedScale = MathUtil::max( 0.0f, speedScale );
        _reviverId        = reviverId;
        if ( isReviving() == false )
        {
            _bReviving = SW_TRUE;
            pushEvent( VitalityEventType::ReviveStarted, 0.0f, reviverId );
        }
        if ( _settings._reviveTime <= 0.0f )
            finishRevive();
        return true;
    }

    void Vitality::stopRevive()
    {
        if ( isReviving() == false )
            return;
        _bReviving        = SW_FALSE;
        _reviverId        = -1;
        _reviveSpeedScale = 1.0f;
        if ( _settings._bKeepReviveProgress == SW_FALSE )
            _reviveElapsed = 0.0f;
    }

    void Vitality::setInvulnerable( float32 seconds ) { _invulnerable.extendTo( seconds ); }

    void Vitality::kill( int32 instigatorId )
    {
        if ( _state == VitalityState::Dead )
            return;
        enterDead( instigatorId );
    }

    void Vitality::drainEvents( vector<VitalityEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    float32 Vitality::getHealthRatio() const { return _health / _settings._maxHealth; }

    float32 Vitality::getReviveProgress() const
    {
        if ( _settings._reviveTime <= 0.0f )
            return _state == VitalityState::Downed ? 0.0f : 1.0f;
        return MathUtil::saturate( _reviveElapsed / _settings._reviveTime );
    }

    void Vitality::enterDowned( int32 instigatorId )
    {
        _state        = VitalityState::Downed;
        _health       = 0.0f;
        _shield       = 0.0f;
        _downedHealth = _settings._downedHealth;
        _poiseBreak.clear();
        _reviveElapsed = 0.0f;
        _reviverId     = -1;
        _bReviving     = SW_FALSE;
        ++_downCount;
        pushEvent( VitalityEventType::Downed, 0.0f, instigatorId );
    }

    void Vitality::enterDead( int32 instigatorId )
    {
        _state        = VitalityState::Dead;
        _health       = 0.0f;
        _shield       = 0.0f;
        _downedHealth = 0.0f;
        _poiseBreak.clear();
        _reviverId = -1;
        _bReviving = SW_FALSE;
        pushEvent( VitalityEventType::Died, 0.0f, instigatorId );
    }

    void Vitality::finishRevive()
    {
        const int32 reviverId = _reviverId;
        _state                = VitalityState::Alive;
        _health               = _settings._maxHealth * _settings._reviveHealthRatio;
        _shield               = 0.0f;
        _downedHealth         = 0.0f;
        _poise                = _settings._poiseMax;
        _sinceDamage          = 0.0f;
        _sincePoiseDamage     = 0.0f;
        _reviveElapsed        = 0.0f;
        _reviveSpeedScale     = 1.0f;
        _reviverId            = -1;
        _bReviving            = SW_FALSE;
        _invulnerable.extendTo( _settings._invulnerableAfterRevive );
        pushEvent( VitalityEventType::Revived, _health, reviverId );
    }

    void Vitality::pushEvent( VitalityEventType type, float32 amount, int32 instigatorId )
    {
        VitalityEvent event;
        event._type         = type;
        event._amount       = amount;
        event._instigatorId = instigatorId;
        _eventBuffer.push( event );
    }

    void Vitality::writeState( Archive& outArchive ) const
    {
        outArchive << _health;
        outArchive << _shield;
        outArchive << _downedHealth;
        outArchive << _poise;
        outArchive << _sinceDamage;
        outArchive << _sincePoiseDamage;
        StateArchiveUtil::writeCountdown( outArchive, _poiseBreak );
        StateArchiveUtil::writeCountdown( outArchive, _invulnerable );
        outArchive << _reviveElapsed;
        outArchive << _reviveSpeedScale;
        outArchive << _reviverId;
        outArchive << _lastInstigatorId;
        outArchive << _downCount;
        outArchive << static_cast<uint8>( _state );
        outArchive << _bReviving;
    }

    bool Vitality::readState( Archive& archive )
    {
        Vitality restored;
        restored._settings = _settings;
        uint8 state        = 0;
        archive >> restored._health;
        archive >> restored._shield;
        archive >> restored._downedHealth;
        archive >> restored._poise;
        archive >> restored._sinceDamage;
        archive >> restored._sincePoiseDamage;
        const bool bTimerRead = StateArchiveUtil::readCountdown( archive, restored._poiseBreak ) && StateArchiveUtil::readCountdown( archive, restored._invulnerable );
        archive >> restored._reviveElapsed;
        archive >> restored._reviveSpeedScale;
        archive >> restored._reviverId;
        archive >> restored._lastInstigatorId;
        archive >> restored._downCount;
        archive >> state;
        archive >> restored._bReviving;
        const bool bValid = bTimerRead && archive.isOk() && 0.0f <= restored._health && 0.0f <= restored._shield && 0 <= restored._downCount &&
                            state <= static_cast<uint8>( VitalityState::Dead ) && restored._bReviving <= SW_TRUE;
        if ( bValid == false )
            return false;
        restored._state = static_cast<VitalityState>( state );
        *this           = std::move( restored );
        return true;
    }
} // namespace sw
