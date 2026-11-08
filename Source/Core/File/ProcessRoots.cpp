#include "pch.h"

#include "Core/File/ProcessRoots.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Process/ProcessEnvironment.h"

namespace sw
{
    namespace
    {
        struct ProcessRootsInternal
        {
            /** @brief 절대 경로를 만들거나 멈춥니다 - 루트는 폴백 없이 정해져야 한다. */
            static AbsolutePath requireAbsolute( const string& path, const utf8* pWhat )
            {
                const optional<AbsolutePath> absolute = AbsolutePath::tryMake( path );
                if ( absolute.has_value() == false )
                    SW_FATAL( "ProcessRoots: the %# path '%#' is not absolute", pWhat, path );
                return *absolute;
            }

            /** @brief @p startFolder 에서 위로 올라가며 `Content` 가 있는 첫 폴더를 찾습니다. 없으면 빈 글입니다. */
            static string findProjectRoot( const string& startFolder )
            {
                string folder = startFolder;
                while ( folder.empty() == false )
                {
                    if ( FileUtil::isDirectory( FileUtil::joinPath( folder, constant::path::kContentFolder ) ) )
                        return folder;
                    const string parent = FileUtil::getDirectoryPart( folder );
                    if ( parent.empty() || parent == folder )
                        break;
                    folder = parent;
                }
                return {};
            }

#if defined( SW_PLATFORM_ANDROID )
            /** @brief Android 의 바이너리 폴더입니다 - 기동이 넘긴 앱 전용 폴더(`kAndroidAppFolderVariable`). 실행 파일(`/proc/self/exe`)은 시스템의 app_process 라 쓸 수 없다. */
            static string findBinaryFolder()
            {
                string appFolder;
                if ( ProcessEnvironment::tryReadNonEmpty( kAndroidAppFolderVariable, appFolder ) == false )
                    SW_FATAL( "ProcessRoots: %# is not set - AndroidMain must pass the app folder before anything asks for the roots", kAndroidAppFolderVariable );
                return appFolder;
            }
#elif defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_WEB )
            /**
             * @brief 바이너리 폴더(`Bin`)입니다 - 실행 파일의 폴더. 모든 실행 파일이 `Bin` 에 있다 — GameFramework · 키트를 쓰는 실행 파일도 그 DLL 을 직접 링크하지 않고
             *        엔진 기동 뒤에 모듈로 올린다(키트 DLL 사본을 옆에 둔 `ToolBin` · `TestBin` 은 없앴다, `Programs/OnlineLoadBot/LoadBotModuleEntry.h`).
             */
            static string findBinaryFolder() { return FileUtil::getDirectoryPart( FileUtil::getExecutablePath() ); }
#else
    #error "Unsupported platform"
#endif

            static ProcessRoots make()
            {
                const string binary  = findBinaryFolder();
                string       project = findProjectRoot( binary );
#if defined( SW_PLATFORM_ANDROID )
                // 팩은 APK 안이라 `<앱 폴더>/Packs` 가 디스크에 없다 — 콘텐츠 소스가 그 경로를 assets/Packs 로 옮긴다. 프로젝트 루트는 앱 폴더다(패키지와 같다).
                if ( project.empty() )
                    project = binary;
#endif
                // 패키지(배포 스테이징)는 `Content` 없이 실행 파일 옆에 `Packs` 만 둔다 - 그때 프로젝트 루트는 바이너리 폴더다. 둘 다 없으면 리소스를 찾을 곳이 없다.
                if ( project.empty() && FileUtil::isDirectory( FileUtil::joinPath( binary, constant::path::kPacksFolder ) ) )
                    project = binary;
                if ( project.empty() )
                    SW_FATAL( "ProcessRoots: no '%#' folder above the binaries '%#' and no '%#' beside them", constant::path::kContentFolder, binary, constant::path::kPacksFolder );

                ProcessRoots roots{
                    requireAbsolute( binary, "binary" ),
                    requireAbsolute( project, "project" ),
                    requireAbsolute( FileUtil::joinPath( project, constant::path::kContentFolder ), "content" ),
                    requireAbsolute( FileUtil::joinPath( binary, constant::path::kPacksFolder ), "packs" ),
                    requireAbsolute( FileUtil::joinPath( binary, constant::path::kSavedFolder ), "saved" ),
                    requireAbsolute( FileUtil::joinPath( project, constant::path::kSavedFolder ), "derived data" ),
                };
                return roots;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const ProcessRoots& ProcessRoots::get()
    {
        // 실행 파일 위치는 프로세스 동안 바뀌지 않는다 - 함수 정적으로 한 번 정한다(스레드 안전 초기화).
        static const ProcessRoots s_roots = ProcessRootsInternal::make();
        return s_roots;
    }
} // namespace sw
