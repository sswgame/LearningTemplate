/**
 * @file LoginStoreRecords.h
 * @brief 계정 키트의 저장소 레코드 형식입니다(`ServiceRecordTable` — 판은 형식의 `REFLECT( Version )`, 모르는 칸 보존). 비밀 칸은 `Secret32` 라 글 덤프 · 로그에 나가지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Security/NetSecurityTypes.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/SecretValue.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/AccountTypes.h"
#include "GameFramework/Runtime/Online/Store/ServiceRecordUnknownFields.h"

namespace sw
{
    /** @brief 이름 · 비밀번호 계정(`login_account`, 키는 소문자 이름)입니다. */
    REFLECT()
    struct LoginAccountRecord
    {
        REFLECT_BODY();
        PROPERTY()
        uint64 _accountID{ 0 };
        PROPERTY( MaxCount = LoginConstant::kMaxLoginNameSize )
        string _displayName{};
        PROPERTY( Min = 0, Max = 255 )
        uint8 _arrSalt[LoginConstant::kSaltSize]{};
        PROPERTY()
        Secret32 _passwordHash{};
        PROPERTY()
        uint32 _hashMemoryKiB{ 0 }; ///< 해시 매개변수(`NetPasswordHashParams`) — 값을 바꾸면 다음 로그인이 다시 해시한다
        PROPERTY()
        uint32 _hashIterationCount{ 0 };
        PROPERTY()
        uint32 _hashParallelism{ 0 };
        PROPERTY( Min = 0 )
        int32 _failedCount{ 0 };
        PROPERTY()
        int64 _lockedUntilMs{ 0 };

        ServiceRecordUnknownFields _unknownProperties{};

        NetPasswordHashParams getHashParams() const
        {
            NetPasswordHashParams params;
            params._memoryKiB      = _hashMemoryKiB;
            params._iterationCount = _hashIterationCount;
            params._parallelism    = _hashParallelism;
            return params;
        }
        void setHashParams( const NetPasswordHashParams& params )
        {
            _hashMemoryKiB      = params._memoryKiB;
            _hashIterationCount = params._iterationCount;
            _hashParallelism    = params._parallelism;
        }
    };

    /** @brief 계정 프로필(`login_account_id`, 키는 계정 16 진)입니다. 이름, 게스트, 외부 계정을 가리지 않고 모든 계정에 하나씩 있습니다. */
    REFLECT()
    struct LoginProfileRecord
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = LoginConstant::kMaxLoginNameSize )
        string _nameKey{}; ///< 이름과 비밀번호가 있으면 소문자 이름 키이고, 아니면 빈 문자열이다
        PROPERTY( MaxCount = LoginConstant::kMaxDisplayNameSize )
        string _displayName{};
        PROPERTY( MaxCount = LoginConstant::kDigestSize * 2 )
        string _guestDigestHex{}; ///< 게스트 장치 다이제스트이다. 지울 때 그 레코드를 찾는 데 쓰며, 없으면 빈 문자열이다
        PROPERTY()
        int64 _deletionDueMs{ 0 };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bGuest{ SW_FALSE };

        ServiceRecordUnknownFields _unknownProperties{};
        uint64                     _version{ 0 }; ///< 읽은 레코드 버전(저장하지 않는다)
    };

    /** @brief 세션(`login_session`, 키는 세션 16 진)입니다. 토큰 비밀은 담지 않고 그 다이제스트만 담는다. */
    REFLECT()
    struct LoginSessionRecord
    {
        REFLECT_BODY();
        PROPERTY()
        uint64 _accountID{ 0 };
        PROPERTY()
        int64 _issuedAtMs{ 0 };
        PROPERTY()
        int64 _expiresAtMs{ 0 };
        PROPERTY()
        int64 _resumeDeadlineMs{ 0 }; ///< 0이면 접속해 있다
        PROPERTY()
        uint32 _rotation{ 0 };
        PROPERTY()
        Secret32 _tokenDigest{};
        PROPERTY()
        LoginRevokeReason _revokeReason{ LoginRevokeReason::None }; ///< None 이 아니면 묘비이다

        ServiceRecordUnknownFields _unknownProperties{};
    };

    /** @brief 계정 → 외부 주체 연결(`login_account_external`, 키 `<계정 16 진>/<제공자>`)입니다. */
    REFLECT()
    struct LoginExternalSubjectRecord
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = LoginConstant::kDigestSize * 2 )
        string _subjectDigestHex{};

        ServiceRecordUnknownFields _unknownProperties{};
    };
} // namespace sw
