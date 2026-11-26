// EditorAssetMoveOpenDocumentsTest — 옮기기 · 삭제 계획이 열린 문서를 본다(감사 P0-4): 미저장 참조 문서는 "먼저 저장" 으로, 연 문서(폴더면 그 아래)는 삭제를 거절한다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Workspace/IEditorPanelSubsystem.h"

using namespace sw;
using namespace sw::editor;

/** @brief [EditorAssetMoveOpenDocumentsTest] 미저장 문서가 옮기는 에셋의 참조 파일이면 그 id 다. 깨끗한 참조 문서 · 참조하지 않는 미저장 문서는 막지 않는다 */
SW_TEST_CASE( EditorAssetMoveOpenDocumentsTest, DirtyReferrerBlocksTheMove )
{
    EditorAssetMovePlan plan;
    plan._listOldID.push_back( "game/a/textures/rock.png" );
    plan._listNewID.push_back( "game/a/textures/stone.png" );
    plan._listReferrerID.push_back( "game/a/materials/rock.material" );

    vector<EditorOpenDocument> listOpen;
    listOpen.push_back( EditorOpenDocument{ "game/a/materials/rock.material", false } ); // 깨끗하다 — 디스크 글을 고쳐도 덮을 편집이 없다
    listOpen.push_back( EditorOpenDocument{ "game/a/maps/level.scene.xml", true } );     // 미저장이지만 옮기는 에셋을 적지 않았다
    SW_EXPECT_FALSE( EditorAssetFileCommands::findDirtyReferrerDocument( plan, listOpen ).has_value() );

    listOpen[0]._bDirty               = true;
    const optional<string> blockingID = EditorAssetFileCommands::findDirtyReferrerDocument( plan, listOpen );
    SW_REQUIRE_TRUE( blockingID.has_value() );
    SW_EXPECT_TRUE( *blockingID == "game/a/materials/rock.material" );
}

/** @brief [EditorAssetMoveOpenDocumentsTest] 디스크에 아직 없는 새 참조(미저장 글)도 막는다 — 막지 않으면 다음 저장이 사라진 옛 경로를 적는다. 이름 앞부분만 같은 경로는 참조가 아니다 */
SW_TEST_CASE( EditorAssetMoveOpenDocumentsTest, UnsavedNewReferenceBlocksTheMove )
{
    EditorAssetMovePlan plan;
    plan._listOldID.push_back( "game/a/textures/rock.png" );
    plan._listNewID.push_back( "game/a/textures/stone.png" );

    vector<EditorOpenDocument> listOpen;
    listOpen.push_back( EditorOpenDocument{ "game/a/materials/wall.material", true, "<Material><Texture path=\"game/a/textures/rock.png.bak\"/></Material>" } );
    SW_EXPECT_FALSE( EditorAssetFileCommands::findDirtyReferrerDocument( plan, listOpen ).has_value() );

    listOpen[0]._unsavedText          = "<Material><Texture path=\"game/a/textures/rock.png\"/></Material>";
    const optional<string> blockingID = EditorAssetFileCommands::findDirtyReferrerDocument( plan, listOpen );
    SW_REQUIRE_TRUE( blockingID.has_value() );
    SW_EXPECT_TRUE( *blockingID == "game/a/materials/wall.material" );
}

/** @brief [EditorAssetMoveOpenDocumentsTest] 옮기는 에셋 자신이 미저장으로 열려 있는 것은 막지 않는다 — 경로가 새 id 를 따라가 다음 저장이 새 자리에 쓴다 */
SW_TEST_CASE( EditorAssetMoveOpenDocumentsTest, DirtyMovedDocumentFollows )
{
    EditorAssetMovePlan plan;
    plan._listOldID.push_back( "scratch/assetmove/target/keep.material" );
    plan._listNewID.push_back( "scratch/assetmove/target/kept.material" );

    vector<EditorOpenDocument> listOpen;
    listOpen.push_back( EditorOpenDocument{ "scratch/assetmove/target/keep.material", true } );
    SW_EXPECT_FALSE( EditorAssetFileCommands::findDirtyReferrerDocument( plan, listOpen ).has_value() );

    listOpen[0]._bFollowsMove = false; // 글이 없는 문서(프리팹 · UI 미리보기)는 새 자리에서 다시 읽으면 편집을 잃는다 — 먼저 저장한다
    SW_EXPECT_TRUE( EditorAssetFileCommands::findDirtyReferrerDocument( plan, listOpen ).has_value() );
}

/** @brief [EditorAssetMoveOpenDocumentsTest] 지우는 파일 · 폴더 아래에 연 문서가 있으면 그 id 다. 이름 앞부분만 같은 형제(`a/b` 와 `a/bc`)는 아래가 아니다 */
SW_TEST_CASE( EditorAssetMoveOpenDocumentsTest, OpenDocumentBlocksTheDelete )
{
    vector<EditorOpenDocument> listOpen;
    listOpen.push_back( EditorOpenDocument{ "game/a/materials/rock.material", false } );

    const optional<string> sameFile = EditorAssetFileCommands::findOpenDocumentAtOrUnder( "game/a/materials/rock.material", listOpen );
    SW_REQUIRE_TRUE( sameFile.has_value() );
    SW_EXPECT_TRUE( *sameFile == "game/a/materials/rock.material" );
    SW_EXPECT_TRUE( EditorAssetFileCommands::findOpenDocumentAtOrUnder( "game/a/materials", listOpen ).has_value() );
    SW_EXPECT_FALSE( EditorAssetFileCommands::findOpenDocumentAtOrUnder( "game/a/mat", listOpen ).has_value() );
    SW_EXPECT_FALSE( EditorAssetFileCommands::findOpenDocumentAtOrUnder( "game/a/materials/rock.material.bak", listOpen ).has_value() );
    SW_EXPECT_FALSE( EditorAssetFileCommands::findOpenDocumentAtOrUnder( "game/b", listOpen ).has_value() );
}
