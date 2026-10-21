#include "pch.h"

#include "Engine/Object/GameObject/ComponentHandleArgument.h"

#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    SW_LOG_CALLER( "ComponentHandleArgument" );

    const GameObjectManager* ComponentHandleArgument::findManager( const Component& self, string_view functionName )
    {
        const GameObject*        pOwner   = self.getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            SW_LOG_ERROR( "'%#' was not called: its component is not in a scene, so the typed handle argument cannot be checked", string( functionName ).c_str() );
        return pManager;
    }
} // namespace sw
