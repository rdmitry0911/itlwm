//
//  AirportItlwmAPSTAInterface.hpp
//  itlwm
//
//  Recovered Tahoe APSTA/SAP owner layout scaffolding.
//

#ifndef AirportItlwmAPSTAInterface_hpp
#define AirportItlwmAPSTAInterface_hpp

#include <stddef.h>
#include <stdint.h>

enum {
    kAirportItlwmAPSTAVirtualIfCreateCarrierSize = 0x20,
    kAirportItlwmAPSTAVirtualIfBsdNameSize = 0x10,
    kAirportItlwmAPSTARegistrationInfoSize = 0x130,
    kAirportItlwmAPSTARegistrationInitType = 1,
    kAirportItlwmAPSTARegistrationType14 = 2,
    kAirportItlwmAPSTARegistrationSubFamily = 3,
    kAirportItlwmAPSTARegistrationUnit = 1,
    kAirportItlwmAPSTARegistrationWakeFlagBit = 0x4,
    kAirportItlwmAPSTARole = 7,
    kAirportItlwmAPSTAFeatureGate0d = 0x0d,
    kAirportItlwmAPSTAFeatureGate0c = 0x0c,
    kAirportItlwmAPSTACoreExpansionProximityOwnerOffset = 0x2c28,
    kAirportItlwmAPSTACoreExpansionOwnerOffset = 0x2c30,
    kAirportItlwmAPSTACreateFailedReturn = 0xe00002bd,
    kAirportItlwmAPSTAAlreadyExistsReturn = 0xe00002d2,
    kAirportItlwmAPSTAUnknownRoleReturn = 0xe0000001,
    kAirportItlwmAPSTARawInvalidArgumentReturn = 0x16,
    kAirportItlwmAPSTASoftAPNotReadyReturn = 6,
    kAirportItlwmAPSTAInvalidSoftAPInfoReturn = 0xe00002c2,
    kAirportItlwmAPSTARSNConfRejectedReturn = 0xe00002d5,
    kAirportItlwmAPSTASoftAPState = 4,
    kAirportItlwmAPSTAPeerCacheMaximumSize = 8,
    kAirportItlwmAPSTAHostApModeHiddenValue = 1,
    kAirportItlwmAPSTACoreFeatureFlagStoreOffset = 0x45a8,
    kAirportItlwmAPSTACoreFeatureFlagByteCount = 0x10,
    kAirportItlwmAPSTACoreFeatureFlagMaxExclusive =
        kAirportItlwmAPSTACoreFeatureFlagByteCount * 8,
    kAirportItlwmAPSTACoreFeatureFlagIndexShift = 3,
    kAirportItlwmAPSTACoreFeatureFlagIndexMask = 0x07,
    kAirportItlwmAPSTAWifiNetworkInfoIESize = 0x24,
    kAirportItlwmAPSTAWifiNetworkInfoFeatureGate46 = 0x46,
    kAirportItlwmAPSTAWifiNetworkInfoFeatureGate46ByteIndex =
        kAirportItlwmAPSTAWifiNetworkInfoFeatureGate46 >>
        kAirportItlwmAPSTACoreFeatureFlagIndexShift,
    kAirportItlwmAPSTAWifiNetworkInfoFeatureGate46BitMask =
        1 << (kAirportItlwmAPSTAWifiNetworkInfoFeatureGate46 &
              kAirportItlwmAPSTACoreFeatureFlagIndexMask),
    kAirportItlwmAPSTAWifiNetworkInfoLengthTrapThreshold = 0x21,
    kAirportItlwmAPSTAWifiNetworkInfoMaxAcceptedLength =
        kAirportItlwmAPSTAWifiNetworkInfoLengthTrapThreshold - 1,
    kAirportItlwmAPSTACsaMaximumExcludedChannelSpec = 0x10000,
    kAirportItlwmAPSTASoftAPStatsSize = 0x58,
    kAirportItlwmAPSTARSNConfGateBit = 0x10,
    kAirportItlwmAPSTARSNConfHasNullGuard = 0,
    kAirportItlwmAPSTARSNConfVersionCountOffset = 0x08,
    kAirportItlwmAPSTARSNConfVersionListOffset = 0x0c,
    kAirportItlwmAPSTARSNConfPairwiseCipherCountOffset = 0x2c,
    kAirportItlwmAPSTARSNConfPairwiseCipherListOffset = 0x30,
    kAirportItlwmAPSTARSNConfGroupVersionCountOffset = 0x58,
    kAirportItlwmAPSTARSNConfGroupVersionListOffset = 0x5c,
    kAirportItlwmAPSTARSNConfGroupCipherCountOffset = 0x7c,
    kAirportItlwmAPSTARSNConfGroupCipherListOffset = 0x80,
    kAirportItlwmAPSTARSNConfMfpWordOffset = 0xa0,
    kAirportItlwmAPSTARSNConfCarrierSize = 0xa4,
    kAirportItlwmAPSTARSNConfListMaxCount = 8,
    kAirportItlwmAPSTARSNConfVersionClampLimit = 7,
    kAirportItlwmAPSTARSNConfMapMaxIndex = 8,
    kAirportItlwmAPSTARSNConfPairwiseCipherValue1 = 1,
    kAirportItlwmAPSTARSNConfPairwiseCipherValue2 = 2,
    kAirportItlwmAPSTARSNConfPairwiseCipherValue1Mask = 0x02,
    kAirportItlwmAPSTARSNConfPairwiseCipherValue2Mask = 0x04,
    kAirportItlwmAPSTARSNConfGroupCipherValue4 = 4,
    kAirportItlwmAPSTARSNConfGroupCipherValue8 = 8,
    kAirportItlwmAPSTARSNConfGroupCipherValue1000 = 0x1000,
    kAirportItlwmAPSTARSNConfGroupCipherValue4Mask = 0x40,
    kAirportItlwmAPSTARSNConfGroupCipherValue8Mask = 0x80,
    kAirportItlwmAPSTARSNConfGroupCipherValue1000Mask = 0x40000,
    kAirportItlwmAPSTATxSubQueueCount = 4,
    kAirportItlwmAPSTARegisterExtraQueueCount = 2,
    kAirportItlwmAPSTARegisterQueueCount =
        kAirportItlwmAPSTATxSubQueueCount + kAirportItlwmAPSTARegisterExtraQueueCount,
    kAirportItlwmAPSTAForwardPacketQueueSelectorShift = 4,
    kAirportItlwmAPSTAForwardPacketQueueSelectorMask = 0xff8,
    kAirportItlwmAPSTAForwardPacketTxSubQueueBaseOffset = 0x300,
    kAirportItlwmAPSTAForwardPacketQueueSubmitVtableOffset = 0x318,
    kAirportItlwmAPSTADefaultTxHeadroom = 0,
    kAirportItlwmAPSTAMissingQueueMetricValue = 0,
    kAirportItlwmAPSTATxQueueDepthObjectOffset = 0x168,
    kAirportItlwmAPSTATxQueueDepthValueOffset = 0x28,
    kAirportItlwmAPSTARxQueueCapacityObjectOffset = 0x138,
    kAirportItlwmAPSTARxQueueCapacityValueOffset = 0x10,
    kAirportItlwmAPSTADatapathOwnerEnableVtableOffset = 0x120,
    kAirportItlwmAPSTADatapathOwnerDisableVtableOffset = 0x128,
    kAirportItlwmAPSTACompletionQueueStartVtableOffset = 0x150,
    kAirportItlwmAPSTACompletionQueueStopVtableOffset = 0x158,
    kAirportItlwmAPSTARxCompletionQueueArmVtableOffset = 0x298,
    kAirportItlwmAPSTARxCompletionQueueArmArg0 = 0,
    kAirportItlwmAPSTARxCompletionQueueArmArg1 = 0,
    kAirportItlwmAPSTADatapathMissingQueueReturn = 0xe00002bc,
    kAirportItlwmAPSTAStartDatapathOwnerConfigVtableOffset = 0x118,
    kAirportItlwmAPSTAStartWorkQueueAddSourceVtableOffset = 0x140,
    kAirportItlwmAPSTAStartWorkQueueRemoveSourceVtableOffset = 0x148,
    kAirportItlwmAPSTAStartTxQueueTrapThreshold = 7,
    kAirportItlwmAPSTAStartMaxAcceptedTxQueues =
        kAirportItlwmAPSTAStartTxQueueTrapThreshold - 1,
    kAirportItlwmAPSTAStartRegisterFlags = 0,
    kAirportItlwmAPSTAObjectReleaseVtableOffset = 0x28,
    kAirportItlwmAPSTATimerCancelVtableOffset = 0x158,
    kAirportItlwmAPSTATeardownQueueStopVtableOffset = 0x158,
    kAirportItlwmAPSTATeardownWorkQueueRemoveSourceVtableOffset = 0x148,
    kAirportItlwmAPSTATeardownMulticastDirectRemoveWorkSourceCall = 1,
    kAirportItlwmAPSTAStopSuperVtableOffset = 0x5d8,
    kAirportItlwmAPSTATeardownNullValue = 0,
    kAirportItlwmAPSTAFreeAPStatsTimerOffset = 0x70,
    kAirportItlwmAPSTAFreeAPMonitorTimerOffset = 0x78,
    kAirportItlwmAPSTAFreeResource240Offset = 0x240,
    kAirportItlwmAPSTAFreeResource248Offset = 0x248,
    kAirportItlwmAPSTAFreeResource250Offset = 0x250,
    kAirportItlwmAPSTAFreeResource258Offset = 0x258,
    kAirportItlwmAPSTAFreeResource260Offset = 0x260,
    kAirportItlwmAPSTAStopTxSubQueueBaseOffset = 0x300,
    kAirportItlwmAPSTAStopTxCompletionQueueOffset = 0x2e8,
    kAirportItlwmAPSTAStopRxCompletionQueueOffset = 0x2f0,
    kAirportItlwmAPSTAStopMulticastQueueOffset = 0x320,
    kAirportItlwmAPSTAResetScalar00Offset = 0x00,
    kAirportItlwmAPSTAResetState26cOffset = 0x26c,
    kAirportItlwmAPSTAResetFlag329Offset = 0x329,
    kAirportItlwmAPSTAResetConcurrencyMode = 4,
    kAirportItlwmAPSTAResetConcurrencyEnabled = 0,
    kAirportItlwmAPSTAResetPowerSaveState = 0,
    kAirportItlwmAPSTAResetPowerSaveReason = 0x0a,
    kAirportItlwmAPSTAResetTimerActionVtableOffset = 0x218,
    kAirportItlwmAPSTASoftAPRuntime90Offset = 0x90,
    kAirportItlwmAPSTASoftAPRuntime98Offset = 0x98,
    kAirportItlwmAPSTASoftAPRuntimeA0Offset = 0xa0,
    kAirportItlwmAPSTASoftAPRuntimeB0Offset = 0xb0,
    kAirportItlwmAPSTASoftAPRuntimeBlockOffset = 0xb8,
    kAirportItlwmAPSTASoftAPRuntimeBlockClearSize = 0xf0,
    kAirportItlwmAPSTASoftAPRuntime1a8Offset = 0x1a8,
    kAirportItlwmAPSTASoftAPStatsOffset = 0x1b0,
    kAirportItlwmAPSTAInitSoftAPDtimPeriodOffset = 0x16,
    kAirportItlwmAPSTAInitSoftAPAppliedDtimPeriodOffset = 0x6a,
    kAirportItlwmAPSTAInitSoftAPDefaultDtimPeriod = 1,
    kAirportItlwmAPSTAInitSoftAPDefaultParam18 = 0x0f,
    kAirportItlwmAPSTAInitSoftAPDefaultParam1c = 0x1e,
    kAirportItlwmAPSTAInitSoftAPDefaultParam20 = 0x708,
    kAirportItlwmAPSTAInitSoftAPDefaultParam24 = 0x0a,
    kAirportItlwmAPSTAInitSoftAPDefaultParam28 = 3,
    kAirportItlwmAPSTAInitSoftAPDtimIoctl = 0x4e,
    kAirportItlwmAPSTABeaconIntervalAppliedOffset = 0x68,
    kAirportItlwmAPSTABeaconDtimAppliedOffset = 0x6a,
    kAirportItlwmAPSTABeaconIntervalIoctl = 0x4c,
    kAirportItlwmAPSTABeaconDtimPeriodIoctl = 0x4e,
    kAirportItlwmAPSTABeaconPayloadSize = 4,
    kAirportItlwmAPSTABeaconIntervalCallbackLogLevel = 1,
    kAirportItlwmAPSTABeaconDtimCallbackLogLevel = 1,
    kAirportItlwmAPSTABeaconIntervalSyncErrorLine = 0x106b,
    kAirportItlwmAPSTABeaconIntervalCallbackErrorLine = 0x1079,
    kAirportItlwmAPSTABeaconDtimSyncErrorLine = 0x1091,
    kAirportItlwmAPSTABeaconDtimCallbackErrorLine = 0x109f,
    kAirportItlwmAPSTAAsyncRxPayloadDataOffset = 0x00,
    kAirportItlwmAPSTAAsyncRxPayloadLengthOffset = 0x08,
    kAirportItlwmAPSTAAsyncRxPayloadTelemetryFlag = 1,
    kAirportItlwmAPSTADeleteIPv4PktFilterPayloadValue = 0x6c,
    kAirportItlwmAPSTADeleteIPv4PktFilterPayloadSize = 4,
    kAirportItlwmAPSTADeleteIPv4PktFilterCallbackLogLevel = 2,
    kAirportItlwmAPSTADeleteIPv4PktFilterCallbackErrorLine = 0x0ea0,
    kAirportItlwmAPSTACommandPayloadDataOffset = 0x00,
    kAirportItlwmAPSTACommandPayloadLengthOffset = 0x08,
    kAirportItlwmAPSTACommandCallbackFunctionOffset = 0x08,
    kAirportItlwmAPSTACommandCallbackCookieOffset = 0x10,
    kAirportItlwmAPSTABeaconCallbackTelemetryEnabled = 1,
    kAirportItlwmAPSTAHostApSuccessStateValue = 1,
    kAirportItlwmAPSTAHostApStateOffset = 0x26c,
    kAirportItlwmAPSTAHostApStatsAgeOffset = 0x20c,
    kAirportItlwmAPSTAHostApRuntime88Offset = 0x88,
    kAirportItlwmAPSTAHostApStatsTimerOffset = 0x70,
    kAirportItlwmAPSTAHostApMonitorTimerOffset = 0x78,
    kAirportItlwmAPSTAHostApMonitorTimerScheduleVtableOffset = 0x1d0,
    kAirportItlwmAPSTAHostApMonitorTimerInterval = 0x3e8,
    kAirportItlwmAPSTAHostApNetworkDataFlagsOffset = 0x04,
    kAirportItlwmAPSTAHostApShortBeaconFlagBit = 8,
    kAirportItlwmAPSTAHostApClosedNetFlagBit = 9,
    kAirportItlwmAPSTAHostApBeaconIntervalNormal = 0x12c,
    kAirportItlwmAPSTAHostApBeaconIntervalShort = 0x64,
    kAirportItlwmAPSTAHostApClosedNetPayloadValue = 1,
    kAirportItlwmAPSTAHostApClosedNetPayloadSize = 4,
    kAirportItlwmAPSTAHostApClosedNetIovarPresent = 1,
    kAirportItlwmAPSTAHostApConfigCoreOffset = 0x218,
    kAirportItlwmAPSTAHostApConfigPrivateOffset = 0x128,
    kAirportItlwmAPSTAHostApConfigAssocRootOffset = 0x1558,
    kAirportItlwmAPSTAHostApConfigAssocObjectOffset = 0x10,
    kAirportItlwmAPSTAHostApConfigMaxAssocOffset = 0xb4,
    kAirportItlwmAPSTASetMaxAssocAssociatedCountOffset = 0x00,
    kAirportItlwmAPSTASetMaxAssocRequestedOffset = 0x04,
    kAirportItlwmAPSTASetMaxAssocLimitOffset = 0x08,
    kAirportItlwmAPSTASetMaxAssocPayloadSize = 4,
    kAirportItlwmAPSTASetMaxAssocIovarPresent = 1,
    kAirportItlwmAPSTAHostApMisMaxStaVtableOffset = 0xb18,
    kAirportItlwmAPSTAHostApMisMaxStaSelector = 0x57,
    kAirportItlwmAPSTAHostApMisMaxStaPayloadOffset = 0x08,
    kAirportItlwmAPSTAHostApMisMaxStaPayloadSize = 4,
    kAirportItlwmAPSTAHostApVendorIEListLengthOffset = 0x2dc,
    kAirportItlwmAPSTAHostApVendorIEListDataOffset = 0x2e0,
    kAirportItlwmAPSTAVendorIEListMinimumRemaining = 6,
    kAirportItlwmAPSTAVendorIEListHeaderSize = 2,
    kAirportItlwmAPSTAVendorIEListElementIdOffset = 0,
    kAirportItlwmAPSTAVendorIEListLengthOffset = 1,
    kAirportItlwmAPSTAVendorIEDataAllocationSize = 0x814,
    kAirportItlwmAPSTAVendorIEDataHeader00Offset = 0x00,
    kAirportItlwmAPSTAVendorIEDataHeader08Offset = 0x08,
    kAirportItlwmAPSTAVendorIEDataPayloadLengthOffset = 0x10,
    kAirportItlwmAPSTAVendorIEDataElementIdOffset = 0x14,
    kAirportItlwmAPSTAVendorIEDataPayloadOffset = 0x15,
    kAirportItlwmAPSTAVendorIEDataPayloadCapacity =
        kAirportItlwmAPSTAVendorIEDataAllocationSize -
        kAirportItlwmAPSTAVendorIEDataPayloadOffset,
    kAirportItlwmAPSTAAppleVendorIENameLength = 7,
    kAirportItlwmAPSTAAppleVendorIECommandOverhead = 8,
    kAirportItlwmAPSTAAppleVendorIEAppleOUILength = 3,
    kAirportItlwmAPSTAAppleVendorIESetBufferSize = 0x52,
    kAirportItlwmAPSTAAppleVendorIESetBufferCommandOffset = 0x00,
    kAirportItlwmAPSTAAppleVendorIESetBufferHeader04Offset = 0x04,
    kAirportItlwmAPSTAAppleVendorIESetBufferBodyOffset = 0x0c,
    kAirportItlwmAPSTAAppleVendorIESetBufferBodyCapacity =
        kAirportItlwmAPSTAAppleVendorIESetBufferSize -
        kAirportItlwmAPSTAAppleVendorIESetBufferBodyOffset,
    kAirportItlwmAPSTAAppleVendorIEDeleteCommandSize = 4,
    kAirportItlwmAPSTAAppleVendorIEAddCommandSize = 4,
    kAirportItlwmAPSTAAppleVendorIEDeleteIECopyOffset = 0x08,
    kAirportItlwmAPSTAAppleVendorIEDeleteBodyTrapThreshold = 0x45,
    kAirportItlwmAPSTAAppleVendorIECapabilityPayloadSize = 0x18,
    kAirportItlwmAPSTAAppleVendorIECapabilityHeaderOffset = 0x0c,
    kAirportItlwmAPSTAAppleVendorIECapabilityFieldsOffset = 0x14,
    kAirportItlwmAPSTAAppleVendorIEFeatureGate46 = 0x46,
    kAirportItlwmAPSTAAppleVendorIEExtBaseOffset = 0x2c,
    kAirportItlwmAPSTAAppleVendorIEExtField2eOffset = 0x2e,
    kAirportItlwmAPSTAAppleVendorIEExtLengthOffset = 0x2f,
    kAirportItlwmAPSTAAppleVendorIEExtPayloadOffset = 0x30,
    kAirportItlwmAPSTAAppleVendorIEExtExtraFlagOffset = 0x50,
    kAirportItlwmAPSTAAppleVendorIEExtTail51Offset = 0x51,
    kAirportItlwmAPSTAAppleVendorIEExtTail59Offset = 0x59,
    kAirportItlwmAPSTAAppleVendorIEExtLengthTrapThreshold = 0x3e,
    kAirportItlwmAPSTAAppleVendorIEExtExtraLengthMax = 0x39,
    kAirportItlwmAPSTAAppleVendorIEExtFinalPayloadTrapThreshold = 0x41,
    kAirportItlwmAPSTAAppleVendorIEExtFixedTag103 = 0x103,
    kAirportItlwmAPSTAAppleVendorIEExtFixedTag104 = 0x104,
    kAirportItlwmAPSTAAppleVendorIEExtTailType = 0x12,
    kAirportItlwmAPSTAAppleVendorIEExtTailLengthContribution = 0x15,
    kAirportItlwmAPSTAEnableAPRrmFeatureGate = 0x15,
    kAirportItlwmAPSTAEnableAPRrmConfigByteOffset = 0xe2,
    kAirportItlwmAPSTAEnableAPRrmThrottleWindowPayloadValue = 0,
    kAirportItlwmAPSTAEnableAPRrmMaxOffChannelPayloadValue = 0,
    kAirportItlwmAPSTAEnableAPRrmPayloadSize = 4,
    kAirportItlwmAPSTAEnableAPWnmFeatureGate = 0x19,
    kAirportItlwmAPSTAEnableAPWnmConfigByteOffset = 0xe3,
    kAirportItlwmAPSTAEnableAPWnmPayloadValue = 0,
    kAirportItlwmAPSTAEnableAPWnmPayloadSize = 4,
    kAirportItlwmAPSTAEnableAPMpduBootArgSize = 4,
    kAirportItlwmAPSTAEnableAPMpduDefaultValue = -1,
    kAirportItlwmAPSTAEnableAPMpduSkipValue = 0,
    kAirportItlwmAPSTAEnableAPCorePrivateSoftApFlagOffset = 0x2890,
    kAirportItlwmAPSTAEnableAPCorePrivateSoftApFlag = 0x10000,
    kAirportItlwmAPSTAEnableAPVtableE70Offset = 0xe70,
    kAirportItlwmAPSTAEnableAPVtableE70Arg0 = 2,
    kAirportItlwmAPSTAEnableAPVtableE70Arg1 = 1,
    kAirportItlwmAPSTAEnableAPInterfaceNameLengthTrapThreshold = 0x11,
    kAirportItlwmAPSTAEnableAPInterfaceNameMaxAcceptedLength =
        kAirportItlwmAPSTAEnableAPInterfaceNameLengthTrapThreshold - 1,
    kAirportItlwmAPSTAEnableAPScbProbePayloadSize = 0x0c,
    kAirportItlwmAPSTAEnableAPScbProbePayloadHeaderOffset = 0x00,
    kAirportItlwmAPSTAEnableAPScbProbePayloadValueOffset = 0x08,
    kAirportItlwmAPSTAEnableAPScbProbeValue = 5,
    kAirportItlwmAPSTACommandCompletionOwnerOffset = 0x00,
    kAirportItlwmAPSTACommandCompletionFunctionOffset = 0x08,
    kAirportItlwmAPSTACommandCompletionCookieOffset = 0x10,
    kAirportItlwmAPSTACommandCompletionSize = 0x18,
    kAirportItlwmAPSTAEnableAPScbProbeCompletionCookie = 0,
    kAirportItlwmAPSTAEnableAPNotifyEventId = 0x1e,
    kAirportItlwmAPSTAEnableAPNotifyFlag = 1,
    kAirportItlwmAPSTAEnableAPFinalVtableOffset = 0xb18,
    kAirportItlwmAPSTAEnableAPFinalSelector = 4,
    kAirportItlwmAPSTAEnableAPFinalArg0 = 0,
    kAirportItlwmAPSTAEnableAPFinalArg1 = 0,
    kAirportItlwmAPSTAEnableAPFinalArg2 = 0,
    kAirportItlwmAPSTAEnableAPEventBit = 5,
    kAirportItlwmAPSTAHiddenAPRequiredStateOffset = 0x26c,
    kAirportItlwmAPSTAHiddenAPRequiredStateValue = 1,
    kAirportItlwmAPSTAHiddenNotUpReturn = 6,
    kAirportItlwmAPSTAHiddenInputValueOffset = 0x04,
    kAirportItlwmAPSTAHiddenInputCarrierSize = 0x08,
    kAirportItlwmAPSTAHiddenMaxAcceptedValue = 1,
    kAirportItlwmAPSTAHiddenInvalidArgumentReturn = 0x16,
    kAirportItlwmAPSTAHiddenClosedNetPayloadSize = 4,
    kAirportItlwmAPSTAHiddenClosedNetStateOffset = 0x0d,
    kAirportItlwmAPSTAHiddenClearPowerSaveState = 0,
    kAirportItlwmAPSTAHiddenClearPowerSaveReason = 9,
    kAirportItlwmAPSTAHiddenRuntime0eClearOffset = 0x0e,
    kAirportItlwmAPSTAHoldPowerAssertionStateOffset = 0x0c,
    kAirportItlwmAPSTAHoldPowerAssertionStateValue = 1,
    kAirportItlwmAPSTAHoldPowerAssertionPayloadValue = 1,
    kAirportItlwmAPSTAHoldPowerAssertionPayloadSize = 4,
    kAirportItlwmAPSTAHoldPowerAssertionCoreResourceOffset = 0x2c20,
    kAirportItlwmAPSTAHoldPowerAssertionEventId = 0x8d,
    kAirportItlwmAPSTAHoldPowerAssertionNotifyFlag = 1,
    kAirportItlwmAPSTAHostApModeNetworkDataFlagsOffset = 0x04,
    kAirportItlwmAPSTAHostApModeNetworkDataSsidLengthOffset = 0x1c,
    kAirportItlwmAPSTAHostApModeNetworkDataSsidBytesOffset = 0x20,
    kAirportItlwmAPSTAHostApModeNetworkDataVendorIELengthOffset = 0x2dc,
    kAirportItlwmAPSTAHostApModeNetworkDataVendorIEDataOffset = 0x2e0,
    kAirportItlwmAPSTAHostApModeSsidLengthTrapThreshold = 0x21,
    kAirportItlwmAPSTAHostApModeSsidLengthMaxAccepted =
        kAirportItlwmAPSTAHostApModeSsidLengthTrapThreshold - 1,
    kAirportItlwmAPSTAHostApModeVendorIELengthTrapThreshold = 0x101,
    kAirportItlwmAPSTAHostApModeVendorIELengthMaxAccepted =
        kAirportItlwmAPSTAHostApModeVendorIELengthTrapThreshold - 1,
    kAirportItlwmAPSTASetHostApModeNotUpReturn =
        kAirportItlwmAPSTASoftAPNotReadyReturn,
    kAirportItlwmAPSTASetHostApModeInvalidArgumentReturn =
        kAirportItlwmAPSTARawInvalidArgumentReturn,
    kAirportItlwmAPSTAHostApModeFeatureGate46 = 0x46,
    kAirportItlwmAPSTAHostApModeNanOwnerOffset = 0x74f0,
    kAirportItlwmAPSTAHostApModeNanDataOwnerOffset = 0x74f8,
    kAirportItlwmAPSTAHostApModeBringupPrivateFlagOffset = 0x2890,
    kAirportItlwmAPSTAHostApModeBringupPrivateFlagMask = 0x01,
    kAirportItlwmAPSTAHostApModeBringupModeOffset = 0x4d8c,
    kAirportItlwmAPSTAHostApModeBringupMode4 = 4,
    kAirportItlwmAPSTAHostApModeBringupMode1 = 1,
    kAirportItlwmAPSTAHostApModeProximityBringdownPresent = 1,
    kAirportItlwmAPSTAHostApModeNanBringdownPresent = 1,
    kAirportItlwmAPSTAHostApPowerOffInactiveReturn = 0,
    kAirportItlwmAPSTAHostApPowerOffSetPowerSaveState = 0,
    kAirportItlwmAPSTAHostApPowerOffPowerSaveReason = 0x0c,
    kAirportItlwmAPSTAHostApPowerOffConcurrencyFallbackState = 3,
    kAirportItlwmAPSTAHostApPowerOffConcurrencyFallbackReason = 3,
    kAirportItlwmAPSTAHostApPowerOffNotifyEventId = 1,
    kAirportItlwmAPSTAHostApPowerOffNotifyPayloadSize = 0,
    kAirportItlwmAPSTAHostApPowerOffNotifyFlag = 1,
    kAirportItlwmAPSTAConcurrencyFeatureGate46 = 0x46,
    kAirportItlwmAPSTAConcurrencyCorePrivateByteOffset = 0x4d59,
    kAirportItlwmAPSTAConcurrencyCorePrivateMask = 0x1b,
    kAirportItlwmAPSTALowPowerExitStateOffset = 0xb4,
    kAirportItlwmAPSTALowPowerExitWorkQueueActionVtableOffset = 0x130,
    kAirportItlwmAPSTALowPowerExitPayloadSize = 4,
    kAirportItlwmAPSTALowPowerExitSuccessState = 0,
    kAirportItlwmAPSTALowPowerExitLogLineConfigure = 0x0dae,
    kAirportItlwmAPSTALowPowerExitLogLineUnconfigure = 0x0f99,
    kAirportItlwmAPSTAConfigureMpduCorePrivateModeOffset = 0x3fc,
    kAirportItlwmAPSTAConfigureMpduCorePrivateModeRequired = 2,
    kAirportItlwmAPSTAConfigureMpduCorePrivateCountOffset = 0x30c,
    kAirportItlwmAPSTAConfigureMpduCorePrivateMaxAccepted = 4,
    kAirportItlwmAPSTAConfigureMpduPayloadSize = 4,
    kAirportItlwmAPSTAConfigureMpduVerboseMask = 0x80,
    kAirportItlwmAPSTALowPowerModeExitPayloadValue = 0,
    kAirportItlwmAPSTALowPowerModeEnterPayloadValue = 1,
    kAirportItlwmAPSTALowPowerModeCompletionCookie = 0,
    kAirportItlwmAPSTAArpHostIpStateOffset = 0xac,
    kAirportItlwmAPSTAArpHostIpPayloadSize = 4,
    kAirportItlwmAPSTAArpHostIpCompletionCookie = 0,
    kAirportItlwmAPSTAIovarNoBusPreference = 0,
    kAirportItlwmAPSTARpsNoaDutyCyclePayloadSize = 0x10,
    kAirportItlwmAPSTARpsNoaDutyCycleZeroOffset = 0x08,
    kAirportItlwmAPSTARpsNoaDutyCycleModeOffset = 0x0c,
    kAirportItlwmAPSTARpsNoaDutyCycleEnableOffset = 0x0e,
    kAirportItlwmAPSTARpsNoaDutyCycleModeValue = 2,
    kAirportItlwmAPSTARpsNoaDynamicPayloadSize = 0x18,
    kAirportItlwmAPSTARpsNoaDynamicZeroOffset = 0x08,
    kAirportItlwmAPSTARpsNoaDynamicModeOffset = 0x0c,
    kAirportItlwmAPSTARpsNoaDynamicLevelByteOffset = 0x0e,
    kAirportItlwmAPSTARpsNoaDynamicReservedByteOffset = 0x0f,
    kAirportItlwmAPSTARpsNoaDynamicRotatedParamsOffset = 0x10,
    kAirportItlwmAPSTARpsNoaDynamicModeValue = 2,
    kAirportItlwmAPSTARpsNoaDynamicLevelBase = 0x0a,
    kAirportItlwmAPSTARpsNoaDynamicParamsEntrySize = 0x0c,
    kAirportItlwmAPSTARpsNoaDynamicParamsLevelByteOffset = 0x08,
    kAirportItlwmAPSTARpsNoaDynamicRotateBits = 0x20,
    kAirportItlwmAPSTAReleasePowerAssertionStateOffset = 0x0c,
    kAirportItlwmAPSTAReleasePowerAssertionStateValue = 0,
    kAirportItlwmAPSTAReleasePowerAssertionPayloadValue = 0,
    kAirportItlwmAPSTAReleasePowerAssertionPayloadSize = 4,
    kAirportItlwmAPSTAReleasePowerAssertionCoreResourceOffset = 0x2c20,
    kAirportItlwmAPSTAReleasePowerAssertionEventId = 0x8d,
    kAirportItlwmAPSTAReleasePowerAssertionNotifyFlag = 1,
    kAirportItlwmAPSTAPowerStatsLastTimestampOffset = 0x1a8,
    kAirportItlwmAPSTAPowerStatsBucketsOffset = 0x1d0,
    kAirportItlwmAPSTAPowerStatsBucketStride = 0x10,
    kAirportItlwmAPSTAPowerStatsBucketDurationOffset = 0x00,
    kAirportItlwmAPSTAPowerStateReasonMaxKnown = 0x0e,
    kAirportItlwmAPSTAEnableRunningVtableOffset = 0x0d58,
    kAirportItlwmAPSTAEnableSuperVtableOffset = 0x0860,
    kAirportItlwmAPSTAEnableNotRunningReturn = 0xe00002d5,
    kAirportItlwmAPSTADisablePreVtableOffset = 0x0da0,
    kAirportItlwmAPSTADisableSuperVtableOffset = 0x0868,
    kAirportItlwmAPSTADatapathIsEnabledVtableOffset = 0x0cf0,
    kAirportItlwmAPSTADatapathNotEnabledReturn = 0xe00002bc,
    kAirportItlwmAPSTAGetLoggerStateOffset = 0x210,
    kAirportItlwmAPSTAGetNumTxQueuesStateOffset = 0x2a4,
    kAirportItlwmAPSTAGetRxCompQueueStateOffset = 0x2f0,
    kAirportItlwmAPSTAGetTxCompQueueStateOffset = 0x2e8,
    kAirportItlwmAPSTAGetTxSubQueueMapOffset = 0x2b8,
    kAirportItlwmAPSTAGetTxSubQueueBaseOffset = 0x300,
    kAirportItlwmAPSTAGetMultiCastQueueStateOffset = 0x320,
    kAirportItlwmAPSTAGetTxPacketPoolStateOffset = 0x2d8,
    kAirportItlwmAPSTAGetRxPacketPoolStateOffset = 0x2e0,
    kAirportItlwmAPSTASetMacAddressRequiredDownStateOffset = 0x26c,
    kAirportItlwmAPSTASetMacAddressInvalidInterface = -1,
    kAirportItlwmAPSTASetMacAddressPayloadSize = 6,
    kAirportItlwmAPSTASetMacAddressRejectedReturn = 0xe00002bc,
    kAirportItlwmAPSTASoftAPPeerStatsFeatureGate = 0x7a,
    kAirportItlwmAPSTASoftAPPeerStatsPayloadSize = 0x0e,
    kAirportItlwmAPSTASoftAPPeerStatsModeOffset = 0x0a,
    kAirportItlwmAPSTASoftAPPeerStatsEnabledModeValue = 1,
    kAirportItlwmAPSTASoftAPPeerStatsDisabledModeValue = 2,
    kAirportItlwmAPSTASoftAPPeerStatsCookieSize = 1,
    kAirportItlwmAPSTASoftAPPeerStatsStateOffset = 0x328,
    kAirportItlwmAPSTASoftAPPeerStatsAllocFailureReturn = 0xe00002bc,
    kAirportItlwmAPSTAAssocListOutputSize = 0x808,
    kAirportItlwmAPSTAAssocListOutputVersion = 1,
    kAirportItlwmAPSTAAssocListOutputCountOffset = 0x04,
    kAirportItlwmAPSTAAssocListOutputEntriesOffset = 0x08,
    kAirportItlwmAPSTAAssocListOutputEntryStride = 0x10,
    kAirportItlwmAPSTAAssocListMaxCount = 0x80,
    kAirportItlwmAPSTAAssocListClampThreshold =
        kAirportItlwmAPSTAAssocListMaxCount + 1,
    kAirportItlwmAPSTAAssocListEntryValidOffset = 0x00,
    kAirportItlwmAPSTAAssocListEntryMacDwordOffset = 0x04,
    kAirportItlwmAPSTAAssocListEntryMacTailOffset = 0x08,
    kAirportItlwmAPSTAAssocListEntryReservedOffset = 0x0a,
    kAirportItlwmAPSTAAssocListEntryValidValue = 1,
    kAirportItlwmAPSTABcmAssocListCountOffset = 0x00,
    kAirportItlwmAPSTABcmAssocListFirstMacOffset = 0x04,
    kAirportItlwmAPSTABcmAssocListMacStride = 0x06,
    kAirportItlwmAPSTAStatsUpdateAllocSize =
        kAirportItlwmAPSTAAssocListOutputSize,
    kAirportItlwmAPSTAStatsUpdateStationListVtableOffset = 0x0fd8,
    kAirportItlwmAPSTAStatsUpdateAsyncSubmitFailure = 0xe00002d8,
    kAirportItlwmAPSTAStatsUpdateActivityBaselineOffset = 0x88,
    kAirportItlwmAPSTAStatsUpdatePollInterval = 0x1388,
    kAirportItlwmAPSTAStatsUpdateVerboseMask = 0x80000,
    kAirportItlwmAPSTAStatsInactivityKillThreshold = 0x16e360,
    kAirportItlwmAPSTAStatsInactivityKillCompareThreshold =
        kAirportItlwmAPSTAStatsInactivityKillThreshold + 1,
    kAirportItlwmAPSTAStatsInactivityResetThreshold = 0x170a71,
    kAirportItlwmAPSTAStatsInactivityMessageSentinel = -1,
    kAirportItlwmAPSTAStatsTimerScheduleVtableOffset = 0x1d0,
    kAirportItlwmAPSTAMonitorLphsVendorIeFlagOffset = 0x62,
    kAirportItlwmAPSTAMonitorLowTrafficCounterOffset = 0x64,
    kAirportItlwmAPSTAMonitorTimerScheduleVtableOffset = 0x1d0,
    kAirportItlwmAPSTAMonitorTimerInterval = 0x3e8,
    kAirportItlwmAPSTAMonitorCorePrivateByteOffset = 0x4d59,
    kAirportItlwmAPSTAMonitorCorePrivateMirrorOffset = 0x208,
    kAirportItlwmAPSTAMonitorInfraBit = 0x01,
    kAirportItlwmAPSTAMonitorAwdlBit = 0x02,
    kAirportItlwmAPSTAMonitorSoftApBit = 0x04,
    kAirportItlwmAPSTAMonitorNanBit = 0x08,
    kAirportItlwmAPSTAMonitorIrBit = 0x10,
    kAirportItlwmAPSTAMonitorCounterFeatureMask =
        kAirportItlwmAPSTAMonitorNanBit | kAirportItlwmAPSTAMonitorIrBit,
    kAirportItlwmAPSTAMonitorCounterEnableOffset = 0x757e,
    kAirportItlwmAPSTAMonitorCounterEnableBit = 0x01,
    kAirportItlwmAPSTAMonitorCounterBaseOffset = 0x2a78,
    kAirportItlwmAPSTAMonitorCounterCount = 4,
    kAirportItlwmAPSTAMonitorCounterStride = 8,
    kAirportItlwmAPSTAMonitorRxCounterVtableOffset = 0x0c38,
    kAirportItlwmAPSTAMonitorFirmwareRxAccumulatorOffset = 0xa0,
    kAirportItlwmAPSTAMonitorFirmwareRxBaselineOffset = 0x90,
    kAirportItlwmAPSTAMonitorInterfaceRxBaselineOffset = 0x98,
    kAirportItlwmAPSTAMonitorSoftAPTxDeltaStatsOffset = 0x1b8,
    kAirportItlwmAPSTAMonitorSoftAPRxDeltaStatsOffset = 0x1c0,
    kAirportItlwmAPSTAPowerStateCurrentOffset = 0x10,
    kAirportItlwmAPSTAPowerStateOff = 0,
    kAirportItlwmAPSTAPowerStateOn = 1,
    kAirportItlwmAPSTAPowerStateBeaconWait = 2,
    kAirportItlwmAPSTAPowerStateLowPower = 3,
    kAirportItlwmAPSTAPowerStateMaxKnown =
        kAirportItlwmAPSTAPowerStateLowPower,
    kAirportItlwmAPSTAPowerStateReasonMonitor = 4,
    kAirportItlwmAPSTAPowerStateReasonInfraScan = 7,
    kAirportItlwmAPSTAPowerStateReasonReset = 0x0a,
    kAirportItlwmAPSTAPowerStateReasonAllStaLpm = 0x0b,
    kAirportItlwmAPSTAPowerStateReasonPowerOff = 0x0c,
    kAirportItlwmAPSTAPowerStateTransitionRecordBaseOffset = 0x1c8,
    kAirportItlwmAPSTAPowerStateTransitionRecordStride = 0x10,
    kAirportItlwmAPSTAPowerStateTransitionCountOffset = 0x00,
    kAirportItlwmAPSTAPowerStateTransitionDurationOffset = 0x08,
    kAirportItlwmAPSTAPowerStateBeaconWaitPayloadSize = 4,
    kAirportItlwmAPSTAPowerStateBeaconWaitPayloadValue = 0x0a,
    kAirportItlwmAPSTAPowerStateBeaconWaitLphsPayloadValue = 1,
    kAirportItlwmAPSTAMfpFeatureGate = 0x26,
    kAirportItlwmAPSTAMfpPayloadSize = 4,
    kAirportItlwmAPSTAMfpUnsupportedReturn = 0,
    kAirportItlwmAPSTAPrintDataPathBufferOffset = 0x18,
    kAirportItlwmAPSTAPrintDataPathRemainingOffset = 0x20,
    kAirportItlwmAPSTAPrintDataPathPrintedOffset = 0x24,
    kAirportItlwmAPSTAPrintDataPathStartOffset = 0x28,
    kAirportItlwmAPSTAPrintDataPathTxSubQueueVtableOffset = 0x338,
    kAirportItlwmAPSTAPrintDataPathTxCompletionVtableOffset = 0x320,
    kAirportItlwmAPSTAPrintDataPathRxCompletionVtableOffset = 0x328,
    kAirportItlwmAPSTAPrintDataPathSuperVtableOffset = 0x0c68,
    kAirportItlwmAPSTAUpdateRxCounterStateOffset = 0xa0,
    kAirportItlwmAPSTAGetSsidStateLengthOffset = 0x274,
    kAirportItlwmAPSTAGetSsidStateBytesOffset = 0x278,
    kAirportItlwmAPSTAGetSsidMaxLength = 0x20,
    kAirportItlwmAPSTAGetSsidOutputLengthOffset = 0x04,
    kAirportItlwmAPSTAGetSsidOutputBytesOffset = 0x08,
    kAirportItlwmAPSTAGetSsidInvalidArgumentReturn = 0x16,
    kAirportItlwmAPSTAGetStateOutputValueOffset = 0x04,
    kAirportItlwmAPSTAGetStateOutputValue = 4,
    kAirportItlwmAPSTAGetOpModeInvalidArgumentReturn = 0x16,
    kAirportItlwmAPSTAGetOpModeOutputTypeOffset = 0x00,
    kAirportItlwmAPSTAGetOpModeOutputModeOffset = 0x04,
    kAirportItlwmAPSTAGetOpModeTypeValue = 1,
    kAirportItlwmAPSTAGetOpModeAPUpValue = 8,
    kAirportItlwmAPSTAGetOpModeAPDownValue = 0,
    kAirportItlwmAPSTAGetOpModeStateAPUpOffset = 0x26c,
    kAirportItlwmAPSTAGetPeerCacheMaximumSizeOutputOffset = 0x04,
    kAirportItlwmAPSTAGetPeerCacheMaximumSizeValue = 8,
    kAirportItlwmAPSTAGetHostApModeHiddenOutputOffset = 0x00,
    kAirportItlwmAPSTAGetHostApModeHiddenValue = 1,
    kAirportItlwmAPSTAGetHostApModeHiddenInvalidArgumentReturn = 0x16,
    kAirportItlwmAPSTAGetSoftAPParamsHasNullGuard = 0,
    kAirportItlwmAPSTAGetSoftAPStatsStateOffset = 0x1b0,
    kAirportItlwmAPSTAGetSoftAPStatsCopySize = 0x58,
    kAirportItlwmAPSTAGetSoftAPStatsHasNullGuard = 0,
    kAirportItlwmAPSTASetSsidSuccessReturn = 0,
    kAirportItlwmAPSTASetSsidStateMutationCount = 0,
    kAirportItlwmAPSTASetPeerCacheControlCoreOffset = 0x218,
    kAirportItlwmAPSTASetPeerCacheControlCommandOffset = 0x04,
    kAirportItlwmAPSTASetPeerCacheControlValue08Offset = 0x08,
    kAirportItlwmAPSTASetPeerCacheControlValue0cOffset = 0x0c,
    kAirportItlwmAPSTASetPeerCacheControlValue0eOffset = 0x0e,
    kAirportItlwmAPSTASetPeerCacheControlPayloadSize = 0x1d0,
    kAirportItlwmAPSTASetPeerCacheControlEventId = 0x33,
    kAirportItlwmAPSTASetPeerCacheControlNotifyFlag = 1,
    kAirportItlwmAPSTASetPeerCacheControlLocalEventPostCount = 0,
    kAirportItlwmAPSTASetPeerCacheControlReturn = 0,
    kAirportItlwmAPSTASetSoftAPParamsInputParam04Offset = 0x04,
    kAirportItlwmAPSTASetSoftAPParamsInputParam08Offset = 0x08,
    kAirportItlwmAPSTASetSoftAPParamsInputParam0cOffset = 0x0c,
    kAirportItlwmAPSTASetSoftAPParamsInputParam10Offset = 0x10,
    kAirportItlwmAPSTASetSoftAPParamsInputBeaconIntervalOffset = 0x14,
    kAirportItlwmAPSTASetSoftAPParamsInputEnabledOffset = 0x17,
    kAirportItlwmAPSTASetSoftAPParamsInputParam18Offset = 0x18,
    kAirportItlwmAPSTASetSoftAPParamsStateParam18Offset = 0x18,
    kAirportItlwmAPSTASetSoftAPParamsStateParam1cOffset = 0x1c,
    kAirportItlwmAPSTASetSoftAPParamsStateParam20Offset = 0x20,
    kAirportItlwmAPSTASetSoftAPParamsStateParam24Offset = 0x24,
    kAirportItlwmAPSTASetSoftAPParamsStateParam28Offset = 0x28,
    kAirportItlwmAPSTASetSoftAPParamsStateParam28Size = 4,
    kAirportItlwmAPSTASetSoftAPParamsStateEnabledFlagOffset = 0x0e,
    kAirportItlwmAPSTASetSoftAPParamsStateAPUpOffset = 0x26c,
    kAirportItlwmAPSTASetSoftAPParamsStateAppliedBeaconIntervalOffset = 0x68,
    kAirportItlwmAPSTASetSoftAPParamsBeaconSentinel = 0xffff,
    kAirportItlwmAPSTASetSoftAPParamsClearPowerState = 0,
    kAirportItlwmAPSTASetSoftAPParamsClearPowerReason = 0,
    kAirportItlwmAPSTASetSoftAPParamsHoldPowerState = 1,
    kAirportItlwmAPSTASetSoftAPParamsHoldPowerReason = 0,
    kAirportItlwmAPSTASetSoftAPParamsHasNullGuard = 0,
    kAirportItlwmAPSTASetSoftAPParamsReturn = 0,
    kAirportItlwmAPSTASetSoftAPExtCapsInputFlagOffset = 0x00,
    kAirportItlwmAPSTASetSoftAPExtCapsInputTail51Offset = 0x01,
    kAirportItlwmAPSTASetSoftAPExtCapsInputTail59Offset = 0x09,
    kAirportItlwmAPSTASetSoftAPExtCapsStateClear50Offset = 0x50,
    kAirportItlwmAPSTASetSoftAPExtCapsStateClear58Offset = 0x58,
    kAirportItlwmAPSTASetSoftAPExtCapsStateClear60Offset = 0x60,
    kAirportItlwmAPSTASetSoftAPExtCapsStateClear61Offset = 0x61,
    kAirportItlwmAPSTASetSoftAPExtCapsStateFlag50Offset = 0x50,
    kAirportItlwmAPSTASetSoftAPExtCapsStateTail51Offset = 0x51,
    kAirportItlwmAPSTASetSoftAPExtCapsStateTail59Offset = 0x59,
    kAirportItlwmAPSTASetSoftAPExtCapsReturn = 0,
    kAirportItlwmAPSTASetSoftAPExtCapsHasNullGuard = 0,
    kAirportItlwmAPSTASetMisMaxStaRequiredStateOffset = 0x26c,
    kAirportItlwmAPSTASetMisMaxStaInputValueOffset = 0x00,
    kAirportItlwmAPSTASetMisMaxStaReturn = 0,
    kAirportItlwmAPSTASetMisMaxStaHasNullGuardAfterAPUp = 0,
    kAirportItlwmAPSTAGetStationListRequiredStateOffset = 0x26c,
    kAirportItlwmAPSTAGetStationListNullReturn = 0x16,
    kAirportItlwmAPSTAGetStationListNullBeforeAPDown = 1,
    kAirportItlwmAPSTAGetStationListNotUpReturn = 0x39,
    kAirportItlwmAPSTAGetStationListAllocFailureReturn = 0xe00002bd,
    kAirportItlwmAPSTAGetStationListAsyncSubmitFailureReturn = 0xe00002d8,
    kAirportItlwmAPSTAGetStationListMacListSize = 0x100,
    kAirportItlwmAPSTAGetStationListMacListInitialValue = 0x2a,
    kAirportItlwmAPSTAGetStationListVirtualIoctlSelector = 0x9f,
    kAirportItlwmAPSTAGetStationListTxPayloadSize = 0x100,
    kAirportItlwmAPSTAGetStationListAsyncExpectedRange = 0x1000100,
    kAirportItlwmAPSTAStationTableFirstEntryOffset = 0xb9,
    kAirportItlwmAPSTAStationTableEntryStride = 0x30,
    kAirportItlwmAPSTAStationTableEndOffset = 0x1a9,
    kAirportItlwmAPSTAStationTableMacSize = 0x06,
    kAirportItlwmAPSTAStationTableEntryCount = 5,
    kAirportItlwmAPSTAStationTableBaseOffset = 0xb8,
    kAirportItlwmAPSTAStationTableActiveOffset = 0x00,
    kAirportItlwmAPSTAStationTableMacOffsetInEntry = 0x01,
    kAirportItlwmAPSTAStationTableSleepStateOffset = 0x10,
    kAirportItlwmAPSTAStationTableAihsFlagOffset = 0x20,
    kAirportItlwmAPSTAStationTableSharingFlagOffset = 0x24,
    kAirportItlwmAPSTAStationTableAppleStationFlagOffset = 0x28,
    kAirportItlwmAPSTAStationTableDefaultSleepState = 2,
    kAirportItlwmAPSTAStationTableLowPowerSleepState = 1,
    kAirportItlwmAPSTAStationTableAwakeSleepState = 2,
    kAirportItlwmAPSTAGetStaIEListNullReturn = 0x16,
    kAirportItlwmAPSTAGetStaIEListNullBeforeStationSearch = 1,
    kAirportItlwmAPSTAGetStaIEListNotFoundReturn = 2,
    kAirportItlwmAPSTAGetStaIEListInputMacOffset = 0x04,
    kAirportItlwmAPSTAGetStaIEListInputLengthOffset = 0x0c,
    kAirportItlwmAPSTAGetStaIEListOutputMacOffset = 0x10,
    kAirportItlwmAPSTAGetStaIEListOutputMacTailOffset = 0x14,
    kAirportItlwmAPSTAGetStaIEListOutputSourceOffset =
        kAirportItlwmAPSTAStationTableFirstEntryOffset,
    kAirportItlwmAPSTAGetStaIEListOutputSkipsActiveFlag = 1,
    kAirportItlwmAPSTAGetStaIEListReturnedLengthSourceOffset = 0x11,
    kAirportItlwmAPSTAGetStaIEListReturnedLengthBias = 2,
    kAirportItlwmAPSTAGetStaIEListWpaIeNameLength = 5,
    kAirportItlwmAPSTAGetStaStatsRequiredStateOffset = 0x26c,
    kAirportItlwmAPSTAGetStaStatsNotUpReturn = 0x39,
    kAirportItlwmAPSTAGetStaStatsNullReturn = 0x16,
    kAirportItlwmAPSTAGetStaStatsAllocFailureReturn = 0x0c,
    kAirportItlwmAPSTAGetStaStatsCorePrivateCountOffset = 0x30c,
    kAirportItlwmAPSTAGetStaStatsLowThreshold = 0x07,
    kAirportItlwmAPSTAGetStaStatsHighThreshold = 0x0f,
    kAirportItlwmAPSTAGetStaStatsLowAllocSize = 0x108,
    kAirportItlwmAPSTAGetStaStatsBaseAllocSize = 0x118,
    kAirportItlwmAPSTAGetStaStatsHighAllocExtra = 0x10,
    kAirportItlwmAPSTAGetStaStatsInputMacOffset = 0x04,
    kAirportItlwmAPSTAGetStaStatsInputMacSize = 0x06,
    kAirportItlwmAPSTAGetStaStatsOutputValidOffset = 0x00,
    kAirportItlwmAPSTAGetStaStatsOutputValidValue = 1,
    kAirportItlwmAPSTAGetStaStatsOutputField0cOffset = 0x0c,
    kAirportItlwmAPSTAGetStaStatsOutputField10Offset = 0x10,
    kAirportItlwmAPSTAGetStaStatsOutputField14Offset = 0x14,
    kAirportItlwmAPSTAGetStaStatsOutputField18Offset = 0x18,
    kAirportItlwmAPSTAGetStaStatsRxField54Offset = 0x54,
    kAirportItlwmAPSTAGetStaStatsRxField58Offset = 0x58,
    kAirportItlwmAPSTAGetStaStatsRxField60Offset = 0x60,
    kAirportItlwmAPSTAGetStaStatsRxField68Offset = 0x68,
    kAirportItlwmAPSTAGetKeyRscInputKeyIndexOffset = 0x0e,
    kAirportItlwmAPSTAGetKeyRscTxPayloadSize = 0x08,
    kAirportItlwmAPSTAGetKeyRscRxPayloadSize = 0x08,
    kAirportItlwmAPSTAGetKeyRscVirtualIoctlSelector = 0xb7,
    kAirportItlwmAPSTAGetKeyRscOutputLengthOffset = 0x50,
    kAirportItlwmAPSTAGetKeyRscOutputValueOffset = 0x54,
    kAirportItlwmAPSTAGetKeyRscOutputLengthValue = 0x08,
    kAirportItlwmAPSTASetCipherKeyRequiredStateOffset = 0x26c,
    kAirportItlwmAPSTASetCipherKeyNotUpReturn = 6,
    kAirportItlwmAPSTASetCipherKeyInputCipherTypeOffset = 0x08,
    kAirportItlwmAPSTASetCipherKeyCipherNone = 0,
    kAirportItlwmAPSTASetCipherKeyCipherAccepted3 = 3,
    kAirportItlwmAPSTASetCipherKeyCipherAccepted5 = 5,
    kAirportItlwmAPSTASetCipherKeyUnsupportedCipherReturn = 0,
    kAirportItlwmAPSTASetCipherKeyResource210Offset = 0x210,
    kAirportItlwmAPSTASetCipherKeyDumpFlag = 0x800,
    kAirportItlwmAPSTASetCipherKeyWsecKeySize = 0xa4,
    kAirportItlwmAPSTASetCipherKeyVirtualIoctlSelector = 0x2d,
    kAirportItlwmAPSTASetCipherKeyTxPayloadSize = 0xa4,
    kAirportItlwmAPSTAEventTypeOffset = 0x04,
    kAirportItlwmAPSTAEventStatusOffset = 0x08,
    kAirportItlwmAPSTAEventReasonOffset = 0x0c,
    kAirportItlwmAPSTAEventAuthTypeOffset = 0x10,
    kAirportItlwmAPSTAEventDataLengthOffset = 0x14,
    kAirportItlwmAPSTAEventAddressOffset = 0x18,
    kAirportItlwmAPSTAEventAddressSize = 0x06,
    kAirportItlwmAPSTAEventDataOffset = 0x30,
    kAirportItlwmAPSTAEventStateDwordOffset = 0x80,
    kAirportItlwmAPSTAEventStateWordOffset = 0x84,
    kAirportItlwmAPSTAEventAuthInd = 4,
    kAirportItlwmAPSTAEventDeauth = 5,
    kAirportItlwmAPSTAEventDeauthInd = 6,
    kAirportItlwmAPSTAEventAssocInd = 8,
    kAirportItlwmAPSTAEventReassocInd = 10,
    kAirportItlwmAPSTAEventDisassoc = 11,
    kAirportItlwmAPSTAEventDisassocInd = 12,
    kAirportItlwmAPSTAEventLink = 16,
    kAirportItlwmAPSTAEventIf = 0x36,
    kAirportItlwmAPSTAEventActionFrame = 0x4b,
    kAirportItlwmAPSTAEventPskAuth = 0x96,
    kAirportItlwmAPSTAEventSuccessStatus = 0,
    kAirportItlwmAPSTAEventSuccessReason = 0,
    kAirportItlwmAPSTAEventAssocRequiresSuccessStatusAndReason = 1,
    kAirportItlwmAPSTAEventAssocHiddenRequiresAppleIE = 1,
    kAirportItlwmAPSTAEventAssocFullTableStillPosts = 1,
    kAirportItlwmAPSTAEventAssocMessageAppleFlagUsesCurrentIE = 1,
    kAirportItlwmAPSTAEventRemovalCopiesMacShadow = 1,
    kAirportItlwmAPSTAEventIncomingHalEchoCommandCount = 0,
    kAirportItlwmAPSTAEventActionFrameUsesPayload = 1,
    kAirportItlwmAPSTAEventRsnxeOutputOffset = 0x10,
    kAirportItlwmAPSTAEventAssocFlagsOffset = 0x0c,
    kAirportItlwmAPSTAEventAssocFlagAihs = 0x01,
    kAirportItlwmAPSTAEventAssocFlagSharing = 0x02,
    kAirportItlwmAPSTAEventAssocFlagAppleStation = 0x04,
    kAirportItlwmAPSTAEventAssocMessageId = 0x0c,
    kAirportItlwmAPSTAEventAssocMessageSize = 0x114,
    kAirportItlwmAPSTAEventRemoveMessageId = 0x0d,
    kAirportItlwmAPSTAEventRemoveMessageSize = 0x0c,
    kAirportItlwmAPSTAEventAuthIndMessageId = 0x98,
    kAirportItlwmAPSTAEventAuthIndMessageSize = 0x6c,
    kAirportItlwmAPSTAEventAuthIndRequiredStatus = 0,
    kAirportItlwmAPSTAEventAuthIndRequiredAuthType = 3,
    kAirportItlwmAPSTAEventAuthIndTypeValue = 5,
    kAirportItlwmAPSTAEventAuthIndSuccessStatus = 0,
    kAirportItlwmAPSTAEventAuthIndReasonAppleBase = 0xe0823000,
    kAirportItlwmAPSTAEventAuthIndReasonTrapThreshold = 0x2e,
    kAirportItlwmAPSTAEventAuthIndReasonFallback = 0xe3ff8100,
    kAirportItlwmAPSTAEventAuthIndTypeOffset = 0x00,
    kAirportItlwmAPSTAEventAuthIndStatusOffset = 0x08,
    kAirportItlwmAPSTAEventAuthIndMacDwordOffset = 0x0c,
    kAirportItlwmAPSTAEventAuthIndMacTailOffset = 0x10,
    kAirportItlwmAPSTAEventAuthIndChunkType1OutputOffset = 0x18,
    kAirportItlwmAPSTAEventAuthIndChunkType2OutputOffset = 0x54,
    kAirportItlwmAPSTAEventAuthIndChunkType2Size = 0x10,
    kAirportItlwmAPSTAEventAuthIndDataMinimumLength = 0x04,
    kAirportItlwmAPSTAEventAuthIndDataHeaderTypeOffset = 0x00,
    kAirportItlwmAPSTAEventAuthIndDataHeaderLengthOffset = 0x02,
    kAirportItlwmAPSTAEventAuthIndDataHeaderTypeValue = 1,
    kAirportItlwmAPSTAEventAuthIndDataChunkListOffset = 0x04,
    kAirportItlwmAPSTAEventAuthIndChunkTypeOffset = 0x00,
    kAirportItlwmAPSTAEventAuthIndChunkLengthOffset = 0x02,
    kAirportItlwmAPSTAEventAuthIndChunkDataOffset = 0x04,
    kAirportItlwmAPSTAEventAuthIndChunkHeaderSize = 0x04,
    kAirportItlwmAPSTAEventAuthIndChunkType1 = 1,
    kAirportItlwmAPSTAEventAuthIndChunkType2 = 2,
    kAirportItlwmAPSTAEventAuthIndChunkType1MinSize = 0x20,
    kAirportItlwmAPSTAEventAuthIndChunkType1MaxSize = 0x40,
    kAirportItlwmAPSTAEventAuthIndChunkAlignment = 0x04,
    kAirportItlwmAPSTAEventAuthIndChunkAlignedLengthMax = 0xfffa,
    kAirportItlwmAPSTAEventPostDispatchVtableOffset = 0xb18,
    kAirportItlwmAPSTAEventPostNotifyOwnerOffset = 0x2c20,
    kAirportItlwmAPSTAEventPostNotifyFlag = 1,
    kAirportItlwmAPSTAEventQueueStaUpdateVtableOffset = 0x358,
    kAirportItlwmAPSTAEventRemovalQueueNotifyEntryOffset = 0x01,
    kAirportItlwmAPSTAEventRemoveInvalidIndexReturn = 0xe00002bc,
    kAirportItlwmAPSTAEventRemoveClearQwordCount = 6,
    kAirportItlwmAPSTAAppleIEMinScanRemaining = 0x06,
    kAirportItlwmAPSTAAppleIEVendorElementId = 0xdd,
    kAirportItlwmAPSTAAppleIELengthOffset = 0x01,
    kAirportItlwmAPSTAAppleIEOuiOffset = 0x02,
    kAirportItlwmAPSTAAppleIEOuiSize = 0x03,
    kAirportItlwmAPSTAAppleIEInstantHotspotSubtypeOffset = 0x05,
    kAirportItlwmAPSTAAppleIEInstantHotspotSubtype = 0x0b,
    kAirportItlwmAPSTAAppleIEInstantHotspotFlagsOffset = 0x09,
    kAirportItlwmAPSTAAppleIEAihsFlagBit = 0,
    kAirportItlwmAPSTAAppleIESharingFlagBit = 1,
    kAirportItlwmAPSTARSNXEElementId = 0xf4,
    kAirportItlwmAPSTARSNXEMinScanRemaining = 0x02,
    kAirportItlwmAPSTAStationListFirmwareCountOffset = 0x04,
    kAirportItlwmAPSTAStationListFirmwareMacListOffset = 0x0c,
    kAirportItlwmAPSTAStationListFirmwareEntryStride = 0x10,
    kAirportItlwmAPSTAActionFrameMinimumLength = 0x12,
    kAirportItlwmAPSTAActionFrameVersionOffset = 0x00,
    kAirportItlwmAPSTAActionFrameVersion1 = 0x0100,
    kAirportItlwmAPSTAActionFrameVersion2 = 0x0200,
    kAirportItlwmAPSTAActionFrameVersionSwapRejectThreshold = 3,
    kAirportItlwmAPSTAActionFrameUnknownCategoryAction = 0xaa,
    kAirportItlwmAPSTAActionFrameVersion1CategoryOffset = 0x10,
    kAirportItlwmAPSTAActionFrameVersion1ActionOffset = 0x11,
    kAirportItlwmAPSTAActionFrameVersion2MinimumLength = 0x1a,
    kAirportItlwmAPSTAActionFrameVersion2CategoryOffset = 0x18,
    kAirportItlwmAPSTAActionFrameVersion2ActionOffset = 0x19,
    kAirportItlwmAPSTAActionFrameVersion1EventCategoryOffset =
        kAirportItlwmAPSTAEventDataOffset +
        kAirportItlwmAPSTAActionFrameVersion1CategoryOffset,
    kAirportItlwmAPSTAActionFrameVersion1EventActionOffset =
        kAirportItlwmAPSTAEventDataOffset +
        kAirportItlwmAPSTAActionFrameVersion1ActionOffset,
    kAirportItlwmAPSTAActionFrameVersion2EventCategoryOffset =
        kAirportItlwmAPSTAEventDataOffset +
        kAirportItlwmAPSTAActionFrameVersion2CategoryOffset,
    kAirportItlwmAPSTAActionFrameVersion2EventActionOffset =
        kAirportItlwmAPSTAEventDataOffset +
        kAirportItlwmAPSTAActionFrameVersion2ActionOffset,
    kAirportItlwmAPSTAActionFrameLphsCategory = 0x7f,
    kAirportItlwmAPSTAActionFrameLphsActionSleep = 1,
    kAirportItlwmAPSTAActionFrameLphsActionAwake = 2,
    kAirportItlwmAPSTAActionFrameStationSleepStateOffset =
        kAirportItlwmAPSTAStationTableSleepStateOffset,
    kAirportItlwmAPSTACheckAllStaBlockingSleepState =
        kAirportItlwmAPSTAStationTableAwakeSleepState,
    kAirportItlwmAPSTAActionFrameAllStaPowerSaveState = 3,
    kAirportItlwmAPSTAActionFrameAllStaPowerSaveReason = 0x0b,
    kAirportItlwmAPSTAActionFrameLogLineReceived = 0x10b2,
    kAirportItlwmAPSTAActionFrameLogLineEmpty = 0x10b4,
    kAirportItlwmAPSTAActionFrameLogLineInvalidMinimum = 0x10b6,
    kAirportItlwmAPSTAActionFrameLogLineInvalidVersion = 0x10bf,
    kAirportItlwmAPSTAActionFrameLogLineContents = 0x10c9,
    kAirportItlwmAPSTAActionFrameLogLineConcurrency = 0x10da,
    kAirportItlwmAPSTACheckAllStaInLpmLogLine = 0x11cf,
    kAirportItlwmAPSTARunPowerSaveStateMachineLogLine = 0x1422,
    kAirportItlwmAPSTAChannelDataChannelOffset = 0x04,
    kAirportItlwmAPSTAChannelDataNumberOffset = 0x08,
    kAirportItlwmAPSTAChannelDataFlagsOffset = 0x0c,
    kAirportItlwmAPSTAChannelDataSize = 0x10,
    kAirportItlwmAPSTAChannelCarrierSize = 0x0c,
    kAirportItlwmAPSTAChannelCarrierNumberOffset = 0x04,
    kAirportItlwmAPSTAChannelCarrierFlagsOffset = 0x08,
    kAirportItlwmAPSTAGetChannelVirtualIoctlSelector = 0x1d,
    kAirportItlwmAPSTAGetChannelRxPayloadSize = 0x0c,
    kAirportItlwmAPSTAGetChannelOutputNumberOffset = 0x08,
    kAirportItlwmAPSTAGetChannelOutputFlagsOffset = 0x0c,
    kAirportItlwmAPSTAGetChannelBandSplitThreshold = 0x0f,
    kAirportItlwmAPSTAGetChannel2GHzFlag = 0x08,
    kAirportItlwmAPSTAGetChannel5GHzFlag = 0x10,
    kAirportItlwmAPSTASetChannelInvalidArgumentReturn = 0x16,
    kAirportItlwmAPSTASetChannelInvalidSoftAPInfoReturn = 0xe00002c2,
    kAirportItlwmAPSTASetChannelNumberOffset = 0x08,
    kAirportItlwmAPSTASetChannelFlagsOffset = 0x0c,
    kAirportItlwmAPSTASetChannelTrapThreshold = 0x100,
    kAirportItlwmAPSTASetChannelBandSplitThreshold = 0x0f,
    kAirportItlwmAPSTASetChannel5GHzBandArg = 3,
    kAirportItlwmAPSTASetChannel2GHzBandArg = 0,
    kAirportItlwmAPSTASetChannelFlag20MHz = 0x02,
    kAirportItlwmAPSTASetChannelFlag40MHz = 0x04,
    kAirportItlwmAPSTASetChannelFlag80MHz = 0x400,
    kAirportItlwmAPSTASetChannelBandwidth20MHz = 2,
    kAirportItlwmAPSTASetChannelBandwidth40MHz = 3,
    kAirportItlwmAPSTASetChannelBandwidth80MHz = 4,
    kAirportItlwmAPSTASetChannelDefaultBandwidthCorePrivateOffset = 0x408,
    kAirportItlwmAPSTASetChannelChanspecPayloadSize = 4,
    kAirportItlwmAPSTASetChannelLocalCsaTriggerCount = 0,
    kAirportItlwmAPSTASetChannelNoOwnerRoutesPrimary = 1,
    kAirportItlwmAPSTACsaRequiredStateOffset = 0x26c,
    kAirportItlwmAPSTACsaResetFlagOffset = 0x329,
    kAirportItlwmAPSTACsaResetFlagBit = 0x01,
    kAirportItlwmAPSTACsaNotUpReturn = 6,
    kAirportItlwmAPSTACsaInvalidArgumentReturn = 0x16,
    kAirportItlwmAPSTACsaMinimumPrimaryChannel = 1,
    kAirportItlwmAPSTACsaMaximumExcludedPrimaryChannel =
        kAirportItlwmAPSTASetChannelTrapThreshold,
    kAirportItlwmAPSTACsaInputChannelOffset = 0x04,
    kAirportItlwmAPSTACsaInputModeOffset = 0x10,
    kAirportItlwmAPSTACsaInputFeatureGateOffset = 0x14,
    kAirportItlwmAPSTACsaFeatureGate46 = 0x46,
    kAirportItlwmAPSTACsaCorePrivateFeatureByteOffset = 0x4d59,
    kAirportItlwmAPSTACsaCoreFeatureByteMask = 0x01,
    kAirportItlwmAPSTACsaCoreVtable1110Offset = 0x1110,
    kAirportItlwmAPSTACsaCoreVtable1110Arg = 0,
    kAirportItlwmAPSTACsaPayloadZero0Offset = 0x00,
    kAirportItlwmAPSTACsaPayloadModeOffset = 0x01,
    kAirportItlwmAPSTACsaPayloadChanspecOffset = 0x02,
    kAirportItlwmAPSTACsaPayloadReservedOffset = 0x04,
    kAirportItlwmAPSTACsaPayloadSize = 0x06,
    kAirportItlwmAPSTASetMaxAssocNoLocalClamp = 1,
    kAirportItlwmAPSTASetMaxAssocPayloadAddsAssociatedCount = 1,
    kAirportItlwmAPSTAStaAuthorizeNullReturn = 0xe00002c2,
    kAirportItlwmAPSTAStaAuthorizePreAPUpTableMutationCount = 0,
    kAirportItlwmAPSTAStaAuthorizeFlagOffset = 0x04,
    kAirportItlwmAPSTAStaAuthorizeMacOffset = 0x08,
    kAirportItlwmAPSTAStaAuthorizeMacPayloadSize = 0x06,
    kAirportItlwmAPSTAStaAuthorizeFlagThreshold = 1,
    kAirportItlwmAPSTAStaAuthorizeSelectorIfAuthorized = 0x79,
    kAirportItlwmAPSTAStaAuthorizeSelectorIfNotAuthorized = 0x7a,
    kAirportItlwmAPSTAStaDisassocReasonOffset = 0x04,
    kAirportItlwmAPSTAStaDisassocValue08Offset = 0x08,
    kAirportItlwmAPSTAStaDisassocValue0cOffset = 0x0c,
    kAirportItlwmAPSTAStaDisassocTxPayloadSize = 0x0c,
    kAirportItlwmAPSTAStaDisassocRxPayloadSize = 0x0c,
    kAirportItlwmAPSTAStaDisassocVirtualIoctlSelector = 0xc9,
    kAirportItlwmAPSTAStaDisassocPayloadReasonOffset = 0x00,
    kAirportItlwmAPSTAStaDisassocPayloadValue04Offset = 0x04,
    kAirportItlwmAPSTAStaDisassocPayloadValue08Offset = 0x08,
    kAirportItlwmAPSTAStaDisassocPayloadSentinel0aOffset = 0x0a,
    kAirportItlwmAPSTAStaDisassocPayloadSentinel0aValue = 0xaaaa,
    kAirportItlwmAPSTAStaDisassocHasNullGuard = 0,
    kAirportItlwmAPSTAStaDeauthTailcallVtableOffset = 0x1040,
};

static const uint64_t kAirportItlwmAPSTARegistrationOptions24 = 0x8000000080ULL;
static const uint64_t kAirportItlwmAPSTAVendorIEDataHeader00Value = 0x1a00000001ULL;
static const uint64_t kAirportItlwmAPSTAVendorIEDataHeader08Value = 0x400000001ULL;
static const uint64_t kAirportItlwmAPSTAAppleVendorIEAddHeader04Value = 0x700000001ULL;
static const uint64_t kAirportItlwmAPSTAAppleVendorIECapabilityHeader0cValue =
    0x10106f217000addULL;
static const uint64_t kAirportItlwmAPSTAEnableAPScbProbeHeaderValue = 0xf0000001eULL;
static const uint64_t kAirportItlwmAPSTACommandRxPayloadRange0cValue = 0xc000c000cULL;
static const uint64_t kAirportItlwmAPSTAGetStationListRxPayloadRangeValue =
    0x10001000100ULL;
static const uint64_t kAirportItlwmAPSTAGetKeyRscRxPayloadRangeValue =
    0x800040008ULL;
static const uint64_t kAirportItlwmAPSTARpsNoaDutyCycleHeaderValue =
    0x100100101ULL;
static const uint64_t kAirportItlwmAPSTARpsNoaDynamicHeaderValue =
    0x300180101ULL;
static const uint64_t kAirportItlwmAPSTASoftAPPeerStatsHeaderValue =
    0x80002ULL;

static const uint32_t kAirportItlwmAPSTARSNConfAppleCipherMap[] = {
    0x00000000, 0x00000001, 0x00000001,
    0x00000002, 0x00000004, 0x00000004,
    0x00000000, 0x00000000, 0x00000100
};

static const uint8_t kAirportItlwmAPSTAAppleIEOui[] = {0x00, 0x17, 0xf2};
static const uint8_t kAirportItlwmAPSTAAppleIEBsOui[] = {0x00, 0x03, 0x93};
static const uint8_t kAirportItlwmAPSTAAppleIEDeviceInfoOui[] = {0x00, 0xa0, 0x40};

static const char kAirportItlwmAPSTAMaxAssocIovarName[] = "maxassoc";
static const char kAirportItlwmAPSTAVndrIEIovarName[] = "vndr_ie";
static const char kAirportItlwmAPSTAAppleVendorIEDeleteCommand[] = "del";
static const char kAirportItlwmAPSTAAppleVendorIEAddCommand[] = "add";
static const char kAirportItlwmAPSTADeleteIPv4PktFilterIovarName[] =
    "pkt_filter_delete";
static const char kAirportItlwmAPSTABcnIntervalRxPayloadLabel[] =
    "BCNPRD IOCTL rxPayload bytestream: ";
static const char kAirportItlwmAPSTABcnDtimRxPayloadLabel[] =
    "DTIMPRD IOCTL rxPayload bytestream: ";
static const char kAirportItlwmAPSTAEnableAPRrmThrottleWindowIovarName[] =
    "rrm_bcn_req_thrtl_win";
static const char kAirportItlwmAPSTAEnableAPRrmMaxOffChannelIovarName[] =
    "rrm_bcn_req_max_off_chan_time";
static const char kAirportItlwmAPSTAEnableAPWnmIovarName[] = "wnm";
static const char kAirportItlwmAPSTAEnableAPMpduBootArgName[] = "wlan.ap.maxmpdu";
static const char kAirportItlwmAPSTAEnableAPScbProbeIovarName[] = "scb_probe";
static const char kAirportItlwmAPSTAConfigureMpduIovarName[] = "ampdu_mpdu";
static const char kAirportItlwmAPSTALowPowerModeIovarName[] = "lphs_mode";
static const char kAirportItlwmAPSTAArpHostIpClearIovarName[] = "arp_hostip_clear";
static const char kAirportItlwmAPSTAArpHostIpIovarName[] = "arp_hostip";
static const char kAirportItlwmAPSTARpsNoaIovarName[] = "rpsnoa";
static const char kAirportItlwmAPSTAChannelChanspecIovarName[] = "chanspec";
static const char kAirportItlwmAPSTACsaIovarName[] = "csa";
static const char kAirportItlwmAPSTAGetStaIEListIovarName[] = "wpaie";
static const char kAirportItlwmAPSTAGetStaStatsIovarName[] = "sta_info";
static const char kAirportItlwmAPSTASetMacAddressIovarName[] = "cur_etheraddr";
static const char kAirportItlwmAPSTASoftAPPeerStatsIovarName[] = "softap_stats";
static const char kAirportItlwmAPSTAPowerStateBeaconWaitIovarName[] =
    "modesw_bcns_wait";
static const char kAirportItlwmAPSTAMfpIovarName[] = "mfp";

struct AirportItlwmAPSTAVirtualIfCreateCarrierLayout {
    uint32_t version;
    uint8_t  mac[6];
    uint16_t reserved0a;
    uint32_t role;
    uint8_t  bsdName[kAirportItlwmAPSTAVirtualIfBsdNameSize];
} __attribute__((packed));

struct AirportItlwmAPSTARegistrationInfoLayout {
    uint8_t  reserved0000[0x0c];
    uint32_t interfaceSubFamily0c;
    uint8_t  reserved0010[0x04];
    uint32_t registrationType14;
    uint8_t  reserved0018[0x0c];
    uint64_t registrationOptions24;
    uint8_t  reserved002c[0x04];
    const char *bsdNamePrefix30;
    uint32_t bsdUnitNumber38;
    uint8_t  reserved003c[0x04];
    uint32_t powerFlags40;
    uint8_t  reserved0044[0xc4];
    uint8_t  hardwareAddress108[6];
    uint8_t  reserved010e[0x22];
} __attribute__((packed));

struct AirportItlwmAPSTASoftAPParamsOutputLayout {
    uint8_t  reserved0000[0x04];
    uint32_t param04;
    uint32_t param08;
    uint32_t param0c;
    uint32_t param10;
    uint16_t param14;
    uint8_t  mode16;
    uint8_t  enabled17;
    uint8_t  param18;
} __attribute__((packed));

typedef AirportItlwmAPSTASoftAPParamsOutputLayout
    AirportItlwmAPSTASoftAPParamsInputLayout;

struct AirportItlwmAPSTASsidDataLayout {
    uint8_t  reserved0000[0x04];
    uint32_t length04;
    uint8_t  ssid08[kAirportItlwmAPSTAGetSsidMaxLength];
} __attribute__((packed));

struct AirportItlwmAPSTAStateDataLayout {
    uint8_t  reserved0000[0x04];
    uint32_t state04;
} __attribute__((packed));

struct AirportItlwmAPSTAOpModeDataLayout {
    uint32_t type00;
    uint32_t mode04;
} __attribute__((packed));

struct AirportItlwmAPSTAPeerCacheMaximumSizeLayout {
    uint8_t  reserved0000[0x04];
    uint32_t maximum04;
} __attribute__((packed));

struct AirportItlwmAPSTAPeerCacheControlLayout {
    uint8_t  reserved0000[0x04];
    uint32_t command04;
    uint32_t value08;
    uint16_t value0c;
    uint16_t value0e;
} __attribute__((packed));

struct AirportItlwmAPSTAHostApModeHiddenOutputLayout {
    uint32_t hidden00;
} __attribute__((packed));

struct AirportItlwmAPSTAHostApModeHiddenLayout {
    uint32_t version00;
    uint32_t hidden04;
} __attribute__((packed));

struct AirportItlwmAPSTASoftAPStatsLayout {
    uint8_t stats[kAirportItlwmAPSTAGetSoftAPStatsCopySize];
} __attribute__((packed));

struct AirportItlwmAPSTASoftAPExtCapsInputLayout {
    uint8_t flag00;
    uint8_t tail51[8];
    uint8_t tail59[8];
} __attribute__((packed));

struct AirportItlwmAPSTAMisMaxStaInputLayout {
    uint32_t maxSta00;
} __attribute__((packed));

struct AirportItlwmAPSTAStationListMacListLayout {
    uint32_t initialValue00;
    uint8_t  payload04[kAirportItlwmAPSTAGetStationListMacListSize - 0x04];
} __attribute__((packed));

struct AirportItlwmAPSTAStationTableEntryLayout {
    uint8_t  active00;
    uint8_t  mac01[kAirportItlwmAPSTAStationTableMacSize];
    uint8_t  reserved0007[0x09];
    uint32_t sleepState10;
    uint8_t  reserved0014[0x0c];
    uint32_t aihsFlag20;
    uint32_t sharingFlag24;
    uint32_t appleStationFlag28;
    uint8_t  reserved002c[0x04];
} __attribute__((packed));

struct AirportItlwmAPSTAStaRemoveMessageLayout {
    uint32_t macDword00;
    uint16_t macTail04;
    uint16_t reserved06;
    uint32_t associatedCount08;
} __attribute__((packed));

struct AirportItlwmAPSTAStaAssocMessageLayout {
    uint32_t macDword00;
    uint16_t macTail04;
    uint8_t  reserved0006[0x02];
    uint32_t associatedCount08;
    uint8_t  assocFlags0c;
    uint8_t  reserved000d[0x03];
    uint8_t  rsnxe10[kAirportItlwmAPSTAEventAssocMessageSize -
                    kAirportItlwmAPSTAEventRsnxeOutputOffset];
} __attribute__((packed));

struct AirportItlwmAPSTAAuthIndMessageLayout {
    uint32_t type00;
    uint32_t reserved04;
    uint32_t status08;
    uint32_t macDword0c;
    uint16_t macTail10;
    uint8_t  reserved12[0x06];
    uint8_t  chunkType1Data18[0x3c];
    uint8_t  chunkType2Data54[kAirportItlwmAPSTAEventAuthIndChunkType2Size];
    uint8_t  reserved64[0x08];
} __attribute__((packed));

struct AirportItlwmAPSTAWlEventMsgLayout {
    uint16_t eventVersion00;
    uint16_t eventFlags02;
    uint32_t eventType04;
    uint32_t status08;
    uint32_t reason0c;
    uint32_t authType10;
    uint32_t dataLength14;
    uint8_t  address18[kAirportItlwmAPSTAEventAddressSize];
    uint8_t  reserved001e[0x12];
    uint8_t  data30[1];
} __attribute__((packed));

struct AirportItlwmAPSTAActionFrameV1Layout {
    uint16_t version00;
    uint8_t  reserved0002[0x0e];
    uint8_t  category10;
    uint8_t  action11;
} __attribute__((packed));

struct AirportItlwmAPSTAActionFrameV2Layout {
    uint16_t version00;
    uint8_t  reserved0002[0x16];
    uint8_t  category18;
    uint8_t  action19;
} __attribute__((packed));

struct AirportItlwmAPSTAStaIEDataLayout {
    uint8_t  reserved0000[0x04];
    uint8_t  mac04[kAirportItlwmAPSTAStationTableMacSize];
    uint8_t  reserved000a[0x02];
    uint32_t length0c;
    uint8_t  output10[0x06];
} __attribute__((packed));

struct AirportItlwmAPSTAStaStatsDataLayout {
    uint32_t valid00;
    uint8_t  mac04[kAirportItlwmAPSTAGetStaStatsInputMacSize];
    uint8_t  reserved000a[0x02];
    uint32_t field0c;
    uint32_t field10;
    uint32_t field14;
    uint32_t field18;
} __attribute__((packed));

struct AirportItlwmAPSTAKeyRscDataLayout {
    uint8_t  reserved0000[0x0e];
    uint16_t keyIndex0e;
    uint8_t  reserved0010[0x40];
    uint32_t rscLength50;
    uint8_t  rsc54[kAirportItlwmAPSTAGetKeyRscOutputLengthValue];
} __attribute__((packed));

struct AirportItlwmAPSTAWsecKeyLayout {
    uint8_t bytes[kAirportItlwmAPSTASetCipherKeyWsecKeySize];
} __attribute__((packed));

struct AirportItlwmAPSTASoftAPWifiNetworkInfoCarrierLayout {
    uint8_t reserved0000[0x03];
    uint8_t length03;
    uint8_t payload04[0x20];
} __attribute__((packed));

struct AirportItlwmAPSTACommandPayloadHeadLayout {
    void    *data;
    uint16_t length;
} __attribute__((packed));

struct AirportItlwmAPSTAVendorIEDataLayout {
    uint64_t header00;
    uint64_t header08;
    uint32_t payloadLength10;
    uint8_t  elementId14;
    uint8_t  payload15[kAirportItlwmAPSTAVendorIEDataPayloadCapacity];
} __attribute__((packed));

struct AirportItlwmAPSTAAppleVendorIESetBufferLayout {
    char     command00[4];
    uint64_t header04;
    uint8_t  body0c[kAirportItlwmAPSTAAppleVendorIESetBufferBodyCapacity];
} __attribute__((packed));

struct AirportItlwmAPSTAScbProbePayloadLayout {
    uint64_t header00;
    uint32_t value08;
} __attribute__((packed));

struct AirportItlwmAPSTACommandCompletionLayout {
    void *owner00;
    void *function08;
    void *cookie10;
} __attribute__((packed));

struct AirportItlwmAPSTACommandRxPayloadLayout {
    void    *data;
    uint64_t range08;
} __attribute__((packed));

struct AirportItlwmAPSTARpsNoaDutyCyclePayloadLayout {
    uint64_t header00;
    uint32_t zero08;
    uint16_t mode0c;
    uint16_t enable0e;
} __attribute__((packed));

struct AirportItlwmAPSTARpsNoaDynamicPayloadLayout {
    uint64_t header00;
    uint32_t zero08;
    uint16_t mode0c;
    uint8_t  dynamicLevel0e;
    uint8_t  reserved0f;
    uint64_t rotatedParams10;
} __attribute__((packed));

struct AirportItlwmAPSTAPowerStatsBucketLayout {
    uint64_t duration00;
    uint8_t  reserved0008[0x08];
} __attribute__((packed));

struct AirportItlwmAPSTASoftAPPeerStatsPayloadLayout {
    uint32_t header00;
    uint8_t  reserved0004[0x06];
    uint16_t mode0a;
    uint8_t  reserved000c[0x02];
} __attribute__((packed));

struct AirportItlwmAPSTAInactivityKillMessageLayout {
    uint64_t zero00;
    uint32_t sentinel08;
} __attribute__((packed));

struct AirportItlwmAPSTABcmAssocListLayout {
    uint32_t count00;
    uint8_t  firstMac04[kAirportItlwmAPSTAStationTableMacSize];
} __attribute__((packed));

struct AirportItlwmAPSTAAppleAssocEntryLayout {
    uint32_t valid00;
    uint32_t macDword04;
    uint16_t macTail08;
    uint8_t  reserved000a[0x06];
} __attribute__((packed));

struct AirportItlwmAPSTAAppleAssocListLayout {
    uint32_t version00;
    uint32_t count04;
    AirportItlwmAPSTAAppleAssocEntryLayout
        entries08[kAirportItlwmAPSTAAssocListMaxCount];
} __attribute__((packed));

struct AirportItlwmAPSTAPowerStateRecordLayout {
    uint32_t transitionCount00;
    uint8_t  reserved0004[0x04];
    uint64_t duration08;
} __attribute__((packed));

struct AirportItlwmAPSTAChannelDataLayout {
    uint32_t version00;
    uint32_t channelVersion04;
    uint32_t channelNumber08;
    uint32_t channelFlags0c;
} __attribute__((packed));

struct AirportItlwmAPSTAChannelCarrierLayout {
    uint32_t version00;
    uint32_t channelNumber04;
    uint32_t channelFlags08;
} __attribute__((packed));

struct AirportItlwmAPSTACsaInputLayout {
    uint32_t version00;
    AirportItlwmAPSTAChannelCarrierLayout channel04;
    uint8_t  mode10;
    uint8_t  reserved0011[0x03];
    uint8_t  featureGate14;
} __attribute__((packed));

struct AirportItlwmAPSTACsaIovarPayloadLayout {
    uint8_t  zero00;
    uint8_t  mode01;
    uint16_t chanspec02;
    uint16_t reserved04;
} __attribute__((packed));

struct AirportItlwmAPSTAStaAuthorizeInputLayout {
    uint32_t version00;
    uint32_t authorizeFlag04;
    uint8_t  mac08[6];
} __attribute__((packed));

struct AirportItlwmAPSTAStaDisassocInputLayout {
    uint32_t version00;
    uint32_t reason04;
    uint32_t value08;
    uint16_t value0c;
} __attribute__((packed));

struct AirportItlwmAPSTAStaDisassocPayloadLayout {
    uint32_t reason00;
    uint32_t value04;
    uint16_t value08;
    uint16_t sentinel0a;
} __attribute__((packed));

struct AirportItlwmAPSTAHostApModeNetworkDataLayout {
    uint8_t  reserved0000[0x04];
    uint32_t flags04;
    uint8_t  reserved0008[0x14];
    uint32_t ssidLength1c;
    uint8_t  ssid20[kAirportItlwmAPSTAGetSsidMaxLength];
    uint8_t  reserved0040[0x29c];
    uint32_t vendorIELength2dc;
    uint8_t  vendorIEData2e0[1];
} __attribute__((packed));

// AppleBCMWLANIO80211APSTAInterface has object size 0x138 and stores a pointer
// to this APSTA private state block at object offset +0x130.
struct AirportItlwmAPSTAStateBlock {
    uint32_t softapAssociatedStaCount00;
    uint32_t softapMaxAssoc04;
    uint32_t softapMaxAssocLimit08;
    uint8_t  powerAssertionFlag0c;
    uint8_t  hiddenNetworkFlag0d;
    uint8_t  softapParam0e;
    uint8_t  reserved000f[0x01];
    uint8_t  softapMode10;
    uint8_t  reserved0011[0x03];
    uint16_t softapBeaconInterval14;
    uint16_t softapDtimPeriod16;
    uint32_t softapParam18;
    uint32_t softapParam1c;
    uint32_t softapParam20;
    uint32_t softapParam24;
    uint32_t softapParam28;
    uint8_t  softapWifiNetworkInfoIE[kAirportItlwmAPSTAWifiNetworkInfoIESize];
    uint8_t  softapAppleVendorIEExtra50;
    uint8_t  softapAppleVendorIETail51[8];
    uint8_t  softapAppleVendorIETail59[8];
    uint8_t  reserved0061;
    uint8_t  lphsVendorIEFlag62;
    uint8_t  reserved0063;
    uint32_t lowTrafficCounter64;
    uint16_t softapAppliedBeaconInterval68;
    uint16_t softapAppliedDtimPeriod6a;
    uint8_t  reserved006c[0x04];
    void    *apStatsTimerSource;
    void    *apMonitorTimerSource;
    uint32_t softapEvent80;
    uint16_t softapEvent84;
    uint8_t  reserved0086[0x02];
    uint64_t softapRuntime88;
    uint64_t softapRuntime90;
    uint64_t softapRuntime98;
    uint64_t softapRuntimeA0;
    uint8_t  reserved00a8[0x04];
    uint32_t arpHostIpAc;
    uint32_t softapRuntimeB0;
    uint32_t softapPowerStateB4;
    AirportItlwmAPSTAStationTableEntryLayout
        softapStaTableB8[kAirportItlwmAPSTAStationTableEntryCount];
    uint64_t softapRuntime1a8;
    uint8_t  softapStats[kAirportItlwmAPSTASoftAPStatsSize];
    uint8_t  softapConcurrencyMirror208;
    uint8_t  reserved0209[0x03];
    uint32_t softapStatsAge20c;
    void    *resource210;
    void    *ownerCoreOrInterface;
    uint8_t  reserved0220[0x08];
    void    *resource228;
    uint8_t  reserved0230[0x10];
    void    *resource240;
    void    *resource248;
    void    *resource250;
    void    *resource258;
    void    *resource260;
    uint32_t initState268;
    uint32_t resetState26c;
    uint32_t hostApTransitionState270;
    uint32_t softapSsidLength274;
    uint8_t  softapSsid278[kAirportItlwmAPSTAGetSsidMaxLength];
    uint8_t  reserved0298[0x03];
    uint8_t  rsnConfGate29b;
    uint8_t  reserved029c[0x08];
    uint8_t  numTxQueues;
    uint8_t  reserved02a5[0x03];
    uint32_t txQueueServiceClassMap[kAirportItlwmAPSTATxSubQueueCount];
    uint32_t acToTxQueueIndex[kAirportItlwmAPSTATxSubQueueCount];
    void    *busSkywalkProvider2c8;
    void    *datapathOwner2d0;
    void    *txPacketPool;
    void    *rxPacketPool;
    void    *txCompQueue;
    void    *rxCompQueue;
    void    *resource2f8;
    void    *txSubQueues[kAirportItlwmAPSTATxSubQueueCount];
    void    *multiCastQueue;
    uint8_t  softapPeerStatsEnabled328;
    uint8_t  resetFlag329;
    uint8_t  featureGate0d;
    uint8_t  featureGate0c;
    uint8_t  reserved032c[0x04];
    void    *workQueue330;
};

struct AirportItlwmAPSTAStartQueueConfigLayout {
    void    *interfaceOwner;
    uint8_t  numTxQueues;
    uint8_t  reserved0009[0x07];
    uint32_t *txQueueServiceClassMap;
    void    **txSubQueues;
    void    **resource2f8Storage;
    void    **txCompQueueStorage;
    void    **rxCompQueueStorage;
    void    **multiCastQueueStorage;
    void    **txPacketPoolStorage;
    void    **rxPacketPoolStorage;
    uint64_t reserved0050;
    void    *logger;
};

struct AirportItlwmAPSTARegisterQueueListLayout {
    void *txSubQueues[kAirportItlwmAPSTATxSubQueueCount];
    void *txCompQueue;
    void *rxCompQueue;
};

// This is a layout witness only, not the final APSTA owner class.
struct AirportItlwmAPSTAObjectStorageLayout {
    uint8_t reserved0000[0x130];
    AirportItlwmAPSTAStateBlock *state;
};

struct AirportItlwmAPSTACoreExpansionStorageLayout {
    uint8_t reserved0000[kAirportItlwmAPSTACoreExpansionProximityOwnerOffset];
    void *proximityOwner;
    AirportItlwmAPSTAObjectStorageLayout *apstaOwner;
    uint8_t reserved2c38[
        kAirportItlwmAPSTAHostApModeNanOwnerOffset -
        (kAirportItlwmAPSTACoreExpansionOwnerOffset + sizeof(void *))];
    void *nanOwner;
    void *nanDataOwner;
};

typedef AirportItlwmAPSTAObjectStorageLayout *(*AirportItlwmAPSTAFactoryContract)
    (void *core, void *macCarrier, uint32_t role, char *bsdNameCarrier);

static_assert(sizeof(void *) == 8,
              "APSTA recovered offsets assume the Tahoe x86_64 kernel ABI");
static_assert(sizeof(AirportItlwmAPSTAVirtualIfCreateCarrierLayout) ==
              kAirportItlwmAPSTAVirtualIfCreateCarrierSize,
              "APSTA VIRTUAL_IF_CREATE carrier size mismatch");
static_assert(offsetof(AirportItlwmAPSTAVirtualIfCreateCarrierLayout, mac) == 0x04,
              "APSTA VIRTUAL_IF_CREATE MAC carrier offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAVirtualIfCreateCarrierLayout, role) == 0x0c,
              "APSTA VIRTUAL_IF_CREATE role offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAVirtualIfCreateCarrierLayout, bsdName) == 0x10,
              "APSTA VIRTUAL_IF_CREATE BSD-name carrier offset mismatch");
static_assert(sizeof(AirportItlwmAPSTARegistrationInfoLayout) ==
              kAirportItlwmAPSTARegistrationInfoSize,
              "APSTA RegistrationInfo size mismatch");
static_assert(offsetof(AirportItlwmAPSTARegistrationInfoLayout, interfaceSubFamily0c) == 0x0c,
              "APSTA RegistrationInfo +0x0c offset mismatch");
static_assert(offsetof(AirportItlwmAPSTARegistrationInfoLayout, registrationType14) == 0x14,
              "APSTA RegistrationInfo +0x14 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTARegistrationInfoLayout, registrationOptions24) == 0x24,
              "APSTA RegistrationInfo +0x24 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTARegistrationInfoLayout, bsdNamePrefix30) == 0x30,
              "APSTA RegistrationInfo +0x30 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTARegistrationInfoLayout, bsdUnitNumber38) == 0x38,
              "APSTA RegistrationInfo +0x38 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTARegistrationInfoLayout, powerFlags40) == 0x40,
              "APSTA RegistrationInfo +0x40 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTARegistrationInfoLayout, hardwareAddress108) == 0x108,
              "APSTA RegistrationInfo +0x108 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASsidDataLayout, length04) ==
              kAirportItlwmAPSTAGetSsidOutputLengthOffset,
              "APSTA getSSID output length offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASsidDataLayout, ssid08) ==
              kAirportItlwmAPSTAGetSsidOutputBytesOffset,
              "APSTA getSSID output bytes offset mismatch");
static_assert(sizeof(AirportItlwmAPSTASsidDataLayout) ==
              kAirportItlwmAPSTAGetSsidOutputBytesOffset +
              kAirportItlwmAPSTAGetSsidMaxLength,
              "APSTA getSSID output witness size mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateDataLayout, state04) ==
              kAirportItlwmAPSTAGetStateOutputValueOffset,
              "APSTA getSTATE output state offset mismatch");
static_assert(kAirportItlwmAPSTAGetStateOutputValue ==
              kAirportItlwmAPSTASoftAPState,
              "APSTA getSTATE output value mismatch");
static_assert(sizeof(AirportItlwmAPSTAStateDataLayout) == 0x08,
              "APSTA getSTATE output witness size mismatch");
static_assert(offsetof(AirportItlwmAPSTAOpModeDataLayout, type00) ==
              kAirportItlwmAPSTAGetOpModeOutputTypeOffset,
              "APSTA getOP_MODE output type offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAOpModeDataLayout, mode04) ==
              kAirportItlwmAPSTAGetOpModeOutputModeOffset,
              "APSTA getOP_MODE output mode offset mismatch");
static_assert(sizeof(AirportItlwmAPSTAOpModeDataLayout) == 0x08,
              "APSTA getOP_MODE output witness size mismatch");
static_assert(offsetof(AirportItlwmAPSTAPeerCacheMaximumSizeLayout, maximum04) ==
              kAirportItlwmAPSTAGetPeerCacheMaximumSizeOutputOffset,
              "APSTA peer-cache maximum-size output offset mismatch");
static_assert(kAirportItlwmAPSTAGetPeerCacheMaximumSizeValue ==
              kAirportItlwmAPSTAPeerCacheMaximumSize,
              "APSTA peer-cache maximum-size value mismatch");
static_assert(offsetof(AirportItlwmAPSTAPeerCacheControlLayout, command04) ==
              kAirportItlwmAPSTASetPeerCacheControlCommandOffset,
              "APSTA peer-cache control command offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAPeerCacheControlLayout, value08) ==
              kAirportItlwmAPSTASetPeerCacheControlValue08Offset,
              "APSTA peer-cache control +0x08 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAPeerCacheControlLayout, value0c) ==
              kAirportItlwmAPSTASetPeerCacheControlValue0cOffset,
              "APSTA peer-cache control +0x0c offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAPeerCacheControlLayout, value0e) ==
              kAirportItlwmAPSTASetPeerCacheControlValue0eOffset,
              "APSTA peer-cache control +0x0e offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAHostApModeHiddenOutputLayout, hidden00) ==
              kAirportItlwmAPSTAGetHostApModeHiddenOutputOffset,
              "APSTA HostAP hidden output offset mismatch");
static_assert(kAirportItlwmAPSTAGetHostApModeHiddenValue ==
              kAirportItlwmAPSTAHostApModeHiddenValue,
              "APSTA HostAP hidden output value mismatch");
static_assert(offsetof(AirportItlwmAPSTAHostApModeHiddenLayout, hidden04) ==
              kAirportItlwmAPSTAHiddenInputValueOffset,
              "APSTA setHOST_AP_MODE_HIDDEN input value offset mismatch");
static_assert(sizeof(AirportItlwmAPSTAHostApModeHiddenLayout) ==
              kAirportItlwmAPSTAHiddenInputCarrierSize,
              "APSTA setHOST_AP_MODE_HIDDEN input carrier size mismatch");
static_assert(sizeof(AirportItlwmAPSTASoftAPStatsLayout) ==
              kAirportItlwmAPSTAGetSoftAPStatsCopySize,
              "APSTA SoftAP stats output witness size mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPExtCapsInputLayout, flag00) ==
              kAirportItlwmAPSTASetSoftAPExtCapsInputFlagOffset,
              "APSTA SoftAP ext-cap input flag offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPExtCapsInputLayout, tail51) ==
              kAirportItlwmAPSTASetSoftAPExtCapsInputTail51Offset,
              "APSTA SoftAP ext-cap input +0x01 tail offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPExtCapsInputLayout, tail59) ==
              kAirportItlwmAPSTASetSoftAPExtCapsInputTail59Offset,
              "APSTA SoftAP ext-cap input +0x09 tail offset mismatch");
static_assert(sizeof(AirportItlwmAPSTASoftAPExtCapsInputLayout) == 0x11,
              "APSTA SoftAP ext-cap input witness size mismatch");
static_assert(offsetof(AirportItlwmAPSTAMisMaxStaInputLayout, maxSta00) ==
              kAirportItlwmAPSTASetMisMaxStaInputValueOffset,
              "APSTA MIS max-STA input offset mismatch");
static_assert(sizeof(AirportItlwmAPSTAStationListMacListLayout) ==
              kAirportItlwmAPSTAGetStationListMacListSize,
              "APSTA station-list maclist allocation size mismatch");
static_assert(offsetof(AirportItlwmAPSTAStationListMacListLayout, initialValue00) == 0x00,
              "APSTA station-list maclist initial value offset mismatch");
static_assert(sizeof(AirportItlwmAPSTAStationTableEntryLayout) ==
              kAirportItlwmAPSTAStationTableEntryStride,
              "APSTA station-table entry stride mismatch");
static_assert(offsetof(AirportItlwmAPSTAStationTableEntryLayout, active00) ==
              kAirportItlwmAPSTAStationTableActiveOffset,
              "APSTA station-table active flag offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStationTableEntryLayout, mac01) ==
              kAirportItlwmAPSTAStationTableMacOffsetInEntry,
              "APSTA station-table MAC offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStationTableEntryLayout, sleepState10) ==
              kAirportItlwmAPSTAStationTableSleepStateOffset,
              "APSTA station-table sleep-state offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStationTableEntryLayout, aihsFlag20) ==
              kAirportItlwmAPSTAStationTableAihsFlagOffset,
              "APSTA station-table AIHS flag offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStationTableEntryLayout, sharingFlag24) ==
              kAirportItlwmAPSTAStationTableSharingFlagOffset,
              "APSTA station-table sharing flag offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStationTableEntryLayout, appleStationFlag28) ==
              kAirportItlwmAPSTAStationTableAppleStationFlagOffset,
              "APSTA station-table Apple-station flag offset mismatch");
static_assert(kAirportItlwmAPSTAStationTableDefaultSleepState == 2,
              "APSTA station-table default sleep-state mismatch");
static_assert(kAirportItlwmAPSTAStationTableLowPowerSleepState == 1,
              "APSTA station-table low-power sleep-state mismatch");
static_assert(kAirportItlwmAPSTAStationTableAwakeSleepState == 2,
              "APSTA station-table awake sleep-state mismatch");
static_assert(sizeof(AirportItlwmAPSTAStaRemoveMessageLayout) ==
              kAirportItlwmAPSTAEventRemoveMessageSize,
              "APSTA STA removal message size mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaRemoveMessageLayout, associatedCount08) == 0x08,
              "APSTA STA removal associated-count offset mismatch");
static_assert(sizeof(AirportItlwmAPSTAStaAssocMessageLayout) ==
              kAirportItlwmAPSTAEventAssocMessageSize,
              "APSTA STA association message size mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaAssocMessageLayout, assocFlags0c) ==
              kAirportItlwmAPSTAEventAssocFlagsOffset,
              "APSTA STA association flags offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaAssocMessageLayout, rsnxe10) ==
              kAirportItlwmAPSTAEventRsnxeOutputOffset,
              "APSTA STA association RSNXE output offset mismatch");
static_assert(sizeof(AirportItlwmAPSTAAuthIndMessageLayout) ==
              kAirportItlwmAPSTAEventAuthIndMessageSize,
              "APSTA auth-ind message size mismatch");
static_assert(offsetof(AirportItlwmAPSTAAuthIndMessageLayout, type00) ==
              kAirportItlwmAPSTAEventAuthIndTypeOffset,
              "APSTA auth-ind message type offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAAuthIndMessageLayout, status08) ==
              kAirportItlwmAPSTAEventAuthIndStatusOffset,
              "APSTA auth-ind status offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAAuthIndMessageLayout, macDword0c) ==
              kAirportItlwmAPSTAEventAuthIndMacDwordOffset,
              "APSTA auth-ind MAC dword offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAAuthIndMessageLayout, macTail10) ==
              kAirportItlwmAPSTAEventAuthIndMacTailOffset,
              "APSTA auth-ind MAC tail offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAAuthIndMessageLayout, chunkType1Data18) ==
              kAirportItlwmAPSTAEventAuthIndChunkType1OutputOffset,
              "APSTA auth-ind chunk type 1 output offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAAuthIndMessageLayout, chunkType2Data54) ==
              kAirportItlwmAPSTAEventAuthIndChunkType2OutputOffset,
              "APSTA auth-ind chunk type 2 output offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAWlEventMsgLayout, eventType04) ==
              kAirportItlwmAPSTAEventTypeOffset,
              "APSTA wl_event_msg_t event type offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAWlEventMsgLayout, status08) ==
              kAirportItlwmAPSTAEventStatusOffset,
              "APSTA wl_event_msg_t status offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAWlEventMsgLayout, reason0c) ==
              kAirportItlwmAPSTAEventReasonOffset,
              "APSTA wl_event_msg_t reason offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAWlEventMsgLayout, authType10) ==
              kAirportItlwmAPSTAEventAuthTypeOffset,
              "APSTA wl_event_msg_t auth type offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAWlEventMsgLayout, dataLength14) ==
              kAirportItlwmAPSTAEventDataLengthOffset,
              "APSTA wl_event_msg_t data length offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAWlEventMsgLayout, address18) ==
              kAirportItlwmAPSTAEventAddressOffset,
              "APSTA wl_event_msg_t address offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAWlEventMsgLayout, data30) ==
              kAirportItlwmAPSTAEventDataOffset,
              "APSTA wl_event_msg_t data offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAActionFrameV1Layout, version00) ==
              kAirportItlwmAPSTAActionFrameVersionOffset,
              "APSTA action-frame v1 version offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAActionFrameV1Layout, category10) ==
              kAirportItlwmAPSTAActionFrameVersion1CategoryOffset,
              "APSTA action-frame v1 category offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAActionFrameV1Layout, action11) ==
              kAirportItlwmAPSTAActionFrameVersion1ActionOffset,
              "APSTA action-frame v1 action offset mismatch");
static_assert(sizeof(AirportItlwmAPSTAActionFrameV1Layout) ==
              kAirportItlwmAPSTAActionFrameMinimumLength,
              "APSTA action-frame v1 minimum size mismatch");
static_assert(offsetof(AirportItlwmAPSTAActionFrameV2Layout, category18) ==
              kAirportItlwmAPSTAActionFrameVersion2CategoryOffset,
              "APSTA action-frame v2 category offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAActionFrameV2Layout, action19) ==
              kAirportItlwmAPSTAActionFrameVersion2ActionOffset,
              "APSTA action-frame v2 action offset mismatch");
static_assert(sizeof(AirportItlwmAPSTAActionFrameV2Layout) ==
              kAirportItlwmAPSTAActionFrameVersion2MinimumLength,
              "APSTA action-frame v2 minimum size mismatch");
static_assert(kAirportItlwmAPSTAActionFrameVersionSwapRejectThreshold == 3,
              "APSTA action-frame version reject threshold mismatch");
static_assert(kAirportItlwmAPSTAActionFrameUnknownCategoryAction == 0xaa,
              "APSTA action-frame unknown category/action sentinel mismatch");
static_assert(kAirportItlwmAPSTAActionFrameVersion1EventCategoryOffset ==
              kAirportItlwmAPSTAEventDataOffset +
              kAirportItlwmAPSTAActionFrameVersion1CategoryOffset,
              "APSTA action-frame v1 event category offset mismatch");
static_assert(kAirportItlwmAPSTAActionFrameVersion1EventActionOffset ==
              kAirportItlwmAPSTAEventDataOffset +
              kAirportItlwmAPSTAActionFrameVersion1ActionOffset,
              "APSTA action-frame v1 event action offset mismatch");
static_assert(kAirportItlwmAPSTAActionFrameVersion2EventCategoryOffset ==
              kAirportItlwmAPSTAEventDataOffset +
              kAirportItlwmAPSTAActionFrameVersion2CategoryOffset,
              "APSTA action-frame v2 event category offset mismatch");
static_assert(kAirportItlwmAPSTAActionFrameVersion2EventActionOffset ==
              kAirportItlwmAPSTAEventDataOffset +
              kAirportItlwmAPSTAActionFrameVersion2ActionOffset,
              "APSTA action-frame v2 event action offset mismatch");
static_assert(kAirportItlwmAPSTAActionFrameLphsActionSleep ==
              kAirportItlwmAPSTAStationTableLowPowerSleepState,
              "APSTA action-frame LPHS sleep state mismatch");
static_assert(kAirportItlwmAPSTAActionFrameLphsActionAwake ==
              kAirportItlwmAPSTAStationTableAwakeSleepState,
              "APSTA action-frame LPHS awake state mismatch");
static_assert(kAirportItlwmAPSTAActionFrameStationSleepStateOffset ==
              kAirportItlwmAPSTAStationTableSleepStateOffset,
              "APSTA action-frame station sleep-state offset mismatch");
static_assert(kAirportItlwmAPSTACheckAllStaBlockingSleepState ==
              kAirportItlwmAPSTAStationTableAwakeSleepState,
              "APSTA all-STA LPM blocking sleep-state mismatch");
static_assert(kAirportItlwmAPSTAActionFrameAllStaPowerSaveState ==
              kAirportItlwmAPSTAPowerStateLowPower,
              "APSTA all-STA LPM power-save state mismatch");
static_assert(kAirportItlwmAPSTAActionFrameAllStaPowerSaveReason ==
              kAirportItlwmAPSTAPowerStateReasonAllStaLpm,
              "APSTA all-STA LPM power-save reason mismatch");
static_assert(kAirportItlwmAPSTAEventPostDispatchVtableOffset ==
              kAirportItlwmAPSTAHostApMisMaxStaVtableOffset,
              "APSTA STA event dispatcher vtable offset mismatch");
static_assert(kAirportItlwmAPSTAEventPostNotifyOwnerOffset ==
              kAirportItlwmAPSTAHoldPowerAssertionCoreResourceOffset,
              "APSTA STA event notify owner offset mismatch");
static_assert(kAirportItlwmAPSTAEventRemovalQueueNotifyEntryOffset ==
              kAirportItlwmAPSTAStationTableMacOffsetInEntry,
              "APSTA STA removal queue-notify MAC offset mismatch");
static_assert(kAirportItlwmAPSTAAppleIEOuiSize ==
              kAirportItlwmAPSTAAppleVendorIEAppleOUILength,
              "APSTA Apple IE OUI length mismatch");
static_assert(sizeof(kAirportItlwmAPSTAAppleIEOui) ==
              kAirportItlwmAPSTAAppleIEOuiSize,
              "APSTA Apple IE OUI byte-array length mismatch");
static_assert(sizeof(kAirportItlwmAPSTAAppleIEBsOui) ==
              kAirportItlwmAPSTAAppleIEOuiSize,
              "APSTA Apple BS IE OUI byte-array length mismatch");
static_assert(sizeof(kAirportItlwmAPSTAAppleIEDeviceInfoOui) ==
              kAirportItlwmAPSTAAppleIEOuiSize,
              "APSTA Apple device-info IE OUI byte-array length mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaIEDataLayout, mac04) ==
              kAirportItlwmAPSTAGetStaIEListInputMacOffset,
              "APSTA STA IE input MAC offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaIEDataLayout, length0c) ==
              kAirportItlwmAPSTAGetStaIEListInputLengthOffset,
              "APSTA STA IE length offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaIEDataLayout, output10) ==
              kAirportItlwmAPSTAGetStaIEListOutputMacOffset,
              "APSTA STA IE output buffer offset mismatch");
static_assert(kAirportItlwmAPSTAGetStaIEListOutputMacTailOffset ==
              kAirportItlwmAPSTAGetStaIEListOutputMacOffset + 0x04,
              "APSTA STA IE output MAC tail offset mismatch");
static_assert(kAirportItlwmAPSTAGetStaIEListOutputSourceOffset ==
              offsetof(AirportItlwmAPSTAStateBlock, softapStaTableB8) +
              offsetof(AirportItlwmAPSTAStationTableEntryLayout, mac01),
              "APSTA STA IE output source must start at station MAC bytes");
static_assert(offsetof(AirportItlwmAPSTAStaStatsDataLayout, valid00) ==
              kAirportItlwmAPSTAGetStaStatsOutputValidOffset,
              "APSTA STA stats valid offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaStatsDataLayout, mac04) ==
              kAirportItlwmAPSTAGetStaStatsInputMacOffset,
              "APSTA STA stats input MAC offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaStatsDataLayout, field0c) ==
              kAirportItlwmAPSTAGetStaStatsOutputField0cOffset,
              "APSTA STA stats output +0x0c offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaStatsDataLayout, field10) ==
              kAirportItlwmAPSTAGetStaStatsOutputField10Offset,
              "APSTA STA stats output +0x10 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaStatsDataLayout, field14) ==
              kAirportItlwmAPSTAGetStaStatsOutputField14Offset,
              "APSTA STA stats output +0x14 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaStatsDataLayout, field18) ==
              kAirportItlwmAPSTAGetStaStatsOutputField18Offset,
              "APSTA STA stats output +0x18 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAKeyRscDataLayout, keyIndex0e) ==
              kAirportItlwmAPSTAGetKeyRscInputKeyIndexOffset,
              "APSTA key RSC input key-index offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAKeyRscDataLayout, rscLength50) ==
              kAirportItlwmAPSTAGetKeyRscOutputLengthOffset,
              "APSTA key RSC output length offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAKeyRscDataLayout, rsc54) ==
              kAirportItlwmAPSTAGetKeyRscOutputValueOffset,
              "APSTA key RSC output value offset mismatch");
static_assert(sizeof(AirportItlwmAPSTAWsecKeyLayout) ==
              kAirportItlwmAPSTASetCipherKeyWsecKeySize,
              "APSTA wl_wsec_key size witness mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPParamsOutputLayout, param04) == 0x04,
              "APSTA SoftAP params output +0x04 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPParamsOutputLayout, param08) == 0x08,
              "APSTA SoftAP params output +0x08 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPParamsOutputLayout, param0c) == 0x0c,
              "APSTA SoftAP params output +0x0c offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPParamsOutputLayout, param10) == 0x10,
              "APSTA SoftAP params output +0x10 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPParamsOutputLayout, param14) == 0x14,
              "APSTA SoftAP params output +0x14 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPParamsOutputLayout, mode16) == 0x16,
              "APSTA SoftAP params output +0x16 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPParamsOutputLayout, enabled17) == 0x17,
              "APSTA SoftAP params output +0x17 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPParamsOutputLayout, param18) == 0x18,
              "APSTA SoftAP params output +0x18 offset mismatch");
static_assert(sizeof(AirportItlwmAPSTASoftAPParamsOutputLayout) == 0x19,
              "APSTA SoftAP params output witness size mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPParamsInputLayout, param04) ==
              kAirportItlwmAPSTASetSoftAPParamsInputParam04Offset,
              "APSTA SoftAP params input +0x04 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPParamsInputLayout, param08) ==
              kAirportItlwmAPSTASetSoftAPParamsInputParam08Offset,
              "APSTA SoftAP params input +0x08 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPParamsInputLayout, param0c) ==
              kAirportItlwmAPSTASetSoftAPParamsInputParam0cOffset,
              "APSTA SoftAP params input +0x0c offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPParamsInputLayout, param10) ==
              kAirportItlwmAPSTASetSoftAPParamsInputParam10Offset,
              "APSTA SoftAP params input +0x10 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPParamsInputLayout, param14) ==
              kAirportItlwmAPSTASetSoftAPParamsInputBeaconIntervalOffset,
              "APSTA SoftAP params input beacon interval offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPParamsInputLayout, enabled17) ==
              kAirportItlwmAPSTASetSoftAPParamsInputEnabledOffset,
              "APSTA SoftAP params input enabled offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPParamsInputLayout, param18) ==
              kAirportItlwmAPSTASetSoftAPParamsInputParam18Offset,
              "APSTA SoftAP params input +0x18 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPWifiNetworkInfoCarrierLayout, length03) == 0x03,
              "APSTA SoftAP Wi-Fi network info +0x03 length offset mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPWifiNetworkInfoCarrierLayout, payload04) == 0x04,
              "APSTA SoftAP Wi-Fi network info +0x04 payload offset mismatch");
static_assert(sizeof(AirportItlwmAPSTASoftAPWifiNetworkInfoCarrierLayout) ==
              kAirportItlwmAPSTAWifiNetworkInfoIESize,
              "APSTA SoftAP Wi-Fi network info carrier size mismatch");
static_assert(kAirportItlwmAPSTACoreFeatureFlagStoreOffset == 0x45a8,
              "APSTA core feature-flag store offset mismatch");
static_assert(kAirportItlwmAPSTACoreFeatureFlagByteCount == 0x10,
              "APSTA core feature-flag bitmap byte count mismatch");
static_assert(kAirportItlwmAPSTACoreFeatureFlagMaxExclusive == 0x80,
              "APSTA core feature-flag max bit mismatch");
static_assert(kAirportItlwmAPSTAWifiNetworkInfoFeatureGate46ByteIndex == 0x08,
              "APSTA Wi-Fi network info feature-gate byte index mismatch");
static_assert(kAirportItlwmAPSTAWifiNetworkInfoFeatureGate46BitMask == 0x40,
              "APSTA Wi-Fi network info feature-gate bit mask mismatch");
static_assert(kAirportItlwmAPSTAWifiNetworkInfoLengthTrapThreshold == 0x21,
              "APSTA SoftAP Wi-Fi network info length trap threshold mismatch");
static_assert(kAirportItlwmAPSTAWifiNetworkInfoMaxAcceptedLength == 0x20,
              "APSTA SoftAP Wi-Fi network info max accepted length mismatch");
static_assert(offsetof(AirportItlwmAPSTACommandPayloadHeadLayout, data) ==
              kAirportItlwmAPSTACommandPayloadDataOffset,
              "APSTA command payload data offset mismatch");
static_assert(offsetof(AirportItlwmAPSTACommandPayloadHeadLayout, length) ==
              kAirportItlwmAPSTACommandPayloadLengthOffset,
              "APSTA command payload length offset mismatch");
static_assert(sizeof(AirportItlwmAPSTAVendorIEDataLayout) ==
              kAirportItlwmAPSTAVendorIEDataAllocationSize,
              "APSTA vendor IE data carrier size mismatch");
static_assert(offsetof(AirportItlwmAPSTAVendorIEDataLayout, header00) ==
              kAirportItlwmAPSTAVendorIEDataHeader00Offset,
              "APSTA vendor IE data +0x00 header offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAVendorIEDataLayout, header08) ==
              kAirportItlwmAPSTAVendorIEDataHeader08Offset,
              "APSTA vendor IE data +0x08 header offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAVendorIEDataLayout, payloadLength10) ==
              kAirportItlwmAPSTAVendorIEDataPayloadLengthOffset,
              "APSTA vendor IE data payload length offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAVendorIEDataLayout, elementId14) ==
              kAirportItlwmAPSTAVendorIEDataElementIdOffset,
              "APSTA vendor IE data element id offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAVendorIEDataLayout, payload15) ==
              kAirportItlwmAPSTAVendorIEDataPayloadOffset,
              "APSTA vendor IE data payload offset mismatch");
static_assert(sizeof(AirportItlwmAPSTAAppleVendorIESetBufferLayout) ==
              kAirportItlwmAPSTAAppleVendorIESetBufferSize,
              "APSTA Apple vendor IE set buffer size mismatch");
static_assert(offsetof(AirportItlwmAPSTAAppleVendorIESetBufferLayout, command00) ==
              kAirportItlwmAPSTAAppleVendorIESetBufferCommandOffset,
              "APSTA Apple vendor IE command offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAAppleVendorIESetBufferLayout, header04) ==
              kAirportItlwmAPSTAAppleVendorIESetBufferHeader04Offset,
              "APSTA Apple vendor IE +0x04 header offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAAppleVendorIESetBufferLayout, body0c) ==
              kAirportItlwmAPSTAAppleVendorIESetBufferBodyOffset,
              "APSTA Apple vendor IE body offset mismatch");
static_assert(sizeof(kAirportItlwmAPSTAMaxAssocIovarName) ==
              sizeof("maxassoc"),
              "APSTA maxassoc IOVAR name mismatch");
static_assert(sizeof(kAirportItlwmAPSTAVndrIEIovarName) ==
              sizeof("vndr_ie"),
              "APSTA vndr_ie IOVAR name mismatch");
static_assert(sizeof(kAirportItlwmAPSTADeleteIPv4PktFilterIovarName) ==
              sizeof("pkt_filter_delete"),
              "APSTA pkt_filter_delete IOVAR name mismatch");
static_assert(sizeof(kAirportItlwmAPSTABcnIntervalRxPayloadLabel) ==
              sizeof("BCNPRD IOCTL rxPayload bytestream: "),
              "APSTA BCNPRD rxPayload label mismatch");
static_assert(sizeof(kAirportItlwmAPSTABcnDtimRxPayloadLabel) ==
              sizeof("DTIMPRD IOCTL rxPayload bytestream: "),
              "APSTA DTIMPRD rxPayload label mismatch");
static_assert(sizeof(kAirportItlwmAPSTAEnableAPRrmThrottleWindowIovarName) ==
              sizeof("rrm_bcn_req_thrtl_win"),
              "APSTA RRM throttle-window IOVAR name mismatch");
static_assert(sizeof(kAirportItlwmAPSTAEnableAPRrmMaxOffChannelIovarName) ==
              sizeof("rrm_bcn_req_max_off_chan_time"),
              "APSTA RRM max-off-channel IOVAR name mismatch");
static_assert(sizeof(kAirportItlwmAPSTAEnableAPWnmIovarName) ==
              sizeof("wnm"),
              "APSTA WNM IOVAR name mismatch");
static_assert(sizeof(kAirportItlwmAPSTAEnableAPMpduBootArgName) ==
              sizeof("wlan.ap.maxmpdu"),
              "APSTA MPDU boot-arg name mismatch");
static_assert(sizeof(kAirportItlwmAPSTAEnableAPScbProbeIovarName) ==
              sizeof("scb_probe"),
              "APSTA scb_probe IOVAR name mismatch");
static_assert(sizeof(kAirportItlwmAPSTAConfigureMpduIovarName) ==
              sizeof("ampdu_mpdu"),
              "APSTA ampdu_mpdu IOVAR name mismatch");
static_assert(sizeof(kAirportItlwmAPSTALowPowerModeIovarName) ==
              sizeof("lphs_mode"),
              "APSTA lphs_mode IOVAR name mismatch");
static_assert(sizeof(kAirportItlwmAPSTAArpHostIpClearIovarName) ==
              sizeof("arp_hostip_clear"),
              "APSTA arp_hostip_clear IOVAR name mismatch");
static_assert(sizeof(kAirportItlwmAPSTAArpHostIpIovarName) ==
              sizeof("arp_hostip"),
              "APSTA arp_hostip IOVAR name mismatch");
static_assert(sizeof(kAirportItlwmAPSTARpsNoaIovarName) ==
              sizeof("rpsnoa"),
              "APSTA rpsnoa IOVAR name mismatch");
static_assert(sizeof(kAirportItlwmAPSTAGetStaIEListIovarName) ==
              sizeof("wpaie"),
              "APSTA wpaie IOVAR name mismatch");
static_assert(sizeof(kAirportItlwmAPSTAGetStaStatsIovarName) ==
              sizeof("sta_info"),
              "APSTA sta_info IOVAR name mismatch");
static_assert(sizeof(kAirportItlwmAPSTASetMacAddressIovarName) ==
              sizeof("cur_etheraddr"),
              "APSTA cur_etheraddr IOVAR name mismatch");
static_assert(sizeof(kAirportItlwmAPSTASoftAPPeerStatsIovarName) ==
              sizeof("softap_stats"),
              "APSTA softap_stats IOVAR name mismatch");
static_assert(sizeof(kAirportItlwmAPSTAPowerStateBeaconWaitIovarName) ==
              sizeof("modesw_bcns_wait"),
              "APSTA modesw_bcns_wait IOVAR name mismatch");
static_assert(sizeof(kAirportItlwmAPSTAMfpIovarName) == sizeof("mfp"),
              "APSTA mfp IOVAR name mismatch");
static_assert(sizeof(AirportItlwmAPSTAScbProbePayloadLayout) ==
              kAirportItlwmAPSTAEnableAPScbProbePayloadSize,
              "APSTA scb_probe payload size mismatch");
static_assert(offsetof(AirportItlwmAPSTAScbProbePayloadLayout, header00) ==
              kAirportItlwmAPSTAEnableAPScbProbePayloadHeaderOffset,
              "APSTA scb_probe payload header offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAScbProbePayloadLayout, value08) ==
              kAirportItlwmAPSTAEnableAPScbProbePayloadValueOffset,
              "APSTA scb_probe payload value offset mismatch");
static_assert(sizeof(AirportItlwmAPSTACommandCompletionLayout) ==
              kAirportItlwmAPSTACommandCompletionSize,
              "APSTA command completion size mismatch");
static_assert(offsetof(AirportItlwmAPSTACommandCompletionLayout, owner00) ==
              kAirportItlwmAPSTACommandCompletionOwnerOffset,
              "APSTA command completion owner offset mismatch");
static_assert(offsetof(AirportItlwmAPSTACommandCompletionLayout, function08) ==
              kAirportItlwmAPSTACommandCompletionFunctionOffset,
              "APSTA command completion function offset mismatch");
static_assert(offsetof(AirportItlwmAPSTACommandCompletionLayout, cookie10) ==
              kAirportItlwmAPSTACommandCompletionCookieOffset,
              "APSTA command completion cookie offset mismatch");
static_assert(sizeof(AirportItlwmAPSTACommandRxPayloadLayout) == 0x10,
              "APSTA command RX payload witness size mismatch");
static_assert(offsetof(AirportItlwmAPSTACommandRxPayloadLayout, data) == 0x00,
              "APSTA command RX payload data offset mismatch");
static_assert(offsetof(AirportItlwmAPSTACommandRxPayloadLayout, range08) == 0x08,
              "APSTA command RX payload range offset mismatch");
static_assert(offsetof(AirportItlwmAPSTACommandRxPayloadLayout, data) ==
              kAirportItlwmAPSTAAsyncRxPayloadDataOffset,
              "APSTA async RX payload data offset mismatch");
static_assert(offsetof(AirportItlwmAPSTACommandRxPayloadLayout, range08) ==
              kAirportItlwmAPSTAAsyncRxPayloadLengthOffset,
              "APSTA async RX payload length/range offset mismatch");
static_assert(kAirportItlwmAPSTAAsyncRxPayloadTelemetryFlag == 1,
              "APSTA async RX payload telemetry flag mismatch");
static_assert(kAirportItlwmAPSTADeleteIPv4PktFilterPayloadSize ==
              kAirportItlwmAPSTABeaconPayloadSize,
              "APSTA delete IPv4 pkt filter payload size mismatch");
static_assert(kAirportItlwmAPSTADeleteIPv4PktFilterPayloadValue == 0x6c,
              "APSTA delete IPv4 pkt filter payload value mismatch");
static_assert(kAirportItlwmAPSTADeleteIPv4PktFilterCallbackLogLevel == 2,
              "APSTA delete IPv4 pkt filter callback log level mismatch");
static_assert(kAirportItlwmAPSTABeaconIntervalCallbackLogLevel == 1,
              "APSTA beacon interval callback log level mismatch");
static_assert(kAirportItlwmAPSTABeaconDtimCallbackLogLevel == 1,
              "APSTA beacon DTIM callback log level mismatch");
static_assert(sizeof(AirportItlwmAPSTARpsNoaDutyCyclePayloadLayout) ==
              kAirportItlwmAPSTARpsNoaDutyCyclePayloadSize,
              "APSTA rpsnoa duty-cycle payload size mismatch");
static_assert(offsetof(AirportItlwmAPSTARpsNoaDutyCyclePayloadLayout, zero08) ==
              kAirportItlwmAPSTARpsNoaDutyCycleZeroOffset,
              "APSTA rpsnoa duty-cycle zero offset mismatch");
static_assert(offsetof(AirportItlwmAPSTARpsNoaDutyCyclePayloadLayout, mode0c) ==
              kAirportItlwmAPSTARpsNoaDutyCycleModeOffset,
              "APSTA rpsnoa duty-cycle mode offset mismatch");
static_assert(offsetof(AirportItlwmAPSTARpsNoaDutyCyclePayloadLayout, enable0e) ==
              kAirportItlwmAPSTARpsNoaDutyCycleEnableOffset,
              "APSTA rpsnoa duty-cycle enable offset mismatch");
static_assert(sizeof(AirportItlwmAPSTARpsNoaDynamicPayloadLayout) ==
              kAirportItlwmAPSTARpsNoaDynamicPayloadSize,
              "APSTA rpsnoa dynamic payload size mismatch");
static_assert(offsetof(AirportItlwmAPSTARpsNoaDynamicPayloadLayout, zero08) ==
              kAirportItlwmAPSTARpsNoaDynamicZeroOffset,
              "APSTA rpsnoa dynamic zero offset mismatch");
static_assert(offsetof(AirportItlwmAPSTARpsNoaDynamicPayloadLayout, mode0c) ==
              kAirportItlwmAPSTARpsNoaDynamicModeOffset,
              "APSTA rpsnoa dynamic mode offset mismatch");
static_assert(offsetof(AirportItlwmAPSTARpsNoaDynamicPayloadLayout, dynamicLevel0e) ==
              kAirportItlwmAPSTARpsNoaDynamicLevelByteOffset,
              "APSTA rpsnoa dynamic level offset mismatch");
static_assert(offsetof(AirportItlwmAPSTARpsNoaDynamicPayloadLayout, reserved0f) ==
              kAirportItlwmAPSTARpsNoaDynamicReservedByteOffset,
              "APSTA rpsnoa dynamic reserved byte offset mismatch");
static_assert(offsetof(AirportItlwmAPSTARpsNoaDynamicPayloadLayout, rotatedParams10) ==
              kAirportItlwmAPSTARpsNoaDynamicRotatedParamsOffset,
              "APSTA rpsnoa dynamic rotated-params offset mismatch");
static_assert(sizeof(AirportItlwmAPSTAPowerStatsBucketLayout) ==
              kAirportItlwmAPSTAPowerStatsBucketStride,
              "APSTA power-state stats bucket stride mismatch");
static_assert(offsetof(AirportItlwmAPSTAPowerStatsBucketLayout, duration00) ==
              kAirportItlwmAPSTAPowerStatsBucketDurationOffset,
              "APSTA power-state duration offset mismatch");
static_assert(sizeof(AirportItlwmAPSTASoftAPPeerStatsPayloadLayout) ==
              kAirportItlwmAPSTASoftAPPeerStatsPayloadSize,
              "APSTA SoftAP peer-stats payload size mismatch");
static_assert(offsetof(AirportItlwmAPSTASoftAPPeerStatsPayloadLayout, mode0a) ==
              kAirportItlwmAPSTASoftAPPeerStatsModeOffset,
              "APSTA SoftAP peer-stats mode offset mismatch");
static_assert(sizeof(AirportItlwmAPSTAInactivityKillMessageLayout) == 0x0c,
              "APSTA inactivity-kill message size mismatch");
static_assert(offsetof(AirportItlwmAPSTAInactivityKillMessageLayout, zero00) ==
              0x00,
              "APSTA inactivity-kill zero offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAInactivityKillMessageLayout, sentinel08) ==
              0x08,
              "APSTA inactivity-kill sentinel offset mismatch");
static_assert(kAirportItlwmAPSTAStatsInactivityMessageSentinel == -1,
              "APSTA inactivity-kill sentinel value mismatch");
static_assert(offsetof(AirportItlwmAPSTABcmAssocListLayout, count00) ==
              kAirportItlwmAPSTABcmAssocListCountOffset,
              "APSTA BCM assoc-list count offset mismatch");
static_assert(offsetof(AirportItlwmAPSTABcmAssocListLayout, firstMac04) ==
              kAirportItlwmAPSTABcmAssocListFirstMacOffset,
              "APSTA BCM assoc-list first-MAC offset mismatch");
static_assert(kAirportItlwmAPSTABcmAssocListMacStride ==
              kAirportItlwmAPSTAStationTableMacSize,
              "APSTA BCM assoc-list MAC stride mismatch");
static_assert(sizeof(AirportItlwmAPSTAAppleAssocEntryLayout) ==
              kAirportItlwmAPSTAAssocListOutputEntryStride,
              "APSTA Apple assoc-list entry stride mismatch");
static_assert(offsetof(AirportItlwmAPSTAAppleAssocEntryLayout, valid00) ==
              kAirportItlwmAPSTAAssocListEntryValidOffset,
              "APSTA Apple assoc-list valid offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAAppleAssocEntryLayout, macDword04) ==
              kAirportItlwmAPSTAAssocListEntryMacDwordOffset,
              "APSTA Apple assoc-list MAC dword offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAAppleAssocEntryLayout, macTail08) ==
              kAirportItlwmAPSTAAssocListEntryMacTailOffset,
              "APSTA Apple assoc-list MAC tail offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAAppleAssocEntryLayout, reserved000a) ==
              kAirportItlwmAPSTAAssocListEntryReservedOffset,
              "APSTA Apple assoc-list reserved offset mismatch");
static_assert(sizeof(AirportItlwmAPSTAAppleAssocListLayout) ==
              kAirportItlwmAPSTAAssocListOutputSize,
              "APSTA Apple assoc-list output size mismatch");
static_assert(offsetof(AirportItlwmAPSTAAppleAssocListLayout, version00) == 0x00,
              "APSTA Apple assoc-list version offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAAppleAssocListLayout, count04) ==
              kAirportItlwmAPSTAAssocListOutputCountOffset,
              "APSTA Apple assoc-list count offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAAppleAssocListLayout, entries08) ==
              kAirportItlwmAPSTAAssocListOutputEntriesOffset,
              "APSTA Apple assoc-list entries offset mismatch");
static_assert(kAirportItlwmAPSTAStatsUpdateAllocSize ==
              kAirportItlwmAPSTAAssocListOutputSize,
              "APSTA stats-update allocation must match Apple assoc-list size");
static_assert(kAirportItlwmAPSTAAssocListClampThreshold ==
              kAirportItlwmAPSTAAssocListMaxCount + 1,
              "APSTA assoc-list clamp threshold mismatch");
static_assert(sizeof(AirportItlwmAPSTAPowerStateRecordLayout) ==
              kAirportItlwmAPSTAPowerStateTransitionRecordStride,
              "APSTA power-state transition record stride mismatch");
static_assert(offsetof(AirportItlwmAPSTAPowerStateRecordLayout, transitionCount00) ==
              kAirportItlwmAPSTAPowerStateTransitionCountOffset,
              "APSTA power-state transition count offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAPowerStateRecordLayout, duration08) ==
              kAirportItlwmAPSTAPowerStateTransitionDurationOffset,
              "APSTA power-state transition duration offset mismatch");
static_assert(kAirportItlwmAPSTAPowerStateBeaconWaitPayloadSize ==
              kAirportItlwmAPSTALowPowerExitPayloadSize,
              "APSTA beacon-wait payload size mismatch");
static_assert(kAirportItlwmAPSTAMfpPayloadSize ==
              kAirportItlwmAPSTAPowerStateBeaconWaitPayloadSize,
              "APSTA MFP payload size mismatch");
static_assert(sizeof(kAirportItlwmAPSTAChannelChanspecIovarName) ==
              sizeof("chanspec"),
              "APSTA chanspec IOVAR name mismatch");
static_assert(sizeof(kAirportItlwmAPSTACsaIovarName) == sizeof("csa"),
              "APSTA CSA IOVAR name mismatch");
static_assert(sizeof(AirportItlwmAPSTAChannelDataLayout) ==
              kAirportItlwmAPSTAChannelDataSize,
              "APSTA channel data witness size mismatch");
static_assert(offsetof(AirportItlwmAPSTAChannelDataLayout, channelVersion04) ==
              kAirportItlwmAPSTAChannelDataChannelOffset,
              "APSTA channel data embedded-channel offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAChannelDataLayout, channelNumber08) ==
              kAirportItlwmAPSTAChannelDataNumberOffset,
              "APSTA channel data channel-number offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAChannelDataLayout, channelFlags0c) ==
              kAirportItlwmAPSTAChannelDataFlagsOffset,
              "APSTA channel data flags offset mismatch");
static_assert(sizeof(AirportItlwmAPSTAChannelCarrierLayout) ==
              kAirportItlwmAPSTAChannelCarrierSize,
              "APSTA channel carrier size mismatch");
static_assert(offsetof(AirportItlwmAPSTAChannelCarrierLayout, channelNumber04) ==
              kAirportItlwmAPSTAChannelCarrierNumberOffset,
              "APSTA channel carrier number offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAChannelCarrierLayout, channelFlags08) ==
              kAirportItlwmAPSTAChannelCarrierFlagsOffset,
              "APSTA channel carrier flags offset mismatch");
static_assert(kAirportItlwmAPSTAGetChannelOutputNumberOffset ==
              kAirportItlwmAPSTAChannelDataNumberOffset,
              "APSTA getCHANNEL output number offset mismatch");
static_assert(kAirportItlwmAPSTAGetChannelOutputFlagsOffset ==
              kAirportItlwmAPSTAChannelDataFlagsOffset,
              "APSTA getCHANNEL output flags offset mismatch");
static_assert(kAirportItlwmAPSTASetChannelNumberOffset ==
              kAirportItlwmAPSTAChannelDataNumberOffset,
              "APSTA setCHANNEL input number offset mismatch");
static_assert(kAirportItlwmAPSTASetChannelFlagsOffset ==
              kAirportItlwmAPSTAChannelDataFlagsOffset,
              "APSTA setCHANNEL input flags offset mismatch");
static_assert(kAirportItlwmAPSTASetChannelFlag20MHz == 0x02,
              "APSTA setCHANNEL 20 MHz flag mismatch");
static_assert(kAirportItlwmAPSTASetChannelFlag40MHz == 0x04,
              "APSTA setCHANNEL 40 MHz flag mismatch");
static_assert(kAirportItlwmAPSTASetChannelFlag80MHz == 0x400,
              "APSTA setCHANNEL 80 MHz flag mismatch");
static_assert(kAirportItlwmAPSTASetChannelLocalCsaTriggerCount == 0,
              "APSTA setCHANNEL must not trigger CSA locally");
static_assert(kAirportItlwmAPSTASetChannelNoOwnerRoutesPrimary == 1,
              "APSTA setCHANNEL no-owner route must preserve primary channel setter");
static_assert(sizeof(AirportItlwmAPSTACsaInputLayout) == 0x15,
              "APSTA CSA input witness size mismatch");
static_assert(offsetof(AirportItlwmAPSTACsaInputLayout, channel04) ==
              kAirportItlwmAPSTACsaInputChannelOffset,
              "APSTA CSA input channel offset mismatch");
static_assert(offsetof(AirportItlwmAPSTACsaInputLayout, mode10) ==
              kAirportItlwmAPSTACsaInputModeOffset,
              "APSTA CSA input mode offset mismatch");
static_assert(offsetof(AirportItlwmAPSTACsaInputLayout, featureGate14) ==
              kAirportItlwmAPSTACsaInputFeatureGateOffset,
              "APSTA CSA input feature-gate offset mismatch");
static_assert(sizeof(AirportItlwmAPSTACsaIovarPayloadLayout) ==
              kAirportItlwmAPSTACsaPayloadSize,
              "APSTA CSA IOVAR payload size mismatch");
static_assert(offsetof(AirportItlwmAPSTACsaIovarPayloadLayout, zero00) ==
              kAirportItlwmAPSTACsaPayloadZero0Offset,
              "APSTA CSA payload zero offset mismatch");
static_assert(offsetof(AirportItlwmAPSTACsaIovarPayloadLayout, mode01) ==
              kAirportItlwmAPSTACsaPayloadModeOffset,
              "APSTA CSA payload mode offset mismatch");
static_assert(offsetof(AirportItlwmAPSTACsaIovarPayloadLayout, chanspec02) ==
              kAirportItlwmAPSTACsaPayloadChanspecOffset,
              "APSTA CSA payload chanspec offset mismatch");
static_assert(offsetof(AirportItlwmAPSTACsaIovarPayloadLayout, reserved04) ==
              kAirportItlwmAPSTACsaPayloadReservedOffset,
              "APSTA CSA payload reserved offset mismatch");
static_assert(kAirportItlwmAPSTACsaMaximumExcludedChannelSpec == 0x10000,
              "APSTA CSA accepts parsed chanspec values below 0x10000 only");
static_assert(kAirportItlwmAPSTACsaMinimumPrimaryChannel == 1,
              "APSTA CSA helper rejects primary channel zero");
static_assert(kAirportItlwmAPSTACsaMaximumExcludedPrimaryChannel == 0x100,
              "APSTA CSA helper accepts primary channels below 0x100 only");
static_assert(kAirportItlwmAPSTASetMaxAssocNoLocalClamp == 1,
              "APSTA setMaxAssoc must not clamp requested count locally");
static_assert(kAirportItlwmAPSTASetMaxAssocPayloadAddsAssociatedCount == 1,
              "APSTA setMaxAssoc payload adds associated count");
static_assert(sizeof(AirportItlwmAPSTAStaAuthorizeInputLayout) ==
              kAirportItlwmAPSTAStaAuthorizeMacOffset +
              kAirportItlwmAPSTAStaAuthorizeMacPayloadSize,
              "APSTA STA authorize input witness size mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaAuthorizeInputLayout, authorizeFlag04) ==
              kAirportItlwmAPSTAStaAuthorizeFlagOffset,
              "APSTA STA authorize flag offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaAuthorizeInputLayout, mac08) ==
              kAirportItlwmAPSTAStaAuthorizeMacOffset,
              "APSTA STA authorize MAC offset mismatch");
static_assert(kAirportItlwmAPSTAStaAuthorizeSelectorIfAuthorized == 0x79,
              "APSTA STA authorize selector mismatch");
static_assert(kAirportItlwmAPSTAStaAuthorizeSelectorIfNotAuthorized == 0x7a,
              "APSTA STA deauthorize selector mismatch");
static_assert(sizeof(AirportItlwmAPSTAStaDisassocInputLayout) == 0x0e,
              "APSTA STA disassociate input witness size mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaDisassocInputLayout, reason04) ==
              kAirportItlwmAPSTAStaDisassocReasonOffset,
              "APSTA STA disassociate reason offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaDisassocInputLayout, value08) ==
              kAirportItlwmAPSTAStaDisassocValue08Offset,
              "APSTA STA disassociate +0x08 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaDisassocInputLayout, value0c) ==
              kAirportItlwmAPSTAStaDisassocValue0cOffset,
              "APSTA STA disassociate +0x0c offset mismatch");
static_assert(sizeof(AirportItlwmAPSTAStaDisassocPayloadLayout) ==
              kAirportItlwmAPSTAStaDisassocTxPayloadSize,
              "APSTA STA disassociate payload size mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaDisassocPayloadLayout, reason00) ==
              kAirportItlwmAPSTAStaDisassocPayloadReasonOffset,
              "APSTA STA disassociate payload reason offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaDisassocPayloadLayout, value04) ==
              kAirportItlwmAPSTAStaDisassocPayloadValue04Offset,
              "APSTA STA disassociate payload +0x04 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaDisassocPayloadLayout, value08) ==
              kAirportItlwmAPSTAStaDisassocPayloadValue08Offset,
              "APSTA STA disassociate payload +0x08 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStaDisassocPayloadLayout, sentinel0a) ==
              kAirportItlwmAPSTAStaDisassocPayloadSentinel0aOffset,
              "APSTA STA disassociate payload sentinel offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAHostApModeNetworkDataLayout, flags04) ==
              kAirportItlwmAPSTAHostApModeNetworkDataFlagsOffset,
              "APSTA HostAP network-data flags offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAHostApModeNetworkDataLayout, flags04) ==
              kAirportItlwmAPSTAHostApNetworkDataFlagsOffset,
              "APSTA HostAP success-tail flags offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAHostApModeNetworkDataLayout, ssidLength1c) ==
              kAirportItlwmAPSTAHostApModeNetworkDataSsidLengthOffset,
              "APSTA HostAP network-data SSID length offset mismatch");
static_assert(kAirportItlwmAPSTAHostApModeSsidLengthMaxAccepted ==
              kAirportItlwmAPSTAGetSsidMaxLength,
              "APSTA HostAP accepted SSID length must match cached SSID capacity");
static_assert(offsetof(AirportItlwmAPSTAHostApModeNetworkDataLayout, ssid20) ==
              kAirportItlwmAPSTAHostApModeNetworkDataSsidBytesOffset,
              "APSTA HostAP network-data SSID bytes offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAHostApModeNetworkDataLayout, vendorIELength2dc) ==
              kAirportItlwmAPSTAHostApModeNetworkDataVendorIELengthOffset,
              "APSTA HostAP network-data vendor IE length offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAHostApModeNetworkDataLayout, vendorIELength2dc) ==
              kAirportItlwmAPSTAHostApVendorIEListLengthOffset,
              "APSTA HostAP vendor IE length alias mismatch");
static_assert(offsetof(AirportItlwmAPSTAHostApModeNetworkDataLayout, vendorIEData2e0) ==
              kAirportItlwmAPSTAHostApModeNetworkDataVendorIEDataOffset,
              "APSTA HostAP network-data vendor IE data offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAHostApModeNetworkDataLayout, vendorIEData2e0) ==
              kAirportItlwmAPSTAHostApVendorIEListDataOffset,
              "APSTA HostAP vendor IE data alias mismatch");
static_assert(kAirportItlwmAPSTAEnableAPFinalVtableOffset ==
              kAirportItlwmAPSTAHostApMisMaxStaVtableOffset,
              "APSTA enableAP final selector vtable must share HostAP selector dispatch");
static_assert(offsetof(AirportItlwmAPSTAObjectStorageLayout, state) == 0x130,
              "APSTA state pointer must be at object offset +0x130");
static_assert(sizeof(AirportItlwmAPSTAObjectStorageLayout) == 0x138,
              "APSTA object storage witness must match recovered object size");
static_assert(offsetof(AirportItlwmAPSTACoreExpansionStorageLayout, proximityOwner) ==
              kAirportItlwmAPSTACoreExpansionProximityOwnerOffset,
              "APSTA core expansion proximity-owner offset mismatch");
static_assert(offsetof(AirportItlwmAPSTACoreExpansionStorageLayout, apstaOwner) ==
              kAirportItlwmAPSTACoreExpansionOwnerOffset,
              "APSTA core expansion owner offset mismatch");
static_assert(offsetof(AirportItlwmAPSTACoreExpansionStorageLayout, nanOwner) ==
              kAirportItlwmAPSTAHostApModeNanOwnerOffset,
              "APSTA core expansion NAN owner offset mismatch");
static_assert(offsetof(AirportItlwmAPSTACoreExpansionStorageLayout, nanDataOwner) ==
              kAirportItlwmAPSTAHostApModeNanDataOwnerOffset,
              "APSTA core expansion NAN data owner offset mismatch");

static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapAssociatedStaCount00) ==
              kAirportItlwmAPSTAResetScalar00Offset,
              "APSTA state +0x00 reset scalar offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapAssociatedStaCount00) ==
              kAirportItlwmAPSTASetMaxAssocAssociatedCountOffset,
              "APSTA setMaxAssoc associated-count offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapMaxAssoc04) ==
              kAirportItlwmAPSTASetMaxAssocRequestedOffset,
              "APSTA setMaxAssoc requested offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapMaxAssocLimit08) ==
              kAirportItlwmAPSTASetMaxAssocLimitOffset,
              "APSTA setMaxAssoc limit offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapParam0e) == 0x0e,
              "APSTA state +0x0e offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapParam0e) ==
              kAirportItlwmAPSTAHiddenRuntime0eClearOffset,
              "APSTA hidden-mode +0x0e clear offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapParam0e) ==
              kAirportItlwmAPSTASetSoftAPParamsStateEnabledFlagOffset,
              "APSTA SoftAP params enabled flag offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, powerAssertionFlag0c) == 0x0c,
              "APSTA state +0x0c offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, powerAssertionFlag0c) ==
              kAirportItlwmAPSTAHoldPowerAssertionStateOffset,
              "APSTA hold power assertion flag offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, powerAssertionFlag0c) ==
              kAirportItlwmAPSTAReleasePowerAssertionStateOffset,
              "APSTA release power assertion flag offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, hiddenNetworkFlag0d) == 0x0d,
              "APSTA state +0x0d offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, hiddenNetworkFlag0d) ==
              kAirportItlwmAPSTAHiddenClosedNetStateOffset,
              "APSTA hidden-mode closednet state offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapMode10) == 0x10,
              "APSTA state +0x10 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapMode10) ==
              kAirportItlwmAPSTAPowerStateCurrentOffset,
              "APSTA power-state current offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapBeaconInterval14) == 0x14,
              "APSTA state +0x14 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapDtimPeriod16) ==
              kAirportItlwmAPSTAInitSoftAPDtimPeriodOffset,
              "APSTA state +0x16 DTIM period offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapParam18) == 0x18,
              "APSTA state +0x18 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapParam18) ==
              kAirportItlwmAPSTASetSoftAPParamsStateParam18Offset,
              "APSTA setSOFTAP_PARAMS state +0x18 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapParam1c) == 0x1c,
              "APSTA state +0x1c offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapParam1c) ==
              kAirportItlwmAPSTASetSoftAPParamsStateParam1cOffset,
              "APSTA setSOFTAP_PARAMS state +0x1c offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapParam20) == 0x20,
              "APSTA state +0x20 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapParam20) ==
              kAirportItlwmAPSTASetSoftAPParamsStateParam20Offset,
              "APSTA setSOFTAP_PARAMS state +0x20 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapParam24) == 0x24,
              "APSTA state +0x24 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapParam24) ==
              kAirportItlwmAPSTASetSoftAPParamsStateParam24Offset,
              "APSTA setSOFTAP_PARAMS state +0x24 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapParam28) == 0x28,
              "APSTA state +0x28 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapParam28) ==
              kAirportItlwmAPSTASetSoftAPParamsStateParam28Offset,
              "APSTA setSOFTAP_PARAMS state +0x28 offset mismatch");
static_assert(sizeof(((AirportItlwmAPSTAStateBlock *)0)->softapParam28) ==
              kAirportItlwmAPSTASetSoftAPParamsStateParam28Size,
              "APSTA setSOFTAP_PARAMS state +0x28 dword size mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapWifiNetworkInfoIE) == 0x2c,
              "APSTA state +0x2c offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapWifiNetworkInfoIE) ==
              kAirportItlwmAPSTAAppleVendorIEExtBaseOffset,
              "APSTA Apple vendor IE extension base offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapWifiNetworkInfoIE) + 0x02 ==
              kAirportItlwmAPSTAAppleVendorIEExtField2eOffset,
              "APSTA Apple vendor IE extension +0x2e field offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapWifiNetworkInfoIE) + 0x03 ==
              kAirportItlwmAPSTAAppleVendorIEExtLengthOffset,
              "APSTA Apple vendor IE extension length offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapWifiNetworkInfoIE) + 0x04 ==
              kAirportItlwmAPSTAAppleVendorIEExtPayloadOffset,
              "APSTA Apple vendor IE extension payload offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapAppleVendorIEExtra50) ==
              kAirportItlwmAPSTAAppleVendorIEExtExtraFlagOffset,
              "APSTA Apple vendor IE extension extra flag offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapAppleVendorIEExtra50) ==
              kAirportItlwmAPSTASetSoftAPExtCapsStateFlag50Offset,
              "APSTA SoftAP ext-cap state flag offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapAppleVendorIEExtra50) ==
              kAirportItlwmAPSTASetSoftAPExtCapsStateClear50Offset,
              "APSTA SoftAP ext-cap clear +0x50 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapAppleVendorIETail51) ==
              kAirportItlwmAPSTAAppleVendorIEExtTail51Offset,
              "APSTA Apple vendor IE extension +0x51 tail offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapAppleVendorIETail51) ==
              kAirportItlwmAPSTASetSoftAPExtCapsStateTail51Offset,
              "APSTA SoftAP ext-cap state +0x51 tail offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapAppleVendorIETail59) ==
              kAirportItlwmAPSTAAppleVendorIEExtTail59Offset,
              "APSTA Apple vendor IE extension +0x59 tail offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapAppleVendorIETail59) ==
              kAirportItlwmAPSTASetSoftAPExtCapsStateTail59Offset,
              "APSTA SoftAP ext-cap state +0x59 tail offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, reserved0061) ==
              kAirportItlwmAPSTASetSoftAPExtCapsStateClear61Offset,
              "APSTA SoftAP ext-cap clear byte +0x61 offset mismatch");
static_assert(kAirportItlwmAPSTASetSoftAPExtCapsStateClear58Offset == 0x58,
              "APSTA SoftAP ext-cap clear qword +0x58 offset mismatch");
static_assert(kAirportItlwmAPSTASetSoftAPExtCapsStateClear60Offset == 0x60,
              "APSTA SoftAP ext-cap clear word +0x60 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, lphsVendorIEFlag62) ==
              kAirportItlwmAPSTAMonitorLphsVendorIeFlagOffset,
              "APSTA monitor LPHS vendor-IE flag offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, lowTrafficCounter64) ==
              kAirportItlwmAPSTAMonitorLowTrafficCounterOffset,
              "APSTA monitor low-traffic counter offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapAppliedBeaconInterval68) == 0x68,
              "APSTA state +0x68 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapAppliedBeaconInterval68) ==
              kAirportItlwmAPSTABeaconIntervalAppliedOffset,
              "APSTA applied beacon interval offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapAppliedBeaconInterval68) ==
              kAirportItlwmAPSTASetSoftAPParamsStateAppliedBeaconIntervalOffset,
              "APSTA SoftAP params applied beacon interval offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapAppliedDtimPeriod6a) ==
              kAirportItlwmAPSTAInitSoftAPAppliedDtimPeriodOffset,
              "APSTA state +0x6a applied DTIM period offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapAppliedDtimPeriod6a) ==
              kAirportItlwmAPSTABeaconDtimAppliedOffset,
              "APSTA applied DTIM period offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, apStatsTimerSource) == 0x70,
              "APSTA state +0x70 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, apStatsTimerSource) ==
              kAirportItlwmAPSTAHostApStatsTimerOffset,
              "APSTA HostAP stats timer offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, apStatsTimerSource) ==
              kAirportItlwmAPSTAFreeAPStatsTimerOffset,
              "APSTA freeResources AP stats timer offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, apMonitorTimerSource) == 0x78,
              "APSTA state +0x78 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, apMonitorTimerSource) ==
              kAirportItlwmAPSTAHostApMonitorTimerOffset,
              "APSTA HostAP monitor timer offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, apMonitorTimerSource) ==
              kAirportItlwmAPSTAFreeAPMonitorTimerOffset,
              "APSTA freeResources AP monitor timer offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapEvent80) == 0x80,
              "APSTA state +0x80 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapEvent80) ==
              kAirportItlwmAPSTAEventStateDwordOffset,
              "APSTA event metadata dword offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapEvent84) == 0x84,
              "APSTA state +0x84 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapEvent84) ==
              kAirportItlwmAPSTAEventStateWordOffset,
              "APSTA event metadata word offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapRuntime88) == 0x88,
              "APSTA state +0x88 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapRuntime88) ==
              kAirportItlwmAPSTAHostApRuntime88Offset,
              "APSTA HostAP runtime +0x88 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapRuntime88) ==
              kAirportItlwmAPSTAStatsUpdateActivityBaselineOffset,
              "APSTA stats-update activity baseline offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapRuntime90) ==
              kAirportItlwmAPSTASoftAPRuntime90Offset,
              "APSTA state +0x90 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapRuntime90) ==
              kAirportItlwmAPSTAMonitorFirmwareRxBaselineOffset,
              "APSTA monitor firmware-RX baseline offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapRuntime98) ==
              kAirportItlwmAPSTASoftAPRuntime98Offset,
              "APSTA state +0x98 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapRuntime98) ==
              kAirportItlwmAPSTAMonitorInterfaceRxBaselineOffset,
              "APSTA monitor interface-RX baseline offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapRuntimeA0) ==
              kAirportItlwmAPSTASoftAPRuntimeA0Offset,
              "APSTA state +0xa0 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapRuntimeA0) ==
              kAirportItlwmAPSTAMonitorFirmwareRxAccumulatorOffset,
              "APSTA monitor firmware-RX accumulator offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapRuntimeA0) ==
              kAirportItlwmAPSTAUpdateRxCounterStateOffset,
              "APSTA updateRxCounter state offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, arpHostIpAc) ==
              kAirportItlwmAPSTAArpHostIpStateOffset,
              "APSTA ARP host IP state offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapRuntimeB0) == 0xb0,
              "APSTA state +0xb0 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapRuntimeB0) ==
              kAirportItlwmAPSTASoftAPRuntimeB0Offset,
              "APSTA reset +0xb0 runtime offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapPowerStateB4) == 0xb4,
              "APSTA state +0xb4 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapPowerStateB4) ==
              kAirportItlwmAPSTALowPowerExitStateOffset,
              "APSTA low-power exit state offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapStaTableB8) == 0xb8,
              "APSTA state +0xb8 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapStaTableB8) ==
              kAirportItlwmAPSTASoftAPRuntimeBlockOffset,
              "APSTA SoftAP runtime block offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapStaTableB8) ==
              kAirportItlwmAPSTAStationTableBaseOffset,
              "APSTA station table base offset mismatch");
static_assert(kAirportItlwmAPSTAStationTableFirstEntryOffset ==
              offsetof(AirportItlwmAPSTAStateBlock, softapStaTableB8) +
              offsetof(AirportItlwmAPSTAStationTableEntryLayout, mac01),
              "APSTA station table first entry offset mismatch");
static_assert(kAirportItlwmAPSTAStationTableEndOffset ==
              kAirportItlwmAPSTAStationTableFirstEntryOffset +
              (kAirportItlwmAPSTAStationTableEntryCount *
               kAirportItlwmAPSTAStationTableEntryStride),
              "APSTA station table end offset mismatch");
static_assert(sizeof(((AirportItlwmAPSTAStateBlock *)0)->softapStaTableB8) ==
              kAirportItlwmAPSTASoftAPRuntimeBlockClearSize,
              "APSTA station table total clear size mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapRuntime1a8) == 0x1a8,
              "APSTA state +0x1a8 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapRuntime1a8) ==
              kAirportItlwmAPSTASoftAPRuntime1a8Offset,
              "APSTA SoftAP runtime +0x1a8 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapRuntime1a8) ==
              kAirportItlwmAPSTAPowerStatsLastTimestampOffset,
              "APSTA power stats last timestamp offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapStats) == 0x1b0,
              "APSTA state +0x1b0 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapStats) ==
              kAirportItlwmAPSTASoftAPStatsOffset,
              "APSTA SoftAP stats offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapStats) ==
              kAirportItlwmAPSTAGetSoftAPStatsStateOffset,
              "APSTA getSOFTAP_STATS source offset mismatch");
static_assert(kAirportItlwmAPSTASoftAPStatsSize == 0x58,
              "APSTA SoftAP stats clear size mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapStats) + 0x08 ==
              kAirportItlwmAPSTAMonitorSoftAPTxDeltaStatsOffset,
              "APSTA monitor SoftAP TX-delta stats offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapStats) + 0x10 ==
              kAirportItlwmAPSTAMonitorSoftAPRxDeltaStatsOffset,
              "APSTA monitor SoftAP RX-delta stats offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapStats) + 0x18 ==
              kAirportItlwmAPSTAPowerStateTransitionRecordBaseOffset,
              "APSTA power-state transition record base offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapStats) + 0x20 ==
              kAirportItlwmAPSTAPowerStatsBucketsOffset,
              "APSTA power-state duration bucket base offset mismatch");
static_assert(kAirportItlwmAPSTAPowerStateTransitionRecordBaseOffset +
              kAirportItlwmAPSTAPowerStateTransitionDurationOffset ==
              kAirportItlwmAPSTAPowerStatsBucketsOffset,
              "APSTA transition duration aliases power-stats bucket base");
static_assert(kAirportItlwmAPSTAGetSoftAPStatsCopySize ==
              kAirportItlwmAPSTASoftAPStatsSize,
              "APSTA getSOFTAP_STATS copy size mismatch");
static_assert(kAirportItlwmAPSTASoftAPRuntimeBlockClearSize == 0xf0,
              "APSTA SoftAP runtime block clear size mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapConcurrencyMirror208) ==
              kAirportItlwmAPSTAMonitorCorePrivateMirrorOffset,
              "APSTA monitor concurrency mirror offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapStatsAge20c) == 0x20c,
              "APSTA state +0x20c offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapStatsAge20c) ==
              kAirportItlwmAPSTAHostApStatsAgeOffset,
              "APSTA HostAP stats age offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resource210) == 0x210,
              "APSTA state +0x210 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resource210) ==
              kAirportItlwmAPSTAGetLoggerStateOffset,
              "APSTA getLogger state offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resource210) ==
              kAirportItlwmAPSTASetCipherKeyResource210Offset,
              "APSTA setCIPHER_KEY bytestream resource offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, ownerCoreOrInterface) == 0x218,
              "APSTA state +0x218 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, ownerCoreOrInterface) ==
              kAirportItlwmAPSTASetPeerCacheControlCoreOffset,
              "APSTA peer-cache control core offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resource228) == 0x228,
              "APSTA state +0x228 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resource240) == 0x240,
              "APSTA state +0x240 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resource240) ==
              kAirportItlwmAPSTAFreeResource240Offset,
              "APSTA freeResources +0x240 resource offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resource248) == 0x248,
              "APSTA state +0x248 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resource248) ==
              kAirportItlwmAPSTAFreeResource248Offset,
              "APSTA freeResources +0x248 resource offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resource250) == 0x250,
              "APSTA state +0x250 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resource250) ==
              kAirportItlwmAPSTAFreeResource250Offset,
              "APSTA freeResources +0x250 resource offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resource258) == 0x258,
              "APSTA state +0x258 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resource258) ==
              kAirportItlwmAPSTAFreeResource258Offset,
              "APSTA freeResources +0x258 resource offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resource260) == 0x260,
              "APSTA state +0x260 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resource260) ==
              kAirportItlwmAPSTAFreeResource260Offset,
              "APSTA freeResources +0x260 resource offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, initState268) == 0x268,
              "APSTA state +0x268 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resetState26c) == 0x26c,
              "APSTA state +0x26c offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resetState26c) ==
              kAirportItlwmAPSTAHostApStateOffset,
              "APSTA HostAP state offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resetState26c) ==
              kAirportItlwmAPSTAResetState26cOffset,
              "APSTA reset state +0x26c offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resetState26c) ==
              kAirportItlwmAPSTAHiddenAPRequiredStateOffset,
              "APSTA hidden-mode AP state offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resetState26c) ==
              kAirportItlwmAPSTACsaRequiredStateOffset,
              "APSTA CSA AP state offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resetState26c) ==
              kAirportItlwmAPSTAGetOpModeStateAPUpOffset,
              "APSTA getOP_MODE AP-up state source offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resetState26c) ==
              kAirportItlwmAPSTASetSoftAPParamsStateAPUpOffset,
              "APSTA setSOFTAP_PARAMS AP-up state offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resetState26c) ==
              kAirportItlwmAPSTASetMisMaxStaRequiredStateOffset,
              "APSTA setMIS_MAX_STA AP-up state offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resetState26c) ==
              kAirportItlwmAPSTAGetStationListRequiredStateOffset,
              "APSTA getSTATION_LIST AP-up state offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resetState26c) ==
              kAirportItlwmAPSTAGetStaStatsRequiredStateOffset,
              "APSTA getSTA_STATS AP-up state offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resetState26c) ==
              kAirportItlwmAPSTASetCipherKeyRequiredStateOffset,
              "APSTA setCIPHER_KEY AP-up state offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, hostApTransitionState270) == 0x270,
              "APSTA state +0x270 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapSsidLength274) ==
              kAirportItlwmAPSTAGetSsidStateLengthOffset,
              "APSTA getSSID state length offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapSsid278) ==
              kAirportItlwmAPSTAGetSsidStateBytesOffset,
              "APSTA getSSID state bytes offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, rsnConfGate29b) == 0x29b,
              "APSTA state +0x29b offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, numTxQueues) == 0x2a4,
              "APSTA state +0x2a4 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, numTxQueues) ==
              kAirportItlwmAPSTAGetNumTxQueuesStateOffset,
              "APSTA getNumTxQueues offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, txQueueServiceClassMap) == 0x2a8,
              "APSTA state +0x2a8 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, acToTxQueueIndex) == 0x2b8,
              "APSTA state +0x2b8 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, acToTxQueueIndex) ==
              kAirportItlwmAPSTAGetTxSubQueueMapOffset,
              "APSTA getTxSubQueue AC map offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, busSkywalkProvider2c8) == 0x2c8,
              "APSTA state +0x2c8 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, datapathOwner2d0) == 0x2d0,
              "APSTA state +0x2d0 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, txPacketPool) == 0x2d8,
              "APSTA state +0x2d8 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, txPacketPool) ==
              kAirportItlwmAPSTAGetTxPacketPoolStateOffset,
              "APSTA getTxPacketPool offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, rxPacketPool) == 0x2e0,
              "APSTA state +0x2e0 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, rxPacketPool) ==
              kAirportItlwmAPSTAGetRxPacketPoolStateOffset,
              "APSTA getRxPacketPool offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, txCompQueue) == 0x2e8,
              "APSTA state +0x2e8 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, txCompQueue) ==
              kAirportItlwmAPSTAGetTxCompQueueStateOffset,
              "APSTA getTxCompQueue offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, txCompQueue) ==
              kAirportItlwmAPSTAStopTxCompletionQueueOffset,
              "APSTA stop TX completion queue offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, rxCompQueue) == 0x2f0,
              "APSTA state +0x2f0 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, rxCompQueue) ==
              kAirportItlwmAPSTAGetRxCompQueueStateOffset,
              "APSTA getRxCompQueue offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, rxCompQueue) ==
              kAirportItlwmAPSTAStopRxCompletionQueueOffset,
              "APSTA stop RX completion queue offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resource2f8) == 0x2f8,
              "APSTA state +0x2f8 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, txSubQueues) == 0x300,
              "APSTA state +0x300 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, txSubQueues) ==
              kAirportItlwmAPSTAForwardPacketTxSubQueueBaseOffset,
              "APSTA forwardPacket TX subqueue base offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, txSubQueues) ==
              kAirportItlwmAPSTAGetTxSubQueueBaseOffset,
              "APSTA getTxSubQueue base offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, txSubQueues) ==
              kAirportItlwmAPSTAStopTxSubQueueBaseOffset,
              "APSTA stop TX subqueue base offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, multiCastQueue) == 0x320,
              "APSTA state +0x320 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, multiCastQueue) ==
              kAirportItlwmAPSTAGetMultiCastQueueStateOffset,
              "APSTA getMultiCastQueue offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, multiCastQueue) ==
              kAirportItlwmAPSTAStopMulticastQueueOffset,
              "APSTA stop multicast queue offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, softapPeerStatsEnabled328) ==
              kAirportItlwmAPSTASoftAPPeerStatsStateOffset,
              "APSTA SoftAP peer-stats enabled state offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resetFlag329) == 0x329,
              "APSTA state +0x329 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resetFlag329) ==
              kAirportItlwmAPSTAResetFlag329Offset,
              "APSTA reset flag +0x329 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, resetFlag329) ==
              kAirportItlwmAPSTACsaResetFlagOffset,
              "APSTA CSA reset/link flag offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, featureGate0d) == 0x32a,
              "APSTA state +0x32a offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, featureGate0c) == 0x32b,
              "APSTA state +0x32b offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStateBlock, workQueue330) == 0x330,
              "APSTA state +0x330 offset mismatch");
static_assert(sizeof(AirportItlwmAPSTAStateBlock) == 0x338,
              "APSTA state block witness must match recovered allocation size");

static_assert(offsetof(AirportItlwmAPSTAStartQueueConfigLayout, numTxQueues) == 0x08,
              "APSTA start queue config +0x08 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStartQueueConfigLayout, txQueueServiceClassMap) == 0x10,
              "APSTA start queue config +0x10 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStartQueueConfigLayout, txSubQueues) == 0x18,
              "APSTA start queue config +0x18 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStartQueueConfigLayout, resource2f8Storage) == 0x20,
              "APSTA start queue config +0x20 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStartQueueConfigLayout, txCompQueueStorage) == 0x28,
              "APSTA start queue config +0x28 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStartQueueConfigLayout, rxCompQueueStorage) == 0x30,
              "APSTA start queue config +0x30 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStartQueueConfigLayout, multiCastQueueStorage) == 0x38,
              "APSTA start queue config +0x38 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStartQueueConfigLayout, txPacketPoolStorage) == 0x40,
              "APSTA start queue config +0x40 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStartQueueConfigLayout, rxPacketPoolStorage) == 0x48,
              "APSTA start queue config +0x48 offset mismatch");
static_assert(offsetof(AirportItlwmAPSTAStartQueueConfigLayout, logger) == 0x58,
              "APSTA start queue config +0x58 offset mismatch");
static_assert(sizeof(AirportItlwmAPSTAStartQueueConfigLayout) == 0x60,
              "APSTA start queue config size mismatch");
static_assert(kAirportItlwmAPSTARegisterQueueCount == 6,
              "APSTA register queue count must match recovered four-TX plus two-completion topology");
static_assert(offsetof(AirportItlwmAPSTARegisterQueueListLayout, txCompQueue) == 0x20,
              "APSTA register queue list TX completion offset mismatch");
static_assert(offsetof(AirportItlwmAPSTARegisterQueueListLayout, rxCompQueue) == 0x28,
              "APSTA register queue list RX completion offset mismatch");
static_assert(sizeof(AirportItlwmAPSTARegisterQueueListLayout) == 0x30,
              "APSTA register queue list size mismatch");
static_assert(kAirportItlwmAPSTAConfigureMpduCorePrivateCountOffset ==
              kAirportItlwmAPSTAGetStaStatsCorePrivateCountOffset,
              "APSTA core-private +0x30c count offset alias mismatch");
static_assert(kAirportItlwmAPSTALowPowerExitPayloadSize ==
              kAirportItlwmAPSTAArpHostIpPayloadSize,
              "APSTA low-power and ARP host-IP payload sizes must remain four bytes");
static_assert(kAirportItlwmAPSTAReleasePowerAssertionCoreResourceOffset ==
              kAirportItlwmAPSTAHoldPowerAssertionCoreResourceOffset,
              "APSTA hold/release power assertion core resource offset mismatch");
static_assert(kAirportItlwmAPSTAReleasePowerAssertionEventId ==
              kAirportItlwmAPSTAHoldPowerAssertionEventId,
              "APSTA hold/release power assertion event id mismatch");
static_assert(kAirportItlwmAPSTADatapathNotEnabledReturn ==
              kAirportItlwmAPSTADatapathMissingQueueReturn,
              "APSTA datapath not-enabled return must match missing-queue return");
static_assert(kAirportItlwmAPSTASetMacAddressRequiredDownStateOffset ==
              kAirportItlwmAPSTAHostApStateOffset,
              "APSTA setMacAddress AP-up gate offset mismatch");
static_assert(kAirportItlwmAPSTAMonitorTimerInterval ==
              kAirportItlwmAPSTAHostApMonitorTimerInterval,
              "APSTA monitor timer interval alias mismatch");
static_assert(kAirportItlwmAPSTAMonitorTimerScheduleVtableOffset ==
              kAirportItlwmAPSTAHostApMonitorTimerScheduleVtableOffset,
              "APSTA monitor timer schedule slot alias mismatch");
static_assert(kAirportItlwmAPSTAStatsTimerScheduleVtableOffset ==
              kAirportItlwmAPSTAHostApMonitorTimerScheduleVtableOffset,
              "APSTA stats timer schedule slot alias mismatch");
static_assert(kAirportItlwmAPSTAStatsUpdatePollInterval !=
              kAirportItlwmAPSTAMonitorTimerInterval,
              "APSTA stats and monitor timers must remain distinct");
static_assert(kAirportItlwmAPSTAStatsUpdateAsyncSubmitFailure ==
              kAirportItlwmAPSTAGetStationListAsyncSubmitFailureReturn,
              "APSTA stats-update async submit failure return mismatch");
static_assert(kAirportItlwmAPSTAMonitorCorePrivateByteOffset ==
              kAirportItlwmAPSTAConcurrencyCorePrivateByteOffset,
              "APSTA monitor core-private byte alias mismatch");
static_assert(kAirportItlwmAPSTAMonitorCounterFeatureMask ==
              (kAirportItlwmAPSTAMonitorNanBit |
               kAirportItlwmAPSTAMonitorIrBit),
              "APSTA monitor counter feature mask mismatch");
static_assert(kAirportItlwmAPSTAPowerStateReasonReset ==
              kAirportItlwmAPSTAResetPowerSaveReason,
              "APSTA power-state reset reason mismatch");
static_assert(kAirportItlwmAPSTAPowerStateReasonPowerOff ==
              kAirportItlwmAPSTAHostApPowerOffPowerSaveReason,
              "APSTA power-state power-off reason mismatch");
static_assert(kAirportItlwmAPSTAMfpUnsupportedReturn == 0,
              "APSTA MFP unsupported return mismatch");

#ifdef __cplusplus
/*
 * Runtime role-7 lifetime is owned by AirportItlwmAPSTAOwner. This
 * header intentionally keeps only the recovered APSTA layout, ABI,
 * return-code, queue/datapath, and public SAP witnesses.
 */
static_assert(kAirportItlwmAPSTAVirtualIfBsdNameSize == 0x10,
              "APSTA bsd_name buffer must match recovered carrier limit");
static_assert(kAirportItlwmAPSTAStationTableEntryCount > 0,
              "APSTA station table must have at least one slot");
static_assert(kAirportItlwmAPSTAStationTableEntryStride > 0,
              "APSTA station table stride must be positive");

#endif /* __cplusplus */

#endif /* AirportItlwmAPSTAInterface_hpp */
