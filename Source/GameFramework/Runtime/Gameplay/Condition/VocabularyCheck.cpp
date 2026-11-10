/**
 * @file VocabularyCheck.cpp
 * @brief 공통 어휘의 개발 명령 `vocabulary.check <폴더>` 와 감시 값 `Vocabulary.Flag` 입니다. 행동 트리 · 스케줄 · 대화를 한 플래그 표 위에서 차례로 돌립니다.
 * @details 폴더에는 `guard.bt.xml` · `watch.schedules.xml` · `report.dialogue.xml` 이 있다. 실행기는 합치지 않으므로 셋을 각자의 실행기로 돌리고, 문맥(플래그 칸)만 같이 넘긴다.
 *          시나리오 `game.empty.vocabulary` 가 쓴다 — 어휘가 실행기마다 따로면 앞 실행기가 쓴 값을 뒤 실행기가 읽지 못한다.
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/formatString.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Behavior/GameplayContext.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"

#include "GameFramework/Runtime/Character/AI/BehaviorTreeAsset.h"
#include "GameFramework/Runtime/Character/AI/Blackboard.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleSystem.h"
#include "GameFramework/Runtime/Foundation/Condition/GameConditions.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Dialogue/DialogueRunnerComponent.h"

namespace sw
{
    /**
     * @struct VocabularyCheckResult
     * @brief 마지막 `vocabulary.check` 의 플래그 표입니다. 감시 값이 읽는다(게임 스레드만).
     * @details GameFramework 의 모듈 부분(`SW_MODULE_PART( Engine, … )`)이라 엔진 범위와 함께 생기고 사라진다 — 정적 칸이면 엔진을 다시 세워도 지난 점검이 남는다.
     */
    struct VocabularyCheckResult
    {
        static constexpr const utf8* kModulePartName = "VocabularyCheckResult";

        GameFlags _flags;
    };

    SW_MODULE_PART( Engine, VocabularyCheckResult, VocabularyCheckResult );

    namespace
    {
        struct VocabularyCheckInternal
        {
            static constexpr int32 kMinutesPerHour = 60;

            static bool run( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                VocabularyCheckResult* const pResult = game::findSubsystem<VocabularyCheckResult>();
                if ( pResult == nullptr )
                {
                    outReply = "no game services in this process";
                    return true;
                }
                const string& folder = listArgument[0];
                GameFlags&    flags  = pResult->_flags;
                flags.clear();
                GameplayContext world;
                world.set<GameFlagsSlot>( &flags );
                world.set<GameFlagsWriteSlot>( &flags );

                // 1) 행동 트리: 경보가 꺼져 있으면 켠다.
                const shared_ptr<const BehaviorTreeAsset> pTree = SharedAssetCache<BehaviorTreeAsset>::acquire( folder + "/guard.bt.xml" );
                if ( pTree == nullptr )
                {
                    outReply = "the behavior tree could not be read (see the log)";
                    return false;
                }
                BehaviorTreeRunner runner;
                runner.initialize( &pTree->getTree() );
                runner.setGameplayContext( world );
                Blackboard blackboard;
                (void)runner.tick( blackboard, nullptr, 0.0f ); // 결과는 플래그로 본다

                // 2) 스케줄: 경보가 켜진 날에만 문으로 가고, 도착하면 visits 를 올린다.
                const shared_ptr<const ScheduleCatalog> pCatalog = SharedAssetCache<ScheduleCatalog>::acquire( folder + "/watch.schedules.xml" );
                if ( pCatalog == nullptr )
                {
                    outReply = "the schedule could not be read (see the log)";
                    return false;
                }
                ScheduleSystem schedules;
                schedules.setFlags( &flags );
                schedules.setActionContext( world );
                if ( schedules.initialize( pCatalog.get(), ScheduleSystemSettings{}, 7 * kMinutesPerHour ) == false )
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

                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "alarm=%# visits=%# reported=%#", flags.getFlag( "alarm" ), flags.getFlag( "visits" ), flags.getFlag( "reported" ) );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief 감시 값 `Vocabulary.Flag` 입니다. 마지막 `vocabulary.check` 의 플래그 `key` 값입니다(없으면 0). */
            [[nodiscard]] static bool readFlag( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const VocabularyCheckResult* const pResult = game::findSubsystem<VocabularyCheckResult>();
                outValue.setNumber( pResult != nullptr ? static_cast<float64>( pResult->_flags.getFlag( hashed_string( query._key ) ) ) : 0.0 );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( VocabularyCheck, "vocabulary.check", "vocabulary.check <folder>", "Run a behavior tree, a schedule and a dialogue from the folder on one flag table (shared condition and action vocabulary)",
                    &VocabularyCheckInternal::run );
    SW_AUTOMATION_QUERY_WATCH( vocabularyFlag, "Vocabulary.Flag", "Flag key after the last vocabulary.check", &VocabularyCheckInternal::readFlag, false, true );
} // namespace sw
