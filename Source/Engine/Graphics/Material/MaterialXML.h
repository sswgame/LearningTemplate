/**
 * @file MaterialXML.h
 * @brief 머티리얼 XML 의 필드 · 프로퍼티 · 블렌드 모드 · 품질 · 퍼뮤테이션 노드를 읽고 씁니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Shader/ShaderSourceAssetKind.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    class Texture2D;
    struct MaterialParameterCollectionDef;

    /**
     * @brief 머티리얼 속성 항목 하나(`<_properties><item name type …>`)의 칸입니다. 칸 · 기본값은 여기 PROPERTY 가 정본이다(`MaterialXML::parsePropertyNode` 가 `MaterialProperty` 로 옮긴다).
     * @details 종류(`type` · `shaderType`)는 패킹의 종류 이름(`MaterialPacking::stringToType`)이라 글로 받는다. 깃발 칸은 파일 철자 그대로(`bHdr` · `bSrgb` …)다.
     *          예전에는 칸마다 속성과 같은 이름의 자식 원소를 모두 받고 `type` 에 옛 이름 `cpuType` 을 받았다 — 콘텐츠가 쓰지 않는 두 번째 문법이라 지웠다.
     */
    REFLECT()
    struct SW_API MaterialPropertyParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name{};
        PROPERTY()
        string _type{};
        PROPERTY()
        string _defaultValue{};
        PROPERTY()
        string _value{};
        PROPERTY()
        AssetRef<Texture2D> _assetPath;
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
        PROPERTY( Tooltip = "Material parameter collection (.matparams.xml) that gives this property its value" )
        AssetRef<MaterialParameterCollectionDef> _collection;
        PROPERTY( Tooltip = "Name inside the collection; required with _collection" )
        string _collectionParameter{};
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

        MaterialPropertyParams(); // 텍스처 · 묶음이 완전한 자리(.cpp)에서 짓는다
    };

    /** @brief 열거 프로퍼티의 항목 하나(`<_enumEntries><item name value/>`)입니다. */
    REFLECT()
    struct SW_API MaterialEnumEntryParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name{};
        PROPERTY()
        uint32 _value{ 0 };
    };

    /**
     * @brief 머티리얼 루트(`<MaterialDef …>`)의 칸입니다. `formatVersion` 은 에셋 형식이, `_base` 는 부모 펼치기(`DataInheritance`)가 읽는다.
     * @details 쓸 때는 기본값도 모두 쓴다(`XMLFields::writeEveryField`) — 부모와 다른 칸만 남기는 차이(`DataInheritance::makeDelta`)가 적지 않은 칸을 부모 값으로 읽는다.
     *          렌더 상태 칸(`FixedRenderState`)은 같은 원소에 적지만 이 구조체에 들지 않는다 — "적지 않음" 이 "패스의 값" 이라 적은 칸만 쓴다(`MaterialXML::parseRenderProperties`).
     */
    REFLECT()
    struct SW_API MaterialRootParams
    {
        REFLECT_BODY();

        PROPERTY()
        string _name{};
        PROPERTY()
        AssetRef<ShaderSourceAsset> _shaderPath;
        PROPERTY()
        MaterialAlphaMode _alphaMode{ MaterialAlphaMode::Opaque };
        PROPERTY( Required, Tooltip = "How the surface values meet light (MaterialSurfaceModel) - the shader's lighting hook must match it" )
        MaterialSurfaceModel _surfaceModel{ MaterialSurfaceModel::Lit };
        PROPERTY( Min = 0, Max = 1 )
        float32 _alphaCutoff{ MaterialDef{}._alphaCutoff };
        PROPERTY()
        int32 _renderQueue{ 0 };
        PROPERTY()
        bool _bTwoSidedLighting{ false }; ///< NOLINT(sw-bool-flag): 집합체다
        PROPERTY()
        string _blendPreset{};
    };

    /** @brief 순열(`<_permutations quality shaderLOD usage>`)의 칸입니다. 목록(`_alwaysDefines` · `_staticSwitches` · `_multiCompiles`)은 자식 원소라 손으로 푼다. */
    REFLECT()
    struct SW_API MaterialPermutationParams
    {
        REFLECT_BODY();

        PROPERTY()
        MaterialQualityLevel _quality{ MaterialPermutationDef{}._quality };
        PROPERTY()
        uint32 _shaderLOD{ MaterialPermutationDef{}._shaderLOD };
        PROPERTY()
        MaterialUsageFlags _usage{ MaterialPermutationDef{}._usage };
    };

    /** @brief 정적 스위치(`<_staticSwitches><item …/>`) 하나의 칸입니다. 이름은 필수다 — 자식 머티리얼이 이름으로 합친다(`makeInheritanceSchema`). */
    REFLECT()
    struct SW_API MaterialStaticSwitchParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name{};
        PROPERTY()
        string _keyword{};
        PROPERTY()
        string _keywordOff{};
        PROPERTY( Name = "bEnabled" )
        bool _bEnabled{ SW_FALSE };
        PROPERTY( Name = "bShaderFeature" )
        bool _bShaderFeature{ true };
    };

    /** @brief 멀티 컴파일(`<_multiCompiles><item name selected>`) 하나의 칸입니다. 선택지(`_options`)는 자식 목록이다. */
    REFLECT()
    struct SW_API MaterialMultiCompileParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name{};
        PROPERTY()
        string _selected{};
    };

} // namespace sw

namespace sw
{
    /** @brief 머티리얼 XML 의 필드 · 프로퍼티 · 블렌드 모드 · 품질 · 퍼뮤테이션 노드를 읽고 씁니다. */
    struct SW_API MaterialXML
    {
        /** @brief 프로퍼티 한 줄을 읽습니다. 읽지 못한 필드가 있으면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool parsePropertyNode( XMLNode item, MaterialProperty& outProp );

        static void appendAttribute( XMLNode parent, const utf8* pName, string_view value );
        /** @brief 에셋 경로 @p path 를 참조 글(GUID, 정체성이 없으면 경로)로 적습니다. 빈 경로는 적지 않습니다. */
        static void appendReference( XMLNode parent, const utf8* pName, string_view path );

        static void appendBoolAttribute( XMLNode parent, const utf8* pName, bool value );

        /** @brief 루트 속성 @p pName 이 렌더 상태 칸(`FixedRenderState`)이면 true 입니다. 칸 표는 리플렉션 하나다. */
        [[nodiscard]] static bool isRenderStateAttribute( const utf8* pName );
        /**
         * @brief 루트의 렌더 칸(@p rootParams 와 렌더 상태 · `_listBlendTarget`)을 읽어 @p inoutDesc 에 담습니다. 받아 줄 수 없는 조합은 오류를 남기고 false 입니다.
         * @details 조합 규칙: 깊이 테스트는 덮을 수 없다(첨부와 함께 움직인다 — `depthCompare="Always"` 를 쓴다). 켜진 블렌드는 `alphaMode="Blend"` 만 받는다. 프리셋과 타깃 목록은 함께 적지 않는다.
         *          `twoSidedLighting` 은 `cullMode="None"` 과 함께다. `alphaCutoff` 는 `Mask` 일 때만 기본값이 아닐 수 있다. `MATERIAL_ALPHA_CUTOFF` · `MATERIAL_TWO_SIDED` 를 define 으로 직접 적지 않는다.
         *          포워드 전용 표면 모델(`MaterialPermutation::isForwardOnlySurfaceModel`)은 `MATERIAL_FORWARD_ONLY` 를 모델이 내므로 직접 적지 않는다.
         *          @p inoutDesc 의 퍼뮤테이션을 읽은 뒤에 부른다.
         */
        [[nodiscard]] static bool parseRenderProperties( XMLNode root, const MaterialRootParams& rootParams, string_view sourceName, MaterialDef& inoutDesc );
        /** @brief 렌더 상태 칸 중 적은 것과 `_listBlendTarget` 을 씁니다(`parseRenderProperties` 의 짝). */
        static void appendRenderProperties( XMLNode root, const MaterialDef& desc );

        /** @brief 순열 설정을 읽습니다(칸은 `MaterialPermutationParams` · `MaterialStaticSwitchParams` · `MaterialMultiCompileParams`). 읽지 못한 칸이 있으면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool parsePermutationNode( XMLNode root, MaterialPermutationDef& outDesc );

        static void appendPermutationNode( XMLNode root, const MaterialPermutationDef& permutationDesc );

        /**
         * @brief 머티리얼의 합치기 규칙입니다(`DataInheritance`). 머티리얼은 손으로 읽는 XML 이라 값으로 적는다 — 언리얼 Material Instance 의 "부모 + 바꾼 값만" 과 같은 일이다.
         * @details 루트 속성(이름 · 셰이더 · 블렌드)은 자식이 적은 것만 덮고, `_properties` 는 `name` 으로 합치고(`_enumEntries` 는 자식이 적으면 통째로),
         *          `_permutations` 는 속성을 덮고 목록을 이름으로 합친다 — `_alwaysDefines` 는 define 글, `_staticSwitches` · `_multiCompiles` 는 `name` 이 열쇠다(`_options` 는 자식이 적으면 통째로).
         *          그래서 자식은 더할 define 한 줄만 적고, 부모에 define 이 늘면 자식도 따라 받는다(빼려면 `_op="Remove"`).
         *          값 없는 깃발(`bHidden`)을 빈 글로 읽지 못하므로 부모의 값을 자식이 지울 수는 없다(`_bEmptyClears` 를 끈다).
         */
        [[nodiscard]] static DataInheritanceSchema makeInheritanceSchema();
    };
} // namespace sw
