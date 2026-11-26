#include "pch.h"

#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

#include "Core/Math/SecureRandom.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/Subsystem/SceneNetLink.h"

namespace sw
{
    WorldSeed WorldSeed::makeForWorld( const Component& component, uint32 fixedSeed )
    {
        const NetLinkValue* const pLink = SceneNetLink::findBoundFor( component );
        if ( pLink == nullptr || pLink->_role != NetLinkRole::Server )
            return WorldSeed{ fixedSeed };
        const uint32 seed = static_cast<uint32>( SecureRandom::nextUint64() ); // 운영체제 난수
        return WorldSeed{ seed != 0 ? seed : 1u };                             // 난수 열은 0 을 받지 않는다(`DeterministicRandom32`)
    }
} // namespace sw
