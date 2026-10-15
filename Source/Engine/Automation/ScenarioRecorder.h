/**
 * @file ScenarioRecorder.h
 * @brief 실행 중 입력을 녹화해 자동화 시나리오(`.scenario.xml`)의 시간 순 입력 단계로 바꿉니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Input/Virtual/InputReplay.h"

namespace sw
{
    class InputManager;

    /** @struct ScenarioRecordSettings @brief 녹화를 시나리오 글로 바꿀 때 쓰는 값입니다. */
    struct ScenarioRecordSettings
    {
        string  _name{ "recorded" }; ///< 루트 `name` 속성입니다.
        float32 _fixedDelta{ 1.0f / 60.0f };
        uint32  _clientWidth{ 0 }; ///< 마우스 위치를 비율로 바꿀 창 클라이언트 크기입니다. 0 이면 `MousePosition` 을 쓰지 않습니다.
        uint32  _clientHeight{ 0 };
        uint32  _firstFrame{ 1 }; ///< 녹화 프레임 0 이 놓일 시나리오 프레임입니다.
    };
} // namespace sw

namespace sw
{
    /** @struct ScenarioRecordOutput @brief 변환 결과입니다. */
    struct ScenarioRecordOutput
    {
        string _xmlText;
        uint32 _writtenStepCount{ 0 };
        uint32 _skippedEventCount{ 0 }; ///< 시나리오 단계로 나타낼 수 없어 건너뛴 원시 이벤트 수입니다(조합 중 글자 · 포커스 · 포인터 출입 · 패드 연결 등).
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScenarioRecorder
     * @brief `InputReplay` 로 프레임별 원시 입력을 녹화하고, 멈출 때 시나리오 입력 단계(`Press` · `Release` · `MousePosition` · `MouseDelta` · `GamepadAxis` · `Text`)로 씁니다.
     * @details 키 반복(`_bRepeat`)은 누름이 이어지는 것이라 쓰지 않는다. 한 프레임의 마우스 이동은 마지막 위치 하나, 원시 이동량은 합 하나로 줄인다.
     *          가상 입력(`_bSynthetic`)은 시나리오 재생이 낸 것이라 다시 녹화하지 않는다.
     */
    class SW_API ScenarioRecorder
    {
    public:
        /** @brief 녹화를 시작합니다. 있던 녹화는 버립니다. */
        void start();
        /** @brief 입력 프레임마다 한 번 부릅니다. 같은 입력 프레임에 두 번 불러도 한 번만 기록합니다. */
        void update( const InputManager& input, float32 deltaTime );
        /** @brief 녹화를 멈추고 시나리오 글로 바꿉니다. 녹화 중이 아니면 빈 결과입니다. */
        ScenarioRecordOutput stop( const ScenarioRecordSettings& settings );

        bool   isRecording() const { return _replay.isRecording(); }
        uint32 getFrameCount() const { return _replay.getFrameCount(); }

        /** @brief 녹화한 프레임을 시나리오 XML 글로 바꿉니다(순수 함수). */
        static ScenarioRecordOutput toScenarioXML( const vector<InputReplayFrame>& listFrame, const ScenarioRecordSettings& settings );
        /** @brief 상대 경로이고 `Saved/Automation/` 아래이며 `..` 이 없고 `.scenario.xml` 로 끝나면 true 입니다. */
        static bool isAllowedOutputPath( string_view path );

    private:
        InputReplay _replay;
        uint32      _lastBeginFrameCount{ 0 };
        bool        _bHasLastBeginFrame{ false };
    };
} // namespace sw
