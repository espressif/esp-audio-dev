/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_log.h"

#include "esp_codec_dev_defaults.h"

static const char *TAG = "ADEV_CODEC_FACTORY";

typedef const audio_codec_if_t *(*codec_new_fn)(void *cfg);
typedef void (*codec_from_common_fn)(const audio_codec_cfg_t *common, void *chip_cfg);

typedef struct {
    const char           *name;
    codec_new_fn          create;
    size_t                chip_cfg_size;
    codec_from_common_fn  from_common;
} codec_registry_entry_t;

#define CODEC_NEW_FUNC(codec_name)          codec_name##_codec_new
#define CODEC_CFG_TYPE(codec_name)          codec_name##_codec_cfg_t
#define CODEC_FROM_COMMON_FUNC(codec_name)  codec_name##_from_common

#define DEFINE_CODEC_FROM_COMMON_BEGIN(codec_name)                                   \
    static void CODEC_FROM_COMMON_FUNC(codec_name)(const audio_codec_cfg_t *common,  \
                                                   void *chip_cfg)                   \
    {                                                                                \
        CODEC_CFG_TYPE(codec_name) *cfg = (CODEC_CFG_TYPE(codec_name) *)chip_cfg;    \
        memset(cfg, 0, sizeof(*cfg));

#define DEFINE_CODEC_FROM_COMMON_END()  }

#ifdef CONFIG_CODEC_ES8311_SUPPORT
DEFINE_CODEC_FROM_COMMON_BEGIN(es8311)
    cfg->ctrl_if = common->ctrl_if;
    cfg->gpio_if = common->gpio_if;
    cfg->sys_cfg = common->sys_cfg;
    cfg->adc_cfg = common->adc_cfg;
    cfg->dac_cfg = common->dac_cfg;
    cfg->pa_cfg = common->pa_cfg;
DEFINE_CODEC_FROM_COMMON_END()
#endif  /* CONFIG_CODEC_ES8311_SUPPORT */

#ifdef CONFIG_CODEC_ES7210_SUPPORT
DEFINE_CODEC_FROM_COMMON_BEGIN(es7210)
    cfg->ctrl_if = common->ctrl_if;
    cfg->sys_cfg = common->sys_cfg;
    cfg->adc_cfg = common->adc_cfg;
DEFINE_CODEC_FROM_COMMON_END()
#endif  /* CONFIG_CODEC_ES7210_SUPPORT */

#ifdef CONFIG_CODEC_ES7243_SUPPORT
DEFINE_CODEC_FROM_COMMON_BEGIN(es7243)
    cfg->ctrl_if = common->ctrl_if;
    cfg->adc_cfg = common->adc_cfg;
DEFINE_CODEC_FROM_COMMON_END()
#endif  /* CONFIG_CODEC_ES7243_SUPPORT */

#ifdef CONFIG_CODEC_ES7243E_SUPPORT
DEFINE_CODEC_FROM_COMMON_BEGIN(es7243e)
    cfg->ctrl_if = common->ctrl_if;
    cfg->adc_cfg = common->adc_cfg;
DEFINE_CODEC_FROM_COMMON_END()
#endif  /* CONFIG_CODEC_ES7243E_SUPPORT */

#ifdef CONFIG_CODEC_ES8156_SUPPORT
DEFINE_CODEC_FROM_COMMON_BEGIN(es8156)
    cfg->ctrl_if = common->ctrl_if;
    cfg->gpio_if = common->gpio_if;
    cfg->pa_cfg = common->pa_cfg;
DEFINE_CODEC_FROM_COMMON_END()
#endif  /* CONFIG_CODEC_ES8156_SUPPORT */

#ifdef CONFIG_CODEC_ES8374_SUPPORT
DEFINE_CODEC_FROM_COMMON_BEGIN(es8374)
    cfg->ctrl_if = common->ctrl_if;
    cfg->gpio_if = common->gpio_if;
    cfg->sys_cfg = common->sys_cfg;
    cfg->adc_cfg = common->adc_cfg;
    cfg->dac_cfg = common->dac_cfg;
    cfg->pa_cfg = common->pa_cfg;
DEFINE_CODEC_FROM_COMMON_END()
#endif  /* CONFIG_CODEC_ES8374_SUPPORT */

#ifdef CONFIG_CODEC_ES8388_SUPPORT
DEFINE_CODEC_FROM_COMMON_BEGIN(es8388)
    cfg->ctrl_if = common->ctrl_if;
    cfg->gpio_if = common->gpio_if;
    cfg->sys_cfg = common->sys_cfg;
    cfg->pa_cfg = common->pa_cfg;
DEFINE_CODEC_FROM_COMMON_END()
#endif  /* CONFIG_CODEC_ES8388_SUPPORT */

#ifdef CONFIG_CODEC_ES8389_SUPPORT
DEFINE_CODEC_FROM_COMMON_BEGIN(es8389)
    cfg->ctrl_if = common->ctrl_if;
    cfg->gpio_if = common->gpio_if;
    cfg->sys_cfg = common->sys_cfg;
    cfg->adc_cfg = common->adc_cfg;
    cfg->dac_cfg = common->dac_cfg;
    cfg->pa_cfg = common->pa_cfg;
DEFINE_CODEC_FROM_COMMON_END()
#endif  /* CONFIG_CODEC_ES8389_SUPPORT */

#ifdef CONFIG_CODEC_AW88298_SUPPORT
DEFINE_CODEC_FROM_COMMON_BEGIN(aw88298)
    cfg->ctrl_if = common->ctrl_if;
    cfg->gpio_if = common->gpio_if;
    cfg->pa_cfg = common->pa_cfg;
    cfg->reset_cfg = common->reset_cfg;
DEFINE_CODEC_FROM_COMMON_END()
#endif  /* CONFIG_CODEC_AW88298_SUPPORT */

#ifdef CONFIG_CODEC_TAS5805M_SUPPORT
DEFINE_CODEC_FROM_COMMON_BEGIN(tas5805m)
    cfg->ctrl_if = common->ctrl_if;
    cfg->gpio_if = common->gpio_if;
    cfg->sys_cfg = common->sys_cfg;
    cfg->pa_cfg = common->pa_cfg;
    cfg->reset_cfg = common->reset_cfg;
DEFINE_CODEC_FROM_COMMON_END()
#endif  /* CONFIG_CODEC_TAS5805M_SUPPORT */

#ifdef CONFIG_CODEC_ZL38063_SUPPORT
DEFINE_CODEC_FROM_COMMON_BEGIN(zl38063)
    cfg->ctrl_if = common->ctrl_if;
    cfg->gpio_if = common->gpio_if;
    cfg->pa_cfg = common->pa_cfg;
    cfg->reset_cfg = common->reset_cfg;
DEFINE_CODEC_FROM_COMMON_END()
#endif  /* CONFIG_CODEC_ZL38063_SUPPORT */

#ifdef CONFIG_CODEC_CJC8910_SUPPORT
DEFINE_CODEC_FROM_COMMON_BEGIN(cjc8910)
    cfg->ctrl_if = common->ctrl_if;
    cfg->gpio_if = common->gpio_if;
    cfg->pa_cfg = common->pa_cfg;
DEFINE_CODEC_FROM_COMMON_END()
#endif  /* CONFIG_CODEC_CJC8910_SUPPORT */

#ifdef CONFIG_CODEC_DUMMY_SUPPORT
DEFINE_CODEC_FROM_COMMON_BEGIN(dummy)
    cfg->gpio_if = common->gpio_if;
    cfg->pa_cfg = common->pa_cfg;
DEFINE_CODEC_FROM_COMMON_END()
#endif  /* CONFIG_CODEC_DUMMY_SUPPORT */

#define CODEC_REGISTRY_ENTRY(codec_name)  {    \
    #codec_name,                               \
    (codec_new_fn)CODEC_NEW_FUNC(codec_name),  \
    sizeof(CODEC_CFG_TYPE(codec_name)),        \
    CODEC_FROM_COMMON_FUNC(codec_name),        \
}

static const codec_registry_entry_t s_codec_registry[] = {
#ifdef CONFIG_CODEC_ES8311_SUPPORT
    CODEC_REGISTRY_ENTRY(es8311),
#endif  /* CONFIG_CODEC_ES8311_SUPPORT */
#ifdef CONFIG_CODEC_ES7210_SUPPORT
    CODEC_REGISTRY_ENTRY(es7210),
#endif  /* CONFIG_CODEC_ES7210_SUPPORT */
#ifdef CONFIG_CODEC_ES7243_SUPPORT
    CODEC_REGISTRY_ENTRY(es7243),
#endif  /* CONFIG_CODEC_ES7243_SUPPORT */
#ifdef CONFIG_CODEC_ES7243E_SUPPORT
    CODEC_REGISTRY_ENTRY(es7243e),
#endif  /* CONFIG_CODEC_ES7243E_SUPPORT */
#ifdef CONFIG_CODEC_ES8156_SUPPORT
    CODEC_REGISTRY_ENTRY(es8156),
#endif  /* CONFIG_CODEC_ES8156_SUPPORT */
#ifdef CONFIG_CODEC_ES8374_SUPPORT
    CODEC_REGISTRY_ENTRY(es8374),
#endif  /* CONFIG_CODEC_ES8374_SUPPORT */
#ifdef CONFIG_CODEC_ES8388_SUPPORT
    CODEC_REGISTRY_ENTRY(es8388),
#endif  /* CONFIG_CODEC_ES8388_SUPPORT */
#ifdef CONFIG_CODEC_ES8389_SUPPORT
    CODEC_REGISTRY_ENTRY(es8389),
#endif  /* CONFIG_CODEC_ES8389_SUPPORT */
#ifdef CONFIG_CODEC_AW88298_SUPPORT
    CODEC_REGISTRY_ENTRY(aw88298),
#endif  /* CONFIG_CODEC_AW88298_SUPPORT */
#ifdef CONFIG_CODEC_TAS5805M_SUPPORT
    CODEC_REGISTRY_ENTRY(tas5805m),
#endif  /* CONFIG_CODEC_TAS5805M_SUPPORT */
#ifdef CONFIG_CODEC_ZL38063_SUPPORT
    CODEC_REGISTRY_ENTRY(zl38063),
#endif  /* CONFIG_CODEC_ZL38063_SUPPORT */
#ifdef CONFIG_CODEC_CJC8910_SUPPORT
    CODEC_REGISTRY_ENTRY(cjc8910),
#endif  /* CONFIG_CODEC_CJC8910_SUPPORT */
#ifdef CONFIG_CODEC_DUMMY_SUPPORT
    CODEC_REGISTRY_ENTRY(dummy),
#endif  /* CONFIG_CODEC_DUMMY_SUPPORT */
};

static const codec_registry_entry_t *find_codec(const char *name)
{
    for (size_t i = 0; i < sizeof(s_codec_registry) / sizeof(s_codec_registry[0]); i++) {
        if (strcmp(s_codec_registry[i].name, name) == 0) {
            return &s_codec_registry[i];
        }
    }
    return NULL;
}

const audio_codec_if_t *audio_codec_new(const char *codec_name, const void *codec_cfg, int cfg_size)
{
    if (codec_name == NULL || codec_cfg == NULL) {
        ESP_LOGE(TAG, "Invalid codec name or configuration");
        return NULL;
    }

    const codec_registry_entry_t *entry = find_codec(codec_name);
    if (entry == NULL) {
        ESP_LOGE(TAG, "Unsupported codec name: %s", codec_name);
        return NULL;
    }

    /* Chip-specific configuration: pass through directly. */
    if (cfg_size == (int)entry->chip_cfg_size) {
        return entry->create((void *)codec_cfg);
    }

    /* Common bag configuration: map used fields into chip-specific cfg. */
    if (cfg_size == (int)sizeof(audio_codec_cfg_t)) {
        uint8_t chip_cfg[entry->chip_cfg_size];
        entry->from_common((const audio_codec_cfg_t *)codec_cfg, chip_cfg);
        return entry->create(chip_cfg);
    }

    ESP_LOGE(TAG, "Invalid %s configuration size %d, expected %d or %d",
             codec_name, cfg_size, (int)entry->chip_cfg_size, (int)sizeof(audio_codec_cfg_t));
    return NULL;
}
