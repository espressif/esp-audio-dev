# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded_idf.dut import IdfDut
from pytest_embedded_idf.utils import idf_parametrize

UAC_HW_TIMEOUT_S = 900


@pytest.mark.skip(reason='esp32s3 Korvo-2L build-only in CI; no on-target flash')
@pytest.mark.UT_T1_AUDIO
@pytest.mark.esp32s3
@idf_parametrize('target', ['esp32s3'], indirect=['target'])
def test_codec_dev_uac_api(dut: IdfDut) -> None:
    dut.run_all_single_board_cases(
        group='uac',
        timeout=UAC_HW_TIMEOUT_S,
        reset=True,
    )
