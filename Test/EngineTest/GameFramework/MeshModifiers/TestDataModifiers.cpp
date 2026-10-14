// 데이터 · 충돌 · 기타 모디파이어 — 충돌 도형 밀어내기, 충돌점 찌그러짐, 포인트 캐시 보간, 정점 애니메이션 결정성, 묶음 = 하위 스택, 텍스처 변위, 2D FFD 의 Z 보존.
#include "pch.h"

#include "Core/File/FileUtil.h"

#include "EngineTest/GameFramework/MeshModifiers/MeshModifierTestUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/DeformableModifier.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct DataModifiersTestInternal
    {
        /** @brief 위치만 있는 메시를 만듭니다. */
        static MeshModifierMesh makePointMesh( const vector<float3>& listPosition )
        {
            MeshModifierMesh mesh{};
            mesh._listPosition = listPosition;
            return mesh;
        }

        static bool setNumberList( MeshModifier& modifier, string_view name, const vector<float32>& listNumber )
        {
            uint32 paramIndex = 0;
            if ( modifier.tryGetParamIndex( name, paramIndex ) == false )
                return false;
            modifier.setNumberList( paramIndex, listNumber );
            modifier.onParamsChanged();
            return true;
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

        static bool isNear( const float3& a, const float3& b ) { return ( a - b ).getLength() < 1e-4f; }
    };

    /** @brief 어떤 경로든 같은 색을 내는 가짜 텍스처 입력입니다. */
    class ConstantTextureInputs final : public IMeshModifierInputs
    {
    public:
        explicit ConstantTextureInputs( const float4& color )
            : _color{ color }
        {
        }

        [[nodiscard]] bool tryGetWorldMatrix( string_view, float4x4& ) const override { return false; }
        [[nodiscard]] bool tryGetCurveLength( string_view, float32& ) const override { return false; }
        [[nodiscard]] bool trySampleCurve( string_view, float32, float3&, float3& ) const override { return false; }
        [[nodiscard]] bool tryGetMeshTriangles( string_view, vector<float3>& ) const override { return false; }
        [[nodiscard]] bool trySampleTexture( string_view, const float2&, float4& outColor ) const override
        {
            outColor = _color;
            return true;
        }

    private:
        float4 _color;
    };
} // namespace

SW_TEST_CASE( DataModifiersTest, CollisionDeformPushesPointsOutOfSphereAndKeepsOutside )
{
    unique_ptr<MeshModifier> modifier = MeshModifierRegistry::create( "collisiondeform" );
    SW_ASSERT_TRUE( modifier != nullptr );
    // 반지름 1 구(원점)
    SW_EXPECT_TRUE( DataModifiersTestInternal::setNumberList( *modifier, "Colliders", vector<float32>{ 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f } ) );
    MeshModifierMesh mesh = DataModifiersTestInternal::makePointMesh( vector<float3>{
        float3{0.5f,   0.0f, 0.0f},
        float3{0.0f, -0.25f, 0.0f},
        float3{2.0f,   0.0f, 0.0f}
    } );
    test::MeshModifierTestUtil::applyOne( *modifier, mesh );
    SW_EXPECT_TRUE( DataModifiersTestInternal::isNear( mesh._listPosition[0], float3{ 1.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( DataModifiersTestInternal::isNear( mesh._listPosition[1], float3{ 0.0f, -1.0f, 0.0f } ) );
    SW_EXPECT_TRUE( mesh._listPosition[2] == ( float3{ 2.0f, 0.0f, 0.0f } ) );
}

SW_TEST_CASE( DataModifiersTest, HitDeformDentsInsideRadiusOnly )
{
    unique_ptr<MeshModifier> modifier = MeshModifierRegistry::create( "hitdeform" );
    SW_ASSERT_TRUE( modifier != nullptr );
    // 원점 충돌, 아래 방향, 반경 1, 세기 0.5, 감쇠 0(선형)
    SW_EXPECT_TRUE( DataModifiersTestInternal::setNumberList( *modifier, "Hits", vector<float32>{ 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.5f, 0.0f } ) );
    MeshModifierMesh mesh = DataModifiersTestInternal::makePointMesh( vector<float3>{
        float3{0.0f, 0.0f, 0.0f},
        float3{0.5f, 0.0f, 0.0f},
        float3{3.0f, 0.0f, 0.0f}
    } );
    test::MeshModifierTestUtil::applyOne( *modifier, mesh );
    SW_EXPECT_TRUE( DataModifiersTestInternal::isNear( mesh._listPosition[0], float3{ 0.0f, -0.5f, 0.0f } ) );
    SW_EXPECT_TRUE( DataModifiersTestInternal::isNear( mesh._listPosition[1], float3{ 0.5f, -0.25f, 0.0f } ) );
    SW_EXPECT_TRUE( mesh._listPosition[2] == ( float3{ 3.0f, 0.0f, 0.0f } ) );
}

SW_TEST_CASE( DataModifiersTest, PointCacheInterpolatesFramesAndSkipsMismatchedMesh )
{
    // 정점 2 · 프레임 2 · fps 1. 프레임 1 은 프레임 0 을 x 로 2 만큼 옮긴 것
    const vector<float3> listFrame0{
        float3{0.0f, 0.0f, 0.0f},
        float3{1.0f, 1.0f, 1.0f}
    };
    vector<uint8> listByte;
    const uint32  vertexCount = 2;
    const uint32  frameCount  = 2;
    const float32 fps         = 1.0f;
    const auto    appendBytes = [&listByte]( const void* pData, size_t size )
    {
        const uint8* pByte = static_cast<const uint8*>( pData );
        listByte.insert( listByte.end(), pByte, pByte + size );
    };
    appendBytes( &vertexCount, 4 );
    appendBytes( &frameCount, 4 );
    appendBytes( &fps, 4 );
    for ( uint32 frame = 0; frame < frameCount; ++frame )
    {
        for ( const float3& position : listFrame0 )
        {
            const float3 value = position + float3{ 2.0f * static_cast<float32>( frame ), 0.0f, 0.0f };
            appendBytes( &value, sizeof( float3 ) );
        }
    }
    const string path = test::makeTempPath( "point_cache.bin" );
    SW_ASSERT_TRUE( FileUtil::writeFile( path, listByte.data(), listByte.size() ) );

    unique_ptr<MeshModifier> modifier = MeshModifierRegistry::create( "pointcache" );
    SW_ASSERT_TRUE( modifier != nullptr );
    SW_EXPECT_TRUE( DataModifiersTestInternal::setText( *modifier, "Cache File", path ) );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::setScalar( *modifier, "Loop", 0.0f ) );

    MeshModifierEvaluation evaluation{};
    evaluation._time      = 0.25f;
    MeshModifierMesh mesh = DataModifiersTestInternal::makePointMesh( vector<float3>{
        float3{ 5.0f, 5.0f, 5.0f},
        float3{-5.0f, 0.0f, 0.0f}
    } );
    modifier->apply( mesh, evaluation );
    SW_EXPECT_TRUE( DataModifiersTestInternal::isNear( mesh._listPosition[0], float3{ 0.5f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( DataModifiersTestInternal::isNear( mesh._listPosition[1], float3{ 1.5f, 1.0f, 1.0f } ) );

    // 정점 수가 다르면 항등
    MeshModifierMesh other = DataModifiersTestInternal::makePointMesh( vector<float3>{
        float3{ 5.0f, 5.0f, 5.0f }
    } );
    modifier->apply( other, evaluation );
    SW_EXPECT_TRUE( other._listPosition[0] == ( float3{ 5.0f, 5.0f, 5.0f } ) );
}

SW_TEST_CASE( DataModifiersTest, VertexAnimIsDeterministicAndMovesOverTime )
{
    unique_ptr<MeshModifier> modifier = MeshModifierRegistry::create( "vertexanim" );
    SW_ASSERT_TRUE( modifier != nullptr );
    uint32 amplitudeIndex = 0;
    SW_ASSERT_TRUE( modifier->tryGetParamIndex( "Amplitude", amplitudeIndex ) );
    modifier->setVector3( amplitudeIndex, float3{ 0.5f, 0.25f, 0.0f } );
    modifier->onParamsChanged();

    const MeshModifierMesh source = test::MeshModifierTestUtil::makeBoxMesh( 2 );
    MeshModifierEvaluation evaluation{};
    evaluation._time        = 0.3f;
    MeshModifierMesh first  = source;
    MeshModifierMesh second = source;
    modifier->apply( first, evaluation );
    modifier->apply( second, evaluation );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::isBitwiseEqual( first._listPosition, second._listPosition ) );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( first._listPosition, source._listPosition ) > 1e-3f );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( first._listPosition, source._listPosition ) < 0.6f );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::isFinite( first._listPosition ) );
}

SW_TEST_CASE( DataModifiersTest, DeformableMatchesItsSubStackAndSurvivesSave )
{
    unique_ptr<MeshModifier> modifier = MeshModifierRegistry::create( "deformable" );
    SW_ASSERT_TRUE( modifier != nullptr );
    DeformableModifier* pDeformable = DeformableModifier::findFrom( *modifier );
    SW_ASSERT_TRUE( pDeformable != nullptr );

    MeshModifierStack reference;
    for ( const string_view typeID : { string_view( "twist" ), string_view( "bend" ) } )
    {
        const int32 subIndex = pDeformable->getStack().addModifier( typeID );
        const int32 refIndex = reference.addModifier( typeID );
        SW_ASSERT_TRUE( subIndex >= 0 && refIndex >= 0 );
        test::MeshModifierTestUtil::fillParams( *pDeformable->getStack().findModifier( subIndex ), 7u + static_cast<uint32>( subIndex ) );
        test::MeshModifierTestUtil::fillParams( *reference.findModifier( refIndex ), 7u + static_cast<uint32>( refIndex ) );
    }

    const MeshModifierMesh source   = test::MeshModifierTestUtil::makeBoxMesh( 3 );
    MeshModifierMesh       grouped  = source;
    MeshModifierMesh       expected = source;
    test::MeshModifierTestUtil::applyOne( *pDeformable, grouped );
    reference.apply( expected, MeshModifierEvaluation{} );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( grouped._listPosition, expected._listPosition ) < 1e-4f );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( grouped._listPosition, source._listPosition ) > 1e-3f );

    // 바깥 스택 저장 문서로 왕복해도 하위 스택이 남는다
    MeshModifierStack outer;
    SW_ASSERT_TRUE( outer.addModifier( std::move( modifier ) ) == 0 );
    MeshModifierStack loaded;
    SW_ASSERT_TRUE( loaded.readJSONText( outer.makeJSONText() ) );
    SW_ASSERT_TRUE( loaded.getModifierCount() == 1 );
    MeshModifierMesh reloaded = source;
    loaded.apply( reloaded, MeshModifierEvaluation{} );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( reloaded._listPosition, expected._listPosition ) < 1e-4f );
}

SW_TEST_CASE( DataModifiersTest, TextureDisplaceMovesAlongNormalWithInputs )
{
    const ConstantTextureInputs inputs( float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
    for ( const string_view typeID : { string_view( "displacerendertarget" ), string_view( "displacewebcam" ) } )
    {
        unique_ptr<MeshModifier> modifier = MeshModifierRegistry::create( typeID );
        SW_ASSERT_TRUE( modifier != nullptr );
        uint32 pathIndex = 0;
        SW_ASSERT_TRUE( modifier->tryGetParamIndex( typeID == "displacewebcam" ? "Frame" : "Texture", pathIndex ) );
        modifier->setText( pathIndex, "Texture/Height.png" );
        SW_EXPECT_TRUE( test::MeshModifierTestUtil::setScalar( *modifier, "Amount", 0.5f ) );

        const MeshModifierMesh source  = test::MeshModifierTestUtil::makeBoxMesh( 1 );
        MeshModifierMesh       noInput = source;
        test::MeshModifierTestUtil::applyOne( *modifier, noInput ); // 입력이 없으면 항등
        SW_EXPECT_TRUE( test::MeshModifierTestUtil::isBitwiseEqual( noInput._listPosition, source._listPosition ) );

        MeshModifierEvaluation evaluation{};
        evaluation._pInputs   = &inputs;
        MeshModifierMesh mesh = source;
        modifier->apply( mesh, evaluation );
        for ( size_t index = 0; index < mesh.getVertexCount(); ++index )
        {
            SW_EXPECT_TRUE( DataModifiersTestInternal::isNear( mesh._listPosition[index], source._listPosition[index] + source._listNormal[index] * 0.5f ) );
        }
    }
}

SW_TEST_CASE( DataModifiersTest, FFD2DMovesXYAndKeepsZ )
{
    unique_ptr<MeshModifier> modifier = MeshModifierRegistry::create( "ffd2d" );
    SW_ASSERT_TRUE( modifier != nullptr );
    // 3 x 3 격자의 가운데 제어점(1 + 3 · 1 = 4)만 옮긴다
    vector<float32> listOffset( 18, 0.0f );
    listOffset[8] = 0.2f;
    listOffset[9] = -0.1f;
    SW_EXPECT_TRUE( DataModifiersTestInternal::setNumberList( *modifier, "Offsets", listOffset ) );

    const MeshModifierMesh source = test::MeshModifierTestUtil::makeBoxMesh( 3 );
    MeshModifierMesh       mesh   = source;
    test::MeshModifierTestUtil::applyOne( *modifier, mesh );
    bool bSameZ = true;
    for ( size_t index = 0; index < mesh.getVertexCount(); ++index )
    {
        if ( mesh._listPosition[index]._z != source._listPosition[index]._z )
            bSameZ = false;
    }
    SW_EXPECT_TRUE( bSameZ );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::computeMaxDistance( mesh._listPosition, source._listPosition ) > 1e-3f );
    SW_EXPECT_TRUE( test::MeshModifierTestUtil::isFinite( mesh._listPosition ) );
}
