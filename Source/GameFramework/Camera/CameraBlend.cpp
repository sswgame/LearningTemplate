#include "pch.h"

#include "GameFramework/Camera/CameraBlend.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    CameraPose blendPoses( const CameraPose& from, const CameraPose& to, float32 weight )
    {
        CameraPose pose;
        pose._position      = float3::lerp( from._position, to._position, weight );
        pose._rotation      = quaternion::slerp( from._rotation, to._rotation, weight );
        pose._fieldOfViewY  = MathUtil::lerp( from._fieldOfViewY, to._fieldOfViewY, weight );
        pose._orthoHeight   = MathUtil::lerp( from._orthoHeight, to._orthoHeight, weight );
        pose._nearPlane     = MathUtil::lerp( from._nearPlane, to._nearPlane, weight );
        pose._farPlane      = MathUtil::lerp( from._farPlane, to._farPlane, weight );
        pose._bOrthographic = weight < 0.5f ? from._bOrthographic : to._bOrthographic;
        return pose;
    }
} // namespace sw
