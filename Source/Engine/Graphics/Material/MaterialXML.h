/**
 * @file MaterialXML.h
 * @brief 머티리얼 XML 의 필드 · 프로퍼티 · 블렌드 모드 · 품질 · 퍼뮤테이션 노드를 읽고 씁니다(`Material` · `MaterialInstance` 가 함께 쓴다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    /**
     * @brief 머티리얼 속성 항목 하나(`<_properties><item name type …>`)의 칸입니다. 칸 · 기본값은 여기 PROPERTY 가 정본이다(`MaterialXML::parsePropertyNode` 가 `MaterialProperty` 로 옮긴다).
     * @details 종류(`type` · `shaderType`)는 패킹의 종류 이름(`MaterialPacking::stringToType`)이라 글로 받는다. 깃발 칸은 파일 철자 그대로(`bHdr` · `bSrgb` …)다.
     *          예전에는 칸마다 속성과 같은 이름의 자식 원소를 모두 받고 `type` 에 옛 이름 `cpuType` 을 받았다 — 콘텐츠가 쓰지 않는 두 번째 문법이라 지웠다.
     */
    REFLECT()
    struct SW_API MaterialPropertyFields
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name{};
        PROPERTY()
        string _type{};
        PROPERTY()
        string _shaderType{};
        PROPERTY()
        string _defaultValue{};
        PROPERTY()
        string _value{};
        PROPERTY()
        string _assetPath{};
        PROPERTY()
        string _enumType{};
        PROPERTY()
        string _displayName{};
        PROPERTY()
        string _group{};
        PROPERTY()
        string _tooltip{};
        PROPERTY()
        string _shaderKeyword{};
        PROPERTY()
        float32 _min{ 0.0f };
        PROPERTY()
        float32 _max{ 1.0f };
        PROPERTY( Name = "bHdr" )
        bool _bHdr{ false };
        PROPERTY( Name = "bSrgb" )
        bool _bSrgb{ true };
        PROPERTY( Name = "bHidden" )
        bool _bHidden{ false };
        PROPERTY( Name = "bAdvanced" )
        bool _bAdvanced{ false };
    };
} // namespace sw

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

        /** @brief 부모 사슬의 최대 길이입니다. 넘으면 순환으로 보고 오류입니다. */
        static constexpr uint32 kMaxParentDepth = 8;
        /**
         * @brief 루트의 `parent="<리소스 경로>"` 를 풀어 부모 칸을 모두 담은 한 벌의 XML 로 펼칩니다(언리얼 Material Instance — 부모 + 바꾼 값만).
         * @details 규칙: 루트 속성(이름 · 셰이더 · 블렌드)은 자식이 적은 것만 덮는다. `_properties` 는 이름으로 맞춰 자식 줄이 적은 속성만 덮고(자식 줄에
         *          하위 원소(`_enumEntries`)가 있으면 그 줄을 통째로 바꾼다), 부모에 없는 이름은 뒤에 더한다. `_permutations` 는 자식이 적으면 통째로 바꾼다.
         *          부모를 읽지 못하거나 사슬이 돌면(`kMaxParentDepth`) 오류를 남기고 false 다 — 부모 없이 자식 칸만으로 읽지 않는다.
         *          `parent` 가 없으면 원문 그대로다. @p outParentPath 는 바로 위 부모 경로(없으면 빈 글)다.
         */
        [[nodiscard]] static bool flattenParent( string_view xmlText, string& outText, string& outParentPath );
        /**
         * @brief 펼친 머티리얼 XML(@p fullText)을 펼친 부모(@p parentFullText, 경로 @p parentPath)와 다른 칸만 남긴 자식 XML 로 줄입니다(에디터 저장이 부모 구조를 지키게).
         * @details 같은 줄은 빼고, 다른 줄은 이름 + 다른 속성만 남긴다(하위 원소가 다르면 줄을 통째로). 루트의 셰이더 · 블렌드는 다를 때만, 퍼뮤테이션은 다를 때만 적는다.
         *          두 글은 같은 직렬화(`Material::saveFullToString`)로 만든 것이어야 한다 — 손으로 쓴 원문과 맞대면 같은 값도 글자가 달라 다른 칸으로 남는다.
         */
        [[nodiscard]] static bool makeParentDelta( string_view fullText, string_view parentFullText, string_view parentPath, string& outText );
    };
} // namespace sw
