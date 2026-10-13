/**
 * @file Engine/Module/ReflectionSubsystem.h
 * @brief 기동 순서의 Reflection 자리입니다 — 이 프로세스에 올라온 타입 등록기를 모으고 명령줄의 enum 이름을 적용합니다. 타입 표(`TypeRegistry`)를 소유합니다.
 * @details 배포 구성은 GameFramework · 킷 · 게임이 정적 링크라 "Engine" 이 다 모으고, 개발 구성의 모듈 DLL 은 `ModuleTypes` 단계에서 호스트 로더가 올리며
 *          제 이름으로 등록한다. 명령줄의 enum 이름(`-gv_rhiBackend=Vulkan`)은 enum 표가 선 지금 적용한다 — 모르는 이름이면 기본값으로 돌지 않고 멈춘다.
 *          테스트 하네스는 이것을 상속해 GameFramework · 테스트 타입까지 모은다(`TestReflectionSubsystem`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    class TypeRegistry;

    /**
     * @class ReflectionSubsystem
     * @brief 타입 등록기를 모아 타입 표를 채우는 서브시스템입니다. 설정 · 에셋 · 씬 객체가 이 뒤에 서서 먼저 소멸한다.
     */
    class SW_API ReflectionSubsystem : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "Reflection";

        /** @brief @p typeRegistry 를 넘겨받습니다(서비스 표에는 호스트가 이미 묶었다). */
        explicit ReflectionSubsystem( unique_ptr<TypeRegistry> typeRegistry );
        ~ReflectionSubsystem() override;

        ReflectionSubsystem( const ReflectionSubsystem& )            = delete;
        ReflectionSubsystem& operator=( const ReflectionSubsystem& ) = delete;

        SubsystemInitResult initializeSubsystem() override;
        /** @brief enum 이름 풀이는 타입 표를 본다 — 표가 소멸하기 전에 뗀다. */
        void        shutdownSubsystem() override;
        const utf8* getSubsystemName() const override { return kSubsystemName; }
        /** @brief 설정 · 엔진 데이터는 낮은 층이라 이것의 이름을 모른다 — 역방향으로 적는다. */
        span<const utf8* const> getSubsystemDependents() const override;
        MemoryTag               getSubsystemMemoryTag() const override { return MemoryTag::Reflection; }

        TypeRegistry& getTypeRegistry() const { return *_typeRegistry; }

    protected:
        /** @brief 모은 뒤 명령줄 enum 이름을 적용합니다. 하네스가 더 모은 다음 부른다. */
        static SubsystemInitResult bindEnumNames();

    private:
        unique_ptr<TypeRegistry> _typeRegistry;
    };
} // namespace sw
