/**
 * @file ResourceUtil.h
 * @brief 엔진 · 공통 · 게임 · 에디터 리소스의 루트 경로를 해석합니다.
 * @note
 *   - `Resource/` 는 표시 · 감시용 최상위일 뿐, getResourcePath 의 검색 루트가 아닙니다.
 *   - 검색 루트: `game/<pack>/`, `common/`, `engine/`, `editor/` (파일은 항상 이들 아래에 있습니다).
 *   - 키(소문자 정규형):
 *     - 팩 상대: `pipeline/foo.xml`, `shaders/bar.hlsl` → 검색 루트들을 차례로 봅니다
 *     - 전역 ID: `engine/...`, `common/...`, `game/<pack>/...`, `editor/...` → 그 도메인 루트만 봅니다
 *   - 없으면 빈 문자열입니다. Resource/ 에 붙여 억지로 성공시키지 않습니다.
 *   - 비교 · 맵 키는 FileUtil::normalizePath(전체 경로)를 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    class ResourcePackManager;

    /**
     * @class ResourceUtil
     * @brief 리소스 도메인 루트 · 검색 루트를 해석하고, 논리 경로 ↔ 절대 경로를 바꾸며, 마운트된 VFS .pack 에서 읽습니다.
     * @note 경로 I/O 만 맡습니다. 에셋 소유권은 ResourceManager 에 있습니다.
     */
    class SW_API ResourceUtil
    {
    public:
        /**
         * @brief 작업 디렉터리에서 위로 올라가며 `Resource/` 를 찾고, 도메인 · 검색 루트를 채웁니다.
         * @return 프로젝트 루트를 찾으면 true 입니다. 못 찾으면 false 이고 단정도 걸립니다.
         */
        static bool initialize();

        /**
         * @brief 상대 리소스 경로를 절대 경로로 해석합니다(파일이 있을 때만).
         * @param filePath 팩 상대 키 또는 `engine/`/`common/`/`game/<pack>/`/`editor/` 전역 ID
         * @param folderName 검색 루트마다 이 하위 폴더로 범위를 좁힙니다(비우면 루트 바로 아래부터)
         * @return I/O 에 쓸 수 있는 절대 경로입니다. 못 찾으면 빈 문자열입니다.
         * @note 소문자 키로 먼저 찾고, 못 찾으면 원래 표기로 한 번 더 찾습니다(대소문자를 가리는 파일시스템).
         */
        static string getResourcePath( string_view filePath, string_view folderName = "" );

        /**
         * @brief 논리 경로를 절대 경로로 바꿉니다(파일이 아직 없어도 도메인 루트 기준으로 조합합니다).
         * @param relativePath 전역 ID 또는 이미 있는 팩 상대 키
         * @return 절대 경로입니다. 전역 ID 가 아니면서 파일도 없으면 빈 문자열입니다.
         * @details 저장 · .meta 생성용입니다. 전역 ID(`engine/`/`common/`/`game/<pack>/`/`editor/`…)만 파일이 없어도 경로를 정할 수 있습니다.
         *          팩 상대 키는 이미 있는 경우에만 getResourcePath 결과를 반환합니다.
         */
        static string makeAbsolutePath( string_view relativePath );

        /**
         * @brief 아직 없는 파일을 **쓰기 위한** 절대 경로를 정합니다.
         * @param path 절대 경로 · 전역 ID · 이미 있는 팩 상대 키 중 무엇이든 받습니다.
         * @return 쓰기에 쓸 절대 경로입니다. 아무것도 해석되지 않으면 인자를 그대로 반환합니다.
         * @details 저장하는 쪽은 `getResourcePath` 만으로는 안 됩니다. 그것은 **이미 있는** 파일만
         *          찾으므로 새 파일에는 빈 문자열을 반환하고, 그러면 부르는 쪽이 상대 경로로 파일을 써서
         *          `Resource/` 가 아니라 **실행 파일의 작업 디렉터리**에 떨어집니다(`PrefabAsset` 이
         *          실제로 그랬습니다. 에셋은 Bin 아래, `.meta` 는 `Resource/` 아래로 갈라졌습니다).
         *          해석 순서를 여기 한 번만 적어 두고 모든 세이버가 같이 씁니다.
         */
        static string getWritePath( string_view path );

        /**
         * @brief 상대 리소스 경로를 해석해 텍스트로 읽습니다. 낱개 파일 우선이 켜져 있으면 디스크를 먼저, 꺼져 있으면 마운트된 팩만 봅니다(OS 절대 경로는 디스크에서 바로 읽습니다).
         * @param relativePath 팩 상대 키 또는 전역 ID(낱개 파일 우선일 때 경로를 풀지 못하면 인자 그대로 열어 봅니다)
         * @param outText 읽은 UTF-8 본문
         * @param pOutAbsPath 실제로 쓴 절대 경로(nullptr 가능)
         * @return 파일을 읽었으면 true 입니다.
         */
        static bool readTextResource( string_view relativePath, string& outText,
                                      string* pOutAbsPath = nullptr );

        /**
         * @brief 상대 리소스 경로의 바이너리 데이터를 로드합니다. 찾는 순서는 `readTextResource` 와 같습니다.
         * @param relativePath 팩 상대 키 또는 전역 ID
         * @param outBytes 읽은 바이너리 데이터
         * @return 파일을 읽었으면 true 입니다.
         */
        static bool readBinaryResource( string_view relativePath, vector<uint8>& outBytes );

        /**
         * @brief 리소스가 있는지 검사합니다(VFS 팩 또는 로컬 디스크 파일).
         * @param relativePath 팩 상대 키 또는 전역 ID
         * @return 있으면 true 입니다.
         */
        static bool hasResource( string_view relativePath );

        /** @brief VFS 팩 매니저를 반환합니다. */
        static ResourcePackManager& getPackManager();

        /**
         * @brief 도메인의 하위 폴더 절대 경로를 반환합니다(`Resource/<domainName>/<subFolder>`).
         * @param domainName 예: "engine", "common", "game/empty", "editor", "dlc/winter"
         * @param subFolder  예: "shaders", "textures", "prefabs"(비우면 도메인 루트)
         * @return 디렉터리가 있으면 정규화한 절대 경로, 없으면 빈 문자열
         */
        static string getDomainFolderPath( string_view domainName, string_view subFolder = {} );

        /** @brief `Resource/` 폴더의 절대 경로입니다(표시 · 감시용 최상위, 검색 루트 아님). */
        static const string& getRootFolderPath();
        /** @brief 프로젝트 루트의 절대 경로입니다(`Resource/` · `Config/` 의 부모). */
        static const string& getProjectFolderPath();

        /**
         * @brief 리소스 검색 루트 우선순위를 설정합니다(예: EngineConfig._listResourcePriority).
         * @param listPriority 우선순위 토큰 목록(예: "game", "common", "engine", "editor", "dlc/expansion1")
         * @return 우선순위를 다시 세웠으면 true 입니다.
         * @note 기존 경로 해석 캐시는 저절로 비워집니다.
         */
        static bool setSearchPriority( const vector<string>& listPriority );

        /** @brief 지금 쓰는 검색 우선순위 토큰 목록을 반환합니다. */
        static const vector<string>& getSearchPriority();

        /** @brief EngineConfig 리플렉션 기본값에 정의된 기본 검색 우선순위 목록을 반환합니다. */
        static const vector<string>& getDefaultSearchPriority();

        /** @brief 캐시해 둔 리소스 경로 해석 결과를 모두 비웁니다. */
        static void clearPathCache();

        /**
         * @brief 저장용 폴더 절대 경로를 만듭니다.
         * @param absoluteFolder 검색 · 도메인 루트 아래의 절대 폴더
         * @return 루트는 파일시스템 대소문자를 그대로 두고, 루트 아래 상대 구간만 소문자로 바꾼 경로
         */
        static string makeSaveFolderPath( string_view absoluteFolder );

        /**
         * @brief 저장 · 임포트용 절대 파일 경로를 만듭니다.
         * @param absoluteFolder 저장 대상 폴더(절대)
         * @param fileName 파일 이름. 소문자로 바꿉니다
         * @details absoluteFolder 가 속한 검색 루트는 파일시스템 대소문자를 그대로 두고,
         *          루트 아래 상대 폴더 · fileName 은 소문자로 바꿉니다.
         */
        static string makeSavePath( string_view absoluteFolder, string_view fileName );

        /**
         * @brief 저장 · 임포트용 절대 파일 경로를 만들되, **이미 있는 파일을 가리키지 않게** 합니다.
         * @details 경로 규칙은 `makeSavePath` 와 같고, 그 자리에 파일이 이미 있으면 확장자 앞에
         *          `_2` · `_3` … 을 붙여 비어 있는 이름을 찾습니다. 게임 오브젝트 이름을 고를 때
         *          쓰는 `GameObjectManager::makeUniqueNameUnlocked` 와 같은 규약입니다.
         * @note 이름을 고르는 것과 파일을 만드는 것 사이에는 틈이 있습니다. 그 사이에 다른 프로세스가
         *       같은 이름을 만들면 겹칠 수 있습니다. 에디터의 임포트는 한 스레드에서 돌므로 이것으로 충분합니다.
         * @return 비어 있는 절대 경로입니다. 후보를 다 써도 찾지 못하면 `makeSavePath` 와 같은 경로입니다.
         */
        static string makeUniqueSavePath( string_view absoluteFolder, string_view fileName );

    private:
        static atomic<bool>   _s_bInitialize;            ///< initialize() 가 성공했는지
        static string         _s_projectFolderPath;      ///< 프로젝트 루트
        static string         _s_resourceRootFolderPath; ///< Resource/ 최상위 루트(유일한 기준점)
        static vector<string> _s_listSearchPriority;     ///< 검색 우선순위 토큰 목록
        static vector<string> _s_listResourceFolder;     ///< getResourcePath 검색 루트들
    };
} // namespace sw
