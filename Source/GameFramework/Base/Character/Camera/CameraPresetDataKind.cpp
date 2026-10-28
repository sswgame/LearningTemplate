#include "pch.h"

#include "Engine/Asset/AssetDataKind.h"

#include "GameFramework/Base/Character/Camera/CameraPreset.h"

namespace sw
{
    /** @brief `CameraPresetCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_RESOURCE_DATA_KIND( CameraPresets, "camerapresets", nullptr, nullptr, ".cameras.xml", &AssetDataKindUtil::loadCatalog<CameraPresetCatalog> );
} // namespace sw
