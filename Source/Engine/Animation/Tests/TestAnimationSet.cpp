// AnimationSet — 이름 → 클립 표: 바탕 셋 위 차이, 다른 셋의 클립 빌리기, 잘못된 줄 거절, 왕복.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/AnimationSet.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"

using namespace sw;

namespace
{
    struct AnimationSetTestUtil
    {
        /** @brief 표에서 이름의 경로를 읽습니다. 없으면 빈 글입니다. */
        static string findPath( const AnimationSet& animationSet, const utf8* pName )
        {
            const string* const pPath = animationSet.findClipPath( hashed_string( pName ) );
            return pPath != nullptr ? *pPath : string{};
        }

        /** @brief 공유 파일 두 줄(idle · walk)인 셋 파일을 씁니다. 리소스 경로를 돌려줍니다. */
        static string writeBaseSet( const test::ScopedScratchDomain& domain, const utf8* pFileName )
        {
            AnimationSet animationSet;
            animationSet.setClip( "Idle", "common/models/_shared/clips/aaaa.animclip" );
            animationSet.setClip( "walk", "common/models/_shared/clips/bbbb.animclip" );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( pFileName ), animationSet.saveToString() ) );
            return test::ScopedScratchDomain::makeID( pFileName ).str();
        }
    };
} // namespace

/**
 * @brief [AnimationSetTest] 표는 저장 뒤 읽어도 같고 이름은 대소문자를 가리지 않는다(애니메이터는 상태 이름을 그대로 넘긴다)
 */
SW_TEST_CASE( AnimationSetTest, RoundTripsAndIgnoresNameCase )
{
    AnimationSet written;
    written.setClip( "Walk", "common/models/_shared/clips/bbbb.animclip" );
    written.setClip( "idle", "common/models/_shared/clips/aaaa.animclip" );
    const string text = written.saveToString();

    AnimationSet read;
    SW_REQUIRE_TRUE( read.parseXML( text, "<test>" ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, read.getEntries().size() );
    SW_EXPECT_EQUAL( string( "idle" ), read.getEntries()[0]._name ); // 이름순
    SW_EXPECT_EQUAL( string( "common/models/_shared/clips/bbbb.animclip" ), AnimationSetTestUtil::findPath( read, "WALK" ) );
    SW_EXPECT_TRUE( read.findClipPath( hashed_string( "run" ) ) == nullptr );
    SW_EXPECT_EQUAL( text, read.saveToString() ); // 같은 표는 같은 바이트(스탬프의 결과 해시)
}

/**
 * @brief [AnimationSetTest] 바탕 셋 위에 차이만 적는다 — 같은 이름은 덮고, 다른 셋의 클립은 셋과 이름으로 빌린다(손으로 쓰는 셋은 내용 키 경로를 적지 않는다)
 */
SW_TEST_CASE( AnimationSetTest, BaseAndBorrowedClipsFlattenIntoOneTable )
{
    const test::ScopedScratchDomain domain;
    const string                    base = AnimationSetTestUtil::writeBaseSet( domain, "base.animset.xml" );
    AnimationSet                    other;
    other.setClip( "farm_harvest", "common/models/_shared/clips/cccc.animclip" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "other.animset.xml" ), other.saveToString() ) );
    const string otherID = test::ScopedScratchDomain::makeID( "other.animset.xml" ).str();

    const string text = "<AnimationSet formatVersion=\"0\" base=\"" + base + "\">" +                                     //
                        "<clip name=\"walk\" path=\"common/models/_shared/clips/dddd.animclip\" />" +                    // 차이
                        "<clip name=\"farm_harvest\" set=\"" + otherID + "\" />" +                                       // 같은 이름을 빌린다
                        "<clip name=\"harvest\" set=\"" + otherID + "\" source=\"farm_harvest\" />" + "</AnimationSet>"; // 다른 이름으로 빌린다
    AnimationSet animationSet;
    SW_REQUIRE_TRUE( animationSet.parseXML( text, "<test>" ) );
    SW_EXPECT_EQUAL( string( "common/models/_shared/clips/aaaa.animclip" ), AnimationSetTestUtil::findPath( animationSet, "idle" ) ); // 바탕 그대로
    SW_EXPECT_EQUAL( string( "common/models/_shared/clips/dddd.animclip" ), AnimationSetTestUtil::findPath( animationSet, "walk" ) );
    SW_EXPECT_EQUAL( string( "common/models/_shared/clips/cccc.animclip" ), AnimationSetTestUtil::findPath( animationSet, "farm_harvest" ) );
    SW_EXPECT_EQUAL( string( "common/models/_shared/clips/cccc.animclip" ), AnimationSetTestUtil::findPath( animationSet, "harvest" ) );
}

/**
 * @brief [AnimationSetTest] 잘못된 줄은 그 줄을 빼고 읽지 않고 셋을 비운다 — 경로와 셋 둘 다 · 둘 다 없음 · 같은 이름 둘 · 이름의 경로 · 없는 클립 · 모르는 속성
 */
SW_TEST_CASE( AnimationSetTest, RejectsBrokenLines )
{
    AnimationSet animationSet;
    SW_EXPECT_LOG( "AnimationSet.ClipNeedsOneSource" );
    SW_EXPECT_FALSE( animationSet.parseXML( R"(<AnimationSet formatVersion="0"><clip name="a" path="x" set="y"/></AnimationSet>)", "<test>" ) );
    SW_EXPECT_FALSE( animationSet.parseXML( R"(<AnimationSet formatVersion="0"><clip name="a"/></AnimationSet>)", "<test>" ) );
    SW_EXPECT_LOG( "AnimationSet.DuplicateClip" );
    SW_EXPECT_FALSE( animationSet.parseXML( R"(<AnimationSet formatVersion="0"><clip name="a" path="x"/><clip name="A" path="y"/></AnimationSet>)", "<test>" ) );
    SW_EXPECT_LOG( "AnimationSet.NameHasPath" );
    SW_EXPECT_FALSE( animationSet.parseXML( R"(<AnimationSet formatVersion="0"><clip name="a/b" path="x"/></AnimationSet>)", "<test>" ) );
    SW_EXPECT_LOG( "XMLFields.UnknownField" );
    SW_EXPECT_FALSE( animationSet.parseXML( R"(<AnimationSet formatVersion="0"><clip name="a" path="x" speed="2"/></AnimationSet>)", "<test>" ) );
    SW_EXPECT_TRUE( animationSet.getEntries().empty() );

    const test::ScopedScratchDomain domain;
    const string                    base = AnimationSetTestUtil::writeBaseSet( domain, "base.animset.xml" );
    SW_EXPECT_LOG( "AnimationSet.ClipNotInSet" );
    SW_EXPECT_FALSE( animationSet.parseXML( "<AnimationSet formatVersion=\"0\"><clip name=\"run\" set=\"" + base + "\"/></AnimationSet>", "<test>" ) );
}

/**
 * @brief [AnimationSetTest] 바탕 사슬이 돌면 깊이 상한에서 오류로 멈춘다
 */
SW_TEST_CASE( AnimationSetTest, BaseLoopStopsAtDepthLimit )
{
    const test::ScopedScratchDomain domain;
    const string                    self = test::ScopedScratchDomain::makeID( "loop.animset.xml" ).str();
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "loop.animset.xml" ), "<AnimationSet formatVersion=\"0\" base=\"" + self + "\"/>" ) );
    AnimationSet animationSet;
    SW_EXPECT_LOG( "AnimationSet.ChainTooDeep" );
    SW_EXPECT_FALSE( animationSet.loadFromAsset( test::ScopedScratchDomain::makeID( "loop.animset.xml" ) ) );
}
