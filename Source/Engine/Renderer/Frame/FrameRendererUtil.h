/**
 * @file FrameRendererUtil.h
 * @brief FrameRenderer 의 번역 단위들이 함께 쓰는 상수와 헬퍼를 정의합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/Half.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/NormalizedInt.h"
#include "Core/Name/hashed_string.h"
#include "Core/String/StringUtil.h"

#include "Engine/Graphics/RHI/RHIFormatInfo.h"
#include "Engine/Graphics/RenderState/RenderState.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/View/RenderViewSettings.h"
#include "Engine/Renderer/Pipeline/RenderGraphPassDesc.h"
#include "Engine/Renderer/Pipeline/RenderPassKindInfo.h"

namespace sw
{
    /** @brief FrameRenderer 의 번역 단위들이 함께 쓰는 패스 이름, 어태치먼트 이름, 헬퍼입니다. */
    struct FrameRendererUtil
    {
        struct Attachment
        {
            static constexpr auto kSwapchain         = "Swapchain";
            static constexpr auto kSceneColor        = "SceneColor";
            static constexpr auto kSceneDepth        = "SceneDepth";
            static constexpr auto kShadowMap         = "ShadowMap";
            static constexpr auto kLocalShadowMap    = "LocalShadowMap";
            static constexpr auto kGBufferAlbedo     = "GBufferAlbedo";
            static constexpr auto kGBufferNormal     = "GBufferNormal";
            static constexpr auto kGBufferMaterial   = "GBufferMaterial";
            static constexpr auto kGBufferCustomData = "GBufferCustomData";
            static constexpr auto kLitColor          = "LitColor";
            static constexpr auto kBloomColor        = "BloomColor";
            static constexpr auto kBloomBright       = "BloomBright";
            static constexpr auto kOutlineColor      = "OutlineColor";
            static constexpr auto kTransparentColor  = "TransparentColor";
            static constexpr auto kTAAColor          = "TAAColor";
        };

        static constexpr uint32 kDefaultTransientSize = 1280;

        // 첨부의 클리어 색은 파이프라인 XML 의 선언이 기준이다 — 이름으로 짐작해 첨부를 만들 때 쓰던 색(SceneColor · 깊이 · 블룸)은 지웠다.
        static constexpr float4 kBlackClear = { 0.0f, 0.0f, 0.0f, 1.0f };

        /**
         * @brief 뷰의 내부 해상도 한 축(픽셀)을 냅니다. 정수 업스케일(`RenderViewSettings::_upscaleZoom`)이면 출력 사각형 픽셀 / 배율의 내림이고, 아니면
         *        출력 사각형 픽셀 × 해상도 배율의 반올림입니다. 1 보다 작아지지 않습니다.
         * @details 내림이어야 배율을 곱한 크기가 사각형 안에 들어가 Present 가 정수 배로 키울 수 있다(유니티 Pixel Perfect Camera 의 Upscale Render Texture).
         */
        static uint32 computeViewPoolExtent( float32 rectPixels, float32 resolutionScale, uint32 upscaleZoom )
        {
            if ( upscaleZoom > 0 )
                return MathUtil::max( 1u, static_cast<uint32>( rectPixels ) / upscaleZoom );
            return MathUtil::max( 1u, static_cast<uint32>( rectPixels * resolutionScale + 0.5f ) );
        }
        /** @brief 뷰 사각형 상수 둘입니다(PassCB `g_ViewRectScale` · `g_ViewRectSize`, binding.hlsli 1 절). */
        struct ViewRectConstants
        {
            float4 _scale; ///< xy 이번 사각형 / 풀 배율, zw 지난 프레임의 그 배율(지난 프레임 첨부 — TAA 히스토리)
            float4 _size;  ///< xy 사각형 픽셀, zw 1 / 풀 픽셀(셰이더가 가장자리 반 텍셀 안쪽 상한을 구한다)
        };
        /**
         * @brief 첨부 풀(@p poolWidth × @p poolHeight) 안에서 뷰가 그리는 사각형(원점 0,0)의 셰이더 상수를 냅니다. 사각형이 풀을 다 덮으면 배율이 정확히 1 이라 그림이 같습니다.
         * @details 언리얼 `FViewInfo::ViewRect` 와 `BufferUVMax`, 유니티 HDRP `_RTHandleScale` 자리다. 자동 렌더 배율은 풀을 다시 만들지 않고 이 사각형만 줄인다(RS-4).
         *          사각형은 풀보다 클 수 없다(풀은 최대 크기로 잡는다) — 넘으면 풀 크기로 자른다. 풀이 비었으면 배율 1, 크기 0 이다.
         */
        static ViewRectConstants makeViewRectConstants( uint32 rectWidth, uint32 rectHeight, uint32 poolWidth, uint32 poolHeight, const float2& previousScale )
        {
            ViewRectConstants constants{};
            const uint32      width  = MathUtil::min( rectWidth, poolWidth );
            const uint32      height = MathUtil::min( rectHeight, poolHeight );
            const float32     scaleX = poolWidth > 0 ? static_cast<float32>( width ) / static_cast<float32>( poolWidth ) : 1.0f;
            const float32     scaleY = poolHeight > 0 ? static_cast<float32>( height ) / static_cast<float32>( poolHeight ) : 1.0f;
            constants._scale         = float4{ scaleX, scaleY, previousScale._x, previousScale._y };
            constants._size          = float4{ static_cast<float32>( width ), static_cast<float32>( height ), poolWidth > 0 ? 1.0f / static_cast<float32>( poolWidth ) : 0.0f,
                                      poolHeight > 0 ? 1.0f / static_cast<float32>( poolHeight ) : 0.0f };
            return constants;
        }
        /**
         * @brief 정수 업스케일 뷰를 출력 사각형 가운데에 놓는 한 축의 시작 픽셀입니다. 남는 픽셀은 양쪽에 반씩(왼쪽 · 위가 내림) 띠로 남습니다.
         */
        static uint32 computeUpscaleOffset( uint32 rectStartPixels, uint32 rectPixels, uint32 poolPixels, uint32 upscaleZoom )
        {
            const uint32 scaledPixels = poolPixels * upscaleZoom;
            return rectStartPixels + ( scaledPixels < rectPixels ? ( rectPixels - scaledPixels ) / 2u : 0u );
        }

        /** @brief 렌더 파이프라인을 고르는 데 쓰는 후보들입니다. 빈 경로는 "정하지 않음" 입니다. */
        struct PipelineChoice
        {
            string_view _argumentPath;       ///< `initialize` 인자(테스트 · 도구)
            string_view _commandLinePath;    ///< `-gv_renderPipeline`
            string_view _deferredPath;       ///< 엔진 기본 디퍼드(`-gv_deferredShading` 일 때만)
            string_view _gamePath;           ///< 팩 설정(`PackEngineSettings::_renderPipeline`)
            string_view _forwardPath;        ///< 엔진 기본 포워드
            bool        _bDeferred{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };
        /**
         * @brief 그릴 파이프라인을 고릅니다. 인자 → `-gv_renderPipeline` → `-gv_deferredShading` → 팩 설정 → 엔진 기본 포워드 순서입니다.
         * @details 명령줄은 측정 · 검증 스위치라 게임 데이터보다 앞선다(유니티 Graphics 설정의 Render Pipeline Asset 을 실행 인자가 덮는 것과 같다).
         */
        static string_view computePipelinePath( const PipelineChoice& choice )
        {
            if ( choice._argumentPath.empty() == false )
                return choice._argumentPath;
            if ( choice._commandLinePath.empty() == false )
                return choice._commandLinePath;
            if ( choice._bDeferred && choice._deferredPath.empty() == false )
                return choice._deferredPath;
            if ( choice._gamePath.empty() == false )
                return choice._gamePath;
            return choice._forwardPath;
        }

        /**
         * @brief GPU 인스턴스 회전의 기준 각속도와 편차 폭(라디안/초)입니다.
         * @details 편차가 기준보다 **커야** 인스턴스마다 속도가 확연히 갈린다. 폭이 기준보다 작으면
         *          속도 차이가 눈에 띄지 않아 결국 "다 같은 속도" 로 보인다. 그것이 이 패스를 만든 이유다.
         *          instance_animation.hlsl 이 시드 해시로 [기준, 기준+폭) 에서 속도를 고르고 방향도 정한다.
         */
        static constexpr float32 kGPUSpinBaseSpeed  = 0.35f;
        static constexpr float32 kGPUSpinSpeedRange = 1.75f;

        /**
         * @brief GPU 메시 모프의 변형 크기(로컬 단위)와 공간 주파수입니다.
         * @details 도형이 모두 단위 크기(반지름 0.5)라 진폭이 0.5 에 가까우면 원점을 지나 뒤집힌다.
         *          주파수는 정점마다 위상을 어긋나게 하는 값이다. 0 이면 도형이 통째로 커졌다 작아질 뿐
         *          형태가 변하지 않아, 모프가 실제로 적용되었는지 이미지로 구분할 수 없다.
         */
        static constexpr float32 kMeshMorphAmplitude = 0.12f;
        static constexpr float32 kMeshMorphFrequency = 6.0f;

        /**
         * @brief 컬링 디스패치 상수(gpu_cull.hlsl `CullParams`, b0)입니다. 상수 버퍼 크기와 올리는 값이 이 정의 하나를 씁니다.
         * @details HZB 셋은 가림 판정이다(`HZBOcclusion::fillCullParams`) — 피라미드의 깊이를 그린 뷰-투영(행 벡터 규약, 셰이더가 `-Zpr` 로 그대로 읽는다),
         *          제어(지은 깊이 사각형 폭 · 높이 px, 밉 수, 판정 발자국), 값(깊이 치우침). 발자국이 0 이면 셰이더가 나머지를 읽지 않는다(그림자 슬롯 · 피라미드가 없는 프레임 · `gv_occlusionCulling` 끔).
         *          끝의 단계는 2 단계 가림이다(gpu_cull.hlsl 머리말 — 0 한 단계, 1 앞, 2 뒤).
         */
        struct GPUCullParams
        {
            float32 _arrPlane[6][4]{};
            uint32  _instanceCount{ 0 };
            uint32  _batchCount{ 0 };
            uint32  _visibleSlotCount{ 0 };  ///< 가시 목록의 칸 수(가시 구간의 합). 셰이더 g_VisibleSlotCount
            uint32  _instanceSlotCount{ 0 }; ///< 칸 순 인스턴스 버퍼의 원소 수(GPUScene 칸 수). 셰이더 g_InstanceSlotCount — 차례 → 칸 목록이 가리킨 칸의 범위다
            float32 _arrHZBViewProj[16]{};
            uint32  _arrHZBControl[4]{}; ///< xy 지은 깊이 사각형(px), z 밉 수, w 판정 발자국(0 = 판정 끔). 셰이더 g_HZBControl
            float32 _arrHZBParams[4]{};  ///< x 깊이 치우침(NDC). 셰이더 g_HZBParams
            /// @brief 인스턴스 LOD 선택의 화면 크기 식 항입니다(`LODView::computeProjectionTerms`). xyz 클립 w 를 만드는 열, w 가 m44.
            float32 _arrLODDepthRow[4]{};
            /// @brief xyz 눈 위치(눈이 경계 구 안이면 크기 1), w 투영 y 배율.
            float32 _arrLODView[4]{};
            /// @brief x 인스턴스 LOD 선택 켬 1 · 끔 0(끄면 늘 단계 0). 나머지는 예약.
            uint32 _arrLODControl[4]{};
            /// @brief x 컬링 단계(`kCullPhaseSingle` · `kCullPhaseEarly` · `kCullPhaseLate`). 셰이더 g_CullPhase
            uint32 _arrCullPhase[4]{};
        };
        /// @brief 컬링 단계(gpu_cull.hlsl g_CullPhase.x) — 한 단계, 2 단계 가림의 앞(가려진 차례를 적는다) · 뒤(적힌 차례만 다시 판정한다).
        static constexpr uint32 kCullPhaseSingle = 0;
        static constexpr uint32 kCullPhaseEarly  = 1;
        static constexpr uint32 kCullPhaseLate   = 2;
        /// @brief 가림 타일 한 변(px)입니다. 타일마다 지난 장면 깊이의 최대를 하나 둔다 — HZB 의 16 px 밉(`HZBLayout::kTileMip`, hzb_build.hlsl)이 쓴다.
        static constexpr uint32 kOcclusionTileSize = 16;
        /** @brief @p value 를 @p divisor 칸으로 나눈 칸 수(올림)입니다. 가림 타일 격자와 그 디스패치 그룹 수가 쓴다. */
        static constexpr uint32 divideRoundUp( uint32 value, uint32 divisor ) { return ( value + divisor - 1u ) / divisor; }
        /** @brief 인스턴스 애니메이션 디스패치 상수(instance_animation.hlsl `AnimationParams`, b0)입니다. */
        struct GPUAnimationParams
        {
            float32 _time{ 0.0f };
            float32 _baseSpeed{ 0.0f };
            float32 _speedRange{ 0.0f };
            uint32  _instanceCount{ 0 };
        };
        /** @brief 메시 모프 디스패치 상수(mesh_morph.hlsl `MorphParams`, b0)입니다. */
        struct GPUMorphParams
        {
            float32 _time{ 0.0f };
            float32 _amplitude{ 0.0f };
            float32 _frequency{ 0.0f };
            uint32  _vertexCount{ 0 };
        };
        /** @brief 메시 스킨 디스패치 상수(mesh_skin.hlsl `SkinParams`, b0)입니다. */
        struct GPUSkinParams
        {
            uint32 _skinVertexBase{ 0 };
            uint32 _skinVertexCount{ 0 };
            uint32 _skinBoneCount{ 0 };
            uint32 _skinInstanceCount{ 0 };
            uint32 _arrPad[4]{ 0, 0, 0, 0 };
        };
        /** @brief 인스턴스 정렬 디스패치 상수(instance_sort.hlsl `SortParams`, b0)입니다. */
        struct GPUSortParams
        {
            float32 _arrCameraPos[4]{};
            uint32  _instanceCount{ 0 };
            uint32  _batchCount{ 0 };
            uint32  _bUseViewRank{ 0 };        ///< 1이면 t2 의 뷰 순번으로 정렬하고(추가 뷰), 0이면 인스턴스 번호로 정렬한다(주 시점, 그림자).
            uint32  _transparentTailBase{ 0 }; ///< 순번 테이블의 0번이 가리키는 인스턴스 번호
        };

        static bool isDepthFormat( RHIFormat format ) { return RHIFormatUtil::isDepth( format ); }
        /** @brief 첨부 @p attachmentName 을 컴퓨트 패스가 읽거나 쓰면 true 입니다 — RW 텍스처로 묶을 수 있게 만들어야 합니다. */
        static bool isComputeOutputOrInput( const vector<RenderGraphPassDesc>& listPass, string_view attachmentName )
        {
            for ( const RenderGraphPassDesc& pass : listPass )
            {
                if ( pass._type != RenderPassKind::Compute )
                    continue;
                for ( const string& name : pass._listOutput )
                {
                    if ( name == attachmentName )
                        return true;
                }
                for ( const string& name : pass._listInput )
                {
                    if ( name == attachmentName )
                        return true;
                }
            }
            return false;
        }

        /**
         * @brief IEEE half(16비트) 한 채널을 [0,1] 로 자른 8비트 값으로 바꿉니다.
         * @details HDR 첨부를 PPM 으로 덤프할 때만 쓴다. 톤매핑하지 않고 그냥 자른다. 이 덤프는
         *          이미지를 예쁘게 보려는 것이 아니라 그 단계에 **무엇이 들어 있나** 를 보려는 것이다.
         */
        static uint8 halfToUnorm8( uint16 bits )
        {
            const half value = half::fromBits( bits );
            if ( ( bits & 0x8000u ) != 0u )
                return 0; // 음수(-0 · 음의 NaN 포함)는 0으로 자른다.
            if ( value.isFinite() == false )
                return 255; // Inf 나 NaN 은 눈에 띄게 흰색으로 둔다. unorm8::fromFloat 는 NaN 을 0 으로 만들어 따로 가른다.
            return unorm8::fromFloat( value.toFloat() ).getRaw();
        }

        /**
         * @brief 패스의 `_listOutput` 에서 컬러 어태치먼트 포맷을 순서대로 모읍니다. 반환값은 컬러 RT 수입니다.
         * @details PSO 생성(FramePSOManager::makePassPSODesc)과 셰이더 쿠커가 **같은 답**을 내야 하는 규칙이다. 컬러
         *          출력이 하나도 없으면 뎁스만 쓰는 패스이고, 그 패스에는 픽셀 스테이지가 없다. 출력 선언이
         *          아예 없으면 여기서는 알 수 없으므로 fallbackCount 를 반환한다(호출하는 쪽이 패스 타입으로 정한 값).
         *          Swapchain 출력은 컬러 하나로 세고 포맷은 Unknown 이다(대상 포맷은 출력 패스가 정한다). 첨부에 없는 다른 이름은
         *          파이프라인 로드가 이미 거절했으므로 세지 않는다 — 선언이 있으면 넘긴 수로 물러나지 않는다.
         * @param pOutFormat 컬러 포맷을 받을 배열입니다. nullptr 이면 개수만 셉니다.
         * @param capacity   pOutFormat 의 크기입니다. 그보다 많은 컬러 출력은 버립니다.
         */
        static uint32 collectColorOutputFormats( const RenderGraphPassDesc&          pass,
                                                 const vector<RenderPassAttachment>& listAttachment,
                                                 RHIFormat*                          pOutFormat,
                                                 uint32                              capacity,
                                                 uint32                              fallbackCount )
        {
            if ( pass._listOutput.empty() )
                return fallbackCount;

            uint32 colorCount{ 0 };
            for ( const string& outName : pass._listOutput )
            {
                if ( colorCount >= capacity )
                    break;
                if ( outName == Attachment::kSwapchain )
                {
                    if ( pOutFormat != nullptr )
                        pOutFormat[colorCount] = RHIFormat::Unknown;
                    ++colorCount;
                    continue;
                }
                for ( const RenderPassAttachment& attachment : listAttachment )
                {
                    if ( attachment._name != outName )
                        continue;
                    const RHIFormat format = attachment._format;
                    if ( isDepthFormat( format ) == false )
                    {
                        if ( pOutFormat != nullptr )
                            pOutFormat[colorCount] = format;
                        ++colorCount;
                    }
                    break;
                }
            }
            return colorCount;
        }

        /**
         * @brief 이 패스에 픽셀 스테이지가 있는지 반환합니다. 컬러 출력이 하나라도 있어야 합니다.
         * @details 셰이더 쿠커가 "이 패스의 PS 를 쿠킹하는가" 를 정할 때 쓴다. 런타임은 같은 규칙을
         *          FramePSOManager::makePassPSODesc 이 RT 수로 적용한다(RT 가 0개면 PS 경로를 비운다). 주의: 쿠커와 런타임이
         *          다른 규칙을 쓰면 그림자 패스에 머티리얼 define 을 적용한 변형이 PS 리플렉션을 요구하는데 매니페스트에는
         *          그 조합이 없다(Shipping 의 `리플렉션 매니페스트에 'shadow_depth.hlsl' 가 없습니다`).
         * @note 언리얼은 그림자 깊이에도 **마스크드 머티리얼일 때만** PS 를 붙인다(clip 을 위해). 알파 마스크가
         *       들어오면 이 규칙에 "머티리얼이 픽셀 폐기를 요구하는가" 축이 더해져야 한다. 쿠커도 이 함수를
         *       보므로 그때도 고칠 곳은 여기 하나다.
         */
        static bool hasPixelStage( const RenderGraphPassDesc& pass, const vector<RenderPassAttachment>& listAttachment )
        {
            const uint32 fallbackCount = getRenderPassKindInfo( pass._type ).isDepthOnly() ? 0u : 1u;
            return collectColorOutputFormats( pass, listAttachment, nullptr, kMaxColorAttachments, fallbackCount ) > 0;
        }

        // 이름 목록에서 있는 첫 첨부를 고르던 도우미(`pickFirstExisting`)는 지웠다 — 선언 대신 이름으로 짐작하는 모양이라 다시 쓰지 못하게 했다.
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AttachmentNames
     * @brief 어태치먼트와 패스 리소스 이름의 hashed_string 캐시입니다.
     * @details PassConstantNames 와 같은 이유로 둔다. hashed_string 생성은 전역 레지스트리 intern
     *          (FNV 해시, 32-way 샤드 뮤텍스, 조회)이다. 이 이름들은 모두 코드 리터럴이라
     *          값이 고정이므로 한 번만 intern 한다. 특히 `commitBindlessTextureBindings` 는 DX11 과 GL 경로에서
     *          **드로우 호출마다** 불리므로 이름을 그 자리에서 만들면 안 된다.
     * @note 문자열이 필요한 곳에는 `view()` 를 씁니다. 락 없는 O(1) 포인터 역참조입니다.
     */
    struct AttachmentNames
    {
        hashed_string _swapchain{ FrameRendererUtil::Attachment::kSwapchain };
        hashed_string _sceneColor{ FrameRendererUtil::Attachment::kSceneColor };
        hashed_string _sceneDepth{ FrameRendererUtil::Attachment::kSceneDepth };
        hashed_string _shadowMap{ FrameRendererUtil::Attachment::kShadowMap };
        /// 점광 · 스폿 그림자 아틀라스 — 첨부 이름이 곧 역할 이름(`g_LocalShadowMapIndex`)이다. 그림자 캐시 층도 복원 드로우 동안 이 이름으로 건다.
        hashed_string _localShadowMap{ FrameRendererUtil::Attachment::kLocalShadowMap };
        hashed_string _gbufferAlbedo{ FrameRendererUtil::Attachment::kGBufferAlbedo };
        hashed_string _gbufferNormal{ FrameRendererUtil::Attachment::kGBufferNormal };
        hashed_string _gbufferMaterial{ FrameRendererUtil::Attachment::kGBufferMaterial };
        hashed_string _gbufferCustomData{ FrameRendererUtil::Attachment::kGBufferCustomData };
        hashed_string _litColor{ FrameRendererUtil::Attachment::kLitColor };
        hashed_string _bloomColor{ FrameRendererUtil::Attachment::kBloomColor };
        hashed_string _outlineColor{ FrameRendererUtil::Attachment::kOutlineColor };
        hashed_string _transparentColor{ FrameRendererUtil::Attachment::kTransparentColor };
        hashed_string _taaColor{ FrameRendererUtil::Attachment::kTAAColor };

        /// 셰이더가 보는 이름입니다(어태치먼트 이름과 다를 수 있습니다. registerPassTexture 의 canonical 인자).
        /// 값은 RenderPassInputRole 의 이름과 같습니다. 역할로 바인딩하고 셰이더가 `g_<Role>Index` 로 읽습니다.
        hashed_string _sourceColor{ "SourceColor" };
        hashed_string _ambientOcclusion{ "AmbientOcclusion" };
        hashed_string _bloomBright{ "BloomBright" };
        /// 반투명 앞에 떠 둔 장면 색 사본(SceneColorCopy 패스의 출력) — 반투명 패스가 선언한 입력을 역할로 건다(물의 굴절).
        hashed_string _sceneColorCopy{ "SceneColorCopy" };
        /// 반투명 앞에 떠 둔 장면 깊이 사본(SceneDepthCopy 패스의 출력, R32 실수) — 반투명 패스가 선언한 입력을 역할로 건다(물의 화면 공간 두께).
        hashed_string _sceneDepthCopy{ "SceneDepthCopy" };
        /// 화면 속도(SceneVelocity — 불투명 지오메트리 패스의 출력) — TAA · 모션 블러가 선언한 입력을 역할로 건다(`g_VelocityIndex`).
        hashed_string _velocity{ "Velocity" };
        /// 모션 블러 이웃 최대 속도(MotionBlurNeighborMax 출력) — 모션 블러가 `g_VelocityNeighborMaxIndex` 로 읽는다.
        hashed_string _velocityNeighborMax{ "VelocityNeighborMax" };
        /// DBuffer 데칼 셋(Decal 패스의 출력) — 베이스 패스(G버퍼 · 포워드 불투명)가 선언한 입력을 역할로 건다(`g_DBufferAIndex` · `g_DBufferBIndex` · `g_DBufferCIndex`).
        hashed_string _dbufferA{ "DBufferA" };
        hashed_string _dbufferB{ "DBufferB" };
        hashed_string _dbufferC{ "DBufferC" };
        /// 볼륨 구름(VolumetricClouds 패스의 저해상도 출력) — 스카이 패스가 선언한 입력을 역할로 건다(`g_SkyCloudsIndex`).
        hashed_string _skyClouds{ "SkyClouds" };
        /// 화면 왜곡 오프셋(SceneDistortion — 왜곡 누적 패스의 출력) — 왜곡 적용 패스가 선언한 입력을 역할로 건다(`g_DistortionIndex`).
        hashed_string _distortion{ "Distortion" };
        /// 접촉 그림자 마스크(ContactShadow 패스의 출력, 채널 넷) — 조명 · 포워드 불투명 패스가 선언한 입력을 역할로 건다(`g_ContactShadowIndex`).
        hashed_string _contactShadow{ "ContactShadow" };
        /// 2D 빛 쿠키 아틀라스(Light2DCookieAtlas) — 파이프라인 첨부가 아니라 렌더러가 소유한 텍스처를 역할 이름으로 건다.
        hashed_string _light2DCookieAtlas{ "Light2DCookieAtlas" };
        /// 자동 노출의 이번 프레임 EV(1x1, 뷰의 `_autoExposureHistory`) — 톤맵하는 패스가 `g_AutoExposureIndex` 로 읽는다. 렌더러가 소유한 텍스처다.
        hashed_string _autoExposure{ "AutoExposure" };
        /// 컬러 그레이딩 LUT(뷰의 후처리 값 `_colorGradingLUT`, `PathTextureSet`) — 톤맵하는 패스가 `g_ColorGradingLUTIndex` 로 읽는다. 텍스처 캐시가 소유한 텍스처다.
        hashed_string _colorGradingLUT{ "ColorGradingLUT" };
        /// 겹친 볼륨이 섞는 둘째 컬러 그레이딩 LUT(뷰의 후처리 값 `_colorGradingLUTBlend`) — 톤맵하는 패스가 `g_ColorGradingLUTBlendIndex` 로 읽는다. 텍스처 캐시가 소유한 텍스처다.
        hashed_string _colorGradingLUTBlend{ "ColorGradingLUTBlend" };
        /// 블룸 먼지 마스크(뷰의 후처리 값 `_bloomDirtMask`) — 블룸을 더하는 패스가 `g_BloomDirtMaskIndex` 로 읽는다. 텍스처 캐시가 소유한 텍스처다.
        hashed_string _bloomDirtMask{ "BloomDirtMask" };
        /// 표면 변형 층의 높이 텍스처(패킷의 `SurfaceDeformBinding`) — 씬 지오메트리 패스의 정점 셰이더가 `g_SurfaceDeformIndex` 로 읽는다. 텍스처 캐시가 소유한 CPU 텍스처다.
        hashed_string _surfaceDeform{ "SurfaceDeform" };
        /// 비 가림 맵의 깊이(`FrameRainOcclusion`) — 표면 셰이더가 `g_RainOcclusionIndex` 로 읽는다. 렌더러가 소유한 텍스처다.
        hashed_string _rainOcclusion{ "RainOcclusion" };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 프로세스 전역 AttachmentNames 를 반환합니다.
     * @details 함수 지역 static 이라 문자열 레지스트리보다 먼저 초기화될 위험이 없다.
     */
    inline const AttachmentNames& attachmentNames()
    {
        static const AttachmentNames s_names{};
        return s_names;
    }

    /**
     * @brief PassCB 와 리소스 이름의 hashed_string 캐시입니다.
     * @details hashed_string 생성은 전역 문자열 레지스트리에 intern 하는 작업이다(FNV 해시,
     *          샤드 공유락, 조회). 리터럴은 값이 고정이므로(`g_World` 처럼 드로우마다 쓰는 것도) 한 번만 만들어 다시 쓴다.
     */
    struct PassConstantNames
    {
        hashed_string _lightViewProj{ "g_LightViewProj" };
        hashed_string _viewProj{ "g_ViewProj" };
        /// @brief 뷰-투영의 역행렬입니다. 디퍼드 조명이 깊이에서 월드 위치를 복원합니다.
        hashed_string _invViewProj{ "g_InvViewProj" };
        /// @brief 지난번 이 뷰를 그린 뷰-투영입니다(모션 블러가 화면 속도를 구한다, motion_blur.hlsl). 처음 · 컷 프레임은 이번 것과 같다.
        hashed_string _prevViewProj{ "g_PrevViewProj" };
        hashed_string _temporalJitter{ "g_TemporalJitter" }; ///< 이번 프레임 투영 지터(xy, NDC) — 화면 속도가 뺀다
        hashed_string _textureMipBias{ "g_TextureMipBias" }; ///< 머티리얼 텍스처 밉 바이어스(x = log2(렌더 높이 / 출력 높이), `ViewTextureMipBiasUtil`)
        hashed_string _world{ "g_World" };
        hashed_string _keyLightDirIntensity{ "g_KeyLightDirIntensity" };
        hashed_string _keyLightColor{ "g_KeyLightColor" };
        /// @brief 하늘 그러데이션입니다(스카이 패스). 배치는 `SkyGradient` 머리 주석 — 천정(rgb, w 지수) · 지평선(rgb) · 땅(rgb, w 세기).
        hashed_string _skyZenith{ "g_SkyZenith" };
        hashed_string _skyHorizon{ "g_SkyHorizon" };
        hashed_string _skyGround{ "g_SkyGround" };
        /// @brief 대기 산란과 구름 층입니다(스카이 패스). 배치는 `FrameRenderer::FrameSkyState` 주석.
        hashed_string _skyAtmosphereRayleigh{ "g_SkyAtmosphereRayleigh" };
        hashed_string _skyAtmosphereMie{ "g_SkyAtmosphereMie" };
        hashed_string _skyAtmosphereGeometry{ "g_SkyAtmosphereGeometry" };
        hashed_string _skyCloudShape{ "g_SkyCloudShape" };
        hashed_string _skyCloudWind{ "g_SkyCloudWind" };
        hashed_string _skyCloudColor{ "g_SkyCloudColor" };
        hashed_string _skyCloudVolume{ "g_SkyCloudVolume" };
        hashed_string _skyCloudSun{ "g_SkyCloudSun" };
        hashed_string _skyCloudAmbient{ "g_SkyCloudAmbient" };
        hashed_string _skyCloudGround{ "g_SkyCloudGround" };
        hashed_string _skyCloudSample{ "g_SkyCloudSample" }; ///< 뷰마다 볼륨 구름 패스가 쓴다(`FramePassExecutor::executeVolumetricCloudsPass`)
        /// @brief 스카이 라이트의 프로브 버퍼 자리입니다(lighting.hlsli `SkyLight`). 배치는 `LightProbeElementBuilder::getSkyLight`.
        hashed_string _skyLight{ "g_SkyLight" };
        /// @brief 높이 · 거리 지수 안개입니다(안개 패스). 배치는 `FogSettings` 머리 주석 — 색 · 밀도 / 높이 감쇠 · 기준 높이 · 시작 거리 · 최대 불투명도 / 방향광 산란 / 볼륨 안개.
        hashed_string _fogColorDensity{ "g_FogColorDensity" };
        hashed_string _fogHeightParams{ "g_FogHeightParams" };
        hashed_string _fogInscatter{ "g_FogInscatter" };
        hashed_string _fogVolumeParams{ "g_FogVolumeParams" };
        /// @brief 표면 변형 층의 창(`SurfaceDeformBinding::_params`) — 지면 · 지형의 정점 셰이더와 그림자 패스가 같은 값을 읽는다.
        hashed_string _surfaceDeformParams{ "g_SurfaceDeformParams" };
        /// @brief 전역 머티리얼 값(`MaterialGlobals`)입니다.
        hashed_string _materialGlobalWeather{ "g_MaterialGlobalWeather" };
        hashed_string _materialGlobalRain{ "g_MaterialGlobalRain" };
        hashed_string _materialGlobalEnvironment{ "g_MaterialGlobalEnvironment" };
        /// @brief 비 가림 맵의 뷰-투영과 (깊이 바이어스 NDC, 노멀 오프셋 m, 1 / 한 변, 켬)입니다(`FrameRainOcclusion`).
        hashed_string _rainOcclusionViewProj{ "g_RainOcclusionViewProj" };
        hashed_string _rainOcclusionParams{ "g_RainOcclusionParams" };
        /// @brief 안개 패스의 진단 보기입니다(`FrameFog::makeDebugParams`, fog.hlsl).
        hashed_string _fogDebugParams{ "g_FogDebugParams" };
        hashed_string _postProcessMaterial{ "g_PostProcessMaterial" };   ///< 후처리 머티리얼 그리기마다의 무게(x)
        hashed_string _decalFirst{ "g_DecalFirst" };                     ///< 데칼 인스턴스 그리기 묶음의 첫 원소(decal.hlsl — SV_InstanceID 에 더한다)
        hashed_string _decalCount{ "g_DecalCount" };                     ///< 데칼 구조 버퍼의 원소 수 — 셰이더가 그 밖을 읽지 않는다
        hashed_string _swDecals{ "SwDecals" };                           ///< 데칼 구조 버퍼(decal.hlsl g_SwDecals, `FrameRenderState::_decalBuffer`)
        hashed_string _lensFlareTexture{ "g_LensFlareTexture" };         ///< 렌즈 플레어 Image 구간의 텍스처 번호(MaterialTexture::sampleIndex 의 뜻, 없으면 kInvalidIndex)
        hashed_string _deferredTileCapacity{ "g_DeferredTileCapacity" }; ///< 디퍼드 조명 타일 목록의 종류 하나 칸 수(타일 그리기 — deferred_lighting.hlsl VSTile)
        hashed_string _shadowParams{ "g_ShadowParams" };
        /// @brief 방향광 캐스케이드 배치입니다 — x 수, y 칸 배율, z 경계 섞기 폭, w 거리 끝에서 옅어지기 시작하는 깊이. `ViewShadowData::_cascadeLayout`.
        hashed_string _shadowCascadeLayout{ "g_ShadowCascadeLayout" };
        /// @brief 캐스케이드마다 (배율, 이동 x, 이동 y, 깊이 끝)입니다. 이름이 넷인 까닭: 배열 멤버의 리플렉션 이름이 백엔드마다 다르다(GL 은 `[0]` 이 붙는다).
        hashed_string _arrShadowCascade[shaderslot::kShadowCascadeMax]{ hashed_string{ "g_ShadowCascade0" }, hashed_string{ "g_ShadowCascade1" },
                                                                        hashed_string{ "g_ShadowCascade2" }, hashed_string{ "g_ShadowCascade3" } };
        /// @brief 그림자 필터입니다 — x 품질(0 PCF 3x3, 1 PCSS), y 방향광 반그림자 배율, z · w 커널 반지름 상한 · 하한(텍셀). `ShadowFilter::makeShaderParams`.
        hashed_string _shadowFilter{ "g_ShadowFilter" };
        /// @brief 접촉 그림자 채널 넷의 빛 원소 번호입니다(-1 = 빈 채널). `FrameShadowData::_contactShadowLights`, contact_shadow.hlsl.
        hashed_string _contactShadowLights{ "g_ContactShadowLights" };
        hashed_string _bloomParams{ "g_BloomParams" };
        /// @brief 카메라별 색 보정입니다 — x 노출 배율(2^EV), y 채도, z 대비, w 비네트 세기. `PostProcessValues`, post_grade.hlsli.
        hashed_string _cameraGrade{ "g_CameraGrade" };
        /// @brief 카메라별 화면 틀입니다 — x 레터박스로 남길 가로/세로 비율(0 = 없음), y 지금 뷰의 가로/세로 비율, z 색각 보정 방식(화면 출력 뷰만, 렌더 텍스처는 0). post_grade.hlsli.
        hashed_string _cameraFrame{ "g_CameraFrame" };
        /// @brief 화면 출력입니다 — x 톤 곡선 어깨(SDR 1, HDR 출력에 그리는 뷰는 `FrameDisplayEncode::getToneShoulder`). display_encode.hlsli.
        hashed_string _displayOutput{ "g_DisplayOutput" };
        /// @brief 카메라별 피사계 심도입니다 — x 초점 거리(m), y 착란원 반지름 배율(px), z 반지름 상한(px), w 켬(1) · 끔(0, 끈 뷰는 패스를 기록하지 않아 셰이더는 읽지 않는다). depth_of_field.hlsl.
        hashed_string _cameraLens{ "g_CameraLens" };
        /// @brief 컬러 그레이딩 화이트 밸런스 행 셋입니다(`PostGradeConstants::_arrWhiteBalance`, post_grade.hlsli).
        hashed_string _arrGradeWhiteBalance[3]{ hashed_string{ "g_GradeWhiteBalance0" }, hashed_string{ "g_GradeWhiteBalance1" }, hashed_string{ "g_GradeWhiteBalance2" } };
        /// @brief 컬러 그레이딩 범위 상수입니다 — 범위(그림자 · 중간 · 밝은 부분)마다 채도 · 대비 · 감마 · 게인 · 오프셋(`PostGradeConstants::_arrRange` 와 같은 차례).
        /// @brief 컬러 그레이딩 범위 상수의 수입니다(`PostGradeConstants::kRangeCount * kRangeFieldCount`, FramePassExecutor 가 static_assert 로 맞춘다).
        static constexpr uint32 kGradeRangeConstantCount = 15;
        hashed_string           _arrGradeRange[kGradeRangeConstantCount]{ hashed_string{ "g_GradeShadowsSaturation" }, hashed_string{ "g_GradeShadowsContrast" }, hashed_string{ "g_GradeShadowsGamma" },
                                                                          hashed_string{ "g_GradeShadowsGain" }, hashed_string{ "g_GradeShadowsOffset" }, hashed_string{ "g_GradeMidtonesSaturation" },
                                                                          hashed_string{ "g_GradeMidtonesContrast" }, hashed_string{ "g_GradeMidtonesGamma" }, hashed_string{ "g_GradeMidtonesGain" },
                                                                          hashed_string{ "g_GradeMidtonesOffset" }, hashed_string{ "g_GradeHighlightsSaturation" }, hashed_string{ "g_GradeHighlightsContrast" },
                                                                          hashed_string{ "g_GradeHighlightsGamma" }, hashed_string{ "g_GradeHighlightsGain" }, hashed_string{ "g_GradeHighlightsOffset" } };
        /// @brief 컬러 그레이딩 범위 경계 · 켬(`PostGradeConstants::_params`)과 LUT 크기 · 세기(`_lut`)입니다.
        hashed_string _gradeParams{ "g_GradeParams" };
        hashed_string _gradeLUT{ "g_GradeLUT" };
        /// @brief 렌즈 효과(색수차 세기 · 시작 반경 · 렌즈 왜곡 · 선명화)와 필름 그레인(세기 · 크기 px)입니다. post_grade.hlsli 의 `CameraGrade::applyLens` · `applyFilmGrain`.
        hashed_string _lensParams{ "g_LensParams" };
        hashed_string _grainParams{ "g_GrainParams" };
        /// @brief 블룸 렌즈(먼지 마스크 세기 · 플레어 세기 · 고스트 간격 · 헤일로 반지름)와 플레어 색입니다. post_bloom.hlsli 의 `Bloom::computeLensFlare` · `applyDirt`.
        hashed_string _bloomLens{ "g_BloomLens" };
        hashed_string _flareTint{ "g_FlareTint" };
        /// @brief 화면 공간 반사(세기 · 최대 거리 · 두께 · 최대 거칠기)입니다. screen_space_reflection.hlsl.
        hashed_string _ssrParams{ "g_SSRParams" };
        /// @brief 화면 공간 반사의 HZB(xy 지은 깊이 사각형 px, z 밉 수)입니다. screen_space_reflection.hlsl 의 hi-z trace.
        hashed_string _ssrHZBParams{ "g_SSRHZBParams" };
        hashed_string _outlineColor{ "g_OutlineColor" };
        hashed_string _outlineParams{ "g_OutlineParams" };
        hashed_string _sourceTexel{ "g_SourceTexel" };
        /// @brief 뷰 사각형 배율(xy 이번, zw 지난 프레임)입니다. binding.hlsli PassCB, `FrameRendererUtil::makeViewRectConstants`.
        hashed_string _viewRectScale{ "g_ViewRectScale" };
        /// @brief 뷰 사각형 크기(xy 픽셀)와 풀 텍셀(zw = 1 / 풀 픽셀)입니다.
        hashed_string _viewRectSize{ "g_ViewRectSize" };
        /// @brief 출력 해상도 공간의 사각형 크기(xy 픽셀)와 출력 풀 텍셀(zw)입니다 — 시간 업스케일 뒤 공간. 업스케일하지 않으면 `_viewRectSize` 와 같다.
        hashed_string _outputRectSize{ "g_OutputRectSize" };
        /// @brief 시간 업스케일(x 켬 1 · 끔 0, y 렌더 / 출력 배율)입니다. taa.hlsl.
        hashed_string _temporalUpscale{ "g_TemporalUpscale" };
        hashed_string _flags{ "g_Flags" };
        /// @brief 인스턴스 버퍼 원소 수입니다. 셰이더 InstanceData::load 가 범위를 제한합니다.
        hashed_string _swInstanceCount{ "g_SwInstanceCount" };
        /// @brief 가시 인스턴스 ID 목록의 칸 수(가시 구간의 합)입니다. PassCB g_SwVisibleSlotCount 에 대응하며, 셰이더 InstanceData::resolveID 가 슬롯 범위를 제한합니다.
        hashed_string _swVisibleSlotCount{ "g_SwVisibleSlotCount" };
        /// @brief 배치의 머티리얼 데이터 버퍼 원소 수입니다. 셰이더 SW_MATERIAL 이 클램프합니다.
        hashed_string _swMaterialCount{ "g_SwMaterialCount" };
        hashed_string _swInstances{ "SwInstances" };
        /// @brief GPU 가 변형한 정점 풀입니다. binding.hlsli 의 g_SwMorphVertices(t7)와 PassCB g_SwMorphVerticesIndex 에 대응합니다.
        hashed_string _swMorphVertices{ "SwMorphVertices" };
        /// @brief 그 풀의 원소 수입니다. PassCB g_SwMorphVertexCount 에 대응합니다.
        hashed_string _swMorphVertexCount{ "g_SwMorphVertexCount" };
        /// @brief 스킨 칸 → 결과 시작 표입니다. binding.hlsli 의 g_SwSkinResultOffsets(t14)와 PassCB g_SwSkinResultOffsetsIndex 에 대응합니다.
        hashed_string _swSkinResultOffsets{ "SwSkinResultOffsets" };
        /// @brief 그 표의 칸 수입니다. PassCB g_SwSkinResultOffsetCount 에 대응합니다.
        hashed_string _swSkinResultOffsetCount{ "g_SwSkinResultOffsetCount" };
        /// @brief 스킨 결과의 이번 벌 이동(정점)입니다. PassCB g_SwSkinResultShift 에 대응합니다(`GPUMeshMorphPool::getSkinCurrentShift`).
        hashed_string _swSkinResultShift{ "g_SwSkinResultShift" };
        /// @brief 지난 프레임 스킨 결과가 든 벌의 이동(정점, 없으면 무효)입니다. PassCB g_SwSkinPreviousShift 에 대응합니다.
        hashed_string _swSkinPreviousShift{ "g_SwSkinPreviousShift" };
        /// @brief 인스턴스마다의 지난 월드 행렬입니다. binding.hlsli 의 g_SwPrevInstanceWorlds(t15)와 PassCB g_SwPrevInstanceWorldsIndex 에 대응합니다.
        hashed_string _swPrevInstanceWorlds{ "SwPrevInstanceWorlds" };
        /// @brief 그 버퍼의 원소 수입니다. PassCB g_SwPrevInstanceWorldCount 에 대응합니다.
        hashed_string _swPrevInstanceWorldCount{ "g_SwPrevInstanceWorldCount" };
        /// @brief 컬링이 만든 가시 인스턴스 ID 목록입니다. binding.hlsli g_SwVisibleInstanceIDs 와 "SwVisibleInstanceIDs" 에 대응합니다.
        hashed_string _swVisibleInstanceIDs{ "SwVisibleInstanceIDs" };
        /// @brief 배치의 머티리얼 데이터 구조 버퍼입니다. binding.hlsli g_SwMaterials 와 "SwMaterials" 에 대응하며, 배치마다 등록합니다.
        hashed_string _swMaterials{ "SwMaterials" };
        /// @brief 씬 라이트 구조 버퍼입니다. lighting.hlsli g_SwLights 와 "SwLights" 에 대응하며, 패스당 한 번 바인딩합니다.
        hashed_string _swLights{ "SwLights" };
        /// @brief 씬 배치 테이블입니다. binding.hlsli g_SwBatches 와 "SwBatches" 에 대응하며, 패스당 한 번 바인딩합니다. 드로우는 배치 번호만 전달합니다.
        hashed_string _swBatches{ "SwBatches" };
        /// @brief 그 테이블의 원소 수입니다. PassCB g_SwBatchCount 에 대응합니다.
        hashed_string _swBatchCount{ "g_SwBatchCount" };
        /// @brief 씬 라이트 버퍼의 빛 원소 수입니다. PassCB g_SwLightCount 에 대응하며, 0이면 셰이더가 빛을 돌지 않습니다(앰비언트만).
        hashed_string _swLightCount{ "g_SwLightCount" };
        /// @brief 씬 라이트 버퍼의 전체 원소 수(빛 + 그림자 타일 원소)입니다. PassCB g_SwLightElementCount 에 대응하며, 셰이더가 그 밖을 읽지 않습니다(LightBuffer::loadElement).
        hashed_string _swLightElementCount{ "g_SwLightElementCount" };
        /// @brief 지금 뷰의 라이트 그리드 첫 단어입니다. PassCB g_SwLightGridBase 에 대응하며, 없으면 constant::invalid_index::kUint32 입니다(셰이더는 목록 전체를 돕니다).
        hashed_string _swLightGridBase{ "g_SwLightGridBase" };
        /// @brief 라이트 그리드 버퍼의 단어 수입니다. PassCB g_SwLightGridWordCount 에 대응하며, 셰이더가 그 밖을 읽지 않습니다(LightGrid::loadWord).
        hashed_string _swLightGridWordCount{ "g_SwLightGridWordCount" };
        /// @brief 뷰마다의 라이트 그리드 버퍼입니다. lighting.hlsli g_SwLightGrid(t12)와 "SwLightGrid" 에 대응하며, 패스당 한 번 바인딩합니다(CPU · GPU 생산자 공통).
        hashed_string _swLightGrid{ "SwLightGrid" };
        /// @brief 라이트 프로브 SH9 버퍼입니다. lighting.hlsli g_SwLightProbes(t13)와 "SwLightProbes" 에 대응하며, 볼륨이 있는 프레임에만 바인딩합니다.
        hashed_string _swLightProbes{ "SwLightProbes" };
        /// @brief 라이트 프로브 버퍼의 원소(float4) 수입니다. PassCB g_SwLightProbeElementCount 에 대응하며, 셰이더가 그 밖을 읽지 않습니다(LightProbe::loadFromXML).
        hashed_string _swLightProbeElementCount{ "g_SwLightProbeElementCount" };
        /// @brief 정점 애니메이션(VAT) 테이블입니다. binding.hlsli g_SwVertexAnimation 과 "SwVertexAnimation" 에 대응하며, 패스당 한 번 바인딩합니다.
        hashed_string _swVertexAnimation{ "SwVertexAnimation" };
        /// @brief 그 테이블의 원소 수입니다. PassCB g_SwVertexAnimationCount 에 대응합니다.
        hashed_string _swVertexAnimationCount{ "g_SwVertexAnimationCount" };
        /// @brief VAT 시계(초)입니다. PassCB g_SwVertexAnimationTime 에 대응합니다.
        hashed_string _swVertexAnimationTime{ "g_SwVertexAnimationTime" };
        /// @brief 머티리얼 애니메이션 시계(초)입니다. PassCB g_SwMaterialTime 에 대응하며, VAT 표가 없어도 패스마다 겁니다(툰 UV 애니메이션).
        hashed_string _swMaterialTime{ "g_SwMaterialTime" };
    };

    /**
     * @brief 프로세스 전역 PassConstantNames 를 반환합니다.
     * @details 함수 지역 static 이라 첫 호출 때 한 번만, 스레드 안전하게 만들어진다. 전역 정적
     *          객체로 두면 문자열 레지스트리보다 먼저 초기화될 수 있다.
     */
    inline const PassConstantNames& passConstantNames()
    {
        static const PassConstantNames s_names{};
        return s_names;
    }
} // namespace sw
