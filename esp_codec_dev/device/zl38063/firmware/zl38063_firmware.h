/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

/*Firmware Version : Microsemi_ZLS38063_1_P1_4_0_Firmware.s3, modified: Tue Sep 18 20:50:24 2018 */

#ifndef _ZL38063_FIRMWARE_H_
#define _ZL38063_FIRMWARE_H_

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

extern const twFwr st_twFirmware[];

extern const unsigned short firmwareStreamLen;
extern const unsigned long programBaseAddress;
extern const unsigned long executionAddress;
extern const unsigned char haveProgramBaseAddress;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* _ZL38063_FIRMWARE_H_ */
