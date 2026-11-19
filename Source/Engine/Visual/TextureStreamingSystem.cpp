#include "pch.h"

#include "Engine/Visual/TextureStreamingSystem.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"
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
        // 기다리는 올리기가 없어도 알린다 — 내리기(R6-6)가 오래 안 보인 텍스처와 화면에서 작은 텍스처를 이 알림으로 가린다.
        TextureCache&        textures  = pAssets->getAssetCache<TextureCache>();
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
        engine::runParallelIntoByCost<float32>( _listMeshScreenSize, [&lodSystem, &listPrimitive]( uint32 index, float32& outScreenSize )
        {
            // 화면 크기 × UV 밀도 / 반지름 — 화면에서 UV 한 단위가 덮는 픽셀의 대리값이다(배율은 밀도 · 반지름에 같이 들어 지워진다). 같은 화면 크기라도
            // UV 를 촘촘히 펼친 메시가 위쪽 밉을 먼저 받는다(언리얼 텍스처 스트리밍의 UV density). UV 가 없는 메시는 0 이라 알리지 않는다.
            // 빈 칸(지운 프리미티브 — 레지스트리는 칸을 옮기지 않고 비워 둔다)은 0 이라 알리지 않는다.
            if ( listPrimitive[index] == nullptr )
            {
                outScreenSize = 0.0f;
                return;
            }
            const MeshComponent& mesh    = static_cast<const MeshComponent&>( *listPrimitive[index] );
            const Mesh* const    pMesh   = mesh.getMesh().get();
            const float32        density = pMesh != nullptr ? pMesh->getUVDensity() / MathUtil::max( pMesh->getBoundingRadius(), MathUtil::kEpsilon ) : 0.0f;
            outScreenSize                = lodSystem.computeScreenSize( mesh ) * density;
        } );
        // 메시 패스는 머티리얼별 최댓값만 구한다. 글 만들기 · 경로 정규화 · 잠금은 머티리얼마다 한 번이다.
        _mapMaterialScreenSize.clear();
        for ( uint32 index = 0; index < count; ++index )
        {
            if ( _listMeshScreenSize[index] <= 0.0f ) // 빈 칸도 여기서 빠진다
                continue;
            const Material* pMaterial = static_cast<MeshComponent&>( *listPrimitive[index] ).getMaterial();
            if ( pMaterial == nullptr )
                continue;
            float32& maxScreenSize = _mapMaterialScreenSize[pMaterial];
            maxScreenSize          = MathUtil::max( maxScreenSize, _listMeshScreenSize[index] );
        }
        for ( const auto& [pMaterial, screenSize] : _mapMaterialScreenSize )
        {
            _listPathScratch.clear();
            pMaterial->collectTexturePaths( _listPathScratch );
            textures.recordScreenSizes( _listPathScratch, screenSize );
        }
    }
} // namespace sw
