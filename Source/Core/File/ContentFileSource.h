/**
 * @file ContentFileSource.h
 * @brief 콘텐츠(팩 · 낱개 리소스)를 읽는 자리입니다. 실제 파일 경로를 가정하지 않습니다.
 * @details Android 는 팩이 APK 안이라 경로로 열리지 않고, 웹은 파일이 없어 기동 때 내려받은 바이트를 메모리에 듭니다. 자리가 없으면 `#if` 가 `AssetIO` · `PackReader` 로 번진다.
 *          언리얼 `IPlatformFile` 체인 · Godot `FileAccess` + `PackedData` 와 같은 자리다(기억). 콘텐츠만 여기를 지난다 — 사용자 저장 · 로그 · 개발 도구는 `FileUtil` 그대로다(웹 OPFS · 앱 전용 폴더가 경로로 보인다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 콘텐츠를 읽는 쪽의 계약입니다. 경로는 이 소스가 뜻을 정하는 글이고, 구현은 여러 스레드가 동시에 불러도 됩니다. */
    class IContentFileSource
    {
    public:
        virtual ~IContentFileSource() = default;

        /** @brief 구간 읽기용으로 엽니다. 없으면 `isValid() == false` 입니다. */
        [[nodiscard]] virtual AsyncFileHandle open( string_view path ) const = 0;
        /** @brief 파일이 있으면 true 입니다(폴더는 아니다). */
        [[nodiscard]] virtual bool isFile( string_view path ) const = 0;
        /** @brief 폴더가 있으면 true 입니다. */
        [[nodiscard]] virtual bool isFolder( string_view path ) const = 0;
        /** @brief 폴더 바로 아래 파일의 경로를 모읍니다. 읽지 못하면 false 입니다. */
        [[nodiscard]] virtual bool collectFiles( string_view folder, vector<string>& outListPath ) const = 0;
        /** @brief 파일 전체를 바이트로 읽습니다. */
        [[nodiscard]] virtual bool readFile( string_view path, vector<uint8>& outBytes ) const = 0;
        /** @brief 파일 전체를 UTF-8 글로 읽습니다(BOM 을 뗀다). */
        [[nodiscard]] virtual bool readTextFile( string_view path, string& outText ) const = 0;
        /** @brief 파일 전체를 @p io 로 비동기로 읽습니다. 열기가 부른 스레드를 막지 않는 길이 있으면 그 길로 간다. */
        virtual AsyncReadHandle readFileAsync( AsyncFileIO& io, string_view path, AsyncIOPriority priority, const AsyncReadCompleteDelegate& onComplete ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 실제 파일 시스템입니다. 데스크톱의 콘텐츠 소스이고 `FileUtil` 그대로입니다. */
    class SW_API DiskContentFileSource final : public IContentFileSource
    {
    public:
        AsyncFileHandle open( string_view path ) const override;
        bool            isFile( string_view path ) const override;
        bool            isFolder( string_view path ) const override;
        bool            collectFiles( string_view folder, vector<string>& outListPath ) const override;
        bool            readFile( string_view path, vector<uint8>& outBytes ) const override;
        bool            readTextFile( string_view path, string& outText ) const override;
        AsyncReadHandle readFileAsync( AsyncFileIO& io, string_view path, AsyncIOPriority priority, const AsyncReadCompleteDelegate& onComplete ) const override;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 메모리에 든 파일들입니다. 웹이 기동 때 내려받은 팩을 여기에 담습니다.
     * @details 채우기(`addFile`)는 소스를 걸기 전에 끝낸다 — 건 뒤에는 읽기만 하므로 잠금이 없다. 경로의 구분자는 `/` 로 맞추고 대소문자는 가린다.
     */
    class SW_API MemoryContentFileSource final : public IContentFileSource
    {
    public:
        MemoryContentFileSource();

        /** @brief 파일 하나를 담습니다. 같은 경로면 덮어씁니다. */
        void addFile( string_view path, vector<uint8> bytes );

        AsyncFileHandle open( string_view path ) const override;
        bool            isFile( string_view path ) const override;
        bool            isFolder( string_view path ) const override;
        bool            collectFiles( string_view folder, vector<string>& outListPath ) const override;
        bool            readFile( string_view path, vector<uint8>& outBytes ) const override;
        bool            readTextFile( string_view path, string& outText ) const override;
        AsyncReadHandle readFileAsync( AsyncFileIO& io, string_view path, AsyncIOPriority priority, const AsyncReadCompleteDelegate& onComplete ) const override;

    private:
        unordered_map<string, shared_ptr<const vector<uint8>>> _mapFile;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 이 프로세스의 콘텐츠 소스 자리입니다. 엔진 없이 도는 도구(쿠커)도 팩을 읽으므로 서브시스템 칸이 아니라 프로세스 범위다.
     * @details 기본은 플랫폼 것이다(데스크톱은 `DiskContentFileSource`. Android · 웹은 그 플랫폼의 기동이 소스를 건 뒤에 읽는다).
     */
    struct SW_API ContentFileSource
    {
        /** @brief 지금 소스입니다. */
        static const IContentFileSource& getCurrent();
        /**
         * @brief 소스를 바꾸고 이전 것을 돌려줍니다. nullptr 이면 플랫폼 기본으로 돌아갑니다. 읽기 스레드가 돌기 전(기동)이나 테스트에서만 부릅니다.
         * @param pSource 호출자가 소유한다. 다시 바꿀 때까지 살아 있어야 한다.
         */
        static const IContentFileSource* bindCurrent( const IContentFileSource* pSource );
    };
} // namespace sw
