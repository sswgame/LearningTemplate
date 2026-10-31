/**
 * @file DevOnlineExtension.h
 * @brief 온라인 기반 · 키트의 개발 대체물(개발 인증서 · 개발 티켓 키 · 가벼운 해시 · 테스트 계정 · 가짜 제공자)을 꽂는 확장점입니다.
 * @details - 확장점은 기능이 자기 `.cpp` 에 정의하고(`SW_DEFINE_DEV_ONLINE_EXTENSION`), 값은 정적 등록자로 올린다(`SW_REGISTER_DEV_ONLINE_EXTENSION`).
 *            배포 구성은 등록하지 않고 값을 `[[maybe_unused]]` 로만 붙잡는다 — 개발 대체물 코드는 모든 구성에서 컴파일되고, 쓰는 곳이 없어 배포 실행 파일에서 빠진다.
 *            파일 이름으로 배포에서 빼지 않는다(`docs/agents/BuildConfigSwitch.md`).
 *          - 쓰는 쪽은 `require` 로 값을 받는다. 비었으면(배포 · DevOnline 단위가 없는 구성) 까닭을 적고 nullptr — 폴백 없이 기동 오류로 멈춘다.
 *          - 확장점과 등록자는 같은 모듈(DLL)에 있다 — 모듈을 내리면 둘이 함께 사라지고, 등록자 소멸이 칸을 비운다(핫 리로드 · 언로드).
 *          - 칸은 상수 초기화(constexpr 생성자)라 정적 초기화 순서와 무관하고, 정적 초기화(등록자) 뒤에는 읽기만 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    template <typename TValue>
    class DevOnlineExtensionRegistrar;

    /**
     * @class DevOnlineExtension
     * @brief 개발 대체물 하나의 칸입니다. 값은 등록자가 빌려 준 정적 객체입니다. 파일 머리말 참고.
     */
    template <typename TValue>
    class DevOnlineExtension
    {
    public:
        /** @param pOptionName 오류 글에 나오는 개발 옵션 이름(문자열 리터럴) */
        explicit constexpr DevOnlineExtension( const utf8* pOptionName )
            : _pOptionName{ pOptionName }
            , _pValue{ nullptr }
        {
        }

        DevOnlineExtension( const DevOnlineExtension& )            = delete;
        DevOnlineExtension& operator=( const DevOnlineExtension& ) = delete;

        /** @brief 등록된 값입니다. 없으면(배포 · DevOnline 단위가 없는 구성) @p outError 에 까닭을 쓰고 nullptr 입니다. */
        [[nodiscard]] const TValue* require( string& outError ) const
        {
            if ( _pValue != nullptr )
                return _pValue;
            outError.assign( _pOptionName );
            outError.append( " is a development option - this build has no DevOnline unit (Shipping does not build *DevOnline.cpp)" );
            return nullptr;
        }
        /** @brief 값이 등록되었는가 — 개발 전용 값이 들어와도 되는 구성인지 묻는 쪽(배포가 거절할 개발 제공자 검사)이 씁니다. */
        bool isRegistered() const { return _pValue != nullptr; }

    private:
        friend class DevOnlineExtensionRegistrar<TValue>;

        const utf8*   _pOptionName;
        const TValue* _pValue;
    };

    /**
     * @class DevOnlineExtensionRegistrar
     * @brief `<기능>DevOnline.cpp` 가 정적 초기화 때 값을 칸에 올리고, 모듈을 내릴 때 칸을 비웁니다. `SW_REGISTER_DEV_ONLINE_EXTENSION` 으로 씁니다.
     */
    template <typename TValue>
    class DevOnlineExtensionRegistrar
    {
    public:
        DevOnlineExtensionRegistrar( DevOnlineExtension<TValue>& extension, const TValue& value )
            : _pExtension{ &extension }
        {
            extension._pValue = &value;
        }
        ~DevOnlineExtensionRegistrar() { _pExtension->_pValue = nullptr; }

        DevOnlineExtensionRegistrar( const DevOnlineExtensionRegistrar& )            = delete;
        DevOnlineExtensionRegistrar& operator=( const DevOnlineExtensionRegistrar& ) = delete;

    private:
        DevOnlineExtension<TValue>* _pExtension;
    };
} // namespace sw

/**
 * @brief 값 타입 @p Type 의 확장점 `Type::s_devOnline` 을 정의합니다. 그 타입을 쓰는 기능의 `.cpp`(배포도 짓는 파일)의 `namespace sw` 안에서 한 번 씁니다.
 * @details @p Type 은 `static DevOnlineExtension<Type> s_devOnline;` 을 선언한다.
 * @param OptionName 오류 글에 나오는 개발 옵션 이름(문자열 리터럴)
 */
#define SW_DEFINE_DEV_ONLINE_EXTENSION( Type, OptionName ) \
    ::sw::DevOnlineExtension<Type> Type::s_devOnline       \
    {                                                      \
        OptionName                                         \
    }

/**
 * @brief 값 @p Value(정적 객체)를 `Type::s_devOnline` 에 올립니다. `namespace sw` 안, 익명 네임스페이스 밖에서 한 번 씁니다. 배포 구성은 올리지 않는다(머리말).
 */
#if defined( SW_SHIPPING )
    #define SW_REGISTER_DEV_ONLINE_EXTENSION( Type, Value ) [[maybe_unused]] static constexpr const auto* s_devOnlineShippingUnused##Type = &( Value )
#else
    #define SW_REGISTER_DEV_ONLINE_EXTENSION( Type, Value )                      \
        const ::sw::DevOnlineExtensionRegistrar<Type> s_devOnlineRegistrar##Type \
        {                                                                        \
            Type::s_devOnline, Value                                             \
        }
#endif
