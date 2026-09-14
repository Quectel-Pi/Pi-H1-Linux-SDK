// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <linux/soundwire/sdw.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include "lpass.h"
#include "qdsp6/q6afe.h"
#include "qdsp6/q6prm.h"
#include "common.h"
#include "sdw.h"

#define DRIVER_NAME		"qcs9100"
#define WCN_CDC_SLIM_RX_CH_MAX	2
#define WCN_CDC_SLIM_TX_CH_MAX	2
#define NAME_SIZE	32

struct qcs9100_snd_data {
	bool stream_prepared[AFE_PORT_MAX];
	struct snd_soc_card *card;
	struct sdw_stream_runtime *sruntime[AFE_PORT_MAX];
	struct snd_soc_jack jack;
	bool jack_setup;
	struct clk *macro;
	struct clk *dcodec;
	struct snd_soc_jack hdmi_jack[8];
};

static int qcs9100_snd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct qcs9100_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	int ret = 0;
	char jack_name[NAME_SIZE];
	struct snd_soc_jack *hdmi_jack  = NULL;
	int hdmi_pcm_id = 0;
	struct snd_soc_dai *codec_dai;
	int rval, i;

	switch (cpu_dai->id) {
	case PRIMARY_MI2S_RX:
	case PRIMARY_MI2S_TX:
	case PRIMARY_TDM_RX_0:
	case PRIMARY_TDM_TX_0:
		break;
	case DISPLAY_PORT_RX_0:
		hdmi_pcm_id = 0;
		hdmi_jack = &data->hdmi_jack[0];
		break;
	case DISPLAY_PORT_RX_1:
		hdmi_pcm_id = 1;
		hdmi_jack = &data->hdmi_jack[1];
		break;

	default:
		break;
	}
	if (hdmi_jack) {
		snprintf(jack_name, sizeof(jack_name), "HDMI/DP%d Jack", hdmi_pcm_id);
		rval = snd_soc_card_jack_new(rtd->card, jack_name, SND_JACK_AVOUT, hdmi_jack);

		if (rval)
			return rval;

		for_each_rtd_codec_dais(rtd, i, codec_dai) {
			rval = snd_soc_component_set_jack(codec_dai->component, hdmi_jack, NULL);
			if (rval != 0 && rval != -EOPNOTSUPP) {
				dev_warn(rtd->card->dev, "Failed to set HDMI jack: %d\n", rval);
				return rval;
			}
		}
		return qcom_snd_wcd_jack_setup(rtd, &data->jack, &data->jack_setup);
	}

	return ret;
}

static int qcs9100_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				      struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	rate->min = 48000;
	rate->max = 48000;
	channels->min = 2;
	channels->max = 2;

	return 0;
}

static int qcs9100_snd_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct qcs9100_snd_data *pdata = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_card *card = rtd->card;
	int ret = 0;
	int mclk = 0;

	switch (cpu_dai->id) {
	case TERTIARY_MI2S_RX:
	case TERTIARY_MI2S_TX:
	case TERTIARY_TDM_RX_0:
	case TERTIARY_TDM_TX_0:
	case PRIMARY_SDR_MI2S_RX ... QUINARY_SDR_TDM_TX_0:
		/* clock setting is done for qcs9100 target to support high
		 * speed i2s interface
		 */
		if (pdata->macro) {
			ret = clk_prepare_enable(pdata->macro);
			if (ret) {
				dev_err(pdata->card->dev, "unable to prepare macro\n");
				return ret;
			}
		}
		if (pdata->dcodec) {
			ret = clk_prepare_enable(pdata->dcodec);
			if (ret) {
				dev_err(pdata->card->dev, "unable to prepare decode\n");
				return ret;
			}
		}
		break;
	default:
		break;
	}

	if (strcmp(card->name, "monaco-monza") == 0) {
		switch (params_rate(params)) {
		case 8000:
		case 16000:
		case 24000:
		case 32000:
		case 48000:
		case 64000:
		case 96000:
			mclk = 12288000;
			break;
		case 11025:
		case 22050:
		case 44100:
		case 88200:
			mclk = 11289600;
			break;
		default:
			return -EINVAL;
		}

		ret = snd_soc_dai_set_sysclk(codec_dai, 0, mclk, SND_SOC_CLOCK_IN);
		if (ret) {
			dev_err(codec_dai->dev, "Can't set codec DAI clock: %d\n", ret);
			return ret;
		}
	}

	return qcom_snd_sdw_hw_params(substream, params, &pdata->sruntime[cpu_dai->id]);
}

static int qcs9100_snd_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct qcs9100_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];

	return qcom_snd_sdw_prepare(substream, sruntime,
				    &data->stream_prepared[cpu_dai->id]);
}

static int qcs9100_snd_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct qcs9100_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];

	switch (cpu_dai->id) {
	case TERTIARY_MI2S_RX:
	case TERTIARY_MI2S_TX:
	case TERTIARY_TDM_RX_0:
	case TERTIARY_TDM_TX_0:
	case PRIMARY_SDR_MI2S_RX ... QUINARY_SDR_TDM_TX_0:
		/* clock disable is done for qcs9100 target to support high
		 * speed i2s interface
		 */
		if (data->dcodec)
			clk_disable_unprepare(data->dcodec);

		if (data->macro)
			clk_disable_unprepare(data->macro);

		break;
	default:
		break;
	}

	return qcom_snd_sdw_hw_free(substream, sruntime,
				    &data->stream_prepared[cpu_dai->id]);
}

static const struct snd_soc_dapm_widget monaco_monza_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

static const struct snd_soc_dapm_route monaco_monza_dapm_routes[] = {
	{"IN34", NULL, "Headset Mic"},
	{"Headset Mic", NULL, "MICBIAS"},
	{"DMICL", NULL, "Int Mic"},
	{"Int Mic", NULL, "MICBIAS"},
	{"Headphone", NULL, "HPL"},
	{"Headphone", NULL, "HPR"},
	{"Speaker", NULL, "SPKL"},
	{"Speaker", NULL, "SPKR"},
};

static const struct snd_kcontrol_new monaco_monza_max98090_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
	SOC_DAPM_PIN_SWITCH("Speaker"),
};

static const struct snd_soc_ops qcs9100_be_ops = {
	.hw_params = qcs9100_snd_hw_params,
	.hw_free = qcs9100_snd_hw_free,
	.prepare = qcs9100_snd_prepare,
};

static struct snd_soc_card snd_soc_iq8_8275_evk_data = {
	.name = "iq8-8275-evk",
};

static struct snd_soc_card snd_soc_monaco_monza_data = {
	.name = "monaco-monza",
	.dapm_widgets = monaco_monza_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(monaco_monza_dapm_widgets),
	.dapm_routes = monaco_monza_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(monaco_monza_dapm_routes),
	.controls = monaco_monza_max98090_controls,
	.num_controls = ARRAY_SIZE(monaco_monza_max98090_controls),
};

static struct snd_soc_card snd_soc_qcs8300_data = {
	.name = "qcs8300",
};

static struct snd_soc_card snd_soc_qcs9100_data = {
	.name = "qcs9100",
};

static struct snd_soc_card snd_soc_qcs9075_rb8_data = {
	.name = "qcs9075-rb8",
};

static void qcs9100_add_be_ops(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *link;
	int i;

	for_each_card_prelinks(card, i, link) {
		/* Apply BE ops to backend links and links with codecs */
		if (link->no_pcm == 1 || link->num_codecs > 0) {
			link->init = qcs9100_snd_init;
			link->be_hw_params_fixup = qcs9100_be_hw_params_fixup;
			link->ops = &qcs9100_be_ops;
			if (strcmp(card->name, "monaco-monza") == 0) {
				link->dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
					| SND_SOC_DAIFMT_CBM_CFM;
			}
		}
	}
}

static int qcs9100_platform_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct qcs9100_snd_data *data;
	struct device *dev = &pdev->dev;
	int ret;

	card = (struct snd_soc_card *)of_device_get_match_data(&pdev->dev);
	if (!card)
		return -EINVAL;

	card->owner = THIS_MODULE;
	/* Allocate the private data */
	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	card->dev = dev;

	dev_set_drvdata(dev, card);
	snd_soc_card_set_drvdata(card, data);
	ret = qcom_snd_parse_of(card);
	if (ret)
		return ret;

	card->driver_name = DRIVER_NAME;
	qcs9100_add_be_ops(card);

	/* get clock info to set clock for qcs9100 target to support high
	 * speed i2s interface
	 */
	data->macro = devm_clk_get_optional(dev, "macro");
	if (IS_ERR(data->macro)) {
		dev_info(dev, "getting macro clock info FAILED, ret %d\n", PTR_ERR(data->macro));
		return PTR_ERR(data->macro);
	}
	data->dcodec = devm_clk_get_optional(dev, "dcodec");
	if (IS_ERR(data->dcodec)) {
		dev_info(dev, "getting decode clock info FAILED, ret %d\n", PTR_ERR(data->dcodec));
		return PTR_ERR(data->dcodec);
	}

	return devm_snd_soc_register_card(dev, card);
}

static const struct of_device_id snd_qcs9100_dt_match[] = {
	{.compatible = "qcom,iq8-8275-evk-sndcard", .data = &snd_soc_iq8_8275_evk_data},
	{.compatible = "qcom,qcs8300-ridesx-sndcard", .data = &snd_soc_qcs8300_data},
	{.compatible = "qcom,qcs9100-ridesx-sndcard", .data = &snd_soc_qcs9100_data},
	{.compatible = "qcom,qcs9075-rb8-sndcard", .data = &snd_soc_qcs9075_rb8_data},
	{.compatible = "qcom,monaco-monza-sndcard", .data = &snd_soc_monaco_monza_data},
	{}
};

MODULE_DEVICE_TABLE(of, snd_qcs9100_dt_match);

static struct platform_driver snd_qcs9100_driver = {
	.probe  = qcs9100_platform_probe,
	.driver = {
		.name = "snd-qcs9100",
		.of_match_table = snd_qcs9100_dt_match,
	},
};
module_platform_driver(snd_qcs9100_driver);
MODULE_DESCRIPTION("qcs9100 ASoC Machine Driver");
MODULE_LICENSE("GPL");
