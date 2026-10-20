#include "pch.h"

#include "GameFramework/Base/World/Environment/WeatherSystem.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    SW_LOG_CALLER( "WeatherSystem" );

    float32 WeatherDef::computeWeight( const hashed_string& season ) const
    {
        if ( _listSeasonWeight.empty() )
            return _weight;
        for ( const WeatherSeasonWeight& seasonWeight : _listSeasonWeight )
        {
            if ( seasonWeight._season == season )
                return seasonWeight._weight;
        }
        return 0.0f;
    }

    uint32 WeatherCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 적지 않은 칸은 초기값이다. 깨진 값 · 범위 밖 값은 오류다(자르지 않는다). 날씨는 그 원소를, 전환 시간은 표 전체를 넣지 않는다.
        constexpr float32 kAnyFloat = MathUtil::kMaxFloat;
        if ( GameDataXML::readOptionalNumber( root, "transition", sourceName, 0.0f, kAnyFloat, _transitionTime ) == false )
            return 0;
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild( "Weather" ); node; node = node.findNextSibling( "Weather" ) )
        {
            const utf8* pID = GameDataXML::findRequiredID( node, sourceName );
            if ( pID == nullptr )
                continue;
            WeatherDef weather;
            weather._id = hashed_string( pID );
            bool bValid = GameDataXML::readOptionalNumber( node, "weight", sourceName, 0.0f, kAnyFloat, weather._weight );
            bValid      = GameDataXML::readOptionalNumber( node, "minDuration", sourceName, WeatherDef::kMinDuration, kAnyFloat, weather._minDuration ) && bValid;
            bValid      = GameDataXML::readOptionalNumber( node, "maxDuration", sourceName, weather._minDuration, kAnyFloat, weather._maxDuration ) && bValid;
            // "Spring:3,Summer:1" — 계절:가중치(가중치를 빼면 그 날씨의 `weight`). 읽히지 않거나 음수인 가중치는 오류다.
            GameDataXML::forEachNamedNumber( node.getAttributeText( "seasons" ), ", ", weather._weight, GameDataXML::MissingValue::UseDefault,
                                             [&]( string_view token, string_view seasonName, float32 weight, bool bNumberValid )
            {
                if ( bNumberValid == false || weight < 0.0f )
                {
                    SW_LOG_ERROR( "%#: weather '%#' has an invalid season weight '%#'", sourceName, pID, token );
                    bValid = false;
                    return;
                }
                WeatherSeasonWeight seasonWeight;
                seasonWeight._season = hashed_string( seasonName );
                seasonWeight._weight = weight;
                weather._listSeasonWeight.push_back( seasonWeight );
            } );
            if ( bValid == false )
            {
                SW_LOG_ERROR( "%#: weather '%#' has invalid fields - skipped", sourceName, pID );
                continue;
            }
            const XMLNode valueNode = node.findChild( "Values" );
            if ( valueNode )
                (void)weather._values.loadFromAttributes( valueNode ); // 읽은 속성 수만 돌려준다 — 없으면 빈 값이다
            (void)add( weather );
            ++loadedCount;
        }
        return loadedCount;
    }

    WeatherSystem::WeatherSystem()
        : _pCatalog{ nullptr }
        , _pCurrent{ nullptr }
        , _pPrevious{ nullptr }
        , _random{}
        , _remaining{ 0.0f }
        , _transitionElapsed{ 0.0f }
    {
    }

    void WeatherSystem::initialize( const WeatherCatalog* pCatalog, uint32 seed, const hashed_string& season )
    {
        _pCatalog = pCatalog;
        _random.setSeed( seed );
        _pPrevious         = nullptr;
        _pCurrent          = pickWeather( season, _random, nullptr );
        _remaining         = _pCurrent != nullptr ? pickDuration( *_pCurrent, _random ) : 0.0f;
        _transitionElapsed = _pCatalog != nullptr ? _pCatalog->getTransitionTime() : 0.0f;
    }

    const WeatherDef* WeatherSystem::pickWeather( const hashed_string& season, GameRandom& random, const WeatherDef* pExclude ) const
    {
        if ( _pCatalog == nullptr )
            return nullptr;
        float32 total = 0.0f;
        for ( const WeatherDef& weather : _pCatalog->getAll() )
        {
            total += &weather == pExclude ? 0.0f : weather.computeWeight( season );
        }
        if ( total <= 0.0f )
            return pExclude; // 다른 것이 없다 — 그대로 이어간다
        float32 pick = random.nextFloat() * total;
        for ( const WeatherDef& weather : _pCatalog->getAll() )
        {
            const float32 weight = &weather == pExclude ? 0.0f : weather.computeWeight( season );
            if ( pick < weight )
                return &weather;
            pick -= weight;
        }
        return pExclude;
    }

    float32 WeatherSystem::pickDuration( const WeatherDef& weather, GameRandom& random ) const
    {
        return random.nextRange( weather._minDuration, weather._maxDuration + 1.0e-3f );
    }

    bool WeatherSystem::update( float32 gameSeconds, const hashed_string& season )
    {
        if ( _pCurrent == nullptr || gameSeconds <= 0.0f )
            return false;
        _transitionElapsed += gameSeconds;
        _remaining -= gameSeconds;
        if ( _remaining > 0.0f )
            return false;
        const WeatherDef* pNext    = pickWeather( season, _random, _pCurrent );
        const bool        bChanged = pNext != _pCurrent;
        if ( bChanged )
        {
            _pPrevious         = _pCurrent;
            _pCurrent          = pNext;
            _transitionElapsed = 0.0f;
        }
        _remaining = pickDuration( *_pCurrent, _random );
        return bChanged;
    }

    void WeatherSystem::forceWeather( const hashed_string& weatherID, float32 duration, bool bImmediate )
    {
        const WeatherDef* pWeather = _pCatalog != nullptr ? _pCatalog->find( weatherID ) : nullptr;
        if ( pWeather == nullptr )
            return;
        if ( pWeather != _pCurrent )
        {
            _pPrevious         = _pCurrent;
            _pCurrent          = pWeather;
            _transitionElapsed = bImmediate ? _pCatalog->getTransitionTime() : 0.0f;
        }
        _remaining = duration > 0.0f ? duration : pickDuration( *pWeather, _random );
    }

    void WeatherSystem::forecast( const hashed_string& season, int32 count, vector<hashed_string>& outListWeather ) const
    {
        outListWeather.clear();
        GameRandom        random   = _random; // 복사 — 진짜 흐름과 같은 수를 낸다
        const WeatherDef* pCurrent = _pCurrent;
        for ( int32 index = 0; index < count && pCurrent != nullptr; ++index )
        {
            pCurrent = pickWeather( season, random, pCurrent );
            (void)pickDuration( *pCurrent, random );
            outListWeather.push_back( pCurrent->_id );
        }
    }

    float32 WeatherSystem::getBlend() const
    {
        const float32 transition = _pCatalog != nullptr ? _pCatalog->getTransitionTime() : 0.0f;
        if ( _pPrevious == nullptr || transition <= 0.0f )
            return 1.0f;
        return MathUtil::saturate( _transitionElapsed / transition );
    }

    float32 WeatherSystem::computeValue( const hashed_string& name ) const
    {
        const float32 current = _pCurrent != nullptr ? _pCurrent->_values.getValue( name ) : 0.0f;
        if ( _pPrevious == nullptr )
            return current;
        const float32 blend    = getBlend();
        const float32 previous = _pPrevious->_values.getValue( name );
        return previous + ( current - previous ) * blend;
    }

    void WeatherSystem::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeName( outArchive, getCurrent() );
        StateArchiveUtil::writeName( outArchive, getPrevious() );
        StateArchiveUtil::writeRandom( outArchive, _random );
        outArchive << _remaining;
        outArchive << _transitionElapsed;
    }

    bool WeatherSystem::readState( Archive& archive )
    {
        hashed_string currentID;
        hashed_string previousID;
        GameRandom    random            = _random;
        float32       remaining         = 0.0f;
        float32       transitionElapsed = 0.0f;
        const bool    bRead             = StateArchiveUtil::readName( archive, currentID ) && StateArchiveUtil::readName( archive, previousID ) && StateArchiveUtil::readRandom( archive, random );
        archive >> remaining;
        archive >> transitionElapsed;
        if ( bRead == false || archive.isError() )
            return false;
        const WeatherDef* pCurrent  = currentID.empty() || _pCatalog == nullptr ? nullptr : _pCatalog->find( currentID );
        const WeatherDef* pPrevious = previousID.empty() || _pCatalog == nullptr ? nullptr : _pCatalog->find( previousID );
        const bool        bKnown    = ( currentID.empty() || pCurrent != nullptr ) && ( previousID.empty() || pPrevious != nullptr );
        if ( bKnown == false )
            return false;
        _pCurrent          = pCurrent;
        _pPrevious         = pPrevious;
        _random            = random;
        _remaining         = remaining;
        _transitionElapsed = transitionElapsed;
        return true;
    }
} // namespace sw
