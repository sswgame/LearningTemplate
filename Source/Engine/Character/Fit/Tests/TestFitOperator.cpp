#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/FitOperator.h"
#include "Engine/Character/Fit/FitPartDef.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/Fit/FitTables.h"

using namespace sw;

// 피팅 연산(FitOperator) — 조임 · 밀어내기 · 잘라 내기 · 보고를 풀이 상태에 직접 걸어 변위와 표시를 본다. 디바이스 없음(nogpu).

namespace
{
    /** @brief 이름과 단계만 정하는 연산입니다(레지스트리 규칙을 볼 때 씁니다). */
    class FitOperatorTestCustomOperator final : public IFitOperator
    {
    public:
        explicit FitOperatorTestCustomOperator( const hashed_string& name )
            : _name{ name }
        {
        }

        const hashed_string& getName() const override { return _name; }
        FitPhase             getPhase() const override { return FitPhase::Validate; }
        bool                 acceptsArgument( const hashed_string& ) const override { return false; }
        void                 execute( FitSolveState&, const FitPairContext& ) const override {}

    private:
        hashed_string _name;
    };

    /** @brief 안쪽 부품(0)과 바깥 부품(1)의 풀이 상태입니다. 형상 · 프로필 · 고리를 채우고 `build` 를 부른 뒤 연산을 겁니다. 겹마다 부품 하나입니다. */
    struct FitOperatorTestScene
    {
        AppearanceGeometry  _innerGeometry{};
        AppearanceGeometry  _outerGeometry{};
        FitPartDef          _innerFit{};
        FitPartDef          _outerFit{};
        FitPartInput        _innerInput{};
        FitPartInput        _outerInput{};
        FitProfileDef       _innerProfile{};
        FitProfileDef       _outerProfile{};
        FitInteractionDef   _interaction{};
        FitReport           _report{};
        FitSolveState       _state{};
        FitPairContext      _pair{};
        FitOperatorRegistry _registry{};

        void build()
        {
            _innerInput = FitPartInput{ hashed_string( "Inner" ), &_innerGeometry, &_innerFit };
            _outerInput = FitPartInput{ hashed_string( "Outer" ), &_outerGeometry, &_outerFit };
            _state._listPart.resize( 2 );
            _state._listLayerSurface.resize( 2 );
            buildPart( 0, _innerGeometry, _innerInput, _innerProfile );
            buildPart( 1, _outerGeometry, _outerInput, _outerProfile );
            _state._pReport     = &_report;
            _pair._pInteraction = &_interaction;
            _pair._innerPart    = 0;
            _pair._outerPart    = 1;
            _registry.registerDefaultOperators();
        }

        void addArgument( const utf8* pName, float32 value )
        {
            FitArgument argument;
            argument._name  = hashed_string( pName );
            argument._value = value;
            _interaction._listArgument.push_back( argument );
        }

        void run( const utf8* pOperatorName )
        {
            const IFitOperator* pOperator = _registry.findOperator( hashed_string( pOperatorName ) );
            SW_REQUIRE_NOT_NULL( pOperator );
            pOperator->execute( _state, _pair );
        }

        /** @brief 쌓인 변위를 위치에 적용하고 비웁니다(풀이의 야코비 반복 한 번과 같습니다). */
        void applyDisplacementToInner()
        {
            FitPartState& inner = _state._listPart[0];
            for ( size_t vertex = 0; vertex < inner._listPosition.size(); ++vertex )
            {
                inner._listPosition[vertex] += inner._listDisplacement[vertex];
                inner._listDisplacement[vertex]      = float3::Zero;
                inner._listDisplacementCount[vertex] = 0;
            }
        }

    private:
        void buildPart( uint32 partIndex, const AppearanceGeometry& geometry, const FitPartInput& input, const FitProfileDef& profile )
        {
            FitPartState& part     = _state._listPart[partIndex];
            part._listBindPosition = geometry._listPosition;
            part._listPosition     = geometry._listPosition;
            part._listNormal       = geometry._listNormal;
            part._listDisplacement.assign( geometry.getVertexCount(), float3::Zero );
            part._listDisplacementCount.assign( geometry.getVertexCount(), 0u );
            part._listCovered.assign( geometry.getTriangleCount(), static_cast<uint8>( SW_FALSE ) );
            part._listFalloffAxis.assign( geometry.getVertexCount(), float3::Zero );
            part._pInput        = &input;
            part._pProfile      = &profile;
            part._layerIndex    = partIndex;
            SurfaceBVH& surface = _state._listLayerSurface[partIndex];
            part._surfaceIndex  = surface.addSurface( static_cast<uint16>( partIndex ), part._listPosition, geometry._listIndex );
            surface.build();
        }
    };

    struct FitOperatorTestUtil
    {
        static constexpr float32 kTolerance = 1.0e-5f;

        /** @brief 주어진 위치의 정점들입니다(법선 +z, 삼각형 없음). */
        static AppearanceGeometry makePoints( std::initializer_list<float3> listPosition )
        {
            AppearanceGeometry geometry;
            for ( const float3& position : listPosition )
            {
                geometry._listPosition.push_back( position );
                geometry._listNormal.push_back( float3::UnitZ );
            }
            return geometry;
        }

        /** @brief 평면 z 위의 사각형을 @p inoutGeometry 에 더합니다(면 법선 +z). */
        static void addQuad( AppearanceGeometry& inoutGeometry, float32 minimum, float32 maximum, float32 z )
        {
            const uint32 base = inoutGeometry.getVertexCount();
            inoutGeometry._listPosition.insert( inoutGeometry._listPosition.end(), {
                                                                                       float3{minimum, minimum, z},
                                                                                       float3{maximum, minimum, z},
                                                                                       float3{maximum, maximum, z},
                                                                                       float3{minimum, maximum, z}
            } );
            inoutGeometry._listNormal.insert( inoutGeometry._listNormal.end(), 4, float3::UnitZ );
            inoutGeometry._listIndex.insert( inoutGeometry._listIndex.end(), { base, base + 1u, base + 2u, base, base + 2u, base + 3u } );
        }

        /** @brief 안쪽 부품 정점 @p vertex 에 변위가 쌓이지 않았는지 봅니다. */
        static void expectUntouched( const FitPartState& part, uint32 vertex ) { SW_EXPECT_EQUAL( 0u, part._listDisplacementCount[vertex] ); }

        static void expectDisplacement( const FitPartState& part, uint32 vertex, const float3& expected )
        {
            SW_EXPECT_EQUAL( 1u, part._listDisplacementCount[vertex] );
            SW_EXPECT_NEAR( expected._x, part._listDisplacement[vertex]._x, kTolerance );
            SW_EXPECT_NEAR( expected._y, part._listDisplacement[vertex]._y, kTolerance );
            SW_EXPECT_NEAR( expected._z, part._listDisplacement[vertex]._z, kTolerance );
        }

        /** @brief 축 Y, 원점, 반지름 0.1, 폭 0.02 의 고리입니다. */
        static FitRingDef makeRing()
        {
            FitRingDef ring;
            ring._radius = 0.1f;
            ring._width  = 0.02f;
            return ring;
        }
    };
} // namespace

/**
 * @brief [FitOperatorTest] 기본 연산 넷의 이름 · 단계 · 받는 인자가 표와 맞는다(모르는 인자는 표를 읽을 때 오류의 근거다)
 */
SW_TEST_CASE( FitOperatorTest, DefaultOperatorsDeclarePhaseAndArguments )
{
    FitOperatorRegistry registry;
    SW_EXPECT_EQUAL( 0u, registry.getOperatorCount() );
    registry.registerDefaultOperators();
    SW_EXPECT_EQUAL( 4u, registry.getOperatorCount() );

    const IFitOperator* pShrink = registry.findOperator( hashed_string( "Shrink" ) );
    const IFitOperator* pPush   = registry.findOperator( hashed_string( "Push" ) );
    const IFitOperator* pCut    = registry.findOperator( hashed_string( "Cut" ) );
    const IFitOperator* pReport = registry.findOperator( hashed_string( "Report" ) );
    SW_REQUIRE_NOT_NULL( pShrink );
    SW_REQUIRE_NOT_NULL( pPush );
    SW_REQUIRE_NOT_NULL( pCut );
    SW_REQUIRE_NOT_NULL( pReport );
    SW_EXPECT_NULL( registry.findOperator( hashed_string( "Missing" ) ) );

    SW_EXPECT_TRUE( pShrink->getPhase() == FitPhase::Deform );
    SW_EXPECT_TRUE( pPush->getPhase() == FitPhase::Deform );
    SW_EXPECT_TRUE( pCut->getPhase() == FitPhase::Coverage );
    SW_EXPECT_TRUE( pReport->getPhase() == FitPhase::Validate );
    SW_EXPECT_TRUE( pShrink->getName() == hashed_string( "Shrink" ) );

    for ( const utf8* pName : { "strength", "margin", "searchDistance", "falloffWidth" } )
    {
        SW_EXPECT_TRUE( pShrink->acceptsArgument( hashed_string( pName ) ) );
    }
    SW_EXPECT_FALSE( pShrink->acceptsArgument( hashed_string( "distance" ) ) );
    for ( const utf8* pName : { "strength", "distance", "searchDistance", "falloffWidth" } )
    {
        SW_EXPECT_TRUE( pPush->acceptsArgument( hashed_string( pName ) ) );
    }
    SW_EXPECT_FALSE( pPush->acceptsArgument( hashed_string( "margin" ) ) );
    SW_EXPECT_TRUE( pCut->acceptsArgument( hashed_string( "distance" ) ) );
    SW_EXPECT_FALSE( pCut->acceptsArgument( hashed_string( "strength" ) ) );
    SW_EXPECT_TRUE( pReport->acceptsArgument( hashed_string( "tolerance" ) ) );
    SW_EXPECT_TRUE( pReport->acceptsArgument( hashed_string( "searchDistance" ) ) );
    SW_EXPECT_FALSE( pReport->acceptsArgument( hashed_string( "distance" ) ) );
}

/**
 * @brief [FitOperatorTest] 같은 이름은 먼저 것을 두고 거절하고, 빈 이름 · 널도 올리지 않는다
 */
SW_TEST_CASE( FitOperatorTest, RegistryRejectsDuplicateEmptyAndNull )
{
    SW_EXPECT_LOG( "Registration.DuplicateName" );
    SW_EXPECT_LOG( "Registration.EmptyName" );
    FitOperatorRegistry registry;
    registry.registerDefaultOperators();

    SW_EXPECT_FALSE( registry.registerOperator( make_unique<FitOperatorTestCustomOperator>( hashed_string( "Shrink" ) ) ) );
    SW_EXPECT_EQUAL( 4u, registry.getOperatorCount() );
    SW_EXPECT_TRUE( registry.findOperator( hashed_string( "Shrink" ) )->getPhase() == FitPhase::Deform ); // 사용자 연산은 Validate 다

    SW_EXPECT_FALSE( registry.registerOperator( make_unique<FitOperatorTestCustomOperator>( hashed_string{} ) ) );
    SW_EXPECT_FALSE( registry.registerOperator( unique_ptr<IFitOperator>{} ) );
    SW_EXPECT_EQUAL( 4u, registry.getOperatorCount() );

    SW_EXPECT_TRUE( registry.registerOperator( make_unique<FitOperatorTestCustomOperator>( hashed_string( "Custom" ) ) ) );
    SW_EXPECT_EQUAL( 5u, registry.getOperatorCount() );
    SW_EXPECT_NOT_NULL( registry.findOperator( hashed_string( "Custom" ) ) );
}

/**
 * @brief [FitOperatorTest] 고리 조임은 반지름 − 여유까지 당기고, 고리 안 · 판정 거리 밖 · 감쇠 폭 밖은 건드리지 않으며, 폭 밖 감쇠 구간은 그만큼 덜 당긴다
 */
SW_TEST_CASE( FitOperatorTest, ShrinkRingPullsToTargetRadiusWithFalloff )
{
    using Util = FitOperatorTestUtil;
    FitOperatorTestScene scene;
    scene._innerGeometry = Util::makePoints( {
        float3{0.12f,   0.0f, 0.0f}, // 폭 안: 목표 반지름 0.098 까지
        float3{0.05f,   0.0f, 0.0f}, // 이미 고리 안
        float3{ 0.2f,   0.0f, 0.0f}, // 판정 거리(0.1 + 0.05) 밖
        float3{0.12f, 0.035f, 0.0f}, // 폭 밖 0.025 = 감쇠 폭의 절반: 가중치 0.5
        float3{0.12f,   0.1f, 0.0f}, // 감쇠 폭 밖
    } );
    scene._outerFit.addRing( Util::makeRing() );
    scene.build();
    scene.run( "Shrink" );

    const FitPartState& inner = scene._state._listPart[0];
    Util::expectDisplacement( inner, 0, float3{ -0.022f, 0.0f, 0.0f } );
    Util::expectUntouched( inner, 1 );
    Util::expectUntouched( inner, 2 );
    Util::expectDisplacement( inner, 3, float3{ -0.011f, 0.0f, 0.0f } );
    Util::expectUntouched( inner, 4 );
}

/**
 * @brief [FitOperatorTest] 고리 조임은 바인드 위치에서 목표를 정해 반복해도 같은 곳에 멈춘다(변위가 더 쌓이지 않는다)
 */
SW_TEST_CASE( FitOperatorTest, ShrinkRingIsIdempotentAcrossIterations )
{
    using Util = FitOperatorTestUtil;
    FitOperatorTestScene scene;
    scene._innerGeometry = Util::makePoints( {
        float3{ 0.12f, 0.0f, 0.0f }
    } );
    scene._outerFit.addRing( Util::makeRing() );
    scene.build();

    scene.run( "Shrink" );
    SW_EXPECT_EQUAL( 1u, scene._state._listPart[0]._listDisplacementCount[0] );
    scene.applyDisplacementToInner();
    scene.run( "Shrink" );
    Util::expectUntouched( scene._state._listPart[0], 0 );
    SW_EXPECT_NEAR( 0.098f, scene._state._listPart[0]._listPosition[0]._x, Util::kTolerance );
}

/**
 * @brief [FitOperatorTest] 조임 세기는 부드러움(1 − 단단함)을 곱하고, 다 단단하면 연산을 걸지 않으며, 상호작용 인자가 프로필 값을 덮는다
 */
SW_TEST_CASE( FitOperatorTest, ShrinkScalesWithSoftnessAndArguments )
{
    using Util = FitOperatorTestUtil;
    {
        FitOperatorTestScene scene;
        scene._innerGeometry          = Util::makePoints( {
            float3{ 0.12f, 0.0f, 0.0f }
        } );
        scene._innerProfile._rigidity = 0.5f;
        scene._outerFit.addRing( Util::makeRing() );
        scene.build();
        scene.run( "Shrink" );
        Util::expectDisplacement( scene._state._listPart[0], 0, float3{ -0.011f, 0.0f, 0.0f } ); // 0.022 의 절반
    }
    {
        FitOperatorTestScene scene;
        scene._innerGeometry          = Util::makePoints( {
            float3{ 0.12f, 0.0f, 0.0f }
        } );
        scene._innerProfile._rigidity = 1.0f;
        scene._outerFit.addRing( Util::makeRing() );
        scene.build();
        scene.run( "Shrink" );
        Util::expectUntouched( scene._state._listPart[0], 0 );
    }
    {
        FitOperatorTestScene scene;
        scene._innerGeometry = Util::makePoints( {
            float3{ 0.12f, 0.0f, 0.0f }
        } );
        scene._outerFit.addRing( Util::makeRing() );
        scene.addArgument( "margin", 0.01f );
        scene.addArgument( "strength", 0.5f );
        scene.build();
        scene.run( "Shrink" );
        Util::expectDisplacement( scene._state._listPart[0], 0, float3{ -0.015f, 0.0f, 0.0f } ); // 0.12 → 0.09 의 절반
    }
}

/**
 * @brief [FitOperatorTest] 고리의 중심과 축은 본의 바인드 변환을 따른다. 없는 본은 원점 기준이다
 */
SW_TEST_CASE( FitOperatorTest, ShrinkRingFollowsBone )
{
    using Util = FitOperatorTestUtil;
    CharacterBoneArray bones;
    SW_REQUIRE_TRUE( bones.addBone( hashed_string( "arm" ), -1, float4x4::makeTranslation( 0.0f, 1.0f, 0.0f ) ) == 0 );

    FitOperatorTestScene scene;
    scene._innerGeometry = Util::makePoints( {
        float3{0.12f, 1.0f, 0.0f}, // 본 높이의 고리에 걸린다
        float3{0.12f, 0.0f, 0.0f}, // 고리에서 축으로 멀다
    } );
    FitRingDef ring      = Util::makeRing();
    ring._bone           = hashed_string( "arm" );
    scene._outerFit.addRing( ring );
    scene.build();
    scene._state._pBindBones = &bones;
    scene.run( "Shrink" );
    Util::expectDisplacement( scene._state._listPart[0], 0, float3{ -0.022f, 0.0f, 0.0f } );
    Util::expectUntouched( scene._state._listPart[0], 1 );
}

/**
 * @brief [FitOperatorTest] 고리가 없으면 표면 조임이다 — 바깥 겹을 지나는 가장 깊은 면 + 여유만큼 법선 반대로 당긴다
 */
SW_TEST_CASE( FitOperatorTest, ShrinkSurfaceUsesDeepestHit )
{
    using Util = FitOperatorTestUtil;
    FitOperatorTestScene scene;
    scene._innerGeometry = Util::makePoints( {
        float3{ 0.002f, -0.003f, 0.02f }
    } );
    Util::addQuad( scene._outerGeometry, -0.01f, 0.01f, 0.01f );
    Util::addQuad( scene._outerGeometry, -0.01f, 0.01f, -0.01f ); // 더 깊은 안면
    scene.build();
    scene.run( "Shrink" );
    Util::expectDisplacement( scene._state._listPart[0], 0, float3{ 0.0f, 0.0f, -0.032f } ); // 0.03 + 여유 0.002
}

/**
 * @brief [FitOperatorTest] 표면 조임의 감쇠 — 폭이 0 이면 맞은 정점만 움직이고, 있으면 이웃이 거리만큼 덜 움직이며, 본 축이 있으면 축 방향 거리만 센다
 */
SW_TEST_CASE( FitOperatorTest, ShrinkSurfaceSpreadsWithFalloff )
{
    using Util = FitOperatorTestUtil;
    // 정점 1 은 사각형 밖이라 광선이 빗나가고, 정점 0 과 0.025 떨어졌다(기본 감쇠 폭 0.05 의 절반).
    const AppearanceGeometry points = Util::makePoints( {
        float3{0.002f, -0.003f, 0.02f},
        float3{0.027f, -0.003f, 0.02f},
    } );
    {
        FitOperatorTestScene scene;
        scene._innerGeometry = points;
        Util::addQuad( scene._outerGeometry, -0.01f, 0.01f, 0.01f );
        scene.addArgument( "falloffWidth", 0.0f );
        scene.build();
        scene.run( "Shrink" );
        Util::expectDisplacement( scene._state._listPart[0], 0, float3{ 0.0f, 0.0f, -0.012f } );
        Util::expectUntouched( scene._state._listPart[0], 1 );
    }
    {
        FitOperatorTestScene scene;
        scene._innerGeometry = points;
        Util::addQuad( scene._outerGeometry, -0.01f, 0.01f, 0.01f );
        scene.build();
        scene.run( "Shrink" );
        Util::expectDisplacement( scene._state._listPart[0], 1, float3{ 0.0f, 0.0f, -0.006f } ); // 가중치 0.5
    }
    {
        FitOperatorTestScene scene;
        scene._innerGeometry = points;
        Util::addQuad( scene._outerGeometry, -0.01f, 0.01f, 0.01f );
        scene.build();
        scene._state._listPart[0]._listFalloffAxis[1] = float3::UnitZ; // 축 방향 거리는 0 이라 감쇠가 없다
        scene.run( "Shrink" );
        Util::expectDisplacement( scene._state._listPart[0], 1, float3{ 0.0f, 0.0f, -0.012f } );
    }
}

/**
 * @brief [FitOperatorTest] 밀어내기 — 안쪽 표면 뒤에 묻힌 정점은 깊이 + 거리만큼, 거리보다 가까운 정점은 모자란 만큼 밀고, 충분히 떨어진 정점은 두며, 다 단단하면 걸지 않는다
 */
SW_TEST_CASE( FitOperatorTest, PushMovesBuriedAndNearVertices )
{
    using Util                      = FitOperatorTestUtil;
    const AppearanceGeometry points = Util::makePoints( {
        float3{0.002f, -0.003f, -0.01f}, // 묻힘
        float3{0.002f, -0.003f, 0.001f}, // 앞면에 너무 가까움
        float3{0.002f, -0.003f,  0.01f}, // 충분히 떨어짐
    } );
    {
        FitOperatorTestScene scene;
        scene._outerGeometry = points;
        Util::addQuad( scene._innerGeometry, -0.05f, 0.05f, 0.0f );
        scene.addArgument( "falloffWidth", 0.0f );
        scene.build();
        scene.run( "Push" );
        const FitPartState& outer = scene._state._listPart[1];
        Util::expectDisplacement( outer, 0, float3{ 0.0f, 0.0f, 0.014f } ); // 0.01 + 기본 거리 0.004
        Util::expectDisplacement( outer, 1, float3{ 0.0f, 0.0f, 0.003f } );
        Util::expectUntouched( outer, 2 );
    }
    {
        FitOperatorTestScene scene;
        scene._outerGeometry = points;
        Util::addQuad( scene._innerGeometry, -0.05f, 0.05f, 0.0f );
        scene.addArgument( "falloffWidth", 0.0f );
        scene.addArgument( "distance", 0.006f );
        scene.addArgument( "strength", 0.5f );
        scene.build();
        scene.run( "Push" );
        Util::expectDisplacement( scene._state._listPart[1], 0, float3{ 0.0f, 0.0f, 0.008f } ); // ( 0.01 + 0.006 ) × 0.5
    }
    {
        FitOperatorTestScene scene;
        scene._outerGeometry = points;
        Util::addQuad( scene._innerGeometry, -0.05f, 0.05f, 0.0f );
        scene._outerProfile._rigidity = 1.0f;
        scene.build();
        scene.run( "Push" );
        Util::expectUntouched( scene._state._listPart[1], 0 );
    }
}

/**
 * @brief [FitOperatorTest] 잘라 내기는 세 정점이 모두 덮인 삼각형만 표시한다. 판정 거리 밖이면 덮이지 않았고, 인자가 프로필의 거리를 덮는다
 */
SW_TEST_CASE( FitOperatorTest, CutMarksOnlyFullyCoveredTriangles )
{
    using Util = FitOperatorTestUtil;
    // 안쪽은 x 로 늘어선 사각형 둘(열 x = 0, 0.1, 0.2)이고, 바깥은 열 0 과 0.1 만 덮는다.
    const auto makeInner = []()
    {
        AppearanceGeometry geometry;
        for ( uint32 column = 0; column <= 2u; ++column )
        {
            for ( uint32 row = 0; row <= 1u; ++row )
            {
                geometry._listPosition.push_back( float3{ 0.1f * static_cast<float32>( column ), 0.1f * static_cast<float32>( row ), 0.0f } );
                geometry._listNormal.push_back( float3::UnitZ );
            }
        }
        for ( uint32 quad = 0; quad < 2u; ++quad )
        {
            const uint32 corner = quad * 2u;
            geometry._listIndex.insert( geometry._listIndex.end(), { corner, corner + 2u, corner + 3u, corner, corner + 3u, corner + 1u } );
        }
        return geometry;
    };
    {
        FitOperatorTestScene scene;
        scene._innerGeometry = makeInner();
        Util::addQuad( scene._outerGeometry, -0.05f, 0.15f, 0.01f );
        scene.build();
        scene.run( "Cut" );
        const FitPartState& inner = scene._state._listPart[0];
        SW_EXPECT_EQUAL( static_cast<uint32>( SW_TRUE ), static_cast<uint32>( inner._listCovered[0] ) );
        SW_EXPECT_EQUAL( static_cast<uint32>( SW_TRUE ), static_cast<uint32>( inner._listCovered[1] ) );
        SW_EXPECT_EQUAL( static_cast<uint32>( SW_FALSE ), static_cast<uint32>( inner._listCovered[2] ) ); // 열 0.2 의 정점은 덮이지 않았다
        SW_EXPECT_EQUAL( static_cast<uint32>( SW_FALSE ), static_cast<uint32>( inner._listCovered[3] ) );
    }
    {
        FitOperatorTestScene scene;
        scene._innerGeometry = makeInner();
        Util::addQuad( scene._outerGeometry, -0.05f, 0.15f, 0.05f ); // 프로필의 덮임 거리 0.02 밖
        scene.build();
        scene.run( "Cut" );
        SW_EXPECT_EQUAL( static_cast<uint32>( SW_FALSE ), static_cast<uint32>( scene._state._listPart[0]._listCovered[0] ) );
    }
    {
        FitOperatorTestScene scene;
        scene._innerGeometry = makeInner();
        Util::addQuad( scene._outerGeometry, -0.05f, 0.15f, 0.05f );
        scene.addArgument( "distance", 0.1f );
        scene.build();
        scene.run( "Cut" );
        SW_EXPECT_EQUAL( static_cast<uint32>( SW_TRUE ), static_cast<uint32>( scene._state._listPart[0]._listCovered[0] ) );
    }
}

/**
 * @brief [FitOperatorTest] 보고는 표면 뒤로 파고든 정점을 세어 깊이 · 위치 · 글을 남기고 고치지 않는다
 */
SW_TEST_CASE( FitOperatorTest, ReportCountsPenetrationWithoutMoving )
{
    using Util = FitOperatorTestUtil;
    FitOperatorTestScene scene;
    // 안쪽 삼각형 전체가 바깥 면 뒤 0.005 에 있다. 바깥 사각형의 네 모서리는 안쪽 삼각형에서 판정 거리 밖이다.
    scene._innerGeometry            = Util::makePoints( {
        float3{ 0.0f,  0.0f, -0.005f},
        float3{0.01f,  0.0f, -0.005f},
        float3{ 0.0f, 0.01f, -0.005f},
    } );
    scene._innerGeometry._listIndex = { 0u, 1u, 2u };
    Util::addQuad( scene._outerGeometry, -0.1f, 0.1f, 0.0f );
    scene.build();
    scene.run( "Report" );

    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( scene._report._listPenetration.size() ) );
    const FitPenetration& penetration = scene._report._listPenetration[0];
    SW_EXPECT_EQUAL( 0u, penetration._partA );
    SW_EXPECT_EQUAL( 1u, penetration._partB );
    SW_EXPECT_EQUAL( 3u, penetration._vertexCount );
    SW_EXPECT_NEAR( 0.005f, penetration._maxDepth, Util::kTolerance );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( penetration._listPoint.size() ) );
    SW_EXPECT_EQUAL( 1u, scene._report.countWithID( "Character.FitRigidPenetration" ) );
    for ( uint32 vertex = 0; vertex < 3u; ++vertex )
    {
        Util::expectUntouched( scene._state._listPart[0], vertex );
    }
}

/**
 * @brief [FitOperatorTest] 면 앞의 정점 · 허용 오차 안의 파고듦은 관통이 아니고, 보고가 없어도 멈추지 않는다
 */
SW_TEST_CASE( FitOperatorTest, ReportIgnoresFrontAndTolerance )
{
    using Util = FitOperatorTestUtil;
    {
        FitOperatorTestScene scene;
        scene._innerGeometry = Util::makePoints( {
            float3{ 0.0f, 0.0f, 0.005f }
        } );
        Util::addQuad( scene._outerGeometry, -0.1f, 0.1f, 0.0f );
        scene.build();
        scene.run( "Report" );
        SW_EXPECT_TRUE( scene._report._listPenetration.empty() );
        SW_EXPECT_TRUE( scene._report._listMessage.empty() );
    }
    {
        FitOperatorTestScene scene;
        scene._innerGeometry = Util::makePoints( {
            float3{ 0.0f, 0.0f, -0.005f }
        } );
        Util::addQuad( scene._outerGeometry, -0.1f, 0.1f, 0.0f );
        scene.addArgument( "tolerance", 0.01f );
        scene.build();
        scene.run( "Report" );
        SW_EXPECT_TRUE( scene._report._listPenetration.empty() );
    }
    {
        FitOperatorTestScene scene;
        scene._innerGeometry = Util::makePoints( {
            float3{ 0.0f, 0.0f, -0.005f }
        } );
        Util::addQuad( scene._outerGeometry, -0.1f, 0.1f, 0.0f );
        scene.build();
        scene._state._pReport = nullptr;
        scene.run( "Report" );
        SW_EXPECT_TRUE( scene._report._listPenetration.empty() );
    }
}
