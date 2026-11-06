// SpriteLibraryTest — 라이브러리(`.spritelib.xml`) 읽기 · 차이 파일 · 오류, 클립 프레임의 라벨 칸(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Animation/Sprite/SpriteLibrary.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"

using namespace sw;

namespace
{
    constexpr string_view kBaseLibrary = R"(<SpriteLibrary _atlasPath="engine/textures/test/quadrants.dds" _atlasPixelSize="64,64">
        <_listCategory>
            <SpriteLibraryCategory _name="Body">
                <_listLabel>
                    <SpriteLibraryLabel _name="Idle" _uvRect="0,0,0.5,0.5" />
                    <SpriteLibraryLabel _name="Run" _uvRect="0.5,0,0.5,0.5" _borderPixels="4,0,0,0" />
                </_listLabel>
            </SpriteLibraryCategory>
        </_listCategory>
    </SpriteLibrary>)";
} // namespace

/** @brief [SpriteLibraryTest] 분류 · 라벨로 프레임을 찾고, 픽셀 테두리를 프레임 크기의 비율로 바꾼다. 없는 이름은 nullptr 이다 */
SW_TEST_CASE( SpriteLibraryTest, FindsLabelsAndResolvesBorders )
{
    SpriteLibrary library;
    SW_REQUIRE_TRUE( library.parseXML( kBaseLibrary, "<test>" ) );
    const SpriteLibraryLabel* pRun = library.findLabel( hashed_string( "Body" ), hashed_string( "Run" ) );
    SW_REQUIRE_TRUE( pRun != nullptr );
    SW_EXPECT_NEAR( 0.125f, pRun->_border._x, 1e-6f ); // 4 픽셀 / (0.5 × 64)
    SW_EXPECT_TRUE( library.findLabel( hashed_string( "Body" ), hashed_string( "Jump" ) ) == nullptr );
    SW_EXPECT_TRUE( library.findLabel( hashed_string( "Head" ), hashed_string( "Idle" ) ) == nullptr );
}

/** @brief [SpriteLibraryTest] 차이 파일 — 라벨 하나만 바꾸고 나머지는 물려받으며, 분류 · 라벨을 더하고 뺄 수 있다 */
SW_TEST_CASE( SpriteLibraryTest, ChildChangesOnlyWhatItNames )
{
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "base.spritelib.xml" ), string{ kBaseLibrary } ) );
    const string baseID = test::ScopedScratchDomain::makeID( "base.spritelib.xml" ).str();

    const string  text = "<SpriteLibrary _base=\"" + baseID + "\"><_listCategory><SpriteLibraryCategory _name=\"Body\"><_listLabel>"
                                                              "<SpriteLibraryLabel _name=\"Idle\" _uvRect=\"0,0.5,0.5,0.5\" /><SpriteLibraryLabel _name=\"Run\" _op=\"Remove\" /></_listLabel></SpriteLibraryCategory>"
                                                              "<SpriteLibraryCategory _name=\"Head\"><_listLabel><SpriteLibraryLabel _name=\"Hat\" /></_listLabel></SpriteLibraryCategory></_listCategory></SpriteLibrary>";
    SpriteLibrary library;
    SW_REQUIRE_TRUE( library.parseXML( text, "<test>" ) );
    SW_EXPECT_EQUAL( string( "engine/textures/test/quadrants.dds" ), library._atlasPath ); // 물려받음
    const SpriteLibraryLabel* pIdle = library.findLabel( hashed_string( "Body" ), hashed_string( "Idle" ) );
    SW_REQUIRE_TRUE( pIdle != nullptr );
    SW_EXPECT_NEAR( 0.5f, pIdle->_uvRect._y, 1e-6f );
    SW_EXPECT_TRUE( library.findLabel( hashed_string( "Body" ), hashed_string( "Run" ) ) == nullptr );
    SW_EXPECT_TRUE( library.findLabel( hashed_string( "Head" ), hashed_string( "Hat" ) ) != nullptr );
}

/** @brief [SpriteLibraryTest] 틀린 파일은 내용을 비운다 — 아틀라스 없음 · 겹친 라벨 · 크기 없는 테두리 · 모르는 칸 */
SW_TEST_CASE( SpriteLibraryTest, RejectsBrokenFiles )
{
    SpriteLibrary library;
    SW_EXPECT_LOG( "SpriteLibrary.NoAtlas" );
    SW_EXPECT_FALSE( library.parseXML( R"(<SpriteLibrary />)", "<test>" ) );
    SW_EXPECT_LOG( "SpriteLibrary.LabelRepeated" );
    SW_EXPECT_FALSE( library.parseXML( R"(<SpriteLibrary _atlasPath="a.dds"><_listCategory><SpriteLibraryCategory _name="B"><_listLabel><SpriteLibraryLabel _name="X" /><SpriteLibraryLabel _name="X" /></_listLabel></SpriteLibraryCategory></_listCategory></SpriteLibrary>)", "<test>" ) );
    SW_EXPECT_LOG( "SpriteLibrary.BorderNeedsAtlasSize" );
    SW_EXPECT_FALSE( library.parseXML( R"(<SpriteLibrary _atlasPath="a.dds"><_listCategory><SpriteLibraryCategory _name="B"><_listLabel><SpriteLibraryLabel _name="X" _borderPixels="1,0,0,0" /></_listLabel></SpriteLibraryCategory></_listCategory></SpriteLibrary>)", "<test>" ) );
    SW_EXPECT_LOG( "Config.KeyUnknown" );
    SW_EXPECT_FALSE( library.parseXML( R"(<SpriteLibrary _atlasPath="a.dds" _colour="red" />)", "<test>" ) );
    SW_EXPECT_TRUE( library._listCategory.empty() );
}

/** @brief [SpriteLibraryTest] 클립 프레임의 라벨은 왕복하고 라벨을 쓰지 않는 클립의 글은 그대로이며, 분류 없는 라벨은 읽지 않는다 */
SW_TEST_CASE( SpriteLibraryTest, ClipFrameLabelsRoundTrip )
{
    SpriteClipAsset clip;
    SW_REQUIRE_TRUE( clip.parseXML( R"(<SpriteClipAsset><_listFrame><SpriteClipFrame _category="Body" _label="Idle" _durationMs="50" /><SpriteClipFrame _uvRect="0,0,1,1" /></_listFrame></SpriteClipAsset>)" ) );
    SW_EXPECT_TRUE( clip.usesLibrary() );
    SW_EXPECT_TRUE( clip._listFrame[0].usesLibrary() );
    SW_EXPECT_FALSE( clip._listFrame[1].usesLibrary() );
    const string    written = clip.toXML();
    SpriteClipAsset reread;
    SW_REQUIRE_TRUE( reread.parseXML( written ) );
    SW_EXPECT_TRUE( reread._listFrame[0]._label == hashed_string( "Idle" ) );
    SW_EXPECT_EQUAL( int32{ 50 }, reread._listFrame[0]._durationMs );

    SpriteClipAsset plain;
    plain._listFrame.resize( 1 );
    SW_EXPECT_FALSE( plain.usesLibrary() );
    SW_EXPECT_TRUE( plain.toXML().find( "_label" ) == string::npos );

    SW_EXPECT_LOG( "SpriteClip.SpriteClipLabelNeedsCategory" );
    SW_EXPECT_FALSE( clip.parseXML( R"(<SpriteClipAsset><_listFrame><SpriteClipFrame _label="Idle" /></_listFrame></SpriteClipAsset>)" ) );
}
