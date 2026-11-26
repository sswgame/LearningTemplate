/**
 * @file ScheduleSystemDevCommands.cpp
 * @brief `ScheduleSystem` 의 개발 명령입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/Subsystem.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"
#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleActivity.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleSaveState.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleSystem.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

#include <mutex>

namespace sw
{
    namespace
    {
        struct ScheduleTraceCommandDevInternal
        {
            /**
             * @brief `schedule.trace <npc id | *>`: 살아 있는 일정 시스템마다 그 NPC 의 "왜 여기 있나" 와 오늘 시간표를 답합니다(개발 명령 — 배포본에는 등록되지 않는다).
             * @details 예전에는 테스트용 변수(`-gv_scheduleTrace`)를 일정 시스템이 틱마다 읽었다. 묻는 쪽이 부르는 명령으로 바꿔 런타임 틱에서 개발 분기를 뺐다.
             *          시작부터 찍으려면 `-gv_devConsoleExec=schedule.trace *`.
             */
            static bool runTrace( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                const string&                 request = listArgument[0];
                const bool                    bAll    = request == "*";
                vector<const ScheduleSystem*> listSystem;
                ScheduleSystem::collectLiveSystems( listSystem );
                string text;
                for ( const ScheduleSystem* pSystem : listSystem )
                {
                    for ( int32 npcIndex = 0; npcIndex < pSystem->getNPCCount(); ++npcIndex )
                    {
                        if ( bAll == false && pSystem->getNPCID( npcIndex ) != hashed_string( request ) )
                            continue;
                        pSystem->explainNPC( npcIndex, text );
                        outReply += text;
                        pSystem->dumpTimeline( npcIndex, text );
                        outReply += text;
                    }
                }
                if ( outReply.empty() )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "no live schedule NPC matches '%#'", request );
                    outReply = string( replyBuilder.view() );
                }
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( ScheduleTrace, "schedule.trace", "schedule.trace <npc id | *>", "Reply the why-am-I-here trace and today's timeline of this schedule NPC (* = all)",
                    &ScheduleTraceCommandDevInternal::runTrace );
} // namespace sw
