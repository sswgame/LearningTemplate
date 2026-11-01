/**
 * @file SceneCapture.h
 * @brief 씬 하나를 화면 밖 타깃에 몇 장 그리고 GPU 에서 읽어 오는 요청 실행기입니다(초상화 · 반사 프로브 굽기가 함께 쓴다). 렌더러가 아니다.
 * @details 렌더러 종류를 늘리지 않는다 — 그리기는 주 렌더러의 하위 렌더러가 한다(`FrameRenderer::executeOffscreenPacket`, 미리보기 씬과 같은 길). 언리얼의
 *          씬 캡처가 자기 뷰 · 렌더 타깃으로 같은 장면 렌더러를 타는 것과 같다.
 *          스레드는 언리얼 렌더 명령과 같이 나눈다. 씬 → 패킷(`RenderPacketSceneFill`)은 게임 스레드가 짓고, 그리기 · 읽기는 렌더 스레드가 명령으로 실행한다
 *          (`RenderThread::executeAndWait`). 렌더 스레드에서 도는 일은 `...OnRenderThread` 멤버 함수뿐이고, 스레드를 넘는 값은 일감(`_job`) 하나에 모은다 —
 *          게임 스레드는 명령이 끝날 때까지 기다리므로 일감을 두 스레드가 동시에 만지지 않는다.
 *          예전에는 캡처마다 게임 스레드가 렌더 스레드를 세우고 컨텍스트를 빼앗아 자기 렌더러로 직접 그렸고(`FrameRenderer::execute( scene )`), 출력은 주 렌더러와
 *          함께 쓰는 창 백버퍼였다(백버퍼를 거치는 순서가 다른 DX11 에서 배경만 읽었다). 출력은 언제나 이 객체의 렌더 타깃이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Renderer/Frame/Packet/RenderPacketSceneFill.h"

namespace sw
{
    class IRHIDevice;
    class RenderThread;
    class Scene;
    class Texture2D;

    /** @brief 읽어 온 이미지 한 장입니다. 행 사이에 빈틈이 있을 수 있다(`_layout._rowBytes`). */
    struct SceneCaptureResult
    {
        vector<uint8>     _bytes;
        RHITextureMipSpan _layout{};
        RHIFormat         _format{ RHIFormat::R8G8B8A8_UNORM };
    };

    /** @brief 캡처 한 번의 요청입니다. */
    struct SceneCaptureRequest
    {
        RenderSceneViewDesc _view{};                               ///< 시점과 크기(출력 타깃은 이 객체가 정한다)
        float4              _clearColor{ 0.0f, 0.0f, 0.0f, 1.0f }; ///< 출력 타깃을 여는 색
        uint32              _frameCount{ 3 };                      ///< 그릴 장 수 — 첫 장은 GPUScene 업로드 · 텍스처 도착이 아직이라 몇 장 그린 뒤 읽는다
        string              _attachmentName{};                     ///< 비면 출력(Present 결과, RGBA8)을, 아니면 그 이름의 첨부(예: 디퍼드의 `LitColor`)를 읽는다
    };

    /**
     * @class SceneCapture
     * @brief 캡처 요청 실행기입니다. 게임 스레드에서 부릅니다.
     */
    class SW_API SceneCapture
    {
    public:
        SceneCapture();
        ~SceneCapture();

        SceneCapture( const SceneCapture& )            = delete;
        SceneCapture& operator=( const SceneCapture& ) = delete;

        /**
         * @brief @p renderThread 의 주 렌더러로 @p pipelinePath 파이프라인을 쓰도록 정합니다. 렌더 스레드에 디바이스 · 렌더러가 없으면 false 입니다.
         * @details 하위 렌더러는 첫 `capture` 에서 렌더 스레드가 만들고, 다음 주 프레임에 주 렌더러가 내린다(`FrameRenderer::retireScenePreviews`).
         */
        [[nodiscard]] bool initialize( RenderThread& renderThread, string_view pipelinePath );
        /** @brief 출력 타깃을 렌더 스레드에서 놓습니다. */
        void shutdown();
        /** @brief 렌더 스레드에 붙은 디바이스입니다(초기화 전이면 nullptr). 게임 스레드는 머티리얼 초기화(텍스처 요청)에만 넘긴다. */
        IRHIDevice* getDevice() const;

        /**
         * @brief @p scene 을 @p request 대로 몇 장 그리고 마지막 장을 읽어 @p outResult 에 담습니다. 그리거나 읽지 못하면 false 입니다(이유는 로그에 남는다).
         * @details 씬의 월드 행렬은 부르는 쪽이 반영해 둔다(`GameObjectManager::flushSceneTransforms`) — 시점이 그 행렬로 정해져 있다.
         */
        [[nodiscard]] bool capture( Scene& scene, const SceneCaptureRequest& request, SceneCaptureResult& outResult );

    private:
        /** @brief 렌더 스레드로 넘기는 일감입니다. 게임 스레드가 채우고, 끝날 때까지 기다리는 명령 안에서 렌더 스레드가 읽고 결과를 적는다. */
        struct RenderJob
        {
            RenderFramePacket   _packet;
            SceneCaptureRequest _request;
            SceneCaptureResult* _pResult{ nullptr }; ///< 마지막 장이면 읽어 담을 곳, 아니면 nullptr
            bool                _bSucceeded{ false };
        };

        /** @brief (렌더 스레드) 일감의 패킷을 출력 타깃에 한 장 그리고, 읽을 곳이 있으면 읽습니다. */
        void renderFrameOnRenderThread( IRHIDevice& device );
        /** @brief (렌더 스레드) 출력 타깃을 놓습니다. */
        void releaseOnRenderThread( IRHIDevice& device );
        /** @brief (렌더 스레드) 요청 크기의 출력 타깃(RGBA8)을 갖춥니다. 크기가 다르면 다시 만든다. */
        [[nodiscard]] bool ensureOutputTarget( IRHIDevice& device, uint32 width, uint32 height );

        RenderThread*         _pRenderThread;
        string                _pipelinePath;
        unique_ptr<Texture2D> _outputTarget; ///< 렌더 스레드 객체 — 출력(Present 결과)이 그려지는 곳
        RenderPacketSceneFill _fill;         ///< 씬 → 패킷(게임 스레드)
        RenderJob             _job;
        uint32                _sceneKey; ///< 하위 렌더러를 고르는 씬 키(미리보기 씬 키와 겹치지 않는 공간)
    };
} // namespace sw
