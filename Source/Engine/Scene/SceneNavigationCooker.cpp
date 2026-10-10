#include "pch.h"

#include "Engine/Scene/SceneNavigationCooker.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Object/Component/Navigation/NavMeshSurfaceComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/SceneNavigation.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

namespace sw
{
    SW_LOG_CALLER( "SceneNavigationCooker" );

    namespace
    {
        struct SceneNavigationCookerInternal
        {
            static constexpr string_view kSceneSuffix   = ".scene.xml";
            static constexpr string_view kSurfaceMarker = "NavMeshSurfaceComponent";

            static bool isInOtherGamePack( string_view relativePath, string_view activePackRoot )
            {
                if ( activePackRoot.empty() || StringUtil::startsWith( relativePath, "game/", true ) == false )
                    return false;
                const string activePrefix = string( activePackRoot ) + "/";
                return StringUtil::startsWith( relativePath, activePrefix, true ) == false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool SceneNavigationCooker::cookScene( string_view sceneResourcePath, NavMeshAsset& outAsset )
    {
        outAsset = NavMeshAsset{};
        SceneDocument document;
        if ( document.loadXML( sceneResourcePath ) == false )
        {
            SW_LOG_ERROR( "Navmesh cook could not read scene '%#'", sceneResourcePath );
            return false;
        }
        Scene scene{ "NavMeshCook" };
        if ( scene.instantiate( document ) == false || scene.getObjectManager() == nullptr )
        {
            SW_LOG_ERROR( "Navmesh cook could not build scene '%#'", sceneResourcePath );
            return false;
        }
        GameObjectManager&    manager    = *scene.getObjectManager();
        SceneNavigation&      navigation = manager.getSceneNavigation();
        vector<hashed_string> listAgentType;
        manager.forEachComponentOfType<NavMeshSurfaceComponent>( [&listAgentType]( NavMeshSurfaceComponent* pSurface )
        {
            if ( pSurface->getAgentTypes().empty() )
                listAgentType.push_back( hashed_string{} );
            for ( const hashed_string& agentType : pSurface->getAgentTypes() )
            {
                listAgentType.push_back( agentType );
            }
        } );
        bool bAll = true;
        for ( const hashed_string& agentType : listAgentType )
        {
            NavMeshAssetEntry entry;
            if ( navigation.makeCookedEntry( agentType, entry ) == false )
            {
                SW_LOG_ERROR( "Navmesh cook could not bake agent type '%#' of scene '%#'", agentType.c_str(), sceneResourcePath );
                bAll = false;
                continue;
            }
            outAsset.setEntry( std::move( entry ) );
        }
        return bAll;
    }

    uint32 SceneNavigationCooker::cookAll( const string& resourceRoot, const string& cookedDir, uint32& outFailedCount )
    {
        using Internal = SceneNavigationCookerInternal;
        if ( cookedDir.empty() )
        {
            ++outFailedCount;
            return 0;
        }
        vector<string> listFile;
        FileUtil::collectFiles( resourceRoot, ".xml", listFile, true );
        const string normalizedRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) );
        const string activePackRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( GameConfig::getActive()._packRoot ) );
        uint32       writtenCount   = 0;
        for ( const string& filePath : listFile )
        {
            if ( StringUtil::endsWith( filePath, Internal::kSceneSuffix, true ) == false )
                continue;
            const string normalizedFile = FileUtil::normalizeSeparators( filePath );
            const string relativePath   = normalizedFile.substr( std::min( normalizedRoot.size() + 1, normalizedFile.size() ) );
            if ( Internal::isInOtherGamePack( relativePath, activePackRoot ) )
                continue;
            // 표면이 없는 씬은 세우지 않는다 — 글에 컴포넌트 이름이 없으면 넘긴다.
            string text;
            if ( FileUtil::readTextFile( filePath, text ) == false || text.find( Internal::kSurfaceMarker.data() ) == string::npos )
                continue;
            NavMeshAsset asset;
            if ( cookScene( relativePath, asset ) == false )
            {
                ++outFailedCount;
                continue;
            }
            if ( asset.getEntries().empty() )
                continue;
            const string cookedPath = FileUtil::joinPath( cookedDir, NavMeshAsset::makeCookedPath( relativePath ) );
            if ( asset.saveToFile( cookedPath ) == false )
            {
                SW_LOG_ERROR( "Navmesh cook could not write '%#'", cookedPath.c_str() );
                ++outFailedCount;
                continue;
            }
            SW_LOG_INFO( "Cooked navmesh '%#' (%# agent types)", cookedPath.c_str(), asset.getEntries().size() );
            ++writtenCount;
        }
        return writtenCount;
    }
} // namespace sw
