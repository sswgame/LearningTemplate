#include "pch.h"

#include "GameFramework/Runtime/Character/Control/Controller/PlayerStateComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/ComponentStateStore.h"

namespace sw
{
    namespace
    {
        /** @brief 플레이어마다의 진행 플래그를 모든 게임의 상태 스냅샷에 싣습니다(게임마다 올리지 않는다). */
        const StatefulComponentRegistrar<PlayerStateComponent> s_stateRegistration{ "PlayerStateComponent" };
    } // namespace

    PlayerStateComponent::PlayerStateComponent()
        : _progressFlags{}
    {
    }

    void PlayerStateComponent::serializeState( StateIO& io )
    {
        uint32 version = kStateVersion;
        io.value( version );
        if ( io.isReading() && ( io.isOk() == false || version != kStateVersion ) )
        {
            SW_LOG_WARNING_ID( "PlayerState.StateVersion", "Player state on '%#': saved state version %# did not read - keeping the current progress flags",
                               getOwner() != nullptr ? getOwner()->getName().c_str() : "?", version );
            io.fail();
            return;
        }
        _progressFlags.serializeState( io );
    }

    void PlayerStateComponent::restoreState( vector<uint8>&& bytes )
    {
        Archive archive( bytes.data(), static_cast<uint64>( bytes.size() ) );
        (void)readState( archive ); // 맞지 않는 묶음은 serializeState 가 알리고 지금 값으로 둔다
    }
} // namespace sw
