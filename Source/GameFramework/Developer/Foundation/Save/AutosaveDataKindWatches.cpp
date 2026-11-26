/**
 * @file AutosaveDataKindWatches.cpp
 * @brief `AutosaveDataKind` 의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Container/string.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Content/AssetTypeRegistry.h"

#include "GameFramework/Runtime/Foundation/Save/Autosave.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct AutosaveWatchesDevInternal
        {
            /** @brief 이번 실행에서 끝난 자동 저장 수입니다. 자동 저장이 없는 게임이면 값이 없습니다. */
            [[nodiscard]] static bool readSaveCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const AutosaveController* pAutosave = game::findSubsystem<AutosaveController>();
                if ( pAutosave == nullptr )
                    return false;
                outValue = static_cast<float64>( pAutosave->getSaveCount() );
                return true;
            }

            /** @brief 마지막으로 닿은 체크포인트 이름입니다. */
            [[nodiscard]] static bool readLastCheckpoint( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const AutosaveController* pAutosave = game::findSubsystem<AutosaveController>();
                if ( pAutosave == nullptr )
                {
                    outValue.setText( "this game has no autosave (gamesettings autosaveSettings)" );
                    return false;
                }
                outValue.setText( string_view{ pAutosave->getLastCheckpointID().c_str() } );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( autosaveSaveCount, "Autosave.SaveCount", "Autosaves this run finished", &AutosaveWatchesDevInternal::readSaveCount );
    SW_AUTOMATION_QUERY_WATCH( autosaveLastCheckpoint, "Autosave.LastCheckpoint", "Name of the last checkpoint reached", &AutosaveWatchesDevInternal::readLastCheckpoint, false, false );
} // namespace sw
