#include "pch.h"

#include "Core/CoreMinimal.h"
#include "Core/File/Mac/MacFileDialog.h"

#if defined( SW_PLATFORM_MACOS )

namespace sw
{
	bool MacFileDialog::open( const FileDialogParams& params, vector<string>& outPaths )
	{
		string cmd = "osascript -e 'choose file ";
		if ( params._type == FileDialogParams::Type::Save )
			cmd = "osascript -e 'choose file name ";
		if ( params._description.empty() == false )
			cmd += "with prompt \"" + params._description + "\" ";
		if ( params._bEnableMultiselect && params._type == FileDialogParams::Type::Open )
			cmd += "with multiple selections allowed ";
		cmd += "'";

		FILE* pipe = popen( cmd.c_str(), "r" );
		if ( pipe == nullptr )
			return false;

		utf8   buf[constant::kMaxBuffer1024];
		string output;
		while ( fgets( buf, sizeof( buf ), pipe ) != nullptr )
			output += buf;
		pclose( pipe );

		while ( output.empty() == false && ( output.back() == '\n' || output.back() == '\r' ) )
			output.pop_back();

		if ( output.empty() )
			return false;

		outPaths.push_back( FileUtil::normalizePath( output ) );
		return true;
	}
} // namespace sw

#endif
