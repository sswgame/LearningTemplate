/**
 * @file EditorRenderViews.h
 * @brief 에디터가 그리는 뷰(씬 · 게임)의 렌더 타깃과, 그것을 만들고 ImGui 에 올리는 RHI 디바이스 · 렌더러 백엔드, 게임 뷰의 입력 상태를 드는 에디터 서브시스템입니다.
 * @details 셸(`ImGuiEditor`)이 컨텍스트를 세운 뒤 디바이스와 백엔드를 알리고(`setRHIDevice` · `setRendererBackend`), 패널이 뷰 크기에 맞춰 렌더 타깃을 만들고
 *          그린 것을 표시하며, 호스트는 프레임마다 핸들만 읽는다(`ImGuiEditor::getSceneViewport` · `getGameViewport`). 종료(`shutdownSubsystem`)에서 렌더 타깃을 놓는다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/EditorViewTargetUtil.h"

namespace sw
{
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
        uint8  _bDrawn{ SW_FALSE }; ///< 이번 UI 프레임에 패널이 이 뷰를 그렸다(보인다). 프레임을 시작할 때 지운다
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
        /** @brief 뷰 렌더 타깃을 모두 해제합니다. */
        void destroyViewTargets();
        /** @brief 패널이 이번 프레임에 그 뷰를 그렸다고 알립니다. 셸은 그린 뷰만 호스트에 요청합니다. */
        void markViewDrawn( EditorViewKind kind ) { _arrViewTarget[static_cast<uint32>( kind )]._bDrawn = SW_TRUE; }
        /** @brief 이번 프레임에 그 뷰를 그렸으면 true 입니다. */
        bool wasViewDrawn( EditorViewKind kind ) const { return getViewTarget( kind )._bDrawn == SW_TRUE; }
        /** @brief UI 프레임을 시작할 때 그린 뷰 표시를 지웁니다. */
        void clearViewDrawnMarks();

    private:
        IRHIDevice*            _pRHIDevice{ nullptr };       ///< 렌더 타깃을 만드는 디바이스(셸이 알린다)
        IImGuiRendererBackend* _pRendererBackend{ nullptr }; ///< 렌더 타깃을 ImGui 텍스처로 올리는 백엔드(셸이 알린다)
        EditorViewTarget       _arrViewTarget[static_cast<uint32>( EditorViewKind::Count )]{};
        uint8                  _bGameViewHovered{ SW_FALSE }; ///< 게임 뷰 창 위에 마우스가 있다
        uint8                  _bGameViewFocused{ SW_FALSE }; ///< 게임 뷰 창이 포커스를 가졌다
    };
} // namespace sw::editor
