#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Process/CrashContext.h"
#include "Core/Process/CrashHandler.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) CrashReport — 크래시 경로에서 **할당 없이** 미리 담아 둔 컨텍스트와, 세 플랫폼이 함께 쓰는 리포트 본문
//    진짜 크래시를 낼 수는 없으므로 리포트를 만드는 조각들을 직접 부른다.
// ------------------------------------------------------------------------------

namespace
{
    /** @brief 이 파일의 테스트가 리포트를 쏟아 놓을 임시 폴더를 준비하고 그 경로를 돌려줍니다. */
    sw::string prepareReportFolderInternal()
    {
        const sw::string folder = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "SwCrashReportTest" );
        sw::FileUtil::ensureDirectoryExists( folder );
        sw::setCrashReportFolder( folder );
        return folder;
    }

    /** @brief 세션 ID 로 만들어지는 리포트 파일 하나를 읽습니다. */
    bool readReportFileInternal( const utf8* pExtension, sw::string& outText )
    {
        utf8 arrPath[sw::constant::kMaxBuffer1024]{};
        sw::buildCrashReportPath( arrPath, sw::constant::kMaxBuffer1024, pExtension );
        return sw::FileUtil::readTextFile( arrPath, outText );
    }
} // namespace

/**
 * @brief [CrashReportTest] 미리 등록해 둔 키-값이 컨텍스트 파일에 그대로 나온다
 * @details 덤프·코어만으로는 "어느 백엔드였는가" 를 알 수 없다. 그 답은 크래시 **전에** 올려 둔
 *          것에서만 나오므로, 올린 것이 실제로 파일에 닿는지 못박는다.
 */
SW_TEST_CASE( CrashReportTest, RegisteredContextReachesTheContextFile )
{
    prepareReportFolderInternal();

    sw::CrashHandler::setContextValue( "RHI", "DX12" );
    sw::CrashHandler::setContextValue( "Build", "CrashReportTest" );

    sw::writeCrashContextFile( "unit test reason", reinterpret_cast<const void*>( 0xDEADBEEFull ), 4242, 8484 );

    sw::string text;
    SW_ASSERT_TRUE( readReportFileInternal( "txt", text ) );

    SW_EXPECT_TRUE( text.find( "unit test reason" ) != sw::string::npos );
    SW_EXPECT_TRUE( text.find( "deadbeef" ) != sw::string::npos );
    SW_EXPECT_TRUE( text.find( "4242" ) != sw::string::npos );
    SW_EXPECT_TRUE( text.find( "8484" ) != sw::string::npos );
    SW_EXPECT_TRUE( text.find( "DX12" ) != sw::string::npos );
    SW_EXPECT_TRUE( text.find( "CrashReportTest" ) != sw::string::npos );
    SW_EXPECT_TRUE( text.find( sw::CrashHandler::getSessionId() ) != sw::string::npos );
}

/**
 * @brief [CrashReportTest] 같은 키는 덮어쓰고, 자리가 차면 조용히 버린다
 * @details 자리가 없을 때 버리는 것은 의도다 — 크래시 진단을 돕자고 넣은 것이 실패를 키우면 안 된다.
 *          다만 **버리되 망가뜨리지는 않아야** 한다. 이미 들어간 값은 그대로 남아야 한다.
 */
SW_TEST_CASE( CrashReportTest, ContextStoreOverwritesKeyAndDropsOverflowSafely )
{
    prepareReportFolderInternal();

    sw::CrashContextStore& store = sw::CrashContextStore::get();
    store._entryCount            = 0;

    store.set( "RHI", "Vulkan" );
    store.set( "RHI", "DX11" );
    SW_EXPECT_EQUAL( 1u, store._entryCount );
    SW_EXPECT_TRUE( store._arrEntry[0]._value.view() == sw::string_view( "DX11" ) );

    // 정원을 넘겨 본다. 개수는 정원에서 멈추고 첫 항목은 그대로여야 한다.
    for ( uint32 index = 0; index < sw::CrashHandler::kMaxContextEntry + 8; ++index )
    {
        const sw::string key = "Key" + sw::string( std::to_string( index ).c_str() );
        store.set( key, "value" );
    }
    SW_EXPECT_EQUAL( sw::CrashHandler::kMaxContextEntry, store._entryCount );
    SW_EXPECT_TRUE( store._arrEntry[0]._value.view() == sw::string_view( "DX11" ) );

    // 빈 키는 아무 일도 하지 않는다.
    const uint32 countBefore = store._entryCount;
    store.set( "", "ignored" );
    SW_EXPECT_EQUAL( countBefore, store._entryCount );

    store._entryCount = 0;
}

/**
 * @brief [CrashReportTest] 리포트가 "무엇을 보내면 되는지" 를 적고, 없는 덤프는 적지 않는다
 * @details 이 본문은 예전에 Windows 와 POSIX 가 각자 적고 있었고 이미 갈려 있었다 — 파일 목록이
 *          Windows 에만 있었고(리눅스 사용자는 리포트가 어디 났는지 알 수 없었다), 그 목록마저
 *          미니덤프를 **쓰지 못했을 때도** 적고 스택 파일은 아예 빠뜨렸다. 이제 한 곳에서 만든다.
 */
SW_TEST_CASE( CrashReportTest, ReportListsFilesToSendAndSkipsTheDumpWhenThereIsNone )
{
    prepareReportFolderInternal();

    utf8 arrContextPath[sw::constant::kMaxBuffer1024]{};
    utf8 arrStackPath[sw::constant::kMaxBuffer1024]{};
    utf8 arrDumpPath[sw::constant::kMaxBuffer1024]{};
    sw::buildCrashReportPath( arrContextPath, sw::constant::kMaxBuffer1024, "txt" );
    sw::buildCrashReportPath( arrStackPath, sw::constant::kMaxBuffer1024, "stack.txt" );
    sw::buildCrashReportPath( arrDumpPath, sw::constant::kMaxBuffer1024, "dmp" );

    // 1) 덤프가 없을 때 — 컨텍스트와 스택 파일만 적혀야 한다.
    sw::writeCrashReport( "unit test fault", reinterpret_cast<const void*>( 0x1234ull ), nullptr, false );

    sw::string text;
    SW_ASSERT_TRUE( readReportFileInternal( "stack.txt", text ) );

    SW_EXPECT_TRUE( text.find( "unit test fault" ) != sw::string::npos );
    SW_EXPECT_TRUE( text.find( "crash report files" ) != sw::string::npos );
    SW_EXPECT_TRUE( text.find( arrContextPath ) != sw::string::npos );
    SW_EXPECT_TRUE_MSG( text.find( arrStackPath ) != sw::string::npos,
                        "스택 파일 경로가 목록에 없다 — 정작 스택이 든 파일을 안 보내게 된다" );
    SW_EXPECT_TRUE_MSG( text.find( arrDumpPath ) == sw::string::npos,
                        "쓰지도 않은 미니덤프를 보내라고 적었다" );

    // 2) 덤프를 썼을 때 — 그때만 덤프 경로가 붙는다.
    sw::writeCrashReport( "unit test fault", reinterpret_cast<const void*>( 0x1234ull ), nullptr, true );

    SW_ASSERT_TRUE( readReportFileInternal( "stack.txt", text ) );
    SW_EXPECT_TRUE( text.find( arrDumpPath ) != sw::string::npos );
}

/**
 * @brief [CrashReportTest] 세션 ID 는 한 프로세스에서 하나이고 리포트 경로에 들어간다
 * @details 고객이 보낸 덤프와 로그가 같은 실행의 것인지 확인하는 유일한 방법이다.
 */
SW_TEST_CASE( CrashReportTest, SessionIdIsStableAndAppearsInReportPaths )
{
    const utf8* pFirst  = sw::CrashHandler::getSessionId();
    const utf8* pSecond = sw::getCrashSessionId();

    SW_ASSERT_NOT_NULL( pFirst );
    SW_EXPECT_STREQ( pFirst, pSecond );

    const sw::string folder = prepareReportFolderInternal();

    utf8 arrPath[sw::constant::kMaxBuffer1024]{};
    sw::buildCrashReportPath( arrPath, sw::constant::kMaxBuffer1024, "dmp" );

    const sw::string path{ arrPath };
    SW_EXPECT_TRUE( path.find( folder ) != sw::string::npos );
    SW_EXPECT_TRUE( path.find( pFirst ) != sw::string::npos );
    SW_EXPECT_TRUE( path.find( ".dmp" ) != sw::string::npos );
}
