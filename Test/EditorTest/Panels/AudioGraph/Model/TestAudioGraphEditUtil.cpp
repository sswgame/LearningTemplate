#include "pch.h"

#include "Editor/Panels/AudioGraph/Model/AudioGraphEditUtil.h"

#include "Engine/Audio/Graph/AudioGraphDef.h"

#include "TestFramework/TestFramework.h"

// AudioGraphEditUtilTest — 소리 그래프 창의 캔버스 편집(id · 잇기 · 끊기 · 넣기 · 지우기 · 이름 따라가기 · 처음 배치, ImGui 없음).

namespace
{
    /** @brief 사인 → 로우패스 → 게인(출력) 그래프입니다(위치 없음). */
    sw::AudioGraphDef makeAudioGraphEditChain()
    {
        sw::AudioGraphDef def;
        const bool        bLoaded = def.loadFromXMLText( R"(<AudioGraphDef _output="Amp" _durationSeconds="1"><_listNode>
                <AudioGraphNodeDesc _name="Osc" _type="Sine" />
                <AudioGraphNodeDesc _name="Tone" _type="LowPass"><_listInput><item>Osc</item></_listInput></AudioGraphNodeDesc>
                <AudioGraphNodeDesc _name="Amp" _type="Gain"><_listInput><item>Tone</item></_listInput></AudioGraphNodeDesc></_listNode></AudioGraphDef>)" );
        SW_EXPECT_TRUE( bLoaded );
        return def;
    }
} // namespace

/**
 * @brief [AudioGraphEditUtilTest] 핀 · 링크 id 가 노드와 입력 자리로 되돌아가고, 소스의 들어오는 핀과 없는 자리는 모른다
 */
SW_TEST_CASE( AudioGraphEditUtilTest, IdentifiersRoundTrip )
{
    using Util                  = sw::editor::AudioGraphEditUtil;
    const sw::AudioGraphDef def = makeAudioGraphEditChain();
    uint32                  nodeIndex{ 0 };
    bool                    bInput{ false };
    SW_EXPECT_TRUE( Util::tryDecodePinID( def, Util::makeInputPinID( 1 ), nodeIndex, bInput ) );
    SW_EXPECT_EQUAL( 1u, nodeIndex );
    SW_EXPECT_TRUE( bInput );
    SW_EXPECT_TRUE( Util::tryDecodePinID( def, Util::makeOutputPinID( 0 ), nodeIndex, bInput ) );
    SW_EXPECT_FALSE( bInput );
    SW_EXPECT_FALSE( Util::tryDecodePinID( def, Util::makeInputPinID( 0 ), nodeIndex, bInput ) ); // 사인은 소스라 들어오는 핀이 없다
    uint32 readerIndex{ 0 };
    uint32 inputSlot{ 0 };
    SW_EXPECT_TRUE( Util::tryDecodeLinkID( def, Util::makeLinkID( 2, 0 ), readerIndex, inputSlot ) );
    SW_EXPECT_EQUAL( 2u, readerIndex );
    SW_EXPECT_FALSE( Util::tryDecodeLinkID( def, Util::makeLinkID( 2, 1 ), readerIndex, inputSlot ) );
}

/**
 * @brief [AudioGraphEditUtilTest] 잇기는 소스 입력 · 겹침 · 고리를 거절하고 모델을 그대로 두며, 끊기 · 넣기 · 지우기는 입력 목록과 출력을 함께 맞춘다
 */
SW_TEST_CASE( AudioGraphEditUtilTest, ConnectAddRemove )
{
    using Util            = sw::editor::AudioGraphEditUtil;
    sw::AudioGraphDef def = makeAudioGraphEditChain();
    SW_EXPECT_FALSE( Util::connect( def, 2, 0 ) ); // 사인은 입력을 받지 않는다
    SW_EXPECT_FALSE( Util::connect( def, 0, 1 ) ); // 이미 이었다
    SW_EXPECT_FALSE( Util::connect( def, 2, 1 ) ); // Amp → Tone 은 고리다
    SW_EXPECT_EQUAL( size_t( 1 ), def._listNode[1]._listInput.size() );

    uint32 noiseIndex{ 0 };
    SW_ASSERT_TRUE( Util::addNode( def, sw::AudioGraphNodeType::Noise, sw::float2{ 10.0f, 20.0f }, noiseIndex ) );
    SW_EXPECT_TRUE( def._listNode[noiseIndex]._name == sw::hashed_string( "Noise" ) );
    uint32 secondNoise{ 0 };
    SW_ASSERT_TRUE( Util::addNode( def, sw::AudioGraphNodeType::Noise, sw::float2{}, secondNoise ) );
    SW_EXPECT_TRUE( def._listNode[secondNoise]._name == sw::hashed_string( "Noise2" ) );
    SW_EXPECT_TRUE( Util::connect( def, noiseIndex, 2 ) );
    SW_EXPECT_EQUAL( size_t( 2 ), def._listNode[2]._listInput.size() );
    SW_EXPECT_TRUE( def.validate( "<test>" ) );

    SW_EXPECT_TRUE( Util::disconnect( def, 2, 1 ) );
    SW_EXPECT_EQUAL( size_t( 1 ), def._listNode[2]._listInput.size() );
    SW_EXPECT_TRUE( Util::removeNode( def, 1 ) ); // Tone 을 지우면 Amp 의 입력에서도 빠진다
    SW_EXPECT_TRUE( def._listNode[1]._listInput.empty() );
    {
        SW_TEST_DEFENSIVE_SCOPE( "a gain node without inputs fails validation with a reason" ); // 검증 실패를 알리는 로그가 이 확인이 보려는 것이다
        SW_EXPECT_FALSE( def.validate( "<test>" ) );                                            // 입력이 없는 게인 — 저장이 막힌다
    }
    SW_EXPECT_TRUE( Util::removeNode( def, 1 ) ); // 출력 노드를 지우면 출력이 빈다
    SW_EXPECT_TRUE( def._output.empty() );
}

/**
 * @brief [AudioGraphEditUtilTest] 이름을 고치면 입력 · 출력이 따라가고, 위치 없는 그래프는 처리 깊이로 열을 나눠 놓인다(한 번만)
 */
SW_TEST_CASE( AudioGraphEditUtilTest, RenameAndLayout )
{
    using Util             = sw::editor::AudioGraphEditUtil;
    sw::AudioGraphDef def  = makeAudioGraphEditChain();
    def._listNode[2]._name = sw::hashed_string( "Out" );
    Util::replaceNodeReferences( def, sw::hashed_string( "Amp" ), sw::hashed_string( "Out" ) );
    SW_EXPECT_TRUE( def._output == sw::hashed_string( "Out" ) );
    def._listNode[1]._name = sw::hashed_string( "Filter" );
    Util::replaceNodeReferences( def, sw::hashed_string( "Tone" ), sw::hashed_string( "Filter" ) );
    SW_EXPECT_TRUE( def._listNode[2]._listInput[0] == sw::hashed_string( "Filter" ) );
    SW_EXPECT_TRUE( def.validate( "<test>" ) );

    SW_ASSERT_TRUE( Util::layoutIfUnplaced( def ) );
    SW_EXPECT_TRUE( def._listNode[0]._editorPosition._x < def._listNode[1]._editorPosition._x );
    SW_EXPECT_TRUE( def._listNode[1]._editorPosition._x < def._listNode[2]._editorPosition._x );
    SW_EXPECT_FALSE( Util::layoutIfUnplaced( def ) ); // 이미 놓였다
}
