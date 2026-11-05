#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Multiplayer/Model/MultiplayerSyncModel.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 프로세스 표만 가진 세션입니다. 묻는 줄을 기억하고, 답은 케이스가 표를 고쳐 흉내 냅니다. */
    class FakeNetPlayProcesses final : public IEditorNetPlayProcesses
    {
    public:
        vector<EditorNetPlayProcessStatus> _listStatus{};
        vector<string>                     _listCommand{};
        vector<uint64>                     _listObjectRequestTick{};
        uint8                              _bRunning{ SW_TRUE };
        uint8                              _bSendFails{ SW_FALSE };

        bool                       isRunning() const override { return _bRunning != SW_FALSE; }
        EditorNetPlayProcessStatus getProcessStatus( int32 processIndex ) const override { return _listStatus[static_cast<size_t>( processIndex )]; }
        bool                       sendProcessCommand( int32 processIndex, string_view command ) override
        {
            (void)processIndex;
            if ( _bSendFails != SW_FALSE )
                return false;
            _listCommand.emplace_back( command );
            return true;
        }
        bool requestObjectHashes( int32 processIndex, uint64 minTick ) override
        {
            (void)processIndex;
            if ( _bSendFails != SW_FALSE )
                return false;
            _listObjectRequestTick.push_back( minTick );
            return true;
        }
    };

    /** @brief 서버(0)와 클라이언트 둘(1 · 2)입니다. 넉넉한 시한(한 시간)을 기본으로 씁니다. */
    struct MultiplayerSyncModelTest
    {
        static constexpr int64 kLongTimeout = 3600000;

        FakeNetPlayProcesses   _processes;
        EditorStateHashCompare _compare;
        MultiplayerSyncModel   _model;

        MultiplayerSyncModelTest()
            : _processes{}
            , _compare{}
            , _model{}
        {
            _processes._listStatus.resize( 3 );
            _processes._listStatus[0]._label = "server";
            _processes._listStatus[1]._label = "client1";
            _processes._listStatus[2]._label = "client2";
        }

        /** @brief 프로세스가 이력을 답한 것처럼 표를 고칩니다. */
        void answerHistory( int32 processIndex, uint64 hash )
        {
            EditorNetPlayProcessStatus& status = _processes._listStatus[static_cast<size_t>( processIndex )];
            status._listStateHash              = {
                StateHashSample{10,     hash},
                StateHashSample{11, hash + 1}
            };
            ++status._stateHashReportCount;
        }

        /** @brief 프로세스가 오브젝트 해시를 답한 것처럼 표를 고칩니다. */
        void answerObjects( int32 processIndex, uint64 tick, uint64 hash )
        {
            EditorNetPlayProcessStatus& status = _processes._listStatus[static_cast<size_t>( processIndex )];
            status._objectHashTick             = tick;
            status._listObjectHash             = {
                SceneObjectHash{  "same",    1},
                SceneObjectHash{"moving", hash}
            };
            ++status._objectHashReportCount;
        }

        /** @brief 두 칸을 클라이언트 1 · 2 의 이력으로 채웁니다. */
        void loadBothClients()
        {
            for ( const int32 sourceIndex : { 0, 1 } )
            {
                SW_REQUIRE_TRUE( _model.requestClientHistory( _processes, _compare, sourceIndex, sourceIndex + 1 ) );
                answerHistory( sourceIndex + 1, 100 );
            }
            _model.poll( _processes, _compare, kLongTimeout );
            SW_REQUIRE_EQUAL( _compare.getClientOrigin( 1 ), 2 );
        }
    };
} // namespace

// 묻고 나면 답 수가 늘 때까지 기다리고, 답이 오면 칸에 담고 어느 프로세스의 것인지 적는다.
SW_TEST_CASE_F( MultiplayerSyncModelTest, HistoryWaitsForANewAnswer )
{
    SW_REQUIRE_TRUE( _model.requestClientHistory( _processes, _compare, 0, 1 ) );
    SW_REQUIRE_EQUAL( _processes._listCommand.size(), size_t{ 1 } );
    SW_EXPECT_EQUAL( _processes._listCommand[0], formatToString( "state.hashline %#", MultiplayerSyncModel::kClientHistoryTicks ) );
    SW_EXPECT_TRUE( _model.isWaitingForHistory( 0, 1 ) );
    SW_EXPECT_FALSE( _model.isWaitingForHistory( 1, 1 ) );

    _model.poll( _processes, _compare, kLongTimeout );
    SW_EXPECT_TRUE( _model.isWaitingForHistory( 0, 1 ) );
    SW_EXPECT_TRUE( _compare.getSource( 0 )._listSample.empty() );

    answerHistory( 1, 77 );
    _model.poll( _processes, _compare, kLongTimeout );
    SW_EXPECT_FALSE( _model.isWaitingForHistory( 0, 1 ) );
    SW_REQUIRE_EQUAL( _compare.getSource( 0 )._listSample.size(), size_t{ 2 } );
    SW_EXPECT_EQUAL( _compare.getSource( 0 )._listSample[0]._hash, uint64{ 77 } );
    SW_EXPECT_EQUAL( _compare.getClientOrigin( 0 ), 1 );
}

// 씬 줄이 아닌 줄을 고르면 그 이름을 함께 묻는다.
SW_TEST_CASE_F( MultiplayerSyncModelTest, HistoryAsksForTheChosenChannel )
{
    SW_REQUIRE_TRUE( _compare.setHashChannel( "destruction" ) );
    SW_REQUIRE_TRUE( _model.requestClientHistory( _processes, _compare, 0, 1 ) );
    SW_EXPECT_EQUAL( _processes._listCommand[0], formatToString( "state.hashline %# destruction", MultiplayerSyncModel::kClientHistoryTicks ) );
}

// 끝난 프로세스에는 묻지 못한다. 대기 상태가 되지 않는다.
SW_TEST_CASE_F( MultiplayerSyncModelTest, HistoryRequestFailsWhenTheProcessIsGone )
{
    SW_EXPECT_LOG( "MultiplayerSync.SyncCouldNotAsk" );
    _processes._bSendFails = SW_TRUE;
    SW_EXPECT_FALSE( _model.requestClientHistory( _processes, _compare, 0, 1 ) );
    SW_EXPECT_FALSE( _model.isWaitingForHistory( 0, 1 ) );
}

// 답이 안 오면 시한에서 오류로 끝난다. 새 질의는 오류를 지운다.
SW_TEST_CASE_F( MultiplayerSyncModelTest, HistoryTimesOut )
{
    SW_EXPECT_LOG( "MultiplayerSync.Sync" );
    SW_REQUIRE_TRUE( _model.requestClientHistory( _processes, _compare, 0, 1 ) );
    _model.poll( _processes, _compare, 0 );
    SW_EXPECT_FALSE( _model.isWaitingForHistory( 0, 1 ) );
    SW_EXPECT_TRUE( _model.getError().find( "client1" ) != string::npos );

    SW_REQUIRE_TRUE( _model.requestClientHistory( _processes, _compare, 0, 1 ) );
    SW_EXPECT_TRUE( _model.getError().empty() );
}

// 세션이 끝나면 말없이 기다림을 접는다.
SW_TEST_CASE_F( MultiplayerSyncModelTest, HistoryStopsWaitingWhenTheSessionEnds )
{
    SW_REQUIRE_TRUE( _model.requestClientHistory( _processes, _compare, 0, 1 ) );
    _processes._bRunning = SW_FALSE;
    _model.poll( _processes, _compare, 0 );
    SW_EXPECT_FALSE( _model.isWaitingForHistory( 0, 1 ) );
    SW_EXPECT_TRUE( _model.getError().empty() );
}

// 빈 이력 답은 담지 못한다고 알리고 칸을 클라이언트 것으로 적지 않는다.
SW_TEST_CASE_F( MultiplayerSyncModelTest, EmptyHistoryIsNotTaken )
{
    SW_EXPECT_LOG( "MultiplayerSync.SyncCouldNotTake" );
    SW_REQUIRE_TRUE( _model.requestClientHistory( _processes, _compare, 0, 1 ) );
    ++_processes._listStatus[1]._stateHashReportCount;
    _model.poll( _processes, _compare, kLongTimeout );
    SW_EXPECT_EQUAL( _compare.getClientOrigin( 0 ), -1 );
}

// 두 클라이언트의 오브젝트 답이 모두 모여야 견주고, 다른 오브젝트 이름을 짚는다.
SW_TEST_CASE_F( MultiplayerSyncModelTest, ObjectCompareWaitsForBothClients )
{
    loadBothClients();
    _model.startObjectCompare( _processes, _compare, kLongTimeout );
    SW_EXPECT_TRUE( _model.isWaitingForObjects() );
    SW_REQUIRE_EQUAL( _processes._listObjectRequestTick.size(), size_t{ 2 } );
    SW_EXPECT_EQUAL( _processes._listObjectRequestTick[0], uint64{ 0 } ); // 채널 칸이 없으니 틱 하한이 없다

    answerObjects( 1, 50, 1 );
    _model.poll( _processes, _compare, kLongTimeout );
    SW_EXPECT_TRUE( _model.isWaitingForObjects() );
    SW_EXPECT_FALSE( _compare.hasObjectComparison() );

    answerObjects( 2, 50, 2 );
    _model.poll( _processes, _compare, kLongTimeout );
    SW_EXPECT_FALSE( _model.isWaitingForObjects() );
    SW_REQUIRE_TRUE( _compare.hasObjectComparison() );
    SW_EXPECT_EQUAL( _compare.getObjectTick(), uint64{ 50 } );
    SW_REQUIRE_EQUAL( _compare.getDifferingObjects().size(), size_t{ 1 } );
    SW_EXPECT_EQUAL( _compare.getDifferingObjects()[0], string( "moving" ) );
}

// 다른 틱의 답은 견주지 않는다.
SW_TEST_CASE_F( MultiplayerSyncModelTest, ObjectAnswersAtDifferentTicksAreRefused )
{
    SW_EXPECT_LOG( "MultiplayerSync.SyncCouldNotCompare" );
    loadBothClients();
    _model.startObjectCompare( _processes, _compare, kLongTimeout );
    answerObjects( 1, 50, 1 );
    answerObjects( 2, 51, 1 );
    _model.poll( _processes, _compare, kLongTimeout );
    SW_EXPECT_FALSE( _model.isWaitingForObjects() );
    SW_EXPECT_FALSE( _compare.hasObjectComparison() );
}

SW_TEST_CASE_F( MultiplayerSyncModelTest, ObjectCompareTimesOut )
{
    SW_EXPECT_LOG( "MultiplayerSync.Sync" );
    loadBothClients();
    _model.startObjectCompare( _processes, _compare, 0 ); // 시작하면서 한 번 본다 - 답이 없으니 바로 시한이다
    SW_EXPECT_FALSE( _model.isWaitingForObjects() );
    SW_EXPECT_TRUE( _model.getError().find( "object hashes" ) != string::npos );
}

// 물을 수 없거나 세션이 끝나면 기다리지 않는다. 읽어 둔 칸이 없으면 채널을 못 읽어 알린다.
SW_TEST_CASE_F( MultiplayerSyncModelTest, ObjectCompareHandlesAGoneSession )
{
    loadBothClients();
    _model.startObjectCompare( _processes, _compare, kLongTimeout );
    _processes._bRunning = SW_FALSE;
    _model.poll( _processes, _compare, kLongTimeout );
    SW_EXPECT_FALSE( _model.isWaitingForObjects() );
    SW_EXPECT_FALSE( _compare.hasObjectComparison() );

    SW_EXPECT_LOG( "MultiplayerSync.SyncCouldNotAsk2" );
    _model.startObjectCompare( _processes, _compare, kLongTimeout );
    SW_EXPECT_FALSE( _model.isWaitingForObjects() );
}

SW_TEST_CASE_F( MultiplayerSyncModelTest, ObjectCompareWithoutASourceReportsTheChannelError )
{
    SW_EXPECT_LOG( "MultiplayerSync.SyncCouldNotCompare" );
    _model.startObjectCompare( _processes, _compare, kLongTimeout );
    SW_EXPECT_FALSE( _model.isWaitingForObjects() );
    SW_EXPECT_FALSE( _compare.hasObjectComparison() );
}
