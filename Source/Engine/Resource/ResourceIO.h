/**
 * @file ResourceIO.h
 * @brief 리소스 id 를 해석해 낱개 파일 또는 마운트된 VFS .pack 에서 읽습니다. 경로 해석은 ResourcePaths 가 맡습니다.
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

    /** @brief 리소스 하나의 비동기 읽기가 끝났을 때 불립니다. 성공이면 다 읽은(팩이면 풀고 CRC 까지 맞춘) 바이트이고, 옮겨 가져도 됩니다. */
    using ResourceReadCompleteDelegate = Delegate<void( bool bSuccess, vector<uint8>& bytes )>;

    /**
     * @class ResourceIO
     * @brief 리소스를 읽는 창구입니다(동기 · 비동기 · 존재 확인 · 팩 매니저).
     * @note 찾는 순서는 하나입니다 — OS 절대 경로 · 낱개 파일 · 마운트된 팩. 에셋 소유권은 AssetManager 에 있습니다.
     */
    class SW_API ResourceIO
    {
    public:
        /**
         * @brief 상대 리소스 경로를 해석해 텍스트로 읽습니다. 낱개 파일 우선이 켜져 있으면 디스크를 먼저, 꺼져 있으면 마운트된 팩만 봅니다(OS 절대 경로는 디스크에서 바로 읽습니다).
         * @param relativePath 팩 상대 키 또는 전역 ID(낱개 파일 우선일 때 경로를 풀지 못하면 인자 그대로 열어 봅니다)
         * @param outText 읽은 UTF-8 본문
         * @param pOutAbsPath 실제로 쓴 절대 경로(nullptr 가능)
         * @return 파일을 읽었으면 true 입니다.
         */
        [[nodiscard]] static bool readTextResource( string_view relativePath, string& outText,
                                                    string* pOutAbsPath = nullptr );

        /**
         * @brief 상대 리소스 경로의 바이너리 데이터를 로드합니다. 찾는 순서는 `readTextResource` 와 같습니다.
         * @param relativePath 팩 상대 키 또는 전역 ID
         * @param outBytes 읽은 바이너리 데이터
         * @return 파일을 읽었으면 true 입니다.
         */
        [[nodiscard]] static bool readBinaryResource( string_view relativePath, vector<uint8>& outBytes );

        /**
         * @brief 리소스 하나를 비동기로 읽습니다. 찾는 순서는 `readBinaryResource` 와 같고(OS 절대 경로 · 낱개 파일 · 팩), 읽기는
         *        `engine::getSubsystem<AsyncFileIO>()` 가 합니다. 팩 항목의 압축 해제 · CRC 는 완료를 받은 태스크 워커에서 합니다.
         * @details 찾는 일(낱개 파일의 존재 확인 · 팩 색인)은 부른 스레드에서 하고, 바이트 읽기만 IO 스레드로 갑니다.
         * @return 건 읽기의 핸들(취소 · 기다리기). 어디에도 없으면 아무것도 걸지 않고 `isValid() == false` 이며 @p onComplete 는 불리지 않습니다.
         */
        static AsyncReadHandle readBinaryResourceAsync( string_view relativePath, AsyncIOPriority priority, const ResourceReadCompleteDelegate& onComplete );

        /**
         * @brief 리소스가 있는지 검사합니다(VFS 팩 또는 로컬 디스크 파일).
         * @param relativePath 팩 상대 키 또는 전역 ID
         * @return 있으면 true 입니다.
         */
        static bool hasResource( string_view relativePath );

        /** @brief VFS 팩 매니저를 반환합니다. */
        static ResourcePakManager& getPackManager();
    };
} // namespace sw
