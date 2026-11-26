/**
 * @file AutosaveDataKind.cpp
 * @brief 자동 저장 정책 파일(`<팩>/data/autosave.xml`)의 데이터 종류와 자동화 감시 값 `Autosave.SaveCount` · `Autosave.LastCheckpoint` 입니다.
 */
#include "pch.h"

#include "Core/Container/string.h"

#include "Engine/Content/AssetTypeRegistry.h"

#include "GameFramework/Runtime/Foundation/Save/Autosave.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    SW_ASSET_TYPE( AutosaveSettings, AssetTypeDef::makeDataFile( "autosave", AssetPathRule{ "game/", {}, "/data/autosave.xml" }, AssetTypeLoader::of<AutosaveSettings>() ) );
} // namespace sw
