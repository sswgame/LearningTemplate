#include "pch.h"

#include "Core/Process/EngineChildProcess.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Path/AbsolutePath.h"

namespace sw
{
    ProcessCommand EngineChildProcess::makeCommand( const CommandLineManager& parent, string_view executablePath, vector<string> listArgument )
    {
        const optional<AbsolutePath> executable = AbsolutePath::tryMake( executablePath );
        if ( executable.has_value() == false )
            return {};
        if ( parent.isAutomatedRun() )
        {
            const string unattended = "-" + string( getCommandLineArgumentSpelling( CommandLineArgument::UNATTENDED ) );
            if ( VectorUtil::contains( listArgument, unattended ) == false )
                listArgument.push_back( unattended );
        }
        return ProcessCommand::at( *executable, std::move( listArgument ) );
    }
} // namespace sw
