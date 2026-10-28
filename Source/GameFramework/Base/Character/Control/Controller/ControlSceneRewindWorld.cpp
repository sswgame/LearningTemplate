#include "pch.h"

#include "GameFramework/Base/Character/Control/Controller/ControlSceneRewindWorld.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Serializer/SerializePurpose.h"

#include "GameFramework/Base/Character/Control/ControlSystem.h"
#include "GameFramework/Base/Character/Control/Controller/IntentPlayback.h"
#include "GameFramework/Base/Foundation/Framework/Flow/GameInstance.h"

namespace sw
{
    SW_LOG_CALLER( "ControlSceneRewindWorld" );

    ControlSceneRewindWorld::ControlSceneRewindWorld( GameInstance& instance, GameObjectManager& manager, const ControlIntentHistory& history, float32 fixedDeltaSeconds )
        : _instance{ instance }
        , _manager{ manager }
        , _history{ history }
        , _fixedDeltaSeconds{ fixedDeltaSeconds }
    {
    }

    uint32 ControlSceneRewindWorld::getTick() const
    {
        const ControlSystem* pControl = ControlSystem::find( _manager );
        return pControl != nullptr ? pControl->getTick() : 0;
    }

    bool ControlSceneRewindWorld::captureKey( vector<uint8>& outBytes )
    {
        return _instance.captureSnapshot( SerializePurpose::Snapshot, outBytes );
    }

    bool ControlSceneRewindWorld::restoreKey( uint32 keyTick, const vector<uint8>& bytes )
    {
        if ( _instance.restoreSnapshot( SerializePurpose::Snapshot, bytes ) == false )
            return false;
        // 장면을 비울 때 조종 시스템도 떨어졌다. 폰이 다시 등록되며 붙은 시스템의 틱을 키 틱으로 맞춘다.
        ControlSystem::ensureFor( _manager ).setTick( keyTick );
        uint32 skippedCount = 0;
        IntentPlayback::start( _manager, _history, false, keyTick, &skippedCount );
        if ( skippedCount > 0 )
            SW_LOG_WARNING( "Rewind to tick %#: %# track(s) could not be replayed from there", keyTick, skippedCount );
        return true;
    }

    void ControlSceneRewindWorld::stepTick()
    {
        _manager.update( _fixedDeltaSeconds );
    }
} // namespace sw
