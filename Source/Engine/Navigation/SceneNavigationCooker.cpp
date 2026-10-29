#include "pch.h"

#include "Engine/Navigation/SceneNavigationCooker.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Asset/Cook/CookStore.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/FileSystem/AssetIO.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Navigation/Component/NavMeshSurfaceComponent.h"
#include "Engine/Navigation/Component/SceneNavigation.h"
#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
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

            static bool isInInactiveGamePack( string_view relativePath, string_view activePackRoot )
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
    bool SceneNavigationCooker::cookScene( const SceneEngineServices& services, const AssetID& assetID, NavMeshAsset& outAsset )
    {
        const string_view sceneAssetPath = assetID.view();
        outAsset                         = NavMeshAsset{};
        SceneDocument document;
        if ( document.loadXML( assetID ) == false )
        {
            SW_LOG_ERROR( "Navmesh cook could not read scene '%#'", sceneAssetPath );
            return false;
        }
        Scene scene{ "NavMeshCook", services };
        if ( scene.instantiate( document ) == false || scene.getObjectManager() == nullptr )
        {
            SW_LOG_ERROR( "Navmesh cook could not build scene '%#'", sceneAssetPath );
            return false;
        }
        GameObjectManager&    manager    = *scene.getObjectManager();
        SceneNavigation&      navigation = manager.getSubsystem<SceneNavigation>();
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
        // 매니저 순회는 순서를 약속하지 않는다. 쿠킹 항목이 방문 순서에 따라 달라지지 않게 이름순으로 정렬하고, 같은 에이전트 타입은 한 번만 베이크한다.
        std::sort( listAgentType.begin(), listAgentType.end(), []( const hashed_string& lhs, const hashed_string& rhs )
        {
            return lhs.view() < rhs.view();
        } );
        listAgentType.erase( std::unique( listAgentType.begin(), listAgentType.end() ), listAgentType.end() );
        bool bAll = true;
        for ( const hashed_string& agentType : listAgentType )
        {
            NavMeshAssetEntry entry;
            if ( navigation.makeCookedEntry( agentType, entry ) == false )
            {
                SW_LOG_ERROR( "Navmesh cook could not bake agent type '%#' of scene '%#'", agentType.c_str(), sceneAssetPath );
                bAll = false;
                continue;
            }
            outAsset.setEntry( std::move( entry ) );
        }
        return bAll;
    }

    uint32 SceneNavigationCooker::cookAll( const SceneEngineServices& services, const string& resourceRoot, const string& cookedDir, const CookStore& store, uint32& outFailedCount )
    {
        using Internal = SceneNavigationCookerInternal;
        if ( cookedDir.empty() )
        {
            ++outFailedCount;
            return 0;
        }
        vector<string> listFile;
        if ( AssetPaths::collectContentFiles( resourceRoot, ".xml", listFile ) == false )
        {
            SW_LOG_ERROR( "Scene navigation cook could not list '%#' - nothing is cooked", resourceRoot );
            ++outFailedCount;
            return 0;
        }
        std::sort( listFile.begin(), listFile.end() );
        const string      normalizedRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) );
        const string      activePackRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( engine::getSubsystem<GameConfig>()._packRoot.get() ) );
        const CookJobDesc desc           = makeCookJobDesc();
        CookTally         tally;
        for ( const string& filePath : listFile )
        {
            if ( StringUtil::endsWith( filePath, Internal::kSceneSuffix, true ) == false )
                continue;
            const string normalizedFile = FileUtil::normalizeSeparators( filePath );
            const string relativePath   = normalizedFile.substr( std::min( normalizedRoot.size() + 1, normalizedFile.size() ) );
            if ( Internal::isInInactiveGamePack( relativePath, activePackRoot ) )
                continue;
            const string      outputID   = NavMeshAsset::makeCookedPath( relativePath );
            const string      cookedPath = FileUtil::joinPath( cookedDir, outputID );
            const CookOutcome outcome    = store.cookFile( desc, outputID, cookedPath, [&]()
               {
                // 표면이 없는 씬은 세우지 않는다 — 글에 컴포넌트 이름이 없으면 넘긴다. 작업 안에서 읽어 씬이 이 작업의 입력이 된다.
                string text;
                if ( AssetIO::readTextFile( filePath, text ) == false || text.find( Internal::kSurfaceMarker.data() ) == string::npos )
                    return CookStep::Skipped;
                NavMeshAsset asset;
                if ( cookScene( services, AssetID{ relativePath }, asset ) == false )
                    return CookStep::Failed;
                if ( asset.getEntries().empty() )
                    return CookStep::Skipped;
                if ( asset.saveToFile( cookedPath ) == false )
                {
                    SW_LOG_ERROR( "Navmesh cook could not write '%#'", cookedPath.c_str() );
                    return CookStep::Failed;
                }
                SW_LOG_INFO( "Cooked navmesh '%#' (%# agent types)", cookedPath.c_str(), asset.getEntries().size() );
                return CookStep::Written;
            } );
            outFailedCount += outcome == CookOutcome::Failed ? 1u : 0u;
            tally.add( outcome );
        }
        SW_LOG_INFO( "Navmesh cook: %# reused from the DDC, %# cooked", tally._reusedCount, tally._cookedCount );
        return tally.getWrittenCount();
    }

    CookJobDesc SceneNavigationCooker::makeCookJobDesc()
    {
        CookJobDesc desc;
        desc._cookerName    = "NavMesh";
        desc._cookerVersion = kCookVersion;
        desc._target        = "Any";
        desc._settingsHash  = 0; // 베이크 설정은 리소스(내비 설정 표)라 읽기로 입력에 든다
        return desc;
    }
} // namespace sw
