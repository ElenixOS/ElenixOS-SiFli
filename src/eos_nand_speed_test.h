/**
 * @file eos_nand_speed_test.h
 * @brief NAND Flash speed diagnostic test
 */

#ifndef EOS_NAND_SPEED_TEST_H
#define EOS_NAND_SPEED_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EOS_DIAG_NAND_SPEED_TEST
#define EOS_DIAG_NAND_SPEED_TEST 1
#endif

void eos_nand_speed_test(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_NAND_SPEED_TEST_H */
