#include "pch.h"

#include "Core/String/RegistrationList.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 등록 대상 역할만 하는 객체입니다. 레지스트리는 소유하지 않으므로 테스트가 들고 있습니다. */
    struct RegistrationProbe
    {
        int32 _value{ 0 };
    };
} // namespace

// ------------------------------------------------------------------------------
// RegistrationListTest — 엔진 · 에디터 레지스트리의 공통 모양
// ------------------------------------------------------------------------------
/**
 * @brief [RegistrationListTest] 같은 객체 · nullptr 는 한 번도 더 오르지 않고, 빼기는 남은 것의 등록 순서를 지킨다
 * @details 빛 · 카메라 · 모듈 언로드 리스너가 이 모양이다. 빼기가 맨 뒤를 빈자리로 옮기면(swap-and-pop) "등록된 첫 빛" 이 빼기마다 바뀐다.
 */
SW_TEST_CASE( RegistrationListTest, RejectsDuplicatesAndKeepsInsertionOrderOnRemove )
{
    RegistrationProbe first{ 1 };
    RegistrationProbe second{ 2 };
    RegistrationProbe third{ 3 };

    RegistrationList<RegistrationProbe> list{ "TestList" };
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
 * @details 에셋 캐시 레지스트리가 이 모양이다 — 모듈이 내리지 않고 사라진 캐시의 이름을 그 객체에 묻지 않고 말해야 한다.
 */
SW_TEST_CASE( RegistrationListTest, CopiesNamesAndRejectsADuplicateName )
{
    RegistrationProbe material{};
    RegistrationProbe impostor{};
    RegistrationProbe nameless{};

    RegistrationList<RegistrationProbe> list{ "TestList" };
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
 * @details 에디터 확장 레지스트리가 이 모양이다 — 정적 초기화 순서는 번역 단위마다 달라 등록 순서로 보여 주면 빌드마다 메뉴가 바뀐다.
 */
SW_TEST_CASE( RegistrationListTest, OrdersByOrderThenNameAndRequiresNames )
{
    RegistrationProbe late{};
    RegistrationProbe earlyB{};
    RegistrationProbe earlyA{};
    RegistrationProbe unnamed{};

    RegistrationList<RegistrationProbe> list{ "TestList", RegistrationOrder::ByOrderThenName, true };
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

/**
 * @brief [RegistrationListTest] 리로드 중인 이미지의 등록은 같은 이름을 대체하고, 옛 이미지가 내려가며 빼도 새 등록이 남는다
 * @details 새 이미지의 정적 등록자는 옛 이미지가 아직 도는 동안 돈다. 거절하면 옛 이미지가 내려간 뒤 그 이름(개발 명령 · 탐침 · 시나리오 단계)이 사라진다.
 *          리로드 표지 밖의 중복은 지금처럼 거절한다.
 */
SW_TEST_CASE( RegistrationListTest, ReloadScopeReplacesTheNameAndTheOldImageRemovalKeepsTheNewOne )
{
    RegistrationProbe oldImage{ 1 };
    RegistrationProbe newImage{ 2 };
    RegistrationProbe stranger{ 3 };

    RegistrationList<RegistrationProbe> list{ "TestList" };
    SW_EXPECT_TRUE( list.add( &oldImage, "play" ) == RegistrationResult::Added );
    SW_EXPECT_TRUE( list.add( &stranger, "play" ) == RegistrationResult::DuplicateName ); // 리로드가 아니면 거절한다
    {
        RegistrationReloadScope scope;
        SW_EXPECT_TRUE( list.add( &newImage, "play" ) == RegistrationResult::Added );
    }
    SW_ASSERT_EQUAL( 1u, list.getCount() );
    SW_EXPECT_EQUAL( &newImage, list.findByName( "play" ) );

    SW_EXPECT_TRUE( list.remove( &oldImage ) ); // 옛 이미지가 내려가며 뺀다
    SW_EXPECT_FALSE( list.remove( &oldImage ) );
    SW_ASSERT_EQUAL( 1u, list.getCount() );
    SW_EXPECT_EQUAL( &newImage, list.findByName( "play" ) );
}

/**
 * @brief [RegistrationListTest] 교체한 새 이미지가 취소되면(prepare 실패) 옛 등록이 이름을 되찾고, 연달은 리로드는 가장 나중 그림자부터 돌아온다
 */
SW_TEST_CASE( RegistrationListTest, CancelledReplacementRestoresTheOldRegistration )
{
    RegistrationProbe oldImage{ 1 };
    RegistrationProbe firstNew{ 2 };
    RegistrationProbe secondNew{ 3 };

    RegistrationList<RegistrationProbe> list( "TestList", RegistrationOrder::ByOrderThenName, true );
    SW_EXPECT_TRUE( list.add( &oldImage, "probe", 5 ) == RegistrationResult::Added );
    {
        RegistrationReloadScope scope;
        SW_EXPECT_TRUE( list.add( &firstNew, "probe", 5 ) == RegistrationResult::Added );
    }
    SW_EXPECT_TRUE( list.remove( &firstNew ) ); // 새 이미지를 버린다
    SW_ASSERT_EQUAL( 1u, list.getCount() );
    SW_EXPECT_EQUAL( &oldImage, list.findByName( "probe" ) ); // 옛 등록이 돌아왔다

    // 연달은 리로드: 새 것이 새 것을 대체하고, 옛 이미지들이 내려갈 때는 그림자만 버린다.
    {
        RegistrationReloadScope scope;
        SW_EXPECT_TRUE( list.add( &firstNew, "probe", 5 ) == RegistrationResult::Added );
        SW_EXPECT_TRUE( list.add( &secondNew, "probe", 5 ) == RegistrationResult::Added );
    }
    SW_EXPECT_EQUAL( &secondNew, list.findByName( "probe" ) );
    SW_EXPECT_TRUE( list.remove( &oldImage ) );
    SW_EXPECT_TRUE( list.remove( &firstNew ) );
    SW_ASSERT_EQUAL( 1u, list.getCount() );
    SW_EXPECT_EQUAL( &secondNew, list.findByName( "probe" ) );
}

/**
 * @brief [RegistrationListTest] 목록은 살아 있는 동안만 전역 열거(`RegistrationListBase::collect`)에 이름 · 항목 수와 함께 나오고, 사라지면 빠진다
 * @details 열거는 모듈 로드 · 언로드 뒤 항목이 빠졌는지, 같은 이름이 둘 올랐는지를 한곳에서 보는 길이다(SmokeTest 의 모듈 언로드 검사가 쓴다).
 */
SW_TEST_CASE( RegistrationListTest, ListAppearsInTheGlobalEnumerationWhileAlive )
{
    const uint32 listCountBefore = RegistrationListBase::getListCount();
    {
        RegistrationProbe                   first{ 1 };
        RegistrationProbe                   second{ 2 };
        RegistrationList<RegistrationProbe> list{ "EnumerationProbeList" };
        SW_EXPECT_TRUE( list.add( &first, "first" ) == RegistrationResult::Added );
        SW_EXPECT_TRUE( list.add( &second, "second" ) == RegistrationResult::Added );
        SW_EXPECT_EQUAL( listCountBefore + 1u, RegistrationListBase::getListCount() );

        vector<const RegistrationListBase*> listRegistration;
        RegistrationListBase::collect( listRegistration );
        SW_ASSERT_EQUAL( static_cast<size_t>( listCountBefore + 1u ), listRegistration.size() );

        const RegistrationListBase* pFound = nullptr;
        for ( const RegistrationListBase* pList : listRegistration )
        {
            if ( string_view{ pList->getListName() } == "EnumerationProbeList" )
                pFound = pList;
        }
        SW_ASSERT_NOT_NULL( pFound );
        SW_EXPECT_EQUAL( 2u, pFound->getCount() );
        SW_EXPECT_TRUE( string_view{ pFound->getNameAt( 0 ) } == "first" );
        SW_EXPECT_TRUE( string_view{ pFound->getNameAt( 1 ) } == "second" );

        // 살아 있는 모든 목록에서 같은 이름이 둘 오르지 않았다(리로드 표지 밖의 중복은 목록이 거절한다).
        for ( const RegistrationListBase* pList : listRegistration )
        {
            for ( uint32 index = 0; index < pList->getCount(); ++index )
            {
                if ( pList->getNameAt( index ).empty() )
                    continue;
                for ( uint32 other = index + 1; other < pList->getCount(); ++other )
                {
                    SW_EXPECT_FALSE_MSG( pList->getNameAt( index ) == pList->getNameAt( other ), pList->getListName() );
                }
            }
        }
    }
    SW_EXPECT_EQUAL( listCountBefore, RegistrationListBase::getListCount() );
}

/**
 * @brief [NameRegistryTest] 이름이 비었거나 겹치면 올리지 않고, 이름으로 빼도 남은 것의 등록 순서는 그대로다
 * @details 값을 소유하는 레지스트리(활동 종류 · 연산 · 모듈 렌더 패스)가 이름 거절과 빼기를 직접 짜지 않게 하는 공용 형식이다.
 */
SW_TEST_CASE( NameRegistryTest, RejectsEmptyAndDuplicateNamesAndRemovesByNameInOrder )
{
    NameRegistry<int32> registry;
    SW_EXPECT_FALSE( registry.add( hashed_string{}, 1 ) );
    SW_EXPECT_TRUE( registry.add( hashed_string{ "a" }, 1 ) );
    SW_EXPECT_TRUE( registry.add( hashed_string{ "b" }, 2 ) );
    SW_EXPECT_TRUE( registry.add( hashed_string{ "c" }, 3 ) );
    SW_EXPECT_FALSE( registry.add( hashed_string{ "b" }, 9 ) );
    SW_ASSERT_EQUAL( 3u, registry.getCount() );
    SW_ASSERT_NOT_NULL( registry.find( hashed_string{ "b" } ) );
    SW_EXPECT_EQUAL( 2, *registry.find( hashed_string{ "b" } ) );

    SW_EXPECT_TRUE( registry.remove( hashed_string{ "b" } ) );
    SW_EXPECT_FALSE( registry.remove( hashed_string{ "b" } ) );
    SW_EXPECT_FALSE( registry.remove( hashed_string{} ) );
    SW_ASSERT_EQUAL( 2u, registry.getCount() );
    SW_EXPECT_NULL( registry.find( hashed_string{ "b" } ) );
    SW_EXPECT_EQUAL( 1, registry.getItems()[0] );
    SW_EXPECT_EQUAL( 3, registry.getItems()[1] );
    SW_EXPECT_TRUE( registry.add( hashed_string{ "b" }, 4 ) ); // 빼면 같은 이름을 다시 올릴 수 있다
}
