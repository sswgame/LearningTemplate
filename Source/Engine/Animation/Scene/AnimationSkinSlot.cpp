#include "pch.h"

#include "Engine/Animation/Scene/AnimationSkinSlot.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Container/vector.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    AnimationSkinSlot::AnimationSkinSlot()
    {
        AnimationSkinSlotRegistry& registry = engine::getSubsystem<AnimationSkinSlotRegistry>();
        ScopedLock<SpinLock>       lock{ registry._spinLock };
        if ( registry._listFree.empty() )
        {
            _slot = registry._nextSlot++;
            registry._listGeneration.push_back( 0 );
        }
        else
        {
            _slot = registry._listFree.back();
            registry._listFree.pop_back();
        }
        _generation = registry._listGeneration[_slot];
    }

    AnimationSkinSlot::~AnimationSkinSlot()
    {
        AnimationSkinSlotRegistry& registry = engine::getSubsystem<AnimationSkinSlotRegistry>();
        ScopedLock<SpinLock>       lock{ registry._spinLock };
        ++registry._listGeneration[_slot]; // 다음 주인은 새 세대다
        registry._listFree.push_back( _slot );
    }

    SW_ENGINE_SERVICE_SUBSYSTEM( AnimationSkinSlotRegistry, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( AnimationSkinSlotRegistry ) );
} // namespace sw
