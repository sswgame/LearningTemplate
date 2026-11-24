#include "pch.h"

#include "Core/Process/EngineChildProcess.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/StringBuilder.h"

namespace sw
{
    ProcessCommand EngineChildProcess::makeCommand( const CommandLineManager& parent, const AbsolutePath& executablePath, vector<string> listArgument )
    {
        if ( executablePath.empty() )
            return {};
        if ( parent.isAutomatedRun() )
        {
            StringBuilder<constant::buffer::kMax64> unattended;
            unattended.appendFormat( "-%#", getCommandLineArgumentSpelling( CommandLineArgument::UNATTENDED ) );
            VectorUtil::addUnique( listArgument, string( unattended.view() ) );
        }
        return ProcessCommand::at( executablePath, std::move( listArgument ) );
    }
} // namespace sw
