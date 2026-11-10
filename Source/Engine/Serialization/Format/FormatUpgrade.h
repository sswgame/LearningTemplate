/**
 * @file FormatUpgrade.h
 * @brief 저장된 형식의 판 올림 사슬입니다 — 종류마다 n → n+1 순수 함수를 등록하고(`SW_ASSET_FORMAT_UPGRADE` · `SW_SAVE_FORMAT_UPGRADE`), 읽는 쪽이 옛 판을 현재 판까지 올린다.
 * @details 출시 뒤 첫 판 올림에서 사용자 데이터(세이브)와 저장소 밖 데이터(모드 · 사용자 에셋)를 잃지 않게 한다. 빠진 단계는 오류다 — 옛 판을 그대로 읽거나
 *          기본값으로 채우는 폴백은 없다. 표가 빈 종류는 예전처럼 판이 다르면 거절한다(그 종류에 아직 이관이 없다).
 *          언리얼 `FCustomVersion`(종류마다 판 + Serialize 안의 판 분기)을 분기 대신 등록된 순수 함수 사슬로 둔 자리다 — 단계마다 단위 테스트가
 *          `Test/Fixtures/formats/<종류>/v<n>/` 의 표본을 올려 본다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"

#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    /** @brief 판 올림이 다루는 데이터의 자리입니다. 종류 이름은 자리마다 따로 센다. */
    enum class FormatUpgradeDomain : uint8
    {
        AssetXML,  ///< XML 에셋 — 종류 이름은 `AssetTypeDef::_name`, 판은 루트의 `formatVersion`
        SaveBytes, ///< 세이브 바이트 — 종류 이름은 `SaveGame::getFormatKind`, 판은 로컬 저장 봉투의 형식 판
    };

    /** @brief 한 단계가 고치는 데이터입니다. `AssetXML` 은 `_root`, `SaveBytes` 는 `_pBytes` 가 찬다. */
    struct FormatUpgradePayload
    {
        XMLNode        _root{};            ///< 에셋 루트(제자리에서 고친다)
        vector<uint8>* _pBytes{ nullptr }; ///< 세이브 바이트(제자리에서 고친다)
    };

    /** @brief n → n+1 순수 함수입니다. 데이터만 보고 고치며, 고치지 못하면 false 입니다(사슬이 멈추고 읽기가 실패한다). */
    using FormatUpgradeFunction = bool ( * )( FormatUpgradePayload& payload );

    /**
     * @struct FormatUpgradeStep
     * @brief 판 올림 단계 하나(등록 항목)와 그 표입니다. 문자열과 함수는 등록한 이미지의 정적 저장소에 있습니다.
     * @details 모듈을 내리면 그 모듈의 단계도 빠집니다. 같은 (자리, 종류, 판) 두 번 등록은 거절합니다.
     */
    struct SW_API FormatUpgradeStep
    {
        FormatUpgradeDomain   _domain;      ///< 데이터의 자리
        const utf8*           _pKind;       ///< 종류 이름
        uint32                _fromVersion; ///< 이 판을 `_fromVersion + 1` 로 올린다
        FormatUpgradeFunction _pFunction;   ///< 올리는 함수

        /** @brief 등록합니다. 같은 (자리, 종류, 판)이 이미 있거나 칸이 비면 false 이고 등록하지 않습니다. */
        [[nodiscard]] static bool registerStep( const FormatUpgradeStep* pStep );
        static void               unregisterStep( const FormatUpgradeStep* pStep );

        /**
         * @brief @p payload 를 @p fromVersion 에서 @p toVersion 까지 한 판씩 올립니다. 같은 판이면 아무것도 하지 않고 true 입니다.
         * @return 새 판에서 옛 판으로(내리기) · 빠진 단계 · 단계 실패면 종류와 판을 적은 오류를 남기고 false 입니다(데이터는 중간 판일 수 있다 — 쓰지 않는다).
         */
        [[nodiscard]] static bool upgrade( FormatUpgradeDomain domain, string_view kind, uint32 fromVersion, uint32 toVersion, FormatUpgradePayload& payload );
        /** @brief (자리, 종류)에 등록된 단계 수입니다(진단 · 테스트). */
        static uint32 countSteps( FormatUpgradeDomain domain, string_view kind );
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_*_FORMAT_UPGRADE` 가 두는 정적 등록자입니다(공통 틀 `StaticRegistrar`). */
    SW_DEFINE_STATIC_REGISTRAR( FormatUpgradeRegistrar, const FormatUpgradeStep*, &FormatUpgradeStep::registerStep, &FormatUpgradeStep::unregisterStep );
} // namespace sw

/**
 * @brief XML 에셋 종류 @p kindName(`AssetTypeDef::_name`)의 판 @p fromVersion → @p toVersion 단계를 등록합니다(.cpp 의 파일 범위, `namespace sw` 안).
 *        @p toVersion 은 @p fromVersion + 1 이어야 한다(한 단계씩 — 건너뛰는 단계는 컴파일 오류). @p id 는 정적 이름을 만드는 식별자입니다.
 */
#define SW_ASSET_FORMAT_UPGRADE( id, kindName, fromVersion, toVersion, pFunction )                             \
    static_assert( ( toVersion ) == ( fromVersion ) + 1, "a format upgrade step raises exactly one version" ); \
    SW_STATIC_REGISTRATION( formatUpgrade, ::sw::FormatUpgradeStep, ::sw::FormatUpgradeRegistrar, id, ::sw::FormatUpgradeDomain::AssetXML, kindName, fromVersion, pFunction )

/** @brief 세이브 종류 @p kindName(`SaveGame::getFormatKind`)의 판 @p fromVersion → @p toVersion 단계를 등록합니다. 규칙은 `SW_ASSET_FORMAT_UPGRADE` 와 같다. */
#define SW_SAVE_FORMAT_UPGRADE( id, kindName, fromVersion, toVersion, pFunction )                              \
    static_assert( ( toVersion ) == ( fromVersion ) + 1, "a format upgrade step raises exactly one version" ); \
    SW_STATIC_REGISTRATION( formatUpgrade, ::sw::FormatUpgradeStep, ::sw::FormatUpgradeRegistrar, id, ::sw::FormatUpgradeDomain::SaveBytes, kindName, fromVersion, pFunction )
