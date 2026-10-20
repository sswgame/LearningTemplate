/**
 * @file IEditorPanel.h
 * @brief 도킹할 수 있는 에디터 패널의 인터페이스입니다. 셸 크롬은 draw() 가, 내용은 drawContent() 가 그립니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"

namespace sw
{
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    /**
     * @brief 기본 도킹 배치(`EditorDockLayout::applyDefaultDockLayout`)에서 패널이 붙는 영역입니다. 패널이 스스로 밝힙니다.
     * @details Godot 의 도크 기본 슬롯, 언리얼 탭 스포너의 기본 레이아웃과 같다. 배치가 패널 제목을 문자열로 따로 적어 두면
     *          제목이 바뀐 패널이 말없이 떠 있는 창이 된다(프리팹 오버라이드 도구가 큰 창으로 씬 뷰를 덮었다). 그래서 배치는 등록된 패널의 제목만 쓴다.
     */
    enum class EditorDockSlot : uint8
    {
        Floating = 0, ///< 붙이지 않는다 — 떠 있는 창으로 연다
        Left,
        Right,
        Center,
        CenterFront, ///< 가운데 영역의 앞 탭(Scene). 한 패널만 쓴다
        Bottom,
    };

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
        /** @brief 기본 배치에서 붙을 영역입니다. 기본은 떠 있는 창입니다. */
        virtual EditorDockSlot getDefaultDockSlot() const { return EditorDockSlot::Floating; }
        /**
         * @brief 이 패널이 여는 문서의 애셋 종류입니다. 문서 패널이 아니면 `Unknown` 입니다.
         * @details 경로 · 애셋 종류로 여는 요청(`EditorWorkspace::requestOpenDocumentPanel`)이 이것으로 패널을 찾는다 — 종류 표의 패널 제목 글로 찾지 않는다.
         */
        virtual EditorAssetType getDocumentKind() const { return EditorAssetType::Unknown; }
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
        //    **닫는 길은 하나다 — `requestClose`.** X 도, 패널 메뉴 토글도, 프리팹 창의 Close 단추도, 레이아웃 불러오기도, 최대화도 이것을 거친다.
        //    미저장 편집이 있으면 닫지 않고 Save / Don't Save / Cancel 확인을 기다린다. 길마다 확인을 손으로 부르게 두면 새 길이 확인을 빼먹는다.
        // ------------------------------------------------------------------------------
        /** @brief 패널이 열려 있으면 true 입니다. 닫기 확인을 기다리는 동안에도 true 입니다. */
        bool isOpen() const { return _bOpen; }
        /**
         * @brief true 면 엽니다. false 면 `requestClose` 입니다 — 미저장 편집이 있으면 바로 닫히지 않으므로 부른 뒤에 `isOpen()` 이 true 일 수 있습니다.
         * @details 확인 없이 닫는 입구는 없다. 편집을 버리고 닫으려면 `applyCloseChoice( EditorUnsavedChoice::Discard )` 로 명시한다.
         */
        void setOpen( bool open )
        {
            if ( open )
                _bOpen = true;
            else
                requestClose();
        }
        /**
         * @brief 패널을 닫으려 합니다. 깨끗하면(또는 `asksBeforeClose` 가 false 면) 바로 닫고, 미저장 편집이 있으면 열어 둔 채 확인을 기다립니다.
         * @details 이미 닫힌 패널이면 아무 일도 하지 않는다 — 그려지지 않는 패널이 확인을 쥔 채 남지 않게 한다.
         */
        void requestClose()
        {
            if ( _bOpen == false )
                return;
            if ( _bDocumentDirty && asksBeforeClose() )
                _bConfirmClose = true;
            else
                _bOpen = false;
        }
        /** @brief 닫기 확인(Save / Don't Save / Cancel)을 기다리는 중이면 true 입니다. */
        bool isCloseConfirmPending() const { return _bConfirmClose; }
        /**
         * @brief 닫기 확인에서 고른 항목을 적용합니다. 선택이 없으면 아무 일도 하지 않습니다.
         * @details Save 는 저장에 성공해야 닫는다(실패하면 열어 두어 편집을 지키고 확인도 끝낸다). Don't Save 는 `revertDocument` 로 편집을 버린 뒤 닫고, Cancel 은 열어 둔다.
         */
        void applyCloseChoice( EditorUnsavedChoice choice )
        {
            if ( choice == EditorUnsavedChoice::None )
                return;
            _bConfirmClose = false;
            if ( EditorSessionPolicy::shouldSaveBeforeAction( choice ) && saveDocumentAndClearDirty() == false )
                return;
            if ( EditorSessionPolicy::shouldClearDirtyWithoutSave( choice ) )
                discardDirtyDocument();
            if ( EditorSessionPolicy::shouldProceedWithAction( choice ) )
                _bOpen = false;
        }
        /**
         * @brief ImGui 가 X 로 내린 열림 표시를 닫기 요청으로 바꿉니다. `draw()` 가 창을 연 직후 부릅니다.
         * @details ImGui 는 X 를 누르면 그 프레임에 `p_open` 만 내린다. 그대로 두면 미저장 편집이 확인 없이 닫힌다.
         */
        void absorbCloseButton()
        {
            if ( _bOpen )
                return;
            _bOpen = true;
            requestClose();
        }
        /** @brief ImGui 에 넘길 열림 상태 포인터를 반환합니다. X 가 내린 값은 `absorbCloseButton` 이 받습니다. */
        bool* getOpenFlagAddress() { return &_bOpen; }

    protected:
        /** @brief 기본 열림 상태로 에디터 패널을 만듭니다. */
        explicit IEditorPanel( bool bOpenByDefault = true )
            : _bOpen{ bOpenByDefault }
            , _bWindowFocused{ false }
            , _bDocumentDirty{ false }
            , _bConfirmClose{ false }
        {
        }

        /**
         * @brief 닫을 때 미저장 편집을 확인해야 하면 true 입니다. 기본은 true 입니다.
         * @details 편집이 패널 밖(전역 변수 관리자처럼 바로 적용되는 곳)에 살아 있어서 패널을 닫아도 아무것도 잃지 않는 패널만 false 로 재정의한다.
         *          그런 패널에 확인을 두면 "저장하거나 값을 초기화하거나" 말고는 창을 숨길 수 없다.
         */
        virtual bool asksBeforeClose() const { return true; }
        /**
         * @brief 닫기 확인과 제목의 미저장 표시보다 앞서 부릅니다(`draw()` 마다, 패널이 접혀 있어도). 편집을 패널 바깥의 기록(미리보기 씬의 Undo 스택 등)에서 읽는 패널이
         *        여기서 `markDocumentDirty` 를 해, 편집한 바로 다음 프레임에 X 를 눌러도 확인을 건너뛰지 않게 합니다. 기본은 아무 일도 하지 않습니다.
         */
        virtual void syncDocumentDirty() {}

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
        /** @brief 닫기 확인 모달을 그리고 고른 항목을 적용합니다. */
        void drawCloseConfirm();

        bool _bOpen;
        bool _bWindowFocused;
        bool _bDocumentDirty;
        bool _bConfirmClose; ///< 닫으려다 미저장 확인을 기다리는 중
    };
} // namespace sw::editor
