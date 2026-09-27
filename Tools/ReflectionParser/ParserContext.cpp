#include "pch.h"

#include "ReflectionParser/ParserContext.h"

#include "Core/Common/Types.h"
#include "Core/Log/Logger.h"

#include "ReflectionParser/ParserConfig.h"

SW_LOG_CALLER( "ParserContext" );
namespace sw
{
    ParserContext::ParserContext( const ParserConfig& config )
        : _pConfig{ &config }
        , _index{ nullptr }
        , _translationUnit{ nullptr }
    {
        _index = clang_createIndex( 0, 0 );
    }

    ParserContext::~ParserContext()
    {
        if ( _translationUnit != nullptr )
        {
            clang_disposeTranslationUnit( _translationUnit );
            _translationUnit = nullptr;
        }
        if ( _index != nullptr )
        {
            clang_disposeIndex( _index );
            _index = nullptr;
        }
    }

    bool ParserContext::parse( const string& filePath, const vector<string>& listIncludePath, const vector<ParserUnsavedFile>& listUnsaved,
                               const bool bReportErrors )
    {
        if ( _index == nullptr )
        {
            SW_LOG_ERROR( "Failed to create CXIndex." );
            return false;
        }

        if ( _translationUnit != nullptr )
        {
            clang_disposeTranslationUnit( _translationUnit );
            _translationUnit = nullptr;
        }

        const vector<string> listArgString = _pConfig->buildArgs( listIncludePath );
        vector<const utf8*>  listArgPtr;
        listArgPtr.reserve( listArgString.size() );
        for ( const string& arg : listArgString )
            listArgPtr.push_back( arg.c_str() );

        vector<CXUnsavedFile> listCxUnsaved;
        listCxUnsaved.reserve( listUnsaved.size() );
        for ( const ParserUnsavedFile& unsaved : listUnsaved )
        {
            CXUnsavedFile cxUnsaved{};
            cxUnsaved.Filename = unsaved._pPath->c_str();
            cxUnsaved.Contents = unsaved._pContent->c_str();
            cxUnsaved.Length   = static_cast<uint32>( unsaved._pContent->size() );
            listCxUnsaved.push_back( cxUnsaved );
        }

        // DetailedPreprocessingRecord 는 annotate 매크로 경로에 필요 없고 TU 비용만 크다
        constexpr uint32 kParseFlags = CXTranslationUnit_SkipFunctionBodies | CXTranslationUnit_Incomplete;
        _translationUnit             = clang_parseTranslationUnit(
            _index, filePath.c_str(), listArgPtr.data(), static_cast<int32>( listArgPtr.size() ),
            listCxUnsaved.empty() ? nullptr : listCxUnsaved.data(), static_cast<uint32>( listCxUnsaved.size() ), kParseFlags );

        if ( _translationUnit == nullptr )
        {
            SW_LOG_ERROR( "clang_parseTranslationUnit failed for: %#", filePath );
            return false;
        }

        bool         bHasError = false;
        const uint32 numDiags  = clang_getNumDiagnostics( _translationUnit );
        for ( uint32 diagIndex = 0; diagIndex < numDiags; ++diagIndex )
        {
            const CXDiagnostic diag = clang_getDiagnostic( _translationUnit, diagIndex );
            if ( clang_getDiagnosticSeverity( diag ) >= CXDiagnostic_Error )
            {
                const CXString message = clang_formatDiagnostic( diag, clang_defaultDiagnosticDisplayOptions() );
                if ( bReportErrors )
                    SW_LOG_ERROR( "%#", clang_getCString( message ) );
                else
                    SW_LOG_TRACE( "%#", clang_getCString( message ) );
                clang_disposeString( message );
                bHasError = true;
            }
            clang_disposeDiagnostic( diag );
        }

        if ( bHasError && bReportErrors )
            SW_LOG_ERROR( "Parsing failed with errors. Check include paths with --include." );
        return bHasError == false;
    }
} // namespace sw
