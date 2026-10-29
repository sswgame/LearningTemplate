#include "pch.h"

#include "Editor/Common/Commands/EditorBackgroundIO.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/FileSystem/AssetPaths.h"

namespace sw::editor
{
    // ======================================================================
    // EditorFileCollectJob
    // ======================================================================

    void EditorFileCollectJob::request( string_view folder, string_view extension, bool recursive )
    {
        EditorFileCollectInput input;
        input._folder     = string{ folder };
        input._extension  = string{ extension };
        input._bRecursive = recursive;

        const uint32 generation = beginRequest( std::move( input ) );
        submitEditorBackgroundJob( "EditorFileCollect",
                                   SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorFileCollectJob::runJob ),
                                   MakeTaskArgs( _pState, generation ) );
    }

    void EditorFileCollectJob::runJob( const TaskArgs& args )
    {
        const shared_ptr<State> pState     = args.get<shared_ptr<State>>( 0 );
        const uint32            generation = args.get<uint32>( 1 );

        EditorFileCollectInput input;
        if ( readInput( pState, generation, input ) == false )
            return;

        vector<string> listFile;
        // 표시용 목록이다 — 폴더가 없으면 빈 목록이 맞고, 도중에 멈춘 목록은 collectFiles 가 경로와 함께 알린 뒤 모은 데까지 보인다.
        const StopToken stopToken = readStopSignal( pState );
        (void)FileUtil::collectFiles( input._folder, input._extension, listFile, input._bRecursive, stopToken );
        if ( stopToken.isStopRequested() )
            return; // 종료가 시작돼 훑기가 멈췄다 — 짧은 목록을 싣지 않는다

        publish( pState, generation, std::move( listFile ) );
    }

    // ======================================================================
    // EditorLocalizationLoadJob
    // ======================================================================

    void EditorLocalizationLoadJob::request( string_view projectPath )
    {
        const uint32 generation = beginRequest( string( projectPath ) );
        submitEditorBackgroundJob( "EditorLocalizationLoad",
                                   SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorLocalizationLoadJob::runJob ),
                                   MakeTaskArgs( _pState, generation ) );
    }

    void EditorLocalizationLoadJob::runJob( const TaskArgs& args )
    {
        const shared_ptr<State> pState     = args.get<shared_ptr<State>>( 0 );
        const uint32            generation = args.get<uint32>( 1 );

        string projectPath;
        if ( readInput( pState, generation, projectPath ) == false )
            return;

        const StopToken stopToken = readStopSignal( pState );
        if ( stopToken.isStopRequested() )
            return; // 종료가 시작됐다 — 읽기를 시작하지 않는다
        // 읽지 못한 테이블은 경고가 남고 그 칸만 빈다 — 나머지는 그대로 보인다(저장은 그 파일을 덮지 않는다).
        LocalizationSheet sheet;
        (void)EditorDataTableCommands::loadLocalizationProject( projectPath, sheet ); // 읽지 못한 테이블은 경고로 남고 그 칸만 빈다

        if ( stopToken.isStopRequested() )
            return;
        publish( pState, generation, std::move( sheet ) );
    }

    // ======================================================================
    // EditorGameDataScanJob
    // ======================================================================

    void EditorGameDataScanJob::request()
    {
        const uint32 generation = beginRequest( EditorDataTableCommands::getGameDataFolderPath() );
        submitEditorBackgroundJob( "EditorGameDataScan",
                                   SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorGameDataScanJob::runJob ),
                                   MakeTaskArgs( _pState, generation ) );
    }

    void EditorGameDataScanJob::runJob( const TaskArgs& args )
    {
        const shared_ptr<State> pState     = args.get<shared_ptr<State>>( 0 );
        const uint32            generation = args.get<uint32>( 1 );

        string dataFolder;
        if ( readInput( pState, generation, dataFolder ) == false )
            return;

        const StopToken stopToken = readStopSignal( pState );
        if ( stopToken.isStopRequested() )
            return;
        vector<GameDataFileEntry> listEntry;
        // 표시용 목록이다 — data 폴더가 없으면 빈 목록이 맞고, 도중에 멈춘 목록은 경로와 함께 알린 뒤 모은 데까지 보인다.
        (void)EditorDataTableCommands::collectGameDataFiles( dataFolder, listEntry );

        if ( stopToken.isStopRequested() )
            return;
        publish( pState, generation, std::move( listEntry ) );
    }

    // ======================================================================
    // EditorAssetIndexJob
    // ======================================================================

    void EditorAssetIndexJob::request()
    {
        const uint32 generation = beginRequest( {} );
        // 경로 상태는 여기(에디터 스레드)서 찾아 넘긴다 — 워커는 에디터 서비스 범위를 읽지 않는다.
        const AssetPaths* const pAssetPaths = &editor::getSubsystem<AssetPaths>();
        submitEditorBackgroundJob( "EditorAssetIndex",
                                   SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorAssetIndexJob::runJob ),
                                   MakeTaskArgs( _pState, generation, pAssetPaths ) );
    }

    void EditorAssetIndexJob::runJob( const TaskArgs& args )
    {
        const shared_ptr<State> pState     = args.get<shared_ptr<State>>( 0 );
        const uint32            generation = args.get<uint32>( 1 );

        EditorBackgroundNoInput input;
        if ( readInput( pState, generation, input ) == false )
            return;

        vector<EditorAssetIndexEntry> listEntry;
        const StopToken               stopToken = readStopSignal( pState );
        EditorAssetIndex::collectEntries( *args.get<const AssetPaths*>( 2 ), listEntry, stopToken );
        if ( stopToken.isStopRequested() )
            return;

        publish( pState, generation, std::move( listEntry ) );
    }

    // ======================================================================
    // EditorFolderListingJob
    // ======================================================================

    void EditorFolderListingJob::request( string_view folderAbs, bool bRecursive )
    {
        EditorFolderListingInput input;
        input._folder     = string{ folderAbs };
        input._bRecursive = bRecursive;

        const uint32            generation  = beginRequest( std::move( input ) );
        const AssetPaths* const pAssetPaths = &editor::getSubsystem<AssetPaths>(); // 워커는 서비스를 찾지 않는다 — 여기서 넘긴다
        submitEditorBackgroundJob( "EditorFolderListing",
                                   SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorFolderListingJob::runJob ),
                                   MakeTaskArgs( _pState, generation, pAssetPaths ) );
    }

    void EditorFolderListingJob::runJob( const TaskArgs& args )
    {
        const shared_ptr<State> pState     = args.get<shared_ptr<State>>( 0 );
        const uint32            generation = args.get<uint32>( 1 );

        EditorFolderListingInput input;
        if ( readInput( pState, generation, input ) == false )
            return;

        vector<EditorFolderListingEntry> listEntry;
        const StopToken                  stopToken = readStopSignal( pState );
        EditorFolderListing::collectFolderListing( *args.get<const AssetPaths*>( 2 ), input._folder, listEntry, input._bRecursive, stopToken );
        if ( stopToken.isStopRequested() )
            return;

        publish( pState, generation, std::move( listEntry ) );
    }

    // ======================================================================
    // EditorReferenceIndexJob
    // ======================================================================

    void EditorReferenceIndexJob::request( string_view resourceRoot, TaskManager* pTaskManager )
    {
        const uint32 generation = beginRequest( string{ resourceRoot } );
        submitEditorBackgroundJob( "EditorReferenceIndex",
                                   SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorReferenceIndexJob::runJob ),
                                   MakeTaskArgs( _pState, generation, pTaskManager ) );
    }

    void EditorReferenceIndexJob::runJob( const TaskArgs& args )
    {
        const shared_ptr<State> pState     = args.get<shared_ptr<State>>( 0 );
        const uint32            generation = args.get<uint32>( 1 );
        TaskManager* const      pTasks     = args.get<TaskManager*>( 2 );

        string resourceRoot;
        if ( readInput( pState, generation, resourceRoot ) == false )
            return;

        EditorReferenceIndexData data{};
        const StopToken          stopToken = readStopSignal( pState );
        EditorReferenceIndex::scan( resourceRoot, data, pTasks, stopToken );
        if ( stopToken.isStopRequested() )
            return; // 종료가 시작돼 스캔이 중간에 멈췄다 — 불완전한 색인을 싣지 않는다

        publish( pState, generation, std::move( data ) );
    }

    // ======================================================================
    // EditorResourceCatalogJob
    // ======================================================================

    void EditorResourceCatalogJob::request()
    {
        const uint32            generation  = beginRequest( {} );
        const AssetPaths* const pAssetPaths = &editor::getSubsystem<AssetPaths>(); // 워커는 서비스를 찾지 않는다 — 여기서 넘긴다
        submitEditorBackgroundJob( "EditorResourceCatalog",
                                   SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorResourceCatalogJob::runJob ),
                                   MakeTaskArgs( _pState, generation, pAssetPaths ) );
    }

    void EditorResourceCatalogJob::runJob( const TaskArgs& args )
    {
        const shared_ptr<State> pState     = args.get<shared_ptr<State>>( 0 );
        const uint32            generation = args.get<uint32>( 1 );

        EditorBackgroundNoInput input;
        if ( readInput( pState, generation, input ) == false )
            return;

        EditorResourceCatalogCounts counts{};
        const StopToken             stopToken = readStopSignal( pState );
        EditorAssetIndex::collectCatalogCounts( *args.get<const AssetPaths*>( 2 ), counts, stopToken );
        if ( stopToken.isStopRequested() )
            return;

        publish( pState, generation, std::move( counts ) );
    }
} // namespace sw::editor
