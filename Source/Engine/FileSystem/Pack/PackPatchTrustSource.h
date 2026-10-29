/**
 * @file PackPatchTrustSource.h
 * @brief 패치 목록 서명의 신뢰 설정을 기동 앞에서 만드는 자리입니다 — 엔진 설정의 공개 키와 암호 제공자를 묶습니다(`PackPatchTrust`).
 * @details 암호 제공자(OpenSSL)는 GameFramework 에 있어 Engine 은 링크하지 않는다. GameFramework 가 그 제공자를 모듈 서브시스템으로 프로세스 범위에 공개하고
 *          (`SW_MODULE_SUBSYSTEM( Process, OpenSSLNetSecurityProvider, INetSecurityProvider )`), 엔진은 인터페이스 타입으로 찾는다(`engine::findSubsystem<INetSecurityProvider>()`)
 *          — 함수 칸을 두고 높은 층이 채우던 길(언리얼 IModularFeatures 의 구현 등록과 같은 자리)을 범위 조회 하나로 바꿨다. 시작 마운트(`AssetManager::mountStartupPacks`)와
 *          패치 만들기(`-make-patch`)가 모두 같은 조회로 제공자를 받는다 — 게임 인스턴스가 서기 전이어도 닿는다(모듈 라이브러리는 에셋 마운트보다 먼저 올라온다).
 *          - **공개 키**는 엔진 설정(`EngineConfig::_patchTrustPublicKeyXHex` · `Y`, ES256 P-256 의 x · y 각 64 자리 16 진)이다. 둘 다 비면 서명을 요구하지 않는다(스팀처럼 스토어가
 *            무결성을 맡는 채널 — 배포가 정한다). 하나만 있거나 형식이 틀리면 오류다.
 *          - 서명을 요구하는데(키가 있는데) 공개된 제공자가 없으면 오류로 멈춘다 — 서명을 못 보는 채로 패치를 올리는 폴백은 없다.
 *          - 개인 키는 이 설정에 없다 — 서명하는 쪽만 `-patch-sign-key <PEM 경로>` 로 저장소 밖 파일에서 읽는다(`PackPatchBuilder`).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/FileSystem/Pack/PackPatch.h"

namespace sw
{
    struct EngineConfig;

    /** @brief 신뢰 설정을 만든 결과입니다. */
    enum class PackPatchTrustResult : uint8
    {
        NotConfigured = 0, ///< 공개 키 설정이 없다 — 서명을 요구하지 않는다
        Ready,             ///< 신뢰 설정을 채웠다
        Invalid            ///< 키 설정이 틀렸거나 공개된 제공자가 없다(이유를 남겼다) — 기동을 멈춘다
    };

    /**
     * @class PackPatchTrustSource
     * @brief 신뢰 설정 만들기입니다(상태 없는 함수).
     */
    class SW_API PackPatchTrustSource
    {
    public:
        /**
         * @brief 엔진 설정의 공개 키와 공개된 암호 제공자로 @p outTrust 를 만듭니다. 결과 값은 `PackPatchTrustResult` 를 보세요.
         */
        [[nodiscard]] static PackPatchTrustResult resolve( const EngineConfig& config, PackPatchTrust& outTrust );
    };
} // namespace sw
