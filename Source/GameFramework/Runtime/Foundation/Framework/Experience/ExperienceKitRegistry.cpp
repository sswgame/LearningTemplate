#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceKitRegistry.h"

#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSubsystemRegistrar.h"

namespace sw
{
    bool activateKitWithoutServices( ExperienceScope& scope )
    {
        (void)scope;
        return true;
    }

    ExperienceKitRegistry::ExperienceKitRegistry()
        : _mutex{}
        , _registration{ "ExperienceKitRegistry", RegistrationOrder::Insertion, true, NameCase::IgnoreCase }
    {
    }

    bool ExperienceKitRegistry::registerEntry( const ExperienceKitRegistration* pRegistration )
    {
        if ( pRegistration == nullptr || StringUtil::isNullOrEmpty( pRegistration->_pName ) || pRegistration->_pActivate == nullptr )
            return false;
        RegistrationResult result = RegistrationResult::Added;
        {
            ScopedLock<mutex> lock{ _mutex };
            result = _registration.add( pRegistration, pRegistration->_pName ); // 잠금은 잎이다 — 거절 로그는 놓은 뒤에 쓴다
        }
        reportRegistrationRejected( result, "Experience kit", pRegistration->_pName );
        return result == RegistrationResult::Added;
    }

    void ExperienceKitRegistry::unregisterEntry( const ExperienceKitRegistration* pRegistration )
    {
        ScopedLock<mutex> lock{ _mutex };
        (void)_registration.remove( pRegistration ); // 등록되어 있지 않은 키트를 빼는 것은 할 일이 없다
    }

    const ExperienceKitRegistration* ExperienceKitRegistry::findKit( string_view name ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _registration.findByName( name );
    }

    /**
     * 키트 모듈의 등록자(`SW_EXPERIENCE_KIT`)가 붙는 곳 — 프로세스 범위다. 등록은 다른 모듈(키트)의 이미지와 수명이 같아, 엔진 범위(기동 단계와 함께 모듈 내리기보다
     * 먼저 내려간다)에 두면 키트가 내려가며 뗄 때 레지스트리가 이미 없어 멈췄다(`--check-text` 종료).
     */
    SW_MODULE_SUBSYSTEM( Process, ExperienceKitRegistry, ExperienceKitRegistry );
} // namespace sw
