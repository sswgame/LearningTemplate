// InputMapXML — 바인딩 원소 스키마(InputMapParams.h)로 사용자 바인딩을 쓰고 읽기: 종류마다 왕복, 모르는 이름 · 칸의 거절(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Map/InputRemapSerializer.h"
#include "Engine/Input/RawInputEvent.h"

namespace
{
    /** @brief 형식이 담는 바인딩 종류를 모두 가진 맵을 만듭니다. */
    void fillEveryBindingKind( sw::InputMap& map )
    {
        using namespace sw;
        map.bind( hashed_string( "Jump" ), Key::Space, ActionTrigger::Pressed, hashed_string( "Gameplay" ) );
        map.bind( hashed_string( "Fire" ), InputSlot::fromGamepadButton( GamepadButton::RightShoulder, 1 ), ActionTrigger::HoldThreshold, hashed_string( "Gameplay" ) );
        (void)map.setBindingTriggerTime( hashed_string( "Fire" ), 0, 0.4f );
        map.bindShortcut( hashed_string( "Save" ), Key::S, static_cast<uint8>( ModifierKey::Ctrl | ModifierKey::Shift ), ActionTrigger::Pressed, hashed_string( "Editor" ) );
        map.bindAxis1DComposite( hashed_string( "Zoom" ), InputSlot::fromKey( Key::Q ), InputSlot::fromKey( Key::E ), hashed_string( "Gameplay" ), ActionTrigger::Down );
        map.bindVector2D( hashed_string( "Move" ), Key::W, Key::S, Key::A, Key::D, 0.1f, hashed_string( "Gameplay" ) );
        map.bindGamepadStick2D( hashed_string( "Look" ), GamepadStick::Right, 0.2f, hashed_string( "Gameplay" ), 1, 0.9f, 2.0f, float2{ 1.0f, -1.0f }, true, GamepadStickAxis::Y );
        map.bindChord( hashed_string( "Reload" ), Key::LeftControl, Key::R, ActionTrigger::Pressed, hashed_string( "Debug" ) );
        map.bindMouseDelta( hashed_string( "Aim" ), 0.5f, hashed_string( "Gameplay" ) );
        map.bindMouseWheel( hashed_string( "Scroll" ), 2.0f, hashed_string( "Gameplay" ) );
        map.bindAnyKey( hashed_string( "Start" ), hashed_string( "Title" ) );
    }

    /** @brief 맵을 사용자 바인딩 파일로 쓰고 그 글을 돌려줍니다. */
    sw::string saveText( const sw::InputMap& map, const sw::string& path )
    {
        sw::string text;
        SW_EXPECT_TRUE( sw::InputRemapSerializer::save( map, path ) && sw::FileUtil::readTextFile( path, text ) );
        return text;
    }
} // namespace

/**
 * @brief [InputMapXMLTest] 종류마다의 바인딩이 같은 글로 왕복한다 — 장치 · 수정 키 · 스틱 칸 · 공통 칸(발화 시간)
 */
SW_TEST_CASE( InputMapXMLTest, UserBindingsRoundTripEveryKind )
{
    sw::InputMap map;
    fillEveryBindingKind( map );
    const sw::string first = saveText( map, sw::test::makeScratchPath( "first.xml" ) );

    sw::InputMap reread;
    SW_REQUIRE_TRUE( sw::InputRemapSerializer::load( reread, sw::test::makeScratchPath( "first.xml" ) ) );
    SW_EXPECT_EQUAL( map.getBindingCount( sw::hashed_string( "Look" ) ), reread.getBindingCount( sw::hashed_string( "Look" ) ) );
    const sw::ActionBinding* pLook = reread.getBinding( sw::hashed_string( "Look" ), 0 );
    SW_REQUIRE_TRUE( pLook != nullptr );
    SW_EXPECT_TRUE( pLook->_stick == sw::GamepadStick::Right && pLook->_stickAxis == sw::GamepadStickAxis::Y && pLook->_bScaleByDelta );
    const sw::ActionBinding* pSave = reread.getBinding( sw::hashed_string( "Save" ), 0 );
    SW_REQUIRE_TRUE( pSave != nullptr );
    SW_EXPECT_TRUE( pSave->_kind == sw::BindingKind::Shortcut && pSave->_modifierMask == static_cast<uint8>( sw::ModifierKey::Ctrl | sw::ModifierKey::Shift ) );
    SW_EXPECT_EQUAL( first, saveText( reread, sw::test::makeScratchPath( "second.xml" ) ) );
}

/**
 * @brief [InputMapXMLTest] 모르는 키 이름 · 모르는 속성 · 모르는 원소는 그 바인딩을 넣지 않고 읽기를 실패로 돌린다(조용히 버리지 않는다)
 */
SW_TEST_CASE( InputMapXMLTest, RejectsUnknownNamesAndProperties )
{
    SW_EXPECT_LOG( "InputMapXML.BindingRejected" );
    SW_EXPECT_LOG( "XMLFields.UnknownField" );
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    SW_EXPECT_LOG( "InputRemapSerializer.UserBindingsInvalidAttribute" );
    const sw::string brokenBinds   = sw::test::makeScratchPath( "brokenbinds.xml" );
    const sw::string brokenElement = sw::test::makeScratchPath( "brokenelement.xml" );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( brokenBinds, "<UserBindings><action name=\"Jump\">"
                                                               "<bind source=\"Key\" code=\"NoSuchKey\" layer=\"Gameplay\"/>"
                                                               "<bind source=\"Key\" code=\"Space\" colour=\"red\"/>"
                                                               "</action></UserBindings>" ) );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( brokenElement, "<UserBindings><action name=\"Jump\"><wiggle/></action></UserBindings>" ) );
    sw::InputMap map;
    SW_EXPECT_FALSE( sw::InputRemapSerializer::load( map, brokenBinds ) );
    SW_EXPECT_FALSE( sw::InputRemapSerializer::load( map, brokenElement ) ); // 모르는 원소가 있는 액션은 통째로 읽지 않는다
    SW_EXPECT_EQUAL( uint32{ 0 }, map.getBindingCount( sw::hashed_string( "Jump" ) ) );
}
