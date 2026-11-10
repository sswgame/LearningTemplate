#include "pch.h"

#include "DevTools/Client/Automation/Runner/AutomationGoldenUtil.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Process/Process.h"
#include "Core/String/StringUtil.h"

#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/JSON/JSONWriter.h"

namespace sw
{
    namespace
    {
        struct AutomationGoldenInternal
        {
            static constexpr string_view kImageExtension = ".ppm";
            static constexpr string_view kMetaExtension  = ".golden.json";
            static constexpr int32       kMetaIndent     = 4;

            static bool isFraction( float64 value ) { return value >= 0.0 && value <= 1.0; }

            static void writeDifference( JSONWriter& writer, const AutomationImageDifference& difference )
            {
                writer.beginObject();
                writer.key( "differingFraction" );
                writer.valueFloat( difference._differingFraction );
                writer.key( "meanDistance" );
                writer.valueFloat( difference._meanDistance );
                writer.key( "largestDeepRegionPixels" );
                writer.valueUint( difference._largestDeepRegionPixels );
                writer.endObject();
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    expected<AutomationGoldenPolicy> AutomationGoldenUtil::parsePolicy( string_view jsonText, string_view sourceName )
    {
        using Internal = AutomationGoldenInternal;
        JSONDocument document;
        if ( document.parse( jsonText, sourceName ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the golden policy is not JSON", sourceName } );
        const JSONValue         root           = document.getRoot();
        const optional<uint32>  differing      = root.readRequired<uint32>( "_differingMinDistance" );
        const optional<uint32>  deep           = root.readRequired<uint32>( "_deepMinDistance" );
        const optional<float64> maxFraction    = root.readRequired<float64>( "_maxDifferingFraction" );
        const optional<float64> maxMean        = root.readRequired<float64>( "_maxMeanDistance" );
        const optional<uint32>  maxRegion      = root.readRequired<uint32>( "_maxDeepRegionPixels" );
        const optional<float64> nearLimitRatio = root.readRequired<float64>( "_nearLimitRatio" );
        const bool              bAllRead       = differing.has_value() && deep.has_value() && maxFraction.has_value() && maxMean.has_value() && maxRegion.has_value() && nearLimitRatio.has_value();
        if ( bAllRead == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the golden policy needs _differingMinDistance, _deepMinDistance, _maxDifferingFraction, _maxMeanDistance, _maxDeepRegionPixels, _nearLimitRatio", sourceName } );
        const bool bDistancesInRange = *differing >= 1 && *differing <= AutomationImage::kMaxColorDistance && *deep >= *differing && *deep <= AutomationImage::kMaxColorDistance;
        const bool bLimitsInRange    = Internal::isFraction( *maxFraction ) && Internal::isFraction( *maxMean ) && *nearLimitRatio > 0.0 && *nearLimitRatio < 1.0;
        if ( bDistancesInRange == false || bLimitsInRange == false )
            return unexpected( Error{ ErrorKind::InvalidArgument,
                                      "the golden policy is out of range (distances 1..765 with deep >= differing, fractions 0..1, _nearLimitRatio between 0 and 1)", sourceName } );
        AutomationGoldenPolicy policy;
        policy._measure._differingMinDistance = *differing;
        policy._measure._deepMinDistance      = *deep;
        policy._limits._maxDifferingFraction  = *maxFraction;
        policy._limits._maxMeanDistance       = *maxMean;
        policy._limits._maxDeepRegionPixels   = *maxRegion;
        policy._nearLimitRatio                = *nearLimitRatio;
        return policy;
    }

    expected<AutomationGoldenPolicy> AutomationGoldenUtil::loadPolicy()
    {
        const string path = FileUtil::joinPath( ProcessRoots::get()._project.str(), kPolicyFile );
        string       text;
        if ( FileUtil::readTextFile( path, text ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "could not read the golden policy", path } );
        return parsePolicy( text, path );
    }

    GoldenResult AutomationGoldenUtil::judge( const AutomationImageDifference& difference, const AutomationGoldenLimits& limits, float64 nearLimitRatio )
    {
        const float64 region    = static_cast<float64>( difference._largestDeepRegionPixels );
        const float64 maxRegion = static_cast<float64>( limits._maxDeepRegionPixels );
        const bool    bFail     = difference._differingFraction > limits._maxDifferingFraction || difference._meanDistance > limits._maxMeanDistance || region > maxRegion;
        if ( bFail )
            return GoldenResult::Fail;
        const bool bNear = difference._differingFraction > limits._maxDifferingFraction * nearLimitRatio || difference._meanDistance > limits._maxMeanDistance * nearLimitRatio ||
                           region > maxRegion * nearLimitRatio;
        return bNear ? GoldenResult::PassNearLimit : GoldenResult::Pass;
    }

    GoldenAction AutomationGoldenUtil::decide( bool bHasGolden, bool bCompared, GoldenResult verdict, bool bUpdateGolden )
    {
        const bool bPass = bCompared && verdict == GoldenResult::Pass;
        if ( bPass )
            return GoldenAction::Keep;
        if ( bUpdateGolden )
            return GoldenAction::Write;
        if ( bHasGolden == false )
            return GoldenAction::FailMissing;
        if ( bCompared == false )
            return GoldenAction::FailUnreadable;
        return verdict == GoldenResult::PassNearLimit ? GoldenAction::KeepNearLimit : GoldenAction::Fail;
    }

    const utf8* AutomationGoldenUtil::describeWriteReason( bool bHasGolden, bool bCompared, GoldenResult verdict )
    {
        if ( bHasGolden == false )
            return "missing";
        if ( bCompared == false )
            return "size-changed";
        return verdict == GoldenResult::PassNearLimit ? "near-limit" : "failed";
    }

    optional<string_view> AutomationGoldenUtil::findBackendTag( uint32 backendIndex )
    {
        const utf8* pName = RHIBackendInfoTable::findCommandLineName( static_cast<RHIBackend>( backendIndex ) );
        if ( pName == nullptr )
            return nullopt;
        return string_view{ pName };
    }

    string_view AutomationGoldenUtil::getPlatformTag()
    {
        // Linux 는 골든을 따로 둔다 - 편집기 기본 글꼴이 OS 마다 달라 배치가 다르고, 지금 도는 곳이 소프트웨어 렌더러(lavapipe)다.
#if defined( SW_PLATFORM_LINUX )
        return ".linux";
#elif defined( SW_PLATFORM_WINDOWS )
        return "";
#else
    #error "Unsupported platform"
#endif
    }

    string AutomationGoldenUtil::makeGoldenID( string_view folderID, string_view scenarioName, string_view goldenName, string_view backendTag, string_view platformTag )
    {
        string id( folderID );
        id += "/";
        id += scenarioName;
        id += ".";
        id += goldenName;
        id += ".";
        id += backendTag;
        id += platformTag;
        id += AutomationGoldenInternal::kImageExtension;
        return id;
    }

    string AutomationGoldenUtil::makeMetaID( string_view goldenID )
    {
        using Internal = AutomationGoldenInternal;
        SW_ASSERT( StringUtil::endsWith( goldenID, Internal::kImageExtension ) );
        string id( goldenID.substr( 0, goldenID.size() - Internal::kImageExtension.size() ) );
        id += Internal::kMetaExtension;
        return id;
    }

    string AutomationGoldenUtil::makeMetaText( const AutomationGoldenMeta& meta )
    {
        string     text;
        JSONWriter writer( text, AutomationGoldenInternal::kMetaIndent );
        writer.beginObject();
        writer.key( "backend" );
        writer.valueString( meta._backendTag );
        writer.key( "platform" );
        writer.valueString( meta._platform );
        writer.key( "commit" );
        writer.valueString( meta._commit );
        writer.key( "recordedUnixMs" );
        writer.valueInt( meta._recordedUnixMs );
        writer.key( "reason" );
        writer.valueString( meta._reason );
        writer.key( "width" );
        writer.valueUint( meta._width );
        writer.key( "height" );
        writer.valueUint( meta._height );
        writer.key( "previousDifference" ); // 쓰기 전 골든과 견준 값 — 없던 · 크기가 다른 골든은 null
        if ( meta._bMeasured )
            AutomationGoldenInternal::writeDifference( writer, meta._difference );
        else
            writer.valueNull();
        writer.key( "limits" );
        writer.beginObject();
        writer.key( "maxDifferingFraction" );
        writer.valueFloat( meta._limits._maxDifferingFraction );
        writer.key( "maxMeanDistance" );
        writer.valueFloat( meta._limits._maxMeanDistance );
        writer.key( "maxDeepRegionPixels" );
        writer.valueUint( meta._limits._maxDeepRegionPixels );
        writer.endObject();
        writer.endObject();
        SW_ASSERT( writer.isComplete() );
        text += '\n';
        return text;
    }

    expected<string> AutomationGoldenUtil::findRecordingCommit()
    {
        string                firstLine;
        const expected<int32> exitCode = Process::execute( ProcessCommand::fromSearchPath( "git", { "-C", string( ProcessRoots::get()._project.str() ), "describe", "--always", "--dirty", "--abbrev=40" } ), {},
                                                           ProcessOutputDelegate::create( [&firstLine]( string_view line )
        {
            if ( firstLine.empty() )
                firstLine = string( StringUtil::trim( line ) );
        } ) );
        return readRecordingCommit( exitCode, firstLine );
    }

    expected<string> AutomationGoldenUtil::readRecordingCommit( const expected<int32>& exitCode, string_view firstLine )
    {
        // 기록한 커밋 없이 골든을 쓰지 않는다(폴백 없음) — 메타가 언제 뜬 그림인지 말하지 못하면 다음 리뷰가 그 골든을 믿을 근거가 없다.
        if ( exitCode.has_value() == false )
            return unexpected( Error{ ErrorKind::NotFound, "git could not be started (not on PATH) - recording a golden needs git to write the recording commit into its meta", exitCode.error().detail() } );
        if ( *exitCode != 0 )
            return unexpected( Error{ ErrorKind::ExternalFailure, "git describe failed (is the project folder a git work tree?) - recording a golden needs the recording commit", firstLine } );
        if ( firstLine.empty() )
            return unexpected( Error{ ErrorKind::ExternalFailure, "git describe printed no commit - recording a golden needs the recording commit" } );
        return string( firstLine );
    }
} // namespace sw
