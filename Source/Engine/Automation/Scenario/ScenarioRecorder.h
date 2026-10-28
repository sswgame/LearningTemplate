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
    class GameObjectManager;
    class InputManager;
    class InputMap;

    /**
     * @struct ScenarioRecordCheckpoint
     * @brief 녹화를 멈출 때 읽은 탐침 값 하나입니다. 시나리오 끝에 `<Expect>` 로 실려 녹화가 스스로를 검증합니다(같은 입력이면 같은 값 — 결정적 재생).
     */
    struct ScenarioRecordCheckpoint
    {
        string _probe{};
        string _target{};
        string _key{};
        string _value{}; ///< 읽은 값(수는 끝의 0 을 정리한 글)
        bool   _bText{ false };
    };
} // namespace sw

namespace sw
{
    /** @struct ScenarioRecordSettings @brief 녹화를 시나리오 글로 바꿀 때 쓰는 값입니다. */
    struct ScenarioRecordSettings
    {
        /**
         * @brief 단추 단계를 액션 단계로 쓸 때 거꾸로 찾는 입력 맵입니다. nullptr 이면 키 · 단추 단계(`Press` · `Release`)만 씁니다.
         * @details `ScenarioRecorder::stop` 이 녹화한 입력 관리자의 맵으로 채운다. `toScenarioXML` 을 직접 부르는 쪽(테스트)은 직접 넣는다.
         */
        const InputMap*                  _pInputMap{ nullptr };
        vector<ScenarioRecordCheckpoint> _listCheckpoint{};   ///< 끝 프레임에 `<Expect>` 로 쓸 값입니다(수는 `near`, 글은 `is`).
        string                           _name{ "recorded" }; ///< 루트 `name` 속성입니다.
        float32                          _fixedDelta{ 1.0f / 60.0f };
        float32                          _checkpointTolerance{ 1.0e-3f }; ///< 수 검사점의 `tolerance` 입니다.
        uint32                           _clientWidth{ 0 };               ///< 마우스 위치를 비율로 바꿀 창 클라이언트 크기입니다. 0 이면 `MousePosition` 을 쓰지 않습니다.
        uint32                           _clientHeight{ 0 };
        uint32                           _firstFrame{ 1 };                 ///< 녹화 프레임 0 이 놓일 시나리오 프레임입니다.
        bool                             _bIncludeSynthetic{ false };      ///< 가상 입력(시나리오 재생이 낸 것)도 씁니다. 시나리오로 돌린 플레이를 다시 녹화해 왕복을 확인할 때 켭니다.
        bool                             _bStartWhenScenePlaying{ false }; ///< 루트를 `startAfter="ScenePlaying"` 로 씁니다(녹화를 씬 플레이 첫 프레임에 시작했을 때). 거짓이면 `Immediately` 입니다.
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
     * @details 눌린 단추가 입력 맵의 어떤 액션이 재생 때 누를 단추(그 장치의 첫 단추 바인딩)와 같으면 액션 단계(`ActionPress` · `ActionRelease`)로 쓴다.
     *          재생은 같은 단추를 누르므로 결과가 같고, 키 배치를 바꿔도 녹화가 산다. 어느 액션의 그 자리도 아닌 단추는 키 · 단추 단계로 남는다.
     *          패드 스틱도 같다 — 어떤 액션의 첫 패드 축 바인딩(같은 패드 · 같은 스틱의 2D 바인딩)이면 값이 바뀔 때마다 앞 구간을 `ActionAxis`(값, 구간 길이)로 쓰고, 아니면 `GamepadAxis` 로 남는다.
     * @details 키 반복(`_bRepeat`)은 누름이 이어지는 것이라 쓰지 않는다. 한 프레임의 마우스 이동은 마지막 위치 하나, 원시 이동량은 합 하나로 줄인다.
     *          가상 입력(`_bSynthetic`)은 시나리오 재생이 낸 것이라 기본으로 쓰지 않는다(`_bIncludeSynthetic` 이면 쓴다 — 왕복 확인).
          검사점(`_listCheckpoint`)이 있으면 마지막 녹화 프레임에 `<Expect>` 로, 그다음 프레임에 `<Pass/>` 를 쓴다.
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
        /**
         * @brief 작업 폴더의 `Saved` 를 붙이는 디스크 도메인입니다. 녹화는 디스크 경로 `Saved/Automation/a.scenario.xml` 로 쓰고 id `saved/Automation/a.scenario.xml` 로 다시 읽습니다.
         * @details 시나리오 입구는 리소스 id 만 받는다. Godot 의 `user://` 처럼 사용자 데이터 폴더를 도메인으로 붙인 자리다.
         */
        static constexpr string_view kSavedDomain = "saved";
        /** @brief @p workingFolder 아래 `Saved` 를 `kSavedDomain` 으로 붙입니다(없으면 만든다). 못 붙이면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool mountSavedDomain( string_view workingFolder );
        /** @brief 녹화 경로(`isAllowedOutputPath`)를 다시 돌릴 때의 리소스 id 로 바꿉니다. 허락되지 않은 경로면 빈 문자열입니다. */
        static string makeReplayID( string_view outputPath );
        /**
         * @brief 검사점 하나(`탐침`, `탐침@대상`, `탐침@대상@키`)를 지금 값으로 읽습니다. 모르는 탐침이거나 값이 없으면 false 와 이유입니다.
         * @details 녹화를 멈출 때 부른다. 그 프레임의 씬 틱 뒤 값이라 재생에서도 같은 프레임 끝에 같은 값이 나와야 한다.
         */
        [[nodiscard]] static bool readCheckpoint( string_view spec, const GameObjectManager* pManager, ScenarioRecordCheckpoint& outCheckpoint, string& outError );

    private:
        InputReplay     _replay;
        const InputMap* _pInputMap{ nullptr }; ///< 마지막 `update` 의 입력 관리자 맵 — `stop` 이 단추를 액션으로 거꾸로 찾는다
        uint32          _lastBeginFrameCount{ 0 };
        bool            _bHasLastBeginFrame{ false };
    };
} // namespace sw
