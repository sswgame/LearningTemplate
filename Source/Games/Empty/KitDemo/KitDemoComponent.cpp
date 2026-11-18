#include "pch.h"

#include "Games/Empty/KitDemo/KitDemoComponent.h"

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

    void KitDemoComponent::serializeResults( StateIO& io, uint32 stateTag, uint32 stateVersion, std::initializer_list<int32*> listResult )
    {
        SW_ASSERT( listResult.size() <= kMaxResultCount );
        io.header( stateTag, stateVersion );
        if ( io.isOk() == false )
            return;
        int32 demoState = _demoState;
        io.value( demoState );
        int32  arrResult[kMaxResultCount] = {};
        uint32 resultCount                = 0;
        for ( const int32* pResult : listResult )
        {
            arrResult[resultCount] = *pResult;
            io.value( arrResult[resultCount] );
            ++resultCount;
        }
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _demoState         = demoState;
        uint32 resultIndex = 0;
        for ( int32* pResult : listResult )
        {
            *pResult = arrResult[resultIndex];
            ++resultIndex;
        }
    }
} // namespace sw
