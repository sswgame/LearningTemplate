// LiveOpsTypesTest — 라이브옵스 공유 글자 규칙: 푸시 제공자 id(`[0-9a-z_]`)와 이벤트 키(`[0-9a-z_.]`)의 글자 · 길이 경계.
// 두 규칙은 '.' 하나가 다르다(키만 점 계층을 쓴다).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Runtime/LiveOpsTypes.h"

using namespace sw;

/** @brief [LiveOpsTypesTest] 제공자 id 는 `[0-9a-z_]` 1..상한, 키는 `[0-9a-z_.]` 1..받은 상한이다 */
SW_TEST_CASE( LiveOpsTypesTest, ProviderIDAndKeyAcceptOnlyTheirAlphabetAndLength )
{
    // 틀리면: 두 규칙의 '.' 처리가 바뀌면 '.' 단언이, 길이 · 빈 글 · 대문자 검사 중 하나를 빼면 해당 거짓 단언이 진다.
    SW_EXPECT_TRUE( PushLimit::isValidProviderID( "fcm_v1" ) );
    SW_EXPECT_TRUE( PushLimit::isValidProviderID( string( static_cast<size_t>( PushLimit::kMaxProviderIDSize ), 'p' ) ) );
    SW_EXPECT_FALSE( PushLimit::isValidProviderID( string( static_cast<size_t>( PushLimit::kMaxProviderIDSize ) + 1, 'p' ) ) );
    SW_EXPECT_FALSE( PushLimit::isValidProviderID( "fcm.v1" ) );
    SW_EXPECT_FALSE( PushLimit::isValidProviderID( "FCM" ) );
    SW_EXPECT_FALSE( PushLimit::isValidProviderID( "" ) );

    SW_EXPECT_TRUE( LiveOpsLimit::isValidKey( "event.summer_2", LiveOpsLimit::kMaxIDSize ) );
    SW_EXPECT_TRUE( LiveOpsLimit::isValidKey( string( 4, 'k' ), 4 ) );
    SW_EXPECT_FALSE( LiveOpsLimit::isValidKey( string( 5, 'k' ), 4 ) );
    SW_EXPECT_FALSE( LiveOpsLimit::isValidKey( "event/summer", LiveOpsLimit::kMaxIDSize ) );
    SW_EXPECT_FALSE( LiveOpsLimit::isValidKey( "Event", LiveOpsLimit::kMaxIDSize ) );
    SW_EXPECT_FALSE( LiveOpsLimit::isValidKey( "", LiveOpsLimit::kMaxIDSize ) );
}
