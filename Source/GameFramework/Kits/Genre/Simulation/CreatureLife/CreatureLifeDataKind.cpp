#include "pch.h"

#include "Engine/Resource/ResourceDataKind.h"

#include "GameFramework/Kits/Genre/Simulation/CreatureLife/CreatureLifeCatalog.h"

namespace sw
{
    /** @brief `CreatureLifeCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_RESOURCE_DATA_KIND( Creatures, "creatures", "game/", nullptr, "/data/creatures.xml", &ResourceDataKindUtil::loadCatalog<CreatureLifeCatalog> );
} // namespace sw
