#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/EngineLoop.h"
#include "Engine/Input/Events/RawInputEvent.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"

#include "TestFramework/TestFramework.h"

#include <thread>

// InputMap — 바인딩 · 레이어 · 조합 키 · 벡터 축 합성. 장치가 아니라 **매핑 규칙**을 본다.
/**
 * @brief [InputMapTest] 같은 레이어에서 이미 쓰는 키를 찾아낸다.
 * @details 에디터의 Rebind(`InputMapEditorPanel::rebindSelectedAction`)가 이것을 불러 이미 쓰는 키로 바꾸는 것을 알린다.
 *          계약을 여기서 고정한다.
 */
SW_TEST_CASE( InputMapTest, DetectsBindingConflictInSameLayer )
{
    sw::InputMap inputMap;
    inputMap.registerLayer( "Gameplay", 0, true, false, false );
    inputMap.registerLayer( "Menu", 10, true, false, false );

    inputMap.bind( "Jump", sw::Key::Space, sw::ActionTrigger::Pressed, "Gameplay" );
    inputMap.bind( "Confirm", sw::Key::Enter, sw::ActionTrigger::Pressed, "Menu" );

    sw::string conflicting;

    // 같은 레이어에서 이미 쓰는 키다.
    SW_EXPECT_TRUE( inputMap.hasBindingConflict( sw::InputSlot::fromKey( sw::Key::Space ), "Gameplay", conflicting ) );
    SW_EXPECT_EQUAL( sw::string( "Jump" ), conflicting );

    // 아무도 안 쓰는 키는 충돌이 아니다.
    conflicting.clear();
    SW_EXPECT_FALSE( inputMap.hasBindingConflict( sw::InputSlot::fromKey( sw::Key::F1 ), "Gameplay", conflicting ) );

    // 레이어가 다르면 같은 키라도 충돌이 아니다 — 레이어가 있는 이유가 그것이다.
    conflicting.clear();
    SW_EXPECT_FALSE( inputMap.hasBindingConflict( sw::InputSlot::fromKey( sw::Key::Space ), "Menu", conflicting ) );
}

/**
 * @brief [InputMapTest] default.input.xml 리소스 로드 및 레이어/액션/코드 바인딩 무결성 검증
 */
SW_TEST_CASE( InputMapTest, LoadFromDefaultInputXmlResource )
{
    sw::InputManager inputManager;
    SW_EXPECT_TRUE( inputManager.initialize() );

    sw::InputMap inputMap;
    inputMap.setInputManager( &inputManager );
    SW_EXPECT_TRUE( inputMap.loadFromResource( "engine/input/default.input.xml" ) );

    SW_EXPECT_TRUE( inputMap.hasLayer( "Title" ) );
    SW_EXPECT_TRUE( inputMap.hasLayer( "Gameplay" ) );
    SW_EXPECT_TRUE( inputMap.hasLayer( "Debug" ) );

    SW_EXPECT_TRUE( inputMap.hasAction( "Confirm" ) );
    SW_EXPECT_TRUE( inputMap.hasAction( "Continue" ) );
    SW_EXPECT_TRUE( inputMap.hasAction( "Cancel" ) );
    SW_EXPECT_TRUE( inputMap.hasAction( "ReloadEditor" ) );

    inputManager.shutdown();
}

/**
 * @brief [InputMapTest] getGlyphForAction 디바이스별 및 코드/축 조합 글리프 포맷 검증
 */
SW_TEST_CASE( InputMapTest, GlyphResolutionWithGlyphStyleAndChords )
{
    sw::InputManager inputManager;
    SW_EXPECT_TRUE( inputManager.initialize() );

    sw::InputMap& inputMap = inputManager.getInputMap();

    inputMap.bind( "Interact", sw::Key::E );
    inputMap.bind( "Fire", sw::MouseButton::Left );
    inputMap.bind( "Jump", sw::GamepadButton::A );
    inputMap.bindChord( "QuickSave", sw::Key::LeftControl, sw::Key::S );

    inputManager.setActiveGlyphStyle( sw::InputGlyphStyle::KeyboardMouse );
    const sw::string glyphInteract = inputMap.getGlyphForAction( "Interact" );
    SW_EXPECT_TRUE( glyphInteract.find( "E" ) != sw::string::npos );

    const sw::string glyphFire = inputMap.getGlyphForAction( "Fire" );
    SW_EXPECT_TRUE( glyphFire.find( "Left" ) != sw::string::npos );

    const sw::string glyphSave = inputMap.getGlyphForAction( "QuickSave" );
    SW_EXPECT_TRUE( glyphSave.find( "LeftControl" ) != sw::string::npos );
    SW_EXPECT_TRUE( glyphSave.find( "S" ) != sw::string::npos );

    inputManager.setActiveGlyphStyle( sw::InputGlyphStyle::GamepadXbox );
    const sw::string glyphJumpXbox = inputMap.getGlyphForAction( "Jump" );
    SW_EXPECT_TRUE( glyphJumpXbox.find( "A" ) != sw::string::npos );

    inputManager.setActiveGlyphStyle( sw::InputGlyphStyle::GamepadPlayStation );
    const sw::string glyphJumpPS = inputMap.getGlyphForAction( "Jump" );
    SW_EXPECT_TRUE( glyphJumpPS.find( "X" ) != sw::string::npos );

    inputManager.shutdown();
}

/**
 * @brief [InputMapTest] MouseDelta2D FPS 룩 벡터 바인딩 검증
 */
SW_TEST_CASE( InputMapTest, MouseDeltaLookBinding )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::InputMap& inputMap = input.getInputMap();
    inputMap.bindMouseDelta( "Look", 2.0f );

    input.postRawEvent( sw::RawInputEvent::makeMouseMove( 10, 5 ) );
    input.beginFrame( 0.016f );

    const sw::float2 lookVec = inputMap.getVector2D( "Look" );
    SW_EXPECT_TRUE( lookVec._x != 0.0f || lookVec._y != 0.0f );

    input.shutdown();
}

/**
 * @brief [InputMapTest] 다중 수정자 복합 단축키(Shortcut) 바인딩 검증
 */
SW_TEST_CASE( InputMapTest, MultiModifierShortcutBinding )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::InputMap& inputMap = input.getInputMap();
    inputMap.bindShortcut( "SaveAs", sw::Key::S, sw::ModifierKey::Ctrl | sw::ModifierKey::Shift );

    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::S ) );
    input.beginFrame( 0.016f );
    SW_EXPECT_FALSE( inputMap.wasActionTriggered( "SaveAs" ) );
    input.endFrame();

    input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::S ) );
    input.beginFrame( 0.016f );
    input.endFrame();

    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::LeftControl ) );
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::LeftShift ) );
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::S, 0, false, sw::ModifierKey::Ctrl | sw::ModifierKey::Shift ) );
    input.beginFrame( 0.016f );
    SW_EXPECT_TRUE( inputMap.wasActionTriggered( "SaveAs" ) );

    input.shutdown();
}

/**
 * @brief [InputMapTest] AnyKey 타이틀 화면 바인딩 검증
 */
SW_TEST_CASE( InputMapTest, AnyKeyBinding )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::InputMap& inputMap = input.getInputMap();
    inputMap.bindAnyKey( "PressAnyKeyToStart" );

    input.beginFrame( 0.016f );
    SW_EXPECT_FALSE( inputMap.wasActionTriggered( "PressAnyKeyToStart" ) );
    input.endFrame();

    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Space ) );
    input.beginFrame( 0.016f );
    SW_EXPECT_TRUE( inputMap.wasActionTriggered( "PressAnyKeyToStart" ) );

    input.shutdown();
}

/**
 * @brief [InputMapTest] 가상 조이스틱(마우스 드래그) 바인딩의 플로팅 앵커·데드존·리셋 검증
 */
SW_TEST_CASE( InputMapTest, VirtualJoystickDragBinding )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::InputMap& inputMap = input.getInputMap();
    inputMap.bindVirtualJoystick2D( "Move", sw::MouseButton::Left, 100.0f, 0.1f );

    // 1) 버튼을 누르지 않은 상태에서는 0벡터
    input.beginFrame( 0.016f );
    sw::float2 idleVec = inputMap.getVector2D( "Move" );
    SW_EXPECT_NEAR_EQUAL( 0.0f, idleVec._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, idleVec._y, 0.001f );
    input.endFrame();

    // 2) (200,200)에서 누르면 그 지점이 앵커가 되고, 아직 같은 지점이라 0벡터
    input.postRawEvent( sw::RawInputEvent::makeMouseButtonDown( sw::MouseButton::Left, 200, 200 ) );
    input.beginFrame( 0.016f );
    sw::float2 anchoredVec = inputMap.getVector2D( "Move" );
    SW_EXPECT_NEAR_EQUAL( 0.0f, anchoredVec._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, anchoredVec._y, 0.001f );
    input.endFrame();

    // 3) 앵커(200,200)에서 (300,200)으로 드래그 → +X 방향 최대치(반경 100 도달)
    input.postRawEvent( sw::RawInputEvent::makeMouseMove( 300, 200 ) );
    input.beginFrame( 0.016f );
    sw::float2 dragVec = inputMap.getVector2D( "Move" );
    SW_EXPECT_NEAR_EQUAL( 1.0f, dragVec._x, 0.01f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, dragVec._y, 0.01f );
    input.endFrame();

    // 4) 버튼을 떼면 즉시 0벡터로 리셋
    input.postRawEvent( sw::RawInputEvent::makeMouseButtonUp( sw::MouseButton::Left, 300, 200 ) );
    input.beginFrame( 0.016f );
    sw::float2 releasedVec = inputMap.getVector2D( "Move" );
    SW_EXPECT_NEAR_EQUAL( 0.0f, releasedVec._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, releasedVec._y, 0.001f );
    input.endFrame();

    // 5) 다른 위치(50,50)에서 다시 누르면 앵커가 새 위치로 플로팅되어 다시 0벡터
    input.postRawEvent( sw::RawInputEvent::makeMouseButtonDown( sw::MouseButton::Left, 50, 50 ) );
    input.beginFrame( 0.016f );
    sw::float2 reAnchoredVec = inputMap.getVector2D( "Move" );
    SW_EXPECT_NEAR_EQUAL( 0.0f, reAnchoredVec._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, reAnchoredVec._y, 0.001f );
    input.endFrame();

    input.shutdown();
}

/**
 * @brief [InputMapTest] 리바인딩 충돌 해결(Swap / Override / AddSecondary) 검증
 */
SW_TEST_CASE( InputMapTest, RebindConflictResolution )
{
    sw::InputMap inputMap;
    inputMap.bind( "ActionA", sw::Key::F );
    inputMap.bind( "ActionB", sw::Key::G );

    const bool bSwapOk = inputMap.rebindWithResolution( "ActionB", sw::InputSlot::fromKey( sw::Key::F ), sw::ConflictResolution::Swap );
    SW_EXPECT_TRUE( bSwapOk );
    const sw::ActionBinding* pBindB = inputMap.getBinding( "ActionB", 0 );
    const sw::ActionBinding* pBindA = inputMap.getBinding( "ActionA", 0 );
    SW_EXPECT_TRUE( pBindB != nullptr && pBindB->_arrSlot[0]._controlIndex == static_cast<uint16>( sw::Key::F ) );
    SW_EXPECT_TRUE( pBindA != nullptr && pBindA->_arrSlot[0]._controlIndex == static_cast<uint16>( sw::Key::G ) );

    const bool bOverrideOk = inputMap.rebindWithResolution( "ActionA", sw::InputSlot::fromKey( sw::Key::F ), sw::ConflictResolution::Override );
    SW_EXPECT_TRUE( bOverrideOk );
    pBindA = inputMap.getBinding( "ActionA", 0 );
    pBindB = inputMap.getBinding( "ActionB", 0 );
    SW_EXPECT_TRUE( pBindA != nullptr && pBindA->_arrSlot[0]._controlIndex == static_cast<uint16>( sw::Key::F ) );
    SW_EXPECT_TRUE( pBindB != nullptr && pBindB->_arrSlot[0]._controlIndex == static_cast<uint16>( sw::Key::Unknown ) );

    const bool bAddOk = inputMap.rebindWithResolution( "ActionB", sw::InputSlot::fromKey( sw::Key::F ), sw::ConflictResolution::AddSecondary );
    SW_EXPECT_TRUE( bAddOk );
    SW_EXPECT_EQUAL( 2u, inputMap.getBindingCount( "ActionB" ) );
}

/**
 * @brief [InputMapTest] DebugActionState 실시간 덤프 검증
 */
SW_TEST_CASE( InputMapTest, DebugActionStatesDump )
{
    sw::InputMap inputMap;
    inputMap.bind( "Jump", sw::Key::Space );
    inputMap.bind( "Fire", sw::MouseButton::Left );

    sw::vector<sw::DebugActionState> listState;
    inputMap.getDebugActionStates( listState );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listState.size() ) );
}

/**
 * @brief [InputMapTest] ActionHandle 기반 Zero-Lookup O(1) 액션 상태 폴링 검증
 */
SW_TEST_CASE( InputMapTest, ActionHandleZeroLookup )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::InputMap& inputMap = input.getInputMap();
    inputMap.bind( "Fire", sw::Key::Space );
    inputMap.bindVector2D( "Move", sw::Key::W, sw::Key::S, sw::Key::A, sw::Key::D );

    const sw::ActionHandle hFire = inputMap.getActionHandle( "Fire" );
    const sw::ActionHandle hMove = inputMap.getActionHandle( "Move" );

    SW_EXPECT_TRUE( hFire.isValid() );
    SW_EXPECT_TRUE( hMove.isValid() );

    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Space ) );
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::W ) );
    input.beginFrame( 0.016f );

    SW_EXPECT_TRUE( inputMap.wasActionTriggered( hFire ) );
    SW_EXPECT_TRUE( inputMap.isActionDown( hFire ) );
    SW_EXPECT_TRUE( inputMap.wasActionPressed( hFire ) );
    SW_EXPECT_FALSE( inputMap.wasActionReleased( hFire ) );

    const sw::float2 moveVec = inputMap.getVector2D( hMove );
    SW_EXPECT_NEAR_EQUAL( 0.0f, moveVec._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, moveVec._y, 0.001f );
    SW_EXPECT_EQUAL( static_cast<uint32>( sw::ActionPhase::Triggered ), static_cast<uint32>( inputMap.getActionPhase( hFire ) ) );

    input.shutdown();
}

/**
 * @brief [InputMapTest] 2D 벡터 합성 WASD 대각선 정규화 모드(Circular vs IndependentAxes) 검증
 */
SW_TEST_CASE( InputMapTest, DigitalNormalizationModes )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::InputMap& inputMap = input.getInputMap();
    inputMap.bindVector2D( "Move", sw::Key::W, sw::Key::S, sw::Key::A, sw::Key::D );

    // 1) IndependentAxes 모드: W + D 대각선 입력 시 X=1.0, Y=1.0 유지
    inputMap.setDigitalNormalization( sw::DigitalNormalization::IndependentAxes );
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::W ) );
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::D ) );
    input.beginFrame( 0.016f );

    sw::float2 vecIndep = inputMap.getVector2D( "Move" );
    SW_EXPECT_NEAR_EQUAL( 1.0f, vecIndep._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, vecIndep._y, 0.001f );
    input.endFrame();

    // 2) Circular 모드: W + D 대각선 입력 시 단위 원(길이 1.0)으로 정규화
    inputMap.setDigitalNormalization( sw::DigitalNormalization::Circular );
    input.beginFrame( 0.016f );

    sw::float2    vecCirc = inputMap.getVector2D( "Move" );
    const float32 len     = sw::MathUtil::sqrt( vecCirc._x * vecCirc._x + vecCirc._y * vecCirc._y );
    SW_EXPECT_NEAR_EQUAL( 1.0f, len, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.7071f, vecCirc._x, 0.01f );
    SW_EXPECT_NEAR_EQUAL( 0.7071f, vecCirc._y, 0.01f );

    input.shutdown();
}

/**
 * @brief [InputMapTest] 링버퍼 기반 선입력 및 커맨드 히스토리 제로 할당 래핑 검증
 */
SW_TEST_CASE( InputMapTest, RingBufferZeroAllocation )
{
    sw::InputMap inputMap;

    // 1) 선입력 버퍼링 16개 초과 주입 (오버플로우 링 래핑)
    for ( uint32 index = 0; index < 20; ++index )
    {
        sw::StringBuilder<sw::constant::kMaxBuffer32> sb;
        sb.append( "Action_" ).append( index );
        inputMap.bufferAction( sw::hashed_string( sb.view() ), 0.5f );
    }
    SW_EXPECT_TRUE( inputMap.consumeBufferedAction( "Action_19" ) );
    SW_EXPECT_FALSE( inputMap.consumeBufferedAction( "Action_0" ) ); // 0번은 래핑으로 덮어씌워짐

    // 2) 시간 경과 후 만료 테스트
    inputMap.update( 0.6f );
    SW_EXPECT_FALSE( inputMap.consumeBufferedAction( "Action_19" ) );
}

/**
 * @brief [InputMapTest] 넘패드 표기법 기반 격투 커맨드 콤보 패턴(236P, 623P) 검증
 */
SW_TEST_CASE( InputMapTest, CommandPatternFuzzyCombo )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::InputMap& inputMap = input.getInputMap();
    inputMap.bind( "Down", sw::Key::S );
    inputMap.bind( "DownRight", sw::Key::C );
    inputMap.bind( "Right", sw::Key::D );
    inputMap.bind( "Punch", sw::Key::J );

    // 1) 2 (Down) -> 3 (DownRight) -> 6 (Right) -> Punch (236P 파동권) 순차 입력
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::S ) );
    input.beginFrame( 0.05f );
    input.endFrame();

    input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::S ) );
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::C ) );
    input.beginFrame( 0.05f );
    input.endFrame();

    input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::C ) );
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::D ) );
    input.beginFrame( 0.05f );
    input.endFrame();

    input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::D ) );
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::J ) );
    input.beginFrame( 0.05f );
    input.endFrame();

    SW_EXPECT_TRUE( inputMap.wasCommandPatternTriggered( "236Punch", 0.5f ) );
    SW_EXPECT_FALSE( inputMap.wasCommandPatternTriggered( "623Punch", 0.5f ) );

    input.shutdown();
}

/**
 * @brief [InputMapTest] 바인딩 종류 표가 **비어 있는 칸 없이** 모든 종류를 덮는가
 * @details 아래 `SaveAndLoadAllBindingKinds` 는 아홉 종류를 **손으로 나열한다** — 열 번째를 더한
 *          사람이 그 목록도 같이 고쳐야 하므로, 새 종류는 그 케이스가 덮어 주지 못한다. 이 케이스는
 *          `BindingKind::Count` 까지 돌면서 묻기 때문에 **더하는 순간 자동으로 적용된다.**
 *
 *          세 가지를 본다:
 *          1. 모든 종류가 XML 이름을 갖는가 — 이름이 비면 저장은 `kind=""` 를 적고 로드는 못 읽는다.
 *          2. 이름이 값으로 되돌아오는가(`fromName(toName(k)) == k`) — 저장과 로드가 같은 표를 쓰는지.
 *          3. 이름이 서로 다른가 — 표 한 줄을 복사해 붙이고 이름만 안 고치면 두 종류가 한 이름을
 *             공유하고, 그때 `fromName` 은 **먼저 나오는 쪽**을 돌려준다(조용히 다른 종류가 된다).
 *             `static_assert` 는 줄 수만 세므로 이것은 잡지 못한다 — 그래서 테스트가 필요하다.
 *          4. 충돌 슬롯 수가 슬롯 배열(4칸)을 넘지 않는가 — 넘으면 충돌 검사가 배열 밖을 읽는다.
 */
SW_TEST_CASE( InputMapTest, BindingKindTableCoversEveryKind )
{
    const uint32 kindCount = static_cast<uint32>( sw::BindingKind::Count );
    SW_ASSERT_TRUE( kindCount > 0 );

    sw::vector<sw::string> listVisitedName;
    for ( uint32 kindIndex = 0; kindIndex < kindCount; ++kindIndex )
    {
        const sw::BindingKind kind  = static_cast<sw::BindingKind>( kindIndex );
        const utf8* const     pName = sw::BindingKinds::toName( kind );

        SW_ASSERT_NOT_NULL( pName );
        SW_EXPECT_TRUE_MSG( pName[0] != 0, "XML 이름이 없는 바인딩 종류가 있습니다 — 표에 줄을 빠뜨렸습니다" );

        // 저장이 적은 이름을 로드가 같은 종류로 되돌려야 한다.
        SW_EXPECT_TRUE_MSG( sw::BindingKinds::fromName( pName ) == kind,
                            "toName/fromName 이 서로의 역이 아닙니다 — 저장한 파일을 못 읽습니다" );

        for ( const sw::string& visitedName : listVisitedName )
        {
            SW_EXPECT_TRUE_MSG( visitedName != pName,
                                "두 바인딩 종류가 같은 XML 이름을 씁니다 — 나중 것이 조용히 앞 것으로 읽힙니다" );
        }
        listVisitedName.push_back( sw::string( pName ) );

        // 충돌 검사는 이 수만큼 `_arrSlot` 을 훑는다. 배열은 4칸이다.
        SW_EXPECT_TRUE_MSG( sw::BindingKinds::getConflictSlotCount( kind ) <= 4,
                            "충돌 슬롯 수가 슬롯 배열보다 큽니다 — 배열 밖을 읽습니다" );
    }

    // 모르는 이름은 Count 로 돌아와야 한다 — 그래야 로드가 "모르는 종류" 를 구분해 소리를 낸다.
    SW_EXPECT_TRUE( sw::BindingKinds::fromName( "nosuchkind" ) == sw::BindingKind::Count );
    SW_EXPECT_TRUE( sw::BindingKinds::fromName( "" ) == sw::BindingKind::Count );
}

/**
 * @brief [InputMapTest] XML 유저 바인딩 전면 직렬화 및 역직렬화 검증 (모든 BindingKind)
 */
SW_TEST_CASE( InputMapTest, SaveAndLoadAllBindingKinds )
{
    sw::InputMap mapSave;
    mapSave.bind( "SingleKey", sw::Key::E );
    mapSave.bindAxis1DComposite( "MoveX", sw::Key::A, sw::Key::D );
    mapSave.bindVector2D( "Move2D", sw::Key::W, sw::Key::S, sw::Key::A, sw::Key::D, 0.1f );
    mapSave.bindGamepadStick2D( "LookStick", sw::GamepadStick::Right, 0.2f, {}, 0, 0.95f, 1.5f );
    mapSave.bindMouseDelta( "LookMouse", 2.5f );
    mapSave.bindChord( "ChordAction", sw::Key::LeftControl, sw::Key::K );
    mapSave.bindShortcut( "ShortcutAction", sw::Key::S, sw::ModifierKey::Ctrl | sw::ModifierKey::Shift );
    mapSave.bindAnyKey( "AnyKeyAction" );
    mapSave.bindVirtualJoystick2D( "MoveJoystick", sw::MouseButton::Right, 80.0f, 0.2f, {}, 0.9f );

    const sw::string savePath = test::makeTempPath( "test_all_user_bindings.xml" );
    SW_EXPECT_TRUE( mapSave.saveUserBindings( savePath ) );

    sw::InputMap mapLoad;
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
}

/**
 * @brief [InputMapTest] 유저 바인딩의 패드 번호 · 수정 키 마스크가 범위를 벗어나면 그 바인딩을 버리고 알린다 — 감아서 엉뚱한 패드에 묶지 않는다
 * @details `static_cast<uint8>( getAttributeInt( … ) )` 로 읽으면 `pad="256"` 은 0 번, `pad="-1"` 은 255 번 패드가 되고
 *          (`pad="4"` 는 없는 패드), `modifierMask="257"` 은 Ctrl 이 된다 — 모두 말없이. 패드 번호는 슬롯 수(`kMaxGamepadSlot`),
 *          마스크는 아는 비트(`ModifierKey::All`) 안에서만 받고(`XmlNode::tryGetAttributeIntInRange`), 벗어나면 경고하고 그 바인딩을 버린다.
 */
SW_TEST_CASE( InputMapTest, UserBindingsRejectOutOfRangePadAndModifierMask )
{
    const sw::string stickKind    = sw::BindingKinds::toName( sw::BindingKind::GamepadStick2D );
    const sw::string shortcutKind = sw::BindingKinds::toName( sw::BindingKind::Shortcut );
    const sw::string singleKind   = sw::BindingKinds::toName( sw::BindingKind::SingleSlot );
    const sw::string keyS         = sw::KeyCodeUtil::toName( sw::Key::S );

    sw::string xml = "<UserBindings>";
    xml += "<bind action=\"StickOk\" kind=\"" + stickKind + "\" stick=\"Right\" pad=\"3\"/>";
    xml += "<bind action=\"StickWrapsHigh\" kind=\"" + stickKind + "\" pad=\"256\"/>";
    xml += "<bind action=\"StickWrapsLow\" kind=\"" + stickKind + "\" pad=\"-1\"/>";
    xml += "<bind action=\"StickNoSuchPad\" kind=\"" + stickKind + "\" pad=\"4\"/>";
    xml += "<bind action=\"ShortcutOk\" kind=\"" + shortcutKind + "\" key=\"" + keyS + "\" modifierMask=\"3\"/>";
    xml += "<bind action=\"ShortcutWraps\" kind=\"" + shortcutKind + "\" key=\"" + keyS + "\" modifierMask=\"257\"/>";
    xml += "<bind action=\"PadButtonOk\" kind=\"" + singleKind + "\" source=\"gamepad\" code=\"A\" pad=\"2\"/>";
    xml += "<bind action=\"PadButtonWraps\" kind=\"" + singleKind + "\" source=\"gamepad\" code=\"A\" pad=\"256\"/>";
    xml += "</UserBindings>";

    const sw::string path = test::makeTempPath( "range_checked_user_bindings.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( path, xml ) );

    test::ScopedLogCollector logs;
    sw::InputMap             inputMap;
    {
        SW_TEST_DEFENSIVE_SCOPE( "out-of-range pad index and modifier mask in user bindings" );
        SW_ASSERT_TRUE( inputMap.loadUserBindings( path ) );
    }

    // 범위 안의 값은 그대로 읽힌다.
    const sw::ActionBinding* pStick = inputMap.getBinding( "StickOk", 0 );
    SW_ASSERT_NOT_NULL( pStick );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( pStick->_deviceIndex ) );
    const sw::ActionBinding* pShortcut = inputMap.getBinding( "ShortcutOk", 0 );
    SW_ASSERT_NOT_NULL( pShortcut );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( pShortcut->_modifierMask ) );
    const sw::ActionBinding* pButton = inputMap.getBinding( "PadButtonOk", 0 );
    SW_ASSERT_NOT_NULL( pButton );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( pButton->_arrSlot[0]._deviceIndex ) );

    // 벗어난 값은 감기지 않는다 — 그 바인딩이 없다.
    SW_EXPECT_TRUE_MSG( inputMap.hasAction( "StickWrapsHigh" ) == false, "pad=\"256\" 이 0 번 패드로 감겼습니다" );
    SW_EXPECT_TRUE_MSG( inputMap.hasAction( "StickWrapsLow" ) == false, "pad=\"-1\" 이 255 번 패드로 감겼습니다" );
    SW_EXPECT_TRUE_MSG( inputMap.hasAction( "StickNoSuchPad" ) == false, "없는 4 번 패드에 묶였습니다" );
    SW_EXPECT_TRUE_MSG( inputMap.hasAction( "ShortcutWraps" ) == false, "modifierMask=\"257\" 이 Ctrl 로 감겼습니다" );
    SW_EXPECT_TRUE_MSG( inputMap.hasAction( "PadButtonWraps" ) == false, "단일 버튼의 pad=\"256\" 이 0 번 패드로 감겼습니다" );

    // 버린 것마다 한 줄씩 알린다.
    SW_EXPECT_TRUE_MSG( logs.countContaining( "attribute 'pad'" ) == 4, logs.joined().c_str() );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "attribute 'modifierMask'" ) == 1, logs.joined().c_str() );
}

/**
 * @brief [InputMapTest] 유저 바인딩은 저장 쪽이 쓰는 모양만 읽는다 — 종류(kind)가 없거나 단일 슬롯의 특성 이름이 다르면 버리고 알린다
 * @details 저장 쪽(`saveUserBindings`)은 늘 `kind` 를 쓰고, 단일 슬롯은 `source="key" key=…` · `source="mouse" button=…` ·
 *          `source="gamepad" code=… pad=…` 이다. 읽는 쪽이 `kind` 없는 바인딩과 `source="key" code=…` 같은 저장하지 않는 모양까지
 *          짐작해 읽으면, 저장 형식이 바뀌어도 시험이 그 차이를 보지 못한다.
 */
SW_TEST_CASE( InputMapTest, UserBindingsReadOnlyTheSavedShape )
{
    const sw::string singleKind = sw::BindingKinds::toName( sw::BindingKind::SingleSlot );

    sw::string xml = "<UserBindings>";
    xml += "<bind action=\"SavedKey\" kind=\"" + singleKind + "\" source=\"key\" key=\"A\"/>";
    xml += "<bind action=\"NoKind\" source=\"key\" key=\"A\"/>";
    xml += "<bind action=\"UnknownKind\" kind=\"NoSuchKind\" source=\"key\" key=\"A\"/>";
    xml += "<bind action=\"CodeForKey\" kind=\"" + singleKind + "\" source=\"key\" code=\"A\"/>";
    xml += "<bind action=\"NoSource\" kind=\"" + singleKind + "\" key=\"A\"/>";
    xml += "</UserBindings>";

    const sw::string path = test::makeTempPath( "saved_shape_user_bindings.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( path, xml ) );

    test::ScopedLogCollector logs;
    sw::InputMap             inputMap;
    {
        SW_TEST_DEFENSIVE_SCOPE( "user bindings without kind or with an unsaved single-slot shape" );
        SW_ASSERT_TRUE( inputMap.loadUserBindings( path ) );
    }

    SW_EXPECT_TRUE( inputMap.hasAction( "SavedKey" ) );
    SW_EXPECT_TRUE_MSG( inputMap.hasAction( "NoKind" ) == false, "kind 없는 바인딩을 짐작해 읽었습니다" );
    SW_EXPECT_TRUE_MSG( inputMap.hasAction( "UnknownKind" ) == false, "모르는 kind 를 단일 슬롯으로 읽었습니다" );
    SW_EXPECT_TRUE_MSG( inputMap.hasAction( "CodeForKey" ) == false, "source=key 의 code 특성(저장하지 않는 모양)을 읽었습니다" );
    SW_EXPECT_TRUE_MSG( inputMap.hasAction( "NoSource" ) == false, "source 없는 단일 슬롯을 읽었습니다" );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "action=NoKind" ) == 1, logs.joined().c_str() );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "action=UnknownKind" ) == 1, logs.joined().c_str() );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "action=NoSource" ) == 1, logs.joined().c_str() );
}

/**
 * @brief [InputMapTest] 유저 바인딩의 특성이 빠져도 죽지 않는다 — 모든 바인딩 종류에서 kind 만 있는 바인딩을 읽는다
 * @details 종류별 읽기는 `XmlNode::getAttributeText`(없으면 빈 글)를 넘긴다. `node.findAttribute( … )` 를 그대로 넘기면 특성이 없을 때
 *          nullptr 로 `string_view` 를 만들어 strlen(nullptr) 에서 죽는다.
 */
SW_TEST_CASE( InputMapTest, UserBindingsWithMissingAttributesDoNotCrash )
{
    sw::string xml = "<UserBindings>";
    for ( uint32 kindIndex = 0; kindIndex < static_cast<uint32>( sw::BindingKind::Count ); ++kindIndex )
    {
        const sw::BindingKind kind = static_cast<sw::BindingKind>( kindIndex );
        xml += "<bind action=\"Bare" + sw::string( sw::BindingKinds::toName( kind ) ) + "\" kind=\"" + sw::BindingKinds::toName( kind ) + "\"/>";
    }
    xml += "<bind action=\"BareSingleKey\" kind=\"" + sw::string( sw::BindingKinds::toName( sw::BindingKind::SingleSlot ) ) + "\" source=\"key\"/>";
    xml += "<bind action=\"BareSingleMouse\" kind=\"" + sw::string( sw::BindingKinds::toName( sw::BindingKind::SingleSlot ) ) + "\" source=\"mouse\"/>";
    xml += "<bind action=\"BareSingleGamepad\" kind=\"" + sw::string( sw::BindingKinds::toName( sw::BindingKind::SingleSlot ) ) + "\" source=\"gamepad\"/>";
    xml += "</UserBindings>";

    const sw::string path = test::makeTempPath( "missing_attribute_user_bindings.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( path, xml ) );

    sw::InputMap inputMap;
    {
        SW_TEST_DEFENSIVE_SCOPE( "user bindings whose kind-specific attributes are missing" );
        SW_EXPECT_TRUE( inputMap.loadUserBindings( path ) );
    }
    SW_EXPECT_TRUE( inputMap.hasAction( "BareSingleKey" ) == false );
    SW_EXPECT_TRUE( inputMap.hasAction( "BareSingleMouse" ) == false );
    SW_EXPECT_TRUE( inputMap.hasAction( "BareSingleGamepad" ) == false );
}

/**
 * @brief [InputMapTest] 대량 레이어 동적 등록으로 _mapLayer/_listLayerEntry가 여러 번 재할당된 뒤에도
 *        먼저 바인딩된 액션의 ActionBinding::_cachedLayerIndex(레이어 활성 판정 캐시)가 여전히 정확한지 검증.
 */
SW_TEST_CASE( InputMapTest, LayerCacheStableAcrossMassiveDynamicRegistration )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::InputMap& inputMap = input.getInputMap();

    // 1) 먼저 레이어와 액션을 하나 만들어 _cachedLayerIndex가 여기서 캐싱되게 한다.
    inputMap.registerLayer( "EarlyLayer", 0, true );
    inputMap.bind( "EarlyAction", sw::Key::E, sw::ActionTrigger::Down, "EarlyLayer" );

    // 2) 그 뒤로 레이어 1,000개를 등록해 내부 저장소가 여러 번 재할당되도록 강제한다.
    for ( uint32 layerIndex = 0; layerIndex < 1000; ++layerIndex )
    {
        sw::StringBuilder<sw::constant::kMaxBuffer32> sb;
        sb.append( "Layer_" ).append( layerIndex );
        inputMap.registerLayer( sw::hashed_string( sb.view() ), 0, true );
    }

    // 3) 재할당 이후에도 EarlyAction의 레이어 활성 판정이 정확해야 한다.
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::E ) );
    input.beginFrame( 0.016f );
    SW_EXPECT_TRUE( inputMap.isActionDown( "EarlyAction" ) );
    input.endFrame();

    // 4) 재할당이 여러 번 일어난 뒤에 EarlyLayer를 비활성화해도 즉시 반영되어야 한다
    //    (댕글링 포인터였다면 해제/이동된 메모리를 읽어 결과가 틀리거나 크래시했을 지점).
    inputMap.setLayerEnabled( "EarlyLayer", false );
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::E ) );
    input.beginFrame( 0.016f );
    SW_EXPECT_FALSE( inputMap.isActionDown( "EarlyAction" ) );
    input.endFrame();

    input.shutdown();
}

/**
 * @brief [InputMapTest] 1,000개 대량 액션 생성 및 맵 재구성 시 세대 토큰 무효화 스트레스 검증
 */
SW_TEST_CASE( InputMapTest, GenerationalHandleStressAndMassiveActions )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::InputMap inputMap;
    inputMap.setInputManager( &input );

    constexpr uint32             kActionCount = 1000;
    sw::vector<sw::ActionHandle> listHandle;
    listHandle.reserve( kActionCount );

    for ( uint32 actionIndex = 0; actionIndex < kActionCount; ++actionIndex )
    {
        sw::StringBuilder<sw::constant::kMaxBuffer32> sb;
        sb.append( "Action_A_" ).append( actionIndex );

        inputMap.bind( sw::hashed_string( sb.view() ), sw::InputSlot::fromKey( sw::Key::A ), sw::ActionTrigger::Pressed );
        const sw::ActionHandle handle = inputMap.getActionHandle( sw::hashed_string( sb.view() ) );
        SW_EXPECT_TRUE( handle.isValid() );
        listHandle.push_back( handle );
    }

    // 맵 전체 초기화
    inputMap.clear();

    // 구버전 핸들은 모두 무효화되어야 함
    for ( uint32 actionIndex = 0; actionIndex < kActionCount; ++actionIndex )
    {
        SW_EXPECT_FALSE( inputMap.wasActionTriggered( listHandle[actionIndex] ) );
        SW_EXPECT_FALSE( inputMap.isActionDown( listHandle[actionIndex] ) );
    }

    // 새로운 이름의 액션 1,000개 재생성
    for ( uint32 actionIndex = 0; actionIndex < kActionCount; ++actionIndex )
    {
        sw::StringBuilder<sw::constant::kMaxBuffer32> sb;
        sb.append( "Action_B_" ).append( actionIndex );
        inputMap.bind( sw::hashed_string( sb.view() ), sw::InputSlot::fromKey( sw::Key::B ), sw::ActionTrigger::Pressed );
    }

    // 구버전 핸들은 새 액션 슬롯과 인덱스가 겹쳐도 세대 불일치로 절대 트리거되지 않아야 함
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::B ) );
    input.beginFrame( 0.016f );
    inputMap.update( 0.016f );

    for ( uint32 actionIndex = 0; actionIndex < kActionCount; ++actionIndex )
    {
        SW_EXPECT_FALSE( inputMap.wasActionTriggered( listHandle[actionIndex] ) );
        SW_EXPECT_FALSE( inputMap.isActionDown( listHandle[actionIndex] ) );
    }

    input.endFrame();
    input.shutdown();
}

/**
 * @brief [InputMapTest] 커맨드 콤보 파서(236P) 링버퍼 고속 입력 및 시퀀스 매칭 검증
 */
SW_TEST_CASE( InputMapTest, ComboParserRingBufferOverflowStress )
{
    sw::InputManager input;
    SW_EXPECT_TRUE( input.initialize() );

    sw::InputMap inputMap;
    inputMap.setInputManager( &input );

    // 커맨드 구성 바인딩 (Down, DownRight, Right, P)
    inputMap.bind( "Down", sw::Key::S, sw::ActionTrigger::Pressed );
    inputMap.bind( "DownRight", sw::Key::C, sw::ActionTrigger::Pressed );
    inputMap.bind( "Right", sw::Key::D, sw::ActionTrigger::Pressed );
    inputMap.bind( "P", sw::Key::J, sw::ActionTrigger::Pressed );

    // 1단계: Down 입력
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::S ) );
    input.beginFrame( 0.016f );
    inputMap.update( 0.016f );
    input.endFrame();

    // 2단계: DownRight 입력
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::C ) );
    input.beginFrame( 0.016f );
    inputMap.update( 0.016f );
    input.endFrame();

    // 3단계: Right 입력
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::D ) );
    input.beginFrame( 0.016f );
    inputMap.update( 0.016f );
    input.endFrame();

    // 4단계: P 입력
    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::J ) );
    input.beginFrame( 0.016f );
    inputMap.update( 0.016f );
    input.endFrame();

    // 콤보 패턴 매칭 검증 (236P)
    SW_EXPECT_TRUE( inputMap.wasCommandPatternTriggered( "236P", 0.5f ) );

    input.shutdown();
}

/**
 * @brief [InputMapTest] Pulse 는 누르고 있는 동안 간격(0.1 초)마다 한 번 발화하고, 발화 수가 프레임률을 따르지 않는다.
 * @details `타이머 >= 간격` 만 보고 타이머를 되감지 않으면 첫 간격이 지난 뒤 매 프레임 발화해, 연사 무기가 144 fps 에서 초당 144 발, 30 fps 에서
 *          30 발을 쏜다.
 */
SW_TEST_CASE( InputMapTest, PulseTriggerFiresOncePerInterval )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );

    sw::InputMap& inputMap = input.getInputMap();
    inputMap.bind( "Fire", sw::Key::F, sw::ActionTrigger::Pulse );

    // 1.05 초 동안 누른다 — 100 fps 와 30 fps 두 번. 둘 다 10 번이어야 한다(0.1 · 0.2 · … · 1.0 초).
    const float32 arrFrameSecond[] = { 0.01f, 0.035f };
    for ( const float32 frameSecond : arrFrameSecond )
    {
        input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::F ) );
        const uint32 frameCount = static_cast<uint32>( 1.05f / frameSecond + 0.5f );
        uint32       fireCount  = 0;
        for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            input.beginFrame( frameSecond );
            if ( inputMap.wasActionTriggered( "Fire" ) )
                ++fireCount;
            input.endFrame();
        }
        SW_EXPECT_EQUAL( 10u, fireCount );

        input.postRawEvent( sw::RawInputEvent::makeKeyUp( sw::Key::F ) );
        input.beginFrame( frameSecond );
        input.endFrame();
    }

    input.shutdown();
}

/**
 * @brief [InputMapTest] 키를 다시 잡아도 바인딩 종류는 그대로다 — Chord 는 방아쇠만 바뀌고, 합성 축은 키 하나로 바꾸지 않는다
 * @details `rebindKey` 가 어떤 바인딩이든 단일 키로 바꾸면 편집기의 Rebind 로 `Ctrl+S` 를 다시 잡을 때 수식 키가 사라지고, A/D 축을 다시 잡으면 축이
 *          단일 키가 된다.
 */
SW_TEST_CASE( InputMapTest, RebindKeepsTheBindingKind )
{
    sw::InputMap inputMap;

    inputMap.bind( "Jump", sw::Key::Space );
    SW_EXPECT_TRUE( inputMap.rebindKey( "Jump", sw::Key::J ) );
    const sw::ActionBinding* pSingle = inputMap.getBinding( "Jump", 0 );
    SW_ASSERT_NOT_NULL( pSingle );
    SW_EXPECT_TRUE( pSingle->_kind == sw::BindingKind::SingleSlot );
    SW_EXPECT_TRUE( pSingle->_arrSlot[0] == sw::InputSlot::fromKey( sw::Key::J ) );

    inputMap.bindChord( "Save", sw::Key::LeftControl, sw::Key::S );
    SW_EXPECT_TRUE( inputMap.rebindKey( "Save", sw::Key::D ) );
    const sw::ActionBinding* pChord = inputMap.getBinding( "Save", 0 );
    SW_ASSERT_NOT_NULL( pChord );
    SW_EXPECT_TRUE( pChord->_kind == sw::BindingKind::Chord );
    SW_EXPECT_TRUE( pChord->_arrSlot[0] == sw::InputSlot::fromKey( sw::Key::LeftControl ) ); // 수식 키는 그대로
    SW_EXPECT_TRUE( pChord->_arrSlot[1] == sw::InputSlot::fromKey( sw::Key::D ) );

    inputMap.bindAxis1DComposite( "MoveX", sw::Key::A, sw::Key::D );
    {
        test::ScopedDefensiveTestLog expected( "rebinding a composite axis with one key" );
        SW_EXPECT_FALSE( inputMap.rebindKey( "MoveX", sw::Key::Q ) );
    }
    const sw::ActionBinding* pAxis = inputMap.getBinding( "MoveX", 0 );
    SW_ASSERT_NOT_NULL( pAxis );
    SW_EXPECT_TRUE( pAxis->_kind == sw::BindingKind::Axis1DComposite );
    SW_EXPECT_TRUE( pAxis->_arrSlot[0] == sw::InputSlot::fromKey( sw::Key::A ) );
    SW_EXPECT_TRUE( pAxis->_arrSlot[1] == sw::InputSlot::fromKey( sw::Key::D ) );
}

/**
 * @brief [InputMapTest] 충돌 해결 리바인딩도 바인딩 종류를 지킨다 — Chord 는 방아쇠를 바꾸고, 남의 방아쇠 · 축의 키와의 겹침을 알아본다
 * @details `rebindWithResolution` 이 늘 0 번 슬롯에 쓰면 Chord 의 수식 키를 덮고, 겹침도 남의 0 번 슬롯만 보면 Chord 의 방아쇠(1 번)나 축의 양의
 *          키와 겹쳐도 모른다(같은 키가 두 액션에 남는다).
 */
SW_TEST_CASE( InputMapTest, RebindWithResolutionKeepsTheBindingKind )
{
    const auto keySlot = []( sw::Key key )
    { return sw::InputSlot::fromKey( key ); };
    sw::InputMap inputMap;
    inputMap.bindChord( "Save", sw::Key::LeftControl, sw::Key::S );
    inputMap.bind( "Jump", sw::Key::D );
    inputMap.bindAxis1DComposite( "MoveX", sw::Key::A, sw::Key::E );

    // Chord 는 방아쇠가 바뀌고 수식 키는 그대로다.
    SW_EXPECT_TRUE( inputMap.rebindWithResolution( "Save", keySlot( sw::Key::F ), sw::ConflictResolution::Swap ) );
    const sw::ActionBinding* pSave = inputMap.getBinding( "Save", 0 );
    SW_ASSERT_NOT_NULL( pSave );
    SW_EXPECT_TRUE( pSave->_arrSlot[0] == keySlot( sw::Key::LeftControl ) );
    SW_EXPECT_TRUE( pSave->_arrSlot[1] == keySlot( sw::Key::F ) );

    // Jump 를 F 로 — Save 의 방아쇠(1 번)와 겹친다. 맞바꾸면 Save 의 방아쇠가 Jump 의 옛 키 D 가 된다.
    SW_EXPECT_TRUE( inputMap.rebindWithResolution( "Jump", keySlot( sw::Key::F ), sw::ConflictResolution::Swap ) );
    SW_EXPECT_TRUE( inputMap.getBinding( "Jump", 0 )->_arrSlot[0] == keySlot( sw::Key::F ) );
    SW_EXPECT_TRUE( pSave->_arrSlot[0] == keySlot( sw::Key::LeftControl ) );
    SW_EXPECT_TRUE( pSave->_arrSlot[1] == keySlot( sw::Key::D ) );

    // Jump 를 E 로 — 축의 양의 키(1 번)와 겹친다. 맞바꾸면 그 부분이 F 가 된다(축은 그대로 축).
    SW_EXPECT_TRUE( inputMap.rebindWithResolution( "Jump", keySlot( sw::Key::E ), sw::ConflictResolution::Swap ) );
    const sw::ActionBinding* pAxis = inputMap.getBinding( "MoveX", 0 );
    SW_ASSERT_NOT_NULL( pAxis );
    SW_EXPECT_TRUE( pAxis->_kind == sw::BindingKind::Axis1DComposite );
    SW_EXPECT_TRUE( pAxis->_arrSlot[0] == keySlot( sw::Key::A ) );
    SW_EXPECT_TRUE( pAxis->_arrSlot[1] == keySlot( sw::Key::F ) );

    // 축 자체는 키 하나로 다시 잡지 않는다.
    {
        test::ScopedDefensiveTestLog expected( "rebinding a composite axis with one key" );
        SW_EXPECT_FALSE( inputMap.rebindWithResolution( "MoveX", keySlot( sw::Key::Q ), sw::ConflictResolution::Override ) );
    }
    SW_EXPECT_TRUE( pAxis->_arrSlot[0] == keySlot( sw::Key::A ) );
}

/**
 * @brief [InputMapTest] 통합 InputMap 은 입력 프레임이 갱신한다 — 따로 update() 를 부르지 않아도 액션이 한 번 발동한다
 * @details 입력 프레임이 통합 맵을 갱신하지 않으면 실제 루프에서 게임플레이 맵(`PlayerController` 가 읽는 맵)의 액션이 하나도 발동하지 않는다.
 *          시험이 `beginFrame` 뒤에 손으로 `update` 를 부르면 그것을 못 잡는다.
 */
SW_TEST_CASE( InputMapTest, IntegratedMapIsUpdatedByTheInputFrame )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::InputMap& inputMap = input.getInputMap();
    inputMap.bind( "Jump", sw::Key::Space, sw::ActionTrigger::Pressed );

    input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Space ) );
    input.beginFrame( 0.016f );
    SW_EXPECT_TRUE( inputMap.wasActionTriggered( "Jump" ) );
    input.endFrame();

    input.beginFrame( 0.016f ); // 누른 채다 — 눌림 엣지는 한 프레임뿐이다
    SW_EXPECT_FALSE( inputMap.wasActionTriggered( "Jump" ) );
    SW_EXPECT_TRUE( inputMap.isActionDown( "Jump" ) );
    input.endFrame();
    input.shutdown();
}

/**
 * @brief [InputMapTest] 셸 디버그 액션 맵은 엔진 InputMap(default.input.xml)의 것이다 — 리로드 조합 키 셋과 타이틀 액션이 있고, 다른 것은 없다
 */
SW_TEST_CASE( InputMapTest, ShellInputMapComesFromTheEngineInputMap )
{
    const sw::unique_ptr<sw::InputMap> pMap = sw::EngineLoop::createShellInputMap( "engine/input/default.input.xml" );
    SW_ASSERT_NOT_NULL( pMap.get() );
    SW_EXPECT_TRUE( pMap->hasAction( "ReloadEditor" ) );
    SW_EXPECT_TRUE( pMap->hasAction( "ReloadGame" ) );
    SW_EXPECT_TRUE( pMap->hasAction( "ReloadShaders" ) );
    SW_EXPECT_TRUE( pMap->hasAction( "Confirm" ) );
    SW_EXPECT_FALSE( pMap->hasAction( "Jump" ) );
}

/**
 * @brief [InputMapTest] 셸 InputMap 을 읽지 못하면 맵은 비어 있다 — 손으로 적은 바인딩(WASD · Space · F5/F9 …)으로 바꿔 끼우지 않는다
 * @details 실패는 오류 로그로 알린다. 바꿔 끼운 바인딩은 리소스와 내용이 달라(Title 레이어 없음) 실패를 가리고 다른 입력을 만든다.
 */
SW_TEST_CASE( InputMapTest, MissingShellInputMapLeavesNoBindings )
{
    sw::unique_ptr<sw::InputMap> pMissing;
    sw::unique_ptr<sw::InputMap> pEmptyPath;
    {
        SW_TEST_DEFENSIVE_SCOPE( "shell input map that does not exist" );
        pMissing   = sw::EngineLoop::createShellInputMap( "engine/input/does_not_exist.input.xml" );
        pEmptyPath = sw::EngineLoop::createShellInputMap( "" );
    }
    SW_ASSERT_NOT_NULL( pMissing.get() );
    SW_ASSERT_NOT_NULL( pEmptyPath.get() );
    SW_EXPECT_TRUE( pMissing->getActionNames().empty() );
    SW_EXPECT_TRUE( pEmptyPath->getActionNames().empty() );
    SW_EXPECT_FALSE( pMissing->hasAction( "Jump" ) );
    SW_EXPECT_FALSE( pMissing->hasAction( "ReloadShaders" ) );
}
