#include "pch.h"

#include "Games/Playground/KitDemo/KitDemoComponent.h"

#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    KitDemoComponent::KitDemoComponent()
        : _demoState{ 0 }
    {
    }

    KitDemoComponent::~KitDemoComponent() = default;

    void KitDemoComponent::updateGame( float32 deltaTime )
    {
        (void)deltaTime;
    }

    void KitDemoComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        (void)manager;
        (void)bRespawnViews;
    }

    void KitDemoComponent::serializeResults( StateIO& io, uint32 stateTag, uint32 stateVersion )
    {
        io.header( stateTag, stateVersion );
        io.end();
    }
} // namespace sw
