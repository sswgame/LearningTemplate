#include "pch.h"

#include "Engine/FileSystem/Pack/PackPatchTrustSource.h"

#include "Core/Log/Logger.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/String/HexUtil.h"

#include "Engine/Config/EngineConfig.h"

namespace sw
{
    SW_LOG_CALLER( "PackPatchTrustSource" );

    namespace
    {
        PackPatchProviderGetter s_pfnProviderGetter = nullptr;

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

    void PackPatchTrustSource::setProviderGetter( PackPatchProviderGetter pfnGetter )
    {
        s_pfnProviderGetter = pfnGetter;
    }

    void PackPatchTrustSource::clearProviderGetter( PackPatchProviderGetter pfnGetter )
    {
        if ( s_pfnProviderGetter == pfnGetter )
            s_pfnProviderGetter = nullptr;
    }

    INetSecurityProvider* PackPatchTrustSource::findProvider()
    {
        return s_pfnProviderGetter != nullptr ? &s_pfnProviderGetter() : nullptr;
    }

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
        INetSecurityProvider* pProvider = findProvider();
        if ( pProvider == nullptr )
        {
            SW_LOG_ERROR( "Pack patch signatures are required (the engine config holds a public key) but the crypto provider slot is empty - the GameFramework module did not fill it before the packs were mounted" );
            return PackPatchTrustResult::Invalid;
        }
        outTrust._pProvider = pProvider;
        outTrust._publicKey = std::move( publicKey );
        return PackPatchTrustResult::Ready;
    }
} // namespace sw
