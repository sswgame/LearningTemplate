# ==============================================================================
# @file ThirdParty/openssl/CMakeLists.txt
# @brief OpenSSL 3(Apache-2.0 — 배포물 고지는 THIRD_PARTY_NOTICES). TLS 1.3 · X25519 · AEAD · HKDF 를 Engine 의 네트워크 보안 백엔드
#        (`Source/GameFramework/Base/Online/Security/OpenSSL`)만 쓴다 — 경계는 `CheckThirdPartyIsolation` 이 지킨다.
#        vcpkg 의 FindOpenSSL 래퍼가 OpenSSL::SSL · OpenSSL::Crypto 를 만든다. x64-windows 는 DLL(libssl-3-x64 · libcrypto-3-x64), x64-linux 는 정적.
# ==============================================================================

find_package(OpenSSL REQUIRED)

# 임포트 타깃은 이 디렉터리에만 보인다 — Engine 이 링크하므로 전역으로 올린다(tracy 와 같은 방식).
foreach(swOpenSslTarget IN ITEMS OpenSSL::SSL OpenSSL::Crypto)
	if(TARGET ${swOpenSslTarget})
		set_target_properties(${swOpenSslTarget} PROPERTIES IMPORTED_GLOBAL TRUE)
	endif()
endforeach()

add_library(openssl INTERFACE)
target_link_libraries(openssl INTERFACE OpenSSL::SSL OpenSSL::Crypto)
message(STATUS "[ThirdParty] OpenSSL ${OPENSSL_VERSION} (${OPENSSL_CRYPTO_LIBRARY})")
