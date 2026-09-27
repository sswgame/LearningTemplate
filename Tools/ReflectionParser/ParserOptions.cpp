#include "pch.h"

#include "ReflectionParser/ParserOptions.h"

#include "Core/Log/Logger.h"

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
            {          cliConstants::kInput,                              nullptr,   &ParserOptions::_listInputFile,            "<header.h>",                "header to parse (repeatable)"},
            {         cliConstants::kOutput,           &ParserOptions::_outputDir,                          nullptr,                 "<dir>",    "directory for .gen.cpp / .gen.h / stamps"},
            {        cliConstants::kInclude,                              nullptr, &ParserOptions::_listIncludePath,                 "<dir>",             "clang include path (repeatable)"},
            {       cliConstants::kBuiltins,        &ParserOptions::_builtinsPath,                          nullptr, "<ReflectBuiltins.xxx>",          "scalar aliases and container rules"},
            { cliConstants::kAnnotationMeta,  &ParserOptions::_annotationMetaPath,                          nullptr,  "<AnnotationMeta.txt>",                  "annotation token spellings"},
            {  cliConstants::kEmitTemplates,    &ParserOptions::_emitTemplatesDir,                          nullptr,                 "<dir>",                   "Templates/*.tpl directory"},
            {     cliConstants::kSourceRoot,          &ParserOptions::_sourceRoot,                          nullptr,                 "<dir>",   "module rules match paths relative to this"},
            {cliConstants::kEmitBuiltinsGen, &ParserOptions::_emitBuiltinsGenPath,                          nullptr,            "<file.cpp>", "only write ReflectBuiltins.gen.cpp and exit"},
        };

        struct ParserOptionsInternal
        {
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

    void ParserOptions::logUsage()
    {
        SW_LOG_INFO( "Usage: ReflectionParser --input <header.h> ... --output <dir> --annotation-meta <file> --emit-templates <dir> [...]" );
        SW_LOG_INFO( "   or: ReflectionParser --builtins <file> --emit-templates <dir> --emit-builtins-gen <file.cpp>" );
        for ( [[maybe_unused]] const OptionRow& row : kArrOptionRow )
            SW_LOG_INFO( "  %# %#  %#", row._pFlag, row._pValueName, row._pHelp );
    }
} // namespace sw
