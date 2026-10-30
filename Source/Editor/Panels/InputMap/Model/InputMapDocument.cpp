#include "pch.h"

#include "Editor/Panels/InputMap/Model/InputMapDocument.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/KeyCodeUtil.h"

namespace sw::editor
{
    SW_LOG_CALLER( "InputMapDocument" );

    InputMapDocument::InputMapDocument()
        : _inputMap{}
        , _replay{}
        , _listConflict{}
        , _inputMapPath{ getSubsystem<const EngineDefaultAssets>()._debugKeyInputMap } // 처음 여는 맵은 디버그 키 맵이다 — 경로의 원본은 enginedefaultassets.xml
        , _replayFilePath{ getEditorToolDefaults()._inputReplayFile }                  // 처음 가리키는 파일 — 원본은 editortooldefaults.json
        , _selectedAction{ "" }
        , _plot{}
        , _recordedBeginFrameCount{ 0 }
        , _capturingBindIndex{ 0 }
        , _bLoaded{ SW_FALSE }
        , _bCapturingKey{ SW_FALSE }
        , _bPlotPaused{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void InputMapDocument::tick( InputManager& input, float32 deltaTime )
    {
        if ( _inputMap.getInputManager() != &input )
            _inputMap.setInputManager( &input );
        _inputMap.update( deltaTime );

        if ( _bPlotPaused == SW_FALSE )
            sampleInput( input );

        // 입력 녹화는 입력 프레임마다 한 번, 그 프레임에 장치에 적용된 이벤트를 기록한다.
        const bool bNewRecordingFrame = _replay.isRecording() && input.getBeginFrameCount() != _recordedBeginFrameCount;
        if ( bNewRecordingFrame )
        {
            _recordedBeginFrameCount = input.getBeginFrameCount();
            _replay.recordFrame( deltaTime, input.getLastFrameEvents() );
        }
        // 재생(가상 입력으로 붙인 리플레이)이 끝나면 해제한다.
        const bool bReplayFinished = input.getVirtualInput() == &_replay && _replay.isFinished( input.getVirtualFrameIndex() );
        if ( bReplayFinished )
            input.detachVirtualInput();
    }

    void InputMapDocument::sampleInput( InputManager& input )
    {
        // 실시간 시계열 샘플링
        float32        lx = 0.0f, ly = 0.0f, lt = 0.0f, rt = 0.0f;
        GamepadDevice* pGamepad = input.getGamepad( 0 );
        if ( pGamepad != nullptr && pGamepad->isConnected() )
        {
            const float2 vecLeftStick1 = pGamepad->getLeftStick();
            lx                         = vecLeftStick1._x;
            ly                         = vecLeftStick1._y;
            lt                         = pGamepad->getLeftTrigger();
            rt                         = pGamepad->getRightTrigger();
        }

        const int2   vecMouseDelta1 = input.getMouseDelta();
        const uint32 offset         = _plot._offset;

        _plot._arrLeftStickX[offset]  = lx;
        _plot._arrLeftStickY[offset]  = ly;
        _plot._arrMouseDeltaX[offset] = static_cast<float32>( vecMouseDelta1._x );
        _plot._arrMouseDeltaY[offset] = static_cast<float32>( vecMouseDelta1._y );
        _plot._arrTriggerL[offset]    = lt;
        _plot._arrTriggerR[offset]    = rt;

        _plot._offset = ( offset + 1 ) % InputMapPlotSamples::kSampleCount;
    }

    bool InputMapDocument::reload()
    {
        _bLoaded = SW_TRUE;
        // 읽지 못하면 편집 중인 바인딩과 dirty 를 그대로 둔다. dirty 를 지우고 "다시 읽었다" 고 표시하지 않는다.
        if ( _inputMap.loadFromAsset( AssetID{ _inputMapPath.c_str() } ) == false )
        {
            SW_LOG_ERROR( "Could not reload InputMap from %# - keeping the edited bindings", _inputMapPath.c_str() );
            return false;
        }
        SW_LOG_INFO( "Reloaded InputMap from %#", _inputMapPath.c_str() );
        return true;
    }

    bool InputMapDocument::save() const
    {
        // 패널은 리소스의 기본 바인딩(`<InputMap>`)을 편집한다. 다시 읽기(`reload`)와 같은 형식, 같은 위치에 쓴다.
        // 플레이어 리매핑은 UserSettings 의 `keyBinding` 설정이 사용자 파일에 보관한다.
        const std::optional<AbsolutePath> inputMapFile = editor::getSubsystem<AssetPaths>().resolveWritePath( _inputMapPath.c_str() );
        if ( inputMapFile.has_value() == false || _inputMap.saveToAsset( *inputMapFile ) == false )
        {
            SW_LOG_WARNING( "Failed to save InputMap to %#", _inputMapPath.c_str() );
            return false;
        }
        SW_LOG_INFO( "Saved InputMap to %#", _inputMapPath.c_str() );
        return true;
    }

    bool InputMapDocument::createAction( string_view name, int32 valueTypeIndex )
    {
        if ( name.empty() )
            return false;
        static constexpr InputActionValueType kArrValueType[] = { InputActionValueType::Boolean, InputActionValueType::Axis1D, InputActionValueType::Axis2D };
        const InputActionValueType            valueType       = kArrValueType[MathUtil::clamp( valueTypeIndex, 0, 2 )];
        _inputMap.createAction( sw::hashed_string( string( name ).c_str() ), valueType );
        return true;
    }

    void InputMapDocument::beginKeyCapture( uint32 bindIndex )
    {
        _capturingBindIndex = bindIndex;
        _bCapturingKey      = SW_TRUE;
    }

    bool InputMapDocument::finishKeyCapture( Key newKey )
    {
        _bCapturingKey = SW_FALSE;
        // 어느 레이어에서 충돌을 따져야 하는지는 그 바인딩이 직접 안다.
        const sw::ActionBinding* pBinding = _inputMap.getBinding( sw::hashed_string( _selectedAction.c_str() ), _capturingBindIndex );
        const string_view        layer    = ( pBinding != nullptr ) ? pBinding->_layer.view() : string_view{};

        sw::string conflictingAction;
        if ( _inputMap.hasBindingConflict( sw::InputSlot::fromKey( newKey ), sw::hashed_string( layer ), conflictingAction ) && conflictingAction != _selectedAction )
        {
            SW_LOG_WARNING( "Binding '%#' to %# - conflicts with '%#' on the same layer.",
                            _selectedAction.c_str(), sw::KeyCodeUtil::toName( newKey ), conflictingAction.c_str() );
        }

        // 키 하나로 바꿀 수 없는 바인딩(합성 축, 스틱)이면 바뀌지 않는다. 경고는 InputMap 이 남긴다. 이때는 문서를 dirty 로 표시하지 않는다.
        return _inputMap.rebindKey( sw::hashed_string( _selectedAction.c_str() ), newKey, _capturingBindIndex );
    }

    void InputMapDocument::refreshConflicts()
    {
        InputMapConflicts::collect( _inputMap, _listConflict );
    }

    bool InputMapDocument::readSelectedBindingCount( uint32& outCount ) const
    {
        if ( _selectedAction.empty() )
            return false;
        outCount = _inputMap.getBindingCount( hashed_string( _selectedAction.c_str() ) );
        return true;
    }

    bool InputMapDocument::readSelectedTrigger( uint32& outTrigger ) const
    {
        if ( _selectedAction.empty() || _inputMap.getBindingCount( hashed_string( _selectedAction.c_str() ) ) == 0 )
            return false;
        outTrigger = static_cast<uint32>( _inputMap.getBindingTrigger( hashed_string( _selectedAction.c_str() ), 0 ) );
        return true;
    }

    bool InputMapDocument::isReplayPlaying( const InputManager& input ) const
    {
        return input.getVirtualInput() == &_replay;
    }

    uint32 InputMapDocument::getReplayCurrentFrame( const InputManager& input ) const
    {
        return _replay.getStartFrameIndex() + ( isReplayPlaying( input ) ? input.getVirtualFrameIndex() : 0u );
    }

    void InputMapDocument::startRecording( const InputManager& input )
    {
        _replay.startRecording( "GameplaySession" );
        _recordedBeginFrameCount = input.getBeginFrameCount();
    }

    void InputMapDocument::playReplay( InputManager& input )
    {
        if ( getReplayCurrentFrame( input ) >= _replay.getFrameCount() )
            _replay.seekTo( input, 0 );
        input.attachVirtualInput( &_replay, VirtualInputMode::Exclusive, false );
    }

    void InputMapDocument::saveReplay() const
    {
        const std::optional<AbsolutePath> diskPath = editor::getSubsystem<AssetPaths>().resolveWritePath( _replayFilePath.get() );
        if ( diskPath.has_value() == false || _replay.saveToFile( *diskPath ) == false )
            SW_LOG_ERROR( "Could not save input replay '%#'", _replayFilePath.c_str() );
    }

    void InputMapDocument::loadReplay( InputManager& input )
    {
        if ( input.getVirtualInput() == &_replay )
            input.detachVirtualInput();
        const string diskPath = editor::getSubsystem<AssetPaths>().findLooseFile( _replayFilePath.get() ).getDiskPath();
        if ( diskPath.empty() )
            SW_LOG_ERROR( "Could not load input replay '%#': no such file on disk", _replayFilePath.c_str() );
        else if ( _replay.loadFromFile( diskPath ) == false )
            SW_LOG_ERROR( "Could not load input replay '%#' from '%#'", _replayFilePath.c_str(), diskPath.c_str() );
    }
} // namespace sw::editor
