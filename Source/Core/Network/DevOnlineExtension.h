/**
 * @file DevOnlineExtension.h
 * @brief 네트워크 · 온라인 기반과 키트의 개발 대체물(평문 공개 게임 포트 허용 · 개발 인증서 · 개발 티켓 키 · 가벼운 해시 · 테스트 계정 · 가짜 제공자)을 꽂는 확장점입니다.
 * @details - 확장점은 기능이 자기 `.cpp` 에 정의하고(`SW_DEFINE_DEV_ONLINE_EXTENSION`), 값은 정적 등록자로 올린다(`SW_REGISTER_DEV_ONLINE_EXTENSION`).
 *            배포 구성은 등록하지 않고 값을 `[[maybe_unused]]` 로만 붙잡는다 — 개발 대체물 코드는 모든 구성에서 컴파일되고, 쓰는 곳이 없어 배포 실행 파일에서 빠진다.
 *            파일 이름으로 배포에서 빼지 않는다(`docs/agents/BuildConfigSwitch.md`).
 *          - 쓰는 쪽은 `require` 로 값을 받는다. 비었으면(배포 · DevOnline 단위가 없는 구성) 까닭을 적고 nullptr — 폴백 없이 기동 오류로 멈춘다.
 *          - 확장점과 등록자는 같은 모듈(DLL)에 있다 — 모듈을 내리면 둘이 함께 사라지고, 등록자 소멸이 칸을 비운다(핫 리로드 · 언로드).
 *          - 칸은 상수 초기화(constexpr 생성자)라 정적 초기화 순서와 무관하고, 정적 초기화(등록자) 뒤에는 읽기만 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationSlot.h"

namespace sw
{
    /**
     * @struct DevOnlineExtension
     * @brief 개발 대체물 하나의 칸(`TValue::getDevOnline()` — 정적 등록 칸 `RegistrationSlot`)을 읽고 거는 창구입니다. 파일 머리말 참고.
     * @details 칸은 공통 틀(`SW_DEFINE_REGISTRATION_SLOT`)이라 다른 TU 의 등록자가 먼저 와도 서 있고, 등록자가 내려가며 뗀다. 예전에는 값 타입마다 클래스 정적 칸을 두었다.
     * @tparam TValue `static RegistrationSlot<const TValue>& getDevOnline();` 을 선언한 값 타입입니다.
     */
    template <typename TValue>
    struct DevOnlineExtension
    {
        /** @brief 등록된 값입니다. 없으면(배포 · DevOnline 단위가 없는 구성) 대상이 개발 옵션 이름인 오류입니다. */
        [[nodiscard]] static expected<const TValue*> require()
        {
            const RegistrationSlot<const TValue>& slot   = TValue::getDevOnline();
            const TValue* const                   pValue = slot.get();
            if ( pValue != nullptr )
                return pValue;
            return unexpected( Error{ ErrorKind::Unsupported, "a development option - this build registers no development substitutes (Shipping)", slot.getListName() } );
        }
        /** @brief 값이 등록되었는가 — 개발 전용 값이 들어와도 되는 구성인지 묻는 쪽(배포가 거절할 개발 제공자 검사)이 씁니다. */
        static bool isRegistered() { return TValue::getDevOnline().get() != nullptr; }
        /** @brief 값을 겁니다(정적 등록자가 부른다). 이미 다른 값이 걸려 있으면 공용 거절 로그를 남기고 false 입니다. */
        static bool attach( const TValue* pValue )
        {
            RegistrationSlot<const TValue>& slot = TValue::getDevOnline();
            return slot.setLogged( pValue, "Dev online option", slot.getListName() ) == RegistrationResult::Added;
        }
        /** @brief 건 값을 뗍니다(건 이미지가 내려갈 때). */
        static void detach( const TValue* pValue ) { (void)TValue::getDevOnline().reset( pValue ); }
    };
} // namespace sw

/**
 * @brief 값 타입 @p Type 의 확장점 칸 `Type::getDevOnline()` 을 정의합니다. 그 타입을 쓰는 기능의 `.cpp`(배포도 짓는 파일)의 `namespace sw` 안에서 한 번 씁니다.
 * @details @p Type 은 `static RegistrationSlot<const Type>& getDevOnline();` 을 선언한다.
 * @param OptionName 오류 글 · 칸 이름에 나오는 개발 옵션 이름(문자열 리터럴)
 */
#define SW_DEFINE_DEV_ONLINE_EXTENSION( Type, OptionName ) SW_DEFINE_REGISTRATION_SLOT( Type::getDevOnline, const Type, OptionName )

/**
 * @brief 값 @p Value(정적 객체)를 `Type::getDevOnline()` 에 겁니다. `namespace sw` 안, 익명 네임스페이스 밖에서 한 번 씁니다. 배포 구성은 걸지 않는다(머리말).
 */
#if defined( SW_SHIPPING )
    #define SW_REGISTER_DEV_ONLINE_EXTENSION( Type, Value ) [[maybe_unused]] static constexpr const auto* s_devOnlineShippingUnused##Type = &( Value )
#else
    #define SW_REGISTER_DEV_ONLINE_EXTENSION( Type, Value )                                              \
        const ::sw::StaticRegistrar<const Type*> s_devOnlineRegistrar##Type                              \
        {                                                                                                \
            &( Value ), &::sw::DevOnlineExtension<Type>::attach, &::sw::DevOnlineExtension<Type>::detach \
        }
#endif
