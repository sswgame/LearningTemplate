/**
 * @file DerivedDataCacheTestAccess.h
 * @brief 테스트가 임시 폴더 층으로 DDC 를 만드는 창구입니다(제품 코드는 `DerivedDataCache::open` 만 쓴다 — 생성자가 비공개다).
 */
#pragma once
#include "Engine/Asset/Cook/CookRecord.h"

namespace sw
{
    /** @struct DerivedDataCacheTestAccess @brief 파일 머리말 참고. */
    struct DerivedDataCacheTestAccess
    {
        static DerivedDataCache make( vector<DerivedDataLayer> listLayer ) { return DerivedDataCache{ std::move( listLayer ) }; }
    };
} // namespace sw
