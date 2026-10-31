/**
 * @file MaterialXML.h
 * @brief 머티리얼 XML 의 필드 · 프로퍼티 · 블렌드 모드 · 품질 · 퍼뮤테이션 노드를 읽고 씁니다(`Material` · `MaterialInstance` 가 함께 쓴다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    /** @brief 머티리얼 XML 의 필드 · 프로퍼티 · 블렌드 모드 · 품질 · 퍼뮤테이션 노드를 읽고 씁니다(`Material` · `MaterialInstance` 가 함께 쓴다). */
    struct SW_API MaterialXML
    {
        static string fieldText( XMLNode node, const utf8* pName );

        /**
         * @brief 선택 필드를 읽습니다. 필드가 없으면 @p absentValue(형식의 "적지 않음" 값)이고, 있는데 읽지 못하면 오류를 남기고 false 입니다.
         * @details 쓰기(`appendBoolAttribute`)가 기본값과 같은 값을 적지 않는 필드만 이 함수로 읽습니다.
         */
        [[nodiscard]] static bool parseOptionalBoolField( XMLNode node, const utf8* pName, bool absentValue, bool& outValue );

        /** @brief 프로퍼티 한 줄을 읽습니다. 읽지 못한 필드가 있으면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool parsePropertyNode( XMLNode item, MaterialProperty& outProp );

        static void appendAttribute( XMLNode parent, const utf8* pName, string_view value );

        static void appendBoolAttribute( XMLNode parent, const utf8* pName, bool value );

        /** @brief 블렌드 모드 이름(`Opaque` · `Transparent`, 철자 그대로 — 대소문자가 다르면 모르는 이름)을 읽습니다. 모르는 이름이면 오류를 남기고 false 이며 @p outMode 는 그대로입니다. */
        [[nodiscard]] static bool parseBlendMode( string_view modeName, RHIBlendMode& outMode );

        static const utf8* blendModeToString( RHIBlendMode mode );

        /** @brief 품질 이름을 읽습니다. 모르는 이름이면 오류를 남기고 false 이며 @p outQuality 는 그대로입니다. */
        [[nodiscard]] static bool parseQuality( string_view qualityName, MaterialQualityLevel& outQuality );

        static const utf8* qualityToString( MaterialQualityLevel quality );

        /** @brief 순열 설정을 읽습니다. 읽지 못한 불리언 필드가 있으면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool parsePermutationNode( XMLNode root, MaterialPermutationDesc& outDesc );

        static void appendPermutationNode( XMLNode root, const MaterialPermutationDesc& permutationDesc );
    };
} // namespace sw
