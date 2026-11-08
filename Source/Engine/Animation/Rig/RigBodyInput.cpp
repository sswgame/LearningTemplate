#include "pch.h"

#include "Engine/Animation/Rig/RigBodyInput.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    RigBodyInput::RigBodyInput()
        : _listJiggleGroup{}
        , _pendingImpulse{}
        , _frameImpulse{}
        , _footLift{ 0.0f }
        , _motionScale{ 1.0f }
    {
    }

    void RigBodyInput::setMotionScale( float32 scale )
    {
        _motionScale = MathUtil::clamp( scale, 0.0f, kMaxMotionScale );
    }

    void RigBodyInput::beginFrame()
    {
        _frameImpulse   = _pendingImpulse;
        _pendingImpulse = float3{};
    }

    const RigJiggleGroupParams* RigBodyInput::findJiggleGroup( const hashed_string& group ) const
    {
        return VectorUtil::findBy<&RigJiggleGroupParams::_group>( _listJiggleGroup, group );
    }
} // namespace sw
