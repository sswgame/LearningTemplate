#include "pch.h"

#include "GameFramework/Kits/Rpg/OpenWorldWestern/WesternHorse.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Kits/Rpg/OpenWorldWestern/WesternCatalog.h"

namespace sw
{
    namespace
    {
        struct WesternHorseInternal
        {
            static constexpr float32 kFearDecayPerSecond = 0.25f; ///< 쌓인 겁이 식는 빠르기
            static constexpr float32 kBuckChanceScale    = 0.8f;  ///< 저항이 0 일 때 떨어뜨릴 확률
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
        , _bondExperience{ 0.0f }
        , _fear{ 0.0f }
        , _hoursSinceBrush{ 0.0f }
        , _bondLevel{ 1 }
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
        _healthCore                                 = kCoreMax;
        _staminaCore                                = kCoreMax;
        _bondExperience                             = 0.0f;
        _fear                                       = 0.0f;
        _hoursSinceBrush                            = pCatalog->getBondExperience()._brushCooldownHours;
        _bondLevel                                  = 0;
        _bRidden                                    = SW_FALSE;
        _bGalloping                                 = SW_FALSE;
        const vector<WesternBondLevelDef>& listBond = pCatalog->getBondLevels();
        applyBondLevel( listBond.empty() ? 1 : listBond.front()._level );
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
            _fear = MathUtil::max( 0.0f, _fear - WesternHorseInternal::kFearDecayPerSecond * deltaTime );
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
        const bool bBuck = _random.nextChance( ( 1.0f - resist ) * WesternHorseInternal::kBuckChanceScale ) && _bRidden != SW_FALSE;
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
        _bondExperience += amount;
        int32 level = _bondLevel;
        for ( const WesternBondLevelDef& bond : _pCatalog->getBondLevels() )
        {
            if ( bond._experience <= _bondExperience )
                level = MathUtil::max( level, bond._level );
        }
        if ( level != _bondLevel )
            applyBondLevel( level );
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

    void WesternHorse::applyBondLevel( int32 level )
    {
        _bondLevel           = level;
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
        for ( const WesternBondLevelDef& bond : _pCatalog->getBondLevels() )
        {
            if ( bond._level <= _bondLevel )
                resist = ( _pDef != nullptr ? _pDef->_courage : 0.0f ) + bond._fearResist;
        }
        return resist;
    }
} // namespace sw
