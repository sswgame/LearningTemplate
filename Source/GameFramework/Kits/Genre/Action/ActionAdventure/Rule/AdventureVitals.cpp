#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureVitals.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Foundation/Utility/ConfigRangeUtil.h"

namespace sw
{
    AdventureVitals::AdventureVitals()
        : _settings{}
        , _health{}
        , _magic{}
        , _stamina{}
        , _heartCount{ 0 }
        , _heartPieceCount{ 0 }
        , _bMagicUpgraded{ SW_FALSE }
    {
        // 기본 설정은 선언된 범위 안이다(테스트 `AdventureVitalsTest.RefusesOutOfRangeSettingsInsteadOfClamping` 이 지킨다).
        const bool bDefaultsAccepted = initialize( AdventureVitalsSettings{} );
        SW_ASSERT( bDefaultsAccepted );
        (void)bDefaultsAccepted;
    }

    bool AdventureVitals::initialize( const AdventureVitalsSettings& settings )
    {
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "AdventureVitals.AdventureVitalsRefusedSettings", "AdventureVitals: refused settings - %#", rangeError.c_str() );
            return false;
        }

        // 따로 만들어 모두 받아들여질 때만 바꾼다 — 하나라도 거절되면 상태는 그대로다.
        CombatHealthSettings healthSettings;
        healthSettings._maxHealth = static_cast<float32>( settings._startHeartCount * kQuartersPerHeart );
        ResourceGaugeSettings magicSettings;
        magicSettings._max       = settings._magicMax;
        magicSettings._regenRate = 0.0f; // 마법은 저절로 차지 않는다(항아리나 물약으로 채운다)
        ResourceGaugeSettings staminaSettings;
        staminaSettings._max              = settings._staminaMax;
        staminaSettings._regenRate        = settings._staminaRegenRate;
        staminaSettings._regenDelay       = settings._staminaRegenDelay;
        staminaSettings._exhaustThreshold = settings._staminaRecoverLevel;
        CombatHealth  health;
        ResourceGauge magic;
        ResourceGauge stamina;
        const bool    bHealthAccepted  = health.initialize( healthSettings );
        const bool    bMagicAccepted   = magic.initialize( magicSettings );
        const bool    bStaminaAccepted = stamina.initialize( staminaSettings );
        const bool    bAllAccepted     = bHealthAccepted && bMagicAccepted && bStaminaAccepted;
        if ( bAllAccepted == false )
            return false;

        _settings        = settings;
        _heartCount      = settings._startHeartCount;
        _heartPieceCount = 0;
        _bMagicUpgraded  = SW_FALSE;
        _health          = health;
        _magic           = magic;
        _stamina         = stamina;
        return true;
    }

    bool AdventureVitals::resetHealth()
    {
        CombatHealthSettings health;
        health._maxHealth = static_cast<float32>( _heartCount * kQuartersPerHeart );
        return _health.initialize( health );
    }

    bool AdventureVitals::applyDamage( int32 quarters )
    {
        if ( quarters <= 0 )
            return false;
        const CombatHealthDamageResult result = _health.applyDamage( static_cast<float32>( quarters ) );
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
        _health.setMaxHealth( static_cast<float32>( _heartCount * kQuartersPerHeart ), true ); // 새 그릇은 가득 찬 상태로 시작한다(알림과 상태는 그대로 둔다)
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

    AdventureStaminaResult AdventureVitals::updateStamina( AdventureStaminaAction action, float32 deltaTime )
    {
        float32 rate = 0.0f;
        switch ( action )
        {
            case AdventureStaminaAction::Idle:
            {
                _stamina.update( deltaTime );
                return AdventureStaminaResult::Continue;
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
            return AdventureStaminaResult::Continue;
        switch ( action )
        {
            case AdventureStaminaAction::Climb:
            case AdventureStaminaAction::Glide:
            {
                return AdventureStaminaResult::Fall;
            }
            case AdventureStaminaAction::Swim:
            {
                (void)applyDamage( _settings._drownDamage ); // 죽었는지는 체력이 판단한다. 여기서는 빠졌다는 결과만 알린다
                _stamina.refill();                           // 물가로 돌아가면 다시 헤엄칠 수 있다
                return AdventureStaminaResult::Drown;
            }
            default:
            {
                return AdventureStaminaResult::Stop;
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

    int32 AdventureVitals::computeHealthQuarters() const
    {
        return static_cast<int32>( _health.getHealth() + 0.5f );
    }

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
        // 사본에 읽고 끝까지 맞으면 바꾼다. 설정은 사본이 그대로 가지고 있다.
        AdventureVitals restored = *this;
        archive >> restored._heartCount;
        archive >> restored._heartPieceCount;
        archive >> restored._bMagicUpgraded;
        const bool bHeadValid = archive.isOk() && 1 <= restored._heartCount && restored._heartCount <= _settings._maxHeartCount && 0 <= restored._heartPieceCount &&
                                restored._heartPieceCount < _settings._piecesPerHeart && restored._bMagicUpgraded <= SW_TRUE;
        if ( bHeadValid == false )
            return false;
        if ( restored.resetHealth() == false ) // 체력 최대값은 하트 수가 정한다. 현재 값은 아래에서 읽는다
            return false;
        const bool bBodyRead = restored._health.readState( archive ) && restored._magic.readState( archive ) && restored._stamina.readState( archive );
        if ( bBodyRead == false )
            return false;
        // 값은 하트 수와 그릇이 정한 최대를 넘지 못하고, 두 배 마법을 받지 않았으면 마법 보너스가 없다(기반 값들은 자기 최대를 보지 않는다).
        const bool bConsistent = restored._health.getHealth() <= static_cast<float32>( restored.getMaxHealthQuarters() ) && restored._magic.getValue() <= restored._magic.getMax() &&
                                 restored._stamina.getValue() <= restored._stamina.getMax() && 0.0f <= restored._magic.getMaxBonus() && 0.0f <= restored._stamina.getMaxBonus() &&
                                 restored._stamina.getMaxBonus() <= _settings._staminaVesselMax + 0.001f && ( restored._bMagicUpgraded == SW_TRUE || restored._magic.getMaxBonus() <= 0.0f );
        if ( bConsistent == false )
            return false;
        *this = std::move( restored );
        return true;
    }
} // namespace sw
