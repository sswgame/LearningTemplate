#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/AudioGraph/Model/AudioGraphEditUtil.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [AudioGraphEditUtilTest] 복제는 사본끼리의 입력만 새 이름으로 잇고, 바깥에서 오던 입력은 버린다
 */
SW_TEST_CASE( AudioGraphEditUtilTest, DuplicateKeepsInnerInputsOnly )
{
    AudioGraphDef def;
    uint32        sineIndex       = 0;
    uint32        secondSineIndex = 0;
    uint32        gainIndex       = 0;
    SW_REQUIRE_TRUE( AudioGraphEditUtil::addNode( def, AudioGraphNodeKind::Sine, float2{ 0.0f, 0.0f }, sineIndex ) );
    SW_REQUIRE_TRUE( AudioGraphEditUtil::addNode( def, AudioGraphNodeKind::Sine, float2{ 0.0f, 100.0f }, secondSineIndex ) );
    SW_REQUIRE_TRUE( AudioGraphEditUtil::addNode( def, AudioGraphNodeKind::Gain, float2{ 200.0f, 0.0f }, gainIndex ) );
    SW_REQUIRE_TRUE( AudioGraphEditUtil::connect( def, sineIndex, gainIndex ) );
    SW_REQUIRE_TRUE( AudioGraphEditUtil::connect( def, secondSineIndex, gainIndex ) );
    const hashed_string sineName = def._listNode[sineIndex]._name;
    const hashed_string gainName = def._listNode[gainIndex]._name;

    const vector<hashed_string> listNew = AudioGraphEditUtil::duplicateNodes( def, { sineName, gainName }, float2{ 40.0f, 40.0f } );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 2 ), listNew.size() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 5 ), def._listNode.size() );
    const optional<uint32> copyGain = def.findNodeIndex( listNew[1] );
    SW_REQUIRE_TRUE( copyGain.has_value() );
    const AudioGraphNodeDef& gainCopy = def._listNode[*copyGain];
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), gainCopy._listInput.size() ); // 둘째 Sine 은 복제하지 않았다 — 그 입력은 버린다
    SW_EXPECT_TRUE( gainCopy._listInput[0] == listNew[0] );
    SW_EXPECT_NEAR( 240.0f, gainCopy._editorPosition._x, 1e-4f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), def._listNode[gainIndex]._listInput.size() ); // 원본은 그대로
}
