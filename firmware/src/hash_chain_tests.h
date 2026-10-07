#ifndef HASH_CHAIN_TESTS_H
#define HASH_CHAIN_TESTS_H

#include <Arduino.h>
#include "hash_chain.h"

// Independent Layer 3 Cryptographic Hash Chain Verification Suite (22 Deterministic Tests)
class HashChainTestSuite {
public:
    static bool runAllTests();

    // Required 22 Individual Deterministic Test Cases
    static bool test1_GenesisRecord();
    static bool test2_ExactFirstRecordHash();
    static bool test3_SecondChainedRecord();
    static bool test4_MultipleRecordChain();
    static bool test5_Exact3DecimalFormatting();
    static bool test6_ExactCanonicalSerialization();
    static bool test7_CppPythonParity();
    static bool test8_TamperedGasValue();
    static bool test9_TamperedPowerValue();
    static bool test10_TamperedTimestamp();
    static bool test11_TamperedLayer1Status();
    static bool test12_TamperedLayer2Status();
    static bool test13_TamperedPreviousHash();
    static bool test14_DeletedIntermediateRecord();
    static bool test15_MissingLogFile();
    static bool test16_MalformedFinalJSONLine();
    static bool test17_SDWriteFailureRetry();
    static bool test18_RebootRecovery();
    static bool test19_EmptySDCard();
    static bool test20_InvalidGenesisGuard();
    static bool test21_100RecordChain();
    static bool test22_1000RecordStressChain();
};

#endif // HASH_CHAIN_TESTS_H
