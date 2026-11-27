/**
 * @file DynamicLibrary.h
 * @brief OS 로더로 연 동적 라이브러리의 소유 형식 `DynamicLibrary`(이동 전용, 소멸에서 내린다)와 비소유 뷰 `DynamicLibraryView` 입니다.
 * @details 언리얼 `FPlatformProcess::GetDllHandle` · Godot `OS::open_dynamic_library` 는 `void*` 를 주고 내리기를 부르는 쪽 약속에 맡긴다. 그러면 빠뜨린 언로드 · 두 번 언로드 ·
 *          인스턴스 핸들과 섞기가 모두 컴파일된다. 여기서는 연 쪽이 `DynamicLibrary` 를 쥐고, 빌려 쓰는 쪽은 `DynamicLibraryView` 를 받는다.
 *          프로세스 끝까지 두는 곳은 `leakForProcessLifetime` 으로 그 자리에 드러난다. 이름 · 경로 규칙은 `ModuleLibraryUtil` 에 있다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/DynamicModules.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

namespace sw
{
    /**
     * @class DynamicLibraryView
     * @brief 로드된 라이브러리를 가리키는 비소유 뷰입니다. 내리지 않는다. 기본값은 빈 뷰(정적 링크 · 로드 전)입니다.
     * @details 모듈 경계의 라이브러리 핸들과 인스턴스 핸들(`GameHandle` · `EditorHandle`)이 둘 다 `void*` 면 서로의 자리에 넘겨도 컴파일된다. 이 형식이 그 섞임을 막는다.
     */
    class SW_API DynamicLibraryView
    {
    public:
        constexpr DynamicLibraryView() noexcept = default;

        /** @brief 로드된 라이브러리를 가리키면 true 입니다. */
        [[nodiscard]] constexpr bool isValid() const noexcept { return _pOSHandle != nullptr; }
        /** @brief 라이브러리의 심볼 @p symbolName 주소입니다. 빈 뷰이거나 없으면 nullptr 입니다. */
        [[nodiscard]] void* findSymbol( string_view symbolName ) const;
        /** @brief OS 값(Windows `HMODULE` 은 이미지 기준 주소)입니다. 로그의 주소 표시와 PE 머리 읽기(`ModuleLibraryUtil::findImageNtHeaders`)에만 쓴다. */
        [[nodiscard]] constexpr const void* getOSHandle() const noexcept { return _pOSHandle; }

        /** @brief 같은 라이브러리를 가리키면 true 입니다. */
        [[nodiscard]] constexpr bool operator==( const DynamicLibraryView& other ) const noexcept { return _pOSHandle == other._pOSHandle; }
        /** @brief 다른 라이브러리를 가리키면 true 입니다. */
        [[nodiscard]] constexpr bool operator!=( const DynamicLibraryView& other ) const noexcept { return _pOSHandle != other._pOSHandle; }

        /** @brief 이미 로드된 라이브러리 @p libraryName 을 찾아 프로세스 끝까지 내려가지 않게 고정한 뷰입니다(주입된 RenderDoc 같은 남의 라이브러리를 빌릴 때). 없으면 빈 뷰입니다. */
        [[nodiscard]] static DynamicLibraryView findLoadedAndPin( string_view libraryName );

    private:
        friend class DynamicLibrary;
        constexpr explicit DynamicLibraryView( void* pOSHandle ) noexcept
            : _pOSHandle{ pOSHandle }
        {
        }

    private:
        void* _pOSHandle{ nullptr };
    };

    /**
     * @class DynamicLibrary
     * @brief 연 동적 라이브러리 하나를 소유합니다. 이동 전용이고 소멸 · `reset` 때 내립니다. 빈 값은 아무것도 하지 않습니다.
     */
    class SW_API DynamicLibrary final
    {
    public:
        constexpr DynamicLibrary() noexcept = default;
        ~DynamicLibrary() { reset(); }

        DynamicLibrary( const DynamicLibrary& )            = delete;
        DynamicLibrary& operator=( const DynamicLibrary& ) = delete;
        DynamicLibrary( DynamicLibrary&& other ) noexcept
            : _pOSHandle{ other._pOSHandle }
        {
            other._pOSHandle = nullptr;
        }
        DynamicLibrary& operator=( DynamicLibrary&& other ) noexcept
        {
            if ( this != &other )
            {
                reset();
                _pOSHandle       = other._pOSHandle;
                other._pOSHandle = nullptr;
            }
            return *this;
        }

        /**
         * @brief 절대 경로 @p libraryPath 의 라이브러리를 엽니다. 검색하지 않고 한 번만 시도한다(경로인지 이름인지 추측하지 않는다).
         * @details 의존 라이브러리는 그 라이브러리 폴더 · 응용 프로그램 폴더 · `Bin` · `Bin/Modules` · 시스템 폴더에서 찾는다(Windows `AddDllDirectory`, Linux 는 RPATH).
         * @return 실패하면 이유(OS 오류)를 경고로 남기고 빈 값입니다.
         */
        [[nodiscard]] static DynamicLibrary loadAt( const AbsolutePath& libraryPath );
        /**
         * @brief OS 가 가진 라이브러리를 이름 @p libraryName(`xinput1_4.dll` · `libasound.so.2`)으로 엽니다. Windows 는 System32 에서만, Linux 는 동적 링커 검색 경로에서 찾는다.
         * @details 응용 프로그램 폴더를 보지 않는다(같은 이름의 DLL 을 옆에 두어 바꿔치기하는 것을 막는다). 엔진이 함께 내놓는 라이브러리는 `loadAt` 이다.
         * @return 실패하면 빈 값입니다. 없을 수 있는 선택 라이브러리가 많아(XInput 판 · ALSA) 로그는 부르는 쪽이 남긴다.
         */
        [[nodiscard]] static DynamicLibrary loadSystem( string_view libraryName );

        /** @brief 라이브러리를 쥐고 있으면 true 입니다. */
        [[nodiscard]] bool isValid() const noexcept { return _pOSHandle != nullptr; }
        /** @brief 빌려 줄 비소유 뷰입니다. 이 객체가 내리면 뷰도 낡는다. */
        [[nodiscard]] DynamicLibraryView getView() const noexcept { return DynamicLibraryView{ _pOSHandle }; }
        /** @brief 라이브러리의 심볼 @p symbolName 주소입니다. 빈 값이거나 없으면 nullptr 입니다. */
        [[nodiscard]] void* findSymbol( string_view symbolName ) const { return getView().findSymbol( symbolName ); }

        /** @brief 쥔 라이브러리를 지금 내리고 빈 값이 됩니다. 빈 값이면 아무것도 하지 않습니다. */
        void reset() noexcept;
        /**
         * @brief 내리지 않고 프로세스 끝까지 두며 뷰를 돌려줍니다. 받은 함수 포인터를 함수 정적이 쥐는 자리(DXC · 시스템 DLL 함수 표)처럼 내릴 때를 고를 수 없는 곳이 쓴다.
         * @details 이름이 있는 호출이라 "프로세스 끝까지 둔다" 는 결정이 그 자리에 드러난다(예전에는 주석 약속이었다).
         */
        DynamicLibraryView leakForProcessLifetime() noexcept
        {
            const DynamicLibraryView view{ _pOSHandle };
            _pOSHandle = nullptr;
            return view;
        }

#if SW_WITH_DYNAMIC_MODULES
        /**
         * @brief 모듈 라이브러리를 내립니다. 그 라이브러리가 끌어온 의존 라이브러리(GameFramework 같은 공유 모듈)는 먼저 프로세스 끝까지 고정해 남긴다. 빈 값이 됩니다.
         * @details 그 라이브러리를 가리키는 등록은 부르는 쪽(모듈 경로)이 먼저 뗐다. 구독은 소유자가, 모듈 등록자 · 타입은 `engine::unregisterModuleTypes` 가 뗀다.
         *          의존 라이브러리가 언제 함께 언로드되는지는 로더만 알아서 그 코드를 미리 해제할 수 없다. Windows 는 지연 로드가 의존 DLL 을 프로세스 끝까지 붙잡으므로 원래 그렇고,
         *          리눅스는 `DT_NEEDED` 참조가 함께 풀려 언로드되므로 여기서 고정한다(Windows import 표는 `GET_MODULE_HANDLE_EX_FLAG_PIN`, 리눅스는 `RTLD_NODELETE | RTLD_NOLOAD`).
         *          핫 리로드(`ModuleHotReloadManager`)는 언로드를 미룬다(`DeferredLibraryUnloadQueue`).
         * @return 라이브러리를 내렸으면 true 입니다.
         */
        bool unloadModule();
#endif // SW_WITH_DYNAMIC_MODULES

    private:
        constexpr explicit DynamicLibrary( void* pOSHandle ) noexcept
            : _pOSHandle{ pOSHandle }
        {
        }

    private:
        void* _pOSHandle{ nullptr };
    };
} // namespace sw
