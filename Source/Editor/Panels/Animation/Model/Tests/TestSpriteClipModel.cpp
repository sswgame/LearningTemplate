#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Animation/Model/SpriteClipModel.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct SpriteClipModelTestUtil
    {
        /** @brief 프레임이 @p frameCount 개가 될 때까지 더합니다(처음 빈 프레임을 포함해 센다). */
        static void fillFrames( SpriteClipModel& model, int32 frameCount )
        {
            while ( static_cast<int32>( model.getFrames().size() ) < frameCount )
            {
                model.addFrame();
            }
        }

        /** @brief 프레임 시간을 모두 @p durationMs 로 둡니다. */
        static void setFrameDuration( SpriteClipModel& model, int32 durationMs )
        {
            for ( int32 frameIndex = 0; frameIndex < static_cast<int32>( model.getFrames().size() ); ++frameIndex )
            {
                model.selectFrame( frameIndex );
                model.editSelectedFrame()->_durationMs = durationMs;
            }
        }
    };
    using Util = SpriteClipModelTestUtil;
} // namespace

// ---------------------------------------------------------------------- 파일에 있는 칸은 편집기를 지나도 남는다

// 예전에는 칸을 따로 옮겨 담아 `_atlasPixelSize` 가 빠졌다 - 에디터로 저장하면 9-슬라이스 테두리가 게임에서 사라졌다.
SW_TEST_CASE( SpriteClipModelTest, UndoTextKeepsAtlasSizeAndBorders )
{
    SpriteClipModel model;
    model.editAtlasPixelSize() = float2{ 256.0f, 128.0f };
    SW_REQUIRE_TRUE( model.commitAtlasPixelSize() );
    model.selectFrame( 0 );
    model.editSelectedFrame()->_borderPixels = float4{ 4.0f, 4.0f, 4.0f, 4.0f };
    SW_REQUIRE_TRUE( model.commitSelectedFrameBorder() );

    SpriteClipAsset readBack;
    SW_REQUIRE_TRUE( readBack.loadFromXMLText( model.captureText() ) );
    SW_EXPECT_NEAR( 256.0f, readBack._atlasPixelSize._x, 1e-3f );
    SW_REQUIRE_EQUAL( 1, readBack.getFrameCount() );
    SW_EXPECT_TRUE( readBack._listFrame[0].hasBorder() );

    SpriteClipModel restored;
    restored.applyText( model.captureText() );
    SW_EXPECT_NEAR( 128.0f, restored.editAtlasPixelSize()._y, 1e-3f );
    SW_EXPECT_NEAR( 4.0f, restored.getFrames()[0]._borderPixels._x, 1e-3f );
}

// 구간의 알림은 편집 칸이 없어도 저장에서 빠지지 않는다.
SW_TEST_CASE( SpriteClipModelTest, UndoTextKeepsNotifies )
{
    SpriteClipAsset clip;
    clip._listFrame.push_back( SpriteClipFrame{} );
    SpriteClipAnimation animation{};
    animation._name       = "run";
    animation._frameCount = 1;
    AnimationNotifyEvent notify{};
    notify._name = hashed_string( "step" );
    animation._listNotify.push_back( notify );
    clip._listAnimation.push_back( animation );

    SpriteClipModel model;
    model.applyText( clip.toXML() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, model.getAnimations().size() );
    SW_EXPECT_EQUAL( size_t{ 1 }, model.getAnimations()[0]._listNotify.size() );
}

// 아틀라스 없는 문서로 되돌리면 칸도 빈다 - 비었을 때 그대로 두면 앞 문서의 아틀라스가 이 문서에 저장됐다.
SW_TEST_CASE( SpriteClipModelTest, ClipWithoutAtlasClearsTheField )
{
    SpriteClipModel model;
    model.editAtlasPath()  = "art/first.dds";
    const string withAtlas = model.captureText();
    model.editAtlasPath().clear();
    const string withoutAtlas = model.captureText();
    model.applyText( withAtlas );
    SW_EXPECT_TRUE( model.hasAtlasPath() );
    model.applyText( withoutAtlas );
    SW_EXPECT_FALSE( model.hasAtlasPath() );
}

// 되돌리기가 고른 항목을 맨 앞으로 옮기지 않는다.
SW_TEST_CASE( SpriteClipModelTest, UndoKeepsTheSelectionInRange )
{
    SpriteClipModel model;
    Util::fillFrames( model, 3 );
    const string threeFrames = model.captureText();
    model.selectFrame( 2 );
    model.applyText( threeFrames );
    SW_EXPECT_EQUAL( 2, model.getSelectedFrame() );

    SW_REQUIRE_TRUE( model.removeSelectedFrame() );
    SW_REQUIRE_TRUE( model.removeSelectedFrame() );
    const string oneFrame = model.captureText();
    model.applyText( threeFrames );
    model.selectFrame( 2 );
    model.applyText( oneFrame );
    SW_EXPECT_EQUAL( 0, model.getSelectedFrame() );
}

// ---------------------------------------------------------------------- 문서 열기 · 저장

// 없는 파일은 새 문서다 - 앞 문서의 프레임이 남으면 저장할 때 새 파일에 쓰였고 "저장 안 함" 도 편집을 버리지 못했다.
SW_TEST_CASE( SpriteClipModelTest, MissingFileStartsANewClip )
{
    SpriteClipModel model;
    Util::fillFrames( model, 3 );
    SW_REQUIRE_TRUE( model.addAnimation() );
    const string missingPath = test::makeScratchPath( "SwSpriteClipModelMissing.sprite.xml" );
    SW_EXPECT_TRUE( model.loadDocument( missingPath ) == ToolAssetLoadResult::Missing );
    SW_EXPECT_EQUAL( size_t{ 1 }, model.getFrames().size() );
    SW_EXPECT_TRUE( model.getAnimations().empty() );
    SW_EXPECT_EQUAL( -1, model.getSelectedAnimation() );
}

SW_TEST_CASE( SpriteClipModelTest, SaveAndLoadRoundTrip )
{
    const string path = test::makeScratchPath( "SwSpriteClipModelRoundTrip.sprite.xml" );
    {
        SpriteClipModel model;
        Util::fillFrames( model, 2 );
        SW_REQUIRE_TRUE( model.addAnimation() );
        model.editAtlasPixelSize() = float2{ 64.0f, 64.0f };
        SW_REQUIRE_TRUE( model.commitAtlasPixelSize() );
        SW_REQUIRE_TRUE( model.saveDocument( path ) );
        SW_EXPECT_FALSE( model.getStatus().empty() );
    }
    SpriteClipModel model;
    SW_EXPECT_TRUE( model.loadDocument( path ) == ToolAssetLoadResult::Loaded );
    SW_EXPECT_EQUAL( size_t{ 2 }, model.getFrames().size() );
    SW_EXPECT_EQUAL( 0, model.getSelectedAnimation() );
    SW_EXPECT_NEAR( 64.0f, model.editAtlasPixelSize()._x, 1e-3f );
    (void)FileUtil::removeFile( path );
}

// 포커스한 이미지로 연 문서의 경로는 이미지다 - 그대로 저장하면 이미지를 클립 글로 덮어썼다.
SW_TEST_CASE( SpriteClipModelTest, SavingNeverOverwritesAnImage )
{
    SW_EXPECT_TRUE( SpriteClipModel::computeClipPath( "art/hero.dds" ) == "art/hero.sprite.xml" );
    SW_EXPECT_TRUE( SpriteClipModel::computeClipPath( "art/hero.sprite.xml" ) == "art/hero.sprite.xml" );
    SW_EXPECT_TRUE( SpriteClipModel::computeClipPath( "" ).empty() );

    SpriteClipModel model;
    SW_EXPECT_FALSE( model.saveDocument( "art/hero.dds" ) );
    SW_EXPECT_FALSE( model.getStatus().empty() );
}

// ---------------------------------------------------------------------- 프레임

// 프레임을 지워도 구간은 같은 프레임을 가리킨다 - 예전에는 번호가 그대로라 다른 프레임을 돌렸고 파일에 프레임 밖 구간이 남았다.
SW_TEST_CASE( SpriteClipModelTest, RemovingAFrameShiftsAnimationRanges )
{
    SpriteClipAsset clip;
    clip._listFrame.resize( 6 );
    SpriteClipAnimation late{};
    late._name       = "late";
    late._firstFrame = 3;
    late._frameCount = 2;
    clip._listAnimation.push_back( late );
    SpriteClipAnimation single{};
    single._name       = "single";
    single._firstFrame = 1;
    single._frameCount = 1;
    clip._listAnimation.push_back( single );

    SpriteClipModel model;
    model.applyText( clip.toXML() );
    model.selectFrame( 1 ); // 구간 single 의 하나뿐인 프레임이고 late 보다 앞이다
    SW_REQUIRE_TRUE( model.removeSelectedFrame() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, model.getAnimations().size() ); // 읽기가 버릴 빈 구간을 남기지 않는다
    SW_EXPECT_EQUAL( 2, model.getAnimations()[0]._firstFrame );
    SW_EXPECT_EQUAL( 2, model.getAnimations()[0]._frameCount );

    model.selectFrame( 3 ); // 구간 안
    SW_REQUIRE_TRUE( model.removeSelectedFrame() );
    SW_EXPECT_EQUAL( 2, model.getAnimations()[0]._firstFrame );
    SW_EXPECT_EQUAL( 1, model.getAnimations()[0]._frameCount );

    model.selectFrame( -1 );
    SW_EXPECT_FALSE( model.removeSelectedFrame() );
}

SW_TEST_CASE( SpriteClipModelTest, DuplicatingAFrameGrowsTheRangeItIsIn )
{
    SpriteClipModel model;
    Util::fillFrames( model, 3 );
    SW_REQUIRE_TRUE( model.addAnimation() ); // 0..2
    model.selectFrame( 0 );
    model.editSelectedFrame()->_durationMs = 321;
    SW_REQUIRE_TRUE( model.duplicateSelectedFrame() );
    SW_EXPECT_EQUAL( 1, model.getSelectedFrame() );
    SW_REQUIRE_EQUAL( size_t{ 4 }, model.getFrames().size() );
    SW_EXPECT_EQUAL( 321, model.getFrames()[1]._durationMs );
    SW_EXPECT_EQUAL( 4, model.getAnimations()[0]._frameCount );

    model.selectFrame( 3 ); // 구간의 끝 뒤에 넣으면 구간은 그대로다
    SW_REQUIRE_TRUE( model.duplicateSelectedFrame() );
    SW_EXPECT_EQUAL( 4, model.getAnimations()[0]._frameCount );

    model.selectFrame( -1 );
    SW_EXPECT_FALSE( model.duplicateSelectedFrame() );
}

SW_TEST_CASE( SpriteClipModelTest, MovingAFrameStopsAtTheEnds )
{
    SpriteClipModel model;
    Util::fillFrames( model, 2 );
    model.selectFrame( 0 );
    model.editSelectedFrame()->_durationMs = 321;
    SW_EXPECT_FALSE( model.moveSelectedFrame( -1 ) );
    SW_EXPECT_TRUE( model.moveSelectedFrame( 1 ) );
    SW_EXPECT_EQUAL( 1, model.getSelectedFrame() );
    SW_EXPECT_EQUAL( 321, model.getFrames()[1]._durationMs );
    SW_EXPECT_FALSE( model.moveSelectedFrame( 1 ) );
}

SW_TEST_CASE( SpriteClipModelTest, AddFramesFromGridCoversEveryCell )
{
    SpriteClipModel model;
    model.editSliceColumns() = 2;
    model.editSliceRows()    = 2;
    model.addFramesFromGrid();
    SW_REQUIRE_EQUAL( size_t{ 5 }, model.getFrames().size() ); // 처음 빈 프레임 + 넷
    SW_EXPECT_EQUAL( 4, model.getSelectedFrame() );
    SW_EXPECT_NEAR( 0.5f, model.getFrames()[4]._uvRect._x, 1e-5f );
    SW_EXPECT_NEAR( 0.5f, model.getFrames()[4]._uvRect._y, 1e-5f );
    SW_EXPECT_NEAR( 0.5f, model.getFrames()[4]._uvRect._z, 1e-5f );
}

SW_TEST_CASE( SpriteClipModelTest, PickingACellEditsOrAddsAFrame )
{
    SpriteClipModel model;
    model.editSliceColumns() = 2;
    model.editSliceRows()    = 1;
    SW_EXPECT_TRUE( model.pickAtlasCell( 2.0f, 0.5f, false ) == SpriteClipModel::AtlasPickResult::None );
    SW_EXPECT_TRUE( model.pickAtlasCell( 0.75f, 0.5f, false ) == SpriteClipModel::AtlasPickResult::AddedFrame ); // 고른 프레임이 없다
    SW_EXPECT_EQUAL( 1, model.getSelectedFrame() );
    SW_EXPECT_TRUE( model.pickAtlasCell( 0.25f, 0.5f, false ) == SpriteClipModel::AtlasPickResult::EditedFrame );
    SW_EXPECT_NEAR( 0.0f, model.getFrames()[1]._uvRect._x, 1e-5f );
    SW_EXPECT_TRUE( model.pickAtlasCell( 0.25f, 0.5f, true ) == SpriteClipModel::AtlasPickResult::AddedFrame );
    SW_EXPECT_EQUAL( size_t{ 3 }, model.getFrames().size() );
}

// ---------------------------------------------------------------------- 테두리

// 아틀라스 크기를 모르면 픽셀 테두리를 비율로 바꿀 수 없다 - 저장하면 읽기가 오류로 버린다.
SW_TEST_CASE( SpriteClipModelTest, BorderNeedsAKnownAtlasSize )
{
    SpriteClipModel model;
    model.selectFrame( 0 );
    model.editSelectedFrame()->_borderPixels = float4{ 4.0f, 0.0f, 0.0f, 0.0f };
    SW_EXPECT_FALSE( model.commitSelectedFrameBorder() );
    SW_EXPECT_NEAR( 0.0f, model.getFrames()[0]._borderPixels._x, 1e-5f );
    SW_EXPECT_FALSE( model.getStatus().empty() );

    model.editAtlasPixelSize() = float2{ 32.0f, 32.0f };
    SW_REQUIRE_TRUE( model.commitAtlasPixelSize() );
    model.editSelectedFrame()->_borderPixels = float4{ 4.0f, -3.0f, 0.0f, 0.0f };
    SW_EXPECT_TRUE( model.commitSelectedFrameBorder() );
    SW_EXPECT_NEAR( 0.0f, model.getFrames()[0]._borderPixels._y, 1e-5f ); // 음수는 읽기가 거절한다

    model.editAtlasPixelSize() = float2{ 0.0f, 0.0f }; // 쓰는 프레임이 있으면 지우지 못한다
    SW_EXPECT_FALSE( model.commitAtlasPixelSize() );
    SW_EXPECT_NEAR( 32.0f, model.editAtlasPixelSize()._x, 1e-5f );
    SW_EXPECT_FALSE( model.commitAtlasPixelSize() ); // 바뀐 것이 없다
}

// ---------------------------------------------------------------------- 애니메이션 구간

// 개수로 이름을 지으면 앞 구간을 지운 뒤 남은 이름과 겹쳤다.
SW_TEST_CASE( SpriteClipModelTest, NewAnimationNamesNeverCollide )
{
    SpriteClipModel model;
    SW_REQUIRE_TRUE( model.addAnimation() ); // anim0
    SW_REQUIRE_TRUE( model.addAnimation() ); // anim1
    model.selectAnimation( 0 );
    SW_REQUIRE_TRUE( model.removeSelectedAnimation() );
    SW_REQUIRE_TRUE( model.addAnimation() );
    SW_REQUIRE_EQUAL( size_t{ 2 }, model.getAnimations().size() );
    SW_EXPECT_TRUE( model.getAnimations()[0]._name != model.getAnimations()[1]._name );
}

SW_TEST_CASE( SpriteClipModelTest, AnimationNeedsAFrame )
{
    SpriteClipModel model;
    model.selectFrame( 0 );
    SW_REQUIRE_TRUE( model.removeSelectedFrame() );
    SW_EXPECT_FALSE( model.addAnimation() ); // 길이 0 구간은 읽기가 버린다
    SW_EXPECT_TRUE( model.getAnimations().empty() );
    SW_EXPECT_FALSE( model.getStatus().empty() );
}

SW_TEST_CASE( SpriteClipModelTest, AnimationNameMustBeUniqueAndNotEmpty )
{
    SpriteClipModel model;
    SW_REQUIRE_TRUE( model.addAnimation() );
    SW_REQUIRE_TRUE( model.addAnimation() ); // 고른 것은 이것이다

    model.editAnimationName() = "";
    SW_EXPECT_FALSE( model.commitAnimationName() );
    SW_EXPECT_TRUE( model.getAnimations()[1]._name == model.editAnimationName().c_str() ); // 입력란을 되돌린다

    model.editAnimationName() = model.getAnimations()[0]._name.c_str();
    SW_EXPECT_FALSE( model.commitAnimationName() );

    model.editAnimationName() = model.getAnimations()[1]._name.c_str(); // 자기 이름은 된다
    SW_EXPECT_TRUE( model.commitAnimationName() );
    model.editAnimationName() = "run";
    SW_EXPECT_TRUE( model.commitAnimationName() );
    SW_EXPECT_TRUE( model.getAnimations()[1]._name == "run" );
}

// 손으로 친 구간을 프레임 안으로 묶는다 - 밖으로 저장하면 읽을 때마다 경고로 잘렸다.
SW_TEST_CASE( SpriteClipModelTest, AnimationRangeIsClampedToTheFrames )
{
    SpriteClipModel model;
    Util::fillFrames( model, 3 );
    SW_REQUIRE_TRUE( model.addAnimation() );
    SpriteClipAnimation* pAnimation = model.editSelectedAnimation();
    SW_REQUIRE_TRUE( pAnimation != nullptr );
    pAnimation->_firstFrame = 9;
    pAnimation->_frameCount = 9;
    SW_EXPECT_TRUE( model.commitAnimationRange() );
    SW_EXPECT_EQUAL( 2, pAnimation->_firstFrame );
    SW_EXPECT_EQUAL( 1, pAnimation->_frameCount );
    pAnimation->_firstFrame = -1;
    pAnimation->_frameCount = 0;
    SW_EXPECT_TRUE( model.commitAnimationRange() );
    SW_EXPECT_EQUAL( 0, pAnimation->_firstFrame );
    SW_EXPECT_EQUAL( 1, pAnimation->_frameCount );

    model.selectAnimation( -1 );
    SW_EXPECT_FALSE( model.commitAnimationRange() );
}

// ---------------------------------------------------------------------- 미리보기

SW_TEST_CASE( SpriteClipModelTest, PreviewAdvancesFrameDurationsAndLoops )
{
    SpriteClipModel model;
    Util::fillFrames( model, 3 );
    Util::setFrameDuration( model, 100 );
    SW_EXPECT_EQUAL( 0, model.getPreviewFrame() );
    model.advancePreview( 0.15f ); // 꺼져 있으면 흐르지 않는다
    SW_EXPECT_EQUAL( 0, model.getPreviewFrame() );

    model.togglePreview();
    model.advancePreview( 0.15f );
    SW_EXPECT_EQUAL( 1, model.getPreviewFrame() );
    model.advancePreview( 0.1f );
    SW_EXPECT_EQUAL( 2, model.getPreviewFrame() );
    model.advancePreview( 0.1f ); // 0.35 초 - 프레임 전체는 반복한다
    SW_EXPECT_EQUAL( 0, model.getPreviewFrame() );
    SW_EXPECT_TRUE( model.isPreviewPlaying() );
}

SW_TEST_CASE( SpriteClipModelTest, PreviewPlaysTheSelectedRangeAndStopsWithoutLoop )
{
    SpriteClipModel model;
    Util::fillFrames( model, 4 );
    Util::setFrameDuration( model, 100 );
    SW_REQUIRE_TRUE( model.addAnimation() );
    SpriteClipAnimation* pAnimation = model.editSelectedAnimation();
    SW_REQUIRE_TRUE( pAnimation != nullptr );
    pAnimation->_firstFrame = 2;
    pAnimation->_frameCount = 2;
    pAnimation->_bLoop      = SW_FALSE;

    SW_EXPECT_EQUAL( 2, model.getPreviewFrame() );
    model.togglePreview();
    model.advancePreview( 0.15f );
    SW_EXPECT_EQUAL( 3, model.getPreviewFrame() );
    model.advancePreview( 1.0f );
    SW_EXPECT_EQUAL( 3, model.getPreviewFrame() ); // 끝에 머문다
    SW_EXPECT_FALSE( model.isPreviewPlaying() );
}

// 시간이 없는 프레임은 애니메이터처럼 기본 프레임 속도로 머문다.
SW_TEST_CASE( SpriteClipModelTest, PreviewUsesTheFrameRateForFramesWithoutDuration )
{
    SpriteClipModel model;
    Util::fillFrames( model, 2 );
    Util::setFrameDuration( model, 0 );
    model.togglePreview();
    model.advancePreview( SpriteClipModel::kPreviewFrameSeconds * 1.5f );
    SW_EXPECT_EQUAL( 1, model.getPreviewFrame() );
}

SW_TEST_CASE( SpriteClipModelTest, PreviewWithoutFramesShowsNothing )
{
    SpriteClipModel model;
    model.selectFrame( 0 );
    SW_REQUIRE_TRUE( model.removeSelectedFrame() );
    SW_EXPECT_EQUAL( -1, model.getPreviewFrame() );
    model.togglePreview();
    model.advancePreview( 1.0f );
    SW_EXPECT_FALSE( model.isPreviewPlaying() );
}

// ---------------------------------------------------------------------- 불규칙 프레임(아틀라스 위에서 끈 사각형 · 가장자리)

// 끈 사각형은 순서와 상관없이 프레임 uv 가 되고, 아틀라스 크기를 알면 모서리가 픽셀 경계에 맞는다(옆 그림이 번지지 않게).
SW_TEST_CASE( SpriteClipModelTest, DraggedRegionSnapsToAtlasPixels )
{
    SpriteClipModel model;
    model.editAtlasPixelSize() = float2{ 272.0f, 256.0f };
    SW_REQUIRE_TRUE( model.commitAtlasPixelSize() );
    model.selectFrame( 0 );
    const SpriteClipModel::AtlasPickResult result = model.setFrameRegion( float2{ 0.30f, 0.40f }, float2{ 0.10f, 0.05f }, false );
    SW_EXPECT_TRUE( result == SpriteClipModel::AtlasPickResult::EditedFrame );
    const float4& uv = model.getFrames()[0]._uvRect;
    // 0.10 × 272 = 27.2 → 27 px, 0.30 × 272 = 81.6 → 82 px. 0.05 × 256 = 12.8 → 13 px, 0.40 × 256 = 102.4 → 102 px.
    SW_EXPECT_NEAR( 27.0f / 272.0f, uv._x, 1e-6f );
    SW_EXPECT_NEAR( 13.0f / 256.0f, uv._y, 1e-6f );
    SW_EXPECT_NEAR( 55.0f / 272.0f, uv._z, 1e-6f );
    SW_EXPECT_NEAR( 89.0f / 256.0f, uv._w, 1e-6f );
    // 맞춘 뒤 크기가 0 이면 프레임을 만들지 않는다.
    SW_EXPECT_TRUE( model.setFrameRegion( float2{ 0.5f, 0.5f }, float2{ 0.5005f, 0.7f }, true ) == SpriteClipModel::AtlasPickResult::None );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( model.getFrames().size() ) );
}

// 가장자리는 맞은편을 넘지 않고, 좌우가 뒤집힌 프레임은 뒤집힘을 지킨 채 같은 그림 영역의 가장자리가 옮겨진다.
SW_TEST_CASE( SpriteClipModelTest, EdgeDragKeepsFlipAndSize )
{
    SpriteClipModel model;
    model.editAtlasPixelSize() = float2{ 100.0f, 100.0f };
    SW_REQUIRE_TRUE( model.commitAtlasPixelSize() );
    model.selectFrame( 0 );
    model.editSelectedFrame()->_uvRect = float4{ 0.5f, 0.0f, -0.25f, 0.5f }; // 그림 영역 0.25 .. 0.5, 좌우 뒤집힘
    SW_REQUIRE_TRUE( model.moveSelectedFrameEdge( SpriteFrameEdge::Left, 0.2f ) );
    const float4& uv = model.getFrames()[0]._uvRect;
    SW_EXPECT_NEAR( 0.5f, uv._x, 1e-6f );
    SW_EXPECT_NEAR( -0.3f, uv._z, 1e-6f );
    SW_EXPECT_FALSE( model.moveSelectedFrameEdge( SpriteFrameEdge::Left, 0.6f ) ); // 오른쪽(0.5)을 넘는다
    SW_EXPECT_FALSE( model.moveSelectedFrameEdge( SpriteFrameEdge::Left, 0.2f ) ); // 그대로
    SW_REQUIRE_TRUE( model.moveSelectedFrameEdge( SpriteFrameEdge::Bottom, 0.333f ) );
    SW_EXPECT_NEAR( 0.33f, model.getFrames()[0]._uvRect._w, 1e-6f ); // 픽셀 경계(33 px)

    // 저장 글을 런타임이 같은 uv 로 읽는다.
    SpriteClipAsset readBack;
    SW_REQUIRE_TRUE( readBack.loadFromXMLText( model.captureText() ) );
    SW_EXPECT_NEAR( -0.3f, readBack._listFrame[0]._uvRect._z, 1e-5f );
}
