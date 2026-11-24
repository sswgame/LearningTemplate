#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorMapCheck.h"

#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Asset/EditorContentScan.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"

namespace sw::editor
{
    namespace
    {
        struct EditorMapCheckInternal
        {
            /** @brief 검사 출처 번호의 표시 비트입니다(오브젝트 id 는 이 비트를 쓰지 않는다). */
            static constexpr uint64 kCheckSourceBit = 1ull << 63u;

            /** @brief 활성 팩의 데이터 XML 을 리소스 id 로 모읍니다. 팩이 없으면 비어 있습니다. */
            static void collectPackAssets( vector<string>& outListAsset )
            {
                const DataPath& packRoot = editor::getSubsystem<const GameConfig>()._packRoot;
                const string    folder   = packRoot.isSet() ? EditorPaths::findContentFolder( packRoot.get() ) : string{};
                if ( folder.empty() )
                    return;
                vector<string> listFile;
                (void)EditorContentScan::collectFiles( FileUtil::joinPath( folder, "data" ), ".xml", listFile, true ); // 데이터 폴더가 없는 팩이면 검사할 것도 없다
                for ( const string& file : listFile )
                {
                    string assetID = editor::getSubsystem<AssetPaths>().toAssetID( file );
                    if ( assetID.empty() == false )
                        outListAsset.push_back( std::move( assetID ) );
                }
            }
        };
    } // namespace

    SW_DEFINE_EDITOR_EXTENSION_KIND( EditorMapCheckRegistration );

    EditorMapCheckReport::EditorMapCheckReport( const vector<string>& listPackAsset )
        : _pListPackAsset{ &listPackAsset }
        , _listIssue{}
    {
    }

    void EditorMapCheckReport::add( ValidationSeverity severity, string_view label, string_view message )
    {
        ValidationIssue& issue = _listIssue.emplace_back();
        issue._severity        = severity;
        issue._sourceLabel     = string( label );
        issue._message         = string( message );
    }

    uint64 EditorMapCheck::makeSourceID( string_view registrationID )
    {
        return HashUtil::computeStringHash64( registrationID, false ) | EditorMapCheckInternal::kCheckSourceBit;
    }

    bool EditorMapCheck::isCheckSource( uint64 sourceID )
    {
        return ( sourceID & EditorMapCheckInternal::kCheckSourceBit ) != 0;
    }

    uint32 EditorMapCheck::runAll( GameObjectManager& manager )
    {
        using MapCheckRegistry = EditorExtensionRegistry<EditorMapCheckRegistration>;
        const Stopwatch stopwatch;
        vector<string>  listPackAsset;
        EditorMapCheckInternal::collectPackAssets( listPackAsset );
        ValidationIssueLog& log   = manager.getSubsystem<SceneValidationIssues>().getLog();
        uint32              count = 0;
        for ( uint32 index = 0; index < MapCheckRegistry::getCount(); ++index )
        {
            const EditorMapCheckRegistration& registration = MapCheckRegistry::getAt( index );
            EditorMapCheckReport              report{ listPackAsset };
            registration._pfnCheck( manager, report );
            log.replaceIssues( makeSourceID( registration._pID ), report.getIssues() );
            count += static_cast<uint32>( report.getIssues().size() );
        }
        SW_LOG_INFO_ID( "MapCheck.ChecksRan", "Map Check: %# registered check(s), %# issue(s) in %# ms", MapCheckRegistry::getCount(), count, stopwatch.getElapsedMilliseconds() );
        return count;
    }
} // namespace sw::editor
