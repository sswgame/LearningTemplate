// 묶음(Deformable) 모디파이어의 중첩 - 경로로 찾기, 담은 스택 편집, 두 단계 중첩의 저장 문서 왕복과 적용 결과.
#include "pch.h"

#include "EngineTest/GameFramework/MeshModifiers/MeshModifierTestUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/DeformableModifier.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct DeformableNestingTestInternal
    {
        /** @brief root[0] = 묶음 { twist, 묶음 { bend } }, root[1] = scale 인 스택입니다. */
        static void buildNested( MeshModifierStack& root )
        {
            const int32 outerIndex = root.addModifier( "deformable" );
            (void)root.addModifier( "scale" );
            DeformableModifier* pOuter = DeformableModifier::findFrom( *root.findModifier( outerIndex ) );
            if ( pOuter == nullptr )
                return;
            const int32 twistIndex = pOuter->getStack().addModifier( "twist" );
            const int32 innerIndex = pOuter->getStack().addModifier( "deformable" );
            test::MeshModifierTestUtil::fillParams( *pOuter->getStack().findModifier( twistIndex ), 3u );
            DeformableModifier* pInner = DeformableModifier::findFrom( *pOuter->getStack().findModifier( innerIndex ) );
            if ( pInner == nullptr )
                return;
            const int32 bendIndex = pInner->getStack().addModifier( "bend" );
            test::MeshModifierTestUtil::fillParams( *pInner->getStack().findModifier( bendIndex ), 5u );
        }
    };
} // namespace

SW_TEST_CASE( DeformableNestingTest, FindByPathFollowsGroupsAndRejectsBadPaths )
{
    MeshModifierStack root;
    DeformableNestingTestInternal::buildNested( root );

    SW_EXPECT_TRUE( DeformableModifier::findByPath( root, { 0 } ) == root.findModifier( 0 ) );
    SW_EXPECT_TRUE( DeformableModifier::findByPath( root, { 1 } ) == root.findModifier( 1 ) );
    MeshModifier* pTwist = DeformableModifier::findByPath( root, { 0, 0 } );
    SW_ASSERT_NOT_NULL( pTwist );
    SW_EXPECT_EQUAL( string( "twist" ), pTwist->getTypeID() );
    MeshModifier* pBend = DeformableModifier::findByPath( root, { 0, 1, 0 } );
    SW_ASSERT_NOT_NULL( pBend );
    SW_EXPECT_EQUAL( string( "bend" ), pBend->getTypeID() );

    // 빈 경로 · 범위 밖 · 묶음이 아닌 모디파이어를 뚫고 가는 경로는 못 찾는다
    SW_EXPECT_TRUE( DeformableModifier::findByPath( root, {} ) == nullptr );
    SW_EXPECT_TRUE( DeformableModifier::findByPath( root, { 2 } ) == nullptr );
    SW_EXPECT_TRUE( DeformableModifier::findByPath( root, { -1 } ) == nullptr );
    SW_EXPECT_TRUE( DeformableModifier::findByPath( root, { 0, 5 } ) == nullptr );
    SW_EXPECT_TRUE( DeformableModifier::findByPath( root, { 1, 0 } ) == nullptr );
    SW_EXPECT_TRUE( DeformableModifier::findByPath( root, { 0, 0, 0 } ) == nullptr );
}

SW_TEST_CASE( DeformableNestingTest, OwningStackIsTheStackThatEditsApplyTo )
{
    MeshModifierStack root;
    DeformableNestingTestInternal::buildNested( root );

    SW_EXPECT_TRUE( DeformableModifier::findOwningStack( root, { 1 } ) == &root );
    MeshModifierStack* pOuterStack = DeformableModifier::findOwningStack( root, { 0, 1 } );
    SW_ASSERT_NOT_NULL( pOuterStack );
    SW_EXPECT_TRUE( pOuterStack != &root );
    SW_EXPECT_EQUAL( 2, pOuterStack->getModifierCount() );
    SW_EXPECT_TRUE( DeformableModifier::findOwningStack( root, { 0, 1, 7 } ) == nullptr );

    // 하위 스택을 그 스택으로 직접 고친다 - 위아래를 바꾸면 경로의 뜻도 따라 바뀐다
    SW_ASSERT_TRUE( pOuterStack->moveUp( 1 ) );
    SW_EXPECT_EQUAL( string( "deformable" ), DeformableModifier::findByPath( root, { 0, 0 } )->getTypeID() );
    SW_EXPECT_EQUAL( string( "twist" ), DeformableModifier::findByPath( root, { 0, 1 } )->getTypeID() );
    SW_ASSERT_TRUE( pOuterStack->removeModifier( 0 ) );
    SW_EXPECT_EQUAL( 1, pOuterStack->getModifierCount() );
    SW_EXPECT_TRUE( DeformableModifier::findByPath( root, { 0, 1 } ) == nullptr );
}

SW_TEST_CASE( DeformableNestingTest, TwoLevelNestingSurvivesDocumentRoundTrip )
{
    MeshModifierStack root;
    DeformableNestingTestInternal::buildNested( root );
    const string text = root.makeJSONText( 2 );

    MeshModifierStack reread;
    SW_ASSERT_TRUE( reread.readJSONText( text ) );
    SW_EXPECT_EQUAL( text, reread.makeJSONText( 2 ) );
    MeshModifier* pBend = DeformableModifier::findByPath( reread, { 0, 1, 0 } );
    SW_ASSERT_NOT_NULL( pBend );
    SW_EXPECT_EQUAL( string( "bend" ), pBend->getTypeID() );

    const MeshModifierMesh source = test::MeshModifierTestUtil::makeBoxMesh( 3 );
    MeshModifierMesh       first  = source;
    MeshModifierMesh       second = source;
    root.apply( first, MeshModifierEvaluation{} );
    reread.apply( second, MeshModifierEvaluation{} );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::isBitwiseEqual( first._listPosition, second._listPosition ) );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( first._listPosition, source._listPosition ) > 1e-3f );
}

SW_TEST_CASE( DeformableNestingTest, EditingNestedModifierChangesTheResult )
{
    MeshModifierStack root;
    DeformableNestingTestInternal::buildNested( root );
    const MeshModifierMesh source = test::MeshModifierTestUtil::makeBoxMesh( 3 );
    MeshModifierMesh       before = source;
    root.apply( before, MeshModifierEvaluation{} );

    // 가장 안쪽 bend 를 끄면 결과가 달라지고, 다시 켜면 되돌아온다
    MeshModifier* pBend = DeformableModifier::findByPath( root, { 0, 1, 0 } );
    SW_ASSERT_NOT_NULL( pBend );
    pBend->setEnabled( false );
    MeshModifierMesh disabled = source;
    root.apply( disabled, MeshModifierEvaluation{} );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( before._listPosition, disabled._listPosition ) > 1e-4f );
    pBend->setEnabled( true );
    MeshModifierMesh restored = source;
    root.apply( restored, MeshModifierEvaluation{} );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::isBitwiseEqual( before._listPosition, restored._listPosition ) );
}
