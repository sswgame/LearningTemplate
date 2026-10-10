#include "pch.h"

#include "Editor/Panels/PreferencesPanel.h"

#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Panels/Inspector/InspectorPropertyLayout.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Reflection/ReflectionTypes.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct PreferencesPanelInternal
        {
            /** @brief 마지막 편집 뒤 저장까지 기다리는 시간입니다. 끌기 한 번에 파일을 수십 번 쓰지 않게 묶는다. */
            static constexpr int64 kSaveDelayNanoseconds = constant::kNanosecondsPerSecond / 2;

            static uint32& getVisibleSectionCount()
            {
                static uint32 s_visibleSectionCount{ 0 };
                return s_visibleSectionCount;
            }

            /** @brief 검색어에 맞는 프로퍼티가 섹션에 하나라도 있으면 true 입니다. 검색어가 없으면 늘 true 입니다. */
            static bool matchesFilter( const EditorSettingsRegistration& registration, const utf8* pFilterText )
            {
                const EditorListFilter filter{ pFilterText };
                if ( filter.isActive() == false )
                    return true;
                if ( filter.matches( registration._pLabel ) )
                    return true;
                vector<InspectorPropertyGroup> listGroup;
                InspectorPropertyLayout::collectPropertyGroups( *registration._pfnGetType(), {}, filter, listGroup );
                return listGroup.empty() == false;
            }

            static void openPanel()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                    (void)pContext->getPanelManager().setPanelOpen( "preferences", true ); // 패널은 이 파일에 등록되어 있다
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    PreferencesPanel::PreferencesPanel()
        : IEditorPanel( false )
        , _grid{}
        , _pSelected{ nullptr }
        , _pendingSaveNanoseconds{ 0 }
        , _bModifiedOnly{ false }
    {
    }

    void PreferencesPanel::drawContent()
    {
        using SettingsRegistry = EditorRegistry<EditorSettingsRegistration>;
        // 체크를 먼저 둔다 — 검색 칸이 남은 폭을 다 쓰므로 뒤에 두면 창 밖으로 밀린다.
        ImGui::Checkbox( "Modified only", &_bModifiedOnly );
        EditorSelfTestMarks::note( "preferences.modifiedOnly" );
        ImGui::SameLine();
        _grid.drawSearchBar( "preferences.search" );

        // 왼쪽: 검색어에 맞는 섹션 목록. 고른 섹션이 걸러지면 첫 섹션으로 옮긴다.
        const float32 listWidth = 180.0f * EditorThemeUtil::getDpiScale();
        uint32        visibleCount{ 0 };
        bool          bSelectedVisible = false;
        ImGui::BeginChild( "##PreferenceSections", ImVec2{ listWidth, 0.0f }, ImGuiChildFlags_Borders );
        for ( uint32 index = 0; index < SettingsRegistry::getCount(); ++index )
        {
            const EditorSettingsRegistration& registration = SettingsRegistry::getAt( index );
            if ( PreferencesPanelInternal::matchesFilter( registration, _grid.getFilterText() ) == false )
                continue;
            ++visibleCount;
            if ( _pSelected == nullptr )
                _pSelected = &registration;
            const bool bSelected = _pSelected == &registration;
            bSelectedVisible     = bSelectedVisible || bSelected;
            if ( ImGui::Selectable( registration._pLabel, bSelected ) )
                _pSelected = &registration;
        }
        ImGui::EndChild();
        PreferencesPanelInternal::getVisibleSectionCount() = visibleCount;
        if ( bSelectedVisible == false )
            _pSelected = nullptr;

        // 오른쪽: 고른 섹션의 그리드. "Modified only" 면 기본값과 같은 프로퍼티를 이미 그린 것으로 넘겨 숨긴다.
        ImGui::SameLine();
        ImGui::BeginChild( "##PreferenceProperties", ImVec2{ 0.0f, 0.0f } );
        if ( _pSelected == nullptr || _pSelected->_pfnGetType() == nullptr )
        {
            EditorWidgets::drawEmptyHint( "No section matches the search." );
            ImGui::EndChild();
            savePendingChanges( false );
            return;
        }
        const TypeInfo&       type      = *_pSelected->_pfnGetType();
        void*                 pInstance = _pSelected->_pfnGetInstance();
        vector<hashed_string> listHidden;
        if ( _bModifiedOnly )
        {
            for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
            {
                if ( EditorPreferencesStore::isPropertyModified( prop, pInstance, _pSelected->_pfnGetDefault() ) == false )
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
        EditorWidgets::drawTooltip( "이 섹션의 값을 모두 기본값으로 되돌립니다" );
        ImGui::EndChild();
        savePendingChanges( false );
    }

    void PreferencesPanel::shutdown( IRHIDevice* /*pRHIDevice*/ )
    {
        savePendingChanges( true );
    }

    uint32 PreferencesPanel::getVisibleSectionCount()
    {
        return PreferencesPanelInternal::getVisibleSectionCount();
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

    SW_EDITOR_PANEL( PreferencesPanel, "preferences", EditorPanelCategory::Tool, 2000 );
    SW_EDITOR_COMMAND( EditorPreferences, "editor.preferences", 2210, "Preferences...", editoricon::kSettings, "Editor", "에디터 환경설정 창을 엽니다",
                       "Open editor preferences", {}, &PreferencesPanelInternal::openPanel, nullptr, "MainMenu/Edit" );
} // namespace sw::editor
