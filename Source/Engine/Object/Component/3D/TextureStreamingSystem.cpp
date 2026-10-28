#include "pch.h"

#include "Engine/Object/Component/3D/TextureStreamingSystem.h"

#include "Core/Container/vector.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/MeshLODSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"

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
        SW_PROFILE_SCOPE( "GT.TextureStreaming.noteScreenSize" );
        vector<string> listPath;
        _manager.forEachComponentOfType<MeshComponent>( [&]( MeshComponent* pMesh )
        {
            const Material* pMaterial = pMesh->getMaterial();
            if ( pMaterial == nullptr )
                return;
            const float32 screenSize = lodSystem.computeScreenSize( *pMesh );
            if ( screenSize <= 0.0f )
                return;
            listPath.clear();
            pMaterial->collectTexturePaths( listPath );
            for ( const string& path : listPath )
            {
                textures.noteScreenSize( path, screenSize );
            }
        } );
    }
} // namespace sw
