/**
 * @file ResourcePaths.h
 * @brief 엔진, 공통, 게임, 에디터 리소스의 루트 경로를 해석합니다.
 * @note
 *   - `Resource/` 는 표시와 감시용 최상위일 뿐, findLooseFile 의 검색 루트가 아닙니다.
 *   - 검색 루트는 `game/<pack>/`, `common/`, `engine/`, `editor/` 이고, 파일은 항상 이들 아래에 있습니다.
 *   - 키(소문자 정규형)는 두 종류입니다.
 *     - 팩 상대 키: `pipeline/foo.xml`, `shaders/bar.hlsl` 같은 키는 검색 루트를 차례로 봅니다.
 *     - 전역 ID: `engine/...`, `common/...`, `game/<pack>/...`, `editor/...` 는 그 도메인 루트만 봅니다.
 *   - 없으면 빈 문자열을 반환합니다. Resource/ 에 붙여 억지로 성공시키지 않습니다.
 *   - 비교와 맵 키에는 FileUtil::normalizePath(전체 경로)를 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Resource/LooseFilePath.h"

namespace sw
{
    /**
     * @class ResourcePaths
     * @brief 리소스 도메인 루트와 검색 루트를 해석하고, 논리 경로와 절대 경로를 서로 바꾸며, 저장 경로를 정합니다.
     * @note 읽기는 ResourceIO 가 맡습니다. 에셋 소유권은 AssetManager 에 있습니다.
     */
    class SW_API ResourcePaths
    {
    public:
        /**
         * @brief 작업 디렉터리에서 위로 올라가며 `Resource/` 를 찾고, 도메인 루트와 검색 루트를 채웁니다.
         * @return 프로젝트 루트를 찾으면 true 입니다. 못 찾으면 false 를 반환하고 단언도 실패합니다.
         */
        [[nodiscard]] static bool initialize();

        /**
         * @brief 상대 리소스 경로를 **디스크의 낱개 파일**로 풉니다(파일이 있을 때만). 팩은 보지 않습니다.
         * @param filePath 팩 상대 키 또는 `engine/`/`common/`/`game/<pack>/`/`editor/` 전역 ID
         * @param folderName 검색 루트마다 이 하위 폴더로 범위를 좁힙니다(비우면 루트 바로 아래부터)
         * @return 디스크 경로입니다. 못 찾으면 `isEmpty()` 입니다. 런타임 리소스 읽기 · 나열은 이 결과가 아니라 `ResourceIO` 를 씁니다 —
         *         결과 형식(`LooseFilePath`)이 `string` 으로 저절로 바뀌지 않는 까닭입니다.
         * @note 소문자 키로 먼저 찾고, 못 찾으면 원래 표기로 한 번 더 찾습니다(대소문자를 가리는 파일시스템).
         */
        static LooseFilePath findLooseFile( string_view filePath, string_view folderName = "" );

        /**
         * @brief 리소스 id 의 파일을 **쓸 자리**의 절대 경로를 정합니다(파일이 아직 없어도 도메인 루트 기준으로 조합합니다). 읽기용은 `findLooseFile` 입니다.
         * @param resourceID 절대 경로, 전역 ID, 이미 있는 팩 상대 키 중 무엇이든 받습니다(절대 경로는 구분자만 맞춥니다).
         * @return 이미 있는 파일이면 그 디스크 경로입니다. 없으면 도메인 루트 아래에 이은 경로이고, 전역 ID 도 붙인 도메인 id 도 아니거나 `..` 로 올라가면 빈 문자열입니다.
         * @details 저장과 .meta 생성에 씁니다. 전역 ID(`engine/`/`common/`/`game/<pack>/`/`editor/`…)와 붙인 디스크 도메인(`mountDiskDomain`)만 파일이 없어도 경로를 정할 수 있습니다.
         *          팩 상대 키는 이미 있는 경우에만 `findLooseFile` 결과를 반환합니다.
         *          **대소문자.** 리소스 id 는 소문자인데 붙인 디스크 도메인의 폴더는 대소문자가 섞일 수 있다. 대소문자를 구분하는 파일 시스템(Linux)에서는 이미 있는 폴더
         *          성분은 디스크 표기를 쓰고(`Saved/Automation` 이 있으면 `saved/automation/x` 도 그 안에 쓴다), 없는 성분부터는 id 의 표기를 씁니다. 리소스 트리의 도메인은
         *          소문자 규칙이라 모든 플랫폼에서 소문자로 잇습니다. 읽기용 `findLooseFile` 이 붙인 디스크 도메인에서 푸는 규칙과 짝입니다.
         */
        static string resolveWritePath( string_view resourceID );

        /**
         * @brief 아직 없는 파일을 **쓰기 위한** 절대 경로를 정합니다.
         * @param path 절대 경로, 전역 ID, 이미 있는 팩 상대 키 중 무엇이든 받습니다.
         * @return 쓰기에 쓸 절대 경로입니다. 아무것도 해석되지 않으면 인자를 그대로 반환합니다.
         * @details 저장하는 쪽은 `findLooseFile` 만으로는 안 된다. 그 함수는 **이미 있는** 파일만
         *          찾으므로 새 파일에는 빈 문자열을 반환하고, 그러면 부르는 쪽이 상대 경로로 파일을 써서
         *          `Resource/` 가 아니라 **실행 파일의 작업 디렉터리**에 떨어진다. 예를 들어 `PrefabAsset` 이 이 방식으로 저장하면
         *          에셋은 Bin 아래로, `.meta` 는 `Resource/` 아래로 갈라진다.
         *          해석 순서를 여기 한 번만 적어 두고 모든 세이버가 같이 쓴다(`resolveWritePath` 에 리소스 트리 밖으로 나가는 경로의 거절과 마지막 물러남을 더한 것이다).
         */
        static string getWritePath( string_view path );

        /**
         * @brief 편집기와 도구가 가진 경로를 리소스 id(전역 id. 예: `game/empty/maps/x.scene.xml`, 소문자와 `/`)로 바꿉니다.
         * @param path 절대 경로, 프로젝트 기준 경로(`Resource/…`), 이미 리소스 id 인 경로 중 무엇이든 받습니다.
         * @return 리소스 id 입니다. 붙인 디스크 도메인(`mountDiskDomain`) 폴더 아래의 절대 경로면 `<도메인>/…` 입니다. 리소스 루트 밖이거나 `..` 로 올라가면 빈 문자열입니다.
         * @details 편집기는 변환을 곳곳에서 따로 하지 않고 이 함수를 쓴다. 프로젝트 루트 기준 `makeRelativePath` 를 리소스 id 에 다시 적용하면
         *          경로가 깨진다(텍스처를 끌어다 놓은 스프라이트가 흰 사각형이 된다).
         */
        static string toResourceID( string_view path );

        /**
         * @brief 리소스 id 에 `..` 성분이 있으면 true 입니다. 리소스 루트 밖으로 나가는 id 입니다.
         * @details `findLooseFile` 과 `ResourceIO` 의 읽기가 이런 id 를 해석하지 않는 기준 하나다.
         */
        static bool hasParentDirectoryComponent( string_view path );

        /**
         * @brief 이 프로세스를 띄운 호스트의 빌드 타깃(`"Client"`, `"Server"`)을 정합니다. 그 타깃이 패키지에서 빼는 에셋 묶음
         *        (`Config/Engine/CookContract.json` 의 `target_excluded_asset_kinds` — 묶음은 종류 표 `AssetTypeDef::_excludeGroup`. 서버는 텍스처, 셰이더 바이너리, 오디오)는 이 호출 뒤부터
         *        **없는 것**으로 취급합니다. 읽기는 조용히 false 를 반환하고 `hasResource` 도 false 입니다("파일 없음" 경고와 오류를 남기지 않습니다).
         * @details 팩 쿠커(`PackCooker`, `isExcludedForTarget`)가 같은 테이블로 그 파일을 팩에서 뺀다. 엔진 기동 첫머리(`Engine::initialize`)에서
         *          한 번 부르고, 그 뒤로는 읽기만 한다. 비우면(`""`) 아무것도 빼지 않는다(테스트 하네스).
         */
        static void setHostTarget( string_view buildTargetName );
        /** @brief @p resourcePath(리소스 id, 팩 상대 키, 절대 경로)가 이 호스트가 읽지 않는 에셋 종류면 true 입니다(`setHostTarget`). */
        static bool isExcludedForHost( string_view resourcePath );
        /**
         * @brief @p resourcePath 가 빌드 타깃 @p buildTargetName(`Game` · `Client` · `Server`)이 패키지에서 빼는 에셋 종류면 true 입니다. 빈 타깃은 아무것도 빼지 않습니다.
         * @details 팩 쿠커(`PackCooker`)가 그 타깃의 팩을 구울 때 쓴다. 호스트 상태(`setHostTarget`)를 바꾸지 않고 같은 표 · 같은 맞춤 규칙을 쓴다.
         */
        static bool isExcludedForTarget( string_view buildTargetName, string_view resourcePath );

        /**
         * @brief 도메인의 하위 폴더 절대 경로를 반환합니다(`Resource/<domainName>/<subFolder>`).
         * @param domainName 예: "engine", "common", "game/empty", "editor", "dlc/winter"
         * @param subFolder  예: "shaders", "textures", "prefabs"(비우면 도메인 루트)
         * @return 디스크에 디렉터리가 있으면 정규화한 절대 경로, 없으면 `isEmpty()`. 팩은 보지 않습니다(에디터 · 쿠커 · 도구용).
         */
        static LooseFilePath findLooseDomainFolder( string_view domainName, string_view subFolder = {} );

        /**
         * @brief 리소스 루트 **밖**의 디스크 폴더 하나를 도메인 이름으로 붙입니다. 그 도메인의 id(`<domainName>/<key>`)는 그 폴더의 파일로 풀립니다.
         * @details 리소스 입구(`ResourceIO::read*Resource` · `hasResource`)는 리소스 id 만 받고 OS 경로는 오류로 거절합니다(계획 `DiskPathVsResourceId` 안 B).
         *          루트 밖 디스크 파일(테스트 케이스 임시 폴더, 도구 작업 폴더)을 캐시 · 로더가 리소스로 읽어야 할 때 이 함수로 붙이고 id 로 부릅니다.
         *          붙인 도메인은 낱개 파일 금지 구성(배포본)에서도 디스크에서 읽습니다 — 붙인 쪽이 고른 것이고, 팩에는 없습니다.
         *          리소스 루트 **안**의 폴더는 붙일 수 없습니다(배포본의 팩 전용 규칙을 지나 낱개 에셋을 읽는 길이 된다).
         * @return 이름이 비었거나 `/` · `:` · `..` 가 들었거나, 폴더가 없거나, 리소스 루트 안이거나, 같은 이름이 다른 폴더에 붙어 있으면 false 입니다(오류 로그).
         *         같은 이름을 같은 폴더에 다시 붙이면 true 입니다.
         */
        [[nodiscard]] static bool mountDiskDomain( string_view domainName, string_view diskFolder );
        /** @brief @p resourceID 가 `mountDiskDomain` 으로 붙인 도메인 아래면 true 입니다. 그 id 는 팩을 보지 않고 디스크에서만 읽습니다. */
        static bool isMountedDiskDomainID( string_view resourceID );

        /** @brief `Resource/` 폴더의 절대 경로입니다. 표시와 감시용 최상위이며 검색 루트는 아닙니다. */
        static const string& getRootFolderPath();
        /** @brief 프로젝트 루트의 절대 경로입니다(`Resource/` 와 `Config/` 의 부모). */
        static const string& getProjectFolderPath();

        /**
         * @brief 리소스 검색 루트 우선순위를 설정합니다(예: EngineConfig._listResourcePriority).
         * @param listPriority 우선순위 토큰 목록(예: "game", "common", "engine", "editor", "dlc/expansion1")
         * @return 우선순위를 다시 설정했으면 true 입니다.
         * @note 기존 경로 해석 캐시는 저절로 비워집니다.
         * @warning 검색 목록 · 루트 · 호스트 제외 그룹은 락 없이 읽는다. 쓰는 API(`setSearchPriority` · `exchangeRootFolderPath` · `setHostTarget`)는 병렬 단계(워커 · 병렬 본문) 밖,
         *          메인 스레드의 직렬 구간에서만 부른다 — 어기면 로그 단언이 남는다.
         */
        static bool setSearchPriority( const vector<string>& listPriority );

        /** @brief 지금 쓰는 검색 우선순위 토큰 목록을 반환합니다. */
        static const vector<string>& getSearchPriority();

        /**
         * @brief 리소스 루트(`Resource/` 폴더)를 바꾸고 검색 폴더와 경로 캐시를 다시 만듭니다. 이전 루트를 반환합니다(되돌릴 때 넘깁니다).
         * @details 테스트가 임시 폴더에 개별 파일을 두고 같은 키를 가진 팩과 우선순위를 비교할 때 쓴다(검색 폴더는 루트에서만 채워진다).
         * @warning 실행 중인 게임에서 부르면 안 된다. 다른 스레드의 경로 해석과 겹치면 중간 상태를 본다.
         */
        static string exchangeRootFolderPath( string_view resourceRootFolderPath );

        /** @brief EngineConfig 리플렉션 기본값에 정의된 기본 검색 우선순위 목록을 반환합니다. */
        static const vector<string>& getDefaultSearchPriority();

        /**
         * @brief 캐시해 둔 리소스 경로 해석 결과를 모두 비우고 그 저장소까지 해제합니다. 종료 때의 반납은 캐시를 소유한 엔진 수명 범위가 합니다.
         * @details 부르는 때: 검색 목록이 바뀔 때, 에디터의 파일 감시가 리소스 트리의 변경을 알릴 때(`FileWatchDispatcher`). 이 프로세스의 `FileUtil` 이 만들거나 지운 파일은
         *          부르지 않아도 된다 — 캐시 칸이 `FileUtil::getCreationEpoch` · `getRemovalEpoch` 로 스스로 낡음을 안다. 바깥 프로세스가 바꾼 파일만 이 호출이 필요하다.
         */
        static void clearPathCache();

        /**
         * @brief 저장용 폴더 절대 경로를 만듭니다.
         * @param absoluteFolder 검색 루트나 도메인 루트 아래의 절대 폴더
         * @return 루트는 파일시스템 대소문자를 그대로 두고, 루트 아래 상대 구간만 소문자로 바꾼 경로
         */
        static string makeSaveFolderPath( string_view absoluteFolder );

        /**
         * @brief 저장과 임포트에 쓸 절대 파일 경로를 만듭니다.
         * @param absoluteFolder 저장 대상 폴더(절대)
         * @param fileName 파일 이름. 소문자로 바꿉니다
         * @details absoluteFolder 가 속한 검색 루트는 파일시스템 대소문자를 그대로 두고,
         *          루트 아래 상대 폴더와 fileName 은 소문자로 바꿉니다.
         */
        static string makeSavePath( string_view absoluteFolder, string_view fileName );

        /**
         * @brief 저장과 임포트에 쓸 절대 파일 경로를 만들되, **이미 있는 파일을 가리키지 않게** 합니다.
         * @details 경로 규칙은 `makeSavePath` 와 같고, 그 위치에 파일이 이미 있으면 확장자 앞에
         *          `_2`, `_3` 등을 붙여 비어 있는 이름을 찾는다. 게임 오브젝트 이름을 고를 때
         *          쓰는 `GameObjectManager::makeUniqueNameUnlocked` 와 같은 규약이다.
         * @note 이름을 고르는 것과 파일을 만드는 것 사이에는 틈이 있다. 그 사이에 다른 프로세스가
         *       같은 이름을 만들면 겹칠 수 있다. 에디터의 임포트는 한 스레드에서 돌므로 이것으로 충분하다.
         * @return 비어 있는 절대 경로입니다. 후보를 다 써도 찾지 못하면 `makeSavePath` 와 같은 경로입니다.
         */
        static string makeUniqueSavePath( string_view absoluteFolder, string_view fileName );

    private:
        static atomic<bool>   _s_bInitialize;            ///< initialize() 가 성공했는지
        static string         _s_projectFolderPath;      ///< 프로젝트 루트
        static string         _s_resourceRootFolderPath; ///< Resource/ 최상위 루트(유일한 기준점)
        static vector<string> _s_listSearchPriority;     ///< 검색 우선순위 토큰 목록
        static vector<string> _s_listResourceFolder;     ///< findLooseFile 의 검색 루트 목록
        static vector<string> _s_listHostExcludedGroup;  ///< 이 호스트가 읽지 않는 에셋 묶음(`Texture` · `ShaderBinary` · `Audio`, 종류 표의 `_excludeGroup`)
    };
} // namespace sw
