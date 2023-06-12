/* sound/soc/samsung/abox_v2/abox_cmpnt.h
 *
 * ALSA SoC - Samsung Abox ASoC Component driver
 *
 * Copyright (c) 2018 Samsung Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SND_SOC_ABOX_CMPNT_H
#define __SND_SOC_ABOX_CMPNT_H

#include <linux/device.h>
#include <sound/soc.h>
#include "abox.h"

/**
 * lock specific asrc id to the dma
 * @param[in]	data	pointer to abox_data
 * @param[in]	stream	SNDRV_PCM_STREAM_PLAYBACK or SNDRV_PCM_STREAM_CAPTURE
 * @param[in]	idx	index of requesting DMA
 * @param[in]	id	id of ASRC
 * @return		0 or error code
 */
extern int abox_cmpnt_asrc_lock(struct abox_data *data, int stream,
		int idx, int id);

/**
 * release asrc from the dma
 * @param[in]	data	pointer to abox_data
 * @param[in]	stream	SNDRV_PCM_STREAM_PLAYBACK or SNDRV_PCM_STREAM_CAPTURE
 * @param[in]	idx	index of requesting DMA
 * @return		0 or error code
 */
extern void abox_cmpnt_asrc_release(struct abox_data *data, int stream,
		int idx);

/**
 * update asrc tick
 * @param[in]	adev	pointer to abox device
 * @return		0 or error code
 */
extern int abox_cmpnt_update_asrc_tick(struct device *adev);

/**
 * prepare sifsm and sidetone for a RDMA
 * @param[in]	dev		calling device
 * @param[in]	data		pointer to abox data
 * @param[in]	dma_data	pointer to dma data
 * @return			0 or error code
 */
extern int abox_cmpnt_sifsm_prepare(struct device *dev, struct abox_data *data,
		enum abox_dai dai);

/**
 * adjust sample bank size
 * @param[in]	data	pointer to abox_data
 * @param[in]	id	id of ABOX DAI
 * @param[in]	params	hardware paramter
 * @return		sample bank size or error code
 */
extern int abox_cmpnt_adjust_sbank(struct abox_data *data,
		enum abox_dai id, struct snd_pcm_hw_params *params);

/**
 * reset count value of a sifs which is connected to given uaif or dsif
 * @param[in]	dev	pointer to calling device
 * @param[in]	cmpnt	component
 * @param[in]	id	id of ABOX DAI
 * @return		error code
 */
extern int abox_cmpnt_reset_cnt_val(struct device *dev,
		struct snd_soc_component *cmpnt, enum abox_dai id);

/**
 * update count value for sifs
 * @param[in]	adev	pointer to abox device
 * @return		0 or error code
 */
extern int abox_cmpnt_update_cnt_val(struct device *adev);

/**
 * hw params fixup helper
 * @param[in]	rtd	snd_soc_pcm_runtime
 * @param[out]	params	snd_pcm_hw_params
 * @param[in]	stream	SNDRV_PCM_STREAM_PLAYBACK or SNDRV_PCM_STREAM_CAPTURE
 * @return		0 or error code
 */
extern int abox_cmpnt_hw_params_fixup_helper(struct snd_soc_pcm_runtime *rtd,
		struct snd_pcm_hw_params *params, int stream);

/**
 * Register uaif or dsif to abox
 * @param[in]	dev_abox	pointer to abox device
 * @param[in]	dev		pointer to abox if device
 * @param[in]	id		number
 * @param[in]	dapm		dapm context of the uaif or dsif
 * @param[in]	name		dai name
 * @param[in]	playback	true if dai has playback capability
 * @param[in]	capture		true if dai has capture capability
 * @return	error code if any
 */
extern int abox_cmpnt_register_if(struct device *dev_abox,
		struct device *dev, unsigned int id,
		struct snd_soc_dapm_context *dapm, const char *name,
		bool playback, bool capture);

/**
 * Register rdma to abox
 * @param[in]	dev_abox	pointer to abox device
 * @param[in]	dev		pointer to abox rdma device
 * @param[in]	id		number
 * @param[in]	name		name of the dai
 * @return	error code if any
 */
extern int abox_cmpnt_register_rdma(struct device *dev_abox,
		struct device *dev, unsigned int id, const char *name);

/**
 * Register wdma to abox
 * @param[in]	dev_abox	pointer to abox device
 * @param[in]	dev		pointer to abox wdma device
 * @param[in]	id		number
 * @param[in]	name		name of the dai
 * @return	error code if any
 */
extern int abox_cmpnt_register_wdma(struct device *dev_abox,
		struct device *dev, unsigned int id, const char *name);

/**
 * restore snd_soc_component object to firmware
 * @param[in]	adev	pointer to abox device
 * @return		0 or error code
 */
extern int abox_cmpnt_restore(struct device *adev);

/**
 * register snd_soc_component object for current SoC
 * @param[in]	adev	pointer to abox device
 * @return		0 or error code
 */
extern int abox_cmpnt_register(struct device *adev);

#endif /* __SND_SOC_ABOX_CMPNT_H */
