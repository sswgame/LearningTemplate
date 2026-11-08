/**
 * @file BundledContentFileSource.h
 * @brief 앱 묶음(Android APK 의 `assets/`) 안 폴더 하나를 파일 시스템 경로 아래에 걸어 읽는 콘텐츠 소스입니다.
 * @details 묶음 안 파일은 경로로 열리지 않는다. 엔진은 팩 폴더를 늘 `ProcessRoots::_packs` 경로로 묻는다 — 이 소스가 그 경로 아래를 묶음 안 이름(`Packs/<파일>`)으로 옮겨
 *          묶음 읽기(`IBundledFileReader` — Android 는 `AndroidAssetFileReader`, 테스트는 가짜)에 넘긴다. 걸린 폴더 밖 경로는 없는 파일이다(디스크로 가지 않는다).
 *          언리얼 `FAndroidPlatformFile`(APK 안 OBB · 에셋을 경로로 보이게 한다) · Godot `FileAccessAndroid`(`res://` → AAssetManager)와 같은 자리다(기억).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/ContentFileSource.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @brief 앱 묶음 안 파일을 읽는 쪽의 계약입니다. 이름은 묶음 안 상대 경로(`/` 구분, 앞 구분자 없음)입니다. 여러 스레드가 동시에 불러도 됩니다.
     */
    class IBundledFileReader
    {
    public:
        virtual ~IBundledFileReader() = default;

        /** @brief 구간 읽기용으로 엽니다. 없거나 묶음 안에서 압축돼 바로 읽을 수 없으면 `isValid() == false` 입니다(압축은 구현이 오류로 알린다). */
        [[nodiscard]] virtual AsyncFileHandle open( string_view bundledName ) const = 0;
        /** @brief 폴더 @p bundledFolder 바로 아래 파일의 이름(폴더 이름을 뺀 것)을 모읍니다. 하위 폴더는 들지 않는다. 읽지 못하면 false 입니다. */
        [[nodiscard]] virtual bool collectFileNames( string_view bundledFolder, vector<string>& outListFileName ) const = 0;
    };

    /**
     * @class BundledContentFileSource
     * @brief 파일 시스템 경로 @p mountRoot 아래를 묶음 안 폴더 @p bundledFolder 로 옮겨 읽습니다. 파일 머리말 참고.
     */
    class SW_API BundledContentFileSource final : public IContentFileSource
    {
    public:
        /**
         * @param reader 호출자가 소유한다. 이 소스보다 오래 살아야 한다.
         * @param mountRoot 엔진이 묻는 폴더 경로(`ProcessRoots::_packs`)입니다.
         * @param bundledFolder 묶음 안 폴더 이름(`Packs`)입니다.
         */
        BundledContentFileSource( const IBundledFileReader& reader, string_view mountRoot, string_view bundledFolder );

        /**
         * @brief 경로 @p path 를 묶음 안 이름으로 옮깁니다. @p mountRoot 자체는 @p bundledFolder, 그 아래는 `<bundledFolder>/<나머지>` 입니다.
         * @return 걸린 폴더 밖이면 false 입니다. 구분자는 `/` 로 맞추고 대소문자는 가린다(묶음 이름은 대소문자를 가린다).
         */
        [[nodiscard]] static bool tryMapToBundledName( string_view path, string_view mountRoot, string_view bundledFolder, string& outBundledName );

        AsyncFileHandle open( string_view path ) const override;
        bool            isFile( string_view path ) const override;
        bool            isFolder( string_view path ) const override;
        bool            collectFiles( string_view folder, vector<string>& outListPath ) const override;
        bool            readFile( string_view path, vector<uint8>& outBytes ) const override;
        bool            readTextFile( string_view path, string& outText ) const override;
        AsyncReadHandle readFileAsync( AsyncFileIO& io, string_view path, AsyncIOPriority priority, const AsyncReadCompleteDelegate& onComplete ) const override;

    private:
        const IBundledFileReader& _reader;
        string                    _mountRoot;     ///< 구분자를 `/` 로 맞추고 끝 구분자를 뗀 경로
        string                    _bundledFolder; ///< 끝 구분자를 뗀 묶음 안 폴더 이름
    };
} // namespace sw
