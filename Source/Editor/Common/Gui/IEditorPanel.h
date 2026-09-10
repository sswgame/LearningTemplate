/**
 * @file IEditorPanel.h
 * @brief 도킹 가능한 에디터 패널 (셸 크롬은 draw(), 내용은 drawContent())
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Gui/EditorChrome.h"

namespace sw
{
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    /**
     * @class IEditorPanel
     * @brief 도킹 가능한 ImGui 패널. 파생은 drawContent()만 구현합니다.
     */
    class IEditorPanel
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 생명주기 — 파생이 GPU 리소스를 쓰면 shutdown 오버라이드
        // ------------------------------------------------------------------------------
        /** @brief 파생 패널이 리소스를 해제할 수 있게 합니다. */
        virtual ~IEditorPanel() = default;

        // ------------------------------------------------------------------------------
        // 2) IEditorPanel — 제목 / 크롬 / 내용
        //    preRender/shutdown은 GPU 리소스가 있는 패널만 오버라이드
        //    isToolPanel이면 닫힌 채 시작 (온디맨드 도구)
        // ------------------------------------------------------------------------------
        /** @brief 패널 제목을 반환합니다. */
        virtual const utf8* getPanelTitle() const = 0;
        /** @brief Panel 크롬을 열고 drawContent()를 호출합니다. */
        void draw();
        /** @brief 렌더링 전에 필요한 RHI 리소스를 업데이트합니다. */
        virtual void preRender( IRHIDevice* /*rhiDevice*/ ) {}
        /** @brief 패널 종료 시 리소스를 정리합니다. */
        virtual void shutdown( IRHIDevice* /*rhiDevice*/ ) {}
        /** @brief 온디맨드 도구는 닫힌 채 시작하고, 핵심 패널은 열린 채 시작합니다. */
        virtual bool isToolPanel() const { return false; }
        /** @brief 마지막 draw에서 이 패널 윈도우가 포커스되었으면 true입니다. */
        bool isWindowFocused() const { return _bWindowFocused; }

        // ------------------------------------------------------------------------------
        // 3) 문서 상태 — dirty 비트는 **기반이 든다**
        //    파생은 markDocumentDirty() 로 알리고, saveDocument()/revertDocument() 만 구현한다.
        // ------------------------------------------------------------------------------
        /**
         * @brief 저장되지 않은 편집이 있으면 true입니다.
         * @details 가상이 아니다. 예전에는 이것이 가상이어서 패널마다 자기 dirty 플래그를 들고
         *          네 메서드를 다시 구현했고(세 패널이 똑같은 것을 복사했다), 하나는 플래그만 두고
         *          계약을 아예 구현하지 않았다 — InputMapEditorPanel 이 "* Unsaved changes" 를
         *          화면에는 띄우면서 Ctrl+S 와 종료 확인에는 보이지 않아, 편집이 조용히 사라졌다.
         *          비트를 기반이 들면 그런 반쪽 구현이 불가능하다.
         */
        bool isDocumentDirty() const { return _bDocumentDirty; }
        /** @brief dirty면 saveDocument()를 부릅니다. 저장했으면 true입니다. */
        bool trySaveDirtyDocument()
        {
            if ( _bDocumentDirty == false )
                return false;
            return saveDocument();
        }
        /** @brief dirty면 revertDocument()를 부르고 dirty를 지웁니다. */
        void discardDirtyDocument()
        {
            if ( _bDocumentDirty == false )
                return;
            revertDocument();
            clearDocumentDirty();
        }

        // ------------------------------------------------------------------------------
        // 4) 열림 상태 — ImGui Begin의 p_open
        // ------------------------------------------------------------------------------
        /** @brief 패널이 열려 있는지 여부를 반환합니다. */
        bool isOpen() const { return _bOpen; }
        /** @brief 패널 열림 상태를 설정합니다. */
        void setOpen( bool open ) { _bOpen = open; }
        /** @brief ImGui에서 사용할 열림 상태 포인터를 반환합니다. */
        bool* getOpenPtr() { return &_bOpen; }

    protected:
        /** @brief 기본 열림 상태로 에디터 패널을 생성합니다. */
        explicit IEditorPanel( bool bOpenByDefault = true )
            : _bOpen{ bOpenByDefault }
            , _bWindowFocused{ false }
            , _bDocumentDirty{ false }
        {
        }

        /** @brief 패널 본문을 그립니다. Begin/End는 draw()가 처리합니다. */
        virtual void drawContent() = 0;
        /** @brief 패널이 접히거나 탭이 숨겨졌을 때 호출됩니다. */
        virtual void onPanelCollapsed() {}
        /**
         * @brief EditorPanelFlags 조합. 기본은 None.
         * @details `UnsavedDocument` 는 여기에 넣지 않습니다 — draw()가 dirty 상태를 보고 스스로
         *          더합니다. 그래야 이 함수를 다른 플래그 때문에 재정의한 패널이 제목의 미저장
         *          표시를 잃지 않습니다(세 패널이 각자 같은 분기를 적고 있었습니다).
         */
        virtual EditorPanelFlags getPanelFlags() const { return EditorPanelFlags::None; }
        /** @brief FirstUseEver 크기. (0,0)이면 적용하지 않습니다. */
        virtual float2 getInitialPanelSize() const { return float2{ 0.0f, 0.0f }; }

        /** @brief 편집이 생겼음을 알립니다. 제목의 미저장 표시와 종료 확인이 이것을 봅니다. */
        void markDocumentDirty() { _bDocumentDirty = true; }
        /** @brief 저장·되돌리기 직후 dirty를 지웁니다. */
        void clearDocumentDirty() { _bDocumentDirty = false; }
        /** @brief 문서를 디스크에 씁니다. 성공하면 true입니다. 문서가 없는 패널은 구현하지 않습니다. */
        virtual bool saveDocument() { return false; }
        /** @brief 저장하지 않고 디스크/기본값 상태로 되돌립니다. 되돌릴 것이 없으면 구현하지 않습니다. */
        virtual void revertDocument() {}

    private:
        bool _bOpen;
        bool _bWindowFocused;
        bool _bDocumentDirty;
    };
} // namespace sw::editor
