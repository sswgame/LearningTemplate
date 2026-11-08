/**
 * @file RegistrationSlot.h
 * @brief 하나만 받는 정적 등록 칸 `RegistrationSlot<T>` 입니다. 다른 이미지(개발 도구 · 오디오 · 클라이언트)가 정적 등록자로 창구 하나를 걸고, 런타임은 칸만 읽는다.
 * @details `RegistrationList` 와 같은 자리(다른 TU · DLL 의 정적 등록자가 엔진 범위보다 먼저 쓴다)의 "하나만" 변형입니다. 기능마다 두던 `static T* s_p` 칸 · 원자 함수 포인터를
 *          대신한다. 둘째 등록은 거절하고 오류 로그를 남긴다(먼저 것을 둔다) — 이미지에 하나인 창구가 둘이면 등록의 잘못이다. 핫 리로드가 교체 이미지를 올리는 동안
 *          (`RegistrationReloadScope`)은 새 등록이 대신하고 옛 것은 그림자로 두었다가, 옛 이미지가 내려가며 빼면 버리고 새 이미지가 취소되면 돌아온다(목록과 같은 규칙).
 *          읽기(`get`)는 어느 스레드에서도 된다(원자 포인터). 걸기 · 떼기는 정적 초기화 · 모듈 언로드(게임 스레드)에서만 한다.
 *          바뀜은 세대 번호(`getGeneration`)로 본다. 전역 열거(`RegistrationListBase::collect`)에 항목 0 · 1 개 목록으로 나온다.
 *          언리얼은 이런 창구를 모듈 인터페이스(`IModularFeatures`)의 이름 표로 두지만, 하나만 받는 자리는 칸 하나로 충분하다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/RegistrationList.h"

#include <atomic>

namespace sw
{
    /**
     * @class RegistrationSlot
     * @brief 항목 하나를 소유하지 않고 거는 칸입니다(수명은 등록한 쪽). 파일 머리말 참고.
     * @tparam T 거는 항목의 타입입니다. 함수 타입이면 칸은 함수 포인터를 든다(`RegistrationSlot<void( Policy& )>`).
     */
    template <typename T>
    class RegistrationSlot final : public RegistrationListBase
    {
    public:
        /** @param pSlotName 전역 열거와 거절 로그에 나오는 칸 이름입니다. 문자열 리터럴이어야 합니다. */
        explicit RegistrationSlot( const utf8* pSlotName )
            : RegistrationListBase( pSlotName )
            , _pItem{ nullptr }
            , _pShadowItem{ nullptr }
            , _name{}
            , _shadowName{}
            , _generation{ 0 }
        {
        }

        ~RegistrationSlot() { unlink(); }

        /**
         * @brief 항목을 겁니다. 이미 다른 항목이 걸려 있으면 `DuplicateName`(리로드 중이면 대신하고 옛 것은 그림자로), 같은 항목이면 `AlreadyPresent` 입니다.
         * @param name 진단 · 거절 로그에 쓰는 이름입니다. 지금 복사합니다.
         */
        [[nodiscard]] RegistrationResult set( T* pItem, string_view name )
        {
            if ( pItem == nullptr )
                return RegistrationResult::NullItem;
            T* const pCurrent = _pItem.load( std::memory_order_acquire );
            if ( pCurrent == pItem )
                return RegistrationResult::AlreadyPresent;
            if ( pCurrent != nullptr )
            {
                if ( RegistrationReloadScope::isActive() == false )
                    return RegistrationResult::DuplicateName;
                _pShadowItem = pCurrent;
                _shadowName  = _name;
            }
            _name = string( name );
            _pItem.store( pItem, std::memory_order_release );
            _generation.fetch_add( 1, std::memory_order_acq_rel );
            return RegistrationResult::Added;
        }

        /** @brief `set` 하고 거절되면(`DuplicateName` · `NullItem`) 공용 오류 로그를 남깁니다. @param pWhat 로그에 나오는 등록 대상의 영문 이름(문자열 리터럴)입니다. */
        RegistrationResult setLogged( T* pItem, const utf8* pWhat, string_view name )
        {
            const RegistrationResult result = set( pItem, name );
            reportRegistrationRejected( result, pWhat, name );
            return result;
        }

        /** @brief 그 항목이 걸려 있으면 뗍니다(그림자가 있으면 그것이 돌아온다). 그림자면 그림자만 버립니다. 뗐으면 true 입니다. 다른 항목은 건드리지 않습니다. */
        [[nodiscard]] bool reset( T* pItem )
        {
            if ( pItem == nullptr )
                return false;
            if ( _pShadowItem == pItem )
            {
                _pShadowItem = nullptr;
                _shadowName  = string{};
                return true;
            }
            if ( _pItem.load( std::memory_order_acquire ) != pItem )
                return false;
            _pItem.store( _pShadowItem, std::memory_order_release );
            _name        = std::move( _shadowName );
            _pShadowItem = nullptr;
            _shadowName  = string{};
            _generation.fetch_add( 1, std::memory_order_acq_rel );
            return true;
        }

        /** @brief 걸린 항목입니다. 없으면 nullptr 입니다. 어느 스레드에서도 부를 수 있습니다. */
        T* get() const { return _pItem.load( std::memory_order_acquire ); }
        /** @brief 걸기 · 떼기 · 리로드 대체마다 오르는 번호입니다. */
        uint32 getGeneration() const { return _generation.load( std::memory_order_acquire ); }

        uint32        getCount() const override { return get() != nullptr ? 1u : 0u; }
        const string& getNameAt( uint32 index ) const override
        {
            (void)index;
            return _name;
        }

    private:
        std::atomic<T*>     _pItem;       ///< 걸린 항목
        T*                  _pShadowItem; ///< 리로드 대체에 칸을 내준 옛 항목(옛 이미지가 내려갈 때까지)
        string              _name;        ///< 걸 때 복사한 이름
        string              _shadowName;  ///< 그림자 항목의 이름
        std::atomic<uint32> _generation;  ///< `getGeneration`
    };
} // namespace sw

/**
 * @brief 하나만 받는 칸을 함수 지역 정적으로 돌려주는 함수 @p getName 을 정의합니다(.cpp 에서). 함수 정적 가변 변수는 이 매크로와
 *        `SW_DEFINE_REGISTRATION_LIST` 로만 둔다(린트 `function-statics`). 나머지 런타임 상태는 엔진 · 에디터 서브시스템이 든다.
 * @param T 거는 항목의 타입입니다. @param pSlotName 칸 이름(문자열 리터럴)입니다.
 */
#define SW_DEFINE_REGISTRATION_SLOT( getName, T, pSlotName )  \
    ::sw::RegistrationSlot<T>& getName()                      \
    {                                                         \
        static ::sw::RegistrationSlot<T> s_slot{ pSlotName }; \
        return s_slot;                                        \
    }
