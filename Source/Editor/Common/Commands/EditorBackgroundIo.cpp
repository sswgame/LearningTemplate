#include "pch.h"

#include "Editor/Common/Commands/EditorBackgroundIo.h"

#include "Core/File/FileUtil.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Workspace/EditorService.h"

namespace sw::editor
{
	namespace
	{
		struct EditorBackgroundIoInternal
		{
			static void submitOrRun( string_view name, const TaskArgsDelegate& delegate, const TaskArgs& args )
			{
				TaskManager* pTaskManager = editor::getService<TaskManager>();
				if ( pTaskManager == nullptr )
				{
					delegate( args );
					return;
				}

				TaskHandle handle = pTaskManager->emplaceTask( name, delegate, args );
				handle.submit();
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	struct EditorFileCollectJob::State
	{
		mutable mutex		   _mutex{};
		string				   _folder{};
		string				   _extension{};
		vector<string>		   _listResult{};
		uint32				   _generation{ 0 };
		uint8				   _bRecursive : 1 { SW_FALSE };
		uint8				   _bPending   : 1 { SW_FALSE };
		uint8				   _bReady	   : 1 { SW_FALSE };
		[[maybe_unused]] uint8 _reserved   : 5 { 0 };
	};

	EditorFileCollectJob::EditorFileCollectJob()
		: _pState{ sw::make_shared<State>() }
	{
	}

	void EditorFileCollectJob::request( string_view folder, string_view extension, bool recursive )
	{
		if ( _pState == nullptr )
			return;

		uint32 generation = 0;
		{
			std::scoped_lock<mutex> lock{ _pState->_mutex };
			++_pState->_generation;
			_pState->_folder	 = string{ folder };
			_pState->_extension	 = string{ extension };
			_pState->_bRecursive = recursive ? SW_TRUE : SW_FALSE;
			_pState->_bPending	 = SW_TRUE;
			_pState->_bReady	 = SW_FALSE;
			_pState->_listResult.clear();
			generation = _pState->_generation;
		}

		EditorBackgroundIoInternal::submitOrRun( "EditorFileCollect",
												 SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorFileCollectJob::runJob ),
												 MakeTaskArgs( _pState, generation ) );
	}

	bool EditorFileCollectJob::take( vector<string>& outList )
	{
		if ( _pState == nullptr )
			return false;

		std::scoped_lock<mutex> lock{ _pState->_mutex };
		if ( _pState->_bReady == SW_FALSE )
			return false;
		outList			   = std::move( _pState->_listResult );
		_pState->_bReady   = SW_FALSE;
		_pState->_bPending = SW_FALSE;
		return true;
	}

	bool EditorFileCollectJob::isPending() const
	{
		if ( _pState == nullptr )
			return false;

		std::scoped_lock<mutex> lock{ _pState->_mutex };
		return _pState->_bPending == SW_TRUE;
	}

	void EditorFileCollectJob::runJob( const TaskArgs& args )
	{
		shared_ptr<State> pState = args.get<shared_ptr<State>>( 0 );
		const uint32	  gen	 = args.get<uint32>( 1 );
		if ( pState == nullptr )
			return;

		string folder;
		string extension;
		bool   bRecursive = false;
		{
			std::scoped_lock<mutex> lock{ pState->_mutex };
			if ( gen != pState->_generation )
				return;
			folder	   = pState->_folder;
			extension  = pState->_extension;
			bRecursive = ( pState->_bRecursive == SW_TRUE );
		}

		vector<string> listFile;
		FileUtil::collectFiles( folder, extension, listFile, bRecursive, false );

		std::scoped_lock<mutex> lock{ pState->_mutex };
		if ( gen != pState->_generation )
			return;
		pState->_listResult = std::move( listFile );
		pState->_bReady		= SW_TRUE;
		pState->_bPending	= SW_FALSE;
	}

	struct EditorLocalizationLoadJob::State
	{
		mutable mutex		   _mutex{};
		vector<LocRecord>	   _listResult{};
		uint32				   _generation{ 0 };
		uint8				   _bPending : 1 { SW_FALSE };
		uint8				   _bReady	 : 1 { SW_FALSE };
		[[maybe_unused]] uint8 _reserved : 6 { 0 };
	};

	EditorLocalizationLoadJob::EditorLocalizationLoadJob()
		: _pState{ sw::make_shared<State>() }
	{
	}

	void EditorLocalizationLoadJob::request()
	{
		if ( _pState == nullptr )
			return;

		uint32 generation = 0;
		{
			std::scoped_lock<mutex> lock{ _pState->_mutex };
			++_pState->_generation;
			_pState->_bPending = SW_TRUE;
			_pState->_bReady   = SW_FALSE;
			_pState->_listResult.clear();
			generation = _pState->_generation;
		}

		EditorBackgroundIoInternal::submitOrRun( "EditorLocalizationLoad",
												 SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorLocalizationLoadJob::runJob ),
												 MakeTaskArgs( _pState, generation ) );
	}

	bool EditorLocalizationLoadJob::take( vector<LocRecord>& outList )
	{
		if ( _pState == nullptr )
			return false;

		std::scoped_lock<mutex> lock{ _pState->_mutex };
		if ( _pState->_bReady == SW_FALSE )
			return false;
		outList			   = std::move( _pState->_listResult );
		_pState->_bReady   = SW_FALSE;
		_pState->_bPending = SW_FALSE;
		return true;
	}

	bool EditorLocalizationLoadJob::isPending() const
	{
		if ( _pState == nullptr )
			return false;

		std::scoped_lock<mutex> lock{ _pState->_mutex };
		return _pState->_bPending == SW_TRUE;
	}

	void EditorLocalizationLoadJob::runJob( const TaskArgs& args )
	{
		shared_ptr<State> pState = args.get<shared_ptr<State>>( 0 );
		const uint32	  gen	 = args.get<uint32>( 1 );
		if ( pState == nullptr )
			return;

		{
			std::scoped_lock<mutex> lock{ pState->_mutex };
			if ( gen != pState->_generation )
				return;
		}

		vector<LocRecord> listRecord;
		EditorDataTableCommands::loadLocalization( listRecord );

		std::scoped_lock<mutex> lock{ pState->_mutex };
		if ( gen != pState->_generation )
			return;
		pState->_listResult = std::move( listRecord );
		pState->_bReady		= SW_TRUE;
		pState->_bPending	= SW_FALSE;
	}

	struct EditorGameDataScanJob::State
	{
		mutable mutex			  _mutex{};
		vector<GameDataFileEntry> _listResult{};
		uint32					  _generation{ 0 };
		uint8					  _bPending : 1 { SW_FALSE };
		uint8					  _bReady	: 1 { SW_FALSE };
		[[maybe_unused]] uint8	  _reserved : 6 { 0 };
	};

	EditorGameDataScanJob::EditorGameDataScanJob()
		: _pState{ sw::make_shared<State>() }
	{
	}

	void EditorGameDataScanJob::request()
	{
		if ( _pState == nullptr )
			return;

		uint32 generation = 0;
		{
			std::scoped_lock<mutex> lock{ _pState->_mutex };
			++_pState->_generation;
			_pState->_bPending = SW_TRUE;
			_pState->_bReady   = SW_FALSE;
			_pState->_listResult.clear();
			generation = _pState->_generation;
		}

		EditorBackgroundIoInternal::submitOrRun( "EditorGameDataScan",
												 SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorGameDataScanJob::runJob ),
												 MakeTaskArgs( _pState, generation ) );
	}

	bool EditorGameDataScanJob::take( vector<GameDataFileEntry>& outList )
	{
		if ( _pState == nullptr )
			return false;

		std::scoped_lock<mutex> lock{ _pState->_mutex };
		if ( _pState->_bReady == SW_FALSE )
			return false;
		outList			   = std::move( _pState->_listResult );
		_pState->_bReady   = SW_FALSE;
		_pState->_bPending = SW_FALSE;
		return true;
	}

	bool EditorGameDataScanJob::isPending() const
	{
		if ( _pState == nullptr )
			return false;

		std::scoped_lock<mutex> lock{ _pState->_mutex };
		return _pState->_bPending == SW_TRUE;
	}

	void EditorGameDataScanJob::runJob( const TaskArgs& args )
	{
		shared_ptr<State> pState = args.get<shared_ptr<State>>( 0 );
		const uint32	  gen	 = args.get<uint32>( 1 );
		if ( pState == nullptr )
			return;

		{
			std::scoped_lock<mutex> lock{ pState->_mutex };
			if ( gen != pState->_generation )
				return;
		}

		vector<GameDataFileEntry> listEntry;
		EditorDataTableCommands::collectGameDataFiles( listEntry );

		std::scoped_lock<mutex> lock{ pState->_mutex };
		if ( gen != pState->_generation )
			return;
		pState->_listResult = std::move( listEntry );
		pState->_bReady		= SW_TRUE;
		pState->_bPending	= SW_FALSE;
	}

	struct EditorResourceIndexJob::State
	{
		mutable mutex					 _mutex{};
		vector<EditorResourceIndexEntry> _listResult{};
		uint32							 _generation{ 0 };
		uint8							 _bPending : 1 { SW_FALSE };
		uint8							 _bReady   : 1 { SW_FALSE };
		[[maybe_unused]] uint8			 _reserved : 6 { 0 };
	};

	EditorResourceIndexJob::EditorResourceIndexJob()
		: _pState{ sw::make_shared<State>() }
	{
	}

	void EditorResourceIndexJob::request()
	{
		if ( _pState == nullptr )
			return;

		uint32 generation = 0;
		{
			std::scoped_lock<mutex> lock{ _pState->_mutex };
			++_pState->_generation;
			_pState->_bPending = SW_TRUE;
			_pState->_bReady   = SW_FALSE;
			_pState->_listResult.clear();
			generation = _pState->_generation;
		}

		EditorBackgroundIoInternal::submitOrRun( "EditorResourceIndex",
												 SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorResourceIndexJob::runJob ),
												 MakeTaskArgs( _pState, generation ) );
	}

	bool EditorResourceIndexJob::take( vector<EditorResourceIndexEntry>& outList )
	{
		if ( _pState == nullptr )
			return false;

		std::scoped_lock<mutex> lock{ _pState->_mutex };
		if ( _pState->_bReady == SW_FALSE )
			return false;
		outList			   = std::move( _pState->_listResult );
		_pState->_bReady   = SW_FALSE;
		_pState->_bPending = SW_FALSE;
		return true;
	}

	bool EditorResourceIndexJob::isPending() const
	{
		if ( _pState == nullptr )
			return false;

		std::scoped_lock<mutex> lock{ _pState->_mutex };
		return _pState->_bPending == SW_TRUE;
	}

	void EditorResourceIndexJob::runJob( const TaskArgs& args )
	{
		shared_ptr<State> pState = args.get<shared_ptr<State>>( 0 );
		const uint32	  gen	 = args.get<uint32>( 1 );
		if ( pState == nullptr )
			return;

		{
			std::scoped_lock<mutex> lock{ pState->_mutex };
			if ( gen != pState->_generation )
				return;
		}

		vector<EditorResourceIndexEntry> listEntry;
		EditorAssetCommands::collectResourceIndex( listEntry );

		std::scoped_lock<mutex> lock{ pState->_mutex };
		if ( gen != pState->_generation )
			return;
		pState->_listResult = std::move( listEntry );
		pState->_bReady		= SW_TRUE;
		pState->_bPending	= SW_FALSE;
	}

	struct EditorFolderListingJob::State
	{
		mutable mutex					 _mutex{};
		string							 _folder{};
		vector<EditorFolderListingEntry> _listResult{};
		uint32							 _generation{ 0 };
		uint8							 _bPending : 1 { SW_FALSE };
		uint8							 _bReady   : 1 { SW_FALSE };
		[[maybe_unused]] uint8			 _reserved : 6 { 0 };
	};

	EditorFolderListingJob::EditorFolderListingJob()
		: _pState{ sw::make_shared<State>() }
	{
	}

	void EditorFolderListingJob::request( string_view folderAbs )
	{
		if ( _pState == nullptr )
			return;

		uint32 generation = 0;
		{
			std::scoped_lock<mutex> lock{ _pState->_mutex };
			++_pState->_generation;
			_pState->_folder   = string{ folderAbs };
			_pState->_bPending = SW_TRUE;
			_pState->_bReady   = SW_FALSE;
			_pState->_listResult.clear();
			generation = _pState->_generation;
		}

		EditorBackgroundIoInternal::submitOrRun( "EditorFolderListing",
												 SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorFolderListingJob::runJob ),
												 MakeTaskArgs( _pState, generation ) );
	}

	bool EditorFolderListingJob::take( vector<EditorFolderListingEntry>& outList )
	{
		if ( _pState == nullptr )
			return false;

		std::scoped_lock<mutex> lock{ _pState->_mutex };
		if ( _pState->_bReady == SW_FALSE )
			return false;
		outList			   = std::move( _pState->_listResult );
		_pState->_bReady   = SW_FALSE;
		_pState->_bPending = SW_FALSE;
		return true;
	}

	bool EditorFolderListingJob::isPending() const
	{
		if ( _pState == nullptr )
			return false;

		std::scoped_lock<mutex> lock{ _pState->_mutex };
		return _pState->_bPending == SW_TRUE;
	}

	void EditorFolderListingJob::runJob( const TaskArgs& args )
	{
		shared_ptr<State> pState = args.get<shared_ptr<State>>( 0 );
		const uint32	  gen	 = args.get<uint32>( 1 );
		if ( pState == nullptr )
			return;

		string folder;
		{
			std::scoped_lock<mutex> lock{ pState->_mutex };
			if ( gen != pState->_generation )
				return;
			folder = pState->_folder;
		}

		vector<EditorFolderListingEntry> listEntry;
		EditorAssetCommands::collectFolderListing( folder, listEntry );

		std::scoped_lock<mutex> lock{ pState->_mutex };
		if ( gen != pState->_generation )
			return;
		pState->_listResult = std::move( listEntry );
		pState->_bReady		= SW_TRUE;
		pState->_bPending	= SW_FALSE;
	}

	struct EditorResourceCatalogJob::State
	{
		mutable mutex				_mutex{};
		EditorResourceCatalogCounts _counts{};
		uint32						_generation{ 0 };
		uint8						_bPending : 1 { SW_FALSE };
		uint8						_bReady	  : 1 { SW_FALSE };
		[[maybe_unused]] uint8		_reserved : 6 { 0 };
	};

	EditorResourceCatalogJob::EditorResourceCatalogJob()
		: _pState{ sw::make_shared<State>() }
	{
	}

	void EditorResourceCatalogJob::request()
	{
		if ( _pState == nullptr )
			return;

		uint32 generation = 0;
		{
			std::scoped_lock<mutex> lock{ _pState->_mutex };
			++_pState->_generation;
			_pState->_bPending = SW_TRUE;
			_pState->_bReady   = SW_FALSE;
			_pState->_counts   = {};
			generation		   = _pState->_generation;
		}

		EditorBackgroundIoInternal::submitOrRun( "EditorResourceCatalog",
												 SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorResourceCatalogJob::runJob ),
												 MakeTaskArgs( _pState, generation ) );
	}

	bool EditorResourceCatalogJob::take( EditorResourceCatalogCounts& outCounts )
	{
		if ( _pState == nullptr )
			return false;

		std::scoped_lock<mutex> lock{ _pState->_mutex };
		if ( _pState->_bReady == SW_FALSE )
			return false;
		outCounts		   = _pState->_counts;
		_pState->_bReady   = SW_FALSE;
		_pState->_bPending = SW_FALSE;
		return true;
	}

	bool EditorResourceCatalogJob::isPending() const
	{
		if ( _pState == nullptr )
			return false;

		std::scoped_lock<mutex> lock{ _pState->_mutex };
		return _pState->_bPending == SW_TRUE;
	}

	void EditorResourceCatalogJob::runJob( const TaskArgs& args )
	{
		shared_ptr<State> pState = args.get<shared_ptr<State>>( 0 );
		const uint32	  gen	 = args.get<uint32>( 1 );
		if ( pState == nullptr )
			return;

		{
			std::scoped_lock<mutex> lock{ pState->_mutex };
			if ( gen != pState->_generation )
				return;
		}

		EditorResourceCatalogCounts counts{};
		EditorAssetCommands::collectResourceCatalogCounts( counts );

		std::scoped_lock<mutex> lock{ pState->_mutex };
		if ( gen != pState->_generation )
			return;
		pState->_counts	  = counts;
		pState->_bReady	  = SW_TRUE;
		pState->_bPending = SW_FALSE;
	}
} // namespace sw::editor
