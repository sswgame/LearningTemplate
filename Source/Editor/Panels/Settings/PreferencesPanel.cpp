#include "pch.h"

#include "Editor/Panels/Settings/PreferencesPanel.h"

#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorTextExtentImGui.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/Inspector/Model/InspectorPropertyLayout.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct PreferencesPanelInternal
        {
            /** @brief 마지막 편집 뒤 저장까지 기다리는 시간입니다. 끌기 한 번에 파일을 수십 번 쓰지 않게 묶는다. */
            static constexpr int64 kSaveDelayNanoseconds = constant::kNanosecondsPerSecond / 2;

            /** @brief 검색어에 맞는 프로퍼티가 섹션에 하나라도 있으면 true 입니다. 검색어가 없으면 늘 true 입니다. */
            static bool matchesFilter( const EditorSettingsRegistration& registration, const utf8* pFilterText )
            {
                const EditorListFilter filter{ pFilterText };
                if ( filter.isActive() == false )
                    return true;
                if ( filter.matches( registration._pLabel ) )
                    return true;
                if ( registration._pfnGetType() == nullptr )
                    return false; // 그리기 함수 쪽은 이름으로만 찾는다
                vector<InspectorPropertyGroup> listGroup;
                InspectorPropertyLayout::collectPropertyGroups( *registration._pfnGetType(), {}, filter, listGroup );
                return listGroup.empty() == false;
            }

            static void openPanel()
            {
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                (void)editorContext.getSubsystem<EditorPanelManager>().setPanelOpen<PreferencesPanel>( true ); // 패널은 이 파일에 등록되어 있다
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    PreferencesPanel::PreferencesPanel()
        : IEditorPanel( false )
        , _grid{}
        , _listVisibleSection{}
        , _sectionFilterText{}
        , _sectionListGeneration{ 0 }
        , _sectionTypeGeneration{ 0 }
        , _bSectionListBuilt{ false }
        , _pSelected{ nullptr }
        , _pendingSaveNanoseconds{ 0 }
        , _visibleSectionCount{ 0 }
        , _bModifiedOnly{ false }
    {
    }

    const vector<const EditorSettingsRegistration*>& PreferencesPanel::getVisibleSections()
    {
        using SettingsRegistry         = EditorExtensionRegistry<EditorSettingsRegistration>;
        const uint32 listGeneration    = SettingsRegistry::getList().getGeneration();
        const uint32 typeGeneration    = TypeRegistry::getGeneration();
        const bool   bSameAsLastBuild  = _bSectionListBuilt && _sectionListGeneration == listGeneration && _sectionTypeGeneration == typeGeneration;
        const bool   bListStillCurrent = bSameAsLastBuild && _sectionFilterText == _grid.getFilterText();
        if ( bListStillCurrent )
            return _listVisibleSection;

        // 검색 글이 있으면 섹션마다 프로퍼티 묶음을 지어 본다 — 매 프레임 하면 섹션 수만큼 묶음을 짓는다.
        _listVisibleSection.clear();
        for ( uint32 index = 0; index < SettingsRegistry::getCount(); ++index )
        {
            const EditorSettingsRegistration& registration = SettingsRegistry::getAt( index );
            if ( PreferencesPanelInternal::matchesFilter( registration, _grid.getFilterText() ) )
                _listVisibleSection.push_back( &registration );
        }
        _sectionFilterText     = _grid.getFilterText();
        _sectionListGeneration = listGeneration;
        _sectionTypeGeneration = typeGeneration;
        _bSectionListBuilt     = true;
        return _listVisibleSection;
    }

    void PreferencesPanel::drawContent()
    {
        // 체크를 먼저 둔다 — 검색 칸이 남은 폭을 다 쓰므로 뒤에 두면 창 밖으로 밀린다.
        ImGui::Checkbox( "Modified only", &_bModifiedOnly );
        EditorSelfTestMarks::note( "preferences.modifiedOnly" );
        ImGui::SameLine();
        _grid.drawSearchBar( "preferences.search" );

        // 왼쪽: 검색어에 맞는 섹션 목록. 고른 섹션이 걸러지면 첫 섹션으로 옮긴다.
        const float32 listWidth = measureExtent( EditorTextExtent::ofText( 24.0f, 0.0f ) ).x;
        uint32        visibleCount{ 0 };
        bool          bSelectedVisible = false;
        ImGui::BeginChild( "##PreferenceSections", ImVec2{ listWidth, 0.0f }, ImGuiChildFlags_Borders );
        for ( const EditorSettingsRegistration* pRegistration : getVisibleSections() )
        {
            const EditorSettingsRegistration& registration = *pRegistration;
            ++visibleCount;
            if ( _pSelected == nullptr )
                _pSelected = &registration;
            const bool bSelected = _pSelected == &registration;
            bSelectedVisible     = bSelectedVisible || bSelected;
            if ( ImGui::Selectable( registration._pLabel, bSelected ) )
                _pSelected = &registration;
        }
        ImGui::EndChild();
        _visibleSectionCount = visibleCount;
        if ( bSelectedVisible == false )
            _pSelected = nullptr;

        // 오른쪽: 고른 섹션의 그리드. "Modified only" 면 기본값과 같은 프로퍼티를 이미 그린 것으로 넘겨 숨긴다.
        ImGui::SameLine();
        ImGui::BeginChild( "##PreferenceProperties", ImVec2{ 0.0f, 0.0f } );
        if ( _pSelected != nullptr && _pSelected->_pfnDrawPage != nullptr )
        {
            _pSelected->_pfnDrawPage();
            ImGui::EndChild();
            savePendingChanges( false );
            return;
        }
        if ( _pSelected == nullptr || _pSelected->_pfnGetType() == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSettings", "NoSectionMatches" ) );
            ImGui::EndChild();
            savePendingChanges( false );
            return;
        }
        const TypeInfo&       type      = *_pSelected->_pfnGetType();
        void*                 pInstance = _pSelected->_pInstance;
        vector<hashed_string> listHidden;
        if ( _bModifiedOnly )
        {
            for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
            {
                if ( EditorPreferencesStore::isPropertyModified( prop, ObjectAddress{ pInstance }, ObjectAddress{ _pSelected->_pDefault } ) == false )
                    listHidden.push_back( prop._name );
            }
        }
        EditorPropertyGridTarget target{};
        target._pInstance = pInstance;
        target._pType     = &type;
        target._onEdited  = SW_DELEGATE_METHOD( Delegate<void( const PropertyInfo& )>, &PreferencesPanel::onPropertyEdited, this );
        _grid.drawProperties( target, _pSelected->_pLabel, listHidden );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kRefresh, "Reset Section" ) ) )
        {
            EditorPreferencesStore::resetSection( *_pSelected );
            if ( _pendingSaveNanoseconds == 0 )
                _pendingSaveNanoseconds = MonotonicClock::nowNanoseconds();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "ResetSection" ) );
        ImGui::EndChild();
        savePendingChanges( false );
    }

    void PreferencesPanel::shutdown( IRHIDevice* /*pRHIDevice*/ )
    {
        savePendingChanges( true );
    }

    uint32 PreferencesPanel::getVisibleSectionCount()
    {
        const PreferencesPanel* pPanel = editor::getSubsystem<EditorPanelManager>().findPanel<PreferencesPanel>(); // 필수 서비스: 없으면 버그(getSubsystem 이 멈춘다)
        return pPanel != nullptr ? pPanel->_visibleSectionCount : 0u;                                              // 패널이 없으면 보인 섹션도 없다
    }

    void PreferencesPanel::onPropertyEdited( const PropertyInfo& /*prop*/ )
    {
        if ( _pSelected != nullptr && _pSelected->_pfnOnChanged != nullptr )
            _pSelected->_pfnOnChanged();
        if ( _pendingSaveNanoseconds == 0 )
            _pendingSaveNanoseconds = MonotonicClock::nowNanoseconds();
    }

    void PreferencesPanel::savePendingChanges( bool bForce )
    {
        if ( _pendingSaveNanoseconds == 0 )
            return;
        if ( bForce == false && MonotonicClock::nowNanoseconds() - _pendingSaveNanoseconds < PreferencesPanelInternal::kSaveDelayNanoseconds )
            return;
        _pendingSaveNanoseconds = 0;
        (void)EditorPreferencesStore::saveAll( EditorPreferencesStore::getDefaultFilePath() ); // 실패는 saveAll 이 경고로 알린다
    }

    SW_EDITOR_PANEL( PreferencesPanel, PreferencesPanel::kPanelID, EditorPanelCategory::Tool, 2000 );
    SW_EDITOR_COMMAND( EditorPreferences, "editor.preferences", 2210, "Preferences...", editoricon::kSettings, "Editor", "에디터 환경설정 창을 엽니다",
                       "Open editor preferences", {}, &PreferencesPanelInternal::openPanel, nullptr, NoScene, "MainMenu/Edit" );
} // namespace sw::editor
