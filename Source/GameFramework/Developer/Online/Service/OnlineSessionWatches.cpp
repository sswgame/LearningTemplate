/**
 * @file OnlineSessionWatches.cpp
 * @brief 온라인 세션의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/PlatformSocketUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/WallClock.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Online/Security/NetSecurity.h"
#include "GameFramework/Runtime/Online/Service/IOnlineLocalServer.h"
#include "GameFramework/Runtime/Online/Service/OnlineClientRegistry.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineSession.h"

namespace sw
{
    namespace
    {
        struct OnlineSessionWatchesInternal
        {
            // 개발 명령 - 게임 코드 없이 세션을 시나리오와 스크립트가 움직인다.
            [[nodiscard]] static bool readState( const GameObjectManager* pManager, AutomationWatchValue& outValue )
            {
                (void)pManager;
                const OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                    return false;
                outValue.setEnum( pSession->getState() );
                return true;
            }

            [[nodiscard]] static bool readClientCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->getClientCount() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( onlineSessionState, "Online.SessionState", "OnlineClientState name of the online session", &OnlineSessionWatchesInternal::readState );
    SW_AUTOMATION_WATCH( onlineSessionClientCount, "Online.SessionClientCount", "Kit clients the online session built from the loaded kits", &OnlineSessionWatchesInternal::readClientCount );
} // namespace sw
