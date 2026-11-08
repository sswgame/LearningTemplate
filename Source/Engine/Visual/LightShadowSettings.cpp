#include "pch.h"

#include "Engine/Visual/LightShadowSettings.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Shader/Binding/GPULight.h"

namespace sw
{
    void LightShadowSettings::writeGPULightFields( GPULight& outLight ) const
    {
        outLight._extra._x = MathUtil::max( _sourceSize, 0.0f );
        outLight._extra._y = MathUtil::max( _contactShadowLength, 0.0f );
    }
} // namespace sw
