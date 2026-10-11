/**
 * @file IEditorPanel.h
 * @brief 도킹할 수 있는 에디터 패널의 인터페이스입니다. 셸 크롬은 draw() 가, 내용은 drawContent() 가 그립니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/GUI/EditorChrome.h"

namespace sw
{
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    /**
     * @class IEditorPanel
     * @brief 도킹 가능한 ImGui 패널입니다. 파생 클래스는 drawContent() 만 구현합니다.
     */
    class SW_EDITOR_API IEditorPanel
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명 주기. GPU 리소스를 쓰는 파생 클래스는 shutdown 을 재정의한다
        // ------------------------------------------------------------------------------
        /** @brief 파생 패널이 리소스를 해제할 수 있도록 소멸을 가상으로 둡니다. */
        virtual ~IEditorPanel()                        = default;
        IEditorPanel( const IEditorPanel& )            = delete;
        IEditorPanel& operator=( const IEditorPanel& ) = delete;

        // ------------------------------------------------------------------------------
        // 2) IEditorPanel: 제목, 크롬, 내용
        //    preRender 와 shutdown 은 GPU 리소스가 있는 패널만 재정의한다
        //    isToolPanel 이면 닫힌 채로 시작한다(필요할 때 여는 도구)
        // ------------------------------------------------------------------------------
        /** @brief 패널 제목을 반환합니다. */
        virtual const utf8* getPanelTitle() const = 0;
        /** @brief 패널 크롬을 열고 drawContent() 를 호출합니다. */
        void draw();
        /** @brief 렌더링 전에 필요한 RHI 리소스를 갱신합니다. */
        virtual void preRender( IRHIDevice* /*rhiDevice*/ ) {}
        /** @brief 패널을 종료할 때 리소스를 정리합니다. */
        virtual void shutdown( IRHIDevice* /*rhiDevice*/ ) {}
        /** @brief 도구 패널이면 true 입니다. 도구 패널은 닫힌 채, 핵심 패널은 열린 채 시작합니다. */
        virtual bool isToolPanel() const { return false; }
        /** @brief 마지막 draw 에서 이 패널 창에 포커스가 있었으면 true 입니다. */
        bool isWindowFocused() const { return _bWindowFocused; }

        // ------------------------------------------------------------------------------
        // 3) 문서 상태. dirty 비트는 기반 클래스가 보관한다
        //    파생 클래스는 markDocumentDirty() 로 알리고, saveDocument() 와 revertDocument() 만 구현한다.
        // ------------------------------------------------------------------------------
        /**
         * @brief 저장되지 않은 편집이 있으면 true 입니다.
         * @details 가상 함수가 아니다. 비트를 기반 클래스가 보관하므로, 패널이 "* Unsaved changes" 를 화면에 띄우면서 Ctrl+S 와
         *          종료 확인에는 나타나지 않는 반쪽짜리 구현이 불가능하다.
         */
        bool isDocumentDirty() const { return _bDocumentDirty; }
        /**
         * @brief `saveDocument()` 를 호출하고 성공했을 때만 dirty 를 지웁니다.
         * @details 모든 저장 경로가 이 함수를 거친다. 바로 아래 `discardDirtyDocument` 와 마찬가지로 순서를 기반 클래스가 관리한다.
         *          파생 클래스가 각자 구현하면 실패해도 dirty 를 지워 편집이 조용히 사라지거나, 성공해도 계속 미저장으로 남는다.
         *          파생 클래스는 파일을 쓰고 성공 여부를 반환하기만 하면 된다.
         * @return 저장에 성공했으면 true 입니다. false 이면 dirty 는 그대로 남습니다.
         */
        [[nodiscard]] bool saveDocumentAndClearDirty()
        {
            if ( canSaveDocument() == false )
                return false;
            if ( saveDocument() == false )
                return false;
            clearDocumentDirty();
            return true;
        }
        /** @brief dirty 이면 저장합니다. 이미 깨끗하거나 저장에 실패하면 false 입니다. */
        [[nodiscard]] bool trySaveDirtyDocument()
        {
            if ( _bDocumentDirty == false )
                return false;
            return saveDocumentAndClearDirty();
        }
        /** @brief dirty 이면 revertDocument() 를 호출하고 dirty 를 지웁니다. */
        void discardDirtyDocument()
        {
            if ( _bDocumentDirty == false )
                return;
            revertDocument();
            clearDocumentDirty();
        }

        // ------------------------------------------------------------------------------
        // 4) 열림 상태. ImGui Begin 의 p_open
        // ------------------------------------------------------------------------------
        /** @brief 패널이 열려 있으면 true 입니다. */
        bool isOpen() const { return _bOpen; }
        /** @brief 패널의 열림 상태를 설정합니다. */
        void setOpen( bool open ) { _bOpen = open; }
        /** @brief ImGui 에 넘길 열림 상태 포인터를 반환합니다. */
        bool* getOpenPtr() { return &_bOpen; }

    protected:
        /** @brief 기본 열림 상태로 에디터 패널을 만듭니다. */
        explicit IEditorPanel( bool bOpenByDefault = true )
            : _bOpen{ bOpenByDefault }
            , _bWindowFocused{ false }
            , _bDocumentDirty{ false }
        {
        }

        /** @brief 패널 본문을 그립니다. Begin 과 End 는 draw() 가 처리합니다. */
        virtual void drawContent() = 0;
        /** @brief 패널이 접히거나 탭이 숨겨졌을 때 호출합니다. */
        virtual void onPanelCollapsed() {}
        /**
         * @brief EditorPanelFlags 조합을 반환합니다. 기본은 None 입니다.
         * @details `UnsavedDocument` 는 여기에 넣지 않는다. draw() 가 dirty 상태를 보고 스스로 추가한다. 그래야 이 함수를
         *          다른 플래그 때문에 재정의한 패널도 제목의 미저장 표시를 잃지 않는다.
         */
        virtual EditorPanelFlags getPanelFlags() const { return EditorPanelFlags::None; }
        /**
         * @brief 떠 있는 창으로 처음 열 때의 크기입니다. 96 DPI 기준이며 UI 배율은 `EditorChrome::setNextPanelSize` 가 곱합니다.
         * @details 기본값이 있어야 한다. 크기를 정하지 않은 도구 창은 내용 크기로 열려서, Data Table 은 높이가 100 px 라 테이블이 보이지 않고
         *          User Settings 는 값 입력란의 폭이 0 이 된다. 도킹된 창에는 적용되지 않는다.
         */
        virtual float2 getInitialPanelSize() const { return float2{ 640.0f, 420.0f }; }

        /** @brief 편집이 생겼음을 알립니다. 제목의 미저장 표시와 종료 확인이 이 값을 봅니다. */
        void markDocumentDirty() { _bDocumentDirty = true; }
        /** @brief 저장이나 되돌리기 직후 dirty 를 지웁니다. */
        void clearDocumentDirty() { _bDocumentDirty = false; }
        /** @brief 문서를 디스크에 씁니다. 성공하면 true 입니다. 문서가 없는 패널은 구현하지 않습니다. */
        [[nodiscard]] virtual bool saveDocument() { return false; }
        /**
         * @brief 지금 저장해도 되면 true 입니다. false 이면 `saveDocumentAndClearDirty` 가 `saveDocument` 를 호출하지 않고 false 를 반환합니다.
         * @details 문서를 읽지 못한 패널(깨졌거나 새 형식)은 이전 문서의 데이터를 가지고 있다. 그대로 쓰면 읽지 못한 파일을 이전 문서로
         *          덮어쓴다(`EditorDocumentPanel::markDocumentLoadFailed`).
         */
        virtual bool canSaveDocument() const { return true; }
        /** @brief 저장하지 않고 디스크나 기본값 상태로 되돌립니다. 되돌릴 것이 없으면 구현하지 않습니다. */
        virtual void revertDocument() {}

    private:
        bool _bOpen;
        bool _bWindowFocused;
        bool _bDocumentDirty;
    };
} // namespace sw::editor
