#include "pch.h"

#include "GameFramework/Kits/Rpg/OpenWorldWestern/WesternHorse.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Rpg/OpenWorldWestern/WesternCatalog.h"

namespace sw
{
    namespace
    {
        struct WesternHorseInternal
        {
        };
    } // namespace
} // namespace sw

namespace sw
{
    WesternHorse::WesternHorse()
        : _eventBuffer{}
        , _listAbility{}
        , _health{}
        , _stamina{}
        , _random{}
        , _pCatalog{ nullptr }
        , _pDef{ nullptr }
        , _healthCore{ kCoreMax }
        , _staminaCore{ kCoreMax }
        , _bond{}
        , _bondXpCarry{ 0.0f }
        , _fear{ 0.0f }
        , _hoursSinceBrush{ 0.0f }
        , _bRidden{ SW_FALSE }
        , _bGalloping{ SW_FALSE }
    {
    }

    bool WesternHorse::initialize( const WesternCatalog* pCatalog, const hashed_string& horseId, uint32 seed )
    {
        _pCatalog = pCatalog;
        _pDef     = pCatalog != nullptr ? pCatalog->findHorse( horseId ) : nullptr;
        if ( _pDef == nullptr )
            return false;
        ResourceGaugeSettings health;
        health._max        = _pDef->_health;
        health._regenRate  = _pDef->_health * 0.02f;
        health._regenDelay = 3.0f;
        _health.initialize( health );
        ResourceGaugeSettings stamina;
        stamina._max              = _pDef->_stamina;
        stamina._regenRate        = _pDef->_stamina * 0.1f;
        stamina._regenDelay       = 1.0f;
        stamina._drainPerSecond   = _pDef->_gallopDrain;
        stamina._exhaustThreshold = _pDef->_stamina * 0.25f; // 바닥나면 4 분의 1 까지 쉬어야 다시 달린다
        _stamina.initialize( stamina );
        _random.setSeed( seed );
        _eventBuffer.clear();
        _listAbility.clear();
        _healthCore      = kCoreMax;
        _staminaCore     = kCoreMax;
        _bond            = LevelProgress{};
        _bondXpCarry     = 0.0f;
        _fear            = 0.0f;
        _hoursSinceBrush = pCatalog->getBondExperience()._brushCooldownHours;
        _bRidden         = SW_FALSE;
        _bGalloping      = SW_FALSE;
        applyBondLevel( getBondLevel() );
        _eventBuffer.clear(); // 첫 단계는 알리지 않는다
        return true;
    }

    void WesternHorse::update( float32 deltaTime, float32 gameHours )
    {
        if ( _pDef == nullptr )
            return;
        if ( deltaTime > 0.0f )
        {
            _health.setRegenScale( computeRegenScale( _healthCore ) );
            _stamina.setRegenScale( computeRegenScale( _staminaCore ) );
            _health.update( deltaTime );
            _stamina.update( deltaTime );
            _fear = MathUtil::max( 0.0f, _fear - _pDef->_fearDecayPerSecond * deltaTime );
            if ( _bRidden != SW_FALSE )
                addBondExperience( _pCatalog->getBondExperience()._ridePerSecond * deltaTime );
        }
        if ( gameHours > 0.0f )
        {
            const float32 drain = ( _pDef->_coreDrainPerHour + ( _bGalloping != SW_FALSE ? _pDef->_gallopCoreDrainPerHour : 0.0f ) ) * gameHours;
            _healthCore         = MathUtil::max( 0.0f, _healthCore - drain );
            _staminaCore        = MathUtil::max( 0.0f, _staminaCore - drain );
            _hoursSinceBrush += gameHours;
        }
        _bGalloping = SW_FALSE;
    }

    bool WesternHorse::gallop( float32 deltaTime )
    {
        if ( _pDef == nullptr )
            return false;
        _bGalloping           = SW_TRUE;
        const bool bWasLocked = _stamina.isExhausted();
        const bool bGalloping = _stamina.drain( deltaTime );
        if ( bGalloping == false && bWasLocked == false )
        {
            WesternHorseEvent event;
            event._kind = WesternHorseEvent::Kind::Exhausted;
            _eventBuffer.push( event );
        }
        return bGalloping;
    }

    bool WesternHorse::brush()
    {
        if ( _pCatalog == nullptr )
            return false;
        const WesternBondExperience& bondExperience = _pCatalog->getBondExperience();
        if ( _hoursSinceBrush < bondExperience._brushCooldownHours )
            return false;
        _hoursSinceBrush = 0.0f;
        addBondExperience( bondExperience._brush );
        return true;
    }

    bool WesternHorse::feed( const hashed_string& foodId )
    {
        const WesternFoodDef* pFood = _pCatalog != nullptr ? _pCatalog->findFood( foodId ) : nullptr;
        if ( pFood == nullptr || _pDef == nullptr )
            return false;
        _healthCore  = MathUtil::clamp( _healthCore + pFood->_healthCore, 0.0f, kCoreMax );
        _staminaCore = MathUtil::clamp( _staminaCore + pFood->_staminaCore, 0.0f, kCoreMax );
        _health.restore( pFood->_health );
        _stamina.restore( pFood->_stamina );
        addBondExperience( pFood->_bond > 0.0f ? pFood->_bond : _pCatalog->getBondExperience()._feed );
        return true;
    }

    WesternHorseReaction WesternHorse::frighten( float32 amount )
    {
        if ( _pDef == nullptr || amount <= 0.0f )
            return WesternHorseReaction::Calm;
        const float32 resist = MathUtil::saturate( computeFearResist() );
        _fear += amount * ( 1.0f - resist );
        if ( _fear < kFearThreshold )
            return WesternHorseReaction::Calm;
        _fear = 0.0f;
        WesternHorseEvent event;
        // 탄 사람이 있을 때만 떨어뜨릴 수 있다. 난수는 늘 하나 쓴다(탔는지와 상관없이 같은 수열).
        const bool bBuck = _random.nextChance( ( 1.0f - resist ) * _pDef->_buckChanceScale ) && _bRidden != SW_FALSE;
        event._kind      = bBuck ? WesternHorseEvent::Kind::ThrewRider : WesternHorseEvent::Kind::Spooked;
        _eventBuffer.push( event );
        if ( bBuck )
            _bRidden = SW_FALSE;
        return bBuck ? WesternHorseReaction::Bucked : WesternHorseReaction::Rearing;
    }

    void WesternHorse::calm()
    {
        _fear = 0.0f;
        if ( _pCatalog != nullptr )
            addBondExperience( _pCatalog->getBondExperience()._calm );
    }

    void WesternHorse::addBondExperience( float32 amount )
    {
        if ( _pCatalog == nullptr || amount <= 0.0f )
            return;
        // 기반 레벨 진행은 정수 경험치다 — 소수(초당 타기)는 1 이 될 때까지 들고 있는다.
        const float32 total = _bondXpCarry + amount;
        const int64   whole = static_cast<int64>( MathUtil::floor( total ) );
        _bondXpCarry        = total - static_cast<float32>( whole );
        if ( whole > 0 && _bond.addXp( _pCatalog->getBondCurve(), whole ) > 0 )
            applyBondLevel( getBondLevel() );
    }

    bool WesternHorse::hasAbility( const hashed_string& abilityId ) const
    {
        for ( const hashed_string& ability : _listAbility )
        {
            if ( ability == abilityId )
                return true;
        }
        return false;
    }

    float32 WesternHorse::computeRegenScale( float32 core ) const
    {
        const float32 minScale = _pCatalog != nullptr ? _pCatalog->getSurvival()._minRegenScale : 0.2f;
        return MathUtil::lerp( minScale, 1.0f, MathUtil::saturate( core / kCoreMax ) );
    }

    void WesternHorse::drainEvents( vector<WesternHorseEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void WesternHorse::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeName( outArchive, _pDef != nullptr ? _pDef->_id : hashed_string{} );
        outArchive << static_cast<uint32>( _listAbility.size() );
        for ( const hashed_string& ability : _listAbility )
        {
            StateArchiveUtil::writeName( outArchive, ability );
        }
        _health.writeState( outArchive );
        _stamina.writeState( outArchive );
        StateArchiveUtil::writeRandom( outArchive, _random );
        outArchive << _healthCore;
        outArchive << _staminaCore;
        _bond.writeState( outArchive );
        outArchive << _bondXpCarry;
        outArchive << _fear;
        outArchive << _hoursSinceBrush;
        outArchive << _bRidden;
        outArchive << _bGalloping;
    }

    bool WesternHorse::readState( Archive& archive )
    {
        hashed_string horseId;
        if ( _pCatalog == nullptr || StateArchiveUtil::readName( archive, horseId ) == false )
            return false;
        // 사본에 읽고 끝까지 맞으면 바꾼다. 품종이 다르면 사본을 그 품종으로 열어 게이지 설정을 맞춘다(값은 아래에서 덮는다).
        WesternHorse restored = *this;
        const bool   bSameDef = _pDef != nullptr && _pDef->_id == horseId;
        if ( bSameDef == false && restored.initialize( _pCatalog, horseId, 1 ) == false )
            return false;
        uint32 abilityCount = 0;
        if ( StateArchiveUtil::readCount( archive, 4, abilityCount ) == false )
            return false;
        restored._listAbility.resize( abilityCount );
        for ( hashed_string& ability : restored._listAbility )
        {
            if ( StateArchiveUtil::readName( archive, ability ) == false )
                return false;
        }
        const bool bGaugesRead = restored._health.readState( archive ) && restored._stamina.readState( archive ) &&
                                 StateArchiveUtil::readRandom( archive, restored._random );
        if ( bGaugesRead == false )
            return false;
        archive >> restored._healthCore;
        archive >> restored._staminaCore;
        if ( restored._bond.readState( archive ) == false )
            return false;
        archive >> restored._bondXpCarry;
        archive >> restored._fear;
        archive >> restored._hoursSinceBrush;
        archive >> restored._bRidden;
        archive >> restored._bGalloping;
        const bool bValid = archive.isOk() && restored._bRidden <= SW_TRUE && restored._bGalloping <= SW_TRUE && 0.0f <= restored._fear && 0.0f <= restored._bondXpCarry &&
                            restored._bondXpCarry < 1.0f && restored._bond.getLevel() <= MathUtil::max( 1, _pCatalog->getBondCurve().getMaxLevel() );
        if ( bValid == false )
            return false;
        restored._eventBuffer.clear();
        *this = std::move( restored );
        return true;
    }

    int32 WesternHorse::getBondLevel() const
    {
        const int32 progressLevel = _bond.getLevel();
        if ( _pCatalog == nullptr || _pCatalog->getBondLevels().empty() )
            return progressLevel;
        const vector<WesternBondLevelDef>& listBond = _pCatalog->getBondLevels();
        const int32                        index    = MathUtil::clamp( progressLevel - 1, 0, static_cast<int32>( listBond.size() ) - 1 );
        return listBond[static_cast<size_t>( index )]._level;
    }

    void WesternHorse::applyBondLevel( int32 level )
    {
        float32 staminaBonus = 0.0f;
        float32 healthBonus  = 0.0f;
        for ( const WesternBondLevelDef& bond : _pCatalog->getBondLevels() )
        {
            if ( bond._level > level )
                break;
            staminaBonus = bond._staminaBonus;
            healthBonus  = bond._healthBonus;
            for ( const hashed_string& unlock : bond._listUnlock )
            {
                if ( hasAbility( unlock ) )
                    continue;
                _listAbility.push_back( unlock );
                WesternHorseEvent event;
                event._kind    = WesternHorseEvent::Kind::AbilityUnlocked;
                event._ability = unlock;
                event._value   = bond._level;
                _eventBuffer.push( event );
            }
        }
        _stamina.setMaxBonus( staminaBonus );
        _health.setMaxBonus( healthBonus );
        WesternHorseEvent event;
        event._kind  = WesternHorseEvent::Kind::BondLevelUp;
        event._value = level;
        _eventBuffer.push( event );
    }

    float32 WesternHorse::computeFearResist() const
    {
        float32 resist = _pDef != nullptr ? _pDef->_courage : 0.0f;
        if ( _pCatalog == nullptr )
            return resist;
        const int32 bondLevel = getBondLevel();
        for ( const WesternBondLevelDef& bond : _pCatalog->getBondLevels() )
        {
            if ( bond._level <= bondLevel )
                resist = ( _pDef != nullptr ? _pDef->_courage : 0.0f ) + bond._fearResist;
        }
        return resist;
    }
} // namespace sw
