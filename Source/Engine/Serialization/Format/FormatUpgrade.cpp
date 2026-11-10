#include "pch.h"

#include "Engine/Serialization/Format/FormatUpgrade.h"

#include "Core/Container/string.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    namespace
    {
        struct FormatUpgradeInternal
        {
            /** @brief 등록된 단계(등록 순서). Engine 이미지의 함수 정적이라 게임 모듈이 바뀌어도 남는다(`AutomationStageResetRegistry` 와 같다). */
            static SW_DEFINE_REGISTRATION_LIST( getSteps, const FormatUpgradeStep, "FormatUpgradeSteps", RegistrationOrder::Insertion, true )

                /** @brief 중복 검사 이름(`<자리>:<종류>:<판>`)입니다. */
                static string makeStepName( FormatUpgradeDomain domain, string_view kind, uint32 fromVersion )
            {
                string name = domain == FormatUpgradeDomain::AssetXML ? "AssetXML:" : "SaveBytes:";
                name += kind;
                name += ':';
                name += sw::to_string( fromVersion );
                return name;
            }

            static const FormatUpgradeStep* findStep( FormatUpgradeDomain domain, string_view kind, uint32 fromVersion )
            {
                const RegistrationList<const FormatUpgradeStep>& steps = getSteps();
                for ( uint32 index = 0; index < steps.getCount(); ++index )
                {
                    const FormatUpgradeStep* pStep = steps.getAt( index );
                    if ( pStep->_domain == domain && pStep->_fromVersion == fromVersion && string_view{ pStep->_pKind } == kind )
                        return pStep;
                }
                return nullptr;
            }
        };
    } // namespace

    bool FormatUpgradeStep::registerStep( const FormatUpgradeStep* pStep )
    {
        if ( pStep == nullptr || StringUtil::isNullOrEmpty( pStep->_pKind ) || pStep->_pFunction == nullptr )
            return false;
        const string name = FormatUpgradeInternal::makeStepName( pStep->_domain, pStep->_pKind, pStep->_fromVersion );
        return FormatUpgradeInternal::getSteps().addLogged( pStep, "Format upgrade step", name ) == RegistrationResult::Added;
    }

    void FormatUpgradeStep::unregisterStep( const FormatUpgradeStep* pStep )
    {
        (void)FormatUpgradeInternal::getSteps().remove( pStep ); // 등록되어 있지 않은(거절된) 항목을 빼는 것은 할 일이 없다
    }

    bool FormatUpgradeStep::upgrade( FormatUpgradeDomain domain, string_view kind, uint32 fromVersion, uint32 toVersion, FormatUpgradePayload& payload )
    {
        if ( fromVersion > toVersion )
        {
            SW_LOG_ERROR_ID( "FormatUpgrade.NewerThanSupported", "%# format version %# is newer than the supported %# - written by a newer build", string( kind ).c_str(), fromVersion, toVersion );
            return false;
        }
        for ( uint32 version = fromVersion; version < toVersion; ++version )
        {
            const FormatUpgradeStep* pStep = FormatUpgradeInternal::findStep( domain, kind, version );
            if ( pStep == nullptr )
            {
                SW_LOG_ERROR_ID( "FormatUpgrade.StepMissing", "%# format version %# has no upgrade step to %# (supported %#) - add SW_*_FORMAT_UPGRADE or rewrite the data", string( kind ).c_str(), version, version + 1,
                                 toVersion );
                return false;
            }
            if ( pStep->_pFunction( payload ) == false )
            {
                SW_LOG_ERROR_ID( "FormatUpgrade.StepFailed", "%# format upgrade %# -> %# failed", string( kind ).c_str(), version, version + 1 );
                return false;
            }
        }
        return true;
    }

    uint32 FormatUpgradeStep::countSteps( FormatUpgradeDomain domain, string_view kind )
    {
        const RegistrationList<const FormatUpgradeStep>& steps = FormatUpgradeInternal::getSteps();
        uint32                                           count = 0;
        for ( uint32 index = 0; index < steps.getCount(); ++index )
        {
            const FormatUpgradeStep* pStep = steps.getAt( index );
            if ( pStep->_domain == domain && string_view{ pStep->_pKind } == kind )
                ++count;
        }
        return count;
    }
} // namespace sw
