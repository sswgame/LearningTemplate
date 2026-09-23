#include "pch.h"

#include "Core/File/Linux/LinuxFileDialog.h"

#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Common/PlatformOsHeaders.h"

namespace sw
{
    namespace
    {
        struct LinuxFileDialogInternal
        {
            enum class DialogBackend : uint8
            {
                None,
                Zenity,
                KDialog,
                Yad,
            };

            /** @brief 셸 명령 인자로 안전하게 넘기도록 홑따옴표로 감싸고 이스케이프합니다. */
            static string shellQuote( string_view value )
            {
                string quoted;
                quoted.reserve( value.size() + 2 );
                quoted.push_back( '\'' );
                for ( const utf8 ch : value )
                {
                    if ( ch == '\'' )
                        quoted.append( "'\\''" );
                    else
                        quoted.push_back( ch );
                }
                quoted.push_back( '\'' );
                return quoted;
            }

            /** @brief 경로의 파일이 실행 가능한지 반환합니다. */
            static bool isExecutableFile( string_view path )
            {
                const string pathNt( path );
                return access( pathNt.c_str(), X_OK ) == 0;
            }

            /** @brief PATH 환경 변수를 뒤져 이름에 맞는 실행 파일의 경로를 찾습니다. */
            static string findExecutable( const utf8* pName )
            {
                if ( pName == nullptr || pName[0] == '\0' )
                    return {};

                if ( std::strchr( pName, '/' ) != nullptr )
                    return isExecutableFile( pName ) ? string{ pName } : string{};

                const utf8* pathEnv = std::getenv( "PATH" );
                if ( pathEnv == nullptr || pathEnv[0] == '\0' )
                    return {};

                const string pathEnvStr{ pathEnv };
                size_t       start{ 0 };
                while ( start <= pathEnvStr.size() )
                {
                    size_t end = pathEnvStr.find( ':', start );
                    string dir = pathEnvStr.substr( start, ( end == string::npos ) ? string::npos : end - start );
                    if ( dir.empty() )
                        dir = ".";

                    const string candidate = FileUtil::joinPath( dir, pName );
                    if ( isExecutableFile( candidate ) )
                        return candidate;

                    if ( end == string::npos )
                        break;
                    start = end + 1;
                }
                return {};
            }

            /** @brief 쓸 수 있는 다이얼로그 도구(zenity, kdialog 등)를 찾아 그 백엔드를 반환합니다. */
            static DialogBackend resolveBackend( string& outToolPath )
            {
                struct Candidate
                {
                    const utf8*   _pName;
                    DialogBackend _backend;
                };

                static constexpr Candidate kArrCandidates[] = {
                    {    "zenity",  DialogBackend::Zenity},
                    {     "qarma",  DialogBackend::Zenity}, // zenity 호환
                    {"matedialog",  DialogBackend::Zenity}, // zenity 호환
                    {   "kdialog", DialogBackend::KDialog},
                    {       "yad",     DialogBackend::Yad},
                };

                for ( const Candidate& candidate : kArrCandidates )
                {
                    outToolPath = findExecutable( candidate._pName );
                    if ( outToolPath.empty() == false )
                        return candidate._backend;
                }

                outToolPath.clear();
                return DialogBackend::None;
            }

            /** @brief 확장자 하나의 glob 패턴(예: *.txt)을 만듭니다. */
            static string makeGlobPattern( string_view extension )
            {
                string pattern = "*";
                if ( extension.empty() == false )
                {
                    if ( extension[0] != '.' )
                        pattern.push_back( '.' );
                    pattern.append( extension.data(), extension.size() );
                }
                return pattern;
            }

            /** @brief 여러 확장자를 합친 glob 목록 문자열을 만듭니다. */
            static string makeCombinedGlobList( const vector<string>& listExtension )
            {
                if ( listExtension.empty() )
                    return "*";

                string combined;
                for ( const string& ext : listExtension )
                {
                    if ( combined.empty() == false )
                        combined.push_back( ' ' );
                    combined.append( makeGlobPattern( ext ) );
                }
                return combined;
            }

            /** @brief 다이얼로그에 표시할 제목을 정합니다. */
            static string dialogTitle( const FileDialogParams& params )
            {
                if ( params._title.empty() == false )
                    return params._title;
                if ( params._description.empty() == false )
                    return params._description;
                return ( params._type == FileDialogParams::Type::Save ) ? "Save File" : "Open File";
            }

            /** @brief 셸 명령을 실행하고 표준 출력(stdout)을 문자열로 받아 반환합니다. */
            static string runCommandCapture( string_view command, int32& outExitCode )
            {
                outExitCode = -1;
                const string commandNt( command );
                FILE*        pPipe = popen( commandNt.c_str(), "r" );
                if ( pPipe == nullptr )
                    return {};

                utf8   arrBuffer[constant::kMaxBuffer4096];
                string output;
                while ( fgets( arrBuffer, sizeof( arrBuffer ), pPipe ) != nullptr )
                    output += arrBuffer;

                const int32 status = pclose( pPipe );
                if ( status == -1 )
                    outExitCode = -1;
                else if ( WIFEXITED( status ) )
                    outExitCode = WEXITSTATUS( status );
                else
                    outExitCode = -1;

                while ( output.empty() == false && ( output.back() == '\n' || output.back() == '\r' ) )
                    output.pop_back();
                return output;
            }

            /** @brief 다중 선택 결과를 구분자(separator)로 나눠 배열에 담습니다. */
            static void splitPaths( string_view output, utf8 separator, vector<string>& outListPath )
            {
                size_t start{ 0 };
                while ( start < output.size() )
                {
                    const size_t sep = output.find( separator, start );
                    const size_t end = ( sep == string::npos ) ? output.size() : sep;
                    string       part{ output.substr( start, end - start ) };
                    while ( part.empty() == false && ( part.back() == '\n' || part.back() == '\r' ) )
                        part.pop_back();
                    if ( part.empty() == false )
                        outListPath.push_back( FileUtil::normalizeSeparators( part ) );
                    if ( sep == string::npos )
                        break;
                    start = sep + 1;
                }
            }

            /**
             * @brief zenity · yad 계열 도구의 명령줄을 만듭니다. **두 도구의 차이는 인자 두 개뿐입니다.**
             * @param pFileSelectionFlag 파일 선택 모드를 켜는 플래그(zenity `--file-selection`, yad `--file`)
             * @param bAppendAllFilesFilter "All files | *" 필터를 하나 더 붙일지 여부
             *
             * @details 두 도구의 명령 조립이 **글자까지 같은 스무 줄로 복사**돼 있었는데, 그 사본이 이미 갈라져 있었습니다.
             *          **zenity 만 "All files" 필터를 붙입니다.** 그래서 yad 만 설치된 기계에서는 선언한 확장자 밖의 파일을
             *          **고를 방법이 없습니다.** 같은 제품이 설치된 도구에 따라 다르게 동작하는데, 어느 쪽도 로그를 남기지 않습니다.
             *
             * @note **그 차이는 여기서 고치지 않았습니다.** yad 가 `--file-filter` 를 여러 번 받는지 이 기계에서 확인할 수 없기
             *       때문입니다. 잘못 넣으면 "필터가 제한된다" 로 끝나지 않고 도구가 인자를 거부해 **다이얼로그가 아예 뜨지 않습니다.**
             *       지금보다 나빠지는 것입니다. 대신 차이를 **인자 하나로 드러내서**, 다음 사람이 한 줄만 바꾸면 되게 했습니다.
             *       확인 방법: yad 가 설치된 리눅스에서 `yad --file --file-filter='A | *.txt'
             *       --file-filter='All files | *'` 가 뜨는지 봅니다. 뜨면 아래 호출을 `true` 로 바꿉니다.
             */
            static string buildGtkStyleCommand( string_view toolPath, const utf8* pFileSelectionFlag,
                                                const FileDialogParams& params, bool bMulti, bool bAppendAllFilesFilter )
            {
                string cmd = shellQuote( toolPath );
                cmd += " ";
                cmd += pFileSelectionFlag;

                if ( params._type == FileDialogParams::Type::Save )
                    cmd += " --save --confirm-overwrite";

                cmd += " --title=";
                cmd += shellQuote( dialogTitle( params ) );

                if ( bMulti )
                    cmd += " --multiple --separator='|'";

                if ( params._initialDirectory.empty() == false )
                {
                    // 디렉터리로 열리게 하려면 끝에 슬래시가 있어야 한다. 없으면 그 이름의 파일을 고른 것으로 본다.
                    string initial = FileUtil::normalizeSeparators( params._initialDirectory );
                    if ( initial.empty() == false && initial.back() != '/' )
                        initial.push_back( '/' );
                    cmd += " --filename=";
                    cmd += shellQuote( initial );
                }

                if ( params._listFilterExtension.empty() == false )
                {
                    const string label = params._description.empty() ? "Files" : params._description;
                    const string globs = makeCombinedGlobList( params._listFilterExtension );
                    cmd += " --file-filter=";
                    cmd += shellQuote( label + " | " + globs );

                    if ( bAppendAllFilesFilter )
                    {
                        cmd += " --file-filter=";
                        cmd += shellQuote( string{ "All files | *" } );
                    }
                }
                return cmd;
            }

            /** @brief zenity · yad 의 출력(구분자 `|`)을 경로 목록으로 바꿉니다. */
            static bool collectGtkStyleOutput( string_view output, bool bMulti, vector<string>& outListPath )
            {
                if ( bMulti )
                    splitPaths( output, '|', outListPath );
                else
                    outListPath.push_back( FileUtil::normalizeSeparators( output ) );
                return outListPath.empty() == false;
            }

            /** @brief 다중 선택을 쓸 수 있는 상황인지 반환합니다(저장 다이얼로그는 하나만 고릅니다). */
            static bool isMultiselectEnabled( const FileDialogParams& params )
            {
                return params._bEnableMultiselect && params._type == FileDialogParams::Type::Open;
            }

            /** @brief zenity(또는 호환 도구)로 다이얼로그를 엽니다. */
            static bool openWithZenity( string_view toolPath, const FileDialogParams& params, vector<string>& outListPath )
            {
                const bool   bMulti = isMultiselectEnabled( params );
                const string cmd    = buildGtkStyleCommand( toolPath, "--file-selection", params, bMulti, true );

                int32        exitCode = -1;
                const string output   = runCommandCapture( cmd, exitCode );
                if ( exitCode != 0 || output.empty() )
                    return false;

                return collectGtkStyleOutput( output, bMulti, outListPath );
            }

            /**
             * @brief yad 로 다이얼로그를 엽니다.
             * @note "All files" 필터를 붙이지 않는 것이 zenity 와의 유일한 차이입니다. 그대로 둔 이유는 `buildGtkStyleCommand` 의
             *       설명을 보십시오.
             */
            static bool openWithYad( string_view toolPath, const FileDialogParams& params, vector<string>& outListPath )
            {
                const bool   bMulti = isMultiselectEnabled( params );
                const string cmd    = buildGtkStyleCommand( toolPath, "--file", params, bMulti, false );

                int32        exitCode = -1;
                const string output   = runCommandCapture( cmd, exitCode );
                if ( exitCode != 0 || output.empty() )
                    return false;

                return collectGtkStyleOutput( output, bMulti, outListPath );
            }

            /** @brief kdialog 로 다이얼로그를 엽니다. */
            static bool openWithKDialog( string_view toolPath, const FileDialogParams& params, vector<string>& outListPath )
            {
                string cmd = shellQuote( toolPath );
                if ( params._type == FileDialogParams::Type::Save )
                    cmd += " --getsavefilename";
                else
                    cmd += " --getopenfilename";

                cmd += " --title ";
                cmd += shellQuote( dialogTitle( params ) );

                const bool bMulti = params._bEnableMultiselect && params._type == FileDialogParams::Type::Open;
                if ( bMulti )
                    cmd += " --multiple --separate-output";

                string startDir = ".";
                if ( params._initialDirectory.empty() == false )
                    startDir = FileUtil::normalizeSeparators( params._initialDirectory );
                cmd.push_back( ' ' );
                cmd += shellQuote( startDir );

                if ( params._listFilterExtension.empty() == false )
                {
                    const string label = params._description.empty() ? "Files" : params._description;
                    const string globs = makeCombinedGlobList( params._listFilterExtension );
                    // kdialog 필터 형식: "Description (*.ext *.ext2)"
                    cmd.push_back( ' ' );
                    cmd += shellQuote( label + " (" + globs + ")" );
                }

                int32        exitCode = -1;
                const string output   = runCommandCapture( cmd, exitCode );
                if ( exitCode != 0 || output.empty() )
                    return false;

                if ( bMulti )
                    splitPaths( output, '\n', outListPath );
                else
                    outListPath.push_back( FileUtil::normalizeSeparators( output ) );
                return outListPath.empty() == false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "LinuxFileDialog" );

    bool LinuxFileDialog::open( const FileDialogParams& params, vector<string>& outListPath )
    {
        outListPath.clear();

        LinuxFileDialogInternal::DialogBackend backend = LinuxFileDialogInternal::DialogBackend::None;
        string                                 toolPath;
        BLOCK( "Resolve Backend" )
        {
            backend = LinuxFileDialogInternal::resolveBackend( toolPath );
            if ( backend == LinuxFileDialogInternal::DialogBackend::None )
            {
                SW_LOG_WARNING( "No file dialog tool found. Install one of: zenity, kdialog, yad "
                                "(e.g. sudo apt install zenity)." );
                return false;
            }
        }

        bool ok{ false };
        BLOCK( "Execute Backend" )
        {
            switch ( backend )
            {
                case LinuxFileDialogInternal::DialogBackend::Zenity:
                {
                    ok = LinuxFileDialogInternal::openWithZenity( toolPath, params, outListPath );
                    break;
                }
                case LinuxFileDialogInternal::DialogBackend::KDialog:
                {
                    ok = LinuxFileDialogInternal::openWithKDialog( toolPath, params, outListPath );
                    break;
                }
                case LinuxFileDialogInternal::DialogBackend::Yad:
                {
                    ok = LinuxFileDialogInternal::openWithYad( toolPath, params, outListPath );
                    break;
                }
                default:
                    break;
            }
        }

        return ok;
    }
} // namespace sw

#endif // SW_PLATFORM_LINUX
