/**
 * @file SystemFontLocator.h
 * @brief OS 시스템 폰트 폴더와 그 안의 폰트 파일을 찾습니다(런타임 텍스트와 에디터 ImGui 가 함께 씁니다).
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class SystemFontLocator
     * @brief 시스템 폰트 폴더 목록과 파일 이름으로 찾기를 제공합니다. 찾는 쪽(글꼴 서비스 · Wayland 글 그리기)이 값으로 들고, 리눅스의 하위 폴더 색인은 그 값이 갖는다.
     * @details 예전에는 색인이 소유자 없는 엔진 수명 값(`ScopeOwned`)이었다. 찾는 쪽마다 하나라 색인을 두 번 훑을 수 있지만, 훑기는 쓰는 쪽마다 처음 한 번이다
     *          (Godot `OS::get_system_font_path` 가 상태 없이 찾고 부르는 쪽이 결과를 갖는 것과 같은 나눔).
     */
    class SW_API SystemFontLocator
    {
    public:
        SystemFontLocator()                                      = default;
        SystemFontLocator( const SystemFontLocator& )            = delete;
        SystemFontLocator& operator=( const SystemFontLocator& ) = delete;

        /**
         * @brief 이 플랫폼의 시스템 폰트 폴더들입니다(있는 것만).
         * @details Windows 는 `%WINDIR%/Fonts` 와 사용자 폰트 `%LOCALAPPDATA%/Microsoft/Windows/Fonts` 이고, 리눅스는 `/usr/share/fonts`, `/usr/local/share/fonts`,
         *          `~/.local/share/fonts` 이다.
         */
        static vector<string> getSystemFontDirectories();

        /**
         * @brief 파일 이름으로 시스템 폰트 파일을 찾습니다. 없으면 빈 문자열입니다. 여러 스레드가 함께 불러도 됩니다.
         * @details 폴더 바로 아래를 먼저 보고, 리눅스는 하위 폴더까지 훑는다(배포판이 `truetype/<패밀리>/` 처럼 나누며, 훑은 결과는 이 값이 한 번 캐시한다).
         *          이름 비교는 대소문자를 가리지 않는다.
         */
        string findSystemFontFile( string_view fileName );

    private:
#if defined( SW_PLATFORM_LINUX )
        /** @brief 하위 폴더 색인에서 @p fileName(대소문자 무시)의 경로를 찾습니다. 처음 부를 때 색인을 채웁니다. 없으면 빈 글입니다. */
        string findInRecursiveIndex( string_view fileName ) SW_EXCLUDES_LOCK( _mutex );

        mutex                                            _mutex;
        bool _bIndexBuilt                                SW_GUARDED_BY( _mutex ){ false };
        unordered_map<string, string> _mapFileNameToPath SW_GUARDED_BY( _mutex ); ///< 소문자 파일 이름 → 경로(앞 폴더가 이긴다)
#endif
    };
} // namespace sw
