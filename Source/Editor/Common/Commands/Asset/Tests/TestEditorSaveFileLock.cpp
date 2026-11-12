// EditorSaveFileLockTest — 거절 시나리오가 잠근 파일은 시나리오가 어떻게 끝나든 쓰기 가능으로 돌아온다(감사 E-11, nogpu). 상태 저장소를 케이스마다 따로 건다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/Asset/EditorSaveFileLock.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 글 하나를 쓴 케이스 사본 파일의 경로입니다. */
    string makeLockTarget( const utf8* pName, const utf8* pText )
    {
        const string path = test::makeScratchPath( pName );
        (void)FileUtil::ensureParentDirectoryExists( path );
        (void)FileUtil::writeTextFile( path, pText );
        return path;
    }
} // namespace

/** @brief [EditorSaveFileLockTest] 잠금을 쥔 상태가 사라지면(체인 단계 초기화의 resetAll · 에디터 종료) `panel.unlockSaveFiles` 없이도 쓰기 가능으로 돌아온다 */
SW_TEST_CASE( EditorSaveFileLockTest, LockIsReleasedWhenTheStateGoes )
{
    EditorStateStore store;
    SubsystemScope   scope;
    SW_REQUIRE_TRUE( scope.bind( &store ) );
    const ScopedEditorScope scopedEditor( scope );

    const string path = makeLockTarget( "engineconfig.xml", "kept" );
    SW_REQUIRE_TRUE( EditorSaveFileLock::lock( { path } ).has_value() );
    SW_EXPECT_TRUE( FileUtil::isReadOnlyFile( path ) );
    SW_EXPECT_EQUAL( EditorSaveFileLock::getLockedCount(), 1u );

    store.resetAll(); // 시나리오가 끊긴 뒤 다음 단계 · 종료가 하는 일
    SW_EXPECT_FALSE( FileUtil::isReadOnlyFile( path ) );
    SW_EXPECT_EQUAL( EditorSaveFileLock::getLockedCount(), 0u );
}

/** @brief [EditorSaveFileLockTest] 풀기는 쓰기를 먼저 되돌리고, 잠긴 동안 바뀐 파일의 경로를 알린다 */
SW_TEST_CASE( EditorSaveFileLockTest, UnlockReportsAChangedFile )
{
    EditorStateStore store;
    SubsystemScope   scope;
    SW_REQUIRE_TRUE( scope.bind( &store ) );
    const ScopedEditorScope scopedEditor( scope );

    const string kept    = makeLockTarget( "kept.xml", "kept" );
    const string changed = makeLockTarget( "changed.xml", "before" );
    SW_REQUIRE_TRUE( EditorSaveFileLock::lock( { kept, changed } ).has_value() );
    SW_REQUIRE_TRUE( FileUtil::setWritable( changed, true ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( changed, "after" ) );

    const optional<string> changedPath = EditorSaveFileLock::unlock();
    SW_REQUIRE_TRUE( changedPath.has_value() );
    SW_EXPECT_EQUAL( *changedPath, changed );
    SW_EXPECT_FALSE( FileUtil::isReadOnlyFile( kept ) );
    SW_EXPECT_EQUAL( EditorSaveFileLock::getLockedCount(), 0u );
}

/** @brief [EditorSaveFileLockTest] 장부에 남은 잠금(소멸자가 돌지 못한 죽은 프로세스의 몫)은 restoreLeftovers 가 푼다 */
SW_TEST_CASE( EditorSaveFileLockTest, LedgerLeftoversAreRestored )
{
    EditorStateStore store;
    SubsystemScope   scope;
    SW_REQUIRE_TRUE( scope.bind( &store ) );
    const ScopedEditorScope scopedEditor( scope );

    const string path = makeLockTarget( "leftover.xml", "kept" );
    SW_REQUIRE_TRUE( EditorSaveFileLock::lock( { path } ).has_value() );
    EditorSaveFileLock::restoreLeftovers(); // 같은 장부를 다음 실행이 읽은 것과 같다
    SW_EXPECT_FALSE( FileUtil::isReadOnlyFile( path ) );
    SW_EXPECT_FALSE( EditorSaveFileLock::unlock().has_value() );
}
