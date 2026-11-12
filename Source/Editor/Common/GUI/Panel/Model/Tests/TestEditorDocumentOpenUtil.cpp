// EditorDocumentOpenUtil — 문서 패널이 열 문서를 정하는 판정: "Open..." 후보, 최근 목록, 포커스 교체와 취소.
#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/GUI/Panel/Model/EditorDocumentOpenUtil.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct EditorDocumentOpenTestUtil
    {
        static EditorAssetIndexEntry makeEntry( const EditorAssetType& kind, const utf8* pPath )
        {
            EditorAssetIndexEntry entry{};
            entry._kind = kind;
            entry._path = pPath;
            return entry;
        }

        static bool acceptAll( string_view ) { return true; }
    };
} // namespace

SW_TEST_CASE( EditorDocumentOpenUtilTest, CandidatesKeepOnlyTheKindThePanelAccepts )
{
    const EditorAssetType               data{ EditorAssetTypeIDs::kData };
    const EditorAssetType               shader{ EditorAssetTypeIDs::kShader };
    const vector<EditorAssetIndexEntry> listEntry{
        EditorDocumentOpenTestUtil::makeEntry( data, "fx/smoke.vfx.xml" ),
        EditorDocumentOpenTestUtil::makeEntry( data, "items/sword.xml" ),
        EditorDocumentOpenTestUtil::makeEntry( shader, "fx/smoke.vfx.xml.hlsl" ),
    };

    vector<string> listPath;
    EditorDocumentOpenUtil::collectCandidates( listEntry, data, []( string_view path )
    { return StringUtil::endsWith( path, ".vfx.xml" ); }, listPath );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listPath.size() );
    SW_EXPECT_EQUAL( string( "fx/smoke.vfx.xml" ), listPath[0] );

    EditorDocumentOpenUtil::collectCandidates( listEntry, EditorAssetType{}, &EditorDocumentOpenTestUtil::acceptAll, listPath );
    SW_EXPECT_TRUE( listPath.empty() ); // 앞 후보가 남지 않는다
}

// 경로는 후보 줄의 ImGui ID 다 — 같은 경로가 둘이면 한 줄만 눌린다.
SW_TEST_CASE( EditorDocumentOpenUtilTest, CandidatesAreSortedWithoutDuplicates )
{
    const EditorAssetType               data{ EditorAssetTypeIDs::kData };
    const vector<EditorAssetIndexEntry> listEntry{
        EditorDocumentOpenTestUtil::makeEntry( data, "b.xml" ),
        EditorDocumentOpenTestUtil::makeEntry( data, "a.xml" ),
        EditorDocumentOpenTestUtil::makeEntry( data, "b.xml" ),
    };

    vector<string> listPath;
    EditorDocumentOpenUtil::collectCandidates( listEntry, data, &EditorDocumentOpenTestUtil::acceptAll, listPath );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listPath.size() );
    SW_EXPECT_EQUAL( string( "a.xml" ), listPath[0] );
    SW_EXPECT_EQUAL( string( "b.xml" ), listPath[1] );
}

SW_TEST_CASE( EditorDocumentOpenUtilTest, RecentMovesToTheFrontAndIsBounded )
{
    vector<string> listPath;
    EditorDocumentOpenUtil::rememberRecent( listPath, "a", 2 );
    EditorDocumentOpenUtil::rememberRecent( listPath, "b", 2 );
    EditorDocumentOpenUtil::rememberRecent( listPath, "a", 2 );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listPath.size() );
    SW_EXPECT_EQUAL( string( "a" ), listPath[0] );
    SW_EXPECT_EQUAL( string( "b" ), listPath[1] );

    EditorDocumentOpenUtil::rememberRecent( listPath, "c", 2 );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listPath.size() );
    SW_EXPECT_EQUAL( string( "c" ), listPath[0] );
    SW_EXPECT_EQUAL( string( "a" ), listPath[1] );

    EditorDocumentOpenUtil::rememberRecent( listPath, "", 2 );
    SW_EXPECT_EQUAL( string( "c" ), listPath[0] );
}

// 목록 안의 글을 넘겨도 지운 글을 읽지 않는다.
SW_TEST_CASE( EditorDocumentOpenUtilTest, RecentAcceptsAPathFromItsOwnList )
{
    vector<string> listPath{ "first", "a path long enough not to fit the small string buffer" };
    EditorDocumentOpenUtil::rememberRecent( listPath, listPath[1], 8 );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listPath.size() );
    SW_EXPECT_EQUAL( string( "a path long enough not to fit the small string buffer" ), listPath[0] );
    SW_EXPECT_EQUAL( string( "first" ), listPath[1] );
}

SW_TEST_CASE( EditorDocumentOpenUtilTest, NewFocusIsAnotherDocumentOnly )
{
    SW_EXPECT_TRUE( EditorDocumentOpenUtil::isNewFocus( "a", "" ) );
    SW_EXPECT_TRUE( EditorDocumentOpenUtil::isNewFocus( "a", "b" ) );
    SW_EXPECT_FALSE( EditorDocumentOpenUtil::isNewFocus( "a", "a" ) );
    SW_EXPECT_FALSE( EditorDocumentOpenUtil::isNewFocus( "", "a" ) );
    SW_EXPECT_FALSE( EditorDocumentOpenUtil::isNewFocus( "", "" ) );
}

SW_TEST_CASE( EditorDocumentOpenUtilTest, TheDocumentTheUserAnsweredForWinsOverTheFocus )
{
    SW_EXPECT_TRUE( EditorDocumentOpenUtil::chooseDocumentToAccept( "pending", "focused" ) == "pending" );
    SW_EXPECT_TRUE( EditorDocumentOpenUtil::chooseDocumentToAccept( "", "focused" ) == "focused" );
    SW_EXPECT_TRUE( EditorDocumentOpenUtil::chooseDocumentToAccept( "", "" ).empty() );
}

SW_TEST_CASE( EditorDocumentOpenUtilTest, CancelGoesBackToTheOpenDocument )
{
    EditorWorkspace workspace{ nullptr };
    workspace.setFocusedAssetPath( "new.xml" );
    EditorDocumentOpenUtil::restoreFocusAfterCancel( workspace, "open.xml" );
    SW_EXPECT_EQUAL( string( "open.xml" ), workspace.getFocusedAssetPath() );
    SW_EXPECT_FALSE( EditorDocumentOpenUtil::isNewFocus( workspace.getFocusedAssetPath(), "open.xml" ) );
}

// 결함: 저장 안 한 새 문서(열린 경로 없음)에서 취소하면 포커스가 그대로 남아, 다음 그리기가 같은 확인 창을 또 띄웠다.
SW_TEST_CASE( EditorDocumentOpenUtilTest, CancelWithNoOpenDocumentDoesNotAskAgain )
{
    EditorWorkspace workspace{ nullptr };
    workspace.setFocusedAssetPath( "new.xml" );
    const string loaded;
    EditorDocumentOpenUtil::restoreFocusAfterCancel( workspace, loaded );
    SW_EXPECT_TRUE( workspace.getFocusedAssetPath().empty() );
    SW_EXPECT_FALSE( EditorDocumentOpenUtil::isNewFocus( workspace.getFocusedAssetPath(), loaded ) );
}

// 결함(감사 P0-4): 옮김 방송이 연 문서 경로만 고치고, 디스크에서 글을 고친 깨끗한 참조 문서는 옛 id 를 든 채 남아 다음 저장이 고친 참조를 덮었다.
SW_TEST_CASE( EditorDocumentOpenUtilTest, AssetMoveRemapsAndFindsRewrittenReferrer )
{
    EditorAssetMoveNotice notice;
    notice._listOldID       = { "game/a/textures/rock.png", "game/a/materials/rock.material" };
    notice._listNewID       = { "game/a/textures/stone.png", "game/b/materials/rock.material" };
    notice._listRewrittenID = { "game/a/maps/level.scene.xml", "game/b/materials/rock.material" }; // 옮긴 참조자는 새 id 로 온다

    // 옮긴 에셋 자신(참조자가 아니다): 경로만 따라가고 다시 읽지 않는다
    string         loaded = "game/a/textures/rock.png";
    vector<string> listRecent{ "game/a/textures/rock.png", "game/a/other.xml" };
    SW_EXPECT_FALSE( EditorDocumentOpenUtil::applyAssetMove( notice, loaded, listRecent ) );
    SW_EXPECT_EQUAL( string( "game/a/textures/stone.png" ), loaded );
    SW_EXPECT_TRUE( ( listRecent == vector<string>{ "game/a/textures/stone.png", "game/a/other.xml" } ) );

    // 옮기지 않은 참조 문서: 글을 고쳤으니 다시 읽는다
    loaded = "game/a/maps/level.scene.xml";
    SW_EXPECT_TRUE( EditorDocumentOpenUtil::applyAssetMove( notice, loaded, listRecent ) );
    SW_EXPECT_EQUAL( string( "game/a/maps/level.scene.xml" ), loaded );

    // 함께 옮긴 참조 문서: 새 id 로 바꾼 뒤 그 id 로 찾는다
    loaded = "game/a/materials/rock.material";
    SW_EXPECT_TRUE( EditorDocumentOpenUtil::applyAssetMove( notice, loaded, listRecent ) );
    SW_EXPECT_EQUAL( string( "game/b/materials/rock.material" ), loaded );

    // 연 문서가 없으면 다시 읽을 것도 없다
    loaded.clear();
    SW_EXPECT_FALSE( EditorDocumentOpenUtil::applyAssetMove( notice, loaded, listRecent ) );
}
