/**
 * @file LinuxFileDialog.cpp
 * @brief zenity / kdialog / yad 기반 Linux 파일 다이얼로그
 */
#include "pch.h"
#include "LinuxFileDialog.h"

#if defined( SW_PLATFORM_LINUX )
	#include "Core/Utility/Log/Logger.h"
	#include "Core/Utility/File/FileUtil.h"

	#include <sys/wait.h>
	#include <unistd.h>
	#include <cstdlib>
	#include <cstring>

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

		std::string shellQuote( const std::string& value )
		{
			std::string quoted;
			quoted.reserve( value.size() + 2 );
			quoted.push_back( '\'' );
			for ( const char c : value )
			{
				if ( c == '\'' )
					quoted.append( "'\\''" );
				else
					quoted.push_back( c );
			}
			quoted.push_back( '\'' );
			return quoted;
		}

		bool isExecutableFile( const std::string& path )
		{
			return access( path.c_str(), X_OK ) == 0;
		}

		std::string findExecutable( const char* name )
		{
			if ( name == nullptr || name[0] == '\0' )
				return {};

			if ( std::strchr( name, '/' ) != nullptr )
				return isExecutableFile( name ) ? std::string{ name } : std::string{};

			const char* pathEnv = std::getenv( "PATH" );
			if ( pathEnv == nullptr || pathEnv[0] == '\0' )
				return {};

			const std::string pathList{ pathEnv };
			size_t			  start = 0;
			while ( start <= pathList.size() )
			{
				size_t		end	  = pathList.find( ':', start );
				std::string dir	  = pathList.substr( start, ( end == std::string::npos ) ? std::string::npos : end - start );
				if ( dir.empty() )
					dir = ".";

				std::string candidate = dir;
				if ( candidate.back() != '/' )
					candidate.push_back( '/' );
				candidate.append( name );

				if ( isExecutableFile( candidate ) )
					return candidate;

				if ( end == std::string::npos )
					break;
				start = end + 1;
			}
			return {};
		}

		DialogBackend resolveBackend( std::string& outToolPath )
		{
			struct Candidate
			{
				const char*	  name;
				DialogBackend backend;
			};

			static constexpr Candidate kCandidates[] = {
				{ "zenity", DialogBackend::Zenity },
				{ "qarma", DialogBackend::Zenity },		 // zenity-compatible
				{ "matedialog", DialogBackend::Zenity }, // zenity-compatible
				{ "kdialog", DialogBackend::KDialog },
				{ "yad", DialogBackend::Yad },
			};

			for ( const Candidate& candidate : kCandidates )
			{
				outToolPath = findExecutable( candidate.name );
				if ( outToolPath.empty() == false )
					return candidate.backend;
			}

			outToolPath.clear();
			return DialogBackend::None;
		}

		std::string makeGlobPattern( const std::string& extension )
		{
			std::string pattern = "*";
			if ( extension.empty() == false )
			{
				if ( extension[0] != '.' )
					pattern.push_back( '.' );
				pattern.append( extension );
			}
			return pattern;
		}

		std::string makeCombinedGlobList( const std::vector<std::string>& extensions )
		{
			if ( extensions.empty() )
				return "*";

			std::string combined;
			for ( const std::string& ext : extensions )
			{
				if ( combined.empty() == false )
					combined.push_back( ' ' );
				combined.append( makeGlobPattern( ext ) );
			}
			return combined;
		}

		std::string dialogTitle( const FileDialogParams& params )
		{
			if ( params._title.empty() == false )
				return params._title;
			if ( params._description.empty() == false )
				return params._description;
			return ( params._type == FileDialogParams::Type::Save ) ? "Save File" : "Open File";
		}

		std::string runCommandCapture( const std::string& command, int& outExitCode )
		{
			outExitCode = -1;
			FILE* pipe	= popen( command.c_str(), "r" );
			if ( pipe == nullptr )
				return {};

			char		buffer[4096];
			std::string output;
			while ( fgets( buffer, sizeof( buffer ), pipe ) != nullptr )
				output += buffer;

			const int status = pclose( pipe );
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

		void splitPaths( const std::string& output, char separator, std::vector<std::string>& outPaths )
		{
			size_t start = 0;
			while ( start < output.size() )
			{
				const size_t sep = output.find( separator, start );
				const size_t end = ( sep == std::string::npos ) ? output.size() : sep;
				std::string	 part = output.substr( start, end - start );
				while ( part.empty() == false && ( part.back() == '\n' || part.back() == '\r' ) )
					part.pop_back();
				if ( part.empty() == false )
					outPaths.push_back( FileUtil::normalizeSeparators( part ) );
				if ( sep == std::string::npos )
					break;
				start = sep + 1;
			}
		}

		bool openWithZenity( const std::string& toolPath, const FileDialogParams& params, std::vector<std::string>& outPaths )
		{
			std::string cmd = shellQuote( toolPath );
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
				std::string initial = FileUtil::normalizeSeparators( params._initialDirectory );
				if ( initial.empty() == false && initial.back() != '/' )
					initial.push_back( '/' );
				cmd += " --filename=";
				cmd += shellQuote( initial );
			}

			if ( params._filterExtensionList.empty() == false )
			{
				const std::string label	  = params._description.empty() ? "Files" : params._description;
				const std::string globs	  = makeCombinedGlobList( params._filterExtensionList );
				cmd += " --file-filter=";
				cmd += shellQuote( label + " | " + globs );
				cmd += " --file-filter=";
				cmd += shellQuote( std::string{ "All files | *" } );
			}

			int				 exitCode = -1;
			const std::string output   = runCommandCapture( cmd, exitCode );
			if ( exitCode != 0 || output.empty() )
				return false;

			if ( bMulti )
				splitPaths( output, '|', outPaths );
			else
				outPaths.push_back( FileUtil::normalizeSeparators( output ) );
			return outPaths.empty() == false;
		}

		bool openWithKDialog( const std::string& toolPath, const FileDialogParams& params, std::vector<std::string>& outPaths )
		{
			std::string cmd = shellQuote( toolPath );
			if ( params._type == FileDialogParams::Type::Save )
				cmd += " --getsavefilename";
			else
				cmd += " --getopenfilename";

			cmd += " --title ";
			cmd += shellQuote( dialogTitle( params ) );

			const bool bMulti = params._bEnableMultiselect && params._type == FileDialogParams::Type::Open;
			if ( bMulti )
				cmd += " --multiple --separate-output";

			std::string startDir = ".";
			if ( params._initialDirectory.empty() == false )
				startDir = FileUtil::normalizeSeparators( params._initialDirectory );
			cmd.push_back( ' ' );
			cmd += shellQuote( startDir );

			if ( params._filterExtensionList.empty() == false )
			{
				const std::string label = params._description.empty() ? "Files" : params._description;
				const std::string globs = makeCombinedGlobList( params._filterExtensionList );
				// kdialog filter: "Description (*.ext *.ext2)"
				cmd.push_back( ' ' );
				cmd += shellQuote( label + " (" + globs + ")" );
			}

			int				 exitCode = -1;
			const std::string output   = runCommandCapture( cmd, exitCode );
			if ( exitCode != 0 || output.empty() )
				return false;

			if ( bMulti )
				splitPaths( output, '\n', outPaths );
			else
				outPaths.push_back( FileUtil::normalizeSeparators( output ) );
			return outPaths.empty() == false;
		}

		bool openWithYad( const std::string& toolPath, const FileDialogParams& params, std::vector<std::string>& outPaths )
		{
			std::string cmd = shellQuote( toolPath );
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
				std::string initial = FileUtil::normalizeSeparators( params._initialDirectory );
				if ( initial.empty() == false && initial.back() != '/' )
					initial.push_back( '/' );
				cmd += " --filename=";
				cmd += shellQuote( initial );
			}

			if ( params._filterExtensionList.empty() == false )
			{
				const std::string label = params._description.empty() ? "Files" : params._description;
				const std::string globs = makeCombinedGlobList( params._filterExtensionList );
				cmd += " --file-filter=";
				cmd += shellQuote( label + " | " + globs );
			}

			int				 exitCode = -1;
			const std::string output   = runCommandCapture( cmd, exitCode );
			if ( exitCode != 0 || output.empty() )
				return false;

			if ( bMulti )
				splitPaths( output, '|', outPaths );
			else
				outPaths.push_back( FileUtil::normalizeSeparators( output ) );
			return outPaths.empty() == false;
		}
	} // namespace

	bool LinuxFileDialog::open( const FileDialogParams& params, std::vector<std::string>& outPaths )
	{
		outPaths.clear();

		std::string			toolPath;
		const DialogBackend backend = resolveBackend( toolPath );
		if ( backend == DialogBackend::None )
		{
			SW_LOG_WARNING(
				"[LinuxFileDialog] No file dialog tool found. Install one of: zenity, kdialog, yad "
				"(e.g. sudo apt install zenity)." );
			return false;
		}

		bool ok = false;
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

		return ok;
	}
} // namespace sw

#endif // SW_PLATFORM_LINUX
