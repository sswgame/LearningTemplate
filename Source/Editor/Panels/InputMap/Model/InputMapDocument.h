/**
 * @file InputMapDocument.h
 * @brief Input Map Editor 의 모델 — 편집하는 InputMap · 리플레이, 경로, 고른 액션, 키 다시 잡기, 충돌, 실시간 그래프 표본, 녹화 · 재생 명령. ImGui 를 모릅니다.
 * @details 패널(`InputMapPanel`)은 그리기와 입력칸만 들고, 상태와 명령은 이것이 든다 — 인스펙터 · 프로파일러 `Model/` 과 같은 선이다(언리얼 Enhanced Input 편집기가
 *          `UInputMappingContext` 에셋을 편집 대상으로 두고 화면이 그것을 고치는 것과 같다). 바인딩 한 칸 고치기(트리거 · 시간 · Negate)는 엔진 `InputMap` 의 API 가 곧 명령이라
 *          `editInputMap` 으로 넘기고, 문서 dirty 는 패널(`IEditorPanel`)이 든다 — 명령은 바꿨으면 true 를 반환한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Widgets/EditableAssetPath.h"
#include "Editor/Panels/InputMap/Model/InputMapConflicts.h"

#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Virtual/InputReplay.h"

namespace sw
{
    class InputManager;
} // namespace sw

namespace sw::editor
{
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
     * @class InputMapDocument
     * @brief Input Map Editor 의 상태와 명령입니다. 파일 머리말 참고.
     */
    class InputMapDocument
    {
    public:
        /** @brief 처음 여는 맵은 디버그 키 맵(enginedefaultassets.xml), 리플레이 파일은 editortooldefaults.json 의 `_inputReplayFile` 입니다. 읽기는 첫 `tick` 전에 합니다. */
        InputMapDocument();

        /**
         * @brief 패널이 열려 있는 프레임마다 부릅니다. InputMap 을 @p input 에 붙여 갱신하고, 그래프 표본을 담고, 녹화 중이면 입력 프레임을 기록하고, 끝난 재생을 뗍니다.
         * @details ActionPhase 상태 머신 · 커맨드 히스토리 · 버퍼 만료 타이머는 `InputMap::update` 안에서만 갱신되므로 프레임마다 불러야 액션 표 · 콤보 테스터가 동작한다.
         */
        void tick( InputManager& input, float32 deltaTime );

        // ------------------------------------------------------------------------------
        // 1) 파일 — 성공하면 true(패널이 dirty 를 내린다). 실패는 로그로 알린다
        // ------------------------------------------------------------------------------
        /** @brief 아직 한 번도 읽지 않았으면 true 입니다. 패널은 첫 그리기에서 `reload` 를 부릅니다. */
        bool needsFirstLoad() const { return _bLoaded == SW_FALSE; }
        /** @brief 확정한 맵 경로에서 다시 읽습니다. 읽지 못하면 편집 중인 바인딩을 그대로 둡니다. */
        [[nodiscard]] bool reload();
        /** @brief 확정한 맵 경로에 리소스의 기본 바인딩(`<InputMap>`)을 씁니다. */
        [[nodiscard]] bool save() const;

        EditableAssetPath& editInputMapPath() { return _inputMapPath; }
        EditableAssetPath& editReplayFilePath() { return _replayFilePath; }

        // ------------------------------------------------------------------------------
        // 2) 액션 · 바인딩
        // ------------------------------------------------------------------------------
        const InputMap& getInputMap() const { return _inputMap; }
        /** @brief 바인딩 한 칸을 고치는 엔진 API 가 곧 명령이라 그대로 넘깁니다. 고친 뒤 dirty 는 부르는 쪽이 올립니다. */
        InputMap& editInputMap() { return _inputMap; }

        const string& getSelectedAction() const { return _selectedAction; }
        void          selectAction( string_view action ) { _selectedAction = action; }
        /** @brief 새 액션을 만듭니다. @p valueTypeIndex 는 0 Boolean, 1 Axis1D, 2 Axis2D 입니다. 이름이 비면 false 입니다. */
        bool createAction( string_view name, int32 valueTypeIndex );

        /** @brief 선택한 액션의 @p bindIndex 바인딩을 다음에 누를 키로 다시 잡기 시작합니다(키 잡기 창이 열린다). */
        void beginKeyCapture( uint32 bindIndex );
        /** @brief 키 잡기를 그만둡니다. */
        void cancelKeyCapture() { _bCapturingKey = SW_FALSE; }
        bool isCapturingKey() const { return _bCapturingKey == SW_TRUE; }
        /**
         * @brief 잡은 키로 선택한 액션의 바인딩을 바꾸고 키 잡기를 끝냅니다. 바꿨으면 true 입니다.
         * @details 키 감지와 버튼 격자가 모두 이 함수를 거친다. 이미 다른 액션이 쓰는 키이면(`InputMap::hasBindingConflict`) 되돌리지는 않고
         *          무엇과 부딪히는지 알린다. 덮어쓰기를 원할 수 있기 때문이다.
         */
        bool finishKeyCapture( Key newKey );

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
        /** @brief 리플레이를 @p input 에 붙여 재생 중이면 true 입니다. */
        bool isReplayPlaying( const InputManager& input ) const;
        /** @brief 지금 보이는 리플레이 프레임입니다(재생 중이면 진행한 프레임까지 더한다). */
        uint32 getReplayCurrentFrame( const InputManager& input ) const;
        void   startRecording( const InputManager& input );
        void   stopRecording() { _replay.stopRecording(); }
        /** @brief 처음부터(끝에 있었으면 되감아) 재생합니다. */
        void playReplay( InputManager& input );
        void seekReplay( InputManager& input, uint32 frame ) { _replay.seekTo( input, frame ); }
        /** @brief 확정한 리플레이 경로에 씁니다. 실패는 로그로 알립니다. */
        void saveReplay() const;
        /** @brief 확정한 리플레이 경로에서 읽습니다. 재생 중이면 먼저 뗍니다. 실패는 로그로 알립니다. */
        void loadReplay( InputManager& input );

    private:
        /** @brief 이번 프레임의 스틱 · 트리거 · 마우스 변화량을 그래프 고리 버퍼에 담습니다. */
        void sampleInput( InputManager& input );

    private:
        InputMap                 _inputMap;
        InputReplay              _replay;
        vector<InputMapConflict> _listConflict;   ///< 이번 프레임의 바인딩 충돌이다. Mappings 탭이 다시 모은다
        EditableAssetPath        _inputMapPath;   ///< 편집하는 InputMap 리소스. 처음 값은 enginedefaultassets 의 디버그 키 맵, 칸에서 고친 글은 확정해야 읽기 · 쓰기에 쓰인다
        EditableAssetPath        _replayFilePath; ///< Save · Load 가 쓰는 .swreplay 리소스. 처음 값은 editortooldefaults 의 `_inputReplayFile`
        string                   _selectedAction;
        InputMapPlotSamples      _plot;
        uint32                   _recordedBeginFrameCount; ///< 마지막으로 녹화한 입력 프레임(`InputManager::getBeginFrameCount`)이다. 같은 프레임을 두 번 기록하지 않는다
        uint32                   _capturingBindIndex;      ///< 키 잡기 중인 바인딩 번호. `_bCapturingKey` 일 때만 의미가 있다
        uint8                    _bLoaded       : 1;
        uint8                    _bCapturingKey : 1;
        uint8                    _bPlotPaused   : 1;
        [[maybe_unused]] uint8   _reserved      : 5;
    };
} // namespace sw::editor
