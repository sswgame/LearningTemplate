/**
 * @file ModuleRenderPassRegistry.h
 * @brief 모듈(게임 · 키트)이 렌더 패스를 등록하는 창구입니다. 파이프라인 XML 의 `_type="Module"` 패스가 `_modulePass` 이름으로 여기를 봅니다.
 * @details **모듈 경계를 넘는 것은 데이터뿐입니다.** 모듈은 패스 이름과 패스 상수(이름 · float4)만 넘기고, 커맨드 리스트 · 트랜지언트 풀 · `FramePassContext` 는
 *          엔진 안에 남습니다. 렌더러는 병렬 기록 워커에서 락 안에서 상수를 값으로 복사해 갈 뿐 모듈 코드를 부르지 않습니다. 그래서
 *          - RT 안전: 모듈이 렌더 스레드 상태를 건드릴 길이 형식으로 없습니다(넘긴 값은 복사본이다).
 *          - 핫 리로드: 모듈 DLL 이 내려가도 렌더 스레드에 그 모듈의 함수 · vtable 이 남지 않습니다. 등록은 `ModuleRenderPassRegistration` 이 소멸자에서 풉니다.
 *          그리는 일(셰이더 · 입력 바인딩 · 대상)은 파이프라인 데이터가 정합니다 — 셰이더는 패스의 `_shaderPath`, 입력은 선언한 첨부의 역할, 대상은 첫 출력입니다.
 *          모듈이 직접 드로우를 기록하는 길(커맨드 리스트를 내보내는 길)은 열지 않았습니다. 필요해지면 그때 RT 계약(기록 시점 · 리소스 수명)을 따로 정합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    /**
     * @class ModuleRenderPassRegistry
     * @brief 모듈 패스 이름 → 패스 상수 목록입니다. 엔진 서비스 범위의 서브시스템이며 모든 함수가 스레드 안전합니다(락 하나).
     * @details 엔진이 소유하므로 게임 · 키트 모듈이 내려가도 사라지지 않습니다. 렌더러는 `engine::getSubsystem<ModuleRenderPassRegistry>()`,
     *          모듈은 `game::findSubsystem<ModuleRenderPassRegistry>()` 로 찾습니다(`EngineServiceList.xxx` 의 GameVisible 행).
     */
    class SW_API ModuleRenderPassRegistry : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. 초기화하거나 종료할 것이 없고, 수명만 컬렉션이 정합니다. */
        static constexpr const utf8* kSubsystemName = "ModuleRenderPass";

        /** @brief 패스 상수 하나입니다. 이름은 모듈 셰이더의 패스 상수 칸 이름입니다(바인더가 리플렉션 이름으로 맞춘다). */
        struct Constant
        {
            hashed_string _name{};
            float4        _value{};
        };

        /** @brief 패스 하나가 가질 수 있는 상수 수입니다. 넘기면 `setConstant` 가 거절합니다(패스 상수 버퍼는 크기가 정해져 있다). */
        static constexpr size_t kMaxConstantCount = 16;

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }
        MemoryTag           getSubsystemMemoryTag() const override { return MemoryTag::RenderCPU; }

        /** @brief 패스 이름을 등록합니다. 이름이 비었거나 이미 있으면 false 입니다(두 모듈이 한 이름을 나눠 쓰지 않는다). */
        [[nodiscard]] bool registerPass( const hashed_string& modulePass );
        /** @brief 패스 이름을 풉니다. 없으면 아무것도 하지 않습니다. 다음 프레임부터 그 패스는 오류를 남기고 그려지지 않습니다. */
        void unregisterPass( const hashed_string& modulePass );
        /** @brief 패스 상수 하나를 씁니다(같은 이름이면 덮어씀). 등록되지 않은 패스이거나 상수가 `kMaxConstantCount` 를 넘으면 false 입니다. */
        [[nodiscard]] bool setConstant( const hashed_string& modulePass, const hashed_string& name, const float4& value );
        /** @brief 등록됐는지 반환합니다. */
        bool isRegistered( const hashed_string& modulePass ) const;
        /** @brief 렌더러가 부릅니다. 상수를 값으로 복사합니다. 등록되지 않은 패스면 false 이고 @p outListConstant 는 비웁니다. */
        [[nodiscard]] bool copyConstants( const hashed_string& modulePass, vector<Constant>& outListConstant ) const;
        /**
         * @brief 그릴 수 없는 모듈 패스의 오류를 이름마다 한 번만 남기게 합니다. 처음이면 true 입니다.
         * @details 다시 등록하면 그 이름의 기록이 지워진다 — 리로드 뒤에 또 빠지면 다시 알린다.
         */
        bool noteMissingOnce( const hashed_string& modulePass );

    private:
        mutable mutex                                  _mutex;
        unordered_map<hashed_string, vector<Constant>> _mapPass;
        unordered_set<hashed_string>                   _uniqueMissingLogged;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ModuleRenderPassRegistration
     * @brief 모듈 패스 등록 하나의 수명입니다. 만들 때 등록하고 소멸할 때 풉니다 — 모듈이 내려갈 때 등록이 남지 않게 하는 형식입니다.
     * @details 모듈의 게임 인스턴스 · 서브시스템 멤버로 둡니다. 이름이 겹쳐 등록하지 못했으면 `isRegistered` 가 false 이고 오류가 남았습니다.
     *          레지스트리는 엔진 서비스 범위에 있어 이 객체보다 오래 삽니다.
     */
    class SW_API ModuleRenderPassRegistration
    {
    public:
        ModuleRenderPassRegistration( ModuleRenderPassRegistry& registry, string_view modulePass );
        ~ModuleRenderPassRegistration();

        ModuleRenderPassRegistration( const ModuleRenderPassRegistration& )            = delete;
        ModuleRenderPassRegistration& operator=( const ModuleRenderPassRegistration& ) = delete;

        bool isRegistered() const { return _bRegistered != SW_FALSE; }
        /** @brief 패스 상수를 씁니다. 등록하지 못한 등록이면 false 입니다. */
        [[nodiscard]] bool setConstant( string_view name, const float4& value );

    private:
        ModuleRenderPassRegistry& _registry;
        hashed_string             _modulePass;
        uint8                     _bRegistered;
    };
} // namespace sw
