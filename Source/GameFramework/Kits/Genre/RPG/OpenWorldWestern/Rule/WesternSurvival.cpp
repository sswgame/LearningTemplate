#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Rule/WesternSurvival.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/World/Environment/WeatherSystem.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Catalog/WesternCatalog.h"

namespace sw
{
    namespace
    {
        /** @brief 틱마다 쓰는 이름입니다. 리터럴을 매번 intern 하지 않도록 한 번만 만듭니다. */
        const hashed_string& getTemperatureName()
        {
            static const hashed_string name( "temperature" );
            return name;
        }
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
        {
            core = kCoreMax;
        }
    }

    bool WesternSurvival::initialize( const WesternCatalog* pCatalog )
    {
        if ( pCatalog == nullptr )
        {
            SW_LOG_ERROR( "WesternSurvival: refused - no catalog (the survival numbers come from the catalog, there is no default set)" );
            return false;
        }
        const WesternSurvivalDef& survival = pCatalog->getSurvival();
        ResourceGaugeSettings     healthSettings;
        healthSettings._max        = survival._health;
        healthSettings._regenRate  = survival._healthRegen;
        healthSettings._regenDelay = 4.0f;
        ResourceGaugeSettings staminaSettings;
        staminaSettings._max              = survival._stamina;
        staminaSettings._regenRate        = survival._staminaRegen;
        staminaSettings._regenDelay       = 1.0f;
        staminaSettings._exhaustThreshold = survival._stamina * 0.2f;
        ResourceGaugeSettings deadEyeSettings;
        deadEyeSettings._max            = survival._deadEye;
        deadEyeSettings._regenRate      = survival._deadEyeRegen;
        deadEyeSettings._regenDelay     = 2.0f;
        deadEyeSettings._drainPerSecond = survival._deadEyeDrain;
        // 따로 만들어 셋 다 받아들여질 때만 바꾼다 — 하나라도 거절되면 상태는 그대로다.
        ResourceGauge health;
        ResourceGauge stamina;
        ResourceGauge deadEye;
        const bool    bHealthAccepted  = health.initialize( healthSettings );
        const bool    bStaminaAccepted = stamina.initialize( staminaSettings );
        const bool    bDeadEyeAccepted = deadEye.initialize( deadEyeSettings );
        const bool    bAllAccepted     = bHealthAccepted && bStaminaAccepted && bDeadEyeAccepted;
        if ( bAllAccepted == false )
            return false;
        _pCatalog                                              = pCatalog;
        _arrGauge[static_cast<size_t>( WesternCore::Health )]  = health;
        _arrGauge[static_cast<size_t>( WesternCore::Stamina )] = stamina;
        _arrGauge[static_cast<size_t>( WesternCore::DeadEye )] = deadEye;
        for ( float32& core : _arrCore )
        {
            core = kCoreMax;
        }
        _listClothing.clear();
        _listMark.clear();
        _deadEyeLevel   = 1;
        _bDeadEyeActive = SW_FALSE;
        return true;
    }

    void WesternSurvival::update( float32 deltaTime, float32 gameHours, float32 airTemperature )
    {
        if ( _pCatalog == nullptr )
            return;
        const WesternSurvivalDef& survival = _pCatalog->getSurvival();
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
            deactivateDeadEye(); // 바닥났다. 표시한 대상은 게임이 이미 쐈거나 놓친 것이다
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
        return regionBaseTemperature + weather.computeValue( getTemperatureName() );
    }

    bool WesternSurvival::eat( const hashed_string& foodID )
    {
        const WesternFoodDef* pFood = _pCatalog != nullptr ? _pCatalog->findFood( foodID ) : nullptr;
        if ( pFood == nullptr )
            return false;
        setCore( WesternCore::Health, getCore( WesternCore::Health ) + pFood->_healthCore );
        setCore( WesternCore::Stamina, getCore( WesternCore::Stamina ) + pFood->_staminaCore );
        setCore( WesternCore::DeadEye, getCore( WesternCore::DeadEye ) + pFood->_deadEyeCore );
        getGauge( WesternCore::Health ).restore( pFood->_health );
        getGauge( WesternCore::Stamina ).restore( pFood->_stamina );
        return true;
    }

    void WesternSurvival::setClothing( const vector<hashed_string>& listClothing )
    {
        _listClothing = listClothing;
    }

    float32 WesternSurvival::computeWarmth() const
    {
        float32 warmth = 0.0f;
        if ( _pCatalog == nullptr )
            return warmth;
        for ( const hashed_string& clothingID : _listClothing )
        {
            const WesternClothingDef* pClothing = _pCatalog->findClothing( clothingID );
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

    bool WesternSurvival::markTarget( uint64 targetID )
    {
        if ( _bDeadEyeActive == SW_FALSE || static_cast<int32>( _listMark.size() ) >= getMarkLimit() )
            return false;
        _listMark.push_back( targetID ); // 같은 대상에 여러 번 표시해도 된다(한 발씩 쏜다)
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

    void WesternSurvival::setCore( WesternCore core, float32 value )
    {
        _arrCore[static_cast<size_t>( core )] = MathUtil::clamp( value, 0.0f, kCoreMax );
    }

    float32 WesternSurvival::computeRegenScale( WesternCore core ) const
    {
        const float32 minScale = _pCatalog != nullptr ? _pCatalog->getSurvival()._minRegenScale : 0.2f;
        return MathUtil::lerp( minScale, 1.0f, MathUtil::saturate( getCore( core ) / kCoreMax ) );
    }

    void WesternSurvival::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listClothing.size() );
        for ( const hashed_string& clothingID : _listClothing )
        {
            StateArchiveUtil::writeName( outArchive, clothingID );
        }
        outArchive << static_cast<uint32>( _listMark.size() );
        for ( const uint64 targetID : _listMark )
        {
            outArchive << targetID;
        }
        for ( size_t coreIndex = 0; coreIndex < kCoreCount; ++coreIndex )
        {
            _arrGauge[coreIndex].writeState( outArchive );
            outArchive << _arrCore[coreIndex];
        }
        outArchive << _deadEyeLevel;
        outArchive << _bDeadEyeActive;
    }

    bool WesternSurvival::readState( Archive& archive )
    {
        // 사본에 읽고 끝까지 맞으면 바꾼다. 게이지 설정과 카탈로그는 사본이 그대로 가지고 있다.
        WesternSurvival restored = *this;
        uint32          count    = 0;
        if ( StateArchiveUtil::readCount( archive, 4, count ) == false )
            return false;
        restored._listClothing.resize( count );
        for ( hashed_string& clothingID : restored._listClothing )
        {
            if ( StateArchiveUtil::readName( archive, clothingID ) == false )
                return false;
        }
        if ( StateArchiveUtil::readCount( archive, 8, count ) == false )
            return false;
        restored._listMark.resize( count );
        for ( uint64& targetID : restored._listMark )
        {
            archive >> targetID;
        }
        for ( size_t coreIndex = 0; coreIndex < kCoreCount; ++coreIndex )
        {
            if ( restored._arrGauge[coreIndex].readState( archive ) == false )
                return false;
            archive >> restored._arrCore[coreIndex];
            const bool bCoreValid = 0.0f <= restored._arrCore[coreIndex] && restored._arrCore[coreIndex] <= kCoreMax;
            if ( bCoreValid == false )
                return false;
        }
        archive >> restored._deadEyeLevel;
        archive >> restored._bDeadEyeActive;
        if ( archive.isError() || restored._bDeadEyeActive > SW_TRUE )
            return false;
        *this = std::move( restored );
        return true;
    }
} // namespace sw
