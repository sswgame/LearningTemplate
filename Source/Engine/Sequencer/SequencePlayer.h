/**
 * @file SequencePlayer.h
 * @brief SequenceAsset 타임라인을 프레임 단위로 재생합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Sequencer/SequenceAsset.h"

namespace sw
{
	/**
	 * @class SequencePlayer
	 * @brief fps로 프레임을 진행하고 collectActiveItems로 현재 클립/이벤트를 조회합니다.
	 */
	class SW_API SequencePlayer
	{
	public:
		SequencePlayer();

		/** @brief JSON 시퀀스를 로드합니다. */
		bool loadFromFile( string_view path );
		/** @brief 이미 파싱된 애셋을 설정합니다. */
		void setAsset( const SequenceAsset& asset );

		void play();
		void playFromFrame( int32 frame );
		void stop();
		void pause();
		void resume();
		void update( float32 deltaSeconds );

		void  setFramesPerSecond( float32 fps );
		void  setLoop( bool bLoop );
		void  seekToFrame( int32 frame );
		int32 getCurrentFrame() const;
		int32 getPreviousFrame() const { return _previousFrame; }
		bool  isPlaying() const { return _bPlaying; }
		bool  isPaused() const { return _bPaused; }

		void				 collectActiveItems( vector<const SequenceTrackItem*>& outItemList ) const;
		const SequenceAsset& getAsset() const { return _asset; }

	private:
		int32 computeFrame( float32 timeSeconds ) const;

		SequenceAsset _asset;
		float32		  _framesPerSecond;
		float32		  _playbackTime;
		int32		  _previousFrame;
		bool		  _bPlaying;
		bool		  _bPaused;
		bool		  _bLoop;
	};
} // namespace sw
