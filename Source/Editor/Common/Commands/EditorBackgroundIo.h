/**
 * @file EditorBackgroundIo.h
 * @brief 에디터 파일 스캔/로컬라이즈 로드를 TaskManager 워커에서 수행하는 잡
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Commands/EditorDataTableCommands.h"

namespace sw::editor
{
	/**
	 * @class EditorFileCollectJob
	 * @brief 폴더 파일 목록을 워커에서 모으고 게임 스레드에서 꺼냅니다.
	 */
	class EditorFileCollectJob
	{
	public:
		/** @brief 빈 잡을 만듭니다. */
		EditorFileCollectJob();

		/** @brief 워커에 폴더 스캔을 요청합니다. 이미 대기 중이면 세대를 올립니다. */
		void request( string_view folder, string_view extension, bool recursive );
		/** @brief 완료된 경로 목록을 가져옵니다. 새 결과가 있으면 true입니다. */
		bool take( vector<string>& outList );
		/** @brief 워커가 아직 돌고 있으면 true입니다. */
		bool isPending() const;

	private:
		static void runJob( const TaskArgs& args );

	private:
		mutable mutex		   _mutex;
		string				   _folder;
		string				   _extension;
		vector<string>		   _listResult;
		uint32				   _generation;
		uint8				   _bRecursive : 1;
		uint8				   _bPending   : 1;
		uint8				   _bReady	   : 1;
		[[maybe_unused]] uint8 _reserved   : 5;
	};

	/**
	 * @class EditorLocalizationLoadJob
	 * @brief 로컬라이즈 JSON을 워커에서 읽고 게임 스레드에서 적용합니다.
	 */
	class EditorLocalizationLoadJob
	{
	public:
		/** @brief 빈 잡을 만듭니다. */
		EditorLocalizationLoadJob();

		/** @brief 워커에 JSON 로드를 요청합니다. */
		void request();
		/** @brief 완료된 레코드를 가져옵니다. */
		bool take( vector<LocRecord>& outList );
		/** @brief 워커가 아직 돌고 있으면 true입니다. */
		bool isPending() const;

	private:
		static void runJob( const TaskArgs& args );

	private:
		mutable mutex		   _mutex;
		vector<LocRecord>	   _listResult;
		uint32				   _generation;
		uint8				   _bPending : 1;
		uint8				   _bReady	 : 1;
		[[maybe_unused]] uint8 _reserved : 6;
	};

	/**
	 * @class EditorGameDataScanJob
	 * @brief 게임 데이터 XML 파일 목록을 워커에서 모읍니다.
	 */
	class EditorGameDataScanJob
	{
	public:
		/** @brief 빈 잡을 만듭니다. */
		EditorGameDataScanJob();

		/** @brief 워커에 XML 목록 스캔을 요청합니다. */
		void request();
		/** @brief 완료된 항목을 가져옵니다. */
		bool take( vector<GameDataFileEntry>& outList );
		/** @brief 워커가 아직 돌고 있으면 true입니다. */
		bool isPending() const;

	private:
		static void runJob( const TaskArgs& args );

	private:
		mutable mutex			  _mutex;
		vector<GameDataFileEntry> _listResult;
		uint32					  _generation;
		uint8					  _bPending : 1;
		uint8					  _bReady	: 1;
		[[maybe_unused]] uint8	  _reserved : 6;
	};

	/**
	 * @class EditorResourceIndexJob
	 * @brief Resource 트리 분류 인덱스를 워커에서 만듭니다.
	 */
	class EditorResourceIndexJob
	{
	public:
		/** @brief 빈 잡을 만듭니다. */
		EditorResourceIndexJob();

		/** @brief 워커에 Resource 스캔을 요청합니다. */
		void request();
		/** @brief 완료된 인덱스를 가져옵니다. */
		bool take( vector<EditorResourceIndexEntry>& outList );
		/** @brief 워커가 아직 돌고 있으면 true입니다. */
		bool isPending() const;

	private:
		static void runJob( const TaskArgs& args );

	private:
		mutable mutex					 _mutex;
		vector<EditorResourceIndexEntry> _listResult;
		uint32							 _generation;
		uint8							 _bPending : 1;
		uint8							 _bReady   : 1;
		[[maybe_unused]] uint8			 _reserved : 6;
	};
} // namespace sw::editor
