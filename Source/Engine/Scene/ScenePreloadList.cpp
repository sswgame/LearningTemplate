#include "pch.h"

#include "Engine/Scene/ScenePreloadList.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentAssetPathUtil.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    SW_LOG_CALLER( "ScenePreloadList" );

    namespace
    {
        struct ScenePreloadListInternal
        {
            static constexpr string_view kSceneXML = ".scene.xml";
            static constexpr string_view kSceneBin = ".scene.bin";

            /** @brief 프리로드할 에셋 경로인가 — 확장자가 있고(내장 id 가 아니고) 씬 경로가 아니다. */
            static bool isPreloadable( string_view path )
            {
                string_view extension;
                FileUtil::getExtension( path, extension );
                if ( extension.empty() )
                    return false;
                return StringUtil::endsWith( path, kSceneXML, true ) == false && StringUtil::endsWith( path, kSceneBin, true ) == false;
            }
        };
    } // namespace

    string ScenePreloadList::makeListPath( string_view scenePath )
    {
        for ( const string_view suffix : { ScenePreloadListInternal::kSceneXML, ScenePreloadListInternal::kSceneBin } )
        {
            if ( StringUtil::endsWith( scenePath, suffix, true ) )
                return string( scenePath.substr( 0, scenePath.size() - suffix.size() ) ) + string( kExtension );
        }
        return {};
    }

    void ScenePreloadList::collect( const GameObjectManager& objects, vector<string>& outListPath )
    {
        outListPath.clear();
        objects.forEachGameObject( [&outListPath]( GameObject* pObject )
        {
            for ( Component* pComponent : pObject->getComponents() )
            {
                if ( pComponent == nullptr || pComponent->isPendingDestroy() )
                    continue;
                const TypeInfo* pType = pComponent->getTypeInfo();
                if ( pType == nullptr )
                    continue;
                pType->forEachProperty( [pComponent, &outListPath]( const PropertyInfo& prop )
                {
                    const string_view path = ComponentAssetPathUtil::readAssetPath( prop, pComponent );
                    if ( ScenePreloadListInternal::isPreloadable( path ) )
                        outListPath.push_back( FileUtil::normalizeSeparators( path ) );
                }, true );
            }
        } );
        VectorUtil::sortUnique( outListPath );
    }

    bool ScenePreloadList::write( string_view diskPath, const vector<string>& listPath )
    {
        JSONDocument document;
        JSONValue    root = document.makeObject();
        root.set( "version" ).setUint( kVersion );
        JSONValue paths = root.set( "paths" );
        paths.setArray();
        for ( const string& path : listPath )
        {
            paths.pushBack().setString( path );
        }
        return document.saveFile( diskPath, 2 );
    }

    bool ScenePreloadList::read( const ResourceID& resourceID, vector<string>& outListPath )
    {
        const string_view listResourceID = resourceID.view();
        outListPath.clear();
        JSONDocument document;
        if ( document.loadResource( resourceID ) == false )
        {
            SW_LOG_ERROR( "Scene preload list '%#' could not be read: %#", listResourceID, document.getLastError().c_str() );
            return false;
        }
        const JSONValue root    = document.getRoot();
        const auto      version = root.readRequired<uint32>( "version" );
        if ( version.has_value() == false || *version != kVersion )
        {
            SW_LOG_ERROR( "Scene preload list '%#' has version %# - this build reads version %#", listResourceID, version.has_value() ? *version : 0u, kVersion );
            return false;
        }
        const JSONValue paths = root.get( "paths" );
        if ( paths.isArray() == false )
        {
            SW_LOG_ERROR( "Scene preload list '%#' has no 'paths' array", listResourceID );
            return false;
        }
        for ( size_t index = 0; index < paths.size(); ++index )
        {
            const auto path = paths.at( index ).read<string>();
            if ( path.has_value() == false )
            {
                outListPath.clear();
                SW_LOG_ERROR( "Scene preload list '%#': path %# is not a string", listResourceID, static_cast<uint32>( index ) );
                return false;
            }
            outListPath.push_back( *path );
        }
        return true;
    }
} // namespace sw
