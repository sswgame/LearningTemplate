/**
 * @file RegisteredParams.h
 * @brief 등록 줄(모듈 이미지가 올린 동작 · 조건)의 칸 구조체로 만든 값을 쥡니다. 줄이 빠진 뒤에 놓이면 그 값의 소멸자를 부르지 않습니다(일부러 남긴다).
 * @details 칸 값은 등록 줄의 함수 표(`InstancedStructOps`)가 그 이미지 안에서 만든다 — 공유 포인터의 해제 코드(제어 블록)가 그 이미지에 있다. 핫 리로드로 이미지를 내린 뒤
 *          컴파일한 호출 · 식이 사라지며 값을 놓으면 내린 코드를 불렀다(감사 S-14). 줄이 아직 있으면 평소처럼 놓고, 빠졌으면 값을 남긴다(작은 누수가 크래시보다 낫다 —
 *          언리얼도 핫 리로드의 옛 UClass 인스턴스를 바로 지우지 않고 남겨 두었다가 다시 인스턴스로 바꾼다). 줄이 살아 있는지는 등록 목록이 답한다(`LockedRegistrationListBase`).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class LockedRegistrationListBase;

    /** @class RegisteredParams @brief 파일 머리말 참고. 복사하면 값을 나눠 든다(읽기 전용). */
    class SW_API RegisteredParams
    {
    public:
        RegisteredParams();
        /** @brief 목록 @p entries 의 등록 줄 @p pEntry 가 만든 값 @p pParams 를 쥡니다. @p generation 은 줄을 찾기 **전에** 읽은 등록표 세대다. */
        RegisteredParams( shared_ptr<const void> pParams, const void* pEntry, const LockedRegistrationListBase& entries, uint32 generation );
        RegisteredParams( const RegisteredParams& other );
        RegisteredParams& operator=( const RegisteredParams& other );
        RegisteredParams( RegisteredParams&& other ) noexcept;
        RegisteredParams& operator=( RegisteredParams&& other ) noexcept;
        ~RegisteredParams();

        /** @brief 쥔 값입니다. 비었으면 nullptr 입니다. 줄이 빠졌으면 값은 남아 있지만 부르는 쪽이 쓰면 안 된다(`isEntryRegistered`). */
        const void* get() const { return _pRaw; }
        /** @brief 값을 만든 줄이 아직 올라 있는지입니다. 세대가 그대로면 원자 읽기 하나다. 빈 값은 true 입니다. */
        bool isEntryRegistered() const;

    private:
        /** @brief 쥔 공유 포인터를 놓습니다. 줄이 빠졌으면 소멸자를 부르지 않고 칸을 버린다. */
        void                          release();
        shared_ptr<const void>&       getShared() { return *reinterpret_cast<shared_ptr<const void>*>( _arrStorage ); }
        const shared_ptr<const void>& getShared() const { return *reinterpret_cast<const shared_ptr<const void>*>( _arrStorage ); }

    private:
        alignas( shared_ptr<const void> ) uint8 _arrStorage[sizeof( shared_ptr<const void> )]; ///< 공유 포인터 자리 — 줄이 빠지면 소멸자를 건너뛰려고 직접 든다
        const void*                       _pRaw;                                               ///< 값(빈 값은 nullptr — 그때 자리는 비어 있다)
        const void*                       _pEntry;                                             ///< 값을 만든 등록 줄(가리키기만 한다)
        const LockedRegistrationListBase* _pEntries;                                           ///< 줄이 오른 목록(정적 — 목록을 정의한 이미지의 것이라 늘 물을 수 있다)
        uint32                            _generation;                                         ///< 쥘 때의 세대
    };
} // namespace sw
