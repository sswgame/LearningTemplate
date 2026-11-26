/**
 * @file FeatureDemoDevCommands.cpp
 * @brief 기능 시연 컴포넌트(차례 · 경비 · 제작 · 스마트 오브젝트)의 개발 명령입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/String/Name/hashed_string.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Character/AI/AIStimulusSourceComponent.h"

#include "Games/Playground/FeatureDemo/CraftingDemoComponent.h"
#include "Games/Playground/FeatureDemo/GuardDemoComponent.h"
#include "Games/Playground/FeatureDemo/SmartObjectDemoComponent.h"
#include "Games/Playground/FeatureDemo/TurnOrderDemoComponent.h"

namespace sw
{
    namespace
    {
        /** @brief 시연 명령이 함께 쓰는 대상 찾기 · 답 적기입니다. */
        struct FeatureDemoCommandInternal
        {
            /** @brief 활성 씬에서 이름 @p objectName 의 열린 시연(@p TDemo)입니다. 없으면 까닭을 @p outReply 에 적고 nullptr 입니다. */
            template <typename TDemo>
            [[nodiscard]] static TDemo* findStarted( const string& objectName, string& outReply )
            {
                GameObject* pObject = engine::findActiveObject( hashed_string( objectName ) );
                TDemo*      pDemo   = pObject != nullptr ? pObject->getComponent<TDemo>() : nullptr;
                if ( pDemo != nullptr && pDemo->isStarted() )
                    return pDemo;
                reply( outReply, "no object named '%#' with a started demo component", objectName );
                return nullptr;
            }

            /** @brief 형식 글로 답을 적습니다. */
            template <typename... TArgs>
            static void reply( string& outReply, const utf8* pFormat, const TArgs&... args )
            {
                StringBuilder<constant::buffer::kMax256> builder;
                builder.appendFormat( pFormat, args... );
                outReply = string( builder.view() );
            }
        };

        struct TurnOrderDemoDevCommandsInternal
        {
            /** @brief 명령 인자 `<demo object> <actor> …` 의 앞 둘을 읽습니다. 시연이 없거나 없는 행위자면 까닭을 적고 nullptr 입니다. */
            [[nodiscard]] static TurnOrderDemoComponent* findActor( const vector<string>& listArgument, int32& outActorID, string& outReply )
            {
                if ( StringUtil::parseInt( listArgument[1], outActorID ) == false )
                    return nullptr;
                TurnOrderDemoComponent* pDemo = FeatureDemoCommandInternal::findStarted<TurnOrderDemoComponent>( listArgument[0], outReply );
                if ( pDemo == nullptr || pDemo->hasActor( outActorID ) )
                    return pDemo;
                FeatureDemoCommandInternal::reply( outReply, "no actor %# in the turn order", outActorID );
                return nullptr;
            }

            static void writeReply( const TurnOrderDemoComponent& demo, string& outReply )
            {
                FeatureDemoCommandInternal::reply( outReply, "turn %#, next %#", demo.getCurrentActor(), demo.getOrderText() );
            }

            static bool runNext( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                TurnOrderDemoComponent* pDemo = FeatureDemoCommandInternal::findStarted<TurnOrderDemoComponent>( listArgument[0], outReply );
                if ( pDemo == nullptr )
                    return false;
                pDemo->advanceTurn();
                writeReply( *pDemo, outReply );
                return true;
            }

            static bool runDelay( const vector<string>& listArgument, string& outReply )
            {
                float32 amount  = 0.0f;
                int32   actorID = 0;
                if ( listArgument.size() != 3 || StringUtil::parseFloat( listArgument[2], amount ) == false )
                    return false;
                TurnOrderDemoComponent* pDemo = findActor( listArgument, actorID, outReply );
                if ( pDemo == nullptr )
                    return false;
                pDemo->delayActor( actorID, amount );
                writeReply( *pDemo, outReply );
                return true;
            }

            static bool runPriority( const vector<string>& listArgument, string& outReply )
            {
                int32 priority = 0;
                int32 actorID  = 0;
                if ( listArgument.size() != 3 || StringUtil::parseInt( listArgument[2], priority ) == false )
                    return false;
                TurnOrderDemoComponent* pDemo = findActor( listArgument, actorID, outReply );
                if ( pDemo == nullptr )
                    return false;
                pDemo->setPriority( actorID, priority );
                writeReply( *pDemo, outReply );
                return true;
            }

            static bool runRemove( const vector<string>& listArgument, string& outReply )
            {
                int32 actorID = 0;
                if ( listArgument.size() != 2 )
                    return false;
                TurnOrderDemoComponent* pDemo = findActor( listArgument, actorID, outReply );
                if ( pDemo == nullptr )
                    return false;
                pDemo->removeActor( actorID );
                writeReply( *pDemo, outReply );
                return true;
            }
        };

        struct GuardDemoDevCommandsInternal
        {
            static bool runLock( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                GuardDemoComponent* pGuard = FeatureDemoCommandInternal::findStarted<GuardDemoComponent>( listArgument[0], outReply );
                if ( pGuard == nullptr )
                    return false;
                (void)pGuard->lockBest(); // 잡을 것이 없으면 풀린 채다 — 답과 상태 칸이 알린다
                FeatureDemoCommandInternal::reply( outReply, "locked %#", pGuard->getLockedTarget() );
                return true;
            }

            static bool runCycle( const vector<string>& listArgument, string& outReply )
            {
                const bool bRight = listArgument.size() == 2 && StringUtil::equals( listArgument[1], "right", true );
                const bool bLeft  = listArgument.size() == 2 && StringUtil::equals( listArgument[1], "left", true );
                if ( bRight == false && bLeft == false )
                    return false;
                GuardDemoComponent* pGuard = FeatureDemoCommandInternal::findStarted<GuardDemoComponent>( listArgument[0], outReply );
                if ( pGuard == nullptr )
                    return false;
                pGuard->cycleLock( bRight ? 1 : -1 );
                FeatureDemoCommandInternal::reply( outReply, "locked %#", pGuard->getLockedTarget() );
                return true;
            }

            static bool runRelease( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                GuardDemoComponent* pGuard = FeatureDemoCommandInternal::findStarted<GuardDemoComponent>( listArgument[0], outReply );
                if ( pGuard == nullptr )
                    return false;
                pGuard->releaseLock();
                outReply = "released";
                return true;
            }

            /** @brief `noise.report <object> <radius>` — 자극 원천이 낸 소리 반경(m)을 겁니다. 0 이면 거둡니다. */
            static bool runNoise( const vector<string>& listArgument, string& outReply )
            {
                float32 radius = 0.0f;
                if ( listArgument.size() != 2 || StringUtil::parseFloat( listArgument[1], radius ) == false )
                    return false;
                GameObject*                pObject = engine::findActiveObject( hashed_string( listArgument[0] ) );
                AIStimulusSourceComponent* pSource = pObject != nullptr ? pObject->getComponent<AIStimulusSourceComponent>() : nullptr;
                if ( pSource == nullptr )
                {
                    FeatureDemoCommandInternal::reply( outReply, "no object named '%#' with an AIStimulusSourceComponent", listArgument[0] );
                    return false;
                }
                pSource->reportNoise( radius );
                FeatureDemoCommandInternal::reply( outReply, "%# makes noise of radius %#", listArgument[0], radius );
                return true;
            }
        };

        struct CraftingDemoDevCommandsInternal
        {
            static bool runEnqueue( const vector<string>& listArgument, string& outReply )
            {
                int32      count     = 1;
                const bool bBadCount = listArgument.size() == 3 && StringUtil::parseInt( listArgument[2], count ) == false;
                if ( listArgument.size() < 2 || listArgument.size() > 3 || bBadCount )
                    return false;
                CraftingDemoComponent* pDemo = FeatureDemoCommandInternal::findStarted<CraftingDemoComponent>( listArgument[0], outReply );
                if ( pDemo == nullptr )
                    return false;
                if ( pDemo->enqueue( hashed_string( listArgument[1] ), count ).has_value() == false )
                {
                    outReply = "the crafting demo has no character with a bag";
                    return false;
                }
                FeatureDemoCommandInternal::reply( outReply, "craft %# x%#: %#", listArgument[1], count, pDemo->getLastResult().c_str() ); // 거절도 명령의 답이다
                return true;
            }

            static bool runCancel( const vector<string>& listArgument, string& outReply )
            {
                int32 index = 0;
                if ( listArgument.size() != 2 || StringUtil::parseInt( listArgument[1], index ) == false )
                    return false;
                CraftingDemoComponent* pDemo = FeatureDemoCommandInternal::findStarted<CraftingDemoComponent>( listArgument[0], outReply );
                if ( pDemo == nullptr )
                    return false;
                outReply = pDemo->cancel( index ) ? "cancelled" : "not cancelled (no such job or no room for its inputs)";
                return true;
            }

            static bool runLearn( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 )
                    return false;
                CraftingDemoComponent* pDemo = FeatureDemoCommandInternal::findStarted<CraftingDemoComponent>( listArgument[0], outReply );
                if ( pDemo == nullptr )
                    return false;
                pDemo->learn( hashed_string( listArgument[1] ) );
                FeatureDemoCommandInternal::reply( outReply, "learned %#", listArgument[1] );
                return true;
            }

            static bool runLevel( const vector<string>& listArgument, string& outReply )
            {
                int32 level = 0;
                if ( listArgument.size() != 2 || StringUtil::parseInt( listArgument[1], level ) == false )
                    return false;
                CraftingDemoComponent* pDemo = FeatureDemoCommandInternal::findStarted<CraftingDemoComponent>( listArgument[0], outReply );
                if ( pDemo == nullptr )
                    return false;
                pDemo->setLevel( level );
                FeatureDemoCommandInternal::reply( outReply, "crafting level %#", level );
                return true;
            }
        };

        struct SmartObjectDemoDevCommandsInternal
        {
            static constexpr int32   kMinutesPerHour = 60;
            static constexpr float32 kMaxHour        = 24.0f; ///< 첫날의 끝

            /** @brief `schedule.advance <demo object> <hour>` — 첫날 그 시각(소수 시간)까지 일정을 흘립니다. */
            static bool runAdvance( const vector<string>& listArgument, string& outReply )
            {
                float32 hour = 0.0f;
                if ( listArgument.size() != 2 || StringUtil::parseFloat( listArgument[1], hour ) == false || hour < 0.0f || hour > kMaxHour )
                    return false;
                SmartObjectDemoComponent* pDemo = FeatureDemoCommandInternal::findStarted<SmartObjectDemoComponent>( listArgument[0], outReply );
                if ( pDemo == nullptr )
                    return false;
                pDemo->advanceToMinute( static_cast<int32>( hour * static_cast<float32>( kMinutesPerHour ) ) );
                FeatureDemoCommandInternal::reply( outReply, "schedule at hour %#", hour );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( TurnNext, "turn.next", "turn.next <demo object>", "Give the turn to the next actor of a turn order demo", &TurnOrderDemoDevCommandsInternal::runNext );
    SW_DEV_COMMAND( TurnDelay, "turn.delay", "turn.delay <demo object> <actor> <turns>", "Delay an actor of a timeline turn order demo", &TurnOrderDemoDevCommandsInternal::runDelay );
    SW_DEV_COMMAND( TurnPriority, "turn.priority", "turn.priority <demo object> <actor> <priority>", "Set an actor's priority and restart the round of a turn order demo", &TurnOrderDemoDevCommandsInternal::runPriority );
    SW_DEV_COMMAND( TurnRemove, "turn.remove", "turn.remove <demo object> <actor>", "Take an actor out of a turn order demo", &TurnOrderDemoDevCommandsInternal::runRemove );
    SW_DEV_COMMAND( GuardLock, "guard.lock", "guard.lock <guard object>", "Lock on to the best target the guard sees", &GuardDemoDevCommandsInternal::runLock );
    SW_DEV_COMMAND( GuardCycle, "guard.cycle", "guard.cycle <guard object> <left|right>", "Move the guard's lock to the next target on that side", &GuardDemoDevCommandsInternal::runCycle );
    SW_DEV_COMMAND( GuardRelease, "guard.release", "guard.release <guard object>", "Release the guard's lock", &GuardDemoDevCommandsInternal::runRelease );
    SW_DEV_COMMAND( NoiseReport, "noise.report", "noise.report <object> <radius>", "Set the noise radius of a stimulus source (0 clears it)", &GuardDemoDevCommandsInternal::runNoise );
    SW_DEV_COMMAND( CraftEnqueue, "craft.enqueue", "craft.enqueue <demo object> <recipe> [count]", "Queue a recipe at a crafting demo", &CraftingDemoDevCommandsInternal::runEnqueue );
    SW_DEV_COMMAND( CraftCancel, "craft.cancel", "craft.cancel <demo object> <job index>", "Cancel a queued job of a crafting demo and return its inputs", &CraftingDemoDevCommandsInternal::runCancel );
    SW_DEV_COMMAND( CraftLearn, "craft.learn", "craft.learn <demo object> <recipe>", "Learn a recipe at a crafting demo", &CraftingDemoDevCommandsInternal::runLearn );
    SW_DEV_COMMAND( CraftLevel, "craft.level", "craft.level <demo object> <level>", "Set the crafting level of a crafting demo", &CraftingDemoDevCommandsInternal::runLevel );
    SW_DEV_COMMAND( ScheduleAdvance, "schedule.advance", "schedule.advance <demo object> <hour>", "Advance the schedule of a smart object demo to an hour of day 0",
                    &SmartObjectDemoDevCommandsInternal::runAdvance );
} // namespace sw
