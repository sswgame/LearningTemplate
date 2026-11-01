/**
 * @file Engine/Renderer/Frame/Packet/RenderPacketSceneFill.h
 * @brief 씬 하나를 카메라 하나로 본 렌더 패킷을 채웁니다(게임 스레드). 미리보기 씬과 캡처(초상화 · 반사 프로브 굽기)가 같은 규칙을 씁니다.
 * @details 예전에는 미리보기 채우기와 캡처의 직접 그리기(`FrameRenderer::execute( scene )`)가 같은 규칙을 따로 적어 서로 갈라졌다
 *          (미리보기는 빛 쿠키 · 실시간 프로브를 싣지 않았다). 한 곳에 두면 주 패킷과 같은 빛 규칙이 모든 씬 패킷에 간다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Animation/Scene/AnimationLOD.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Frame/View/RenderView.h"

namespace sw
{
    struct RenderFramePacket;
    class GPUSceneBuilder;
    class GPUUploadQueue;
    class Scene;

    /** @brief 씬 패킷 하나가 보는 시점과 출력입니다. */
    struct RenderSceneViewDesc
    {
        RenderViewSettings _settings{};
        float4x4           _viewProj{};
        float3             _cameraPos{};
        RHITextureHandle   _outputRenderTarget{ 0 }; ///< 0 이면 렌더러의 기본 출력(백버퍼 · Present 캡처)
        uint32             _width{ 0 };
        uint32             _height{ 0 };
    };

    /**
     * @class RenderPacketSceneFill
     * @brief 씬 패킷 채우기와 그 작업 배열입니다. 값으로 두고 프레임마다 다시 씁니다.
     */
    class SW_API RenderPacketSceneFill
    {
    public:
        /**
         * @brief @p scene 을 @p view 로 본 패킷을 @p outPacket 에 채웁니다 — 주 시점 · 키 라이트 · 모든 라이트(쿠키 · 실시간 프로브) · 하늘 · 안개 · 그림자 · LOD 뷰 · 씬 스냅샷.
         * @details 패킷을 비우지 않는다(부르는 쪽이 `resetForFrame` 하고 씬 키 · 프레임 번호를 적는다). 스냅샷은 @p builder 가 짓고, 업로드 요청은 @p pUploadQueue 로 낸다(없으면 내지 않는다).
         */
        void fill( RenderFramePacket& outPacket, Scene& scene, const RenderSceneViewDesc& view, GPUSceneBuilder& builder, GPUUploadQueue* pUploadQueue );

    private:
        vector<uint32>  _listShadowLightIndex; ///< 그림자를 켠 점광과 스폿의 라이트 원소 번호(`collectSceneLights` → `ShadowSetupUtil::populateFrame`)
        vector<LODView> _listLODView;          ///< 이 시점 하나의 애니메이션 LOD · 메시 LOD 뷰
    };
} // namespace sw
