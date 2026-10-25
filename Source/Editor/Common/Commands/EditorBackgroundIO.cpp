#include "pch.h"

#include "Editor/Common/Commands/EditorBackgroundIO.h"

#include "Core/File/FileUtil.h"

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
        FileUtil::collectFiles( input._folder, input._extension, listFile, input._bRecursive );

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

        // 읽지 못한 테이블은 경고가 남고 그 칸만 빈다 — 나머지는 그대로 보인다(저장은 그 파일을 덮지 않는다).
        LocalizationSheet sheet;
        (void)EditorDataTableCommands::loadLocalizationProject( projectPath, sheet ); // 읽지 못한 테이블은 경고로 남고 그 칸만 빈다

        publish( pState, generation, std::move( sheet ) );
    }

    // ======================================================================
    // EditorGameDataScanJob
    // ======================================================================

    void EditorGameDataScanJob::request()
    {
        const uint32 generation = beginRequest( {} );
        submitEditorBackgroundJob( "EditorGameDataScan",
                                   SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorGameDataScanJob::runJob ),
                                   MakeTaskArgs( _pState, generation ) );
    }

    void EditorGameDataScanJob::runJob( const TaskArgs& args )
    {
        const shared_ptr<State> pState     = args.get<shared_ptr<State>>( 0 );
        const uint32            generation = args.get<uint32>( 1 );

        EditorBackgroundNoInput input;
        if ( readInput( pState, generation, input ) == false )
            return;

        vector<GameDataFileEntry> listEntry;
        EditorDataTableCommands::collectGameDataFiles( listEntry );

        publish( pState, generation, std::move( listEntry ) );
    }

    // ======================================================================
    // EditorResourceIndexJob
    // ======================================================================

    void EditorResourceIndexJob::request()
    {
        const uint32 generation = beginRequest( {} );
        submitEditorBackgroundJob( "EditorResourceIndex",
                                   SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorResourceIndexJob::runJob ),
                                   MakeTaskArgs( _pState, generation ) );
    }

    void EditorResourceIndexJob::runJob( const TaskArgs& args )
    {
        const shared_ptr<State> pState     = args.get<shared_ptr<State>>( 0 );
        const uint32            generation = args.get<uint32>( 1 );

        EditorBackgroundNoInput input;
        if ( readInput( pState, generation, input ) == false )
            return;

        vector<EditorResourceIndexEntry> listEntry;
        EditorResourceIndex::collectEntries( listEntry );

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

        const uint32 generation = beginRequest( std::move( input ) );
        submitEditorBackgroundJob( "EditorFolderListing",
                                   SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorFolderListingJob::runJob ),
                                   MakeTaskArgs( _pState, generation ) );
    }

    void EditorFolderListingJob::runJob( const TaskArgs& args )
    {
        const shared_ptr<State> pState     = args.get<shared_ptr<State>>( 0 );
        const uint32            generation = args.get<uint32>( 1 );

        EditorFolderListingInput input;
        if ( readInput( pState, generation, input ) == false )
            return;

        vector<EditorFolderListingEntry> listEntry;
        EditorFolderListing::collectFolderListing( input._folder, listEntry, input._bRecursive );

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
        EditorReferenceIndex::scan( resourceRoot, data, pTasks );

        publish( pState, generation, std::move( data ) );
    }

    // ======================================================================
    // EditorResourceCatalogJob
    // ======================================================================

    void EditorResourceCatalogJob::request()
    {
        const uint32 generation = beginRequest( {} );
        submitEditorBackgroundJob( "EditorResourceCatalog",
                                   SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorResourceCatalogJob::runJob ),
                                   MakeTaskArgs( _pState, generation ) );
    }

    void EditorResourceCatalogJob::runJob( const TaskArgs& args )
    {
        const shared_ptr<State> pState     = args.get<shared_ptr<State>>( 0 );
        const uint32            generation = args.get<uint32>( 1 );

        EditorBackgroundNoInput input;
        if ( readInput( pState, generation, input ) == false )
            return;

        EditorResourceCatalogCounts counts{};
        EditorResourceIndex::collectCatalogCounts( counts );

        publish( pState, generation, std::move( counts ) );
    }
} // namespace sw::editor
