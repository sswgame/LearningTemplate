#include "pch.h"

#include "Engine/Animation/Facial/LiveLinkFace.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/Math/BitUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"

namespace sw
{
    namespace
    {
        struct LiveLinkFaceInternal
        {
            /** @brief 빅 엔디언 바이트 읽기입니다. 모자라면 실패를 기억하고 0 을 냅니다. */
            class Reader
            {
            public:
                Reader( const uint8* pData, size_t size )
                    : _pData{ pData }
                    , _size{ size }
                    , _offset{ 0 }
                    , _bFailed{ SW_FALSE }
                {
                }

                uint8 readUint8()
                {
                    if ( require( 1 ) == false )
                        return 0;
                    return _pData[_offset++];
                }

                uint32 readUint32()
                {
                    if ( require( 4 ) == false )
                        return 0;
                    const uint32 value = ( static_cast<uint32>( _pData[_offset] ) << 24 ) | ( static_cast<uint32>( _pData[_offset + 1] ) << 16 ) |
                                         ( static_cast<uint32>( _pData[_offset + 2] ) << 8 ) | static_cast<uint32>( _pData[_offset + 3] );
                    _offset += 4;
                    return value;
                }

                int32   readInt32() { return static_cast<int32>( readUint32() ); }
                float32 readFloat32() { return BitUtil::bitsToFloat( readUint32() ); }

                /** @brief FString — int32 길이(끝 널 포함) 뒤 ANSI 바이트입니다. 음수 길이(UTF-16)는 받지 않습니다. 실패는 `hasFailed` 가 알리고 빈 글을 냅니다. */
                string takeString()
                {
                    const int32 length      = readInt32();
                    const bool  bUnreadable = _bFailed || length < 0; // 음수 길이는 UTF-16 문자열이다
                    if ( bUnreadable || require( static_cast<size_t>( length ) ) == false )
                    {
                        _bFailed = SW_TRUE;
                        return string{};
                    }
                    const bool   bNullTerminated = length > 0 && _pData[_offset + static_cast<size_t>( length ) - 1] == 0;
                    const size_t textLength      = bNullTerminated ? static_cast<size_t>( length ) - 1 : static_cast<size_t>( length );
                    string       text( reinterpret_cast<const utf8*>( _pData + _offset ), textLength );
                    _offset += static_cast<size_t>( length );
                    return text;
                }

                /** @brief 쓰지 않는 필드 @p count 바이트를 건너뜁니다. 모자라면 실패를 기억합니다. */
                void skipBytes( size_t count )
                {
                    if ( require( count ) )
                        _offset += count;
                }

                bool   hasFailed() const { return _bFailed; }
                size_t getRemaining() const { return _size - _offset; }

            private:
                bool require( size_t count )
                {
                    if ( _bFailed || _size - _offset < count )
                    {
                        _bFailed = SW_TRUE;
                        return false;
                    }
                    return true;
                }

            private:
                const uint8* _pData;
                size_t       _size;
                size_t       _offset;
                uint8        _bFailed : 1;
            };
            /** @brief 32비트 값을 빅 엔디안으로 덧붙입니다. */
            static void appendUint32( vector<uint8>& outBytes, uint32 value )
            {
                outBytes.push_back( static_cast<uint8>( value >> 24 ) );
                outBytes.push_back( static_cast<uint8>( value >> 16 ) );
                outBytes.push_back( static_cast<uint8>( value >> 8 ) );
                outBytes.push_back( static_cast<uint8>( value ) );
            }

            /** @brief 길이(끝 0 포함) 앞붙임 문자열을 덧붙입니다. */
            static void appendString( vector<uint8>& outBytes, const string& text )
            {
                appendUint32( outBytes, static_cast<uint32>( text.size() + 1 ) );
                outBytes.insert( outBytes.end(), text.begin(), text.end() );
                outBytes.push_back( 0 );
            }
        };
    } // namespace

    LiveLinkFaceSource::LiveLinkFaceSource()
        : _listExpression{}
        , _channelCount{ 0 }
        , _bHasFrame{ SW_FALSE }
    {
    }

} // namespace sw

namespace sw
{
    bool LiveLinkFaceLayout::loadFromXMLText( string_view xml, string_view sourceLabel )
    {
        return loadComposed( xml, sourceLabel, ResourceContext::forType( *StaticType() ) );
    }

    bool LiveLinkFaceLayout::loadComposed( string_view xml, string_view sourceLabel, const ResourceContext& context )
    {
        *this = LiveLinkFaceLayout{};
        if ( ConfigManager::readComposedXMLDocument( this, *StaticType(), xml, sourceLabel, context ) == false )
        {
            *this = LiveLinkFaceLayout{};
            return false;
        }
        for ( size_t channelIndex = 0; channelIndex < _listChannel.size(); ++channelIndex )
        {
            if ( _listChannel[channelIndex].empty() || findChannelIndex( _listChannel[channelIndex] ) != static_cast<uint32>( channelIndex ) )
            {
                SW_LOG_ERROR_ID( "LiveLinkFace.LiveLinkFaceLayout", "Live Link Face layout '%#': channel %# needs a unique, non-empty name", sourceLabel, channelIndex );
                *this = LiveLinkFaceLayout{};
                return false;
            }
        }
        if ( _listChannel.empty() )
        {
            SW_LOG_ERROR_ID( "LiveLinkFace.LiveLinkFaceLayout2", "Live Link Face layout '%#' has no channels", sourceLabel );
            return false;
        }
        return true;
    }

    bool LiveLinkFaceLayout::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "LiveLinkFace.LiveLinkFaceLayout3", "Live Link Face layout '%#' could not be read", path );
            *this = LiveLinkFaceLayout{};
            return false;
        }
        return loadComposed( text, path, ResourceContext::forType( *StaticType() ).withAsset( assetID ) );
    }

    optional<uint32> LiveLinkFaceLayout::findChannelIndex( const hashed_string& channel ) const
    {
        for ( size_t channelIndex = 0; channelIndex < _listChannel.size(); ++channelIndex )
        {
            if ( _listChannel[channelIndex] == channel )
                return static_cast<uint32>( channelIndex );
        }
        return nullopt;
    }

    bool LiveLinkFaceMapping::loadFromXMLText( string_view xml, string_view sourceLabel, LiveLinkFaceLayout& outLayout )
    {
        return loadComposed( xml, sourceLabel, ResourceContext::forType( *StaticType() ), outLayout );
    }

    bool LiveLinkFaceMapping::loadComposed( string_view xml, string_view sourceLabel, const ResourceContext& context, LiveLinkFaceLayout& outLayout )
    {
        *this     = LiveLinkFaceMapping{};
        outLayout = LiveLinkFaceLayout{};
        if ( ConfigManager::readComposedXMLDocument( this, *StaticType(), xml, sourceLabel, context ) == false )
        {
            *this = LiveLinkFaceMapping{};
            return false;
        }
        // 배치 표는 데이터가 고른다 — 적지 않았으면 기본 표로 물러서지 않고 오류다.
        if ( _layoutPath.empty() )
        {
            SW_LOG_ERROR_ID( "LiveLinkFace.LiveLinkFaceMapping", "Live Link Face mapping '%#' needs _layoutPath (the packet layout it reads)", sourceLabel );
            *this = LiveLinkFaceMapping{};
            return false;
        }
        if ( outLayout.loadFromAsset( _layoutPath.id() ) == false )
        {
            *this = LiveLinkFaceMapping{};
            return false; // 읽기가 까닭을 알렸다
        }
        const LiveLinkFaceLayout& layout = outLayout;
        vector<hashed_string>     listSeen;
        for ( const LiveLinkFaceExpression& expression : _listExpression )
        {
            const bool bBadExpression = expression._expression.empty() || expression._listChannel.empty() || VectorUtil::contains( listSeen, expression._expression );
            if ( bBadExpression )
            {
                SW_LOG_ERROR_ID( "LiveLinkFace.LiveLinkFaceMapping2", "Live Link Face mapping '%#': expression '%#' needs a unique name and at least one channel", sourceLabel, expression._expression.c_str() );
                *this = LiveLinkFaceMapping{};
                return false;
            }
            listSeen.push_back( expression._expression );
            for ( const LiveLinkFaceChannelWeight& channel : expression._listChannel )
            {
                if ( layout.findChannelIndex( channel._channel ).has_value() == false )
                {
                    SW_LOG_ERROR_ID( "LiveLinkFace.LiveLinkFaceMapping3", "Live Link Face mapping '%#': expression '%#' names channel '%#' that the packet layout does not have", sourceLabel,
                                     expression._expression.c_str(), channel._channel.c_str() );
                    *this = LiveLinkFaceMapping{};
                    return false;
                }
            }
        }
        return true;
    }

    bool LiveLinkFaceMapping::loadFromAsset( const AssetID& assetID, LiveLinkFaceLayout& outLayout )
    {
        const string_view path = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "LiveLinkFace.LiveLinkFaceMapping4", "Live Link Face mapping '%#' could not be read", path );
            *this     = LiveLinkFaceMapping{};
            outLayout = LiveLinkFaceLayout{};
            return false;
        }
        return loadComposed( text, path, ResourceContext::forType( *StaticType() ).withAsset( assetID ), outLayout );
    }

    expected<LiveLinkFaceFrame> LiveLinkFaceFrame::parse( const uint8* pData, size_t size, const LiveLinkFaceLayout& layout )
    {
        if ( pData == nullptr )
            return unexpected( Error{ ErrorKind::InvalidArgument, "empty packet" } );
        LiveLinkFaceFrame            frame{};
        LiveLinkFaceInternal::Reader reader( pData, size );
        const uint8                  version = reader.readUint8();
        utf8                         arrDetail[constant::buffer::kMax64]{};
        if ( reader.hasFailed() == false && static_cast<int32>( version ) != layout._packetVersion )
        {
            formatstring( arrDetail, constant::buffer::kMax64, "packet %#, layout %#", static_cast<uint32>( version ), layout._packetVersion );
            return unexpected( Error{ ErrorKind::InvalidArgument, "packet version differs from the layout", arrDetail } );
        }
        frame._deviceID    = reader.takeString();
        frame._subjectName = reader.takeString();
        frame._frameNumber = reader.readInt32();
        reader.skipBytes( sizeof( float32 ) + 2 * sizeof( int32 ) ); // 서브프레임 · 프레임 속도(분자 · 분모) — 얼굴 값만 쓴다
        const uint8 channelCount = reader.readUint8();
        if ( reader.hasFailed() == false && channelCount != layout._listChannel.size() )
        {
            formatstring( arrDetail, constant::buffer::kMax64, "packet %#, layout %#", static_cast<uint32>( channelCount ), static_cast<uint32>( layout._listChannel.size() ) );
            return unexpected( Error{ ErrorKind::InvalidArgument, "packet channel count differs from the layout", arrDetail } );
        }
        frame._listValue.resize( channelCount );
        for ( float32& value : frame._listValue )
        {
            value = reader.readFloat32();
        }
        if ( reader.hasFailed() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "packet is shorter than its fields" } );
        if ( reader.getRemaining() != 0 )
        {
            formatstring( arrDetail, constant::buffer::kMax64, "%# bytes", static_cast<uint64>( reader.getRemaining() ) );
            return unexpected( Error{ ErrorKind::InvalidArgument, "packet has bytes past its last channel", arrDetail } );
        }
        return frame;
    }

    void LiveLinkFaceFrame::write( const LiveLinkFaceFrame& frame, uint8 version, vector<uint8>& outBytes )
    {
        outBytes.clear();
        using Internal = LiveLinkFaceInternal;
        outBytes.push_back( version );
        Internal::appendString( outBytes, frame._deviceID );
        Internal::appendString( outBytes, frame._subjectName );
        Internal::appendUint32( outBytes, static_cast<uint32>( frame._frameNumber ) );
        Internal::appendUint32( outBytes, BitUtil::floatToBits( 0.0f ) );
        Internal::appendUint32( outBytes, 60u );
        Internal::appendUint32( outBytes, 1u );
        outBytes.push_back( static_cast<uint8>( frame._listValue.size() ) );
        for ( const float32 value : frame._listValue )
        {
            Internal::appendUint32( outBytes, BitUtil::floatToBits( value ) );
        }
    }

    void LiveLinkFaceSource::initialize( const LiveLinkFaceLayout& layout, const LiveLinkFaceMapping& mapping )
    {
        _listExpression.clear();
        _channelCount = static_cast<uint32>( layout._listChannel.size() );
        _bHasFrame    = SW_FALSE;
        for ( const LiveLinkFaceExpression& expression : mapping._listExpression )
        {
            ExpressionInput input{};
            input._expression = expression._expression;
            for ( const LiveLinkFaceChannelWeight& channel : expression._listChannel )
            {
                // 읽기(`LiveLinkFaceMapping::loadFromXMLText`)가 같은 배치로 이미 걸렀다 — 다른 배치와 짝지어 넘기면 그 채널은 알리고 뺀다.
                const optional<uint32> channelIndex = layout.findChannelIndex( channel._channel );
                if ( channelIndex.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "LiveLinkFace.SourceChannelNotInLayout", "Live Link Face source: expression '%#' names channel '%#' that the packet layout does not have - it is skipped",
                                     expression._expression.c_str(), channel._channel.c_str() );
                    continue;
                }
                input._listChannelIndex.push_back( *channelIndex );
                input._listScale.push_back( channel._scale );
            }
            _listExpression.push_back( std::move( input ) );
        }
    }

    void LiveLinkFaceSource::applyFrame( const LiveLinkFaceFrame& frame )
    {
        if ( frame._listValue.size() != _channelCount )
            return;
        for ( ExpressionInput& input : _listExpression )
        {
            input._value = 0.0f;
            for ( size_t index = 0; index < input._listChannelIndex.size(); ++index )
            {
                input._value += frame._listValue[input._listChannelIndex[index]] * input._listScale[index];
            }
        }
        _bHasFrame = SW_TRUE;
    }

    bool LiveLinkFaceSource::tryGetChannel( const hashed_string& channel, float32& outWeight ) const
    {
        if ( _bHasFrame == false )
            return false;
        for ( const ExpressionInput& input : _listExpression )
        {
            if ( input._expression == channel )
            {
                outWeight = input._value;
                return true;
            }
        }
        return false;
    }

    /** @brief 연결 표 파일 종류입니다(읽기가 패킷 배치 표를 함께 받아 표의 로더를 두지 않는다). */
    SW_ASSET_TYPE( LiveLinkFaceMapping, LiveLinkFaceMapping::kAssetType );
} // namespace sw
