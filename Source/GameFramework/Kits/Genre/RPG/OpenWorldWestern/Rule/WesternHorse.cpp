#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Rule/WesternHorse.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Catalog/WesternCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"

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
        , _bondXPCarry{ 0.0f }
        , _fear{ 0.0f }
        , _hoursSinceBrush{ 0.0f }
        , _bRidden{ SW_FALSE }
        , _bGalloping{ SW_FALSE }
    {
    }

    bool WesternHorse::initialize( const WesternCatalog* pCatalog, const hashed_string& horseID, uint32 seed )
    {
        _pCatalog = pCatalog;
        _pDef     = pCatalog != nullptr ? pCatalog->findHorse( horseID ) : nullptr;
        if ( _pDef == nullptr )
            return false;
        ResourceGaugeSettings healthSettings;
        healthSettings._max        = _pDef->_health;
        healthSettings._regenRate  = _pDef->_health * 0.02f;
        healthSettings._regenDelay = 3.0f;
        ResourceGaugeSettings staminaSettings;
        staminaSettings._max              = _pDef->_stamina;
        staminaSettings._regenRate        = _pDef->_stamina * 0.1f;
        staminaSettings._regenDelay       = 1.0f;
        staminaSettings._drainPerSecond   = _pDef->_gallopDrain;
        staminaSettings._exhaustThreshold = _pDef->_stamina * 0.25f; // 바닥나면 4분의 1까지 쉬어야 다시 달릴 수 있다
        if ( _health.initialize( healthSettings ) == false || _stamina.initialize( staminaSettings ) == false )
        {
            _pDef = nullptr; // 게이지 설정이 거절된 말은 서지 않는다(update 가 _pDef 없음으로 멈춘다)
            return false;
        }
        _random.setSeed( seed );
        _eventBuffer.clear();
        _listAbility.clear();
        _healthCore      = kCoreMax;
        _staminaCore     = kCoreMax;
        _bond            = LevelProgress{};
        _bondXPCarry     = 0.0f;
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
            DepletingValue::take( _healthCore, drain );
            DepletingValue::take( _staminaCore, drain );
            _hoursSinceBrush += gameHours;
        }
        _bGalloping = SW_FALSE;
    }

    bool WesternHorse::gallop( float32 deltaTime )
    {
        if ( _pDef == nullptr )
            return false;
        const bool bWasLocked = _stamina.isExhausted();
        const bool bGalloping = _stamina.drain( deltaTime );
        _bGalloping           = bWasLocked ? SW_FALSE : SW_TRUE; // 탈진해 달리지 못한 걸음은 질주가 아니다(코어가 더 줄지 않는다)
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

    bool WesternHorse::feed( const hashed_string& foodID )
    {
        const WesternFoodDef* pFood = _pCatalog != nullptr ? _pCatalog->findFood( foodID ) : nullptr;
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
        // 탄 사람이 있을 때만 떨어뜨릴 수 있다. 난수는 항상 하나 쓴다(탔는지와 상관없이 같은 수열이 나온다).
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
        // 기반 레벨 진행은 정수 경험치를 쓴다. 소수(초당 타기)는 1이 될 때까지 따로 보관한다.
        const float32 total = _bondXPCarry + amount;
        const int64   whole = static_cast<int64>( MathUtil::floor( total ) );
        _bondXPCarry        = total - static_cast<float32>( whole );
        if ( whole > 0 && _bond.addXP( _pCatalog->getBondCurve(), whole ) > 0 )
            applyBondLevel( getBondLevel() );
    }

    bool WesternHorse::hasAbility( const hashed_string& abilityID ) const
    {
        return VectorUtil::contains( _listAbility, abilityID );
    }

    float32 WesternHorse::computeRegenScale( float32 core ) const
    {
        const float32 minScale = _pCatalog != nullptr ? _pCatalog->getSurvival()._minRegenScale : 0.2f;
        return MathUtil::lerp( minScale, 1.0f, MathUtil::saturate( core / kCoreMax ) );
    }

    void WesternHorse::pollEvents( vector<WesternHorseEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    void WesternHorse::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        hashed_string horseID = _pDef != nullptr ? _pDef->_id : hashed_string{};
        io.value( horseID );
        if ( io.isOk() == false )
            return;
        // 품종이 다르면 그 품종으로 열어 게이지 설정을 맞춘다(값은 아래에서 덮어쓴다).
        if ( io.isReading() && ( _pDef == nullptr || _pDef->_id != horseID ) && io.check( initialize( _pCatalog, horseID, 1 ) ) == false )
            return;
        io.list( _listAbility );
        io.value( _health );
        io.value( _stamina );
        io.value( _random );
        io.value( _healthCore );
        io.value( _staminaCore );
        io.value( _bond );
        io.value( _bondXPCarry );
        io.value( _fear );
        io.value( _hoursSinceBrush );
        io.flag( _bRidden );
        io.flag( _bGalloping );
        if ( io.isWriting() || io.isOk() == false )
            return;
        // 비교는 NaN 에서 거짓이라 범위를 쓴 쪽이 NaN 도 거른다. 코어가 NaN 이면 회복 배율과 코어 감소가 영영 NaN 으로 남는다.
        io.check( 0.0f <= _healthCore && _healthCore <= kCoreMax && 0.0f <= _staminaCore && _staminaCore <= kCoreMax );
        io.check( 0.0f <= _fear && 0.0f <= _hoursSinceBrush && 0.0f <= _bondXPCarry && _bondXPCarry < 1.0f && _bond.getLevel() <= MathUtil::max( 1, _pCatalog->getBondCurve().getMaxLevel() ) );
        io.check( areAbilitiesUnlocked() );
        _eventBuffer.clear();
    }

    bool WesternHorse::areAbilitiesUnlocked() const
    {
        if ( VectorUtil::findDuplicate( _listAbility ) != nullptr )
            return false;
        const int32 bondLevel = getBondLevel();
        for ( const hashed_string& ability : _listAbility )
        {
            bool bUnlocked = false;
            for ( const WesternBondLevelDef& bond : _pCatalog->getBondLevels() )
            {
                bUnlocked = bUnlocked || ( bond._level <= bondLevel && VectorUtil::contains( bond._listUnlock, ability ) );
            }
            if ( bUnlocked == false )
                return false;
        }
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
