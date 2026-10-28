/**
 * @file PackPatchTrustSource.h
 * @brief 패치 목록 서명의 신뢰 설정을 기동 앞에서 만드는 자리입니다 — 엔진 설정의 공개 키와 암호 제공자 함수 칸을 묶습니다(`PackPatchTrust`).
 * @details 암호 제공자(OpenSSL)는 GameFramework 에 있어 Engine 은 링크하지 않는다. 그래서 Engine 이 **함수 칸**(`PackPatchProviderGetter`)을 하나 두고,
 *          GameFramework 가 모듈이 올라올 때(정적 등록자의 생성자) `NetSecurity::getProvider` 로 칸을 채우고 내려갈 때 비운다. 시작 마운트(`AssetManager::mountStartupPacks`)와
 *          패치 만들기(`-make-patch`)가 모두 같은 칸에서 제공자를 받는다 — 게임 인스턴스가 서기 전이어도 닿는다(모듈 이미지는 에셋 마운트보다 먼저 올라온다).
 *          - **공개 키**는 엔진 설정(`EngineConfig::_patchTrustPublicKeyXHex` · `Y`, ES256 P-256 의 x · y 각 64 자리 16 진)이다. 둘 다 비면 서명을 요구하지 않는다(스팀처럼 스토어가
 *            무결성을 맡는 채널 — 배포가 정한다). 하나만 있거나 형식이 틀리면 오류다.
 *          - 서명을 요구하는데(키가 있는데) 칸이 비었으면 오류로 멈춘다 — 서명을 못 보는 채로 패치를 올리는 폴백은 없다.
 *          - 개인 키는 이 설정에 없다 — 서명하는 쪽만 `-patch-sign-key <PEM 경로>` 로 저장소 밖 파일에서 읽는다(`PackPatchBuilder`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Asset/Pack/PackPatch.h"

namespace sw
{
    class INetSecurityProvider;
    struct EngineConfig;

    /** @brief 암호 제공자를 돌려주는 함수입니다(`NetSecurity::getProvider` 가 이 모양이다). */
    using PackPatchProviderGetter = INetSecurityProvider& (*)();

    /** @brief 신뢰 설정을 만든 결과입니다. */
    enum class PackPatchTrustResult : uint8
    {
        NotConfigured = 0, ///< 공개 키 설정이 없다 — 서명을 요구하지 않는다
        Ready,             ///< 신뢰 설정을 채웠다
        Invalid            ///< 키 설정이 틀렸거나 제공자 칸이 비었다(이유를 남겼다) — 기동을 멈춘다
    };

    /**
     * @class PackPatchTrustSource
     * @brief 제공자 함수 칸과 신뢰 설정 만들기입니다. 칸은 모듈 로드 · 언로드(게임 스레드 기동 구간)에서만 바뀝니다.
     */
    class SW_API PackPatchTrustSource
    {
    public:
        /** @brief 제공자 함수를 칸에 넣습니다(GameFramework 가 올라올 때). 다른 함수가 이미 있으면 바꿉니다(핫 리로드의 교체 이미지). */
        static void setProviderGetter( PackPatchProviderGetter pfnGetter );
        /** @brief 칸이 @p pfnGetter 이면 비웁니다(내려가는 이미지가 자기 함수만 뺀다 — 새 이미지가 이미 채웠다면 건드리지 않는다). */
        static void clearProviderGetter( PackPatchProviderGetter pfnGetter );
        /** @brief 칸의 제공자입니다. 칸이 비었으면 nullptr 입니다. */
        [[nodiscard]] static INetSecurityProvider* findProvider();

        /**
         * @brief 엔진 설정의 공개 키와 제공자 칸으로 @p outTrust 를 만듭니다. 결과 값은 `PackPatchTrustResult` 를 보세요.
         */
        [[nodiscard]] static PackPatchTrustResult resolve( const EngineConfig& config, PackPatchTrust& outTrust );
    };
} // namespace sw
