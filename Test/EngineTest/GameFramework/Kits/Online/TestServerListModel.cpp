// 서버 목록 모델 — 클라이언트가 없거나 연결이 준비되지 않았을 때 낡은 목록을 남기지 않고 Failed 로 끝나는 길, 묻는 중에는 다시 묻지 않는 길.
// 화면(ServerPickerScreen)은 UI 시스템이 있어야 열려 EngineTest 의 UI 시나리오 쪽에서 본다.
#include "pch.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Shared/ServerListModel.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

/**
 * @brief [ServerListModelTest] 클라이언트 없이 묻거나 준비되지 않은 클라이언트로 물으면 Failed 로 끝나고 목록은 비어 있으며, 상태가 바뀔 때마다 판이 올라간다
 */
SW_TEST_CASE( ServerListModelTest, WithoutAReadyClientTheListFailsInsteadOfStayingStale )
{
    ServerListModel model;
    SW_EXPECT_TRUE( model.getState() == ServerListState::Idle );
    SW_EXPECT_TRUE( model.getEntries().empty() );

    const uint64 before = model.getRevision();
    SW_EXPECT_FALSE( model.refresh( "game" ) ); // 클라이언트가 없다
    SW_EXPECT_TRUE( model.getState() == ServerListState::Failed );
    SW_EXPECT_TRUE( model.getErrorCode() == OnlineError::kUnavailable );
    SW_EXPECT_TRUE( model.getRevision() > before );

    // 서비스 클라이언트가 초기화되지 않은 클라이언트: 요청은 보내지지 않고 이 호출 안에서 Unavailable 로 끝난다.
    OnlineServiceClient   service;
    ServerDirectoryClient directory;
    directory.initialize( &service );
    SW_ASSERT_TRUE( service.registerClientService( &directory ) );
    model.initialize( &directory );
    SW_EXPECT_FALSE( model.refresh( "game" ) );
    SW_EXPECT_TRUE( model.getState() == ServerListState::Failed );
    SW_EXPECT_TRUE( model.getEntries().empty() );
}
