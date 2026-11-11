#include "pch.h"

#include "Editor/Panels/InputMap/Model/InputMapModel.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Viewport/ViewportInputOverlay.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/Map/InputGlyphResolver.h"
#include "Engine/Input/Map/InputMapBundle.h"
#include "Engine/Input/Map/InputRemapSerializer.h"

namespace sw::editor
{
    InputMapModel::InputMapModel()
        : _inputMap{}
        , _replay{}
        , _listConflict{}
        , _inputMapPath{ getSubsystem<const EngineDefaultAssets>()._debugKeyInputMap } // 처음 여는 맵은 디버그 키 맵이다 — 경로의 원본은 enginedefaultassets.xml
        , _replayFilePath{ getEditorToolDefaults()._inputReplayFile }                  // 처음 가리키는 파일 — 원본은 editortooldefaults.json
        , _selectedAction{ "" }
        , _plot{}
        , _recordedBeginFrameCount{ 0 }
        , _capturingBindIndex{ 0 }
        , _pendingCaptureModifier{ Key::Unknown }
        , _bCapturingKey{ SW_FALSE }
        , _bCaptureNewBinding{ SW_FALSE }
        , _bPlotPaused{ SW_FALSE }
        , _bDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void InputMapModel::tick( InputManager& input, float32 deltaTime )
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

    void InputMapModel::releaseInput()
    {
        _replay.stopRecording();
        cancelKeyCapture();
        InputManager* pInput = _inputMap.getInputManager();
        if ( pInput != nullptr && pInput->getVirtualInput() == &_replay )
            pInput->detachVirtualInput();
    }

    void InputMapModel::sampleInput( InputManager& input )
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

    bool InputMapModel::isDocumentPath( string_view assetPath )
    {
        return StringUtil::endsWith( assetPath, kMapSuffix, true ) || InputMapBundle::isSettingsFile( assetPath );
    }

    bool InputMapModel::reload()
    {
        // 읽지 못하면 편집 중인 바인딩과 dirty 를 그대로 둔다. dirty 를 지우고 "다시 읽었다" 고 표시하지 않는다.
        // 묶음은 읽기 실패가 맵을 비우므로(`InputMap::loadBundle`) 맵에 넣기 전에 묶음만 먼저 검사한다.
        const AssetID assetID{ _inputMapPath.c_str() };
        const bool    bBundle = isBundlePath();
        if ( bBundle && InputMapBundle::checkSettings( assetID ) == false )
        {
            SW_LOG_ERROR_ID( "InputMapModel.CouldNotReloadInputMap", "Could not reload InputMap from %# - keeping the edited bindings", _inputMapPath.c_str() );
            return false;
        }
        const bool bLoaded = bBundle ? InputMapBundle::loadFromSettings( _inputMap, assetID ) : _inputMap.loadFromAsset( assetID );
        if ( bLoaded == false )
        {
            SW_LOG_ERROR_ID( "InputMapModel.CouldNotReloadInputMap", "Could not reload InputMap from %# - keeping the edited bindings", _inputMapPath.c_str() );
            return false;
        }
        SW_LOG_INFO_ID( "InputMapModel.ReloadedInputMap", "Reloaded InputMap from %#", _inputMapPath.c_str() );
        cancelKeyCapture();
        // 새 맵에 없는 액션을 든 채면 오른쪽 칸이 없는 액션의 "바인딩 없음" 을 보여 준다.
        if ( _inputMap.hasAction( hashed_string( _selectedAction.c_str() ) ) == false )
            _selectedAction.clear();
        _bDirty = SW_FALSE;
        return true;
    }

    bool InputMapModel::saveDocument()
    {
        // 패널은 리소스의 기본 바인딩(`<InputMap>`)을 편집한다. 다시 읽기(`reload`)와 같은 형식, 같은 위치에 쓴다.
        // 플레이어 리매핑은 UserSettings 의 `keyBinding` 설정이 사용자 파일에 보관한다.
        if ( isBundlePath() )
        {
            if ( saveBundle() == false )
            {
                SW_LOG_WARNING_ID( "InputMapModel.FailedSaveInputMap", "Failed to save InputMap to %#", _inputMapPath.c_str() );
                return false;
            }
            _bDirty = SW_FALSE;
            return true;
        }
        const optional<AbsolutePath> inputMapFile = editor::getSubsystem<AssetPaths>().toDiskPath( _inputMapPath.get() );
        if ( inputMapFile.has_value() == false || _inputMap.saveToAsset( *inputMapFile ) == false )
        {
            SW_LOG_WARNING_ID( "InputMapModel.FailedSaveInputMap", "Failed to save InputMap to %#", _inputMapPath.c_str() );
            return false;
        }
        SW_LOG_INFO_ID( "InputMapModel.SavedInputMap", "Saved InputMap to %#", _inputMapPath.c_str() );
        _bDirty = SW_FALSE;
        return true;
    }

    bool InputMapModel::isBundlePath() const
    {
        return InputMapBundle::isSettingsFile( string_view{ _inputMapPath.c_str() } );
    }

    bool InputMapModel::saveBundle() const
    {
        // 파일마다 원래 몫을 돌려보낸다. 편집이 건드리지 않은 파일은 쓰지 않는다 — 다른 게임과 함께 쓰는 공통 파일의 주석과 차례를 지키고, 함께 쓰는 파일을 말없이 바꾸지 않는다.
        vector<AssetID> listAsset;
        InputMap        onDisk;
        const bool      bHasDisk = InputMapBundle::collectAssets( AssetID{ _inputMapPath.get() }, listAsset ) && onDisk.loadBundle( listAsset ) && onDisk.getSourceCount() == _inputMap.getSourceCount();
        for ( uint32 sourceIndex = 0; sourceIndex < _inputMap.getSourceCount(); ++sourceIndex )
        {
            string editedText;
            string diskText;
            if ( _inputMap.writeSourceText( sourceIndex, editedText ) == false )
                return false;
            if ( bHasDisk && onDisk.writeSourceText( sourceIndex, diskText ) && editedText == diskText )
                continue;
            const string&                assetPath = _inputMap.getSourceAssetPath( sourceIndex );
            const optional<AbsolutePath> filePath  = editor::getSubsystem<AssetPaths>().toDiskPath( assetPath );
            if ( filePath.has_value() == false || _inputMap.saveSourceToAsset( sourceIndex, *filePath ) == false )
                return false;
            SW_LOG_INFO_ID( "InputMapModel.SavedInputMap", "Saved InputMap to %#", assetPath.c_str() );
        }
        return true;
    }

    string InputMapModel::describeActionSource( const hashed_string& action ) const
    {
        if ( _inputMap.hasAction( action ) == false || _inputMap.getSourceCount() == 0 )
            return {};
        return _inputMap.getSourceAssetPath( _inputMap.getActionSourceIndex( action ) );
    }

    bool InputMapModel::recordEdit( bool bChanged )
    {
        _bDirty = _bDirty | ( bChanged ? SW_TRUE : SW_FALSE );
        return bChanged;
    }

    void InputMapModel::resetAllBindingsToDefault()
    {
        _inputMap.resetAllBindingsToDefault();
        _bDirty = SW_TRUE;
    }

    bool InputMapModel::resetActionToDefault( const hashed_string& action )
    {
        return recordEdit( _inputMap.resetActionToDefault( action ) );
    }

    bool InputMapModel::setLayerEnabled( const hashed_string& layer, bool bEnabled )
    {
        if ( _inputMap.hasLayer( layer ) == false || _inputMap.isLayerEnabled( layer ) == bEnabled )
            return false;
        _inputMap.setLayerEnabled( layer, bEnabled );
        return recordEdit( true );
    }

    bool InputMapModel::canSaveTriggerProperties( BindingKind kind )
    {
        return kind == BindingKind::SingleSlot || kind == BindingKind::Shortcut || kind == BindingKind::Axis1DComposite || kind == BindingKind::Chord;
    }

    bool InputMapModel::canEditTriggerFields( const hashed_string& action, uint32 bindIndex ) const
    {
        const ActionBinding* pBinding = _inputMap.getBinding( action, bindIndex );
        return pBinding != nullptr && canSaveTriggerProperties( pBinding->_kind );
    }

    bool InputMapModel::setBindingTrigger( const hashed_string& action, uint32 bindIndex, ActionTrigger trigger )
    {
        return canEditTriggerFields( action, bindIndex ) && recordEdit( _inputMap.setBindingTrigger( action, bindIndex, trigger ) );
    }

    bool InputMapModel::setBindingTriggerTime( const hashed_string& action, uint32 bindIndex, float32 seconds )
    {
        return canEditTriggerFields( action, bindIndex ) && recordEdit( _inputMap.setBindingTriggerTime( action, bindIndex, seconds ) );
    }

    bool InputMapModel::setBindingNegate( const hashed_string& action, uint32 bindIndex, bool bNegate )
    {
        return canEditTriggerFields( action, bindIndex ) && recordEdit( _inputMap.setBindingNegate( action, bindIndex, bNegate ) );
    }

    bool InputMapModel::setBindingTapCount( const hashed_string& action, uint32 bindIndex, uint8 tapCount )
    {
        return canEditTriggerFields( action, bindIndex ) && recordEdit( _inputMap.setBindingTapCount( action, bindIndex, tapCount ) );
    }

    bool InputMapModel::setBindingLayer( const hashed_string& action, uint32 bindIndex, const hashed_string& layer )
    {
        return recordEdit( _inputMap.setBindingLayer( action, bindIndex, layer ) );
    }

    bool InputMapModel::removeBinding( const hashed_string& action, uint32 bindIndex )
    {
        return recordEdit( _inputMap.removeBinding( action, bindIndex ) );
    }

    bool InputMapModel::canCreateAction( string_view name ) const
    {
        return name.empty() == false && _inputMap.hasAction( hashed_string( string( name ).c_str() ) ) == false;
    }

    bool InputMapModel::createAction( string_view name, InputActionValueType valueType )
    {
        // 있는 이름을 다시 만들면 엔진은 그 액션을 돌려준다. 그대로 두면 아무것도 만들지 않고 "만들었다" 고 입력칸을 비운다.
        if ( canCreateAction( name ) == false )
            return false;
        _inputMap.createAction( sw::hashed_string( string( name ).c_str() ), valueType );
        _selectedAction = name; // 만들자마자 바인딩을 더할 수 있게 고른다
        return recordEdit( true );
    }

    bool InputMapModel::deleteAction( const hashed_string& action )
    {
        if ( _inputMap.removeAction( action ) == false )
            return false;
        if ( _selectedAction == action.view() )
            _selectedAction.clear();
        return recordEdit( true );
    }

    bool InputMapModel::canCreateLayer( string_view name ) const
    {
        return name.empty() == false && _inputMap.hasLayer( hashed_string( string( name ).c_str() ) ) == false;
    }

    bool InputMapModel::createLayer( string_view name, int32 priority )
    {
        if ( canCreateLayer( name ) == false )
            return false;
        _inputMap.registerLayer( hashed_string( string( name ).c_str() ), priority );
        return recordEdit( true );
    }

    void InputMapModel::collectVisibleActions( const EditorListFilter& filter, vector<hashed_string>& outListVisibleAction ) const
    {
        // 호출한 쪽이 멤버 버퍼를 다시 쓴다. 키 표기로도 찾는다 — "Space 에 뭐가 묶였나" 를 묻는 일이 많다(Godot 입력 맵의 이벤트 검색).
        outListVisibleAction.clear();
        for ( const hashed_string& action : _inputMap.getActionNames() )
        {
            if ( filter.isActive() == false || filter.matchesAny( { action.view(), string_view{ InputGlyphResolver::findActionGlyph( _inputMap, action ) } } ) )
                outListVisibleAction.push_back( action );
        }
    }

    const utf8* InputMapModel::describeActionState( const hashed_string& action ) const
    {
        if ( _inputMap.wasActionTriggered( action ) )
            return "triggered";
        if ( _inputMap.isActionDown( action ) )
            return "down";
        return _inputMap.getActionPhase( action ) != ActionPhase::None ? "ongoing" : "idle";
    }

    string InputMapModel::describeBinding( const hashed_string& action, uint32 bindIndex ) const
    {
        const ActionBinding* pBinding = _inputMap.getBinding( action, bindIndex );
        if ( pBinding == nullptr )
            return string{};
        // 슬롯 하나만 보이면 Ctrl+S 가 S 로 읽혀, 다른 액션의 S 와 부딪히는 것처럼 보인다.
        const InputGlyphStyle style = InputGlyphStyle::KeyboardMouse;
        if ( pBinding->_kind == BindingKind::Shortcut )
            return InputRemapSerializer::formatModifierNames( pBinding->_modifierMask ) + " + " + InputGlyphResolver::makeSlotGlyph( pBinding->_arrSlot[0], style );
        if ( pBinding->_kind == BindingKind::Chord )
            return InputGlyphResolver::makeSlotGlyph( pBinding->_arrSlot[0], style ) + " + " + InputGlyphResolver::makeSlotGlyph( pBinding->_arrSlot[1], style );
        if ( pBinding->_kind == BindingKind::SingleSlot )
            return InputGlyphResolver::makeSlotGlyph( pBinding->_arrSlot[0], style );
        return string{ BindingKinds::toName( pBinding->_kind ) };
    }

    void InputMapModel::beginKeyCapture( uint32 bindIndex )
    {
        _capturingBindIndex     = bindIndex;
        _pendingCaptureModifier = Key::Unknown;
        _bCaptureNewBinding     = SW_FALSE;
        _bCapturingKey          = SW_TRUE;
    }

    void InputMapModel::beginNewBindingCapture()
    {
        if ( _inputMap.hasAction( hashed_string( _selectedAction.c_str() ) ) == false )
            return;
        beginKeyCapture( 0 );
        _bCaptureNewBinding = SW_TRUE;
    }

    void InputMapModel::cancelKeyCapture()
    {
        _bCapturingKey          = SW_FALSE;
        _bCaptureNewBinding     = SW_FALSE;
        _pendingCaptureModifier = Key::Unknown;
    }

    void InputMapModel::advanceKeyCapture( const InputMapCaptureFrame& frame )
    {
        if ( _bCapturingKey == SW_FALSE )
            return;
        // Esc 는 그만두기다(언리얼 키 선택기와 같다). Esc 를 묶으려면 버튼 격자에서 고른다.
        if ( frame._chordKey == Key::Escape && frame._modifierMask == ModifierKey::None )
        {
            cancelKeyCapture();
            return;
        }
        if ( frame._chordKey != Key::Unknown )
        {
            (void)finishKeyCapture( frame._chordKey, frame._modifierMask );
            return;
        }
        // 수식 키는 눌린 순간 알 수 없다 — 뒤이어 다른 키가 오면 조합이고, 그냥 떼면 그 키 자신(달리기의 Shift)이다.
        if ( frame._pressedKey != Key::Unknown )
            _pendingCaptureModifier = frame._pressedKey;
        else if ( _pendingCaptureModifier != Key::Unknown && frame._bPendingModifierDown == false )
            (void)finishKeyCapture( _pendingCaptureModifier, ModifierKey::None );
    }

    bool InputMapModel::bindCapturedKey( const hashed_string& action, Key key, uint8 modifierMask, uint32& outBindIndex )
    {
        const uint32        countBefore = _inputMap.getBindingCount( action );
        const hashed_string layer       = findNewBindingLayer( action );
        if ( modifierMask == ModifierKey::None )
            _inputMap.bind( action, key, ActionTrigger::Pressed, layer );
        else
            _inputMap.bindShortcut( action, key, modifierMask, ActionTrigger::Pressed, layer );
        outBindIndex = countBefore;
        return _inputMap.getBindingCount( action ) > countBefore; // 키가 없으면 엔진이 묶지 않는다
    }

    bool InputMapModel::rebindCapturedKey( const hashed_string& action, uint32 bindIndex, Key key, uint8 modifierMask )
    {
        const ActionBinding* pBinding = _inputMap.getBinding( action, bindIndex );
        if ( pBinding == nullptr )
            return false;
        const bool bKeyOrShortcut = pBinding->_kind == BindingKind::SingleSlot || pBinding->_kind == BindingKind::Shortcut;
        const bool bKeyboardSlot  = pBinding->_arrSlot[0]._deviceKind == InputDeviceKind::Keyboard;
        if ( bKeyOrShortcut && bKeyboardSlot )
            return _inputMap.rebindKeyShortcut( action, bindIndex, key, modifierMask );
        // 패드 · 마우스 단일 바인딩은 먼저 키보드 슬롯으로 바꾼 뒤 수식 키를 얹는다. Chord 의 방아쇠와 키 하나로 못 바꾸는 종류는 엔진의 규칙(`rebindKey`)을 따른다.
        if ( _inputMap.rebindKey( action, key, bindIndex ) == false )
            return false;
        return modifierMask == ModifierKey::None || bKeyOrShortcut == false || _inputMap.rebindKeyShortcut( action, bindIndex, key, modifierMask );
    }

    hashed_string InputMapModel::findNewBindingLayer( const hashed_string& action ) const
    {
        const uint32 bindingCount = _inputMap.getBindingCount( action );
        return bindingCount > 0 ? _inputMap.findBindingLayer( action, bindingCount - 1 ) : _inputMap.getDefaultLayerName();
    }

    bool InputMapModel::recordCapturedBinding( const hashed_string& action, uint32 bindIndex )
    {
        // 겹침 규칙은 충돌 칸과 한 벌이다. 그대로 두는 것은 덮어쓰기를 원할 수 있어서이고, 충돌 칸이 다시 잡기 · 지우기를 준다.
        refreshConflicts();
        if ( InputMapConflict::isInvolved( _listConflict, action, bindIndex ) )
            SW_LOG_WARNING_ID( "InputMapModel.BindingConflictsSameLayer", "Binding %# of '%#' now fires together with another action on the same layer - see Conflicts", bindIndex, action.c_str() );
        return recordEdit( true );
    }

    bool InputMapModel::finishKeyCapture( Key newKey, uint8 modifierMask )
    {
        if ( _bCapturingKey == SW_FALSE )
            return false;
        const bool          bNewBinding = _bCaptureNewBinding == SW_TRUE;
        uint32              bindIndex   = _capturingBindIndex;
        const hashed_string action( _selectedAction.c_str() );
        cancelKeyCapture();
        // 키 하나로 바꿀 수 없는 바인딩(합성 축, 스틱)이면 바뀌지 않는다. 경고는 InputMap 이 남긴다. 이때는 문서를 dirty 로 표시하지 않는다.
        const bool bChanged = bNewBinding ? bindCapturedKey( action, newKey, modifierMask, bindIndex ) : rebindCapturedKey( action, bindIndex, newKey, modifierMask );
        return bChanged && recordCapturedBinding( action, bindIndex );
    }

    bool InputMapModel::canCaptureSlot() const
    {
        if ( _bCapturingKey == SW_FALSE )
            return false;
        if ( _bCaptureNewBinding == SW_TRUE )
            return true;
        // Shortcut · Chord 는 키보드 조합이다. 패드 버튼을 넣으면 파일이 이름 붙이지 못한다.
        const ActionBinding* pBinding = _inputMap.getBinding( hashed_string( _selectedAction.c_str() ), _capturingBindIndex );
        return pBinding != nullptr && pBinding->_kind == BindingKind::SingleSlot;
    }

    bool InputMapModel::finishSlotCapture( const InputSlot& slot )
    {
        if ( canCaptureSlot() == false )
            return false;
        const bool          bNewBinding = _bCaptureNewBinding == SW_TRUE;
        uint32              bindIndex   = _capturingBindIndex;
        const hashed_string action( _selectedAction.c_str() );
        cancelKeyCapture();
        if ( bNewBinding )
        {
            bindIndex = _inputMap.getBindingCount( action );
            _inputMap.bind( action, slot, ActionTrigger::Pressed, findNewBindingLayer( action ) );
            // 다시 묶기가 맵 읽기와 같은 검사(가상 커서가 마우스로 바꾸는 버튼)를 한다. 묶지 못할 버튼이면 파일이 다음 읽기에서 거절되므로 지운다.
            if ( _inputMap.rebindSlot( action, slot, bindIndex ) == false )
            {
                (void)_inputMap.removeBinding( action, bindIndex );
                return false;
            }
        }
        else if ( _inputMap.rebindSlot( action, slot, bindIndex ) == false )
            return false;
        return recordCapturedBinding( action, bindIndex );
    }

    void InputMapModel::pollGamepadCapture( InputManager& input )
    {
        GamepadDevice* pGamepad = input.getGamepad( 0 );
        if ( canCaptureSlot() == false || pGamepad == nullptr || pGamepad->isConnected() == false )
            return;
        for ( uint32 buttonIndex = 0; buttonIndex < static_cast<uint32>( GamepadButton::Count ); ++buttonIndex )
        {
            const GamepadButton button = static_cast<GamepadButton>( buttonIndex );
            if ( pGamepad->wasButtonPressed( button ) )
            {
                (void)finishSlotCapture( InputSlot::fromGamepadButton( button, 0 ) );
                return;
            }
        }
    }

    void InputMapModel::refreshConflicts()
    {
        InputMapConflict::collect( _inputMap, _listConflict );
    }

    bool InputMapModel::readSelectedBindingCount( uint32& outCount ) const
    {
        if ( _selectedAction.empty() )
            return false;
        outCount = _inputMap.getBindingCount( hashed_string( _selectedAction.c_str() ) );
        return true;
    }

    bool InputMapModel::readSelectedTrigger( uint32& outTrigger ) const
    {
        if ( _selectedAction.empty() || _inputMap.getBindingCount( hashed_string( _selectedAction.c_str() ) ) == 0 )
            return false;
        outTrigger = static_cast<uint32>( _inputMap.getBindingTrigger( hashed_string( _selectedAction.c_str() ), 0 ) );
        return true;
    }

    bool InputMapModel::isReplayPlaying( const InputManager& input ) const
    {
        return input.getVirtualInput() == &_replay;
    }

    uint32 InputMapModel::getReplayCurrentFrame( const InputManager& input ) const
    {
        return _replay.getStartFrameIndex() + ( isReplayPlaying( input ) ? input.getVirtualFrameIndex() : 0u );
    }

    void InputMapModel::startRecording( const InputManager& input )
    {
        _replay.startRecording( "GameplaySession" );
        _recordedBeginFrameCount = input.getBeginFrameCount();
    }

    void InputMapModel::playReplay( InputManager& input )
    {
        if ( getReplayCurrentFrame( input ) >= _replay.getFrameCount() )
            _replay.seekTo( input, 0 );
        input.attachVirtualInput( &_replay, VirtualInputMode::Exclusive, false );
    }

    void InputMapModel::saveReplay() const
    {
        const optional<AbsolutePath> diskPath = editor::getSubsystem<AssetPaths>().toDiskPath( _replayFilePath.get() );
        if ( diskPath.has_value() == false || _replay.saveToFile( *diskPath ) == false )
            SW_LOG_ERROR_ID( "InputMapModel.CouldNotSaveInput", "Could not save input replay '%#'", _replayFilePath.c_str() );
    }

    void InputMapModel::loadReplay( InputManager& input )
    {
        if ( input.getVirtualInput() == &_replay )
            input.detachVirtualInput();
        _replay.stopRecording(); // 녹화 중이면 읽은 프레임 뒤에 이어 적는다
        const optional<AbsolutePath> diskPath = editor::getSubsystem<AssetPaths>().findLooseFile( _replayFilePath.get() );
        if ( diskPath.has_value() == false )
            SW_LOG_ERROR_ID( "InputMapModel.CouldNotLoadInput", "Could not load input replay '%#': no such file on disk", _replayFilePath.c_str() );
        else if ( _replay.loadFromFile( diskPath->str() ) == false )
            SW_LOG_ERROR_ID( "InputMapModel.CouldNotLoadInput2", "Could not load input replay '%#' from '%#'", _replayFilePath.c_str(), diskPath->c_str() );
    }

    InputManager& InputMapModel::getInputManager()
    {
        return editor::getSubsystem<InputManager>();
    }

    ViewportInputOverlay& InputMapModel::editViewportOverlay()
    {
        return editor::getSubsystem<ViewportInputOverlaySettings>()._overlay;
    }
} // namespace sw::editor
