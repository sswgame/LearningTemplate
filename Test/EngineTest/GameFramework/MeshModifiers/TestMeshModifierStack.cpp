// 모디파이어 스택 — 순서가 결과를 바꾼다, 위/아래/빼기, 꺼진 모디파이어는 건너뜀, 사이드카 · 굽기 경로, 월드 공간(기준 오브젝트) 변환, 시간 입력.
#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "EngineTest/GameFramework/MeshModifiers/MeshModifierTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( MeshModifierStackTest, OrderChangesTheResult )
{
    const MeshModifierMesh source = test::MeshModifierTestUtil::makeBoxMesh( 3 );
    MeshModifierStack      stack;
    const int32            twist = stack.addModifier( "twist" );
    const int32            bend  = stack.addModifier( "bend" );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *stack.findModifier( twist ), "Angle", 90.0f ) );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *stack.findModifier( bend ), "Angle", 60.0f ) );
    const MeshModifierEvaluation evaluation{};

    MeshModifierMesh twistThenBend = source;
    stack.apply( twistThenBend, evaluation );
    SW_ASSERT_TRUE( stack.moveDown( 0 ) );
    SW_EXPECT_EQUAL( string( "bend" ), stack.findModifier( 0 )->getTypeID() );
    MeshModifierMesh bendThenTwist = source;
    stack.apply( bendThenTwist, evaluation );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( twistThenBend._listPosition, bendThenTwist._listPosition ) > 1e-2f );
}

SW_TEST_CASE( MeshModifierStackTest, SplitApplyMatchesApply )
{
    // 에디터 세션의 앞부분 캐시: 앞 구간 결과에 뒷 구간만 이어 건 것이 apply 한 번과 비트 단위로 같고, 입력 상자도 같은 순서로 모인다
    const MeshModifierMesh source = test::MeshModifierTestUtil::makeBoxMesh( 3 );
    MeshModifierStack      stack;
    const int32            twist  = stack.addModifier( "twist" );
    const int32            bend   = stack.addModifier( "bend" );
    const int32            limits = stack.addModifier( "displacelimits" );
    SW_ASSERT_TRUE( limits >= 0 );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *stack.findModifier( limits ), "Clamp Displacement", 1.0f ) ); // 스택 입력 위치가 필요해진다
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *stack.findModifier( twist ), "Angle", 90.0f ) );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *stack.findModifier( bend ), "Angle", 60.0f ) );
    stack.setRecomputingNormals( true );
    SW_ASSERT_TRUE( stack.needsStackInputPosition() );

    vector<MeshModifierBounds> listWholeBounds;
    MeshModifierEvaluation     wholeEvaluation{};
    wholeEvaluation._pListInputBounds = &listWholeBounds;
    MeshModifierMesh whole            = source;
    stack.apply( whole, wholeEvaluation );

    vector<MeshModifierBounds> listSplitBounds;
    MeshModifierEvaluation     splitEvaluation{};
    splitEvaluation._pListInputBounds        = &listSplitBounds;
    splitEvaluation._pListStackInputPosition = &source._listPosition;
    MeshModifierMesh split                   = source;
    stack.applyRange( split, splitEvaluation, 0, 1 );
    stack.applyRange( split, splitEvaluation, 1, stack.getModifierCount() );
    stack.finishApply( split );

    SW_EXPECT_TRUE( test::MeshModifierTestUtil::isBitwiseEqual( whole._listPosition, split._listPosition ) );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::isBitwiseEqual( whole._listNormal, split._listNormal ) );
    SW_ASSERT_EQUAL( listWholeBounds.size(), listSplitBounds.size() );
    for ( size_t index = 0; index < listWholeBounds.size(); ++index )
    {
        SW_EXPECT_TRUE( listWholeBounds[index]._min == listSplitBounds[index]._min );
        SW_EXPECT_TRUE( listWholeBounds[index]._max == listSplitBounds[index]._max );
    }
}

SW_TEST_CASE( MeshModifierStackTest, PrefixTextFollowsOnlyThePrefix )
{
    MeshModifierStack stack;
    const int32       twist  = stack.addModifier( "twist" );
    const int32       bend   = stack.addModifier( "bend" );
    const string      before = stack.makePrefixJSONText( 1 );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *stack.findModifier( bend ), "Angle", 30.0f ) );
    SW_EXPECT_EQUAL( before, stack.makePrefixJSONText( 1 ) ); // 뒤쪽(고른 것)만 바뀌면 앞부분 캐시는 그대로다
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *stack.findModifier( twist ), "Angle", 30.0f ) );
    SW_EXPECT_TRUE( before != stack.makePrefixJSONText( 1 ) );
    SW_EXPECT_EQUAL( MeshModifierStack{}.makePrefixJSONText( 2 ), stack.makePrefixJSONText( 0 ) ); // 앞부분이 없으면 빈 스택과 같다
}

SW_TEST_CASE( MeshModifierStackTest, EditOperationsCheckRange )
{
    MeshModifierStack stack;
    SW_EXPECT_EQUAL( -1, stack.addModifier( "no_such_modifier" ) );
    SW_EXPECT_EQUAL( 0, stack.addModifier( "bend" ) );
    SW_EXPECT_EQUAL( 1, stack.addModifier( "noise" ) );
    SW_EXPECT_FALSE( stack.moveUp( 0 ) );
    SW_EXPECT_FALSE( stack.moveDown( 1 ) );
    SW_EXPECT_TRUE( stack.moveUp( 1 ) );
    SW_EXPECT_EQUAL( string( "noise" ), stack.findModifier( 0 )->getTypeID() );
    SW_EXPECT_FALSE( stack.removeModifier( 2 ) );
    SW_EXPECT_TRUE( stack.removeModifier( 0 ) );
    SW_EXPECT_EQUAL( 1, stack.getModifierCount() );
    SW_EXPECT_NULL( stack.findModifier( 1 ) );
}

SW_TEST_CASE( MeshModifierStackTest, DisabledModifierIsSkipped )
{
    const MeshModifierMesh source = test::MeshModifierTestUtil::makeBoxMesh( 2 );
    MeshModifierStack      stack;
    const int32            index = stack.addModifier( "push" );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *stack.findModifier( index ), "Amount", 1.0f ) );
    stack.findModifier( index )->setEnabled( false );
    MeshModifierMesh mesh = source;
    stack.apply( mesh, MeshModifierEvaluation{} );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::isBitwiseEqual( source._listPosition, mesh._listPosition ) );
}

SW_TEST_CASE( MeshModifierStackTest, SidecarAndBakedPaths )
{
    SW_EXPECT_EQUAL( string( "game/x/models/rock.modifiers.json" ), MeshModifierStack::makeSidecarPath( "game/x/models/rock.mesh" ) );
    SW_EXPECT_EQUAL( string( "game/x/models/rock_modified.mesh" ), MeshModifierStack::makeBakedMeshPath( "game/x/models/rock.mesh" ) );
}

SW_TEST_CASE( MeshModifierStackTest, WorldSpaceUsesTheReferenceObject )
{
    // 메시는 월드에서 X 로 +10 에 있고, 기준 오브젝트도 +10 에 있다 → 기준 공간에서 보면 메시가 원점에 있어 로컬 공간과 같은 결과다
    const MeshModifierMesh   source = test::MeshModifierTestUtil::makeBoxMesh( 2 );
    unique_ptr<MeshModifier> local  = MeshModifierRegistry::create( "twist" );
    unique_ptr<MeshModifier> world  = MeshModifierRegistry::create( "twist" );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *local, "Angle", 45.0f ) );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *world, "Angle", 45.0f ) );
    world->setWorldSpace( true );
    world->setSpaceReference( "Warp" );

    test::FakeMeshModifierInputs inputs;
    inputs.setObject( "Warp", float4x4::makeTranslation( 10.0f, 0.0f, 0.0f ) );
    MeshModifierEvaluation evaluation{};
    evaluation._meshToWorld = float4x4::makeTranslation( 10.0f, 0.0f, 0.0f );
    evaluation._pInputs     = &inputs;

    MeshModifierMesh localMesh = source;
    MeshModifierMesh worldMesh = source;
    local->apply( localMesh, MeshModifierEvaluation{} );
    world->apply( worldMesh, evaluation );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( localMesh._listPosition, worldMesh._listPosition ) < 1e-4f );

    // 기준을 못 찾으면 월드 원점 기준이라 메시가 축에서 10 떨어져 결과가 달라진다
    world->setSpaceReference( "Missing" );
    MeshModifierMesh offsetMesh = source;
    world->apply( offsetMesh, evaluation );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( localMesh._listPosition, offsetMesh._listPosition ) > 1e-2f );
}

SW_TEST_CASE( MeshModifierStackTest, TimeDrivesAnimatedWave )
{
    const MeshModifierMesh   source = test::MeshModifierTestUtil::makeBoxMesh( 3 );
    unique_ptr<MeshModifier> wave   = MeshModifierRegistry::create( "wave" );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *wave, "Amplitude", 0.2f ) );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *wave, "Speed", 90.0f ) );
    MeshModifierEvaluation atZero{};
    MeshModifierEvaluation atOne{};
    atOne._time             = 1.0f;
    MeshModifierMesh first  = source;
    MeshModifierMesh second = source;
    wave->apply( first, atZero );
    wave->apply( second, atOne );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( first._listPosition, second._listPosition ) > 1e-3f );
}
