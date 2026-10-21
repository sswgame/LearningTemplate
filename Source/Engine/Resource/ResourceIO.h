/**
 * @file ResourceIO.h
 * @brief 리소스 id 를 해석해 낱개 파일 또는 마운트된 VFS .pack 에서 읽습니다. 경로 해석은 ResourcePaths 가 맡습니다.
 * @details 입구는 둘로 나뉩니다. 리소스 입구(`read*Resource` · `hasResource` · `collectResources`)는 리소스 id 만 받고, 디스크 입구(`readTextFile` ·
 *          `readBinaryFile`)는 디스크 경로만 받습니다. 리소스 입구에 OS 경로를 넣으면 구성과 무관하게 오류로 멈춥니다 — 예전에는 Dev 가 받고 배포본만
 *          거절해 잘못 넣은 호출이 배포 테스트에서야 드러났습니다(계획 `DiskPathVsResourceId`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

namespace sw
{
    enum class AsyncIOPriority : uint8;

    class AsyncReadHandle;
    class ResourcePakManager;

    /** @brief 리소스 폴더 나열(`ResourceIO::collectResources`)의 결과입니다. 폴더가 없는 것과 나열에 실패한 것을 가릅니다. */
    enum class ResourceFolderListing : uint8
    {
        Listed,        ///< 폴더가 있었다(낱개 폴더 또는 팩 항목). 거른 결과가 비어 있을 수 있다
        FolderMissing, ///< 어느 팩에도, (낱개 파일을 허용하면) 디스크에도 그 폴더가 없다
        Failed,        ///< 폴더 인자가 리소스 id 가 아니거나 디스크 폴더를 읽지 못했다(오류 로그를 남겼다)
    };

    /** @brief 리소스 하나의 비동기 읽기가 끝났을 때 불립니다. 성공이면 다 읽은(팩이면 풀고 CRC 까지 맞춘) 바이트이고, 옮겨 가져도 됩니다. */
    using ResourceReadCompleteDelegate = Delegate<void( bool bSuccess, vector<uint8>& bytes )>;

    /**
     * @class ResourceIO
     * @brief 리소스를 읽는 창구입니다(동기 · 비동기 · 존재 확인 · 팩 매니저).
     * @note 찾는 순서는 하나입니다 — 낱개 파일(허용될 때, 붙인 디스크 도메인은 늘) · 마운트된 팩. 에셋 소유권은 AssetManager 에 있습니다.
     */
    class SW_API ResourceIO
    {
    public:
        /**
         * @brief 리소스 id 를 해석해 텍스트로 읽습니다. 낱개 파일 우선이 켜져 있으면 디스크를 먼저, 꺼져 있으면 마운트된 팩만 봅니다.
         *        **OS 경로는 받지 않습니다** — 모든 구성에서 오류 로그를 남기고 false 입니다(계획 `DiskPathVsResourceId` 안 B). 디스크 파일은
         *        `readTextFile` 로, 루트 밖 폴더를 리소스로 읽으려면 `ResourcePaths::mountDiskDomain` 으로 붙여 id 로 읽습니다.
         * @param relativePath 팩 상대 키 또는 전역 ID. 풀지 못하면 false 입니다(작업 폴더 기준 경로로 다시 열어 보지 않는다)
         * @param outText 읽은 UTF-8 본문
         * @param pOutAbsPath 실제로 쓴 절대 경로(nullptr 가능)
         * @return 파일을 읽었으면 true 입니다.
         */
        [[nodiscard]] static bool readTextResource( string_view relativePath, string& outText,
                                                    string* pOutAbsPath = nullptr );
        /**
         * @brief 디스크 경로(절대 경로 또는 작업 폴더 기준)의 파일을 텍스트로 읽습니다. 리소스 id 로 풀지 않습니다.
         * @details 경로로 읽는 문서 로더(`XMLDocument::loadFile` · `JSONDocument::loadFile`)가 쓴다. 쿠킹 작업 안이면 그 작업의 입력으로 기록하고,
         *          쿠킹 세션 안인데 작업 밖이면 실패한다(`CookJob::admitRead`) — `FileUtil` 로 바로 읽으면 기록에서 빠진다.
         */
        [[nodiscard]] static bool readTextFile( string_view filePath, string& outText );
        /** @brief 디스크 경로의 파일을 바이트로 읽습니다. 리소스 id 로 풀지 않습니다. 쿠킹 기록 규칙은 `readTextFile` 과 같습니다. */
        [[nodiscard]] static bool readBinaryFile( string_view filePath, vector<uint8>& outBytes );

        /**
         * @brief 상대 리소스 경로의 바이너리 데이터를 로드합니다. 찾는 순서는 `readTextResource` 와 같습니다.
         * @param relativePath 팩 상대 키 또는 전역 ID
         * @param outBytes 읽은 바이너리 데이터
         * @return 파일을 읽었으면 true 입니다.
         */
        [[nodiscard]] static bool readBinaryResource( string_view relativePath, vector<uint8>& outBytes );

        /**
         * @brief 리소스 하나를 비동기로 읽습니다. 찾는 순서는 `readBinaryResource` 와 같고(낱개 파일 · 팩, OS 경로는 오류), 읽기는
         *        `engine::getSubsystem<AsyncFileIO>()` 가 합니다. 팩 항목의 압축 해제 · CRC 는 완료를 받은 태스크 워커에서 합니다.
         * @details 찾는 일(낱개 파일의 존재 확인 · 팩 색인)은 부른 스레드에서 하고, 바이트 읽기만 IO 스레드로 갑니다.
         * @return 건 읽기의 핸들(취소 · 기다리기). 어디에도 없으면 아무것도 걸지 않고 `isValid() == false` 이며 @p onComplete 는 불리지 않습니다.
         */
        static AsyncReadHandle readBinaryResourceAsync( string_view relativePath, AsyncIOPriority priority, const ResourceReadCompleteDelegate& onComplete );

        /**
         * @brief 리소스가 있는지 검사합니다(VFS 팩 또는 낱개 파일). 찾는 규칙은 읽기와 같습니다 — OS 경로는 오류 로그를 남기고 false 입니다.
         * @param relativePath 팩 상대 키 또는 전역 ID
         * @return 있으면 true 입니다.
         */
        static bool hasResource( string_view relativePath );

        /**
         * @brief 리소스 폴더 바로 아래 파일(하위 폴더 안은 빼고)의 리소스 id 를 모읍니다. 런타임 코드가 리소스 폴더를 훑는 길은 이것 하나입니다 —
         *        `FileUtil::collectFiles` 는 디스크만 보므로 팩으로 쿠킹한 빌드에서 폴더가 비어 보입니다.
         * @param relativeFolder 리소스 폴더 id(예: "game/shooter3d/data/appearance/presets"). OS 절대 경로와 `..` 는 받지 않습니다.
         * @param extension 이름 끝 거르기(대소문자 무시, 예: ".appearance.xml"). 빈 값이면 모두입니다.
         * @param outListPath 찾은 리소스 id. 마운트된 모든 팩과, 낱개 파일을 허용하면 디스크 폴더의 합집합입니다. 대소문자를 무시해 중복을 없애고
         *                    (디스크 · 높은 우선순위 팩의 표기가 남는다) 소문자 기준 이름순으로 정렬합니다 — 실행마다 같습니다.
         * @return 폴더가 없으면 `FolderMissing`, 인자가 잘못됐거나 디스크 폴더를 읽지 못하면 `Failed` 입니다. 그 둘이면 @p outListPath 는 비어 있습니다.
         */
        [[nodiscard]] static ResourceFolderListing collectResources( string_view relativeFolder, string_view extension, vector<string>& outListPath );

        /** @brief VFS 팩 매니저를 반환합니다. */
        static ResourcePakManager& getPackManager();
    };
} // namespace sw
