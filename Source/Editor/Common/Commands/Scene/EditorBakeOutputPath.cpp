#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorBakeOutputPath.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Resource/ResourcePaths.h"
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

    string makeSceneBakeResourcePath( const Scene& scene, const GameObject& owner, string_view extension )
    {
        const string& sourcePath = scene.getSourcePath();
        string        resourceID = ResourcePaths::toResourceID( sourcePath );
        if ( resourceID.empty() )
            resourceID = sourcePath;
        if ( resourceID.empty() )
            return {};
        const size_t suffix = resourceID.rfind( EditorBakeOutputPathInternal::kSceneSuffix );
        string       path{ suffix == string::npos ? resourceID : resourceID.substr( 0, suffix ) };
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
