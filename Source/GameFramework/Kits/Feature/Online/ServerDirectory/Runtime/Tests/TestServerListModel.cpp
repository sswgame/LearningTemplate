// ServerListModelTest — 서버 목록 상태: 클라이언트가 없거나 연결 전이면 Failed(kUnavailable), 종류가 상한을 넘으면 보내지 않고 Failed(kInvalidRequest),
// 실패마다 목록을 비우고 수(revision)를 올린다. 응답이 오는 길(Loaded · 겹친 요청 거절)은 서버가 있어야 하므로 여기서 보지 않는다.
// 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerDirectoryClient.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerListModel.h"
#include "GameFramework/Runtime/Online/Directory/ServerRecord.h"
#include "GameFramework/Runtime/Online/Service/OnlineServiceClient.h"

using namespace sw;

/** @brief [ServerListModelTest] 클라이언트가 없으면 묻지 않고 Failed(kUnavailable) 로 끝난다 */
SW_TEST_CASE( ServerListModelTest, NoClientFailsAsUnavailable )
{
    ServerListModel model;
    SW_EXPECT_TRUE( model.getState() == ServerListState::Idle );
    SW_EXPECT_FALSE( model.refresh( "dedicated" ) );
    SW_EXPECT_TRUE( model.getState() == ServerListState::Failed );
    SW_EXPECT_EQUAL( OnlineError::kUnavailable, model.getErrorCode() );
    SW_EXPECT_EQUAL( uint64{ 1 }, model.getRevision() );
    SW_EXPECT_TRUE( model.getEntries().empty() );
}

/** @brief [ServerListModelTest] 연결 전 클라이언트는 그 호출 안에서 kUnavailable 로 답하고, 상태는 Loading 에 머물지 않는다(다시 물을 수 있다) */
SW_TEST_CASE( ServerListModelTest, UnconnectedClientFailsInsideTheCall )
{
    OnlineServiceClient   serviceClient; // 디렉터리 클라이언트보다 먼저 만들어 나중에 사라진다
    ServerDirectoryClient directoryClient;
    directoryClient.initialize( &serviceClient );
    ServerListModel model;
    model.initialize( &directoryClient );

    SW_EXPECT_FALSE( model.refresh( "dedicated" ) );
    SW_EXPECT_TRUE( model.getState() == ServerListState::Failed );
    SW_EXPECT_EQUAL( OnlineError::kUnavailable, model.getErrorCode() );
    SW_EXPECT_EQUAL( uint64{ 2 }, model.getRevision() ); // 로딩 표시 한 번 + 응답 한 번

    SW_EXPECT_FALSE( model.refresh( "dedicated" ) ); // Loading 에 갇히지 않았으므로 다시 보낸다
    SW_EXPECT_EQUAL( uint64{ 4 }, model.getRevision() );
}

/** @brief [ServerListModelTest] 종류가 상한보다 길면 보내지 않고 kInvalidRequest 로 끝난다 */
SW_TEST_CASE( ServerListModelTest, OversizedKindIsRejected )
{
    OnlineServiceClient   serviceClient;
    ServerDirectoryClient directoryClient;
    directoryClient.initialize( &serviceClient );
    ServerListModel model;
    model.initialize( &directoryClient );

    SW_EXPECT_LOG( "OnlineProtocol.MessageOutOfRange" ); // 쓰지 못한 메시지는 오류 로그를 남긴다
    const string longKind( static_cast<size_t>( ServerRecord::kMaxNameSize ) + 1, 'k' );
    SW_EXPECT_FALSE( model.refresh( longKind ) );
    SW_EXPECT_TRUE( model.getState() == ServerListState::Failed );
    SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, model.getErrorCode() );
    SW_EXPECT_TRUE( model.getEntries().empty() );
}
