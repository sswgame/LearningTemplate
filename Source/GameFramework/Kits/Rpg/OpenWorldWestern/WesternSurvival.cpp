#include "pch.h"

#include "GameFramework/Kits/Rpg/OpenWorldWestern/WesternSurvival.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/World/WeatherSystem.h"
#include "GameFramework/Kits/Rpg/OpenWorldWestern/WesternCatalog.h"

namespace sw
{
    namespace
    {
        /** @brief 틱마다 쓰는 이름 — 리터럴을 매번 intern 하지 않게 한 번만 만든다. */
        struct WesternSurvivalInternal
        {
            static const hashed_string& getTemperatureName()
            {
                static const hashed_string name( "temperature" );
                return name;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    WesternSurvival::WesternSurvival()
        : _listClothing{}
        , _listMark{}
        , _arrGauge{}
        , _arrCore{}
        , _pCatalog{ nullptr }
        , _deadEyeLevel{ 1 }
        , _bDeadEyeActive{ SW_FALSE }
    {
        for ( float32& core : _arrCore )
            core = kCoreMax;
    }

    void WesternSurvival::initialize( const WesternCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        const WesternSurvivalSettings  fallback{};
        const WesternSurvivalSettings& survival = pCatalog != nullptr ? pCatalog->getSurvival() : fallback;
        ResourceGaugeSettings          health;
        health._max        = survival._health;
        health._regenRate  = survival._healthRegen;
        health._regenDelay = 4.0f;
        ResourceGaugeSettings stamina;
        stamina._max              = survival._stamina;
        stamina._regenRate        = survival._staminaRegen;
        stamina._regenDelay       = 1.0f;
        stamina._exhaustThreshold = survival._stamina * 0.2f;
        ResourceGaugeSettings deadEye;
        deadEye._max            = survival._deadEye;
        deadEye._regenRate      = survival._deadEyeRegen;
        deadEye._regenDelay     = 2.0f;
        deadEye._drainPerSecond = survival._deadEyeDrain;
        _arrGauge[static_cast<size_t>( WesternCore::Health )].initialize( health );
        _arrGauge[static_cast<size_t>( WesternCore::Stamina )].initialize( stamina );
        _arrGauge[static_cast<size_t>( WesternCore::DeadEye )].initialize( deadEye );
        for ( float32& core : _arrCore )
            core = kCoreMax;
        _listClothing.clear();
        _listMark.clear();
        _deadEyeLevel   = 1;
        _bDeadEyeActive = SW_FALSE;
    }

    void WesternSurvival::update( float32 deltaTime, float32 gameHours, float32 airTemperature )
    {
        if ( _pCatalog == nullptr )
            return;
        const WesternSurvivalSettings& survival = _pCatalog->getSurvival();
        if ( gameHours > 0.0f )
        {
            const float32 felt    = computeFeltTemperature( airTemperature );
            const float32 cold    = MathUtil::max( 0.0f, survival._comfortMin - felt );
            const float32 heat    = MathUtil::max( 0.0f, felt - survival._comfortMax );
            const float32 base    = survival._coreDrainPerHour * gameHours;
            float32&      health  = _arrCore[static_cast<size_t>( WesternCore::Health )];
            float32&      stamina = _arrCore[static_cast<size_t>( WesternCore::Stamina )];
            float32&      deadEye = _arrCore[static_cast<size_t>( WesternCore::DeadEye )];
            health                = MathUtil::max( 0.0f, health - base - cold * survival._temperatureDrain * gameHours );
            stamina               = MathUtil::max( 0.0f, stamina - base - heat * survival._temperatureDrain * gameHours );
            deadEye               = MathUtil::max( 0.0f, deadEye - base );
        }
        if ( deltaTime <= 0.0f )
            return;
        ResourceGauge& deadEyeGauge = getGauge( WesternCore::DeadEye );
        if ( _bDeadEyeActive != SW_FALSE && deadEyeGauge.drain( deltaTime ) == false )
            deactivateDeadEye(); // 바닥났다 — 표시한 대상은 게임이 이미 쐈거나 놓친다
        for ( size_t index = 0; index < kCoreCount; ++index )
        {
            if ( index == static_cast<size_t>( WesternCore::DeadEye ) && _bDeadEyeActive != SW_FALSE )
                continue; // 켜 둔 동안은 차지 않는다
            _arrGauge[index].setRegenScale( computeRegenScale( static_cast<WesternCore>( index ) ) );
            _arrGauge[index].update( deltaTime );
        }
    }

    float32 WesternSurvival::computeAirTemperature( const WeatherSystem& weather, float32 regionBaseTemperature )
    {
        return regionBaseTemperature + weather.computeValue( WesternSurvivalInternal::getTemperatureName() );
    }

    bool WesternSurvival::eat( const hashed_string& foodId )
    {
        const WesternFoodDef* pFood = _pCatalog != nullptr ? _pCatalog->findFood( foodId ) : nullptr;
        if ( pFood == nullptr )
            return false;
        setCore( WesternCore::Health, getCore( WesternCore::Health ) + pFood->_healthCore );
        setCore( WesternCore::Stamina, getCore( WesternCore::Stamina ) + pFood->_staminaCore );
        setCore( WesternCore::DeadEye, getCore( WesternCore::DeadEye ) + pFood->_deadEyeCore );
        getGauge( WesternCore::Health ).restore( pFood->_health );
        getGauge( WesternCore::Stamina ).restore( pFood->_stamina );
        return true;
    }

    void WesternSurvival::setClothing( const vector<hashed_string>& listClothing ) { _listClothing = listClothing; }

    float32 WesternSurvival::computeWarmth() const
    {
        float32 warmth = 0.0f;
        if ( _pCatalog == nullptr )
            return warmth;
        for ( const hashed_string& clothingId : _listClothing )
        {
            const WesternClothingDef* pClothing = _pCatalog->findClothing( clothingId );
            warmth += pClothing != nullptr ? pClothing->_warmth : 0.0f;
        }
        return warmth;
    }

    bool WesternSurvival::activateDeadEye()
    {
        if ( _bDeadEyeActive != SW_FALSE || _pCatalog == nullptr )
            return false;
        const ResourceGauge& gauge = getGauge( WesternCore::DeadEye );
        if ( gauge.canUse() == false || gauge.getValue() < _pCatalog->getSurvival()._deadEyeMinimum )
            return false;
        _bDeadEyeActive = SW_TRUE;
        _listMark.clear();
        return true;
    }

    bool WesternSurvival::markTarget( uint64 targetId )
    {
        if ( _bDeadEyeActive == SW_FALSE || static_cast<int32>( _listMark.size() ) >= getMarkLimit() )
            return false;
        _listMark.push_back( targetId ); // 같은 대상에 여러 번 표시해도 된다(한 발씩)
        return true;
    }

    float32 WesternSurvival::getTimeScale() const
    {
        if ( _bDeadEyeActive == SW_FALSE || _pCatalog == nullptr )
            return 1.0f;
        const WesternDeadEyeLevelDef* pLevel = _pCatalog->findDeadEyeLevel( _deadEyeLevel );
        return pLevel != nullptr ? pLevel->_timeScale : 1.0f;
    }

    int32 WesternSurvival::getMarkLimit() const
    {
        const WesternDeadEyeLevelDef* pLevel = _pCatalog != nullptr ? _pCatalog->findDeadEyeLevel( _deadEyeLevel ) : nullptr;
        return pLevel != nullptr ? pLevel->_markCount : 0;
    }

    void WesternSurvival::setCore( WesternCore core, float32 value ) { _arrCore[static_cast<size_t>( core )] = MathUtil::clamp( value, 0.0f, kCoreMax ); }

    float32 WesternSurvival::computeRegenScale( WesternCore core ) const
    {
        const float32 minScale = _pCatalog != nullptr ? _pCatalog->getSurvival()._minRegenScale : 0.2f;
        return MathUtil::lerp( minScale, 1.0f, MathUtil::saturate( getCore( core ) / kCoreMax ) );
    }
} // namespace sw
