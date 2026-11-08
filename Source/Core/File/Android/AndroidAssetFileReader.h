/**
 * @file AndroidAssetFileReader.h
 * @brief APK 의 `assets/` 를 AAssetManager 로 읽는 묶음 읽기입니다(`IBundledFileReader`). 기동(AndroidMain)이 `BundledContentFileSource` 에 끼워 팩 폴더에 건다.
 * @details 열기는 압축하지 않고 저장된 파일만 받는다 — `AAsset_openFileDescriptor64` 가 APK 파일의 fd 와 구간을 주고, 팩 리더는 그 구간을 위치 지정 읽기로 읽는다
 *          (팩을 통째로 메모리에 올리지 않는다). 압축된 파일은 오류다: `android-apk` 가 `.pack` 을 `aapt2 -0 pack` 으로 저장한다.
 *          AAssetManager 는 여러 스레드가 함께 써도 되고, AAsset 은 아니라 열 때마다 따로 열고 바로 닫는다
 *          (https://developer.android.com/ndk/reference/group/asset).
 */
#pragma once
#include "Core/File/BundledContentFileSource.h"

struct AAssetManager;

namespace sw
{
    /**
     * @class AndroidAssetFileReader
     * @brief AAssetManager 하나로 APK 안 파일을 엽니다. 파일 머리말 참고.
     */
    class SW_API AndroidAssetFileReader final : public IBundledFileReader
    {
    public:
        /** @param pAssetManager 액티비티의 것(`ANativeActivity::assetManager`)이다. 앱 수명 동안 산다. 없으면 멈춘다. */
        explicit AndroidAssetFileReader( AAssetManager* pAssetManager );

        AsyncFileHandle open( string_view bundledName ) const override;
        bool            collectFileNames( string_view bundledFolder, vector<string>& outListFileName ) const override;

    private:
        AAssetManager* _pAssetManager;
    };
} // namespace sw
