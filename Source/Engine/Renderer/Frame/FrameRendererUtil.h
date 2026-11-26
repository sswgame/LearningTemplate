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
#include "Core/String/Name/hashed_string.h"
#include "Core/String/StringUtil.h"

#include "Engine/Graphics/RHI/RHIFormatInfo.h"
#include "Engine/Graphics/RenderState/RenderState.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/View/RenderViewSettings.h"
#include "Engine/Renderer/Pipeline/RenderGraphPassDesc.h"
#include "Engine/Renderer/Pipeline/RenderPassKindInfo.h"

namespace sw
{
    /** @brief FrameRenderer 의 번역 단위들이 함께 쓰는 헬퍼입니다(이름 상수는 `RenderPassNames.h`). */
    struct FrameRendererUtil
    {
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
            string_view _commandLinePath; ///< `-gv_renderPipeline`
            string_view _deferredPath;    ///< 엔진 기본 디퍼드(`-gv_deferredShading` 일 때만)
            string_view _gamePath;        ///< 팩 설정(`PackEngineSettings::_renderPipeline`)
            string_view _qualityPath;     ///< 엔진 렌더 설정(`RenderConfig::_renderPipeline` — 바탕 · 플랫폼 · 품질 단계 층을 얹은 값)
            bool        _bDeferred{ false };
        };
        /**
         * @brief 그릴 파이프라인을 고릅니다. `-gv_renderPipeline` → `-gv_deferredShading` → 팩 설정 → 엔진 렌더 설정(플랫폼 · 품질 단계) 순서입니다.
         * @details 명령줄은 측정 · 검증 스위치라 게임 데이터보다 앞선다(유니티 Graphics 설정의 Render Pipeline Asset 을 실행 인자가 덮는 것과 같다).
         *          엔진 렌더 설정은 필수 칸이라 비지 않는다 — 마지막 단계에 코드 기본값이 없다.
         */
        static string_view computePipelinePath( const PipelineChoice& choice )
        {
            if ( choice._commandLinePath.empty() == false )
                return choice._commandLinePath;
            if ( choice._bDeferred && choice._deferredPath.empty() == false )
                return choice._deferredPath;
            if ( choice._gamePath.empty() == false )
                return choice._gamePath;
            return choice._qualityPath;
        }

        /**
         * @brief GPU 인스턴스 회전의 기준 각속도와 편차 폭(라디안/초)입니다.
         * @details 편차가 기준보다 커야 인스턴스마다 속도가 확연히 갈린다. 폭이 기준보다 작으면
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
         *          이미지를 예쁘게 보려는 것이 아니라 그 단계에 무엇이 들어 있나 를 보려는 것이다.
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
         * @details PSO 생성(FramePSOManager::makePassPSODesc)과 셰이더 쿠커가 같은 답을 내야 하는 규칙이다. 컬러
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
         * @note 언리얼은 그림자 깊이에도 마스크드 머티리얼일 때만 PS 를 붙인다(clip 을 위해). 알파 마스크가
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
