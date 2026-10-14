// 곡선 · 경로 · 참조 모디파이어 — 곧은 경로는 이동만, 원호 경로는 길이 보존, Conform 뒤 정점은 대상 평면 위, 참조가 비면 항등, 곡선 변위 · 조각 · 베지어 끌개의 값.
#include "pch.h"

#include "EngineTest/GameFramework/MeshModifiers/MeshModifierTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 곡선 하나(곧은 선 또는 XY 평면 반원)와 이름 붙은 평면 메시를 아는 가짜 외부 입력입니다. 오브젝트 변환은 모른다(경로 자기 공간 = 월드). */
    class PathFakeInputs final : public IMeshModifierInputs
    {
    public:
        PathFakeInputs()
            : _curveName{}
            , _start{ 0.0f, 0.0f, 0.0f }
            , _length{ 0.0f }
            , _arcRadius{ 0.0f }
            , _listPlaneName{}
            , _listPlaneHeight{}
        {
        }

        /** @brief @p start 에서 +Y 로 @p length 만큼 가는 곧은 경로입니다. */
        void setStraightCurve( string_view name, const float3& start, float32 length )
        {
            _curveName = string( name );
            _start     = start;
            _length    = length;
            _arcRadius = 0.0f;
        }

        /** @brief 원점에서 +Y 로 출발해 +X 쪽으로 도는 반지름 @p radius 반원 경로입니다. */
        void setArcCurve( string_view name, float32 radius )
        {
            _curveName = string( name );
            _start     = float3{ 0.0f, 0.0f, 0.0f };
            _length    = MathUtil::kPi * radius;
            _arcRadius = radius;
        }

        /** @brief 높이 @p height 의 넓은 수평 평면(XZ, 삼각형 둘) 메시를 더합니다. */
        void addPlane( string_view name, float32 height )
        {
            _listPlaneName.push_back( string( name ) );
            _listPlaneHeight.push_back( height );
        }

        [[nodiscard]] bool tryGetWorldMatrix( string_view, float4x4& ) const override { return false; }

        [[nodiscard]] bool tryGetCurveLength( string_view reference, float32& outLength ) const override
        {
            if ( _curveName.empty() || reference != string_view( _curveName ) )
                return false;
            outLength = _length;
            return true;
        }

        [[nodiscard]] bool trySampleCurve( string_view reference, float32 fraction, float3& outPosition, float3& outTangent ) const override
        {
            if ( _curveName.empty() || reference != string_view( _curveName ) )
                return false;
            const float32 ratio = MathUtil::saturate( fraction );
            if ( _arcRadius <= 0.0f )
            {
                outPosition = _start + float3{ 0.0f, _length * ratio, 0.0f };
                outTangent  = float3{ 0.0f, 1.0f, 0.0f };
                return true;
            }
            const float32 angle = MathUtil::kPi * ratio;
            outPosition         = float3{ _arcRadius - _arcRadius * MathUtil::cos( angle ), _arcRadius * MathUtil::sin( angle ), 0.0f };
            outTangent          = float3{ MathUtil::sin( angle ), MathUtil::cos( angle ), 0.0f };
            return true;
        }

        [[nodiscard]] bool tryGetMeshTriangles( string_view reference, vector<float3>& outListPosition ) const override
        {
            for ( size_t index = 0; index < _listPlaneName.size(); ++index )
            {
                if ( reference != string_view( _listPlaneName[index] ) )
                    continue;
                const float32 height = _listPlaneHeight[index];
                outListPosition      = { float3{ -100.0f, height, -100.0f }, float3{ 100.0f, height, -100.0f }, float3{ 100.0f, height, 100.0f },
                                         float3{ -100.0f, height, -100.0f }, float3{ 100.0f, height, 100.0f },  float3{ -100.0f, height, 100.0f } };
                return true;
            }
            return false;
        }

        [[nodiscard]] bool trySampleTexture( string_view, const float2&, float4& ) const override { return false; }

    private:
        string          _curveName;
        float3          _start;
        float32         _length;
        float32         _arcRadius;
        vector<string>  _listPlaneName;
        vector<float32> _listPlaneHeight;
    };

    struct PathModifiersTestInternal
    {
        static void apply( const MeshModifier& modifier, MeshModifierMesh& inoutMesh, const IMeshModifierInputs& inputs )
        {
            MeshModifierEvaluation evaluation{};
            evaluation._pInputs = &inputs;
            modifier.apply( inoutMesh, evaluation );
        }

        static bool setText( MeshModifier& modifier, string_view name, string_view text )
        {
            uint32 paramIndex = 0;
            if ( modifier.tryGetParamIndex( name, paramIndex ) == false )
                return false;
            modifier.setText( paramIndex, text );
            modifier.onParamsChanged();
            return true;
        }

        static bool setCurve( MeshModifier& modifier, string_view name, const vector<float2>& listKey )
        {
            uint32 paramIndex = 0;
            if ( modifier.tryGetParamIndex( name, paramIndex ) == false )
                return false;
            modifier.setCurveKeys( paramIndex, listKey );
            modifier.onParamsChanged();
            return true;
        }

        static bool setVector3( MeshModifier& modifier, string_view name, const float3& value )
        {
            uint32 paramIndex = 0;
            if ( modifier.tryGetParamIndex( name, paramIndex ) == false )
                return false;
            modifier.setVector3( paramIndex, value );
            modifier.onParamsChanged();
            return true;
        }

        /** @brief 주축(Y) 위 -2..2 를 40 칸으로 나눈 점 41 개입니다. */
        static MeshModifierMesh makeAxisLine()
        {
            MeshModifierMesh mesh{};
            for ( uint32 index = 0; index <= 40; ++index )
            {
                mesh._listPosition.push_back( float3{ 0.0f, -2.0f + 0.1f * static_cast<float32>( index ), 0.0f } );
            }
            return mesh;
        }

        static float32 computePolylineLength( const vector<float3>& listPoint )
        {
            float32 length = 0.0f;
            for ( size_t index = 1; index < listPoint.size(); ++index )
            {
                length += ( listPoint[index] - listPoint[index - 1] ).getLength();
            }
            return length;
        }
    };
} // namespace

SW_TEST_CASE( PathModifiersTest, PathDeformStraightPathOnlyTranslates )
{
    PathFakeInputs inputs{};
    inputs.setStraightCurve( "path", float3{ 5.0f, 0.0f, 0.0f }, 10.0f );
    MeshModifierMesh         mesh   = test::MeshModifierTestUtil::makeBoxMesh( 2 );
    const vector<float3>     before = mesh._listPosition;
    unique_ptr<MeshModifier> path   = MeshModifierRegistry::create( "pathdeform" );
    SW_ASSERT_NOT_NULL( path.get() );
    SW_ASSERT_TRUE( PathModifiersTestInternal::setText( *path, "Path", "path" ) );
    PathModifiersTestInternal::apply( *path, mesh, inputs );
    // 상자 아래 끝(y = -2)이 경로 시작(5, 0, 0)에 붙는다 — 모든 정점이 (5, 2, 0) 만큼 옮겨진다
    for ( size_t index = 0; index < before.size(); ++index )
    {
        SW_EXPECT_TRUE( ( mesh._listPosition[index] - before[index] - float3{ 5.0f, 2.0f, 0.0f } ).getLength() < 1e-4f );
    }
}

SW_TEST_CASE( PathModifiersTest, PathDeformArcPreservesAxisLength )
{
    PathFakeInputs inputs{};
    inputs.setArcCurve( "arc", 3.0f );
    MeshModifierMesh         mesh = PathModifiersTestInternal::makeAxisLine();
    unique_ptr<MeshModifier> path = MeshModifierRegistry::create( "pathdeform" );
    SW_ASSERT_TRUE( PathModifiersTestInternal::setText( *path, "Path", "arc" ) );
    PathModifiersTestInternal::apply( *path, mesh, inputs );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::isFinite( mesh._listPosition ) );
    SW_EXPECT_NEAR_EQUAL( 4.0f, PathModifiersTestInternal::computePolylineLength( mesh._listPosition ), 1e-3f );
    // 축 위 점은 원호 위에 놓인다(중심 (3, 0, 0) 에서 거리 3)
    for ( const float3& point : mesh._listPosition )
    {
        SW_EXPECT_NEAR_EQUAL( 3.0f, ( point - float3{ 3.0f, 0.0f, 0.0f } ).getLength(), 1e-3f );
    }
}

SW_TEST_CASE( PathModifiersTest, PathDeformWithEmptyReferenceIsIdentity )
{
    PathFakeInputs inputs{};
    inputs.setStraightCurve( "path", float3{ 5.0f, 0.0f, 0.0f }, 10.0f );
    for ( const utf8* pTypeID : { "pathdeform", "worldpathdeform", "ropedeform", "conform", "conformmulti" } )
    {
        MeshModifierMesh         mesh     = test::MeshModifierTestUtil::makeBoxMesh( 2 );
        const vector<float3>     before   = mesh._listPosition;
        unique_ptr<MeshModifier> modifier = MeshModifierRegistry::create( pTypeID );
        SW_ASSERT_NOT_NULL( modifier.get() );
        PathModifiersTestInternal::apply( *modifier, mesh, inputs );
        SW_EXPECT_TRUE( test::MeshModifierTestUtil::isBitwiseEqual( before, mesh._listPosition ) );
    }
}

SW_TEST_CASE( PathModifiersTest, WorldPathDeformStraightPathOnlyTranslates )
{
    PathFakeInputs inputs{};
    inputs.setStraightCurve( "path", float3{ 0.0f, 3.0f, 1.0f }, 10.0f );
    MeshModifierMesh         mesh   = test::MeshModifierTestUtil::makeBoxMesh( 2 );
    const vector<float3>     before = mesh._listPosition;
    unique_ptr<MeshModifier> path   = MeshModifierRegistry::create( "worldpathdeform" );
    SW_ASSERT_TRUE( PathModifiersTestInternal::setText( *path, "Path", "path" ) );
    PathModifiersTestInternal::apply( *path, mesh, inputs );
    for ( size_t index = 0; index < before.size(); ++index )
    {
        SW_EXPECT_TRUE( ( mesh._listPosition[index] - before[index] - float3{ 0.0f, 5.0f, 1.0f } ).getLength() < 1e-4f );
    }
}

SW_TEST_CASE( PathModifiersTest, RopeDeformStretchesAxisOverWholePath )
{
    PathFakeInputs inputs{};
    inputs.setStraightCurve( "rope", float3{ 0.0f, 0.0f, 0.0f }, 8.0f );
    MeshModifierMesh         mesh = PathModifiersTestInternal::makeAxisLine();
    unique_ptr<MeshModifier> rope = MeshModifierRegistry::create( "ropedeform" );
    SW_ASSERT_TRUE( PathModifiersTestInternal::setText( *rope, "Path", "rope" ) );
    PathModifiersTestInternal::apply( *rope, mesh, inputs );
    SW_EXPECT_TRUE( mesh._listPosition.front().getLength() < 1e-4f );
    SW_EXPECT_TRUE( ( mesh._listPosition.back() - float3{ 0.0f, 8.0f, 0.0f } ).getLength() < 1e-4f );
}

SW_TEST_CASE( PathModifiersTest, ConformPutsVerticesOnTargetPlane )
{
    PathFakeInputs inputs{};
    inputs.addPlane( "floor", -5.0f );
    for ( const float32 mode : { 0.0f, 1.0f } )
    {
        MeshModifierMesh         mesh    = test::MeshModifierTestUtil::makeBoxMesh( 2 );
        unique_ptr<MeshModifier> conform = MeshModifierRegistry::create( "conform" );
        SW_ASSERT_TRUE( PathModifiersTestInternal::setText( *conform, "Target", "floor" ) );
        SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *conform, "Mode", mode ) );
        PathModifiersTestInternal::apply( *conform, mesh, inputs );
        for ( const float3& point : mesh._listPosition )
        {
            SW_EXPECT_NEAR_EQUAL( -5.0f, point._y, 1e-4f );
        }
    }
}

SW_TEST_CASE( PathModifiersTest, ConformMultiPicksNearestTarget )
{
    PathFakeInputs inputs{};
    inputs.addPlane( "floor", -5.0f );
    inputs.addPlane( "ceiling", 5.0f );
    MeshModifierMesh         mesh   = test::MeshModifierTestUtil::makeBoxMesh( 2 );
    const vector<float3>     before = mesh._listPosition;
    unique_ptr<MeshModifier> multi  = MeshModifierRegistry::create( "conformmulti" );
    SW_ASSERT_TRUE( PathModifiersTestInternal::setText( *multi, "Target 1", "floor" ) );
    SW_ASSERT_TRUE( PathModifiersTestInternal::setText( *multi, "Target 2", "ceiling" ) );
    PathModifiersTestInternal::apply( *multi, mesh, inputs );
    for ( size_t index = 0; index < before.size(); ++index )
    {
        if ( before[index]._y > 0.5f )
            SW_EXPECT_NEAR_EQUAL( 5.0f, mesh._listPosition[index]._y, 1e-4f );
        if ( before[index]._y < -0.5f )
            SW_EXPECT_NEAR_EQUAL( -5.0f, mesh._listPosition[index]._y, 1e-4f );
    }
}

SW_TEST_CASE( PathModifiersTest, CurveDeformConstantDisplacementTranslates )
{
    MeshModifierMesh         mesh   = test::MeshModifierTestUtil::makeBoxMesh( 2 );
    const vector<float3>     before = mesh._listPosition;
    unique_ptr<MeshModifier> curve  = MeshModifierRegistry::create( "curvedeform" );
    SW_ASSERT_NOT_NULL( curve.get() );
    SW_ASSERT_TRUE( PathModifiersTestInternal::setCurve( *curve, "Displace Curve", { float2{ 0.0f, 1.0f }, float2{ 1.0f, 1.0f } } ) );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *curve, "Amount", 2.0f ) );
    test::MeshModifierTestUtil::applyOne( *curve, mesh );
    // 주축 Y 의 다음 축(Z)으로 2 만큼 민다
    for ( size_t index = 0; index < before.size(); ++index )
    {
        SW_EXPECT_TRUE( ( mesh._listPosition[index] - before[index] - float3{ 0.0f, 0.0f, 2.0f } ).getLength() < 1e-4f );
    }
}

SW_TEST_CASE( PathModifiersTest, CurveSculptConstantCurvePushesAlongNormal )
{
    MeshModifierMesh         mesh   = test::MeshModifierTestUtil::makeBoxMesh( 2 );
    const MeshModifierMesh   before = mesh;
    unique_ptr<MeshModifier> sculpt = MeshModifierRegistry::create( "curvesculpt" );
    SW_ASSERT_NOT_NULL( sculpt.get() );
    SW_ASSERT_TRUE( PathModifiersTestInternal::setCurve( *sculpt, "Curve", { float2{ 0.0f, 1.0f }, float2{ 1.0f, 1.0f } } ) );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *sculpt, "Strength", 0.5f ) );
    test::MeshModifierTestUtil::applyOne( *sculpt, mesh );
    for ( size_t index = 0; index < before.getVertexCount(); ++index )
    {
        SW_EXPECT_TRUE( ( mesh._listPosition[index] - before._listPosition[index] - before._listNormal[index] * 0.5f ).getLength() < 1e-4f );
    }
}

SW_TEST_CASE( PathModifiersTest, CurveSculptLayeredMovesOnlyInsideLayer )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = { float3{ -1.0f, 0.0f, -1.0f }, float3{ 1.0f, 0.0f, 1.0f }, float3{ 0.0f, 0.0f, 0.0f } };
    mesh._listNormal   = { float3::UnitY, float3::UnitY, float3::UnitY };
    unique_ptr<MeshModifier> layered = MeshModifierRegistry::create( "curvesculptlayered" );
    SW_ASSERT_NOT_NULL( layered.get() );
    uint32 layersIndex = 0;
    SW_ASSERT_TRUE( layered->tryGetParamIndex( "Layers", layersIndex ) );
    layered->setNumberList( layersIndex, { 0.5f, 0.5f, 0.1f, 1.0f } );
    SW_ASSERT_TRUE( PathModifiersTestInternal::setCurve( *layered, "Shape", { float2{ 0.0f, 1.0f }, float2{ 1.0f, 1.0f } } ) );
    test::MeshModifierTestUtil::applyOne( *layered, mesh );
    SW_EXPECT_NEAR_EQUAL( 0.0f, mesh._listPosition[0]._y, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, mesh._listPosition[1]._y, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, mesh._listPosition[2]._y, 1e-5f );
}

SW_TEST_CASE( PathModifiersTest, BezierAttractorPullsHalfwayAtHalfRadius )
{
    MeshModifierMesh mesh{};
    mesh._listPosition = { float3{ 0.0f, 0.5f, 0.0f }, float3{ 0.0f, 5.0f, 0.0f } };
    unique_ptr<MeshModifier> bezier = MeshModifierRegistry::create( "bezierattractor" );
    SW_ASSERT_NOT_NULL( bezier.get() );
    // X 축 위 곧은 베지어(-2 → 2)
    SW_ASSERT_TRUE( PathModifiersTestInternal::setVector3( *bezier, "Point 0", float3{ -2.0f, 0.0f, 0.0f } ) );
    SW_ASSERT_TRUE( PathModifiersTestInternal::setVector3( *bezier, "Point 1", float3{ -0.5f, 0.0f, 0.0f } ) );
    SW_ASSERT_TRUE( PathModifiersTestInternal::setVector3( *bezier, "Point 2", float3{ 0.5f, 0.0f, 0.0f } ) );
    SW_ASSERT_TRUE( PathModifiersTestInternal::setVector3( *bezier, "Point 3", float3{ 2.0f, 0.0f, 0.0f } ) );
    SW_ASSERT_TRUE( test::MeshModifierTestUtil::setScalar( *bezier, "Strength", 1.0f ) );
    test::MeshModifierTestUtil::applyOne( *bezier, mesh );
    // 거리 0.5 / 반경 1 → 세기 0.5 로 절반 끌린다. 반경 밖 점은 그대로다
    SW_EXPECT_NEAR_EQUAL( 0.25f, mesh._listPosition[0]._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, mesh._listPosition[1]._y, 1e-6f );
}
