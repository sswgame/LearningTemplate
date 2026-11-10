#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Client/Automation/Runner/AutomationGoldenUtil.h"
#include "DevTools/Client/Automation/Runner/AutomationImage.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

using namespace sw;

namespace
{
    struct AutomationGoldenTestUtil
    {
        static constexpr AutomationImageThreshold kMeasure{ 12, 96 };
        static constexpr AutomationGoldenLimits   kLimits{ 0.01, 0.005, 16 };
        static constexpr float64                  kNearLimitRatio = 0.5;
        static constexpr string_view              kPolicyText =
            R"({ "_differingMinDistance": 12, "_deepMinDistance": 96, "_maxDifferingFraction": 0.01, "_maxMeanDistance": 0.005, "_maxDeepRegionPixels": 16, "_nearLimitRatio": 0.5 })";

        static AutomationImage makeImage( uint32 width, uint32 height, uint8 value )
        {
            AutomationImage image;
            image._width  = width;
            image._height = height;
            image._listRGB.assign( static_cast<size_t>( width ) * height * 3u, value );
            return image;
        }

        static GoldenResult judgeAgainst( const AutomationImage& actual, const AutomationImage& golden )
        {
            const expected<AutomationImageDifference> difference = AutomationImage::compare( actual, golden, kMeasure );
            SW_EXPECT_TRUE( difference.has_value() );
            return difference.has_value() ? AutomationGoldenUtil::judge( *difference, kLimits, kNearLimitRatio ) : GoldenResult::Fail;
        }
    };
    using Util = AutomationGoldenTestUtil;
} // namespace

/** @brief [AutomationGoldenTest] 넓고 얕은 차이(모든 화소가 1 씩 — 디더 · 반올림)는 통과하고, 작고 깊은 덩어리(전체의 0.3 % 인 마젠타 5×5)는 비율 문턱 아래여도 진다 */
SW_TEST_CASE( AutomationGoldenTest, WideShallowPassesNarrowDeepFails )
{
    const AutomationImage golden = Util::makeImage( 160, 60, 100 );

    AutomationImage shallow = golden;
    for ( size_t byteIndex = 0; byteIndex < shallow._listRGB.size(); ++byteIndex )
    {
        shallow._listRGB[byteIndex] = static_cast<uint8>( byteIndex % 3u == 0u ? 101 : 100 ); // 모든 화소가 빨강 1 차이 — 전부 다르지만 "다른 화소" 문턱 아래
    }
    SW_EXPECT_TRUE( Util::judgeAgainst( shallow, golden ) == GoldenResult::Pass );

    AutomationImage blob = golden;
    for ( uint32 y = 10; y < 15; ++y )
    {
        for ( uint32 x = 20; x < 25; ++x )
        {
            uint8* pPixel = &blob._listRGB[( static_cast<size_t>( y ) * blob._width + x ) * 3u];
            pPixel[0]     = 255;
            pPixel[1]     = 0;
            pPixel[2]     = 255;
        }
    }
    const expected<AutomationImageDifference> blobDifference = AutomationImage::compare( blob, golden, Util::kMeasure );
    SW_REQUIRE_TRUE( blobDifference.has_value() );
    SW_EXPECT_TRUE( blobDifference->_differingFraction <= Util::kLimits._maxDifferingFraction ); // 옛 한 식은 지나갔다
    SW_EXPECT_EQUAL( 25u, blobDifference->_largestDeepRegionPixels );
    SW_EXPECT_TRUE( AutomationGoldenUtil::judge( *blobDifference, Util::kLimits, Util::kNearLimitRatio ) == GoldenResult::Fail );
}

/** @brief [AutomationGoldenTest] 판정은 한 식이다 — 한 축이라도 상한을 넘으면 Fail, 한 축이라도 상한의 절반을 넘으면 PassNearLimit, 아니면 Pass */
SW_TEST_CASE( AutomationGoldenTest, VerdictPerAxis )
{
    const auto judge = []( float64 fraction, float64 mean, uint32 region )
    { return AutomationGoldenUtil::judge( AutomationImageDifference{ fraction, mean, region }, Util::kLimits, Util::kNearLimitRatio ); };
    SW_EXPECT_TRUE( judge( 0.0, 0.0, 0 ) == GoldenResult::Pass );
    SW_EXPECT_TRUE( judge( 0.005, 0.0025, 8 ) == GoldenResult::Pass );        // 절반 그대로는 근접이 아니다
    SW_EXPECT_TRUE( judge( 0.0099, 0.0, 0 ) == GoldenResult::PassNearLimit ); // 오늘의 0.99 %
    SW_EXPECT_TRUE( judge( 0.0, 0.004, 0 ) == GoldenResult::PassNearLimit );
    SW_EXPECT_TRUE( judge( 0.0, 0.0, 9 ) == GoldenResult::PassNearLimit );
    SW_EXPECT_TRUE( judge( 0.01, 0.005, 16 ) == GoldenResult::PassNearLimit ); // 상한 그대로는 통과
    SW_EXPECT_TRUE( judge( 0.0102, 0.0, 0 ) == GoldenResult::Fail );           // 다음 실행의 1.02 %
    SW_EXPECT_TRUE( judge( 0.0, 0.0051, 0 ) == GoldenResult::Fail );
    SW_EXPECT_TRUE( judge( 0.0, 0.0, 17 ) == GoldenResult::Fail );
}

/** @brief [AutomationGoldenTest] 기록 모드는 근접 · 실패 · 없는 · 크기가 다른 골든을 다시 쓰고 통과는 둔다. 일반 실행은 근접을 알리기만 하고 진 것만 실패다 */
SW_TEST_CASE( AutomationGoldenTest, RecordModeRewritesNearLimit )
{
    SW_EXPECT_TRUE( AutomationGoldenUtil::decide( true, true, GoldenResult::PassNearLimit, true ) == GoldenAction::Write );
    SW_EXPECT_TRUE( AutomationGoldenUtil::decide( true, true, GoldenResult::Fail, true ) == GoldenAction::Write );
    SW_EXPECT_TRUE( AutomationGoldenUtil::decide( false, false, GoldenResult::Fail, true ) == GoldenAction::Write );
    SW_EXPECT_TRUE( AutomationGoldenUtil::decide( true, false, GoldenResult::Fail, true ) == GoldenAction::Write );
    SW_EXPECT_TRUE( AutomationGoldenUtil::decide( true, true, GoldenResult::Pass, true ) == GoldenAction::Keep );

    SW_EXPECT_TRUE( AutomationGoldenUtil::decide( true, true, GoldenResult::Pass, false ) == GoldenAction::Keep );
    SW_EXPECT_TRUE( AutomationGoldenUtil::decide( true, true, GoldenResult::PassNearLimit, false ) == GoldenAction::KeepNearLimit );
    SW_EXPECT_TRUE( AutomationGoldenUtil::decide( true, true, GoldenResult::Fail, false ) == GoldenAction::Fail );
    SW_EXPECT_TRUE( AutomationGoldenUtil::decide( false, false, GoldenResult::Fail, false ) == GoldenAction::FailMissing );
    SW_EXPECT_TRUE( AutomationGoldenUtil::decide( true, false, GoldenResult::Fail, false ) == GoldenAction::FailUnreadable );

    SW_EXPECT_TRUE( string_view{ AutomationGoldenUtil::describeWriteReason( true, true, GoldenResult::PassNearLimit ) } == "near-limit" );
    SW_EXPECT_TRUE( string_view{ AutomationGoldenUtil::describeWriteReason( false, false, GoldenResult::Fail ) } == "missing" );
}

/** @brief [AutomationGoldenTest] 정책은 칸 여섯이 모두 있어야 하고 범위 안이어야 한다 — 기본값으로 떨어지지 않는다 */
SW_TEST_CASE( AutomationGoldenTest, PolicyNeedsEveryField )
{
    const expected<AutomationGoldenPolicy> policy = AutomationGoldenUtil::parsePolicy( Util::kPolicyText, "test" );
    SW_REQUIRE_TRUE( policy.has_value() );
    SW_EXPECT_EQUAL( 96u, policy->_measure._deepMinDistance );
    SW_EXPECT_EQUAL( 16u, policy->_limits._maxDeepRegionPixels );
    SW_EXPECT_NEAR( 0.5, policy->_nearLimitRatio, 0.0 );

    SW_EXPECT_LOG( "JSON.MissingMember" );
    SW_EXPECT_FALSE( AutomationGoldenUtil::parsePolicy( R"({ "_differingMinDistance": 12 })", "test" ).has_value() );
    const string outOfRange =
        R"({ "_differingMinDistance": 12, "_deepMinDistance": 8, "_maxDifferingFraction": 0.01, "_maxMeanDistance": 0.005, "_maxDeepRegionPixels": 16, "_nearLimitRatio": 0.5 })";
    SW_EXPECT_FALSE( AutomationGoldenUtil::parsePolicy( outOfRange, "test" ).has_value() );
}

/** @brief [AutomationGoldenTest] 저장소의 정책 파일이 읽힌다(시나리오의 이미지 단언이 모두 이것을 읽는다) */
SW_TEST_CASE( AutomationGoldenTest, ProjectPolicyLoads )
{
    const expected<AutomationGoldenPolicy> policy = AutomationGoldenUtil::loadPolicy();
    SW_EXPECT_TRUE_MSG( policy.has_value(), policy.has_value() ? "" : string( policy.error().message() ).c_str() );
}

/** @brief [AutomationGoldenTest] 골든 이름과 메타 이름, 메타 글이 JSON 으로 다시 읽힌다 */
SW_TEST_CASE( AutomationGoldenTest, NamesAndMeta )
{
    const string goldenID = AutomationGoldenUtil::makeGoldenID( "game/x/automation/golden", "game.x.view", "front", "dx12", ".linux" );
    SW_EXPECT_TRUE( goldenID == "game/x/automation/golden/game.x.view.front.dx12.linux.ppm" );
    SW_EXPECT_TRUE( AutomationGoldenUtil::makeMetaID( goldenID ) == "game/x/automation/golden/game.x.view.front.dx12.linux.golden.json" );

    AutomationGoldenMeta meta;
    meta._backendTag = "dx12";
    meta._platform   = "windows";
    meta._commit     = "0123456789abcdef-dirty";
    meta._reason     = "near-limit";
    meta._bMeasured  = true;
    meta._difference = AutomationImageDifference{ 0.0099, 0.001, 3 };
    meta._limits     = Util::kLimits;
    meta._width      = 160;
    meta._height     = 90;
    JSONDocument document;
    SW_REQUIRE_TRUE( document.parse( AutomationGoldenUtil::makeMetaText( meta ), "meta" ) );
    const JSONValue root = document.getRoot();
    SW_EXPECT_TRUE( root.readRequired<string>( "commit" ).value_or( string() ) == meta._commit );
    SW_EXPECT_TRUE( root.readRequired<string>( "reason" ).value_or( string() ) == "near-limit" );
    SW_EXPECT_EQUAL( 3u, root.get( "previousDifference" ).readRequired<uint32>( "largestDeepRegionPixels" ).value_or( 0u ) );
}

/** @brief [AutomationGoldenTest] 기록 커밋을 못 구하면 골든을 쓰지 않고 까닭을 말한다 — git 을 못 띄움(PATH) · 저장소 아님 · 빈 출력이 서로 다른 문장이다 */
SW_TEST_CASE( AutomationGoldenTest, RecordingCommitNeedsGit )
{
    const expected<string> notStarted = AutomationGoldenUtil::readRecordingCommit( -1, "" );
    SW_REQUIRE_FALSE( notStarted.has_value() );
    SW_EXPECT_TRUE( notStarted.error().message().find( "not on PATH" ) != string_view::npos );

    const expected<string> notRepository = AutomationGoldenUtil::readRecordingCommit( 128, "fatal: not a git repository" );
    SW_REQUIRE_FALSE( notRepository.has_value() );
    SW_EXPECT_TRUE( notRepository.error().message().find( "work tree" ) != string_view::npos );
    SW_EXPECT_TRUE( notRepository.error().detail().find( "not a git repository" ) != string_view::npos );

    SW_EXPECT_FALSE( AutomationGoldenUtil::readRecordingCommit( 0, "" ).has_value() );
    SW_EXPECT_TRUE( AutomationGoldenUtil::readRecordingCommit( 0, "0123abcd-dirty" ).value_or( string() ) == "0123abcd-dirty" );
}
