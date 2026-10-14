#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/GUI/EditorContextMenuExtension.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    using ContextMenuRegistrar = EditorExtensionRegistrar<EditorContextMenuEntryRegistration>;

    void runFirstAction( const EditorContextMenuTarget& /*target*/ )
    {
    }
    void runSecondAction( const EditorContextMenuTarget& /*target*/ )
    {
    }
    bool isDirectoryTarget( const EditorContextMenuTarget& target )
    {
        return target._bIsDirectory;
    }

    /** @brief @p menuKind 메뉴의 항목 id 를 그릴 순서대로 쉼표로 이은 글입니다. */
    string joinEntryIDs( EditorContextMenuKind menuKind )
    {
        string text;
        (void)EditorContextMenuExtensionUtil::visitEntries( menuKind, [&]( const EditorContextMenuEntryRegistration& entry ) {
            if ( text.empty() == false )
                text += ',';
            text += entry._pID;
            return false;
        } );
        return text;
    }
} // namespace

/**
 * @brief [EditorContextMenuExtensionTest] 항목은 자기 메뉴 종류에만 `_order` 순서로 붙고, 등록자가 내려가면 빠진다
 * @details 확장 모듈이 언로드되면 정적 등록자가 항목을 빼므로, 메뉴는 매 프레임 목록을 다시 읽어 지난 모듈의 함수를 부르지 않는다.
 */
SW_TEST_CASE( EditorContextMenuExtensionTest, EntriesFilterByMenuKindAndFollowOrder )
{
    SW_ASSERT_EQUAL( 0u, EditorContextMenuExtensionUtil::countEntries( EditorContextMenuKind::GameObject ) );
    {
        const ContextMenuRegistrar late{
            EditorContextMenuEntryRegistration{ { "test.late", 9200 }, EditorContextMenuKind::GameObject, "Late", &runFirstAction, nullptr }
        };
        const ContextMenuRegistrar asset{
            EditorContextMenuEntryRegistration{ { "test.asset", 9000 }, EditorContextMenuKind::Asset, "Asset", &runFirstAction, nullptr }
        };
        unique_ptr<ContextMenuRegistrar> pEarly = make_unique<ContextMenuRegistrar>(
            EditorContextMenuEntryRegistration{ { "test.early", 9100 }, EditorContextMenuKind::GameObject, "Early", &runSecondAction, nullptr } );

        SW_EXPECT_STREQ( "test.early,test.late", joinEntryIDs( EditorContextMenuKind::GameObject ).c_str() );
        SW_EXPECT_STREQ( "test.asset", joinEntryIDs( EditorContextMenuKind::Asset ).c_str() );
        SW_EXPECT_EQUAL( 2u, EditorContextMenuExtensionUtil::countEntries( EditorContextMenuKind::GameObject ) );

        pEarly.reset(); // 확장 모듈이 내려간다
        SW_EXPECT_STREQ( "test.late", joinEntryIDs( EditorContextMenuKind::GameObject ).c_str() );
    }
    SW_EXPECT_EQUAL( 0u, EditorContextMenuExtensionUtil::countEntries( EditorContextMenuKind::Asset ) );
}

/**
 * @brief [EditorContextMenuExtensionTest] 고른 항목의 동작이 돌아오고, 켜짐 판정은 대상과 동작 유무를 본다
 */
SW_TEST_CASE( EditorContextMenuExtensionTest, VisitReturnsChosenActionAndEnabledFollowsTarget )
{
    const ContextMenuRegistrar first{
        EditorContextMenuEntryRegistration{ { "test.first", 9100 }, EditorContextMenuKind::Asset, "First", &runFirstAction, &isDirectoryTarget }
    };
    const ContextMenuRegistrar second{
        EditorContextMenuEntryRegistration{ { "test.second", 9200 }, EditorContextMenuKind::Asset, "Second", &runSecondAction, nullptr }
    };
    const ContextMenuRegistrar noAction{
        EditorContextMenuEntryRegistration{ { "test.noAction", 9300 }, EditorContextMenuKind::Asset, "No Action", nullptr, nullptr }
    };

    auto pfnChosen = EditorContextMenuExtensionUtil::visitEntries( EditorContextMenuKind::Asset, []( const EditorContextMenuEntryRegistration& entry ) {
        return string_view{ entry._pID } == "test.second";
    } );
    SW_EXPECT_TRUE( pfnChosen == &runSecondAction );

    EditorContextMenuTarget fileTarget{};
    fileTarget._kind       = EditorContextMenuKind::Asset;
    fileTarget._pAssetPath = "C:/Project/Content/a.mat";
    EditorContextMenuTarget folderTarget = fileTarget;
    folderTarget._bIsDirectory          = true;

    SW_EXPECT_FALSE( EditorContextMenuExtensionUtil::isEnabled( first.getRegistration(), fileTarget ) );
    SW_EXPECT_TRUE( EditorContextMenuExtensionUtil::isEnabled( first.getRegistration(), folderTarget ) );
    SW_EXPECT_TRUE( EditorContextMenuExtensionUtil::isEnabled( second.getRegistration(), fileTarget ) );
    SW_EXPECT_FALSE( EditorContextMenuExtensionUtil::isEnabled( noAction.getRegistration(), fileTarget ) );
}
