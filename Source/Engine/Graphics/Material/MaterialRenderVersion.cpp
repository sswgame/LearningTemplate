#include "pch.h"

#include "Engine/Graphics/Material/MaterialRenderVersion.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"
#include "Engine/Graphics/Texture/TextureCache.h"

namespace sw
{
    void MaterialRenderVersion::bump()
    {
        ++_value;
        MaterialCache* const pCache = MaterialCache::findBound();
        if ( pCache != nullptr )
            pCache->bumpRenderValueGeneration(); // 엔진 없이 만든 머티리얼은 읽는 쪽(`capture`)도 "모름" 이라 올릴 곳이 없다
    }

    MaterialRenderInputs MaterialRenderInputs::capture()
    {
        MaterialRenderInputs inputs{};
        inputs._shaderReflectionGeneration = ShaderReflectionLibrary::getCacheGeneration();
        if ( engine::areEngineServicesBound() == false )
            return inputs; // 머티리얼 · 텍스처 캐시를 볼 수 없다 — 모르는 값이다(건너뛰지 않는다)
        AssetManager& assets            = engine::getSubsystem<AssetManager>();
        inputs._materialRevision        = assets.getAssetCache<MaterialCache>().getRenderValueGeneration();
        inputs._textureReloadGeneration = assets.getAssetCache<TextureCache>().getReloadGeneration();
        inputs._bKnown                  = SW_TRUE;
        return inputs;
    }
} // namespace sw
