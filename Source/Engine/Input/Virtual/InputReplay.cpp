#include "pch.h"

#include "Engine/Input/Virtual/InputReplay.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Serialization/ByteStream.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/Virtual/VirtualInputScript.h"
#include "Engine/Serialization/Format/FormatDef.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 리플레이 파일의 머리(표 · 판) 뒤 칸입니다.
         */
        struct ReplayHeader
        {
            uint32 _nameLength{ 0 };
            uint32 _frameCount{ 0 };
        };

        /**
         * @brief 리플레이 파일 형식입니다. 프레임마다 `deltaTime(float32)`, `eventCount(uint32)`, `RawInputEvent` n 개가 이어집니다.
         * @details `RawInputEvent` 를 구조체째로 쓰므로 그 배치가 바뀌면 버전을 올린다(다른 버전은 읽지 않는다).
         */
        constexpr FormatDef kReplayFormat{ "SWIR" };

        struct InputReplayInternal
        {
            template <typename T>
            static void appendBytes( vector<uint8>& outBytes, const T& value )
            {
                const uint8* pBytes = reinterpret_cast<const uint8*>( &value );
                outBytes.insert( outBytes.end(), pBytes, pBytes + sizeof( T ) );
            }
        };
    } // namespace

    InputReplay::InputReplay()
        : _listFrame{}
        , _replayName{}
        , _startFrameIndex{ 0 }
        , _maxFrameCount{ 0 }
        , _bRecording{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void InputReplay::startRecording( string_view replayName, uint32 maxFrameCount )
    {
        _listFrame.clear();
        _replayName      = replayName.empty() ? "NewReplay" : string( replayName );
        _startFrameIndex = 0;
        _maxFrameCount   = maxFrameCount;
        _bRecording      = SW_TRUE;
        SW_LOG_INFO_ID( "InputReplay.StartedRecordingInputReplay", "Started recording input replay: %#", _replayName.c_str() );
    }

    void InputReplay::recordFrame( float32 deltaTime, const vector<RawInputEvent>& listEvent )
    {
        if ( _bRecording == SW_FALSE )
            return;

        InputReplayFrame frame{};
        frame._deltaTime    = deltaTime;
        frame._listRawEvent = listEvent;
        // 녹화한 이벤트는 재생할 때 다시 가상 이벤트로 표시된다. 파일에는 원래 출처를 남기지 않는다.
        for ( RawInputEvent& event : frame._listRawEvent )
        {
            event._bSynthetic = SW_FALSE;
        }
        _listFrame.push_back( std::move( frame ) );
        if ( _maxFrameCount != 0 && _listFrame.size() >= _maxFrameCount )
            stopRecording();
    }

    void InputReplay::stopRecording()
    {
        if ( _bRecording == SW_FALSE )
            return;

        _bRecording = SW_FALSE;
        SW_LOG_INFO_ID( "InputReplay.StoppedRecordingInputReplay", "Stopped recording input replay: %# (Total %# frames recorded)", _replayName.c_str(), static_cast<int32>( _listFrame.size() ) );
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
        // 원시 이벤트는 누름과 뗌처럼 방향이 있는 전이라 목표 프레임만 다시 넣으면 상태가 틀린다. 처음부터 그 프레임 직전까지 다시 재생한다.
        SW_ASSERT( input.isVirtualInputAttached( *this ) == false ); // 시작 프레임을 바꾸므로 재생 중이면 붙임을 먼저 푼다
        VirtualInputScript prefix;
        vector<float32>    listDeltaSeconds;
        listDeltaSeconds.reserve( endIndex );
        for ( uint32 index = 0; index < endIndex; ++index )
        {
            for ( const RawInputEvent& event : _listFrame[index]._listRawEvent )
            {
                prefix.addEvent( index, event );
            }
            listDeltaSeconds.push_back( _listFrame[index]._deltaTime );
        }
        input.runVirtualFrames( prefix, listDeltaSeconds );
        _startFrameIndex = endIndex;
    }

    bool InputReplay::saveToFile( const AbsolutePath& absPath ) const
    {
        vector<uint8> bytes;
        ReplayHeader  header{};
        header._nameLength = static_cast<uint32>( _replayName.size() );
        header._frameCount = static_cast<uint32>( _listFrame.size() );
        kReplayFormat.write( bytes );
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

        return FileUtil::ensureParentDirectoryExists( absPath ) && FileUtil::writeFile( absPath, bytes.data(), bytes.size() );
    }

    bool InputReplay::loadFromFile( const AbsolutePath& filePath )
    {
        vector<uint8> bytes;
        if ( FileUtil::readFile( filePath, bytes ) == false )
            return false;

        ByteReader reader{ bytes.data(), bytes.size() };
        if ( kReplayFormat.read( reader, filePath.str() ) == false )
            return false;
        const ReplayHeader header = reader.readPod<ReplayHeader>();
        if ( reader.hasOverflowed() )
            return false;
        const uint8* pName = nullptr;
        pName              = reader.readView( header._nameLength );
        if ( reader.hasOverflowed() )
        {
            SW_LOG_WARNING_ID( "InputReplay.InputReplayTruncatedName", "Input replay is truncated in its name — refusing to load: %#", filePath.str() );
            return false;
        }
        string replayName( reinterpret_cast<const utf8*>( pName ), header._nameLength );

        // 프레임은 적어도 시간과 이벤트 수를 쓴다. 남은 바이트로 담을 수 없는 수는 자리를 잡기 전에 거절한다(망가진 머리말이 큰 할당을 일으키지 않게).
        // 잘린 프레임에서도 멈춘다. 그 뒤를 읽으면 엉뚱한 입력이 재생된다.
        constexpr size_t         kMinFrameBytes = sizeof( float32 ) + sizeof( uint32 );
        vector<InputReplayFrame> listFrame;
        const bool               bFramesRead = reader.readList( listFrame, header._frameCount, kMinFrameBytes, [&]( InputReplayFrame& frame ) -> bool
                      {
            frame._deltaTime  = reader.readFloat32();
            uint32 eventCount = 0;
            eventCount        = reader.readPod<uint32>();
            return reader.hasOverflowed() == false && reader.readPodList( frame._listRawEvent, eventCount );
        } );
        if ( bFramesRead == false )
        {
            SW_LOG_WARNING_ID( "InputReplay.InputReplayTruncatedFrame", "Input replay promises %# frames but is truncated or holds a non-finite frame time — refusing to load: %#", header._frameCount, filePath.str() );
            return false;
        }

        _listFrame       = std::move( listFrame );
        _replayName      = std::move( replayName );
        _startFrameIndex = 0;
        _bRecording      = SW_FALSE;
        SW_LOG_INFO_ID( "InputReplay.SuccessfullyLoadedReplayFrames", "Successfully loaded replay: %# (%# frames)", _replayName.c_str(), static_cast<int32>( _listFrame.size() ) );
        return true;
    }

    void InputReplay::clear()
    {
        _listFrame.clear();
        _replayName.clear();
        _startFrameIndex = 0;
        _maxFrameCount   = 0;
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
