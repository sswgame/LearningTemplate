/**
 * @file IEditorPanel.h
 * @brief 도킹할 수 있는 에디터 패널의 인터페이스입니다. 셸 크롬은 draw() 가, 내용은 drawContent() 가 그립니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"

namespace sw
{
} // namespace sw

namespace sw::editor
{
    class IEditorDocumentPanel;

    /**
     * @brief 기본 도킹 배치(`EditorDockLayout::applyDefaultDockLayout`)에서 패널이 붙는 영역입니다. 패널이 스스로 밝힙니다.
     * @details Godot 의 도크 기본 슬롯, 언리얼 탭 스포너의 기본 레이아웃과 같다. 배치가 패널 제목을 문자열로 따로 적어 두면
     *          제목이 바뀐 패널이 말없이 떠 있는 창이 된다(프리팹 오버라이드 도구가 큰 창으로 씬 뷰를 덮었다). 그래서 배치는 등록된 패널의 제목만 쓴다.
     */
    enum class EditorDockSlot : uint8
    {
        Floating = 0, ///< 도킹하지 않고 떠 있는 창으로 연다
        Left,
        Right,
        Center,
        CenterFront, ///< 가운데 영역의 앞 탭(Scene). 한 패널만 쓴다
        Bottom,
        BottomFront, ///< 아래 영역의 앞 탭(Output Log). 한 패널만 쓴다 - 정하지 않으면 직전에 포커스를 받은 탭이 남아 배치 초기화가 새 프로세스와 달랐다
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
        //    shutdown 은 GPU 리소스가 있는 패널만 재정의한다(렌더 전 패널 훅은 두지 않는다 — 재정의가 없는데 매 프레임 가상 호출로 돌았다)
        //    처음 열림은 생성자 인자 하나가 정한다(필요할 때 여는 도구는 false)
        // ------------------------------------------------------------------------------
        /** @brief 패널 제목을 반환합니다. */
        virtual const utf8* getPanelTitle() const = 0;
        /** @brief 패널 크롬을 열고 drawContent() 를 호출합니다. */
        void draw();
        /** @brief 패널을 종료할 때 리소스를 정리합니다. */
        virtual void shutdown() {}
        /** @brief 기본 배치에서 붙을 영역입니다. 기본은 떠 있는 창입니다. */
        virtual EditorDockSlot getDefaultDockSlot() const { return EditorDockSlot::Floating; }
        /**
         * @brief 이 패널이 여는 문서의 애셋 종류입니다. 문서 패널이 아니면 알 수 없음(빈 값)입니다.
         * @details 경로와 애셋 종류로 여는 요청(`EditorWorkspace::requestOpenDocumentPanel`)이 이것으로 패널을 찾는다. 종류 등록의 패널 제목 문자열로 찾지 않는다.
         */
        virtual EditorAssetType getDocumentKind() const { return EditorAssetType{}; }
        /** @brief 마지막 draw 에서 이 패널 창에 포커스가 있었으면 true 입니다. */
        bool isWindowFocused() const { return _bWindowFocused; }
        /** @brief 마지막 draw 에서 내용 영역에 글자 줄(`GetTextLineHeightWithSpacing`)이 몇 줄 들어갔는지입니다. 기본 배치의 크기 점검(감시 값 `Editor.PanelContentLines`)이 읽습니다. */
        float32 getContentLineCount() const { return _contentLineCount; }
        /** @brief 마지막 draw 에서 내용 영역에 숫자 "0" 이 몇 칸 들어갔는지입니다(감시 값 `Editor.PanelContentColumns`). */
        float32 getContentColumnCount() const { return _contentColumnCount; }
        /** @brief 내용을 한 번이라도 그렸으면 true 입니다. 접히거나 가려진 채면 false 라서 `getContentLineCount` 가 낡은 값을 낼 일이 없습니다. */
        bool hasDrawnContent() const { return _bContentDrawn; }

        // ------------------------------------------------------------------------------
        // 3) 문서. 문서를 가진 패널만 `IEditorDocumentPanel` 을 상속한다 — 미저장 비트 · 저장 · 되돌리기 · 닫기 확인이 거기에만 있다
        // ------------------------------------------------------------------------------
        /** @brief 문서 패널이면 그 문서 쪽(`IEditorDocumentPanel`), 아니면 nullptr 입니다. 패널 관리자가 저장 · 종료 확인에 이것만 쓴다. */
        virtual IEditorDocumentPanel*       asDocumentPanel() { return nullptr; }
        virtual const IEditorDocumentPanel* asDocumentPanel() const { return nullptr; }
        /** @brief 저장되지 않은 편집이 있으면 true 입니다(문서 없는 패널은 늘 false). */
        bool hasUnsavedDocument() const;

        // ------------------------------------------------------------------------------
        // 4) 열림 상태. ImGui Begin 의 p_open
        //    **닫는 경로는 `requestClose` 하나다.** X, 패널 메뉴 토글, 프리팹 창의 Close 단추, 레이아웃 불러오기, 최대화가 모두 이것을 거친다.
        //    미저장 편집이 있으면 닫지 않고 Save / Don't Save / Cancel 확인을 기다린다. 경로마다 확인을 직접 부르게 두면 새 경로가 확인을 빼먹는다.
        // ------------------------------------------------------------------------------
        /** @brief 패널이 열려 있으면 true 입니다. 닫기 확인을 기다리는 동안에도 true 입니다. */
        bool isOpen() const { return _bOpen; }
        /**
         * @brief true 면 엽니다. false 면 `requestClose` 를 부릅니다. 미저장 편집이 있으면 바로 닫히지 않으므로 부른 뒤에도 `isOpen()` 이 true 일 수 있습니다.
         * @details 확인 없이 닫는 입구는 없다. 편집을 버리고 닫으려면 문서 쪽 `applyCloseChoice( EditorUnsavedChoice::Discard )` 로 명시한다.
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
         * @details 이미 닫힌 패널이면 아무 일도 하지 않는다. 그려지지 않는 패널이 확인 대기 상태로 남지 않게 하기 위해서다.
         */
        void requestClose();
        /**
         * @brief ImGui 가 X 로 내린 열림 표시를 닫기 요청으로 바꿉니다. `draw()` 가 창을 연 직후 부릅니다.
         * @details ImGui 는 X 를 누르면 그 프레임에 `p_open` 만 false 로 만든다. 그대로 두면 미저장 편집이 확인 없이 닫힌다.
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
            , _bWindowFocused{ SW_FALSE }
            , _bContentDrawn{ SW_FALSE }
            , _contentLineCount{ 0.0f }
            , _contentColumnCount{ 0.0f }
        {
        }

        /** @brief 패널 본문을 그립니다. Begin 과 End 는 draw() 가 처리합니다. */
        virtual void drawContent() = 0;
        /** @brief 패널이 접히거나 탭이 숨겨졌을 때 호출합니다. */
        virtual void onPanelCollapsed() {}
        /**
         * @brief 패널이 닫힌 순간 한 번 부릅니다. `requestClose` 와 문서의 `applyCloseChoice` 가 닫는 모든 경로에서 부릅니다.
         * @details 닫힌 패널은 `draw()` 가 돌지 않으므로, 창이 가지고 있던 전역 상태(초점 씬, Undo 스택)는 여기서 해제해야 한다.
         */
        virtual void onPanelClosed() {}
        /**
         * @brief EditorPanelFlags 조합을 반환합니다. 기본은 None 입니다.
         * @details `UnsavedDocument` 는 여기에 넣지 않는다. draw() 가 문서의 dirty 상태를 보고 스스로 추가한다. 그래야 이 함수를
         *          다른 플래그 때문에 재정의한 패널도 제목의 미저장 표시를 잃지 않는다.
         */
        virtual EditorPanelFlags getPanelFlags() const { return EditorPanelFlags::None; }
        /**
         * @brief 떠 있는 창으로 처음 열 때의 크기입니다. 글자 칸 수 · 줄 수로 적으며(`EditorTextExtent`, 픽셀 값은 만들 수 없다) 작업 영역의 90 % 까지로 잘립니다.
         * @details 기본값이 있어야 한다. 크기를 정하지 않은 도구 창은 내용 크기로 열려서, Data Table 은 높이가 100 px 라 테이블이 보이지 않고
         *          User Settings 는 값 입력란의 폭이 0 이 된다. 도킹된 창에는 적용되지 않는다.
         */
        virtual EditorTextExtent getInitialPanelSize() const { return EditorTextExtent::ofText( 85.0f, 22.0f ); }

        /**
         * @brief 다음 프레임을 곧 그리게 합니다(`EditorRenderViews::requestAnimationFrame`) — 재생 · 애니메이션 중인 창이 에디터 유휴로 들지 않게 한다.
         * @details 패널 View 가 작업 공간 헤더(`EditorContext` · `EditorRenderViews`)를 보지 않게 기반이 창구를 둔다(`docs/agents/EditorUI.md` MVVM 계약). 에디터가 서 있을 때(그리기)만 부른다.
         */
        static void requestAnimationFrame();

        /** @brief 열림 표시를 끄고 `onPanelClosed` 를 부릅니다. 패널을 닫는 유일한 위치입니다(문서 쪽 닫기 확인도 이것으로 닫는다). */
        void closeNow()
        {
            _bOpen = false;
            onPanelClosed();
        }

    private:
        bool    _bOpen; ///< NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
        uint8   _bWindowFocused : 1;
        uint8   _bContentDrawn  : 1; ///< 마지막 draw 에서 내용을 그렸다(접히거나 가려지지 않았다)
        float32 _contentLineCount;   ///< 마지막으로 그린 draw 의 내용 영역 높이 / 글자 줄 높이
        float32 _contentColumnCount; ///< 마지막으로 그린 draw 의 내용 영역 폭 / 숫자 "0" 너비
    };
} // namespace sw::editor
