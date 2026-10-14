#include "pch.h"

#include "Editor/Common/Widgets/EditorAssetFilter.h"
#include "Editor/Viewport/EditorViewportMouseClaim.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorViewportMouseClaimTest] 가져가면 잡힌 상태, 소유자가 풀면 풀린다
 */
SW_TEST_CASE( EditorViewportMouseClaimTest, AcquireAndRelease )
{
    int32 ownerA = 0;
    EditorViewportMouseClaim::release( &ownerA );
    SW_EXPECT_FALSE( EditorViewportMouseClaim::isClaimed() );

    EditorViewportMouseClaim::acquire( &ownerA );
    SW_EXPECT_TRUE( EditorViewportMouseClaim::isClaimed() );
    SW_EXPECT_TRUE( EditorViewportMouseClaim::isClaimedBy( &ownerA ) );

    EditorViewportMouseClaim::release( &ownerA );
    SW_EXPECT_FALSE( EditorViewportMouseClaim::isClaimed() );
    SW_EXPECT_FALSE( EditorViewportMouseClaim::isClaimedBy( &ownerA ) );
}

/**
 * @brief [EditorViewportMouseClaimTest] 새 소유자가 가져가면 이전 소유자는 잃고, 이전 소유자의 release 는 아무것도 풀지 않는다
 */
SW_TEST_CASE( EditorViewportMouseClaimTest, NewOwnerReplacesOld )
{
    int32 ownerA = 0;
    int32 ownerB = 0;
    EditorViewportMouseClaim::acquire( &ownerA );
    EditorViewportMouseClaim::acquire( &ownerB );
    SW_EXPECT_FALSE( EditorViewportMouseClaim::isClaimedBy( &ownerA ) );
    SW_EXPECT_TRUE( EditorViewportMouseClaim::isClaimedBy( &ownerB ) );

    EditorViewportMouseClaim::release( &ownerA );
    SW_EXPECT_TRUE( EditorViewportMouseClaim::isClaimedBy( &ownerB ) );

    EditorViewportMouseClaim::release( &ownerB );
    SW_EXPECT_FALSE( EditorViewportMouseClaim::isClaimed() );
    SW_EXPECT_FALSE( EditorViewportMouseClaim::isClaimedBy( nullptr ) );
}

/**
 * @brief [EditorViewportMouseClaimTest] 매 프레임 다시 잡는 소유자는 유지되고, 한 프레임 건너뛰면(그리기 함수가 안 돈 경우) 다음 프레임 시작에 거둬진다
 */
SW_TEST_CASE( EditorViewportMouseClaimTest, UnrenewedClaimExpiresAtNextFrame )
{
    int32 owner = 0;
    EditorViewportMouseClaim::beginFrame( 100 );
    EditorViewportMouseClaim::acquire( &owner ); // 프레임 100 의 그리기에서 잡았다
    SW_EXPECT_TRUE( EditorViewportMouseClaim::isClaimedBy( &owner ) );

    EditorViewportMouseClaim::beginFrame( 101 ); // 지난 프레임에 잡았으니 이번 프레임의 그리기가 오기 전까지는 유지된다
    SW_EXPECT_TRUE( EditorViewportMouseClaim::isClaimedBy( &owner ) );
    EditorViewportMouseClaim::acquire( &owner ); // 끄는 중이라 다시 잡는다

    EditorViewportMouseClaim::beginFrame( 102 );
    SW_EXPECT_TRUE( EditorViewportMouseClaim::isClaimedBy( &owner ) );
    // 프레임 102 에 시각화 토글을 껐다 - acquire 도 release 도 오지 않는다
    EditorViewportMouseClaim::beginFrame( 103 );
    SW_EXPECT_FALSE( EditorViewportMouseClaim::isClaimed() );
}

/**
 * @brief [EditorViewportMouseClaimTest] 같은 프레임 번호로 여러 뷰포트가 beginFrame 을 불러도 한 번만 센다
 */
SW_TEST_CASE( EditorViewportMouseClaimTest, SameFrameNumberCountsOnce )
{
    int32 owner = 0;
    EditorViewportMouseClaim::beginFrame( 200 );
    EditorViewportMouseClaim::acquire( &owner );
    EditorViewportMouseClaim::beginFrame( 201 ); // 첫 번째 뷰포트
    EditorViewportMouseClaim::acquire( &owner );
    EditorViewportMouseClaim::beginFrame( 201 ); // 두 번째 뷰포트 - 같은 프레임이라 무시된다
    SW_EXPECT_TRUE( EditorViewportMouseClaim::isClaimedBy( &owner ) );
    EditorViewportMouseClaim::beginFrame( 202 );
    SW_EXPECT_TRUE( EditorViewportMouseClaim::isClaimedBy( &owner ) ); // 201 에 잡았다
    EditorViewportMouseClaim::release( &owner );
    SW_EXPECT_FALSE( EditorViewportMouseClaim::isClaimed() );
}

/**
 * @brief [EditorAssetFilterTest] 두 단계 접미사 필터(.modifiers.json)도 맞춘다
 */
SW_TEST_CASE( EditorAssetFilterTest, MatchesCompoundSuffix )
{
    SW_EXPECT_TRUE( EditorAssetFilter::matches( "game/models/hull.modifiers.json", ".modifiers.json" ) );
    SW_EXPECT_TRUE( EditorAssetFilter::matches( "game/models/hull.modifiers.json", ".png, .modifiers.json" ) );
    SW_EXPECT_FALSE( EditorAssetFilter::matches( "game/models/hull.json", ".modifiers.json" ) );
}

/**
 * @brief [EditorAssetFilterTest] 쉼표 · 공백으로 나누고 빈 조각은 버린다
 */
SW_TEST_CASE( EditorAssetFilterTest, SplitsOnCommaAndSpace )
{
    const vector<string_view> listExtension = EditorAssetFilter::splitExtensions( " .png, .jpg  .tga,," );
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), listExtension.size() );
    if ( listExtension.size() == 3 )
    {
        SW_EXPECT_TRUE( listExtension[0] == ".png" );
        SW_EXPECT_TRUE( listExtension[1] == ".jpg" );
        SW_EXPECT_TRUE( listExtension[2] == ".tga" );
    }
    SW_EXPECT_TRUE( EditorAssetFilter::splitExtensions( "" ).empty() );
}

/**
 * @brief [EditorAssetFilterTest] 여러 확장자 중 하나와 맞으면 받고, 한 개짜리 필터는 예전처럼 동작하며, 빈 필터는 모두 받는다
 */
SW_TEST_CASE( EditorAssetFilterTest, MatchesAnyListedExtension )
{
    constexpr string_view kTextureFilter = ".png,.jpg,.jpeg,.tga,.dds,.hdr,.bmp";
    SW_EXPECT_TRUE( EditorAssetFilter::matches( "game/textures/a.png", kTextureFilter ) );
    SW_EXPECT_TRUE( EditorAssetFilter::matches( "game/textures/b.DDS", kTextureFilter ) );
    SW_EXPECT_FALSE( EditorAssetFilter::matches( "game/models/c.mesh", kTextureFilter ) );

    SW_EXPECT_TRUE( EditorAssetFilter::matches( "shaders/x.hlsl", ".hlsl" ) );
    SW_EXPECT_FALSE( EditorAssetFilter::matches( "shaders/x.hlsli", ".hlsl" ) );

    SW_EXPECT_TRUE( EditorAssetFilter::matches( "any/file.bin", "" ) );
    SW_EXPECT_TRUE( EditorAssetFilter::matches( "any/file.bin", " , " ) );
}
