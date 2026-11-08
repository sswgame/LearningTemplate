#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/CharacterLODSettings.h"
#include "Engine/Character/Fit/CharacterMeshUtil.h"
#include "Engine/Graphics/Mesh/Mesh.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceMeshPart.h"
#include "GameFramework/Runtime/Gameplay/Appearance/ResolvedAppearance.h"

using namespace sw;

// 외형 런타임 조립(언리얼 Mutable 과 같은 단계) — 체형 모프를 굽고(`AppearanceMeshPart::assemble`) 몸과 부품을 구간 있는 메시 하나로 합친다(`mergeParts`).
// 씬 · 컴포넌트 · 디바이스 없이 돈다(nogpu).

namespace
{
    struct AppearanceMeshAssemblyTestInternal
    {
        /** @brief 사각형(정점 넷 · 삼각형 둘) @p quadCount 개인 스킨드 메시입니다. 정점 i 는 x = i 이고 뼈 i % 4 에 가중치 1 이다. */
        static shared_ptr<Mesh> makeQuads( uint32 quadCount )
        {
            vector<RHIVertex>      listVertex( quadCount * 4u );
            vector<uint32>         listIndex;
            vector<MeshSkinVertex> listSkin( listVertex.size() );
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
                listIndex.insert( listIndex.end(), { first, first + 1u, first + 2u, first, first + 2u, first + 3u } );
            }
            shared_ptr<Mesh> mesh = Mesh::create();
            SW_EXPECT_TRUE( mesh->setGeometryWithTangents( std::move( listVertex ), std::move( listIndex ) ) );
            mesh->setSkin( std::move( listSkin ), MeshSkinBinding::makeIdentity( 4u ) );
            return mesh;
        }

        static ResolvedPart makePart( const hashed_string& owner, const utf8* pName )
        {
            ResolvedPart part;
            part._owner    = owner;
            part._partName = hashed_string( pName );
            return part;
        }
    };
} // namespace

/**
 * @brief [AppearanceMeshAssemblyTest] 몸 모프는 몸에만 구워지고(모프가 없는 옷은 원본을 그린다), 몸과 옷이 부품 구간 이름의 메시 하나로 합쳐지며 구운 위치가 남는지 검증
 */
SW_TEST_CASE( AppearanceMeshAssemblyTest, BakesBodyShapeThenMergesPartsIntoOneMesh )
{
    using Internal                = AppearanceMeshAssemblyTestInternal;
    const shared_ptr<Mesh> body   = Internal::makeQuads( 2u );
    const shared_ptr<Mesh> outfit = Internal::makeQuads( 1u );
    MeshMorphTarget        heavy{ hashed_string( "Heavy" ), {} };
    heavy._listDelta.push_back( MeshMorphDelta{
        0u, float3{ 0.0f, 0.0f, 2.0f },
        float3{}
    } );
    body->setMorphTargets( { heavy } );

    ResolvedAppearance resolved;
    resolved._listMorph.push_back( ResolvedMorph{ hashed_string{}, hashed_string( "Heavy" ), 0.5f } ); // 체형 축이 정한 몸 모프
    const ResolvedPart bodyPart   = Internal::makePart( hashed_string{}, "Body" );
    const ResolvedPart outfitPart = Internal::makePart( hashed_string( "Torso" ), "Shirt" );

    const vector<AppearanceMeshPart> listPart{
        AppearanceMeshPart{  bodyPart._partName,   bodyPart._owner,   body.get(), nullptr},
        AppearanceMeshPart{outfitPart._partName, outfitPart._owner, outfit.get(), nullptr}
    };
    vector<shared_ptr<Mesh>> listBaked;
    string                   report;
    SW_REQUIRE_TRUE( AppearanceMeshPart::assemble( resolved, listPart, nullptr, CharacterBoneArray{}, listBaked, report ) );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( listBaked.size() ) );
    SW_REQUIRE_TRUE( listBaked[0] != nullptr );
    SW_EXPECT_TRUE( listBaked[1] == nullptr );                                              // 옷에는 그 이름의 모프가 없다 — 바꿀 것이 없어 원본을 그린다
    SW_EXPECT_NEAR( 1.0f, listBaked[0]->getVertices()[0]._arrPosition[2], 1e-5f );          // 2 × 0.5 를 레스트에 구웠다
    SW_EXPECT_FALSE( listBaked[0]->findMorphTargetIndex( hashed_string( "Heavy" ) ) >= 0 ); // 구운 모프는 사본에서 빠진다

    vector<MeshMergePart>           listMergePart;
    vector<CharacterLODSectionRule> listLODRule;
    listMergePart.push_back( AppearanceMeshPart::makeMergePart( bodyPart, *listBaked[0], true ) );
    AppearanceMeshPart::appendLODRules( bodyPart, listMergePart.back(), listLODRule );
    listMergePart.push_back( AppearanceMeshPart::makeMergePart( outfitPart, *outfit, false ) );
    AppearanceMeshPart::appendLODRules( outfitPart, listMergePart.back(), listLODRule );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( listLODRule.size() ) );

    const CharacterLODSettings       noLevels; // 단계가 없으면 LOD 0 만 — LOD 만들기는 CharacterLODSettings 테스트가 본다
    const expected<shared_ptr<Mesh>> merged = AppearanceMeshPart::mergeParts( listMergePart, noLevels, listLODRule );
    SW_REQUIRE_TRUE( merged.has_value() );
    const Mesh& mesh = **merged;
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( mesh.getSections().size() ) );
    SW_EXPECT_TRUE( mesh.getSections()[0]._name == hashed_string( "Body" ) );
    SW_EXPECT_TRUE( mesh.getSections()[1]._name == hashed_string( "Torso.Shirt" ) );
    SW_EXPECT_EQUAL( 12u, mesh.getVertexCount() );
    SW_EXPECT_NEAR( 1.0f, mesh.getVertices()[0]._arrPosition[2], 1e-5f );

    // 합칠 조각이 없으면 까닭과 함께 거절한다.
    const vector<MeshMergePart>      listEmpty;
    const expected<shared_ptr<Mesh>> rejected = AppearanceMeshPart::mergeParts( listEmpty, noLevels, listLODRule );
    SW_EXPECT_FALSE( rejected.has_value() );
}
