/**
 * @file RegistrationChain.h
 * @brief 할당 없는 침입형 정적 등록 사슬 `RegistrationChain<T>` 입니다. 등록자가 제 다음 칸(`_pNext`)을 들고, 사슬은 머리 하나만 든다.
 * @details `RegistrationList` 와 같은 자리(다른 TU · DLL 의 정적 등록자가 엔진 범위보다 먼저 쓴다)이지만 **할당 · 이름 복사가 없고 상수 초기화**다 —
 *          리플렉션 형식 · 전역 변수처럼 메모리 프로파일러 · 이름 풀이보다 먼저 도는 등록자, 그리고 모듈을 올린 쪽이 "이 이미지가 방금 단 것" 을 통째로
 *          걷어 가는(`takeAll`) 사슬이 쓴다. 기능마다 손으로 두던 `static T* s_pHead` 머리와 "머리를 읽고 나를 앞에 놓기" 를 한 벌로 둔다.
 *          머리는 상수 초기화(`constexpr` 생성자)이고 소멸자가 하는 일이 없어, 어느 TU 의 정적 초기화 · 종료 순서와도 무관하다.
 *          사슬은 등록자를 빼지 않는다 — 쓰는 곳은 둘뿐이다: 모듈을 올린 쪽이 걷어 가는 사슬(걷힌 뒤 머리는 비고, 이미지가 내려가도 사슬에 남는 것이 없다)과
 *          엔진 이미지 안에만 있는 플랫폼 백엔드 사슬(이미지와 수명이 같다). 내려갈 수 있는 이미지가 걷히지 않은 채 남는 등록자는 `RegistrationList` 로 둔다.
 *          언리얼의 compiled-in 등록 목록(`FCompiledInDefer` 의 정적 배열) · Godot `ClassDB` 의 등록 함수 표와 같은 자리다.
 *          스레드: 정적 초기화 · 모듈을 올리고 내리는 스레드만 만진다(잠금 없음).
 */
#pragma once
#include "Core/Foundation/Macros.h"

namespace sw
{
    /**
     * @class RegistrationChain
     * @brief 등록자 @p T 의 사슬 머리입니다. 등록자는 생성자에서 `_pNext{ chain.pushFront( this ) }` 로 붙는다(나중에 만든 것이 앞).
     * @details 함수 정적으로만 둔다(`SW_DEFINE_REGISTRATION_CHAIN`). 복사 · 이동은 막혀 있다.
     */
    template <typename T>
    class RegistrationChain final
    {
    public:
        constexpr RegistrationChain()
            : _pHead{ nullptr }
        {
        }

        RegistrationChain( const RegistrationChain& )            = delete;
        RegistrationChain& operator=( const RegistrationChain& ) = delete;

        /** @brief @p pItem 을 머리로 놓고 앞 머리를 돌려줍니다 — 등록자가 그 값을 제 다음 칸으로 든다. */
        [[nodiscard]] T* pushFront( T* pItem )
        {
            T* const pNext = _pHead;
            _pHead         = pItem;
            return pNext;
        }

        /** @brief 지금 머리입니다(비우지 않습니다). 없으면 nullptr 입니다. */
        T* getHead() const { return _pHead; }

        /** @brief 머리를 돌려주고 사슬을 비웁니다 — 걷어 간 쪽만 그 등록자들을 안다(다음 이미지는 제 것만 단다). */
        [[nodiscard]] T* takeAll()
        {
            T* const pHead = _pHead;
            _pHead         = nullptr;
            return pHead;
        }

        /** @brief 떼어 두었던 머리 @p pHead 로 되돌립니다(중단한 핫 리로드 · 지역 등록자를 단 테스트). */
        void resetHead( T* pHead ) { _pHead = pHead; }

    private:
        T* _pHead; ///< 마지막으로 붙은 등록자. 상수 초기화라 정적 초기화 순서를 타지 않는다
    };
} // namespace sw

/**
 * @brief 등록 사슬(`RegistrationChain<T>`)을 함수 지역 정적으로 돌려주는 함수 @p getName 을 정의합니다(.cpp 에서 — 이미지마다 하나라 헤더에 두지 않는다).
 * @details 사슬은 상수 초기화라 다른 TU · DLL 의 등록자가 먼저 와도 빈 채로 서 있고, 소멸자가 없어 종료 순서도 타지 않는다.
 *          클래스 정적 멤버의 바깥 정의로도 쓴다(`SW_DEFINE_REGISTRATION_CHAIN( TypeRegistrar::getChain, TypeRegistrar )`).
 */
#define SW_DEFINE_REGISTRATION_CHAIN( getName, T )   \
    ::sw::RegistrationChain<T>& getName()            \
    {                                                \
        static ::sw::RegistrationChain<T> s_chain{}; \
        return s_chain;                              \
    }
