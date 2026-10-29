#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/UI/Accessibility/AtSpiMapping.h"

using namespace sw;

// 리눅스 AT-SPI 브리지의 순수 규칙 — 역할 · 상태 · 인터페이스가 at-spi2-core 의 번호로 바뀌는지 본다(D-Bus 는 쓰지 않는다).

/**
 * @brief [AtSpiMappingTest] 역할은 AtspiRole 번호로 바뀌고, 읽을 것이 없는 `None` 만 INVALID(0)이며, 모든 역할이 이름을 가진다
 * @details 변이: 단추를 PUSH_BUTTON(43) 이 아닌 번호에 두면 첫 단언이 진다. 새 `AccessibleRole` 을 더하고 매핑을 빠뜨리면 이름 단언이 진다.
 */
SW_TEST_CASE( AtSpiMappingTest, RolesMapToAtspiRoleNumbers )
{
    SW_EXPECT_EQUAL( 43u, AtSpiMapping::getRole( AccessibleRole::Button ) );
    SW_EXPECT_EQUAL( 7u, AtSpiMapping::getRole( AccessibleRole::CheckBox ) );
    SW_EXPECT_EQUAL( 51u, AtSpiMapping::getRole( AccessibleRole::Slider ) );
    SW_EXPECT_EQUAL( 42u, AtSpiMapping::getRole( AccessibleRole::ProgressBar ) );
    SW_EXPECT_EQUAL( 61u, AtSpiMapping::getRole( AccessibleRole::TextInput ) );
    SW_EXPECT_EQUAL( 29u, AtSpiMapping::getRole( AccessibleRole::Text ) );
    SW_EXPECT_EQUAL( 0u, AtSpiMapping::getRole( AccessibleRole::None ) );
    for ( uint32 index = static_cast<uint32>( AccessibleRole::Group ); index <= static_cast<uint32>( AccessibleRole::List ); ++index )
    {
        const AccessibleRole role = static_cast<AccessibleRole>( index );
        SW_EXPECT_TRUE_MSG( AtSpiMapping::getRole( role ) != 0, AtSpiMapping::getRoleName( role ) );
        SW_EXPECT_TRUE( string_view{ AtSpiMapping::getRoleName( role ) } != "invalid" );
    }
}

/**
 * @brief [AtSpiMappingTest] 상태 비트는 AtspiStateType 번호의 낱말 · 비트로 간다 — 보이는 위젯은 늘 VISIBLE · SHOWING, 꺼지면 ENABLED · SENSITIVE 가 빠진다
 * @details 번호: ENABLED 8, FOCUSABLE 11, FOCUSED 12, PRESSED 20, SENSITIVE 24, SHOWING 25, VISIBLE 30, CHECKABLE 41(낱말 1 의 9), READ_ONLY 43(낱말 1 의 11).
 */
SW_TEST_CASE( AtSpiMappingTest, StatesMapToAtspiStateBits )
{
    const AtSpiStateSet plain = AtSpiMapping::makeStateSet( AccessibleRole::Button, AccessibleState::kFocusable );
    SW_EXPECT_TRUE( ( plain._word[0] & ( 1u << 30 ) ) != 0 ); // VISIBLE
    SW_EXPECT_TRUE( ( plain._word[0] & ( 1u << 25 ) ) != 0 ); // SHOWING
    SW_EXPECT_TRUE( ( plain._word[0] & ( 1u << 8 ) ) != 0 );  // ENABLED
    SW_EXPECT_TRUE( ( plain._word[0] & ( 1u << 24 ) ) != 0 ); // SENSITIVE
    SW_EXPECT_TRUE( ( plain._word[0] & ( 1u << 11 ) ) != 0 ); // FOCUSABLE
    SW_EXPECT_TRUE( ( plain._word[0] & ( 1u << 12 ) ) == 0 ); // FOCUSED 아님

    const AtSpiStateSet disabled = AtSpiMapping::makeStateSet( AccessibleRole::Button, AccessibleState::kDisabled | AccessibleState::kFocused | AccessibleState::kPressed );
    SW_EXPECT_TRUE( ( disabled._word[0] & ( 1u << 8 ) ) == 0 );
    SW_EXPECT_TRUE( ( disabled._word[0] & ( 1u << 24 ) ) == 0 );
    SW_EXPECT_TRUE( ( disabled._word[0] & ( 1u << 12 ) ) != 0 ); // FOCUSED
    SW_EXPECT_TRUE( ( disabled._word[0] & ( 1u << 20 ) ) != 0 ); // PRESSED

    const AtSpiStateSet check = AtSpiMapping::makeStateSet( AccessibleRole::CheckBox, AccessibleState::kChecked );
    SW_EXPECT_TRUE( ( check._word[0] & ( 1u << 4 ) ) != 0 ); // CHECKED
    SW_EXPECT_TRUE( ( check._word[1] & ( 1u << 9 ) ) != 0 ); // CHECKABLE(41)
    const AtSpiStateSet readOnly = AtSpiMapping::makeStateSet( AccessibleRole::ProgressBar, AccessibleState::kReadOnly );
    SW_EXPECT_TRUE( ( readOnly._word[1] & ( 1u << 11 ) ) != 0 ); // READ_ONLY(43)
    const AtSpiStateSet input = AtSpiMapping::makeStateSet( AccessibleRole::TextInput, 0 );
    SW_EXPECT_TRUE( ( input._word[0] & ( 1u << 7 ) ) != 0 ); // EDITABLE
}

/**
 * @brief [AtSpiMappingTest] 인터페이스는 역할이 정한다 — 모두 Component, 단추 · 체크 상자는 Action, 슬라이더 · 진행 막대는 Value
 */
SW_TEST_CASE( AtSpiMappingTest, InterfacesFollowTheRole )
{
    SW_EXPECT_EQUAL( AtSpiInterface::kComponent, AtSpiMapping::getInterfaces( AccessibleRole::Text ) );
    SW_EXPECT_EQUAL( AtSpiInterface::kComponent | AtSpiInterface::kAction, AtSpiMapping::getInterfaces( AccessibleRole::Button ) );
    SW_EXPECT_EQUAL( AtSpiInterface::kComponent | AtSpiInterface::kAction, AtSpiMapping::getInterfaces( AccessibleRole::CheckBox ) );
    SW_EXPECT_EQUAL( AtSpiInterface::kComponent | AtSpiInterface::kValue, AtSpiMapping::getInterfaces( AccessibleRole::Slider ) );
    SW_EXPECT_EQUAL( AtSpiInterface::kComponent | AtSpiInterface::kValue, AtSpiMapping::getInterfaces( AccessibleRole::ProgressBar ) );
}
