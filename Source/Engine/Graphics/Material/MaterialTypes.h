/**
 * @file MaterialTypes.h
 * @brief Material 과 MaterialInstance 가 함께 쓰는 타입과 디스크립터입니다.
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Math/BitUtil.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RenderState/RenderState.h"
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

    /**
     * @brief 머티리얼의 표면 모델입니다 — 표면 값(알베도 · 노멀 · 거칠기 · 금속도 · 발광)을 빛과 어떻게 합치는지, 그래서 어느 패스가 그 값을 담을 수 있는지를 정한다.
     * @details 언리얼 Material Shading Model · 유니티 셰이더 그래프 Material(Lit · Unlit) · Godot `render_mode unshaded` 자리다. `.material` 의 필수 칸 `surfaceModel` 이고
     *          (`_base` 로 물려받는다), 셰이더 define `MATERIAL_SURFACE_MODEL_<이름>` 을 낸다(`MaterialPermutation::appendSurfaceModelDefines`). 셰이더 뼈대
     *          (material_shader.hlsli)가 그 define 과 셰이더가 고른 조명 훅이 맞는지 보고 어긋나면 쿠킹이 멈춘다 — 데이터와 셰이더가 다른 모델을 말할 수 없다.
     *          G버퍼에 칸이 없는 모델(`Toon` · `Hair` · `CustomLit`)은 `MATERIAL_FORWARD_ONLY` 를 모델이 낸다 — `.material` 에 그 define 을 적으면 읽기 오류다.
     */
    ENUM()
    enum class MaterialSurfaceModel : uint8
    {
        Lit,       ///< 기본 PBR(`SW_MATERIAL_DEFAULT_LIGHTING`), 또는 G버퍼 셰이딩 모델 번호를 스스로 고르는 겹 셰이딩(`SW_MATERIAL_CUSTOM_LIGHTING` + `SW_MATERIAL_CUSTOM_SHADING`)
        Unlit,     ///< 표면 색 그대로(`SW_MATERIAL_UNLIT`) — G버퍼에는 Unlit 번호로 간다
        Emissive,  ///< 기본 PBR + 자체 발광 훅(`SW_MATERIAL_EMISSIVE`) — G버퍼 발광 칸으로 디퍼드에서도 그린다
        Toon,      ///< 셀 셰이딩(toon.hlsl — 계단 · 그림자색 · 림 · 맷캡). 포워드 전용
        Hair,      ///< 머리카락 Marschner(hair.hlsl · 그래프 셰이딩 모델 Hair). 포워드 전용
        CustomLit, ///< 셰이더가 정한 그 밖의 조명 식(물 · 2D 빛). 포워드 전용
    };

    /**
     * @brief G버퍼로 나타낼 수 없는 셰이딩의 머티리얼 define 입니다. G버퍼 패스가 그 배치를 빼고 `ForwardOnlyOpaque` 패스가 포워드로 그립니다.
     * @details 포워드 전용 표면 모델(`MaterialSurfaceModel` — 툰 · 머리카락 · 자체 조명)은 모델이 이 define 을 낸다. G버퍼에 담길 수 있는 모델이어도 칸이 모자라는
     *          갈래(겹친 엽 — appearance_lit.hlsl)는 `.material` 의 `_alwaysDefines` 에 직접 적는다. 유니티 URP 디퍼드의 `UniversalForwardOnly` 와 같다.
     */
    inline constexpr const utf8* kMaterialForwardOnlyDefine = "MATERIAL_FORWARD_ONLY";

    /** @brief `alphaMode="Mask"` 머티리얼의 define 입니다. 깊이 패스(그림자, 깊이 프리패스)도 머티리얼 셰이더로 그려 같은 형태로 오립니다(`RenderPassKindInfo::usesMaterialShaderForDepth`). 머티리얼 XML 에 직접 적지 않습니다(읽기 오류). */
    inline constexpr const utf8* kMaterialAlphaCutoffDefine = "MATERIAL_ALPHA_CUTOFF";
    /// @brief 머티리얼 변형의 텍스처 칸 수 define 의 이름입니다(`MATERIAL_TEXTURE_COUNT=N`, `Material::getTextureSlotCount`). 웹 에뮬 셰이더가 이 칸만 선언한다.
    inline constexpr const utf8* kMaterialTextureCountDefine = "MATERIAL_TEXTURE_COUNT";
    /**
     * @brief 화면 왜곡을 그리는 머티리얼의 define 입니다(`.material` 의 `_alwaysDefines`). 왜곡 누적 패스는 이것이 있는 반투명 배치만 그린다(`RenderPassKindInfo::_pRequiredMaterialDefine`).
     * @details 이 define 을 적는 머티리얼의 셰이더는 왜곡 훅을 정의한다(`SW_MATERIAL_CUSTOM_DISTORTION` — 없으면 material_shader.hlsli 의 `#error`). 본보기는 engine/shaders/distortion.hlsl 이다.
     *          VFX 메시 입자는 방출기의 메시 머티리얼이 이 머티리얼이면 같은 길로 왜곡을 그린다.
     */
    inline constexpr const utf8* kMaterialDistortionDefine = "MATERIAL_DISTORTION";
    /**
     * @brief 메시 외곽선 패스가 그리는 불투명 배치의 머티리얼 define 입니다. 퍼뮤테이션에 이것이 없는 배치는 그 패스에서 빠집니다.
     * @details 머티리얼의 정적 스위치(`Outline`)가 켭니다. 외곽선을 아는 셰이더만 이 스위치를 가지므로 다른 셰이더가 외곽선 define 으로
     *          그려질 일이 없습니다(언리얼이 메시 패스 프로세서에서 머티리얼의 성질로 드로우를 거르는 것에 해당합니다).
     */
    inline constexpr const utf8* kMaterialOutlineDefine = "MATERIAL_OUTLINE";
    /** @brief 정점 셰이더가 정점을 옮기는 머티리얼(바람)의 define 입니다. 그림자 패스는 머티리얼 셰이더로 그리고 깊이 프리패스는 그 배치를 뺀다. */
    inline constexpr const utf8* kMaterialVertexDeformDefine = "MATERIAL_VERTEX_DEFORM";
    /**
     * @brief 높이 맵으로 표면을 노멀 방향으로 옮기는 머티리얼(`material_displacement.hlsli`)의 define 입니다. 그 배치는 헐 · 도메인 스테이지가 붙은 변형 PSO 로 그린다.
     * @details 깊이 패스(그림자 · 깊이 프리패스)도 머티리얼 셰이더로 같은 변위를 그린다(`RenderPassKindInfo::usesMaterialShaderForDepth`) — 패스 셰이더로 그리면 윤곽이 평면으로 남는다.
     *          테셀레이션이 없는 백엔드(`RHICapabilities::_bMaterialTessellation`)에서는 변형을 만들지 않고 오류를 남긴다 — 평면으로 대신 그리지 않는다.
     */
    inline constexpr const utf8* kMaterialDisplacementDefine = "MATERIAL_DISPLACEMENT";
    /// @brief 메시 표면 영역의 define 입니다. 후처리 영역과 함께 적지 않습니다(읽기 오류).
    inline constexpr const utf8* kMaterialDomainSurfaceDefine = "MATERIAL_DOMAIN_SURFACE";
    /**
     * @brief 후처리 머티리얼(PostProcess 영역)의 define 입니다(`.material` 의 `_alwaysDefines`). 메시에 붙이지 않고 블렌더블 목록에 넣는 머티리얼이다.
     * @details 게임 스레드가 블렌더블을 풀 때 이 define 이 없는 머티리얼은 오류로 거절하고(`PostProcessMaterialSet`), 쿠커는 메시 패스 변형을 굽지 않는다(풀스크린 VS · PS 한 벌만).
     *          메시 패스의 기능(왜곡 · 외곽선 · 변형 · 변위 · 굴절 · 반투명)과 함께 적지 않는다(읽기 오류 — 그려지지 않는 기능이다). 언리얼 Material Domain = Post Process 와 같다.
     */
    inline constexpr const utf8* kMaterialDomainPostProcessDefine = "MATERIAL_DOMAIN_POST_PROCESS";
    /// @brief 장면 색 사본을 굴절해 읽는 머티리얼의 define 입니다 — 렌더러가 굴절 배치를 가른다(`GPUScene::hasRefractionBatches`, scene_refraction.hlsli 를 쓰는 셰이더 — 물 · 외형 유리).
    inline constexpr const utf8* kMaterialRefractionDefine = "MATERIAL_REFRACTION";
    /// @brief 반투명 블렌드 define 입니다 — `alphaMode="Blend"` 와 함께 움직인다(하나만 있으면 알파가 새거나 불투명 목록에서 섞인다).
    inline constexpr const utf8* kMaterialTranslucentDefine = "MATERIAL_BLEND_TRANSLUCENT";
    /// @brief 변위 머티리얼이 높이를 읽는 텍스처 프로퍼티의 이름입니다(`MATERIAL_DISPLACEMENT` 의 필수 입력).
    inline constexpr const utf8* kMaterialDisplacementMapProperty = "_displacementMap";
    /** @brief `twoSidedLighting="true"` 머티리얼의 define 입니다. 셰이더가 뒷면 노멀을 뒤집습니다. 머티리얼 XML 에 직접 적지 않습니다(읽기 오류). */
    inline constexpr const utf8* kMaterialTwoSidedDefine = "MATERIAL_TWO_SIDED";

    /// @brief 머티리얼 프로퍼티 타입 종류입니다.
    enum class MaterialPropertyKind : uint8
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
    ENUM()
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
        StaticMesh    = BitUtil::bit( 0 ),
        SkeletalMesh  = BitUtil::bit( 1 ),
        Instanced     = BitUtil::bit( 2 ),
        Particles     = BitUtil::bit( 3 ),
        Decal         = BitUtil::bit( 4 ),
        UI            = BitUtil::bit( 5 ),
        PostProcess   = BitUtil::bit( 6 ),
        LightFunction = BitUtil::bit( 7 ),
        MorphTargets  = BitUtil::bit( 8 ),
        SplineMesh    = BitUtil::bit( 9 ),
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
    struct SW_API MaterialProperty
    {
        string               _name;
        MaterialPropertyKind _type;
        MaterialPropertyKind _shaderType;

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

        float32 _min;
        float32 _max;
        uint32  _offset;
        uint32  _size;
        uint32  _textureIndex; ///< 셰이더가 읽는 텍스처 값 — 네이티브 bindless 는 SRV 번호, 에뮬(DX11 · GL · WebGPU)은 칸 서수. 없으면 `kInvalidDescriptorIndex` 와 같은 값

        uint8                  _bHDR      : 1;
        uint8                  _bSRGB     : 1;
        uint8                  _bHidden   : 1;
        uint8                  _bAdvanced : 1;
        [[maybe_unused]] uint8 _reserved  : 4;

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
        uint8                  _bShaderFeature : 1;
        [[maybe_unused]] uint8 _reserved       : 6;

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
        [[nodiscard]] SW_API bool hasAlwaysDefine( string_view define ) const;
    };
} // namespace sw

namespace sw
{
    /// @brief 머티리얼 에셋의 디스크립터입니다(셰이더 경로, 프로퍼티, 퍼뮤테이션).
    struct MaterialDef
    {
        /** @brief .material 리소스에서 채웁니다(팩별 C++ 기본값 없음). */
        string                   _name;
        string                   _shaderPath;
        MaterialAlphaMode        _alphaMode{ MaterialAlphaMode::Opaque };    ///< 어느 배치 목록으로 가고 알파를 어떻게 쓰는지입니다
        MaterialSurfaceModel     _surfaceModel{ MaterialSurfaceModel::Lit }; ///< 표면 값을 빛과 합치는 법입니다. `.material` 에서는 필수 칸이다(초기값은 코드로 만든 머티리얼의 형식일 뿐)
        float32                  _alphaCutoff{ 0.5f };                       ///< `Mask` 의 문턱값입니다. 셰이더의 `_alphaCutoff` 프로퍼티로 간다
        int32                    _renderQueue{ 0 };                          ///< 그리기 순서입니다. 작은 것부터 그립니다(유니티 renderQueue · 언리얼 Translucency Sort Priority 자리). 0 이 기본입니다
        uint8                    _bTwoSidedLighting{ SW_FALSE };             ///< 뒷면에서 노멀을 뒤집습니다(`MATERIAL_TWO_SIDED`). `cullMode="None"` 과 함께 적습니다
        string                   _blendPreset;                               ///< 블렌드 프리셋 이름입니다. 저장용 — 값은 `_renderStateOverride` 에 풀려 있다
        vector<BlendTargetState> _listBlendTarget;                           ///< 렌더 타깃마다의 블렌드입니다. 저장용 — 값은 `_renderStateOverride` 에 풀려 있다
        RenderStateOverride      _renderStateOverride;                       ///< 패스의 렌더 상태 위에 덮는 칸입니다(표: `docs/agents/Rendering.md`)
        MaterialPermutationDef   _permutations;
        /** @brief 부모 머티리얼(`.material` 루트의 `_base`)입니다. 비어 있지 않으면 위 칸은 부모를 펼친 값이고, 저장은 부모와 다른 칸만 적는다(`MaterialXML::makeParentDelta`). */
        string _parentPath;
    };
} // namespace sw

namespace sw
{
    /// @brief 런타임 머티리얼 데이터입니다(패킹 버퍼와 프로퍼티 — 프로퍼티의 정본은 여기 하나다, 저장도 이것을 쓴다).
    struct MaterialData
    {
        vector<MaterialProperty> _listProperty;
        vector<uint8>            _bytes;
    };

    /**
     * @struct MaterialParameterHandle
     * @brief 이름으로 한 번 찾아 둔 머티리얼 프로퍼티의 자리입니다(`Material::findParameter`). 같은 배치 동안 이름을 다시 찾지 않습니다(유니티 `Shader.PropertyToID`, 언리얼 MID 의 파라미터 색인 자리).
     * @details 배치 세대를 함께 들어 프로퍼티 목록 · 오프셋이 바뀌면(다시 읽기 · 셰이더 레이아웃 맞춤) 저절로 낡는다 — 낡은 자리로는 `Material::findProperty( handle )` 가 아무것도 내지 않는다.
     *          오프셋 · 종류는 들지 않는다(프로퍼티가 정본이다).
     */
    struct MaterialParameterHandle
    {
        uint32 _index{ constant::invalid_index::kUint32 }; ///< 프로퍼티 번호. 그 이름이 없으면 무효다(없음도 한 번만 찾는다)
        uint32 _layoutGeneration{ 0 };                     ///< 찾을 때의 배치 세대(0 은 찾지 않음)
    };
} // namespace sw
