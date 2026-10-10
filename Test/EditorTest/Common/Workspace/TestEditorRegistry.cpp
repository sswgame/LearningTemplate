#include "pch.h"

#include "Core/Container/string.h"

#include "Editor/Common/Workspace/EditorRegistry.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 시험 전용 등록 줄. EditorTest 에는 실제 패널 · 시각화(ImGui 를 그린다)가 링크되지 않는다. */
    struct FakeRegistration : EditorRegistration
    {
        static constexpr const utf8* kKindName = "fake";

        int32 _payload;
    };

    using FakeRegistry  = EditorRegistry<FakeRegistration>;
    using FakeRegistrar = EditorRegistrar<FakeRegistration>;

    /** @brief 등록부의 id 를 순서대로 쉼표로 이은 글입니다. */
    string joinRegisteredIDs()
    {
        string text;
        for ( uint32 index = 0; index < FakeRegistry::getCount(); ++index )
        {
            if ( text.empty() == false )
                text += ',';
            text += FakeRegistry::getAt( index )._pID;
        }
        return text;
    }
} // namespace

/**
 * @brief [EditorRegistryTest] 보이는 순서는 등록 순서가 아니라 순서 키 — 같으면 id 사전순 — 가 정한다
 * @details 패널 · 팝업 · 시각화는 각자 자기 .cpp 의 정적 등록자로 등록되고, 번역 단위 사이의 정적 초기화 순서는 정해지지 않는다.
 *          그래서 Panel 메뉴 · 툴바 체크박스 순서가 링크 순서에 따라 바뀌지 않으려면 등록부가 순서 키로 줄을 세워야 한다.
 */
SW_TEST_CASE( EditorRegistryTest, OrderKeyDecidesOrderNotRegistrationOrder )
{
    SW_ASSERT_EQUAL( 0u, FakeRegistry::getCount() );
    {
        const FakeRegistrar third{
            FakeRegistration{ { "third", 300 }, 3 }
        };
        const FakeRegistrar first{
            FakeRegistration{ { "first", 100 }, 1 }
        };
        const FakeRegistrar secondB{
            FakeRegistration{ { "second_b", 200 }, 22 }
        };
        const FakeRegistrar secondA{
            FakeRegistration{ { "second_a", 200 }, 21 }
        };

        SW_EXPECT_STREQ( "first,second_a,second_b,third", joinRegisteredIDs().c_str() );
        SW_EXPECT_TRUE( first.isRegistered() );

        const FakeRegistration* pFound = FakeRegistry::find( "second_b" );
        SW_ASSERT_TRUE( pFound != nullptr );
        SW_EXPECT_EQUAL( 22, pFound->_payload );
        SW_EXPECT_TRUE( pFound == &secondB.getRegistration() );
        SW_EXPECT_TRUE( FakeRegistry::find( "missing" ) == nullptr );

        // 등록자가 내려가면(모듈 언로드) 그 줄만 빠진다.
        {
            const FakeRegistrar middle{
                FakeRegistration{ { "middle", 250 }, 0 }
            };
            SW_EXPECT_STREQ( "first,second_a,second_b,middle,third", joinRegisteredIDs().c_str() );
        }
        SW_EXPECT_STREQ( "first,second_a,second_b,third", joinRegisteredIDs().c_str() );
    }
    SW_EXPECT_EQUAL( 0u, FakeRegistry::getCount() );
}

/**
 * @brief [EditorRegistryTest] 같은 id 의 둘째 등록은 거절되고, 그것이 내려가도 첫째는 남는다
 * @details 두 파일이 같은 패널 id 를 쓰면 `windows.ini` 가시성 키와 `-gv_editorOpenPanel` 이 둘 중 아무것에나 걸린다. 첫째만 받아 오류로 알린다.
 */
SW_TEST_CASE( EditorRegistryTest, SecondRegistrationOfAnIDIsRejected )
{
    const FakeRegistrar first{
        FakeRegistration{ { "panel_id", 100 }, 1 }
    };
    SW_ASSERT_TRUE( first.isRegistered() );
    {
        test::ScopedDefensiveTestLog expected( "a second registration of one editor id" );
        const FakeRegistrar          second{
            FakeRegistration{ { "panel_id", 50 }, 2 }
        };
        SW_EXPECT_FALSE( second.isRegistered() );
        SW_EXPECT_EQUAL( 1u, FakeRegistry::getCount() );
        SW_EXPECT_EQUAL( 1, FakeRegistry::getAt( 0 )._payload );

        const FakeRegistrar empty{
            FakeRegistration{ { "", 10 }, 3 }
        };
        SW_EXPECT_FALSE( empty.isRegistered() );
        SW_EXPECT_EQUAL( 1u, FakeRegistry::getCount() );
    }
    SW_EXPECT_EQUAL( 1u, FakeRegistry::getCount() );
    SW_EXPECT_TRUE( FakeRegistry::find( "panel_id" ) == &first.getRegistration() );
}

/**
 * @brief [EditorRegistryTest] 종류 이름이 같으면 어느 템플릿 인스턴스에서 찾든 같은 목록이다
 * @details 확장 모듈(DLL)의 등록이 EditorModule 의 목록에 올라야 에디터에 보인다. 목록을 템플릿 함수 정적으로 두면 DLL 마다 사본이 생긴다.
 */
SW_TEST_CASE( EditorRegistryTest, KindNameResolvesToOneList )
{
    EditorRegistrationList& first  = getEditorRegistrationList( "test.kind" );
    EditorRegistrationList& second = getEditorRegistrationList( "test.kind" );
    SW_EXPECT_TRUE( &first == &second );
    SW_EXPECT_TRUE( &first != &getEditorRegistrationList( "test.other" ) );
    SW_EXPECT_TRUE( &FakeRegistry::getList() == &getEditorRegistrationList( FakeRegistration::kKindName ) );
}

/**
 * @brief [EditorRegistryTest] 넣고 빼면 세대가 오르고, 거절된 등록은 세대를 올리지 않는다
 * @details 매니저(패널 · 인스펙터 · 팝업 · 커맨드)는 세대가 바뀌었을 때만 인스턴스를 다시 맞춘다.
 */
SW_TEST_CASE( EditorRegistryTest, GenerationCountsChanges )
{
    EditorRegistrationList&         list   = getEditorRegistrationList( "test.generation" );
    const uint32                    before = list.getGeneration();
    static const EditorRegistration kFirst{ "a", 1 };
    static const EditorRegistration kDuplicate{ "a", 2 };
    SW_EXPECT_TRUE( list.addRegistration( kFirst ) );
    SW_EXPECT_EQUAL( before + 1, list.getGeneration() );
    {
        test::ScopedDefensiveTestLog expected( "a second registration of one editor id" );
        SW_EXPECT_FALSE( list.addRegistration( kDuplicate ) );
    }
    SW_EXPECT_EQUAL( before + 1, list.getGeneration() );
    list.removeRegistration( kDuplicate ); // 올라 있지 않은 줄 — 세대는 그대로
    SW_EXPECT_EQUAL( before + 1, list.getGeneration() );
    list.removeRegistration( kFirst );
    SW_EXPECT_EQUAL( before + 2, list.getGeneration() );
}
