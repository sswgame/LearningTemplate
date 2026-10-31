#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorBakeOutputPath.h"

#include "Core/Container/StringUtil.h"

namespace sw::editor
{
    namespace
    {
        struct EditorBakeOutputPathInternal
        {
            /// @brief 씬 파일 이름의 끝 — 이것을 떼고 오브젝트 이름을 붙인다.
            static constexpr string_view kSceneSuffix = ".scene.xml";
        };
    } // namespace

    string makeSceneBakeAssetPath( string_view sceneAssetID, string_view ownerName, string_view extension )
    {
        if ( ownerName.empty() || StringUtil::endsWith( sceneAssetID, EditorBakeOutputPathInternal::kSceneSuffix ) == false )
            return {};
        string path{ sceneAssetID.substr( 0, sceneAssetID.size() - EditorBakeOutputPathInternal::kSceneSuffix.size() ) };
        string name = StringUtil::toLower( string( ownerName ).c_str() );
        for ( utf8& character : name )
        {
            const bool bAllowed = ( 'a' <= character && character <= 'z' ) || ( '0' <= character && character <= '9' );
            if ( bAllowed == false )
                character = '_';
        }
        path += '.';
        path += name;
        path += extension;
        return path;
    }
} // namespace sw::editor
