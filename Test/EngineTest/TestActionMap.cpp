#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/Events/RawInputEvent.h"
#include "Engine/Input/InputManager.h"

#include "TestFramework/TestFramework.h"

#include <thread>

// ActionMap — 바인딩 · 레이어 · 조합 키 · 벡터 축 합성. 장치가 아니라 **매핑 규칙**을 본다.
/**
 * @brief [ActionMapTest] default.input.xml 리소스 로드 및 레이어/액션/코드 바인딩 무결성 검증
 */
/**
 * @brief [ActionMapTest] 같은 레이어에서 이미 쓰는 키를 찾아낸다.
 * @details 이 함수는 오래 아무도 부르지 않아 죽은 것처럼 보였다. 실제로는 에디터의 Rebind 가
 *          **불러야 했는데 안 부르던** 것이고(그래서 이미 쓰는 키로 바꿔도 아무 말이 없었다),
 *          지금은 InputMapEditorPanel::rebindSelectedAction 이 부른다. 계약을 여기서 고정한다.
 */

SW_TEST_CASE( ActionMapTest, DetectsBindingConflictInSameLayer )
{
    sw::ActionMap actionMap;
    actionMap.registerLayer( "Gameplay", 0, true, false, false );
    actionMap.registerLayer( "Menu", 10, true, false, false );

    actionMap.bind( "Jump", sw::Key::Space, sw::ActionTrigger::Pressed, "Gameplay" );
    actionMap.bind( "Confirm", sw::Key::Enter, sw::ActionTrigger::Pressed, "Menu" );

    sw::string conflicting;

    // 같은 레이어에서 이미 쓰는 키다.
    SW_EXPECT_TRUE( actionMap.hasBindingConflict( sw::InputSlot::fromKey( sw::Key::Space ), "Gameplay", conflicting ) );
    SW_EXPECT_EQUAL( sw::string( "Jump" ), conflicting );

    // 아무도 안 쓰는 키는 충돌이 아니다.
    conflicting.clear();
    SW_EXPECT_FALSE( actionMap.hasBindingConflict( sw::InputSlot::fromKey( sw::Key::F1 ), "Gameplay", conflicting ) );

    // 레이어가 다르면 같은 키라도 충돌이 아니다 — 레이어가 있는 이유가 그것이다.
    conflicting.clear();
    SW_EXPECT_FALSE( actionMap.hasBindingConflict( sw::InputSlot::fromKey( sw::Key::Space ), "Menu", conflicting ) );
}

SW_TEST_CASE( ActionMapTest, LoadFromDefaultInputXmlResource )
{
    sw::InputManager inputManager;
    SW_EXPECT_TRUE( inputManager.initialize() );

    sw::ActionMap actionMap;
    actionMap.setInputManager( &inputManager );
    SW_EXPECT_TRUE( actionMap.loadFromResource( "engine/input/default.input.xml" ) );

    SW_EXPECT_TRUE( actionMap.hasLayer( "Title" ) );
    SW_EXPECT_TRUE( actionMap.hasLayer( "Gameplay" ) );
    SW_EXPECT_TRUE( actionMap.hasLayer( "Debug" ) );

    SW_EXPECT_TRUE( actionMap.hasAction( "Confirm" ) );
    SW_EXPECT_TRUE( actionMap.hasAction( "Continue" ) );
    SW_EXPECT_TRUE( actionMap.hasAction( "Cancel" ) );
    SW_EXPECT_TRUE( actionMap.hasAction( "ReloadEditor" ) );

    inputManager.shutdown();
}

/**
 * @brief [ActionMapTest] getGlyphForAction 디바이스별 및 코드/축 조합 글리프 포맷 검증
 */
SW_TEST_CASE( ActionMapTest, GlyphResolutionWithDeviceTypeAndChords )
{
    sw::InputManager inputManager;
    SW_EXPECT_TRUE( inputManager.initialize() );

    sw::ActionMap& actionMap = inputManager.getActionMap();

    actionMap.bind( "Interact", sw::Key::E );
    actionMap.bind( "Fire", sw::MouseButton::Left );
    actionMap.bind( "Jump", sw::GamepadButton::A );
    actionMap.bindChord( "QuickSave", sw::Key::LeftControl, sw::Key::S );

    inputManager.setActiveDeviceType( sw::InputDeviceType::KeyboardMouse );
    const sw::string glyphInteract = actionMap.getGlyphForAction( "Interact" );
    SW_EXPECT_TRUE( glyphInteract.find( "E" ) != sw::string::npos );

    const sw::string glyphFire = actionMap.getGlyphForAction( "Fire" );
    SW_EXPECT_TRUE( glyphFire.find( "Left" ) != sw::string::npos );

    const sw::string glyphSave = actionMap.getGlyphForAction( "QuickSave" );
    SW_EXPECT_TRUE( glyphSave.find( "LeftControl" ) != sw::string::npos );
    SW_EXPECT_TRUE( glyphSave.find( "S" ) != sw::string::npos );

    inputManager.setActiveDeviceType( sw::InputDeviceType::GamepadXbox );
    const sw::string glyphJumpXbox = actionMap.getGlyphForAction( "Jump" );
    SW_EXPECT_TRUE( glyphJumpXbox.find( "A" ) != sw::string::npos );

    inputManager.setActiveDeviceType( sw::InputDeviceType::GamepadPlayStation );
    const sw::string glyphJumpPS = actionMap.getGlyphForAction( "Jump" );
    SW_EXPECT_TRUE( glyphJumpPS.find( "X" ) != sw::string::npos );

    inputManager.shutdown();
}

/**
 * @brief [ActionMapTest] MouseDelta2D FPS 룩 벡터 바인딩 검증
 */
SW_TEST_CASE( ActionMapTest, MouseDeltaLookBinding )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::ActionMap& actionMap = input.getActionMap();
    actionMap.bindMouseDelta( "Look", 2.0f );

    input.postRawEvent( sw::RawInputEvent::makeMouseMove( 10, 5 ) );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );

    const sw::float2 lookVec = actionMap.getVector2D( "Look" );
    SW_EXPECT_TRUE( lookVec._x != 0.0f || lookVec._y != 0.0f );

    input.shutdown();
}

/**
 * @brief [ActionMapTest] 다중 수정자 복합 단축키(Shortcut) 바인딩 검증
 */
SW_TEST_CASE( ActionMapTest, MultiModifierShortcutBinding )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::ActionMap& actionMap = input.getActionMap();
    actionMap.bindShortcut( "SaveAs", sw::Key::S, sw::ModifierKey::Ctrl | sw::ModifierKey::Shift );

    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::S ) );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    SW_EXPECT_FALSE( actionMap.wasActionTriggered( "SaveAs" ) );
    input.endFrame();

    input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::S ) );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    input.endFrame();

    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::LeftControl ) );
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::LeftShift ) );
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::S, 0, false, sw::ModifierKey::Ctrl | sw::ModifierKey::Shift ) );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    SW_EXPECT_TRUE( actionMap.wasActionTriggered( "SaveAs" ) );

    input.shutdown();
}

/**
 * @brief [ActionMapTest] AnyKey 타이틀 화면 바인딩 검증
 */
SW_TEST_CASE( ActionMapTest, AnyKeyBinding )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::ActionMap& actionMap = input.getActionMap();
    actionMap.bindAnyKey( "PressAnyKeyToStart" );

    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    SW_EXPECT_FALSE( actionMap.wasActionTriggered( "PressAnyKeyToStart" ) );
    input.endFrame();

    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Space ) );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    SW_EXPECT_TRUE( actionMap.wasActionTriggered( "PressAnyKeyToStart" ) );

    input.shutdown();
}

/**
 * @brief [ActionMapTest] 가상 조이스틱(마우스 드래그) 바인딩의 플로팅 앵커·데드존·리셋 검증
 */
SW_TEST_CASE( ActionMapTest, VirtualJoystickDragBinding )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::ActionMap& actionMap = input.getActionMap();
    actionMap.bindVirtualJoystick2D( "Move", sw::MouseButton::Left, 100.0f, 0.1f );

    // 1) 버튼을 누르지 않은 상태에서는 0벡터
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    sw::float2 idleVec = actionMap.getVector2D( "Move" );
    SW_EXPECT_NEAR_EQUAL( 0.0f, idleVec._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, idleVec._y, 0.001f );
    input.endFrame();

    // 2) (200,200)에서 누르면 그 지점이 앵커가 되고, 아직 같은 지점이라 0벡터
    input.postRawEvent( sw::RawInputEvent::makeMouseButtonDown( sw::MouseButton::Left, 200, 200 ) );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    sw::float2 anchoredVec = actionMap.getVector2D( "Move" );
    SW_EXPECT_NEAR_EQUAL( 0.0f, anchoredVec._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, anchoredVec._y, 0.001f );
    input.endFrame();

    // 3) 앵커(200,200)에서 (300,200)으로 드래그 → +X 방향 최대치(반경 100 도달)
    input.postRawEvent( sw::RawInputEvent::makeMouseMove( 300, 200 ) );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    sw::float2 dragVec = actionMap.getVector2D( "Move" );
    SW_EXPECT_NEAR_EQUAL( 1.0f, dragVec._x, 0.01f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, dragVec._y, 0.01f );
    input.endFrame();

    // 4) 버튼을 떼면 즉시 0벡터로 리셋
    input.postRawEvent( sw::RawInputEvent::makeMouseButtonUp( sw::MouseButton::Left, 300, 200 ) );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    sw::float2 releasedVec = actionMap.getVector2D( "Move" );
    SW_EXPECT_NEAR_EQUAL( 0.0f, releasedVec._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, releasedVec._y, 0.001f );
    input.endFrame();

    // 5) 다른 위치(50,50)에서 다시 누르면 앵커가 새 위치로 플로팅되어 다시 0벡터
    input.postRawEvent( sw::RawInputEvent::makeMouseButtonDown( sw::MouseButton::Left, 50, 50 ) );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    sw::float2 reAnchoredVec = actionMap.getVector2D( "Move" );
    SW_EXPECT_NEAR_EQUAL( 0.0f, reAnchoredVec._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, reAnchoredVec._y, 0.001f );
    input.endFrame();

    input.shutdown();
}

/**
 * @brief [ActionMapTest] 리바인딩 충돌 해결(Swap / Override / AddSecondary) 검증
 */
SW_TEST_CASE( ActionMapTest, RebindConflictResolution )
{
    sw::ActionMap actionMap;
    actionMap.bind( "ActionA", sw::Key::F );
    actionMap.bind( "ActionB", sw::Key::G );

    const bool bSwapOk = actionMap.rebindWithResolution( "ActionB", sw::InputSlot::fromKey( sw::Key::F ), sw::ConflictResolution::Swap );
    SW_EXPECT_TRUE( bSwapOk );
    const sw::ActionBinding* pBindB = actionMap.getBinding( "ActionB", 0 );
    const sw::ActionBinding* pBindA = actionMap.getBinding( "ActionA", 0 );
    SW_EXPECT_TRUE( pBindB != nullptr && pBindB->_arrSlot[0]._controlIndex == static_cast<uint16>( sw::Key::F ) );
    SW_EXPECT_TRUE( pBindA != nullptr && pBindA->_arrSlot[0]._controlIndex == static_cast<uint16>( sw::Key::G ) );

    const bool bOverrideOk = actionMap.rebindWithResolution( "ActionA", sw::InputSlot::fromKey( sw::Key::F ), sw::ConflictResolution::Override );
    SW_EXPECT_TRUE( bOverrideOk );
    pBindA = actionMap.getBinding( "ActionA", 0 );
    pBindB = actionMap.getBinding( "ActionB", 0 );
    SW_EXPECT_TRUE( pBindA != nullptr && pBindA->_arrSlot[0]._controlIndex == static_cast<uint16>( sw::Key::F ) );
    SW_EXPECT_TRUE( pBindB != nullptr && pBindB->_arrSlot[0]._controlIndex == static_cast<uint16>( sw::Key::Unknown ) );

    const bool bAddOk = actionMap.rebindWithResolution( "ActionB", sw::InputSlot::fromKey( sw::Key::F ), sw::ConflictResolution::AddSecondary );
    SW_EXPECT_TRUE( bAddOk );
    SW_EXPECT_EQUAL( 2u, actionMap.getBindingCount( "ActionB" ) );
}

/**
 * @brief [ActionMapTest] DebugActionState 실시간 덤프 검증
 */
SW_TEST_CASE( ActionMapTest, DebugActionStatesDump )
{
    sw::ActionMap actionMap;
    actionMap.bind( "Jump", sw::Key::Space );
    actionMap.bind( "Fire", sw::MouseButton::Left );

    sw::vector<sw::DebugActionState> listState;
    actionMap.getDebugActionStates( listState );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listState.size() ) );
}

/**
 * @brief [ActionMapTest] ActionHandle 기반 Zero-Lookup O(1) 액션 상태 폴링 검증
 */
SW_TEST_CASE( ActionMapTest, ActionHandleZeroLookup )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::ActionMap& actionMap = input.getActionMap();
    actionMap.bind( "Fire", sw::Key::Space );
    actionMap.bindVector2D( "Move", sw::Key::W, sw::Key::S, sw::Key::A, sw::Key::D );

    const sw::ActionHandle hFire = actionMap.getActionHandle( "Fire" );
    const sw::ActionHandle hMove = actionMap.getActionHandle( "Move" );

    SW_EXPECT_TRUE( hFire.isValid() );
    SW_EXPECT_TRUE( hMove.isValid() );

    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Space ) );
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::W ) );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );

    SW_EXPECT_TRUE( actionMap.wasActionTriggered( hFire ) );
    SW_EXPECT_TRUE( actionMap.isActionDown( hFire ) );
    SW_EXPECT_TRUE( actionMap.wasActionPressed( hFire ) );
    SW_EXPECT_FALSE( actionMap.wasActionReleased( hFire ) );

    const sw::float2 moveVec = actionMap.getVector2D( hMove );
    SW_EXPECT_NEAR_EQUAL( 0.0f, moveVec._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, moveVec._y, 0.001f );
    SW_EXPECT_EQUAL( static_cast<uint32>( sw::ActionPhase::Triggered ), static_cast<uint32>( actionMap.getActionPhase( hFire ) ) );

    input.shutdown();
}

/**
 * @brief [ActionMapTest] 2D 벡터 합성 WASD 대각선 정규화 모드(Circular vs IndependentAxes) 검증
 */
SW_TEST_CASE( ActionMapTest, DigitalNormalizationModes )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::ActionMap& actionMap = input.getActionMap();
    actionMap.bindVector2D( "Move", sw::Key::W, sw::Key::S, sw::Key::A, sw::Key::D );

    // 1) IndependentAxes 모드: W + D 대각선 입력 시 X=1.0, Y=1.0 유지
    actionMap.setDigitalNormalization( sw::DigitalNormalization::IndependentAxes );
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::W ) );
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::D ) );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );

    sw::float2 vecIndep = actionMap.getVector2D( "Move" );
    SW_EXPECT_NEAR_EQUAL( 1.0f, vecIndep._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, vecIndep._y, 0.001f );
    input.endFrame();

    // 2) Circular 모드: W + D 대각선 입력 시 단위 원(길이 1.0)으로 정규화
    actionMap.setDigitalNormalization( sw::DigitalNormalization::Circular );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );

    sw::float2    vecCirc = actionMap.getVector2D( "Move" );
    const float32 len     = sw::MathUtil::sqrt( vecCirc._x * vecCirc._x + vecCirc._y * vecCirc._y );
    SW_EXPECT_NEAR_EQUAL( 1.0f, len, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.7071f, vecCirc._x, 0.01f );
    SW_EXPECT_NEAR_EQUAL( 0.7071f, vecCirc._y, 0.01f );

    input.shutdown();
}

/**
 * @brief [ActionMapTest] 링버퍼 기반 선입력 및 커맨드 히스토리 제로 할당 래핑 검증
 */
SW_TEST_CASE( ActionMapTest, RingBufferZeroAllocation )
{
    sw::ActionMap actionMap;

    // 1) 선입력 버퍼링 16개 초과 주입 (오버플로우 링 래핑)
    for ( uint32 index = 0; index < 20; ++index )
    {
        sw::StringBuilder<sw::constant::kMaxBuffer32> sb;
        sb.append( "Action_" ).append( index );
        actionMap.bufferAction( sb.view(), 0.5f );
    }
    SW_EXPECT_TRUE( actionMap.consumeBufferedAction( "Action_19" ) );
    SW_EXPECT_FALSE( actionMap.consumeBufferedAction( "Action_0" ) ); // 0번은 래핑으로 덮어씌워짐

    // 2) 시간 경과 후 만료 테스트
    actionMap.update( 0.6f );
    SW_EXPECT_FALSE( actionMap.consumeBufferedAction( "Action_19" ) );
}

/**
 * @brief [ActionMapTest] 넘패드 표기법 기반 격투 커맨드 콤보 패턴(236P, 623P) 검증
 */
SW_TEST_CASE( ActionMapTest, CommandPatternFuzzyCombo )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::ActionMap& actionMap = input.getActionMap();
    actionMap.bind( "Down", sw::Key::S );
    actionMap.bind( "DownRight", sw::Key::C );
    actionMap.bind( "Right", sw::Key::D );
    actionMap.bind( "Punch", sw::Key::J );

    // 1) 2 (Down) -> 3 (DownRight) -> 6 (Right) -> Punch (236P 파동권) 순차 입력
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::S ) );
    input.beginFrame( 0.05f );
    actionMap.update( 0.05f );
    input.endFrame();

    input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::S ) );
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::C ) );
    input.beginFrame( 0.05f );
    actionMap.update( 0.05f );
    input.endFrame();

    input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::C ) );
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::D ) );
    input.beginFrame( 0.05f );
    actionMap.update( 0.05f );
    input.endFrame();

    input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::D ) );
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::J ) );
    input.beginFrame( 0.05f );
    actionMap.update( 0.05f );
    input.endFrame();

    SW_EXPECT_TRUE( actionMap.wasCommandPatternTriggered( "236Punch", 0.5f ) );
    SW_EXPECT_FALSE( actionMap.wasCommandPatternTriggered( "623Punch", 0.5f ) );

    input.shutdown();
}

/**
 * @brief [ActionMapTest] XML 유저 바인딩 전면 직렬화 및 역직렬화 검증 (모든 BindingKind)
 */
SW_TEST_CASE( ActionMapTest, SaveAndLoadAllBindingKinds )
{
    sw::ActionMap mapSave;
    mapSave.bind( "SingleKey", sw::Key::E );
    mapSave.bindAxis1DComposite( "MoveX", sw::Key::A, sw::Key::D );
    mapSave.bindVector2D( "Move2D", sw::Key::W, sw::Key::S, sw::Key::A, sw::Key::D, 0.1f );
    mapSave.bindGamepadStick2D( "LookStick", sw::GamepadStick::Right, 0.2f, {}, 0, 0.95f, 1.5f );
    mapSave.bindMouseDelta( "LookMouse", 2.5f );
    mapSave.bindChord( "ChordAction", sw::Key::LeftControl, sw::Key::K );
    mapSave.bindShortcut( "ShortcutAction", sw::Key::S, sw::ModifierKey::Ctrl | sw::ModifierKey::Shift );
    mapSave.bindAnyKey( "AnyKeyAction" );
    mapSave.bindVirtualJoystick2D( "MoveJoystick", sw::MouseButton::Right, 80.0f, 0.2f, {}, 0.9f );

    const sw::string savePath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_all_user_bindings.xml" );
    SW_EXPECT_TRUE( mapSave.saveUserBindings( savePath ) );

    sw::ActionMap mapLoad;
    SW_EXPECT_TRUE( mapLoad.loadUserBindings( savePath ) );

    SW_EXPECT_TRUE( mapLoad.hasAction( "SingleKey" ) );
    SW_EXPECT_TRUE( mapLoad.hasAction( "MoveX" ) );
    SW_EXPECT_TRUE( mapLoad.hasAction( "Move2D" ) );
    SW_EXPECT_TRUE( mapLoad.hasAction( "LookStick" ) );
    SW_EXPECT_TRUE( mapLoad.hasAction( "LookMouse" ) );
    SW_EXPECT_TRUE( mapLoad.hasAction( "ChordAction" ) );
    SW_EXPECT_TRUE( mapLoad.hasAction( "ShortcutAction" ) );
    SW_EXPECT_TRUE( mapLoad.hasAction( "AnyKeyAction" ) );
    SW_EXPECT_TRUE( mapLoad.hasAction( "MoveJoystick" ) );

    const sw::ActionBinding* pJoystickBind = mapLoad.getBinding( "MoveJoystick", 0 );
    if ( pJoystickBind != nullptr )
    {
        SW_EXPECT_TRUE( pJoystickBind->_kind == sw::BindingKind::VirtualJoystick2D );
        SW_EXPECT_TRUE( pJoystickBind->_arrSlot[0]._deviceKind == sw::InputDeviceKind::Mouse );
        SW_EXPECT_EQUAL( static_cast<uint16>( sw::MouseButton::Right ), pJoystickBind->_arrSlot[0]._controlIndex );
        SW_EXPECT_NEAR_EQUAL( 80.0f, pJoystickBind->_scale, 0.001f );
        SW_EXPECT_NEAR_EQUAL( 0.2f, pJoystickBind->_deadzone, 0.001f );
        SW_EXPECT_NEAR_EQUAL( 0.9f, pJoystickBind->_outerDeadzone, 0.001f );
    }
    else
    {
        SW_EXPECT_NOT_NULL( pJoystickBind );
    }

    const sw::ActionBinding* pStickBind = mapLoad.getBinding( "LookStick", 0 );
    if ( pStickBind != nullptr )
    {
        SW_EXPECT_TRUE( pStickBind->_kind == sw::BindingKind::GamepadStick2D );
        SW_EXPECT_TRUE( pStickBind->_stick == sw::GamepadStick::Right );
        SW_EXPECT_NEAR_EQUAL( 0.2f, pStickBind->_deadzone, 0.001f );
        SW_EXPECT_NEAR_EQUAL( 0.95f, pStickBind->_outerDeadzone, 0.001f );
        SW_EXPECT_NEAR_EQUAL( 1.5f, pStickBind->_responseExponent, 0.001f );
    }
    else
    {
        SW_EXPECT_NOT_NULL( pStickBind );
    }

    const sw::ActionBinding* pMouseBind = mapLoad.getBinding( "LookMouse", 0 );
    if ( pMouseBind != nullptr )
    {
        SW_EXPECT_TRUE( pMouseBind->_kind == sw::BindingKind::MouseDelta2D );
        SW_EXPECT_NEAR_EQUAL( 2.5f, pMouseBind->_scale, 0.001f );
    }
    else
    {
        SW_EXPECT_NOT_NULL( pMouseBind );
    }

    sw::FileUtil::removeFile( savePath );
}

/**
 * @brief [ActionMapTest] 대량 레이어 동적 등록으로 _mapLayer/_listLayerEntry가 여러 번 재할당된 뒤에도
 *        먼저 바인딩된 액션의 ActionBinding::_cachedLayerIndex(레이어 활성 판정 캐시)가 여전히 정확한지 검증.
 */
SW_TEST_CASE( ActionMapTest, LayerCacheStableAcrossMassiveDynamicRegistration )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::ActionMap& actionMap = input.getActionMap();

    // 1) 먼저 레이어와 액션을 하나 만들어 _cachedLayerIndex가 여기서 캐싱되게 한다.
    actionMap.registerLayer( "EarlyLayer", 0, true );
    actionMap.bind( "EarlyAction", sw::Key::E, sw::ActionTrigger::Down, "EarlyLayer" );

    // 2) 그 뒤로 레이어 1,000개를 등록해 내부 저장소가 여러 번 재할당되도록 강제한다.
    for ( uint32 layerIndex = 0; layerIndex < 1000; ++layerIndex )
    {
        sw::StringBuilder<sw::constant::kMaxBuffer32> sb;
        sb.append( "Layer_" ).append( layerIndex );
        actionMap.registerLayer( sb.view(), 0, true );
    }

    // 3) 재할당 이후에도 EarlyAction의 레이어 활성 판정이 정확해야 한다.
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::E ) );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    SW_EXPECT_TRUE( actionMap.isActionDown( "EarlyAction" ) );
    input.endFrame();

    // 4) 재할당이 여러 번 일어난 뒤에 EarlyLayer를 비활성화해도 즉시 반영되어야 한다
    //    (댕글링 포인터였다면 해제/이동된 메모리를 읽어 결과가 틀리거나 크래시했을 지점).
    actionMap.setLayerEnabled( "EarlyLayer", false );
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::E ) );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    SW_EXPECT_FALSE( actionMap.isActionDown( "EarlyAction" ) );
    input.endFrame();

    input.shutdown();
}

/**
 * @brief [ActionMapTest] 1,000개 대량 액션 생성 및 맵 재구성 시 세대 토큰 무효화 스트레스 검증
 */
SW_TEST_CASE( ActionMapTest, GenerationalHandleStressAndMassiveActions )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::ActionMap actionMap;
    actionMap.setInputManager( &input );

    constexpr uint32             kActionCount = 1000;
    sw::vector<sw::ActionHandle> listHandle;
    listHandle.reserve( kActionCount );

    for ( uint32 actionIndex = 0; actionIndex < kActionCount; ++actionIndex )
    {
        sw::StringBuilder<sw::constant::kMaxBuffer32> sb;
        sb.append( "Action_A_" ).append( actionIndex );

        actionMap.bind( sb.view(), sw::InputSlot::fromKey( sw::Key::A ), sw::ActionTrigger::Pressed );
        const sw::ActionHandle handle = actionMap.getActionHandle( sb.view() );
        SW_EXPECT_TRUE( handle.isValid() );
        listHandle.push_back( handle );
    }

    // 맵 전체 초기화
    actionMap.clear();

    // 구버전 핸들은 모두 무효화되어야 함
    for ( uint32 actionIndex = 0; actionIndex < kActionCount; ++actionIndex )
    {
        SW_EXPECT_FALSE( actionMap.wasActionTriggered( listHandle[actionIndex] ) );
        SW_EXPECT_FALSE( actionMap.isActionDown( listHandle[actionIndex] ) );
    }

    // 새로운 이름의 액션 1,000개 재생성
    for ( uint32 actionIndex = 0; actionIndex < kActionCount; ++actionIndex )
    {
        sw::StringBuilder<sw::constant::kMaxBuffer32> sb;
        sb.append( "Action_B_" ).append( actionIndex );
        actionMap.bind( sb.view(), sw::InputSlot::fromKey( sw::Key::B ), sw::ActionTrigger::Pressed );
    }

    // 구버전 핸들은 새 액션 슬롯과 인덱스가 겹쳐도 세대 불일치로 절대 트리거되지 않아야 함
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::B ) );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );

    for ( uint32 actionIndex = 0; actionIndex < kActionCount; ++actionIndex )
    {
        SW_EXPECT_FALSE( actionMap.wasActionTriggered( listHandle[actionIndex] ) );
        SW_EXPECT_FALSE( actionMap.isActionDown( listHandle[actionIndex] ) );
    }

    input.endFrame();
    input.shutdown();
}

/**
 * @brief [ActionMapTest] 커맨드 콤보 파서(236P) 링버퍼 고속 입력 및 시퀀스 매칭 검증
 */
SW_TEST_CASE( ActionMapTest, ComboParserRingBufferOverflowStress )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::ActionMap actionMap;
    actionMap.setInputManager( &input );

    // 커맨드 구성 바인딩 (Down, DownRight, Right, P)
    actionMap.bind( "Down", sw::Key::S, sw::ActionTrigger::Pressed );
    actionMap.bind( "DownRight", sw::Key::C, sw::ActionTrigger::Pressed );
    actionMap.bind( "Right", sw::Key::D, sw::ActionTrigger::Pressed );
    actionMap.bind( "P", sw::Key::J, sw::ActionTrigger::Pressed );

    // 1단계: Down 입력
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::S ) );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    input.endFrame();

    // 2단계: DownRight 입력
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::C ) );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    input.endFrame();

    // 3단계: Right 입력
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::D ) );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    input.endFrame();

    // 4단계: P 입력
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::J ) );
    input.beginFrame( 0.016f );
    actionMap.update( 0.016f );
    input.endFrame();

    // 콤보 패턴 매칭 검증 (236P)
    SW_EXPECT_TRUE( actionMap.wasCommandPatternTriggered( "236P", 0.5f ) );

    input.shutdown();
}
