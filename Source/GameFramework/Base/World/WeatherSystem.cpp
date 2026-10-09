#include "pch.h"

#include "GameFramework/Base/World/WeatherSystem.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Data/GameDataXml.h"
#include "GameFramework/Base/Utility/StateArchiveUtil.h"

namespace sw
{
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

    uint32 WeatherCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        _transitionTime    = MathUtil::max( 0.0f, root.getAttributeFloat( "transition", _transitionTime ) );
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Weather" ); node; node = node.findNextSibling( "Weather" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            WeatherDef weather;
            weather._id          = hashed_string( pId );
            weather._weight      = MathUtil::max( 0.0f, node.getAttributeFloat( "weight", weather._weight ) );
            weather._minDuration = MathUtil::max( 1.0f, node.getAttributeFloat( "minDuration", weather._minDuration ) );
            weather._maxDuration = MathUtil::max( weather._minDuration, node.getAttributeFloat( "maxDuration", weather._maxDuration ) );
            // "Spring:3,Summer:1" — 계절:가중치(가중치를 빼면 `weight`).
            GameDataXml::forEachToken( node.getAttributeText( "seasons" ), ", ", [&]( string_view token )
            {
                WeatherSeasonWeight seasonWeight;
                const size_t        colon = token.find( ':' );
                seasonWeight._season      = hashed_string( colon == string_view::npos ? token : token.substr( 0, colon ) );
                seasonWeight._weight      = weather._weight;
                float32 value             = 0.0f;
                if ( colon != string_view::npos && StringUtil::parseFloat( token.substr( colon + 1 ), value ) )
                    seasonWeight._weight = MathUtil::max( 0.0f, value );
                weather._listSeasonWeight.push_back( seasonWeight );
            } );
            const XmlNode valueNode = node.findChild( "Values" );
            if ( valueNode )
                (void)weather._values.loadFromAttributes( valueNode ); // 읽은 속성 수만 돌려준다 — 없으면 빈 값이다
            (void)_catalog.add( weather );
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
        for ( const WeatherDef& weather : _pCatalog->getWeathers() )
        {
            total += &weather == pExclude ? 0.0f : weather.computeWeight( season );
        }
        if ( total <= 0.0f )
            return pExclude; // 다른 것이 없다 — 그대로 이어간다
        float32 pick = random.nextFloat() * total;
        for ( const WeatherDef& weather : _pCatalog->getWeathers() )
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

    void WeatherSystem::forceWeather( const hashed_string& weatherId, float32 duration, bool bImmediate )
    {
        const WeatherDef* pWeather = _pCatalog != nullptr ? _pCatalog->findWeather( weatherId ) : nullptr;
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
        hashed_string currentId;
        hashed_string previousId;
        GameRandom    random            = _random;
        float32       remaining         = 0.0f;
        float32       transitionElapsed = 0.0f;
        const bool    bRead             = StateArchiveUtil::readName( archive, currentId ) && StateArchiveUtil::readName( archive, previousId ) && StateArchiveUtil::readRandom( archive, random );
        archive >> remaining;
        archive >> transitionElapsed;
        if ( bRead == false || archive.isError() )
            return false;
        const WeatherDef* pCurrent  = currentId.empty() || _pCatalog == nullptr ? nullptr : _pCatalog->findWeather( currentId );
        const WeatherDef* pPrevious = previousId.empty() || _pCatalog == nullptr ? nullptr : _pCatalog->findWeather( previousId );
        const bool        bKnown    = ( currentId.empty() || pCurrent != nullptr ) && ( previousId.empty() || pPrevious != nullptr );
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
