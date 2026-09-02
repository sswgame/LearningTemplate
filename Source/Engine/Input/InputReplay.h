/**
 * @file InputReplay.h
 * @brief 프레임 단위 결정론적 입력 녹화, 파일 직렬화 및 재생(Playback/Scrubbing) 시스템
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Input/Events/RawInputEvent.h"
#include "Engine/Input/InputSnapshot.h"

namespace sw
{
	class InputManager;

	/**
	 * @struct InputReplayFrame
	 * @brief 단일 틱/프레임 동안 발생한 모든 입력 스냅샷과 원시 이벤트 패킷 묶음
	 */
	struct SW_API InputReplayFrame
	{
		uint32				  _tickNumber{ 0 };
		float32				  _deltaTime{ 0.016667f };
		InputSnapshot		  _snapshot{};
		vector<RawInputEvent> _listRawEvent{};
	};

	/**
	 * @class InputReplay
	 * @brief 게임플레이 입력을 파일(.swreplay)로 녹화하고 프레임 스크러빙/재생/QA 버그 재현을 수행하는 엔진 시스템
	 */
	class SW_API InputReplay
	{
	public:
		InputReplay();
		~InputReplay() = default;

		InputReplay( const InputReplay& )				 = default;
		InputReplay& operator=( const InputReplay& )	 = default;
		InputReplay( InputReplay&& ) noexcept			 = default;
		InputReplay& operator=( InputReplay&& ) noexcept = default;

		/** @brief 입력 녹화를 시작합니다. */
		void startRecording( string_view replayName = {} );

		/** @brief 현재 프레임의 입력 스냅샷 및 이벤트를 녹화 버퍼에 추가합니다. */
		void recordFrame( uint32 tickNumber, float32 deltaTime, const InputSnapshot& snapshot, const vector<RawInputEvent>& listEvent );

		/** @brief 입력 녹화를 종료합니다. */
		void stopRecording();

		/** @brief 리플레이 재생을 시작합니다. */
		void play();

		/** @brief 리플레이 재생을 일시정지합니다. */
		void pause();

		/** @brief 일시정지된 리플레이를 다시 재생합니다. */
		void resume();

		/** @brief 리플레이 재생을 정지하고 처음으로 되돌립니다. */
		void stop();

		/** @brief 1프레임 앞으로 전진하여 해당 프레임 입력을 엔진에 주입합니다. */
		void stepForward( InputManager* pInput );

		/** @brief 1프레임 뒤로 후진합니다. */
		void stepBackward( InputManager* pInput );

		/** @brief 특정 프레임 인덱스로 이동합니다. */
		void seek( uint32 frameIndex );

		/** @brief 매 프레임 업데이트하여 저장된 입력을 InputManager로 주입합니다. */
		void updatePlayback( float32 deltaTime, InputManager* pInput );

		/** @brief 바이너리 파일(.swreplay)로 저장합니다. */
		bool saveToFile( string_view filePath ) const;

		/** @brief 바이너리 파일(.swreplay)로부터 리플레이를 로드합니다. */
		bool loadFromFile( string_view filePath );

		/** @brief 녹화된 모든 프레임 버퍼를 비웁니다. */
		void clear();

		bool	isRecording() const { return _bRecording == SW_TRUE; }
		bool	isPlaying() const { return _bPlaying == SW_TRUE; }
		bool	isPaused() const { return _bPaused == SW_TRUE; }
		bool	isLooping() const { return _bLoop == SW_TRUE; }
		void	setLooping( bool bLoop ) { _bLoop = bLoop ? SW_TRUE : SW_FALSE; }
		void	setPlaybackSpeed( float32 speed ) { _playbackSpeed = speed; }
		float32 getPlaybackSpeed() const { return _playbackSpeed; }
		uint32	getFrameCount() const { return static_cast<uint32>( _listFrame.size() ); }
		uint32	getCurrentFrameIndex() const { return _currentPlaybackIndex; }
		float32 getTotalDuration() const;
		float32 getCurrentPlaybackTime() const;

		const string&					getReplayName() const { return _replayName; }
		const InputReplayFrame*			getCurrentFrame() const;
		const vector<InputReplayFrame>& getFrames() const { return _listFrame; }

	private:
		/** @brief 모든 장치 상태를 리셋한 뒤, [0, exclusiveEndIndex) 프레임을 순서대로 재생하여 재구성합니다. */
		void resyncUpTo( InputManager* pInput, uint32 exclusiveEndIndex ) const;

		vector<InputReplayFrame> _listFrame;
		string					 _replayName;
		uint32					 _currentPlaybackIndex;
		float32					 _playbackSpeed;
		float32					 _accumulatedTime;
		uint8					 _bRecording : 1;
		uint8					 _bPlaying	 : 1;
		uint8					 _bPaused	 : 1;
		uint8					 _bLoop		 : 1;
		[[maybe_unused]] uint8	 _reserved	 : 4;
	};
} // namespace sw
