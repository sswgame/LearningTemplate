#include "pch.h"

#include "Engine/Animation/SpriteClipAsset.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    SW_LOG_CALLER( "SpriteClip" );

} // namespace sw

namespace sw
{
    bool SpriteClipAsset::loadFromFile( string_view path )
    {
        clear();
        if ( path.empty() )
            return false;

        JsonDocument doc;
        if ( doc.loadPath( path ) == false )
        {
            SW_LOG_WARNING( "Could not read sprite clip '%#': %#", string( path ), doc.getLastError() );
            return false;
        }
        return parseRoot( doc.getRoot(), path );
    }

    bool SpriteClipAsset::saveToFile( string_view path ) const
    {
        if ( path.empty() )
            return false;
        FileUtil::ensureParentDirectoryExists( path );
        return FileUtil::writeTextFile( path, toJson() );
    }

    bool SpriteClipAsset::parseJson( string_view json )
    {
        clear();
        JsonDocument doc;
        if ( doc.parse( json ) == false )
            return false;
        return parseRoot( doc.getRoot(), "<text>" );
    }

    void SpriteClipAsset::clear()
    {
        _atlasPath.clear();
        _listFrame.clear();
        _listKey.clear();
        _listAnimation.clear();
    }

    bool SpriteClipAsset::parseRoot( const JsonValue& root, string_view sourceLabel )
    {
        if ( root.isObject() == false )
        {
            SW_LOG_WARNING( "Sprite clip '%#' is not a JSON object - nothing was read", string( sourceLabel ) );
            return false;
        }

        _atlasPath = root.get( "atlas" ).asString();

        // 키가 빠진 프레임은 구조체 기본값이다(UV 전체). 시간이 빠지면 0 = 애니메이터의 프레임 속도. 에디터는 다섯 키를 늘 다 쓴다.
        forEachObjectInArray( root, "frames", [this, sourceLabel]( const JsonValue& frameJson, size_t frameIndex )
        {
            SpriteClipFrame frame{};
            frame._uvRect._x  = static_cast<float32>( frameJson.get( "u" ).asFloat( 0.0 ) );
            frame._uvRect._y  = static_cast<float32>( frameJson.get( "v" ).asFloat( 0.0 ) );
            frame._uvRect._z  = static_cast<float32>( frameJson.get( "w" ).asFloat( 1.0 ) );
            frame._uvRect._w  = static_cast<float32>( frameJson.get( "h" ).asFloat( 1.0 ) );
            frame._durationMs = static_cast<int32>( frameJson.get( "durationMs" ).asInt( 0 ) );
            // 9-슬라이스 테두리는 선택 키다. 숫자 넷의 배열이 아니면 데이터 오류라 알리고 테두리 없이 읽는다.
            const JsonValue borderJson = frameJson.get( "border" );
            if ( borderJson.isValid() )
            {
                if ( borderJson.isArray() && borderJson.size() == 4 )
                {
                    frame._border._x = MathUtil::saturate( static_cast<float32>( borderJson.at( 0 ).asFloat( 0.0 ) ) );
                    frame._border._y = MathUtil::saturate( static_cast<float32>( borderJson.at( 1 ).asFloat( 0.0 ) ) );
                    frame._border._z = MathUtil::saturate( static_cast<float32>( borderJson.at( 2 ).asFloat( 0.0 ) ) );
                    frame._border._w = MathUtil::saturate( static_cast<float32>( borderJson.at( 3 ).asFloat( 0.0 ) ) );
                }
                else
                {
                    SW_LOG_ERROR( "Sprite clip '%#' frame %#: \"border\" must be [left, bottom, right, top] fractions - read without a border",
                                  string( sourceLabel ), frameIndex );
                }
            }
            _listFrame.push_back( frame );
        } );

        forEachObjectInArray( root, "transformKeys", [this]( const JsonValue& keyJson, size_t /*keyIndex*/ )
        {
            SpriteClipKey key{};
            key._time        = static_cast<float32>( keyJson.get( "time" ).asFloat( 0.0 ) );
            key._position._x = static_cast<float32>( keyJson.get( "x" ).asFloat( 0.0 ) );
            key._position._y = static_cast<float32>( keyJson.get( "y" ).asFloat( 0.0 ) );
            key._angleDeg    = static_cast<float32>( keyJson.get( "angleDeg" ).asFloat( 0.0 ) );
            _listKey.push_back( key );
        } );

        // 구간은 프레임 목록 안으로 자른다. 잘린 것 · 버린 것은 로드마다 한 번 알린다(조용히 다른 프레임을 보이지 않게).
        const int32 frameCount   = getFrameCount();
        uint32      clampedCount = 0;
        forEachObjectInArray( root, "animations", [this, frameCount, &clampedCount]( const JsonValue& animationJson, size_t /*animationIndex*/ )
        {
            SpriteClipAnimation animation{};
            animation._name            = animationJson.get( "name" ).asString();
            const int64 requestedFirst = animationJson.get( "start" ).asInt( 0 );
            const int64 requestedCount = animationJson.get( "count" ).asInt( 0 );
            animation._bLoop           = animationJson.get( "loop" ).asBool( true ) ? SW_TRUE : SW_FALSE;
            const int64 firstFrame     = MathUtil::clamp<int64>( requestedFirst, 0, frameCount );
            const int64 lastFrame      = MathUtil::clamp<int64>( requestedFirst + requestedCount, firstFrame, frameCount );
            animation._firstFrame      = static_cast<int32>( firstFrame );
            animation._frameCount      = static_cast<int32>( lastFrame - firstFrame );
            const bool bRangeKept      = ( firstFrame == requestedFirst ) && ( lastFrame - firstFrame == requestedCount );
            if ( bRangeKept == false )
                ++clampedCount;
            if ( animation._name.empty() || animation._frameCount <= 0 )
                return;
            _listAnimation.push_back( std::move( animation ) );
        } );
        if ( clampedCount > 0 )
            SW_LOG_WARNING( "Sprite clip '%#': %# animation range(s) fell outside the %# frame(s) and were clamped or dropped", string( sourceLabel ),
                            clampedCount, frameCount );
        return true;
    }

    string SpriteClipAsset::toJson() const
    {
        JsonDocument    doc;
        const JsonValue root = doc.makeObject();
        root.set( "atlas" ).setString( _atlasPath );

        const JsonValue framesVal = root.set( "frames" );
        framesVal.setArray();
        for ( const SpriteClipFrame& frame : _listFrame )
        {
            const JsonValue frameJson = framesVal.pushBack();
            frameJson.setObject();
            frameJson.set( "u" ).setFloat( static_cast<float64>( frame._uvRect._x ) );
            frameJson.set( "v" ).setFloat( static_cast<float64>( frame._uvRect._y ) );
            frameJson.set( "w" ).setFloat( static_cast<float64>( frame._uvRect._z ) );
            frameJson.set( "h" ).setFloat( static_cast<float64>( frame._uvRect._w ) );
            frameJson.set( "durationMs" ).setInt( frame._durationMs );
            if ( frame.hasBorder() )
            {
                const JsonValue borderJson = frameJson.set( "border" );
                borderJson.setArray();
                borderJson.pushBack().setFloat( static_cast<float64>( frame._border._x ) );
                borderJson.pushBack().setFloat( static_cast<float64>( frame._border._y ) );
                borderJson.pushBack().setFloat( static_cast<float64>( frame._border._z ) );
                borderJson.pushBack().setFloat( static_cast<float64>( frame._border._w ) );
            }
        }

        const JsonValue keysVal = root.set( "transformKeys" );
        keysVal.setArray();
        for ( const SpriteClipKey& key : _listKey )
        {
            const JsonValue keyJson = keysVal.pushBack();
            keyJson.setObject();
            keyJson.set( "time" ).setFloat( static_cast<float64>( key._time ) );
            keyJson.set( "x" ).setFloat( static_cast<float64>( key._position._x ) );
            keyJson.set( "y" ).setFloat( static_cast<float64>( key._position._y ) );
            keyJson.set( "angleDeg" ).setFloat( static_cast<float64>( key._angleDeg ) );
        }

        if ( _listAnimation.empty() == false )
        {
            const JsonValue animationsVal = root.set( "animations" );
            animationsVal.setArray();
            for ( const SpriteClipAnimation& animation : _listAnimation )
            {
                const JsonValue animationJson = animationsVal.pushBack();
                animationJson.setObject();
                animationJson.set( "name" ).setString( animation._name );
                animationJson.set( "start" ).setInt( animation._firstFrame );
                animationJson.set( "count" ).setInt( animation._frameCount );
                animationJson.set( "loop" ).setBool( animation._bLoop == SW_TRUE );
            }
        }

        return doc.dump( 2 );
    }

    const SpriteClipFrame* SpriteClipAsset::findFrame( int32 frameIndex ) const
    {
        if ( frameIndex < 0 || frameIndex >= getFrameCount() )
            return nullptr;
        return &_listFrame[static_cast<size_t>( frameIndex )];
    }

    float32 SpriteClipAsset::getFrameDurationSeconds( int32 frameIndex, float32 fallbackSeconds ) const
    {
        const SpriteClipFrame* pFrame = findFrame( frameIndex );
        if ( pFrame == nullptr || pFrame->_durationMs <= 0 )
            return fallbackSeconds;
        return static_cast<float32>( pFrame->_durationMs ) * 0.001f;
    }

    const SpriteClipAnimation* SpriteClipAsset::findAnimation( string_view name ) const
    {
        for ( const SpriteClipAnimation& animation : _listAnimation )
        {
            if ( animation._name == name )
                return &animation;
        }
        return nullptr;
    }

    bool SpriteClipAsset::findFrameRange( string_view name, SpriteClipAnimation& outRange ) const
    {
        if ( _listAnimation.empty() )
        {
            if ( _listFrame.empty() )
                return false;
            outRange._firstFrame = 0;
            outRange._frameCount = getFrameCount();
            outRange._bLoop      = SW_TRUE;
            return true;
        }
        const SpriteClipAnimation* pAnimation = findAnimation( name );
        if ( pAnimation == nullptr )
            return false;
        outRange._firstFrame = pAnimation->_firstFrame;
        outRange._frameCount = pAnimation->_frameCount;
        outRange._bLoop      = pAnimation->_bLoop;
        return true;
    }

    float32 SpriteClipAsset::computeFrameStartSeconds( int32 frameIndex, float32 fallbackSeconds ) const
    {
        const int32 endFrame  = MathUtil::min( frameIndex, getFrameCount() );
        float32     startTime = 0.0f;
        for ( int32 earlierFrame = 0; earlierFrame < endFrame; ++earlierFrame )
            startTime += getFrameDurationSeconds( earlierFrame, fallbackSeconds );
        return startTime;
    }

    bool SpriteClipAsset::sampleTransformKey( float32 clipSeconds, SpriteClipKey& outKey ) const
    {
        if ( _listKey.empty() )
            return false;

        // 앞 키 = 시각이 clipSeconds 이하인 것 중 가장 늦은 것, 뒤 키 = clipSeconds 보다 늦은 것 중 가장 이른 것. 순서가 없는 목록이라 한 번 훑는다.
        const SpriteClipKey* pBefore = nullptr;
        const SpriteClipKey* pAfter  = nullptr;
        for ( const SpriteClipKey& key : _listKey )
        {
            if ( key._time <= clipSeconds )
            {
                if ( pBefore == nullptr || key._time > pBefore->_time )
                    pBefore = &key;
            }
            else if ( pAfter == nullptr || key._time < pAfter->_time )
            {
                pAfter = &key;
            }
        }

        if ( pBefore == nullptr || pAfter == nullptr )
        {
            outKey = ( pBefore != nullptr ) ? *pBefore : *pAfter;
            return true;
        }

        const float32 alpha = ( clipSeconds - pBefore->_time ) / ( pAfter->_time - pBefore->_time );
        outKey._time        = clipSeconds;
        outKey._position._x = MathUtil::lerp( pBefore->_position._x, pAfter->_position._x, alpha );
        outKey._position._y = MathUtil::lerp( pBefore->_position._y, pAfter->_position._y, alpha );
        outKey._angleDeg    = MathUtil::lerp( pBefore->_angleDeg, pAfter->_angleDeg, alpha );
        return true;
    }
} // namespace sw
