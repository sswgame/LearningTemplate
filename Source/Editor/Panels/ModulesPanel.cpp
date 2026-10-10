#include "pch.h"

#include "Editor/Panels/ModulesPanel.h"

#include "Core/File/FileUtil.h"
#include "Core/Module/ModuleImageUtil.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/EditorModuleOverrides.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "sw/config/ConfigConstants.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "ModulesPanel" );

    namespace
    {
        struct ModulesPanelInternal
        {
            static uint32& getPreviewNewlyInactiveCount()
            {
                static uint32 s_count{ 0 };
                return s_count;
            }

            static const utf8* getKindName( ModuleKind kind )
            {
                switch ( kind )
                {
                    case ModuleKind::GameFramework:
                        return "GameFramework";
                    case ModuleKind::Kit:
                        return "Kits";
                    case ModuleKind::Game:
                        return "Game";
                    case ModuleKind::Editor:
                        return "Editor";
                    case ModuleKind::RHI:
                        return "RHI backends";
                    case ModuleKind::EditorExtension:
                        return "Editor extensions";
                }
                return "";
            }

            /** @brief 활성 게임의 프로젝트 매니페스트 소스 경로입니다(`Config/Game/<게임>.json` 의 이름이 게임 폴더 이름이다). */
            static string getProjectManifestPath()
            {
                string       gameName = FileUtil::getFileNamePart( config::kFileRuntimeGameConfig );
                const size_t dot      = gameName.rfind( '.' );
                if ( dot != string::npos )
                    gameName.resize( dot );
                return EditorUtil::resolveProjectRelativePath( "Source/Games/" + gameName + "/" + config::kTargetGameModule + ModuleCatalog::kManifestExtension );
            }

            static string findInactiveReason( const ModuleResolution& resolution, const string& name )
            {
                for ( const ModuleInactiveEntry& entry : resolution._listInactive )
                {
                    if ( entry._name == name )
                        return entry._reason;
                }
                return {};
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    ModulesPanel::ModulesPanel()
        : IEditorPanel( false )
        , _catalog{}
        , _resolution{}
        , _loadError{}
        , _pendingModule{}
        , _listNewlyInactive{}
        , _listNewlyActive{}
        , _bPendingEnabled{ false }
        , _bChangedSinceStart{ false }
        , _filter{}
    {
    }

    void ModulesPanel::reloadCatalog()
    {
        _catalog = ModuleCatalog{};
        _loadError.clear();
        const string         directory = ModuleImageUtil::getModuleDirectory();
        ModuleResolveContext context{};
        if ( _catalog.loadDirectory( directory, _loadError ) == false || _catalog.resolve( context, _resolution, _loadError ) == false )
            SW_LOG_WARNING( "Modules: the catalog in %# could not be resolved - %#", directory.c_str(), _loadError.c_str() );
    }

    void ModulesPanel::drawContent()
    {
        if ( _catalog.getManifestCount() == 0 && _loadError.empty() )
            reloadCatalog();
        if ( _loadError.empty() == false )
        {
            EditorThemeUtil::textError( _loadError.c_str() );
            return;
        }
        if ( _bChangedSinceStart )
        {
            EditorThemeUtil::textWarning( "Module set changed - build to apply, then restart the editor." );
            ImGui::SameLine();
            if ( ImGui::SmallButton( "Build" ) )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                    (void)pContext->getCommandRegistry().execute( "build.compileAll" ); // 막혀 있으면 빌드 상태 표시가 알린다
            }
        }
        if ( ImGui::SmallButton( "Refresh" ) )
            reloadCatalog();
        ImGui::SameLine();
        EditorWidgets::drawSearchField( "##ModuleFilter", _filter, "Search modules...", 0.0f, false );
        EditorSelfTestMarks::note( "modules.search" );
        const EditorListFilter filter{ _filter.c_str() };

        static constexpr ModuleKind kArrKindOrder[] = { ModuleKind::GameFramework, ModuleKind::Kit, ModuleKind::EditorExtension, ModuleKind::RHI, ModuleKind::Editor,
                                                        ModuleKind::Game };
        for ( const ModuleKind kind : kArrKindOrder )
        {
            if ( ImGui::CollapsingHeader( ModulesPanelInternal::getKindName( kind ), ImGuiTreeNodeFlags_DefaultOpen ) == false )
                continue;
            if ( ImGui::BeginTable( ModulesPanelInternal::getKindName( kind ), 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV ) == false )
                continue;
            ImGui::TableSetupColumn( "On", ImGuiTableColumnFlags_WidthFixed, 32.0f * EditorThemeUtil::getDpiScale() );
            ImGui::TableSetupColumn( "Module" );
            ImGui::TableSetupColumn( "Version", ImGuiTableColumnFlags_WidthFixed, 64.0f * EditorThemeUtil::getDpiScale() );
            ImGui::TableSetupColumn( "Status" );
            for ( const ModuleManifest& manifest : _catalog.getManifests() )
            {
                if ( manifest._kind != kind || filter.matchesAny( { manifest._name, manifest._description } ) == false )
                    continue;
                ImGui::PushID( manifest._name.c_str() );
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                bool       bOn     = _resolution.isActive( manifest._name );
                const bool bLocked = kind == ModuleKind::Editor || kind == ModuleKind::Game; // 끄면 이 창이 사라지거나 프로젝트가 없다
                ImGui::BeginDisabled( bLocked );
                if ( ImGui::Checkbox( "##On", &bOn ) )
                {
                    _pendingModule   = manifest._name;
                    _bPendingEnabled = bOn;
                    string error;
                    if ( EditorModuleOverrideUtil::previewToggle( _catalog, ModuleResolveContext{}, manifest._name, bOn, _listNewlyInactive, _listNewlyActive, error ) == false )
                    {
                        SW_LOG_WARNING( "Modules: turning %# %# breaks the module graph - %#", manifest._name.c_str(), bOn ? "on" : "off", error.c_str() );
                        _pendingModule.clear();
                    }
                    else
                        ImGui::OpenPopup( "Confirm Module Change" );
                }
                fixed_string<constant::kMaxBuffer128> mark;
                formatstring( mark.data(), mark.capacity(), "modules.toggle.%#", manifest._name.c_str() );
                EditorSelfTestMarks::note( mark.c_str() );
                ImGui::EndDisabled();
                ImGui::TableSetColumnIndex( 1 );
                ImGui::TextUnformatted( manifest._name.c_str() );
                EditorWidgets::drawTooltip( manifest._description.c_str() );
                ImGui::TableSetColumnIndex( 2 );
                ImGui::TextDisabled( "%s", manifest._version.toString().c_str() );
                ImGui::TableSetColumnIndex( 3 );
                if ( _resolution.isActive( manifest._name ) )
                    ImGui::TextUnformatted( "On" );
                else
                    ImGui::TextDisabled( "Off - %s", ModulesPanelInternal::findInactiveReason( _resolution, manifest._name ).c_str() );
                drawPreview();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ModulesPanelInternal::getPreviewNewlyInactiveCount() = _pendingModule.empty() ? 0u : static_cast<uint32>( _listNewlyInactive.size() );
    }

    void ModulesPanel::drawPreview()
    {
        if ( ImGui::BeginPopupModal( "Confirm Module Change", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) == false )
            return;
        ImGui::Text( "Turn %s %s?", _pendingModule.c_str(), _bPendingEnabled ? "on" : "off" );
        for ( const ModuleInactiveEntry& entry : _listNewlyInactive )
        {
            ImGui::BulletText( "Turns off: %s (%s)", entry._name.c_str(), entry._reason.c_str() );
        }
        for ( const string& name : _listNewlyActive )
        {
            ImGui::BulletText( "Turns on: %s", name.c_str() );
        }
        if ( ImGui::Button( "Apply" ) )
        {
            if ( writeProjectOverride() )
                _bChangedSinceStart = true;
            _pendingModule.clear();
            ImGui::CloseCurrentPopup();
        }
        EditorSelfTestMarks::note( "modules.apply" );
        ImGui::SameLine();
        if ( ImGui::Button( "Cancel" ) )
        {
            _pendingModule.clear();
            ImGui::CloseCurrentPopup();
        }
        EditorSelfTestMarks::note( "modules.cancel" );
        ImGui::EndPopup();
    }

    bool ModulesPanel::writeProjectOverride()
    {
        const string          path      = ModulesPanelInternal::getProjectManifestPath();
        const ModuleManifest* pManifest = _catalog.findManifest( _pendingModule );
        string                text;
        string                rewritten;
        if ( pManifest == nullptr || FileUtil::readTextFile( path, text ) == false )
        {
            SW_LOG_WARNING( "Modules: the project manifest %# could not be read", path.c_str() );
            return false;
        }
        if ( FileUtil::isReadOnlyFile( path ) )
        {
            SW_LOG_WARNING( "Modules: %# is read-only - check it out first", path.c_str() );
            return false;
        }
        if ( EditorModuleOverrideUtil::setOverride( text, _pendingModule, _bPendingEnabled, pManifest->_bEnabledByDefault, rewritten ) == false ||
             FileUtil::writeTextFile( path, rewritten ) == false )
        {
            SW_LOG_WARNING( "Modules: the project manifest %# could not be rewritten", path.c_str() );
            return false;
        }
        SW_LOG_INFO( "Modules: %# turned %# in %# - build and restart to apply", _pendingModule.c_str(), _bPendingEnabled ? "on" : "off", path.c_str() );
        return true;
    }

    uint32 ModulesPanel::getPreviewNewlyInactiveCount()
    {
        return ModulesPanelInternal::getPreviewNewlyInactiveCount();
    }

    SW_EDITOR_PANEL( ModulesPanel, "modules", EditorPanelCategory::Tool, 2020 );
} // namespace sw::editor
