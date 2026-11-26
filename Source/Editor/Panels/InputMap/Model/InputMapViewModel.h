/**
 * @file InputMapViewModel.h
 * @brief Input Map Editor 의 모델 — 편집하는 InputMap · 리플레이, 경로, 고른 액션, 키 다시 잡기, 충돌, 실시간 그래프 표본, 녹화 · 재생 명령. ImGui 를 모릅니다.
 * @details 패널(`InputMapPanel`)은 그리기와 입력칸만 들고, 상태와 명령은 이것이 든다 — 인스펙터 · 프로파일러 `Model/` 과 같은 선이다(언리얼 Enhanced Input 편집기가
 *          `UInputMappingContext` 에셋을 편집 대상으로 두고 화면이 그것을 고치는 것과 같다). 바인딩 한 칸 고치기(트리거 · 시간 · Negate)는 엔진 `InputMap` 의 API 가 곧 명령이라
 *          `editInputMap` 으로 넘기고, 문서 dirty 는 패널(`EditorPanel`)이 든다 — 명령은 바꿨으면 편집 알림 창구(`DocumentEditRecorder`)로 알리고 true 를 반환한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/Name/hashed_string.h"

#include "Editor/Common/Widgets/EditableAssetPath.h"

#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Virtual/InputReplay.h"

// Input Map Editor 의 바인딩 충돌 찾기입니다 — 같은 레이어에서 한 입력이 두 액션을 함께 발화시키는 곳(ImGui 없음, EditorTest 가 본다).
namespace sw
{
    class InputMap;
} // namespace sw

namespace sw::editor
{
    struct InputMapConflictView;

    /** @brief 충돌 하나 — 액션 둘이 같은 레이어에서 같은 슬롯을 쓴다. */
    struct InputMapConflict
    {
        hashed_string _actionA;
        hashed_string _actionB;
        hashed_string _layer;
        InputSlot     _slot{};
        uint32        _bindIndexA{ 0 }; ///< A 의 그 바인딩 번호
        uint32        _bindIndexB{ 0 }; ///< B 의 그 바인딩 번호

        // 바인딩 충돌을 모읍니다. 슬롯 수는 `BindingKinds::getConflictSlotCount` 가 정한다(합성 축의 네 방향 · Chord 의 수식 키 포함).
        // 같은 액션 안의 겹침(Enter · Space 가 둘 다 Confirm)은 충돌이 아니다. 쌍은 액션 순서대로 한 번만 담는다.
        /** @brief @p inputMap 의 충돌을 @p outListConflict 에 담습니다(먼저 비운다). */
        static void collect( const InputMap& inputMap, vector<InputMapConflict>& outListConflict );
        /** @brief @p action 이 낀 충돌이 있으면 true 입니다. */
        static bool isInvolved( const vector<InputMapConflict>& listConflict, const hashed_string& action );
        /** @brief @p action 의 바인딩 @p bindIndex 가 낀 충돌이 있으면 true 입니다. */
        static bool isInvolved( const vector<InputMapConflict>& listConflict, const hashed_string& action, uint32 bindIndex );
        /** @brief @p action 이 낀 충돌을 그 액션 쪽에서 본 줄로 @p outListView 에 담습니다(먼저 비운다). */
        static void collectFor( const vector<InputMapConflict>& listConflict, const hashed_string& action, vector<InputMapConflictView>& outListView );
    };

    /** @brief 충돌 하나를 어느 한 액션 쪽에서 본 것입니다(상세 칸의 한 줄). */
    struct InputMapConflictView
    {
        hashed_string _otherAction;
        hashed_string _layer;
        InputSlot     _slot{};
        uint32        _bindIndex{ 0 };      ///< 보는 액션의 바인딩 번호
        uint32        _otherBindIndex{ 0 }; ///< 상대 액션의 바인딩 번호
    };
} // namespace sw::editor

namespace sw
{
    class InputManager;
} // namespace sw

namespace sw::editor
{
    class AssetWriter;

    class DocumentEditRecorder;
    class EditorListFilter;

    struct ViewportInputOverlay;

    /** @brief 키 잡기가 한 프레임에 본 키보드 입력입니다. 읽기는 패널이(ImGui), 판단은 문서가 한다. */
    struct InputMapCaptureFrame
    {
        Key   _chordKey{ Key::Unknown };      ///< 이번 프레임에 새로 눌린 수식 키가 아닌 키
        uint8 _modifierMask{ 0 };             ///< 그때 누른 수식 키(`ModifierKey`)
        Key   _pressedKey{ Key::Unknown };    ///< 이번 프레임에 새로 눌린 아무 키(`_chordKey` 가 없으면 수식 키다)
        bool  _bPendingModifierDown{ false }; ///< `getPendingCaptureModifier` 의 키가 아직 눌려 있다.
    };

    /**
     * @struct InputMapDeviceState
     * @brief Debug 탭 장치 모니터가 그리는 입력 장치 값 묶음입니다(`InputMapViewModel::gatherDeviceState`). 패널(View)은 입력 매니저를 보지 않고 이 값만 읽는다.
     */
    struct InputMapDeviceState
    {
        const utf8* _pActiveDeviceName{ "" }; ///< 활성 입력 장치 종류의 표시 이름(`InputGlyphStyle`)
        vector<Key> _listHeldKey{};           ///< 게임에 보이는 눌린 키(`InputManager::isKeyDown`), 키 번호 순
        int2        _mousePosition{};         ///< 마우스 위치(창 픽셀)
        int2        _mouseDelta{};            ///< 이번 프레임 마우스 변화량
        float2      _mouseSmoothDelta{};      ///< 부드럽게 한 변화량
        float32     _mouseWheel{ 0.0f };      ///< 휠
        const utf8* _pBatteryName{ "" };      ///< 0 번 패드 배터리 잔량의 표시 이름. 패드가 없으면 빈 글
        float2      _leftStick{};             ///< 0 번 패드 왼쪽 스틱
        float2      _rightStick{};            ///< 0 번 패드 오른쪽 스틱
        float32     _leftTrigger{ 0.0f };     ///< 0 번 패드 왼쪽 트리거
        float32     _rightTrigger{ 0.0f };    ///< 0 번 패드 오른쪽 트리거
        bool        _bMouseLeftDown{ false };
        bool        _bMouseRightDown{ false };
        bool        _bMouseMiddleDown{ false };
        bool        _bGamepadConnected{ false }; ///< 0 번 패드가 연결됐다.
    };

    /**
     * @struct InputMapPlotSamples
     * @brief Debug 탭 그래프의 최근 표본입니다(고리 버퍼, `_offset` 이 다음에 쓸 칸).
     */
    struct InputMapPlotSamples
    {
        static constexpr size_t kSampleCount = 120;

        float32 _arrLeftStickX[kSampleCount]{};
        float32 _arrLeftStickY[kSampleCount]{};
        float32 _arrMouseDeltaX[kSampleCount]{};
        float32 _arrMouseDeltaY[kSampleCount]{};
        float32 _arrTriggerL[kSampleCount]{};
        float32 _arrTriggerR[kSampleCount]{};
        uint32  _offset{ 0 };
    };

    /**
     * @class InputMapViewModel
     * @brief Input Map Editor 의 상태와 명령입니다. 파일 머리말 참고.
     */
    class InputMapViewModel
    {
    public:
        /** @brief 처음 여는 맵은 디버그 키 맵(enginedefaultassets.xml), 리플레이 파일은 editortooldefaults.json 의 `_inputReplayFile` 입니다. 읽기는 첫 `update` 전에 합니다. */
        /** @param recorder 편집 알림 창구입니다(문서 패널이 쥔다). 맵을 고친 뒤 이리 알려 문서 되돌리기 한 단계가 된다(감사 E-13). */
        explicit InputMapViewModel( const DocumentEditRecorder& recorder );

        /**
         * @brief 패널이 열려 있는 프레임마다 부릅니다. InputMap 을 호스트 입력 매니저에 붙여 갱신하고, 그래프 표본을 담고, 녹화 중이면 입력 프레임을 기록하고, 끝난 재생을 뗍니다.
         * @details ActionPhase 상태 머신 · 커맨드 히스토리 · 버퍼 만료 타이머는 `InputMap::update` 안에서만 갱신되므로 프레임마다 불러야 액션 표 · 콤보 테스터가 동작한다.
         *          입력 매니저는 모델이 찾는다 — 패널(View)은 엔진 입력 상태를 쥐지 않는다(MVVM 계약).
         */
        void update( float32 deltaTime );
        /**
         * @brief 패널이 닫히거나 내려갈 때 부릅니다. 녹화를 끝내고, 재생 중인 리플레이를 입력에서 떼고, 키 잡기를 그만둡니다.
         * @details 닫힌 패널은 `update` 이 돌지 않는다. 그대로 두면 끝난 재생을 뗄 사람이 없어 입력이 독점(`Exclusive`)된 채 멈추고, 패널이 사라지면 입력이 해제된 리플레이를 가리킨다.
         */
        void releaseInput();

        // ------------------------------------------------------------------------------
        // 1) 파일 — 성공하면 true(저장하지 않은 표시가 내려간다). 실패는 로그로 알린다
        // ------------------------------------------------------------------------------
        /** @brief 입력 맵 파일 하나(`*.input.xml`)의 접미사입니다. */
        static constexpr string_view kMapSuffix = ".input.xml";
        /** @brief @p assetPath 가 이 편집기의 문서(입력 맵 파일 하나 또는 게임의 입력 맵 묶음 — 게임 설정 파일)이면 true 입니다. */
        [[nodiscard]] static bool isDocumentPath( string_view assetPath );
        /** @brief 확정한 맵 경로에서 다시 읽습니다. 읽지 못하면 편집 중인 바인딩을 그대로 둡니다. */
        [[nodiscard]] bool reload();
        /**
         * @brief 확정한 맵 경로에 리소스의 기본 바인딩(`<InputMap>`)을 씁니다.
         * @details 경로가 게임 설정 파일(`gamesettings.xml`)이면 입력 맵 묶음을 고치는 중이다. 액션은 그것을 마지막으로 정한 파일로 돌아가고, 고치지 않은 파일은 쓰지 않습니다.
         */
        [[nodiscard]] bool saveDocument( AssetWriter& writer );
        /** @brief 경로가 맵 파일 하나가 아니라 게임의 입력 맵 묶음을 가리키면 true 입니다. */
        bool isBundlePath() const;
        /** @brief 액션을 정한 파일의 리소스 경로입니다. 파일에서 읽지 않은 액션이면 빈 글입니다. */
        string describeActionSource( const hashed_string& action ) const;

        /**
         * @brief 편집하는 맵을 저장과 같은 글로 씁니다(문서 되돌리기의 글). 형식이 담지 못하는 바인딩이 있으면 빈 글입니다.
         * @details 묶음(게임 설정 파일)이면 파일마다 몫(`InputMap::writeSourceText`)을 리소스 id · 바이트 수와 함께 이어 쓴다 — 어느 파일이 액션을 정했는지까지 한 단계로 돌린다.
         */
        string captureText() const;
        /** @brief 되돌리기가 준 글로 맵을 다시 읽습니다. 미저장 판정은 문서 패널이 글로 한다. 읽지 못하면 false 입니다(그때는 파일에서 다시 읽는다). */
        bool applyText( string_view text );
        /** @brief 묶음의 되돌리기 글(`captureText`)을 파일마다 나눕니다. 틀이 깨졌거나 비었으면 false 입니다. */
        [[nodiscard]] static bool parseBundleText( string_view text, vector<InputMapSourceText>& outListSource );

        EditableAssetPath& editInputMapPath() { return _inputMapPath; }
        EditableAssetPath& editReplayFilePath() { return _replayFilePath; }

        // ------------------------------------------------------------------------------
        // 2) 액션 · 바인딩
        // ------------------------------------------------------------------------------
        const InputMap& getInputMap() const { return _inputMap; }
        /** @name 편집 — 맵을 고치는 길은 이 함수들뿐이라 저장하지 않은 표시를 빠뜨릴 수 없다. 실제로 바뀌었으면 true 입니다.
         *  @{ */
        void resetAllBindingsToDefault();
        bool resetActionToDefault( const hashed_string& action );
        bool setLayerEnabled( const hashed_string& layer, bool bEnabled );
        bool setBindingTrigger( const hashed_string& action, uint32 bindIndex, ActionTrigger trigger );
        bool setBindingTriggerTime( const hashed_string& action, uint32 bindIndex, float32 seconds );
        bool setBindingNegate( const hashed_string& action, uint32 bindIndex, bool bNegate );
        bool setBindingTapCount( const hashed_string& action, uint32 bindIndex, uint8 tapCount );
        /** @brief 바인딩을 이미 있는 레이어로 옮깁니다. */
        bool setBindingLayer( const hashed_string& action, uint32 bindIndex, const hashed_string& layer );
        /** @brief 바인딩을 지웁니다(충돌 칸의 "상대 비우기"도 이것이다 — 키만 비우면 파일이 이름 붙이지 못하는 코드가 저장되어 다음 읽기가 맵 전체를 거절한다). */
        bool removeBinding( const hashed_string& action, uint32 bindIndex );
        /** @brief 새 액션을 만들고 고릅니다. 이름이 비었거나 이미 있으면 false 입니다. */
        bool createAction( string_view name, InputActionValueType valueType );
        /** @brief 액션을 지웁니다. 고른 액션이면 고름도 풉니다. */
        bool deleteAction( const hashed_string& action );
        /** @brief 새 레이어를 만듭니다. 이름이 비었거나 이미 있으면 false 입니다. */
        bool createLayer( string_view name, int32 priority );
        /** @} */

        /** @brief 이 이름으로 액션 · 레이어를 만들 수 있으면 true 입니다. 패널은 이것으로 만들기 단추를 끈다. */
        bool canCreateAction( string_view name ) const;
        bool canCreateLayer( string_view name ) const;
        /**
         * @brief 이 종류의 바인딩이 발화 규칙 · 시간 · 횟수 · Negate 를 파일에 쓰면 true 입니다.
         * @details 2D 합성 · 스틱 · 마우스 축 · 가상 조이스틱 · AnyKey 는 매 프레임 읽는 값이라 형식에 이 칸이 없다(`InputMapXML`). 고치게 두면 저장 뒤 말없이 사라진다.
         */
        static bool canSaveTriggerProperties( BindingKind kind );
        // ------------------------------------------------------------------------------
        // 장치 모니터 · 입력 주입 — 패널은 입력 매니저를 쥐지 않고 값 묶음을 읽고 이 명령을 낸다
        // ------------------------------------------------------------------------------
        /** @brief 호스트 입력 매니저의 장치 값을 @p outState 에 담습니다. 에디터가 서 있을 때(그리기)만 부른다. */
        static void gatherDeviceState( InputMapDeviceState& outState );
        /** @brief @p input 의 장치 값을 @p outState 에 담습니다(위의 몸 — 테스트가 지역 입력 매니저로 부른다). */
        static void gatherDeviceState( const InputManager& input, InputMapDeviceState& outState );
        /** @brief 가상 키 누름 · 뗌을 호스트 입력에 넣습니다(다음 입력 프레임에 적용된다). 큐가 차서 못 넣으면 경고합니다. */
        static void postKey( Key key, bool bDown );
        /** @brief 0 번 패드의 왼쪽 스틱 축(0 · 1)을 @p stick 으로 둡니다. 패드가 없으면 아무것도 하지 않습니다. */
        static void setVirtualStick( float2 stick );
        /** @brief 0 번 패드를 @p seconds 동안 진동시킵니다. 패드가 없으면 아무것도 하지 않습니다. */
        static void playGamepadVibration( float32 leftMotor, float32 rightMotor, float32 seconds );
        /** @brief 0 번 패드의 진동을 멈춥니다. */
        static void stopGamepadVibration();
        /** @brief 게임 뷰 입력 오버레이 설정입니다(에디터 서브시스템 `ViewportInputOverlaySettings`). 에디터가 서 있을 때만 부른다 — 없으면 멈춘다. 보기 설정이라 되돌리기에 남지 않는다. */
        static ViewportInputOverlay& editViewportOverlay();

        const string& getSelectedAction() const { return _selectedAction; }
        void          selectAction( string_view action ) { _selectedAction = action; }
        /** @brief 필터를 지난 액션을 모읍니다. 이름과 키 표기를 봅니다(@p outListVisibleAction 은 호출한 쪽이 재사용하는 버퍼). */
        void collectVisibleActions( const EditorListFilter& filter, vector<hashed_string>& outListVisibleAction ) const;
        /** @brief 액션의 지금 상태 글입니다(triggered · down · ongoing · idle). */
        const utf8* describeActionState( const hashed_string& action ) const;
        /** @brief 바인딩의 표기입니다. 단축키 · 조합은 수식 키까지, 키 하나로 못 나타내는 종류는 종류 이름입니다. 바인딩이 없으면 빈 글입니다. */
        string describeBinding( const hashed_string& action, uint32 bindIndex ) const;

        /** @brief 선택한 액션의 @p bindIndex 바인딩을 다음에 누를 키로 다시 잡기 시작합니다(키 잡기 창이 열린다). */
        void beginKeyCapture( uint32 bindIndex );
        /** @brief 선택한 액션에 다음에 누를 키로 새 바인딩을 더하기 시작합니다. 새 바인딩은 그 액션의 마지막 바인딩의 레이어(없으면 맵의 기본 레이어)에 듭니다. 고른 액션이 없으면 시작하지 않습니다. */
        void beginNewBindingCapture();
        /** @brief 키 잡기를 그만둡니다. */
        void cancelKeyCapture();
        bool isCapturingKey() const { return _bCapturingKey == SW_TRUE; }
        /** @brief 키 잡기 중 혼자 눌려 있는 수식 키입니다. 패널이 지금도 눌렸는지 물어 `advanceKeyCapture` 에 넘깁니다. 없으면 `Key::Unknown` 입니다. */
        Key getPendingCaptureModifier() const { return _pendingCaptureModifier; }
        /** @brief 이번 프레임의 키 입력으로 키 잡기를 진행합니다. 조합이 오면 끝내고, Esc 면 그만두고, 수식 키를 혼자 눌렀다 떼면 그 키로 끝냅니다. */
        void advanceKeyCapture( const InputMapCaptureFrame& frame );
        /**
         * @brief 잡은 키 + 수식 키로 끝냅니다. 바꿨으면 true 입니다. 수식 키가 없으면 단일 키, 있으면 단축키입니다(Chord 는 방아쇠만 바뀐다).
         * @details 키 감지와 버튼 격자가 모두 이 함수를 거친다. 다른 액션과 겹치면 되돌리지는 않고 경고한다. 덮어쓰기를 원할 수 있고, 충돌 칸이 고칠 길을 준다.
         */
        bool finishKeyCapture( Key newKey, uint8 modifierMask = ModifierKey::None );
        /** @brief 잡은 슬롯(패드 버튼)으로 끝냅니다. 단일 바인딩과 새 바인딩만 받습니다. 받지 않으면 잡기를 이어 가고 false 입니다. */
        bool finishSlotCapture( const InputSlot& slot );
        /** @brief 키 잡기 중 0 번 패드에서 새로 눌린 버튼이 있으면 그것으로 끝냅니다. 패널이 키 잡기 창을 그리는 동안 프레임마다 부릅니다. */
        void pollGamepadCapture();

        /** @brief 바인딩 충돌을 다시 모읍니다. Mappings 탭이 그리기 전에 부릅니다. */
        void                            refreshConflicts();
        const vector<InputMapConflict>& getConflicts() const { return _listConflict; }

        /** @brief 선택한 액션의 바인딩 수를 반환합니다. 선택한 액션이 없으면 false 입니다. */
        [[nodiscard]] bool readSelectedBindingCount( uint32& outCount ) const;
        /** @brief 선택한 액션의 0번 바인딩의 발화 규칙(`ActionTrigger` 번호)을 반환합니다. 선택한 액션이 없으면 false 입니다. */
        [[nodiscard]] bool readSelectedTrigger( uint32& outTrigger ) const;

        // ------------------------------------------------------------------------------
        // 3) 그래프 표본
        // ------------------------------------------------------------------------------
        const InputMapPlotSamples& getPlotSamples() const { return _plot; }
        bool                       isPlotPaused() const { return _bPlotPaused == SW_TRUE; }
        void                       setPlotPaused( bool bPaused ) { _bPlotPaused = bPaused ? SW_TRUE : SW_FALSE; }

        // ------------------------------------------------------------------------------
        // 4) 입력 녹화 · 재생 — 재생은 리플레이를 가상 입력으로 붙이는 것이고, 일시정지 · 탐색은 그 프레임으로 seekTo 하는 것이다
        // ------------------------------------------------------------------------------
        const InputReplay& getReplay() const { return _replay; }
        /** @brief 리플레이를 호스트 입력에 붙여 재생 중이면 true 입니다. */
        bool isReplayPlaying() const;
        /** @brief 지금 보이는 리플레이 프레임입니다(재생 중이면 진행한 프레임까지 더한다). */
        uint32 getReplayCurrentFrame() const;
        void   startRecording();
        void   stopRecording() { _replay.stopRecording(); }
        /** @brief 처음부터(끝에 있었으면 되감아) 재생합니다. */
        void playReplay();
        /** @brief 리플레이를 @p frame 의 상태로 맞춥니다(일시정지 · 탐색 — 상태를 재현한 뒤 뗀다). */
        void seekReplay( uint32 frame );
        /** @brief 확정한 리플레이 경로에 씁니다. 실패는 로그로 알립니다. */
        void saveReplay() const;
        /** @brief 확정한 리플레이 경로에서 읽습니다. 재생 중이면 먼저 뗍니다. 실패는 로그로 알립니다. */
        void loadReplay();

    private:
        /** @brief 호스트의 입력 매니저입니다. 에디터가 서 있을 때만 부른다 — 없으면 멈춘다. 모델 안에서만 쓴다(패널은 쥐지 않는다). */
        static InputManager& getInputManager();
        /** @brief 이번 프레임의 스틱 · 트리거 · 마우스 변화량을 그래프 고리 버퍼에 담습니다. */
        void sampleInput( InputManager& input );
        /** @brief 묶음의 파일마다 고친 몫을 원래 자리에 씁니다. */
        [[nodiscard]] bool saveBundle( AssetWriter& writer ) const;
        /** @brief @p bChanged 면 편집을 알리고(`DocumentEditRecorder` — 미저장 · 되돌리기는 문서 패널이 든다) 그대로 돌려줍니다. */
        bool recordEdit( bool bChanged );
        /** @brief 이 바인딩의 발화 칸을 고쳐도 저장에 남으면 true 입니다(`canSaveTriggerProperties`). */
        bool canEditTriggerProperties( const hashed_string& action, uint32 bindIndex ) const;
        /** @brief 지금 키 잡기가 패드 버튼을 받을 수 있으면 true 입니다. */
        bool canCaptureSlot() const;
        /** @brief 새 바인딩이 들어갈 레이어입니다. 마지막 바인딩의 레이어, 없으면 맵의 기본 레이어입니다. */
        hashed_string findNewBindingLayer( const hashed_string& action ) const;
        /** @brief 잡은 키로 새 바인딩을 더합니다. @p outBindIndex 는 새 번호입니다. */
        bool bindCapturedKey( const hashed_string& action, Key key, uint8 modifierMask, uint32& outBindIndex );
        /** @brief 잡은 키로 바인딩 @p bindIndex 를 바꿉니다. */
        bool rebindCapturedKey( const hashed_string& action, uint32 bindIndex, Key key, uint8 modifierMask );
        /** @brief 잡기로 바뀐 바인딩의 겹침을 알리고 저장하지 않은 표시를 켭니다. */
        bool recordCapturedBinding( const hashed_string& action, uint32 bindIndex );

    private:
        const DocumentEditRecorder* _pRecorder; ///< 편집 알림 창구(문서 패널이 쥔다)
        InputMap                    _inputMap;
        InputReplay                 _replay;
        Subscription                _replayPlayback; ///< 재생 중인 `_replay` 의 붙임이다. 풀면 재생이 멈춘다(모델이 사라질 때도)
        vector<InputMapConflict>    _listConflict;   ///< 이번 프레임의 바인딩 충돌이다. Mappings 탭이 다시 모은다
        EditableAssetPath           _inputMapPath;   ///< 편집하는 InputMap 리소스. 처음 값은 enginedefaultassets 의 디버그 키 맵, 칸에서 고친 글은 확정해야 읽기 · 쓰기에 쓰인다
        EditableAssetPath           _replayFilePath; ///< Save · Load 가 쓰는 .swreplay 리소스. 처음 값은 editortooldefaults 의 `_inputReplayFile`
        string                      _selectedAction;
        InputMapPlotSamples         _plot;
        uint32                      _recordedBeginFrameCount; ///< 마지막으로 녹화한 입력 프레임(`InputManager::getBeginFrameCount`)이다. 같은 프레임을 두 번 기록하지 않는다
        uint32                      _capturingBindIndex;      ///< 키 잡기 중인 바인딩 번호. `_bCapturingKey` 이고 새 바인딩이 아닐 때만 의미가 있다
        Key                         _pendingCaptureModifier;  ///< 키 잡기 중 혼자 눌린 수식 키. 없으면 `Key::Unknown`
        uint8                       _bCapturingKey      : 1;
        uint8                       _bCaptureNewBinding : 1; ///< 키 잡기가 있는 바인딩을 바꾸지 않고 새 바인딩을 더한다
        uint8                       _bPlotPaused        : 1;
        [[maybe_unused]] uint8      _reserved           : 5;
    };
} // namespace sw::editor
