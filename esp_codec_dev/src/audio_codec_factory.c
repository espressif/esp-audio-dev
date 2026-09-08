/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "esp_codec_dev_defaults.h"

static const char *TAG = "ADEV_CODEC_FACTORY";

#define CODEC_NEW_FUNC(codec_name)             codec_name##_codec_new
#define CODEC_CFG_TYPE(codec_name)             codec_name##_codec_cfg_t
#define CODEC_BUILD_CHIP_CFG_FUNC(codec_name)  codec_name##_build_chip_cfg

#define DEFINE_CODEC_BUILD_CHIP_CFG_BEGIN(codec_name)                                \
    _Static_assert(sizeof(CODEC_CFG_TYPE(codec_name)) != sizeof(audio_codec_cfg_t),  \
                   #codec_name " chip cfg size collides with audio_codec_cfg_t");    \
    static void CODEC_BUILD_CHIP_CFG_FUNC(codec_name)(const audio_codec_cfg_t *src,  \
                                                      void *chip_cfg)                \
    {                                                                                \
        CODEC_CFG_TYPE(codec_name) *cfg = (CODEC_CFG_TYPE(codec_name) *)chip_cfg;    \
        memset(cfg, 0, sizeof(*cfg));

#define DEFINE_CODEC_BUILD_CHIP_CFG_END()  }

#ifdef CONFIG_CODEC_ES8311_SUPPORT
DEFINE_CODEC_BUILD_CHIP_CFG_BEGIN(es8311)
    cfg->ctrl_if = src->ctrl_if;
    cfg->gpio_if = src->gpio_if;
    cfg->sys_cfg = src->sys_cfg;
    cfg->adc_cfg = src->adc_cfg;
    cfg->dac_cfg = src->dac_cfg;
    cfg->pa_cfg = src->pa_cfg;
DEFINE_CODEC_BUILD_CHIP_CFG_END()
#endif  /* CONFIG_CODEC_ES8311_SUPPORT */

#ifdef CONFIG_CODEC_ES7210_SUPPORT
DEFINE_CODEC_BUILD_CHIP_CFG_BEGIN(es7210)
    cfg->ctrl_if = src->ctrl_if;
    cfg->sys_cfg = src->sys_cfg;
    cfg->adc_cfg = src->adc_cfg;
DEFINE_CODEC_BUILD_CHIP_CFG_END()
#endif  /* CONFIG_CODEC_ES7210_SUPPORT */

#ifdef CONFIG_CODEC_ES7243_SUPPORT
DEFINE_CODEC_BUILD_CHIP_CFG_BEGIN(es7243)
    cfg->ctrl_if = src->ctrl_if;
    cfg->adc_cfg = src->adc_cfg;
DEFINE_CODEC_BUILD_CHIP_CFG_END()
#endif  /* CONFIG_CODEC_ES7243_SUPPORT */

#ifdef CONFIG_CODEC_ES7243E_SUPPORT
DEFINE_CODEC_BUILD_CHIP_CFG_BEGIN(es7243e)
    cfg->ctrl_if = src->ctrl_if;
    cfg->adc_cfg = src->adc_cfg;
DEFINE_CODEC_BUILD_CHIP_CFG_END()
#endif  /* CONFIG_CODEC_ES7243E_SUPPORT */

#ifdef CONFIG_CODEC_ES8156_SUPPORT
DEFINE_CODEC_BUILD_CHIP_CFG_BEGIN(es8156)
    cfg->ctrl_if = src->ctrl_if;
    cfg->gpio_if = src->gpio_if;
    cfg->pa_cfg = src->pa_cfg;
DEFINE_CODEC_BUILD_CHIP_CFG_END()
#endif  /* CONFIG_CODEC_ES8156_SUPPORT */

#ifdef CONFIG_CODEC_ES8374_SUPPORT
DEFINE_CODEC_BUILD_CHIP_CFG_BEGIN(es8374)
    cfg->ctrl_if = src->ctrl_if;
    cfg->gpio_if = src->gpio_if;
    cfg->sys_cfg = src->sys_cfg;
    cfg->adc_cfg = src->adc_cfg;
    cfg->dac_cfg = src->dac_cfg;
    cfg->pa_cfg = src->pa_cfg;
DEFINE_CODEC_BUILD_CHIP_CFG_END()
#endif  /* CONFIG_CODEC_ES8374_SUPPORT */

#ifdef CONFIG_CODEC_ES8388_SUPPORT
DEFINE_CODEC_BUILD_CHIP_CFG_BEGIN(es8388)
    cfg->ctrl_if = src->ctrl_if;
    cfg->gpio_if = src->gpio_if;
    cfg->sys_cfg = src->sys_cfg;
    cfg->pa_cfg = src->pa_cfg;
DEFINE_CODEC_BUILD_CHIP_CFG_END()
#endif  /* CONFIG_CODEC_ES8388_SUPPORT */

#ifdef CONFIG_CODEC_ES8389_SUPPORT
DEFINE_CODEC_BUILD_CHIP_CFG_BEGIN(es8389)
    cfg->ctrl_if = src->ctrl_if;
    cfg->gpio_if = src->gpio_if;
    cfg->sys_cfg = src->sys_cfg;
    cfg->adc_cfg = src->adc_cfg;
    cfg->dac_cfg = src->dac_cfg;
    cfg->pa_cfg = src->pa_cfg;
DEFINE_CODEC_BUILD_CHIP_CFG_END()
#endif  /* CONFIG_CODEC_ES8389_SUPPORT */

#ifdef CONFIG_CODEC_AW88298_SUPPORT
DEFINE_CODEC_BUILD_CHIP_CFG_BEGIN(aw88298)
    cfg->ctrl_if = src->ctrl_if;
    cfg->gpio_if = src->gpio_if;
    cfg->pa_cfg = src->pa_cfg;
    cfg->reset_cfg = src->reset_cfg;
DEFINE_CODEC_BUILD_CHIP_CFG_END()
#endif  /* CONFIG_CODEC_AW88298_SUPPORT */

#ifdef CONFIG_CODEC_TAS5805M_SUPPORT
DEFINE_CODEC_BUILD_CHIP_CFG_BEGIN(tas5805m)
    cfg->ctrl_if = src->ctrl_if;
    cfg->gpio_if = src->gpio_if;
    cfg->sys_cfg = src->sys_cfg;
    cfg->pa_cfg = src->pa_cfg;
    cfg->reset_cfg = src->reset_cfg;
DEFINE_CODEC_BUILD_CHIP_CFG_END()
#endif  /* CONFIG_CODEC_TAS5805M_SUPPORT */

#ifdef CONFIG_CODEC_ZL38063_SUPPORT
DEFINE_CODEC_BUILD_CHIP_CFG_BEGIN(zl38063)
    cfg->ctrl_if = src->ctrl_if;
    cfg->gpio_if = src->gpio_if;
    cfg->pa_cfg = src->pa_cfg;
    cfg->reset_cfg = src->reset_cfg;
DEFINE_CODEC_BUILD_CHIP_CFG_END()
#endif  /* CONFIG_CODEC_ZL38063_SUPPORT */

#ifdef CONFIG_CODEC_CJC8910_SUPPORT
DEFINE_CODEC_BUILD_CHIP_CFG_BEGIN(cjc8910)
    cfg->ctrl_if = src->ctrl_if;
    cfg->gpio_if = src->gpio_if;
    cfg->pa_cfg = src->pa_cfg;
DEFINE_CODEC_BUILD_CHIP_CFG_END()
#endif  /* CONFIG_CODEC_CJC8910_SUPPORT */

#ifdef CONFIG_CODEC_DUMMY_SUPPORT
DEFINE_CODEC_BUILD_CHIP_CFG_BEGIN(dummy)
    cfg->gpio_if = src->gpio_if;
    cfg->pa_cfg = src->pa_cfg;
DEFINE_CODEC_BUILD_CHIP_CFG_END()
#endif  /* CONFIG_CODEC_DUMMY_SUPPORT */

#define CODEC_REGISTRY_ENTRY(codec_name)  {                           \
    #codec_name,                                                      \
    (const audio_codec_if_t *(*)(void *))CODEC_NEW_FUNC(codec_name),  \
    sizeof(CODEC_CFG_TYPE(codec_name)),                               \
    CODEC_BUILD_CHIP_CFG_FUNC(codec_name),                            \
}

static const audio_codec_desc_t s_codec_registry[] = {
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

extern const audio_codec_desc_t _audio_codec_desc_array_start[];
extern const audio_codec_desc_t _audio_codec_desc_array_end[];

static const audio_codec_desc_t *find_builtin_codec(const char *name)
{
    for (size_t i = 0; i < sizeof(s_codec_registry) / sizeof(s_codec_registry[0]); i++) {
        if (strcmp(s_codec_registry[i].name, name) == 0) {
            return &s_codec_registry[i];
        }
    }
    return NULL;
}

static const audio_codec_desc_t *find_registered_codec(const char *name)
{
    for (const audio_codec_desc_t *desc = _audio_codec_desc_array_start;
         desc < _audio_codec_desc_array_end; desc++) {
        if (desc->name != NULL && strcmp(desc->name, name) == 0) {
            return desc;
        }
    }
    return NULL;
}

static const audio_codec_desc_t *find_codec(const char *name)
{
    const audio_codec_desc_t *builtin = find_builtin_codec(name);
    if (builtin != NULL) {
        if (find_registered_codec(name) != NULL) {
            ESP_LOGW(TAG, "Registered codec '%s' is shadowed by the built-in driver; "
                          "disable the matching CONFIG_CODEC_*_SUPPORT to override",
                     name);
        }
        return builtin;
    }
    return find_registered_codec(name);
}

static const audio_codec_if_t *create_from_shared_cfg(const audio_codec_desc_t *entry, const void *codec_cfg)
{
    void *chip_cfg = calloc(1, entry->chip_cfg_size);
    if (chip_cfg == NULL) {
        ESP_LOGE(TAG, "No mem for %s configuration", entry->name);
        return NULL;
    }
    entry->build_chip_cfg((const audio_codec_cfg_t *)codec_cfg, chip_cfg);
    const audio_codec_if_t *codec_if = entry->create(chip_cfg);
    free(chip_cfg);
    return codec_if;
}

const audio_codec_if_t *audio_codec_new(const char *codec_name, const void *codec_cfg, int cfg_size)
{
    if (codec_name == NULL || codec_cfg == NULL) {
        ESP_LOGE(TAG, "Invalid codec name or configuration");
        return NULL;
    }

    const audio_codec_desc_t *entry = find_codec(codec_name);
    if (entry == NULL) {
        ESP_LOGE(TAG, "Unsupported codec name: %s", codec_name);
        return NULL;
    }
    if (entry->create == NULL || entry->chip_cfg_size == 0) {
        ESP_LOGE(TAG, "Malformed descriptor for codec: %s", codec_name);
        return NULL;
    }

    const bool is_shared = (cfg_size == (int)sizeof(audio_codec_cfg_t));
    const bool is_chip = (cfg_size == (int)entry->chip_cfg_size);
    if (is_shared && entry->build_chip_cfg != NULL) {
        return create_from_shared_cfg(entry, codec_cfg);
    }
    if (is_chip) {
        return entry->create((void *)codec_cfg);
    }

    ESP_LOGE(TAG, "Invalid %s configuration size %d, expected %d%s",
             codec_name, cfg_size, (int)entry->chip_cfg_size,
             entry->build_chip_cfg ? " or audio_codec_cfg_t" : "");
    return NULL;
}
