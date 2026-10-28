#include "pch.h"

#include "Engine/Asset/AssetDataKind.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CityCatalog.h"

namespace sw
{
    /** @brief `CityCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_RESOURCE_DATA_KIND( City, CityCatalog::kDataKind, "game/", nullptr, "/data/city.xml", &AssetDataKindUtil::loadCatalog<CityCatalog> );
} // namespace sw
