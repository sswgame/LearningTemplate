/**
 * @file ViewGraphResource.h
 * @brief 뷰 그래프 패스 종류가 쓰는 엔진 자원(첨부 밖 — 뷰가 가진 텍스처 · 프레임 그래프 버퍼의 이 뷰 칸)의 접근 핸들입니다.
 * @details 파이프라인 XML 은 첨부만 적는다. 패스 종류가 스스로 쓰는 엔진 자원(자동 노출 기록 · FSR 중간 RT · 그림자 캐시 층 · 깊이 사본 · HZB · 컬링 버퍼)은
 *          종류가 선언한다(`FramePassExecutor::bindPassCallbacks`) — 언리얼 RDG 패스 파라미터가 패스 형식에 박힌 자원을 적는 자리다. 핸들은 뷰마다 `FramePassExecutor::registerViewResources`
 *          가 칸 0 으로 등록한다(이 뷰의 것만 — 등록 조건이 곧 실행 조건이다). 패스 몸통은 이 핸들로만 그 자원을 받고(`RenderGraphPassContext::getTexture` · `getBuffer`) 패스 안 왕복도
 *          이 핸들로 옮긴다(`RenderGraphPassContext::transition`).
 */
#pragma once
#include "Core/Name/hashed_string.h"

#include "Engine/Renderer/Graph/RenderGraph.h"

namespace sw
{
    /**
     * @struct ViewGraphResource
     * @brief 뷰 그래프 엔진 자원의 이름 · 접근입니다. 이름은 한 번만 intern 한다(전역 문자열 표의 뮤텍스를 프레임마다 잡지 않는다).
     */
    struct ViewGraphResource
    {
        /** @brief 지난 프레임의 노출 기록(읽는 칸)입니다 — 톤맵하는 패스가 읽는다. */
        static RenderGraphTextureSRV autoExposureHistory()
        {
            static const hashed_string kName( "AutoExposureHistory" );
            return RenderGraphTextureSRV{ kName };
        }
        /** @brief 이번 프레임의 노출 기록(쓰는 칸)입니다 — 톤맵하는 패스가 히스토그램 컴퓨트(UAV) 또는 1 px 그리기(렌더 타깃)로 쓰고 같은 패스가 셰이더 읽기로 읽는다. */
        static RenderGraphTextureUAV autoExposureTarget()
        {
            static const hashed_string kName( "AutoExposureTarget" );
            return RenderGraphTextureUAV{ kName };
        }
        /** @brief FSR 1 의 EASU 출력(중간 RT)입니다 — Present 가 그리고 같은 패스의 RCAS 가 읽는다. */
        static RenderGraphRenderTarget upscaleIntermediate()
        {
            static const hashed_string kName( "UpscaleIntermediate" );
            return RenderGraphRenderTarget{ kName };
        }
        /** @brief 점광 · 스폿 그림자 캐시 층입니다 — 다시 그리는 프레임에만 등록한다(그림자 패스가 깊이로 그리고 같은 패스의 복원이 셰이더 읽기로 읽는다). */
        static RenderGraphDepthWrite shadowCacheLayer()
        {
            static const hashed_string kName( "ShadowCacheLayer" );
            return RenderGraphDepthWrite{ kName };
        }
        /** @brief 같은 자리 후처리 머티리얼의 앞 결과 사본(톤맵 앞)입니다 — 출력을 복사해 다음 머티리얼이 읽는다. */
        static RenderGraphTextureCopyDest postProcessChainBeforeTonemap()
        {
            static const hashed_string kName( "PostProcessChainBeforeTonemap" );
            return RenderGraphTextureCopyDest{ kName };
        }
        /** @brief 같은 자리 후처리 머티리얼의 앞 결과 사본(톤맵 뒤)입니다. */
        static RenderGraphTextureCopyDest postProcessChainAfterTonemap()
        {
            static const hashed_string kName( "PostProcessChainAfterTonemap" );
            return RenderGraphTextureCopyDest{ kName };
        }
        /** @brief 뷰의 깊이 사본(R32 — 이번 프레임 장면 깊이를 옮겨 HZB 를 짓는다)입니다. 2 단계 가림의 뒤 단계 · 화면 공간 반사가 그리고(렌더 타깃) RW 텍스처로 읽는다. */
        static RenderGraphRenderTarget depthHistory()
        {
            static const hashed_string kName( "DepthHistory" );
            return RenderGraphRenderTarget{ kName };
        }
        /** @brief 뷰의 HZB 피라미드입니다(프레임 그래프 `HZB` 의 이 뷰 칸). */
        static RenderGraphBufferUAV hzb()
        {
            static const hashed_string kName( "HZB" );
            return RenderGraphBufferUAV{ kName };
        }
        /** @brief 뷰의 가림 타일입니다(프레임 그래프 `OcclusionTiles` 의 이 뷰 칸). */
        static RenderGraphBufferUAV occlusionTiles()
        {
            static const hashed_string kName( "OcclusionTiles" );
            return RenderGraphBufferUAV{ kName };
        }
        /** @brief 주 시점 컬링 슬롯의 간접 인자입니다(프레임 그래프 `CullArgs` 의 슬롯 0). 2 단계 가림의 뒤 단계가 개수를 더한다. */
        static RenderGraphBufferUAV cullArgs()
        {
            static const hashed_string kName( "CullArgs" );
            return RenderGraphBufferUAV{ kName };
        }
        /** @brief 주 시점 컬링 슬롯의 가시 목록입니다(프레임 그래프 `CullVisibleInstances` 의 슬롯 0). */
        static RenderGraphBufferUAV cullVisibleInstances()
        {
            static const hashed_string kName( "CullVisibleInstances" );
            return RenderGraphBufferUAV{ kName };
        }
        /** @brief 주 시점 컬링 슬롯의 LOD 전환 상태입니다(프레임 그래프 `CullLODState` 의 슬롯 0 — 그 슬롯에 상태가 있을 때만). */
        static RenderGraphBufferUAV cullLODState()
        {
            static const hashed_string kName( "CullLODState" );
            return RenderGraphBufferUAV{ kName };
        }
        /** @brief 뷰의 디퍼드 조명 타일 간접 인자입니다 — 분류를 돌릴 수 있는 뷰만 등록한다(조명 패스가 컴퓨트로 쓰고 같은 패스가 간접 그리기로 읽는다). */
        static RenderGraphBufferUAV deferredTileArgs()
        {
            static const hashed_string kName( "DeferredTileArgs" );
            return RenderGraphBufferUAV{ kName };
        }
        /** @brief 뷰의 디퍼드 조명 타일 목록입니다(타일 그리기의 정점 셰이더가 읽는다). */
        static RenderGraphBufferUAV deferredTileList()
        {
            static const hashed_string kName( "DeferredTileList" );
            return RenderGraphBufferUAV{ kName };
        }
        /** @brief 2 단계 가림의 차례 표시입니다(프레임 그래프 `OccludedOrders`). */
        static RenderGraphBufferUAV occludedOrders()
        {
            static const hashed_string kName( "OccludedOrders" );
            return RenderGraphBufferUAV{ kName };
        }
    };
} // namespace sw
