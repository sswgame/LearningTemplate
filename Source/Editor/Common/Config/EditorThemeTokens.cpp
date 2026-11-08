#include "pch.h"

#include "Editor/Common/Config/EditorThemeTokens.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Config/EditorThemeConfig.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"

#include <algorithm>

namespace sw::editor
{
    namespace
    {
        struct EditorThemeTokensInternal
        {
            /** @brief 0 이 될 수 없는 수치 키입니다(글꼴 크기 · 아이콘 비율 · 밀도). 0 이면 글자가 사라지거나 여백이 무너진다. */
            static constexpr const utf8* kArrPositiveMetric[] = { "Typography.FontSize", "Typography.IconScale", "Layout.Density" };

            /** @brief 키가 `Namespace.Key` 모양인지 봅니다(점 하나, 양쪽이 비지 않음). 지역화 키와 같은 모양이다. */
            static bool isValidKey( string_view key )
            {
                const size_t dot = key.find( '.' );
                return dot != string_view::npos && dot > 0 && dot + 1 < key.size() && key.find( '.', dot + 1 ) == string_view::npos;
            }

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

            /** @brief 벌 하나를 `_base` 사슬 끝까지 읽어 합칩니다(자식의 키가 이긴다). 없는 부모 · 순환 · 깊이 초과 · 틀린 파일이면 false 와 이유입니다. */
            [[nodiscard]] static bool readThemeChain( string_view absolutePath, EditorThemeDef& outDef, string& outError )
            {
                vector<string>         listPath;
                vector<EditorThemeDef> listLayer; // 자식부터
                string                 path{ absolutePath };
                while ( path.empty() == false )
                {
                    if ( VectorUtil::contains( listPath, path ) || listPath.size() >= DataInheritance::getMaxDepth() )
                    {
                        outError = path + ": its _base chain is a cycle or too deep";
                        return false;
                    }
                    listPath.push_back( path );
                    EditorThemeDef&        layer  = listLayer.emplace_back();
                    const ConfigReadResult result = ConfigManager::readConfigFile( layer, path );
                    if ( result != ConfigReadResult::Loaded )
                    {
                        outError = path + ( result == ConfigReadResult::Missing ? ": file not found" : ": not a valid theme token file" );
                        return false;
                    }
                    if ( layer._base.empty() )
                        break;
                    string_view folder;
                    FileUtil::getDirectoryPart( path, folder );
                    string fileName = layer._base;
                    fileName.append( EditorThemeTokens::kExtension );
                    path = FileUtil::joinPath( folder, fileName );
                }
                EditorThemeDef merged = std::move( listLayer.back() );
                listLayer.pop_back();
                while ( listLayer.empty() == false )
                {
                    const EditorThemeDef& layer = listLayer.back();
                    merged._name                = layer._name;
                    merged._displayName         = layer._displayName;
                    for ( const auto& [key, text] : layer._color )
                    {
                        merged._color[key] = text;
                    }
                    for ( const auto& [key, value] : layer._metric )
                    {
                        merged._metric[key] = value;
                    }
                    listLayer.pop_back();
                }
                outDef = std::move( merged );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    string EditorThemeTokens::getFolderPath()
    {
        return EditorPaths::findContentFolder( FileUtil::joinPath( constant::path::kEditorPack, kFolderName ) );
    }

    void EditorThemeTokens::listThemeNames( vector<string>& outListName )
    {
        outListName.clear();
        const string folder = getFolderPath();
        if ( folder.empty() )
            return;
        // 폴더가 없으면 목록이 비고, 부르는 쪽이 기본 벌을 찾다 실패를 남긴다.
        (void)FileUtil::forEachDirectoryEntry( folder, false, &EditorThemeTokensInternal::addThemeName, &outListName );
        VectorUtil::sort( outListName );
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

    bool EditorThemeTokens::loadThemeResource( const AssetID& id, EditorThemeConfig& outConfig )
    {
        const string&                assetID = id.str();
        string                       error;
        const optional<AbsolutePath> themeFile = editor::getSubsystem<AssetPaths>().findLooseFile( assetID );
        if ( themeFile.has_value() && loadThemeFile( themeFile->str(), outConfig, error ) )
            return true;
        SW_LOG_ERROR_ID( "EditorThemeTokens.ThemeTokensNotLoaded", "Theme tokens were not loaded: %#", error.c_str() );
        return false;
    }

    bool EditorThemeTokens::loadThemeFile( string_view absolutePath, EditorThemeConfig& outConfig, string& outError )
    {
        EditorThemeDef def{};
        if ( EditorThemeTokensInternal::readThemeChain( absolutePath, def, outError ) == false )
            return false;
        if ( resolve( def, outConfig, outError ) == false )
        {
            outError = string{ absolutePath } + ": " + outError;
            return false;
        }
        return true;
    }

    bool EditorThemeTokens::resolve( const EditorThemeDef& def, EditorThemeConfig& outConfig, string& outError )
    {
        if ( def._name.empty() )
        {
            outError = "_name is empty";
            return false;
        }
        if ( def._color.empty() || def._metric.empty() )
        {
            outError = "_color and _metric must not be empty";
            return false;
        }
        EditorThemeConfig resolved{};
        for ( const auto& [key, text] : def._color )
        {
            Color color{};
            if ( EditorThemeTokensInternal::isValidKey( key ) == false || Color::fromHex( text, color ) == false )
            {
                outError = "_color." + key + " needs a Namespace.Key name and #RRGGBB or #RRGGBBAA, got '" + text + "'";
                return false;
            }
            resolved._mapColor[hashed_string( string_view( key ) )] = color;
        }
        for ( const auto& [key, value] : def._metric )
        {
            if ( EditorThemeTokensInternal::isValidKey( key ) == false || value < 0.0f )
            {
                outError = "_metric." + key + " needs a Namespace.Key name and a value of 0 or more";
                return false;
            }
            resolved._mapMetric[hashed_string( string_view( key ) )] = value;
        }
        for ( const utf8* pKey : EditorThemeTokensInternal::kArrPositiveMetric )
        {
            const float32* pValue = resolved.findMetric( hashed_string( pKey ) );
            if ( pValue == nullptr || *pValue <= 0.0f )
            {
                outError = string( "_metric." ) + pKey + " must be present and positive";
                return false;
            }
        }
        resolved._name        = def._name;
        resolved._displayName = def._displayName.empty() ? def._name : def._displayName;
        outConfig             = std::move( resolved );
        return true;
    }

    bool EditorThemeTokens::findKeyMismatch( const EditorThemeConfig& reference, const EditorThemeConfig& other, string& outKey )
    {
        const auto findMissing = []( const auto& mapFrom, const auto& mapIn, string& outMissing )
        {
            for ( const auto& [key, value] : mapFrom )
            {
                if ( mapIn.find( key ) == mapIn.end() )
                {
                    outMissing = key.c_str();
                    return true;
                }
            }
            return false;
        };
        return findMissing( reference._mapColor, other._mapColor, outKey ) || findMissing( other._mapColor, reference._mapColor, outKey ) ||
               findMissing( reference._mapMetric, other._mapMetric, outKey ) || findMissing( other._mapMetric, reference._mapMetric, outKey );
    }
} // namespace sw::editor
