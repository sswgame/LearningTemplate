#include "pch.h"

#include "DevTools/Diagnostics/UnitTestRun.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/TestFramework/TestRunner.h"

#include "DevTools/Diagnostics/TestCaseStateProbe.h"

#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    bool UnitTestRun::run( const CommandLineManager& commandLine, string_view defaultSourceFolder, string_view excludedTag )
    {
        test::TestRunOptions options;
        int32                seed        = 0;
        int32                repeatCount = 1;
        (void)commandLine.getArgument( CommandLineArgument::TEST_FILTER, options._filter );
        (void)commandLine.getArgument( CommandLineArgument::TEST_SOURCE, options._sourceFolder );
        if ( options._sourceFolder.empty() )
            options._sourceFolder = string( defaultSourceFolder );
        if ( excludedTag.empty() == false )
        {
            StringBuilder<constant::buffer::kMax512> filter;
            filter.appendFormat( "%#,-[%#]", options._filter, excludedTag );
            options._filter = string( filter.view() );
        }
        (void)commandLine.getArgument( CommandLineArgument::TEST_SEED, seed );
        (void)commandLine.getArgument( CommandLineArgument::TEST_REPEAT, repeatCount );
        (void)commandLine.getArgument( CommandLineArgument::TEST_REPORT, options._reportPath );
        bool bUpdateSnapshots        = false;
        bool bRecordMissingSnapshots = false;
        (void)commandLine.getArgument( CommandLineArgument::UPDATE_SNAPSHOTS, bUpdateSnapshots );
        (void)commandLine.getArgument( CommandLineArgument::RECORD_MISSING_SNAPSHOTS, bRecordMissingSnapshots );
        if ( bUpdateSnapshots && bRecordMissingSnapshots )
        {
            SW_LOG_ERROR_ID( "Headless.SnapshotFlagsConflict", "--update-snapshots rewrites every snapshot and --record-missing-snapshots only the missing ones - pick one" );
            return false;
        }
        options._snapshotWriteMode = bUpdateSnapshots        ? test::SnapshotWriteMode::All
                                   : bRecordMissingSnapshots ? test::SnapshotWriteMode::Missing
                                                             : test::SnapshotWriteMode::Off;
        (void)commandLine.getArgument( CommandLineArgument::TEST_RUN_DISABLED, options._bRunDisabled );
        (void)commandLine.getArgument( CommandLineArgument::TEST_CHILD_ROLE, options._childRole );
        (void)commandLine.getArgument( CommandLineArgument::TEST_CHILD_ARG, options._childArgument );
        (void)commandLine.getArgument( CommandLineArgument::TEST_ISOLATED_CASE, options._isolatedCase );
        string shard{ "0/1" }; // 주지 않으면 몫 하나(전부)
        (void)commandLine.getArgument( CommandLineArgument::TEST_SHARD, shard );
        if ( test::TestRunner::parseShard( shard, options._shardIndex, options._shardCount ) == false )
        {
            SW_LOG_ERROR_ID( "Headless.TestShardNotI", "--test-shard=%# is not i/n (0 <= i < n)", shard.c_str() );
            return false;
        }
        options._seed          = static_cast<uint32>( seed );
        options._repeatCount   = repeatCount > 0 ? static_cast<uint32>( repeatCount ) : 1u;
        options._pCrashHandler = &engine::getSubsystem<CrashHandler>();
        TestCaseStateProbe stateProbe;
        options._onCaseBegin = SW_DELEGATE_METHOD( Delegate<void()>, &TestCaseStateProbe::beginCase, &stateProbe );
        options._onCaseEnd   = SW_DELEGATE_METHOD( Delegate<void( vector<string>& )>, &TestCaseStateProbe::endCase, &stateProbe );
        return test::TestRunner::run( options );
    }
} // namespace sw
