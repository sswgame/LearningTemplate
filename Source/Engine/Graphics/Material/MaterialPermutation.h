/**
 * @file MaterialPermutation.h
 * @brief 머티리얼 퍼뮤테이션입니다. 셰이더 define 이름, 정적 스위치 · 멀티 컴파일 · 품질 · 용도의 서술(`MaterialPermutationDef`), define 목록과 그 해시, 퍼뮤테이션이 바뀐 세대를 담당합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Math/BitUtil.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct MaterialDef;
    struct MaterialProperty;
    enum class MaterialSurfaceModel : uint8;

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
    /// @brief 장면 색 사본을 굴절해 읽는 머티리얼의 define 입니다. 렌더러가 굴절 배치를 가른다(`GPUScene::hasRefractionBatches`, scene_refraction.hlsli 를 쓰는 셰이더: 물 · 외형 유리).
    inline constexpr const utf8* kMaterialRefractionDefine = "MATERIAL_REFRACTION";
    /// @brief 반투명 블렌드 define 입니다 — `alphaMode="Blend"` 와 함께 움직인다(하나만 있으면 알파가 새거나 불투명 목록에서 섞인다).
    inline constexpr const utf8* kMaterialTranslucentDefine = "MATERIAL_BLEND_TRANSLUCENT";
    /// @brief 변위 머티리얼이 높이를 읽는 텍스처 프로퍼티의 이름입니다(`MATERIAL_DISPLACEMENT` 의 필수 입력).
    inline constexpr const utf8* kMaterialDisplacementMapProperty = "_displacementMap";
    /** @brief `twoSidedLighting="true"` 머티리얼의 define 입니다. 셰이더가 뒷면 노멀을 뒤집습니다. 머티리얼 XML 에 직접 적지 않습니다(읽기 오류). */
    inline constexpr const utf8* kMaterialTwoSidedDefine = "MATERIAL_TWO_SIDED";

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

    /**
     * @struct MaterialPassFilter
     * @brief 패스가 그릴 머티리얼을 define 으로 고르는 조건입니다(유니티 Pass Tag `LightMode` 와 렌더러의 `ShaderTagId` 목록, 언리얼 `FMeshPassProcessor` 의 머티리얼 거르기 자리).
     * @details 필수 define 이 있으면 머티리얼 define 목록에 있어야 하고, 제외 define 이 있으면 없어야 한다. 둘 다 비면 모든 배치를 그린다.
     *          파이프라인 패스 서술이 적고(`RenderGraphPassDesc::_requiredMaterialDefine`), 비운 칸은 패스 종류 표의 값이다. 드로우 루프 · 머티리얼 PSO 변형 · 셰이더 쿠커 ·
     *          머티리얼 질의(`Material::isDrawnByPass`)가 이 판정 하나를 본다 — 셋이 갈리면 쿠킹하지 않은 변형을 런타임이 찾는다.
     */
    struct MaterialPassFilter
    {
        string _requiredDefine; ///< 머티리얼에 있어야 하는 define(비면 묻지 않는다)
        string _excludedDefine; ///< 머티리얼에 없어야 하는 define(비면 묻지 않는다)

        /** @brief 거르지 않는 조건(모든 배치를 그린다)이면 true 입니다. */
        [[nodiscard]] bool isEmpty() const { return _requiredDefine.empty() && _excludedDefine.empty(); }
        /** @brief 이 조건의 패스가 define 목록 @p pListMaterialDefine 의 배치를 그리면 true 입니다. nullptr 은 머티리얼이 없는 배치다(필수 define 이 있으면 빠진다). */
        [[nodiscard]] SW_API bool shouldDraw( const vector<string>* pListMaterialDefine ) const;
        [[nodiscard]] bool        operator==( const MaterialPassFilter& other ) const { return _requiredDefine == other._requiredDefine && _excludedDefine == other._excludedDefine; }
        [[nodiscard]] bool        operator!=( const MaterialPassFilter& other ) const { return ( *this == other ) == false; }
    };

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

    /// @brief 멀티 컴파일 키워드 목록입니다.
    struct MaterialMultiCompile
    {
        string         _name;
        string         _selected;
        vector<string> _listOption;
    };

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

    /** @brief 머티리얼 퍼뮤테이션 define 목록(용도 · 품질 · 키워드)과 그 해시, 퍼뮤테이션이 바뀐 세대를 담당합니다. */
    struct SW_API MaterialPermutation
    {
        /**
         * @brief 퍼뮤테이션(셰이더 define)이 바뀔 때마다 증가하는 세대입니다. 엔진에 묶인 머티리얼 캐시의 변경 기록이다(`MaterialCache::getPermutationLog`) — 엔진이 없으면 0(모름)입니다.
         * @details 정지한 씬을 위한 신호입니다. `GPUScene` 은 "등록과 해제도 없고 움직인 것도 없으면"
         *          수집 자체를 건너뜁니다. 그런데 머티리얼과 인스턴스의 정적 스위치, 키워드, 멀티 컴파일을
         *          런타임에 바꾸면 프리미티브는 하나도 더러워지지 않으므로, 그 건너뛰기에 걸려
         *          바뀐 퍼뮤테이션이 영원히 반영되지 않습니다(움직이는 씬에서는 가려집니다).
         *          세대와 함께 바꾼 주인(머티리얼 · 인스턴스)을 적어, 읽는 쪽은 그 주인을 쓰는 프리미티브만 다시 모은다(키워드 하나에 장면 전체를 다시 모으지 않는다).
         */
        static uint64 getPermutationGeneration();

        /**
         * @brief 세대를 올리고 @p pOwner(define 이 바뀐 머티리얼 · 인스턴스)를 적습니다. define 이 실제로 달라진 설정에서만 부릅니다.
         * @param pOwner 머티리얼이면 그 머티리얼(과 그 인스턴스)을 쓰는 프리미티브, 인스턴스면 그 인스턴스를 쓰는 프리미티브가 다시 모인다. nullptr 이면 모두다.
         */
        static void recordPermutationChange( const void* pOwner );

        /**
         * @brief define 목록 @p listDefine(머티리얼이 낼 수 있는 조합 하나)이 그려질 수 있는 조합이면 true 입니다. 아니면 이유와 함께 오류(`Material.UnsupportedFeature`)를 남기고 false 입니다.
         * @details 기능 define 마다의 조건(받는 알파 모드 · 함께 쓸 수 없는 기능 · 필수 프로퍼티)은 `MaterialPermutation.cpp` 의 표 하나다. 예전에는 조건이 패스 종류 표와 셰이더 `#error` 에
         *          흩어져, 불투명 왜곡 · 반투명 외곽선처럼 어느 패스도 그리지 않는 조합이 조용히 읽히고 쿠킹됐다. 머티리얼 읽기(`Material::loadComposed`)가 낼 수 있는 조합 모두에 부르므로
         *          쿠킹도 그 파일에서 멈춘다(`ShaderCookRequestCollector`).
         */
        [[nodiscard]] static bool validateFeatures( const MaterialDef& desc, const vector<MaterialProperty>& listProperty, const vector<string>& listDefine, string_view sourceLabel );

        static void appendUniqueDefine( vector<string>& outListDefine, string_view define );

        /**
         * @brief 변형 define 을 바탕 목록에 얹습니다 — 같은 이름(`=` 앞)이 있으면 그 자리의 값을 바꾸고, 없으면 뒤에 붙입니다.
         * @details 패스 PSO 는 머티리얼 없는 배치를 위해 `MATERIAL_TEXTURE_COUNT=0` 을 갖고, 머티리얼 변형은 같은 이름을 자기 칸 수로 바꾼다 — 같은 이름이 둘이면
         *          셰이더 컴파일이 재정의로 갈린다. 런타임 PSO 변형(FramePSOManager)과 쿠커(ShaderCookRequestCollector)가 이 함수 하나로 합쳐 같은 목록 · 같은 차례가 된다.
         */
        static void mergeDefine( vector<string>& inoutListDefine, string_view define );

        /** @brief define 목록에 이름 @p name 의 define(`NAME` 또는 `NAME=VALUE` — `mergeDefine` 과 같은 이름 규칙)이 있으면 true 입니다. */
        [[nodiscard]] static bool hasDefineName( const vector<string>& listDefine, string_view name );

        /** @brief `MATERIAL_TEXTURE_COUNT=<count>` define 입니다(`kMaterialTextureCountDefine`). */
        static string makeTextureCountDefine( uint32 count );

        static void appendUsageDefines( MaterialUsageFlags usage, vector<string>& outListDefine );

        static void appendQualityDefines( MaterialQualityLevel quality, vector<string>& outListDefine );
        /**
         * @brief 표면 모델의 define `MATERIAL_SURFACE_MODEL_<이름>` 을, 포워드 전용 모델이면 `MATERIAL_FORWARD_ONLY` 도 더합니다.
         * @details 셰이더 뼈대(material_shader.hlsli)가 모델 define 과 조명 훅을 맞춰 보고, G버퍼 패스는 포워드 전용 배치를 뺀다(`RenderPassKindInfo` G버퍼 줄).
         */
        static void appendSurfaceModelDefines( MaterialSurfaceModel model, vector<string>& outListDefine );
        /** @brief G버퍼에 칸이 없어 포워드로만 그리는 표면 모델이면 true 입니다(`Toon` · `Hair` · `CustomLit`). */
        [[nodiscard]] static bool isForwardOnlySurfaceModel( MaterialSurfaceModel model );

        static uint64 hashDefines( const vector<string>& listDefine );
    };
} // namespace sw
