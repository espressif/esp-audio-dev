# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded_idf.dut import IdfDut
from pytest_embedded_idf.utils import idf_parametrize

CASE_TIMEOUT_S = 900


@pytest.mark.skip(reason='esp32 build-only in CI; no on-target flash')
@pytest.mark.UT_T1_AUDIO
@pytest.mark.esp32
@idf_parametrize('target', ['esp32'], indirect=['target'])
def test_codec_dev_i2s_esp32_lyrat_mini(dut: IdfDut) -> None:
    dut.run_all_single_board_cases(
        group=['mock', 'lyrat_mini'],
        timeout=CASE_TIMEOUT_S,
        reset=True,
    )


@pytest.mark.UT_T1_AUDIO
@pytest.mark.esp32s3
@idf_parametrize('target', ['esp32s3'], indirect=['target'])
def test_codec_dev_i2s_esp32s3_korvo2(dut: IdfDut) -> None:
    dut.run_all_single_board_cases(
        group=['mock', 'korvo2_v3'],
        timeout=CASE_TIMEOUT_S,
        reset=True,
    )


@pytest.mark.UT_T1_AUDIO
@pytest.mark.esp32p4
@idf_parametrize('target', ['esp32p4'], indirect=['target'])
def test_codec_dev_i2s_esp32p4_ev_board(dut: IdfDut) -> None:
    dut.run_all_single_board_cases(
        group=['mock', 'p4_ev'],
        timeout=CASE_TIMEOUT_S,
        reset=True,
    )


@pytest.mark.UT_T1_AUDIO
@pytest.mark.esp32c3
@idf_parametrize('target', ['esp32c3'], indirect=['target'])
def test_codec_dev_i2s_esp32c3_c3_lyra(dut: IdfDut) -> None:
    dut.run_all_single_board_cases(
        group=['mock', 'c3_lyra'],
        timeout=CASE_TIMEOUT_S,
        reset=True,
    )


@pytest.mark.UT_T1_AUDIO
@pytest.mark.esp32s31
@idf_parametrize('target', ['esp32s31'], indirect=['target'])
def test_codec_dev_i2s_esp32s31_korvo(dut: IdfDut) -> None:
    dut.run_all_single_board_cases(
        group=['mock', 'es8389'],
        timeout=CASE_TIMEOUT_S,
        reset=True,
    )
