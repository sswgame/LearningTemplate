/**
 * @file SequenceDiagnostics.h
 * @brief 컷씬 디버거가 읽는 재생 기록(`SequencePlaybackHistory`)과 키트가 보태는 진단 제공자(`SW_SEQUENCE_DIAGNOSTIC`)입니다.
 * @details **기록**: 플레이어마다 고정 용량(`kCapacity`)의 고리 버퍼라 오래 돌아도 늘지 않는다. 시작 · 끝 · 멈춤 · 이벤트 · 카메라 신호 · 묶이지 않은 바인딩을 남긴다.
 *          배포본(`build::kShipping`)은 적지 않는다 — 읽는 화면(에디터 컷씬 디버거)이 없다.
 *          **제공자**: 엔진 시퀀서는 키트를 모른다. 키트(게임 프레임워크의 시네마틱 카메라 · 외형)가 정적 등록자를 자기 모듈에 두면, 디버거가 고른 플레이어마다
 *          그 키트의 해석(실제 블렌드 가중치 · 적용한 외형)을 한 줄씩 붙인다. 등록자는 모듈 이미지의 정적 객체라 모듈이 내려가거나(언로드 · 핫 리로드) 키트가 꺼지면
 *          소멸자가 빼낸다 — 내려간 DLL 의 함수 포인터가 목록에 남지 않는다(`SW_AUTOMATION_WATCH` 와 같은 수명). 별도 키트 로더를 두지 않는다(`ModuleCatalog` 가 이미 정한다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/RegistrationList.h"
#include "Core/String/fixed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class SequencePlayerComponent;

    /** @brief 기록 한 줄의 종류입니다. 이름은 리플렉션이 준다(`getEnumName` — 감시 값 `Sequence.History` 의 key). */
    ENUM()
    enum class SequenceHistoryKind : uint8
    {
        Started = 0,    ///< 재생을 시작했다
        Finished,       ///< 끝 프레임에 닿아 끝났다(루프가 아닐 때)
        Stopped,        ///< 끝나기 전에 멈췄다(`stop` · 씬 나감 · 다시 틀기)
        Event,          ///< 시작 프레임을 지난 항목(이벤트 · 외형 · 시나리오)
        CameraSignal,   ///< 카메라 신호(`_detail` 이 `SequenceCameraSignalKind`)
        UnboundBinding, ///< 묶이지 않은 바인딩 때문에 틀지 않았다(`_binding`)
        Span,           ///< 구간 항목(애니메이션 · 소리 · 자막)에 들어갔다
    };

    /** @brief 기록 한 줄입니다. 이름은 고정 길이로 잘라 담는다(할당 없음). */
    struct SequenceHistoryEntry
    {
        fixed_string<constant::buffer::kMax64> _name{};       ///< 항목 이름(또는 바인딩 이름)
        uint32                                 _binding{ 0 }; ///< 관계된 바인딩 id(없으면 0)
        int32                                  _frame{ 0 };   ///< 시퀀스 프레임
        SequenceHistoryKind                    _kind{ SequenceHistoryKind::Started };
        uint8                                  _detail{ 0 }; ///< 종류별 덧값(카메라 신호의 종류)
    };

    /**
     * @class SequencePlaybackHistory
     * @brief 최근 기록 `kCapacity` 줄의 고리 버퍼입니다. 넘치면 가장 오래된 줄을 덮는다.
     */
    class SW_API SequencePlaybackHistory
    {
    public:
        /** @brief 담는 줄 수입니다. 디버거 화면 한 장과 시나리오 판정에 넉넉한 길이다. */
        static constexpr uint32 kCapacity = 32;

        SequencePlaybackHistory();

        /** @brief 줄을 더합니다(배포본은 아무것도 하지 않는다). */
        void record( SequenceHistoryKind kind, int32 frame, uint32 bindingID, string_view name, uint8 detail = 0 );
        /** @brief 모두 지웁니다(셈은 이어 간다 — 디버거가 새 줄을 알아본다). */
        void clear();
        /** @brief 지금 담긴 줄 수입니다(최대 `kCapacity`). */
        uint32 getCount() const { return _count; }
        /** @brief 지금까지 더한 줄의 누적 수입니다(덮인 줄 포함). */
        uint64 getRecordedCount() const { return _recordedCount; }
        /** @brief 담긴 줄 @p index 입니다(0 이 가장 오래된 줄). 범위 밖이면 멈춘다(부르는 쪽이 `getCount` 로 돈다). */
        const SequenceHistoryEntry& getEntry( uint32 index ) const;
        /** @brief 가장 최근 줄입니다. 없으면 nullptr 입니다. */
        const SequenceHistoryEntry* findLatest() const;
        /** @brief 담긴 줄 가운데 종류가 @p kind 인 줄 수입니다(감시 값 · 테스트). */
        uint32 countKind( SequenceHistoryKind kind ) const;

    private:
        SequenceHistoryEntry _arrEntry[kCapacity];
        uint64               _recordedCount;
        uint32               _head; ///< 다음에 쓸 칸
        uint32               _count;
    };
} // namespace sw

namespace sw
{
    /** @brief 진단 제공자 함수입니다. 고른 플레이어 @p player 에 대한 이 키트의 해석을 @p outText 에 적고, 붙일 것이 없으면 false 입니다. 게임 스레드에서 부릅니다. */
    using SequenceDiagnosticFunction = bool ( * )( const SequencePlayerComponent& player, string& outText );

    /** @brief 진단 제공자 한 줄입니다. 문자열과 함수는 등록한 이미지의 정적 저장소에 있습니다. */
    struct SequenceDiagnosticRegistration
    {
        const utf8*                _pName;     ///< 디버거 구역 이름(`Cinematic camera`)
        SequenceDiagnosticFunction _pDescribe; ///< 해석 함수
    };

    /** @brief 진단 제공자 목록입니다(정적 등록자가 올리고 내린다). */
    struct SW_API SequenceDiagnosticRegistry
    {
        [[nodiscard]] static bool registerProvider( const SequenceDiagnosticRegistration* pRegistration );
        static void               unregisterProvider( const SequenceDiagnosticRegistration* pRegistration );
        /** @brief 올라 있는 제공자를 등록 순서로 채웁니다. */
        static void collectProviders( vector<const SequenceDiagnosticRegistration*>& outListProvider );
    };

    /** @brief `SW_SEQUENCE_DIAGNOSTIC` 이 두는 정적 등록자입니다(공통 틀 `StaticRegistrar` — 거절된 등록은 내리지 않는다). */
    SW_DEFINE_STATIC_REGISTRAR( SequenceDiagnosticRegistrar, const SequenceDiagnosticRegistration*, &SequenceDiagnosticRegistry::registerProvider, &SequenceDiagnosticRegistry::unregisterProvider );
} // namespace sw

/** @brief 진단 제공자를 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). 모듈이 내려가면 빠진다. */
#define SW_SEQUENCE_DIAGNOSTIC( id, pName, pFunction ) SW_STATIC_REGISTRATION( sequenceDiagnostic, ::sw::SequenceDiagnosticRegistration, ::sw::SequenceDiagnosticRegistrar, id, pName, pFunction )
