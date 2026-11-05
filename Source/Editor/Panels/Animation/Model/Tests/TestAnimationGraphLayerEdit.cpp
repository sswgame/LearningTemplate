#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Animation/Model/AnimationGraphLayerEdit.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( AnimationGraphLayerEditTest, NewItemsGetUniqueNames )
{
    AnimationGraphAsset graph;
    SW_EXPECT_EQUAL( 0, AnimationGraphLayerEdit::addLayer( graph ) );
    SW_EXPECT_EQUAL( 1, AnimationGraphLayerEdit::addLayer( graph ) );
    SW_EXPECT_TRUE( graph._listLayer[0]._name == hashed_string( "Layer" ) );
    SW_EXPECT_TRUE( graph._listLayer[1]._name == hashed_string( "Layer2" ) );
    SW_EXPECT_EQUAL( 0, AnimationGraphLayerEdit::addMask( graph ) );
    SW_EXPECT_EQUAL( 1, AnimationGraphLayerEdit::addMask( graph ) );
    SW_EXPECT_TRUE( graph._listBoneMask[1]._name == hashed_string( "Mask2" ) );
}

SW_TEST_CASE( AnimationGraphLayerEditTest, MoveStopsAtTheEnds )
{
    AnimationGraphAsset graph;
    (void)AnimationGraphLayerEdit::addLayer( graph );
    (void)AnimationGraphLayerEdit::addLayer( graph );
    SW_EXPECT_EQUAL( 0, AnimationGraphLayerEdit::moveLayer( graph, 0, -1 ) );
    SW_EXPECT_EQUAL( 1, AnimationGraphLayerEdit::moveLayer( graph, 0, 1 ) );
    SW_EXPECT_TRUE( graph._listLayer[1]._name == hashed_string( "Layer" ) );
    SW_EXPECT_EQUAL( 1, AnimationGraphLayerEdit::moveLayer( graph, 1, 1 ) );
}

// 레이어가 쓰는 마스크를 지우면 저장한 그래프가 로드 오류가 된다.
SW_TEST_CASE( AnimationGraphLayerEditTest, MaskInUseIsNotRemoved )
{
    AnimationGraphAsset graph;
    (void)AnimationGraphLayerEdit::addMask( graph );
    (void)AnimationGraphLayerEdit::addLayer( graph );
    graph._listLayer[0]._maskName = graph._listBoneMask[0]._name;
    SW_EXPECT_FALSE( AnimationGraphLayerEdit::removeMask( graph, 0 ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( graph._listBoneMask.size() ) );
    graph._listLayer[0]._maskName = hashed_string{};
    SW_EXPECT_TRUE( AnimationGraphLayerEdit::removeMask( graph, 0 ) );
    SW_EXPECT_FALSE( AnimationGraphLayerEdit::removeMask( graph, 0 ) );
}

SW_TEST_CASE( AnimationGraphLayerEditTest, EmptyPresetEmbedsTheEntries )
{
    AnimationBoneMaskDef mask;
    mask._filePath = "x.bonemask.xml";
    SW_EXPECT_TRUE( AnimationGraphLayerEdit::applyMaskPreset( mask, "" ) == BoneMaskPresetResult::Embedded );
    SW_EXPECT_TRUE( mask._filePath.empty() );
}
