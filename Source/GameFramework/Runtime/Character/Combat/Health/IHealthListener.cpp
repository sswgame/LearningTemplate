#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/Health/IHealthListener.h"

#include "Core/Container/InlineAllocator.h"
#include "Core/Container/vector.h"

#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    void IHealthListener::broadcast( const GameObject& owner, const HealthChangedEvent& event )
    {
        vector<IHealthListener*, InlineAllocator<IHealthListener*, 4>> listListener;
        owner.findInterfaces<IHealthListener>( listListener );
        for ( IHealthListener* pListener : listListener )
        {
            pListener->onHealthChanged( event );
        }
    }
} // namespace sw
