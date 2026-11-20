/**
 * @file ZoneProfileAssetKind.h
 * @brief 지역 프로필(`*.zone.xml`) 데이터 종류입니다 — 엔진 칸이 프로필 형식을 모른 채 타입 있는 참조(`AssetRef<ZoneProfileAssetKind>`)로 가리키게 합니다.
 * @details 프로필 형식(GameFramework `ZoneProfile`)은 엔진 위 층에 있다. 엔진의 표면 종류(`SurfaceKindDef::_zoneProfile`)는 경로만 들고 프로필을 읽지 않으므로(층 방향)
 *          종류 정의만 이 층에 두고 `ZoneProfile::kAssetType` 이 이 값을 그대로 쓴다(종류 정의는 한 곳). 읽기는 위 층이 `SharedAssetCache<ZoneProfile>` 로 한다.
 *          언리얼 `TSoftObjectPtr<UObject>` 를 아래 모듈이 위 모듈의 형식 없이 드는 것과 같은 자리다.
 */
#pragma once
#include "Engine/Content/AssetTypeRegistry.h"

namespace sw
{
    /** @brief 지역 프로필 종류입니다. 형식이 아니라 종류 정의만 든다(파일 머리말). */
    struct ZoneProfileAssetKind
    {
        /** @brief `*.zone.xml` 데이터 종류입니다. `ZoneProfile::kAssetType` 이 이 값이다. */
        static constexpr AssetTypeDef kAssetType = AssetTypeDef::makeDataFile( "zone", AssetPathRule{ {}, {}, ".zone.xml" }, {} );
    };
} // namespace sw
