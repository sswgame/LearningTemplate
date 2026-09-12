/**
 * @file ModuleHandleProvider.h
 * @brief 지연 로드(delay-load)가 "이 DLL 은 이미 어디에 올라와 있나" 를 묻는 **유일한** 창구.
 *
 * @details 모듈 DLL 마다 컴파일되는 지연 로드 훅(`DelayLoadNotifyHook.cpp`)은 핫리로드가 갈아 끼운 그림자 복사본을
 *          가리켜야 한다. 그 답을 아는 것은 App 의 `LiveReloadManager` 지만, 훅은 **모듈 DLL 안**에 있으므로 App.exe 의
 *          심볼을 링크할 수 없다 — 그래서 훅이 링크하는 이 작은 인터페이스만 Engine.dll 에 둔다.
 *
 *          Engine 이 모듈 리로드에 대해 아는 것은 여기까지다. 감시·그림자 복사·의존 그래프·재적재 같은 기계는 전부
 *          App 에 있고(`Source/App/Module/LiveReloadManager.*`), Shipping 에서는 그 파일이 빌드에서 빠진다. 이 인터페이스는
 *          Shipping 에도 남지만 **아무도 등록하지 않으므로** 조회는 늘 nullptr 이고 훅은 곧장 폴백(Bin 에서 직접 로드)으로 간다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    /**
     * @brief 로드된 모듈 핸들을 이름으로 찾아 주는 제공자입니다.
     * @note 구현은 App 이 한다. Engine 은 이 창구만 들고 있고 수명은 등록한 쪽이 쥔다.
     */
    class SW_API IModuleHandleProvider
    {
    public:
        virtual ~IModuleHandleProvider() = default;

        /**
         * @brief 리로드 그래프가 깨져 있으면 true — 이때는 핸들을 믿으면 안 됩니다.
         * @details 리로드가 중간에 실패해 어떤 모듈이 어느 판본인지 알 수 없는 상태다. 그럴 때 옛 핸들을 돌려주면
         *          지연 로드가 사라진 코드로 뛰어든다 — 차라리 디스크에서 다시 읽게 둔다.
         */
        virtual bool isModuleGraphBroken() const = 0;

        /** @brief 이름(확장자 없는 스템)으로 이미 로드된 모듈 핸들을 찾습니다. 없으면 nullptr. */
        virtual void* findLoadedModuleHandle( string_view moduleName ) const = 0;
    };

    namespace engine
    {
        /** @brief 제공자를 등록합니다. nullptr 로 해제합니다. 등록한 쪽이 사라지기 전에 반드시 해제하세요. */
        SW_API void setModuleHandleProvider( IModuleHandleProvider* pProvider );

        /** @brief 등록된 제공자입니다. 없으면 nullptr (Shipping 은 늘 nullptr). */
        SW_API IModuleHandleProvider* getModuleHandleProvider();
    } // namespace engine
} // namespace sw
