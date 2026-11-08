#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Character/Fit/CharacterLODSettings.h"
#include "Engine/Graphics/Mesh/Mesh.h"

using namespace sw;

// 조립한 캐릭터 메시의 LOD(CharacterLODSettings::buildLODs) — 단계마다 구간이 그대로 있고 구간마다 줄며, 단계는 쓰는 정점만 스킨과 함께 가진다. 디바이스 없음(nogpu).

namespace
{
    struct CharacterMeshLODTestInternal
    {
        static constexpr uint32 kGrid = 8u; ///< 구간 하나가 kGrid x kGrid 사각형 평면 — 안쪽 정점은 오차 없이 줄어든다

        /** @brief 평면 격자 둘(구간 둘, x 로 떨어져 있다)인 스킨드 메시입니다. 구간 0 의 정점은 x < kGrid, 구간 1 은 x > kGrid 다. */
        static shared_ptr<Mesh> makeTwoSectionGrid( bool bSkin = true )
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
            if ( bSkin )
                mesh->setSkin( std::move( listSkin ), MeshSkinBinding::makeIdentity( 2u ) );
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
    SW_REQUIRE_TRUE( settings.loadFromXMLText( R"(<CharacterLODSettings><Level triangle_ratio="0.5" max_error="0.01" screen_size="0.3" lock_border="true" permissive="false" prune="false"/><Level triangle_ratio="0.2" max_error="0.05" screen_size="0.1" lock_border="false" permissive="true" prune="false"/></CharacterLODSettings>)",
                                               "test" ) );
    SW_REQUIRE_TRUE( CharacterLODSettings::buildLODs( *mesh, settings, {} ) );
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
 * @brief [CharacterMeshLODTest] 구간 규칙 — `_dropLevel` 의 구간은 그 단계부터 범위가 비고, `_detail` 이 큰 구간은 같은 단계에서 삼각형을 더 남기는지 검증
 */
SW_TEST_CASE( CharacterMeshLODTest, SectionRulesDropAndProtectSections )
{
    using Internal = CharacterMeshLODTestInternal;
    CharacterLODSettings settings;
    settings._listLevel.push_back( CharacterLODLevelRule{ 0.5f, 0.01f, 0.3f, true, false, false } );
    settings._listLevel.push_back( CharacterLODLevelRule{ 0.2f, 0.05f, 0.1f, true, false, false } );

    const shared_ptr<Mesh> plain = Internal::makeTwoSectionGrid();
    SW_REQUIRE_TRUE( CharacterLODSettings::buildLODs( *plain, settings, {} ) );
    const shared_ptr<Mesh>                ruled = Internal::makeTwoSectionGrid();
    const vector<CharacterLODSectionRule> listRule{
        CharacterLODSectionRule{hashed_string( "s0" ), 1.0f, 2u},
        CharacterLODSectionRule{hashed_string( "s1" ), 2.0f, 0u}
    };
    SW_REQUIRE_TRUE( CharacterLODSettings::buildLODs( *ruled, settings, listRule ) );
    SW_REQUIRE_EQUAL( 3u, ruled->getLODCount() );

    // s0: LOD 1 은 그대로 줄고, LOD 2 부터 비어 그리지 않는다.
    SW_EXPECT_TRUE( ruled->getLODs()[0]._listSectionRange[0]._indexCount > 0u );
    SW_EXPECT_EQUAL( 0u, ruled->getLODs()[1]._listSectionRange[0]._indexCount );
    // s1: 비율이 두 배라 규칙 없는 메시보다 LOD 1 삼각형이 많다.
    SW_EXPECT_TRUE( ruled->getLODs()[0]._listSectionRange[1]._indexCount > plain->getLODs()[0]._listSectionRange[1]._indexCount );
}

/**
 * @brief [CharacterMeshLODTest] 단계가 낮아질수록 비율 · 화면 크기가 작아지지 않는 표는 거절하고 비는지 검증
 */
SW_TEST_CASE( CharacterMeshLODTest, SettingsRejectNonDecreasingLevels )
{
    SW_EXPECT_LOG( "CharacterMeshLOD.SettingsLevelInvalid" );
    CharacterLODSettings settings;
    SW_EXPECT_FALSE( settings.loadFromXMLText( R"(<CharacterLODSettings><Level triangle_ratio="0.5" max_error="0.01" screen_size="0.3" lock_border="true" permissive="false" prune="false"/><Level triangle_ratio="0.6" max_error="0.01" screen_size="0.1" lock_border="true" permissive="false" prune="false"/></CharacterLODSettings>)",
                                               "test" ) );
    SW_EXPECT_TRUE( settings._listLevel.empty() );
}

/**
 * @brief [CharacterMeshLODTest] 스킨 없는 모프 메시의 고정 스킨 원본은 같은 원본이면 한 벌을 나누고, 구간 · LOD(단계 스킨 포함)가 따라오는지 검증
 */
SW_TEST_CASE( CharacterMeshLODTest, RigidSkinSourceIsSharedAndKeepsSectionsAndLevels )
{
    using Internal              = CharacterMeshLODTestInternal;
    const shared_ptr<Mesh> mesh = Internal::makeTwoSectionGrid( false );
    MeshMorphTarget        target;
    target._name = hashed_string( "smile" );
    target._listDelta.push_back( MeshMorphDelta{} );
    mesh->setMorphTargets( { target } );
    CharacterLODSettings settings;
    settings._listLevel.push_back( CharacterLODLevelRule{ 0.5f, 0.01f, 0.3f, true, false, false } );
    SW_REQUIRE_TRUE( CharacterLODSettings::buildLODs( *mesh, settings, {} ) );

    const shared_ptr<Mesh> rigid = Mesh::acquireRigidSkinSource( *mesh );
    SW_EXPECT_TRUE( rigid == Mesh::acquireRigidSkinSource( *mesh ) ); // 유닛마다 사본이 아니다
    SW_EXPECT_TRUE( rigid->hasSkin() );
    SW_EXPECT_EQUAL( 1u, rigid->getMorphTargetCount() );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( rigid->getSections().size() ) );
    SW_REQUIRE_EQUAL( mesh->getLODCount(), rigid->getLODCount() );
    SW_EXPECT_EQUAL( rigid->getLODVertices().size(), rigid->getLODSkinVertices().size() );
    const MeshLOD& expected = mesh->getLODs()[0];
    const MeshLOD& actual   = rigid->getLODs()[0];
    SW_EXPECT_EQUAL( expected._indexCount, actual._indexCount );
    SW_EXPECT_EQUAL( expected._listSectionRange[1]._firstIndex, actual._listSectionRange[1]._firstIndex );
    SW_EXPECT_TRUE( mesh->getLODIndices() == rigid->getLODIndices() );
}
