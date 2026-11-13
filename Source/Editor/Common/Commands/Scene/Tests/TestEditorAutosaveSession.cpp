#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/Scene/EditorAutosaveCommands.h"

// EditorAutosaveSessionTest — 자동 저장 세션 폴더의 디스크 규칙. 세션마다 폴더 하나라 다른 에디터가 begin · end 해도 살아 있는 세션의 사본은 남고,
// pid 가 죽은 세션만 넘겨받아 지운다. 스크래치 폴더만 쓴다.

namespace
{
    struct EditorAutosaveSessionTestInternal
    {
        // 실제 프로세스가 가질 수 없는 큰 pid(Linux pid_max 는 2^22 이하, Windows 는 실제로 이 근처를 쓰지 않는다) — 죽은 세션 · 다른 에디터를 흉내 낸다.
        static constexpr int32 kOtherEditorPID = 0x7FFFFFF8;
        static constexpr int32 kDeadPID        = 0x7FFFFFFC;

        /** @brief 세션 폴더 안 씬 칸에 사본 파일 하나를 씁니다. 그 경로입니다. */
        static sw::string writeCopy( sw::string_view root, const sw::editor::EditorAutosaveSession& session )
        {
            const sw::string slotFolder = sw::FileUtil::joinPath( session.makeFolder( root ), "maps_test.scene.xml" );
            const sw::string copyPath   = sw::FileUtil::joinPath( slotFolder, "1.scene.xml" );
            SW_EXPECT_TRUE( sw::FileUtil::ensureDirectoryExists( slotFolder ) );
            SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( copyPath, "<Scene/>" ) );
            return copyPath;
        }
    };
} // namespace

/**
 * @brief [EditorAutosaveSessionTest] 세션 A 가 사본을 쓴 뒤 세션 B 가 begin · end 해도 A 의 사본이 남는다
 * @details 예전에는 잠금 · 사본 폴더가 모든 에디터에 하나라 둘째 에디터의 정상 종료가 첫 에디터의 사본을 지웠다. A 는 이 테스트 프로세스(살아 있다)다.
 */
SW_TEST_CASE( EditorAutosaveSessionTest, OtherSessionEndKeepsLiveSessionCopies )
{
    using Session         = sw::editor::EditorAutosaveSession;
    using Internal        = EditorAutosaveSessionTestInternal;
    const sw::string root = sw::test::makeScratchPath( "Autosave" );

    const Session          sessionA{ sw::Process::getCurrentProcessID(), 1 };
    sw::vector<sw::string> listRecoveredFolder;
    SW_REQUIRE_TRUE( sessionA.open( root, listRecoveredFolder ) );
    SW_EXPECT_TRUE( listRecoveredFolder.empty() );
    const sw::string copyA = Internal::writeCopy( root, sessionA );

    const Session sessionB{ Internal::kOtherEditorPID, 2 };
    SW_REQUIRE_TRUE( sessionB.open( root, listRecoveredFolder ) );
    SW_EXPECT_TRUE( listRecoveredFolder.empty() ); // A 는 살아 있다 — 복구 후보가 아니다
    Session::removeOwned( root, sessionB._processID );

    SW_EXPECT_TRUE( sw::FileUtil::exists( copyA ) );
    SW_EXPECT_FALSE( sw::FileUtil::isDirectory( sessionB.makeFolder( root ) ) );
}

/**
 * @brief [EditorAutosaveSessionTest] pid 가 죽은 세션만 넘겨받는다 — 잠금을 새 세션으로 고쳐 쓰고, 새 세션이 끝날 때 함께 지운다
 */
SW_TEST_CASE( EditorAutosaveSessionTest, DeadSessionIsRecoveredAndRemovedWithRecoveringSession )
{
    using Session         = sw::editor::EditorAutosaveSession;
    using Internal        = EditorAutosaveSessionTestInternal;
    const sw::string root = sw::test::makeScratchPath( "Autosave" );

    const Session          deadSession{ Internal::kDeadPID, 3 };
    sw::vector<sw::string> listRecoveredFolder;
    SW_REQUIRE_TRUE( deadSession.open( root, listRecoveredFolder ) );
    const sw::string deadCopy = Internal::writeCopy( root, deadSession );

    const Session recoveringSession{ Internal::kOtherEditorPID, 4 };
    SW_REQUIRE_TRUE( recoveringSession.open( root, listRecoveredFolder ) );
    SW_REQUIRE_TRUE( listRecoveredFolder.size() == 1 );
    SW_EXPECT_TRUE( sw::FileUtil::isSameDiskPath( listRecoveredFolder[0], deadSession.makeFolder( root ) ) );
    Session owner{};
    SW_REQUIRE_TRUE( Session::readLock( listRecoveredFolder[0], owner ) );
    SW_EXPECT_TRUE( owner._processID == recoveringSession._processID && owner._startTicks == recoveringSession._startTicks );
    SW_EXPECT_TRUE( sw::FileUtil::exists( deadCopy ) ); // 넘겨받기는 사본을 지우지 않는다 — 복구 후보다

    Session::removeOwned( root, recoveringSession._processID );
    SW_EXPECT_FALSE( sw::FileUtil::exists( deadCopy ) );
}
