// EXPECT-ERROR: writes AssetPath or AssetType by hand
#pragma once
#include "Core/Container/string.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    /** @brief 글 칸에 경로 표시를 달아 에셋을 가리키면 종류 검사도 GUID 저장도 없다 — 생성 오류다(`AssetRef<T>` · `DataPath` 로 적는다). */
    REFLECT()
    struct AssetPathOnTextType
    {
        REFLECT_BODY();
        PROPERTY( AssetPath )
        string _path;
    };
} // namespace sw::fixture
