#include "pch.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Serialization/Format/JSONSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "SpriteClip" );

    namespace
    {
        /** @brief 네 칸 중 하나라도 0 보다 크면 true 입니다. */
        bool hasPositiveComponent( const float4& value )
        {
            return value._x > 0.0f || value._y > 0.0f || value._z > 0.0f || value._w > 0.0f;
        }

        /** @brief 네 칸 중 하나라도 음수면 true 입니다. */
        bool hasNegativeComponent( const float4& value )
        {
            return value._x < 0.0f || value._y < 0.0f || value._z < 0.0f || value._w < 0.0f;
        }
    } // namespace
} // namespace sw

namespace sw
{
    bool SpriteClipAsset::loadFromFile( string_view path )
    {
        clear();
        if ( path.empty() )
            return false;

        string text;
        if ( ResourceIO::readTextResource( path, text ) == false )
        {
            SW_LOG_WARNING( "Could not read sprite clip '%#'", path );
            return false;
        }
        const string label{ path };
        // 키는 대소문자까지 맞아야 하고, 모르는 키 · 읽지 못한 값은 오류다(`readConfigJSON` 이 로그로 알린다).
        if ( ConfigManager::readConfigJSON( *this, text, label.c_str() ) && resolveLoadedValues( path ) )
            return true;
        clear();
        return false;
    }

    bool SpriteClipAsset::saveToFile( string_view path ) const
    {
        if ( path.empty() )
            return false;
        FileUtil::ensureParentDirectoryExists( path );
        return FileUtil::writeTextFile( path, toJSON() );
    }

    bool SpriteClipAsset::parseJSON( string_view json )
    {
        clear();
        if ( ConfigManager::readConfigJSON( *this, json, "<sprite clip text>" ) && resolveLoadedValues( "<sprite clip text>" ) )
            return true;
        clear();
        return false;
    }

    void SpriteClipAsset::clear()
    {
        _atlasPath.clear();
        _atlasPixelSize = float2{ 0.0f, 0.0f };
        _listFrame.clear();
        _listKey.clear();
        _listAnimation.clear();
    }

    bool SpriteClipAsset::resolveLoadedValues( string_view sourceLabel )
    {
        // 음수 크기 · 테두리는 쓸 수 없는 값이다 — 0 으로 바꿔 읽지 않고 오류로 멈춘다.
        bool bValid = true;
        if ( _atlasPixelSize._x < 0.0f || _atlasPixelSize._y < 0.0f )
        {
            SW_LOG_ERROR( "Sprite clip '%#': _atlasPixelSize must not be negative", sourceLabel );
            bValid = false;
        }
        for ( size_t frameIndex = 0; frameIndex < _listFrame.size(); ++frameIndex )
        {
            if ( hasNegativeComponent( _listFrame[frameIndex]._borderPixels ) == false )
                continue;
            SW_LOG_ERROR( "Sprite clip '%#' frame %#: _borderPixels must not be negative", sourceLabel, frameIndex );
            bValid = false;
        }
        if ( bValid == false )
            return false;

        // 비율 테두리는 파일에 없다 — 픽셀 테두리와 아틀라스 크기로 채운다. 아틀라스 크기가 없으면 그 테두리를 버리고 알린다.
        const uint32 droppedBorderCount = rebuildFrameBorders();
        if ( droppedBorderCount > 0 )
            SW_LOG_ERROR( "Sprite clip '%#': %# frame(s) have a pixel border but the clip has no _atlasPixelSize - read without those borders", sourceLabel,
                          droppedBorderCount );

        // 구간은 프레임 목록 안으로 자른다. 잘리거나 버린 것은 로드마다 한 번 알린다(조용히 다른 프레임을 보이지 않게 하려는 것이다).
        // 에디터는 구간 칸을 숫자로 바로 고치므로 프레임을 지운 뒤 저장한 파일에 프레임 밖 구간이 남을 수 있다.
        const int32                 frameCount   = getFrameCount();
        uint32                      clampedCount = 0;
        vector<SpriteClipAnimation> listKept;
        listKept.reserve( _listAnimation.size() );
        for ( SpriteClipAnimation& animation : _listAnimation )
        {
            const int64 requestedFirst = animation._firstFrame;
            const int64 requestedCount = animation._frameCount;
            const int64 firstFrame     = MathUtil::clamp<int64>( requestedFirst, 0, frameCount );
            const int64 lastFrame      = MathUtil::clamp<int64>( requestedFirst + requestedCount, firstFrame, frameCount );
            animation._firstFrame      = static_cast<int32>( firstFrame );
            animation._frameCount      = static_cast<int32>( lastFrame - firstFrame );
            const bool bRangeKept      = ( firstFrame == requestedFirst ) && ( lastFrame - firstFrame == requestedCount );
            if ( bRangeKept == false )
                ++clampedCount;
            // 알림은 이름으로 찾으므로 이름이 빈 알림은 울릴 곳이 없다.
            const auto itUnnamed = std::remove_if( animation._listNotify.begin(), animation._listNotify.end(), []( const AnimationNotifyEvent& event )
            { return event._name.empty(); } );
            animation._listNotify.erase( itUnnamed, animation._listNotify.end() );
            if ( animation._name.empty() || animation._frameCount <= 0 )
                continue;
            listKept.push_back( std::move( animation ) );
        }
        _listAnimation = std::move( listKept );
        if ( clampedCount > 0 )
            SW_LOG_WARNING( "Sprite clip '%#': %# animation range(s) fell outside the %# frame(s) and were clamped or dropped", sourceLabel, clampedCount,
                            frameCount );
        return true;
    }

    uint32 SpriteClipAsset::rebuildFrameBorders()
    {
        uint32 droppedCount{ 0 };
        for ( SpriteClipFrame& frame : _listFrame )
        {
            frame._border        = float4{ 0.0f, 0.0f, 0.0f, 0.0f };
            const float4& pixels = frame._borderPixels;
            if ( hasPositiveComponent( pixels ) == false )
                continue;
            // 프레임의 픽셀 크기 = UV 크기 × 아틀라스 크기(폭이 음수인 뒤집힌 프레임도 크기는 절댓값이다).
            const float32 frameWidth  = MathUtil::abs( frame._uvRect._z ) * _atlasPixelSize._x;
            const float32 frameHeight = MathUtil::abs( frame._uvRect._w ) * _atlasPixelSize._y;
            if ( frameWidth <= 0.0f || frameHeight <= 0.0f )
            {
                ++droppedCount;
                continue;
            }
            frame._border._x = MathUtil::saturate( pixels._x / frameWidth );
            frame._border._y = MathUtil::saturate( pixels._y / frameHeight );
            frame._border._z = MathUtil::saturate( pixels._z / frameWidth );
            frame._border._w = MathUtil::saturate( pixels._w / frameHeight );
        }
        return droppedCount;
    }

    string SpriteClipAsset::toJSON() const
    {
        return JSONSerializer::serializePretty( this, *StaticType(), 2 );
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
        {
            startTime += getFrameDurationSeconds( earlierFrame, fallbackSeconds );
        }
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
        outKey._angle       = MathUtil::lerp( pBefore->_angle, pAfter->_angle, alpha );
        return true;
    }
} // namespace sw
