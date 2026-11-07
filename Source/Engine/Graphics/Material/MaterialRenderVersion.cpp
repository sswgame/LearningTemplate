#include "pch.h"

#include "Engine/Graphics/Material/MaterialRenderVersion.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"
#include "Engine/Graphics/Texture/TextureCache.h"

namespace sw
{
    atomic<uint64> MaterialRenderVersion::_s_global{ 0 };

    MaterialRenderInputs MaterialRenderInputs::capture()
    {
        MaterialRenderInputs inputs{};
        inputs._materialRevision           = MaterialRenderVersion::getGlobal();
        inputs._shaderReflectionGeneration = ShaderReflectionLibrary::getCacheGeneration();
        if ( engine::areEngineServicesBound() == false )
            return inputs; // 텍스처 캐시를 볼 수 없다 — 모르는 값이다(건너뛰지 않는다)
        inputs._textureReloadGeneration = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().getReloadGeneration();
        inputs._bKnown                  = SW_TRUE;
        return inputs;
    }
} // namespace sw
