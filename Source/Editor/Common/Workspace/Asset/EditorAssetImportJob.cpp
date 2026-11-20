#include "pch.h"

#include "Editor/Common/Workspace/Asset/EditorAssetImportJob.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

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

    namespace
    {
        struct EditorAssetImportJobInternal
        {
            /** @brief 원본 @p relativePath 가 든 폴더의 비교 열쇠(소문자 · '/' 구분 · 끝에 '/')입니다. 조상 폴더 판정이 접두로 된다. */
            static string makeFolderKey( string_view relativePath )
            {
                string folder = StringUtil::toLower( FileUtil::normalizeSeparators( FileUtil::getDirectoryPart( relativePath ) ).c_str() );
                folder.push_back( '/' );
                return folder;
            }

            /** @brief 폴더 묶음을 나눠 도는 본체입니다(`runParallel` 의 청크 — 묶음 번호를 하나씩 가져간다). */
            struct GroupRun
            {
                const EditorAssetImportInput* _pInput{ nullptr };
                const EditorAssetImportPlan*  _pPlan{ nullptr };
                EditorAssetImportResult*      _pResult{ nullptr };
                const atomic<bool>*           _pCancel{ nullptr };
                const StopToken*              _pStopToken{ nullptr };
                atomic<uint32>                _nextGroup{ 0 };

                void runItem( uint32 itemIndex ) const
                {
                    const EditorAssetImportItem& item       = _pInput->_listItem[itemIndex];
                    EditorAssetImportItemResult& itemResult = _pResult->_listItem[itemIndex]; // 번호마다 한 스레드만 쓴다
                    if ( _pCancel->load() || _pStopToken->isStopRequested() )
                        return; // 건너뛴 원본은 호출한 쪽이 다시 요청한다(`_bRan == false`)
                    itemResult._bRan     = true;
                    itemResult._bHandled = item._pfnImport( *_pInput->_pAssetPaths, item._relativePath );
                }

                void takeGroups( uint32 /*start*/, uint32 /*end*/ )
                {
                    const uint32 groupCount = static_cast<uint32>( _pPlan->_listParallelGroup.size() );
                    for ( ;; )
                    {
                        const uint32 group = _nextGroup.fetch_add( 1, std::memory_order_relaxed );
                        if ( group >= groupCount )
                            return;
                        for ( const uint32 itemIndex : _pPlan->_listParallelGroup[group] )
                            runItem( itemIndex );
                    }
                }
            };
        };
    } // namespace

    void EditorAssetImportJob::planGroups( const vector<EditorAssetImportItem>& listItem, EditorAssetImportPlan& outPlan )
    {
        outPlan._listParallelGroup.clear();
        outPlan._listSerial.clear();
        vector<string>                                       listGroupFolder; // 묶음마다 가장 위 폴더 열쇠
        vector<bool ( * )( const AssetPaths&, string_view )> listGroupImporter;
        for ( uint32 itemIndex = 0; itemIndex < static_cast<uint32>( listItem.size() ); ++itemIndex )
        {
            const EditorAssetImportItem& item = listItem[itemIndex];
            if ( item._thread != AssetImportThread::WorkerParallel )
            {
                outPlan._listSerial.push_back( itemIndex );
                continue;
            }
            const string folder = EditorAssetImportJobInternal::makeFolderKey( item._relativePath );
            // 조상 · 자손 관계의 묶음은 모두 이 원본과 한 묶음이 된다(새 원본이 두 묶음을 잇기도 한다).
            uint32 target = static_cast<uint32>( outPlan._listParallelGroup.size() );
            for ( uint32 group = 0; group < static_cast<uint32>( outPlan._listParallelGroup.size() ); ++group )
            {
                const string& groupFolder = listGroupFolder[group];
                const bool    bRelated    = StringUtil::startsWith( folder, groupFolder ) || StringUtil::startsWith( groupFolder, folder );
                if ( bRelated == false || listGroupImporter[group] != item._pfnImport )
                    continue;
                if ( target == static_cast<uint32>( outPlan._listParallelGroup.size() ) )
                {
                    target = group;
                    if ( groupFolder.size() > folder.size() )
                        listGroupFolder[group] = folder;
                    continue;
                }
                // 두 번째로 맞는 묶음은 첫 묶음에 합친다(입력 순서를 지킨다)
                vector<uint32>& merged = outPlan._listParallelGroup[target];
                merged.insert( merged.end(), outPlan._listParallelGroup[group].begin(), outPlan._listParallelGroup[group].end() );
                VectorUtil::sort( merged );
                if ( listGroupFolder[group].size() < listGroupFolder[target].size() )
                    listGroupFolder[target] = listGroupFolder[group];
                outPlan._listParallelGroup.erase( outPlan._listParallelGroup.begin() + static_cast<ptrdiff_t>( group ) );
                listGroupFolder.erase( listGroupFolder.begin() + static_cast<ptrdiff_t>( group ) );
                listGroupImporter.erase( listGroupImporter.begin() + static_cast<ptrdiff_t>( group ) );
                --group;
            }
            if ( target == static_cast<uint32>( outPlan._listParallelGroup.size() ) )
            {
                outPlan._listParallelGroup.emplace_back();
                listGroupFolder.push_back( folder );
                listGroupImporter.push_back( item._pfnImport );
            }
            outPlan._listParallelGroup[target].push_back( itemIndex );
        }
    }

    bool EditorAssetImportJob::request( vector<EditorAssetImportItem> listItem, const AssetPaths& assetPaths, TaskManager* pTaskManager )
    {
        if ( isPending() )
            return false;

        EditorAssetImportInput input;
        input._listItem     = std::move( listItem );
        input._pAssetPaths  = &assetPaths;
        input._pTaskManager = pTaskManager;
        _pCancel            = sw::make_shared<atomic<bool>>( false );

        const uint32 generation = beginRequest( std::move( input ) );
        submitJob( "EditorAssetImport",
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

        // 종료 신호는 사용자 취소와 같이 다룬다 — 도는 임포트는 끝내고 남은 원본은 건너뛴다(원본마다 임포트는 중간에 끊을 수 없다).
        const StopToken         stopToken = readStopSignal( pState );
        EditorAssetImportResult result;
        result._listItem.resize( input._listItem.size() );
        for ( size_t index = 0; index < input._listItem.size(); ++index )
            result._listItem[index]._relativePath = input._listItem[index]._relativePath;

        EditorAssetImportPlan plan;
        planGroups( input._listItem, plan );
        EditorAssetImportJobInternal::GroupRun run;
        run._pInput             = &input;
        run._pPlan              = &plan;
        run._pResult            = &result;
        run._pCancel            = pCancel.get();
        run._pStopToken         = &stopToken;
        const uint32 groupCount = static_cast<uint32>( plan._listParallelGroup.size() );
        if ( input._pTaskManager != nullptr && groupCount > 1 )
        {
            // 참여자(워커 + 이 스레드)마다 청크 하나 — 먼저 끝난 스레드가 남은 묶음을 이어서 가져간다. 텍스처 밴드 압축이 안에서 다시 `runParallel` 을 불러도 기다리는 스레드가 돕는다.
            const uint32 participantCount = MathUtil::min( groupCount, input._pTaskManager->getWorkerCount() + 1 );
            input._pTaskManager->runParallel( participantCount, 1, SW_DELEGATE_METHOD( ParallelBlockDelegate, &EditorAssetImportJobInternal::GroupRun::takeGroups, &run ) );
        }
        else
            run.takeGroups( 0, 1 );
        for ( const uint32 itemIndex : plan._listSerial )
            run.runItem( itemIndex );
        for ( const EditorAssetImportItemResult& itemResult : result._listItem )
            result._bCanceled = result._bCanceled || itemResult._bRan == false;

        publish( pState, generation, std::move( result ) );
    }
} // namespace sw::editor
