# Codec 芯片在线文档

本文汇总 `esp_codec_dev` 已支持芯片的厂商在线文档入口，便于对照寄存器与电气特性。软件能力与配置模型见 [features.md](features.md) 第 10 节。

厂商链接可能变更或下架；以各官网当前页面为准。

## 顺芯 Everest

产品与公开 datasheet 汇总页：[Everest 产品介绍](http://everest-semi.com/cn_products.php)

| Codec | 在线文档 | 备注 |
| --- | --- | --- |
| ES7210 | [ES7210 PB.pdf](http://everest-semi.com/pdf/ES7210%20PB.pdf) | 官网公开下载 |
| ES7243 | [ES7243 PB.pdf](http://everest-semi.com/pdf/ES7243%20PB.pdf) | 官网公开下载 |
| ES7243E | [ES7243 PB.pdf](http://everest-semi.com/pdf/ES7243%20PB.pdf) | 官网无独立 ES7243E PDF，一般对照 ES7243 |
| ES8156 | [ES8156 PB.pdf](http://everest-semi.com/pdf/ES8156%20PB.pdf) | 官网公开下载 |
| ES8311 | [ES8311 PB.pdf](http://everest-semi.com/pdf/ES8311%20PB.pdf) | 官网公开下载 |
| ES8389 | [ES8389 PB.pdf](http://everest-semi.com/pdf/ES8389%20PB.pdf) | 官网公开下载 |
| ES8374 | — | 官网公开区已无 ES8374 PDF；宣传仍提及该型号，成本优化型号见 [ES8375 PB.pdf](http://everest-semi.com/pdf/ES8375%20PB.pdf) |
| ES8388 | [第三方镜像 PDF](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1141/ES8388.pdf) | 官网公开区已下架；选型指南建议新设计改用 ES8389 / ES8390 |

更完整的公开列表（含选择指南等）见上述产品页「产品数据手册 Datasheet」表格。

## 其他厂商

| Codec | 厂商 | 在线文档 | 备注 |
| --- | --- | --- | --- |
| CJC8910 | 菉华 CSC | [产品页](http://www.csc-ic.com/index.php/Do_d_gci_331_id_116.html) | 规格书需在官网产品页获取；另有 [主控适配说明](http://www.csc-ic.com/Uploads/Dw/69ddac02722a9.pdf) |
| AW88298 | 艾为 Awinic | [AW88298QNR 产品页](https://www.awinic.com/en/productDetail/AW88298QNR) | Datasheet 在产品页「Technical documentation」下载 |
| TAS5805M | TI | [Datasheet PDF](https://www.ti.com/lit/ds/symlink/tas5805m.pdf) / [产品页](https://www.ti.com/product/TAS5805M) | — |
| ZL38063 | Microchip | [产品页](https://www.microchip.com/en-us/product/zl38063#tab2) | 完整资料可能需厂商授权 |

`dummy`、`template_codec`、片上 ADC 与 USB UAC 无独立外部 codec datasheet。
