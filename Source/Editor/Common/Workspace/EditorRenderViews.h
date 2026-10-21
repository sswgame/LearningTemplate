/**
 * @file EditorRenderViews.h
 * @brief 에디터가 그리는 뷰(씬 · 게임 · 툴 창의 미리보기 씬)의 렌더 타깃과, 그것을 만들고 ImGui 에 올리는 RHI 디바이스 · 렌더러 백엔드, 게임 뷰의 입력 상태를 드는 에디터 서브시스템입니다.
 * @details 셸(`ImGuiEditor`)이 컨텍스트를 세운 뒤 디바이스와 백엔드를 알리고(`setRHIDevice` · `setRendererBackend`), 패널이 뷰 크기에 맞춰 렌더 타깃을 만들고
 *          그린 것을 표시하며, 호스트는 프레임마다 핸들만 읽는다(`ImGuiEditor::getSceneViewport` · `getGameViewport` · `getSceneViews`). 종료(`shutdownSubsystem`)에서 렌더 타깃을 놓는다.
 *          씬 뷰 · 게임 뷰는 활성 씬의 고정 두 슬롯이고, 툴 창의 미리보기 타깃은 id 로 찾는 가변 목록이다(`allocatePreviewTarget`).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/EditorViewTargetUtil.h"

namespace sw
{
    struct HostSceneView;

    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    class IImGuiRendererBackend;

    /** @brief 에디터가 소유하는 뷰 렌더 타깃 하나입니다. 씬 뷰와 게임 뷰가 각각 하나씩 가집니다. 호스트는 프레임마다 핸들만 조회합니다(`ImGuiEditor::getSceneViewport`, `getGameViewport`). */
    struct EditorViewTarget
    {
        uint64 _renderTarget{ 0 };
        void*  _pTextureID{ nullptr };
        uint32 _width{ 0 };
        uint32 _height{ 0 };
        int32  _windowX{ 0 };       ///< 이번 프레임에 이미지 왼쪽 위가 놓인 주 창 클라이언트 픽셀(`markViewDrawn`). 호스트가 포인터를 뷰 픽셀로 옮긴다
        int32  _windowY{ 0 };       ///< 위와 같습니다(세로)
        uint8  _bDrawn{ SW_FALSE }; ///< 이번 UI 프레임에 패널이 이 뷰를 그렸다(보인다). 프레임을 시작할 때 지운다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 툴 창(미리보기 씬)이 가지는 뷰 렌더 타깃 하나와 이번 프레임의 눈입니다. id 로 찾습니다(`EditorRenderViews::allocatePreviewTarget`).
     * @details 호스트에는 `HostViewTargets::_arrSceneView` 로 간다 — 보인 프레임(`submitPreviewView`)에만 간다.
     */
    struct EditorPreviewViewTarget
    {
        EditorViewTarget _target{};
        float4x4         _viewProj{};
        float3           _cameraPos{};
        uint32           _id{ 0 };
        uint32           _sceneKey{ 0 }; ///< 그릴 미리보기 씬(`SceneManager::getPreviewSceneKey`)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorRenderViews
     * @brief 뷰 렌더 타깃 · RHI 디바이스 · ImGui 렌더러 백엔드 · 게임 뷰 입력 상태입니다(`EditorContext::getSubsystem<EditorRenderViews>()`).
     */
    class SW_EDITOR_API EditorRenderViews final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorRenderViews";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        /** @brief 남은 뷰 렌더 타깃을 놓고 디바이스 · 백엔드를 잊습니다. */
        void shutdownSubsystem() override;

        void        setRHIDevice( IRHIDevice* pDevice ) { _pRHIDevice = pDevice; }
        IRHIDevice* getRHIDevice() const { return _pRHIDevice; }
        void        setRendererBackend( IImGuiRendererBackend* pBackend ) { _pRendererBackend = pBackend; }
        /** @brief ImGui 렌더러 백엔드를 반환합니다. 텍스처를 ImGui 에 등록하려는 패널이 씁니다. 없으면 nullptr 입니다. */
        IImGuiRendererBackend* getRendererBackend() const { return _pRendererBackend; }

        void setGameViewHovered( bool bHovered ) { _bGameViewHovered = bHovered ? SW_TRUE : SW_FALSE; }
        void setGameViewFocused( bool bFocused ) { _bGameViewFocused = bFocused ? SW_TRUE : SW_FALSE; }
        bool isGameViewHovered() const { return _bGameViewHovered == SW_TRUE; }
        bool isGameViewFocused() const { return _bGameViewFocused == SW_TRUE; }

        /** @brief 뷰 RT 하나입니다. */
        const EditorViewTarget& getViewTarget( EditorViewKind kind ) const { return _arrViewTarget[static_cast<uint32>( kind )]; }
        /** @brief 뷰 렌더 타깃을 그 크기로 (다시) 만듭니다. 같은 크기이면 아무 일도 하지 않습니다. */
        void ensureViewTargetSize( EditorViewKind kind, uint32 width, uint32 height );
        /** @brief 뷰 렌더 타깃 하나를 해제합니다. 그 렌더 타깃을 그렸을 프레임의 GPU 작업이 끝난 뒤에 파괴됩니다. */
        void destroyViewTarget( EditorViewKind kind );
        /** @brief 뷰 렌더 타깃을 모두 해제합니다(미리보기 타깃 포함). */
        void destroyViewTargets();
        /**
         * @brief 패널이 이번 프레임에 그 뷰를 그렸다고 알립니다. 셸은 그린 뷰만 호스트에 요청합니다.
         * @param imageScreenPos 이미지 왼쪽 위의 ImGui 화면 좌표입니다. 주 뷰포트 기준(주 창 클라이언트 픽셀)으로 바꿔 둡니다 — 그린 프레임마다 자리가 함께 오므로 낡지 않습니다.
         */
        void markViewDrawn( EditorViewKind kind, const float2& imageScreenPos );
        /** @brief 이번 프레임에 그 뷰를 그렸으면 true 입니다. */
        bool wasViewDrawn( EditorViewKind kind ) const { return getViewTarget( kind )._bDrawn == SW_TRUE; }
        /** @brief UI 프레임을 시작할 때 그린 뷰 표시를 지웁니다(미리보기 타깃 포함). */
        void clearViewDrawnMarks();

        /** @brief 툴 창 하나의 미리보기 타깃 자리를 만들고 id 를 돌려줍니다(1 부터, 다시 쓰지 않음). 렌더 타깃은 `ensurePreviewTargetSize` 가 만듭니다. */
        uint32 allocatePreviewTarget();
        /** @brief 미리보기 타깃을 놓습니다(GPU 작업이 끝난 뒤 파괴). 모르는 id 면 아무것도 하지 않습니다. */
        void freePreviewTarget( uint32 id );
        /** @brief 미리보기 타깃의 렌더 타깃을 그 크기로 (다시) 만듭니다. 같은 크기이면 아무 일도 하지 않습니다. */
        void ensurePreviewTargetSize( uint32 id, uint32 width, uint32 height );
        /** @brief 미리보기 타깃을 찾습니다. 없으면 nullptr 입니다. */
        const EditorPreviewViewTarget* findPreviewTarget( uint32 id ) const;
        /** @brief 툴 창이 이번 프레임에 그 타깃을 보였고, 이 씬 · 이 눈으로 그려 달라고 알립니다. 보인 프레임마다 부릅니다. */
        void submitPreviewView( uint32 id, uint32 sceneKey, const float4x4& viewProj, const float3& cameraPos );
        /** @brief 이번 프레임에 보인 미리보기 뷰를 호스트 형식으로 씁니다. 쓴 개수를 반환합니다(`ImGuiEditor::getSceneViews`). */
        uint32 collectSceneViews( HostSceneView* pOutView, uint32 capacity ) const;
        /** @brief 지금 가진 미리보기 타깃 수입니다(테스트용). */
        uint32 getPreviewTargetCount() const { return static_cast<uint32>( _listPreviewTarget.size() ); }

    private:
        /** @brief 뷰 렌더 타깃 하나를 놓습니다. */
        void destroyTarget( EditorViewTarget& view );
        /** @brief 뷰 렌더 타깃 하나를 그 크기로 (다시) 만듭니다. */
        void ensureTargetSize( EditorViewTarget& view, uint32 width, uint32 height );
        /** @brief 미리보기 타깃을 찾습니다(고칠 수 있게). 없으면 nullptr 입니다. */
        EditorPreviewViewTarget* findPreviewTargetMutable( uint32 id );

    private:
        IRHIDevice*                     _pRHIDevice{ nullptr };       ///< 렌더 타깃을 만드는 디바이스(셸이 알린다)
        IImGuiRendererBackend*          _pRendererBackend{ nullptr }; ///< 렌더 타깃을 ImGui 텍스처로 올리는 백엔드(셸이 알린다)
        EditorViewTarget                _arrViewTarget[static_cast<uint32>( EditorViewKind::Count )]{};
        vector<EditorPreviewViewTarget> _listPreviewTarget{}; ///< 툴 창의 미리보기 타깃(`allocatePreviewTarget`)
        uint32                          _nextPreviewTargetID{ 1 };
        uint8                           _bGameViewHovered{ SW_FALSE }; ///< 게임 뷰 창 위에 마우스가 있다
        uint8                           _bGameViewFocused{ SW_FALSE }; ///< 게임 뷰 창이 포커스를 가졌다
    };
} // namespace sw::editor
