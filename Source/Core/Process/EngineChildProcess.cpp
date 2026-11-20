#include "pch.h"

#include "Core/Process/EngineChildProcess.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/StringBuilder.h"

namespace sw
{
    ProcessCommand EngineChildProcess::makeCommand( const CommandLineManager& parent, string_view executablePath, vector<string> listArgument )
    {
        const optional<AbsolutePath> executable = AbsolutePath::tryMake( executablePath );
        if ( executable.has_value() == false )
            return {};
        if ( parent.isAutomatedRun() )
        {
            StringBuilder<constant::buffer::kMax64> unattended;
            unattended.appendFormat( "-%#", getCommandLineArgumentSpelling( CommandLineArgument::UNATTENDED ) );
            VectorUtil::addUnique( listArgument, string( unattended.view() ) );
        }
        return ProcessCommand::at( *executable, std::move( listArgument ) );
    }
} // namespace sw
