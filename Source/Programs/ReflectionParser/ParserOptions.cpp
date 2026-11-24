#include "pch.h"

#include "Programs/ReflectionParser/ParserOptions.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Programs/ReflectionParser/ParserDefines.h"

namespace sw
{
    namespace
    {
        /** @brief 플래그 한 줄입니다. 글 하나(`_pValue`) · 글 목록(`_pList`) · 디스크 경로 하나(`_pPath`) · 디스크 경로 목록(`_pPathList`) 중 하나를 가리킵니다. */
        struct OptionRow
        {
            const utf8* _pFlag;
            string ParserOptions::* _pValue;
            vector<string> ParserOptions::* _pList;
            AbsolutePath ParserOptions::* _pPath;
            vector<AbsolutePath> ParserOptions::* _pPathList;
            const utf8*                           _pValueName;
            const utf8*                           _pHelp;
        };

        constexpr OptionRow kArrOptionRow[] = {
            {          cli::kInput,                     nullptr,                          nullptr,                              nullptr, &ParserOptions::_listInputFile,            "<header.h>",                                                                              "header to parse (repeatable)"},
            {         cli::kOutput,                     nullptr,                          nullptr,           &ParserOptions::_outputDir,                        nullptr,                 "<dir>",                                                                  "directory for .gen.cpp / .gen.h / stamps"},
            {        cli::kInclude,                     nullptr, &ParserOptions::_listIncludePath,                              nullptr,                        nullptr,                 "<dir>",                                                                           "clang include path (repeatable)"},
            {         cli::kConfig,                     nullptr,                          nullptr,          &ParserOptions::_configPath,                        nullptr,   "<abs defaults.json>",                       "committed parser_config.defaults.json (local and toolchain settings sit next to it)"},
            {       cli::kBuiltins,                     nullptr,                          nullptr,        &ParserOptions::_builtinsPath,                        nullptr, "<ReflectBuiltins.xxx>",                                                                        "scalar aliases and container rules"},
            {  cli::kEmitTemplates,                     nullptr,                          nullptr,    &ParserOptions::_emitTemplatesDir,                        nullptr,                 "<dir>",                                                                                 "Templates/*.tpl directory"},
            {         cli::kModule, &ParserOptions::_moduleName,                          nullptr,                              nullptr,                        nullptr,                "<name>",                                       "runtime module the types register under (the CMake target's module)"},
            {cli::kEmitBuiltinsGen,                     nullptr,                          nullptr, &ParserOptions::_emitBuiltinsGenPath,                        nullptr,            "<file.cpp>",                                                               "only write ReflectBuiltins.gen.cpp and exit"},
            {      cli::kTargetArg,                     nullptr,   &ParserOptions::_listTargetArg,                              nullptr,                        nullptr,           "<clang arg>", "clang argument describing the build target: -D target macro, --target=, --sysroot=, -isystem (repeatable)"},
            {        cli::kDepfile,                     nullptr,                          nullptr,         &ParserOptions::_depfilePath,                        nullptr,              "<file.d>",                                          "write the headers the inputs include as a Makefile-style depfile"},
        };

        /** @brief 값 없이 켜는 플래그 한 줄입니다. */
        struct SwitchRow
        {
            const utf8* _pFlag;
            bool ParserOptions::* _pSwitch;
            const utf8*           _pHelp;
        };

        constexpr SwitchRow kArrSwitchRow[] = {
            {         "--dump",        &ParserOptions::_bDump,                                            "re-parse even up-to-date inputs and print what was extracted per header"},
            {cli::kModuleLocal, &ParserOptions::_bModuleLocal, "the target's types never cross a module boundary (executable, game module), so bus body types need no export macro"},
            {         "--help",        &ParserOptions::_bHelp,                                                                                          "print this usage and exit"},
            {             "-h",        &ParserOptions::_bHelp,                                                                                                     "same as --help"},
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

            /**
             * @brief `@파일` 인자를 그 파일의 줄들로 펼칩니다(줄 하나가 인자 하나, 빈 줄은 건너뜀).
             * @details 명령 줄은 Windows 에서 32767 자까지라 입력 헤더가 많으면 넘는다 — 빌드 체계는 인자를 응답 파일로 넘긴다.
             * @return 파일을 못 읽으면 이유를 알리고 false
             */
            static bool expandArguments( const int32 argc, utf8* argv[], vector<string>& outListArgument )
            {
                for ( int32 argIndex = 1; argIndex < argc; ++argIndex )
                {
                    if ( argv[argIndex][0] != '@' )
                    {
                        outListArgument.emplace_back( argv[argIndex] );
                        continue;
                    }
                    string text;
                    if ( FileUtil::readTextFile( FileUtil::toDiskPath( argv[argIndex] + 1 ), text ) == false )
                    {
                        SW_LOG_ERROR_ID( "ParserOptions.CouldNotReadResponse", "Could not read the response file: %#", argv[argIndex] + 1 );
                        return false;
                    }
                    size_t lineBegin = 0;
                    while ( lineBegin <= text.size() )
                    {
                        size_t lineEnd = text.find( '\n', lineBegin );
                        if ( lineEnd == string::npos )
                            lineEnd = text.size();
                        size_t trimmedEnd = lineEnd;
                        while ( trimmedEnd > lineBegin && text[trimmedEnd - 1] == '\r' )
                        {
                            --trimmedEnd;
                        }
                        if ( trimmedEnd > lineBegin )
                            outListArgument.emplace_back( text.substr( lineBegin, trimmedEnd - lineBegin ) );
                        lineBegin = lineEnd + 1;
                    }
                }
                return true;
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
            static bool isGiven( string_view value, const utf8* pFlag, const utf8* pMode )
            {
                if ( value.empty() == false )
                    return true;
                SW_LOG_ERROR_ID( "ParserOptions.Requires", "%# requires %#.", pMode, pFlag );
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ParserOptions::parse( const int32 argc, utf8* argv[] )
    {
        vector<string> listArgument;
        if ( ParserOptionsInternal::expandArguments( argc, argv, listArgument ) == false )
            return false;

        const size_t argCount = listArgument.size();
        for ( size_t argIndex = 0; argIndex < argCount; ++argIndex )
        {
            const string& argument = listArgument[argIndex];
            if ( const SwitchRow* pSwitch = ParserOptionsInternal::findSwitch( argument ) )
            {
                this->*pSwitch->_pSwitch = true;
                continue;
            }
            const OptionRow* pRow = ParserOptionsInternal::findRow( argument );
            if ( pRow == nullptr )
            {
                SW_LOG_ERROR_ID( "ParserOptions.UnknownArgument", "Unknown argument: %#", argument );
                return false;
            }
            if ( argIndex + 1 >= argCount )
            {
                SW_LOG_ERROR_ID( "ParserOptions.NeedsValue", "%# needs a value %#.", pRow->_pFlag, pRow->_pValueName );
                return false;
            }

            const string& value = listArgument[++argIndex];
            if ( pRow->_pList != nullptr )
                ( this->*pRow->_pList ).emplace_back( value );
            else if ( pRow->_pValue != nullptr )
                this->*pRow->_pValue = value;
            else
            {
                // 빌드 체계는 절대 경로만 넘긴다 — 작업 폴더 기준으로 짐작하지 않는다.
                optional<AbsolutePath> path = AbsolutePath::tryMake( value );
                if ( path.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "ParserOptions.NeedsAbsolutePath", "%# needs an absolute path, got '%#'.", pRow->_pFlag, value );
                    return false;
                }
                if ( pRow->_pPathList != nullptr )
                    ( this->*pRow->_pPathList ).push_back( std::move( *path ) );
                else
                    this->*pRow->_pPath = std::move( *path );
            }
        }

        // 사용법만 묻는 실행 — 필수 인자를 따지지 않는다.
        if ( _bHelp )
            return true;

        if ( isBuiltinsGenMode() )
        {
            return ParserOptionsInternal::isGiven( _builtinsPath.str(), cli::kBuiltins, cli::kEmitBuiltinsGen ) &&
                   ParserOptionsInternal::isGiven( _emitTemplatesDir.str(), cli::kEmitTemplates, cli::kEmitBuiltinsGen ) &&
                   ParserOptionsInternal::isGiven( _moduleName, cli::kModule, cli::kEmitBuiltinsGen );
        }

        if ( _listInputFile.empty() )
        {
            SW_LOG_ERROR_ID( "ParserOptions.NoFilesSpecified", "No %# files specified.", cli::kInput );
            return false;
        }
        // 필수 인자가 빠지면 시작할 때 이유를 알린다.
        return ParserOptionsInternal::isGiven( _outputDir.str(), cli::kOutput, cli::kInput ) &&
               ParserOptionsInternal::isGiven( _builtinsPath.str(), cli::kBuiltins, cli::kInput ) &&
               ParserOptionsInternal::isGiven( _configPath.str(), cli::kConfig, cli::kInput ) &&
               ParserOptionsInternal::isGiven( _emitTemplatesDir.str(), cli::kEmitTemplates, cli::kInput ) &&
               ParserOptionsInternal::isGiven( _moduleName, cli::kModule, cli::kInput ) && hasTargetArgs();
    }

    bool ParserOptions::hasTargetArgs() const
    {
        if ( _listTargetArg.empty() == false )
            return true;
        SW_LOG_ERROR_ID( "ParserOptions.NoTargetArgs", "%# requires %# (the build target's macros - CMake passes them; the parser does not guess from its own host build).", cli::kInput, cli::kTargetArg );
        return false;
    }

    void ParserOptions::printUsage()
    {
        StringBuilder<constant::buffer::kMax4096> out;
        out.append( "Usage: ReflectionParser --input <header.h> ... --output <dir> --module <name> --config <file> --emit-templates <dir> [...]\n" );
        out.append( "   or: ReflectionParser --builtins <file> --emit-templates <dir> --module <name> --emit-builtins-gen <file.cpp>\n" );
        for ( const OptionRow& row : kArrOptionRow )
        {
            out.appendFormat( "  %# %#  %#\n", row._pFlag, row._pValueName, row._pHelp );
        }
        for ( const SwitchRow& row : kArrSwitchRow )
        {
            out.appendFormat( "  %#  %#\n", row._pFlag, row._pHelp );
        }
        std::fwrite( out.c_str(), 1, out.size(), stdout );
        std::fflush( stdout );
    }
} // namespace sw
