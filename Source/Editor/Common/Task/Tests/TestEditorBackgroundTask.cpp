// EditorBackgroundTaskTest — 백그라운드 잡의 규약: 상태(없음 · 도는 중 · 끝남), 결과는 한 번만, 새 요청 뒤의 옛 세대는 입력도 결과도 쓰지 못한다(워커 없이 규약만 — nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Task/EditorBackgroundTask.h"
#include "Editor/Common/Task/EditorExternalToolJob.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 규약의 보호 멤버를 여는 잡입니다 — 제출하지 않고 워커의 차례를 테스트가 부른다. */
    class ProtocolTask final : public EditorBackgroundTask<int32, int32>
    {
    public:
        uint32             begin( int32 input ) { return beginRequest( input ); }
        [[nodiscard]] bool read( uint32 generation, int32& outInput ) const { return readInput( _pState, generation, outInput ); }
        void               finish( uint32 generation, int32 result ) { publish( _pState, generation, std::move( result ) ); }
    };
} // namespace

/** @brief [EditorBackgroundTaskTest] 요청 전은 없음, 요청하면 도는 중, 결과는 한 번만 나오고 다시 없음이다 */
SW_TEST_CASE( EditorBackgroundTaskTest, ResultIsTakenOnce )
{
    ProtocolTask task;
    int32        result = -1;
    SW_EXPECT_TRUE( task.take( result ) == EditorTaskPoll::Idle );
    SW_EXPECT_FALSE( task.isPending() );

    const uint32 generation = task.begin( 7 );
    SW_EXPECT_TRUE( task.isPending() );
    SW_EXPECT_TRUE( task.take( result ) == EditorTaskPoll::Running );
    SW_EXPECT_EQUAL( -1, result ); // 끝나기 전에는 그대로다
    int32 input = 0;
    SW_REQUIRE_TRUE( task.read( generation, input ) );
    SW_EXPECT_EQUAL( 7, input );

    task.finish( generation, 70 );
    SW_EXPECT_FALSE( task.isPending() );
    SW_EXPECT_TRUE( task.take( result ) == EditorTaskPoll::Ready );
    SW_EXPECT_EQUAL( 70, result );
    SW_EXPECT_TRUE( task.take( result ) == EditorTaskPoll::Idle );
}

/** @brief [EditorBackgroundTaskTest] 새 요청 뒤의 옛 세대는 입력을 읽지도 결과를 싣지도 못하고, 새 요청은 가져가지 않은 옛 결과를 버린다 */
SW_TEST_CASE( EditorBackgroundTaskTest, StaleGenerationIsIgnored )
{
    ProtocolTask task;
    const uint32 stale   = task.begin( 1 );
    const uint32 current = task.begin( 2 );
    int32        value   = 0;
    SW_EXPECT_FALSE( task.read( stale, value ) );
    task.finish( stale, 10 );
    SW_EXPECT_TRUE( task.take( value ) == EditorTaskPoll::Running );
    task.finish( current, 20 );

    const uint32 next = task.begin( 3 ); // 20 은 가져가지 않았다
    SW_EXPECT_TRUE( task.take( value ) == EditorTaskPoll::Running );
    task.finish( next, 30 );
    SW_EXPECT_TRUE( task.take( value ) == EditorTaskPoll::Ready );
    SW_EXPECT_EQUAL( 30, value );
}

/** @brief [EditorExternalToolJobTest] 파이썬 스크립트 명령은 검색 경로의 실행기에 스크립트를 마지막 인자로 주고, 요청하지 않은 잡은 줄도 결과도 없다 */
SW_TEST_CASE( EditorExternalToolJobTest, ScriptCommandAndIdleJob )
{
    const ProcessCommand command = EditorExternalToolJob::makePythonScriptCommand( "Scripts/dev/tool.py" );
    SW_EXPECT_TRUE( command._bSearchPath != 0 );
    SW_REQUIRE_FALSE( command._listArgument.empty() );
    SW_EXPECT_EQUAL( string( "Scripts/dev/tool.py" ), command._listArgument.back() );

    EditorExternalToolJob    job;
    EditorExternalToolResult result;
    vector<string>           listLine;
    job.takeLines( listLine );
    SW_EXPECT_TRUE( listLine.empty() );
    SW_EXPECT_TRUE( job.take( result ) == EditorTaskPoll::Idle );
    job.cancelAndWait(); // 도는 것이 없어도 부를 수 있다
    SW_EXPECT_FALSE( job.isPending() );
}
