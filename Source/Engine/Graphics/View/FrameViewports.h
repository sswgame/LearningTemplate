/**
 * @file Engine/Graphics/View/FrameViewports.h
 * @brief 호스트가 엔진 루프에 넘기는 뷰 요청 값(`Engine::update`)입니다 — 값 타입이고 렌더 스레드 · 렌더러 형식이 없어 전용 서버 호스트도 읽습니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/RHI/RHIHandles.h"
#include "Engine/Graphics/View/RenderViewSettings.h"
#include "Engine/Presentation/ViewCameraProvider.h"

namespace sw
{
    /**
     * @brief 뷰포트 하나의 출력입니다 — 호스트(에디터)가 만든 렌더 타깃과 그것이 창에 보인 자리입니다(언리얼 `FViewport` 자리). 렌더 타깃이 0이면 이번 프레임에 그 뷰를 그리지 않습니다.
     * @details `_windowX` · `_windowY` 는 이 출력의 왼쪽 위가 주 창 클라이언트의 어느 픽셀에 보였는지다(에디터 패널의 이미지 자리). 런타임 UI 는 창 픽셀 포인터를
     *          이만큼 빼서 출력 픽셀로 옮긴다(`UIViewport::_pointerOrigin`). 이미지는 렌더 타깃 크기 그대로 보이므로(늘이지 않는다) 배율은 없다.
     */
    struct ViewportTarget
    {
        RHITextureHandle _renderTarget{ 0 };
        uint32           _width{ 0 };
        uint32           _height{ 0 };
        int32            _windowX{ 0 };
        int32            _windowY{ 0 };

        /** @brief 그릴 수 있는 출력인지 나타냅니다. */
        bool isValid() const { return _renderTarget != 0 && _width > 0 && _height > 0; }
    };

    /// @brief 호스트가 한 프레임에 요청할 수 있는 미리보기 씬 뷰의 최대 수입니다(`FrameViewports::_arrPreviewView`).
    inline constexpr uint32 kMaxPreviewSceneView = 8;

    /**
     * @struct PreviewSceneView
     * @brief 호스트(에디터 툴 창)가 이번 프레임에 그려 달라고 요청한 **미리보기 씬** 뷰 하나입니다(언리얼 `FPreviewScene` 의 뷰). 값 타입이며 C ABI 를 넘어갑니다(`EditorAPI::getPreviewSceneViews`).
     * @details 어느 씬(`_sceneKey` — `SceneManager::getPreviewSceneKey`), 어느 눈(`_viewProj` · `_cameraPos`), 어느 렌더 타깃(`_target`)인지를 고른다.
     *          씬마다 GT 의 스냅샷 빌더 하나 · RT 의 렌더러 하나가 따로 돌므로 메인 씬과 섞이지 않는다(`RenderPacketBuilder::populateScenePreviews`).
     *          안 보이는 창은 요청하지 않는다 — 요청이 없는 씬은 그리지 않는다.
     */
    struct PreviewSceneView
    {
        float4x4           _viewProj{};
        float3             _cameraPos{};
        RenderViewSettings _settings{};
        ViewportTarget     _target{};
        uint32             _sceneKey{ 0 };
    };

    /**
     * @struct FrameViewports
     * @brief 이번 프레임에 호스트가 그려 달라고 요청한 뷰포트들입니다(`Engine::update`) — 활성 씬의 게임 · 씬 뷰포트와 미리보기 씬 목록입니다.
     * @details 게임 뷰는 게임 카메라, 화면 UI, 화면 사각형 뷰를 담는 주 출력이고, 씬 뷰는 호스트 카메라의 추가 뷰(`RenderViewOutputKind::ViewportTarget`)다.
     *          게임 뷰가 없고 씬 뷰만 있으면 씬 뷰가 주 출력이 된다(화면 UI와 화면 사각형 뷰 없이). 둘 다 없으면 백버퍼에 게임 카메라로 그린다(에디터 없는 실행).
     *          미리보기 씬 뷰(`_arrPreviewView`)는 활성 씬이 아닌 씬을 자기 타깃에 그린다. 고정 배열인 까닭은 호스트가 프레임마다 이 값을 새로 채우기 때문이다(힙을 건드리지 않는다).
     */
    struct FrameViewports
    {
        ViewportTarget   _game;
        ViewportTarget   _scene;
        PreviewSceneView _arrPreviewView[kMaxPreviewSceneView]{};
        uint32           _previewViewCount{ 0 };

        /** @brief 씬 뷰가 주 출력인지 나타냅니다(게임 뷰가 없는 경우입니다). */
        bool isSceneViewMain() const { return _game.isValid() == false && _scene.isValid(); }
        /** @brief 이번 프레임의 주 출력입니다. 둘 다 없으면 빈 값이며 백버퍼를 뜻합니다. */
        const ViewportTarget& getMainOutput() const { return isSceneViewMain() ? _scene : _game; }
    };
} // namespace sw
