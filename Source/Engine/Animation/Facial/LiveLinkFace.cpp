#include "pch.h"

#include "Engine/Animation/Facial/LiveLinkFace.h"

#include "Core/Common/BitUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Resource/ResourceIO.h"

namespace sw
{
    SW_LOG_CALLER( "LiveLinkFace" );

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
                    , _bFailed{ false }
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
                        _bFailed = true;
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
                        _bFailed = true;
                        return false;
                    }
                    return true;
                }

                const uint8* _pData;
                size_t       _size;
                size_t       _offset;
                bool         _bFailed;
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool LiveLinkFaceLayout::parseJSON( string_view json, string_view sourceLabel )
    {
        *this = LiveLinkFaceLayout{};
        const string label{ sourceLabel };
        if ( ConfigManager::readConfigJSON( *this, json, label.c_str() ) == false )
        {
            *this = LiveLinkFaceLayout{};
            return false;
        }
        for ( size_t channelIndex = 0; channelIndex < _listChannel.size(); ++channelIndex )
        {
            if ( _listChannel[channelIndex].empty() || findChannelIndex( _listChannel[channelIndex] ) != static_cast<int32>( channelIndex ) )
            {
                SW_LOG_ERROR( "Live Link Face layout '%#': channel %# needs a unique, non-empty name", sourceLabel, channelIndex );
                *this = LiveLinkFaceLayout{};
                return false;
            }
        }
        if ( _listChannel.empty() )
        {
            SW_LOG_ERROR( "Live Link Face layout '%#' has no channels", sourceLabel );
            return false;
        }
        return true;
    }

    bool LiveLinkFaceLayout::loadFromResource( string_view path )
    {
        string text;
        if ( ResourceIO::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Live Link Face layout '%#' could not be read", path );
            *this = LiveLinkFaceLayout{};
            return false;
        }
        return parseJSON( text, path );
    }

    int32 LiveLinkFaceLayout::findChannelIndex( const hashed_string& channel ) const
    {
        for ( size_t channelIndex = 0; channelIndex < _listChannel.size(); ++channelIndex )
        {
            if ( _listChannel[channelIndex] == channel )
                return static_cast<int32>( channelIndex );
        }
        return -1;
    }

    bool LiveLinkFaceMapping::parseJSON( string_view json, string_view sourceLabel, LiveLinkFaceLayout& outLayout )
    {
        *this     = LiveLinkFaceMapping{};
        outLayout = LiveLinkFaceLayout{};
        const string label{ sourceLabel };
        if ( ConfigManager::readConfigJSON( *this, json, label.c_str() ) == false )
        {
            *this = LiveLinkFaceMapping{};
            return false;
        }
        // 배치 표는 데이터가 고른다 — 적지 않았으면 기본 표로 물러서지 않고 오류다.
        if ( _layoutPath.empty() )
        {
            SW_LOG_ERROR( "Live Link Face mapping '%#' needs _layoutPath (the packet layout it reads)", sourceLabel );
            *this = LiveLinkFaceMapping{};
            return false;
        }
        if ( outLayout.loadFromResource( _layoutPath ) == false )
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
                SW_LOG_ERROR( "Live Link Face mapping '%#': expression '%#' needs a unique name and at least one channel", sourceLabel, expression._expression.c_str() );
                *this = LiveLinkFaceMapping{};
                return false;
            }
            listSeen.push_back( expression._expression );
            for ( const LiveLinkFaceChannelWeight& channel : expression._listChannel )
            {
                if ( layout.findChannelIndex( channel._channel ) < 0 )
                {
                    SW_LOG_ERROR( "Live Link Face mapping '%#': expression '%#' names channel '%#' that the packet layout does not have", sourceLabel,
                                  expression._expression.c_str(), channel._channel.c_str() );
                    *this = LiveLinkFaceMapping{};
                    return false;
                }
            }
        }
        return true;
    }

    bool LiveLinkFaceMapping::loadFromResource( string_view path, LiveLinkFaceLayout& outLayout )
    {
        string text;
        if ( ResourceIO::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Live Link Face mapping '%#' could not be read", path );
            *this     = LiveLinkFaceMapping{};
            outLayout = LiveLinkFaceLayout{};
            return false;
        }
        return parseJSON( text, path, outLayout );
    }

    bool LiveLinkFacePacket::parse( const uint8* pData, size_t size, const LiveLinkFaceLayout& layout, LiveLinkFaceFrame& outFrame, string& outError )
    {
        outFrame = LiveLinkFaceFrame{};
        outError.clear();
        if ( pData == nullptr )
        {
            outError = "empty packet";
            return false;
        }
        LiveLinkFaceInternal::Reader reader( pData, size );
        const uint8                  version = reader.readUint8();
        utf8                         arrMessage[constant::kMaxBuffer256]{};
        if ( reader.hasFailed() == false && static_cast<int32>( version ) != layout._packetVersion )
        {
            formatstring( arrMessage, constant::kMaxBuffer256, "packet version %# but the layout reads version %#", static_cast<uint32>( version ), layout._packetVersion );
            outError = arrMessage;
            return false;
        }
        outFrame._deviceID    = reader.takeString();
        outFrame._subjectName = reader.takeString();
        outFrame._frameNumber = reader.readInt32();
        reader.skipBytes( sizeof( float32 ) + 2 * sizeof( int32 ) ); // 서브프레임 · 프레임 속도(분자 · 분모) — 얼굴 값만 쓴다
        const uint8 channelCount = reader.readUint8();
        if ( reader.hasFailed() == false && channelCount != layout._listChannel.size() )
        {
            formatstring( arrMessage, constant::kMaxBuffer256, "packet carries %# channels but the layout has %#", static_cast<uint32>( channelCount ),
                          static_cast<uint32>( layout._listChannel.size() ) );
            outError = arrMessage;
            return false;
        }
        outFrame._listValue.resize( channelCount );
        for ( float32& value : outFrame._listValue )
        {
            value = reader.readFloat32();
        }
        if ( reader.hasFailed() || reader.getRemaining() != 0 )
        {
            if ( reader.hasFailed() )
            {
                outError = "packet is shorter than its fields";
            }
            else
            {
                formatstring( arrMessage, constant::kMaxBuffer256, "packet has %# bytes past its last channel", static_cast<uint64>( reader.getRemaining() ) );
                outError = arrMessage;
            }
            outFrame = LiveLinkFaceFrame{};
            return false;
        }
        return true;
    }

    void LiveLinkFacePacket::write( const LiveLinkFaceFrame& frame, uint8 version, vector<uint8>& outBytes )
    {
        outBytes.clear();
        auto pushUint32 = [&outBytes]( uint32 value )
        {
            outBytes.push_back( static_cast<uint8>( value >> 24 ) );
            outBytes.push_back( static_cast<uint8>( value >> 16 ) );
            outBytes.push_back( static_cast<uint8>( value >> 8 ) );
            outBytes.push_back( static_cast<uint8>( value ) );
        };
        auto pushString = [&outBytes, &pushUint32]( const string& text )
        {
            pushUint32( static_cast<uint32>( text.size() + 1 ) );
            outBytes.insert( outBytes.end(), text.begin(), text.end() );
            outBytes.push_back( 0 );
        };
        outBytes.push_back( version );
        pushString( frame._deviceID );
        pushString( frame._subjectName );
        pushUint32( static_cast<uint32>( frame._frameNumber ) );
        pushUint32( BitUtil::floatToBits( 0.0f ) );
        pushUint32( 60u );
        pushUint32( 1u );
        outBytes.push_back( static_cast<uint8>( frame._listValue.size() ) );
        for ( const float32 value : frame._listValue )
        {
            pushUint32( BitUtil::floatToBits( value ) );
        }
    }

    void LiveLinkFaceSource::initialize( const LiveLinkFaceLayout& layout, const LiveLinkFaceMapping& mapping )
    {
        _listExpression.clear();
        _channelCount = static_cast<uint32>( layout._listChannel.size() );
        _bHasFrame    = false;
        for ( const LiveLinkFaceExpression& expression : mapping._listExpression )
        {
            ExpressionInput input{};
            input._expression = expression._expression;
            for ( const LiveLinkFaceChannelWeight& channel : expression._listChannel )
            {
                input._listChannelIndex.push_back( layout.findChannelIndex( channel._channel ) );
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
                input._value += frame._listValue[static_cast<size_t>( input._listChannelIndex[index] )] * input._listScale[index];
            }
        }
        _bHasFrame = true;
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
} // namespace sw
