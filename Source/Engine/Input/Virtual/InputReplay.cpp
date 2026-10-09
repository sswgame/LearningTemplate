#include "pch.h"

#include "Engine/Input/Virtual/InputReplay.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/Virtual/VirtualInputScript.h"

namespace sw
{
    SW_LOG_CALLER( "InputReplay" );

    namespace
    {
        /**
         * @brief 리플레이 파일 머리말입니다.
         * @details **버전은 읽는 쪽이 반드시 봅니다** — 매직만 보면 다른 판의 파일도 지금 판의 배치로 읽어 조용히 엉뚱한 프레임이 나옵니다.
         */
        struct ReplayHeader
        {
            uint8  _arrMagic[4]{ 'S', 'W', 'R', 'P' };
            uint32 _version{ 0 };
            uint32 _nameLength{ 0 };
            uint32 _frameCount{ 0 };
        };

        /**
         * @brief 지금 쓰는 리플레이 파일 판입니다. 프레임마다 `deltaTime(float32) · eventCount(uint32) · RawInputEvent × n` 입니다.
         * @details `RawInputEvent` 를 구조체째로 적으므로 그 배치가 바뀌면 판을 올립니다(다른 판은 읽지 않습니다).
         */
        constexpr uint32 kReplayVersion = 4;

        struct InputReplayInternal
        {
            template <typename T>
            static void appendBytes( vector<uint8>& outBytes, const T& value )
            {
                const uint8* pBytes = reinterpret_cast<const uint8*>( &value );
                outBytes.insert( outBytes.end(), pBytes, pBytes + sizeof( T ) );
            }

            template <typename T>
            [[nodiscard]] static bool readBytes( const uint8*& pCursor, const uint8* pEnd, T& outValue )
            {
                if ( static_cast<size_t>( pEnd - pCursor ) < sizeof( T ) )
                    return false;
                Memory::copy( &outValue, pCursor, sizeof( T ) );
                pCursor += sizeof( T );
                return true;
            }
        };
    } // namespace

    InputReplay::InputReplay()
        : _listFrame{}
        , _replayName{}
        , _startFrameIndex{ 0 }
        , _bRecording{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void InputReplay::startRecording( string_view replayName )
    {
        _listFrame.clear();
        _replayName      = replayName.empty() ? "NewReplay" : string( replayName );
        _startFrameIndex = 0;
        _bRecording      = SW_TRUE;
        SW_LOG_INFO( "Started recording input replay: %#", _replayName.c_str() );
    }

    void InputReplay::recordFrame( float32 deltaTime, const vector<RawInputEvent>& listEvent )
    {
        if ( _bRecording == SW_FALSE )
            return;

        InputReplayFrame frame{};
        frame._deltaTime    = deltaTime;
        frame._listRawEvent = listEvent;
        // 녹화한 사건은 재생 때 다시 가상 사건으로 표시된다 — 파일에는 원래 출처를 남기지 않는다.
        for ( RawInputEvent& event : frame._listRawEvent )
        {
            event._bSynthetic = SW_FALSE;
        }
        _listFrame.push_back( std::move( frame ) );
    }

    void InputReplay::stopRecording()
    {
        if ( _bRecording == SW_FALSE )
            return;

        _bRecording = SW_FALSE;
        SW_LOG_INFO( "Stopped recording input replay: %# (Total %d frames recorded)", _replayName.c_str(), static_cast<int32>( _listFrame.size() ) );
    }

    void InputReplay::emitFrame( uint32 frameIndex, vector<RawInputEvent>& outListEvent )
    {
        const uint32 recordedIndex = frameIndex + _startFrameIndex;
        if ( recordedIndex >= _listFrame.size() )
            return;
        const vector<RawInputEvent>& listEvent = _listFrame[recordedIndex]._listRawEvent;
        outListEvent.insert( outListEvent.end(), listEvent.begin(), listEvent.end() );
    }

    void InputReplay::seekTo( InputManager& input, uint32 frameIndex )
    {
        const uint32 endIndex = MathUtil::min( frameIndex, getFrameCount() );
        // 원시 사건은 누름 · 뗌처럼 방향이 있는 전이라 목표 프레임만 다시 넣으면 상태가 틀린다 — 처음부터 그 프레임 직전까지 다시 재생한다.
        input.detachVirtualInput();
        VirtualInputScript prefix;
        for ( uint32 index = 0; index < endIndex; ++index )
        {
            for ( const RawInputEvent& event : _listFrame[index]._listRawEvent )
            {
                prefix.addEvent( index, event );
            }
        }
        input.attachVirtualInput( &prefix );
        for ( uint32 index = 0; index < endIndex; ++index )
        {
            input.beginFrame( _listFrame[index]._deltaTime );
            input.endFrame();
        }
        input.detachVirtualInput( false );
        _startFrameIndex = endIndex;
    }

    bool InputReplay::saveToFile( string_view filePath ) const
    {
        vector<uint8> bytes;
        ReplayHeader  header{};
        header._version    = kReplayVersion;
        header._nameLength = static_cast<uint32>( _replayName.size() );
        header._frameCount = static_cast<uint32>( _listFrame.size() );
        InputReplayInternal::appendBytes( bytes, header );

        if ( header._nameLength > 0 )
        {
            const auto* pNameBytes = reinterpret_cast<const uint8*>( _replayName.data() );
            bytes.insert( bytes.end(), pNameBytes, pNameBytes + _replayName.size() );
        }

        for ( const InputReplayFrame& frame : _listFrame )
        {
            InputReplayInternal::appendBytes( bytes, frame._deltaTime );
            InputReplayInternal::appendBytes( bytes, static_cast<uint32>( frame._listRawEvent.size() ) );
            for ( const RawInputEvent& event : frame._listRawEvent )
            {
                InputReplayInternal::appendBytes( bytes, event );
            }
        }

        const string dirPart = FileUtil::getDirectoryPart( filePath );
        if ( dirPart.empty() == false )
            FileUtil::ensureDirectoryExists( dirPart );

        return FileUtil::writeFile( filePath, bytes.data(), bytes.size() );
    }

    bool InputReplay::loadFromFile( string_view filePath )
    {
        vector<uint8> bytes;
        if ( FileUtil::readFile( filePath, bytes ) == false )
            return false;

        const uint8* pCursor = bytes.data();
        const uint8* pEnd    = bytes.data() + bytes.size();

        ReplayHeader header{};
        if ( InputReplayInternal::readBytes( pCursor, pEnd, header ) == false )
            return false;
        if ( header._arrMagic[0] != 'S' || header._arrMagic[1] != 'W' || header._arrMagic[2] != 'R' || header._arrMagic[3] != 'P' )
            return false;

        // 판이 다르면 프레임 배치도 다르다. 읽어 봐야 엉뚱한 값이 나오므로 여기서 멈춘다.
        if ( header._version != kReplayVersion )
        {
            SW_LOG_WARNING( "Input replay version %# is not %# — refusing to load: %#", header._version, kReplayVersion, filePath );
            return false;
        }
        if ( static_cast<size_t>( pEnd - pCursor ) < header._nameLength )
        {
            SW_LOG_WARNING( "Input replay is truncated in its name — refusing to load: %#", filePath );
            return false;
        }

        string replayName;
        replayName.assign( reinterpret_cast<const utf8*>( pCursor ), header._nameLength );
        pCursor += header._nameLength;

        vector<InputReplayFrame> listFrame;
        listFrame.reserve( header._frameCount );
        for ( uint32 frameIndex = 0; frameIndex < header._frameCount; ++frameIndex )
        {
            InputReplayFrame frame{};
            uint32           eventCount = 0;
            // 잘린 프레임에서 멈춘다 — 그 뒤를 읽으면 엉뚱한 입력이 재생된다.
            if ( InputReplayInternal::readBytes( pCursor, pEnd, frame._deltaTime ) == false || InputReplayInternal::readBytes( pCursor, pEnd, eventCount ) == false ||
                 static_cast<size_t>( pEnd - pCursor ) / sizeof( RawInputEvent ) < eventCount )
            {
                SW_LOG_WARNING( "Input replay is truncated at frame %# — refusing to load: %#", frameIndex, filePath );
                return false;
            }
            frame._listRawEvent.resize( eventCount );
            for ( RawInputEvent& event : frame._listRawEvent )
            {
                (void)InputReplayInternal::readBytes( pCursor, pEnd, event ); // 남은 바이트가 eventCount 개 이상임을 위에서 확인했다 — 실패하지 않는다
            }
            listFrame.push_back( std::move( frame ) );
        }

        _listFrame       = std::move( listFrame );
        _replayName      = std::move( replayName );
        _startFrameIndex = 0;
        _bRecording      = SW_FALSE;
        SW_LOG_INFO( "Successfully loaded replay: %# (%d frames)", _replayName.c_str(), static_cast<int32>( _listFrame.size() ) );
        return true;
    }

    void InputReplay::clear()
    {
        _listFrame.clear();
        _replayName.clear();
        _startFrameIndex = 0;
        _bRecording      = SW_FALSE;
    }

    float32 InputReplay::getTotalDuration() const
    {
        float32 totalSec = 0.0f;
        for ( const InputReplayFrame& frame : _listFrame )
        {
            totalSec += frame._deltaTime;
        }
        return totalSec;
    }
} // namespace sw
