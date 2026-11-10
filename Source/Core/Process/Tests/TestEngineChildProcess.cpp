// EngineChildProcess — 엔진 자식 명령의 한 입구: 자동화 실행(-unattended · -scenario)만 자식 인자에 -unattended 를 더하고, 이미 있으면 다시 더하지 않으며, 상대 경로는 빈 명령이다.
#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/FileUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Process/EngineChildProcess.h"
#include "Core/Testing/TestFramework.h"

namespace
{
    /** @brief @p argument 하나(없으면 실행 파일 이름만)로 파싱한 명령줄입니다. */
    void parseCommandLine( sw::CommandLineManager& outCommandLine, const utf8* pArgument )
    {
        outCommandLine.initialize();
        const sw::optional<sw::AbsolutePath> baseDirectory = sw::AbsolutePath::tryMake( sw::test::makeScratchPath( "base" ) );
        SW_ASSERT( baseDirectory.has_value() );
        utf8* argv[] = { const_cast<utf8*>( "TestApp.exe" ), const_cast<utf8*>( pArgument ) };
        outCommandLine.parse( pArgument != nullptr ? 2 : 1, argv, *baseDirectory );
    }
} // namespace

/**
 * @brief [EngineChildProcessTest] 자동화 실행만 자식에 -unattended 를 넘긴다 — 빠지면 넷 플레이 클라이언트가 사양으로 품질을 골라 서버와 합성이 갈렸다
 */
SW_TEST_CASE( EngineChildProcessTest, AutomatedRunPassesUnattendedToChildren )
{
    const sw::string executable = sw::FileUtil::getExecutablePath();

    sw::CommandLineManager humanRun;
    parseCommandLine( humanRun, nullptr );
    const sw::ProcessCommand humanChild = sw::EngineChildProcess::makeCommand( humanRun, executable, { "-gv_firstScene=a.scene.xml" } );
    SW_EXPECT_EQUAL( size_t{ 1 }, humanChild._listArgument.size() );

    sw::CommandLineManager scenarioRun;
    parseCommandLine( scenarioRun, "-scenario=engine/automation/editor/idle.scenario.xml" );
    const sw::ProcessCommand child = sw::EngineChildProcess::makeCommand( scenarioRun, executable, { "-gv_firstScene=a.scene.xml" } );
    SW_REQUIRE_EQUAL( size_t{ 2 }, child._listArgument.size() );
    SW_EXPECT_EQUAL( sw::string( "-unattended" ), child._listArgument[1] );

    const sw::ProcessCommand already = sw::EngineChildProcess::makeCommand( scenarioRun, executable, { "-unattended" } );
    SW_EXPECT_EQUAL( size_t{ 1 }, already._listArgument.size() );
}

/**
 * @brief [EngineChildProcessTest] 절대 경로가 아닌 실행 파일은 빈 명령이다(띄우기가 실패로 알린다)
 */
SW_TEST_CASE( EngineChildProcessTest, RelativeExecutableMakesEmptyCommand )
{
    sw::CommandLineManager humanRun;
    parseCommandLine( humanRun, nullptr );
    const sw::ProcessCommand command = sw::EngineChildProcess::makeCommand( humanRun, "App", { "-x" } );
    SW_EXPECT_TRUE( command._listArgument.empty() );
}
