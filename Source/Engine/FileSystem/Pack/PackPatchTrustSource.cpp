#include "pch.h"

#include "Engine/FileSystem/Pack/PackPatchTrustSource.h"

#include "Core/Log/Logger.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/String/HexUtil.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    SW_LOG_CALLER( "PackPatchTrustSource" );

    namespace
    {
        struct PackPatchTrustSourceInternal
        {
            static constexpr size_t kCoordinateHexLength = 64; ///< P-256 좌표 하나(32 바이트)의 16 진 길이

            /** @brief 64 자리 16 진 글을 32 바이트로 풉니다. 길이나 글자가 틀리면 false 입니다. */
            [[nodiscard]] static bool decodeCoordinate( string_view hex, vector<uint8>& outBytes )
            {
                if ( hex.size() != kCoordinateHexLength )
                    return false;
                return HexUtil::decodeBytes( hex, outBytes );
            }
        };
    } // namespace

    PackPatchTrustResult PackPatchTrustSource::resolve( const EngineConfig& config, PackPatchTrust& outTrust )
    {
        outTrust = PackPatchTrust{};
        if ( config._patchTrustPublicKeyXHex.empty() && config._patchTrustPublicKeyYHex.empty() )
            return PackPatchTrustResult::NotConfigured;
        NetPublicKey publicKey;
        publicKey._algorithm = NetSignatureAlgorithm::EcdsaP256Sha256;
        if ( PackPatchTrustSourceInternal::decodeCoordinate( config._patchTrustPublicKeyXHex, publicKey._x ) == false ||
             PackPatchTrustSourceInternal::decodeCoordinate( config._patchTrustPublicKeyYHex, publicKey._y ) == false )
        {
            SW_LOG_ERROR( "Engine config _patchTrustPublicKeyXHex / _patchTrustPublicKeyYHex must both be 64 hex digits (an ES256 P-256 public key) - the pack patch signature requirement is not usable" );
            return PackPatchTrustResult::Invalid;
        }
        // 제공자는 GameFramework 가 프로세스 범위에 공개한 모듈 서브시스템이다 — 범위를 올라가며 인터페이스 타입으로 찾는다.
        INetSecurityProvider* pProvider = engine::findSubsystem<INetSecurityProvider>();
        if ( pProvider == nullptr )
        {
            SW_LOG_ERROR( "Pack patch signatures are required (the engine config holds a public key) but no INetSecurityProvider is published - the GameFramework module was not loaded before the packs were mounted" );
            return PackPatchTrustResult::Invalid;
        }
        outTrust._pProvider = pProvider;
        outTrust._publicKey = std::move( publicKey );
        return PackPatchTrustResult::Ready;
    }
} // namespace sw
