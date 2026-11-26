/**
 * @file GameSaveSlotsDevCommands.cpp
 * @brief 게임 저장 슬롯(`GameSaveSlots`)의 개발 명령입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Network/GameNetSession.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Foundation/Event/GameEvents.h"
#include "GameFramework/Runtime/Foundation/Flow/GameInstance.h"
#include "GameFramework/Runtime/Foundation/Flow/GameSceneFlow.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/Save/Autosave.h"
#include "GameFramework/Runtime/Foundation/Save/GameLocalStore.h"
#include "GameFramework/Runtime/Foundation/Save/GameSaveSlots.h"
#include "GameFramework/Runtime/Foundation/Save/GameStateSnapshot.h"
#include "GameFramework/Runtime/Foundation/Save/SaveLock.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct GameSaveSlotsDevCommandsInternal
        {
            /** @brief `game.save [슬롯]` · `game.load [슬롯]` 의 몸통입니다. 슬롯을 비우면 기본 슬롯입니다. 맡기기만 하고 끝은 감시 값 `Save.Written` · `Save.Loaded` 로 본다. */
            template <bool bSave>
            static bool runSlotCommand( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                GameSaveSlots* const pSaveSlots = game::findSubsystem<GameSaveSlots>();
                const string_view    slot       = listArgument.empty() ? string_view{} : string_view( listArgument[0] );
                const utf8* const    pVerb      = bSave ? "game.save" : "game.load";
                if ( pSaveSlots == nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "%#: no game is running", pVerb );
                    outReply = string( replyBuilder.view() );
                    return true;
                }
                const bool                               bQueued = bSave ? pSaveSlots->save( slot ) : pSaveSlots->load( slot );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "%#: %# '%#'", pVerb, bQueued ? "queued" : "refused", slot.empty() ? string_view( "<default>" ) : slot );
                outReply = string( replyBuilder.view() );
                return true;
            }

            template <bool bWritten>
        };
    } // namespace

    // 세이브 메뉴 자리의 개발 명령 — 시나리오가 플레이어의 "저장 · 불러오기" 를 같은 창구(`save` · `load`)로 부른다. 메뉴 화면은 아직 없다.
    SW_DEV_COMMAND( GameSave, "game.save", "game.save [slot]", "Save the scene and game state to a save slot (empty: the default slot of gamesettings)",
                    &GameSaveSlotsDevCommandsInternal::runSlotCommand<true> );
    SW_DEV_COMMAND( GameLoad, "game.load", "game.load [slot]", "Load the scene and game state from a save slot (empty: the default slot of gamesettings)",
                    &GameSaveSlotsDevCommandsInternal::runSlotCommand<false> );
    // 막기는 씬 로드(워커)와 세이브 읽기(저장소)의 끝을 따르므로 벽시계 감시 값이다.
    // 활동 수 없음: 저장 잠금은 게임 상태(씬 로드 · 깨진 복원 · 컷신)가 풀어 주는 문이다 — 셀 수 있는 일의 양이 아니다.
} // namespace sw
