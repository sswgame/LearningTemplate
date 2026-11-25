#include "pch.h"

#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

#include "Core/Serialization/ByteStream.h"
#include "Core/UUID/UUID.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"

namespace sw
{
    WorldSeed WorldSeed::makeForWorld( const Component& component, uint32 fixedSeed )
    {
        const NetLinkValue* const pLink = SceneNetLink::findBoundFor( component );
        if ( pLink == nullptr || pLink->_role != NetLinkRole::Server )
            return WorldSeed{ fixedSeed };
        const UUID   uuid = UUID::generate(); // 운영체제 난수
        ByteReader   reader( uuid._arrBytes, sizeof( uuid._arrBytes ) );
        const uint32 seed = reader.readUint32();
        return WorldSeed{ seed != 0 ? seed : 1u }; // 난수 열은 0 을 받지 않는다(`DeterministicRandom32`)
    }
} // namespace sw
