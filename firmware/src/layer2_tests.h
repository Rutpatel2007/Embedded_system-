#ifndef LAYER2_TESTS_H
#define LAYER2_TESTS_H

#include <Arduino.h>
#include "layer2_fingerprint.h"

// Independent Layer 2 Deterministic Software Verification Test Suite
class Layer2TestSuite {
public:
    static bool runAllTests();

    // 28 Deterministic Verification Test Cases
    static bool test1_UnenrolledNode();
    static bool test2_EnrollmentSuccess();
    static bool test3_MatchingSensor();
    static bool test4_ReplacedSensor();
    static bool test5_BaselineDeviation();
    static bool test6_NoiseDeviation();
    static bool test7_ResponseSlopeDeviation();
    static bool test8_RecoveryDeviation();
    static bool test9_WarmRebootWithoutWarmup();
    static bool test10_ColdBootWithWarmup();
    static bool test11_InsufficientFeatures();
    static bool test12_MissingInvalidSensorData();
    static bool test13_ZeroNearZeroDenominator();
    static bool test14_WeightRedistribution();
    static bool test15_SimilarityBoundaryAt85();
    static bool test16_SimilarityBoundaryAt70();
    static bool test17_JustAboveThreshold();
    static bool test18_JustBelowThreshold();
    static bool test19_NVSPersistence();
    static bool test20_CorruptedIncompatibleStoredFingerprint();

    // Additional Hardening Audit Verification Tests (Requirement 9)
    static bool test21_InsufficientFeaturesNotMismatch();
    static bool test22_WarmRebootNoWarmup();
    static bool test23_RepeatedRebootAfterEnrollment();
    static bool test24_InvalidPowerSamples();
    static bool test25_InvalidGasSamples();
    static bool test26_CorruptedNVSData();
    static bool test27_ExistingFingerprintNoOverwrite();
    static bool test28_ProductionBuildTestGating();
};

#endif // LAYER2_TESTS_H
