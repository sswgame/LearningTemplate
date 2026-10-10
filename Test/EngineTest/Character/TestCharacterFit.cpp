#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/FitOperator.h"
#include "Engine/Character/Fit/FitPartData.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/Fit/FitTables.h"
#include "Engine/Character/Fit/GeometryCut.h"
#include "Engine/Character/Fit/SurfaceBvh.h"
#include "Engine/Character/Fit/SurfaceTransfer.h"

#include "EngineTest/CharacterTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// 장비 피팅(FitSolver) — 조임 · 잘라 내기 · 밀어내기 · 보고, 데이터 표에서 이름으로 고르는 연산, 몸 → 장비 전이.

namespace
{
    struct CharacterFitTestInternal
    {
        static constexpr const utf8* kTablesHead =
            "<FitTables>"
            "  <Layer name='Body' order='0'/>"
            "  <Layer name='Shirt' order='10'/>"
            "  <Layer name='Armor' order='20'/>"
            "  <Layer name='Cloak' order='30'/>"
            "  <Layer name='Bracelet' order='40'/>"
            "  <Region name='Forearm' group='Forearm'/>"
            "  <Region name='LowerArm' bones='lowerarm hand' minWeight='0.5'/>"
            "  <Profile name='Skin' rigidity='0' coverageDistance='0.03'/>"
            "  <Profile name='Cloth' rigidity='0' falloffWidth='0.08' pushDistance='0.004' coverageDistance='0.03'/>"
            "  <Profile name='Rigid' rigidity='1'/>";

        /** @brief 표 머리(겹 · 영역 · 프로필) + 상호작용 줄들로 표를 읽는다. */
        [[nodiscard]] static bool loadTables( const FitSolver& solver, string_view interactionLines, FitTables& outTables )
        {
            string text( kTablesHead );
            text += interactionLines;
            text += "</FitTables>";
            return outTables.loadFromXMLText( text, "test.fit.xml", solver.getOperatorRegistry() );
        }

        static FitPartData makeFitData( const utf8* pLayer, const utf8* pProfile )
        {
            FitPartData data;
            data.setLayerAndProfile( hashed_string( pLayer ), hashed_string( pProfile ) );
            return data;
        }

        static AppearanceGeometry makeCylinder( float32 radius, float32 yMin, float32 yMax, uint32 segmentCount, uint32 ringCount, bool bInward = false )
        {
            test::CharacterTestUtil::CylinderDesc desc;
            desc._radius         = radius;
            desc._yMin           = yMin;
            desc._yMax           = yMax;
            desc._segmentCount   = segmentCount;
            desc._ringCount      = ringCount;
            desc._bInwardNormals = bInward;
            return test::CharacterTestUtil::makeCylinder( desc );
        }

        /** @brief 두꺼운 팔찌 — 바깥벽(바깥을 봄) + 안벽(축을 봄). */
        static AppearanceGeometry makeThickBracelet( float32 innerRadius, float32 outerRadius, float32 yMin, float32 yMax )
        {
            AppearanceGeometry bracelet = makeCylinder( outerRadius, yMin, yMax, 30, 4 );
            GeometryCutUtil::appendGeometry( bracelet, makeCylinder( innerRadius, yMin, yMax, 30, 4, true ) );
            return bracelet;
        }

        /** @brief 높이 @p y 의 고리(정점 열)의 최대 반지름 — 그 높이에 정점이 없으면 -1. */
        static float32 findRingRadius( const AppearanceGeometry& geometry, const FitPartResult& result, float32 y )
        {
            float32 maxRadius = -1.0f;
            for ( uint32 vertex = 0; vertex < geometry.getVertexCount(); ++vertex )
            {
                if ( MathUtil::abs( geometry._listPosition[vertex]._y - y ) > 1.0e-4f )
                    continue;
                const float3 moved = geometry._listPosition[vertex] + result._listVertexDelta[vertex];
                maxRadius          = MathUtil::max( maxRadius, test::CharacterTestUtil::computeRadius( moved ) );
            }
            return maxRadius;
        }

        static uint32 countVisible( const FitPartResult& result )
        {
            uint32 visible = 0;
            for ( uint32 triangle = 0; triangle < result._triangleCount; ++triangle )
            {
                if ( result.isTriangleVisible( triangle ) )
                    ++visible;
            }
            return visible;
        }

        /** @brief 삼각형의 세 정점 y 의 최솟값. */
        static float32 findTriangleMinY( const AppearanceGeometry& geometry, uint32 triangle )
        {
            float32 minY = MathUtil::kMaxFloat;
            for ( uint32 corner = 0; corner < 3; ++corner )
            {
                minY = MathUtil::min( minY, geometry._listPosition[geometry._listIndex[triangle * 3 + corner]]._y );
            }
            return minY;
        }

        /** @brief 사용자 연산 — 안쪽 정점을 법선으로 `amount` 만큼 부풀린다(이름으로 표에서 고르는지 보는 시험용). */
        class InflateOperator final : public IFitOperator
        {
        public:
            const hashed_string& getName() const override
            {
                static const hashed_string s_name( "Inflate" );
                return s_name;
            }
            FitPhase getPhase() const override { return FitPhase::Deform; }
            bool     acceptsArgument( const hashed_string& name ) const override { return name == hashed_string( "amount" ); }
            void     execute( FitSolveState& state, const FitPairContext& pair ) const override
            {
                const FitPartState& inner  = state._listPart[pair._innerPart];
                const float32       amount = pair._pInteraction->findArgument( hashed_string( "amount" ), 0.0f );
                for ( uint32 vertex = 0; vertex < inner._listPosition.size(); ++vertex )
                {
                    const float3 desired = inner._listBindPosition[vertex] + inner._listNormal[vertex] * amount;
                    const float3 delta   = desired - inner._listPosition[vertex];
                    if ( delta.getLengthSquared() > 1.0e-14f )
                        state.addDisplacement( pair._innerPart, vertex, delta );
                }
            }
        };
    };
} // namespace

/**
 * @brief [FitSolverTest] 조임(고리) — 팔찌 고리 아래 소매는 고리 안지름 안으로, 축을 따라 부드럽게, 멀리는 그대로
 */
SW_TEST_CASE( FitSolverTest, ShrinkPullsSleeveInsideAuthoredRing )
{
    using Internal = CharacterFitTestInternal;
    FitSolver solver;
    FitTables tables;
    SW_ASSERT_TRUE( Internal::loadTables( solver, "<Interaction inner='Cloth' outer='Rigid' operator='Shrink'/>", tables ) );

    const CharacterBoneArray bones  = test::CharacterTestUtil::makeArmBones();
    AppearanceGeometry       sleeve = Internal::makeCylinder( 0.06f, 0.0f, 1.0f, 24, 50 );
    test::CharacterTestUtil::skinBySplit( sleeve, 0.5f, 1, 2 );
    const AppearanceGeometry bracelet     = Internal::makeThickBracelet( 0.045f, 0.055f, 0.68f, 0.72f );
    const FitPartData        sleeveData   = Internal::makeFitData( "Shirt", "Cloth" );
    FitPartData              braceletData = Internal::makeFitData( "Bracelet", "Rigid" );
    FitRingDef               ring;
    ring._bone   = hashed_string( "lowerarm" );
    ring._offset = float3( 0.0f, 0.2f, 0.0f ); // lowerarm 은 y = 0.5 → 고리 중심 y = 0.7
    ring._axis   = float3::UnitY;
    ring._radius = 0.045f;
    ring._width  = 0.04f;
    braceletData.addRing( ring );

    FitInput input;
    input._pBindBones = &bones;
    input._listPart.push_back( FitPartInput{ hashed_string( "Sleeve" ), &sleeve, &sleeveData } );
    input._listPart.push_back( FitPartInput{ hashed_string( "Bracelet" ), &bracelet, &braceletData } );
    FitResult result;
    SW_ASSERT_TRUE( solver.solve( tables, input, result ) );
    const FitPartResult& sleeveResult = result._listPart[0];

    // 고리 폭 안(0.68 ~ 0.72)은 고리 안지름 이하.
    for ( float32 y = 0.68f; y <= 0.7201f; y += 0.02f )
    {
        SW_EXPECT_TRUE_MSG( Internal::findRingRadius( sleeve, sleeveResult, y ) <= 0.045f + 1.0e-4f, "sleeve under the bracelet must be inside the ring" );
    }
    // 축을 따라 부드럽게 — 멀어질수록 반지름이 줄지 않고, 고리 하나 사이 차가 작다.
    float32 previousRadius = Internal::findRingRadius( sleeve, sleeveResult, 0.72f );
    for ( float32 y = 0.74f; y <= 0.9001f; y += 0.02f )
    {
        const float32 radius = Internal::findRingRadius( sleeve, sleeveResult, y );
        SW_EXPECT_TRUE( radius >= previousRadius - 1.0e-5f );
        SW_EXPECT_TRUE_MSG( radius - previousRadius <= 0.008f, "falloff along the bone axis must be smooth" );
        previousRadius = radius;
    }
    SW_EXPECT_TRUE( Internal::findRingRadius( sleeve, sleeveResult, 0.74f ) < 0.06f - 1.0e-3f ); // 감쇠 구간도 조금은 오므라든다
    // 멀리는 그대로.
    SW_EXPECT_NEAR_EQUAL( 0.06f, Internal::findRingRadius( sleeve, sleeveResult, 0.3f ), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.06f, Internal::findRingRadius( sleeve, sleeveResult, 0.9f ), 1.0e-5f );
    // 단단한 팔찌는 움직이지 않는다.
    for ( const float3& delta : result._listPart[1]._listVertexDelta )
    {
        SW_EXPECT_TRUE( delta.getLengthSquared() < 1.0e-12f );
    }
}

/**
 * @brief [FitSolverTest] 조임(메시) — 고리가 없으면 팔찌 메시의 안벽까지 당긴다(가장 깊은 면), 축을 따라 감쇠
 */
SW_TEST_CASE( FitSolverTest, ShrinkPullsSleeveInsideRigidInnerSurface )
{
    using Internal = CharacterFitTestInternal;
    FitSolver solver;
    FitTables tables;
    SW_ASSERT_TRUE( Internal::loadTables( solver, "<Interaction inner='Cloth' outer='Rigid' operator='Shrink'/>", tables ) );

    const CharacterBoneArray bones  = test::CharacterTestUtil::makeArmBones();
    AppearanceGeometry       sleeve = Internal::makeCylinder( 0.06f, 0.0f, 1.0f, 24, 50 );
    test::CharacterTestUtil::skinBySplit( sleeve, 0.5f, 1, 2 );
    const AppearanceGeometry bracelet     = Internal::makeThickBracelet( 0.045f, 0.055f, 0.65f, 0.75f );
    const FitPartData        sleeveData   = Internal::makeFitData( "Shirt", "Cloth" );
    const FitPartData        braceletData = Internal::makeFitData( "Bracelet", "Rigid" );

    FitInput input;
    input._pBindBones = &bones;
    input._listPart.push_back( FitPartInput{ hashed_string( "Sleeve" ), &sleeve, &sleeveData } );
    input._listPart.push_back( FitPartInput{ hashed_string( "Bracelet" ), &bracelet, &braceletData } );
    FitResult result;
    SW_ASSERT_TRUE( solver.solve( tables, input, result ) );
    const FitPartResult& sleeveResult = result._listPart[0];

    for ( float32 y = 0.66f; y <= 0.7401f; y += 0.02f )
    {
        SW_EXPECT_TRUE_MSG( Internal::findRingRadius( sleeve, sleeveResult, y ) <= 0.045f, "sleeve under the bracelet must be inside its inner wall" );
    }
    float32 previousRadius = Internal::findRingRadius( sleeve, sleeveResult, 0.74f );
    for ( float32 y = 0.76f; y <= 0.9001f; y += 0.02f )
    {
        const float32 radius = Internal::findRingRadius( sleeve, sleeveResult, y );
        SW_EXPECT_TRUE( radius >= previousRadius - 1.0e-5f );
        previousRadius = radius;
    }
    SW_EXPECT_TRUE( Internal::findRingRadius( sleeve, sleeveResult, 0.76f ) < 0.06f - 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.06f, Internal::findRingRadius( sleeve, sleeveResult, 0.3f ), 1.0e-5f );
}

/**
 * @brief [FitSolverTest] 잘라 내기 — 소매에 완전히 덮인 몸 삼각형만 빠지고, 덮이지 않은 쪽과 맞닿은 한 줄은 남는다
 */
SW_TEST_CASE( FitSolverTest, CutRemovesOnlyCoveredTrianglesAndKeepsBorder )
{
    using Internal = CharacterFitTestInternal;
    FitSolver solver;
    FitTables tables;
    SW_ASSERT_TRUE( Internal::loadTables( solver, "<Interaction inner='Skin' outer='Cloth' operator='Cut'/>", tables ) );

    const AppearanceGeometry body       = Internal::makeCylinder( 0.04f, 0.0f, 1.0f, 24, 50 );
    const AppearanceGeometry sleeve     = Internal::makeCylinder( 0.06f, 0.31f, 1.05f, 30, 37 );
    const FitPartData        bodyData   = Internal::makeFitData( "Body", "Skin" );
    const FitPartData        sleeveData = Internal::makeFitData( "Shirt", "Cloth" );
    FitInput                 input;
    input._listPart.push_back( FitPartInput{ hashed_string( "Body" ), &body, &bodyData } );
    input._listPart.push_back( FitPartInput{ hashed_string( "Sleeve" ), &sleeve, &sleeveData } );
    FitResult result;
    SW_ASSERT_TRUE( solver.solve( tables, input, result ) );
    const FitPartResult& bodyResult = result._listPart[0];

    // 몸 고리는 0.02 간격. 0.32 부터 덮임 → 0.30~0.32 띠는 덮이지 않음, 0.32~0.34 띠는 경계로 남음, 0.34 위로는 빠짐.
    for ( uint32 triangle = 0; triangle < body.getTriangleCount(); ++triangle )
    {
        const float32 minY     = Internal::findTriangleMinY( body, triangle );
        const bool    bVisible = bodyResult.isTriangleVisible( triangle );
        if ( minY < 0.335f )
            SW_EXPECT_TRUE_MSG( bVisible, "uncovered triangles and the border row stay" );
        else
            SW_EXPECT_FALSE_MSG( bVisible, "fully covered triangles away from the border are cut" );
    }
    SW_EXPECT_EQUAL( 17u * 48u, Internal::countVisible( bodyResult ) );
    SW_EXPECT_EQUAL( 33u * 48u, bodyResult._cutTriangleCount );
    // 바깥 소매는 그대로다.
    SW_EXPECT_EQUAL( sleeve.getTriangleCount(), Internal::countVisible( result._listPart[1] ) );
}

/**
 * @brief [FitSolverTest] 조임 뒤에 덮임 — 판정 거리 밖의 소매가 팔찌에 조여 몸에 붙으면 그 자리만 덮여 잘린다
 */
SW_TEST_CASE( FitSolverTest, CoverageIsJudgedAfterShrink )
{
    using Internal = CharacterFitTestInternal;
    FitSolver solver;
    FitTables tables;
    SW_ASSERT_TRUE( Internal::loadTables( solver,
                                          "<Interaction inner='Cloth' outer='Rigid' operator='Shrink' args='falloffWidth=0.04'/>"
                                          "<Interaction inner='Skin' outer='Cloth' operator='Cut' args='distance=0.02'/>",
                                          tables ) );
    const CharacterBoneArray bones = test::CharacterTestUtil::makeArmBones();
    AppearanceGeometry       body  = Internal::makeCylinder( 0.04f, 0.0f, 1.0f, 24, 50 );
    test::CharacterTestUtil::skinBySplit( body, 0.5f, 1, 2 );
    AppearanceGeometry sleeve = Internal::makeCylinder( 0.07f, 0.31f, 1.05f, 30, 37 );
    test::CharacterTestUtil::skinBySplit( sleeve, 0.5f, 1, 2 );
    const AppearanceGeometry bracelet     = Internal::makeThickBracelet( 0.055f, 0.065f, 0.6f, 0.8f );
    const FitPartData        bodyData     = Internal::makeFitData( "Body", "Skin" );
    const FitPartData        sleeveData   = Internal::makeFitData( "Shirt", "Cloth" );
    FitPartData              braceletData = Internal::makeFitData( "Bracelet", "Rigid" );
    FitRingDef               ring;
    ring._bone   = hashed_string( "lowerarm" );
    ring._offset = float3( 0.0f, 0.2f, 0.0f );
    ring._radius = 0.055f;
    ring._width  = 0.2f;
    braceletData.addRing( ring );

    FitInput input;
    input._pBindBones = &bones;
    input._listPart.push_back( FitPartInput{ hashed_string( "Body" ), &body, &bodyData } );
    input._listPart.push_back( FitPartInput{ hashed_string( "Sleeve" ), &sleeve, &sleeveData } );
    input._listPart.push_back( FitPartInput{ hashed_string( "Bracelet" ), &bracelet, &braceletData } );
    FitResult result;
    SW_ASSERT_TRUE( solver.solve( tables, input, result ) );
    const FitPartResult& bodyResult = result._listPart[0];

    uint32 cutNearBracelet = 0;
    uint32 cutFarAway      = 0;
    for ( uint32 triangle = 0; triangle < body.getTriangleCount(); ++triangle )
    {
        if ( bodyResult.isTriangleVisible( triangle ) )
            continue;
        const float32 minY = Internal::findTriangleMinY( body, triangle );
        if ( 0.61f <= minY && minY <= 0.77f )
            ++cutNearBracelet;
        else
            ++cutFarAway;
    }
    SW_EXPECT_TRUE_MSG( cutNearBracelet > 0, "the sleeve squeezed by the bracelet covers the body there" );
    SW_EXPECT_EQUAL( 0u, cutFarAway );
}

/**
 * @brief [FitSolverTest] 밀어내기 — 단단한 갑옷 안에 묻힌 망토를 갑옷 밖으로 민다(밀어내기 거리만큼 띄워)
 */
SW_TEST_CASE( FitSolverTest, PushMovesSoftOuterLayerOutOfRigidInner )
{
    using Internal = CharacterFitTestInternal;
    FitSolver solver;
    FitTables tables;
    SW_ASSERT_TRUE( Internal::loadTables( solver, "<Interaction inner='Rigid' outer='Cloth' operator='Push'/>", tables ) );
    const AppearanceGeometry armor     = Internal::makeCylinder( 0.05f, 0.0f, 1.0f, 24, 50 );
    const AppearanceGeometry cloak     = Internal::makeCylinder( 0.045f, 0.21f, 0.79f, 30, 29 );
    const FitPartData        armorData = Internal::makeFitData( "Armor", "Rigid" );
    const FitPartData        cloakData = Internal::makeFitData( "Cloak", "Cloth" );
    FitInput                 input;
    input._listPart.push_back( FitPartInput{ hashed_string( "Armor" ), &armor, &armorData } );
    input._listPart.push_back( FitPartInput{ hashed_string( "Cloak" ), &cloak, &cloakData } );
    FitResult result;
    SW_ASSERT_TRUE( solver.solve( tables, input, result ) );
    const FitPartResult& cloakResult = result._listPart[1];
    for ( uint32 vertex = 0; vertex < cloak.getVertexCount(); ++vertex )
    {
        const float32 radius = test::CharacterTestUtil::computeRadius( cloak._listPosition[vertex] + cloakResult._listVertexDelta[vertex] );
        // 갑옷은 24 각형이라 면의 가운데가 가장 안쪽이다(0.05 × cos 7.5°).
        SW_EXPECT_TRUE_MSG( radius >= 0.05f * MathUtil::cos( MathUtil::kPi / 24.0f ) + 0.004f - 1.0e-4f, "the cloak must sit outside the armor by the push distance" );
        SW_EXPECT_TRUE_MSG( radius <= 0.05f + 0.004f + 1.0e-3f, "and not much further" );
    }
    for ( const float3& delta : result._listPart[0]._listVertexDelta )
    {
        SW_EXPECT_TRUE( delta.getLengthSquared() < 1.0e-12f );
    }
}

/**
 * @brief [FitSolverTest] 보고 — 단단함끼리 겹치면 고치지 않고 관통으로 보고한다, 떨어져 있으면 보고 없음
 */
SW_TEST_CASE( FitSolverTest, ReportListsRigidAgainstRigidPenetration )
{
    using Internal = CharacterFitTestInternal;
    FitSolver solver;
    FitTables tables;
    SW_ASSERT_TRUE( Internal::loadTables( solver, "<Interaction inner='Rigid' outer='Rigid' operator='Report'/>", tables ) );
    const AppearanceGeometry plate     = test::CharacterTestUtil::makeBox( float3::Zero, float3( 0.1f ) );
    const AppearanceGeometry overlap   = test::CharacterTestUtil::makeBox( float3( 0.15f, 0.0f, 0.0f ), float3( 0.08f ) );
    const AppearanceGeometry separated = test::CharacterTestUtil::makeBox( float3( 0.4f, 0.0f, 0.0f ), float3( 0.08f ) );
    const FitPartData        plateData = Internal::makeFitData( "Armor", "Rigid" );
    const FitPartData        cuffData  = Internal::makeFitData( "Bracelet", "Rigid" );

    FitInput input;
    input._listPart.push_back( FitPartInput{ hashed_string( "Plate" ), &plate, &plateData } );
    input._listPart.push_back( FitPartInput{ hashed_string( "Cuff" ), &overlap, &cuffData } );
    FitResult result;
    SW_ASSERT_TRUE( solver.solve( tables, input, result ) );
    SW_ASSERT_EQUAL( size_t( 1 ), result._report._listPenetration.size() );
    SW_EXPECT_TRUE( result._report._listPenetration[0]._vertexCount > 0 );
    SW_EXPECT_NEAR_EQUAL( 0.02f, result._report._listPenetration[0]._maxDepth, 1.0e-3f );
    SW_EXPECT_FALSE( result._report._listMessage.empty() );
    for ( const float3& delta : result._listPart[1]._listVertexDelta )
    {
        SW_EXPECT_TRUE( delta.getLengthSquared() < 1.0e-12f ); // 보고만 한다
    }

    input._listPart[1]._pGeometry = &separated;
    SW_ASSERT_TRUE( solver.solve( tables, input, result ) );
    SW_EXPECT_TRUE( result._report._listPenetration.empty() );
}

/**
 * @brief [FitSolverTest] 연산은 데이터가 이름으로 고른다 — 등록한 사용자 연산이 표 한 줄로 돌고, 모르는 연산 · 인자는 로드 오류
 */
SW_TEST_CASE( FitSolverTest, OperatorIsChosenByNameFromData )
{
    using Internal = CharacterFitTestInternal;
    FitSolver solver;
    SW_ASSERT_TRUE( solver.getOperatorRegistry().registerOperator( make_unique<Internal::InflateOperator>() ) );
    SW_EXPECT_FALSE( solver.getOperatorRegistry().registerOperator( make_unique<Internal::InflateOperator>() ) ); // 같은 이름은 한 번

    FitTables tables;
    SW_ASSERT_TRUE( Internal::loadTables( solver, "<Interaction inner='Cloth' outer='Rigid' operator='Inflate' args='amount=0.01'/>", tables ) );
    const AppearanceGeometry sleeve     = Internal::makeCylinder( 0.06f, 0.0f, 1.0f, 12, 10 );
    const AppearanceGeometry plate      = test::CharacterTestUtil::makeBox( float3( 1.0f, 0.0f, 0.0f ), float3( 0.1f ) );
    const FitPartData        sleeveData = Internal::makeFitData( "Shirt", "Cloth" );
    const FitPartData        plateData  = Internal::makeFitData( "Armor", "Rigid" );
    FitInput                 input;
    input._listPart.push_back( FitPartInput{ hashed_string( "Sleeve" ), &sleeve, &sleeveData } );
    input._listPart.push_back( FitPartInput{ hashed_string( "Plate" ), &plate, &plateData } );
    FitResult result;
    SW_ASSERT_TRUE( solver.solve( tables, input, result ) );
    for ( const float3& delta : result._listPart[0]._listVertexDelta )
    {
        SW_EXPECT_NEAR_EQUAL( 0.01f, delta.getLength(), 1.0e-4f );
    }

    test::ScopedLogCollector logs;
    SW_TEST_DEFENSIVE_SCOPE( "unknown fit operator and argument names are load errors" );
    FitTables broken;
    SW_EXPECT_FALSE( Internal::loadTables( solver, "<Interaction inner='Cloth' outer='Rigid' operator='Explode'/>", broken ) );
    SW_EXPECT_TRUE( logs.countContaining( "unknown operator 'Explode'" ) > 0 );
    SW_EXPECT_FALSE( Internal::loadTables( solver, "<Interaction inner='Cloth' outer='Rigid' operator='Shrink' args='speed=2'/>", broken ) );
    SW_EXPECT_TRUE( logs.countContaining( "has no argument 'speed'" ) > 0 );
    SW_EXPECT_FALSE( Internal::loadTables( solver, "<Interaction inner='Cloth' outer='Metal' operator='Shrink'/>", broken ) );
    SW_EXPECT_TRUE( logs.countContaining( "unknown outer profile 'Metal'" ) > 0 );
}

/**
 * @brief [FitSolverTest] 부품 피팅 데이터 — 모르는 겹 · 프로필 · 영역은 로드 오류, 숨김 영역은 경계 없이 안쪽 삼각형을 뺀다
 */
SW_TEST_CASE( FitSolverTest, HiddenRegionRemovesInnerTriangles )
{
    using Internal = CharacterFitTestInternal;
    FitSolver solver;
    FitTables tables;
    SW_ASSERT_TRUE( Internal::loadTables( solver, "", tables ) );

    FitPartData glove;
    SW_ASSERT_TRUE( glove.loadFromXMLText( "<PartFit layer='Bracelet' profile='Rigid'><Hide region='LowerArm'/></PartFit>", "glove.partfit.xml", tables ) );
    {
        test::ScopedLogCollector logs;
        SW_TEST_DEFENSIVE_SCOPE( "unknown names in part fit data are load errors" );
        FitPartData broken;
        SW_EXPECT_FALSE( broken.loadFromXMLText( "<PartFit layer='Hat' profile='Rigid'/>", "broken.partfit.xml", tables ) );
        SW_EXPECT_FALSE( broken.loadFromXMLText( "<PartFit layer='Bracelet' profile='Rigid'><Hide region='Tail'/></PartFit>", "broken.partfit.xml", tables ) );
        SW_EXPECT_TRUE( logs.countContaining( "unknown region 'Tail'" ) > 0 );
    }

    const CharacterBoneArray bones = test::CharacterTestUtil::makeArmBones();
    AppearanceGeometry       body  = Internal::makeCylinder( 0.04f, 0.0f, 1.0f, 24, 50 );
    test::CharacterTestUtil::skinBySplit( body, 0.5f, 1, 2 );
    const AppearanceGeometry gloveMesh = Internal::makeCylinder( 0.06f, 0.5f, 1.0f, 24, 10 );
    const FitPartData        bodyData  = Internal::makeFitData( "Body", "Skin" );
    FitInput                 input;
    input._pBindBones = &bones;
    input._listPart.push_back( FitPartInput{ hashed_string( "Body" ), &body, &bodyData } );
    input._listPart.push_back( FitPartInput{ hashed_string( "Glove" ), &gloveMesh, &glove } );
    FitResult result;
    SW_ASSERT_TRUE( solver.solve( tables, input, result ) );
    for ( uint32 triangle = 0; triangle < body.getTriangleCount(); ++triangle )
    {
        const bool bAllLower = Internal::findTriangleMinY( body, triangle ) >= 0.5f - 1.0e-5f;
        SW_EXPECT_EQUAL( bAllLower == false, result._listPart[0].isTriangleVisible( triangle ) );
    }
}

/**
 * @brief [FitSolverTest] 손 보정 조각 — 모프가 빈 조각은 늘, 체형 모프 조각은 그 가중치만큼 더한다
 */
SW_TEST_CASE( FitSolverTest, CorrectiveDeltasFollowMorphWeight )
{
    using Internal = CharacterFitTestInternal;
    FitSolver solver;
    FitTables tables;
    SW_ASSERT_TRUE( Internal::loadTables( solver, "", tables ) );
    FitPartData data;
    SW_ASSERT_TRUE( data.loadFromXMLText( "<PartFit layer='Shirt' profile='Cloth'>"
                                          "  <Corrective><Delta vertex='0' offset='0 0.01 0'/></Corrective>"
                                          "  <Corrective morph='Heavy'><Delta vertex='1' offset='0 0 0.02'/></Corrective>"
                                          "</PartFit>",
                                          "shirt.partfit.xml", tables ) );
    const AppearanceGeometry shirt = Internal::makeCylinder( 0.06f, 0.0f, 1.0f, 12, 4 );
    string                   error;
    SW_EXPECT_TRUE( data.validate( nullptr, &shirt, &error ) );

    FitInput input;
    input._listPart.push_back( FitPartInput{ hashed_string( "Shirt" ), &shirt, &data } );
    input._listMorphWeight.push_back( BodyMorphWeight{ hashed_string( "Heavy" ), 0.5f } );
    FitResult result;
    SW_ASSERT_TRUE( solver.solve( tables, input, result ) );
    SW_EXPECT_NEAR_EQUAL( 0.01f, result._listPart[0]._listVertexDelta[0]._y, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.01f, result._listPart[0]._listVertexDelta[1]._z, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, result._listPart[0]._listVertexDelta[2].getLength(), 1.0e-6f );
}

/**
 * @brief [SurfaceTransferTest] 몸 → 장비 — 가장 가까운 삼각형으로 묶어 체형 모프 델타와 스킨 가중치를 옮긴다
 */
SW_TEST_CASE( SurfaceTransferTest, TransfersBodyMorphAndSkinWeightsToGarment )
{
    using Internal          = CharacterFitTestInternal;
    AppearanceGeometry body = Internal::makeCylinder( 0.04f, 0.0f, 1.0f, 24, 50 );
    test::CharacterTestUtil::skinBySplit( body, 0.5f, 1, 2 );
    GeometryMorphTarget heavy;
    heavy._name = hashed_string( "Heavy" );
    for ( const float3& normal : body._listNormal )
    {
        heavy._listPositionDelta.push_back( float3( normal._x, 0.0f, normal._z ) * 0.01f );
    }
    body._listMorph.push_back( heavy );
    AppearanceGeometry garment = Internal::makeCylinder( 0.05f, 0.1f, 0.9f, 30, 20 );

    SurfaceBvh bodySurface;
    bodySurface.addSurface( 0, body._listPosition, body._listIndex );
    bodySurface.build();
    vector<SurfaceBinding> listBinding;
    SurfaceTransferUtil::bindPoints( body, bodySurface, garment._listPosition, 0.05f, listBinding );
    SurfaceTransferUtil::transferMorphs( body, listBinding, garment );
    SurfaceTransferUtil::transferSkinWeights( body, listBinding, garment );

    const GeometryMorphTarget* pTransferred = garment.findMorph( hashed_string( "Heavy" ) );
    SW_ASSERT_NOT_NULL( pTransferred );
    SW_ASSERT_TRUE( garment.isSkinned() );
    for ( uint32 vertex = 0; vertex < garment.getVertexCount(); ++vertex )
    {
        SW_EXPECT_TRUE( listBinding[vertex]._bBound == SW_TRUE );
        SW_EXPECT_NEAR_EQUAL( 0.01f, pTransferred->_listPositionDelta[vertex].getLength(), 5.0e-4f );
        SW_EXPECT_NEAR_EQUAL( 0.01f, listBinding[vertex]._normalOffset, 5.0e-4f );
        const float32 y = garment._listPosition[vertex]._y;
        if ( y > 0.55f )
            SW_EXPECT_EQUAL( int32( 2 ), garment._listSkin[vertex].findDominantJoint() );
        if ( y < 0.45f )
            SW_EXPECT_EQUAL( int32( 1 ), garment._listSkin[vertex].findDominantJoint() );
        SW_EXPECT_NEAR_EQUAL( 1.0f, garment._listSkin[vertex]._arrWeight[0] + garment._listSkin[vertex]._arrWeight[1] + garment._listSkin[vertex]._arrWeight[2] + garment._listSkin[vertex]._arrWeight[3],
                              1.0e-5f );
    }
    // 묶음은 모프를 건 몸을 따라간다 — 몸을 부풀리면 묶인 점도 같이.
    BodyShapeUtil::applyMorphs( body, vector<BodyMorphWeight>{
                                          BodyMorphWeight{ hashed_string( "Heavy" ), 1.0f }
    } );
    const float3 movedPoint = SurfaceTransferUtil::evaluatePoint( body, listBinding[0] );
    SW_EXPECT_NEAR_EQUAL( 0.06f, test::CharacterTestUtil::computeRadius( movedPoint ), 5.0e-4f );
}
