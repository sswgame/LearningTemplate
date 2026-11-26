/**
 * @file GameSaveSlotsWatches.cpp
 * @brief 게임 저장 슬롯(`GameSaveSlots`)의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
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
        struct GameSaveSlotsWatchesInternal
        {
            /** @brief 감시 값 `Save.Blocked` 입니다 — 세이브 막기 문이 들렸으면 1(씬 로드 · 깨진 복원 · 연출). */
            [[nodiscard]] static bool readSaveBlocked( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const SaveLock* const pSaveLock = game::findSubsystem<SaveLock>();
                if ( pSaveLock == nullptr )
                    return false;
                outValue = pSaveLock->isBlocked() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 감시 값 `Save.Written` · `Save.Loaded` 입니다 — 실행 중인 게임이 저장 · 복원을 마친 슬롯 수. */
            template <bool bWritten>
            [[nodiscard]] static bool readSlotCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const GameSaveSlots* const pSaveSlots = game::findSubsystem<GameSaveSlots>();
                if ( pSaveSlots == nullptr )
                    return false;
                const SaveSlotCounts& counts = pSaveSlots->getCounts();
                outValue                     = static_cast<float64>( bWritten ? counts._written : counts._loaded );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( saveWritten, "Save.Written", "Game state save slots written successfully by the running game", &GameSaveSlotsWatchesInternal::readSlotCount<true> );
    // 막기는 씬 로드(워커)와 세이브 읽기(저장소)의 끝을 따르므로 벽시계 감시 값이다.
    // 활동 수 없음: 저장 잠금은 게임 상태(씬 로드 · 깨진 복원 · 컷신)가 풀어 주는 문이다 — 셀 수 있는 일의 양이 아니다.
    SW_AUTOMATION_WALL_WATCH( saveBlocked, "Save.Blocked", "1 while the running game's save gate refuses saving (scene loading, broken restore, cutscene)", &GameSaveSlotsWatchesInternal::readSaveBlocked,
                              nullptr );
    SW_AUTOMATION_WATCH( saveLoaded, "Save.Loaded", "Game state save slots read and restored successfully by the running game", &GameSaveSlotsWatchesInternal::readSlotCount<false> );
} // namespace sw
