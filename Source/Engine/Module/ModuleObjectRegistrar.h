/**
 * @file Engine/Module/ModuleObjectRegistrar.h
 * @brief 모듈 이미지의 정적 등록자 가운데 레지스트리가 엔진 밖(편집기 · 키트)에 있는 것의 공통 바탕입니다 — 모듈을 올릴 때 붙고 내리기 전에 떨어집니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class ModuleObjectRegistrar
     * @brief 이미지의 정적 객체로 두면 링크 때 목록(`getHead`)에 달리고, 모듈을 올린 쪽이 걷어 모듈 이름과 함께 붙이며(`attach`), 내리기 전에 뗍니다(`detach`).
     * @details 타입 등록자(`TypeRegistrar`) · 데이터 종류 등록자(`AssetDataKindRegistrar`)와 같은 길이다(`engine::registerModuleTypes` · `unregisterModuleTypes`).
     *          만들 때 바로 레지스트리에 오르지 않으므로 핫 리로드가 새 이미지를 먼저 올려 두어도 옛 이미지의 같은 id 와 부딪히지 않고, 떼기는 이미지 코드가 아직
     *          살아 있고 로더 잠금 밖일 때 일어나 레지스트리가 그 등록으로 만든 인스턴스를 그 자리에서 거둔다 — 코드 주소로 고르는 일이 없다.
     *          모듈 경로로 올라오지 않은 이미지(테스트 실행 파일)의 등록자는 붙지 않는다. 스레드: 모듈을 올리고 내리는 스레드만 부른다.
     */
    class SW_API ModuleObjectRegistrar
    {
    public:
        /** @brief 아직 아무도 걷어 가지 않은 등록자들의 목록 머리입니다(Engine 이미지가 가집니다). 걷어 간 쪽이 nullptr 로 비웁니다. 나중에 만든 것이 앞입니다. */
        static ModuleObjectRegistrar*& getHead();

        ModuleObjectRegistrar( const ModuleObjectRegistrar& )            = delete;
        ModuleObjectRegistrar& operator=( const ModuleObjectRegistrar& ) = delete;

        /** @brief 목록의 다음(먼저 만든 쪽)입니다. */
        ModuleObjectRegistrar* getNext() const { return _pNext; }
        /** @brief 레지스트리에 올립니다. 모듈을 올린 쪽이 한 번 부릅니다. */
        virtual void attach() = 0;
        /** @brief 레지스트리에서 뺍니다. 붙인 등록자만, 모듈을 내리기 전에 한 번 부릅니다. 뺀 등록으로 만든 인스턴스는 레지스트리가 이 안에서 거둔다. */
        virtual void detach() = 0;
        /**
         * @brief 떼는 차례입니다. 한 모듈의 등록자는 작은 차례부터, 같은 차례 안에서는 붙인 역순으로 뗍니다.
         * @details 다른 등록의 인스턴스가 쓰는 것(편집기 확장 서브시스템)은 1 을 돌려 쓰는 쪽(패널 · 명령)이 먼저 내려가게 한다.
         */
        virtual uint32 getDetachPass() const { return 0; }

    protected:
        ModuleObjectRegistrar();
        ~ModuleObjectRegistrar() = default;

    private:
        ModuleObjectRegistrar* _pNext;
    };
} // namespace sw
