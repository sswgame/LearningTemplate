/**
 * @file InputReplay.h
 * @brief 입력 계층 녹화입니다. 프레임마다 장치에 적용된 원시 이벤트를 .swreplay 로 기록하고, 가상 입력 원천으로 프레임 번호 그대로 재생합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Input/RawInputEvent.h"
#include "Engine/Input/Virtual/IVirtualInputSource.h"

namespace sw
{
    class InputManager;

    /**
     * @struct InputReplayFrame
     * @brief 한 프레임에 장치에 적용된 원시 입력 이벤트 묶음입니다. 몇 번째 프레임인지는 목록에서의 위치로 정해지며, 녹화를 시작한 뒤 `recordFrame` 을 부른 순서입니다.
     */
    struct SW_API InputReplayFrame
    {
        float32               _deltaTime{ 0.016667f }; ///< 녹화 때의 프레임 시간입니다. 표시와 길이 계산에만 쓰고, 재생은 프레임 번호로 합니다.
        vector<RawInputEvent> _listRawEvent{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class InputReplay
     * @brief 입력 계층 QA 녹화입니다(키 바인딩과 포커스까지 재현). 게임플레이 리플레이와 네트워크가 싣는 것은 행동(의도)이지 이것이 아닙니다.
     * @details 재생은 `InputManager::attachVirtualInput( &replay )` 로 붙여서 한다. 녹화한 n 번째 프레임의 이벤트를 붙인 뒤 n 번째 `beginFrame` 에 낸다
     *          (벽시계와 프레임 속도에는 영향받지 않는다). 일시정지는 떼는 것이고, 탐색은 `seekTo` 이다.
     */
    class SW_API InputReplay final : public IVirtualInputSource
    {
    public:
        InputReplay();
        ~InputReplay() override = default;

        InputReplay( const InputReplay& )                = default;
        InputReplay& operator=( const InputReplay& )     = default;
        InputReplay( InputReplay&& ) noexcept            = default;
        InputReplay& operator=( InputReplay&& ) noexcept = default;

        /** @brief 입력 녹화를 시작합니다. 있던 프레임은 지우며, 프레임은 `recordFrame` 을 부른 순서입니다. */
        void startRecording( string_view replayName = {} );
        /** @brief 이번 프레임에 장치에 적용된 원시 이벤트를 녹화합니다(`InputManager::beginFrame` 뒤에 `getLastFrameEvents` 를 넘깁니다). */
        void recordFrame( float32 deltaTime, const vector<RawInputEvent>& listEvent );
        /** @brief 입력 녹화를 마칩니다. */
        void stopRecording();

        void emitFrame( uint32 frameIndex, vector<RawInputEvent>& outListEvent ) override;
        bool isFinished( uint32 frameIndex ) const override { return frameIndex + _startFrameIndex >= getFrameCount(); }

        /**
         * @brief 장치 상태를 지우고 녹화한 [0, @p frameIndex) 프레임을 순서대로 재생해 그 프레임 직전 상태를 만듭니다(되감기와 탐색).
         * @details 붙어 있던 가상 입력은 뗀다. 다음에 상태를 지우지 않고 붙이면(`attachVirtualInput( &replay, mode, false )`) @p frameIndex 부터 낸다.
         */
        void seekTo( InputManager& input, uint32 frameIndex );

        /** @brief 바이너리 파일(.swreplay)로 저장합니다. */
        [[nodiscard]] bool saveToFile( string_view filePath ) const;
        /** @brief 바이너리 파일(.swreplay)에서 읽습니다. 버전이 다르거나 잘렸으면 false 를 반환하고 기존 상태는 그대로 둡니다. */
        [[nodiscard]] bool loadFromFile( string_view filePath );

        /** @brief 녹화된 모든 프레임을 비웁니다. */
        void clear();

        bool   isRecording() const { return _bRecording == SW_TRUE; }
        uint32 getFrameCount() const { return static_cast<uint32>( _listFrame.size() ); }
        /** @brief 붙였을 때 원천 프레임 0 이 내는 녹화 프레임 번호입니다(`seekTo` 가 정합니다). */
        uint32  getStartFrameIndex() const { return _startFrameIndex; }
        float32 getTotalDuration() const;

        const string&                   getReplayName() const { return _replayName; }
        const vector<InputReplayFrame>& getFrames() const { return _listFrame; }

    private:
        vector<InputReplayFrame> _listFrame;
        string                   _replayName;
        uint32                   _startFrameIndex;
        uint8                    _bRecording : 1;
        [[maybe_unused]] uint8   _reserved   : 7;
    };
} // namespace sw
