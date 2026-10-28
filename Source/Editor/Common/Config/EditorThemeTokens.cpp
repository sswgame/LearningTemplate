#include "pch.h"

#include "Editor/Common/Config/EditorThemeTokens.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Config/EditorThemeConfig.h"

#include "Engine/Asset/AssetPaths.h"
#include "Engine/Common/EngineDefines.h"
#include "Engine/Config/ConfigManager.h"

#include <algorithm>

namespace sw::editor
{
    namespace
    {
        struct EditorThemeTokensInternal
        {
            /** @brief 색 토큰 하나: 토큰 키, 읽은 글자, 해석한 값을 담는 필드입니다. 테이블 한 줄이 해석과 오류 문장을 함께 정한다. */
            struct ColorRow
            {
                const utf8* _pKey;
                string EditorThemeColorDef::* _pText;
                Color EditorThemeConfig::* _pColor;
            };

            static constexpr ColorRow kArrColorRow[] = {
                {            "_text",             &EditorThemeColorDef::_text,             &EditorThemeConfig::_text},
                {       "_textMuted",        &EditorThemeColorDef::_textMuted,        &EditorThemeConfig::_textMuted},
                {          "_accent",           &EditorThemeColorDef::_accent,      &EditorThemeConfig::_accentColor},
                {        "_windowBg",         &EditorThemeColorDef::_windowBg,         &EditorThemeConfig::_windowBg},
                {         "_panelBg",          &EditorThemeColorDef::_panelBg,          &EditorThemeConfig::_panelBg},
                {        "_headerBg",         &EditorThemeColorDef::_headerBg,         &EditorThemeConfig::_headerBg},
                {         "_frameBg",          &EditorThemeColorDef::_frameBg,          &EditorThemeConfig::_frameBg},
                {          "_border",           &EditorThemeColorDef::_border,           &EditorThemeConfig::_border},
                {         "_success",          &EditorThemeColorDef::_success,     &EditorThemeConfig::_successColor},
                {         "_warning",          &EditorThemeColorDef::_warning,     &EditorThemeConfig::_warningColor},
                {           "_error",            &EditorThemeColorDef::_error,       &EditorThemeConfig::_errorColor},
                {            "_info",             &EditorThemeColorDef::_info,        &EditorThemeConfig::_infoColor},
                {     "_scrollbarBg",      &EditorThemeColorDef::_scrollbarBg,      &EditorThemeConfig::_scrollbarBg},
                {  "_dockingEmptyBg",   &EditorThemeColorDef::_dockingEmptyBg,   &EditorThemeConfig::_dockingEmptyBg},
                {   "_tableRowBgAlt",    &EditorThemeColorDef::_tableRowBgAlt,    &EditorThemeConfig::_tableRowBgAlt},
                {       "_overlayBg",        &EditorThemeColorDef::_overlayBg,        &EditorThemeConfig::_overlayBg},
                {"_selectionOutline", &EditorThemeColorDef::_selectionOutline, &EditorThemeConfig::_selectionOutline},
                {       "_highlight",        &EditorThemeColorDef::_highlight,        &EditorThemeConfig::_highlight},
                {  "_toggleInactive",   &EditorThemeColorDef::_toggleInactive,   &EditorThemeConfig::_toggleInactive},
                {           "_axisX",            &EditorThemeColorDef::_axisX,            &EditorThemeConfig::_axisX},
                {           "_axisY",            &EditorThemeColorDef::_axisY,            &EditorThemeConfig::_axisY},
                {           "_axisZ",            &EditorThemeColorDef::_axisZ,            &EditorThemeConfig::_axisZ},
                {    "_colliderWire",     &EditorThemeColorDef::_colliderWire,     &EditorThemeConfig::_colliderWire},
                {      "_cameraWire",       &EditorThemeColorDef::_cameraWire,       &EditorThemeConfig::_cameraWire},
                {   "_viewportLabel",    &EditorThemeColorDef::_viewportLabel,    &EditorThemeConfig::_viewportLabel},
            };

            /** @brief 폴더 순회 콜백입니다. 토큰 파일이면 접미사를 뺀 이름을 목록(@p pContext)에 더합니다. */
            static bool addThemeName( void* pContext, const DirectoryEntry& entry )
            {
                string_view fileName;
                FileUtil::getFileNamePart( entry._path, fileName );
                const string_view extension{ EditorThemeTokens::kExtension };
                if ( entry._bDirectory == false && StringUtil::endsWith( fileName, extension, true ) )
                    static_cast<vector<string>*>( pContext )->emplace_back( fileName.substr( 0, fileName.size() - extension.size() ) );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    string EditorThemeTokens::getFolderPath()
    {
        return AssetPaths::findLooseDomainFolder( path::kEditorPack, kFolderName ).getDiskPath();
    }

    void EditorThemeTokens::listThemeNames( vector<string>& outListName )
    {
        outListName.clear();
        const string folder = getFolderPath();
        if ( folder.empty() )
            return;
        // 폴더가 없으면 목록이 비고, 부르는 쪽이 기본 벌을 찾다 실패를 남긴다.
        (void)FileUtil::forEachDirectoryEntry( folder, false, &EditorThemeTokensInternal::addThemeName, &outListName );
        std::sort( outListName.begin(), outListName.end() );
    }

    bool EditorThemeTokens::loadTheme( string_view name, EditorThemeConfig& outConfig, string& outError )
    {
        const string folder = getFolderPath();
        if ( folder.empty() )
        {
            outError = "the content root is not mounted";
            return false;
        }
        string fileName{ name };
        fileName.append( kExtension );
        return loadThemeFile( FileUtil::joinPath( folder, fileName ), outConfig, outError );
    }

    bool EditorThemeTokens::loadThemeResource( const AssetID& id )
    {
        const string&     assetID = id.str();
        EditorThemeConfig config{};
        string            error;
        if ( loadThemeFile( AssetPaths::findLooseFile( assetID ).getDiskPath(), config, error ) )
            return true;
        SW_LOG_ERROR( "Theme tokens were not loaded: %#", error.c_str() );
        return false;
    }

    bool EditorThemeTokens::loadThemeFile( string_view absolutePath, EditorThemeConfig& outConfig, string& outError )
    {
        EditorThemeDef         def{};
        const ConfigReadResult result = ConfigManager::readConfigFile( def, absolutePath );
        if ( result != ConfigReadResult::Loaded )
        {
            outError = string{ absolutePath } + ( result == ConfigReadResult::Missing ? ": file not found" : ": not a valid theme token file" );
            return false;
        }
        if ( resolve( def, outConfig, outError ) == false )
        {
            outError = string{ absolutePath } + ": " + outError;
            return false;
        }
        return true;
    }

    bool EditorThemeTokens::resolve( const EditorThemeDef& def, EditorThemeConfig& outConfig, string& outError )
    {
        EditorThemeConfig resolved{};
        for ( const EditorThemeTokensInternal::ColorRow& row : EditorThemeTokensInternal::kArrColorRow )
        {
            const string& text = def._color.*row._pText;
            if ( text.empty() || Color::fromHex( text, resolved.*row._pColor ) == false )
            {
                outError = string{ "_color." } + row._pKey + " must be #RRGGBB or #RRGGBBAA, got '" + text + "'";
                return false;
            }
        }
        const bool bBadScalar = def._typography._fontSize <= 0.0f || def._typography._iconScale <= 0.0f || def._density <= 0.0f;
        if ( bBadScalar )
        {
            outError = "_typography._fontSize, _typography._iconScale and _density must be positive";
            return false;
        }
        if ( def._name.empty() )
        {
            outError = "_name is empty";
            return false;
        }
        resolved._name        = def._name;
        resolved._displayName = def._displayName.empty() ? def._name : def._displayName;
        resolved._radius      = def._radius;
        resolved._spacing     = def._spacing;
        resolved._stroke      = def._stroke;
        resolved._typography  = def._typography;
        resolved._density     = def._density;
        outConfig             = std::move( resolved );
        return true;
    }
} // namespace sw::editor
