/**
 * @file ModuleShadowCopy.h
 * @brief 모듈 원본을 읽어 검사하고, 세대 이름을 붙인 섀도 복사본 파일을 씁니다. 복사본을 지우는 일도 여기서 합니다.
 *
 * @note 파일과 이미지 바이트만 다룹니다. 복사본을 로드하고 교체하는 순서(정적 등록 헤드 · `RegistrationReloadScope` · 이미지 검사 콜백)는
 *       `LiveReloadManager` 가 정합니다. 핫 리로드만 쓰고, Shipping 빌드에서 함께 빠집니다(`Source/ModuleHost/CMakeLists.txt` 의 제외 목록).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @brief 모듈 이미지 이름 세 가지입니다 — 리눅스는 SONAME, Windows 는 DLL 파일 이름(로더가 import 를 맞추는 이름)입니다.
     * @details `_original` 은 원본 파일의 이름이라 의존 모듈의 NEEDED · import 표가 이것을 적고 있고, `_current` 는 의존 모듈이 **지금**
     *          적어야 할 이름(복사본을 쓸 때 세대 이름으로 바꾼다), `_loaded` 는 commit 된 복사본의 이름입니다. prepare 가 실패하면
     *          `_current` 를 `_loaded` 로 되돌립니다.
     */
    struct ShadowImageNameState
    {
        string _original;
        string _current;
        string _loaded;
    };
} // namespace sw

namespace sw
{
    /** @brief 이 매니저가 올린 다른 모듈 하나의 이름과 이미지 이름입니다(`ShadowCopyParams`). */
    struct ShadowCopyModuleImage
    {
        string_view                 _moduleName;
        const ShadowImageNameState* _pImageName{ nullptr };
    };
} // namespace sw

namespace sw
{
    /** @brief 섀도 복사본 하나를 쓰는 데 필요한 입력입니다(`ModuleShadowCopy::writeShadowCopy`). */
    struct ShadowCopyParams
    {
        string_view _moduleName;
        string_view _originalModulePath;
        uint64      _sourceMtime{ 0 };
        bool        _bReplacingLoadedImage{ false }; ///< 이미 올라온 이미지를 바꾸는 리로드다(거절 로그의 안내 문구만 바뀐다)
        /** @brief 이 모듈이 의존하는 모듈 가운데 이 매니저가 올린 것입니다. 복사본의 import · NEEDED 를 그 의존의 지금 이름으로 바꿉니다. */
        vector<ShadowCopyModuleImage> _listDependencyImage;
        /** @brief 이 매니저에 등록된 모든 모듈입니다(자기 포함). Windows 에서 의존 목록에 없이 등록된 모듈을 import 하는지 봅니다. */
        vector<ShadowCopyModuleImage> _listRegisteredImage;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ModuleShadowCopy
     * @brief 섀도 복사본 파일을 만들고 지웁니다.
     * @details 원본을 한 번 읽어 엔진 ABI 스탬프와 지연 import 를 대조하고, 세대 이름을 붙여(`ModuleImagePatch`) 원본 옆에 씁니다.
     *          Windows 는 복사본마다 제 폴더(`<모듈>_temp_p<pid>_…/<세대 이름>.dll`)를 쓰고, 리눅스는 파일 하나(`<모듈>_temp_p<pid>_….so`)입니다.
     *          복사본 이름의 일련번호와 세대 번호는 프로세스 하나에 하나씩 오릅니다.
     */
    struct ModuleShadowCopy
    {
        /** @brief 원본을 읽습니다. 링커가 아직 쓰는 중이면 잠겨 있으므로 잠깐씩 기다려 다시 읽습니다. */
        [[nodiscard]] static bool readModuleFile( string_view path, vector<uint8>& outBytes );

        /**
         * @brief 모듈 이미지 바이트 @p bytes 에 지연 import 가 있으면 원인을 남기고 true 를 반환합니다 — 그런 이미지는 올리지 않습니다.
         * @details 모듈 이미지는 모든 import 를 로드할 때 묶어야 한다. lld 20 의 x64 지연 로드 썽크(`__tailMerge_<dll>`)는 xmm0~3 을
         *          `__delayLoadHelper2` 호출의 홈 공간에 저장해, 첫 호출이 묶으면 그 호출의 float 인자가 망가진다(`docs/09_Decisions.md`).
         *          빌드는 `/DELAYLOAD` 를 쓰지 않고(`CheckNoDelayLoad`), 이것은 그 규칙을 어긴 이미지를 올리기 **전에** 거절하는 마지막 그물이다.
         */
        static bool rejectsDelayImports( string_view moduleName, string_view modulePath, const vector<uint8>& bytes );

        /**
         * @brief 원본을 읽어 대조하고, 세대 이름을 붙인 섀도 복사본(과 디버그 심볼)을 씁니다. 아직 로드하지 않습니다.
         * @param inoutImageName 이 모듈의 이미지 이름입니다. `_current` 가 세대 이름으로 바뀝니다. 이름을 바꾼 뒤 실패하면 `_loaded` 로 되돌립니다.
         * @param outShadowPath 쓴 복사본 경로입니다. 실패하면 건드리지 않습니다.
         * @return 원본을 못 읽었거나, 엔진 ABI 스탬프가 다르거나 없거나, 지연 import 가 있거나, 세대 이름을 붙이지 못했거나, 쓰지 못했으면 false 입니다.
         *         실패하면 쓰다 만 복사본을 지웁니다.
         * @details 로드하기 **전에** 확인한다. 로드하면 정적 초기화가 돌므로, 돌고 있는 엔진과 다른 헤더로 빌드된 모듈은 그 전에 거절해야 한다.
         */
        [[nodiscard]] static bool writeShadowCopy( const ShadowCopyParams& params, ShadowImageNameState& inoutImageName, string& outShadowPath );

        /**
         * @brief 섀도 복사본과 그 디버그 심볼을 지웁니다(Windows 는 비었으면 복사본 폴더까지). 경로가 비면 아무것도 하지 않습니다.
         * @details 아직 매핑된 복사본은 지금 지울 수 없다. 남은 것은 다음 시작 · 종료의 `ShadowCopyName::removeStaleCopies` 가 지운다.
         */
        static void removeArtifacts( string_view shadowPath );
    };
} // namespace sw
