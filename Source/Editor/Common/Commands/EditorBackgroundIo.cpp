#include "pch.h"

#include "Editor/Common/Commands/EditorBackgroundIo.h"

#include "Core/File/FileUtil.h"
#include "Core/Task/TaskManager.h"

#include "RuntimeAPI/Service/EditorService.h"

namespace sw::editor
{
	namespace
	{
		TaskManager* getTaskManagerOrNull()
		{
			return editor::getService<TaskManager>();
		}

		void submitOrRun( string_view name, const TaskArgsDelegate& delegate, const TaskArgs& args )
		{
			TaskManager* pTaskManager = getTaskManagerOrNull();
			if ( pTaskManager == nullptr )
			{
				delegate( args );
				return;
			}

			TaskHandle handle = pTaskManager->emplaceTask( name, delegate, args );
			handle.submit();
		}
	} // namespace

	EditorFileCollectJob::EditorFileCollectJob()
		: _mutex{}
		, _folder{}
		, _extension{}
		, _listResult{}
		, _generation{ 0 }
		, _bRecursive{ SW_FALSE }
		, _bPending{ SW_FALSE }
		, _bReady{ SW_FALSE }
		, _reserved{ 0 }
	{
	}

	void EditorFileCollectJob::request( string_view folder, string_view extension, bool recursive )
	{
		uint32 generation = 0;
		{
			std::scoped_lock<mutex> lock{ _mutex };
			++_generation;
			_folder		= string{ folder };
			_extension	= string{ extension };
			_bRecursive = recursive ? SW_TRUE : SW_FALSE;
			_bPending	= SW_TRUE;
			_bReady		= SW_FALSE;
			_listResult.clear();
			generation = _generation;
		}

		submitOrRun( "EditorFileCollect",
					 SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorFileCollectJob::runJob ),
					 MakeTaskArgs( this, generation ) );
	}

	bool EditorFileCollectJob::take( vector<string>& outList )
	{
		std::scoped_lock<mutex> lock{ _mutex };
		if ( _bReady == SW_FALSE )
			return false;
		outList	  = std::move( _listResult );
		_bReady	  = SW_FALSE;
		_bPending = SW_FALSE;
		return true;
	}

	bool EditorFileCollectJob::isPending() const
	{
		std::scoped_lock<mutex> lock{ _mutex };
		return _bPending == SW_TRUE;
	}

	void EditorFileCollectJob::runJob( const TaskArgs& args )
	{
		EditorFileCollectJob* pJob = args.get<EditorFileCollectJob*>( 0 );
		const uint32		  gen  = args.get<uint32>( 1 );
		if ( pJob == nullptr )
			return;

		string folder;
		string extension;
		bool   bRecursive = false;
		{
			std::scoped_lock<mutex> lock{ pJob->_mutex };
			if ( gen != pJob->_generation )
				return;
			folder	   = pJob->_folder;
			extension  = pJob->_extension;
			bRecursive = ( pJob->_bRecursive == SW_TRUE );
		}

		vector<string> listFiles;
		FileUtil::collectFiles( folder, extension, listFiles, bRecursive, false );

		std::scoped_lock<mutex> lock{ pJob->_mutex };
		if ( gen != pJob->_generation )
			return;
		pJob->_listResult = std::move( listFiles );
		pJob->_bReady	  = SW_TRUE;
		pJob->_bPending	  = SW_FALSE;
	}

	EditorLocalizationLoadJob::EditorLocalizationLoadJob()
		: _mutex{}
		, _listResult{}
		, _generation{ 0 }
		, _bPending{ SW_FALSE }
		, _bReady{ SW_FALSE }
		, _reserved{ 0 }
	{
	}

	void EditorLocalizationLoadJob::request()
	{
		uint32 generation = 0;
		{
			std::scoped_lock<mutex> lock{ _mutex };
			++_generation;
			_bPending = SW_TRUE;
			_bReady	  = SW_FALSE;
			_listResult.clear();
			generation = _generation;
		}

		submitOrRun( "EditorLocalizationLoad",
					 SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorLocalizationLoadJob::runJob ),
					 MakeTaskArgs( this, generation ) );
	}

	bool EditorLocalizationLoadJob::take( vector<LocRecord>& outList )
	{
		std::scoped_lock<mutex> lock{ _mutex };
		if ( _bReady == SW_FALSE )
			return false;
		outList	  = std::move( _listResult );
		_bReady	  = SW_FALSE;
		_bPending = SW_FALSE;
		return true;
	}

	bool EditorLocalizationLoadJob::isPending() const
	{
		std::scoped_lock<mutex> lock{ _mutex };
		return _bPending == SW_TRUE;
	}

	void EditorLocalizationLoadJob::runJob( const TaskArgs& args )
	{
		EditorLocalizationLoadJob* pJob = args.get<EditorLocalizationLoadJob*>( 0 );
		const uint32			   gen	= args.get<uint32>( 1 );
		if ( pJob == nullptr )
			return;

		{
			std::scoped_lock<mutex> lock{ pJob->_mutex };
			if ( gen != pJob->_generation )
				return;
		}

		vector<LocRecord> listRecord;
		EditorDataTableCommands::loadLocalization( listRecord );

		std::scoped_lock<mutex> lock{ pJob->_mutex };
		if ( gen != pJob->_generation )
			return;
		pJob->_listResult = std::move( listRecord );
		pJob->_bReady	  = SW_TRUE;
		pJob->_bPending	  = SW_FALSE;
	}

	EditorGameDataScanJob::EditorGameDataScanJob()
		: _mutex{}
		, _listResult{}
		, _generation{ 0 }
		, _bPending{ SW_FALSE }
		, _bReady{ SW_FALSE }
		, _reserved{ 0 }
	{
	}

	void EditorGameDataScanJob::request()
	{
		uint32 generation = 0;
		{
			std::scoped_lock<mutex> lock{ _mutex };
			++_generation;
			_bPending = SW_TRUE;
			_bReady	  = SW_FALSE;
			_listResult.clear();
			generation = _generation;
		}

		submitOrRun( "EditorGameDataScan",
					 SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorGameDataScanJob::runJob ),
					 MakeTaskArgs( this, generation ) );
	}

	bool EditorGameDataScanJob::take( vector<GameDataFileEntry>& outList )
	{
		std::scoped_lock<mutex> lock{ _mutex };
		if ( _bReady == SW_FALSE )
			return false;
		outList	  = std::move( _listResult );
		_bReady	  = SW_FALSE;
		_bPending = SW_FALSE;
		return true;
	}

	bool EditorGameDataScanJob::isPending() const
	{
		std::scoped_lock<mutex> lock{ _mutex };
		return _bPending == SW_TRUE;
	}

	void EditorGameDataScanJob::runJob( const TaskArgs& args )
	{
		EditorGameDataScanJob* pJob = args.get<EditorGameDataScanJob*>( 0 );
		const uint32		   gen	= args.get<uint32>( 1 );
		if ( pJob == nullptr )
			return;

		{
			std::scoped_lock<mutex> lock{ pJob->_mutex };
			if ( gen != pJob->_generation )
				return;
		}

		vector<GameDataFileEntry> listEntry;
		EditorDataTableCommands::collectGameDataFiles( listEntry );

		std::scoped_lock<mutex> lock{ pJob->_mutex };
		if ( gen != pJob->_generation )
			return;
		pJob->_listResult = std::move( listEntry );
		pJob->_bReady	  = SW_TRUE;
		pJob->_bPending	  = SW_FALSE;
	}

	EditorResourceIndexJob::EditorResourceIndexJob()
		: _mutex{}
		, _listResult{}
		, _generation{ 0 }
		, _bPending{ SW_FALSE }
		, _bReady{ SW_FALSE }
		, _reserved{ 0 }
	{
	}

	void EditorResourceIndexJob::request()
	{
		uint32 generation = 0;
		{
			std::scoped_lock<mutex> lock{ _mutex };
			++_generation;
			_bPending = SW_TRUE;
			_bReady	  = SW_FALSE;
			_listResult.clear();
			generation = _generation;
		}

		submitOrRun( "EditorResourceIndex",
					 SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorResourceIndexJob::runJob ),
					 MakeTaskArgs( this, generation ) );
	}

	bool EditorResourceIndexJob::take( vector<EditorResourceIndexEntry>& outList )
	{
		std::scoped_lock<mutex> lock{ _mutex };
		if ( _bReady == SW_FALSE )
			return false;
		outList	  = std::move( _listResult );
		_bReady	  = SW_FALSE;
		_bPending = SW_FALSE;
		return true;
	}

	bool EditorResourceIndexJob::isPending() const
	{
		std::scoped_lock<mutex> lock{ _mutex };
		return _bPending == SW_TRUE;
	}

	void EditorResourceIndexJob::runJob( const TaskArgs& args )
	{
		EditorResourceIndexJob* pJob = args.get<EditorResourceIndexJob*>( 0 );
		const uint32			gen	 = args.get<uint32>( 1 );
		if ( pJob == nullptr )
			return;

		{
			std::scoped_lock<mutex> lock{ pJob->_mutex };
			if ( gen != pJob->_generation )
				return;
		}

		vector<EditorResourceIndexEntry> listEntry;
		EditorAssetCommands::collectResourceIndex( listEntry );

		std::scoped_lock<mutex> lock{ pJob->_mutex };
		if ( gen != pJob->_generation )
			return;
		pJob->_listResult = std::move( listEntry );
		pJob->_bReady	  = SW_TRUE;
		pJob->_bPending	  = SW_FALSE;
	}
} // namespace sw::editor
