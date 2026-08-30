#include "pch.h"

#include "Core/File/Mac/MacFileDialog.h"

#include "Core/CoreMinimal.h"

#if defined( SW_PLATFORM_MACOS )

namespace sw
{
	bool MacFileDialog::open( const FileDialogParams& params, vector<string>& outListPath )
	{
		string cmd = "osascript -e 'choose file ";
		if ( params._type == FileDialogParams::Type::Save )
			cmd = "osascript -e 'choose file name ";
		if ( params._description.empty() == false )
			cmd += "with prompt \"" + params._description + "\" ";
		if ( params._bEnableMultiselect && params._type == FileDialogParams::Type::Open )
			cmd += "with multiple selections allowed ";
		cmd += "'";

		FILE* pPipe = popen( cmd.c_str(), "r" );
		if ( pPipe == nullptr )
			return false;

		utf8   arrBuf[constant::kMaxBuffer1024];
		string output;
		while ( fgets( arrBuf, sizeof( arrBuf ), pPipe ) != nullptr )
			output += arrBuf;
		pclose( pPipe );

		while ( output.empty() == false && ( output.back() == '\n' || output.back() == '\r' ) )
			output.pop_back();

		if ( output.empty() )
			return false;

		outListPath.push_back( FileUtil::normalizePath( output ) );
		return true;
	}
} // namespace sw

#endif
