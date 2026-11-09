/**
 * @file MaterialTypes.h
 * @brief Material 과 MaterialInstance 가 함께 쓰는 타입과 디스크립터입니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHIHandles.h"
#include "Engine/Graphics/RHI/RHIRenderState.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @brief 머티리얼의 알파 다루는 법입니다(glTF alphaMode · 언리얼 Blend Mode 자리). 배치 목록과 셰이더 변형이 여기서 정해진다.
     */
    ENUM()
    enum class MaterialAlphaMode : uint8
    {
        Opaque, ///< 불투명 배치 목록
        Mask,   ///< 불투명 배치 목록. `alphaCutoff` 보다 작은 알파를 버린다(`MATERIAL_ALPHA_CUTOFF` — 깊이 패스도 같은 모양으로 오린다)
        Blend,  ///< 반투명 배치 목록. 반투명 패스의 블렌드를 쓰고, 머티리얼이 프리셋으로 덮을 수 있다
    };

    /** @brief `alphaMode="Mask"` 머티리얼의 define 입니다. 깊이 패스(그림자, 깊이 프리패스)도 머티리얼 셰이더로 그려 같은 형태로 오립니다(`FrameRendererUtil::drawsDepthWithMaterialShader`). 머티리얼 XML 에 직접 적지 않습니다(읽기 오류). */
    inline constexpr const utf8* kMaterialAlphaCutoffDefine = "MATERIAL_ALPHA_CUTOFF";
    /** @brief `twoSidedLighting="true"` 머티리얼의 define 입니다. 셰이더가 뒷면 노멀을 뒤집습니다. 머티리얼 XML 에 직접 적지 않습니다(읽기 오류). */
    inline constexpr const utf8* kMaterialTwoSidedDefine = "MATERIAL_TWO_SIDED";

    /// @brief 머티리얼 프로퍼티 타입 종류입니다.
    enum class MaterialPropertyType : uint8
    {
        Float,
        Float2,
        Float3,
        Float4,
        Float4x4,
        Uint,
        Uint2,
        Uint3,
        Uint4,
        Int,
        Int2,
        Int3,
        Int4,
        Bool,
        Range,
        Color,
        Enum,
        BitFlag,
        ChannelMask,
        Texture2D,
        TextureCube,
        Texture3D,
        Texture2DArray,
        Keyword,
        Unknown
    };

    /// @brief 머티리얼 품질 레벨 종류입니다.
    ENUM( Count = Count )
    enum class MaterialQualityLevel : uint8
    {
        Low    = 0,
        Medium = 1,
        High   = 2,
        Epic   = 3,
        Count
    };

    /// @brief 머티리얼 사용 플래그 종류입니다.
    ENUM( Flags )
    enum class MaterialUsageFlags : uint16
    {
        None          = 0,
        StaticMesh    = SW_BIT( 0 ),
        SkeletalMesh  = SW_BIT( 1 ),
        Instanced     = SW_BIT( 2 ),
        Particles     = SW_BIT( 3 ),
        Decal         = SW_BIT( 4 ),
        UI            = SW_BIT( 5 ),
        PostProcess   = SW_BIT( 6 ),
        LightFunction = SW_BIT( 7 ),
        MorphTargets  = SW_BIT( 8 ),
        SplineMesh    = SW_BIT( 9 ),
    };

    /// @brief 머티리얼 enum 항목(이름과 값)입니다.
    struct MaterialEnumEntry
    {
        string _name;
        uint32 _value{ 0 };
    };
} // namespace sw

namespace sw
{
    /// @brief 머티리얼 프로퍼티입니다(타입, 기본값, 패킹 오프셋).
    struct MaterialProperty
    {
        string               _name;
        MaterialPropertyType _type;
        MaterialPropertyType _shaderType;

        /** @brief 작성 기본값입니다. XML 의 `_defaultValue` 이며, 로드할 때 `_value` 의 기본으로 씁니다. */
        string _defaultValue;
        /** @brief 현재 패킹 값입니다(처음에는 `_defaultValue` 와 같습니다). */
        string _value;

        string                    _assetPath;
        string                    _enumType;
        vector<MaterialEnumEntry> _listEnumEntry;

        string _displayName;
        string _group;
        string _tooltip;
        string _shaderKeyword;
        /** @brief 값을 주는 머티리얼 파라미터 묶음(`MaterialParameterCollection`)입니다. 비면 묶지 않습니다. */
        string _collectionPath;
        /** @brief 묶음 안의 이름입니다. `_collectionPath` 와 함께 적습니다. */
        string _collectionParameter;

        float32            _min;
        float32            _max;
        uint32             _offset;
        uint32             _size;
        RHIDescriptorIndex _textureIndex;

        uint8 _bHDR      : 1;
        uint8 _bSRGB     : 1;
        uint8 _bHidden   : 1;
        uint8 _bAdvanced : 1;
        uint8 _reserved  : 4;

        /** @brief 플래그와 범위를 끈 기본값으로 만듭니다. */
        MaterialProperty() noexcept;
    };
} // namespace sw

namespace sw
{
    /// @brief 정적 스위치입니다(켜면 셰이더 define 이 추가됩니다).
    struct MaterialStaticSwitch
    {
        string _name;
        string _keyword;
        string _keywordOff;

        uint8 _bEnabled : 1;
        /**
         * @brief 1 이면 에셋이 정한 상태만 쿠킹합니다(유니티 shader_feature). 0 이면 런타임에 바꿀 수 있는 스위치이므로 켠 상태와 끈 상태를 모두 쿠킹합니다(multi_compile).
         * @details Shipping 에는 실시간 컴파일이 없다. 1 인 스위치를 코드가 `setStaticSwitch` 로 바꾸면 그 변형은 Dev 에서만 그려진다.
         */
        uint8 _bShaderFeature : 1;
        uint8 _reserved       : 6;

        /** @brief 꺼진 스위치로 만듭니다. */
        MaterialStaticSwitch() noexcept;
    };
} // namespace sw

namespace sw
{
    /// @brief 멀티 컴파일 키워드 목록입니다.
    struct MaterialMultiCompile
    {
        string         _name;
        string         _selected;
        vector<string> _listOption;
    };
} // namespace sw

namespace sw
{
    /// @brief 스위치, 멀티 컴파일, 품질을 합친 퍼뮤테이션입니다.
    struct MaterialPermutationDef
    {
        MaterialQualityLevel         _quality   = MaterialQualityLevel::High;
        uint32                       _shaderLOD = 300;
        MaterialUsageFlags           _usage     = MaterialUsageFlags::StaticMesh;
        vector<string>               _listAlwaysDefine;
        vector<MaterialStaticSwitch> _listStaticSwitch;
        vector<MaterialMultiCompile> _listMultiCompile;

        /** @brief 늘 켜는 define 에 @p define 이 있으면 true 입니다. */
        [[nodiscard]] bool hasAlwaysDefine( string_view define ) const;
    };
} // namespace sw

namespace sw
{
    /// @brief 머티리얼 에셋의 디스크립터입니다(셰이더 경로, 프로퍼티, 퍼뮤테이션).
    struct MaterialDef
    {
        /** @brief .material 리소스에서 채웁니다(팩별 C++ 기본값 없음). */
        string                      _name;
        string                      _shaderPath;
        MaterialAlphaMode           _alphaMode{ MaterialAlphaMode::Opaque }; ///< 어느 배치 목록으로 가고 알파를 어떻게 쓰는지입니다
        float32                     _alphaCutoff{ 0.5f };                    ///< `Mask` 의 문턱값입니다. 셰이더의 `_alphaCutoff` 프로퍼티로 간다
        int32                       _renderQueue{ 0 };                       ///< 그리기 순서입니다. 작은 것부터 그립니다(유니티 renderQueue · 언리얼 Translucency Sort Priority 자리). 0 이 기본입니다
        uint8                       _bTwoSidedLighting{ SW_FALSE };          ///< 뒷면에서 노멀을 뒤집습니다(`MATERIAL_TWO_SIDED`). `cullMode="None"` 과 함께 적습니다
        string                      _blendPreset;                            ///< 블렌드 프리셋 이름입니다. 저장용 — 값은 `_renderStateOverride` 에 풀려 있다
        vector<RHIBlendTargetState> _listBlendTarget;                        ///< 렌더 타깃마다의 블렌드입니다. 저장용 — 값은 `_renderStateOverride` 에 풀려 있다
        RHIRenderStateOverride      _renderStateOverride;                    ///< 패스의 렌더 상태 위에 덮는 칸입니다(표: `docs/agents/RenderPipelineXml.md`)
        vector<MaterialProperty>    _listProperty;
        MaterialPermutationDef      _permutations;
        /** @brief 부모 머티리얼(`.material` 루트의 `_base`)입니다. 비어 있지 않으면 위 칸은 부모를 펼친 값이고, 저장은 부모와 다른 칸만 적는다(`MaterialXML::makeParentDelta`). */
        string _parentPath;
    };
} // namespace sw

namespace sw
{
    /** @brief 직렬화되는 인스턴스 오버라이드입니다(게임 MIC / MaterialPropertyBlock). */
    struct MaterialInstanceDef
    {
        string _name;
        string _parentPath; ///< 선택적 힌트입니다. 런타임에는 여전히 Material* 부모가 필요합니다

        /// @brief 인스턴스가 덮어쓴 프로퍼티 값입니다.
        struct Override
        {
            string _name;
            string _value;
            string _assetPath; ///< 텍스처 오버라이드 경로(선택)
        };

        vector<Override> _listOverride;

        /// @brief 인스턴스가 덮어쓴 정적 스위치입니다.
        struct KeywordOverride
        {
            string _name;
            bool   _bEnabled{ SW_TRUE }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

        vector<KeywordOverride> _listKeyword;

        /// @brief 인스턴스가 고른 멀티 컴파일 옵션입니다.
        struct MultiCompileOverride
        {
            string _name;
            string _selected;
        };

        vector<MultiCompileOverride> _listMultiCompile;

        string _quality; ///< 비어 있으면 부모를 따릅니다
    };
} // namespace sw

namespace sw
{
    /// @brief 런타임 머티리얼 데이터입니다(패킹 버퍼와 디스크립터).
    struct MaterialData
    {
        vector<MaterialProperty> _listProperty;
        vector<uint8>            _bytes;
    };
} // namespace sw
