/**
 * @file FrameRendererUtil.h
 * @brief FrameRenderer 번역 단위 공유 상수·헬퍼
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassResource.h"

namespace sw
{
    /**
     * @enum RenderViewMode
     * @brief 씬 지오메트리를 어떻게 보여줄지 — 에디터 뷰포트의 Lit/Unlit/Wireframe.
     * @details 렌더 상태(채우기 모드)와 셰이더 퍼뮤테이션(조명 항)을 함께 가르는 값이라 어느 한쪽에만
     *          둘 수 없다. `FrameRenderer` 가 PSO 변형 키의 한 축으로 들고 있고, 배치 PSO 를 고를 때
     *          머티리얼 퍼뮤테이션과 **같은 자리**에서 적용된다 — 그래서 와이어프레임이 머티리얼 변형을
     *          잃지 않는다(반투명 유리가 와이어프레임에서도 반투명 퍼뮤테이션으로 그려진다).
     * @note 에디터 전용이 아니다. 헤드리스에서도 `-gv_viewMode=<0|1|2>` 로 고를 수 있어 스크린샷
     *       비교로 검증된다 — 뷰 모드가 픽셀을 바꾸는지를 에디터를 띄우지 않고 확인할 수 있다.
     */
    enum class RenderViewMode : uint8
    {
        Lit = 0,   ///< 조명·그림자를 다 계산한 기본 화면
        Unlit,     ///< 알베도만 — 조명 항이 셰이더에서 컴파일 아웃된다
        Wireframe, ///< 삼각형 외곽선만 (RHIFillMode::Wireframe)

        Count
    };

    /**
     * @brief Unlit 뷰 모드가 셰이더에 넘기는 define.
     * @details 여기가 유일한 정본이다 — 이 문자열과 `.hlsl` 의 `#if defined(...)` 가 어긋나면
     *          컴파일은 되고 화면만 안 바뀐다(조용한 실패). 셰이더를 더할 때 이 이름을 보라.
     */
    inline constexpr const utf8* kViewModeUnlitDefine = "SW_VIEWMODE_UNLIT=1";

    /** @brief FrameRenderer TU 공유 패스/어태치먼트 이름과 헬퍼 */
    struct FrameRendererUtil
    {
        struct Attachment
        {
            static constexpr auto kSwapchain        = "Swapchain";
            static constexpr auto kSceneColor       = "SceneColor";
            static constexpr auto kSceneDepth       = "SceneDepth";
            static constexpr auto kShadowMap        = "ShadowMap";
            static constexpr auto kGBufferAlbedo    = "GBufferAlbedo";
            static constexpr auto kGBufferNormal    = "GBufferNormal";
            static constexpr auto kLitColor         = "LitColor";
            static constexpr auto kBloomColor       = "BloomColor";
            static constexpr auto kBloomBright      = "BloomBright";
            static constexpr auto kOutlineColor     = "OutlineColor";
            static constexpr auto kTransparentColor = "TransparentColor";
            static constexpr auto kTaaColor         = "TaaColor";
        };

        struct Entry
        {
            static constexpr auto kVSMain = "VSMain";
            static constexpr auto kPSMain = "PSMain";
            static constexpr auto kCSMain = "CSMain";
        };

        static constexpr uint32  kDefaultTransientSize = 1280;
        static constexpr auto    kDefaultMainPassName  = "DefaultMainPass";
        static constexpr float4  kBlackClear           = { 0.0f, 0.0f, 0.0f, 1.0f };
        static constexpr float4  kSceneClear           = { 0.12f, 0.15f, 0.18f, 1.0f };
        static constexpr float4  kDepthClear           = { 1.0f, 0.0f, 0.0f, 0.0f };
        static constexpr float4  kBloomClear           = { 0.0f, 0.0f, 0.0f, 1.0f };
        static constexpr float4  kNormalClear          = { 0.5f, 0.5f, 1.0f, 1.0f };
        static constexpr float32 kDefaultCameraPos[3]  = { 0.0f, 1.2f, 3.2f };

        /**
         * @brief GPU 인스턴스 회전의 기준 각속도와 편차 폭 (라디안/초).
         * @details 편차가 기준보다 **커야** 인스턴스마다 속도가 확연히 갈린다. 폭이 기준보다 작으면
         *          속도 차이가 눈에 안 띄어 결국 "다 같은 속도"로 보인다 — 그게 이 패스를 만든 이유다.
         *          instanceanim.hlsl 이 시드 해시로 [기준, 기준+폭) 에서 속도를 고르고 방향도 가른다.
         */
        static constexpr float32 kGpuSpinBaseSpeed  = 0.35f;
        static constexpr float32 kGpuSpinSpeedRange = 1.75f;

        static bool isDepthFormat( RHIFormat format ) { return format == RHIFormat::D24_UNORM_S8_UINT; }

        /**
         * @brief 이 패스가 씬 메시(GpuScene 배치)를 그리는가.
         * @details 머티리얼 퍼뮤테이션 PSO 를 미리 만들어 둘 대상이 이 패스들이다. 풀스크린 패스는 배치를 안 쓴다.
         */
        static bool drawsSceneMeshes( RenderPassType passType )
        {
            return passType == RenderPassType::Shadow || passType == RenderPassType::DepthPrepass ||
                   passType == RenderPassType::ForwardOpaque || passType == RenderPassType::ForwardOpaqueNoDepthWrite ||
                   passType == RenderPassType::GBuffer || passType == RenderPassType::GBufferAlbedo ||
                   passType == RenderPassType::GBufferNormal || passType == RenderPassType::Transparent;
        }

        /**
         * @brief 이 패스가 **머티리얼의 셰이더**로 그리는가 (아니면 패스 자신의 셰이더인가).
         * @details 언리얼로 치면 패스가 셰이더 **타입**(TShadowDepthVS 같은)을 정하고 머티리얼이 그 타입의
         *          퍼뮤테이션을 준다. 그림자·뎁스 패스는 지오메트리만 그리므로 자기 셰이더가 정본이고, 머티리얼은
         *          define 만 얹는다(알파 마스크 같은 것이 나중에 여기로 들어온다). 여기서 true 인 패스만
         *          머티리얼이 선언한 .hlsl 로 갈아탄다.
         */
        static bool usesMaterialShader( RenderPassType passType )
        {
            return drawsSceneMeshes( passType ) && passType != RenderPassType::Shadow && passType != RenderPassType::DepthPrepass;
        }

        /**
         * @brief 이 패스에 뷰 모드(Unlit/Wireframe)를 적용하는가.
         * @details 화면 색을 만드는 지오메트리 패스만이다. 그림자·뎁스 프리패스는 **제외한다** —
         *          와이어프레임으로 그림자를 구우면 그림자가 선 몇 개로 남고, 뎁스 프리패스를
         *          와이어프레임으로 채우면 이후 패스의 뎁스 테스트가 삼각형 내부를 전부 버려 화면이 빈다.
         *          둘 다 "보기 방식" 이 아니라 다음 패스의 입력이므로 늘 Solid·Lit 로 둔다.
         * @note 지금은 `usesMaterialShader` 와 같은 집합이지만 근거가 다르므로 따로 둔다 —
         *       한쪽이 바뀔 때 다른 쪽이 조용히 따라가면 안 된다.
         */
        static bool appliesViewMode( RenderPassType passType )
        {
            return drawsSceneMeshes( passType ) && passType != RenderPassType::Shadow && passType != RenderPassType::DepthPrepass;
        }

        /**
         * @brief 이름 목록에서 맵에 실제로 있는 첫 이름을 돌려줍니다 (없으면 nullptr).
         * @details 키 존재만 보고 값은 건드리지 않으므로 어떤 어태치먼트 맵이든 받는다.
         */
        template <typename TAttachmentMap>
        static const utf8* pickFirstExisting( const TAttachmentMap&              mapAttachment,
                                              std::initializer_list<const utf8*> listName )
        {
            for ( const utf8* pName : listName )
            {
                if ( mapAttachment.find( pName ) != mapAttachment.end() )
                    return pName;
            }
            return nullptr;
        }
    };

    /**
     * @struct AttachmentNames
     * @brief 어태치먼트·패스 리소스 이름의 hashed_string 캐시.
     * @details PassConstantNames 와 같은 이유다 — hashed_string 생성은 전역 레지스트리 intern
     *          (FNV 해시 → 32-way 샤드 뮤텍스 → 조회)이다. 이 이름들은 전부 코드 리터럴이라
     *          값이 고정인데, 예전엔 패스마다·드로우마다 새로 intern 했다.
     *          특히 `commitBindlessTextureBindings` 는 DX11/GL 경로에서 **드로우 호출마다**
     *          네 개를 만들고 있었다.
     * @note 문자열이 필요한 자리에는 `view()` 를 쓴다 — 락 없는 O(1) 포인터 역참조다.
     */
    struct AttachmentNames
    {
        hashed_string _swapchain{ FrameRendererUtil::Attachment::kSwapchain };
        hashed_string _sceneColor{ FrameRendererUtil::Attachment::kSceneColor };
        hashed_string _sceneDepth{ FrameRendererUtil::Attachment::kSceneDepth };
        hashed_string _shadowMap{ FrameRendererUtil::Attachment::kShadowMap };
        hashed_string _gbufferAlbedo{ FrameRendererUtil::Attachment::kGBufferAlbedo };
        hashed_string _gbufferNormal{ FrameRendererUtil::Attachment::kGBufferNormal };
        hashed_string _litColor{ FrameRendererUtil::Attachment::kLitColor };
        hashed_string _bloomColor{ FrameRendererUtil::Attachment::kBloomColor };
        hashed_string _outlineColor{ FrameRendererUtil::Attachment::kOutlineColor };
        hashed_string _transparentColor{ FrameRendererUtil::Attachment::kTransparentColor };
        hashed_string _taaColor{ FrameRendererUtil::Attachment::kTaaColor };
        hashed_string _aoColor{ "AOColor" };
        hashed_string _tonemapColor{ "TonemapColor" };

        /// 셰이더가 보는 이름(어태치먼트 이름과 다를 수 있다 — registerPassTexture 의 canonical 인자).
        hashed_string _sourceColor{ "SourceColor" };
        hashed_string _sourceDepth{ "SourceDepth" };
    };

    /**
     * @brief 프로세스 전역 AttachmentNames 를 돌려줍니다.
     * @details 함수 지역 static — 문자열 레지스트리보다 먼저 초기화될 위험이 없다.
     */
    inline const AttachmentNames& attachmentNames()
    {
        static const AttachmentNames s_names{};
        return s_names;
    }

    /**
     * @struct PassConstantNames
     * @brief PassCB/리소스 이름의 hashed_string 캐시.
     * @details hashed_string 생성은 전역 문자열 레지스트리에 intern 하는 작업이다(FNV 해시 →
     *          샤드 공유락 → 조회). 리터럴은 값이 고정이므로 매번 만들 이유가 없는데, 예전엔
     *          `g_World` 를 드로우 호출마다 새로 만들고 있었다. 한 번만 만들어 재사용한다.
     */
    struct PassConstantNames
    {
        hashed_string _lightViewProj{ "g_LightViewProj" };
        hashed_string _viewProj{ "g_ViewProj" };
        hashed_string _world{ "g_World" };
        hashed_string _keyLightDirIntensity{ "g_KeyLightDirIntensity" };
        hashed_string _keyLightColor{ "g_KeyLightColor" };
        hashed_string _shadowParams{ "g_ShadowParams" };
        hashed_string _bloomParams{ "g_BloomParams" };
        hashed_string _outlineColor{ "g_OutlineColor" };
        hashed_string _outlineParams{ "g_OutlineParams" };
        hashed_string _flags{ "g_Flags" };
        hashed_string _instanceBase{ "g_InstanceBase" };
        /// @brief 인스턴스 버퍼 원소 수 — 셰이더 SwLoadInstance 가 범위를 막는다.
        hashed_string _swInstanceCount{ "g_SwInstanceCount" };
        /// @brief 배치의 머티리얼 데이터 버퍼 원소 수 — 셰이더 SW_MATERIAL 이 클램프한다.
        hashed_string _swMaterialCount{ "g_SwMaterialCount" };
        hashed_string _swInstances{ "SwInstances" };
        /// @brief 컬링이 만든 가시 인스턴스 ID 목록 (binding.hlsli g_SwVisibleInstanceIds ↔ "SwVisibleInstanceIds").
        hashed_string _swVisibleInstanceIds{ "SwVisibleInstanceIds" };
        /// @brief 배치의 머티리얼 데이터 구조버퍼 (binding.hlsli g_SwMaterials ↔ "SwMaterials"). 배치마다 등록한다.
        hashed_string _swMaterials{ "SwMaterials" };
    };

    /**
     * @brief 프로세스 전역 PassConstantNames 를 돌려줍니다.
     * @details 함수 지역 static 이라 첫 호출 때 한 번만, 스레드 안전하게 만들어진다 — 전역 정적
     *          객체로 두면 문자열 레지스트리보다 먼저 초기화될 수 있다.
     */
    inline const PassConstantNames& passConstantNames()
    {
        static const PassConstantNames s_names{};
        return s_names;
    }
} // namespace sw
