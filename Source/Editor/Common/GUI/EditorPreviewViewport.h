/**
 * @file EditorPreviewViewport.h
 * @brief 툴 창 안의 미리보기 씬 뷰포트 하나입니다. 미리보기 씬, 조명 환경, 오빗 카메라, 렌더 타깃을 함께 가지고 있습니다.
 * @details 창(`EditorPreviewPanel`)이 값으로 보관하고, 창마다 하나씩 있습니다. 렌더는 엔진이 씬마다 따로 하므로 활성 씬과 섞이지 않습니다
 *          (`HostSceneView` → `RenderPacketBuilder::populateScenePreviews` → `FrameRenderer::executeScenePreviewPacket`).
 *          언리얼의 `FPreviewScene` 과 `SEditorViewport`, 유니티의 `PreviewRenderUtility` 에 해당합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"

#include "Engine/Scene/SceneManager.h"

namespace sw
{
    class GameObject;
    class Scene;
} // namespace sw

namespace sw::editor
{
    /**
     * @brief 미리보기 씬에 스폰하는 조명 환경 프리셋입니다. 씬 안의 오브젝트로 스폰하므로 따로 된 "환경" 타입이 없습니다.
     * @note 하늘은 `SkyComponent` 하나다(색은 컴포넌트의 기본값). `_bSky` 를 끄면 배경은 파이프라인의 SceneColor 클리어 색이다.
     */
    struct EditorPreviewEnvironment
    {
        float3  _lightRotation{ -0.9f, 0.6f, 0.0f }; ///< 방향광의 로컬 회전(라디안, 피치와 요와 롤)
        float3  _lightColor{ 1.0f, 0.97f, 0.92f };
        float32 _lightIntensity{ 2.0f };
        float32 _ambient{ 0.35f };
        float32 _floorSize{ 6.0f };    ///< 바닥 평면 한 변(미터)
        float32 _floorHeight{ -0.5f }; ///< 바닥 높이. 지름 1 의 내장 도형을 원점에 두면 바닥에 닿는다
        bool    _bDirectionalLight{ true };
        bool    _bFloor{ true };
        bool    _bSky{ true }; ///< 하늘(`SkyComponent`) — 빈 곳을 그러데이션으로 칠한다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorPreviewViewport
     * @brief 미리보기 씬 하나를 만들고 정리하며, 보이는 프레임마다 자기 렌더 타깃에 그려 달라고 알리고 그 결과를 ImGui 이미지로 놓습니다.
     * @details 계약:
     *          - `initialize` 가 씬을 만들고(`SceneManager::createPreviewScene`) 조명 환경을 스폰한다. `shutdown` 이 씬과 타깃을 놓으며, 창이 닫히기 전에 부른다.
     *          - `draw` 는 **보이는 프레임에만** 부른다. 부른 프레임만 호스트에 뷰가 전달되고, 부르지 않은 프레임에는 그리지 않는다. 렌더러도 그 씬의 리소스를 정리한다.
     *          - 카메라는 대상, 거리, 요, 피치로 정하는 오빗 카메라다. 창이 포커스이거나 마우스가 올라가 있으면 왼쪽 끌기로 회전하고 휠로 거리를 바꾼다.
     *          - `setFreeCamera` 로 자유 카메라로 바꿀 수 있다 — 눈 위치와 요 · 피치로 정하고, 왼쪽 끌기는 그 자리에서 둘러보고 휠은 시선 방향으로 움직이며 가운데 끌기는 평행 이동한다.
     *            카메라 프리셋(Fixed · Follow · Dolly) 창처럼 대상 둘레를 돌지 않는 시점을 보일 때 쓴다(`setOrbit`/`frameSphere` 가 오빗으로 되돌린다).
     *          - 결과 이미지는 한 프레임 늦다. 호스트가 다음 엔진 틱에 그리기 때문이다. 처음 열린 프레임은 클리어 색이다.
     */
    class SW_EDITOR_API EditorPreviewViewport
    {
    public:
        EditorPreviewViewport();
        ~EditorPreviewViewport();

        EditorPreviewViewport( const EditorPreviewViewport& )            = delete;
        EditorPreviewViewport& operator=( const EditorPreviewViewport& ) = delete;

        /** @brief 미리보기 씬과 렌더 타깃 슬롯을 만들고 환경을 스폰합니다. 이미 있으면 true 를 바로 반환합니다. 씬 매니저가 없으면 false 입니다. */
        [[nodiscard]] bool initialize( string_view sceneName, const ScenePreviewPolicy& policy, const EditorPreviewEnvironment& environment );
        /**
         * @brief 다른 쪽이 가진 미리보기 씬 @p scene 을 이 뷰포트로 그립니다(씬은 만들지도 내리지도 않는다 — 렌더 타깃만 잡는다). 이미 씬이 있으면 false 입니다.
         * @details 한 프로세스 재생(`EditorNetSimSession`)이 하니스 월드의 씬(`NetSimPreviewSceneSource` 가 만든 것)을 창마다 붙인다.
         *          씬을 가진 쪽이 씬을 내리기 전에 이 뷰포트의 `shutdown` 을 부른다.
         */
        [[nodiscard]] bool attachScene( Scene& scene, uint32 sceneKey );
        /** @brief 렌더 타깃을 놓고, 이 뷰포트가 만든 씬이면 씬도 내립니다(붙인 씬은 그대로 둔다). 여러 번 불러도 됩니다. */
        void shutdown();
        bool isInitialized() const { return _pScene != nullptr; }

        /**
         * @brief 렌더 타깃을 @p size 에 맞추고, 오빗 입력을 받고, 이 씬과 이 카메라로 그려 달라고 알린 뒤 지난 프레임의 결과를 이미지로 놓습니다.
         * @param bInputActive 창이 포커스이거나 마우스가 올라가 있어 카메라 입력을 받아도 되는지입니다. 마우스가 기즈모 위에 있거나 기즈모를 끄는 중이면 부르는 쪽이 false 로 넘깁니다.
         */
        void draw( const float2& size, bool bInputActive );

        /** @brief 미리보기 씬입니다. 없으면 nullptr 입니다. 오브젝트는 이 씬의 매니저로 만듭니다. */
        Scene* getScene() const { return _pScene; }
        /** @brief 미리보기 씬의 키입니다(`SceneManager::getPreviewSceneKey`). 없으면 0 입니다. */
        uint32 getSceneKey() const { return _sceneKey; }
        /** @brief 이 뷰포트의 렌더 타깃 id 입니다(`EditorRenderViews::allocatePreviewTarget`). 없으면 0 입니다. */
        uint32 getTargetID() const { return _targetID; }
        /** @brief 지금 렌더 타깃 핸들입니다. 스크린샷과 테스트에 씁니다. 없으면 0 입니다. */
        uint64 getRenderTarget() const;
        /** @brief 지금까지 `draw` 가 호스트에 뷰를 알린 프레임 수입니다. 감시 값과 테스트에 씁니다. */
        uint32 getSubmittedFrameCount() const { return _submittedFrameCount; }
        /**
         * @brief 이 뷰포트의 렌더 타깃을 @p path(디스크 경로)에 이미지로 씁니다 — 확장자가 `.png` 면 PNG, 아니면 PPM 입니다(`RenderThread::requestScreenshot`). 파일은 몇 프레임 뒤에 생깁니다.
         * @details 한 번이라도 그린 뷰포트만 쓸 수 있다. 캐릭터 편집 창의 프리셋 썸네일이 쓴다.
         * @return 렌더 타깃이 없거나 아직 그리지 않았거나 경로가 비어 있으면 false 이고 아무것도 요청하지 않습니다.
         */
        [[nodiscard]] bool requestImageFile( string_view path );

        /** @brief 오빗 카메라를 정합니다. 각은 라디안입니다. */
        void setOrbit( const float3& target, float32 distance, float32 yaw, float32 pitch );
        /**
         * @brief 자유 카메라로 바꿉니다. @p eye 는 눈 위치, 각은 라디안이며 요 · 피치의 방향 규약은 오빗과 같다(요 0 · 피치 0 이면 -Z 를 본다).
         * @param fieldOfViewY 세로 시야각(라디안). 0 이하이면 지금 값을 둔다.
         */
        void setFreeCamera( const float3& eye, float32 yaw, float32 pitch, float32 fieldOfViewY );
        /** @brief 자유 카메라를 끄고 오빗 카메라로 돌아갑니다(대상 · 거리는 마지막 오빗 값이다). */
        void clearFreeCamera() { _bFreeCamera = false; }
        bool isFreeCamera() const { return _bFreeCamera; }
        /** @brief 반지름 @p radius 의 구가 화면에 들어오게 거리를 맞추고 대상을 @p center 로 둡니다. */
        void frameSphere( const float3& center, float32 radius );
        /** @brief 이 비율로 본 오빗 카메라의 뷰-투영 행렬과 눈 위치를 계산합니다. */
        void computeViewProjection( float32 aspectRatio, float4x4& outViewProj, float3& outEye ) const;
        /** @brief 이 비율로 본 오빗 카메라의 뷰 행렬과 투영 행렬을 따로 계산합니다. 기즈모가 씁니다. */
        void computeViewAndProjection( float32 aspectRatio, float4x4& outView, float4x4& outProjection, float3& outEye ) const;
        /** @brief 지난 `draw` 가 이미지를 놓은 화면 위치와 크기입니다. 그리지 않았으면 크기가 0 입니다. */
        const float2& getImagePosition() const { return _imagePosition; }
        const float2& getImageSize() const { return _imageSize; }
        /** @brief 오빗 카메라가 대상에서 떨어진 거리입니다. 휠 줌이 바꾸며 프로브 `Editor.PreviewViewportOrbitDistance` 가 읽습니다. */
        float32 getOrbitDistance() const { return _orbitDistance; }

        /** @brief 환경 프리셋(방향광, 바닥)을 @p scene 에 오브젝트로 스폰하고 만든 수를 반환합니다. 테스트가 창 없이 부릅니다. */
        static uint32 spawnEnvironment( Scene& scene, const EditorPreviewEnvironment& environment );

    private:
        /** @brief `-gv_editorPreviewScreenshot` 이 있으면 충분히 그린 뒤 이 타깃을 한 번 찍습니다(시나리오 눈 확인). */
        void requestScreenshotIfAsked();

        Scene*  _pScene;
        float2  _imagePosition;
        float2  _imageSize;
        float3  _orbitTarget;
        float3  _freeEye; ///< 자유 카메라의 눈 위치(`_bFreeCamera` 일 때만 쓴다). 각은 오빗의 요 · 피치를 같이 쓴다
        float32 _orbitDistance;
        float32 _orbitYaw;
        float32 _orbitPitch;
        float32 _fieldOfViewY;
        uint32  _sceneKey;
        uint32  _targetID;
        uint32  _submittedFrameCount;
        bool    _bFreeCamera; ///< 자유 카메라 모드다(`setFreeCamera`)
        bool    _bOwnsScene;  ///< `initialize` 가 만든 씬이다(`attachScene` 으로 붙인 씬은 거짓 — 내리지 않는다)
    };
} // namespace sw::editor
