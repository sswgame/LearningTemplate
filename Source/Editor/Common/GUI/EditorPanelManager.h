/**
 * @file EditorPanelManager.h
 * @brief 에디터 패널 인스턴스를 한곳에서 등록하고 관리합니다. `EditorContext` 가 소유합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Common/Workspace/EditorExtensionRegistry.h"

namespace sw
{
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{

    /** @brief 에디터 패널의 카테고리입니다. */
    enum class EditorPanelCategory : uint8
    {
        Core = 0, // Hierarchy, Inspector, Scene, Game, Console, Profiler, ContentBrowser
        Tool,     // Sequencer, AnimationGraph, DialogueGraph, Prefab, TileMap, SpriteClip
        Custom    // 게임/플러그인 커스텀 패널
    };

    struct EditorPanelRegistration;

    /** @brief 등록된 에디터 패널 항목의 메타데이터입니다. */
    struct EditorPanelEntry
    {
        string                         _id;
        string                         _title;
        EditorPanelCategory            _category{ EditorPanelCategory::Core };
        unique_ptr<IEditorPanel>       _pInstance;
        const EditorPanelRegistration* _pRegistration{ nullptr }; ///< 이 인스턴스를 만든 등록 항목. 직접 registerPanel 한 것은 nullptr 이다
        uint64                         _drawNanosSum{ 0 };        ///< `-gv_editorPanelTimes` 가 모으는 그리기 시간의 합(ns)
        uint64                         _drawNanosMax{ 0 };        ///< 그중 가장 긴 한 프레임의 시간(ns)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorPanelRegistration
     * @brief 패널 한 종류의 등록 항목입니다. 패널의 .cpp 가 `SW_EDITOR_PANEL` 로 둡니다.
     * @details `_pID` 는 `windows.json` 의 가시성 키이자 `-gv_editorOpenPanel` 의 값이다. `_order` 는 Panel 메뉴와 그리기 순서이고,
     *          카테고리는 Panel 메뉴의 Panels 와 Tools 그룹을 정한다.
     */
    struct EditorPanelRegistration : EditorExtensionRegistration
    {
        static constexpr const utf8* kKindName = "panel";

        EditorPanelCategory _category;
        unique_ptr<IEditorPanel> ( *_pCreate )();
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 등록 항목이 가리키는 패널 생성 함수입니다. */
    template <typename TPanel>
    unique_ptr<IEditorPanel> createEditorPanel()
    {
        return make_unique<TPanel>();
    }

    /**
     * @class EditorPanelManager
     * @brief 에디터 패널 인스턴스를 한곳에서 등록하고 관리합니다. `EditorContext` 가 소유합니다.
     */
    class SW_EDITOR_API EditorPanelManager : public EditorSubsystem
    {
    public:
        EditorPanelManager();
        ~EditorPanelManager() override;

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "EditorPanelManager";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        EditorPanelManager( const EditorPanelManager& )            = delete;
        EditorPanelManager& operator=( const EditorPanelManager& ) = delete;

        /** @brief 패널을 등록합니다. 메뉴에 보이는 이름은 패널의 `getPanelTitle()` 입니다. */
        void registerPanel( unique_ptr<IEditorPanel> pPanel,
                            string_view              panelID,
                            EditorPanelCategory      category = EditorPanelCategory::Core );

        const vector<EditorPanelEntry>& getPanels() const { return _listPanel; }
        /**
         * @brief 패널 클래스로 찾습니다. 없으면 nullptr 입니다.
         * @details id 는 그 클래스의 `kPanelID` 하나에서 온다 — 등록(`SW_EDITOR_PANEL( T, T::kPanelID, … )`)도 같은 상수라, 이름을 바꿔도 조회가 조용히 nullptr 이 되지 않는다.
         *          코드가 아는 패널은 이것으로만 찾는다. 문자열 id 조회는 실행 중에 들어온 글(콘솔 인자 · `-gv_` · 창 배치 파일)용 `findPanelByID` 다.
         */
        template <typename TPanel>
        TPanel* findPanel() const
        {
            return static_cast<TPanel*>( findPanelByID( TPanel::kPanelID ) );
        }
        /** @brief 패널 클래스로 열거나 닫습니다. 그 패널이 없으면 false 입니다(`findPanel` 과 같은 상수로 찾는다). */
        template <typename TPanel>
        bool setPanelOpen( bool bOpen )
        {
            return setPanelOpen( TPanel::kPanelID, bOpen );
        }
        /** @brief 실행 중에 들어온 id(콘솔 인자 · `-gv_` · 창 배치 파일 · 등록 목록)나 창 제목으로 찾습니다. 코드가 아는 패널은 `findPanel<T>()` 로 찾습니다. */
        IEditorPanel* findPanelByID( string_view panelID ) const;
        /** @brief 애셋 종류 @p kind 의 문서를 여는 패널(`IEditorPanel::getDocumentKind`)입니다. 없으면 nullptr 입니다. */
        IEditorPanel* findDocumentPanel( EditorAssetType kind ) const;
        /** @brief id(또는 창 제목)로 열거나 닫습니다. 확장 모듈의 패널과 실행 중 id 용입니다 — 코드가 아는 패널은 `setPanelOpen<T>()` 를 씁니다. */
        bool setPanelOpen( string_view panelID, bool bOpen );
        void clear();
        /** @brief `SW_EDITOR_PANEL` 로 등록된 패널을 순서대로 만들어 둡니다. 이전 목록은 버립니다. */
        void registerDefaultPanels();
        /**
         * @brief 등록 목록과 맞춥니다. 새 항목은 인스턴스를 만들고(지난번에 열려 있던 id 이면 열린 채로), 사라진 항목의 인스턴스는 종료합니다.
         * @details 세대(`EditorExtensionRegistrationList::getGeneration`)가 같으면 바로 반환한다. 에디터 프레임 앞에서 호출한다(`EditorContext::syncExtensionRegistrations`).
         */
        void syncWithRegistry( IRHIDevice* pRHIDevice );
        /**
         * @brief 등록 항목이 [@p pBegin, @p pEnd)(모듈 라이브러리) 안에 있는 패널을 종료하고 지웁니다. 지운 수를 반환합니다.
         * @details 이미지를 언로드하기 전에 호출한다(`EditorModuleUnloadListener`). 저장하지 않은 문서는 경고하고 버린다.
         *          열림 상태는 id 로 기억해 두었다가 같은 id 가 다시 등록되면 연 채로 만든다.
         */
        uint32 releasePanelsWithin( const void* pBegin, const void* pEnd, IRHIDevice* pRHIDevice );
        /**
         * @brief 핵심(Core)이 아닌 패널(도구 · 커스텀 · 확장)을 닫고 등록 줄로 새 인스턴스를 만듭니다 — 새 에디터 세션과 같은 상태입니다. 바꾼 수를 반환합니다.
         * @details 연 문서 · 그래프 · 맵 · 최근 목록 같은 패널 상태는 인스턴스가 소유하므로, 다시 만드는 것이 초기화다(패널마다 초기화 함수를 두지 않는다).
         *          저장하지 않은 편집은 버린다. 등록 줄 없이 직접 넣은 패널(`registerPanel`)은 만들 함수가 없어 닫기만 한다.
         */
        uint32 resetOptionalPanels( IRHIDevice* pRHIDevice );
        void   drawOpenPanels();
        void   shutdownAllPanels( IRHIDevice* pRHIDevice );
        /** @brief 포커스된 도구 문서가 dirty 이면 저장하고 true 를 반환합니다. */
        [[nodiscard]] bool saveFocusedDirtyDocument();
        /** @brief 모든 dirty 도구 문서를 저장합니다. 하나라도 실패하면 false 입니다. */
        [[nodiscard]] bool saveAllDirtyDocuments();
        /** @brief 열린 패널과 닫힌 패널을 모두 포함한 dirty 문서 개수를 반환합니다. */
        uint32 countDirtyDocuments() const;
        /** @brief 모든 dirty 도구 문서를 저장하지 않고 버립니다. */
        void discardAllDirtyDocuments();

    private:
        /** @brief `-gv_editorPanelTimes=N` 이 모은 패널별 시간을 큰 순서로 한 번 로그에 남깁니다(`EditorPanelTime|<id>|avg … us|max … us`). */
        void reportPanelTimes();

        vector<EditorPanelEntry> _listPanel;
        vector<string>           _listRememberedOpenID; ///< 지운 패널 중 열려 있던 id. 다시 등록되면 연다
        uint32                   _syncedGeneration;     ///< 마지막으로 맞춘 등록 세대. 맞춘 적이 없으면 kNotSynced 이다
        uint32                   _panelTimeFrameCount;  ///< 패널 시간을 모은 프레임 수
        bool                     _bPanelTimesReported;  ///< 한 번 기록했으면 더 모으지 않는다

        /** @brief 아직 등록 목록과 맞춘 적이 없다는 표시값입니다. */
        static constexpr uint32 kNotSynced = invalid_index::kUint32;
    };
} // namespace sw::editor

/**
 * @brief 패널 종류를 그 패널의 .cpp 에서 등록합니다. 예: `SW_EDITOR_PANEL( HierarchyPanel, HierarchyPanel::kPanelID, EditorPanelCategory::Core, 100 );`
 * @param TPanel   기본 생성자가 있는 `IEditorPanel` 구현입니다.
 * @param pID      가시성을 저장하는 키입니다. 패널 클래스의 `kPanelID` 를 넘긴다 — 조회(`findPanel<T>()`)가 같은 상수를 쓴다. 종류 안에서 유일해야 합니다.
 * @param category Panel 메뉴의 그룹입니다.
 * @param order    Panel 메뉴와 그리기 순서입니다. 작을수록 앞입니다.
 */
#define SW_EDITOR_PANEL( TPanel, pID, category, order ) \
    SW_EDITOR_REGISTER( ::sw::editor::EditorPanelRegistration, Panel_##TPanel, { pID, order }, category, &::sw::editor::createEditorPanel<TPanel> )
