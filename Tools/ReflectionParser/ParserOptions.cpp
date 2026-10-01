#include "pch.h"

#include "ReflectionParser/ParserOptions.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "ReflectionParser/ParserDefines.h"

SW_LOG_CALLER( "ParserOptions" );
namespace sw
{
    namespace
    {
        /** @brief 플래그 한 줄입니다. 값 하나(`_pValue`) 또는 여러 번 받는 목록(`_pList`) 중 하나를 가리킵니다. */
        struct OptionRow
        {
            const utf8* _pFlag;
            string ParserOptions::* _pValue;
            vector<string> ParserOptions::* _pList;
            const utf8*                     _pValueName;
            const utf8*                     _pHelp;
        };

        constexpr OptionRow kArrOptionRow[] = {
            {          cliConstants::kInput,                              nullptr,   &ParserOptions::_listInputFile,            "<header.h>",                                     "header to parse (repeatable)"},
            {         cliConstants::kOutput,           &ParserOptions::_outputDir,                          nullptr,                 "<dir>",                         "directory for .gen.cpp / .gen.h / stamps"},
            {        cliConstants::kInclude,                              nullptr, &ParserOptions::_listIncludePath,                 "<dir>",                                  "clang include path (repeatable)"},
            {       cliConstants::kBuiltins,        &ParserOptions::_builtinsPath,                          nullptr, "<ReflectBuiltins.xxx>",                               "scalar aliases and container rules"},
            { cliConstants::kAnnotationMeta,  &ParserOptions::_annotationMetaPath,                          nullptr,  "<AnnotationMeta.txt>",                                       "annotation token spellings"},
            {  cliConstants::kEmitTemplates,    &ParserOptions::_emitTemplatesDir,                          nullptr,                 "<dir>",                                        "Templates/*.tpl directory"},
            {     cliConstants::kSourceRoot,          &ParserOptions::_sourceRoot,                          nullptr,                 "<dir>",                        "module rules match paths relative to this"},
            {cliConstants::kEmitBuiltinsGen, &ParserOptions::_emitBuiltinsGenPath,                          nullptr,            "<file.cpp>",                      "only write ReflectBuiltins.gen.cpp and exit"},
            {        cliConstants::kDepfile,         &ParserOptions::_depfilePath,                          nullptr,              "<file.d>", "write the headers the inputs include as a Makefile-style depfile"},
        };

        /** @brief 값 없이 켜는 플래그 한 줄입니다. */
        struct SwitchRow
        {
            const utf8* _pFlag;
            bool ParserOptions::* _pSwitch;
            const utf8*           _pHelp;
        };

        constexpr SwitchRow kArrSwitchRow[] = {
            {"--dump", &ParserOptions::_bDump, "re-parse even up-to-date inputs and print what was extracted per header"},
            {"--help", &ParserOptions::_bHelp,                                               "print this usage and exit"},
            {    "-h", &ParserOptions::_bHelp,                                                          "same as --help"},
        };

        struct ParserOptionsInternal
        {
            static const SwitchRow* findSwitch( const string_view flag )
            {
                for ( const SwitchRow& row : kArrSwitchRow )
                {
                    if ( flag == row._pFlag )
                        return &row;
                }
                return nullptr;
            }

            static const OptionRow* findRow( const string_view flag )
            {
                for ( const OptionRow& row : kArrOptionRow )
                {
                    if ( flag == row._pFlag )
                        return &row;
                }
                return nullptr;
            }

            /** @brief 비어 있으면 무엇이 빠졌는지 알리고 false 입니다. */
            static bool isGiven( const string& value, const utf8* pFlag, const utf8* pMode )
            {
                if ( value.empty() == false )
                    return true;
                SW_LOG_ERROR( "%# requires %#.", pMode, pFlag );
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ParserOptions::parse( const int32 argc, utf8* argv[] )
    {
        for ( int32 argIndex = 1; argIndex < argc; ++argIndex )
        {
            if ( const SwitchRow* pSwitch = ParserOptionsInternal::findSwitch( argv[argIndex] ) )
            {
                this->*pSwitch->_pSwitch = true;
                continue;
            }
            const OptionRow* pRow = ParserOptionsInternal::findRow( argv[argIndex] );
            if ( pRow == nullptr )
            {
                SW_LOG_ERROR( "Unknown argument: %#", argv[argIndex] );
                return false;
            }
            if ( argIndex + 1 >= argc )
            {
                SW_LOG_ERROR( "%# needs a value %#.", pRow->_pFlag, pRow->_pValueName );
                return false;
            }

            const utf8* pValue = argv[++argIndex];
            if ( pRow->_pList != nullptr )
                ( this->*pRow->_pList ).emplace_back( pValue );
            else
                this->*pRow->_pValue = pValue;
        }

        // 사용법만 묻는 실행 — 필수 인자를 따지지 않는다(예전에는 `--help` 가 "Unknown argument" 오류였다).
        if ( _bHelp )
            return true;

        if ( isBuiltinsGenMode() )
        {
            return ParserOptionsInternal::isGiven( _builtinsPath, cliConstants::kBuiltins, cliConstants::kEmitBuiltinsGen ) &&
                   ParserOptionsInternal::isGiven( _emitTemplatesDir, cliConstants::kEmitTemplates, cliConstants::kEmitBuiltinsGen );
        }

        if ( _listInputFile.empty() )
        {
            SW_LOG_ERROR( "No %# files specified.", cliConstants::kInput );
            return false;
        }
        // 철자 표가 없으면 모든 애노테이션 토큰이 "모르는 토큰" 이 되어 빌드가 선다. 시작할 때 이유를 알린다.
        return ParserOptionsInternal::isGiven( _outputDir, cliConstants::kOutput, cliConstants::kInput ) &&
               ParserOptionsInternal::isGiven( _emitTemplatesDir, cliConstants::kEmitTemplates, cliConstants::kInput ) &&
               ParserOptionsInternal::isGiven( _annotationMetaPath, cliConstants::kAnnotationMeta, cliConstants::kInput );
    }

    void ParserOptions::printUsage()
    {
        StringBuilder<constant::kMaxBuffer4096> out;
        out.append( "Usage: ReflectionParser --input <header.h> ... --output <dir> --annotation-meta <file> --emit-templates <dir> [...]\n" );
        out.append( "   or: ReflectionParser --builtins <file> --emit-templates <dir> --emit-builtins-gen <file.cpp>\n" );
        for ( const OptionRow& row : kArrOptionRow )
            out.appendFormat( "  %# %#  %#\n", row._pFlag, row._pValueName, row._pHelp );
        for ( const SwitchRow& row : kArrSwitchRow )
            out.appendFormat( "  %#  %#\n", row._pFlag, row._pHelp );
        std::fwrite( out.c_str(), 1, out.size(), stdout );
        std::fflush( stdout );
    }
} // namespace sw
