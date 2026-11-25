#include "pch.h"

#include "Editor/Common/Workspace/Asset/EditorAssetImportJob.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Asset/AssetImportStamp.h"

namespace sw::editor
{
    // ======================================================================
    // EditorAssetImportJob
    // ======================================================================

    EditorAssetImportJob::EditorAssetImportJob()
        : EditorBackgroundJob{}
        , _cancel{}
    {
    }

    vector<vector<uint32>> EditorAssetImportJob::planBatches( const vector<EditorAssetImportItem>& listItem )
    {
        vector<vector<uint32>>               listBatch;
        vector<AssetSourceBatchImporterFunc> listImporter;
        for ( uint32 itemIndex = 0; itemIndex < static_cast<uint32>( listItem.size() ); ++itemIndex )
        {
            const optional<size_t> found = VectorUtil::findIndex( listImporter, listItem[itemIndex]._pfnImport );
            if ( found.has_value() == false )
            {
                listImporter.push_back( listItem[itemIndex]._pfnImport );
                listBatch.emplace_back();
            }
            listBatch[found.value_or( listBatch.size() - 1 )].push_back( itemIndex );
        }
        return listBatch;
    }

    bool EditorAssetImportJob::request( vector<EditorAssetImportItem> listItem, const AssetPaths& assetPaths, TaskManager* pTaskManager )
    {
        if ( isPending() )
            return false;

        EditorAssetImportInput input;
        input._listItem     = std::move( listItem );
        input._pAssetPaths  = &assetPaths;
        input._pTaskManager = pTaskManager;
        _cancel             = StopSource{};

        const uint32 generation = beginRequest( std::move( input ) );
        submitJob( "EditorAssetImport",
                   SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorAssetImportJob::runJob ),
                   MakeTaskArgs( _pState, generation, _cancel.getToken() ) );
        return true;
    }

    void EditorAssetImportJob::cancel()
    {
        _cancel.requestStop();
    }

    void EditorAssetImportJob::runJob( const TaskArgs& args )
    {
        const shared_ptr<State> pState     = args.get<shared_ptr<State>>( 0 );
        const uint32            generation = args.get<uint32>( 1 );
        const StopToken         cancel     = args.get<StopToken>( 2 );

        EditorAssetImportInput input;
        if ( readInput( pState, generation, input ) == false )
            return;

        // 종료 신호는 사용자 취소와 같이 다룬다 — 도는 임포트는 끝내고 남은 원본은 건너뛴다(원본마다 임포트는 중간에 끊을 수 없다).
        const StopToken         stopToken = readStopSignal( pState );
        EditorAssetImportResult result;
        result._listItem.resize( input._listItem.size() );
        for ( size_t index = 0; index < input._listItem.size(); ++index )
            result._listItem[index]._relativePath = input._listItem[index]._relativePath;

        for ( const vector<uint32>& listBatchItem : planBatches( input._listItem ) )
        {
            AssetSourceBatch batch;
            batch._pAssetPaths       = input._pAssetPaths;
            batch._pTaskManager      = input._pTaskManager;
            batch._control._cancel   = cancel;
            batch._control._shutdown = stopToken;
            for ( const uint32 itemIndex : listBatchItem )
                batch._listItem.push_back( AssetSourceBatchItem{ input._listItem[itemIndex]._relativePath } );
            if ( batch._control.isCanceled() == false )
                input._listItem[listBatchItem[0]]._pfnImport( batch );
            for ( size_t index = 0; index < listBatchItem.size(); ++index )
            {
                result._listItem[listBatchItem[index]]._bRan     = batch._listItem[index]._bRan; // 건너뛴 원본은 호출한 쪽이 다시 요청한다(`_bRan == false`)
                result._listItem[listBatchItem[index]]._bHandled = batch._listItem[index]._bHandled;
            }
        }
        for ( const EditorAssetImportItemResult& itemResult : result._listItem )
            result._bCanceled = result._bCanceled || itemResult._bRan == false;

        publish( pState, generation, std::move( result ) );
    }
} // namespace sw::editor
