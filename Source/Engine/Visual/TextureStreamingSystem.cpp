#include "pch.h"

#include "Engine/Visual/TextureStreamingSystem.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Visual/Component/MeshComponent.h"
#include "Engine/Visual/MeshLODSystem.h"

namespace sw
{
    SW_SCENE_SERVICE( TextureStreamingSystem, FlushEnd, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure, SceneDataKind::Transform } ) )

    TextureStreamingSystem::TextureStreamingSystem( GameObjectManager& manager )
        : _manager{ manager }
        , _mapMaterialScreenSize{}
        , _listMeshScreenSize{}
        , _listPathScratch{}
    {
    }

    void TextureStreamingSystem::update( float32 deltaTime )
    {
        (void)deltaTime;
        AssetManager* pAssets = _manager.findAssetManager();
        if ( pAssets == nullptr )
            return;
        TextureCache& textures = pAssets->getAssetCache<TextureCache>();
        if ( textures.hasPendingStreamingUpgrades() == false )
            return;
        const MeshLODSystem& lodSystem = _manager.getSubsystem<MeshLODSystem>();
        if ( lodSystem.areViewsSet() == false )
            return; // 렌더가 뷰를 넣기 전에는 화면 크기를 모른다
        SW_PROFILE_SCOPE( "GT.TextureStreaming.recordScreenSize" );
        // 화면 크기 재기는 메시마다 독립이라 워커로 나눈다 — 본문은 뷰(`MeshLODSystem`, 이 단계 동안 바뀌지 않는다)와 메시의 월드 캐시를 읽고 자기 칸 `_listMeshScreenSize[index]` 에만 쓴다.
        // 머티리얼별 최댓값 모으기는 이 스레드가 목록 순서로 한다(맵은 공유 쓰기다). 최댓값 규칙이라 결과는 나누는 방법과 무관하다.
        // 메시 LOD(`MeshLODSystem::update`)의 결과는 읽지 않는다 — 둘 다 뷰만 입력이라 사이에 기다릴 의존이 없다(같은 단계에서 차례로 돈다).
        // 월드 캐시는 읽기만 해야 한다 — 남은 더티가 있으면 먼저 펼친다(`MeshLODSystem::update` 와 같은 규칙).
        if ( _manager.hasDirtySceneTransforms() )
            _manager.flushSceneTransforms();
        const vector<ScenePrimitive*>& listPrimitive = _manager.getPrimitiveRegistry().getAll(); // 메시 계열만 든다(`ScenePrimitive.h` 의 불변식)
        const uint32                   count         = static_cast<uint32>( listPrimitive.size() );
        _listMeshScreenSize.resize( count );
        float32* const arrScreenSize = _listMeshScreenSize.data();
        auto           measureRange  = [&lodSystem, &listPrimitive, arrScreenSize]( uint32 start, uint32 end )
        {
            for ( uint32 index = start; index < end; ++index )
                arrScreenSize[index] = lodSystem.computeScreenSize( static_cast<const MeshComponent&>( *listPrimitive[index] ) );
        };
        engine::runParallelByCost( count, SW_DELEGATE_LAMBDA( ParallelBlockDelegate, measureRange ) );
        // 메시 패스는 머티리얼별 최댓값만 구한다. 글 만들기 · 경로 정규화 · 잠금은 머티리얼마다 한 번이다.
        _mapMaterialScreenSize.clear();
        for ( uint32 index = 0; index < count; ++index )
        {
            const Material* pMaterial = static_cast<MeshComponent&>( *listPrimitive[index] ).getMaterial();
            if ( pMaterial == nullptr || arrScreenSize[index] <= 0.0f )
                continue;
            float32& maxScreenSize = _mapMaterialScreenSize[pMaterial];
            maxScreenSize          = MathUtil::max( maxScreenSize, arrScreenSize[index] );
        }
        for ( const auto& [pMaterial, screenSize] : _mapMaterialScreenSize )
        {
            _listPathScratch.clear();
            pMaterial->collectTexturePaths( _listPathScratch );
            textures.recordScreenSizes( _listPathScratch, screenSize );
        }
    }
} // namespace sw
