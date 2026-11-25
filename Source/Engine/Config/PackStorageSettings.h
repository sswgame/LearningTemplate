/**
 * @file PackStorageSettings.h
 * @brief 팩 항목의 저장 방식 설정(`engine/settings/packstorage.xml`, `EngineDefaultAssets::_packStorage`)입니다 - 압축하지 않고 담는 확장자.
 * @details 쿠커(`PackCooker`)와 모드 팩 빌더(`ModPackBuilder`)가 같은 파일을 읽는다. 모드 팩은 배포본(`Config/` 가 없다)에서도 묶을 수 있어야 해서 팩 설정(`PackConfig.json`)이 아니라 엔진 데이터다 -
 *          예전에는 같은 목록을 팩 설정과 게임마다의 모드 설정이 따로 들어 한쪽만 고칠 수 있었다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @struct PackStorageSettings @brief 파일 머리말 참고. 저장 방식은 `PackEntryStorageRule::select` 가 이 목록으로 고른다. */
    REFLECT()
    struct SW_API PackStorageSettings
    {
        REFLECT_BODY();

        PROPERTY( Required )
        vector<string> _listStoredExtension; ///< 압축하지 않고 담는 확장자(`.dds` - 이미 압축된 형식, 대소문자 무시)

        /** @brief 리소스 @p assetID 를 읽습니다. 못 읽었거나 목록이 비었거나 `.` 로 시작하지 않는 확장자가 있으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
    };
} // namespace sw
