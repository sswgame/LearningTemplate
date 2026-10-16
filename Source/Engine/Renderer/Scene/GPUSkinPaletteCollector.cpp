#include "pch.h"

#include "Engine/Renderer/Scene/GPUSkinPaletteCollector.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/SceneAnimation/AnimationCrowd.h"
#include "Engine/Object/SceneAnimation/AnimationSystem.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"

namespace sw
{
    void GPUSkinPaletteCollector::collect( const AnimationSystem& animation, GPUSceneSnapshot& inoutSnapshot )
    {
        if ( animation.getUnitCount() == 0 && inoutSnapshot._listSkinPalette.empty() )
            return;
        SW_PROFILE_SCOPE( "GT.GPUScene.build.skinPalettes" );

        inoutSnapshot._listSkinPalette.clear();
        shared_ptr<vector<float4>>  listRow    = make_shared<vector<float4>>();
        shared_ptr<vector<float32>> listWeight = make_shared<vector<float32>>();
        // 행 벡터 4x4 의 0, 1, 2번 열이다. 셰이더는 dot( float4( p, 1 ), 열 ) 로 옮긴다(meshskin.hlsl). 모프 가중치는 [0, 1] 로 제한해 싣는다.
        auto appendPalette = [&inoutSnapshot, &listRow, &listWeight]( const Mesh* pMesh, const vector<float4x4>& listPalette, const vector<float32>* pListMorphWeight )
        {
            GPUSkinPalette entry{};
            entry._pMesh     = pMesh;
            entry._firstRow  = static_cast<uint32>( listRow->size() );
            entry._boneCount = static_cast<uint32>( listPalette.size() );
            if ( pListMorphWeight != nullptr && pListMorphWeight->empty() == false )
            {
                entry._firstMorphWeight = static_cast<uint32>( listWeight->size() );
                entry._morphWeightCount = static_cast<uint32>( pListMorphWeight->size() );
                for ( const float32 weight : *pListMorphWeight )
                {
                    listWeight->push_back( MathUtil::clamp( weight, 0.0f, 1.0f ) );
                }
            }
            for ( const float4x4& matrix : listPalette )
            {
                listRow->push_back( float4{ matrix._11, matrix._21, matrix._31, matrix._41 } );
                listRow->push_back( float4{ matrix._12, matrix._22, matrix._32, matrix._42 } );
                listRow->push_back( float4{ matrix._13, matrix._23, matrix._33, matrix._43 } );
            }
            inoutSnapshot._listSkinPalette.push_back( entry );
        };
        listRow->reserve( _paletteRowCountHint );
        // 레벨(평가 순서)이 아니라 등록된 유닛 전부를 본다. 유닛 하나가 빠지면(시체를 정리함) 레벨은 다음 평가까지 비어 있어, 레벨로 모으면
        // 그 프레임의 모든 스킨드 메시가 팔레트 없이(단위 행렬이며 바인드 포즈) 한 번 그려진다.
        for ( const SkeletalMeshComponent* pUnit : animation.getUnits() )
        {
            // 군중 묶음과 공유하는 유닛과 VAT 유닛은 자기 팔레트가 없다. 묶음이 아래에서 한 번 싣는다.
            const AnimationCrowdMode mode = pUnit->getCrowdMode();
            if ( mode == AnimationCrowdMode::Shared || mode == AnimationCrowdMode::VertexAnimation )
                continue;
            const Mesh* pMesh = pUnit->getRawMesh();
            if ( pMesh == nullptr || pMesh->hasSkin() == false )
                continue;
            appendPalette( pMesh, pUnit->getSkinPalette(), &pUnit->getMorphWeights() );
        }
        // 군중 묶음은 묶음 하나가 메시 하나, 결과 구간 하나, 팔레트 하나다(멤버가 몇이든). 가리키는 유닛이 있는 묶음만 그려진다.
        for ( const unique_ptr<AnimationCrowdBucket>& bucket : animation.getCrowd().getBuckets() )
        {
            if ( bucket->getReferenceCount() > 0 && bucket->getMesh() != nullptr )
                appendPalette( bucket->getMesh().get(), bucket->getSkinPalette(), nullptr );
        }
        _paletteRowCountHint               = listRow->size();
        inoutSnapshot._pListSkinPaletteRow = std::move( listRow );
        inoutSnapshot._pListMorphWeight    = std::move( listWeight );
        // VAT 시계는 군중 시계 그대로다(인스턴스 시간 오프셋이 이 시계 기준이다).
        inoutSnapshot._vertexAnimationTime = static_cast<float32>( animation.getCrowd().getClock() );
    }
} // namespace sw
