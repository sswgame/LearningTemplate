#include "pch.h"

#include "Engine/Rendering/TextureStreamingSystem.h"

#include "Core/Container/vector.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Rendering/MeshComponent.h"
#include "Engine/Rendering/MeshLODSystem.h"

namespace sw
{
    /**
     * @brief `-gv_textureStreamByScreenSize=0` — 위쪽 밉을 읽힌 차례로만 올립니다(화면 크기로 정렬하지 않는다). 비교 측정 · 진단용입니다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_textureStreamByScreenSize, true, "Upload the top mips of textures used by large on-screen meshes first (false = in load order)" );
} // namespace sw

namespace sw
{
    SW_SCENE_SERVICE( TextureStreamingSystem, FlushEnd )

    TextureStreamingSystem::TextureStreamingSystem( GameObjectManager& manager )
        : _manager{ manager }
        , _mapMaterialScreenSize{}
        , _listPathScratch{}
    {
    }

    void TextureStreamingSystem::update( float32 deltaTime )
    {
        (void)deltaTime;
        if ( gv_textureStreamByScreenSize == false )
            return;
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
        // 메시 패스는 머티리얼별 최댓값만 구한다. 글 만들기 · 경로 정규화 · 잠금은 머티리얼마다 한 번이다(결과는 최댓값 규칙이라 순서와 무관하다).
        _mapMaterialScreenSize.clear();
        _manager.forEachComponentOfType<MeshComponent>( [&]( MeshComponent* pMesh )
        {
            const Material* pMaterial = pMesh->getMaterial();
            if ( pMaterial == nullptr )
                return;
            const float32 screenSize = lodSystem.computeScreenSize( *pMesh );
            if ( screenSize <= 0.0f )
                return;
            float32& maxScreenSize = _mapMaterialScreenSize[pMaterial];
            maxScreenSize          = MathUtil::max( maxScreenSize, screenSize );
        } );
        for ( const auto& [pMaterial, screenSize] : _mapMaterialScreenSize )
        {
            _listPathScratch.clear();
            pMaterial->collectTexturePaths( _listPathScratch );
            textures.recordScreenSizes( _listPathScratch, screenSize );
        }
    }
} // namespace sw
