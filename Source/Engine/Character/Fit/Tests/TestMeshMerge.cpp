#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Character/Fit/CharacterMeshUtil.h"
#include "Engine/Graphics/Mesh/Mesh.h"

using namespace sw;

// 메시 병합(CharacterMeshUtil::mergeMeshes) — 구간 있는 부품은 고른 구간마다 조각이 되고, 고르지 않은 구간의 삼각형 · 정점은 빠진다. 디바이스 없음(nogpu).

namespace
{
    struct MeshMergeTestInternal
    {
        /** @brief 사각형(정점 넷 · 삼각형 둘)마다 구간 하나인 스킨드 메시입니다. 정점 i 는 x = i, 뼈 i % 4 에 가중치 1 이다. */
        static shared_ptr<Mesh> makeQuads( uint32 quadCount, bool bSections )
        {
            vector<RHIVertex>      listVertex( quadCount * 4u );
            vector<uint32>         listIndex;
            vector<MeshSkinVertex> listSkin( listVertex.size() );
            vector<MeshSection>    listSection;
            for ( uint32 quad = 0; quad < quadCount; ++quad )
            {
                const uint32 first = quad * 4u;
                for ( uint32 corner = 0; corner < 4u; ++corner )
                {
                    RHIVertex& vertex      = listVertex[first + corner];
                    vertex._arrPosition[0] = static_cast<float>( first + corner );
                    vertex._arrPosition[1] = static_cast<float>( corner / 2u );
                    vertex._arrNormal[2]   = 1.0f;
                    vertex._arrColor[3]    = 1.0f;
                    MeshSkinVertex& skin   = listSkin[first + corner];
                    skin._arrJoint[0]      = static_cast<uint16>( ( first + corner ) % 4u );
                    skin._arrWeight[0]     = 1.0f;
                }
                const uint32 firstIndex = static_cast<uint32>( listIndex.size() );
                listIndex.insert( listIndex.end(), { first, first + 1u, first + 2u, first, first + 2u, first + 3u } );
                MeshSection section;
                section._name       = hashed_string( ( string( "s" ) + static_cast<char>( '0' + quad ) ).c_str() );
                section._firstIndex = firstIndex;
                section._indexCount = 6u;
                listSection.push_back( section );
            }
            shared_ptr<Mesh> mesh = Mesh::create();
            SW_EXPECT_TRUE( mesh->setGeometryWithTangents( std::move( listVertex ), std::move( listIndex ) ) );
            mesh->setSkin( std::move( listSkin ), MeshSkinBinding::makeIdentity( 4u ) );
            if ( bSections )
                SW_EXPECT_TRUE( mesh->setSections( std::move( listSection ) ) );
            return mesh;
        }
    };
} // namespace

/**
 * @brief [MeshMergeTest] 구간 있는 부품은 고른 구간마다 합친 구간이 되고, 고르지 않은 구간의 삼각형 · 정점은 빠지며 스킨이 따라오는지 검증
 */
SW_TEST_CASE( MeshMergeTest, SectionedPartsMergeOnlyThePickedSections )
{
    using Internal                = MeshMergeTestInternal;
    const shared_ptr<Mesh> body   = Internal::makeQuads( 3u, true ); // 구간 s0 · s1 · s2 — s1 은 고르지 않는다(숨기는 머리카락 같은 것)
    const shared_ptr<Mesh> outfit = Internal::makeQuads( 1u, false );

    vector<MeshMergePart> listPart( 2 );
    listPart[0]._name  = hashed_string( "Body" );
    listPart[0]._pMesh = body.get();
    listPart[0]._listSection.push_back( MeshMergeSectionPick{ hashed_string( "s2" ), hashed_string( "s2" ), hashed_string( "m2" ) } );
    listPart[0]._listSection.push_back( MeshMergeSectionPick{ hashed_string( "s0" ), hashed_string( "s0" ), hashed_string( "m0" ) } );
    listPart[1]._name  = hashed_string( "Outfit.Body" );
    listPart[1]._pMesh = outfit.get();

    MeshMergeResult result;
    string          error;
    SW_REQUIRE_TRUE( CharacterMeshUtil::mergeMeshes( listPart, nullptr, result, &error ) );
    const Mesh& merged = *result._mesh;
    // 구간은 고른 순서(s2 · s0) 뒤에 옷이고, 고르지 않은 s1 의 정점 넷은 빠졌다.
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( merged.getSections().size() ) );
    SW_EXPECT_TRUE( merged.getSections()[0]._name == hashed_string( "s2" ) );
    SW_EXPECT_TRUE( merged.getSections()[1]._name == hashed_string( "s0" ) );
    SW_EXPECT_TRUE( merged.getSections()[2]._name == hashed_string( "Outfit.Body" ) );
    SW_EXPECT_EQUAL( 12u, merged.getVertexCount() );
    SW_EXPECT_EQUAL( 18u, merged.getIndexCount() );
    // 첫 구간의 첫 정점은 몸의 정점 8(s2 의 첫 정점)이고 스킨도 그 정점 것이다.
    const uint32 firstCorner = merged.getIndices()[merged.getSections()[0]._firstIndex];
    SW_EXPECT_NEAR( 8.0f, merged.getVertices()[firstCorner]._arrPosition[0], 1e-6f );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( merged.getSkinVertices()[firstCorner]._arrJoint[0] ) );
    SW_EXPECT_EQUAL( 4u, merged.getSkinBoneCount() );

    // 고른 구간이 메시에 없으면 · 훅 없이 조각 이름이 겹치면 까닭과 함께 거절한다.
    listPart[0]._listSection[1]._source = hashed_string( "missing" );
    SW_EXPECT_FALSE( CharacterMeshUtil::mergeMeshes( listPart, nullptr, result, &error ) );
    SW_EXPECT_TRUE( error.find( "missing" ) != string::npos );
    listPart[0]._listSection[1]._source = hashed_string( "s0" );
    listPart[0]._listSection[1]._name   = hashed_string( "s2" );
    SW_EXPECT_FALSE( CharacterMeshUtil::mergeMeshes( listPart, nullptr, result, &error ) );
    SW_EXPECT_TRUE( error.find( "used twice" ) != string::npos );
}
