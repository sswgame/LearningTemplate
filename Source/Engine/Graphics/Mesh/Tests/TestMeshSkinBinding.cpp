#include "pch.h"

#include "Core/Container/LittleEndianUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"

using namespace sw;

// 역 바인드와 본 배치 지문은 메시의 값이다 — `.mesh` 왕복과 잘림, 스킨 설정 규칙, 합칠 수 있는지의 판정. 디바이스 없음(nogpu).

namespace
{
    struct MeshSkinBindingTestUtil
    {
        /** @brief 삼각형 하나 · 본 둘의 스킨드 메시 내용입니다. 두 번째 역 바인드는 Y 로 -2 옮깁니다. */
        static MeshAssetData makeData()
        {
            MeshAssetData data{};
            data._listVertex.resize( 3 );
            for ( uint32 vertex = 0; vertex < 3u; ++vertex )
            {
                data._listVertex[vertex]._arrPosition[0] = vertex == 1u ? 1.0f : 0.0f;
                data._listVertex[vertex]._arrPosition[1] = vertex == 2u ? 1.0f : 0.0f;
                data._listVertex[vertex]._arrUv[0]       = data._listVertex[vertex]._arrPosition[0];
                data._listVertex[vertex]._arrUv[1]       = data._listVertex[vertex]._arrPosition[1];
                data._listVertex[vertex]._arrNormal[2]   = 1.0f;
                data._listVertex[vertex]._arrColor[3]    = 1.0f;
            }
            data._listIndex      = vector<uint32>{ 0, 1, 2 };
            data._listSkinVertex = vector<MeshSkinVertex>( 3 );
            data._skinBinding    = makeBinding();
            return data;
        }

        static MeshSkinBinding makeBinding()
        {
            MeshSkinBinding binding{};
            binding._listInverseBind = vector<float4x4>{ float4x4::Identity, float4x4::makeTranslation( 0.0f, -2.0f, 0.0f ) };
            binding._layoutHash      = 0x1122334455667788ull;
            return binding;
        }
    };
} // namespace

/**
 * @brief [MeshSkinBindingTest] `.mesh` 가 역 바인드와 본 배치 지문을 값 그대로 싣고 읽는지 검증
 */
SW_TEST_CASE( MeshSkinBindingTest, AssetRoundTripKeepsInverseBindsAndLayout )
{
    using Util               = MeshSkinBindingTestUtil;
    const MeshAssetData data = Util::makeData();
    vector<uint8>       bytes;
    MeshAssetFormat::makeBytes( data, bytes );

    MeshAssetData read{};
    SW_REQUIRE_TRUE( MeshAssetFormat::readFromBytes( bytes.data(), bytes.size(), read ) );
    SW_EXPECT_TRUE( read.hasSkin() );
    SW_EXPECT_EQUAL( 2u, read._skinBinding.getBoneCount() );
    SW_EXPECT_EQUAL( data._skinBinding._layoutHash, read._skinBinding._layoutHash );
    SW_EXPECT_TRUE( read._skinBinding.matches( data._skinBinding ) );
    SW_EXPECT_NEAR( -2.0f, read._skinBinding._listInverseBind[1].getTranslation()._y, 1e-6f );
}

/**
 * @brief [MeshSkinBindingTest] 역 바인드 구간까지 잘린 파일과 판이 다른 파일과 유한하지 않은 역 바인드를 거절하고 내용을 비우는지 검증
 */
SW_TEST_CASE( MeshSkinBindingTest, TruncatedOrBadInverseBindsAreRejected )
{
    using Util = MeshSkinBindingTestUtil;
    vector<uint8> bytes;
    MeshAssetFormat::makeBytes( Util::makeData(), bytes );

    MeshAssetData read{};
    // 정점 · 스킨 · 지문 · 역 바인드 · 인덱스를 모두 합친 길이가 머리의 약속이다 — 한 바이트만 모자라도 진다.
    const size_t skinEnd = MeshAssetFormat::kHeaderSize + 3u * MeshAssetFormat::kVertexSize + 3u * MeshAssetFormat::kSkinVertexSize;
    SW_EXPECT_EQUAL( skinEnd + sizeof( uint64 ) + 2u * MeshAssetFormat::kInverseBindSize + 3u * sizeof( uint32 ), bytes.size() );
    SW_EXPECT_LOG( "MeshAssetFormat.MeshAssetDeclaresVertices" );
    SW_EXPECT_FALSE( MeshAssetFormat::readFromBytes( bytes.data(), bytes.size() - 1u, read ) );
    SW_EXPECT_FALSE( MeshAssetFormat::readFromBytes( bytes.data(), skinEnd + sizeof( uint64 ), read ) );
    SW_EXPECT_FALSE( read.hasSkin() );

    vector<uint8> badValue                    = bytes;
    badValue[skinEnd + sizeof( uint64 ) + 3u] = 0x7F; // 첫 칸의 지수를 모두 켠다
    badValue[skinEnd + sizeof( uint64 ) + 2u] = 0xC0;
    SW_EXPECT_LOG( "MeshAssetFormat.MeshAssetInvalidInverseBind" );
    SW_EXPECT_FALSE( MeshAssetFormat::readFromBytes( badValue.data(), badValue.size(), read ) );
    SW_EXPECT_TRUE( read._listVertex.empty() );

    vector<uint8> oldVersion = bytes;
    oldVersion[4]            = static_cast<uint8>( MeshAssetFormat::kVersion - 1u );
    SW_EXPECT_LOG( "MeshAssetFormat.MeshAssetVersionVertex" );
    SW_EXPECT_FALSE( MeshAssetFormat::readFromBytes( oldVersion.data(), oldVersion.size(), read ) );
}

/**
 * @brief [MeshSkinBindingTest] 역 바인드가 없거나 정점 수가 다른 스킨은 받지 않고, 본 수는 역 바인드 수인지 검증
 */
SW_TEST_CASE( MeshSkinBindingTest, SetSkinRequiresInverseBinds )
{
    using Util            = MeshSkinBindingTestUtil;
    MeshAssetData    data = Util::makeData();
    shared_ptr<Mesh> mesh = Mesh::create();
    SW_REQUIRE_TRUE( mesh->setGeometryWithTangents( std::move( data._listVertex ), std::move( data._listIndex ) ) );

    mesh->setSkin( vector<MeshSkinVertex>( 3 ), MeshSkinBinding{} );
    SW_EXPECT_FALSE( mesh->hasSkin() );
    mesh->setSkin( vector<MeshSkinVertex>( 2 ), Util::makeBinding() );
    SW_EXPECT_FALSE( mesh->hasSkin() );
    SW_EXPECT_EQUAL( 0u, mesh->getSkinBoneCount() );
    mesh->setSkin( vector<MeshSkinVertex>( 3 ), Util::makeBinding() );
    SW_EXPECT_TRUE( mesh->hasSkin() );
    SW_EXPECT_EQUAL( 2u, mesh->getSkinBoneCount() );
}

/**
 * @brief [MeshSkinBindingTest] 합칠 수 있는지의 판정 — 본 수 · 지문 · 역 바인드가 달라도 지고, 배치를 따지지 않는 묶음은 어느 지문과도 맞는지 검증
 */
SW_TEST_CASE( MeshSkinBindingTest, MatchesComparesLayoutAndInverseBinds )
{
    using Util                 = MeshSkinBindingTestUtil;
    const MeshSkinBinding base = Util::makeBinding();

    MeshSkinBinding otherLayout = base;
    otherLayout._layoutHash     = 7u;
    SW_EXPECT_FALSE( base.matches( otherLayout ) );

    MeshSkinBinding anyLayout = base;
    anyLayout._layoutHash     = MeshSkinBinding::kAnyLayout;
    SW_EXPECT_TRUE( base.matches( anyLayout ) );

    MeshSkinBinding otherBind     = base;
    otherBind._listInverseBind[1] = float4x4::makeTranslation( 0.0f, -2.5f, 0.0f );
    SW_EXPECT_FALSE( base.matches( otherBind ) );

    MeshSkinBinding fewerBones = base;
    fewerBones._listInverseBind.pop_back();
    SW_EXPECT_FALSE( base.matches( fewerBones ) );
}
