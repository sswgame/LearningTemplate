#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderRequestLog.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Compile/ShaderSource.h"

namespace sw
{
    namespace
    {
        struct ShaderRequestLogInternal
        {
            /** @brief 줄의 칸 구분자입니다. 리소스 경로 · 진입점 · define 에 탭은 없다. */
            static constexpr char kFieldSeparator = '\t';
            /** @brief define 앞의 고정 칸 수(형식 · 스테이지 · 소스 · 진입점)입니다. */
            static constexpr size_t kFixedFieldCount = 4;

            /** @brief 형식 폴더 이름(`dx12` …)의 형식입니다. 모르면 빈 값입니다. */
            static optional<ShaderTargetFormat> findFormat( string_view folder )
            {
                for ( uint32 index = 0; index < static_cast<uint32>( ShaderTargetFormat::Count ); ++index )
                {
                    const ShaderTargetFormat format = static_cast<ShaderTargetFormat>( index );
                    if ( ShaderCooker::getSubfolderForFormat( format ) == folder )
                        return format;
                }
                return nullopt;
            }

            /** @brief 스테이지 태그(`vs` …)의 스테이지입니다. 모르면 빈 값입니다. */
            static optional<ShaderStage> findStage( string_view tag )
            {
                for ( uint32 index = 0; index < static_cast<uint32>( ShaderStage::Count ); ++index )
                {
                    const ShaderStage stage = static_cast<ShaderStage>( index );
                    if ( string_view( getShaderStageInfo( stage )._pTag ) == tag )
                        return stage;
                }
                return nullopt;
            }

            /** @brief 병렬 컴파일 한 번의 입력과 요청마다의 결과 칸입니다 — 워커는 제 칸에만 쓴다. */
            struct CompileJob
            {
                const vector<ShaderCompileDesc>* _pListDesc{ nullptr };

                /** @brief 요청 @p index 를 컴파일하고, 지면 제 칸 @p outFailed 에 1 을 씁니다(`runParallelInto` 본문). */
                void operator()( uint32 index, uint8& outFailed ) const
                {
                    const ShaderCompileResult result = ShaderCompiler::compileHLSL( ( *_pListDesc )[index] );
                    outFailed                        = result._bSuccess ? 0 : 1;
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    ShaderRequestLog::ShaderRequestLog()
        : _setLine{}
    {
    }

    string ShaderRequestLog::makeLine( const ShaderCompileDesc& desc )
    {
        using Internal = ShaderRequestLogInternal;
        StringBuilder<constant::buffer::kMax256> line;
        line.append( ShaderCooker::getSubfolderForFormat( desc._targetFormat ) ).append( Internal::kFieldSeparator );
        line.append( getShaderStageInfo( desc._stage )._pTag ).append( Internal::kFieldSeparator );
        line.append( desc._source.str() ).append( Internal::kFieldSeparator ).append( desc._entryPoint );
        for ( const ShaderMacroDefine& define : desc._listDefine )
        {
            line.append( Internal::kFieldSeparator ).append( define._name ).append( '=' ).append( define._value );
        }
        return string( line.view() );
    }

    expected<ShaderCompileDesc> ShaderRequestLog::parseLine( string_view line )
    {
        using Internal                       = ShaderRequestLogInternal;
        const vector<string_view> listField  = StringUtil::split( line, string_view( &Internal::kFieldSeparator, 1 ) );
        const bool                bWellSized = listField.size() >= Internal::kFixedFieldCount;
        if ( bWellSized == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "a shader request line needs <format>\\t<stage>\\t<source>\\t<entry>[\\t<define>...]", line } );
        const optional<ShaderTargetFormat> format = Internal::findFormat( listField[0] );
        const optional<ShaderStage>        stage  = Internal::findStage( listField[1] );
        if ( format.has_value() == false || stage.has_value() == false || listField[2].empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "a shader request line has an unknown format, an unknown stage or no source", line } );
        ShaderCompileDesc desc{};
        desc._targetFormat = *format;
        desc._stage        = *stage;
        desc._source       = ShaderSourceID{ listField[2] };
        desc._entryPoint   = string( listField[3] );
        for ( size_t index = Internal::kFixedFieldCount; index < listField.size(); ++index )
        {
            desc._listDefine.push_back( ShaderMacroDefine::parse( listField[index] ) );
        }
        return desc;
    }

    bool ShaderRequestLog::add( const ShaderCompileDesc& desc )
    {
        return _setLine.insert( makeLine( desc ) ).second;
    }

    string ShaderRequestLog::makeText() const
    {
        string text{ kHeaderLine };
        text += '\n';
        for ( const string& line : _setLine )
        {
            text += line;
            text += '\n';
        }
        return text;
    }

    expected<void> ShaderRequestLog::mergeText( string_view text )
    {
        vector<string> listLine;
        bool           bHeaderSeen = false;
        expected<void> parsed      = {};
        StringUtil::forEachLine( text, [&]( string_view line )
        {
            if ( bHeaderSeen == false )
            {
                bHeaderSeen = true;
                if ( line != string_view( kHeaderLine ) )
                    parsed = unexpected( Error{ ErrorKind::InvalidArgument, "a shader request log starts with its header line", line } );
                return parsed.has_value();
            }
            if ( line.empty() )
                return true;
            const expected<ShaderCompileDesc> desc = parseLine( line );
            if ( desc.has_value() == false )
            {
                parsed = unexpected( desc.error() );
                return false;
            }
            listLine.push_back( makeLine( *desc ) ); // 읽은 줄을 다시 적어 같은 요청은 같은 키가 된다
            return true;
        } );
        if ( parsed.has_value() && bHeaderSeen == false )
            parsed = unexpected( Error{ ErrorKind::InvalidArgument, "a shader request log is empty (no header line)" } );
        if ( parsed.has_value() == false )
            return parsed;
        for ( string& line : listLine )
        {
            _setLine.insert( std::move( line ) );
        }
        return {};
    }

    vector<ShaderCompileDesc> ShaderRequestLog::collectDescs() const
    {
        vector<ShaderCompileDesc> listDesc;
        listDesc.reserve( _setLine.size() );
        for ( const string& line : _setLine )
        {
            listDesc.push_back( *parseLine( line ) ); // 이 기록이 적은 줄이라 늘 읽힌다
        }
        return listDesc;
    }

    expected<ShaderRequestLog> ShaderRequestLog::readFolder( const AbsolutePath& folder )
    {
        ShaderRequestLog requestLog;
        if ( FileUtil::isDirectory( folder ) == false )
            return requestLog; // 기록한 실행이 아직 없다
        vector<AbsolutePath> listFile;
        if ( FileUtil::collectFiles( folder, kFileExtension, listFile, false ) == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "could not list the shader request logs", folder.str() } );
        for ( const AbsolutePath& path : listFile )
        {
            string text;
            if ( FileUtil::readTextFile( path, text ) == false )
                return unexpected( Error{ ErrorKind::ExternalFailure, "could not read a shader request log", path.str() } );
            expected<void> merged = requestLog.mergeText( text );
            if ( merged.has_value() == false )
                return unexpected( merged.error().prependDetail( ": " ).prependDetail( path.str() ) );
        }
        return requestLog;
    }

    void ShaderRequestLog::removeMissingSources( vector<ShaderCompileDesc>& inoutList, const ShaderSourceResolver& resolver, vector<string>& outListMissing )
    {
        vector<ShaderCompileDesc> listPresent;
        listPresent.reserve( inoutList.size() );
        flat_set<string> setMissing;
        for ( ShaderCompileDesc& desc : inoutList )
        {
            if ( resolver.resolve( desc._source ).has_value() )
                listPresent.push_back( std::move( desc ) );
            else
                setMissing.insert( desc._source.str() );
        }
        inoutList = std::move( listPresent );
        outListMissing.assign( setMissing.begin(), setMissing.end() );
    }

    ShaderWarmSummary ShaderRequestLog::compileAll( const ShaderEnginePolicy& enginePolicy ) const
    {
        using Internal = ShaderRequestLogInternal;
        ShaderWarmSummary summary{};
        summary._requestCount                         = static_cast<uint32>( _setLine.size() );
        const optional<ShaderSourceResolver> resolver = ShaderSourceResolver::make( engine::getSubsystem<AssetPaths>() );
        if ( resolver.has_value() == false )
        {
            summary._failedCount = summary._requestCount; // 해석기가 까닭을 남겼다
            return summary;
        }
        vector<ShaderCompileDesc> listDesc = collectDescs();
        vector<string>            listMissing;
        removeMissingSources( listDesc, *resolver, listMissing );
        for ( const string& source : listMissing )
        {
            SW_LOG_WARNING_ID( "ShaderRequestLog.SourceGone", "A recorded shader request names a source that is gone now - skipped: '%#'", source );
        }
        summary._missingSourceCount = summary._requestCount - static_cast<uint32>( listDesc.size() );
        for ( ShaderCompileDesc& desc : listDesc )
        {
            desc._enginePolicy = enginePolicy;
        }

        vector<uint8>              listFailed( listDesc.size(), 0 );
        const Internal::CompileJob job{ &listDesc };
        engine::runParallelInto<uint8>( listFailed, 1, job );
        for ( const uint8 bFailed : listFailed )
        {
            summary._failedCount += bFailed;
        }
        return summary;
    }

    bool ShaderRequestLog::warmFromFolder( string_view folder )
    {
        const expected<ShaderRequestLog> requestLog = readFolder( FileUtil::toDiskPath( folder ) );
        if ( requestLog.has_value() == false )
        {
            SW_LOG_ERROR_ID( "ShaderRequestLog.ReadFailed", "%#: %#", requestLog.error().message(), requestLog.error().detail() );
            return false;
        }
        if ( requestLog->size() == 0 )
        {
            SW_LOG_INFO_ID( "ShaderRequestLog.NothingToWarm", "No shader request log under '%#' yet - nothing to warm", folder );
            return true;
        }
        const ShaderWarmSummary summary = requestLog->compileAll( ShaderEnginePolicy::capture() );
        SW_LOG_INFO_ID( "ShaderRequestLog.Warmed", "Shader warm-up from '%#': %# request(s), %# skipped (source gone), %# failed", folder, summary._requestCount, summary._missingSourceCount,
                        summary._failedCount );
        return summary._failedCount == 0;
    }
} // namespace sw
