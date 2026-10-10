/**
 * @file TestShadowCopyName.cpp
 * @brief ShadowCopyName — 섀도 복사본 이름에 만든 프로세스를 적고, 정리는 다른 살아 있는 프로세스의 복사본을 남기는지.
 * @details 실행 파일 폴더는 같은 빌드의 프로세스 여럿이 함께 쓴다(CTest `-j` 가 SmokeTest 와 App 을 띄우는 AppTest 를 나란히 돌린다).
 *          정리가 남의 복사본을 지우면, 그 프로세스가 막 써 두고 올리기 직전의 복사본이 사라져 로드가 "모듈 없음" 으로 진다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Module/ModuleImageUtil.h"
#include "Core/Process/Process.h"

#include "ModuleHost/ShadowCopyName.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief 케이스가 어디서 끝나든 자식 프로세스를 죽이고 거둡니다. */
    struct ChildProcessScope
    {
        sw::Process _process;

        ~ChildProcessScope()
        {
            if ( _process.isRunning() )
                (void)_process.terminate();
            (void)_process.waitForExit();
        }
    };

    /** @brief 빈 파일 하나를 씁니다. */
    [[nodiscard]] bool writeEmptyFile( const sw::string& filePath )
    {
        return sw::FileUtil::writeTextFile( filePath, "" );
    }

    /** @brief @p processID 가 만든 것으로 이름 지은 SWGame 복사본 경로입니다. */
    sw::string makeCopyPath( const sw::string& directory, int32 processID, uint32 serial )
    {
        return sw::FileUtil::joinPath( directory, sw::ModuleImageUtil::formatSharedLibraryName( sw::ShadowCopyName::make( "SWGame", processID, serial, 1234 ) ) );
    }
} // namespace

/**
 * @brief [ShadowCopyNameTest] 이름은 만든 프로세스 ID 를 담고, 읽으면 그 ID 가 나온다 — 옛 형식은 ID 0 으로 읽힌다
 */
SW_TEST_CASE( ShadowCopyNameTest, NameCarriesTheProcessThatMadeIt )
{
    const sw::string name = sw::ShadowCopyName::make( "GF_Overworld", 4120, 3, 13435508261ull );
    SW_EXPECT_STREQ( "GF_Overworld_temp_p4120_3_13435508261", name.c_str() );

    int32 processID{ -1 };
    SW_EXPECT_TRUE( sw::ShadowCopyName::parse( "D:/Bin/" + sw::ModuleImageUtil::formatSharedLibraryName( name ), processID ) );
    SW_EXPECT_EQUAL( 4120, processID );
    // 디버그 심볼과 원자적 쓰기의 임시 파일도 같은 복사본의 것이다.
    SW_EXPECT_TRUE( sw::ShadowCopyName::parse( "GF_Overworld_temp_p4120_3_13435508261.pdb", processID ) );
    SW_EXPECT_EQUAL( 4120, processID );
    SW_EXPECT_TRUE( sw::ShadowCopyName::parse( "GF_Overworld_temp_p4120_3_13435508261.dll.tmp4120_7", processID ) );
    SW_EXPECT_EQUAL( 4120, processID );

    // 프로세스 ID 를 넣기 전 형식은 복사본이지만 주인을 모른다.
    SW_EXPECT_TRUE( sw::ShadowCopyName::parse( "EditorModule_temp_34_13435507478.dll", processID ) );
    SW_EXPECT_EQUAL( 0, processID );

    SW_EXPECT_FALSE( sw::ShadowCopyName::parse( "SWGame.dll", processID ) );
    SW_EXPECT_FALSE( sw::ShadowCopyName::parse( "notes_temp_draft.txt", processID ) );
    SW_EXPECT_FALSE( sw::ShadowCopyName::parse( "SWGame_temp_p_3_1.dll", processID ) );
    SW_EXPECT_FALSE( sw::ShadowCopyName::parse( "SWGame_temp_pX1_3_1.dll", processID ) );
}

/**
 * @brief [ShadowCopyNameTest] 정리는 이 프로세스 · 끝난 프로세스 · 옛 형식의 복사본을 지우고, 다른 살아 있는 프로세스의 것은 남긴다
 * @details 살아 있는 남은 몇 초 사는 자식 프로세스로, 끝난 남은 곧바로 끝나 거둔 자식 프로세스로 세운다.
 */
SW_TEST_CASE( ShadowCopyNameTest, CleanupKeepsCopiesOfOtherLiveProcesses )
{
#if defined( SW_PLATFORM_WINDOWS )
    const sw::string liveCommand = "ping.exe 127.0.0.1 -n 30";
    const sw::string doneCommand = "cmd.exe /c exit 0";
#else
    const sw::string liveCommand = "sleep 30";
    const sw::string doneCommand = "exit 0";
#endif

    ChildProcessScope liveChild;
    SW_ASSERT_TRUE( liveChild._process.launch( liveCommand ) );
    const int32 liveProcessID = liveChild._process.getProcessID();

    sw::Process doneChild;
    SW_ASSERT_TRUE( doneChild.launch( doneCommand ) );
    const int32 doneProcessID = doneChild.getProcessID(); // POSIX 는 거둔 뒤 0 이 되므로 먼저 읽는다
    SW_EXPECT_EQUAL( 0, doneChild.waitForExit() );

    SW_ASSERT_TRUE( liveProcessID > 0 );
    SW_ASSERT_TRUE( doneProcessID > 0 );
    SW_EXPECT_TRUE( sw::Process::isProcessAlive( liveProcessID ) );
    SW_EXPECT_TRUE( sw::Process::isProcessAlive( sw::Process::getCurrentProcessID() ) );
    SW_EXPECT_FALSE( sw::Process::isProcessAlive( doneProcessID ) );
    SW_EXPECT_FALSE( sw::Process::isProcessAlive( 0 ) );

    const sw::string directory     = test::makeTempDirectory( "shadow_copies" );
    const sw::string livePath      = makeCopyPath( directory, liveProcessID, 1 );
    const sw::string ownPath       = makeCopyPath( directory, sw::Process::getCurrentProcessID(), 2 );
    const sw::string donePath      = makeCopyPath( directory, doneProcessID, 3 );
    const sw::string legacyPath    = sw::FileUtil::joinPath( directory, sw::ModuleImageUtil::formatSharedLibraryName( "SWGame_temp_4_1234" ) );
    const sw::string unrelatedPath = sw::FileUtil::joinPath( directory, "SWGame.txt" );
    for ( const sw::string* pPath : { &livePath, &ownPath, &donePath, &legacyPath, &unrelatedPath } )
    {
        SW_ASSERT_TRUE( writeEmptyFile( *pPath ) );
    }

    SW_EXPECT_EQUAL( 3u, sw::ShadowCopyName::removeStaleCopies( directory ) );
    SW_EXPECT_TRUE_MSG( sw::FileUtil::exists( livePath ), "a copy another running process just wrote was deleted" );
    SW_EXPECT_FALSE( sw::FileUtil::exists( ownPath ) );
    SW_EXPECT_FALSE( sw::FileUtil::exists( donePath ) );
    SW_EXPECT_FALSE( sw::FileUtil::exists( legacyPath ) );
    SW_EXPECT_TRUE( sw::FileUtil::exists( unrelatedPath ) );
}
