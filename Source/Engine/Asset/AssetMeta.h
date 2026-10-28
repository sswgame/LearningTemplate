/**
 * @file AssetMeta.h
 * @brief 에셋 곁의 `.meta`(에셋 식별자 GUID · 소스 경로 · 임포트 산출물 여부)의 형식입니다. `.meta` 를 읽고 쓰는 곳은 이 구조체 하나입니다.
 * @details `.meta` 는 에셋 곁에 놓이고 에셋과 함께 참조되므로 에셋의 형식(XML)을 따른다 — 예전의 `key=value` 텍스트(`KeyValueFile`)를 옮겼다.
 *          한 요소 `<AssetMeta _guid="…" _sourcePath="…" _bImported="false"/>` 이고 리플렉션으로 엄격하게 읽는다(모르는 속성 · 빠진 필수 칸은 오류).
 *          `_sourcePath` 는 사람이 읽으라고 남긴다 — 레지스트리는 `.meta` 가 놓인 자리를 경로로 쓴다(`AssetDatabase::makeRegistryText`).
 *          임포터(`ModelImportMaterial`)와 `AssetDatabase` 가 같은 `toXML` 로 쓰므로 형식이 둘로 갈라지지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief `.meta` 하나를 읽은 결과입니다. 없는 것과 깨진 것을 가른다 — 깨진 `.meta` 를 새 GUID 로 덮어쓰면 그 에셋을 가리키던 참조가 모두 끊긴다. */
    enum class [[nodiscard]] AssetMetaReadResult : uint8
    {
        Loaded,  ///< 읽었다
        Missing, ///< 파일이 없다
        Invalid, ///< 파일은 있는데 읽지 못했다(이유는 오류로 남겼다)
    };

    /**
     * @struct AssetMeta
     * @brief `.meta` 한 장입니다. 파일 머리말 참고.
     */
    REFLECT()
    struct SW_API AssetMeta
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _guid; ///< 에셋 식별자(소문자 UUID)

        PROPERTY( Required )
        string _sourcePath; ///< 쓸 때의 리소스 id(사람이 읽는 칸)

        PROPERTY()
        bool _bImported{ false }; ///< 임포터가 만든 산출물인가(원본 에셋이면 false)

        /** @brief XML 글로 씁니다. */
        string toXML() const;
        /** @brief @p text 를 엄격하게 읽습니다. 읽지 못하면 @p pSourceLabel 과 이유를 오류로 남기고 false 입니다. */
        [[nodiscard]] bool parseXML( string_view text, const utf8* pSourceLabel );
    };
} // namespace sw
