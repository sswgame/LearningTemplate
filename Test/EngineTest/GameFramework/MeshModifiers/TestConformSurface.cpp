// Conform 계열의 표면 BVH — 질의가 상태 없이 병렬로 돌아도 직렬과 결과가 같고, 같은 대상이면 BVH 를 다시 만들지 않는다.
#include "pch.h"

#include "Core/Task/TaskManager.h"

#include "EngineTest/GameFramework/MeshModifiers/MeshModifierTestUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/ConformSurface.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 이름 "hills" 로 물결치는 높이 격자 메시를 돌려주는 가짜 외부 입력입니다. */
    class HillsInputs final : public IMeshModifierInputs
    {
    public:
        HillsInputs()
            : _bShifted{ false }
        {
        }

        /** @brief 켜면 언덕이 0.5 m 올라간 다른 대상이 됩니다(대상이 바뀌는 경우). */
        void setShifted( bool bShifted ) { _bShifted = bShifted; }

        [[nodiscard]] bool tryGetWorldMatrix( string_view, float4x4& ) const override { return false; }
        [[nodiscard]] bool tryGetCurveLength( string_view, float32& ) const override { return false; }
        [[nodiscard]] bool trySampleCurve( string_view, float32, float3&, float3& ) const override { return false; }
        [[nodiscard]] bool trySampleTexture( string_view, const float2&, float4& ) const override { return false; }

        [[nodiscard]] bool tryGetMeshTriangles( string_view reference, vector<float3>& outListPosition ) const override
        {
            if ( reference != string_view( "hills" ) )
                return false;
            outListPosition = makeHillTriangles( _bShifted ? 0.5f : 0.0f );
            return true;
        }

        /** @brief 바깥 XZ [-6, 6] 를 24 x 24 칸으로 나눈 삼각형 목록입니다(높이는 사인 두 개의 합). */
        static vector<float3> makeHillTriangles( float32 lift )
        {
            constexpr int32 kCell = 24;
            vector<float3>  listTriangle;
            auto            height = [lift]( float32 x, float32 z )
            { return lift - 3.0f + MathUtil::sin( x * 0.9f ) * MathUtil::cos( z * 0.7f ); };
            for ( int32 cellX = 0; cellX < kCell; ++cellX )
            {
                for ( int32 cellZ = 0; cellZ < kCell; ++cellZ )
                {
                    const float32 x0 = -6.0f + 12.0f * static_cast<float32>( cellX ) / kCell;
                    const float32 x1 = -6.0f + 12.0f * static_cast<float32>( cellX + 1 ) / kCell;
                    const float32 z0 = -6.0f + 12.0f * static_cast<float32>( cellZ ) / kCell;
                    const float32 z1 = -6.0f + 12.0f * static_cast<float32>( cellZ + 1 ) / kCell;
                    const float3  a{ x0, height( x0, z0 ), z0 };
                    const float3  b{ x1, height( x1, z0 ), z0 };
                    const float3  c{ x1, height( x1, z1 ), z1 };
                    const float3  d{ x0, height( x0, z1 ), z1 };
                    listTriangle.push_back( a );
                    listTriangle.push_back( b );
                    listTriangle.push_back( c );
                    listTriangle.push_back( a );
                    listTriangle.push_back( c );
                    listTriangle.push_back( d );
                }
            }
            return listTriangle;
        }

    private:
        bool _bShifted;
    };

    struct ConformSurfaceTestInternal
    {
        /** @brief 덩어리가 여럿 나오는 큰 메시입니다(6 면 × 16 × 16 × 6 = 9216 정점 = 3 덩어리). */
        static MeshModifierMesh makeBigMesh()
        {
            MeshModifierMesh mesh = test::MeshModifierTestUtil::makeBoxMesh( 16 );
            for ( float3& position : mesh._listPosition )
            {
                position._y += 4.0f; // 언덕 위에서 시작해 최근접점 · 광선이 모두 닿게 한다
            }
            return mesh;
        }

        static unique_ptr<MeshModifier> makeConform( float32 mode )
        {
            unique_ptr<MeshModifier> conform    = MeshModifierRegistry::create( "conform" );
            uint32                   paramIndex = 0;
            if ( conform->tryGetParamIndex( "Target", paramIndex ) )
                conform->setText( paramIndex, "hills" );
            if ( conform->tryGetParamIndex( "Mode", paramIndex ) )
                conform->setScalar( paramIndex, mode );
            return conform;
        }

        static void applyWith( const MeshModifier& modifier, MeshModifierMesh& inoutMesh, const IMeshModifierInputs& inputs )
        {
            MeshModifierEvaluation evaluation{};
            evaluation._pInputs = &inputs;
            modifier.apply( inoutMesh, evaluation );
        }
    };
} // namespace

SW_TEST_CASE( ConformSurfaceTest, ParallelQueriesMatchSerialResult )
{
    HillsInputs inputs{};
    TaskManager taskManager;
    SW_ASSERT_TRUE( taskManager.initialize( 3 ) );
    for ( const float32 mode : { 0.0f, 1.0f, 2.0f } )
    {
        MeshModifierMesh         serialMesh      = ConformSurfaceTestInternal::makeBigMesh();
        MeshModifierMesh         parallelMesh    = serialMesh;
        unique_ptr<MeshModifier> serialConform   = ConformSurfaceTestInternal::makeConform( mode );
        unique_ptr<MeshModifier> parallelConform = ConformSurfaceTestInternal::makeConform( mode );

        setMeshModifierTaskManager( nullptr );
        ConformSurfaceTestInternal::applyWith( *serialConform, serialMesh, inputs );
        setMeshModifierTaskManager( &taskManager );
        ConformSurfaceTestInternal::applyWith( *parallelConform, parallelMesh, inputs );
        setMeshModifierTaskManager( nullptr );

        SW_ASSERT_EQUAL( serialMesh._listPosition.size(), parallelMesh._listPosition.size() );
        uint32 movedCount = 0;
        for ( size_t index = 0; index < serialMesh._listPosition.size(); ++index )
        {
            const float3& serial   = serialMesh._listPosition[index];
            const float3& parallel = parallelMesh._listPosition[index];
            SW_EXPECT_TRUE( serial._x == parallel._x && serial._y == parallel._y && serial._z == parallel._z );
            movedCount += serial._y < 3.0f ? 1u : 0u;
        }
        // 가까운 점 · 주축 방식은 거의 모든 정점이, 법선 방식은 위를 보는 면의 정점만 언덕 쪽으로 옮겨진다 - 비교가 빈 결과끼리 맞는 일이 아님을 확인한다
        const uint32 minMoved = mode == 2.0f ? 100u : static_cast<uint32>( serialMesh._listPosition.size() / 2 );
        SW_EXPECT_TRUE( movedCount > minMoved );
    }
    taskManager.shutdown();
}

SW_TEST_CASE( ConformSurfaceTest, AssignRebuildsOnlyWhenTrianglesChange )
{
    ConformSurface       surface{};
    const vector<float3> listHills = HillsInputs::makeHillTriangles( 0.0f );
    SW_EXPECT_TRUE( surface.assign( listHills ) );
    SW_EXPECT_FALSE( surface.assign( listHills ) );
    SW_EXPECT_FALSE( surface.assign( HillsInputs::makeHillTriangles( 0.0f ) ) ); // 같은 값의 다른 벡터
    SW_EXPECT_EQUAL( 1u, surface.getBuildCount() );
    SW_EXPECT_TRUE( surface.assign( HillsInputs::makeHillTriangles( 0.5f ) ) );
    SW_EXPECT_EQUAL( 2u, surface.getBuildCount() );
    SW_EXPECT_EQUAL( static_cast<uint32>( listHills.size() / 3 ), surface.getTriangleCount() );
}

SW_TEST_CASE( ConformSurfaceTest, ChangedTargetGivesNewResult )
{
    HillsInputs              inputs{};
    unique_ptr<MeshModifier> conform = ConformSurfaceTestInternal::makeConform( 1.0f );
    MeshModifierMesh         first   = test::MeshModifierTestUtil::makeBoxMesh( 4 );
    ConformSurfaceTestInternal::applyWith( *conform, first, inputs );
    MeshModifierMesh again = test::MeshModifierTestUtil::makeBoxMesh( 4 );
    ConformSurfaceTestInternal::applyWith( *conform, again, inputs ); // 같은 대상 — 캐시된 BVH
    for ( size_t index = 0; index < first._listPosition.size(); ++index )
    {
        SW_EXPECT_TRUE( ( first._listPosition[index] - again._listPosition[index] ).getLength() == 0.0f );
    }
    inputs.setShifted( true );
    MeshModifierMesh shifted = test::MeshModifierTestUtil::makeBoxMesh( 4 );
    ConformSurfaceTestInternal::applyWith( *conform, shifted, inputs ); // 대상이 올라갔으면 캐시를 쓰지 않고 새 결과
    float32 lifted = 0.0f;
    for ( size_t index = 0; index < first._listPosition.size(); ++index )
    {
        lifted = MathUtil::max( lifted, shifted._listPosition[index]._y - first._listPosition[index]._y );
    }
    SW_EXPECT_TRUE( lifted > 0.4f );
}
