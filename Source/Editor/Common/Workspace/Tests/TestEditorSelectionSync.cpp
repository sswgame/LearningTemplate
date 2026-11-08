/**
 * @file TestEditorSelectionSync.cpp
 * @brief 선택 동기화 채널(`EditorSelectionSync`)이 문맥마다 칸을 따로 두고, 같은 값은 번호를 올리지 않으며, 받는 쪽이 고른 문맥만 보는지 봅니다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/EditorSelectionSync.h"

using namespace sw;
using namespace sw::editor;

/** @brief [EditorSelectionSyncTest] 미리보기 칸이 바뀌어도 메인 씬 칸은 그대로이고, 메인 씬만 따르는 쪽은 미리보기 애셋을 보지 않는다 */
SW_TEST_CASE( EditorSelectionSyncTest, PreviewNeverOverwritesTheMainScene )
{
    EditorSelectionSync sync;
    constexpr uint8     kMainOnly = makeSelectionContextBit( EditorSelectionContext::MainScene );
    SW_EXPECT_TRUE( sync.findLatest( kMainOnly ) == nullptr );

    sync.publish( EditorSelectionContext::MainScene, "game/empty/models/crate_medium.mesh", 7 );
    const uint64 mainSequence = sync.getEntry( EditorSelectionContext::MainScene )._sequence;
    SW_EXPECT_TRUE( mainSequence != 0 );

    sync.publish( EditorSelectionContext::PreviewScene, "game/empty/prefabs/testprop.prefab.xml", 3 );
    SW_EXPECT_EQUAL( mainSequence, sync.getEntry( EditorSelectionContext::MainScene )._sequence );
    const EditorSelectionSyncEntry* pMain = sync.findLatest( kMainOnly );
    SW_REQUIRE_TRUE( pMain != nullptr );
    SW_EXPECT_TRUE( pMain->_assetPath == "game/empty/models/crate_medium.mesh" );

    // 모든 문맥을 따르면 가장 늦은 칸(미리보기)이다.
    const uint8                     kAll    = static_cast<uint8>( kMainOnly | makeSelectionContextBit( EditorSelectionContext::PreviewScene ) | makeSelectionContextBit( EditorSelectionContext::ContentBrowser ) );
    const EditorSelectionSyncEntry* pLatest = sync.findLatest( kAll );
    SW_REQUIRE_TRUE( pLatest != nullptr );
    SW_EXPECT_TRUE( pLatest->_context == EditorSelectionContext::PreviewScene );
}

/** @brief [EditorSelectionSyncTest] 같은 값을 다시 알리면 번호가 그대로다. 애셋이 없는 선택(빈 선택 · 애셋 없는 오브젝트)은 칸을 비우고 받는 쪽이 건너뛴다 */
SW_TEST_CASE( EditorSelectionSyncTest, SameValueKeepsTheSequenceAndEmptyIsSkipped )
{
    EditorSelectionSync sync;
    sync.publish( EditorSelectionContext::ContentBrowser, "engine/vfx/sparks.vfx.xml", 0 );
    const uint64 first = sync.getEntry( EditorSelectionContext::ContentBrowser )._sequence;
    sync.publish( EditorSelectionContext::ContentBrowser, "engine/vfx/sparks.vfx.xml", 0 );
    SW_EXPECT_EQUAL( first, sync.getEntry( EditorSelectionContext::ContentBrowser )._sequence );

    sync.publish( EditorSelectionContext::MainScene, "game/empty/models/crate_medium.mesh", 7 );
    sync.publish( EditorSelectionContext::MainScene, "", 0 ); // 선택을 비웠다
    SW_EXPECT_TRUE( sync.getEntry( EditorSelectionContext::MainScene )._sequence > first );
    SW_EXPECT_TRUE( sync.findLatest( makeSelectionContextBit( EditorSelectionContext::MainScene ) ) == nullptr );
}
