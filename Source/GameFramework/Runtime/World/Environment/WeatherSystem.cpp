#include "pch.h"

#include "GameFramework/Runtime/World/Environment/WeatherSystem.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

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

    uint32 WeatherCatalog::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        // 적지 않은 칸은 초기값이다. 깨진 값 · 범위 밖 값은 오류다(자르지 않는다). 날씨는 그 원소를, 전환 시간은 표 전체를 넣지 않는다.
        WeatherCatalogRootParams rootParams{ _transitionTime };
        if ( root.readProperties( rootParams, sourceName ) == false )
            return 0;
        _transitionTime                        = rootParams._transition;
        using Element                          = XMLCatalogElement<WeatherCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&WeatherCatalog::loadWeather>( "Weather" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result._loadedCount;
    }

    bool WeatherCatalog::loadWeather( const XMLNode& node, string_view sourceName )
    {

        WeatherDef  weather;
        bool        bValid = XMLFields::readProperties( node, weather, sourceName, { "seasons" } ); // 칸 · 범위는 WeatherDef 의 PROPERTY
        const utf8* pID    = weather._id.c_str();
        if ( bValid && weather._maxDuration < weather._minDuration )
        {
            SW_LOG_ERROR_ID( "WeatherSystem.WeatherDurationOrder", "%#: weather '%#' needs maxDuration >= minDuration", sourceName, pID );
            bValid = false;
        }
        // "Spring:3,Summer:1" — 계절:가중치(가중치를 빼면 그 날씨의 `weight`). 읽히지 않거나 음수인 가중치는 오류다.
        XMLReadUtil::forEachNamedNumber( node.getAttributeText( "seasons" ), ", ", weather._weight, XMLReadUtil::MissingValue::UseDefault,
                                         [&]( string_view token, string_view seasonName, float32 weight, bool bNumberValid )
        {
            if ( bNumberValid == false || weight < 0.0f )
            {
                SW_LOG_ERROR_ID( "WeatherSystem.WeatherInvalidSeasonWeight", "%#: weather '%#' has an invalid season weight '%#'", sourceName, pID, token );
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
            SW_LOG_ERROR_ID( "WeatherSystem.WeatherInvalidFieldsSkipped", "%#: weather '%#' has invalid fields - skipped", sourceName, pID );
            return false;
        }
        const XMLNode valueNode = node.findChild( "Values" );
        if ( valueNode )
            weather._values.addAttributes( valueNode ); // 읽은 속성 수만 돌려준다 — 없으면 빈 값이다
        (void)add( weather );
        return true;
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
        if ( gameSeconds <= 0.0f )
            return false;
        if ( _pCurrent == nullptr )
        {
            // 시작 계절에 올 날씨가 없었다. 올 수 있는 계절이 되면 전환 없이 시작한다.
            _pCurrent = pickWeather( season, _random, nullptr );
            if ( _pCurrent == nullptr )
                return false;
            _remaining = pickDuration( *_pCurrent, _random );
            return true;
        }
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

    void WeatherSystem::serializeState( StateIO& io )
    {
        hashed_string currentID  = getCurrent();
        hashed_string previousID = getPrevious();
        io.value( currentID );
        io.value( previousID );
        io.value( _random );
        io.value( _remaining );
        io.value( _transitionElapsed );
        if ( io.isWriting() || io.isOk() == false )
            return;
        _pCurrent  = currentID.empty() || _pCatalog == nullptr ? nullptr : _pCatalog->find( currentID );
        _pPrevious = previousID.empty() || _pCatalog == nullptr ? nullptr : _pCatalog->find( previousID );
        io.check( ( currentID.empty() || _pCurrent != nullptr ) && ( previousID.empty() || _pPrevious != nullptr ) );
    }

} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<WeatherCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( WeatherCatalog, WeatherCatalog, SW_GF_API, WeatherCatalog::kAssetType.withLoader( AssetTypeLoader::of<WeatherCatalog>( MemoryTag::Game ) ) );
} // namespace sw
