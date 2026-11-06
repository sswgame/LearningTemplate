#include "pch.h"

#include "Engine/Config/NetEmulationPresetTable.h"
#include "Engine/Foundation/AssetTypeRegistry.h"

namespace sw
{
    /**
     * @brief 회선 흉내 프리셋 표(`NetEmulationPresetTable`)의 데이터 파일 종류입니다(스키마 테스트가 이 줄로 표를 읽어 본다).
     * @details 표의 형식은 Engine(Config 티어)이 갖고, 등록은 표를 쓰는 게임 세션 쪽에 둔다 — Config 티어는 Resource 티어를 include 하지 않는다.
     */
    SW_ASSET_TYPE( NetEmulationPresets, AssetTypeDef::makeDataFile( "netemulationpresets", AssetPathRule{ "engine/", {}, ".presets.xml" }, AssetTypeLoader::of<NetEmulationPresetTable>() ) );
} // namespace sw
