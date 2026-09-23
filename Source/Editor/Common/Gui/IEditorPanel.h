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
     * @brief 도킹 가능한 ImGui 패널입니다. 파생 클래스는 drawContent() 만 구현합니다.
     */
    class IEditorPanel
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 생명주기 (GPU 리소스를 쓰는 파생은 shutdown 을 재정의한다)
        // ------------------------------------------------------------------------------
        /** @brief 파생 패널이 리소스를 해제할 수 있게 합니다. */
        virtual ~IEditorPanel() = default;

        // ------------------------------------------------------------------------------
        // 2) IEditorPanel: 제목 / 크롬 / 내용
        //    preRender/shutdown 은 GPU 리소스가 있는 패널만 재정의한다
        //    isToolPanel 이면 닫힌 채 시작한다(필요할 때 여는 도구)
        // ------------------------------------------------------------------------------
        /** @brief 패널 제목을 반환합니다. */
        virtual const utf8* getPanelTitle() const = 0;
        /** @brief Panel 크롬을 열고 drawContent()를 호출합니다. */
        void draw();
        /** @brief 렌더링 전에 필요한 RHI 리소스를 업데이트합니다. */
        virtual void preRender( IRHIDevice* /*rhiDevice*/ ) {}
        /** @brief 패널 종료 시 리소스를 정리합니다. */
        virtual void shutdown( IRHIDevice* /*rhiDevice*/ ) {}
        /** @brief 도구 패널이면 true 입니다. 도구 패널은 닫힌 채, 핵심 패널은 열린 채 시작합니다. */
        virtual bool isToolPanel() const { return false; }
        /** @brief 마지막 draw 에서 이 패널 창에 포커스가 있었으면 true 입니다. */
        bool isWindowFocused() const { return _bWindowFocused; }

        // ------------------------------------------------------------------------------
        // 3) 문서 상태. dirty 비트는 **기반 클래스가 든다**
        //    파생은 markDocumentDirty() 로 알리고, saveDocument()/revertDocument() 만 구현한다.
        // ------------------------------------------------------------------------------
        /**
         * @brief 저장되지 않은 편집이 있으면 true입니다.
         * @details 가상 함수가 아닙니다. 예전에는 가상이어서 패널마다 자기 dirty 플래그를 들고 네 메서드를 다시 구현했고(세
         *          패널이 똑같은 것을 복사했습니다), 하나는 플래그만 두고 계약을 아예 구현하지 않았습니다. InputMapEditorPanel
         *          이 "* Unsaved changes" 를 화면에는 띄우면서 Ctrl+S 와 종료 확인에는 보이지 않아, 편집이 조용히 사라졌습니다.
         *          비트를 기반 클래스가 들면 그런 반쪽 구현이 불가능합니다.
         */
        bool isDocumentDirty() const { return _bDocumentDirty; }
        /**
         * @brief `saveDocument()` 를 부르고 **성공했을 때만** dirty 를 지웁니다.
         * @details 저장 경로는 **모두 이것을 거칩니다.** 예전에는 "저장했으면 dirty 를 지운다" 는 순서를 파생 아홉이 각자
         *          구현했고, 그래서 서로 달랐습니다. 둘은 실패해도 무조건 지우고 `true` 를 반환했고(편집이 조용히 사라집니다),
         *          하나는 성공해도 지우지 않았습니다(저장했는데 계속 미저장으로 남습니다). 바로 아래 `discardDirtyDocument` 는
         *          처음부터 기반 클래스가 순서를 들고 있었고, 저장 쪽만 빠져 있었습니다. 파생은 **"쓰고, 됐는지 답한다"** 만
         *          하면 됩니다.
         * @return 저장에 성공했으면 true. false 면 dirty 는 그대로 남습니다.
         */
        bool saveDocumentAndClearDirty()
        {
            if ( saveDocument() == false )
                return false;
            clearDocumentDirty();
            return true;
        }
        /** @brief dirty면 저장합니다. 깨끗하거나 저장에 실패하면 false입니다. */
        bool trySaveDirtyDocument()
        {
            if ( _bDocumentDirty == false )
                return false;
            return saveDocumentAndClearDirty();
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
         * @brief EditorPanelFlags 조합입니다. 기본은 None 입니다.
         * @details `UnsavedDocument` 는 여기에 넣지 않습니다. draw() 가 dirty 상태를 보고 스스로 더합니다. 그래야 이 함수를
         *          다른 플래그 때문에 재정의한 패널이 제목의 미저장 표시를 잃지 않습니다(세 패널이 각자 같은 분기를 적고
         *          있었습니다).
         */
        virtual EditorPanelFlags getPanelFlags() const { return EditorPanelFlags::None; }
        /** @brief FirstUseEver 크기입니다. (0,0) 이면 적용하지 않습니다. */
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
