// BehaviorTreeEditModelTest — 행동 트리 편집 모델의 변형 저장: 부모(`_base`)를 가리키던 트리는 부모와의 차이로 쓰고 다시 읽으면 같은 트리다(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"

#include "GameFramework/Plugin/Editor/GameplayTools/AI/BehaviorTreeEditModel.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [BehaviorTreeEditModelTest] 변형 저장 — 고친 노드만 `id` 로 적고 물려받은 가지는 적지 않는다. 저장 글을 다시 읽으면 같은 트리이고 부모도 그대로다
 */
SW_TEST_CASE( BehaviorTreeEditModelTest, VariantSavesOnlyItsDifference )
{
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "base.bt.xml" ), R"XML(<BehaviorTree>
  <Selector id="root" name="Root">
    <Sequence id="idle">
      <Condition id="calm" test="!flag(alarm)"/>
      <Action id="rest" task="flag.set" flag="rested"/>
    </Sequence>
  </Selector>
</BehaviorTree>)XML" ) );
    const string baseID = test::ScopedScratchDomain::makeID( "base.bt.xml" ).str();

    BehaviorTreeEditModel model;
    string                error;
    SW_REQUIRE_TRUE_MSG( model.parse( "<BehaviorTree _base=\"" + baseID + "\"><Selector id=\"root\" name=\"Guard\"/></BehaviorTree>", error ), error.c_str() );
    SW_EXPECT_EQUAL( baseID, model.getBaseID() );

    string saved;
    SW_REQUIRE_TRUE_MSG( model.makeSaveText( saved, error ), error.c_str() );
    SW_EXPECT_TRUE( saved.find( baseID ) != string::npos );
    SW_EXPECT_TRUE( saved.find( "Guard" ) != string::npos );
    SW_EXPECT_TRUE( saved.find( "rested" ) == string::npos ); // 물려받은 가지는 적지 않는다

    BehaviorTreeEditModel reread;
    SW_REQUIRE_TRUE_MSG( reread.parse( saved, error ), error.c_str() );
    SW_EXPECT_EQUAL( model.write(), reread.write() );
    SW_EXPECT_EQUAL( baseID, reread.getBaseID() );

    BehaviorTreeEditModel plain; // 부모 없는 트리는 펼친 글 그대로다
    SW_REQUIRE_TRUE_MSG( plain.parse( "<BehaviorTree><Action id=\"a\" task=\"flag.set\" flag=\"x\"/></BehaviorTree>", error ), error.c_str() );
    SW_REQUIRE_TRUE( plain.makeSaveText( saved, error ) );
    SW_EXPECT_EQUAL( plain.write(), saved );
}
