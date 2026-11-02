#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Character/Fit/CharacterMeshLOD.h"
#include "Engine/Graphics/Mesh/Mesh.h"

using namespace sw;

// 조립한 캐릭터 메시의 LOD(CharacterMeshLOD::buildLODs) — 단계마다 구간이 그대로 있고 구간마다 줄며, 단계는 쓰는 정점만 스킨과 함께 가진다. 디바이스 없음(nogpu).

namespace
{
    struct CharacterMeshLODTestInternal
    {
        static constexpr uint32 kGrid = 8u; ///< 구간 하나가 kGrid x kGrid 사각형 평면 — 안쪽 정점은 오차 없이 줄어든다

        /** @brief 평면 격자 둘(구간 둘, x 로 떨어져 있다)인 스킨드 메시입니다. 구간 0 의 정점은 x < kGrid, 구간 1 은 x > kGrid 다. */
        static shared_ptr<Mesh> makeTwoSectionGrid()
        {
            vector<RHIVertex>      listVertex;
            vector<uint32>         listIndex;
            vector<MeshSkinVertex> listSkin;
            vector<MeshSection>    listSection;
            for ( uint32 section = 0; section < 2u; ++section )
            {
                const uint32 first      = static_cast<uint32>( listVertex.size() );
                const uint32 firstIndex = static_cast<uint32>( listIndex.size() );
                for ( uint32 y = 0; y <= kGrid; ++y )
                {
                    for ( uint32 x = 0; x <= kGrid; ++x )
                    {
                        RHIVertex vertex{};
                        vertex._arrPosition[0] = static_cast<float>( x + section * ( kGrid + 2u ) );
                        vertex._arrPosition[1] = static_cast<float>( y );
                        vertex._arrNormal[2]   = 1.0f;
                        vertex._arrColor[3]    = 1.0f;
                        listVertex.push_back( vertex );
                        MeshSkinVertex skin{};
                        skin._arrJoint[0]  = static_cast<uint16>( section );
                        skin._arrWeight[0] = 1.0f;
                        listSkin.push_back( skin );
                    }
                }
                for ( uint32 y = 0; y < kGrid; ++y )
                {
                    for ( uint32 x = 0; x < kGrid; ++x )
                    {
                        const uint32 corner = first + y * ( kGrid + 1u ) + x;
                        listIndex.insert( listIndex.end(), { corner, corner + 1u, corner + kGrid + 2u, corner, corner + kGrid + 2u, corner + kGrid + 1u } );
                    }
                }
                MeshSection meshSection;
                meshSection._name       = hashed_string( section == 0 ? "s0" : "s1" );
                meshSection._firstIndex = firstIndex;
                meshSection._indexCount = static_cast<uint32>( listIndex.size() ) - firstIndex;
                listSection.push_back( meshSection );
            }
            shared_ptr<Mesh> mesh = Mesh::create();
            SW_EXPECT_TRUE( mesh->setGeometryWithTangents( std::move( listVertex ), std::move( listIndex ) ) );
            mesh->setSkin( std::move( listSkin ), 2u );
            SW_EXPECT_TRUE( mesh->setSections( std::move( listSection ) ) );
            return mesh;
        }
    };
} // namespace

/**
 * @brief [CharacterMeshLODTest] 단계마다 구간 범위가 LOD 0 구간 순서로 이어지고 구간마다 삼각형이 줄며, 단계 정점은 그 단계가 쓰는 것만 스킨과 함께 있고 구간끼리 섞이지 않는지 검증
 */
SW_TEST_CASE( CharacterMeshLODTest, LevelsKeepSectionsAndOnlyTheirVertices )
{
    using Internal                           = CharacterMeshLODTestInternal;
    const shared_ptr<Mesh> mesh              = Internal::makeTwoSectionGrid();
    const uint32           lod0IndexCount    = mesh->getIndexCount();
    const uint32           lod0VertexCount   = mesh->getVertexCount();
    const uint32           sectionIndexCount = mesh->getSections()[0]._indexCount;

    CharacterLODSettings settings;
    SW_REQUIRE_TRUE( settings.parseXML( R"(<CharacterLODSettings><Level triangle_ratio="0.5" max_error="0.01" screen_size="0.3"/><Level triangle_ratio="0.2" max_error="0.05" screen_size="0.1"/></CharacterLODSettings>)",
                                        "test" ) );
    SW_REQUIRE_TRUE( CharacterMeshLOD::buildLODs( *mesh, settings ) );
    SW_REQUIRE_EQUAL( 3u, mesh->getLODCount() );
    SW_EXPECT_EQUAL( mesh->getLODVertices().size(), mesh->getLODSkinVertices().size() );

    uint32 previousIndexCount = lod0IndexCount;
    for ( const MeshLOD& lod : mesh->getLODs() )
    {
        // 구간 범위는 단계 안에서 LOD 0 구간 순서로 빈틈없이 이어진다.
        SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( lod._listSectionRange.size() ) );
        SW_EXPECT_EQUAL( lod._firstIndex, lod._listSectionRange[0]._firstIndex );
        SW_EXPECT_EQUAL( lod._listSectionRange[0]._firstIndex + lod._listSectionRange[0]._indexCount, lod._listSectionRange[1]._firstIndex );
        SW_EXPECT_EQUAL( lod._indexCount, lod._listSectionRange[0]._indexCount + lod._listSectionRange[1]._indexCount );
        SW_EXPECT_TRUE( lod._indexCount < previousIndexCount );
        SW_EXPECT_TRUE( lod._vertexCount < lod0VertexCount );
        previousIndexCount = lod._indexCount;
        for ( uint32 section = 0; section < 2u; ++section )
        {
            const MeshLODSectionRange& range = lod._listSectionRange[section];
            SW_EXPECT_TRUE( range._indexCount > 0u && range._indexCount < sectionIndexCount );
            for ( uint32 index = range._firstIndex; index < range._firstIndex + range._indexCount; ++index )
            {
                // 인덱스 값은 정점 흐름 번호다 — 이 단계의 정점 범위 안이고, 그 정점은 같은 구간의 것이다(스킨 뼈도 따라왔다).
                const uint32 vertex = mesh->getLODIndices()[index - lod0IndexCount];
                SW_REQUIRE_TRUE( vertex >= lod._firstVertex && vertex < lod._firstVertex + lod._vertexCount );
                const uint32 local = vertex - lod0VertexCount;
                SW_EXPECT_EQUAL( section == 0u, mesh->getLODVertices()[local]._arrPosition[0] < static_cast<float>( Internal::kGrid + 1u ) );
                SW_EXPECT_EQUAL( static_cast<uint16>( section ), mesh->getLODSkinVertices()[local]._arrJoint[0] );
            }
        }
    }
}

/**
 * @brief [CharacterMeshLODTest] 단계가 낮아질수록 비율 · 화면 크기가 작아지지 않는 표는 거절하고 비는지 검증
 */
SW_TEST_CASE( CharacterMeshLODTest, SettingsRejectNonDecreasingLevels )
{
    SW_EXPECT_LOG( "CharacterMeshLOD.SettingsLevelInvalid" );
    CharacterLODSettings settings;
    SW_EXPECT_FALSE( settings.parseXML( R"(<CharacterLODSettings><Level triangle_ratio="0.5" max_error="0.01" screen_size="0.3"/><Level triangle_ratio="0.6" max_error="0.01" screen_size="0.1"/></CharacterLODSettings>)",
                                        "test" ) );
    SW_EXPECT_TRUE( settings._listLevel.empty() );
}
