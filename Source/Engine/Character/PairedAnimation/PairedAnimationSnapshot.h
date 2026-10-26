/**
 * @file PairedAnimationSnapshot.h
 * @brief 돌고 있는 페어드 애니메이션 세션의 결정적 상태입니다. 서버가 복제하고, 클라이언트가 늦게 들어오거나 롤백한 뒤 같은 세션을 이어 받는 데 씁니다.
 * @details 설계는 `docs/plans/PairedAnimation.md` 7절입니다. 세션 상태는 (에셋, 바인딩, 역할마다 진입 시 앵커 시각, 알림 커서, 앵커 시각)으로 정해집니다.
 *          이 구조체는 에셋과 바인딩을 뺀 나머지입니다. 에셋 경로와 역할에 해당하는 오브젝트 id 는 네트워크 계층이 따로 복제하고,
 *          클라이언트는 그것으로 바인딩을 만들어 `PairedAnimationSystem::restoreSession` 에 넘깁니다.
 *          "마지막 갱신 때의 상태"입니다. 앵커 시각은 그 갱신이 읽은 값(그 프레임 평가 전)이고, 알림 커서는 그 갱신이 낸 알림까지 셉니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    class PairedAnimationDef;

    /**
     * @enum PairedAnimationRoleStage
     * @brief 역할 하나의 실행 단계입니다.
     */
    enum class PairedAnimationRoleStage : uint8
    {
        Unbound = 0, ///< 바인딩이 없는 선택 역할
        Waiting,     ///< 바인딩은 있고 `start` 를 기다립니다.
        Playing,
        Exited,
    };
} // namespace sw

namespace sw
{
    /** @brief 역할 하나의 복제 상태입니다. 인덱스는 에셋의 역할 인덱스와 같습니다. */
    struct PairedAnimationRoleSnapshot
    {
        float32                  _entryAnchorTime{ 0.0f }; ///< 역할이 들어온 때의 앵커 시각입니다. `Playing` 일 때만 뜻이 있습니다.
        PairedAnimationRoleStage _stage{ PairedAnimationRoleStage::Unbound };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PairedAnimationSessionSnapshot
     * @brief 세션 상태 하나입니다. 파일 머리말 참고.
     */
    struct SW_API PairedAnimationSessionSnapshot
    {
        static constexpr uint32 kMagic = FourCcUtil::make( "SWPA" );

        /** @brief 바이트를 읽은 결과입니다. `Success` 밖은 출력 스냅샷을 비웁니다. */
        enum class ReadResult : uint8
        {
            Success = 0,
            WrongMagic, ///< 매직이 다릅니다.
            Truncated,  ///< 바이트가 모자라거나, 역할 수가 남은 바이트보다 큽니다.
        };

        uint32                              _sessionID{ 0 };
        float32                             _anchorTime{ 0.0f };
        uint32                              _nextNotifyIndex{ 0 }; ///< 에셋의 알림 목록(시각 순서)에서 아직 내지 않은 첫 알림입니다.
        vector<PairedAnimationRoleSnapshot> _listRole{};

        /**
         * @brief 세션이 시작하고 @p elapsedSeconds 가 지난 때의 이상적인 상태를 만듭니다. 서버가 시작 틱만 복제하고 클라이언트가 늦게 시작해 따라잡을 때 씁니다.
         * @details 앵커는 시각 0 에 들어왔고, `start` 가 지난 역할은 `start` 에 들어온 것으로 칩니다. 서버의 실제 진입은 프레임 단위라 최대 한 프레임 늦으므로 그 차이는 남습니다.
         *          알림 커서는 시각이 @p elapsedSeconds 이하인 알림을 넘깁니다(건너뛴 알림은 내지 않습니다).
         *          서버가 정확한 진입 시각을 알려 줄 수 있으면 `PairedAnimationSession::captureSnapshot` 의 값을 복제하는 쪽이 정확합니다.
         *          @p elapsedSeconds 가 타임라인 길이 이상이면 복원이 거절합니다(`PairedAnimationStartResult::InvalidSnapshot`).
         */
        [[nodiscard]] static PairedAnimationSessionSnapshot makeAtElapsedTime( const PairedAnimationDef& def, uint32 sessionID, float32 elapsedSeconds );

        /** @brief 바이트로 씁니다(`Core/Serialization/BitStream.h`). 읽는 쪽은 `readBytes` 입니다. */
        void writeBytes( vector<uint8>& outBytes ) const;
        /** @brief `writeBytes` 가 쓴 바이트를 읽습니다. 값이 에셋에 맞는지는 읽지 않고, 복원(`restoreSession`)이 에셋과 함께 검사합니다. */
        [[nodiscard]] static ReadResult readBytes( const vector<uint8>& bytes, PairedAnimationSessionSnapshot& outSnapshot );
    };
} // namespace sw
