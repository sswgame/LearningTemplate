/**
 * @file EditorPreviewViewport.h
 * @brief 툴 창 안의 미리보기 씬 뷰포트 하나입니다 — 미리보기 씬 · 조명 환경 · 오빗 카메라 · 렌더 타깃을 함께 듭니다.
 * @details 창(`EditorPreviewPanel`)이 값으로 들고, 창마다 하나씩입니다. 렌더는 엔진이 씬마다 따로 합니다(`HostSceneView` → `RenderPacketBuilder::populateScenePreviews`
 *          → `FrameRenderer::executeScenePreviewPacket`) — 활성 씬과 섞이지 않습니다. 언리얼 `FPreviewScene` + `SEditorViewport`, 유니티 `PreviewRenderUtility` 자리입니다.
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
     * @brief 미리보기 씬에 스폰하는 조명 환경 프리셋입니다. 씬 안의 오브젝트로 스폰하므로 따로 된 "환경" 타입이 없습니다(`EditorMultiWorld.md` 2.1).
     * @note 스카이는 없다 — 엔진에 스카이 컴포넌트가 없어 배경은 파이프라인의 SceneColor 클리어 색이다.
     */
    struct EditorPreviewEnvironment
    {
        float3  _lightRotation{ -0.9f, 0.6f, 0.0f }; ///< 방향광의 로컬 회전(라디안, 피치 · 요 · 롤)
        float3  _lightColor{ 1.0f, 0.97f, 0.92f };
        float32 _lightIntensity{ 2.0f };
        float32 _ambient{ 0.35f };
        float32 _floorSize{ 6.0f };    ///< 바닥 평면 한 변(미터)
        float32 _floorHeight{ -0.5f }; ///< 바닥 높이 — 지름 1 의 내장 도형이 원점에 서면 바닥에 닿는다
        bool    _bDirectionalLight{ true };
        bool    _bFloor{ true };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorPreviewViewport
     * @brief 미리보기 씬 하나를 만들고 내리며, 보이는 프레임마다 자기 렌더 타깃에 그려 달라고 알리고 그 그림을 ImGui 이미지로 놓습니다.
     * @details 계약:
     *          - `initialize` 가 씬을 만들고(`SceneManager::createPreviewScene`) 조명 환경을 스폰한다. `shutdown` 이 씬과 타깃을 놓는다(창이 내려가기 전에 부른다).
     *          - `draw` 는 **보이는 프레임에만** 부른다. 부른 프레임만 호스트에 뷰가 가고, 안 부른 프레임에는 그리지 않는다(렌더러도 그 씬의 것을 내린다).
     *          - 카메라는 오빗(대상 · 거리 · 요 · 피치)이다. 창이 포커스 · 호버일 때 왼쪽 끌기 = 회전, 휠 = 거리다.
     *          - 그림은 한 프레임 늦다(호스트가 다음 엔진 틱에 그린다). 처음 열린 프레임은 클리어 색이다.
     */
    class SW_EDITOR_API EditorPreviewViewport
    {
    public:
        EditorPreviewViewport();
        ~EditorPreviewViewport();

        EditorPreviewViewport( const EditorPreviewViewport& )            = delete;
        EditorPreviewViewport& operator=( const EditorPreviewViewport& ) = delete;

        /** @brief 미리보기 씬과 렌더 타깃 자리를 만들고 환경을 스폰합니다. 이미 있으면 true 를 바로 반환합니다. 씬 매니저가 없으면 false 입니다. */
        bool initialize( string_view sceneName, const ScenePreviewPolicy& policy, const EditorPreviewEnvironment& environment );
        /** @brief 씬과 렌더 타깃을 놓습니다. 여러 번 불러도 됩니다. */
        void shutdown();
        bool isInitialized() const { return _pScene != nullptr; }

        /**
         * @brief 렌더 타깃을 @p size 에 맞추고, 오빗 입력을 받고, 이 씬 · 이 눈으로 그려 달라고 알린 뒤 지난 그림을 이미지로 놓습니다.
         * @param bInputActive 창이 포커스 · 호버여서 카메라 입력을 받아도 되는지입니다.
         */
        void draw( const float2& size, bool bInputActive );

        /** @brief 미리보기 씬입니다. 없으면 nullptr 입니다. 오브젝트는 이 씬의 매니저로 만듭니다. */
        Scene* getScene() const { return _pScene; }
        /** @brief 미리보기 씬의 키입니다(`SceneManager::getPreviewSceneKey`). 없으면 0 입니다. */
        uint32 getSceneKey() const { return _sceneKey; }
        /** @brief 이 뷰포트의 렌더 타깃 id 입니다(`EditorRenderViews::allocatePreviewTarget`). 없으면 0 입니다. */
        uint32 getTargetID() const { return _targetID; }
        /** @brief 지금 렌더 타깃 핸들입니다(스크린샷 · 테스트용). 없으면 0 입니다. */
        uint64 getRenderTarget() const;
        /** @brief 지금까지 `draw` 가 호스트에 뷰를 알린 프레임 수입니다(탐침 · 테스트용). */
        uint32 getSubmittedFrameCount() const { return _submittedFrameCount; }

        /** @brief 오빗 카메라를 정합니다. 각은 라디안입니다. */
        void setOrbit( const float3& target, float32 distance, float32 yaw, float32 pitch );
        /** @brief 반지름 @p radius 의 구가 화면에 들어오게 거리를 맞추고 대상을 @p center 로 둡니다. */
        void frameSphere( const float3& center, float32 radius );
        /** @brief 이 비율로 본 오빗 카메라의 뷰-투영 행렬과 눈 위치를 계산합니다. */
        void computeViewProjection( float32 aspectRatio, float4x4& outViewProj, float3& outEye ) const;

        /** @brief 환경 프리셋을 @p scene 에 오브젝트로 스폰합니다(방향광 · 바닥). 만든 수를 반환합니다. 창 없이 테스트가 부릅니다. */
        static uint32 spawnEnvironment( Scene& scene, const EditorPreviewEnvironment& environment );

    private:
        /** @brief `-gv_editorPreviewScreenshot` 이 있으면 충분히 그린 뒤 이 타깃을 한 번 찍습니다(시나리오 눈 확인). */
        void requestScreenshotIfAsked();

        Scene*  _pScene;
        float3  _orbitTarget;
        float32 _orbitDistance;
        float32 _orbitYaw;
        float32 _orbitPitch;
        float32 _fieldOfViewY;
        uint32  _sceneKey;
        uint32  _targetID;
        uint32  _submittedFrameCount;
    };
} // namespace sw::editor
