/**
 * @file VocabularyCheck.cpp
 * @brief 공통 어휘의 개발 명령 `vocabulary.check <폴더>` 와 감시 값 `Vocabulary.Flag` 입니다. 행동 트리 · 스케줄 · 대화를 한 플래그 표 위에서 차례로 돌립니다.
 * @details 폴더에는 `guard.bt.xml` · `watch.schedules.xml` · `report.dialogue.xml` 이 있다. 실행기는 합치지 않으므로 셋을 각자의 실행기로 돌리고, 문맥(플래그 칸)만 같이 넘긴다.
 *          시나리오 `game.empty.vocabulary` 가 쓴다 — 어휘가 실행기마다 따로면 앞 실행기가 쓴 값을 뒤 실행기가 읽지 못한다.
 */
#include "pch.h"

#include "Core/Container/formatString.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Vocabulary/GameplayContext.h"

#include "GameFramework/Runtime/Character/AI/BehaviorTreeAsset.h"
#include "GameFramework/Runtime/Character/AI/Blackboard.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleSystem.h"
#include "GameFramework/Runtime/UI/Dialogue/DialogueRunnerComponent.h"
#include "GameFramework/Runtime/World/Query/GameConditions.h"
#include "GameFramework/Runtime/World/Query/GameFlags.h"

namespace sw
{
    namespace
    {
        struct VocabularyCheckInternal
        {
            static constexpr int32 kMinutesPerHour = 60;

            /** @brief 마지막 점검의 플래그입니다. 감시 값이 읽는다(게임 스레드만). */
            static GameFlags& getLastFlags()
            {
                static GameFlags s_flags;
                return s_flags;
            }

            static bool run( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                const string& folder = listArgument[0];
                GameFlags&    flags  = getLastFlags();
                flags.clear();
                GameplayContext world;
                world.set<GameFlagsSlot>( &flags );
                world.set<GameFlagsWriteSlot>( &flags );

                // 1) 행동 트리: 경보가 꺼져 있으면 켠다.
                BehaviorTreeAsset tree;
                if ( tree.loadFromAsset( AssetID{ folder + "/guard.bt.xml" } ) == false )
                {
                    outReply = "the behavior tree could not be read (see the log)";
                    return false;
                }
                BehaviorTreeRunner runner;
                runner.initialize( &tree.getTree() );
                runner.setGameplayContext( world );
                Blackboard blackboard;
                (void)runner.tick( blackboard, nullptr, 0.0f ); // 결과는 플래그로 본다

                // 2) 스케줄: 경보가 켜진 날에만 문으로 가고, 도착하면 visits 를 올린다.
                ScheduleCatalog catalog;
                if ( catalog.loadFromAsset( AssetID{ folder + "/watch.schedules.xml" } ) == false )
                {
                    outReply = "the schedule could not be read (see the log)";
                    return false;
                }
                ScheduleSystem schedules;
                schedules.setFlags( &flags );
                schedules.setActionContext( world );
                if ( schedules.initialize( &catalog, ScheduleSystemSettings{}, 7 * kMinutesPerHour ) == false )
                {
                    outReply = "the schedule system did not start";
                    return false;
                }
                schedules.advanceTo( 9 * kMinutesPerHour );

                // 3) 대화: 둘을 모두 봤으면 보고를 남긴다.
                DialogueRunnerComponent dialogue;
                if ( dialogue.loadGraphFile( AssetID{ folder + "/report.dialogue.xml" } ) == false )
                {
                    outReply = "the dialogue could not be read (see the log)";
                    return false;
                }
                dialogue.setFlags( &flags );
                (void)dialogue.startDialogue(); // 결과는 플래그로 본다

                outReply = formatToString( "alarm=%# visits=%# reported=%#", flags.getFlag( "alarm" ), flags.getFlag( "visits" ), flags.getFlag( "reported" ) );
                return true;
            }

            /** @brief 감시 값 `Vocabulary.Flag` 입니다. 마지막 `vocabulary.check` 의 플래그 `key` 값입니다(없으면 0). */
            [[nodiscard]] static bool readFlag( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                outValue.setNumber( static_cast<float64>( getLastFlags().getFlag( hashed_string( query._key ) ) ) );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( VocabularyCheck, "vocabulary.check", "vocabulary.check <folder>", "Run a behavior tree, a schedule and a dialogue from the folder on one flag table (shared condition and action vocabulary)",
                    &VocabularyCheckInternal::run );
    SW_AUTOMATION_QUERY_WATCH( vocabularyFlag, "Vocabulary.Flag", "Flag key after the last vocabulary.check", &VocabularyCheckInternal::readFlag, false, true );
} // namespace sw
