/**
 * @file FractureAssetCache.h
 * @brief `.fracture` 를 경로별로 나눠 주는 캐시입니다("Fracture"). 표는 약한 참조를 쓰므로 마지막 사용자가 놓으면 사라집니다.
 * @details 구현은 `SharedAssetCache` 하나다. 다만 핫 리로드는 제자리에서 바꾸지 않고 표에서 **떼어 낸다**(`SharedAssetReloadMode::Detach`).
 *          쓰던 쪽의 조각 바디와 메시가 이전 형상을 가리킨 채 내용만 바뀌는 일이 없게 하려는 것이다.
 *          쓰는 쪽은 `getReloadCount` 가 바뀌면 다시 받아 다시 만든다.
 */
#pragma once
#include "Core/Common/Macros.h"

#include "Engine/Resource/Cache/SharedAssetCache.h"

namespace sw
{
    struct FractureAsset;

    /** @brief 파일 머리말을 참고하세요. 쓰는 쪽은 `SharedAssetCache<FractureAsset>` 으로 부릅니다. */
    extern template class SW_API_EXTERN_TEMPLATE SharedAssetCache<FractureAsset>;
} // namespace sw
