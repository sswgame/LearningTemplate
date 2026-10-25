#include "pch.h"

#include "Engine/Resource/ResourceDataKind.h"

#include "Games/StarSkirmish/SkirmishUnitComponent.h"

namespace sw
{
    /** @brief StarSkirmish 유닛 모델 표(`SkirmishUnitModelCatalog`)의 데이터 파일 종류입니다(스키마 테스트가 이 줄로 파일을 알아본다). */
    SW_RESOURCE_DATA_KIND( SkirmishUnitModels, "skirmishunitmodels", "game/", nullptr, "/data/unitmodels.xml", &ResourceDataKindUtil::loadCatalog<SkirmishUnitModelCatalog> );
} // namespace sw
