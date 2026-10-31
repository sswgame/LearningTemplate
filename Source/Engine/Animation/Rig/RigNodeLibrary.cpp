#include "pch.h"

#include "Engine/Animation/Rig/RigNodeLibrary.h"

#include "Engine/Animation/Rig/RigConstraintNodes.h"
#include "Engine/Animation/Rig/RigIKNodes.h"
#include "Engine/Animation/Rig/RigSecondaryNodes.h"

namespace sw
{
    bool RigNodeLibrary::registerEngineNodes( RigNodeRegistry& registry )
    {
        RigIKNodes::registerNodes( registry );
        RigConstraintNodes::registerNodes( registry );
        RigSecondaryNodes::registerNodes( registry );
        return true;
    }
} // namespace sw
