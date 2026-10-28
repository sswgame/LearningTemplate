#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorBakeOutputPath.h"

#include "Core/Container/StringUtil.h"

#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Scene/Scene.h"

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

    string makeSceneBakeAssetPath( const Scene& scene, const GameObject& owner, string_view extension )
    {
        const string& sourcePath = scene.getSourcePath();
        string        assetID    = AssetPaths::toAssetID( sourcePath );
        if ( assetID.empty() )
            assetID = sourcePath;
        if ( assetID.empty() )
            return {};
        const size_t suffix = assetID.rfind( EditorBakeOutputPathInternal::kSceneSuffix );
        string       path{ suffix == string::npos ? assetID : assetID.substr( 0, suffix ) };
        string       name = StringUtil::toLower( owner.getName().c_str() );
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
