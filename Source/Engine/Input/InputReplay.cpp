#include "pch.h"

#include "Engine/Input/InputReplay.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Input/InputManager.h"

namespace sw
{
	SW_LOG_CALLER( "InputReplay" );

	namespace
	{
		struct ReplayHeader
		{
			uint8  _arrMagic[4]{ 'S', 'W', 'R', 'P' };
			uint32 _version{ 1 };
			uint32 _nameLength{ 0 };
			uint32 _frameCount{ 0 };
		};
	} // namespace

	InputReplay::InputReplay()
		: _listFrame{}
		, _replayName{}
		, _currentPlaybackIndex{ 0 }
		, _playbackSpeed{ 1.0f }
		, _accumulatedTime{ 0.0f }
		, _bRecording{ SW_FALSE }
		, _bPlaying{ SW_FALSE }
		, _bPaused{ SW_FALSE }
		, _bLoop{ SW_FALSE }
		, _reserved{ 0 }
	{
	}

	void InputReplay::startRecording( string_view replayName )
	{
		_listFrame.clear();
		_replayName			  = replayName.empty() ? "NewReplay" : string( replayName );
		_currentPlaybackIndex = 0;
		_accumulatedTime	  = 0.0f;
		_bRecording			  = SW_TRUE;
		_bPlaying			  = SW_FALSE;
		_bPaused			  = SW_FALSE;
		SW_LOG_INFO( "Started recording input replay: %#", _replayName.c_str() );
	}

	void InputReplay::recordFrame( uint32 tickNumber, float32 deltaTime, const InputSnapshot& snapshot, const vector<RawInputEvent>& listEvent )
	{
		if ( _bRecording == SW_FALSE )
			return;

		InputReplayFrame frame{};
		frame._tickNumber	= tickNumber;
		frame._deltaTime	= deltaTime;
		frame._snapshot		= snapshot;
		frame._listRawEvent = listEvent;
		_listFrame.push_back( std::move( frame ) );
	}

	void InputReplay::stopRecording()
	{
		if ( _bRecording == SW_FALSE )
			return;

		_bRecording = SW_FALSE;
		SW_LOG_INFO( "Stopped recording input replay: %# (Total %d frames recorded)", _replayName.c_str(), static_cast<int32>( _listFrame.size() ) );
	}

	void InputReplay::play()
	{
		if ( _listFrame.empty() )
			return;

		_bRecording			  = SW_FALSE;
		_bPlaying			  = SW_TRUE;
		_bPaused			  = SW_FALSE;
		_currentPlaybackIndex = 0;
		_accumulatedTime	  = 0.0f;
		SW_LOG_INFO( "Started playback of input replay: %#", _replayName.c_str() );
	}

	void InputReplay::pause()
	{
		if ( _bPlaying == SW_TRUE )
			_bPaused = SW_TRUE;
	}

	void InputReplay::resume()
	{
		if ( _bPlaying == SW_TRUE )
			_bPaused = SW_FALSE;
	}

	void InputReplay::stop()
	{
		_bPlaying			  = SW_FALSE;
		_bPaused			  = SW_FALSE;
		_currentPlaybackIndex = 0;
		_accumulatedTime	  = 0.0f;
	}

	void InputReplay::stepForward( InputManager* pInput )
	{
		if ( _listFrame.empty() )
			return;

		if ( _currentPlaybackIndex < _listFrame.size() )
		{
			const InputReplayFrame& frame = _listFrame[_currentPlaybackIndex];
			if ( pInput != nullptr )
			{
				for ( const RawInputEvent& evt : frame._listRawEvent )
					pInput->postRawEvent( evt );
			}
			++_currentPlaybackIndex;
		}
		else if ( _bLoop == SW_TRUE )
		{
			_currentPlaybackIndex = 0;
		}
	}

	void InputReplay::stepBackward( InputManager* pInput )
	{
		if ( _listFrame.empty() )
			return;

		if ( _currentPlaybackIndex > 0 )
		{
			--_currentPlaybackIndex;
			// 원시 이벤트는 KeyDown/KeyUp처럼 방향성을 가진 상태 전이라, 목표 프레임의 이벤트만
			// 그대로 재주입하면(정방향 의미) 역방향 탐색 시 눌림 상태가 고착될 수 있습니다.
			// 0번 프레임부터 목표 프레임까지 장치 상태를 리셋 후 순서대로 재생하여 재구성합니다.
			resyncUpTo( pInput, _currentPlaybackIndex + 1 );
		}
	}

	void InputReplay::resyncUpTo( InputManager* pInput, uint32 exclusiveEndIndex ) const
	{
		if ( pInput == nullptr )
			return;

		pInput->resetAllDeviceState();

		const uint32 endIndex = exclusiveEndIndex < _listFrame.size() ? exclusiveEndIndex : static_cast<uint32>( _listFrame.size() );
		for ( uint32 index = 0; index < endIndex; ++index )
		{
			const InputReplayFrame& frame = _listFrame[index];
			for ( const RawInputEvent& evt : frame._listRawEvent )
				pInput->postRawEvent( evt );
			pInput->beginFrame( frame._deltaTime );
			pInput->endFrame();
		}
	}

	void InputReplay::seek( uint32 frameIndex )
	{
		if ( _listFrame.empty() )
			return;

		if ( frameIndex >= _listFrame.size() )
			_currentPlaybackIndex = static_cast<uint32>( _listFrame.size() - 1 );
		else
			_currentPlaybackIndex = frameIndex;
	}

	void InputReplay::updatePlayback( float32 deltaTime, InputManager* pInput )
	{
		if ( _bPlaying == SW_FALSE || _bPaused == SW_TRUE || _listFrame.empty() )
			return;

		_accumulatedTime += deltaTime * _playbackSpeed;

		while ( _currentPlaybackIndex < _listFrame.size() )
		{
			const InputReplayFrame& frame = _listFrame[_currentPlaybackIndex];
			if ( _accumulatedTime < frame._deltaTime )
				break;

			_accumulatedTime -= frame._deltaTime;

			if ( pInput != nullptr )
			{
				for ( const RawInputEvent& evt : frame._listRawEvent )
					pInput->postRawEvent( evt );
			}

			++_currentPlaybackIndex;
		}

		if ( _currentPlaybackIndex >= _listFrame.size() )
		{
			if ( _bLoop == SW_TRUE )
			{
				_currentPlaybackIndex = 0;
				_accumulatedTime	  = 0.0f;
			}
			else
			{
				_bPlaying = SW_FALSE;
				SW_LOG_INFO( "Finished playback of input replay: %#", _replayName.c_str() );
			}
		}
	}

	bool InputReplay::saveToFile( string_view filePath ) const
	{
		vector<uint8> bytes;
		ReplayHeader  header{};
		header._nameLength = static_cast<uint32>( _replayName.size() );
		header._frameCount = static_cast<uint32>( _listFrame.size() );

		const uint8* pHeaderBytes = reinterpret_cast<const uint8*>( &header );
		bytes.insert( bytes.end(), pHeaderBytes, pHeaderBytes + sizeof( ReplayHeader ) );

		if ( header._nameLength > 0 )
			bytes.insert( bytes.end(), _replayName.begin(), _replayName.end() );

		for ( const InputReplayFrame& frame : _listFrame )
		{
			const uint32  tickNumber = frame._tickNumber;
			const float32 dt		 = frame._deltaTime;
			const uint8*  pTick		 = reinterpret_cast<const uint8*>( &tickNumber );
			const uint8*  pDt		 = reinterpret_cast<const uint8*>( &dt );
			bytes.insert( bytes.end(), pTick, pTick + sizeof( uint32 ) );
			bytes.insert( bytes.end(), pDt, pDt + sizeof( float32 ) );

			uint8		 arrSnapshotBuf[64]{};
			uint32		 snapSize  = frame._snapshot.serialize( arrSnapshotBuf, sizeof( arrSnapshotBuf ) );
			const uint8* pSnapSize = reinterpret_cast<const uint8*>( &snapSize );
			bytes.insert( bytes.end(), pSnapSize, pSnapSize + sizeof( uint32 ) );
			if ( snapSize > 0 )
				bytes.insert( bytes.end(), arrSnapshotBuf, arrSnapshotBuf + snapSize );

			const uint32 eventCount = static_cast<uint32>( frame._listRawEvent.size() );
			const uint8* pCount		= reinterpret_cast<const uint8*>( &eventCount );
			bytes.insert( bytes.end(), pCount, pCount + sizeof( uint32 ) );

			for ( const RawInputEvent& evt : frame._listRawEvent )
			{
				const uint8* pEvtBytes = reinterpret_cast<const uint8*>( &evt );
				bytes.insert( bytes.end(), pEvtBytes, pEvtBytes + sizeof( RawInputEvent ) );
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
		if ( FileUtil::readFile( filePath, bytes ) == false || bytes.size() < sizeof( ReplayHeader ) )
			return false;

		const uint8* pCursor = bytes.data();
		const uint8* pEnd	 = bytes.data() + bytes.size();

		ReplayHeader header{};
		std::memcpy( &header, pCursor, sizeof( ReplayHeader ) );
		pCursor += sizeof( ReplayHeader );

		if ( header._arrMagic[0] != 'S' || header._arrMagic[1] != 'W' || header._arrMagic[2] != 'R' || header._arrMagic[3] != 'P' )
			return false;

		_replayName.clear();
		if ( header._nameLength > 0 && pCursor + header._nameLength <= pEnd )
		{
			_replayName.assign( reinterpret_cast<const utf8*>( pCursor ), header._nameLength );
			pCursor += header._nameLength;
		}

		_listFrame.clear();
		_listFrame.reserve( header._frameCount );

		for ( uint32 frameIndex = 0; frameIndex < header._frameCount; ++frameIndex )
		{
			if ( pCursor + sizeof( uint32 ) + sizeof( float32 ) + sizeof( uint32 ) > pEnd )
				break;

			InputReplayFrame frame{};
			std::memcpy( &frame._tickNumber, pCursor, sizeof( uint32 ) );
			pCursor += sizeof( uint32 );
			std::memcpy( &frame._deltaTime, pCursor, sizeof( float32 ) );
			pCursor += sizeof( float32 );

			uint32 snapSize = 0;
			std::memcpy( &snapSize, pCursor, sizeof( uint32 ) );
			pCursor += sizeof( uint32 );

			if ( snapSize > 0 && pCursor + snapSize <= pEnd )
			{
				frame._snapshot.deserialize( pCursor, snapSize );
				pCursor += snapSize;
			}

			if ( pCursor + sizeof( uint32 ) > pEnd )
				break;

			uint32 eventCount = 0;
			std::memcpy( &eventCount, pCursor, sizeof( uint32 ) );
			pCursor += sizeof( uint32 );

			frame._listRawEvent.reserve( eventCount );
			for ( uint32 evtIndex = 0; evtIndex < eventCount; ++evtIndex )
			{
				if ( pCursor + sizeof( RawInputEvent ) > pEnd )
					break;

				RawInputEvent evt{};
				std::memcpy( &evt, pCursor, sizeof( RawInputEvent ) );
				pCursor += sizeof( RawInputEvent );
				frame._listRawEvent.push_back( evt );
			}

			_listFrame.push_back( std::move( frame ) );
		}

		_currentPlaybackIndex = 0;
		_accumulatedTime	  = 0.0f;
		_bRecording			  = SW_FALSE;
		_bPlaying			  = SW_FALSE;
		_bPaused			  = SW_FALSE;

		SW_LOG_INFO( "Successfully loaded replay: %# (%d frames)", _replayName.c_str(), static_cast<int32>( _listFrame.size() ) );
		return true;
	}

	void InputReplay::clear()
	{
		_listFrame.clear();
		_replayName.clear();
		_currentPlaybackIndex = 0;
		_accumulatedTime	  = 0.0f;
		_bRecording			  = SW_FALSE;
		_bPlaying			  = SW_FALSE;
		_bPaused			  = SW_FALSE;
	}

	float32 InputReplay::getTotalDuration() const
	{
		float32 totalSec = 0.0f;
		for ( const InputReplayFrame& frame : _listFrame )
			totalSec += frame._deltaTime;
		return totalSec;
	}

	float32 InputReplay::getCurrentPlaybackTime() const
	{
		float32		 currentSec = 0.0f;
		const size_t limit		= _currentPlaybackIndex < _listFrame.size() ? _currentPlaybackIndex : _listFrame.size();
		for ( size_t index = 0; index < limit; ++index )
			currentSec += _listFrame[index]._deltaTime;
		return currentSec;
	}

	const InputReplayFrame* InputReplay::getCurrentFrame() const
	{
		if ( _listFrame.empty() || _currentPlaybackIndex >= _listFrame.size() )
			return nullptr;
		return &_listFrame[_currentPlaybackIndex];
	}
} // namespace sw
