/**
 * @file AutosaveDataKind.cpp
 * @brief 자동 저장 정책 파일(`<팩>/data/autosave.xml`)의 데이터 종류와 자동화 감시 값 `Autosave.SaveCount` · `Autosave.LastCheckpoint` 입니다.
 */
#include "pch.h"

#include "Core/Container/string.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Content/AssetTypeRegistry.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Autosave.h"

namespace sw
{
    SW_ASSET_TYPE( AutosaveSettings, AssetTypeDef::makeDataFile( "autosave", AssetPathRule{ "game/", {}, "/data/autosave.xml" }, AssetTypeLoader::of<AutosaveSettings>() ) );

    namespace
    {
        struct AutosaveWatchesInternal
        {
            /** @brief 이번 실행에서 끝난 자동 저장 수입니다. 자동 저장이 없는 게임이면 값이 없습니다. */
            [[nodiscard]] static bool readSaveCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const AutosaveManager* pAutosave = game::findSubsystem<AutosaveManager>();
                if ( pAutosave == nullptr )
                    return false;
                outValue = static_cast<float64>( pAutosave->getSaveCount() );
                return true;
            }

            /** @brief 마지막으로 닿은 체크포인트 이름입니다. */
            [[nodiscard]] static bool readLastCheckpoint( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const AutosaveManager* pAutosave = game::findSubsystem<AutosaveManager>();
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

    SW_AUTOMATION_WATCH( autosaveSaveCount, "Autosave.SaveCount", "Autosaves this run finished", &AutosaveWatchesInternal::readSaveCount );
    SW_AUTOMATION_QUERY_WATCH( autosaveLastCheckpoint, "Autosave.LastCheckpoint", "Name of the last checkpoint reached", &AutosaveWatchesInternal::readLastCheckpoint, false, false );
} // namespace sw
