#include "pch.h"

#include "Core/File/Linux/LinuxFileDialog.h"

#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#if defined( SW_PLATFORM_LINUX )
	#include <sys/wait.h>
	#include <unistd.h>

namespace sw
{
	namespace
	{

		enum class DialogBackend : uint8
		{
			None,
			Zenity,
			KDialog,
			Yad,
		};

		/** @brief 쉘 명령 인자를 안전하게 감싸기 위해 홑따옴표로 이스케이프 처리합니다. */
		string shellQuote( string_view value )
		{
			string quoted;
			quoted.reserve( value.size() + 2 );
			quoted.push_back( '\'' );
			for ( const utf8 ch : value )
			{
				if ( ch == '\'' )
					quoted.append( "'\\''" );
				else
					quoted.push_back( ch );
			}
			quoted.push_back( '\'' );
			return quoted;
		}

		/** @brief 지정된 경로의 파일이 실행 가능한지 여부를 반환합니다. */
		bool isExecutableFile( string_view path )
		{
			const string pathNt( path );
			return access( pathNt.c_str(), X_OK ) == 0;
		}

		/** @brief PATH 환경변수를 탐색하여 주어진 이름의 실행 파일 경로를 찾습니다. */
		string findExecutable( const utf8* name )
		{
			if ( name == nullptr || name[0] == '\0' )
				return {};

			if ( std::strchr( name, '/' ) != nullptr )
				return isExecutableFile( name ) ? string{ name } : string{};

			const utf8* pathEnv = std::getenv( "PATH" );
			if ( pathEnv == nullptr || pathEnv[0] == '\0' )
				return {};

			const string pathList{ pathEnv };
			size_t		 start{ 0 };
			while ( start <= pathList.size() )
			{
				size_t end = pathList.find( ':', start );
				string dir = pathList.substr( start, ( end == string::npos ) ? string::npos : end - start );
				if ( dir.empty() )
					dir = ".";

				string candidate = dir;
				if ( candidate.back() != '/' )
					candidate.push_back( '/' );
				candidate.append( name );

				if ( isExecutableFile( candidate ) )
					return candidate;

				if ( end == string::npos )
					break;
				start = end + 1;
			}
			return {};
		}

		/** @brief 사용 가능한 다이얼로그 툴(zenity, kdialog 등)을 찾아 해당 백엔드를 반환합니다. */
		DialogBackend resolveBackend( string& outToolPath )
		{
			struct Candidate
			{
				const utf8*	  name;
				DialogBackend backend;
			};

			static constexpr Candidate kArrCandidates[] = {
				{	  "zenity",	DialogBackend::Zenity},
				{	  "qarma",  DialogBackend::Zenity}, // zenity-compatible
				{"matedialog",	DialogBackend::Zenity}, // zenity-compatible
				{	  "kdialog", DialogBackend::KDialog},
				{		  "yad",	 DialogBackend::Yad},
			};

			for ( const Candidate& candidate : kArrCandidates )
			{
				outToolPath = findExecutable( candidate.name );
				if ( outToolPath.empty() == false )
					return candidate.backend;
			}

			outToolPath.clear();
			return DialogBackend::None;
		}

		/** @brief 단일 확장자에 대한 Glob 패턴(예: *.txt)을 생성합니다. */
		string makeGlobPattern( string_view extension )
		{
			string pattern = "*";
			if ( extension.empty() == false )
			{
				if ( extension[0] != '.' )
					pattern.push_back( '.' );
				pattern.append( extension );
			}
			return pattern;
		}

		/** @brief 여러 확장자에 대한 조합된 Glob 리스트 문자열을 생성합니다. */
		string makeCombinedGlobList( const vector<string>& extensions )
		{
			if ( extensions.empty() )
				return "*";

			string combined;
			for ( const string& ext : extensions )
			{
				if ( combined.empty() == false )
					combined.push_back( ' ' );
				combined.append( makeGlobPattern( ext ) );
			}
			return combined;
		}

		/** @brief 다이얼로그에 표시될 제목을 결정합니다. */
		string dialogTitle( const FileDialogParams& params )
		{
			if ( params._title.empty() == false )
				return params._title;
			if ( params._description.empty() == false )
				return params._description;
			return ( params._type == FileDialogParams::Type::Save ) ? "Save File" : "Open File";
		}

		/** @brief 쉘 명령을 실행하고 표준 출력(stdout) 결과 문자열을 캡처하여 반환합니다. */
		string runCommandCapture( string_view command, int32& outExitCode )
		{
			outExitCode = -1;
			const string commandNt( command );
			FILE*		 pipe = popen( commandNt.c_str(), "r" );
			if ( pipe == nullptr )
				return {};

			utf8   arrBuffer[constant::kMaxBuffer4096];
			string output;
			while ( fgets( arrBuffer, sizeof( arrBuffer ), pipe ) != nullptr )
				output += arrBuffer;

			const int32 status = pclose( pipe );
			if ( status == -1 )
				outExitCode = -1;
			else if ( WIFEXITED( status ) )
				outExitCode = WEXITSTATUS( status );
			else
				outExitCode = -1;

			while ( output.empty() == false && ( output.back() == '\n' || output.back() == '\r' ) )
				output.pop_back();
			return output;
		}

		/** @brief 다중 파일 선택 결과를 구분자(separator) 기준으로 분리하여 배열에 담습니다. */
		void splitPaths( string_view output, utf8 separator, vector<string>& outPaths )
		{
			size_t start{ 0 };
			while ( start < output.size() )
			{
				const size_t sep  = output.find( separator, start );
				const size_t end  = ( sep == string::npos ) ? output.size() : sep;
				string		 part = output.substr( start, end - start );
				while ( part.empty() == false && ( part.back() == '\n' || part.back() == '\r' ) )
					part.pop_back();
				if ( part.empty() == false )
					outPaths.push_back( FileUtil::normalizeSeparators( part ) );
				if ( sep == string::npos )
					break;
				start = sep + 1;
			}
		}

		/** @brief Zenity (또는 호환) 도구를 사용하여 다이얼로그를 엽니다. */
		bool openWithZenity( string_view toolPath, const FileDialogParams& params, vector<string>& outPaths )
		{
			string cmd = shellQuote( toolPath );
			cmd += " --file-selection";

			if ( params._type == FileDialogParams::Type::Save )
				cmd += " --save --confirm-overwrite";

			cmd += " --title=";
			cmd += shellQuote( dialogTitle( params ) );

			const bool bMulti = params._bEnableMultiselect && params._type == FileDialogParams::Type::Open;
			if ( bMulti )
				cmd += " --multiple --separator='|'";

			if ( params._initialDirectory.empty() == false )
			{
				string initial = FileUtil::normalizeSeparators( params._initialDirectory );
				if ( initial.empty() == false && initial.back() != '/' )
					initial.push_back( '/' );
				cmd += " --filename=";
				cmd += shellQuote( initial );
			}

			if ( params._filterExtensionList.empty() == false )
			{
				const string label = params._description.empty() ? "Files" : params._description;
				const string globs = makeCombinedGlobList( params._filterExtensionList );
				cmd += " --file-filter=";
				cmd += shellQuote( label + " | " + globs );
				cmd += " --file-filter=";
				cmd += shellQuote( string{ "All files | *" } );
			}

			int32		 exitCode = -1;
			const string output	  = runCommandCapture( cmd, exitCode );
			if ( exitCode != 0 || output.empty() )
				return false;

			if ( bMulti )
				splitPaths( output, '|', outPaths );
			else
				outPaths.push_back( FileUtil::normalizeSeparators( output ) );
			return outPaths.empty() == false;
		}

		/** @brief KDialog 도구를 사용하여 다이얼로그를 엽니다. */
		bool openWithKDialog( string_view toolPath, const FileDialogParams& params, vector<string>& outPaths )
		{
			string cmd = shellQuote( toolPath );
			if ( params._type == FileDialogParams::Type::Save )
				cmd += " --getsavefilename";
			else
				cmd += " --getopenfilename";

			cmd += " --title ";
			cmd += shellQuote( dialogTitle( params ) );

			const bool bMulti = params._bEnableMultiselect && params._type == FileDialogParams::Type::Open;
			if ( bMulti )
				cmd += " --multiple --separate-output";

			string startDir = ".";
			if ( params._initialDirectory.empty() == false )
				startDir = FileUtil::normalizeSeparators( params._initialDirectory );
			cmd.push_back( ' ' );
			cmd += shellQuote( startDir );

			if ( params._filterExtensionList.empty() == false )
			{
				const string label = params._description.empty() ? "Files" : params._description;
				const string globs = makeCombinedGlobList( params._filterExtensionList );
				// kdialog filter: "Description (*.ext *.ext2)"
				cmd.push_back( ' ' );
				cmd += shellQuote( label + " (" + globs + ")" );
			}

			int32		 exitCode = -1;
			const string output	  = runCommandCapture( cmd, exitCode );
			if ( exitCode != 0 || output.empty() )
				return false;

			if ( bMulti )
				splitPaths( output, '\n', outPaths );
			else
				outPaths.push_back( FileUtil::normalizeSeparators( output ) );
			return outPaths.empty() == false;
		}

		/** @brief Yad 도구를 사용하여 다이얼로그를 엽니다. */
		bool openWithYad( string_view toolPath, const FileDialogParams& params, vector<string>& outPaths )
		{
			string cmd = shellQuote( toolPath );
			cmd += " --file";

			if ( params._type == FileDialogParams::Type::Save )
				cmd += " --save --confirm-overwrite";

			cmd += " --title=";
			cmd += shellQuote( dialogTitle( params ) );

			const bool bMulti = params._bEnableMultiselect && params._type == FileDialogParams::Type::Open;
			if ( bMulti )
				cmd += " --multiple --separator='|'";

			if ( params._initialDirectory.empty() == false )
			{
				string initial = FileUtil::normalizeSeparators( params._initialDirectory );
				if ( initial.empty() == false && initial.back() != '/' )
					initial.push_back( '/' );
				cmd += " --filename=";
				cmd += shellQuote( initial );
			}

			if ( params._filterExtensionList.empty() == false )
			{
				const string label = params._description.empty() ? "Files" : params._description;
				const string globs = makeCombinedGlobList( params._filterExtensionList );
				cmd += " --file-filter=";
				cmd += shellQuote( label + " | " + globs );
			}

			int32		 exitCode = -1;
			const string output	  = runCommandCapture( cmd, exitCode );
			if ( exitCode != 0 || output.empty() )
				return false;

			if ( bMulti )
				splitPaths( output, '|', outPaths );
			else
				outPaths.push_back( FileUtil::normalizeSeparators( output ) );
			return outPaths.empty() == false;
		}

	} // namespace

	bool LinuxFileDialog::open( const FileDialogParams& params, vector<string>& outPaths )
	{
		outPaths.clear();

		DialogBackend backend = DialogBackend::None;
		string		  toolPath;
		BLOCK( "Resolve Backend" )
		{
			backend = resolveBackend( toolPath );
			if ( backend == DialogBackend::None )
			{
				SW_LOG_WARNING(
					"[LinuxFileDialog] No file dialog tool found. Install one of: zenity, kdialog, yad "
					"(e.g. sudo apt install zenity)." );
				return false;
			}
		}

		bool ok{ false };
		BLOCK( "Execute Backend" )
		{
			switch ( backend )
			{
				case DialogBackend::Zenity:
					ok = openWithZenity( toolPath, params, outPaths );
					break;
				case DialogBackend::KDialog:
					ok = openWithKDialog( toolPath, params, outPaths );
					break;
				case DialogBackend::Yad:
					ok = openWithYad( toolPath, params, outPaths );
					break;
				default:
					break;
			}
		}

		return ok;
	}
} // namespace sw

#endif // SW_PLATFORM_LINUX
