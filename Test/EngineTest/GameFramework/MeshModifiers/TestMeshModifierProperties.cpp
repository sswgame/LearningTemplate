// 메시 모디파이어 공용 속성 테스트 — 레지스트리에 오른 **모든** 모디파이어를 훑는다(새 모디파이어는 이 파일을 고치지 않아도 검사된다).
// 기본값은 항등, 값은 유한, 같은 입력은 같은 출력(병렬 덩어리와 무관), 매개변수 ↔ 저장 문서 왕복.
#include "pch.h"

#include "EngineTest/GameFramework/MeshModifiers/MeshModifierTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct MeshModifierPropertiesTestInternal
    {
        /** @brief 정점이 덩어리 경계(4096)를 넘도록 잘게 나눈 상자입니다(6 면 · 20² 칸 · 6 정점 = 14400 정점). */
        // 등록된 모디파이어마다 세 번씩 두 번 적용한다 — 정점 수가 크면 Debug 에서 등록 수에 비례해 느려진다(20 분할 = 1.4 만 정점 → 15 초). 8 분할 = 2304 정점.
        static constexpr uint32 kSubdivision = 8;

        static vector<const MeshModifierRegistration*> getAll()
        {
            vector<const MeshModifierRegistration*> listRegistration;
            MeshModifierRegistry::getRegistrations( listRegistration );
            return listRegistration;
        }
    };
} // namespace

SW_TEST_CASE( MeshModifierPropertiesTest, RegistryHasTheFirstTwelve )
{
    const utf8* arrID[] = { "bend", "twist", "taper", "stretch", "wave", "ripple", "noise", "melt", "push",
                            "poke", "displace", "lattice", "ffd2", "ffd3", "ffd4", "bubble", "bulge", "cylindrify",
                            "globe", "hump", "radialskew", "skew", "spherify", "squeeze", "scale", "sinuscurve", "rolled",
                            "rubber", "simplemod", "crumple", "dynamicripple", "pageflip", "treebend", "vertnoise", "zstretchnoise", "waving",
                            "pivotadjust", "displacelimits", "paint", "curvedeform", "curvesculpt", "curvesculptlayered", "pathdeform", "worldpathdeform", "bezierattractor",
                            "ropedeform", "conform", "conformmulti", "collisiondeform", "hitdeform", "pointcache", "vertexanim", "deformable", "displacerendertarget",
                            "displacewebcam", "ffd2d", "worldwarp" };
    for ( const utf8* pID : arrID )
    {
        SW_EXPECT_NOT_NULL( MeshModifierRegistry::findRegistration( pID ) );
        const unique_ptr<MeshModifier> modifier = MeshModifierRegistry::create( pID );
        SW_ASSERT_NOT_NULL( modifier.get() );
        SW_EXPECT_EQUAL( string( pID ), modifier->getTypeID() );
    }
    SW_EXPECT_NULL( MeshModifierRegistry::create( "no_such_modifier" ).get() );
}

SW_TEST_CASE( MeshModifierPropertiesTest, DefaultParamsLeaveMeshUnchanged )
{
    const MeshModifierMesh source = test::MeshModifierTestUtil::makeBoxMesh( 4 );
    for ( const MeshModifierRegistration* pRegistration : MeshModifierPropertiesTestInternal::getAll() )
    {
        unique_ptr<MeshModifier> modifier = MeshModifierRegistry::create( pRegistration->_pID );
        SW_ASSERT_NOT_NULL( modifier.get() );
        modifier->fitToBounds( MeshModifierBounds::makeFromPoints( source._listPosition ) );
        MeshModifierMesh mesh = source;
        test::MeshModifierTestUtil::applyOne( *modifier, mesh );
        SW_EXPECT_TRUE_MSG( test::MeshModifierTestUtil::computeMaxDistance( source._listPosition, mesh._listPosition ) < 1e-4f, pRegistration->_pID );
        SW_EXPECT_TRUE_MSG( test::MeshModifierTestUtil::computeMaxDistance( source._listNormal, mesh._listNormal ) < 1e-2f, pRegistration->_pID );
    }
}

SW_TEST_CASE( MeshModifierPropertiesTest, FilledParamsStayFiniteAndDeterministic )
{
    const MeshModifierMesh source = test::MeshModifierTestUtil::makeBoxMesh( MeshModifierPropertiesTestInternal::kSubdivision );
    for ( const MeshModifierRegistration* pRegistration : MeshModifierPropertiesTestInternal::getAll() )
    {
        for ( uint32 salt = 1; salt <= 3; ++salt )
        {
            unique_ptr<MeshModifier> modifier = MeshModifierRegistry::create( pRegistration->_pID );
            SW_ASSERT_NOT_NULL( modifier.get() );
            modifier->fitToBounds( MeshModifierBounds::makeFromPoints( source._listPosition ) );
            test::MeshModifierTestUtil::fillParams( *modifier, salt );

            MeshModifierMesh       first  = source;
            MeshModifierMesh       second = source;
            MeshModifierEvaluation evaluation{};
            evaluation._time = 0.75f;
            modifier->apply( first, evaluation );
            modifier->apply( second, evaluation );
            SW_EXPECT_TRUE_MSG( test::MeshModifierTestUtil::isFinite( first._listPosition ), pRegistration->_pID );
            SW_EXPECT_TRUE_MSG( test::MeshModifierTestUtil::isFinite( first._listNormal ), pRegistration->_pID );
            SW_EXPECT_TRUE_MSG( test::MeshModifierTestUtil::isBitwiseEqual( first._listPosition, second._listPosition ), pRegistration->_pID );
            SW_EXPECT_TRUE_MSG( test::MeshModifierTestUtil::isBitwiseEqual( first._listNormal, second._listNormal ), pRegistration->_pID );
        }
    }
}

SW_TEST_CASE( MeshModifierPropertiesTest, StackDocumentRoundTripsEveryModifier )
{
    const MeshModifierMesh source = test::MeshModifierTestUtil::makeBoxMesh( 3 );
    MeshModifierStack      stack;
    uint32                 salt = 1;
    for ( const MeshModifierRegistration* pRegistration : MeshModifierPropertiesTestInternal::getAll() )
    {
        const int32 index = stack.addModifier( pRegistration->_pID );
        SW_ASSERT_TRUE( index >= 0 );
        MeshModifier* pModifier               = stack.findModifier( index );
        pModifier->getGizmo()._center         = float3{ 0.1f, -0.2f, 0.05f };
        pModifier->getGizmo()._rotationDegree = float3{ 5.0f, 10.0f, -15.0f };
        pModifier->getGizmo()._axisIndex      = salt % 3u;
        pModifier->setEnabled( ( salt % 4u ) != 0 );
        test::MeshModifierTestUtil::fillParams( *pModifier, salt );
        ++salt;
    }
    stack.setRecomputingNormals( true );

    const string      text = stack.makeJSONText( 2 );
    MeshModifierStack reread;
    SW_ASSERT_TRUE( reread.readJSONText( text ) );
    SW_EXPECT_EQUAL( stack.getModifierCount(), reread.getModifierCount() );
    SW_EXPECT_EQUAL( text, reread.makeJSONText( 2 ) );

    MeshModifierMesh             first  = source;
    MeshModifierMesh             second = source;
    const MeshModifierEvaluation evaluation{};
    stack.apply( first, evaluation );
    reread.apply( second, evaluation );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( first._listPosition, second._listPosition ) < 1e-4f );
}

SW_TEST_CASE( MeshModifierPropertiesTest, UnknownTypeIsSkippedOnRead )
{
    const string      text = R"({"version":1,"recomputeNormals":false,"modifiers":[{"type":"no_such_modifier"},{"type":"bend","params":{"Angle":45,"Unknown":3}}]})";
    MeshModifierStack stack;
    SW_ASSERT_TRUE( stack.readJSONText( text ) );
    SW_ASSERT_EQUAL( 1, stack.getModifierCount() );
    SW_EXPECT_EQUAL( string( "bend" ), stack.findModifier( 0 )->getTypeID() );
    uint32 angleIndex = 0;
    SW_ASSERT_TRUE( stack.findModifier( 0 )->tryGetParamIndex( "Angle", angleIndex ) );
    SW_EXPECT_NEAR_EQUAL( 45.0f, stack.findModifier( 0 )->getFloat( angleIndex ), 1e-5f );
}
