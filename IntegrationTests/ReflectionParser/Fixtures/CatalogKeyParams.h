// EXPECT-TYPE: sw::fixture::FixtureCatalogKeyUser
// EXPECT-GEN: "_single", "CatalogKey"
// EXPECT-GEN: "_listMany", "vector<CatalogKey>"
// EXPECT-GEN: PropertyMetadataTextKind::CatalogKeyTag, "FixtureEntryDef"
// EXPECT-NOT-GEN: CatalogKey<FixtureEntryDef>"
#pragma once
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

namespace sw::fixture
{
    /** @brief 항목 종류입니다. 형식만 있으면 된다 — 어느 카탈로그인지는 참조 표가 적는다. */
    struct FixtureEntryDef
    {
    };

    /** @brief 값 하나와 한 겹 시퀀스가 내장 형식 "CatalogKey" 로 모이고 항목 종류 이름이 메타에 실린다. */
    REFLECT()
    struct FixtureCatalogKeyUser
    {
        REFLECT_BODY();

        PROPERTY()
        CatalogKey<FixtureEntryDef> _single;
        PROPERTY()
        vector<CatalogKey<FixtureEntryDef>> _listMany;
    };
} // namespace sw::fixture
