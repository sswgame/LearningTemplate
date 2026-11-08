#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Foliage/FoliageComponent.h"
#include "Engine/Graphics/Mesh/Mesh.h"

using namespace sw;

// 식생 메시의 구간 목록 검사(FoliageComponent::validateMeshSections) — 구간 있는 메시(나무의 줄기 · 잎)는 구간마다 한 번씩 머티리얼을 적어야 하고,
// 구간 없는 메시는 목록이 비어야 한다(레이어 머티리얼로 그린다). 디바이스 없음(nogpu).

namespace
{
    struct FoliageSectionTestInternal
    {
        /** @brief 사각형(정점 넷 · 삼각형 둘)마다 구간 하나인 메시입니다. @p listName 이 비면 구간 없는 메시입니다. */
        static shared_ptr<Mesh> makeMesh( const vector<string>& listName )
        {
            const uint32        quadCount = static_cast<uint32>( listName.empty() ? 1u : listName.size() );
            vector<RHIVertex>   listVertex( quadCount * 4u );
            vector<uint32>      listIndex;
            vector<MeshSection> listSection;
            for ( uint32 quad = 0; quad < quadCount; ++quad )
            {
                const uint32 first = quad * 4u;
                for ( uint32 corner = 0; corner < 4u; ++corner )
                {
                    RHIVertex& vertex      = listVertex[first + corner];
                    vertex._arrPosition[0] = static_cast<float>( corner % 2u );
                    vertex._arrPosition[1] = static_cast<float>( corner / 2u );
                    vertex._arrPosition[2] = static_cast<float>( quad );
                    vertex._arrNormal[2]   = 1.0f;
                    vertex._arrColor[3]    = 1.0f;
                }
                const uint32 firstIndex = static_cast<uint32>( listIndex.size() );
                listIndex.insert( listIndex.end(), { first, first + 1u, first + 2u, first, first + 2u, first + 3u } );
                if ( listName.empty() )
                    continue;
                MeshSection section;
                section._name       = hashed_string( listName[quad].c_str() );
                section._firstIndex = firstIndex;
                section._indexCount = 6u;
                listSection.push_back( section );
            }
            shared_ptr<Mesh> mesh = Mesh::create();
            SW_EXPECT_TRUE( mesh->setGeometry( std::move( listVertex ), std::move( listIndex ) ) );
            if ( listSection.empty() == false )
                SW_EXPECT_TRUE( mesh->setSections( std::move( listSection ) ) );
            return mesh;
        }

        static FoliageMesh makeFoliageMesh( const vector<FoliageSection>& listSection )
        {
            FoliageMesh foliageMesh;
            foliageMesh._meshID      = "test/tree.mesh";
            foliageMesh._listSection = listSection;
            return foliageMesh;
        }

        static FoliageSection makeSection( string_view name, string_view materialPath )
        {
            FoliageSection section;
            section._sectionName  = string( name );
            section._materialPath = string( materialPath );
            return section;
        }
    };
} // namespace

SW_TEST_CASE( FoliageSectionTest, SectionlessMeshUsesTheLayerMaterial )
{
    using Internal              = FoliageSectionTestInternal;
    const shared_ptr<Mesh> mesh = Internal::makeMesh( {} );
    SW_EXPECT_TRUE( FoliageComponent::validateMeshSections( Internal::makeFoliageMesh( {} ), *mesh ).has_value() );
    // 구간 없는 메시에 구간 머티리얼을 적으면 어느 쪽으로 그릴지 모호하다.
    SW_EXPECT_FALSE( FoliageComponent::validateMeshSections( Internal::makeFoliageMesh( { Internal::makeSection( "bark", "bark.material" ) } ), *mesh ).has_value() );
}

SW_TEST_CASE( FoliageSectionTest, EverySectionNeedsItsMaterialOnce )
{
    using Internal              = FoliageSectionTestInternal;
    const shared_ptr<Mesh> tree = Internal::makeMesh( { "bark", "leaves" } );
    // 모두 적으면 순서는 상관없다.
    SW_EXPECT_TRUE( FoliageComponent::validateMeshSections(
                        Internal::makeFoliageMesh( { Internal::makeSection( "leaves", "leaves.material" ), Internal::makeSection( "bark", "bark.material" ) } ), *tree )
                        .has_value() );
    // 구간 있는 메시를 레이어 머티리얼 하나로 그리지 않는다(줄기 그림이 잎에 덮인다).
    SW_EXPECT_FALSE( FoliageComponent::validateMeshSections( Internal::makeFoliageMesh( {} ), *tree ).has_value() );
    // 빠뜨린 구간 — 잎 없는 나무가 조용히 남지 않게 거절한다.
    SW_EXPECT_FALSE( FoliageComponent::validateMeshSections( Internal::makeFoliageMesh( { Internal::makeSection( "bark", "bark.material" ) } ), *tree ).has_value() );
    // 메시에 없는 구간 · 겹친 구간 · 머티리얼 없는 구간.
    SW_EXPECT_FALSE( FoliageComponent::validateMeshSections(
                         Internal::makeFoliageMesh( { Internal::makeSection( "bark", "bark.material" ), Internal::makeSection( "flowers", "flowers.material" ) } ), *tree )
                         .has_value() );
    SW_EXPECT_FALSE( FoliageComponent::validateMeshSections(
                         Internal::makeFoliageMesh( { Internal::makeSection( "bark", "bark.material" ), Internal::makeSection( "bark", "leaves.material" ) } ), *tree )
                         .has_value() );
    SW_EXPECT_FALSE( FoliageComponent::validateMeshSections(
                         Internal::makeFoliageMesh( { Internal::makeSection( "bark", "bark.material" ), Internal::makeSection( "leaves", "" ) } ), *tree )
                         .has_value() );
}
