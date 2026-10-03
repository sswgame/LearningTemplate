#include "pch.h"

#include "Core/Container/RegistrationList.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 등록 대상 역할만 하는 객체입니다. 등록부는 소유하지 않으므로 시험이 들고 있습니다. */
    struct RegistrationProbe
    {
        int32 _value{ 0 };
    };
} // namespace

// ------------------------------------------------------------------------------
// RegistrationListTest — 엔진 · 에디터 등록부의 공통 모양
// ------------------------------------------------------------------------------
/**
 * @brief [RegistrationListTest] 같은 객체 · nullptr 는 한 번도 더 오르지 않고, 빼기는 남은 것의 등록 순서를 지킨다
 * @details 빛 · 카메라 · 모듈 코드 보유자가 이 모양이다. 빼기가 맨 뒤를 빈자리로 옮기면(swap-and-pop) "등록된 첫 빛" 이 빼기마다 바뀐다.
 */
SW_TEST_CASE( RegistrationListTest, RejectsDuplicatesAndKeepsInsertionOrderOnRemove )
{
    RegistrationProbe first{ 1 };
    RegistrationProbe second{ 2 };
    RegistrationProbe third{ 3 };

    RegistrationList<RegistrationProbe> list;
    SW_EXPECT_TRUE( list.add( &first ) == RegistrationResult::Added );
    SW_EXPECT_TRUE( list.add( &second ) == RegistrationResult::Added );
    SW_EXPECT_TRUE( list.add( &third ) == RegistrationResult::Added );
    SW_EXPECT_TRUE( list.add( &second ) == RegistrationResult::AlreadyPresent );
    SW_EXPECT_TRUE( list.add( nullptr ) == RegistrationResult::NullItem );
    SW_ASSERT_EQUAL( 3u, list.getCount() );

    SW_EXPECT_TRUE( list.remove( &first ) );
    SW_EXPECT_FALSE( list.remove( &first ) );
    SW_ASSERT_EQUAL( 2u, list.getCount() );
    SW_EXPECT_EQUAL( &second, list.getAt( 0 ) );
    SW_EXPECT_EQUAL( &third, list.getAt( 1 ) );
    SW_EXPECT_FALSE( list.contains( &first ) );
    SW_EXPECT_TRUE( list.contains( &third ) );
}

/**
 * @brief [RegistrationListTest] 이름은 올릴 때 복사하고, 같은 이름의 다른 객체는 거절해 먼저 것을 둔다
 * @details 에셋 캐시 등록부가 이 모양이다 — 모듈이 내리지 않고 사라진 캐시의 이름을 그 객체에 묻지 않고 말해야 한다.
 */
SW_TEST_CASE( RegistrationListTest, CopiesNamesAndRejectsADuplicateName )
{
    RegistrationProbe material{};
    RegistrationProbe impostor{};
    RegistrationProbe nameless{};

    RegistrationList<RegistrationProbe> list;
    {
        string transientName = "Material";
        SW_EXPECT_TRUE( list.add( &material, transientName ) == RegistrationResult::Added );
        transientName = "Overwritten";
    }
    SW_EXPECT_TRUE( list.add( &impostor, "Material" ) == RegistrationResult::DuplicateName );
    SW_EXPECT_TRUE( list.add( &nameless ) == RegistrationResult::Added );

    SW_EXPECT_EQUAL( &material, list.findByName( "Material" ) );
    SW_EXPECT_NULL( list.findByName( "Overwritten" ) );
    SW_EXPECT_NULL( list.findByName( "" ) );
    SW_EXPECT_STREQ( "Material", list.getNameAt( 0 ).c_str() );
    SW_EXPECT_FALSE( list.contains( &impostor ) );

    // 이름으로 찾지 못하는 것은 이름 없는 항목뿐이다 — 빼면 이름도 같이 빠진다.
    SW_EXPECT_TRUE( list.remove( &material ) );
    SW_EXPECT_NULL( list.findByName( "Material" ) );
    SW_EXPECT_TRUE( list.add( &impostor, "Material" ) == RegistrationResult::Added );
}

/**
 * @brief [RegistrationListTest] (순서 값, 이름) 정렬 목록은 등록 순서와 무관하게 같은 순서이고, 이름을 요구하면 빈 이름을 거절한다
 * @details 에디터 확장 등록부가 이 모양이다 — 정적 초기화 순서는 번역 단위마다 달라 등록 순서로 보여 주면 빌드마다 메뉴가 바뀐다.
 */
SW_TEST_CASE( RegistrationListTest, OrdersByOrderThenNameAndRequiresNames )
{
    RegistrationProbe late{};
    RegistrationProbe earlyB{};
    RegistrationProbe earlyA{};
    RegistrationProbe unnamed{};

    RegistrationList<RegistrationProbe> list{ RegistrationOrder::ByOrderThenName, true };
    SW_EXPECT_TRUE( list.add( &late, "Late", 10 ) == RegistrationResult::Added );
    SW_EXPECT_TRUE( list.add( &earlyB, "B", 0 ) == RegistrationResult::Added );
    SW_EXPECT_TRUE( list.add( &earlyA, "A", 0 ) == RegistrationResult::Added );
    SW_EXPECT_TRUE( list.add( &unnamed ) == RegistrationResult::EmptyName );
    SW_ASSERT_EQUAL( 3u, list.getCount() );
    SW_EXPECT_EQUAL( &earlyA, list.getAt( 0 ) );
    SW_EXPECT_EQUAL( &earlyB, list.getAt( 1 ) );
    SW_EXPECT_EQUAL( &late, list.getAt( 2 ) );

    list.clear();
    SW_EXPECT_EQUAL( 0u, list.getCount() );
    SW_EXPECT_NULL( list.findByName( "A" ) );
}
