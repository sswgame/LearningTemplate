#include "pch.h"

#include "Editor/Common/Workspace/EditorAssetImportJob.h"

#include "Core/Task/TaskManager.h"

#include "Editor/Common/Workspace/EditorService.h"

namespace sw::editor
{
    namespace
    {
        /** @brief TaskManager 가 없으면(테스트 · 초기화 전) 같은 스레드에서 바로 돌립니다. `EditorBackgroundIO.cpp` 의 같은 이름과 같다(위 층이라 include 하지 못한다). */
        void submitOrRun( string_view name, const TaskArgsDelegate& delegate, const TaskArgs& args )
        {
            TaskManager* pTaskManager = editor::findSubsystem<TaskManager>();
            if ( pTaskManager == nullptr )
            {
                delegate( args );
                return;
            }

            TaskHandle handle = pTaskManager->emplaceTask( name, delegate, args );
            handle.submit();
        }
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    // ======================================================================
    // EditorAssetImportJob
    // ======================================================================

    EditorAssetImportJob::EditorAssetImportJob()
        : EditorBackgroundTask{}
        , _pCancel{ sw::make_shared<atomic<bool>>( false ) }
    {
    }

    bool EditorAssetImportJob::request( vector<EditorAssetImportItem> listItem )
    {
        if ( isPending() )
            return false;

        EditorAssetImportInput input;
        input._listItem = std::move( listItem );
        _pCancel        = sw::make_shared<atomic<bool>>( false );

        const uint32 generation = beginRequest( std::move( input ) );
        submitOrRun( "EditorAssetImport",
                     SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorAssetImportJob::runJob ),
                     MakeTaskArgs( _pState, generation, _pCancel ) );
        return true;
    }

    void EditorAssetImportJob::cancel()
    {
        _pCancel->store( true );
    }

    void EditorAssetImportJob::runJob( const TaskArgs& args )
    {
        const shared_ptr<State>        pState     = args.get<shared_ptr<State>>( 0 );
        const uint32                   generation = args.get<uint32>( 1 );
        const shared_ptr<atomic<bool>> pCancel    = args.get<shared_ptr<atomic<bool>>>( 2 );

        EditorAssetImportInput input;
        if ( readInput( pState, generation, input ) == false )
            return;

        EditorAssetImportResult result;
        result._listItem.reserve( input._listItem.size() );
        for ( const EditorAssetImportItem& item : input._listItem )
        {
            EditorAssetImportItemResult& itemResult = result._listItem.emplace_back();
            itemResult._relativePath                = item._relativePath;
            if ( pCancel->load() )
            {
                result._bCanceled = true; // 건너뛴 원본은 호출한 쪽이 다시 요청한다
                continue;
            }
            itemResult._bRan     = true;
            itemResult._bHandled = item._pfnImport( item._relativePath );
        }

        publish( pState, generation, std::move( result ) );
    }
} // namespace sw::editor
