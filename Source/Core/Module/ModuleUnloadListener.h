/**
 * @file ModuleUnloadListener.h
 * @brief 모듈 이미지의 코드(델리게이트 스텁 · vtable)를 들고 있을 수 있는 등록부의 공통 계약과, 그 등록부 목록입니다.
 *
 * 모듈 DLL 보다 오래 사는 등록부(이벤트 버스 · 로그 리스너 · Undo 스택 · 창 처리기 · 에셋 캐시 …)는 모듈이 떼지 않고 내려가면
 * 내려간 코드로 뛴다. 핫 리로드는 이미지를 내리기 전에 등록부를 모두 훑어 그 범위의 코드를 떼야 한다(`ModuleImageUtil::releaseModuleCode`).
 * 등록부는 이 인터페이스를 상속하면 **만들어질 때 스스로** 목록에 들어가고 사라질 때 빠진다 — 훑는 쪽은 목록만 돈다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @class IModuleUnloadListener
     * @brief 모듈 이미지의 코드를 가리킬 수 있는 등록부입니다. 상속하면 프로세스 목록에 자동으로 오르내립니다.
     * @warning `onModuleUnloading` 안에서 다른 리스너를 만들거나 지우지 않습니다 — 훑기는 목록 잠금을 쥔 채 돕니다.
     * @note 리스너 자체는 엔진(또는 App) 쪽 코드가 만들어야 합니다. 모듈이 만든 리스너는 vtable 이 그 이미지에 있어, 모듈보다 오래 살면 훑기가
     *       내려간 코드로 뜁니다. 생성자를 헤더에 인라인으로 두지 않는 것이 그 때문입니다(생성자가 vptr 을 박는 이미지가 vtable 의 집이다).
     */
    class SW_API IModuleUnloadListener
    {
    public:
        /**
         * @struct ReleaseResult
         * @brief 리스너 하나를 훑은 결과입니다.
         */
        struct ReleaseResult
        {
            const utf8* _pListenerName{ nullptr };  ///< `getModuleUnloadListenerName` (정적 문자열)
            uint32      _releasedCount{ 0 };        ///< 뗀 것의 수
            bool        _bKeepImageMapped{ false }; ///< 떼어 낼 수 없는 것이 남아 이미지를 내리면 안 된다
        };

        /** @brief 목록에 자기를 올립니다. */
        IModuleUnloadListener();
        /** @brief 사본도 따로 목록에 오릅니다. */
        IModuleUnloadListener( const IModuleUnloadListener& other );
        /** @brief 목록에서 자기를 내립니다. */
        virtual ~IModuleUnloadListener();

        /** @brief 대입은 목록 자리를 바꾸지 않습니다(이미 올라 있다). */
        IModuleUnloadListener& operator=( const IModuleUnloadListener& other );

        /** @brief 로그에 쓰는 이름입니다("event subscriptions" 처럼 무엇을 들고 있는지). 정적 문자열이어야 합니다. */
        virtual const utf8* getModuleUnloadListenerName() const = 0;

        /**
         * @brief [@p pBegin, @p pEnd) 안의 코드를 가리키는 등록을 뗍니다.
         * @param outKeepImageMapped 떼어 낼 수 없어 이미지를 내리면 안 되면 true 로 둡니다. 그 밖에는 건드리지 않습니다.
         * @return 뗀 것의 수입니다.
         */
        virtual uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) = 0;

        /**
         * @brief 살아 있는 모든 리스너에게 범위를 넘겨 뗍니다. 리스너마다 결과 한 줄입니다(뗀 것이 없어도).
         * @param pBegin · pEnd 모듈 이미지 범위. 둘 중 하나가 nullptr 이면 아무것도 하지 않습니다.
         */
        static void releaseAllWithin( const void* pBegin, const void* pEnd, vector<ReleaseResult>& outListResult );

        /** @brief 지금 목록에 오른 리스너 수입니다(진단 · 시험). */
        static uint32 getListenerCount();

        /** @brief 주소 @p pCode 가 [@p pBegin, @p pEnd) 안인지 봅니다. nullptr 은 늘 밖입니다. */
        static bool isAddressWithin( const void* pCode, const void* pBegin, const void* pEnd );

        /**
         * @brief 다형 객체 @p pObject 의 vtable 주소입니다. 객체가 어느 이미지의 코드인지 묻는 데 씁니다.
         * @details MSVC · Itanium ABI 모두 다형 (부분)객체의 첫 포인터가 vptr 입니다. 가상 함수를 가진 기반 포인터를 넘깁니다.
         */
        static const void* findVtableAddress( const void* pObject );
    };
} // namespace sw
