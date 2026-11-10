#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Inherit/DataInheritance.h"

#include "GameFramework/Runtime/Character/AI/BehaviorTreeAsset.h"
#include "GameFramework/Runtime/Character/AI/Blackboard.h"
#include "GameFramework/Runtime/Foundation/Condition/GameConditions.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"

using namespace sw;

// BehaviorTreeVariantTest — 행동 트리 데이터의 공통 어휘(조건식 · 등록된 동작)와 변형(`_base` 가지 교체 · SubTree 펼침 · 슬롯)을 값으로 본다(nogpu).

namespace
{
    /** @brief 리소스 id → 글 표입니다. 부모 · 하위 트리 파일을 디스크 없이 준다. */
    class XTestSource final : public IDataInheritanceSource
    {
    public:
        vector<std::pair<string, string>> _listFile;

        [[nodiscard]] bool readText( string_view assetID, string& outText ) override
        {
            for ( const auto& [id, text] : _listFile )
            {
                if ( id == assetID )
                {
                    outText = text;
                    return true;
                }
            }
            return false;
        }
    };

    struct XTestUtil
    {
        static constexpr const utf8* kBaseTree = R"XML(<BehaviorTree>
  <Selector id="root" name="Root">
    <Sequence id="idle">
      <Condition id="calm" test="!flag(alarm)"/>
      <Action id="rest" task="flag.set" flag="rested"/>
    </Sequence>
  </Selector>
</BehaviorTree>)XML";

        /** @brief 트리를 한 번 돌리고 플래그를 돌려줍니다. */
        static void tickOnce( const BehaviorTree& tree, GameFlags& flags )
        {
            GameplayContext world;
            world.set<GameFlagsSlot>( &flags );
            world.set<GameFlagsWriteSlot>( &flags );
            BehaviorTreeRunner runner;
            runner.initialize( &tree );
            runner.setGameplayContext( world );
            Blackboard blackboard;
            (void)runner.tick( blackboard, nullptr, 0.0f ); // 결과는 플래그로 본다
        }
    };
} // namespace

/** @brief [BehaviorTreeVariantTest] 데이터 트리의 조건 잎과 작업 잎은 공통 어휘다 — 같은 플래그 표를 읽고 쓴다 */
SW_TEST_CASE( BehaviorTreeVariantTest, LeavesUseSharedVocabulary )
{
    XTestSource       source;
    BehaviorTreeAsset asset;
    SW_REQUIRE_TRUE( asset.loadFromXMLText( XTestUtil::kBaseTree, "base.bt.xml", source ) );
    GameFlags flags;
    XTestUtil::tickOnce( asset.getTree(), flags );
    SW_EXPECT_EQUAL( 1, flags.getFlag( "rested" ) );

    GameFlags alarmed;
    alarmed.setFlag( "alarm" );
    XTestUtil::tickOnce( asset.getTree(), alarmed );
    SW_EXPECT_EQUAL( 0, alarmed.getFlag( "rested" ) );
}

/** @brief [BehaviorTreeVariantTest] `_base` 트리에서 `id` 가 같은 노드만 바꾸고 새 가지를 앞에 끼운다. 모르는 동작은 로드 오류다 */
SW_TEST_CASE( BehaviorTreeVariantTest, BaseTreeReplacesBranchByID )
{
    XTestSource source;
    source._listFile.push_back( { "base.bt.xml", XTestUtil::kBaseTree } );
    constexpr const utf8* kChild = R"XML(<BehaviorTree _base="base.bt.xml">
  <Selector id="root">
    <Action id="shout" task="flag.set" flag="shouted" _before="idle"/>
    <Sequence id="idle">
      <Action id="rest" task="flag.set" flag="slept"/>
    </Sequence>
  </Selector>
</BehaviorTree>)XML";
    BehaviorTreeAsset     asset;
    SW_REQUIRE_TRUE( asset.loadFromXMLText( kChild, "child.bt.xml", source ) );
    GameFlags flags;
    XTestUtil::tickOnce( asset.getTree(), flags );
    SW_EXPECT_EQUAL( 1, flags.getFlag( "shouted" ) ); // 앞에 끼운 가지가 먼저 성공했다
    SW_EXPECT_EQUAL( 0, flags.getFlag( "rested" ) );

    SW_EXPECT_LOG( "BehaviorTreeAsset.NamesUnknownTaskRegistered" );
    BehaviorTreeAsset broken;
    SW_EXPECT_FALSE( broken.loadFromXMLText( R"(<BehaviorTree><Action task="no.such.action"/></BehaviorTree>)", "broken.bt.xml", source ) );
}

/** @brief [BehaviorTreeVariantTest] `<SubTree tree>` 는 읽을 때 펼치고 자기 자신을 넣으면 오류다. `<SubTree slot>` 은 러너가 채운 트리를 돌린다 */
SW_TEST_CASE( BehaviorTreeVariantTest, SubTreeExpandsAndSlotsFill )
{
    XTestSource source;
    source._listFile.push_back( { "rest.bt.xml", R"(<BehaviorTree><Action task="flag.set" flag="rested"/></BehaviorTree>)" } );
    source._listFile.push_back( { "loop.bt.xml", R"(<BehaviorTree><Sequence><SubTree tree="loop.bt.xml"/></Sequence></BehaviorTree>)" } );

    BehaviorTreeAsset expanded;
    SW_REQUIRE_TRUE( expanded.loadFromXMLText( R"(<BehaviorTree><Sequence><SubTree tree="rest.bt.xml"/></Sequence></BehaviorTree>)", "host.bt.xml", source ) );
    SW_EXPECT_EQUAL( 2, expanded.getTree().getNodeCount() ); // 펼친 노드가 그 자리에 있다 — 실행에는 하위 트리 노드가 없다
    GameFlags flags;
    XTestUtil::tickOnce( expanded.getTree(), flags );
    SW_EXPECT_EQUAL( 1, flags.getFlag( "rested" ) );

    SW_EXPECT_LOG( "BehaviorTreeAsset.SubTreeCycle" );
    BehaviorTreeAsset cycle;
    SW_EXPECT_FALSE( cycle.loadFromXMLText( R"(<BehaviorTree><SubTree tree="loop.bt.xml"/></BehaviorTree>)", "cycle.bt.xml", source ) );

    BehaviorTreeAsset host;
    SW_REQUIRE_TRUE( host.loadFromXMLText( R"(<BehaviorTree><Sequence><SubTree slot="combat"/></Sequence></BehaviorTree>)", "slot.bt.xml", source ) );
    BehaviorTreeAsset fill;
    SW_REQUIRE_TRUE( fill.loadFromXMLText( R"(<BehaviorTree><Action task="flag.set" flag="fought"/></BehaviorTree>)", "fill.bt.xml", source ) );
    GameFlags       slotFlags;
    GameplayContext world;
    world.set<GameFlagsSlot>( &slotFlags );
    world.set<GameFlagsWriteSlot>( &slotFlags );
    BehaviorTreeRunner runner;
    runner.initialize( &host.getTree() );
    runner.setGameplayContext( world );
    runner.setSlotTree( "combat", &fill.getTree() );
    Blackboard blackboard;
    SW_EXPECT_TRUE( runner.tick( blackboard, nullptr, 0.0f ) == BehaviorStatus::Success );
    SW_EXPECT_EQUAL( 1, slotFlags.getFlag( "fought" ) );
}
