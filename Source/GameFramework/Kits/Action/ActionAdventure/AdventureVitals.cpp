#include "pch.h"

#include "GameFramework/Kits/Action/ActionAdventure/AdventureVitals.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
    const utf8* toString( AdventureStaminaOutcome outcome )
    {
        switch ( outcome )
        {
            case AdventureStaminaOutcome::Continue:
                return "Continue";
            case AdventureStaminaOutcome::Stop:
                return "Stop";
            case AdventureStaminaOutcome::Fall:
                return "Fall";
            case AdventureStaminaOutcome::Drown:
                return "Drown";
        }
        return "Unknown";
    }

    AdventureVitals::AdventureVitals()
        : _settings{}
        , _health{}
        , _magic{}
        , _stamina{}
        , _heartCount{ 0 }
        , _heartPieceCount{ 0 }
        , _bMagicUpgraded{ SW_FALSE }
    {
        initialize( AdventureVitalsSettings{} );
    }

    void AdventureVitals::initialize( const AdventureVitalsSettings& settings )
    {
        _settings                 = settings;
        _settings._maxHeartCount  = MathUtil::max( 1, _settings._maxHeartCount );
        _settings._piecesPerHeart = MathUtil::max( 1, _settings._piecesPerHeart );
        _heartCount               = MathUtil::clamp( _settings._startHeartCount, 1, _settings._maxHeartCount );
        _heartPieceCount          = 0;
        _bMagicUpgraded           = SW_FALSE;
        resetHealth();

        ResourceGaugeSettings magic;
        magic._max       = MathUtil::max( 1.0f, _settings._magicMax );
        magic._regenRate = 0.0f; // 마법은 저절로 차지 않는다(항아리 · 물약)
        _magic.initialize( magic );

        ResourceGaugeSettings stamina;
        stamina._max              = MathUtil::max( 1.0f, _settings._staminaMax );
        stamina._regenRate        = _settings._staminaRegenRate;
        stamina._regenDelay       = _settings._staminaRegenDelay;
        stamina._exhaustThreshold = MathUtil::clamp( _settings._staminaRecoverLevel, 0.001f, stamina._max );
        _stamina.initialize( stamina );
    }

    void AdventureVitals::resetHealth()
    {
        VitalitySettings health;
        health._maxHealth = static_cast<float32>( _heartCount * kQuartersPerHeart );
        _health.initialize( health );
    }

    bool AdventureVitals::applyDamage( int32 quarters )
    {
        if ( quarters <= 0 )
            return false;
        const VitalityDamageResult result = _health.applyDamage( static_cast<float32>( quarters ) );
        return result._bDied == SW_TRUE;
    }

    int32 AdventureVitals::heal( int32 quarters )
    {
        if ( quarters <= 0 )
            return 0;
        return static_cast<int32>( _health.heal( static_cast<float32>( quarters ) ) + 0.5f );
    }

    bool AdventureVitals::addHeartPiece()
    {
        if ( _heartCount >= _settings._maxHeartCount )
            return false;
        ++_heartPieceCount;
        if ( _heartPieceCount < _settings._piecesPerHeart )
            return false;
        _heartPieceCount = 0;
        return addHeartContainer();
    }

    bool AdventureVitals::addHeartContainer()
    {
        if ( _heartCount >= _settings._maxHeartCount )
            return false;
        ++_heartCount;
        _health.setMaxHealth( static_cast<float32>( _heartCount * kQuartersPerHeart ), true ); // 새 그릇은 가득 찬다(알림 · 상태는 그대로)
        return true;
    }

    bool AdventureVitals::upgradeMagic()
    {
        if ( _bMagicUpgraded == SW_TRUE )
            return false;
        _bMagicUpgraded = SW_TRUE;
        _magic.setMaxBonus( _settings._magicUpgrade );
        return true;
    }

    AdventureStaminaOutcome AdventureVitals::updateStamina( AdventureStaminaAction action, float32 deltaTime )
    {
        float32 rate = 0.0f;
        switch ( action )
        {
            case AdventureStaminaAction::Idle:
            {
                _stamina.update( deltaTime );
                return AdventureStaminaOutcome::Continue;
            }
            case AdventureStaminaAction::Climb:
            {
                rate = _settings._climbDrain;
                break;
            }
            case AdventureStaminaAction::Sprint:
            {
                rate = _settings._sprintDrain;
                break;
            }
            case AdventureStaminaAction::Glide:
            {
                rate = _settings._glideDrain;
                break;
            }
            case AdventureStaminaAction::Swim:
            {
                rate = _settings._swimDrain;
                break;
            }
        }
        if ( _stamina.drain( rate, deltaTime ) )
            return AdventureStaminaOutcome::Continue;
        switch ( action )
        {
            case AdventureStaminaAction::Climb:
            case AdventureStaminaAction::Glide:
            {
                return AdventureStaminaOutcome::Fall;
            }
            case AdventureStaminaAction::Swim:
            {
                (void)applyDamage( _settings._drownDamage );
                _stamina.refill(); // 물가로 돌아가면 다시 헤엄칠 수 있다
                return AdventureStaminaOutcome::Drown;
            }
            default:
            {
                return AdventureStaminaOutcome::Stop;
            }
        }
    }

    bool AdventureVitals::addStaminaVessel()
    {
        const float32 bonus = _stamina.getMaxBonus();
        if ( bonus + _settings._staminaVessel > _settings._staminaVesselMax + 0.001f )
            return false;
        _stamina.setMaxBonus( bonus + _settings._staminaVessel );
        return true;
    }

    int32 AdventureVitals::computeHealthQuarters() const { return static_cast<int32>( _health.getHealth() + 0.5f ); }

    void AdventureVitals::writeState( Archive& outArchive ) const
    {
        outArchive << _heartCount;
        outArchive << _heartPieceCount;
        outArchive << _bMagicUpgraded;
        _health.writeState( outArchive );
        _magic.writeState( outArchive );
        _stamina.writeState( outArchive );
    }

    bool AdventureVitals::readState( Archive& archive )
    {
        // 사본에 읽고 끝까지 맞으면 바꾼다 — 설정은 사본이 그대로 든다.
        AdventureVitals restored = *this;
        archive >> restored._heartCount;
        archive >> restored._heartPieceCount;
        archive >> restored._bMagicUpgraded;
        const bool bHeadValid = archive.isOk() && 1 <= restored._heartCount && restored._heartCount <= _settings._maxHeartCount && 0 <= restored._heartPieceCount &&
                                restored._heartPieceCount < _settings._piecesPerHeart && restored._bMagicUpgraded <= SW_TRUE;
        if ( bHeadValid == false )
            return false;
        restored.resetHealth(); // 체력 최대는 하트 수가 정한다 — 값은 아래에서 읽는다
        const bool bBodyRead = restored._health.readState( archive ) && restored._magic.readState( archive ) && restored._stamina.readState( archive );
        if ( bBodyRead == false )
            return false;
        *this = std::move( restored );
        return true;
    }
} // namespace sw
